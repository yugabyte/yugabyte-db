// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#include "yb/docdb/conflict_resolution.h"

#include <atomic>
#include <map>

#include <boost/container/small_vector.hpp>
#include <boost/logic/tribool.hpp>

#include "yb/ash/wait_state.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/common/transaction_error.h"
#include "yb/common/transaction_priority.h"

#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_filter_policy.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/intent_format.h"
#include "yb/docdb/iter_util.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/intent.h"

#include "yb/gutil/stl_util.h"

#include "yb/rocksdb/options.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"

#include "yb/util/file_util.h"
#include "yb/util/lazy_invoke.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/stopwatch.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"
#include "yb/util/memory/memory.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_RUNTIME_bool(docdb_ht_filter_conflict_with_committed, true,
    "Use hybrid time SST filter when checking for conflicts with committed transactions.");

DEFINE_test_flag(string, file_to_dump_keys_checked_for_conflict_in_intents_db, "",
    "Dump the keys used to check for conflicts in intents db into the file if specified.");

DEFINE_test_flag(string, file_to_dump_keys_checked_for_conflict_in_regular_db, "",
    "Dump the keys used to check for conflicts in regular db into the file if specified.");

namespace yb::docdb {

using dockv::IntentTypeSet;
using dockv::KeyBytes;
using dockv::KeyEntryTypeAsChar;
using dockv::PartialRangeKeyIntents;
using dockv::SubDocKey;

namespace {

// For each conflicting transaction, SubtxnHasNonLockConflict stores a mapping from
// subtransaction_id -> bool which tracks whether or not a subtransaction has a conflict which is
// from modification (i.e., not just from explicit row-level locks such as "FOR UPDATE",
// "FOR SHARE", etc). The map helps in 2 ways -
//   1. After reading all conflicts with a txn, we need the list of subtransactions with
//      conflicting intents so that we can ignore those which have been aborted via savepoint
//      rollbacks.
//   2. If a conflicting transaction has committed and all its live subtransactions wrote only
//      non-modification intents, we don't have to consider them for conflicts.

using TransactionConflictInfoMap = std::unordered_map<TransactionId,
                                                      TransactionConflictInfoPtr,
                                                      TransactionIdHash>;

Status MakeConflictStatus(const TransactionId& our_id, const TransactionId& other_id,
                          const std::string& reason,
                          const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics) {
  tablet_metrics->Increment(tablet::TabletCounters::kTransactionConflicts);
  return (STATUS(TryAgain, Format("$0 conflicts with $1: $2", our_id, reason, other_id),
                 Slice(), TransactionError(TransactionErrorCode::kConflict)));
}

class ConflictResolver;

class ConflictResolverContext {
 public:
  // Read all conflicts for operation/transaction.
  virtual Status ReadConflicts(ConflictResolver* resolver) = 0;

  // Check priority of this one against existing transactions.
  virtual Status CheckPriority(
      ConflictResolver* resolver,
      boost::iterator_range<TransactionConflictData*> transactions) = 0;

  // Check subtransaction data of pending transaction to determine if conflict can be avoided.
  bool CheckConflictWithPending(const TransactionConflictData& transaction_data) {
    // We remove aborted subtransactions when processing the SubtxnSet stored locally or
    // returned by the status tablet. If this is now empty, then all potentially conflicting
    // intents have been aborted and there is no longer a conflict with this transaction.
    return transaction_data.conflict_info->subtransactions.empty();
  }

  // Check for conflict against committed transaction.
  // Returns true if transaction could be removed from list of conflicts.
  virtual Result<bool> CheckConflictWithCommitted(
      const TransactionConflictData& transaction_data, HybridTime commit_time) = 0;

  virtual HybridTime GetResolutionHt() = 0;

  virtual void MakeResolutionAtLeast(const HybridTime& resolution_ht) = 0;

  virtual int64_t GetTxnStartUs() const = 0;

  virtual const std::shared_ptr<tablet::TabletMetricsHolder>& GetTabletMetrics() = 0;

  virtual bool IgnoreConflictsWith(const TransactionId& other) = 0;

  virtual TransactionId transaction_id() const = 0;

  virtual SubTransactionId subtransaction_id() const = 0;

  virtual std::string ToString() const = 0;

  virtual Result<TabletId> GetStatusTablet(ConflictResolver* resolver) const = 0;

  virtual Status InitTxnMetadata(ConflictResolver* resolver) = 0;

  virtual ConflictManagementPolicy GetConflictManagementPolicy() const = 0;

  virtual bool IsSingleShardTransaction() const = 0;

  virtual std::optional<yb::PgSessionRequestVersion> PgSessionRequestVersion() const {
    return std::nullopt;
  }

  virtual TransactionId wait_as_txn_id() const = 0;

  virtual TabletId wait_as_txn_status_tablet() const = 0;

  virtual bool ShouldWaitAsDifferentTxn() const = 0;

  std::string LogPrefix() const {
    return ToString() + ": ";
  }

  // Returns the intents requested by the WriteQuery being processing.
  virtual Result<IntentTypesContainer> GetRequestedIntents(
      ConflictResolver* resolver, KeyBytes* buffer) = 0;

  virtual ~ConflictResolverContext() = default;
};

class ConflictResolver : public std::enable_shared_from_this<ConflictResolver> {
 public:
  ConflictResolver(const DocDB& doc_db,
                   TransactionStatusManager* status_manager,
                   PartialRangeKeyIntents partial_range_key_intents,
                   RequestScope& request_scope,
                   std::unique_ptr<ConflictResolverContext> context,
                   ResolutionCallback callback)
      : doc_db_(doc_db), status_manager_(*status_manager),
        partial_range_key_intents_(partial_range_key_intents),
        request_scope_(request_scope),
        context_(std::move(context)),
        wait_state_(ash::WaitStateInfo::CurrentWaitState()),
        callback_(std::move(callback)) {}

  virtual ~ConflictResolver() = default;

  PartialRangeKeyIntents partial_range_key_intents() {
    return partial_range_key_intents_;
  }

  TransactionStatusManager& status_manager() {
    return status_manager_;
  }

  const DocDB& doc_db() {
    return doc_db_;
  }

  Result<TransactionMetadata> PrepareMetadata(const LWTransactionMetadataPB& pb) {
    return status_manager_.PrepareMetadata(pb);
  }

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) {
    return status_manager_.FillPriorities(inout);
  }

  void Resolve() {
    SET_WAIT_STATUS(ConflictResolution_ResolveConficts);
    auto status = SetRequestScope();
    if (status.ok()) {
      auto start_time = CoarseMonoClock::Now();
      status = context_->ReadConflicts(this);
      DEBUG_ONLY_TEST_SYNC_POINT("ConflictResolver::Resolve");
      status_manager_.RecordConflictResolutionScanLatency(
          MonoDelta(CoarseMonoClock::Now() - start_time));
    }
    if (!status.ok()) {
      InvokeCallback(status);
      return;
    }

    ResolveConflicts();
  }

  std::shared_ptr<ConflictDataManager> ConsumeTransactionDataAndReset() {
    auto conflict_data = std::move(conflict_data_);
    DCHECK(!conflict_data_);

    request_scope_ = RequestScope();
    intent_iter_.Reset();
    DCHECK(intent_key_upperbound_.empty());
    conflicts_.clear();
    DCHECK_EQ(pending_requests_.load(std::memory_order_acquire), 0);

    return conflict_data;
  }

