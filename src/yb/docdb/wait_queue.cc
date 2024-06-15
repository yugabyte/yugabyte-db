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

#include "yb/docdb/wait_queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <memory>
#include <queue>
#include <sstream>

#include <boost/algorithm/string/join.hpp>

#include "yb/ash/wait_state.h"
#include "yb/client/client.h"
#include "yb/client/transaction_rpc.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/common/transaction_error.h"
#include "yb/common/wire_protocol.h"
#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/shared_lock_manager.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/intent.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/server/clock.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/transaction_participant_context.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/operation_counter.h"
#include "yb/util/shared_lock.h"
#include "yb/util/source_location.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/trace.h"
#include "yb/util/unique_lock.h"

DEFINE_RUNTIME_uint64(wait_for_relock_unblocked_txn_keys_ms, 100,
    "If greater than zero, indicates the maximum amount of time to wait to lock keys "
    "needed by a newly unblocked transaction. Otherwise, a default value of 1s is used.");
TAG_FLAG(wait_for_relock_unblocked_txn_keys_ms, advanced);
TAG_FLAG(wait_for_relock_unblocked_txn_keys_ms, hidden);

DEFINE_UNKNOWN_uint64(force_single_shard_waiter_retry_ms, 30000,
              "The amount of time to wait before sending the client of a single shard transaction "
              "a retryable error. Such clients are periodically sent a retryable error to ensure "
              "we don't maintain waiters in the wait queue for unresponsive or disconnected "
              "clients. This is not required for multi-tablet transactions which have a "
              "transaction ID and are rolled back if the transaction coordinator does not receive "
              "a heartbeat, since for these we will eventually discover that the transaction has "
              "been rolled back and remove the waiter. If set to zero, this will default to 30s.");

// Enabling FLAGS_refresh_waiter_timeout_ms is necessary for maintaining up-to-date blocking
// transaction(s) information at the transaction coordinator/deadlock detector. Else, with the
// current implementation, it could result in true deadlocks not being detected.
//
// For instance, refer issue https://github.com/yugabyte/yugabyte-db/issues/16286
//
// Additionally, enabling this flag serves as a fallback mechanism for deadlock detection as it
// helps maintain updated blocker(s) info at the deadlock detector. Since the feature of supporting
// transaction promotion for geo-partitioned workloads in use of wait-queues and deadlock detection
// is relatively new, it is advisable that we have the flag enabled for now. The value can be
// increased once the feature hardens and the above referred issue is resolved.
DEFINE_RUNTIME_uint64(refresh_waiter_timeout_ms, 30000,
                      "The maximum amount of time a waiter transaction waits in the wait-queue "
                      "before its callback is invoked. On invocation, the waiter transaction "
                      "re-runs conflicts resolution and might enter the wait-queue again with "
                      "updated blocker(s) information. Setting the value to 0 disables "
                      "automatically re-running conflict resolution due to timeout. It follows "
                      "that the waiter callback would only be invoked when a blocker txn commits/ "
                      "aborts/gets promoted.");
TAG_FLAG(refresh_waiter_timeout_ms, advanced);
TAG_FLAG(refresh_waiter_timeout_ms, hidden);

DEFINE_test_flag(uint64, sleep_before_entering_wait_queue_ms, 0,
                 "The amount of time for which the thread sleeps before registering a transaction "
                 "with the wait queue.");

DEFINE_test_flag(bool, drop_participant_signal, false,
                 "If true, do nothing with the commit/abort signal from the participant to the "
                 "wait queue.");

DEFINE_test_flag(uint64, delay_rpc_status_req_callback_ms, 0,
                 "If non-zero, upon receiving rpc status of a waiter transaction, we pause for set "
                 "milliseconds before executing the underlying wait-queue callback function. Used "
                 "in tests to assert that the wait-queue instance isn't deallocated while there "
                 "are in-progress callback executions.");

DEFINE_test_flag(bool, skip_waiter_resumption_on_blocking_subtxn_rollback, false,
                 "When set, the wait-queue doesn't signal waiter requests when there's a change "
                 "in the blocker's aborted subtxn set.");

METRIC_DEFINE_event_stats(
    tablet, wait_queue_pending_time_waiting, "Wait Queue - Still Waiting Time",
    yb::MetricUnit::kMicroseconds,
    "The amount of time a still-waiting transaction has been in the wait queue");
METRIC_DEFINE_event_stats(
    tablet, wait_queue_finished_waiting_latency, "Wait Queue - Total Waiting Time",
    yb::MetricUnit::kMicroseconds,
    "The amount of time an unblocked transaction spent in the wait queue");
METRIC_DEFINE_event_stats(
    tablet, wait_queue_blockers_per_waiter, "Wait Queue - Blockers per Waiter",
    yb::MetricUnit::kTransactions, "The number of blockers a waiter is stuck on in the wait queue");
METRIC_DEFINE_event_stats(
    tablet, wait_queue_waiters_per_blocker, "Wait Queue - Waiters per Blocker",
    yb::MetricUnit::kTransactions,
    "The number of waiters stuck on a particular blocker in the wait queue");
METRIC_DEFINE_gauge_uint64(
    tablet, wait_queue_num_waiters, "Wait Queue - Num Waiters",
    yb::MetricUnit::kTransactions, "The number of waiters stuck on a blocker in the wait queue");
METRIC_DEFINE_gauge_uint64(
    tablet, wait_queue_num_blockers, "Wait Queue - Num Blockers",
    yb::MetricUnit::kTransactions, "The number of unique blockers tracked in a wait queue");

using namespace std::chrono_literals;
using namespace std::placeholders;

