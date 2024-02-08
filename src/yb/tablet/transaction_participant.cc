//
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
//

#include "yb/tablet/transaction_participant.h"

#include <ctime>
#include <queue>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include "yb/client/transaction_rpc.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"

#include "yb/consensus/consensus_util.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/rpc/poller.h"

#include "yb/server/clock.h"

#include "yb/tablet/cleanup_aborts_task.h"
#include "yb/tablet/cleanup_intents_task.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/remove_intents_task.h"
#include "yb/tablet/running_transaction.h"
#include "yb/tablet/running_transaction_context.h"
#include "yb/tablet/transaction_loader.h"
#include "yb/tablet/transaction_participant_context.h"
#include "yb/tablet/transaction_status_resolver.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/lru_cache.h"
#include "yb/util/metrics.h"
#include "yb/util/operation_counter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/tsan_util.h"
#include "yb/util/unique_lock.h"

using std::vector;

using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_uint64(transaction_min_running_check_delay_ms, 50,
              "When transaction with minimal start hybrid time is updated at transaction "
              "participant, we wait at least this number of milliseconds before checking its "
              "status at transaction coordinator. Used for the optimization that deletes "
              "provisional records RocksDB SSTable files.");

DEFINE_UNKNOWN_uint64(transaction_min_running_check_interval_ms, 250,
              "While transaction with minimal start hybrid time remains the same, we will try "
              "to check its status at transaction coordinator at regular intervals this "
              "long (ms). Used for the optimization that deletes "
              "provisional records RocksDB SSTable files.");

DEFINE_test_flag(double, transaction_ignore_applying_probability, 0,
                 "Probability to ignore APPLYING update in tests.");
DEFINE_test_flag(bool, fail_in_apply_if_no_metadata, false,
                 "Fail when applying intents if metadata is not found.");

DEFINE_UNKNOWN_int32(max_transactions_in_status_request, 128,
             "Request status for at most specified number of transactions at once. "
                 "0 disables load time transaction status resolution.");

DEFINE_UNKNOWN_uint64(transactions_cleanup_cache_size, 256, "Transactions cleanup cache size.");

DEFINE_UNKNOWN_uint64(transactions_status_poll_interval_ms, 500 * yb::kTimeMultiplier,
              "Transactions poll interval.");

DEFINE_UNKNOWN_bool(transactions_poll_check_aborted, true,
    "Check aborted transactions during poll.");

DEFINE_NON_RUNTIME_int32(wait_queue_poll_interval_ms, 100,
    "The interval duration between wait queue polls to fetch transaction statuses of "
    "active blockers.");

DECLARE_int64(transaction_abort_check_timeout_ms);

DECLARE_int64(cdc_intent_retention_ms);

DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_int32(clear_deadlocked_txns_info_older_than_heartbeats);

METRIC_DEFINE_simple_counter(
    tablet, transaction_not_found, "Total number of missing transactions during load",
    yb::MetricUnit::kTransactions);
METRIC_DEFINE_simple_gauge_uint64(
    tablet, transactions_running, "Total number of transactions running in participant",
    yb::MetricUnit::kTransactions);
METRIC_DEFINE_simple_gauge_uint64(
    tablet, aborted_transactions_pending_cleanup,
    "Total number of aborted transactions running in participant",
    yb::MetricUnit::kTransactions);
METRIC_DEFINE_event_stats(tablet, conflict_resolution_latency, "Conflict Resolution Latency",
                               yb::MetricUnit::kMicroseconds,
                               "Microseconds spent on conflict resolution across all "
                               "transactions at the current tablet");
METRIC_DEFINE_event_stats(tablet, conflict_resolution_num_keys_scanned,
                               "Total Keys Scanned During Conflict Resolution",
                               yb::MetricUnit::kKeys,
                               "Number of keys scanned during conflict resolution)");

DEFINE_test_flag(int32, txn_participant_inject_latency_on_apply_update_txn_ms, 0,
                 "How much latency to inject when a update txn operation is applied.");

DEFINE_test_flag(int32, txn_participant_inject_delay_on_start_shutdown_ms, 0,
                 "How much delay to inject before starting participant shutdown.");

namespace yb {
namespace tablet {

namespace {

YB_STRONGLY_TYPED_BOOL(PostApplyCleanup);

} // namespace

using DeadlockInfo = std::pair<Status, CoarseTimePoint>;

constexpr size_t kRunningTransactionSize = sizeof(RunningTransaction);
const std::string kParentMemTrackerId = "Transactions";

std::string TransactionApplyData::ToString() const {
  return YB_STRUCT_TO_STRING(
      leader_term, transaction_id, aborted, op_id, commit_ht, log_ht, sealed, status_tablet,
      apply_state);
}

void UpdateHistoricalMaxOpId(std::atomic<OpId>* historical_max_op_id, OpId const& op_id) {
  OpId prev_value = (*historical_max_op_id);
  while (prev_value < op_id && !historical_max_op_id->compare_exchange_weak(prev_value, op_id)) {
  }
}

class TransactionParticipant::Impl
    : public RunningTransactionContext, public TransactionLoaderContext {
 public:
  Impl(TransactionParticipantContext* context, TransactionIntentApplier* applier,
       const scoped_refptr<MetricEntity>& entity,
       const std::shared_ptr<MemTracker>& tablets_mem_tracker)
      : RunningTransactionContext(context, applier),
        log_prefix_(context->LogPrefix()),
        loader_(this, entity),
        poller_(log_prefix_, std::bind(&Impl::Poll, this)),
        wait_queue_poller_(log_prefix_, std::bind(&Impl::PollWaitQueue, this)) {
    LOG_WITH_PREFIX(INFO) << "Create";
    metric_transactions_running_ = METRIC_transactions_running.Instantiate(entity, 0);
    metric_transaction_not_found_ = METRIC_transaction_not_found.Instantiate(entity);
    metric_aborted_transactions_pending_cleanup_ =
        METRIC_aborted_transactions_pending_cleanup.Instantiate(entity, 0);
    metric_conflict_resolution_latency_ =
        METRIC_conflict_resolution_latency.Instantiate(entity);
    metric_conflict_resolution_num_keys_scanned_ =
        METRIC_conflict_resolution_num_keys_scanned.Instantiate(entity);
    auto parent_mem_tracker = MemTracker::FindOrCreateTracker(
        kParentMemTrackerId, tablets_mem_tracker);
    mem_tracker_ = MemTracker::CreateTracker(Format("$0-$1", kParentMemTrackerId,
        participant_context_.tablet_id()), /* metric_name */ "PerTransaction",
            parent_mem_tracker, AddToParent::kTrue, CreateMetrics::kFalse);
  }

  ~Impl() {
    if (StartShutdown()) {
      CompleteShutdown();
    } else {
      LOG_IF_WITH_PREFIX(DFATAL, !shutdown_done_.load(std::memory_order_acquire))
          << "Destroying transaction participant that did not complete shutdown";
    }
  }

  void SetWaitQueue(std::unique_ptr<docdb::WaitQueue> wait_queue) {
    wait_queue_ = std::move(wait_queue);
  }

  docdb::WaitQueue* wait_queue() const {
    return wait_queue_.get();
  }

  bool StartShutdown() {
    AtomicFlagSleepMs(&FLAGS_TEST_txn_participant_inject_delay_on_start_shutdown_ms);
    bool expected = false;
    if (!closing_.compare_exchange_strong(expected, true)) {
      return false;
    }

    loader_.StartShutdown();

    wait_queue_poller_.Shutdown();
    if (wait_queue_) {
      wait_queue_->StartShutdown();
    }

    if (start_latch_.count()) {
      start_latch_.CountDown();
    }

    shutdown_latch_.Wait();

    poller_.Shutdown();

    LOG_WITH_PREFIX(INFO) << "Shutdown";
    return true;
  }

  void CompleteShutdown() EXCLUDES(mutex_, status_resolvers_mutex_) {
    LOG_IF_WITH_PREFIX(DFATAL, !Closing()) << __func__ << " w/o StartShutdown";

    if (wait_queue_) {
      wait_queue_->CompleteShutdown();
    }

    {
      UniqueLock lock(mutex_);
      WaitOnConditionVariable(
          &requests_completed_cond_, &lock,
          [this] { return running_requests_.empty(); });

      MinRunningNotifier min_running_notifier(nullptr /* applier */);
      DumpClear(RemoveReason::kShutdown);
      transactions_.clear();
      TransactionsModifiedUnlocked(&min_running_notifier);

      mem_tracker_->UnregisterFromParent();
    }

    decltype(status_resolvers_) status_resolvers;
    {
      std::lock_guard lock(status_resolvers_mutex_);
      status_resolvers.swap(status_resolvers_);
    }

    rpcs_.Shutdown();
    loader_.CompleteShutdown();
    for (auto& resolver : status_resolvers) {
      resolver.Shutdown();
    }
    shutdown_done_.store(true, std::memory_order_release);
  }

  bool Closing() const override {
    return closing_.load(std::memory_order_acquire);
  }

  void Start() {
    LOG_WITH_PREFIX(INFO) << "Start";
    start_latch_.CountDown();
  }

  // Adds new running transaction.
  Result<bool> Add(const TransactionMetadata& metadata) {
    RETURN_NOT_OK(loader_.WaitLoaded(metadata.transaction_id));

    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(metadata.transaction_id);
    if (it != transactions_.end()) {
      return false;
    }
    if (WasTransactionRecentlyRemoved(metadata.transaction_id) ||
        cleanup_cache_.Erase(metadata.transaction_id) != 0) {
      RETURN_NOT_OK(GetTransactionDeadlockStatusUnlocked(metadata.transaction_id));
      auto status = STATUS_EC_FORMAT(
          TryAgain, PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE),
          "Transaction was recently aborted: $0", metadata.transaction_id);
      return status.CloneAndAddErrorCode(TransactionError(TransactionErrorCode::kAborted));
    }
    VLOG_WITH_PREFIX(4) << "Create new transaction: " << metadata.transaction_id;

    VLOG_WITH_PREFIX(3) << "Adding a new transaction txn_id: " << metadata.transaction_id
                        << " with begin_time: " << metadata.start_time.ToUint64();

    auto txn = std::make_shared<RunningTransaction>(
        metadata, TransactionalBatchData(), OneWayBitmap(), metadata.start_time, this);

    {
      // Some transactions might not have metadata flushed into intents DB, but may have apply state
      // stored in regular DB. This can only happen during bootstrap.
      auto pending_apply = loader_.GetPendingApply(metadata.transaction_id);
      if (pending_apply) {
        txn->SetLocalCommitData(pending_apply->commit_ht, pending_apply->state.aborted);
        txn->SetApplyData(pending_apply->state);
      }
    }

    transactions_.insert(txn);
    mem_tracker_->Consume(kRunningTransactionSize);
    TransactionsModifiedUnlocked(&min_running_notifier);
    return true;
  }