  // Reads conflicts for specified intent from DB.
  Status ReadIntentConflicts(IntentTypeSet type, KeyBytes* intent_key_prefix) {
    if (!CreateIntentIteratorIfNecessary()) {
      return Status::OK();
    }

    const auto conflicting_intent_types = kIntentTypeSetConflicts[type.ToUIntPtr()];

    KeyBytes upperbound_key(*intent_key_prefix);
    upperbound_key.AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
    intent_key_upperbound_ = upperbound_key.AsSlice();

    size_t original_size = intent_key_prefix->size();
    intent_key_prefix->AppendKeyEntryType(dockv::KeyEntryType::kIntentTypeSet);
    // Have only weak intents, so could skip other weak intents.
    if (!HasStrong(type)) {
      char value = 1 << dockv::kStrongIntentFlag;
      intent_key_prefix->AppendRawBytes(&value, 1);
    }
    auto se = ScopeExit([this, intent_key_prefix, original_size] {
      intent_key_prefix->Truncate(original_size);
      intent_key_upperbound_.clear();
    });
    Slice prefix_slice(intent_key_prefix->AsSlice().data(), original_size);
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Check conflicts in intents DB; Seek: "
                                 << intent_key_prefix->AsSlice().ToDebugHexString() << " for type "
                                 << ToString(type);
    intent_iter_.Seek(intent_key_prefix->AsSlice());
    int64_t num_keys_scanned = 0;
    while (intent_iter_.Valid()) {
      auto existing_key = intent_iter_.key();
      auto existing_value = intent_iter_.value();
      if (!existing_key.starts_with(prefix_slice)) {
        break;
      }
      // Support for obsolete intent type.
      // When looking for intent with specific prefix it should start with this prefix, followed
      // by ValueType::kIntentTypeSet.
      // Previously we were using intent type, so should support its value type also, now it is
      // kObsoleteIntentType.
      // Actual handling of obsolete intent type is done in ParseIntentKey.
      if (existing_key.size() <= prefix_slice.size() ||
          !dockv::IntentValueType(existing_key[prefix_slice.size()])) {
        break;
      }

      auto existing_intent = VERIFY_RESULT(dockv::ParseIntentKey(
          intent_iter_.key(), existing_value));

      VLOG_WITH_PREFIX_AND_FUNC(4) << "Found: " << SubDocKey::DebugSliceToString(existing_key)
                                   << ", with value: " << existing_value.ToDebugString()
                                   << " has intent types " << ToString(existing_intent.types);
      const auto intent_mask = kIntentTypeSetMask[existing_intent.types.ToUIntPtr()];
      if ((conflicting_intent_types & intent_mask) != 0) {
        auto decoded_value = VERIFY_RESULT(dockv::DecodeIntentValue(
            existing_value, nullptr /* verify_transaction_id_slice */,
            HasStrong(existing_intent.types)));
        auto transaction_id = decoded_value.transaction_id;
        bool lock_only = decoded_value.body.starts_with(dockv::ValueEntryTypeAsChar::kRowLock);
        VLOG_WITH_PREFIX_AND_FUNC(4)
            << "Found conflict with exiting transaction: " << transaction_id
            << ", lock_only: " << lock_only
            << ", body: " << decoded_value.body.ToDebugHexString();

        if (!context_->IgnoreConflictsWith(transaction_id)) {
          auto it = conflicts_.try_emplace(
              transaction_id,
              MakeLazyFactory([]() { return std::make_shared<TransactionConflictInfo>(); }));
          auto& txn_conflict_info = DCHECK_NOTNULL(it.first->second);
          auto& subtxn_conflict_info = txn_conflict_info->subtransactions[
              decoded_value.subtransaction_id];
          subtxn_conflict_info.has_non_lock_conflict |= !lock_only;

          if (RecordLockInfo()) {
            auto doc_path = RefCntPrefix(existing_intent.doc_path);
            RETURN_NOT_OK(dockv::RemoveGroupEndSuffix(&doc_path));
            auto lock_it = subtxn_conflict_info.locks.emplace_back(
                LockInfo {std::move(doc_path), existing_intent.types});
          }
        }
      }
      ++num_keys_scanned;
      intent_iter_.Next();
    }
    status_manager_.RecordConflictResolutionKeysScanned(num_keys_scanned);
    return intent_iter_.status();
  }

  bool CreateIntentIteratorIfNecessary() {
    if (!intent_iter_.Initialized()) {
      // The intent interator should not be created with key bounds set to doc_db_.key_bounds. Else
      // it could miss detecting conflicts against existing intents for the empty doc key, and could
      // lead to violation of isolation guarantees.
      //
      // For instance, consider an in progress serializable transaction that executed a
      // 'select * from <table>;'. It would have the following intent entry -
      //
      // SubDocKey(DocKey([], []), []) [kStrongRead] <ht> -> <transaction>
      //
      // All new transactions trying to perform an update/insert look to acquire a weak lock on
      // the empty doc key - 'DocKey([], []), [])'. The created intent iterator should see existing
      // intent records against the empty doc key to perform conflict resolution correctly. Hence,
      // the iterator should be created with KeyBounds::kNoBounds instead.
      // The iterator uses forward scan only, that's why restart block keys caching is not required.
      intent_iter_ = CreateIntentsIteratorWithHybridTimeFilter(
          doc_db_.intents, &status_manager(), &KeyBounds::kNoBounds,
          &intent_key_upperbound_, rocksdb::CacheRestartBlockKeys::kFalse);
    }
    return intent_iter_.Initialized();
  }

  Result<IntentTypesContainer> GetLockStatusInfo() {
    const size_t kKeyBufferInitialSize = 512;
    KeyBytes buffer;
    buffer.Reserve(kKeyBufferInitialSize);
    return context_->GetRequestedIntents(this, &buffer);
  }

 protected:
  Status SetRequestScope() {
    request_scope_ = VERIFY_RESULT(RequestScope::Create(&status_manager()));
    return Status::OK();
  }

  void InvokeCallback(const Result<HybridTime>& result) {
    // ConflictResolution_ResolveConficts lasts until InvokeCallback.
    ADOPT_WAIT_STATE(wait_state_);
    SET_WAIT_STATUS(OnCpu_Active);
    YB_TRANSACTION_DUMP(
        Conflicts, context_->transaction_id(),
        result.ok() ? *result : HybridTime::kInvalid,
        conflict_data_ ? conflict_data_->DumpConflicts() : Slice());
    intent_iter_.Reset();
    callback_(result);
  }

  MUST_USE_RESULT bool CheckResolutionDone(const Result<bool>& result) {
    if (!result.ok()) {
      TRACE("Abort: $0", result.status().ToString());
      VLOG_WITH_PREFIX(4) << "Abort: " << result.status();
      InvokeCallback(result.status());
      return true;
    }

    if (result.get()) {
      TRACE("No conflicts.");
      VLOG_WITH_PREFIX(4) << "No conflicts: " << context_->GetResolutionHt();
      InvokeCallback(context_->GetResolutionHt());
      return true;
    }

    return false;
  }

  void ResolveConflicts() {
    VLOG_WITH_PREFIX(3) << "Conflicts: " << yb::ToString(conflicts_);
    if (conflicts_.empty()) {
      VTRACE(1, LogPrefix());
      TRACE("No conflicts.");
      InvokeCallback(context_->GetResolutionHt());
      return;
    }

    TRACE("Has conflicts.");
    conflict_data_ = std::make_shared<ConflictDataManager>(conflicts_.size());
    for (const auto& [id, conflict_info] : conflicts_) {
      conflict_data_->AddTransaction(id, conflict_info);
    }

    DoResolveConflicts();
  }

  void DoResolveConflicts() {
    if (CheckResolutionDone(CheckLocalCommits())) {
      return;
    }

    FetchTransactionStatuses();
  }

  void FetchTransactionStatusesDone() {
    if (CheckResolutionDone(ContinueResolve())) {
      return;
    }
  }

  Result<bool> ContinueResolve() {
    if (VERIFY_RESULT(Cleanup())) {
      return true;
    }
    RETURN_NOT_OK(OnConflictingTransactionsFound());
    DEBUG_ONLY_TEST_SYNC_POINT("ConflictResolver::OnConflictingTransactionsFound");
    return false;
  }

  virtual Status OnConflictingTransactionsFound() = 0;

  virtual bool RecordLockInfo() const = 0;

  // Returns true when there are no conflicts left.
  Result<bool> CheckLocalCommits() {
    return conflict_data_->FilterInactiveTransactions([this](auto* transaction) -> Result<bool> {
      return this->CheckLocalRunningTransaction(transaction);
    });
  }

  // Check whether specified transaction was locally committed, and store this state if so.
  // Returns true if conflict with specified transaction is resolved.
  Result<bool> CheckLocalRunningTransaction(TransactionConflictData* transaction) {
    auto local_txn_data = status_manager().LocalTxnData(transaction->id);
    if (!local_txn_data) {
      return false;
    }
    transaction->conflict_info->RemoveAbortedSubtransactions(local_txn_data->aborted_subtxn_set);
    auto commit_time = local_txn_data->commit_ht;
    if (commit_time.is_valid()) {
      transaction->commit_time = commit_time;
      transaction->status = TransactionStatus::COMMITTED;
      auto res = VERIFY_RESULT(context_->CheckConflictWithCommitted(*transaction, commit_time));
      if (!res) {
        VLOG_WITH_PREFIX(4) << "Locally committed: " << transaction->id;
      }
      return res;
    }
    return context_->CheckConflictWithPending(*transaction);
  }

  // Removes all transactions that would not conflict with us anymore.
  // Returns failure if we conflict with transaction that cannot be aborted.
  Result<bool> Cleanup() {
    return conflict_data_->FilterInactiveTransactions([this](auto* transaction) -> Result<bool> {
      return this->CheckCleanup(transaction);
    });
  }