namespace yb {
namespace docdb {

using dockv::DecodedIntentValue;
using dockv::DocKey;
using dockv::DocKeyPart;
using dockv::KeyEntryTypeAsChar;

namespace {

const Status kShuttingDownError = STATUS(
    IllegalState, "Tablet shutdown in progress - there may be a new leader.");

const Status kRetrySingleShardOp = STATUS(
    TimedOut,
    "Single shard transaction timed out while waiting. Forcing retry to confirm client liveness.");

const Status kRefreshWaiterTimeout = STATUS(
    TimedOut,
    "Waiter transaction timed out waiting in queue, invoking callback.");

const Status kWaiterTimeout = STATUS(
    TimedOut,
    "Failed to resume Waiter transaction from queue within deadline."
);

CoarseTimePoint GetWaitForRelockUnblockedKeysDeadline() {
  static constexpr auto kDefaultWaitForRelockUnblockedTxnKeys = 1000ms;
  if (FLAGS_wait_for_relock_unblocked_txn_keys_ms > 0) {
    return FLAGS_wait_for_relock_unblocked_txn_keys_ms * 1ms + CoarseMonoClock::Now();
  }
  return kDefaultWaitForRelockUnblockedTxnKeys + CoarseMonoClock::Now();
}

auto GetMaxSingleShardWaitDuration() {
  constexpr uint64_t kDefaultMaxSingleShardWaitDurationMs = 30000;
  if (FLAGS_force_single_shard_waiter_retry_ms == 0) {
    return kDefaultMaxSingleShardWaitDurationMs * 1ms;
  }
  return FLAGS_force_single_shard_waiter_retry_ms * 1ms;
}

YB_DEFINE_ENUM(ResolutionStatus, (kPending)(kCommitted)(kAborted)(kPromoted)(kDeadlocked));

class BlockerData;
using BlockerDataPtr = std::shared_ptr<BlockerData>;
using BlockerDataAndConflictInfo = std::pair<BlockerDataPtr, TransactionConflictInfoPtr>;

using BlockerDataConflictMap = std::unordered_map<TransactionId,
                                                  BlockerDataAndConflictInfo,
                                                  TransactionIdHash>;

Result<ResolutionStatus> UnwrapResult(const Result<TransactionStatusResult>& res) {
  if (!res.ok()) {
    if (res.status().IsNotFound()) {
      // If txn was not found at local txn participant, then it must have been aborted or committed,
      // but we can treat it as aborted so that we re-trigger conflict resolution.
      return ResolutionStatus::kAborted;
    } else if (res.status().IsTryAgain()) {
      // If we get TryAgain status, then we can assume that the local txn participant is tracking
      // this txn but does not know the status of the txn as of the request time, meaning it must
      // not be aborted or committed. We can treat this as pending.
      return ResolutionStatus::kPending;
    } else {
      // Any other error should be treated as a genuine error.
      return res.status();
    }
  }
  switch (res->status) {
    case COMMITTED:
      return ResolutionStatus::kCommitted;
    case ABORTED:
      return res->expected_deadlock_status.ok()
          ? ResolutionStatus::kAborted
          : ResolutionStatus::kDeadlocked;
    case PENDING:
      return ResolutionStatus::kPending;
    case PROMOTED:
      return ResolutionStatus::kPromoted;
    default:
      return STATUS_FORMAT(
        IllegalState,
        "Unexpected transaction status result in wait queue: $0", res->ToString());
  }
}

inline auto GetMicros(CoarseMonoClock::Duration duration) {
  return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

struct WaiterLockStatusInfo {
  TransactionId id;
  SubTransactionId subtxn_id;
  HybridTime wait_start;
  IntentTypesContainer intents;
  std::vector<BlockerDataPtr> blockers;
};

// Data for an active transaction which is waiting on some number of other transactions with which
// it has detected conflicts. The blockers field owns shared_ptr references to BlockerData of
// pending transactions it's blocked by. These references keep the BlockerData instances alive in
// the wait queue's blocker_status_ field, which stores only weak_ptr references. Invalid references
// in blocker_status_ are presumed to no lonber be of concern to any pending waiting transactions
// and are discarded.
struct WaiterData : public std::enable_shared_from_this<WaiterData> {
  WaiterData(const TransactionId id_, SubTransactionId subtxn_id_, LockBatch* const locks_,
             uint64_t serial_no_, int64_t txn_start_us_, uint64_t request_start_us_,
             int64_t request_id_,
             HybridTime wq_entry_time_,
             const TabletId& status_tablet_,
             const std::vector<BlockerDataAndConflictInfo> blockers_,
             IntentProviderFunc&& intent_provider_,
             const WaitDoneCallback callback_,
             std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration_,
             scoped_refptr<EventStats>* finished_waiting_latency,
             CoarseTimePoint deadline)
      : id(id_),
        subtxn_id(subtxn_id_),
        locks(locks_),
        serial_no(serial_no_),
        txn_start_us(txn_start_us_),
        request_start_us(request_start_us_),
        request_id(request_id_),
        wq_entry_time(wq_entry_time_),
        status_tablet(status_tablet_),
        blockers(std::move(blockers_)),
        wait_state(ash::WaitStateInfo::CurrentWaitState()),
        intent_provider(intent_provider_),
        callback(std::move(callback_)),
        waiter_registration(std::move(waiter_registration_)),
        finished_waiting_latency_(*finished_waiting_latency),
        unlocked_(locks->Unlock()),
        deadline_(deadline) {
    DCHECK(txn_start_us || id.IsNil());
    VLOG_WITH_PREFIX(4) << "Constructed waiter";
  }

  ~WaiterData() {
    VLOG_WITH_PREFIX(4) << "Destructed waiter";
  }

  const TransactionId id;
  const SubTransactionId subtxn_id;
  LockBatch* const locks;
  const uint64_t serial_no;
  const int64_t txn_start_us;
  // Tracks the time at which the query layer started this request. This remains consistent across
  // query-layer retries and wait queue re-entries. The field is used to report wait start time in
  // pg_locks.
  const uint64_t request_start_us;
  int64_t request_id;
  // Tracks when this WaiterData instance was created, used for periodically resuming waiters
  // with TimedOut status so as to make them re-do conflict resolution.
  const HybridTime wq_entry_time;
  const TabletId status_tablet;
  const std::vector<BlockerDataAndConflictInfo> blockers;
  const ash::WaitStateInfoPtr wait_state;
  const IntentProviderFunc intent_provider;
  const WaitDoneCallback callback;
  std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration;

  void InvokeCallbackOrWarn(
      Status waiter_status, HybridTime resume_ht = HybridTime::kInvalid,
      CoarseTimePoint locking_deadline = GetWaitForRelockUnblockedKeysDeadline()) EXCLUDES(mutex_) {
    WARN_NOT_OK(
        InvokeCallback(waiter_status, resume_ht, locking_deadline),
        Format("$0Unable to invoke waiter callback", LogPrefix()));
  }

  // Returns Status::OK() if the waiter's callback would have been executed (either in this
  // function call or sometime in the past) post returning of the function.
  Status InvokeCallback(
      Status waiter_status, HybridTime resume_ht = HybridTime::kInvalid,
      CoarseTimePoint locking_deadline = GetWaitForRelockUnblockedKeysDeadline()) EXCLUDES(mutex_) {
    ADOPT_WAIT_STATE(wait_state);
    SET_WAIT_STATUS(OnCpu_Active);
    // ASH: This may later be set to ResolveConficts for another thread to pick up
    // working on the wait-state.
    ASH_ENABLE_CONCURRENT_UPDATES_FOR(wait_state);
    TRACE_FUNC();
    auto& status = waiter_status;
    std::optional<UnlockedBatch> unlocked_copy = AtomicConsumeUnlockedBatch();
    if (!unlocked_copy) {
      // This branch may be hit somewhat commonly as WaitQueue::Poll and SignalCommitted may
      // concurrently detect that a blocker is resolved. Whichever one is second will hit this path.
      VLOG_WITH_PREFIX_AND_FUNC(1)
          << "Skipping InvokeCallback for waiter whose callback was already invoked.";
      return Status::OK();
    }

    if (status.ok()) {
      if (CoarseMonoClock::Now() > deadline_) {
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Waiter passed deadline. Executing callback with failure.";
        auto wq_entry = MonoTime::FromUint64(wq_entry_time.ToUint64());
        status = kWaiterTimeout.CloneAndAppend(Format(
          "deadline: $0, timeout: $1", deadline_, deadline_ - ToCoarse(wq_entry)));
      } else {
        *locks = unlocked_copy->TryLock(locking_deadline);
        if (!locks->status().ok()) {
          {
            // Restore the state of 'unlocked_' enabling retry of waiter resumption.
            UniqueLock l(mutex_);
            unlocked_.swap(unlocked_copy);
          }
          return locks->status().CloneAndPrepend(
            Format("$0Waiter failed to re-acquire in memory locks", LogPrefix()));
        }
      }
    }
    DEBUG_ONLY_TEST_SYNC_POINT("WaiterData::Impl::InvokeCallback:1");
    {
      UniqueLock l(mutex_);
      finished_waiting_latency_->Increment(MicrosSinceCreation());
      if (is_wait_queue_shutting_down_) {
        status = kShuttingDownError;
      }
    }
    VLOG_WITH_PREFIX(4) << "Invoking waiter callback " << status;
    callback(status, resume_ht);
    return Status::OK();
  }

  std::string LogPrefix() {
    return Format("TxnId: $0, ReqId: $1 ", id, request_id);
  }

  void SignalWaitQueueShutdown() {
    {
      UniqueLock l(mutex_);
      is_wait_queue_shutting_down_ = true;
    }
    InvokeCallbackOrWarn(kShuttingDownError);
  }

  bool IsSingleShard() const {
    return id.IsNil();
  }

  bool ShouldReRunConflictResolution() {
    auto refresh_waiter_timeout = GetAtomicFlag(&FLAGS_refresh_waiter_timeout_ms) * 1ms;
    if (IsSingleShard()) {
      if (refresh_waiter_timeout.count() > 0) {
        refresh_waiter_timeout = std::min(refresh_waiter_timeout, GetMaxSingleShardWaitDuration());
      } else {
        refresh_waiter_timeout = GetMaxSingleShardWaitDuration();
      }
    }
    if (PREDICT_TRUE(refresh_waiter_timeout.count() > 0)) {
      return MicrosSinceCreation() > GetMicros(refresh_waiter_timeout);
    }
    return false;
  }

  Result<WaiterLockStatusInfo> GetWaiterLockStatusInfo() const EXCLUDES(mutex_) {
    SharedLock lock(mutex_);
    auto info = WaiterLockStatusInfo {
      .id = id,
      .subtxn_id = subtxn_id,
      .wait_start = HybridTime::FromMicros(request_start_us),
      .intents = {},
      .blockers = {},
    };
    // 'intent_provider' should not be accessed when the waiter was resumed or is in the process
    // of resumption. The underlying function accesses fields of WriteQuery object, which could be
    // destructed sometime post waiter resumption.
    if (unlocked_) {
      info.blockers.reserve(blockers.size());
      for (const auto& blocker : blockers) {
        info.blockers.push_back(blocker.first);
      }
      info.intents = VERIFY_RESULT(intent_provider());
    }
    return info;
  }

  bool ShouldResumeBefore(const std::shared_ptr<WaiterData>& rhs) const {
    if (txn_start_us && rhs->txn_start_us && txn_start_us != rhs->txn_start_us) {
      return txn_start_us < rhs->txn_start_us;
    }
    return serial_no < rhs->serial_no;
  }

  CoarseTimePoint deadline() {
    return deadline_;
  }

 private:
  int64_t MicrosSinceCreation() const {
    return GetCurrentTimeMicros() - wq_entry_time.GetPhysicalValueMicros();
  }

  std::optional<UnlockedBatch> AtomicConsumeUnlockedBatch() EXCLUDES(mutex_) {
    UniqueLock l(mutex_);
    std::optional<UnlockedBatch> unlocked = std::nullopt;
    unlocked.swap(unlocked_);
    return unlocked;
  }

  scoped_refptr<EventStats>& finished_waiting_latency_;
  mutable rw_spinlock mutex_;
  std::optional<UnlockedBatch> unlocked_ GUARDED_BY(mutex_) = std::nullopt;
  bool is_wait_queue_shutting_down_ GUARDED_BY(mutex_) = false;
  CoarseTimePoint deadline_;
};

using WaiterDataPtr = std::shared_ptr<WaiterData>;

struct WaitingTxn : public std::enable_shared_from_this<WaitingTxn> {
 public:
  WaitingTxn(const TransactionId& id, const TabletId& status_tablet, rpc::Rpcs* rpcs,
             OperationCounter* in_progress_rpc_status_req_callbacks):
      id_(id), status_tablet_(status_tablet), rpcs_(*rpcs),
      in_progress_rpc_status_req_callbacks_(in_progress_rpc_status_req_callbacks) {}

  void AddWaiter(const WaiterDataPtr& waiter) {
    UniqueLock l(mutex_);
    waiters_.push_back(waiter);
  }

  std::vector<WaiterDataPtr> GetWaiters() const {
    SharedLock l(mutex_);
    return waiters_;
  }

  const TabletId& GetStatusTablet() EXCLUDES(mutex_) {
    SharedLock l(mutex_);
    return status_tablet_;
  }

  void UpdateStatusTablet(const TabletId& status_tablet) EXCLUDES(mutex_) {
    UniqueLock l(mutex_);
    status_tablet_ = status_tablet;
  }

  const TransactionId& id() const {
    return id_;
  }

  void InvokeCallbackOrWarn(
      Status waiter_status, HybridTime resume_ht = HybridTime::kInvalid,
      CoarseTimePoint locking_deadline = GetWaitForRelockUnblockedKeysDeadline()) EXCLUDES(mutex_) {
    for (const auto& waiter : PurgeWaiters()) {
      waiter->InvokeCallbackOrWarn(waiter_status, resume_ht, locking_deadline);
    }
  }

  void SignalWaitQueueShutdown() EXCLUDES(mutex_) {
    {
      UniqueLock l(mutex_);
      if (handle_ != rpcs_.InvalidHandle()) {
        (**handle_).Abort();
      }
      is_wait_queue_shutting_down_ = true;
    }
    for (const auto& waiter : PurgeWaiters()) {
      waiter->SignalWaitQueueShutdown();
    }
  }

  auto GetTotalBlockers() const EXCLUDES(mutex_) {
    SharedLock l(mutex_);
    size_t total_blockers = 0;
    for (const auto& waiter : waiters_) {
      total_blockers += waiter->blockers.size();
    }
    return total_blockers;
  }

  auto GetTotalWaitTime(HybridTime now) const EXCLUDES(mutex_) {
    SharedLock l(mutex_);
    MicrosTime total_wait_time = 0;
    for (const auto& waiter : waiters_) {
      auto duration_us =
          now.GetPhysicalValueMicros() - waiter->wq_entry_time.GetPhysicalValueMicros();
      auto seconds = duration_us * 1us / 1s;
      VLOG(4) << waiter->id << " waiting for " << seconds << " seconds";
      total_wait_time += duration_us;
    }
    return total_wait_time;
  }

  using PurgeWaitersFilter = std::function<bool(const WaiterDataPtr&)>;
  std::vector<WaiterDataPtr> PurgeWaiters(
      PurgeWaitersFilter filter = [](const auto& waiter) { return true; }) EXCLUDES(mutex_) {
    std::vector<WaiterDataPtr> waiters_to_signal;
    UniqueLock l(mutex_);
    for (auto it = waiters_.begin(); it != waiters_.end();) {
      if (filter(*it)) {
        waiters_to_signal.push_back(*it);
        it = waiters_.erase(it);
      } else {
        ++it;
      }
    }
    return waiters_to_signal;
  }

  std::vector<WaiterDataPtr> PurgeWaiter(int64_t request_id) EXCLUDES(mutex_) {
    auto purged_waiters = PurgeWaiters([request_id](const auto& waiter_data) {
      return waiter_data->request_id == request_id;
    });
    // WriteQuery(s) generated by ReadRpc requests have request_id assigned as -1. For all other
    // requests, request_id holds the value of the retryable request id generated by YBClient.
    // Hence we don't expect to see multiple requests of the transaction with same request_id,
    // with exception of -1.
    DCHECK(purged_waiters.size() <= 1 || request_id == -1)
        << "Found multiple waiters with the same request_id " << request_id;
    return purged_waiters;
  }

  auto GetNumWaiters() const EXCLUDES(mutex_) {
    SharedLock l(mutex_);
    return waiters_.size();
  }

  using StatusCb = std::function<
      void(const Status& status, const tserver::GetTransactionStatusResponsePB& resp)>;

  void TriggerStatusRequest(HybridTime now, client::YBClient* client, StatusCb cb) {
    UniqueLock l(mutex_);

    if (handle_ != rpcs_.InvalidHandle()) {
      VLOG_WITH_PREFIX(1)
          << "Skipping GetTransactionStatus RPC for waiter already having pending RPC";
      return;
    }

    tserver::GetTransactionStatusRequestPB req;
    DCHECK(!status_tablet_.empty());
    req.set_tablet_id(status_tablet_);
    req.add_transaction_id(id_.data(), id_.size());
    req.set_propagated_hybrid_time(now.ToUint64());
    rpcs_.RegisterAndStart(
        client::GetTransactionStatus(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client,
            &req,
            [instance = shared_from(this), cb](const auto& status, const auto& resp) {
                {
                  UniqueLock l(instance->mutex_);
                  if (instance->handle_ != instance->rpcs_.InvalidHandle()) {
                    instance->rpcs_.Unregister(&instance->handle_);
                  }
                  // The passed callback 'cb' is bound to a raw WaitQueue::Impl pointer. The below
                  // protects us from accessing freed up memory of WaitQueue::Impl, as we could
                  // get here after the wait queue instance has been destructed.
                  if (instance->is_wait_queue_shutting_down_) {
                    VLOG(1) << instance->LogPrefix() << "Skipping GetTransactionStatus RPC callback"
                            << " for waiter as the host wait-queue is shutting down.";
                    return;
                  }
                  instance->in_progress_rpc_status_req_callbacks_->Acquire();
                }
                AtomicFlagSleepMs(&FLAGS_TEST_delay_rpc_status_req_callback_ms);
                cb(status, resp);
                instance->in_progress_rpc_status_req_callbacks_->Release();
            }),
        &handle_);
  }

 private:
  std::string LogPrefix() {
    return Format("TxnId: $0 ", id_);
  }

  const TransactionId id_;
  mutable rw_spinlock mutex_;
  TabletId status_tablet_ GUARDED_BY(mutex_) = "";
  std::vector<WaiterDataPtr> waiters_ GUARDED_BY(mutex_);
  rpc::Rpcs& rpcs_;
  rpc::Rpcs::Handle handle_ GUARDED_BY(mutex_) = rpcs_.InvalidHandle();
  bool is_wait_queue_shutting_down_ GUARDED_BY(mutex_) = false;
  // Raw pointer to the host wait-queue's 'in_progress_rpc_status_req_callbacks_'. Shouldn't be
  // operated on when 'is_wait_queue_shutting_down_' is true.
  OperationCounter* in_progress_rpc_status_req_callbacks_;
};

using WaitingTxnPtr = std::shared_ptr<WaitingTxn>;

// Data for an active transaction which is blocking another active transaction which is waiting
// in the wait queue. Contains a list of weak_ptr to WaiterData (see below) of corresponding waiting
// transactions. If a weak_ptr stored in this list expires, we can assume the waiter is no longer
// active and has exited the wait queue.
class BlockerData {
 public:
  BlockerData(const TransactionId& id, const TabletId& status_tablet)
    : id_(id), status_tablet_(status_tablet) {}

  const TransactionId& id() const { return id_; }

  const TabletId& status_tablet() EXCLUDES(mutex_) {
    SharedLock r_lock(mutex_);
    return status_tablet_;
  }

  std::vector<WaiterDataPtr> Signal(
      Result<TransactionStatusResult>&& txn_status_response, HybridTime now) {
    VLOG_WITH_PREFIX(4) << "Signaling waiters "
            << (txn_status_response.ok() ?
                txn_status_response->ToString() :
                txn_status_response.status().ToString());

    UniqueLock l(mutex_);
    bool was_txn_local = !IsPromotedUnlocked();

    // TODO(wait-queues): Track status hybrid times and ignore old status updates
    txn_status_or_res_ = UnwrapResult(txn_status_response);
    bool is_txn_pending = IsPendingUnlocked();

    // txn_underwent_promotion is set to true only on the first call to Signal with
    // TransactionStatusResult as PROMOTED.
    bool txn_underwent_promotion = was_txn_local && IsPromotedUnlocked();
    bool should_signal = !is_txn_pending || txn_underwent_promotion;

    if (txn_underwent_promotion) {
      status_tablet_ = txn_status_response->status_tablet;
    }

    if (txn_status_response.ok()) {
      DCHECK(!txn_status_response->status_time.is_special() || IsAbortedUnlocked());
      txn_status_ht_ = txn_status_response->status_time;
      if (aborted_subtransactions_ != txn_status_response->aborted_subtxn_set &&
          PREDICT_TRUE(!FLAGS_TEST_skip_waiter_resumption_on_blocking_subtxn_rollback)) {
        // TODO(wait-queues): Avoid copying the subtransaction set. See:
        // https://github.com/yugabyte/yugabyte-db/issues/13823
        aborted_subtransactions_ = std::move(txn_status_response->aborted_subtxn_set);
        should_signal = true;
      }
    }

    if (should_signal) {
      if (txn_status_ht_.is_special()) {
        DCHECK(IsAbortedUnlocked())
          << "Unexpected special status ht in blocker " << txn_status_or_res_
          << " @ " << txn_status_ht_;
        txn_status_ht_ = now;
      }
      return GetWaitersToSignalUnlocked();
    }

    return {};
  }

  std::vector<WaiterDataPtr> GetWaitersToSignal() EXCLUDES(mutex_) {
    UniqueLock l(mutex_);
    return GetWaitersToSignalUnlocked();
  }

  std::vector<WaiterDataPtr> GetWaitersToSignalUnlocked() REQUIRES(mutex_) {
    std::vector<WaiterDataPtr> waiters_to_signal;
    waiters_to_signal.reserve(waiters_.size() + post_resolve_waiters_.size());
    EraseIf([&](const auto& weak_waiter) REQUIRES(mutex_) {
      if (auto waiter = weak_waiter.lock()) {
        waiters_to_signal.push_back(waiter);
        return false;
      }
      return true;
    }, &waiters_);
    EraseIf([&](const auto& weak_waiter) REQUIRES(mutex_) {
      if (auto waiter = weak_waiter.lock()) {
        waiters_to_signal.push_back(waiter);
        return false;
      }
      return true;
    }, &post_resolve_waiters_);
    // TODO(wait-queues): Instead of sorting here, we could modify the API of ResumedWaiterRunner to
    // atomically accept a batch of waiters, and rework some of the upstream code to provide that.
    std::sort(waiters_to_signal.begin(), waiters_to_signal.end(),
        [](const auto& lhs, const auto& rhs) {
      return lhs->ShouldResumeBefore(rhs);
    });
    if (VLOG_IS_ON(4)) {
      std::vector<std::string> waiters;
      for (const auto& waiter : waiters_to_signal) {
        waiters.push_back(waiter->id.ToString());
      }
      VLOG_WITH_PREFIX(4) << "Signaling waiters: " << boost::algorithm::join(waiters, ",");
    }
    return waiters_to_signal;
  }

  void AddWaiter(const WaiterDataPtr& waiter_data) {
    UniqueLock blocker_lock(mutex_);
    if (IsPendingUnlocked()) {
      waiters_.push_back(waiter_data);
    } else {
      post_resolve_waiters_.push_back(waiter_data);
    }
  }

  Result<bool> IsResolved() {
    SharedLock blocker_lock(mutex_);
    return !IsPendingUnlocked();
  }

  bool IsPending() {
    SharedLock blocker_lock(mutex_);
    return IsPendingUnlocked();
  }

  bool IsPendingUnlocked() REQUIRES_SHARED(mutex_) {
    return txn_status_or_res_.ok() && (*txn_status_or_res_ == ResolutionStatus::kPending ||
                                       *txn_status_or_res_ == ResolutionStatus::kPromoted);
  }

  bool IsAbortedUnlocked() REQUIRES_SHARED(mutex_) {
    return txn_status_or_res_.ok() && (*txn_status_or_res_ == ResolutionStatus::kAborted ||
                                       *txn_status_or_res_ == ResolutionStatus::kDeadlocked);
  }

  bool IsPromotedUnlocked() REQUIRES_SHARED(mutex_) {
    return txn_status_or_res_.ok() && *txn_status_or_res_ == ResolutionStatus::kPromoted;
  }

  auto CleanAndGetSize() {
    UniqueLock l(mutex_);
    EraseIf([](const auto& weak_waiter) {
      return weak_waiter.expired();
    }, &waiters_);
    EraseIf([](const auto& weak_waiter) {
      return weak_waiter.expired();
    }, &post_resolve_waiters_);
    if (waiters_.size() == 0 && !IsPendingUnlocked()) {
      // If we hit this branch, then we've processed all waiters which arrived before this blocker
      // was first resolved. Therefore, new incoming requests need not worry about starving the
      // original waiters any more, and can ignore this blocker. This may cause marginal amounts of
      // starvation for any waiters stuck in post_resolve_waiters_, but such starvation should be
      // rare and unlikely to repeat.
      is_active_ = false;
    }
    return waiters_.size() + post_resolve_waiters_.size();
  }

  // Returns true if an incoming waiter should consider this blocker for purposes of fairness.
  auto IsActive() {
    SharedLock l(mutex_);
    return is_active_;
  }

  auto DEBUG_GetWaiterIds() const {
    std::vector<std::string> waiters;
    SharedLock l(mutex_);
    for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
      if (auto waiter = it->lock()) {
        waiters.push_back(waiter->id.ToString());
      }
    }
    return boost::algorithm::join(waiters, ",");
  }

  bool HasLiveSubtransaction(
      const decltype(TransactionConflictInfo::subtransactions)& subtransaction_info) {
    SharedLock blocker_lock(mutex_);
    for (const auto& [id, _] : subtransaction_info) {
      if (!aborted_subtransactions_.Test(id)) {
        return true;
      }
    }
    return false;
  }

  HybridTime status_ht() const {
    SharedLock l(mutex_);
    return txn_status_ht_;
  }

  void AddIntents(const TransactionConflictInfo& conflict_info) {
    UniqueLock l(mutex_);
    for (const auto& [subtxn_id, data] : conflict_info.subtransactions) {
      auto& intents_by_key = subtxn_intents_by_key_[subtxn_id];
      for (const auto& lock : data.locks) {
        auto [iter, did_insert] = intents_by_key.emplace(lock.doc_path, lock.intent_types);
        if (!did_insert) {
          // Combine new intent types with existing intent types.
          iter->second |= lock.intent_types;
        }
      }
    }
  }

  // Find any of this blockers known conflicts with provided doc_path and intent_type_set. If
  // conflict_info is not null, populate conflict information. Otherwise, early return true to
  // indicate a conflict was found. Return false if no conflict is found.
  bool PopulateConflictInfo(
      const RefCntPrefix& doc_path, const dockv::IntentTypeSet& intent_type_set,
      TransactionConflictInfo* conflict_info) {
    bool did_find_conflicts = false;
    SharedLock l(mutex_);
    for (const auto& [subtxn_id, intents_by_key] : subtxn_intents_by_key_) {
      auto it = intents_by_key.find(doc_path);
      if (it != intents_by_key.end() && IntentTypeSetsConflict(intent_type_set, it->second)) {
        if (!conflict_info) {
          return true;
        }
        did_find_conflicts = true;
        conflict_info->subtransactions[subtxn_id].locks.emplace_back(
            LockInfo {doc_path, intent_type_set});
      }
    }
    return did_find_conflicts;
  }

  void DumpHtmlTableRow(std::ostream& out) {
    SharedLock l(mutex_);
    out << "<tr>"
          << "<td>|" << id_ << "</td>"
          << "<td>|" << txn_status_or_res_ << "</td>"
        << "</tr>" << std::endl;
  }


 private:
  std::string LogPrefix() {
    return Format("TxnId: $0 ", id_);
  }

  const TransactionId id_;
  TabletId status_tablet_ GUARDED_BY(mutex_);;
  mutable rw_spinlock mutex_;
  Result<ResolutionStatus> txn_status_or_res_ GUARDED_BY(mutex_) = ResolutionStatus::kPending;
  HybridTime txn_status_ht_ GUARDED_BY(mutex_) = HybridTime::kMin;
  SubtxnSet aborted_subtransactions_ GUARDED_BY(mutex_);
  std::vector<std::weak_ptr<WaiterData>> waiters_ GUARDED_BY(mutex_);
  std::vector<std::weak_ptr<WaiterData>> post_resolve_waiters_ GUARDED_BY(mutex_);
  bool is_active_ GUARDED_BY(mutex_) = true;

  using IntentsByKey = std::unordered_map<
      RefCntPrefix,
      dockv::IntentTypeSet,
      RefCntPrefixHash>;
  std::unordered_map<SubTransactionId, IntentsByKey> subtxn_intents_by_key_ GUARDED_BY(mutex_);
};

struct SerialWaiter {
  WaiterDataPtr waiter;
  HybridTime resolve_ht;
  bool operator()(const SerialWaiter& w1, const SerialWaiter& w2) const {
    // Reverse operator order to ensure we pop off items with lower start time first, since the
    // priority_queue implementation is a max PQ.
    return !w1.waiter->ShouldResumeBefore(w2.waiter);
  }
};

void ResumeContentiousWaiter(const SerialWaiter& to_resume) {
  DCHECK(PREDICT_TRUE(ThreadRestrictions::IsIOAllowed()))
      << "IO disallowed, but the thread might have to perform IO operations downstearm.";

  auto& waiter = to_resume.waiter;
  VLOG(1) << "Resuming waiter " << waiter->LogPrefix() << "on Scheduler.";

  auto start_time = MonoTime::Now();
  auto deadline = waiter->deadline();
  auto s = waiter->InvokeCallback(Status::OK(), to_resume.resolve_ht, deadline);
  if (s.ok()) {
    VLOG(4) << "Successfully executed waiter " << waiter->LogPrefix() << "callback on Scheduler.";
    return;
  }

  s = s.CloneAndAppend(Format("timeout: $0", deadline - ToCoarse(start_time)));
  VLOG(1) << "Couldn't resume waiter on Scheduler: " << s;
  DCHECK_OK(waiter->InvokeCallback(s));
}

// Resumes waiters async, in serial, and in the order of the waiter's serial_no, running the lowest
// serial number first in a best effort manner.
class ResumedWaiterRunner {
 public:
  ResumedWaiterRunner(
      ThreadPoolToken* thread_pool_token, const std::string& log_prefix, rpc::Messenger* messenger)
          : thread_pool_token_(DCHECK_NOTNULL(thread_pool_token)), log_prefix_(log_prefix),
            messenger_(messenger) {}