  HybridTime LocalCommitTime(const TransactionId& id) {
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return HybridTime::kInvalid;
    }
    return (**it).local_commit_time();
  }

  boost::optional<TransactionLocalState> LocalTxnData(const TransactionId& id) {
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return boost::none;
    }
    return boost::make_optional<TransactionLocalState>({
      .commit_ht = (**it).local_commit_time(),
      .aborted_subtxn_set = (**it).last_known_aborted_subtxn_set(),
    });
  }

  Result<std::pair<size_t, size_t>> TEST_CountIntents() {
    {
      MinRunningNotifier min_running_notifier(&applier_);
      std::lock_guard lock(mutex_);
      ProcessRemoveQueueUnlocked(&min_running_notifier);
    }

    std::pair<size_t, size_t> result(0, 0);
    // There is possibility that a shutdown race could happen during this iterating
    // operation in RocksDB. To prevent the race, we should increase the
    // pending_op_counter_blocking_rocksdb_shutdown_start_  before the operation, then shutdown will
    // be delayed if it detects there is still operation hasn't finished yet. In another case, if
    // RocksDB has already been shutdown, the operation will have NOT_OK status.
    ScopedRWOperation operation(pending_op_counter_blocking_rocksdb_shutdown_start_);
    if (!operation.ok()) {
      return STATUS(NotFound, "RocksDB has been shut down.");
    }
    auto iter = docdb::CreateRocksDBIterator(db_.intents,
                                             key_bounds_,
                                             docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                             boost::none,
                                             rocksdb::kDefaultQueryId);
    for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
      ++result.first;
      // Count number of transaction, by counting metadata records.
      if (iter.key().size() == TransactionId::StaticSize() + 1) {
        ++result.second;
        auto key = iter.key();
        key.remove_prefix(1);
        auto id = CHECK_RESULT(FullyDecodeTransactionId(key));
        LOG_WITH_PREFIX(INFO) << "Stored txn meta: " << id;
      }
    }
    RETURN_NOT_OK(iter.status());

    return result;
  }

  template <class PB>
  Result<TransactionMetadata> PrepareMetadata(const PB& pb) {
    if (pb.has_isolation()) {
      auto metadata = VERIFY_RESULT(TransactionMetadata::FromPB(pb));
      std::unique_lock<std::mutex> lock(mutex_);
      auto it = transactions_.find(metadata.transaction_id);
      if (it != transactions_.end()) {
        RETURN_NOT_OK((**it).CheckAborted());
      } else if (WasTransactionRecentlyRemoved(metadata.transaction_id)) {
        RETURN_NOT_OK(GetTransactionDeadlockStatusUnlocked(metadata.transaction_id));
        return MakeAbortedStatus(metadata.transaction_id);
      }
      return metadata;
    }

    auto id = VERIFY_RESULT(FullyDecodeTransactionId(pb.transaction_id()));

    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents or not.
    auto lock_and_iterator = VERIFY_RESULT(LockAndFind(
        id, "metadata"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist}));
    if (!lock_and_iterator.found()) {
      return lock_and_iterator.did_txn_deadlock()
          ? lock_and_iterator.deadlock_status
          : STATUS(TryAgain,
                   Format("Unknown transaction, could be recently aborted: $0", id), Slice(),
                   PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE));
    }
    RETURN_NOT_OK(lock_and_iterator.transaction().CheckAborted());
    return lock_and_iterator.transaction().metadata();
  }

  Result<boost::optional<std::pair<IsolationLevel, TransactionalBatchData>>> PrepareBatchData(
      const TransactionId& id, size_t batch_idx,
      boost::container::small_vector_base<uint8_t>* encoded_replicated_batches) {
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents of not.
    auto lock_and_iterator = VERIFY_RESULT(LockAndFind(
        id, "metadata with write id"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist}));
    if (!lock_and_iterator.found()) {
      if (lock_and_iterator.did_txn_deadlock()) {
        return lock_and_iterator.deadlock_status;
      }
      return boost::none;
    }
    auto& transaction = lock_and_iterator.transaction();
    transaction.AddReplicatedBatch(batch_idx, encoded_replicated_batches);
    return std::make_pair(transaction.metadata().isolation, transaction.last_batch_data());
  }

  void BatchReplicated(const TransactionId& id, const TransactionalBatchData& data) {
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      LOG_IF_WITH_PREFIX(DFATAL, !WasTransactionRecentlyRemoved(id))
          << "Update last write id for unknown transaction: " << id;
      return;
    }
    (**it).BatchReplicated(data);
  }

  void RequestStatusAt(const StatusRequest& request) {
    auto lock_and_iterator_result = LockAndFind(
        *request.id, *request.reason, request.flags);
    if (!lock_and_iterator_result.ok()) {
      request.callback(lock_and_iterator_result.status());
      return;
    }
    if (!lock_and_iterator_result->found()) {
      if (lock_and_iterator_result->did_txn_deadlock()) {
        request.callback(
            TransactionStatusResult::Deadlocked(lock_and_iterator_result->deadlock_status));
      } else {
        request.callback(
            STATUS_FORMAT(NotFound, "Request status of unknown transaction: $0", *request.id));
      }
      return;
    }
    lock_and_iterator_result->transaction().RequestStatusAt(
        request, &lock_and_iterator_result->lock);
  }

  // Registers a request, giving it a newly allocated id and returning this id.
  Result<int64_t> RegisterRequest() {
    if (Closing()) {
      LOG_WITH_PREFIX(INFO) << "Closing, not allow request to be registered";
      return STATUS_FORMAT(ShutdownInProgress, "Tablet is shutting down");
    }

    std::lock_guard lock(mutex_);
    auto result = NextRequestIdUnlocked();
    running_requests_.push_back(result);
    return result;
  }

  // Unregisters a previously registered request.
  void UnregisterRequest(int64_t request) {
    MinRunningNotifier min_running_notifier(&applier_);
    bool notify_completed;
    {
      std::lock_guard lock(mutex_);
      DCHECK(!running_requests_.empty());
      if (running_requests_.front() != request) {
        complete_requests_.push(request);
        return;
      }
      running_requests_.pop_front();
      while (!complete_requests_.empty() && complete_requests_.top() == running_requests_.front()) {
        complete_requests_.pop();
        running_requests_.pop_front();
      }

      notify_completed = running_requests_.empty() && Closing();

      CleanTransactionsUnlocked(&min_running_notifier);
    }

    if (notify_completed) {
      requests_completed_cond_.notify_all();
    }
  }

  // Cleans the intents those are consumed by consumers.
  void SetIntentRetainOpIdAndTime(const OpId& op_id, const MonoDelta& cdc_sdk_op_id_expiration) {
    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    VLOG(1) << "Setting RetainOpId: " << op_id
            << ", previous cdc_sdk_min_checkpoint_op_id_: " << cdc_sdk_min_checkpoint_op_id_;

    cdc_sdk_min_checkpoint_op_id_expiration_ = CoarseMonoClock::now() + cdc_sdk_op_id_expiration;
    cdc_sdk_min_checkpoint_op_id_ = op_id;

    // If new op_id same as  cdc_sdk_min_checkpoint_op_id_ it means already intent before it are
    // already cleaned up, so no need call clean transactions, else call clean the transactions.
    CleanTransactionsUnlocked(&min_running_notifier);
  }

  OpId GetLatestCheckPoint() REQUIRES(mutex_) {
    return GetLatestCheckPointUnlocked();
  }

  OpId GetLatestCheckPointUnlocked() {
    OpId min_checkpoint;
    if (CoarseMonoClock::Now() < cdc_sdk_min_checkpoint_op_id_expiration_ &&
        cdc_sdk_min_checkpoint_op_id_ != OpId::Invalid()) {
      min_checkpoint = cdc_sdk_min_checkpoint_op_id_;
    } else {
      VLOG(1) << "Tablet peer checkpoint is expired with the current time: "
              << ToSeconds(CoarseMonoClock::Now().time_since_epoch()) << " expiration time: "
              << ToSeconds(cdc_sdk_min_checkpoint_op_id_expiration_.time_since_epoch())
              << " checkpoint op_id: " << cdc_sdk_min_checkpoint_op_id_;
      min_checkpoint = OpId::Max();
    }
    return min_checkpoint;
  }

  HybridTime GetMinStartTimeAmongAllRunningTransactions() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& transaction : transactions_.get<StartTimeTag>()) {
      auto const& transaction_status = transaction->last_known_status();
      if (transaction->local_commit_time() == HybridTime::kInvalid &&
          transaction_status != TransactionStatus::ABORTED) {
        return transaction->start_ht();
      }
    }

    return HybridTime::kInvalid;
  }

  OpId GetHistoricalMaxOpId() {
    return historical_max_op_id.load();
  }

  OpId GetRetainOpId() {
    std::lock_guard lock(mutex_);
    return cdc_sdk_min_checkpoint_op_id_;
  }

  CoarseTimePoint GetCheckpointExpirationTime() {
    std::lock_guard lock(mutex_);
    return cdc_sdk_min_checkpoint_op_id_expiration_;
  }

  // Cleans transactions that are requested and now is safe to clean.
  // See RemoveUnlocked for details.
  void CleanTransactionsUnlocked(MinRunningNotifier* min_running_notifier) REQUIRES(mutex_) {
    ProcessRemoveQueueUnlocked(min_running_notifier);

    CleanTransactionsQueue(&immediate_cleanup_queue_, min_running_notifier);
    CleanTransactionsQueue(&graceful_cleanup_queue_, min_running_notifier);
  }

  template <class Queue>
  void CleanTransactionsQueue(
      Queue* queue, MinRunningNotifier* min_running_notifier) REQUIRES(mutex_) {
    int64_t min_request = running_requests_.empty() ? std::numeric_limits<int64_t>::max()
                                                    : running_requests_.front();
    HybridTime safe_time;
    OpId checkpoint_op_id = GetLatestCheckPoint();
    while (!queue->empty()) {
      const auto& front = queue->front();
      if (front.request_id >= min_request) {
        break;
      }
      if (!front.Ready(&participant_context_, &safe_time)) {
        break;
      }
      const auto& id = front.transaction_id;
      auto it = transactions_.find(id);

      if (it != transactions_.end() && !(**it).ProcessingApply()) {
        OpId op_id = (**it).GetApplyOpId();

        // If transaction op_id is greater than the CDCSDK checkpoint op_id.
        // don't clean the intent as well as intent after this.
        if (op_id > checkpoint_op_id) {
          break;
        }
        VLOG_WITH_PREFIX(2) << "Cleaning txn apply opid is: " << op_id.ToString()
                            << " checkpoint opid is: " << checkpoint_op_id.ToString()
                            << " txn id: " << id;
        (**it).ScheduleRemoveIntents(*it, front.reason);
        RemoveTransaction(it, front.reason, min_running_notifier, front.expected_deadlock_status);
      }
      VLOG_WITH_PREFIX(2) << "Cleaned from queue: " << id;
      queue->pop_front();
    }
  }

  void NotifyAbortedTransactionIncrement (const TransactionId& id) override {
    VLOG_WITH_PREFIX(4) << "Transaction: " << id << " is aborted";
    metric_aborted_transactions_pending_cleanup_->Increment();
  }

  void NotifyAbortedTransactionDecrement (const TransactionId& id) override {
    VLOG_WITH_PREFIX(4) << "Aborted Transaction: " << id << " is removed";
    metric_aborted_transactions_pending_cleanup_->Decrement();
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) {
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents of not.
    auto lock_and_iterator_result = LockAndFind(
        id, "abort"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist});
    if (!lock_and_iterator_result.ok()) {
      callback(lock_and_iterator_result.status());
      return;
    }
    if (!lock_and_iterator_result->found()) {
      callback(STATUS_FORMAT(NotFound, "Abort of unknown transaction: $0", id));
      return;
    }
    auto client_result = participant_context_.client();
    if (!client_result.ok()) {
      callback(client_result.status());
      return;
    }
    lock_and_iterator_result->transaction().Abort(
        *client_result, std::move(callback), &lock_and_iterator_result->lock);
  }

  Status CheckAborted(const TransactionId& id) {
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents of not.
    auto lock_and_iterator =
        VERIFY_RESULT(LockAndFind(id, "check aborted"s, TransactionLoadFlags{}));
    if (!lock_and_iterator.found()) {
      return lock_and_iterator.did_txn_deadlock()
          ? lock_and_iterator.deadlock_status
          : MakeAbortedStatus(id);
    }
    return lock_and_iterator.transaction().CheckAborted();
  }

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) {
    // TODO(dtxn) optimize locking
    for (auto& pair : *inout) {
      auto lock_and_iterator = VERIFY_RESULT(LockAndFind(
          pair.first, "fill priorities"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist}));
      if (!lock_and_iterator.found() || lock_and_iterator.transaction().WasAborted()) {
        pair.second = 0; // Minimal priority for already aborted transactions
      } else {
        pair.second = lock_and_iterator.transaction().metadata().priority;
      }
    }
    return Status::OK();
  }

  Result<boost::optional<TabletId>> GetStatusTablet(const TransactionId& id) {
    auto lock_and_iterator =
        VERIFY_RESULT(LockAndFind(id, "get status tablet"s, TransactionLoadFlags{}));
    if (!lock_and_iterator.found() || lock_and_iterator.transaction().WasAborted()) {
      return boost::none;
    }
    return lock_and_iterator.transaction().status_tablet();
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperation> operation, int64_t term) {
    auto txn_status = operation->request()->status();
    if (txn_status == TransactionStatus::APPLYING) {
      HandleApplying(std::move(operation), term);
      return;
    }

    if (txn_status == TransactionStatus::IMMEDIATE_CLEANUP ||
        txn_status == TransactionStatus::GRACEFUL_CLEANUP) {
      auto cleanup_type = txn_status == TransactionStatus::IMMEDIATE_CLEANUP
          ? CleanupType::kImmediate
          : CleanupType::kGraceful;
      HandleCleanup(std::move(operation), term, cleanup_type);
      return;
    }

    auto error_status = STATUS_FORMAT(
        InvalidArgument, "Unexpected status in transaction participant Handle: $0", *operation);
    LOG_WITH_PREFIX(DFATAL) << error_status;
    operation->CompleteWithStatus(error_status);
  }

  Status ProcessReplicated(const ReplicatedData& data) {
    if (FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms > 0) {
      SleepFor(1ms * FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
    }

    auto id = FullyDecodeTransactionId(data.state.transaction_id());
    if (!id.ok()) {
      LOG(ERROR) << "Could not decode transaction details, whose apply record OpId was: "
                 << data.op_id;
      return id.status();
    }

    if (data.state.status() == TransactionStatus::APPLYING) {
      return ReplicatedApplying(*id, data);
    } else if (data.state.status() == TransactionStatus::ABORTED) {
      return ReplicatedAborted(*id, data);
    }

    auto status = STATUS_FORMAT(
        InvalidArgument, "Unexpected status in transaction participant ProcessReplicated: $0, $1",
        data.op_id, data.state);
    LOG_WITH_PREFIX(DFATAL) << status;
    return status;
  }

  Status Cleanup(TransactionIdSet&& set, TransactionStatusManager* status_manager) {
    {
      std::lock_guard lock(mutex_);
      const OpId& cdcsdk_checkpoint_op_id = GetLatestCheckPoint();

      if (cdcsdk_checkpoint_op_id != OpId::Max()) {
        for (auto t_iter = set.begin(); t_iter != set.end();) {
          const TransactionId& transaction_id = *t_iter;
          RETURN_NOT_OK(loader_.WaitLoaded(transaction_id));
          auto iter = transactions_.find(transaction_id);
          if (iter == transactions_.end()) {
            ++t_iter;
            continue;
          }

          const OpId& apply_record_op_id = (**iter).GetApplyOpId();
          if (apply_record_op_id > cdcsdk_checkpoint_op_id) {
            t_iter = set.erase(t_iter);
            VLOG_WITH_PREFIX(2)
                << "Transaction not yet reported to CDCSDK client, should not cleanup."
                << "TransactionId: " << transaction_id
                << ", apply record opId: " << apply_record_op_id
                << ", cdcsdk checkpoint opId: " << cdcsdk_checkpoint_op_id;
          } else {
            ++t_iter;
          }
        }
      }
    }

    auto cleanup_aborts_task = std::make_shared<CleanupAbortsTask>(
        &applier_, std::move(set), &participant_context_, status_manager, LogPrefix());
    cleanup_aborts_task->Prepare(cleanup_aborts_task);
    participant_context_.StrandEnqueue(cleanup_aborts_task.get());
    return Status::OK();
  }

  Status ProcessApply(const TransactionApplyData& data) {
    VLOG_WITH_PREFIX(2) << "Apply: " << data.ToString();

    RETURN_NOT_OK(loader_.WaitLoaded(data.transaction_id));

    ScopedRWOperation operation(pending_op_counter_blocking_rocksdb_shutdown_start_);
    if (!operation.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Process apply rejected";
      return Status::OK();
    }

    bool was_previously_committed = false;

    {
      // It is our last chance to load transaction metadata, if missing.
      // Because it will be deleted when intents are applied.
      // We are not trying to cleanup intents here because we don't know whether this transaction
      // has intents of not.
      auto lock_and_iterator = VERIFY_RESULT(LockAndFind(
          data.transaction_id, "pre apply"s,
          TransactionLoadFlags{TransactionLoadFlag::kMustExist}));
      if (!lock_and_iterator.found()) {
        // This situation is normal and could be caused by 2 scenarios:
        // 1) Write batch failed, but originator doesn't know that.
        // 2) Failed to notify status tablet that we applied transaction.
        YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 1)
            << Format("Apply of unknown transaction: $0", data);
        NotifyApplied(data);
        CHECK(!FLAGS_TEST_fail_in_apply_if_no_metadata);
        return Status::OK();
      }

      auto existing_commit_ht = lock_and_iterator.transaction().local_commit_time();
      if (existing_commit_ht) {
        was_previously_committed = true;
        LOG_WITH_PREFIX(INFO) << "Transaction already applied: " << data.transaction_id;
        LOG_IF_WITH_PREFIX(DFATAL, data.commit_ht != existing_commit_ht)
            << "Transaction was previously applied with another commit ht: " << existing_commit_ht
            << ", new commit ht: " << data.commit_ht;
      } else {
        UpdateHistoricalMaxOpId(&historical_max_op_id, data.op_id);
        VLOG_WITH_PREFIX(3) << "Committed a transaction with txn_id: " << data.transaction_id
                            << " with commit_time: " << data.commit_ht.ToUint64();
        CHECK(transactions_.modify(lock_and_iterator.iterator, [&data](auto& txn) {
          txn->SetLocalCommitData(data.commit_ht, data.aborted);
        }));
        LOG_IF_WITH_PREFIX(DFATAL, data.log_ht < last_safe_time_)
            << "Apply transaction before last safe time " << data.transaction_id << ": "
            << data.log_ht << " vs " << last_safe_time_;
      }
    }

    if (!was_previously_committed) {
      if (wait_queue_) {
        // We signal this commit to the wait queue if it is newly committed. It's important to do so
        // *after* the local running transaction's metadata is updated to indicate that this was
        // committed. Otherwise, the wait queue will resume any waiters blocked on this transaction,
        // and they may re-run conflict resolution and not find that the transaction is committed,
        // forcing them to re-enter the wait queue.
        // TODO(wait-queues): Consider signaling before replicating the transaction update.
        wait_queue_->SignalCommitted(data.transaction_id, data.commit_ht);
      }
      auto apply_state = CHECK_RESULT(applier_.ApplyIntents(data));

      VLOG_WITH_PREFIX(4) << "TXN: " << data.transaction_id << ": apply state: "
                          << apply_state.ToString();

      RETURN_NOT_OK(UpdateAppliedTransaction(data, apply_state, &operation));
    }

    NotifyApplied(data);
    return Status::OK();
  }

  Status UpdateAppliedTransaction(
       const TransactionApplyData& data,
       const docdb::ApplyTransactionState& apply_state,
       ScopedRWOperation* operation) NO_THREAD_SAFETY_ANALYSIS {
    MinRunningNotifier min_running_notifier(&applier_);
    // We are not trying to cleanup intents here because we don't know whether this transaction
    // has intents or not.
    auto lock_and_iterator = VERIFY_RESULT(LockAndFind(
        data.transaction_id, "apply"s, TransactionLoadFlags{TransactionLoadFlag::kMustExist}));
    if (lock_and_iterator.found()) {
      lock_and_iterator.transaction().SetApplyOpId(data.op_id);
      if (!apply_state.active()) {
        RemoveUnlocked(
            lock_and_iterator.iterator, RemoveReason::kApplied, &min_running_notifier);
      } else {
        lock_and_iterator.transaction().SetApplyData(apply_state, &data, operation);
      }
    }
    return Status::OK();
  }

  void NotifyApplied(const TransactionApplyData& data) {
    VLOG_WITH_PREFIX(4) << Format("NotifyApplied($0)", data);

    NotifyApplied(data.leader_term, data.status_tablet, data.transaction_id);
  }

  void NotifyApplied(int64_t leader_term, const TabletId& status_tablet,
                     const TransactionId& transaction_id) {
    if (leader_term == OpId::kUnknownTerm) {
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet);
    req.set_propagated_hybrid_time(participant_context_.Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(transaction_id.data(), transaction_id.size());
    state.set_status(TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS);
    state.add_tablets(participant_context_.tablet_id());
    auto client_result = participant_context_.client();
    if (!client_result.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Get client failed: " << client_result.status();
      return;
    }

    auto handle = rpcs_.Prepare();
    if (handle != rpcs_.InvalidHandle()) {
      *handle = UpdateTransaction(
          TransactionRpcDeadline(),
          nullptr /* remote_tablet */,
          *client_result,
          &req,
          [this, handle](const Status& status,
                         const tserver::UpdateTransactionRequestPB& req,
                         const tserver::UpdateTransactionResponsePB& resp) {
            client::UpdateClock(resp, &participant_context_);
            rpcs_.Unregister(handle);
            LOG_IF_WITH_PREFIX(WARNING, !status.ok()) << "Failed to send applied: " << status;
          });
      (**handle).SendRpc();
    }
  }

  Status ProcessCleanup(const TransactionApplyData& data, CleanupType cleanup_type) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(data) << ", " << AsString(cleanup_type);

    RETURN_NOT_OK(loader_.WaitLoaded(data.transaction_id));

    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(data.transaction_id);
    if (it == transactions_.end()) {
      if (cleanup_type == CleanupType::kImmediate) {
        cleanup_cache_.Insert(data.transaction_id);
        return Status::OK();
      }
    } else {
      // Make sure it's a committed transaction.
      if (data.commit_ht.is_valid()) {
        UpdateHistoricalMaxOpId(&historical_max_op_id, data.op_id);
      }
      CHECK(transactions_.modify(it, [&data](auto& txn) {
        txn->SetApplyOpId(data.op_id);
      }));

      if ((**it).ProcessingApply()) {
        VLOG_WITH_PREFIX(2) << "Don't cleanup transaction because it is applying intents: "
                            << data.transaction_id;
        return Status::OK();
      }
    }

    if (cleanup_type == CleanupType::kGraceful) {
      graceful_cleanup_queue_.push_back(GracefulCleanupQueueEntry{
        .request_id = request_serial_,
        .transaction_id = data.transaction_id,
        .reason = RemoveReason::kProcessCleanup,
        .required_safe_time = participant_context_.Now(),
      });
      return Status::OK();
    }

    auto did_remove = RemoveUnlocked(it, RemoveReason::kProcessCleanup, &min_running_notifier);
    if (!did_remove) {
      VLOG_WITH_PREFIX(2) << "Have added aborted txn to cleanup queue: "
                          << data.transaction_id;
    }

    return Status::OK();
  }

  Status SetDB(
      const docdb::DocDB& db, const docdb::KeyBounds* key_bounds,
      RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start) {
    bool had_db = db_.intents != nullptr;
    db_ = db;
    key_bounds_ = key_bounds;
    pending_op_counter_blocking_rocksdb_shutdown_start_ =
        pending_op_counter_blocking_rocksdb_shutdown_start;

    // We should only load transactions on the initial call to SetDB (when opening the tablet), not
    // in case of truncate/restore.
    if (!had_db) {
      loader_.Start(pending_op_counter_blocking_rocksdb_shutdown_start, db_);
      return Status::OK();
    }

    RETURN_NOT_OK(loader_.WaitAllLoaded());
    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    DumpClear(RemoveReason::kSetDB);
    transactions_.clear();
    mem_tracker_->Release(mem_tracker_->consumption());
    TransactionsModifiedUnlocked(&min_running_notifier);
    return Status::OK();
  }

  void GetStatus(
      const TransactionId& transaction_id,
      size_t required_num_replicated_batches,
      int64_t term,
      tserver::GetTransactionStatusAtParticipantResponsePB* response,
      rpc::RpcContext* context) {
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(transaction_id);
    if (it == transactions_.end()) {
      response->set_num_replicated_batches(0);
      response->set_status_hybrid_time(0);
    } else {
      if ((**it).WasAborted()) {
        response->set_aborted(true);
        return;
      }
      response->set_num_replicated_batches((**it).num_replicated_batches());
      response->set_status_hybrid_time((**it).last_batch_data().hybrid_time.ToUint64());
    }
  }

  TransactionParticipantContext* participant_context() const {
    return &participant_context_;
  }

  HybridTime MinRunningHybridTime() {
    auto result = min_running_ht_.load(std::memory_order_acquire);
    if (result == HybridTime::kMax || result == HybridTime::kInvalid) {
      return result;
    }
    auto now = CoarseMonoClock::now();
    auto current_next_check_min_running = next_check_min_running_.load(std::memory_order_relaxed);
    if (now >= current_next_check_min_running) {
      if (next_check_min_running_.compare_exchange_strong(
              current_next_check_min_running,
              now + 1ms * FLAGS_transaction_min_running_check_interval_ms,
              std::memory_order_acq_rel)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (transactions_.empty()) {
          return HybridTime::kMax;
        }
        auto& first_txn = **transactions_.get<StartTimeTag>().begin();
        VLOG_WITH_PREFIX(1) << "Checking status of long running min txn " << first_txn.id()
                            << ": " << first_txn.WasAborted();
        static const std::string kRequestReason = "min running check"s;
        // Get transaction status
        auto now_ht = participant_context_.Now();
        StatusRequest status_request = {
            .id = &first_txn.id(),
            .read_ht = now_ht,
            .global_limit_ht = now_ht,
            // Could use 0 here, because read_ht == global_limit_ht.
            // So we cannot accept status with time >= read_ht and < global_limit_ht.
            .serial_no = 0,
            .reason = &kRequestReason,
            .flags = TransactionLoadFlags{},
            .callback = [this, id = first_txn.id()](Result<TransactionStatusResult> result) {
              // Aborted status will result in cleanup of intents.
              VLOG_WITH_PREFIX(1) << "Min running status " << id << ": " << result;
            }
        };
        first_txn.RequestStatusAt(status_request, &lock);
      }
    }
    return result;
  }

  void WaitMinRunningHybridTime(HybridTime ht) {
    MinRunningNotifier min_running_notifier(&applier_);
    std::unique_lock<std::mutex> lock(mutex_);
    waiting_for_min_running_ht_ = ht;
    CheckMinRunningHybridTimeSatisfiedUnlocked(&min_running_notifier);
  }

  Status ResolveIntents(HybridTime resolve_at, CoarseTimePoint deadline) {
    RETURN_NOT_OK(WaitUntil(participant_context_.clock_ptr().get(), resolve_at, deadline));

    if (FLAGS_max_transactions_in_status_request == 0) {
      return STATUS(
          IllegalState,
          "Cannot resolve intents when FLAGS_max_transactions_in_status_request is zero");
    }

    std::vector<TransactionId> recheck_ids, committed_ids;

    // Maintain a set of transactions, check their statuses, and remove them as they get
    // committed/applied, aborted or we realize that transaction was not committed at
    // resolve_at.
    for (;;) {
      TransactionStatusResolver resolver(
          &participant_context_, &rpcs_, FLAGS_max_transactions_in_status_request,
          [this, resolve_at, &recheck_ids, &committed_ids](
              const std::vector <TransactionStatusInfo>& status_infos) {
            std::vector<TransactionStatusInfo> aborted;
            for (const auto& info : status_infos) {
              VLOG_WITH_PREFIX(4) << "Transaction status: " << info.ToString();
              if (info.status == TransactionStatus::COMMITTED) {
                if (info.status_ht <= resolve_at) {
                  // Transaction was committed, but not yet applied.
                  // So rely on filtering recheck_ids before next phase.
                  committed_ids.push_back(info.transaction_id);
                }
              } else if (info.status == TransactionStatus::ABORTED) {
                aborted.push_back(info);
              } else {
                LOG_IF_WITH_PREFIX(DFATAL, info.status != TransactionStatus::PENDING)
                    << "Transaction is in unexpected state: " << info.ToString();
                if (info.status_ht <= resolve_at) {
                  recheck_ids.push_back(info.transaction_id);
                }
              }
            }
            if (!aborted.empty()) {
              MinRunningNotifier min_running_notifier(&applier_);
              std::lock_guard lock(mutex_);
              for (const auto& info : aborted) {
                // TODO: Refactor so that the clean up code can use the established iterator
                // instead of executing find again.
                auto it = transactions_.find(info.transaction_id);
                if (it != transactions_.end() && (*it)->status_tablet() != info.status_tablet) {
                  VLOG_WITH_PREFIX(2) << "Dropping Aborted status for txn "
                                      << info.transaction_id.ToString()
                                      << " from old status tablet " << info.status_tablet
                                      << ". New status tablet " << (*it)->status_tablet();
                  continue;
                }
                EnqueueRemoveUnlocked(
                    info.transaction_id, RemoveReason::kStatusReceived, &min_running_notifier,
                    info.expected_deadlock_status);
              }
            }
          });
      auto se = ScopeExit([&resolver] {
        resolver.Shutdown();
      });
      {
        std::lock_guard <std::mutex> lock(mutex_);
        if (recheck_ids.empty() && committed_ids.empty()) {
          // First step, check all transactions.
          for (const auto& transaction : transactions_) {
            if (!transaction->local_commit_time().is_valid()) {
              resolver.Add(transaction->metadata().status_tablet, transaction->id());
            }
          }
        } else {
          for (const auto& id : recheck_ids) {
            auto it = transactions_.find(id);
            if (it == transactions_.end() || (**it).local_commit_time().is_valid()) {
              continue;
            }
            resolver.Add((**it).metadata().status_tablet, id);
          }
          auto filter = [this](const TransactionId& id) {
            auto it = transactions_.find(id);
            return it == transactions_.end() || (**it).local_commit_time().is_valid();
          };
          committed_ids.erase(std::remove_if(committed_ids.begin(), committed_ids.end(), filter),
                              committed_ids.end());
        }
      }

      recheck_ids.clear();
      resolver.Start(deadline);

      RETURN_NOT_OK(resolver.ResultFuture().get());

      if (recheck_ids.empty()) {
        if (committed_ids.empty()) {
          break;
        } else {
          // We are waiting only for committed transactions to be applied.
          // So just add some delay.
          std::this_thread::sleep_for(10ms * std::min<size_t>(10, committed_ids.size()));
        }
      }
    }

    return Status::OK();
  }

  size_t GetNumRunningTransactions() {
    std::lock_guard lock(mutex_);
    auto txn_to_id = [](const RunningTransactionPtr& txn) {
      return txn->id();
    };
    VLOG_WITH_PREFIX(4) << "Transactions: " << AsString(transactions_, txn_to_id)
                        << ", requests: " << AsString(running_requests_);
    return transactions_.size();
  }

  OneWayBitmap TEST_TransactionReplicatedBatches(const TransactionId& id) {
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(id);
    return it != transactions_.end() ? (**it).replicated_batches() : OneWayBitmap();
  }

  std::string DumpTransactions() {
    std::string result;
    std::lock_guard lock(mutex_);

    result += Format(
        "{ safe_time_for_participant: $0 remove_queue_size: $1 ",
        participant_context_.SafeTimeForTransactionParticipant(), remove_queue_.size());
    if (!remove_queue_.empty()) {
      result += "remove_queue_front: " + AsString(remove_queue_.front());
    }
    if (!running_requests_.empty()) {
      result += "running_requests_front: " + AsString(running_requests_.front());
    }
    result += "}\n";

    for (const auto& txn : transactions_.get<StartTimeTag>()) {
      result += txn->ToString();
      result += "\n";
    }
    return result;
  }

  Status StopActiveTxnsPriorTo(
      HybridTime cutoff, CoarseTimePoint deadline, TransactionId* exclude_txn_id) {
    vector<TransactionId> ids_to_abort;
    {
      std::lock_guard lock(mutex_);
      for (const auto& txn : transactions_.get<StartTimeTag>()) {
        if (txn->start_ht() > cutoff ||
            (exclude_txn_id != nullptr && txn->id() == *exclude_txn_id)) {
          break;
        }
        if (!txn->WasAborted()) {
          ids_to_abort.push_back(txn->id());
        }
      }
    }

    if (ids_to_abort.empty()) {
      return Status::OK();
    }

    // It is ok to attempt to abort txns that have committed. We don't care
    // if our request succeeds or not.
    CountDownLatch latch(ids_to_abort.size());
    std::atomic<bool> failed{false};
    Status return_status = Status::OK();
    for (const auto& id : ids_to_abort) {
      Abort(
          id, [this, id, &failed, &return_status, &latch](Result<TransactionStatusResult> result) {
            VLOG_WITH_PREFIX(2) << "Aborting " << id << " got " << result;
            if (!result ||
                (result->status != TransactionStatus::COMMITTED && result->status != ABORTED)) {
              LOG(INFO) << "Could not abort " << id << " got " << result;

              bool expected = false;
              if (failed.compare_exchange_strong(expected, true)) {
                if (!result) {
                  return_status = result.status();
                } else {
                  return_status =
                      STATUS_FORMAT(IllegalState, "Wrong status after abort: $0", result->status);
                }
              }
            }
            latch.CountDown();
          });
    }

    return latch.WaitUntil(deadline) ? return_status
                                     : STATUS(TimedOut, "TimedOut while aborting old transactions");
  }

  Result<HybridTime> WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) {
    return participant_context_.WaitForSafeTime(safe_time, deadline);
  }

  void IgnoreAllTransactionsStartedBefore(HybridTime limit) {
    std::lock_guard lock(mutex_);
    ignore_all_transactions_started_before_ =
        std::max(ignore_all_transactions_started_before_, limit);
  }

  Result<TransactionMetadata> UpdateTransactionStatusLocation(
      const TransactionId& transaction_id, const TabletId& new_status_tablet) {
    RETURN_NOT_OK(loader_.WaitLoaded(transaction_id));
    MinRunningNotifier min_running_notifier(&applier_);

    TransactionMetadata metadata;
    TransactionStatusResult txn_status_res;
    {
      std::lock_guard lock(mutex_);
      auto it = transactions_.find(transaction_id);
      if (it == transactions_.end()) {
        // This case may happen if the transaction gets aborted due to conflict or expired
        // before the update RPC is received.
        YB_LOG_WITH_PREFIX_HIGHER_SEVERITY_WHEN_TOO_MANY(INFO, WARNING, 1s, 50)
            << "Request to unknown transaction " << transaction_id;
        return STATUS_EC_FORMAT(
            Expired, PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE),
            "Transaction $0 expired or aborted by a conflict", transaction_id);
      }

      auto& transaction = *it;
      RETURN_NOT_OK_SET_CODE(transaction->CheckPromotionAllowed(),
                             PgsqlError(YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE));
      metadata = transaction->metadata();
      VLOG_WITH_PREFIX(2) << "Update transaction status location for transaction: "
                          << metadata.transaction_id << " from tablet " << metadata.status_tablet
                          << " to " << new_status_tablet;
      transaction->UpdateTransactionStatusLocation(new_status_tablet);
      TransactionsModifiedUnlocked(&min_running_notifier);
      txn_status_res = TransactionStatusResult{
          TransactionStatus::PROMOTED, transaction->last_known_status_hybrid_time(),
          transaction->last_known_aborted_subtxn_set(), new_status_tablet};
    }

    if (wait_queue_) {
      wait_queue_->SignalPromoted(transaction_id, std::move(txn_status_res));
    }
    return std::move(metadata);
  }

  void RecordConflictResolutionKeysScanned(int64_t num_keys) {
    metric_conflict_resolution_num_keys_scanned_->Increment(num_keys);
  }

  void RecordConflictResolutionScanLatency(MonoDelta latency) {
    metric_conflict_resolution_latency_->Increment(latency.ToMilliseconds());
  }

 private:
  class AbortCheckTimeTag;
  class StartTimeTag;

  typedef boost::multi_index_container<RunningTransactionPtr,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun <
                  RunningTransaction, const TransactionId&, &RunningTransaction::id>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<StartTimeTag>,
              boost::multi_index::const_mem_fun <
                  RunningTransaction, HybridTime, &RunningTransaction::start_ht>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<AbortCheckTimeTag>,
              boost::multi_index::const_mem_fun <
                  RunningTransaction, HybridTime, &RunningTransaction::abort_check_ht>
          >
      >
  > Transactions;

  void CompleteLoad(const std::function<void()>& functor) override {
    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    functor();
    TransactionsModifiedUnlocked(&min_running_notifier);
  }

  void LoadFinished() EXCLUDES(status_resolvers_mutex_) override {
    // The start_latch will be hit either from a CountDown from Start, or from Shutdown, so make
    // sure that at the end of Load, we unblock shutdown.
    auto se = ScopeExit([&] {
      shutdown_latch_.CountDown();
    });
    start_latch_.Wait();
    std::vector<ScopedRWOperation> operations;
    auto pending_applies = loader_.MovePendingApplies();
    operations.reserve(pending_applies.size());
    for (;;) {
      if (Closing()) {
        LOG_WITH_PREFIX(INFO)
            << __func__ << ": closing, not starting transaction status resolution";
        return;
      }
      while (operations.size() < pending_applies.size()) {
        ScopedRWOperation operation(pending_op_counter_blocking_rocksdb_shutdown_start_);
        if (!operation.ok()) {
          break;
        }
        operations.push_back(std::move(operation));
      }
      if (operations.size() == pending_applies.size()) {
        break;
      }
      operations.clear();
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 5)
          << __func__ << ": unable to start scoped RW operation";
      std::this_thread::sleep_for(10ms);
    }

    if (!pending_applies.empty()) {
      LOG_WITH_PREFIX(INFO)
          << __func__ << ": starting " << pending_applies.size() << " pending applies";
      std::lock_guard lock(mutex_);
      size_t idx = 0;
      for (const auto& p : pending_applies) {
        auto it = transactions_.find(p.first);
        if (it == transactions_.end()) {
          LOG_WITH_PREFIX(INFO) << "Unknown transaction for pending apply: " << AsString(p.first);
          continue;
        }

        TransactionApplyData apply_data;
        apply_data.transaction_id = p.first;
        apply_data.commit_ht = p.second.commit_ht;
        (**it).SetApplyData(p.second.state, &apply_data, &operations[idx]);
        ++idx;
      }
    }

    {
      LOG_WITH_PREFIX(INFO) << __func__ << ": starting transaction status resolution";
      std::lock_guard lock(status_resolvers_mutex_);
      for (auto& status_resolver : status_resolvers_) {
        status_resolver.Start(CoarseTimePoint::max());
      }
    }

    poller_.Start(
        &participant_context_.scheduler(), 1ms * FLAGS_transactions_status_poll_interval_ms);

    if (wait_queue_) {
      wait_queue_poller_.Start(
        &participant_context_.scheduler(), 1ms * FLAGS_wait_queue_poll_interval_ms);
    }
  }

  void TransactionsModifiedUnlocked(MinRunningNotifier* min_running_notifier) REQUIRES(mutex_) {
    metric_transactions_running_->set_value(transactions_.size());
    if (!loader_.complete()) {
      return;
    }

    if (transactions_.empty()) {
      min_running_ht_.store(HybridTime::kMax, std::memory_order_release);
      CheckMinRunningHybridTimeSatisfiedUnlocked(min_running_notifier);
      return;
    }

    auto& first_txn = **transactions_.get<StartTimeTag>().begin();
    if (first_txn.start_ht() != min_running_ht_.load(std::memory_order_relaxed)) {
      min_running_ht_.store(first_txn.start_ht(), std::memory_order_release);
      next_check_min_running_.store(
          CoarseMonoClock::now() + 1ms * FLAGS_transaction_min_running_check_delay_ms,
          std::memory_order_release);
      CheckMinRunningHybridTimeSatisfiedUnlocked(min_running_notifier);
      return;
    }
  }

  void EnqueueRemoveUnlocked(
      const TransactionId& id, RemoveReason reason, MinRunningNotifier* min_running_notifier,
      const Status& expected_deadlock_status) REQUIRES(mutex_) override {
    auto now = participant_context_.Now();
    VLOG_WITH_PREFIX_AND_FUNC(4) << id << " at " << now << ", reason: " << AsString(reason);
    remove_queue_.emplace_back(RemoveQueueEntry {
      .id = id,
      .time = now,
      .reason = reason,
      .expected_deadlock_status = expected_deadlock_status,
    });
    ProcessRemoveQueueUnlocked(min_running_notifier);
  }

  void ProcessRemoveQueueUnlocked(MinRunningNotifier* min_running_notifier) REQUIRES(mutex_) {
    if (!remove_queue_.empty()) {
      // When a transaction participant receives an "aborted" response from the coordinator,
      // it puts this transaction into a "remove queue", also storing the current hybrid
      // time. Then queue entries where time is less than current safe time are removed.
      //
      // This is correct because, from a transaction participant's point of view:
      //
      // (1) After we receive a response for a transaction status request, and
      // learn that the transaction is unknown to the coordinator, our local
      // hybrid time is at least as high as the local hybrid time on the
      // transaction status coordinator at the time the transaction was deleted
      // from the coordinator, due to hybrid time propagation on RPC response.
      //
      // (2) If our safe time is greater than the hybrid time when the
      // transaction was deleted from the coordinator, then we have already
      // applied this transaction's provisional records if the transaction was
      // committed.
      auto safe_time = participant_context_.SafeTimeForTransactionParticipant();
      if (!safe_time.is_valid()) {
        VLOG_WITH_PREFIX(3) << "Unable to obtain safe time to check remove queue";
        return;
      }
      VLOG_WITH_PREFIX(3) << "Checking remove queue: " << safe_time << ", "
                          << remove_queue_.front().ToString();
      LOG_IF_WITH_PREFIX(DFATAL, safe_time < last_safe_time_)
          << "Safe time decreased: " << safe_time << " vs " << last_safe_time_;
      last_safe_time_ = safe_time;
      while (!remove_queue_.empty()) {
        auto& front = remove_queue_.front();
        auto it = transactions_.find(front.id);
        if (it == transactions_.end() || (**it).local_commit_time().is_valid()) {
          // A couple possibilities:
          // 1. This is an xcluster transaction so we want to skip the remove path for ABORTED
          // transactions.
          // 2. Since the coordinator returns ABORTED for already applied transaction. But this
          // particular tablet could not yet apply it, so it would add such transaction to remove
          // queue. And it is the main reason why we are waiting for safe time, before removing
          // intents.
          VLOG_WITH_PREFIX(4) << "Evicting txn from remove queue, w/o removing intents: "
                              << front.ToString();
          remove_queue_.pop_front();
          continue;
        }
        if (safe_time <= front.time) {
          break;
        }
        VLOG_WITH_PREFIX(4) << "Removing from remove queue: " << front.ToString();
        RemoveUnlocked(
            it, front.reason, min_running_notifier, front.expected_deadlock_status);
        remove_queue_.pop_front();
      }
    }
  }

  // Tries to remove transaction with specified id.
  // Returns true if transaction is not exists after call to this method, otherwise returns false.
  // Which means that transaction will be removed later.
  bool RemoveUnlocked(
      const TransactionId& id, RemoveReason reason,
      MinRunningNotifier* min_running_notifier) REQUIRES(mutex_) override {
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      return true;
    }
    return RemoveUnlocked(it, reason, min_running_notifier);
  }

  bool RemoveUnlocked(
      const Transactions::iterator& it, RemoveReason reason,
      MinRunningNotifier* min_running_notifier,
      const Status& expected_deadlock_status = Status::OK()) REQUIRES(mutex_) {
    TransactionId txn_id = (**it).id();
    OpId checkpoint_op_id = GetLatestCheckPoint();
    OpId op_id = (**it).GetApplyOpId();

    if (running_requests_.empty() && op_id < checkpoint_op_id) {
      (**it).ScheduleRemoveIntents(*it, reason);
      RemoveTransaction(it, reason, min_running_notifier, expected_deadlock_status);
      VLOG_WITH_PREFIX(2) << "Cleaned transaction: " << txn_id << ", reason: " << reason
                          << " , apply record op_id: " << op_id
                          << ", checkpoint_op_id: " << checkpoint_op_id
                          << ", left: " << transactions_.size();
      return true;
    }

    // We cannot remove the transaction at this point, because there are running requests
    // that are reading the provisional DB and could request status of this transaction.
    // So we store transaction in a queue and wait when all requests that we launched before our
    // attempt to remove this transaction are completed.
    // Since we try to remove the transaction after all its records are removed from the provisional
    // DB, it is safe to complete removal at this point, because it means that there will be no more
    // queries to status of this transactions.
    immediate_cleanup_queue_.push_back(ImmediateCleanupQueueEntry {
      .request_id = request_serial_,
      .transaction_id = (**it).id(),
      .reason = reason,
      .expected_deadlock_status = expected_deadlock_status,
    });
    VLOG_WITH_PREFIX(2) << "Queued for cleanup: " << (**it).id() << ", reason: " << reason;
    return false;
  }

  struct LockAndFindResult {
    static Transactions::const_iterator UninitializedIterator() {
      static const Transactions empty_transactions;
      return empty_transactions.end();
    }

    static LockAndFindResult Deadlocked(Status deadlock_status) {
      DCHECK(!deadlock_status.ok());
      auto res = LockAndFindResult{};
      res.deadlock_status = deadlock_status;
      return res;
    }

    std::unique_lock<std::mutex> lock;
    Transactions::const_iterator iterator = UninitializedIterator();
    Status deadlock_status = Status::OK();

    bool found() const {
      return lock.owns_lock();
    }

    RunningTransaction& transaction() const {
      return **iterator;
    }

    bool did_txn_deadlock() const {
      return !deadlock_status.ok();
    }
  };

  Result<LockAndFindResult> LockAndFind(
      const TransactionId& id, const std::string& reason, TransactionLoadFlags flags) {
    RETURN_NOT_OK(loader_.WaitLoaded(id));
    bool recently_removed;
    Status deadlock_status;
    {
      UniqueLock<std::mutex> lock(mutex_);
      auto it = transactions_.find(id);
      if (it != transactions_.end()) {
        if ((**it).start_ht() <= ignore_all_transactions_started_before_) {
          YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 1)
              << "Ignore transaction for '" << reason << "' because of limit: "
              << ignore_all_transactions_started_before_ << ", txn: " << AsString(**it);
          return LockAndFindResult{};
        }
        return LockAndFindResult{std::move(GetLockForCondition(&lock)), it};
      }
      recently_removed = WasTransactionRecentlyRemoved(id);
      deadlock_status = GetTransactionDeadlockStatusUnlocked(id);
    }
    if (recently_removed) {
      VLOG_WITH_PREFIX(1)
          << "Attempt to load recently removed transaction: " << id << ", for: " << reason;
      if (!deadlock_status.ok()) {
        return LockAndFindResult::Deadlocked(deadlock_status);
      }
      return LockAndFindResult{};
    }
    metric_transaction_not_found_->Increment();
    if (flags.Test(TransactionLoadFlag::kMustExist)) {
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 1)
          << "Transaction not found: " << id << ", for: " << reason;
    } else {
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 1)
          << "Transaction not found: " << id << ", for: " << reason;
    }
    if (flags.Test(TransactionLoadFlag::kCleanup)) {
      VLOG_WITH_PREFIX(2) << "Schedule cleanup for: " << id;
      auto cleanup_task = std::make_shared<CleanupIntentsTask>(
          &participant_context_, &applier_, RemoveReason::kNotFound, id);
      cleanup_task->Prepare(cleanup_task);
      participant_context_.StrandEnqueue(cleanup_task.get());
    }
    if (!deadlock_status.ok()) {
      return LockAndFindResult::Deadlocked(deadlock_status);
    }
    return LockAndFindResult{};
  }

  void LoadTransaction(
      TransactionMetadata&& metadata,
      TransactionalBatchData&& last_batch_data,
      OneWayBitmap&& replicated_batches,
      const ApplyStateWithCommitHt* pending_apply) override {
    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    auto txn = std::make_shared<RunningTransaction>(
        std::move(metadata), std::move(last_batch_data), std::move(replicated_batches),
        participant_context_.Now().AddDelta(1ms * FLAGS_transaction_abort_check_timeout_ms), this);
    if (pending_apply) {
      VLOG_WITH_PREFIX(4) << "Apply state found for " << txn->id() << ": "
                          << pending_apply->ToString();
      txn->SetLocalCommitData(pending_apply->commit_ht, pending_apply->state.aborted);
      txn->SetApplyData(pending_apply->state);
    }
    transactions_.insert(txn);
    mem_tracker_->Consume(kRunningTransactionSize);
    TransactionsModifiedUnlocked(&min_running_notifier);
  }

  const std::string& LogPrefix() const override {
    return log_prefix_;
  }

  void DumpClear(RemoveReason reason) {
      if (FLAGS_dump_transactions) {
        for (const auto& txn : transactions_) {
          DumpRemove(*txn, reason);
        }
      }
  }

  void DumpRemove(const RunningTransaction& transaction, RemoveReason reason) {
    YB_TRANSACTION_DUMP(
        Remove, participant_context_.tablet_id(), transaction.id(), participant_context_.Now(),
        static_cast<uint8_t>(reason));
  }

  void RemoveTransaction(
      Transactions::iterator it, RemoveReason reason, MinRunningNotifier* min_running_notifier,
      const Status& expected_deadlock_status = Status::OK()) REQUIRES(mutex_) {
    auto now = CoarseMonoClock::now();
    CleanupRecentlyRemovedTransactions(now);
    auto& transaction = **it;
    UpdateDeadlockedTransactionsMapUnlocked(transaction.id(), expected_deadlock_status);
    DumpRemove(transaction, reason);
    recently_removed_transactions_cleanup_queue_.push_back({transaction.id(), now + 15s});
    LOG_IF_WITH_PREFIX(DFATAL, !recently_removed_transactions_.insert(transaction.id()).second)
        << "Transaction removed twice: " << transaction.id();
    transactions_.erase(it);
    mem_tracker_->Release(kRunningTransactionSize);
    TransactionsModifiedUnlocked(min_running_notifier);
  }

  void CleanupRecentlyRemovedTransactions(CoarseTimePoint now) {
    while (!recently_removed_transactions_cleanup_queue_.empty() &&
           recently_removed_transactions_cleanup_queue_.front().time <= now) {
      recently_removed_transactions_.erase(recently_removed_transactions_cleanup_queue_.front().id);
      recently_removed_transactions_cleanup_queue_.pop_front();
    }
  }

  bool WasTransactionRecentlyRemoved(const TransactionId& id) {
    CleanupRecentlyRemovedTransactions(CoarseMonoClock::now());
    return recently_removed_transactions_.count(id) != 0;
  }

  void CheckMinRunningHybridTimeSatisfiedUnlocked(
      MinRunningNotifier* min_running_notifier) {
    if (min_running_ht_.load(std::memory_order_acquire) <= waiting_for_min_running_ht_) {
      return;
    }
    waiting_for_min_running_ht_ = HybridTime::kMax;
    min_running_notifier->Satisfied();
  }

  void TransactionsStatus(
      const std::vector<TransactionStatusInfo>& status_infos) {
    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    HybridTime now = participant_context_.Now();
    for (const auto& info : status_infos) {
      auto it = transactions_.find(info.transaction_id);
      if (it == transactions_.end()) {
        continue;
      }
      if ((**it).UpdateStatus(
          info.status_tablet, info.status, info.status_ht, info.coordinator_safe_time,
          info.aborted_subtxn_set, info.expected_deadlock_status)) {
        NotifyAbortedTransactionIncrement(info.transaction_id);
        EnqueueRemoveUnlocked(
            info.transaction_id, RemoveReason::kStatusReceived, &min_running_notifier,
            info.expected_deadlock_status);
      } else {
        CHECK(transactions_.modify(it, [now](const auto& txn) {
          txn->UpdateAbortCheckHT(now, UpdateAbortCheckHTMode::kStatusResponseReceived);
        }));
      }
    }
  }

  void HandleApplying(std::unique_ptr<tablet::UpdateTxnOperation> operation, int64_t term) {
    if (RandomActWithProbability(GetAtomicFlag(
        &FLAGS_TEST_transaction_ignore_applying_probability))) {
      VLOG_WITH_PREFIX(2)
          << "TEST: Rejected apply: "
          << FullyDecodeTransactionId(operation->request()->transaction_id());
      operation->CompleteWithStatus(Status::OK());
      return;
    }
    auto operation_state = operation->ToString();
    Status submit_status = participant_context_.SubmitUpdateTransaction(std::move(operation), term);
    if (!submit_status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Could not submit transaction status update operation: "
                               << operation_state << ", status: " << submit_status;
    }
  }

  void HandleCleanup(
      std::unique_ptr<tablet::UpdateTxnOperation> operation, int64_t term,
      CleanupType cleanup_type) {
    VLOG_WITH_PREFIX(3) << "Cleanup";
    auto id = FullyDecodeTransactionId(operation->request()->transaction_id());
    if (!id.ok()) {
      operation->CompleteWithStatus(id.status());
      return;
    }
    if (operation->request()->status() == TransactionStatus::IMMEDIATE_CLEANUP && wait_queue_) {
      // We should only receive IMMEDIATE_CLEANUP from the client in case of certain txn abort.
      wait_queue_->SignalAborted(*id);
    }

    TransactionApplyData data = {
        .leader_term = term,
        .transaction_id = *id,
        .aborted = SubtxnSet(),
        .op_id = OpId(),
        .commit_ht = HybridTime(),
        .log_ht = HybridTime(),
        .sealed = operation->request()->sealed(),
        .status_tablet = std::string(),
    };
    WARN_NOT_OK(ProcessCleanup(data, cleanup_type), "Process cleanup failed");
    operation->CompleteWithStatus(Status::OK());
  }

  Status ReplicatedApplying(const TransactionId& id, const ReplicatedData& data) {
    // data.state.tablets contains only status tablet.
    if (data.state.tablets().size() != 1) {
      return STATUS_FORMAT(InvalidArgument,
                           "Expected only one table during APPLYING, state received: $0",
                           data.state);
    }
    HybridTime commit_time(data.state.commit_hybrid_time());
    TransactionApplyData apply_data = {
      .leader_term = data.leader_term,
      .transaction_id = id,
      .aborted = VERIFY_RESULT(SubtxnSet::FromPB(data.state.aborted().set())),
      .op_id = data.op_id,
      .commit_ht = commit_time,
      .log_ht = data.hybrid_time,
      .sealed = data.sealed,
      .status_tablet = data.state.tablets().front().ToBuffer()
    };
    if (!data.already_applied_to_regular_db) {
      return ProcessApply(apply_data);
    }
    if (!data.sealed) {
      return ProcessCleanup(apply_data, CleanupType::kImmediate);
    }
    return Status::OK();
  }

  Status ReplicatedAborted(const TransactionId& id, const ReplicatedData& data) {
    MinRunningNotifier min_running_notifier(&applier_);
    std::lock_guard lock(mutex_);
    auto it = transactions_.find(id);
    if (it == transactions_.end()) {
      TransactionMetadata metadata = {
        .transaction_id = id,
        .isolation = IsolationLevel::NON_TRANSACTIONAL,
        .status_tablet = TabletId(),
        .priority = 0,
        .start_time = {},
        .pg_txn_start_us = {},
        .old_status_tablet = {},
      };
      it = transactions_.insert(std::make_shared<RunningTransaction>(
          metadata, TransactionalBatchData(), OneWayBitmap(), HybridTime::kMax, this)).first;
      mem_tracker_->Consume(kRunningTransactionSize);
      TransactionsModifiedUnlocked(&min_running_notifier);
    }

    // TODO(dtxn) store this fact to rocksdb.
    (**it).Aborted();

    return Status::OK();
  }

  void Poll() {
    {
      MinRunningNotifier min_running_notifier(&applier_);
      std::lock_guard lock(mutex_);

      ProcessRemoveQueueUnlocked(&min_running_notifier);
      CleanTransactionsQueue(&graceful_cleanup_queue_, &min_running_notifier);
      CleanDeadlockedTransactionsMapUnlocked();
    }

    if (ANNOTATE_UNPROTECTED_READ(FLAGS_transactions_poll_check_aborted)) {
      CheckForAbortedTransactions();
    }
    CleanupStatusResolvers();
  }

  void PollWaitQueue() {
    DCHECK_NOTNULL(wait_queue_)->Poll(participant_context_.Now());
  }

  void CheckForAbortedTransactions() EXCLUDES(mutex_) {
    TransactionStatusResolver* resolver = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (transactions_.empty()) {
        return;
      }
      auto now = participant_context_.Now();
      auto& index = transactions_.get<AbortCheckTimeTag>();
      for (;;) {
        auto& txn = **index.begin();
        if (txn.abort_check_ht() > now) {
          break;
        }
        if (!resolver) {
          resolver = &AddStatusResolver();
        }
        const auto& metadata = txn.metadata();
        VLOG_WITH_PREFIX(4)
            << "Check aborted: " << metadata.status_tablet << ", " << metadata.transaction_id;
        resolver->Add(metadata.status_tablet, metadata.transaction_id);
        CHECK(index.modify(index.begin(), [now](const auto& txn) {
          txn->UpdateAbortCheckHT(now, UpdateAbortCheckHTMode::kStatusRequestSent);
        }));
      }
    }

    // We don't introduce limit on number of status resolutions here, because we cannot predict
    // transactions throughput. And we rely the logic that we cannot start multiple resolutions
    // for single transaction because we set abort check hybrid time to the same value as
    // status resolution deadline.
    if (resolver) {
      resolver->Start(CoarseMonoClock::now() + 1ms * FLAGS_transaction_abort_check_timeout_ms);
    }
  }

  void CleanupStatusResolvers() EXCLUDES(status_resolvers_mutex_) {
    std::lock_guard lock(status_resolvers_mutex_);
    while (!status_resolvers_.empty() && !status_resolvers_.front().Running()) {
      status_resolvers_.front().Shutdown();
      status_resolvers_.pop_front();
    }
  }

  TransactionStatusResolver& AddStatusResolver() override EXCLUDES(status_resolvers_mutex_) {
    std::lock_guard lock(status_resolvers_mutex_);
    status_resolvers_.emplace_back(
        &participant_context_, &rpcs_, FLAGS_max_transactions_in_status_request,
        std::bind(&Impl::TransactionsStatus, this, _1));
    return status_resolvers_.back();
  }

  void UpdateDeadlockedTransactionsMapUnlocked(const TransactionId& id, const Status& status)
      REQUIRES(mutex_) {
    if (TransactionError::ValueFromStatus(status) == TransactionErrorCode::kDeadlock) {
      recently_deadlocked_txns_info_.emplace(id, DeadlockInfo(status, CoarseMonoClock::Now()));
    }
  }

  // Returns Status::OK() if the transaction isn't in the recently deadlocked transactions list.
  Status GetTransactionDeadlockStatusUnlocked(const TransactionId& id) {
    auto it = recently_deadlocked_txns_info_.find(id);
    return it == recently_deadlocked_txns_info_.end() ? Status::OK() : it->second.first;
  }

  void CleanDeadlockedTransactionsMapUnlocked() REQUIRES(mutex_) {
    auto interval = FLAGS_clear_deadlocked_txns_info_older_than_heartbeats *
        FLAGS_transaction_heartbeat_usec * 1us;
    auto expired_cutoff_time = CoarseMonoClock::Now() - interval;
    for (auto it = recently_deadlocked_txns_info_.begin();
         it != recently_deadlocked_txns_info_.end();) {
      const auto& deadlock_time = it->second.second;
      if (deadlock_time < expired_cutoff_time) {
        VLOG_WITH_PREFIX(1) << "Cleaning up deadlocked txn at participant " << it->first.ToString();
        it = recently_deadlocked_txns_info_.erase(it);
      } else {
        it++;
      }
    }
  }

  struct ImmediateCleanupQueueEntry {
    int64_t request_id;
    TransactionId transaction_id;
    RemoveReason reason;
    // Status containing deadlock info if the transaction was aborted due to deadlock.
    Status expected_deadlock_status = Status::OK();

    bool Ready(TransactionParticipantContext* participant_context, HybridTime* safe_time) const {
      return true;
    }
  };

  struct GracefulCleanupQueueEntry {
    int64_t request_id;
    TransactionId transaction_id;
    RemoveReason reason;
    HybridTime required_safe_time;
    // Status containing deadlock info if the transaction was aborted due to deadlock.
    Status expected_deadlock_status = Status::OK();

    bool Ready(TransactionParticipantContext* participant_context, HybridTime* safe_time) const {
      if (!*safe_time) {
        *safe_time = participant_context->SafeTimeForTransactionParticipant();
      }
      return *safe_time >= required_safe_time;
    }
  };

  std::string log_prefix_;

  docdb::DocDB db_;
  const docdb::KeyBounds* key_bounds_;
  // Owned externally, should be guaranteed that would not be destroyed before this.
  RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start_ = nullptr;

  Transactions transactions_;
  // Ids of running requests, stored in increasing order.
  std::deque<int64_t> running_requests_;
  // Ids of complete requests, minimal request is on top.
  // Contains only ids greater than first running request id, otherwise entry is removed
  // from both collections.
  std::priority_queue<int64_t, std::vector<int64_t>, std::greater<void>> complete_requests_;

  // Queues of transaction ids that should be cleaned, paired with request that should be completed
  // in order to be able to do clean.
  // Immediate cleanup is performed as soon as possible.
  // Graceful cleanup is performed after safe time becomes greater than cleanup request hybrid time.
  std::deque<ImmediateCleanupQueueEntry> immediate_cleanup_queue_ GUARDED_BY(mutex_);
  std::deque<GracefulCleanupQueueEntry> graceful_cleanup_queue_ GUARDED_BY(mutex_);

  // Remove queue maintains transactions that could be cleaned when safe time for follower reaches
  // appropriate time for an entry.
  // Since we add entries with increasing time, this queue is ordered by time.
  struct RemoveQueueEntry {
    TransactionId id;
    HybridTime time;
    RemoveReason reason;
    // Status containing deadlock info if the transaction was aborted due to deadlock.
    Status expected_deadlock_status = Status::OK();

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(id, time, reason, expected_deadlock_status);
    }
  };

  // Guarded by RunningTransactionContext::mutex_
  std::deque<RemoveQueueEntry> remove_queue_;

  // Guarded by RunningTransactionContext::mutex_
  HybridTime last_safe_time_ = HybridTime::kMin;

  HybridTime ignore_all_transactions_started_before_ GUARDED_BY(mutex_) = HybridTime::kMin;

  std::unordered_set<TransactionId, TransactionIdHash> recently_removed_transactions_;
  struct RecentlyRemovedTransaction {
    TransactionId id;
    CoarseTimePoint time;
  };
  std::deque<RecentlyRemovedTransaction> recently_removed_transactions_cleanup_queue_;

  std::mutex status_resolvers_mutex_;
  std::deque<TransactionStatusResolver> status_resolvers_ GUARDED_BY(status_resolvers_mutex_);

  scoped_refptr<AtomicGauge<uint64_t>> metric_transactions_running_;
  scoped_refptr<AtomicGauge<uint64_t>> metric_aborted_transactions_pending_cleanup_;
  scoped_refptr<Counter> metric_transaction_not_found_;
  scoped_refptr<EventStats> metric_conflict_resolution_latency_;
  scoped_refptr<EventStats> metric_conflict_resolution_num_keys_scanned_;

  TransactionLoader loader_;
  std::atomic<bool> closing_{false};
  CountDownLatch start_latch_{1};
  CountDownLatch shutdown_latch_{1};

  std::atomic<HybridTime> min_running_ht_{HybridTime::kInvalid};
  std::atomic<CoarseTimePoint> next_check_min_running_{CoarseTimePoint()};
  HybridTime waiting_for_min_running_ht_ = HybridTime::kMax;
  std::atomic<bool> shutdown_done_{false};

  LRUCache<TransactionId> cleanup_cache_{FLAGS_transactions_cleanup_cache_size};

  // Guarded by RunningTransactionContext::mutex_
  std::unordered_map<TransactionId, DeadlockInfo, TransactionIdHash>
      recently_deadlocked_txns_info_;

  rpc::Poller poller_;

  rpc::Poller wait_queue_poller_;

  OpId cdc_sdk_min_checkpoint_op_id_ = OpId::Invalid();
  std::atomic<OpId> historical_max_op_id = OpId::Invalid();
  CoarseTimePoint cdc_sdk_min_checkpoint_op_id_expiration_ = CoarseTimePoint::min();

  std::condition_variable requests_completed_cond_;

  std::unique_ptr<docdb::WaitQueue> wait_queue_;

  std::shared_ptr<MemTracker> mem_tracker_ GUARDED_BY(mutex_);
};