  Result<bool> CheckCleanup(TransactionConflictData* transaction) {
    RETURN_NOT_OK(transaction->failure);
    auto status = transaction->status;
    if (status == TransactionStatus::COMMITTED) {
      if (VERIFY_RESULT(context_->CheckConflictWithCommitted(
              *transaction, transaction->commit_time))) {
        VLOG_WITH_PREFIX(4)
            << "Committed: " << transaction->id << ", commit time: " << transaction->commit_time;
        return true;
      }
    } else if (status == TransactionStatus::ABORTED) {
      auto commit_time = status_manager().LocalCommitTime(transaction->id);
      if (commit_time) {
        if (VERIFY_RESULT(context_->CheckConflictWithCommitted(*transaction, commit_time))) {
          VLOG_WITH_PREFIX(4)
              << "Locally committed: " << transaction->id << "< commit time: " << commit_time;
          return true;
        }
      } else {
        VLOG_WITH_PREFIX(4) << "Aborted: " << transaction->id;
        return true;
      }
    } else if (status == TransactionStatus::PENDING) {
      if (context_->CheckConflictWithPending(*transaction)) {
        VLOG_WITH_PREFIX(4)
            << "Local aborted_subtxn_set indicates all discovered intents are aborted for "
            << transaction->id << ".";
        return true;
      }
    } else if (status != TransactionStatus::APPLYING) {
      return STATUS_FORMAT(
          IllegalState, "Unexpected transaction state: $0", TransactionStatus_Name(status));
    }
    return false;
  }

  void FetchTransactionStatuses() {
    ASH_ENABLE_CONCURRENT_UPDATES();
    static const std::string kRequestReason = "conflict resolution"s;
    auto self = shared_from_this();
    pending_requests_.store(conflict_data_->NumActiveTransactions());
    TracePtr trace(Trace::CurrentTrace());
    ash::WaitStateSnapshot wait_state_snapshot;
    SET_WAIT_STATUS(TransactionStatusCache_DoGetCommitData);
    for (auto& i : conflict_data_->RemainingTransactions()) {
      auto& transaction = i;
      TRACE("FetchingTransactionStatus for $0", yb::ToString(transaction.id));
      StatusRequest request = {
        &transaction.id,
        context_->GetResolutionHt(),
        context_->GetResolutionHt(),
        request_scope_.request_id(),
        &kRequestReason,
        TransactionLoadFlags{TransactionLoadFlag::kCleanup},
        [self, &transaction, trace, wait_state_snapshot](
            Result<TransactionStatusResult> result) {
          ADOPT_WAIT_STATE(wait_state_snapshot.wait_state);
          ADOPT_TRACE(trace.get());
          if (result.ok()) {
            transaction.ProcessStatus(*result);
          } else if (result.status().IsTryAgain()) {
            // It is safe to suppose that transaction in PENDING state in case of try again error.
            transaction.status = TransactionStatus::PENDING;
          } else if (result.status().IsNotFound() || result.status().IsExpired()) {
            transaction.status = TransactionStatus::ABORTED;
          } else {
            transaction.failure = result.status();
          }
          DCHECK(!transaction.status_tablet.empty() ||
                 transaction.status != TransactionStatus::PENDING);
          if (self->pending_requests_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            SET_WAIT_STATUS_FROM_SNAPSHOT(wait_state_snapshot);
            self->FetchTransactionStatusesDone();
          }
        },
        &transaction.status_tablet,
      };
      status_manager().RequestStatusAt(request);
    }
  }

  std::string LogPrefix() const {
    return context_->LogPrefix();
  }

  DocDB doc_db_;
  TransactionStatusManager& status_manager_;
  PartialRangeKeyIntents partial_range_key_intents_;
  RequestScope& request_scope_;
  std::unique_ptr<ConflictResolverContext> context_;
  const ash::WaitStateInfoPtr wait_state_;
  ResolutionCallback callback_;

  BoundedRocksDbIterator intent_iter_;
  Slice intent_key_upperbound_;
  TransactionConflictInfoMap conflicts_;

  std::shared_ptr<ConflictDataManager> conflict_data_;

  std::atomic<size_t> pending_requests_{0};
};

class FailOnConflictResolver : public ConflictResolver {
 public:
  FailOnConflictResolver(
      const DocDB& doc_db,
      TransactionStatusManager* status_manager,
      PartialRangeKeyIntents partial_range_key_intents,
      RequestScope& request_scope,
      std::unique_ptr<ConflictResolverContext> context,
      ResolutionCallback callback)
      : ConflictResolver(
          doc_db, status_manager, partial_range_key_intents, request_scope, std::move(context),
          std::move(callback))
    {}

  Status OnConflictingTransactionsFound() override {
    DCHECK_GT(conflict_data_->NumActiveTransactions(), 0);
    if (context_->GetConflictManagementPolicy() == SKIP_ON_CONFLICT) {
      return STATUS(InternalError, "Skip locking since entity is already locked",
                    TransactionError(TransactionErrorCode::kSkipLocking));
    }

    RETURN_NOT_OK(context_->CheckPriority(this, conflict_data_->RemainingTransactions()));

    AbortTransactions();
    return Status::OK();
  }

  bool RecordLockInfo() const override {
    return false;
  }

 private:
  void AbortTransactions() {
    auto self = shared_from(this);
    pending_requests_.store(conflict_data_->NumActiveTransactions());
    for (auto& i : conflict_data_->RemainingTransactions()) {
      auto& transaction = i;
      TRACE("Aborting $0", yb::ToString(transaction.id));
      status_manager().Abort(
          transaction.id,
          [self, &transaction](Result<TransactionStatusResult> result) {
        VLOG(4) << self->LogPrefix() << "Abort received: " << AsString(result);
        if (result.ok()) {
          transaction.ProcessStatus(*result);
        } else if (result.status().IsRemoteError() || result.status().IsAborted() ||
                   result.status().IsShutdownInProgress()) {
          // Non retryable errors. Aborted could be caused by shutdown.
          transaction.failure = result.status();
        } else {
          LOG(INFO) << self->LogPrefix() << "Abort failed, would retry: " << result.status();
        }
        if (self->pending_requests_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          self->AbortTransactionsDone();
        }
      });
    }
  }

  void AbortTransactionsDone() {
    if (CheckResolutionDone(Cleanup())) {
      return;
    }

    DoResolveConflicts();
  }
};

class WaitOnConflictResolver : public ConflictResolver {
 public:
  WaitOnConflictResolver(
      const DocDB& doc_db,
      TransactionStatusManager* status_manager,
      PartialRangeKeyIntents partial_range_key_intents,
      RequestScope& request_scope,
      std::unique_ptr<ConflictResolverContext> context,
      ResolutionCallback callback,
      WaitQueue* wait_queue,
      LockBatch* lock_batch,
      uint64_t request_start_us,
      int64_t request_id,
      bool is_advisory_lock_request,
      CoarseTimePoint deadline)
        : ConflictResolver(
            doc_db, status_manager, partial_range_key_intents, request_scope, std::move(context),
            std::move(callback)), wait_queue_(wait_queue), lock_batch_(lock_batch),
            serial_no_(wait_queue_->GetSerialNo()),
        trace_(Trace::CurrentTrace()), request_start_us_(request_start_us),
        request_id_(request_id), is_advisory_lock_request_(is_advisory_lock_request),
        deadline_(deadline) {}

  ~WaitOnConflictResolver() {
    VLOG(3) << "Wait-on-Conflict resolution complete after " << wait_for_iters_ << " iters.";

    if (wait_start_time_.Initialized()) {
      const MonoDelta elapsed_time = MonoTime::Now().GetDeltaSince(wait_start_time_);
      context_->GetTabletMetrics()->Increment(
          tablet::TabletEventStats::kTotalWaitQueueTime,
          make_unsigned(elapsed_time.ToMicroseconds()));
    }
  }

  void Run() {
    if (context_->IsSingleShardTransaction()) {
      return ConflictResolver::Resolve();
    }
    // Populate the transaction metadata here since the request could enter the wait-queue in the
    // 'TryPreWait' step itself. In such a case, we might need the metadata to populate the awaiting
    // locks info of the waiter for incoming 'pg_locks' queries.
    auto init_status = context_->InitTxnMetadata(this);
    if (!init_status.ok()) {
      InvokeCallback(init_status);
      return;
    }

    auto status_tablet_res = context_->GetStatusTablet(this);
    if (status_tablet_res.ok()) {
      status_tablet_id_ = std::move(*status_tablet_res);
      TryPreWait();
    } else {
      InvokeCallback(status_tablet_res.status());
    }
  }

  bool RecordLockInfo() const override {
    return true;
  }

  void MaybeSetWaitStartTime() {
    if (!wait_start_time_.Initialized()) {
      wait_start_time_ = MonoTime::Now();
    }
    DEBUG_ONLY_TEST_SYNC_POINT("ConflictResolver::MaybeSetWaitStartTime");
  }