  void Submit(
      const WaiterDataPtr& waiter, const Status& status,
      HybridTime resolve_ht = HybridTime::kInvalid) {
    {
      UniqueLock l(mutex_);
      if (PREDICT_FALSE(shutting_down_)) {
        // We shouldn't push new entries onto 'pq_' now as we wouldn't be sure if they would be
        // processed by thread_pool_token_. Additionally, we cannot skip executing the waiter's
        // callback here as the record might have already been erased from waiter_status_. Else,
        // we risk dropping execution of the callback all together.
        l.unlock();
        waiter->SignalWaitQueueShutdown();
        return;
      }
      AddWaiter(waiter, status, resolve_ht);
    }
    TriggerPoll();
  }

  void StartShutdown() {
    // thread_pool_token_ shouldn't be Shutdown here since the callee executes this method within
    // the scope of TabletPeer::lock_. thread_pool_token_->Shutdown() would wait for the running
    // thread to complete its execution, and the running thread could be in the process of resuming
    // a waiter with Status::OK() and end up waiting for TabletPeer::lock_ during creation of an
    // operation driver for replication, thus resulting in a deadlock.
    std::vector<WaiterDataPtr> waiters;
    {
      UniqueLock l(mutex_);
      waiters.reserve(pq_.size() + contentious_waiters_.size());
      while (!pq_.empty()) {
        waiters.push_back(std::move(pq_.top().waiter));
        pq_.pop();
      }
      for (auto& weak_waiter : contentious_waiters_) {
        if (auto shared_waiter = weak_waiter.lock()) {
          waiters.push_back(std::move(shared_waiter));
        }
      }
      contentious_waiters_.clear();
      shutting_down_ = true;
    }
    for (const auto& waiter : waiters) {
      waiter->SignalWaitQueueShutdown();
    }
  }