TransactionParticipant::TransactionParticipant(
    TransactionParticipantContext* context, TransactionIntentApplier* applier,
    const scoped_refptr<MetricEntity>& entity,
    const std::shared_ptr<MemTracker>& tablets_mem_tracker)
    : impl_(new Impl(context, applier, entity, tablets_mem_tracker)) {
}

TransactionParticipant::~TransactionParticipant() {
}

void TransactionParticipant::Start() {
  impl_->Start();
}

Result<bool> TransactionParticipant::Add(const TransactionMetadata& metadata) {
  return impl_->Add(metadata);
}

Result<TransactionMetadata> TransactionParticipant::PrepareMetadata(
    const LWTransactionMetadataPB& pb) {
  return impl_->PrepareMetadata(pb);
}

Result<TransactionMetadata> TransactionParticipant::PrepareMetadata(
    const TransactionMetadataPB& pb) {
  return impl_->PrepareMetadata(pb);
}

Result<boost::optional<std::pair<IsolationLevel, TransactionalBatchData>>>
    TransactionParticipant::PrepareBatchData(
    const TransactionId& id, size_t batch_idx,
    boost::container::small_vector_base<uint8_t>* encoded_replicated_batches) {
  return impl_->PrepareBatchData(id, batch_idx, encoded_replicated_batches);
}