  void TryPreWait() {
    const auto& waiter_txn = context_->ShouldWaitAsDifferentTxn()
        ? context_->wait_as_txn_id() : context_->transaction_id();
    const auto& waiter_status_tablet = context_->ShouldWaitAsDifferentTxn()
        ? context_->wait_as_txn_status_tablet() : status_tablet_id_;
    auto did_wait_or_status = wait_queue_->MaybeWaitOnLocks(
        waiter_txn, context_->subtransaction_id(), lock_batch_, waiter_status_tablet,
        serial_no_, context_->GetTxnStartUs(), request_start_us_, request_id_,
        context_->PgSessionRequestVersion(), is_advisory_lock_request_, deadline_,
        std::bind(&WaitOnConflictResolver::GetLockStatusInfo, shared_from(this)),
        std::bind(&WaitOnConflictResolver::WaitingDone, shared_from(this), _1, _2));
    if (!did_wait_or_status.ok()) {
      InvokeCallback(did_wait_or_status.status());
    } else if (!*did_wait_or_status) {
      ConflictResolver::Resolve();
    } else {
      MaybeSetWaitStartTime();
      VLOG(3) << "Wait-on-Conflict resolution entered wait queue in PreWaitOn stage";
    }
  }

  Status OnConflictingTransactionsFound() override {
    DCHECK_GT(conflict_data_->NumActiveTransactions(), 0);
    VTRACE(3, "Waiting on $0 transactions after $1 tries.",
           conflict_data_->NumActiveTransactions(), wait_for_iters_);

    const auto& waiter_txn = context_->ShouldWaitAsDifferentTxn()
        ? context_->wait_as_txn_id() : context_->transaction_id();
    const auto& waiter_status_tablet = context_->ShouldWaitAsDifferentTxn()
        ? context_->wait_as_txn_status_tablet() : status_tablet_id_;
    RETURN_NOT_OK(wait_queue_->WaitOn(
        waiter_txn, context_->subtransaction_id(), lock_batch_,
        ConsumeTransactionDataAndReset(), waiter_status_tablet, serial_no_,
        context_->GetTxnStartUs(), request_start_us_, request_id_,
        context_->PgSessionRequestVersion(), is_advisory_lock_request_, deadline_,
        std::bind(&WaitOnConflictResolver::GetLockStatusInfo, shared_from(this)),
        std::bind(&WaitOnConflictResolver::WaitingDone, shared_from(this), _1, _2)));
    MaybeSetWaitStartTime();
    return Status::OK();
  }

  // Note: we must pass in shared_this to keep the WaitOnConflictResolver alive until the wait queue
  // invokes this call.
  void WaitingDone(const Status& status, HybridTime resume_ht) {
    ADOPT_TRACE(trace_.get());
    TRACE_FUNC();
    VLOG_WITH_FUNC(4) << context_->transaction_id() << " status: " << status;
    wait_for_iters_++;

    if (!status.ok()) {
      InvokeCallback(status);
      return;
    }

    if (resume_ht.is_special()) {
      auto error_msg = Format("Unexpected resume_ht in conflict resolution: $0", resume_ht);
      LOG_WITH_PREFIX(DFATAL) << error_msg;
      InvokeCallback(STATUS(InternalError, error_msg));
      return;
    }
    context_->MakeResolutionAtLeast(resume_ht);

    // If status from wait_queue is OK, then all blockers read earlier are now resolved. Retry
    // conflict resolution with all state reset.
    // TODO(wait-queues): In case wait queue finds that a blocker was committed, and if that blocker
    // has still-live modification conflicts with this operation (i.e. not from rolled back subtxn),
    // we can avoid re-running conflict resolution here and just abort.
    Resolve();
  }

 private:
  WaitQueue* const wait_queue_;
  LockBatch* const lock_batch_;
  uint64_t serial_no_;
  uint32_t wait_for_iters_ = 0;
  TabletId status_tablet_id_;
  MonoTime wait_start_time_ = MonoTime::kUninitialized;
  TracePtr trace_;
  // Stores the start time of the underlying rpc request that created this resolver.
  uint64_t request_start_us_ = 0;
  const int64_t request_id_;
  const bool is_advisory_lock_request_;
  CoarseTimePoint deadline_;
};

class IntentProcessor {
 public:
  IntentProcessor(IntentTypesContainer& container, IsolationLevel isolation_level,
                  dockv::SkipPrefixLocks skip_prefix_locks)
      : container_(container),
        isolation_level_(isolation_level),
        skip_prefix_locks_(skip_prefix_locks)
  {}

  Status Process(
      dockv::AncestorDocKey ancestor_doc_key,
      dockv::FullDocKey full_doc_key,
      const KeyBytes* const intent_key,
      IntentTypeSet intent_types,
      dockv::IsTopLevelKey is_top_level_key,
      boost::tribool pk_is_known) {
    const bool take_weak_lock = VERIFY_RESULT(ShouldTakeWeakLockForPrefix(
        ancestor_doc_key, is_top_level_key,
        skip_prefix_locks_, isolation_level_, pk_is_known, intent_key));
    const auto& intent_type_set = take_weak_lock ? MakeWeak(intent_types) : intent_types;
    auto [i, inserted] = container_.try_emplace(
            intent_key->data(), IntentData{intent_type_set, full_doc_key});
    if (inserted) {
      return Status::OK();
    }

    i->second.types |= intent_type_set;

    // In a batch of keys, the computed full_doc_key value might vary based on the key that produced
    // a particular intent. E.g. suppose we have a primary key (h, r) and s is a subkey. If we are
    // trying to write strong intents on (h) and (h, r, s) in a batch, we end up with the following
    // intent types:
    //
    // (h) -> strong, full_doc_key: true (always true for strong intents)
    // (h, r) -> weak, full_doc_key: true (we did not omit any final doc key components)
    // (h, r, s) -> strong, full_doc_key: true
    //
    // Note that full_doc_key is always true for strong intents because we process one key at a time
    // and when taking that key by itself, (h) looks like the full doc key (nothing follows it).
    // In the above example, the intent (h) is generated both as a strong intent and as a weak
    // intent based on keys (h, r) and (h, r, s), and we OR the value of full_doc_key and end up
    // with true.
    //
    // If we are trying to write strong intents on (h, r) and (h, r, s), we get:
    //
    // (h) -> weak, full_doc_key: false (because we know it is just part of the doc key)
    // (h, r) -> strong, full_doc_key: true
    // (h, r, s) -> strong, full_doc_key: true
    //
    // So we effectively end up with three types of intents:
    // - Weak intents with full_doc_key=false
    // - Weak intents with full_doc_key=true
    // - Strong intents with full_doc_key=true.
    i->second.full_doc_key = i->second.full_doc_key || full_doc_key;
    return Status::OK();
  }

 private:
  IntentTypesContainer& container_;
  const IsolationLevel isolation_level_;
  const dockv::SkipPrefixLocks skip_prefix_locks_;
};

using DocPaths = boost::container::small_vector<RefCntPrefix, 8>;

class DocPathProcessor {
 public:
  DocPathProcessor(IntentProcessor* processor, KeyBytes* buffer,
                   PartialRangeKeyIntents partial, dockv::SkipPrefixLocks skip_prefix_locks)
      : processor_(*processor), buffer_(*buffer),
        partial_(partial), skip_prefix_locks_(skip_prefix_locks) {}

  Status operator()(DocPaths* paths, dockv::IntentTypeSet intent_types,
                    boost::tribool pk_is_known) {
    for (const auto& path : *paths) {
      VLOG(4) << "Doc path: " << SubDocKey::DebugSliceToString(path.as_slice());
      RETURN_NOT_OK(EnumerateIntents(
          path.as_slice(),
          /*intent_value=*/ Slice(),
          [&processor = processor_, &intent_types, &pk_is_known](
              auto ancestor_doc_key, auto full_doc_key, auto, auto intent_key, auto, auto,
              auto is_top_level_key, auto) {
            return processor.Process(ancestor_doc_key, full_doc_key, intent_key,
                                     intent_types, is_top_level_key, pk_is_known);
          },
          &buffer_,
          partial_,
          /*last_key=*/ dockv::LastKey::kFalse,
          skip_prefix_locks_ ? dockv::SkipPrefix::kTrue : dockv::SkipPrefix::kFalse));
    }
    paths->clear();
    return Status::OK();
  }

 private:
  IntentProcessor& processor_;
  KeyBytes& buffer_;
  PartialRangeKeyIntents partial_;
  dockv::SkipPrefixLocks skip_prefix_locks_;