  void CompleteShutdown() {
    thread_pool_token_->Shutdown();
    SharedLock l(mutex_);
    LOG_IF_WITH_PREFIX(DFATAL, !shutting_down_) << "Called when not in shutting_down_ state.";
    LOG_IF_WITH_PREFIX(DFATAL, !pq_.empty())
        << "pq_ found in non-empty state. Could block tablet shutdown.";
    LOG_IF_WITH_PREFIX(DFATAL, !contentious_waiters_.empty())
        << "contentious_waiters_ found in non-empty state. Could block tablet shutdown.";
  }

  void CleanContentiousWeakWaiterDatas() EXCLUDES(mutex_) {
    UniqueLock l(mutex_);
    EraseIf([](const auto& weak_waiter) {
      return weak_waiter.expired();
    }, &contentious_waiters_);
  }

 private:
  void TriggerPoll() {
    WARN_NOT_OK(thread_pool_token_->SubmitFunc([this]() {
      std::vector<SerialWaiter> unresumed_waiters;
      for (;;) {
        WaiterDataPtr waiter;
        HybridTime resolve_ht;
        {
          UniqueLock l(this->mutex_);
          if (pq_.empty()) {
            break;
          }
          waiter = pq_.top().waiter;
          resolve_ht = pq_.top().resolve_ht;
          pq_.pop();
          VLOG_WITH_PREFIX(4) << "Popped waiter " << waiter->id
                              << " with request_id: " << waiter->request_id
                              << " and start time (us): " << waiter->txn_start_us;
        }
        auto s = waiter->InvokeCallback(Status::OK(), resolve_ht);
        if (!s.ok()) {
          VLOG_WITH_PREFIX(1) << "Waiter's InvokeCallback failed: " << s;
          unresumed_waiters.push_back(SerialWaiter {
            .waiter = waiter,
            .resolve_ht = resolve_ht
          });
        }
      }

      bool is_shutting_down = false;
      {
        UniqueLock l(this->mutex_);
        if (shutting_down_) {
          is_shutting_down = true;
        } else {
          contentious_waiters_.reserve(contentious_waiters_.size() + unresumed_waiters.size());
          for (auto& serial_waiter : unresumed_waiters) {
            contentious_waiters_.push_back(serial_waiter.waiter);
          }
        }
      }

      for (auto& serial_waiter : unresumed_waiters) {
        if (is_shutting_down) {
          // A shutdown request could have arrived after the waiter was popped off pq_, and we
          // could have failed to invoke the waiter's callback due to inability to obtain the
          // shared in-memory locks. Instead of scheduling the callback, execute the callback
          // in-line here.
          serial_waiter.waiter->SignalWaitQueueShutdown();
        } else {
          VLOG_WITH_PREFIX(1) << "Scheduling waiter " << serial_waiter.waiter->LogPrefix()
                              << "resumption on the Tablet Server's Scheduler.";
          messenger_->scheduler().Schedule([serial_waiter](const Status& s) {
            if (!s.ok()) {
              serial_waiter.waiter->InvokeCallbackOrWarn(
                  s.CloneAndPrepend("Failed scheduling contentious waiter resumption: "));
              return;
            }
            ResumeContentiousWaiter(serial_waiter);
          }, std::chrono::milliseconds(100));
        }
      }
    }), "Failed to trigger poll of ResumedWaiterRunner in wait queue");
  }

  void AddWaiter(const WaiterDataPtr& waiter, const Status& status,
                 HybridTime resolve_ht) REQUIRES(mutex_) {
    if (status.ok()) {
      pq_.push(SerialWaiter {
        .waiter = waiter,
        .resolve_ht = resolve_ht,
      });
      VLOG_WITH_PREFIX(4) << "Added waiter " << waiter->id
                          << " with request_id: " << waiter->request_id
                          << " and start time (us): " << waiter->txn_start_us;
    } else {
      // If error status, resume waiter right away, no need to respect serial_no
      WARN_NOT_OK(thread_pool_token_->SubmitFunc([waiter, status]() {
        waiter->InvokeCallbackOrWarn(status);
      }), "Failed to submit waiter resumption");
    }
  }

  std::string LogPrefix() const {
    return Format("ResumedWaiterRunner: $0", log_prefix_);
  }

  mutable rw_spinlock mutex_;
  std::priority_queue<
      SerialWaiter, std::vector<SerialWaiter>, SerialWaiter> pq_ GUARDED_BY(mutex_);
  // Set of weak waiter data pointers scheduled for resumption.
  std::vector<std::weak_ptr<WaiterData>> contentious_waiters_ GUARDED_BY(mutex_);
  ThreadPoolToken* thread_pool_token_;
  bool shutting_down_ GUARDED_BY(mutex_) = false;
  const std::string log_prefix_;
  rpc::Messenger* messenger_;
};

} // namespace

class WaitQueue::Impl {
 public:
  Impl(TransactionStatusManager* txn_status_manager, const std::string& permanent_uuid,
       WaitingTxnRegistry* waiting_txn_registry,
       const std::shared_future<client::YBClient*>& client_future,
       const server::ClockPtr& clock, const MetricEntityPtr& metrics,
       std::unique_ptr<ThreadPoolToken> thread_pool_token,
       rpc::Messenger* messenger)
      : txn_status_manager_(txn_status_manager), permanent_uuid_(permanent_uuid),
        waiting_txn_registry_(waiting_txn_registry), client_future_(client_future), clock_(clock),
        thread_pool_token_(std::move(thread_pool_token)),
        waiter_runner_(thread_pool_token_.get(), LogPrefix(), messenger),
        pending_time_waiting_(METRIC_wait_queue_pending_time_waiting.Instantiate(metrics)),
        finished_waiting_latency_(METRIC_wait_queue_finished_waiting_latency.Instantiate(metrics)),
        blockers_per_waiter_(METRIC_wait_queue_blockers_per_waiter.Instantiate(metrics)),
        waiters_per_blocker_(METRIC_wait_queue_waiters_per_blocker.Instantiate(metrics)),
        total_waiters_(METRIC_wait_queue_num_waiters.Instantiate(metrics, 0)),
        total_blockers_(METRIC_wait_queue_num_blockers.Instantiate(metrics, 0)),
        in_progress_rpc_status_req_callbacks_(LogPrefix()) {}