void TransactionParticipant::BatchReplicated(
    const TransactionId& id, const TransactionalBatchData& data) {
  return impl_->BatchReplicated(id, data);
}

HybridTime TransactionParticipant::LocalCommitTime(const TransactionId& id) {
  return impl_->LocalCommitTime(id);
}

boost::optional<TransactionLocalState> TransactionParticipant::LocalTxnData(
    const TransactionId& id) {
  return impl_->LocalTxnData(id);
}

Result<std::pair<size_t, size_t>> TransactionParticipant::TEST_CountIntents() const {
  return impl_->TEST_CountIntents();
}

void TransactionParticipant::RequestStatusAt(const StatusRequest& request) {
  return impl_->RequestStatusAt(request);
}

Result<int64_t> TransactionParticipant::RegisterRequest() {
  return impl_->RegisterRequest();
}

void TransactionParticipant::UnregisterRequest(int64_t request) {
  impl_->UnregisterRequest(request);
}

void TransactionParticipant::Abort(const TransactionId& id,
                                   TransactionStatusCallback callback) {
  return impl_->Abort(id, std::move(callback));
}

void TransactionParticipant::Handle(
    std::unique_ptr<tablet::UpdateTxnOperation> request, int64_t term) {
  impl_->Handle(std::move(request), term);
}