  DISALLOW_COPY_AND_ASSIGN(DocPathProcessor);
};

Result<IntentTypesContainer> GetWriteRequestIntents(
    const DocOperations& doc_ops, KeyBytes* buffer, PartialRangeKeyIntents partial,
    IsolationLevel isolation_level, dockv::SkipPrefixLocks skip_prefix_locks) {
  bool is_lock_batch = !doc_ops.empty() &&
      doc_ops[0]->OpType() == DocOperationType::PGSQL_LOCK_OPERATION;
  static const dockv::IntentTypeSet kStrongReadIntentTypeSet{dockv::IntentType::kStrongRead};
  dockv::IntentTypeSet intent_types;
  if (isolation_level != IsolationLevel::NON_TRANSACTIONAL && !is_lock_batch) {
    intent_types = dockv::GetIntentTypesForWrite(isolation_level);
  }

  IntentTypesContainer container;
  IntentProcessor intent_processor(container, isolation_level, skip_prefix_locks);
  DocPathProcessor processor(&intent_processor, buffer, partial, skip_prefix_locks);
  DocPaths doc_paths;
  for (const auto& doc_op : doc_ops) {
    // pk_is_known may be set for PGSQL_WRITE_OPERATION but never for PGSQL_LOCK_OPERATION.
    boost::tribool pk_is_known =
        doc_op->OpType() == docdb::DocOperation::Type::PGSQL_WRITE_OPERATION ?
        boost::tribool(down_cast<const docdb::PgsqlWriteOperation&>(*doc_op).pk_is_known()) :
        boost::indeterminate;
    IsolationLevel op_isolation;
    RETURN_NOT_OK(doc_op->GetDocPaths(GetDocPathsMode::kIntents, &doc_paths, &op_isolation));
    RETURN_NOT_OK(processor(
        &doc_paths,
        intent_types.None() ? doc_op->GetIntentTypes(op_isolation) : intent_types,
        pk_is_known));

    RETURN_NOT_OK(
        doc_op->GetDocPaths(GetDocPathsMode::kStrongReadIntents, &doc_paths, &op_isolation));
    RETURN_NOT_OK(processor(&doc_paths, kStrongReadIntentTypeSet, pk_is_known));
  }
  return container;
}

class StrongConflictChecker {
 public:
  StrongConflictChecker(const TransactionId& transaction_id,
                        HybridTime read_time,
                        ConflictResolver* resolver,
                        const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
                        KeyBytes* buffer)
      : transaction_id_(transaction_id),
        read_time_(read_time),
        resolver_(*resolver),
        tablet_metrics_(tablet_metrics),
        buffer_(*buffer)
  {}

  Status Check(
      Slice intent_key, bool strong, ConflictManagementPolicy conflict_management_policy) {
    if (PREDICT_FALSE(!FLAGS_TEST_file_to_dump_keys_checked_for_conflict_in_regular_db.empty())) {
      RETURN_NOT_OK(TEST_DumpRegularDbKeyToCheck(intent_key));
    }
    if (!value_iter_->Initialized()) {
      auto hybrid_time_file_filter =
          FLAGS_docdb_ht_filter_conflict_with_committed ? CreateHybridTimeFileFilter(read_time_)
                                                        : nullptr;
      value_iter_ = OptimizedRocksDbIterator<BoundedRocksDbIterator>(CreateRocksDBIterator(
          resolver_.doc_db().regular,
          resolver_.doc_db().key_bounds,
          BloomFilterOptions::Variable(),
          rocksdb::kDefaultQueryId,
          hybrid_time_file_filter,
          /* iterate_upper_bound = */ nullptr,
          rocksdb::CacheRestartBlockKeys::kFalse));
    }
    value_iter_->UpdateFilterKey(intent_key, Slice());
    const auto* entry = &value_iter_->Seek(intent_key);

    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Overwrite; Seek: " << intent_key.ToDebugString() << " ("
        << SubDocKey::DebugSliceToString(intent_key) << "), strong: " << strong
        << ", conflict_management_policy: " << AsString(conflict_management_policy);

    // If we are resolving conflicts for writing a strong intent, look at records in regular RocksDB
    // with the same key as the intent's key (not including hybrid time) and any child keys. This is
    // because a strong intent indicates deletion or replacement of the entire subdocument tree and
    // any element of that tree that has already been committed at a higher hybrid time than the
    // read timestamp would be in conflict.
    //
    // (Note that when writing a strong intent on the entire table, e.g. as part of locking the
    // table, there is currently a performance issue and we'll need a better approach:
    // https://github.com/yugabyte/yugabyte-db/issues/6055).
    //
    // If we are resolving conflicts for writing a weak intent, only look at records in regular
    // RocksDB with the same key as the intent (not including hybrid time). This is because a weak
    // intent indicates that something in the document subtree rooted at that intent's key will
    // change, so it is only directly in conflict with a committed record that deletes or replaces
    // that entire document subtree (similar to a strong intent), so it would have the same exact
    // key as the weak intent (not including hybrid time).
    while (entry->Valid() &&
           (intent_key.starts_with(KeyEntryTypeAsChar::kGroupEnd) ||
            entry->key.starts_with(intent_key))) {
      auto existing_key = entry->key;
      auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&existing_key));
      if (existing_key.empty() ||
          existing_key[existing_key.size() - 1] != KeyEntryTypeAsChar::kHybridTime) {
        return STATUS_FORMAT(
            Corruption, "Hybrid time expected at end of key: $0",
            entry->key.ToDebugString());
      }
      if (!strong && existing_key.size() != intent_key.size() + 1) {
        VLOG_WITH_PREFIX(4)
            << "Check value overwrite, key: " << intent_key.ToDebugString()
            << ", out of bound key: " << existing_key.ToDebugString();
        break;
      }
      VLOG_WITH_PREFIX(4)
          << "Check value overwrite, key: " << SubDocKey::DebugSliceToString(intent_key)
          << ", read time: " << read_time_
          << ", doc ht: " << doc_ht.hybrid_time()
          << ", found key: " << SubDocKey::DebugSliceToString(entry->key)
          << ", after start: " << (doc_ht.hybrid_time() > read_time_)
          << ", value: " << entry->value.ToDebugString();
      if (doc_ht.hybrid_time() > read_time_) {
        if (conflict_management_policy == SKIP_ON_CONFLICT) {
          return STATUS(InternalError, "Skip locking since entity was modified in regular db",
                        TransactionError(TransactionErrorCode::kSkipLocking));
        } else {
          tablet_metrics_->Increment(tablet::TabletCounters::kTransactionConflicts);
          return STATUS_EC_FORMAT(
              TryAgain, TransactionError(TransactionErrorCode::kConflict),
              "$0 conflict with concurrently committed data. Value write after transaction start: "
              "doc ht ($1) > read time ($2), key: $3",
              transaction_id_, doc_ht.hybrid_time(), read_time_,
              SubDocKey::DebugSliceToString(intent_key));
        }
      }
      buffer_.Reset(existing_key);
      // Already have ValueType::kHybridTime at the end
      buffer_.AppendHybridTime(DocHybridTime::kMin);
      entry = &ROCKSDB_SEEK(value_iter_, buffer_.AsSlice());
    }

    return value_iter_->status();
  }

 private:
  std::string LogPrefix() const {
    return Format("$0: ", transaction_id_);
  }

  Status TEST_DumpRegularDbKeyToCheck(const Slice& intent_key) {
    dockv::SubDocKey sub_doc_key;
    CHECK_OK(sub_doc_key.FullyDecodeFrom(intent_key, dockv::HybridTimeRequired::kFalse));
    std::string desc = "regular_check";
    return TEST_DumpStringToFile(desc, sub_doc_key.ToString(),
                                 FLAGS_TEST_file_to_dump_keys_checked_for_conflict_in_regular_db);
  }

  const TransactionId& transaction_id_;
  const HybridTime read_time_;
  ConflictResolver& resolver_;
  std::shared_ptr<tablet::TabletMetricsHolder> tablet_metrics_;
  KeyBytes& buffer_;

  // RocksDb iterator with bloom filter can be reused in case keys has same hash component.
  OptimizedRocksDbIterator<BoundedRocksDbIterator> value_iter_;
};

class ConflictResolverContextBase : public ConflictResolverContext {
 public:
  ConflictResolverContextBase(const DocOperations& doc_ops,
                              HybridTime resolution_ht,
                              int64_t txn_start_us,
                              const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
                              ConflictManagementPolicy conflict_management_policy)
      : doc_ops_(doc_ops),
        resolution_ht_(resolution_ht),
        txn_start_us_(txn_start_us),
        tablet_metrics_(tablet_metrics),
        conflict_management_policy_(conflict_management_policy) {
  }

  const DocOperations& doc_ops() {
    return doc_ops_;
  }

  HybridTime GetResolutionHt() override {
    return resolution_ht_;
  }

  void MakeResolutionAtLeast(const HybridTime& resolution_ht) override {
    resolution_ht_.MakeAtLeast(resolution_ht);
  }

  int64_t GetTxnStartUs() const override {
    return txn_start_us_;
  }

  const std::shared_ptr<tablet::TabletMetricsHolder>& GetTabletMetrics() override {
    return tablet_metrics_;
  }