  ~Impl() {
    if (StartShutdown()) {
      CompleteShutdown();
    } else {
      LOG_IF_WITH_PREFIX(DFATAL, !shutdown_complete_.load())
          << "Destroying wait queue that did not complete shutdown";
    }
  }

  // Find blockers in this wait queue which conflict with the provided locks. If the blockers
  // argument is not null, populate conflict information any time a conflict is found. Otherwise,
  // return true as soon as any conflict is found. Return false if there are no conflicts.
  bool PopulateBlockersUnlocked(
      const TransactionId& waiter_txn_id, LockBatch* locks,
      BlockerDataConflictMap* blockers = nullptr) REQUIRES_SHARED(mutex_) {
    for (const auto& entry : locks->Get()) {
      auto it = blockers_by_key_.find(entry.key);
      if (it == blockers_by_key_.end()) {
        VLOG_WITH_PREFIX(5) << "No blockers found for key " << entry.key.ToString();
        continue;
      }
      VLOG_WITH_PREFIX(4) << "Found blockers for key " << entry.key.ToString();

      for (const auto& blocker_id : it->second) {
        if (blocker_id == waiter_txn_id) {
          continue;
        }
        auto blocker_it = blocker_status_.find(blocker_id);
        if (blocker_it == blocker_status_.end()) {
          LOG(DFATAL) << "Unexpected blocker found in blockers_by_key_ but not blocker_status_";
          continue;
        }

        auto blocker = blocker_it->second.lock();
        if (!blocker || !blocker->IsActive()) {
          continue;
        }
        DCHECK_NE(blocker->id(), waiter_txn_id);

        TransactionConflictInfoPtr conflict_info;
        bool should_emplace_conflict_info = false;
        if (blockers) {
          auto it = blockers->find(blocker->id());
          if (it == blockers->end()) {
            // Need to use a new conflict_info instance and emplace it on the blockers map in case
            // we do find a conflict. We should avoid creating entries in blockers if there is not
            // a genuine conflict.
            conflict_info = std::make_shared<TransactionConflictInfo>();
            should_emplace_conflict_info = true;
          } else {
            // If there is already an entry for this blocker, populate the same conflict info.
            conflict_info = it->second.second;
          }
        }

        if (blocker->PopulateConflictInfo(entry.key, entry.intent_types, conflict_info.get())) {
          VLOG_WITH_PREFIX(3) << "Found conflict for " << waiter_txn_id
                              << " on blocker " << blocker->id();
          if (!blockers) {
            return true;
          }
          if (should_emplace_conflict_info) {
            VLOG_WITH_PREFIX(5) << "Emplacing conflict info for " << waiter_txn_id
                                << " on blocker " << blocker->id();
            auto emplace_it = DCHECK_NOTNULL(blockers)->emplace(
                blocker->id(), std::make_pair(blocker, conflict_info));
            DCHECK(emplace_it.first->second.first == blocker);
          }
        }
      }
    }
    return blockers && !blockers->empty();
  }

  Result<bool> MaybeWaitOnLocks(
      const TransactionId& waiter_txn_id, SubTransactionId subtxn_id, LockBatch* locks,
      const TabletId& status_tablet_id, uint64_t serial_no,
      int64_t txn_start_us, uint64_t request_start_us, int64_t request_id, CoarseTimePoint deadline,
      IntentProviderFunc intent_provider, WaitDoneCallback callback) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "waiter_txn_id=" << waiter_txn_id
                                 << " request_id=" << request_id
                                 << " txn_start_us=" << txn_start_us
                                 << " request_start_us=" << request_start_us
                                 << " status_tablet_id=" << status_tablet_id;
    bool found_blockers = false;
    {
      // First check if there are /any/ conflicts using a shared lock.
      SharedLock wq_lock(mutex_);
      found_blockers = PopulateBlockersUnlocked(waiter_txn_id, locks);
    }

    if (found_blockers) {
      // If there were conflicts, acquire a unique lock, get all conflicts, and insert a new waiter
      // into the wait queue.
      BlockerDataConflictMap blockers_map;
      UniqueLock wq_lock(mutex_);

      if (PopulateBlockersUnlocked(waiter_txn_id, locks, &blockers_map)) {
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Found " << blockers_map.size()
                                     << " blockers for " << waiter_txn_id;

        std::vector<BlockerDataAndConflictInfo> blocker_datas;
        auto blockers = std::make_shared<ConflictDataManager>(blockers_map.size());
        blocker_datas.reserve(blockers_map.size());
        for (const auto& [id, blocker_data] : blockers_map) {
          blocker_datas.emplace_back(blocker_data);
          blockers->AddTransaction(id, blocker_data.second, blocker_data.first->status_tablet());
        }

        RETURN_NOT_OK(SetupWaiterUnlocked(
          waiter_txn_id, subtxn_id, locks, status_tablet_id, serial_no, txn_start_us,
          request_start_us, request_id, std::move(intent_provider), std::move(callback),
          std::move(blocker_datas), std::move(blockers), deadline));
        TRACE("pre-wait will block");
        return true;
      } else {
        // It's possible that between checking above with a shared lock and checking again with a
        // unique lock that conflicting blockers have become inactive.
        VLOG_WITH_PREFIX_AND_FUNC(1)
            << "Pre-wait second check found no blockers for " << waiter_txn_id;
      }
    } else {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Pre-wait found no blockers for " << waiter_txn_id;
    }

    TRACE("pre-wait no blocks");
    return false;
  }

  Status WaitOn(
      const TransactionId& waiter_txn_id, SubTransactionId subtxn_id, LockBatch* locks,
      std::shared_ptr<ConflictDataManager> blockers, const TabletId& status_tablet_id,
      uint64_t serial_no, int64_t txn_start_us, uint64_t request_start_us, int64_t request_id,
      CoarseTimePoint deadline, IntentProviderFunc intent_provider, WaitDoneCallback callback) {
    TRACE_FUNC();
    AtomicFlagSleepMs(&FLAGS_TEST_sleep_before_entering_wait_queue_ms);
    VLOG_WITH_PREFIX_AND_FUNC(4) << "waiter_txn_id=" << waiter_txn_id
                                 << " request_id=" << request_id
                                 << " txn_start_us=" << txn_start_us
                                 << " request_start_us=" << request_start_us
                                 << " blockers=" << *blockers
                                 << " status_tablet_id=" << status_tablet_id;

    // TODO(wait-queues): We can detect tablet-local deadlocks here.
    // See https://github.com/yugabyte/yugabyte-db/issues/13586
    std::vector<BlockerDataAndConflictInfo> blocker_datas;
    {
      UniqueLock wq_lock(mutex_);
      if (shutting_down_) {
        return kShuttingDownError;
      }

      for (auto& blocker : blockers->RemainingTransactions()) {
        auto blocker_data = std::make_shared<BlockerData>(blocker.id, blocker.status_tablet);

        auto [iter, did_insert] = blocker_status_.emplace(blocker.id, blocker_data);
        if (!did_insert) {
          if (auto placed_blocker_node = iter->second.lock()) {
            VLOG_WITH_PREFIX_AND_FUNC(4) << "Re-using blocker " << blocker.id;
            blocker_data = placed_blocker_node;
          } else {
            // TODO(wait-queues): We should only ever hit this case if a blocker was resolved and
            // all references to it in old waiters were destructed. Perhaps we can remove this
            // dangling reference from blocker_status_ and return Status indicating that conflict
            // resolution should be retried since the status of its blockers may have changed, in
            // case we end up in this branch for all blockers.
            VLOG_WITH_PREFIX_AND_FUNC(4) << "Replacing blocker " << blocker.id;
            blocker_status_[blocker.id] = blocker_data;
          }
        } else {
          VLOG_WITH_PREFIX_AND_FUNC(4) << "Created blocker " << blocker.id;
        }
        // Update the waiter txn record with the latest blocker txn's status tablet. This is
        // necessary because of the potential race between handling promotion signal of a blocker
        // txn from the transaction participant and a waiter transaction entering the queue with
        // the blocker's old status tablet.
        auto blocker_status_tablet =
            VERIFY_RESULT(txn_status_manager_->FindStatusTablet(blocker.id));
        if (blocker_status_tablet) {
          blocker.status_tablet = *blocker_status_tablet;
        }

        blocker_data->AddIntents(*DCHECK_NOTNULL(blocker.conflict_info));
        blocker_datas.emplace_back(blocker_data, blocker.conflict_info);

        for (const auto& [_, subtxn_data] : blocker.conflict_info->subtransactions) {
          for (const auto& lock : subtxn_data.locks) {
            blockers_by_key_[lock.doc_path].insert(blocker.id);
          }
        }
      }

      return SetupWaiterUnlocked(
          waiter_txn_id, subtxn_id, locks, status_tablet_id, serial_no, txn_start_us,
          request_start_us, request_id,  std::move(intent_provider), std::move(callback),
          std::move(blocker_datas), std::move(blockers), deadline);
    }
  }