Status TransactionParticipant::Cleanup(TransactionIdSet&& set) {
  return impl_->Cleanup(std::move(set), this);
}

Status TransactionParticipant::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

Status TransactionParticipant::CheckAborted(const TransactionId& id) {
  return impl_->CheckAborted(id);
}

Status TransactionParticipant::FillPriorities(
    boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) {
  return impl_->FillPriorities(inout);
}

Result<boost::optional<TabletId>> TransactionParticipant::FindStatusTablet(
    const TransactionId& id) {
  return impl_->GetStatusTablet(id);
}

Status TransactionParticipant::SetDB(
    const docdb::DocDB& db, const docdb::KeyBounds* key_bounds,
    RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start) {
  return impl_->SetDB(db, key_bounds, pending_op_counter_blocking_rocksdb_shutdown_start);
}

void TransactionParticipant::GetStatus(
    const TransactionId& transaction_id,
    size_t required_num_replicated_batches,
    int64_t term,
    tserver::GetTransactionStatusAtParticipantResponsePB* response,
    rpc::RpcContext* context) {
  impl_->GetStatus(transaction_id, required_num_replicated_batches, term, response, context);
}

TransactionParticipantContext* TransactionParticipant::context() const {
  return impl_->participant_context();
}

HybridTime TransactionParticipant::MinRunningHybridTime() const {
  return impl_->MinRunningHybridTime();
}