  ConflictManagementPolicy GetConflictManagementPolicy() const override {
    return conflict_management_policy_;
  }

 protected:
  Status CheckPriorityInternal(
      ConflictResolver* resolver,
      boost::iterator_range<TransactionConflictData*> transactions,
      const TransactionId& our_transaction_id,
      uint64_t our_priority) {

    if (!fetched_metadata_for_transactions_) {
      boost::container::small_vector<std::pair<TransactionId, uint64_t>, 8> ids_and_priorities;
      ids_and_priorities.reserve(transactions.size());
      for (const auto& transaction : transactions) {
        ids_and_priorities.emplace_back(transaction.id, 0);
      }
      RETURN_NOT_OK(resolver->FillPriorities(&ids_and_priorities));
      for (size_t i = 0; i != transactions.size(); ++i) {
        transactions[i].priority = ids_and_priorities[i].second;
      }
    }
    for (const auto& transaction : transactions) {
      auto their_priority = transaction.priority;

      // READ COMMITTED txns require a guarantee that no txn abort it. They can handle facing a
      // kConflict due to another txn's conflicting intent, but can't handle aborts. To ensure
      // these guarantees -
      //   1. all READ COMMITTED txns are given kHighestPriority and
      //   2. a kConflict is raised even if their_priority equals our_priority.
      if (our_priority <= their_priority) {
        return MakeConflictStatus(
            our_transaction_id, transaction.id,
            our_priority == their_priority ?
              Format("same priority transaction (pri: $0)", our_priority) :
              Format("higher priority transaction (our pri: $0, their pri: $1)",
                     our_priority, their_priority),
            GetTabletMetrics());
      }
    }
    fetched_metadata_for_transactions_ = true;

    return Status::OK();
  }

  Status TEST_DumpIntentsCheckKeys(const IntentTypesContainer& locks, const std::string& path) {
    auto transform = [] (const auto& entry) {
      dockv::SubDocKey sub_doc_key;
      CHECK_OK(sub_doc_key.FullyDecodeFrom(
            entry.first.AsSlice(), dockv::HybridTimeRequired::kFalse));
      return std::make_pair(sub_doc_key.ToString(), entry.second.types);
    };
    std::string desc = "intent_check";
    return TEST_DumpCollectionToFile(desc, locks, transform, path);
  }

 private:
  const DocOperations& doc_ops_;

  // Hybrid time of conflict resolution, used to request transaction status from status tablet.
  HybridTime resolution_ht_;

  int64_t txn_start_us_;

  bool fetched_metadata_for_transactions_ = false;

  std::shared_ptr<tablet::TabletMetricsHolder> tablet_metrics_;

  const ConflictManagementPolicy conflict_management_policy_;
};

// Utility class for ResolveTransactionConflicts implementation.
class TransactionConflictResolverContext : public ConflictResolverContextBase {
 public:
  TransactionConflictResolverContext(
      const DocOperations& doc_ops,
      const LWKeyValueWriteBatchPB& write_batch,
      HybridTime resolution_ht,
      HybridTime read_time,
      int64_t txn_start_us,
      const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
      ConflictManagementPolicy conflict_management_policy,
      dockv::SkipPrefixLocks skip_prefix_locks)
      : ConflictResolverContextBase(
            doc_ops, resolution_ht, txn_start_us, tablet_metrics, conflict_management_policy),
        write_batch_(write_batch),
        read_time_(read_time),
        transaction_id_(FullyDecodeTransactionId(write_batch.transaction().transaction_id())),
        skip_prefix_locks_(skip_prefix_locks)
  {}

  virtual ~TransactionConflictResolverContext() {}

  Result<TabletId> GetStatusTablet(ConflictResolver* resolver) const override {
    RETURN_NOT_OK(transaction_id_);
    // If this is the first operation for this transaction at this tablet, then GetStatusTablet
    // will return std::nullopt since the transaction has not been registered with the tablet's
    // transaction participant. However, the write_batch_ transaction metadata only includes the
    // status tablet on the first write to this tablet.
    if (write_batch_.transaction().has_status_tablet()) {
      return write_batch_.transaction().status_tablet().ToBuffer();
    }
    auto tablet_id_opt =
        VERIFY_RESULT(resolver->status_manager().FindStatusTablet(transaction_id()));
    if (!tablet_id_opt) {
      return STATUS_FORMAT(
          InternalError, "Cannot find status tablet for write_batch transaction $0",
          transaction_id());
    }
    return std::move(*tablet_id_opt);
  }

  Status InitTxnMetadata(ConflictResolver* resolver) override {
    metadata_ = VERIFY_RESULT(resolver->PrepareMetadata(write_batch_.transaction()));
    if (write_batch_.has_background_transaction_id()) {
      background_transaction_meta_.emplace();
      background_transaction_meta_->transaction_id =
          VERIFY_RESULT(FullyDecodeTransactionId(write_batch_.background_transaction_id()));
      background_transaction_meta_->status_tablet = write_batch_.background_txn_status_tablet();
      should_wait_as_background_txn_ = PgSessionRequestVersion().value_or(false);
    }
    return Status::OK();
  }

  TransactionId wait_as_txn_id() const override {
    return background_transaction_meta_
        ? background_transaction_meta_->transaction_id
        : TransactionId::Nil();
  }

  TabletId wait_as_txn_status_tablet() const override {
    return background_transaction_meta_
        ? background_transaction_meta_->status_tablet
        : TabletId();
  }

  bool ShouldWaitAsDifferentTxn() const override {
    return should_wait_as_background_txn_;
  }

 private:
  Result<IntentTypesContainer> GetRequestedIntents(
      ConflictResolver* resolver, KeyBytes* buffer) override {
    auto container = VERIFY_RESULT(GetWriteRequestIntents(
        doc_ops(), buffer, resolver->partial_range_key_intents(), metadata_.isolation,
        skip_prefix_locks_));
    const auto& pairs = write_batch_.read_pairs();
    if (pairs.empty()) {
      return container;
    }
    // Form the intents corresponding to the request's read pairs.
    const auto read_intents =
        dockv::GetIntentTypesForRead(metadata_.isolation, GetRowMarkTypeFromPB(write_batch_));
    IntentProcessor intent_processor(container, metadata_.isolation, skip_prefix_locks_);
    RETURN_NOT_OK(EnumerateIntents(
        pairs,
        [&intent_processor, &read_intents](
            auto ancestor_doc_key, auto full_doc_key, auto, auto* intent_key, auto,
            auto is_row_lock, auto is_top_level_key, auto pk_is_known) {
          return intent_processor.Process(
              ancestor_doc_key, full_doc_key, intent_key,
              GetIntentTypes(read_intents, is_row_lock),
              is_top_level_key, pk_is_known);
        },
        resolver->partial_range_key_intents(),
        skip_prefix_locks_));
    return container;
  }

  Status ReadConflicts(ConflictResolver* resolver) override {
    RETURN_NOT_OK(transaction_id_);

    VLOG_WITH_PREFIX(3) << "Resolve conflicts";

    RETURN_NOT_OK(InitTxnMetadata(resolver));

    constexpr size_t kKeyBufferInitialSize = 512;
    KeyBytes buffer;
    buffer.Reserve(kKeyBufferInitialSize);
    auto container = VERIFY_RESULT(GetRequestedIntents(resolver, &buffer));
    if (container.empty()) {
      return Status::OK();
    }

    VLOG_WITH_PREFIX_AND_FUNC(4) << "Check txn's conflicts for following intents: "
                                 << AsString(container);
    if (PREDICT_FALSE(!FLAGS_TEST_file_to_dump_keys_checked_for_conflict_in_intents_db.empty())) {
      RETURN_NOT_OK(TEST_DumpIntentsCheckKeys(
          container, FLAGS_TEST_file_to_dump_keys_checked_for_conflict_in_intents_db));
    }

    StrongConflictChecker checker(
        *transaction_id_, read_time_, resolver, GetTabletMetrics(), &buffer);
    // Iterator on intents DB should be created before iterator on regular DB.
    // This is to prevent the case when we create an iterator on the regular DB where a
    // provisional record has not yet been applied, and then create an iterator the intents
    // DB where the provisional record has already been removed.
    // Even in the case where there are no intents to iterate over, the following loop must be
    // run, so we cannot return early if the following call returns false.
    resolver->CreateIntentIteratorIfNecessary();

    for (const auto& i : container) {
      const Slice intent_key = i.first.AsSlice();
      if (read_time_ != HybridTime::kMax) {
        bool strong = HasStrong(i.second.types);
        // For strong intents or weak intents at a full document key level (i.e. excluding intents
        // that omit some final range components of the document key), check for conflicts with
        // records in regular RocksDB. We need this because the row might have been deleted
        // concurrently by a single-shard transaction or a committed and applied transaction.
        if (strong || i.second.full_doc_key) {
          RETURN_NOT_OK(checker.Check(intent_key, strong, GetConflictManagementPolicy()));
        }
      }
      buffer.Reset(intent_key);
      RETURN_NOT_OK(resolver->ReadIntentConflicts(i.second.types, &buffer));
    }

    return Status::OK();
  }