  Status SetupWaiterUnlocked(
      const TransactionId& waiter_txn_id, SubTransactionId subtxn_id, LockBatch* locks,
      const TabletId& status_tablet_id, uint64_t serial_no,
      int64_t txn_start_us, uint64_t request_start_us, int64_t request_id,
      IntentProviderFunc intent_provider, WaitDoneCallback callback,
      std::vector<BlockerDataAndConflictInfo>&& blocker_datas,
      std::shared_ptr<ConflictDataManager> blockers, CoarseTimePoint deadline) REQUIRES(mutex_) {
    // TODO(wait-queues): similar to pg, we can wait 1s or so before beginning deadlock detection.
    // See https://github.com/yugabyte/yugabyte-db/issues/13576
    TRACE_FUNC();
    ASH_ENABLE_CONCURRENT_UPDATES();
    SET_WAIT_STATUS(ConflictResolution_WaitOnConflictingTxns);
    auto scoped_reporter = waiting_txn_registry_->Create();
    if (waiter_txn_id.IsNil()) {
      // If waiter_txn_id is Nil, then we're processing a single-shard transaction. We do not have
      // to report single shard transactions to transaction coordinators because they can't
      // possibly be involved in a deadlock. This is true because no transactions can wait on
      // single shard transactions, so they only have out edges in the wait-for graph and cannot
      // be a part of a cycle.
      //
      // We still register the single shard waiters with the local waiting transaction registry
      // for the purpose of observability in pg_locks.
      RETURN_NOT_OK(scoped_reporter->RegisterSingleShardWaiter(
          txn_status_manager_->tablet_id(), request_start_us));
    } else {
      DCHECK(!status_tablet_id.empty());
      // TODO(wait-queues): Instead of moving blockers to local_waiting_txn_registry, we could store
      // the blockers in the waiter_data record itself. That way, we could avoid re-running conflict
      // resolution on blocker promotion and directly update the transaction coordinator with the
      // latest wait-for probes.
      // TODO(wait-queues): During transaction promotion, it's possible that we have two waiting
      // requests registered at the local waiting txn registry but with different status tablets.
      // In this case, we are simply using status_tablet_id provided by the caller, which may differ
      // from the status_tablet_id with which the previous request was registered. This may add
      // latency in deadlock detection, which should be addresed by GHI #21243.
      RETURN_NOT_OK(scoped_reporter->Register(
          waiter_txn_id, request_id, std::move(blockers), status_tablet_id));
      DCHECK_GE(scoped_reporter->GetDataUseCount(), 1);
    }

    auto waiter_data = std::make_shared<WaiterData>(
        waiter_txn_id, subtxn_id, locks, serial_no, txn_start_us, request_start_us, request_id,
        clock_->Now(), status_tablet_id, std::move(blocker_datas), std::move(intent_provider),
        std::move(callback), std::move(scoped_reporter), &finished_waiting_latency_, deadline);
    if (waiter_data->IsSingleShard()) {
      DCHECK(single_shard_waiters_.size() == 0 ||
             waiter_data->wq_entry_time >= single_shard_waiters_.front()->wq_entry_time);
      single_shard_waiters_.push_front(waiter_data);
    } else {
      auto [iter, _] = waiter_status_.emplace(waiter_txn_id, std::make_shared<WaitingTxn>(
          waiter_txn_id, status_tablet_id, &rpcs_, &in_progress_rpc_status_req_callbacks_));
      iter->second->AddWaiter(waiter_data);
      if (iter->second->GetStatusTablet() != status_tablet_id) {
        // In case this txn already had a WaitingTxn, it's possible that the status tablet stored
        // differs from the status tablet specified for this new request. This can happen if the
        // transaction was promoted and the status tablet changed. In such cases, we should update
        // the status tablet stored in the WaitingTxn based on the status tablet currently known to
        // the transaction participant. If the participant is tracking the old status tablet, then
        // it will eventually process the promotion signal for that transaction and we will handle
        // updating this WaitingTxn with the new status tablet at that time.
        auto new_status_tablet =
            VERIFY_RESULT(txn_status_manager_->FindStatusTablet(waiter_txn_id));
        if (new_status_tablet) {
          LOG(INFO) << "Updating status tablet for " << waiter_txn_id << " and request_id "
                    << request_id << " from " << iter->second->GetStatusTablet() << " to "
                    << *new_status_tablet << ". "
                    << "This may happen for requests sent during transaction promotion.";
          iter->second->UpdateStatusTablet(*new_status_tablet);
        } else {
          // This may happen if two requests for the same transaction are sent to the same tablet,
          // one before promotion and another after, and both before the transaction participant
          // has registered this transaction. In such a case, the client will not abort the txn at
          // the old status tablet until all outstanding requests are completed, so we can safely
          // use either status tablet.
          LOG(WARNING) << "Failed to find status tablet for " << waiter_txn_id
                       <<  " and request_id " << request_id << ", "
                       << "but existing status tablet " << iter->second->GetStatusTablet()
                       << " differs from new status tablet " << status_tablet_id << ". "
                       << "This should be rare, but is not an error.";
        }
      }
    }

    // We must add waiters to blockers while holding the wait queue mutex. Otherwise, we may
    // end up removing blockers from blocker_status_ during Poll() after we've already added them
    // to waiter_data.
    for (auto [blocker, _] : waiter_data->blockers) {
      blocker->AddWaiter(waiter_data);
    }
    DEBUG_ONLY_TEST_SYNC_POINT("WaitQueue::Impl::SetupWaiterUnlocked:1");
    return Status::OK();
  }

  void Poll(HybridTime now) EXCLUDES(mutex_) {
    // TODO(wait-queues): Rely on signaling from the RunningTransaction instance of the blocker
    // rather than this polling-based mechanism. We should also signal from the RunningTransaction
    // instance of the waiting transaction in case the waiter is aborted by deadlock or otherwise.
    // See https://github.com/yugabyte/yugabyte-db/issues/13578
    const std::string kBlockerReason = "Getting status for blocker wait queue";
    const std::string kWaiterReason = "Getting status for waiter in wait queue";
    std::vector<WaitingTxnPtr> waiting_txns;
    std::vector<TransactionId> blockers;
    std::vector<WaiterDataPtr> stale_single_shard_waiters;

    {
      UniqueLock l(mutex_);
      if (shutting_down_) {
        return;
      }
      for (auto it = waiter_status_.begin(); it != waiter_status_.end();) {
        auto& waiting_txn = it->second;
        if (waiting_txn->GetNumWaiters() == 0) {
          it = waiter_status_.erase(it);
          continue;
        }
        blockers_per_waiter_->Increment(waiting_txn->GetTotalBlockers());
        DCHECK(!waiting_txn->GetStatusTablet().empty());
        waiting_txns.push_back(waiting_txn);
        it++;
      }
      for (auto it = blocker_status_.begin(); it != blocker_status_.end();) {
        if (auto blocker = it->second.lock()) {
          auto num_waiters = blocker->CleanAndGetSize();
          if (num_waiters != 0) {
            VLOG_WITH_PREFIX(4)
                << Format("blocker($0) has waiters($1)", it->first, blocker->DEBUG_GetWaiterIds());
            blockers.push_back(it->first);
            it++;
            waiters_per_blocker_->Increment(num_waiters);
            continue;
          }
          VLOG_WITH_PREFIX(4) << "Erasing blocker with no live waiters " << it->first;
        } else {
          VLOG_WITH_PREFIX(4) << "Erasing blocker with no references " << it->first;
        }
        it = blocker_status_.erase(it);
      }
      for (auto it = blockers_by_key_.begin(); it != blockers_by_key_.end();) {
        auto& key_blockers = it->second;
        auto has_live = false;
        for (auto txn_id_it = key_blockers.begin(); txn_id_it != key_blockers.end();) {
          auto blocker_it = blocker_status_.find(*txn_id_it);
          if (blocker_it == blocker_status_.end()) {
            txn_id_it = key_blockers.erase(txn_id_it);
          } else {
            has_live = true;
            txn_id_it++;
          }
        }
        if (has_live) {
          it++;
        } else {
          it = blockers_by_key_.erase(it);
        }
      }
      EraseIf([&stale_single_shard_waiters](const auto& waiter) {
        if (waiter->ShouldReRunConflictResolution()) {
          stale_single_shard_waiters.push_back(waiter);
          return true;
        }
        return false;
      }, &single_shard_waiters_);
      total_waiters_->set_value(waiter_status_.size() + single_shard_waiters_.size());
      total_blockers_->set_value(blocker_status_.size());
    }

    for (const auto& waiting_txn : waiting_txns) {
      pending_time_waiting_->Increment(waiting_txn->GetTotalWaitTime(clock_->Now()));
      auto expired_waiters = waiting_txn->PurgeWaiters([](const auto& waiter) {
        return waiter->ShouldReRunConflictResolution();
      });
      for (const auto& waiter : expired_waiters) {
        waiter_runner_.Submit(waiter, kRefreshWaiterTimeout);
      }
      if (waiting_txn->GetNumWaiters() == 0) {
        continue;
      }
      auto transaction_id = waiting_txn->id();
      StatusRequest request {
        .id = &transaction_id,
        .read_ht = now,
        .global_limit_ht = now,
        .serial_no = 0,
        .reason = &kWaiterReason,
        .flags = TransactionLoadFlags {},
        .callback = [waiting_txn, this](Result<TransactionStatusResult> res) {
          HandleWaiterStatusFromParticipant(waiting_txn, res);
        }
      };
      txn_status_manager_->RequestStatusAt(request);
    }

    if (!blockers.empty()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Requesting status for blockers: "
                                   << ToString(blockers) << ".";
    }

    for (const auto& txn : blockers) {
      auto transaction_id = txn;
      StatusRequest request {
        .id = &transaction_id,
        .read_ht = now,
        .global_limit_ht = now,
        .serial_no = 0,
        .reason = &kBlockerReason,
        .flags = TransactionLoadFlags {},
        .callback = [transaction_id, this](Result<TransactionStatusResult> res) {
          MaybeSignalWaitingTransactions(transaction_id, res);
        }
      };
      txn_status_manager_->RequestStatusAt(request);
    }

    for (const auto& waiter : stale_single_shard_waiters) {
      waiter->InvokeCallbackOrWarn(kRetrySingleShardOp);
    }
    waiter_runner_.CleanContentiousWeakWaiterDatas();
  }

  void UpdateWaitersOnBlockerPromotion(const TransactionId& id,
                                       TransactionStatusResult res) EXCLUDES(mutex_) {
    WaitingTxnPtr waiting_txn = nullptr;
    std::shared_ptr<BlockerData> promoted_blocker = nullptr;
    {
      SharedLock r_lock(mutex_);
      if (shutting_down_) {
        return;
      }

      auto waiter_it = waiter_status_.find(id);
      if (waiter_it != waiter_status_.end()) {
        waiting_txn = waiter_it->second;
      }

      auto blocker_it = blocker_status_.find(id);
      if (blocker_it != blocker_status_.end()) {
        promoted_blocker = blocker_it->second.lock();
      }
    }

    // Check if the promoted transaction is an active waiter, and make it re-enter the wait queue
    // to ensure its wait-for relationships are re-registered with its new transaction coordinator,
    // corresponding to its new status tablet.
    if (waiting_txn) {
      for (const auto& waiter_data : waiting_txn->PurgeWaiters()) {
        waiter_runner_.Submit(waiter_data, Status::OK(), res.status_time);
      }
    }

    // Check if the promoted transaction is a blocker, and make all of its waiters re-enter the wait
    // queue to ensure their wait-for relationships are re-registered with their coordinator
    // pointing to the correct blocker status tablet.
    if (promoted_blocker) {
      auto waiters_to_signal = promoted_blocker->Signal(std::move(res), clock_->Now());
      SharedLock r_lock(mutex_);
      for (const auto& waiter : waiters_to_signal) {
        auto txn_it = waiter_status_.find(waiter->id);
        if (txn_it == waiter_status_.end()) {
          // This may happen if the WaitingTxn has been removed after all WaiterData instances were
          // purged but before those WaiterData instances were destructed.
          VLOG(1) << "Waiter not found in waiter_status_ " << waiter->id
                  << ", request_id: " << waiter->request_id;
          continue;
        }
        auto waiters_to_signal = txn_it->second->PurgeWaiter(waiter->request_id);
        DCHECK(!waiters_to_signal.empty())
            << "Did not find waiter to signal during promotion " << waiter->id
            << ", request_id: " << waiter->request_id;
        for (const auto& waiter_to_signal : waiters_to_signal) {
          waiter_runner_.Submit(waiter_to_signal, Status::OK(), res.status_time);
        }
      }
    }
  }

  // Update status of waiter/blocker transactions based on the received TransactionStatusResult.
  void SignalPromoted(const TransactionId& id, TransactionStatusResult&& res) {
    // Transaction promotion signal should be handled in an async manner so as to not block the
    // query layer from further processing the transaction, as processing the singal involves
    // acquiring a mutex that guards waiter transactions as well as blockers.
    WARN_NOT_OK(
      thread_pool_token_->SubmitFunc(
        std::bind(&WaitQueue::Impl::UpdateWaitersOnBlockerPromotion, this, id, res)),
      Format("Failed to submit UpdateWaitersOnBlockerPromotion task for txn $0", id));
  }