void TransactionParticipant::WaitMinRunningHybridTime(HybridTime ht) {
  impl_->WaitMinRunningHybridTime(ht);
}

Status TransactionParticipant::ResolveIntents(HybridTime resolve_at, CoarseTimePoint deadline) {
  return impl_->ResolveIntents(resolve_at, deadline);
}

size_t TransactionParticipant::GetNumRunningTransactions() const {
  return impl_->GetNumRunningTransactions();
}

OneWayBitmap TransactionParticipant::TEST_TransactionReplicatedBatches(
    const TransactionId& id) const {
  return impl_->TEST_TransactionReplicatedBatches(id);
}

std::string TransactionParticipant::ReplicatedData::ToString() const {
  return YB_STRUCT_TO_STRING(leader_term, state, op_id, hybrid_time, already_applied_to_regular_db);
}

void TransactionParticipant::SetWaitQueue(std::unique_ptr<docdb::WaitQueue> wait_queue) {
  return impl_->SetWaitQueue(std::move(wait_queue));
}

docdb::WaitQueue* TransactionParticipant::wait_queue() const {
  return impl_->wait_queue();
}

void TransactionParticipant::StartShutdown() {
  impl_->StartShutdown();
}

void TransactionParticipant::CompleteShutdown() {
  impl_->CompleteShutdown();
}

