// Copyright (c) YugaByte, Inc.
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

#include <map>

#include "yb/common/hybrid_time.h"
#include "yb/common/row_mark.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/common/transaction_error.h"
#include "yb/common/transaction_priority.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/shared_lock_manager.h"
#include "yb/docdb/transaction_dump.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace docdb {

namespace {

using SubtxnHasNonLockConflict = std::unordered_map<SubTransactionId, bool>;

struct TransactionConflictInfo {
  WaitPolicy wait_policy;
  // Map storing subtransaction_id -> bool which tracks whether or not that subtransaction has a
  // conflict which is not from a non-modification row lock (i.e., explicit row-level lock such as
  // "FOR UPDATE", "FOR SHARE", etc). The map helps in 2 ways -
  //   1. After reading all conflicts with a txn, we need the list of subtransactions with
  //      conflicting intents so that we can ignore those which have been aborted via savepoint
  //      rollbacks.
  //   2. If a transaction has committed and all its live subtransactions wrote only
  //      non-modification intents, we don't have to consider them for conflicts.
  SubtxnHasNonLockConflict subtransactions;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(wait_policy, subtransactions);
  }
};

using TransactionConflictInfoMap = std::unordered_map<TransactionId,
                                                      TransactionConflictInfo,
                                                      TransactionIdHash>;

struct TransactionData {
  TransactionData(
      TransactionId id_, WaitPolicy wait_policy_, SubtxnHasNonLockConflict subtransactions_)
      : id(id_), wait_policy(wait_policy_), subtransactions(subtransactions_) {}

  TransactionId id;
  WaitPolicy wait_policy;
  SubtxnHasNonLockConflict subtransactions;
  TransactionStatus status;
  HybridTime commit_time;
  uint64_t priority;
  Status failure;

  void RemoveAbortedSubtransactions(const AbortedSubTransactionSet& aborted_subtxn_set) {
    auto it = subtransactions.begin();
    while (it != subtransactions.end()) {
      if (aborted_subtxn_set.Test(it->first)) {
        it = subtransactions.erase(it);
      } else {
        it++;
      }
    }
  }

  void ProcessStatus(const TransactionStatusResult& result) {
    status = result.status;
    if (status == TransactionStatus::COMMITTED) {
      LOG_IF(DFATAL, !result.status_time.is_valid())
          << "Status time not specified for committed transaction: " << id;
      commit_time = result.status_time;
    }
    if (status != TransactionStatus::ABORTED) {
      DCHECK(status == TransactionStatus::PENDING || status == TransactionStatus::COMMITTED);
      RemoveAbortedSubtransactions(result.aborted_subtxn_set);
    }
  }

  std::string ToString() const {
    return Format("{ id: $0 status: $1 commit_time: $2 priority: $3 failure: $4 }",
                  id, TransactionStatus_Name(status), commit_time, priority, failure);
  }
};