  bool StartShutdown() EXCLUDES(mutex_) {
    decltype(waiter_status_) waiter_status_copy;
    decltype(single_shard_waiters_) single_shard_waiters_copy;

    {
      UniqueLock l(mutex_);
      if (shutting_down_) {
        return false;
      }
      shutting_down_ = true;
      waiter_status_copy.swap(waiter_status_);
      single_shard_waiters_copy.swap(single_shard_waiters_);
      blocker_status_.clear();
      blockers_by_key_.clear();
    }

    waiter_runner_.StartShutdown();

    for (const auto& [_, waiting_txn] : waiter_status_copy) {
      waiting_txn->SignalWaitQueueShutdown();
    }
    for (const auto& waiter_data : single_shard_waiters_copy) {
      waiter_data->SignalWaitQueueShutdown();
    }
    in_progress_rpc_status_req_callbacks_.Shutdown();
    return !shutdown_complete_;
  }

  void CompleteShutdown() EXCLUDES(mutex_) {
    bool expected = false;
    if (!shutdown_complete_.compare_exchange_strong(expected, true)) {
      VLOG_WITH_PREFIX(1) << "Attempted to shutdown wait queue that is already shutdown";
      return;
    }
    waiter_runner_.CompleteShutdown();
    SharedLock l(mutex_);
    rpcs_.Shutdown();
    LOG_IF(DFATAL, !shutting_down_)
        << "Called CompleteShutdown() while not in shutting_down_ state.";
    LOG_IF(DFATAL, !blocker_status_.empty())
        << "Called CompleteShutdown without empty blocker_status_";
    LOG_IF(DFATAL, !waiter_status_.empty())
        << "Called CompleteShutdown without empty waiter_status_";
  }

  void DumpStatusHtml(std::ostream& out) {
    SharedLock l(mutex_);
    if (shutting_down_) {
      out << "Shutting down..." << std::endl;
    }

    out << "<h2>Txn Waiters:</h2>" << std::endl;

    out << "<table>" << std::endl;
    out << "<tr><th>WaiterId</th><th>RequestId</th><th>BlockerId</th></tr>" << std::endl;
    for (const auto& [txn_id, waiting_txn] : waiter_status_) {
      for (const auto& waiter_data : waiting_txn->GetWaiters()) {
        for (const auto& blocker : waiter_data->blockers) {
          out << "<tr>"
                << "<td>|" << txn_id << "</td>"
                << "<td>|" << waiter_data->request_id << "</td>"
                << "<td>|" << blocker.first->id() << "</td>"
              << "</tr>" << std::endl;
        }
      }
    }
    out << "</table>" << std::endl;

    out << "<h2>Single Shard Waiters:</h2>" << std::endl;

    out << "<table>" << std::endl;
    out << "<tr><th>Index</th><th>BlockerId</th></tr>" << std::endl;
    auto idx = 0;
    for (const auto& data : single_shard_waiters_) {
      for (const auto& blocker : data->blockers) {
        out << "<tr>"
              << "<td>|" << idx << "</td>"
              << "<td>|" << blocker.first->id() << "</td>"
            << "</tr>" << std::endl;
      }
      ++idx;
    }
    out << "</table>" << std::endl;

    out << "<h2>Blockers:</h2>" << std::endl;
    out << "<table>" << std::endl;
    out << "<tr><th>BlockerId</th><th>Status</th></tr>" << std::endl;
    for (const auto& [txn_id, weak_data] : blocker_status_) {
      if (auto data = weak_data.lock()) {
        data->DumpHtmlTableRow(out);
      } else {
        out << "<tr>"
              << "<td>|" << txn_id << "</td>"
              << "<td>|released</td>"
            << "</tr>" << std::endl;
      }
    }
    out << "</table>" << std::endl;

    out << "<h3> Extra data: </h3>" << std::endl;
    out << "<h4> Num blocking keys: " << blockers_by_key_.size() << "</h4>" << std::endl;
    out << "<h4> Num single shard waiters: "
        << single_shard_waiters_.size() << "</h4>" << std::endl;
  }

  uint64_t GetSerialNo() {
    return serial_no_.fetch_add(1);
  }