  Status CheckPriority(ConflictResolver* resolver,
                       boost::iterator_range<TransactionConflictData*> transactions) override {
    return CheckPriorityInternal(resolver, transactions, metadata_.transaction_id,
                                 metadata_.priority);
  }

  Result<bool> CheckConflictWithCommitted(
      const TransactionConflictData& transaction_data, HybridTime commit_time) override {
    RSTATUS_DCHECK(commit_time.is_valid(), Corruption, "Invalid transaction commit time");

    VLOG_WITH_PREFIX(4) << "Committed: " << transaction_data.id << ", commit_time: " << commit_time
                        << ", read_time: " << read_time_
                        << ", transaction_data: " << transaction_data.ToString();

    for (const auto& subtxn_and_data : transaction_data.conflict_info->subtransactions) {
      auto has_non_lock_conflict = subtxn_and_data.second.has_non_lock_conflict;
      // If the intents to be written conflict with only "explicit row lock" intents of a committed
      // transaction, we can proceed now because a committed transaction implies that the locks are
      // released. In other words, only a committed transaction with some conflicting intent that
      // results in a modification to data in regular db, can result in a serialization error.
      //
      // A commit_time of HybridTime::kMax means that the transaction is not actually committed,
      // but is being committed. I.e. status tablet is trying to replicate COMMITTED state.
      // So we should always conflict with such a transaction, because we are not able to read its
      // results.
      //
      // read_time equals HybridTime::kMax in case of serializable isolation or when read time was
      // not yet picked for snapshot isolation. So it should conflict only with transactions that
      // are being committed.
      //
      // In all other cases we have a concrete read time and should conflict with transactions
      // that were committed after this point.
      if (has_non_lock_conflict && (commit_time > read_time_ || commit_time == HybridTime::kMax)) {
        if (GetConflictManagementPolicy() == SKIP_ON_CONFLICT) {
          return STATUS(InternalError, "Skip locking since entity was modified by a recent commit",
                        TransactionError(TransactionErrorCode::kSkipLocking));
        } else {
          return MakeConflictStatus(
            *transaction_id_, transaction_data.id, "committed transaction", GetTabletMetrics());
        }
      }
    }

    return true;
  }

  bool IgnoreConflictsWith(const TransactionId& other) override {
    return other == *transaction_id_ ||
           (background_transaction_meta_ && other == background_transaction_meta_->transaction_id);
  }

  TransactionId transaction_id() const override {
    return *transaction_id_;
  }

  SubTransactionId subtransaction_id() const override {
    if (write_batch_.subtransaction().has_subtransaction_id()) {
      return write_batch_.subtransaction().subtransaction_id();
    }
    return kMinSubTransactionId;
  }

  bool IsSingleShardTransaction() const override { return false; }

  std::optional<yb::PgSessionRequestVersion> PgSessionRequestVersion() const override {
    if (write_batch_.has_pg_session_req_version() && write_batch_.pg_session_req_version()) {
      return write_batch_.pg_session_req_version();
    }
    return std::nullopt;
  }

  std::string ToString() const override { return yb::ToString(transaction_id_); }

  const LWKeyValueWriteBatchPB& write_batch_;

  // Read time of the transaction identified by transaction_id_, could be HybridTime::kMax in case
  // of serializable isolation or when read time not yet picked for snapshot isolation.
  const HybridTime read_time_;

  // Id of transaction when is writing intents, for which we are resolving conflicts.
  Result<TransactionId> transaction_id_;

  // This ensures no conflicts occur between session-level and transaction-level advisory
  // locks within the same session.
  // - For session-level advisory lock requests: the below points to
  //   the in-progress DocDB transaction, if any.
  // - For transaction-level advisory lock requests: the below points to
  //   the session-level transaction, if exists.
  std::optional<TransactionMetadata> background_transaction_meta_;

  // When set, indicates that we need to wait as background transaction. Currently, this is used
  // in the following path alone,
  // - When a session advisory lock request is issued in an explicit transaction block, we wait
  //   as the regular/plain transaction as opposed to entering the wait queue with the session
  //   level transaction. This is necessary for breaking any potential deadlocks spanning session
  //   advisory locks, transaction advisory locks, and row level locks.
  bool should_wait_as_background_txn_ = false;

  TransactionMetadata metadata_;

  Status result_ = Status::OK();

  const dockv::SkipPrefixLocks skip_prefix_locks_;
};

class OperationConflictResolverContext : public ConflictResolverContextBase {
 public:
  OperationConflictResolverContext(
      const DocOperations* doc_ops,
      HybridTime resolution_ht,
      int64_t txn_start_us,
      const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
      ConflictManagementPolicy conflict_management_policy)
      : ConflictResolverContextBase(
            *doc_ops, resolution_ht, txn_start_us, tablet_metrics, conflict_management_policy) {
  }

  Status InitObjectLockingTxnMeta(const LWKeyValueWriteBatchPB& write_batch) {
    if (write_batch.has_object_locking_txn_meta()) {
      object_locking_txn_meta_.emplace();
      object_locking_txn_meta_->transaction_id = VERIFY_RESULT(
          FullyDecodeTransactionId(write_batch.object_locking_txn_meta().transaction_id()));
      object_locking_txn_meta_->status_tablet =
          write_batch.object_locking_txn_meta().status_tablet();
    }
    return Status::OK();
  }

  virtual ~OperationConflictResolverContext() {}

  Result<TabletId> GetStatusTablet(ConflictResolver* resolver) const override {
    return STATUS(
        NotSupported,
        "Status tablets are not used for single tablet transactions.");
  }

  Status InitTxnMetadata(ConflictResolver* resolver) override {
    return STATUS(
        NotSupported, "Transaction metadata isn't used for single shard transactions.");
  }

  Result<IntentTypesContainer> GetRequestedIntents(
      ConflictResolver* resolver, KeyBytes* buffer) override {
    return GetWriteRequestIntents(
        doc_ops(), buffer, resolver->partial_range_key_intents(),
        IsolationLevel::NON_TRANSACTIONAL, dockv::SkipPrefixLocks::kFalse);
  }

  // Reads stored intents that could conflict with our operations.
  Status ReadConflicts(ConflictResolver* resolver) override {
    KeyBytes buffer;
    auto container = VERIFY_RESULT(GetRequestedIntents(resolver, &buffer));
    for (const auto& [key, intent_data] : container) {
      buffer.Reset(key.AsSlice());
      RETURN_NOT_OK(resolver->ReadIntentConflicts(intent_data.types, &buffer));
    }
    if (PREDICT_FALSE(!FLAGS_TEST_file_to_dump_keys_checked_for_conflict_in_intents_db.empty())) {
      return TEST_DumpIntentsCheckKeys(
          container, FLAGS_TEST_file_to_dump_keys_checked_for_conflict_in_intents_db);
    }
    return Status::OK();
  }

  Status CheckPriority(ConflictResolver* resolver,
                       boost::iterator_range<TransactionConflictData*> transactions) override {
    return CheckPriorityInternal(resolver,
                                 transactions,
                                 TransactionId::Nil(),
                                 kHighPriTxnLowerBound - 1 /* our_priority */);
  }

  bool IgnoreConflictsWith(const TransactionId& other) override {
    return false;
  }

  TransactionId transaction_id() const override {
    return TransactionId::Nil();
  }

  SubTransactionId subtransaction_id() const override {
    return kMinSubTransactionId;
  }

  bool IsSingleShardTransaction() const override {
    return true;
  }

  std::string ToString() const override {
    return "Operation Context";
  }

  Result<bool> CheckConflictWithCommitted(
      const TransactionConflictData& transaction_data, HybridTime commit_time) override {
    if (commit_time != HybridTime::kMax) {
      MakeResolutionAtLeast(commit_time);
      return true;
    }
    return false;
  }

  bool ShouldWaitAsDifferentTxn() const override {
    return object_locking_txn_meta_.has_value();
  }

  TransactionId wait_as_txn_id() const override {
    return object_locking_txn_meta_
        ? object_locking_txn_meta_->transaction_id
        : TransactionId::Nil();
  }

  TabletId wait_as_txn_status_tablet() const override {
    return object_locking_txn_meta_
        ? object_locking_txn_meta_->status_tablet
        : TabletId();
  }

 private:
  // When object locking is enabled, a fast path txn could deadlock with a ddl transaction.
  // Hence the fast path txn is made to register the wait-for probes with the deadlock detector.
  std::optional<TransactionMetadata> object_locking_txn_meta_;
};

} // namespace