Status MakeConflictStatus(const TransactionId& our_id, const TransactionId& other_id,
                                  const char* reason, Counter* conflicts_metric) {
  conflicts_metric->Increment();
  return (STATUS(TryAgain, Format("$0 Conflicts with $1 transaction: $2", our_id, reason, other_id),
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
      boost::iterator_range<TransactionData*> transactions) = 0;

  // Check subtransaction data of pending transaction to determine if conflict can be avoided.
  bool CheckConflictWithPending(const TransactionData& transaction_data) {
    // We remove aborted subtransactions when processing the SubtxnSet stored locally or
    // returned by the status tablet. If this is now empty, then all potentially conflicting
    // intents have been aborted and there is no longer a conflict with this transaction.
    return transaction_data.subtransactions.empty();
  }

  // Check for conflict against committed transaction.
  // Returns true if transaction could be removed from list of conflicts.
  virtual Result<bool> CheckConflictWithCommitted(
      const TransactionData& transaction_data, HybridTime commit_time) = 0;

  virtual HybridTime GetResolutionHt() = 0;

  virtual bool IgnoreConflictsWith(const TransactionId& other) = 0;

  virtual TransactionId transaction_id() const = 0;

  virtual std::string ToString() const = 0;

  std::string LogPrefix() const {
    return ToString() + ": ";
  }

  virtual ~ConflictResolverContext() = default;
};

class ConflictResolver : public std::enable_shared_from_this<ConflictResolver> {
 public:
  ConflictResolver(const DocDB& doc_db,
                   TransactionStatusManager* status_manager,
                   RequestScope request_scope,
                   PartialRangeKeyIntents partial_range_key_intents,
                   std::unique_ptr<ConflictResolverContext> context,
                   ResolutionCallback callback)
      : doc_db_(doc_db), status_manager_(*status_manager), request_scope_(std::move(request_scope)),
        partial_range_key_intents_(partial_range_key_intents), context_(std::move(context)),
        callback_(std::move(callback)) {}

  PartialRangeKeyIntents partial_range_key_intents() {
    return partial_range_key_intents_;
  }

  TransactionStatusManager& status_manager() {
    return status_manager_;
  }

  const DocDB& doc_db() {
    return doc_db_;
  }

  Result<TransactionMetadata> PrepareMetadata(const TransactionMetadataPB& pb) {
    return status_manager_.PrepareMetadata(pb);
  }

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) {
    return status_manager_.FillPriorities(inout);
  }

  void Resolve() {
    auto status = context_->ReadConflicts(this);
    if (!status.ok()) {
      InvokeCallback(status);
      return;
    }

    ResolveConflicts();
  }

  Result<WaitPolicy> CombineWaitPolicy(WaitPolicy existing_policy, WaitPolicy new_policy) {
    RSTATUS_DCHECK(
        existing_policy != WAIT_BLOCK, InternalError, "WAIT_BLOCK isn't support yet.");

    switch(new_policy) {
      case WAIT_BLOCK:
        return STATUS(NotSupported, "WAIT_BLOCK isn't support yet.");
      case WAIT_ERROR:
        // Even if some intent had a wait policy of WAIT_SKIP, WAIT_ERROR overrides that policy.
        return new_policy;
      case WAIT_SKIP:
        // The existing_policy can either be WAIT_ERROR or WAIT_SKIP. In either case, we can leave
        // it untouched.
        return existing_policy;
    }
    return STATUS(NotSupported, "Unknown wait policy.");
  }

  // Reads conflicts for specified intent from DB.
  Status ReadIntentConflicts(IntentTypeSet type, KeyBytes* intent_key_prefix,
                                     WaitPolicy wait_policy) {
    EnsureIntentIteratorCreated();

    const auto conflicting_intent_types = kIntentTypeSetConflicts[type.ToUIntPtr()];

    KeyBytes upperbound_key(*intent_key_prefix);
    upperbound_key.AppendKeyEntryType(KeyEntryType::kMaxByte);
    intent_key_upperbound_ = upperbound_key.AsSlice();

    size_t original_size = intent_key_prefix->size();
    intent_key_prefix->AppendKeyEntryType(KeyEntryType::kIntentTypeSet);
    // Have only weak intents, so could skip other weak intents.
    if (!HasStrong(type)) {
      char value = 1 << kStrongIntentFlag;
      intent_key_prefix->AppendRawBytes(&value, 1);
    }
    auto se = ScopeExit([this, intent_key_prefix, original_size] {
      intent_key_prefix->Truncate(original_size);
      intent_key_upperbound_.clear();
    });
    Slice prefix_slice(intent_key_prefix->AsSlice().data(), original_size);
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Check conflicts in intents DB; Seek: "
                                 << intent_key_prefix->AsSlice().ToDebugHexString() << " for type "
                                 << ToString(type) << " and wait_policy=" << wait_policy;
    intent_iter_.Seek(intent_key_prefix->AsSlice());
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
          !IntentValueType(existing_key[prefix_slice.size()])) {
        break;
      }

      auto existing_intent = VERIFY_RESULT(
          docdb::ParseIntentKey(intent_iter_.key(), existing_value));

      VLOG_WITH_PREFIX_AND_FUNC(4) << "Found: " << existing_value.ToDebugString()
                                   << " has intent types " << ToString(existing_intent.types);
      auto decoded_value = VERIFY_RESULT(DecodeIntentValue(
          existing_value, nullptr /* verify_transaction_id_slice */,
          HasStrong(existing_intent.types)));
      const auto intent_mask = kIntentTypeSetMask[existing_intent.types.ToUIntPtr()];
      if ((conflicting_intent_types & intent_mask) != 0) {
        auto transaction_id = decoded_value.transaction_id;
        bool lock_only = decoded_value.body.starts_with(KeyEntryTypeAsChar::kRowLock);

        if (!context_->IgnoreConflictsWith(transaction_id)) {
          auto p = conflicts_.emplace(transaction_id,
                                      TransactionConflictInfo {
                                        .wait_policy = wait_policy,
                                        .subtransactions = {},
                                      });
          if (!p.second) {
            p.first->second.wait_policy = VERIFY_RESULT(
                CombineWaitPolicy(p.first->second.wait_policy, wait_policy));
          }
          p.first->second.subtransactions[decoded_value.subtransaction_id] |= !lock_only;
        }
      }

      intent_iter_.Next();
    }

    return intent_iter_.status();
  }

  void EnsureIntentIteratorCreated() {
    if (!intent_iter_.Initialized()) {
      intent_iter_ = CreateRocksDBIterator(
          doc_db_.intents,
          doc_db_.key_bounds,
          BloomFilterMode::DONT_USE_BLOOM_FILTER,
          boost::none /* user_key_for_filter */,
          rocksdb::kDefaultQueryId,
          nullptr /* file_filter */,
          &intent_key_upperbound_);
    }
  }

 private:
  void InvokeCallback(const Result<HybridTime>& result) {
    YB_TRANSACTION_DUMP(
        Conflicts, context_->transaction_id(),
        result.ok() ? *result : HybridTime::kInvalid,
        Slice(pointer_cast<const uint8_t*>(transactions_.data()),
              transactions_.size() * sizeof(transactions_[0])));
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

    transactions_.reserve(conflicts_.size());
    for (const auto& kv : conflicts_) {
      transactions_.emplace_back(kv.first /* id */,
                                 kv.second.wait_policy,
                                 // TODO - avoid potential copy here.
                                 kv.second.subtransactions);
    }
    remaining_transactions_ = transactions_.size();

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

    RETURN_NOT_OK(context_->CheckPriority(this, RemainingTransactions()));

    AbortTransactions();
    return false;
  }

  // Returns true when there are no conflicts left.
  Result<bool> CheckLocalCommits() {
    return DoCleanup([this](auto* transaction) -> Result<bool> {
      return this->CheckLocalRunningTransaction(transaction);
    });
  }

  // Check whether specified transaction was locally committed, and store this state if so.
  // Returns true if conflict with specified transaction is resolved.
  Result<bool> CheckLocalRunningTransaction(TransactionData* transaction) {
    auto local_txn_data = status_manager().LocalTxnData(transaction->id);
    if (!local_txn_data) {
      return false;
    }
    transaction->RemoveAbortedSubtransactions(local_txn_data->aborted_subtxn_set);
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

  // Apply specified functor to all active (i.e. not resolved) transactions.
  // If functor returns true, it means that transaction was resolved.
  // So such transaction is moved out of active transactions range.
  // Returns true if there are no active transaction left.
  template <class F>
  Result<bool> DoCleanup(const F& f) {
    auto end = transactions_.begin() + remaining_transactions_;
    for (auto transaction = transactions_.begin(); transaction != end;) {
      if (!VERIFY_RESULT(f(&*transaction))) {
        ++transaction;
        continue;
      }
      if (--end == transaction) {
        break;
      }
      std::swap(*transaction, *end);
    }
    remaining_transactions_ = end - transactions_.begin();

    return remaining_transactions_ == 0;
  }

  // Removes all transactions that would not conflict with us anymore.
  // Returns failure if we conflict with transaction that cannot be aborted.
  Result<bool> Cleanup() {
    return DoCleanup([this](auto* transaction) -> Result<bool> {
      return this->CheckCleanup(transaction);
    });
  }

  Result<bool> CheckCleanup(TransactionData* transaction) {
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

  boost::iterator_range<TransactionData*> RemainingTransactions() {
    auto begin = transactions_.data();
    return boost::make_iterator_range(begin, begin + remaining_transactions_);
  }

  void FetchTransactionStatuses() {
    static const std::string kRequestReason = "conflict resolution"s;
    auto self = shared_from_this();
    pending_requests_.store(remaining_transactions_);
    for (auto& i : RemainingTransactions()) {
      auto& transaction = i;
      TRACE("FetchingTransactionStatus for $0", yb::ToString(transaction.id));
      StatusRequest request = {
        &transaction.id,
        context_->GetResolutionHt(),
        context_->GetResolutionHt(),
        0, // serial no. Could use 0 here, because read_ht == global_limit_ht.
           // So we cannot accept status with time >= read_ht and < global_limit_ht.
        &kRequestReason,
        TransactionLoadFlags{TransactionLoadFlag::kCleanup},
        [self, &transaction](Result<TransactionStatusResult> result) {
          if (result.ok()) {
            transaction.ProcessStatus(*result);
          } else if (result.status().IsTryAgain()) {
            // It is safe to suppose that transaction in PENDING state in case of try again error.
            transaction.status = TransactionStatus::PENDING;
          } else if (result.status().IsNotFound()) {
            transaction.status = TransactionStatus::ABORTED;
          } else {
            transaction.failure = result.status();
          }
          if (self->pending_requests_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            self->FetchTransactionStatusesDone();
          }
        }
      };
      status_manager().RequestStatusAt(request);
    }
  }

  void AbortTransactions() {
    auto self = shared_from_this();
    pending_requests_.store(remaining_transactions_);
    for (auto& i : RemainingTransactions()) {
      auto& transaction = i;
      TRACE("Aborting $0", yb::ToString(transaction.id));
      status_manager().Abort(
          transaction.id,
          [self, &transaction](Result<TransactionStatusResult> result) {
        VLOG(4) << self->LogPrefix() << "Abort received: " << AsString(result);
        if (result.ok()) {
          transaction.ProcessStatus(*result);
        } else if (result.status().IsRemoteError() || result.status().IsAborted()) {
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

  std::string LogPrefix() const {
    return context_->LogPrefix();
  }

  DocDB doc_db_;
  TransactionStatusManager& status_manager_;
  RequestScope request_scope_;
  PartialRangeKeyIntents partial_range_key_intents_;
  std::unique_ptr<ConflictResolverContext> context_;
  ResolutionCallback callback_;

  BoundedRocksDbIterator intent_iter_;
  Slice intent_key_upperbound_;
  TransactionConflictInfoMap conflicts_;

  // Resolution state for all transactions. Resolved transactions are moved to the end of it.
  std::vector<TransactionData> transactions_;
  // Number of transactions that are not yet resolved. After successful resolution should be 0.
  size_t remaining_transactions_;

  std::atomic<size_t> pending_requests_{0};
};

struct IntentData {
  IntentTypeSet types;
  bool full_doc_key;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(types, full_doc_key);
  }
};

using IntentTypesContainer = std::map<KeyBuffer, IntentData>;

class IntentProcessor {
 public:
  IntentProcessor(IntentTypesContainer* container, const IntentTypeSet& strong_intent_types)
      : container_(*container),
        strong_intent_types_(strong_intent_types),
        weak_intent_types_(StrongToWeak(strong_intent_types_))
  {}

  void Process(IntentStrength strength, FullDocKey full_doc_key, KeyBytes* intent_key) {
    const auto is_strong = strength == IntentStrength::kStrong;
    const auto& intent_type_set = is_strong ? strong_intent_types_ : weak_intent_types_;
    auto i = container_.find(intent_key->data());
    if (i == container_.end()) {
      container_.emplace(intent_key->data(),
                         IntentData{intent_type_set, full_doc_key});
      return;
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
  }

 private:
  IntentTypesContainer& container_;
  const IntentTypeSet strong_intent_types_;
  const IntentTypeSet weak_intent_types_;
};

class StrongConflictChecker {
 public:
  StrongConflictChecker(const TransactionId& transaction_id,
                        HybridTime read_time,
                        ConflictResolver* resolver,
                        Counter* conflicts_metric,
                        KeyBytes* buffer)
      : transaction_id_(transaction_id),
        read_time_(read_time),
        resolver_(*resolver),
        conflicts_metric_(*conflicts_metric),
        buffer_(*buffer)
  {}

  Status Check(const Slice& intent_key, bool strong, WaitPolicy wait_policy) {
    const auto hash = VERIFY_RESULT(DecodeDocKeyHash(intent_key));
    if (PREDICT_FALSE(!value_iter_.Initialized() || hash != value_iter_hash_)) {
      value_iter_ = CreateRocksDBIterator(
          resolver_.doc_db().regular,
          resolver_.doc_db().key_bounds,
          BloomFilterMode::USE_BLOOM_FILTER,
          intent_key,
          rocksdb::kDefaultQueryId);
      value_iter_hash_ = hash;
    }
    value_iter_.Seek(intent_key);
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Overwrite; Seek: " << intent_key.ToDebugString() << " ("
        << SubDocKey::DebugSliceToString(intent_key) << "), strong: " << strong << ", wait_policy: "
        << AsString(wait_policy);
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
    while (value_iter_.Valid() &&
           (intent_key.starts_with(KeyEntryTypeAsChar::kGroupEnd) ||
            value_iter_.key().starts_with(intent_key))) {
      auto existing_key = value_iter_.key();
      auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&existing_key));
      if (existing_key.empty() ||
          existing_key[existing_key.size() - 1] != KeyEntryTypeAsChar::kHybridTime) {
        return STATUS_FORMAT(
            Corruption, "Hybrid time expected at end of key: $0",
            value_iter_.key().ToDebugString());
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
          << ", found key: " << SubDocKey::DebugSliceToString(value_iter_.key())
          << ", after start: " << (doc_ht.hybrid_time() >= read_time_)
          << ", value: " << value_iter_.value().ToDebugString();
      if (doc_ht.hybrid_time() >= read_time_) {
        if (wait_policy == WAIT_SKIP) {
          return STATUS(InternalError, "Skip locking since entity was modified in regular db",
                        TransactionError(TransactionErrorCode::kSkipLocking));
        } else {
          conflicts_metric_.Increment();
          return STATUS_EC_FORMAT(TryAgain, TransactionError(TransactionErrorCode::kConflict),
                                  "Value write after transaction start: $0 >= $1",
                                  doc_ht.hybrid_time(), read_time_);
        }
      }
      buffer_.Reset(existing_key);
      // Already have ValueType::kHybridTime at the end
      buffer_.AppendHybridTime(DocHybridTime::kMin);
      ROCKSDB_SEEK(&value_iter_, buffer_.AsSlice());
    }

    return value_iter_.status();
  }

 private:
  std::string LogPrefix() const {
    return Format("$0: ", transaction_id_);
  }

  const TransactionId& transaction_id_;
  const HybridTime read_time_;
  ConflictResolver& resolver_;
  Counter& conflicts_metric_;
  KeyBytes& buffer_;

  // RocksDb iterator with bloom filter can be reused in case keys has same hash component.
  BoundedRocksDbIterator value_iter_;
  boost::optional<DocKeyHash> value_iter_hash_;

};

class ConflictResolverContextBase : public ConflictResolverContext {
 public:
  ConflictResolverContextBase(const DocOperations& doc_ops,
                              HybridTime resolution_ht,
                              Counter* conflicts_metric)
      : doc_ops_(doc_ops),
        resolution_ht_(resolution_ht),
        conflicts_metric_(conflicts_metric) {
  }

  const DocOperations& doc_ops() {
    return doc_ops_;
  }

  HybridTime GetResolutionHt() override {
    return resolution_ht_;
  }

  void MakeResolutionAtLeast(const HybridTime& resolution_ht) {
    resolution_ht_.MakeAtLeast(resolution_ht);
  }

  Counter* GetConflictsMetric() {
    return conflicts_metric_;
  }

 protected:
  Status CheckPriorityInternal(
      ConflictResolver* resolver,
      boost::iterator_range<TransactionData*> transactions,
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
      if (transaction.wait_policy == WAIT_SKIP) {
        return STATUS(InternalError, "Skip locking since entity is already locked",
                      TransactionError(TransactionErrorCode::kSkipLocking));
      }

      // READ COMMITTED txns require a guarantee that no txn abort it. They can handle facing a
      // kConflict due to another txn's conflicting intent, but can't handle aborts. To ensure
      // these guarantees -
      //   1. all READ COMMITTED txns are given kHighestPriority and
      //   2. a kConflict is raised even if their_priority equals our_priority.
      if (our_priority <= their_priority) {
        return MakeConflictStatus(
            our_transaction_id, transaction.id, "higher priority", GetConflictsMetric());
      }
    }
    fetched_metadata_for_transactions_ = true;

    return Status::OK();
  }

 private:
  const DocOperations& doc_ops_;

  // Hybrid time of conflict resolution, used to request transaction status from status tablet.
  HybridTime resolution_ht_;

  bool fetched_metadata_for_transactions_ = false;

  Counter* conflicts_metric_ = nullptr;
};

// Utility class for ResolveTransactionConflicts implementation.
class TransactionConflictResolverContext : public ConflictResolverContextBase {
 public:
  TransactionConflictResolverContext(const DocOperations& doc_ops,
                                     const KeyValueWriteBatchPB& write_batch,
                                     HybridTime resolution_ht,
                                     HybridTime read_time,
                                     Counter* conflicts_metric)
      : ConflictResolverContextBase(doc_ops, resolution_ht, conflicts_metric),
        write_batch_(write_batch),
        read_time_(read_time),
        transaction_id_(FullyDecodeTransactionId(write_batch.transaction().transaction_id()))
  {}

  virtual ~TransactionConflictResolverContext() {}

 private:
  Status ReadConflicts(ConflictResolver* resolver) override {
    RETURN_NOT_OK(transaction_id_);

    VLOG_WITH_PREFIX(3) << "Resolve conflicts";

    metadata_ = VERIFY_RESULT(resolver->PrepareMetadata(write_batch_.transaction()));

    boost::container::small_vector<RefCntPrefix, 8> paths;

    const size_t kKeyBufferInitialSize = 512;
    KeyBytes buffer;
    buffer.Reserve(kKeyBufferInitialSize);
    const auto row_mark = GetRowMarkTypeFromPB(write_batch_);
    IntentTypesContainer container;
    IntentProcessor write_processor(
        &container,
        GetStrongIntentTypeSet(metadata_.isolation, docdb::OperationKind::kWrite, row_mark));
    for (const auto& doc_op : doc_ops()) {
      paths.clear();
      IsolationLevel ignored_isolation_level;
      RETURN_NOT_OK(doc_op->GetDocPaths(
          GetDocPathsMode::kIntents, &paths, &ignored_isolation_level));

      for (const auto& path : paths) {
        VLOG_WITH_PREFIX_AND_FUNC(4)
            << "Doc path: " << SubDocKey::DebugSliceToString(path.as_slice());
        RETURN_NOT_OK(EnumerateIntents(
            path.as_slice(),
            /* intent_value */ Slice(),
            [&write_processor](
                auto strength, FullDocKey full_doc_key, auto, auto intent_key, auto) {
              write_processor.Process(strength, full_doc_key, intent_key);
              return Status::OK();
            },
            &buffer,
            resolver->partial_range_key_intents()));
      }
    }
    // Either write_batch_.read_pairs is not empty or doc_ops is non empty. Both can't be non empty
    // together. This is because read_pairs is filled only in case of a read operation that has a
    // row mark or is part of a serializable txn.
    // 1. In case doc_ops are present, we use the default wait policy of WAIT_ERROR.
    // 2. In case of a read rpc that has wait_policy, we use that.
    auto wait_policy = WAIT_ERROR;
    const auto& pairs = write_batch_.read_pairs();
    if (!pairs.empty()) {
      IntentProcessor read_processor(
          &container,
          GetStrongIntentTypeSet(metadata_.isolation, docdb::OperationKind::kRead, row_mark));
      wait_policy = write_batch_.wait_policy();
      RETURN_NOT_OK(EnumerateIntents(
          pairs,
          [&read_processor] (
              auto strength, FullDocKey full_doc_key, auto, auto intent_key, auto) {
            read_processor.Process(strength, full_doc_key, intent_key);
            return Status::OK();
          },
          resolver->partial_range_key_intents()));
    }

    if (container.empty()) {
      return Status::OK();
    }

    VLOG_WITH_PREFIX_AND_FUNC(4) << "Check txn's conflicts for following intents: "
                                 << AsString(container);

    StrongConflictChecker checker(
        *transaction_id_, read_time_, resolver, GetConflictsMetric(), &buffer);
    // Iterator on intents DB should be created before iterator on regular DB.
    // This is to prevent the case when we create an iterator on the regular DB where a
    // provisional record has not yet been applied, and then create an iterator the intents
    // DB where the provisional record has already been removed.
    resolver->EnsureIntentIteratorCreated();

    for (const auto& i : container) {
      if (read_time_ != HybridTime::kMax) {
        const Slice intent_key = i.first.AsSlice();
        bool strong = HasStrong(i.second.types);
        // For strong intents or weak intents at a full document key level (i.e. excluding intents
        // that omit some final range components of the document key), check for conflicts with
        // records in regular RocksDB. We need this because the row might have been deleted
        // concurrently by a single-shard transaction or a committed and applied transaction.
        if (strong || i.second.full_doc_key) {
          RETURN_NOT_OK(checker.Check(intent_key, strong, wait_policy));
        }
      }
      buffer.Reset(i.first.AsSlice());
      RETURN_NOT_OK(resolver->ReadIntentConflicts(i.second.types, &buffer, wait_policy));
    }

    return Status::OK();
  }

  Status CheckPriority(ConflictResolver* resolver,
                               boost::iterator_range<TransactionData*> transactions) override {
    return CheckPriorityInternal(resolver, transactions, metadata_.transaction_id,
                                 metadata_.priority);
  }

  Result<bool> CheckConflictWithCommitted(
      const TransactionData& transaction_data, HybridTime commit_time) override {
    RSTATUS_DCHECK(commit_time.is_valid(), Corruption, "Invalid transaction commit time");

    VLOG_WITH_PREFIX(4) << "Committed: " << transaction_data.id << ", commit_time: " << commit_time
                        << ", read_time: " << read_time_
                        << ", transaction_data: " << transaction_data.ToString();

    for (const auto& subtxn_and_data : transaction_data.subtransactions) {
      auto has_non_lock_conflict = subtxn_and_data.second;
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
      if (has_non_lock_conflict && commit_time >= read_time_) {
        if (transaction_data.wait_policy == WAIT_SKIP) {
          return STATUS(InternalError, "Skip locking since entity was modified by a recent commit",
                        TransactionError(TransactionErrorCode::kSkipLocking));
        } else {
          return MakeConflictStatus(
            *transaction_id_, transaction_data.id, "committed", GetConflictsMetric());
        }
      }
    }

    return true;
  }

  bool IgnoreConflictsWith(const TransactionId& other) override {
    return other == *transaction_id_;
  }

  TransactionId transaction_id() const override {
    return *transaction_id_;
  }

  std::string ToString() const override {
    return yb::ToString(transaction_id_);
  }

  const KeyValueWriteBatchPB& write_batch_;

  // Read time of the transaction identified by transaction_id_, could be HybridTime::kMax in case
  // of serializable isolation or when read time not yet picked for snapshot isolation.
  const HybridTime read_time_;

  // Id of transaction when is writing intents, for which we are resolving conflicts.
  Result<TransactionId> transaction_id_;

  TransactionMetadata metadata_;

  Status result_ = Status::OK();
};

class OperationConflictResolverContext : public ConflictResolverContextBase {
 public:
  OperationConflictResolverContext(const DocOperations* doc_ops,
                                   HybridTime resolution_ht,
                                   Counter* conflicts_metric)
      : ConflictResolverContextBase(*doc_ops, resolution_ht, conflicts_metric) {
  }

  virtual ~OperationConflictResolverContext() {}

  // Reads stored intents that could conflict with our operations.
  Status ReadConflicts(ConflictResolver* resolver) override {
    boost::container::small_vector<RefCntPrefix, 8> doc_paths;
    boost::container::small_vector<size_t, 32> key_prefix_lengths;
    KeyBytes encoded_key_buffer;

    IntentTypeSet strong_intent_types;

    EnumerateIntentsCallback callback = [&strong_intent_types, resolver](
        IntentStrength intent_strength, FullDocKey full_doc_key, Slice,
        KeyBytes* encoded_key_buffer, LastKey) {
      return resolver->ReadIntentConflicts(
          intent_strength == IntentStrength::kStrong ? strong_intent_types
                                                     : StrongToWeak(strong_intent_types),
          encoded_key_buffer, WAIT_ERROR);
    };

    for (const auto& doc_op : doc_ops()) {
      doc_paths.clear();
      IsolationLevel isolation;
      RETURN_NOT_OK(doc_op->GetDocPaths(GetDocPathsMode::kIntents, &doc_paths, &isolation));

      strong_intent_types = GetStrongIntentTypeSet(isolation, OperationKind::kWrite,
                                                   RowMarkType::ROW_MARK_ABSENT);

      for (const auto& doc_path : doc_paths) {
        VLOG_WITH_PREFIX_AND_FUNC(4)
            << "Doc path: " << SubDocKey::DebugSliceToString(doc_path.as_slice());
        RETURN_NOT_OK(EnumerateIntents(
            doc_path.as_slice(), Slice(), callback, &encoded_key_buffer,
            PartialRangeKeyIntents::kTrue));
      }
    }

    return Status::OK();
  }

  Status CheckPriority(ConflictResolver* resolver,
                               boost::iterator_range<TransactionData*> transactions) override {
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

  std::string ToString() const override {
    return "Operation Context";
  }

  Result<bool> CheckConflictWithCommitted(
      const TransactionData& transaction_data, HybridTime commit_time) override {
    if (commit_time != HybridTime::kMax) {
      MakeResolutionAtLeast(commit_time);
      return true;
    }
    return false;
  }
};

} // namespace

Status ResolveTransactionConflicts(const DocOperations& doc_ops,
                                   const KeyValueWriteBatchPB& write_batch,
                                   HybridTime hybrid_time,
                                   HybridTime read_time,
                                   const DocDB& doc_db,
                                   PartialRangeKeyIntents partial_range_key_intents,
                                   TransactionStatusManager* status_manager,
                                   Counter* conflicts_metric,
                                   ResolutionCallback callback) {
  DCHECK(hybrid_time.is_valid());
  TRACE("ResolveTransactionConflicts");
  auto context = std::make_unique<TransactionConflictResolverContext>(
      doc_ops, write_batch, hybrid_time, read_time, conflicts_metric);
  auto request_scope = VERIFY_RESULT(RequestScope::Create(status_manager));
  auto resolver = std::make_shared<ConflictResolver>(
      doc_db, status_manager, std::move(request_scope), partial_range_key_intents,
      std::move(context), std::move(callback));
  // Resolve takes a self reference to extend lifetime.
  resolver->Resolve();
  TRACE("resolver->Resolve done");
  return Status::OK();
}

Status ResolveOperationConflicts(const DocOperations& doc_ops,
                                 HybridTime resolution_ht,
                                 const DocDB& doc_db,
                                 PartialRangeKeyIntents partial_range_key_intents,
                                 TransactionStatusManager* status_manager,
                                 Counter* conflicts_metric,
                                 ResolutionCallback callback) {
  TRACE("ResolveOperationConflicts");
  auto context = std::make_unique<OperationConflictResolverContext>(&doc_ops, resolution_ht,
                                                                    conflicts_metric);
  auto request_scope = VERIFY_RESULT(RequestScope::Create(status_manager));
  auto resolver = std::make_shared<ConflictResolver>(
      doc_db, status_manager, std::move(request_scope), partial_range_key_intents,
      std::move(context), std::move(callback));
  // Resolve takes a self reference to extend lifetime.
  resolver->Resolve();
  TRACE("resolver->Resolve done");
  return Status::OK();
}

#define INTENT_KEY_SCHECK(lhs, op, rhs, msg) \
  BOOST_PP_CAT(SCHECK_, op)(lhs, \
                            rhs, \
                            Corruption, \
                            Format("Bad intent key, $0 in $1, transaction from: $2", \
                                   msg, \
                                   intent_key.ToDebugHexString(), \
                                   transaction_id_source.ToDebugHexString()))

// transaction_id_slice used in INTENT_KEY_SCHECK
Result<ParsedIntent> ParseIntentKey(Slice intent_key, Slice transaction_id_source) {
  ParsedIntent result;
  size_t doc_ht_size = 0;
  result.doc_path = intent_key;
  // Intent is encoded as "DocPath + IntentType + DocHybridTime".
  RETURN_NOT_OK(DocHybridTime::CheckAndGetEncodedSize(result.doc_path, &doc_ht_size));
  // 3 comes from (ValueType::kIntentType, the actual intent type, ValueType::kHybridTime).
  INTENT_KEY_SCHECK(result.doc_path.size(), GE, doc_ht_size + 3, "key too short");
  result.doc_path.remove_suffix(doc_ht_size + 3);
  auto intent_type_and_doc_ht = result.doc_path.end();
  if (intent_type_and_doc_ht[0] == KeyEntryTypeAsChar::kObsoleteIntentType) {
    result.types = ObsoleteIntentTypeToSet(intent_type_and_doc_ht[1]);
  } else if (intent_type_and_doc_ht[0] == KeyEntryTypeAsChar::kObsoleteIntentTypeSet) {
    result.types = ObsoleteIntentTypeSetToNew(intent_type_and_doc_ht[1]);
  } else {
    INTENT_KEY_SCHECK(intent_type_and_doc_ht[0], EQ, KeyEntryTypeAsChar::kIntentTypeSet,
        "intent type set type expected");
    result.types = IntentTypeSet(intent_type_and_doc_ht[1]);
  }
  INTENT_KEY_SCHECK(intent_type_and_doc_ht[2], EQ, KeyEntryTypeAsChar::kHybridTime,
                    "hybrid time value type expected");
  result.doc_ht = Slice(result.doc_path.end() + 2, doc_ht_size + 1);
  return result;
}

std::string DebugIntentKeyToString(Slice intent_key) {
  auto parsed = ParseIntentKey(intent_key, Slice());
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

} // namespace docdb
} // namespace yb