std::string TransactionParticipant::DumpTransactions() const {
  return impl_->DumpTransactions();
}

Status TransactionParticipant::StopActiveTxnsPriorTo(
    HybridTime cutoff, CoarseTimePoint deadline, TransactionId* exclude_txn_id) {
  return impl_->StopActiveTxnsPriorTo(cutoff, deadline, exclude_txn_id);
}

Result<HybridTime> TransactionParticipant::WaitForSafeTime(
    HybridTime safe_time, CoarseTimePoint deadline) {
  return impl_->WaitForSafeTime(safe_time, deadline);
}

void TransactionParticipant::IgnoreAllTransactionsStartedBefore(HybridTime limit) {
  impl_->IgnoreAllTransactionsStartedBefore(limit);
}

Result<TransactionMetadata> TransactionParticipant::UpdateTransactionStatusLocation(
      const TransactionId& transaction_id, const TabletId& new_status_tablet) {
  return impl_->UpdateTransactionStatusLocation(transaction_id, new_status_tablet);
}

const TabletId& TransactionParticipant::tablet_id() const {
  return impl_->participant_context()->tablet_id();
}

std::string TransactionParticipantContext::LogPrefix() const {
  return consensus::MakeTabletLogPrefix(tablet_id(), permanent_uuid());
}