Status ResolveTransactionConflicts(
    const DocOperations& doc_ops,
    const ConflictManagementPolicy conflict_management_policy,
    const LWKeyValueWriteBatchPB& write_batch,
    RequestScope& request_scope,
    HybridTime resolution_ht,
    HybridTime read_time,
    int64_t txn_start_us,
    uint64_t request_start_us,
    int64_t request_id,
    const DocDB& doc_db,
    PartialRangeKeyIntents partial_range_key_intents,
    TransactionStatusManager* status_manager,
    const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
    LockBatch* lock_batch,
    WaitQueue* wait_queue,
    bool is_advisory_lock_request,
    CoarseTimePoint deadline,
    ResolutionCallback callback,
    dockv::SkipPrefixLocks skip_prefix_locks) {
  DCHECK(resolution_ht.is_valid());
  TRACE_FUNC();

  VLOG_WITH_FUNC(3)
      << "conflict_management_policy=" << conflict_management_policy
      << ", resolution_ht: " << resolution_ht
      << ", read_time: " << read_time
      << ", write_batch: " << write_batch.ShortDebugString();

  auto context = std::make_unique<TransactionConflictResolverContext>(
      doc_ops, write_batch, resolution_ht, read_time, txn_start_us, tablet_metrics,
      conflict_management_policy, skip_prefix_locks);
  if (conflict_management_policy == WAIT_ON_CONFLICT) {
    RSTATUS_DCHECK(
        wait_queue, InternalError,
        "Cannot use Wait-on-Conflict behavior - wait queue is not initialized");
    DCHECK(lock_batch);
    auto resolver = std::make_shared<WaitOnConflictResolver>(
        doc_db, status_manager, partial_range_key_intents, request_scope, std::move(context),
        std::move(callback), wait_queue, lock_batch, request_start_us, request_id,
        is_advisory_lock_request, deadline);
    resolver->Run();
  } else {
    // SKIP_ON_CONFLICT is piggybacked on FailOnConflictResolver since it is almost the same
    // with just a few lines of extra handling.
    auto resolver = std::make_shared<FailOnConflictResolver>(
        doc_db, status_manager, partial_range_key_intents, request_scope, std::move(context),
        std::move(callback));
    resolver->Resolve();
  }
  TRACE("resolver->Resolve done");
  return Status::OK();
}

Status ResolveOperationConflicts(
    const DocOperations& doc_ops,
    const ConflictManagementPolicy conflict_management_policy,
    const LWKeyValueWriteBatchPB& write_batch,
    RequestScope& request_scope,
    HybridTime intial_resolution_ht,
    uint64_t request_start_us,
    int64_t request_id,
    const DocDB& doc_db,
    PartialRangeKeyIntents partial_range_key_intents,
    TransactionStatusManager* status_manager,
    const std::shared_ptr<tablet::TabletMetricsHolder>& tablet_metrics,
    LockBatch* lock_batch,
    WaitQueue* wait_queue,
    CoarseTimePoint deadline,
    ResolutionCallback callback) {
  TRACE("ResolveOperationConflicts");
  VLOG_WITH_FUNC(3)
      << "conflict_management_policy=" << conflict_management_policy
      << ", initial_resolution_ht: " << intial_resolution_ht
      << ", write_batch: " << write_batch.ShortDebugString();
  auto txn_start_us = write_batch.has_object_locking_txn_meta()
      ? write_batch.object_locking_txn_meta().pg_txn_start_us()
      : write_batch.transaction().pg_txn_start_us();
  auto context = std::make_unique<OperationConflictResolverContext>(
      &doc_ops, intial_resolution_ht, txn_start_us, tablet_metrics, conflict_management_policy);
  RETURN_NOT_OK(context->InitObjectLockingTxnMeta(write_batch));

  if (conflict_management_policy == WAIT_ON_CONFLICT) {
    RSTATUS_DCHECK(
        wait_queue, InternalError,
        "Cannot use Wait-on-Conflict behavior - wait queue is not initialized");
    auto resolver = std::make_shared<WaitOnConflictResolver>(
        doc_db, status_manager, partial_range_key_intents, request_scope, std::move(context),
        std::move(callback), wait_queue, lock_batch, request_start_us, request_id,
        false /* is_advisory_lock_request */, deadline);
    resolver->Run();
  } else {
    // SKIP_ON_CONFLICT is piggybacked on FailOnConflictResolver since it is almost the same
    // with just a few lines of extra handling.
    auto resolver = std::make_shared<FailOnConflictResolver>(
        doc_db, status_manager, partial_range_key_intents, request_scope, std::move(context),
        std::move(callback));
    resolver->Resolve();
  }
  TRACE("resolver->Resolve done");
  return Status::OK();
}

std::string DebugIntentKeyToString(Slice intent_key) {
  auto parsed = dockv::ParseIntentKey(intent_key, Slice());
  if (!parsed.ok()) {
    LOG(WARNING) << "Failed to parse: " << intent_key.ToDebugHexString() << ": " << parsed.status();
    return intent_key.ToDebugHexString();
  }
  auto doc_ht = DocHybridTime::DecodeFromEnd(parsed->doc_ht);
  if (!doc_ht.ok()) {
    LOG(WARNING) << "Failed to decode doc ht: " << intent_key.ToDebugHexString() << ": "
                 << doc_ht.status();
    return intent_key.ToDebugHexString();
  }
  return Format("$0 (key: $1 type: $2 doc_ht: $3)",
                intent_key.ToDebugHexString(), SubDocKey::DebugSliceToString(parsed->doc_path),
                parsed->types, *doc_ht);
}

Status PopulateLockInfoFromParsedIntent(
    const dockv::ParsedIntent& parsed_intent, const dockv::DecodedIntentValue& decoded_value,
    const TableInfoProvider& table_info_provider, LockInfoPB* lock_info, bool intent_has_ht) {
  dockv::SubDocKey subdoc_key;
  RETURN_NOT_OK(subdoc_key.FullyDecodeFrom(
      parsed_intent.doc_path, dockv::HybridTimeRequired::kFalse));
  DCHECK(!subdoc_key.has_hybrid_time());

  const auto& doc_key = subdoc_key.doc_key();
  tablet::TableInfoPtr table_info;
  if (doc_key.has_colocation_id()) {
    table_info = VERIFY_RESULT(table_info_provider.GetTableInfo(doc_key.colocation_id()));
    lock_info->set_pg_table_id(table_info->pg_table_id.empty() ?
        table_info->table_id : table_info->pg_table_id);
  } else {
    table_info = VERIFY_RESULT(table_info_provider.GetTableInfo(kColocationIdNotSet));
  }
  RSTATUS_DCHECK(
      table_info, IllegalState, "Couldn't fetch TableInfo for key $0", doc_key.ToString());

  if (intent_has_ht) {
    auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(parsed_intent.doc_ht));
    lock_info->set_wait_end_ht(doc_ht.hybrid_time().ToUint64());
  }

  for (const auto& hash_key : doc_key.hashed_group()) {
    lock_info->add_hash_cols(hash_key.ToString());
  }
  for (const auto& range_key : doc_key.range_group()) {
    lock_info->add_range_cols(range_key.ToString());
  }
  const auto& schema = table_info->doc_read_context->schema();
  if (subdoc_key.num_subkeys() > 0 && subdoc_key.last_subkey().IsColumnId()) {
    const ColumnId& column_id = subdoc_key.last_subkey().GetColumnId();

    // Don't print the attnum for the liveness column
    if (column_id != 0) {
      const ColumnSchema& column = VERIFY_RESULT(schema.column_by_id(column_id));

      // If the order field is negative, it doesn't correspond to a column in pg_attribute
      if (column.order() > 0) {
        lock_info->set_attnum(column.order());
      }
    }

    lock_info->set_column_id(column_id);
  }

  lock_info->set_subtransaction_id(decoded_value.subtransaction_id);
  lock_info->set_is_explicit(
      decoded_value.body.starts_with(dockv::ValueEntryTypeAsChar::kRowLock));
  lock_info->set_multiple_rows_locked(
      schema.num_hash_key_columns() > doc_key.hashed_group().size() ||
      schema.num_range_key_columns() > doc_key.range_group().size());

  for (const auto& intent_type : parsed_intent.types) {
    switch (intent_type) {
      case dockv::IntentType::kWeakRead:
        lock_info->add_modes(LockMode::WEAK_READ);
        break;
      case dockv::IntentType::kWeakWrite:
        lock_info->add_modes(LockMode::WEAK_WRITE);
        break;
      case dockv::IntentType::kStrongRead:
        lock_info->add_modes(LockMode::STRONG_READ);
        break;
      case dockv::IntentType::kStrongWrite:
        lock_info->add_modes(LockMode::STRONG_WRITE);
        break;
    }
  }
  return Status::OK();
}

} // namespace yb::docdb