  void SignalCommitted(const TransactionId& id, HybridTime commit_ht) {
    if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_TEST_drop_participant_signal))) {
      LOG_WITH_PREFIX_AND_FUNC(INFO) << "Dropping commit signal " << id;
      return;
    }
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Signaling committed " << id << " @ " << commit_ht;
    TransactionStatusResult res(TransactionStatus::COMMITTED, commit_ht);
    MaybeSignalWaitingTransactions(id, res);
  }

  void SignalAborted(const TransactionId& id) {
    if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_TEST_drop_participant_signal))) {
      LOG_WITH_PREFIX_AND_FUNC(INFO) << "Dropping abort signal " << id;
      return;
    }
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Signaling aborted " << id;
    TransactionStatusResult res(TransactionStatus::ABORTED, clock_->Now());
    MaybeSignalWaitingTransactions(id, res);
  }

  Status GetLockStatus(const std::map<TransactionId, SubtxnSet>& transactions,
                       uint64_t max_single_shard_waiter_start_time_us,
                       const TableInfoProvider& table_info_provider,
                       TransactionLockInfoManager* lock_info_manager,
                       uint32_t max_txn_locks) const {
    std::vector<WaiterDataPtr> waiters_copy;
    {
      SharedLock l(mutex_);
      // If the wait-queue is being shutdown, waiter_status_ would  be empty. No need to
      // explicitly check 'shutting_down_' and return.
      if (transactions.empty() && max_single_shard_waiter_start_time_us == 0) {
        // When transactions is empty and the max start time of single shard waiters isn't set,
        // return awaiting locks info of all waiters.
        for (const auto& [_, waiting_txn] : waiter_status_) {
          for (const auto& waiter_data : waiting_txn->GetWaiters()) {
            waiters_copy.push_back(waiter_data);
          }
        }
        for (const auto& waiter_data : single_shard_waiters_) {
          waiters_copy.push_back(waiter_data);
        }
      } else {
        for (const auto& [txn_id, _] : transactions) {
          const auto& it = waiter_status_.find(txn_id);
          if (it != waiter_status_.end()) {
            for (const auto& waiter_data : it->second->GetWaiters()) {
              waiters_copy.push_back(waiter_data);
            }
          }
        }
        for (const auto& waiter_data : single_shard_waiters_) {
          if (waiter_data->request_start_us <= max_single_shard_waiter_start_time_us) {
            waiters_copy.push_back(waiter_data);
          }
        }
      }
    }

    for (const auto& waiter : waiters_copy) {
      auto lock_info = VERIFY_RESULT(waiter->GetWaiterLockStatusInfo());
      if (lock_info.intents.empty()) {
        // If the waiter has been resumed or is in the process of resumption, skip populating
        // its locks as part of 'pg_locks'.
        continue;
      }
      const auto& txn_id = lock_info.id;
      TabletLockInfoPB::WaiterInfoPB* waiter_info = nullptr;
      uint32 granted_locks_size = 0;
      if (txn_id.IsNil()) {
        waiter_info = lock_info_manager->GetSingleShardLockInfo();
      } else {
        auto* lock_entry = lock_info_manager->GetOrAddTransactionLockInfo(txn_id);
        DCHECK(!lock_entry->has_waiting_locks());
        waiter_info = lock_entry->mutable_waiting_locks();
        granted_locks_size = lock_entry->granted_locks_size();
      }

      waiter_info->set_wait_start_ht(lock_info.wait_start.ToUint64());
      for (const auto& blocker : lock_info.blockers) {
        auto& id = blocker->id();
        waiter_info->add_blocking_txn_ids(id.data(), id.size());
      }
      for (auto& [intent_key, intent_data] : lock_info.intents) {
        if (max_txn_locks && (waiter_info->locks_size() + granted_locks_size >= max_txn_locks)) {
          waiter_info->set_has_additional_waiting_locks(true);
          break;
        }
        ParsedIntent parsed_intent {
          .doc_path = intent_key.AsSlice(),
          .types = intent_data.types,
          .doc_ht = Slice(),
        };
        // TODO(pglocks): Populate 'is_explicit' info of waiter txn(s) in the LockInfoPB response.
        // Currently we don't track it for waiter txn(s).
        auto* lock = waiter_info->add_locks();
        RETURN_NOT_OK(docdb::PopulateLockInfoFromParsedIntent(
            parsed_intent, DecodedIntentValue{}, table_info_provider, lock,
            /* intent_has_ht */ false));
        lock->set_subtransaction_id(lock_info.subtxn_id);
      }
    }
    return Status::OK();
  }

 private:
  void HandleWaiterStatusFromParticipant(
      WaitingTxnPtr waiting_txn, Result<TransactionStatusResult> res) {
    {
      SharedLock l(mutex_);
      if (shutting_down_) {
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Skipping status RPC for waiter in shutdown wait queue.";
        return;
      }
    }
    if (!res.ok() && res.status().IsNotFound()) {
      // Currently it may be the case that a waiting txn has not yet registered with the local
      // txn participant if it has not yet operated on the local tablet. The semantics of
      // txn_status_manager_->RequestStatusAt assume that any requested txn_id has participated on
      // the local tablet at least once before, and if there is no state in the local participant
      // relating to this txn_id it returns a NotFound status. So in that case, we send an RPC
      // directly to the status tablet to determine the status of this transaction.
      waiting_txn->TriggerStatusRequest(
          clock_->Now(), &client(),
          std::bind(&Impl::HandleWaiterStatusRpcResponse, this, waiting_txn->id(), _1, _2));
      return;
    }
    HandleWaiterStatusResponse(waiting_txn, res);
  }

  void HandleWaiterStatusRpcResponse(
      const TransactionId& waiter_id, const Status& status,
      const tserver::GetTransactionStatusResponsePB& resp) {
    {
      SharedLock l(mutex_);
      if (shutting_down_) {
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Skipping status RPC for waiter in shutdown wait queue.";
        return;
      }
    }
    if (status.ok() && resp.status(0) == PENDING) {
      return;
    }
    WaitingTxnPtr waiter;
    {
      UniqueLock l(mutex_);
      auto it = waiter_status_.find(waiter_id);
      if (it == waiter_status_.end()) {
        VLOG_WITH_PREFIX(4) << "Got RPC status for removed waiter " << waiter_id;
        return;
      }
      waiter = it->second;
      waiter_status_.erase(it);
    }
    DCHECK(waiter != nullptr);

    if (!status.ok()) {
      waiter->InvokeCallbackOrWarn(
          status.CloneAndPrepend("Failed to get txn status while waiting"));
      return;
    }
    if (resp.has_error()) {
      waiter->InvokeCallbackOrWarn(StatusFromPB(resp.error().status()).CloneAndPrepend(
          "Failed to get txn status while waiting"));
      return;
    }
    if (resp.status(0) == ABORTED) {
      VLOG_WITH_PREFIX(1) << "Waiter status aborted " << waiter_id;
      if (resp.deadlock_reason().empty() || resp.deadlock_reason(0).code() == AppStatusPB::OK) {
        waiter->InvokeCallbackOrWarn(
            // Return InternalError so that TabletInvoker does not retry.
            STATUS_EC_FORMAT(InternalError, TransactionError(TransactionErrorCode::kConflict),
                             "Transaction $0 was aborted while waiting for locks", waiter_id));
      } else {
        waiter->InvokeCallbackOrWarn(StatusFromPB(resp.deadlock_reason(0)));
      }
      return;
    }
    LOG(DFATAL) << "Waiting transaction " << waiter_id
                << " found in unexpected state " << resp.status(0);
  }

  void HandleWaiterStatusResponse(
      const WaitingTxnPtr& waiting_txn, Result<TransactionStatusResult> res) {
    auto txn_status = UnwrapResult(res);
    Status resume_status = Status::OK();
    VLOG_WITH_PREFIX(4) << "Got waiter " << waiting_txn->id()
                        << " status result " << res.ToString();
    if (!txn_status.ok()) {
      resume_status = txn_status.status();
    } else if (*txn_status == ResolutionStatus::kAborted) {
      // TODO(wait-queues): We might hit this branch when a waiter transaction undergoes promotion.
      //
      // Refer issue: https://github.com/yugabyte/yugabyte-db/issues/16375
      resume_status = STATUS_EC_FORMAT(
          InternalError, TransactionError(TransactionErrorCode::kConflict),
          "Transaction $0 was aborted while waiting for locks", waiting_txn->id());
    } else if (*txn_status == ResolutionStatus::kDeadlocked) {
      DCHECK(!res->expected_deadlock_status.ok());
      resume_status = res->expected_deadlock_status;
    }
    if (resume_status.ok()) {
      return;
    }
    for (const auto& waiter_data : waiting_txn->PurgeWaiters()) {
      waiter_runner_.Submit(waiter_data, resume_status, HybridTime::kInvalid);
    }
    // Need not handle waiter promotion case here as this code path is executed only as a callback
    // from WaitQueue::Impl::Poll function, where we periodically request waiter transaction state.
    // Waiter promotion scenario is handled when transaction participant signals the wait-queue
    // by calling WaitQueue::SignalPromoted.
  }

  void MaybeSignalWaitingTransactions(
      const TransactionId& transaction, Result<TransactionStatusResult> res) EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "transaction: " << transaction
                                 << " - res: " << res << " - aborted "
                                 << (res.ok() ? res->aborted_subtxn_set.ToString() : "error");
    std::shared_ptr<BlockerData> resolved_blocker = nullptr;
    {
      SharedLock l(mutex_);
      if (shutting_down_) {
        return;
      }

      auto it = blocker_status_.find(transaction);
      if (it == blocker_status_.end()) {
        VLOG_WITH_PREFIX_AND_FUNC(4) << "Transaction not found - " << transaction << ".";
        return;
      }

      if (auto locked_blocker = it->second.lock()) {
        resolved_blocker = locked_blocker;
      }

      // Don't remove blocker from blocker_status_ here, in case some waiter has already added it
      // to it's blockers field. We should only remove blockers from blocker_status_ when we detect
      // it's invalid while holding a unique lock on mutex_.
    }

    if (!resolved_blocker) {
      VLOG_WITH_PREFIX(4) << "Could not resolve blocker " << transaction << " for result " << res;
      return;
    }

    for (const auto& waiter : resolved_blocker->Signal(std::move(res), clock_->Now())) {
      SignalWaiter(waiter);
    }
  }

  void InvokeWaiterCallback(
      const Status& status, const WaiterDataPtr& waiter_data,
      HybridTime resume_ht = HybridTime::kInvalid) EXCLUDES(mutex_) {
    if (waiter_data->IsSingleShard()) {
      waiter_runner_.Submit(waiter_data, status, resume_ht);
      return;
    }

    // We cannot use the passed in waiter_data here as it may have been replaced in waiter_status_
    // by a new WaiterData instance for the same transaction. Such a situation would indicate that
    // the previous request had returned to the caller and a new request for the same transaction
    // was now waiting. In this situation, we would want to signal the new waiter.
    std::vector<WaiterDataPtr> found_waiters;
    {
      UniqueLock l(mutex_);
      auto it = waiter_status_.find(waiter_data->id);
      if (it != waiter_status_.end()) {
        found_waiters = it->second->PurgeWaiter(waiter_data->request_id);
      }
    }

    if (found_waiters.empty()) {
      VLOG_WITH_PREFIX(1)
        << "Tried to invoke callback on waiter which has already been removed. "
        << "This can happen any time a waiter has multiple blockers which resolve at similar "
        << "times, or if a blocker is detected as resolved by both Poll and SignalCommitted "
        << "concurrently.";
      return;
    }

    // Note -- it's important that we remove the waiter from waiter_status_ before invoking it's
    // callback. Otherwise, the callback will re-run conflict resolution, end up back in the wait
    // queue, and attempt to reuse the WaiterData still present in waiter_status_.
    for (const auto& found_waiter : found_waiters) {
      waiter_runner_.Submit(found_waiter, status, resume_ht);
    }
  }

  void SignalWaiter(const WaiterDataPtr& waiter_data) {
    VLOG_WITH_PREFIX(4) << "Signaling waiter " << waiter_data->id
                        << " with request_id: " << waiter_data->request_id;
    Status status = Status::OK();
    size_t num_resolved_blockers = 0;
    HybridTime max_unblock_ht = HybridTime::kMin;

    for (const auto& [blocker_data, conflict_info] : waiter_data->blockers) {
      auto is_resolved = blocker_data->IsResolved();
      if (!is_resolved.ok()) {
        status = is_resolved.status();
        break;
      }
      if (*is_resolved ||
          (conflict_info && !blocker_data->HasLiveSubtransaction(conflict_info->subtransactions))) {
        max_unblock_ht = std::max(max_unblock_ht, blocker_data->status_ht());
        num_resolved_blockers++;
      }
    }

    if (waiter_data->blockers.size() == num_resolved_blockers || !status.ok()) {
      // TODO(wait-queues): Abort transactions without re-invoking conflict resolution when
      // possible, e.g. if the blocking transaction was not a lock-only conflict and was committed.
      // See https://github.com/yugabyte/yugabyte-db/issues/13577
      InvokeWaiterCallback(status, waiter_data, max_unblock_ht);
    }
  }

  std::string LogPrefix() const {
    return Format("T $0 P $1 - ", txn_status_manager_->tablet_id(), permanent_uuid_);
  }

  client::YBClient& client() { return *client_future_.get(); }

  mutable rw_spinlock mutex_;

  bool shutting_down_ GUARDED_BY(mutex_) = false;

  std::atomic<bool> shutdown_complete_ = false;

  std::unordered_map<
      TransactionId,
      std::weak_ptr<BlockerData>,
      TransactionIdHash>
    blocker_status_ GUARDED_BY(mutex_);

  std::unordered_map<
      RefCntPrefix,
      std::unordered_set<TransactionId, TransactionIdHash>,
      RefCntPrefixHash>
    blockers_by_key_ GUARDED_BY(mutex_);

  std::unordered_map<
      TransactionId,
      WaitingTxnPtr,
      TransactionIdHash>
    waiter_status_ GUARDED_BY(mutex_);

  std::list<WaiterDataPtr> single_shard_waiters_ GUARDED_BY(mutex_);

  rpc::Rpcs rpcs_;

  std::atomic_uint64_t serial_no_ = 0;

  TransactionStatusManager* const txn_status_manager_;
  const std::string& permanent_uuid_;
  WaitingTxnRegistry* const waiting_txn_registry_;
  const std::shared_future<client::YBClient*>& client_future_;
  const server::ClockPtr& clock_;
  std::unique_ptr<ThreadPoolToken> thread_pool_token_;
  ResumedWaiterRunner waiter_runner_;
  scoped_refptr<EventStats> pending_time_waiting_;
  scoped_refptr<EventStats> finished_waiting_latency_;
  scoped_refptr<EventStats> blockers_per_waiter_;
  scoped_refptr<EventStats> waiters_per_blocker_;
  scoped_refptr<AtomicGauge<uint64_t>> total_waiters_;
  scoped_refptr<AtomicGauge<uint64_t>> total_blockers_;
  OperationCounter in_progress_rpc_status_req_callbacks_;
};

WaitQueue::WaitQueue(
    TransactionStatusManager* txn_status_manager,
    const std::string& permanent_uuid,
    WaitingTxnRegistry* waiting_txn_registry,
    const std::shared_future<client::YBClient*>& client_future,
    const server::ClockPtr& clock,
    const MetricEntityPtr& metrics,
    std::unique_ptr<ThreadPoolToken> thread_pool_token,
    rpc::Messenger* messenger):
  impl_(new Impl(txn_status_manager, permanent_uuid, waiting_txn_registry, client_future, clock,
                 metrics, std::move(thread_pool_token), messenger)) {}

WaitQueue::~WaitQueue() = default;

Status WaitQueue::WaitOn(
    const TransactionId& waiter, SubTransactionId subtxn_id, LockBatch* locks,
    std::shared_ptr<ConflictDataManager> blockers, const TabletId& status_tablet_id,
    uint64_t serial_no, int64_t txn_start_us, uint64_t request_start_us, int64_t request_id,
    CoarseTimePoint deadline, IntentProviderFunc intent_provider, WaitDoneCallback callback) {
  return impl_->WaitOn(
      waiter, subtxn_id, locks, std::move(blockers), status_tablet_id, serial_no, txn_start_us,
      request_start_us, request_id, deadline, intent_provider, callback);
}


Result<bool> WaitQueue::MaybeWaitOnLocks(
    const TransactionId& waiter, SubTransactionId subtxn_id, LockBatch* locks,
    const TabletId& status_tablet_id, uint64_t serial_no,
    int64_t txn_start_us, uint64_t request_start_us, int64_t request_id, CoarseTimePoint deadline,
    IntentProviderFunc intent_provider, WaitDoneCallback callback) {
  return impl_->MaybeWaitOnLocks(
      waiter, subtxn_id, locks, status_tablet_id, serial_no, txn_start_us, request_start_us,
      request_id, deadline, intent_provider, callback);
}

void WaitQueue::Poll(HybridTime now) {
  return impl_->Poll(now);
}

void WaitQueue::StartShutdown() {
  impl_->StartShutdown();
}

void WaitQueue::CompleteShutdown() {
  return impl_->CompleteShutdown();
}

void WaitQueue::DumpStatusHtml(std::ostream& out) {
  return impl_->DumpStatusHtml(out);
}

uint64_t WaitQueue::GetSerialNo() {
  return impl_->GetSerialNo();
}

void WaitQueue::SignalCommitted(const TransactionId& id, HybridTime commit_ht) {
  return impl_->SignalCommitted(id, commit_ht);
}

void WaitQueue::SignalAborted(const TransactionId& id) {
  return impl_->SignalAborted(id);
}

void WaitQueue::SignalPromoted(const TransactionId& id, TransactionStatusResult&& res) {
  return impl_->SignalPromoted(id, std::move(res));
}

Status WaitQueue::GetLockStatus(const std::map<TransactionId, SubtxnSet>& transactions,
                                uint64_t max_single_shard_waiter_start_time_us,
                                const TableInfoProvider& table_info_provider,
                                TransactionLockInfoManager* lock_info_manager,
                                uint32_t max_txn_locks) const {
  return impl_->GetLockStatus(
      transactions, max_single_shard_waiter_start_time_us, table_info_provider,
      lock_info_manager, max_txn_locks);
}

}  // namespace docdb
}  // namespace yb