HybridTime TransactionParticipantContext::Now() {
  return clock_ptr()->Now();
}

void TransactionParticipant::SetIntentRetainOpIdAndTime(
    const yb::OpId& op_id, const MonoDelta& cdc_sdk_op_id_expiration) {
  impl_->SetIntentRetainOpIdAndTime(op_id, cdc_sdk_op_id_expiration);
}

OpId TransactionParticipant::GetRetainOpId() const {
  return impl_->GetRetainOpId();
}

CoarseTimePoint TransactionParticipant::GetCheckpointExpirationTime() const {
  return impl_->GetCheckpointExpirationTime();
}

OpId TransactionParticipant::GetLatestCheckPoint() const {
  return impl_->GetLatestCheckPointUnlocked();
}

HybridTime TransactionParticipant::GetMinStartTimeAmongAllRunningTransactions() const {
  return impl_->GetMinStartTimeAmongAllRunningTransactions();
}

OpId TransactionParticipant::GetHistoricalMaxOpId() const {
  return impl_->GetHistoricalMaxOpId();
}

void TransactionParticipant::RecordConflictResolutionKeysScanned(int64_t num_keys) {
  impl_->RecordConflictResolutionKeysScanned(num_keys);
}

void TransactionParticipant::RecordConflictResolutionScanLatency(MonoDelta latency) {
  impl_->RecordConflictResolutionScanLatency(latency);
}

}  // namespace tablet
}  // namespace yb
