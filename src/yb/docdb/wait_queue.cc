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

#include <atomic>
#include <chrono>
#include <future>
#include <list>
#include <memory>
#include <queue>
#include <sstream>

#include <boost/algorithm/string/join.hpp>

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
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/unique_lock.h"

DEFINE_RUNTIME_uint64(wait_for_relock_unblocked_txn_keys_ms, 0,
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

METRIC_DEFINE_coarse_histogram(
    tablet, wait_queue_pending_time_waiting, "Wait Queue - Still Waiting Time",
    yb::MetricUnit::kMicroseconds,
    "The amount of time a still-waiting transaction has been in the wait queue");
METRIC_DEFINE_coarse_histogram(
    tablet, wait_queue_finished_waiting_latency, "Wait Queue - Total Waiting Time",
    yb::MetricUnit::kMicroseconds,
    "The amount of time an unblocked transaction spent in the wait queue");
METRIC_DEFINE_coarse_histogram(
    tablet, wait_queue_blockers_per_waiter, "Wait Queue - Blockers per Waiter",
    yb::MetricUnit::kTransactions, "The number of blockers a waiter is stuck on in the wait queue");
METRIC_DEFINE_coarse_histogram(
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
  LockBatchEntries locks;
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
             uint64_t serial_no_, HybridTime wait_start_, const TabletId& status_tablet_,
             const std::vector<BlockerDataAndConflictInfo> blockers_,
             const WaitDoneCallback callback_,
             std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration_, rpc::Rpcs* rpcs,
             scoped_refptr<Histogram>* finished_waiting_latency)
      : id(id_),
        subtxn_id(subtxn_id_),
        locks(locks_),
        serial_no(serial_no_),
        wait_start(wait_start_),
        status_tablet(status_tablet_),
        blockers(std::move(blockers_)),
        callback(std::move(callback_)),
        waiter_registration(std::move(waiter_registration_)),
        finished_waiting_latency_(*finished_waiting_latency),
        unlocked_(locks->Unlock()),
        rpcs_(*rpcs) {
    VLOG_WITH_PREFIX(4) << "Constructed waiter";
  }

  ~WaiterData() {
    VLOG_WITH_PREFIX(4) << "Destructed waiter";
  }

  const TransactionId id;
  const SubTransactionId subtxn_id;
  LockBatch* const locks;
  const uint64_t serial_no;
  const HybridTime wait_start;
  const TabletId status_tablet;
  const std::vector<BlockerDataAndConflictInfo> blockers;
  const WaitDoneCallback callback;
  std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration;

  void InvokeCallback(const Status& status, HybridTime resume_ht = HybridTime::kInvalid) {
    VLOG_WITH_PREFIX(4) << "Invoking waiter callback " << status;
    UniqueLock l(mutex_);
    if (!unlocked_) {
      LOG_WITH_PREFIX(INFO)
          << "Skipping InvokeCallback for waiter whose callback was already invoked. This should "
          << "be rare.";
      return;
    }
    finished_waiting_latency_->Increment(MicrosSinceCreation());
    if (!status.ok()) {
      unlocked_ = std::nullopt;
      callback(status, resume_ht);
      return;
    }
    *locks = std::move(*unlocked_).Lock(GetWaitForRelockUnblockedKeysDeadline());
    unlocked_ = std::nullopt;
    callback(locks->status(), resume_ht);
  }

  std::string LogPrefix() {
    return Format("TxnId: $0 ", id);
  }

  using StatusCb = std::function<
      void(const Status& status, const tserver::GetTransactionStatusResponsePB& resp)>;

  void TriggerStatusRequest(HybridTime now, client::YBClient* client, StatusCb cb) {
    UniqueLock l(mutex_);
    if (!unlocked_) {
      VLOG_WITH_PREFIX(1)
          << "Skipping GetTransactionStatus RPC for waiter whose callback was already invoked.";
      return;
    }

    if (handle_ != rpcs_.InvalidHandle()) {
      VLOG_WITH_PREFIX(1)
          << "Skipping GetTransactionStatus RPC for waiter already having pending RPC";
      return;
    }

    tserver::GetTransactionStatusRequestPB req;
    DCHECK(!status_tablet.empty());
    req.set_tablet_id(status_tablet);
    req.add_transaction_id(id.data(), id.size());
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
                }
                cb(status, resp);
            }),
        &handle_);
  }

  void ShutdownStatusRequest() {
    UniqueLock l(mutex_);
    if (handle_ != rpcs_.InvalidHandle()) {
      (**handle_).Abort();
    }
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

  LockBatchEntries GetLockBatchEntries() const EXCLUDES(mutex_) {
    SharedLock lock(mutex_);
    return unlocked_ ? unlocked_->Get() : LockBatchEntries{};
  }

  WaiterLockStatusInfo GetWaiterLockStatusInfo() const EXCLUDES(mutex_) {
    SharedLock lock(mutex_);
    auto info = WaiterLockStatusInfo {
      .id = id,
      .subtxn_id = subtxn_id,
      .wait_start = wait_start,
      .locks = unlocked_ ? unlocked_->Get() : LockBatchEntries{},
      .blockers = {},
    };
    info.blockers.reserve(blockers.size());
    for (const auto& blocker : blockers) {
      info.blockers.push_back(blocker.first);
    }
    return info;
  }

 private:
  int64_t MicrosSinceCreation() const {
    return GetCurrentTimeMicros() - wait_start.GetPhysicalValueMicros();
  }

  scoped_refptr<Histogram>& finished_waiting_latency_;
  mutable rw_spinlock mutex_;
  std::optional<UnlockedBatch> unlocked_ GUARDED_BY(mutex_) = std::nullopt;
  rpc::Rpcs& rpcs_;
  rpc::Rpcs::Handle handle_ GUARDED_BY(mutex_) = rpcs_.InvalidHandle();
};

using WaiterDataPtr = std::shared_ptr<WaiterData>;

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
    VLOG(4) << "Signaling waiters "
            << (txn_status_response.ok() ?
                txn_status_response->ToString() :
                txn_status_response.status().ToString());

    UniqueLock l(mutex_);
    bool was_txn_local = !IsPromotedUnlocked();

    // TODO(wait-queues): Track status hybrid times and ignore old status updates
    txn_status_ = UnwrapResult(txn_status_response);
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
      if (aborted_subtransactions_ != txn_status_response->aborted_subtxn_set) {
        // TODO(wait-queues): Avoid copying the subtransaction set. See:
        // https://github.com/yugabyte/yugabyte-db/issues/13823
        aborted_subtransactions_ = std::move(txn_status_response->aborted_subtxn_set);
        should_signal = true;
      }
    }

    if (should_signal) {
      if (txn_status_ht_.is_special()) {
        DCHECK(IsAbortedUnlocked())
          << "Unexpected special status ht in blocker " << txn_status_
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
    return txn_status_.ok() &&
        (*txn_status_ == ResolutionStatus::kPending || *txn_status_ == ResolutionStatus::kPromoted);
  }

  bool IsAbortedUnlocked() REQUIRES_SHARED(mutex_) {
    return txn_status_.ok() && *txn_status_ == ResolutionStatus::kAborted;
  }

  bool IsPromotedUnlocked() REQUIRES_SHARED(mutex_) {
    return txn_status_.ok() && *txn_status_ == ResolutionStatus::kPromoted;
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
          << "<td>|" << txn_status_ << "</td>"
        << "</tr>" << std::endl;
  }

 private:
  const TransactionId id_;
  TabletId status_tablet_ GUARDED_BY(mutex_);;
  mutable rw_spinlock mutex_;
  Result<ResolutionStatus> txn_status_ GUARDED_BY(mutex_) = ResolutionStatus::kPending;
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
    return w1.waiter->serial_no > w2.waiter->serial_no;
  }
};

// Resumes waiters async, in serial, and in the order of the waiter's serial_no, running the lowest
// serial number first in a best effort manner.
class ResumedWaiterRunner {
 public:
  explicit ResumedWaiterRunner(ThreadPoolToken* thread_pool_token)
    : thread_pool_token_(DCHECK_NOTNULL(thread_pool_token)) {}

  void Submit(const WaiterDataPtr& waiter, const Status& status, HybridTime resolve_ht) {
    {
      UniqueLock l(mutex_);
      AddWaiter(waiter, status, resolve_ht);
    }
    TriggerPoll();
  }

  void Shutdown() {
    thread_pool_token_->Shutdown();
  }

 private:
  void TriggerPoll() {
    WARN_NOT_OK(thread_pool_token_->SubmitFunc([this]() {
      for (;;) {
        WaiterDataPtr to_invoke;
        HybridTime resolve_ht;
        {
          UniqueLock l(this->mutex_);
          if (pq_.empty()) {
            return;
          }
          to_invoke = pq_.top().waiter;
          resolve_ht = pq_.top().resolve_ht;
          pq_.pop();
        }
        to_invoke->InvokeCallback(Status::OK(), resolve_ht);
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
    } else {
      // If error status, resume waiter right away, no need to respect serial_no
      WARN_NOT_OK(thread_pool_token_->SubmitFunc([waiter, status]() {
        waiter->InvokeCallback(status);
      }), "Failed to submit waiter resumption");
    }
  }

  mutable rw_spinlock mutex_;
  std::priority_queue<
      SerialWaiter, std::vector<SerialWaiter>, SerialWaiter> pq_ GUARDED_BY(mutex_);
  ThreadPoolToken* thread_pool_token_;
};

const Status kShuttingDownError = STATUS(
    IllegalState, "Tablet shutdown in progress - there may be a new leader.");

const Status kRetrySingleShardOp = STATUS(
    TimedOut,
    "Single shard transaction timed out while waiting. Forcing retry to confirm client liveness.");

const Status kRefreshWaiterTimeout = STATUS(
    TimedOut,
    "Waiter transaction timed out waiting in queue, invoking callback.");

} // namespace

class WaitQueue::Impl {
 public:
  Impl(TransactionStatusManager* txn_status_manager, const std::string& permanent_uuid,
       WaitingTxnRegistry* waiting_txn_registry,
       const std::shared_future<client::YBClient*>& client_future,
       const server::ClockPtr& clock, const MetricEntityPtr& metrics,
       std::unique_ptr<ThreadPoolToken> thread_pool_token)
      : txn_status_manager_(txn_status_manager), permanent_uuid_(permanent_uuid),
        waiting_txn_registry_(waiting_txn_registry), client_future_(client_future), clock_(clock),
        thread_pool_token_(std::move(thread_pool_token)),
        waiter_runner_(thread_pool_token_.get()),
        pending_time_waiting_(METRIC_wait_queue_pending_time_waiting.Instantiate(metrics)),
        finished_waiting_latency_(METRIC_wait_queue_finished_waiting_latency.Instantiate(metrics)),
        blockers_per_waiter_(METRIC_wait_queue_blockers_per_waiter.Instantiate(metrics)),
        waiters_per_blocker_(METRIC_wait_queue_waiters_per_blocker.Instantiate(metrics)),
        total_waiters_(METRIC_wait_queue_num_waiters.Instantiate(metrics, 0)),
        total_blockers_(METRIC_wait_queue_num_blockers.Instantiate(metrics, 0)) {}

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
      const TabletId& status_tablet_id, uint64_t serial_no, WaitDoneCallback callback) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "waiter_txn_id=" << waiter_txn_id
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
            waiter_txn_id, subtxn_id, locks, status_tablet_id, serial_no, std::move(callback),
            std::move(blocker_datas), std::move(blockers)));
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

    return false;
  }

  Status WaitOn(
      const TransactionId& waiter_txn_id, SubTransactionId subtxn_id, LockBatch* locks,
      std::shared_ptr<ConflictDataManager> blockers, const TabletId& status_tablet_id,
      uint64_t serial_no, WaitDoneCallback callback) {
    AtomicFlagSleepMs(&FLAGS_TEST_sleep_before_entering_wait_queue_ms);
    VLOG_WITH_PREFIX_AND_FUNC(4) << "waiter_txn_id=" << waiter_txn_id
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

      if (waiter_status_.contains(waiter_txn_id)) {
        LOG_WITH_PREFIX_AND_FUNC(DFATAL)
            << "Existing waiter already found - " << waiter_txn_id << ". "
            << "This should not happen.";
        waiter_status_[waiter_txn_id]->InvokeCallback(
          STATUS(IllegalState, "Unexpected duplicate waiter in wait queue - try again."));
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
          waiter_txn_id, subtxn_id, locks, status_tablet_id, serial_no, std::move(callback),
          std::move(blocker_datas), std::move(blockers));
    }
  }

  Status SetupWaiterUnlocked(
      const TransactionId& waiter_txn_id, SubTransactionId subtxn_id, LockBatch* locks,
      const TabletId& status_tablet_id, uint64_t serial_no, WaitDoneCallback callback,
      std::vector<BlockerDataAndConflictInfo>&& blocker_datas,
      std::shared_ptr<ConflictDataManager> blockers) REQUIRES(mutex_) {
    // TODO(wait-queues): similar to pg, we can wait 1s or so before beginning deadlock detection.
    // See https://github.com/yugabyte/yugabyte-db/issues/13576
    auto scoped_reporter = waiting_txn_registry_->Create();
    if (!waiter_txn_id.IsNil()) {
      // If waiter_txn_id is Nil, then we're processing a single-shard transaction. We do not have
      // to report single shard transactions to transaction coordinators because they can't
      // possibly be involved in a deadlock. This is true because no transactions can wait on
      // single shard transactions, so they only have out edges in the wait-for graph and cannot
      // be a part of a cycle.
      DCHECK(!status_tablet_id.empty());
      // TODO(wait-queues): Instead of moving blockers to local_waiting_txn_registry, we could store
      // the blockers in the waiter_data record itself. That way, we could avoid re-running conflict
      // resolution on blocker promotion and directly update the transaction coordinator with the
      // latest wait-for probes.
      RETURN_NOT_OK(scoped_reporter->Register(
          waiter_txn_id, std::move(blockers), status_tablet_id));
      DCHECK_GE(scoped_reporter->GetDataUseCount(), 1);
    }

    auto waiter_data = std::make_shared<WaiterData>(
        waiter_txn_id, subtxn_id, locks, serial_no, clock_->Now(), status_tablet_id,
        std::move(blocker_datas), std::move(callback), std::move(scoped_reporter), &rpcs_,
        &finished_waiting_latency_);
    if (waiter_data->IsSingleShard()) {
      DCHECK(single_shard_waiters_.size() == 0 ||
             waiter_data->wait_start >= single_shard_waiters_.front()->wait_start);
      single_shard_waiters_.push_front(waiter_data);
    } else {
      waiter_status_[waiter_txn_id] = waiter_data;
    }

    // We must add waiters to blockers while holding the wait queue mutex. Otherwise, we may
    // end up removing blockers from blocker_status_ during Poll() after we've already added them
    // to waiter_data.
    for (auto [blocker, _] : waiter_data->blockers) {
      blocker->AddWaiter(waiter_data);
    }
    return Status::OK();
  }

  void Poll(HybridTime now) EXCLUDES(mutex_) {
    // TODO(wait-queues): Rely on signaling from the RunningTransaction instance of the blocker
    // rather than this polling-based mechanism. We should also signal from the RunningTransaction
    // instance of the waiting transaction in case the waiter is aborted by deadlock or otherwise.
    // See https://github.com/yugabyte/yugabyte-db/issues/13578
    const std::string kBlockerReason = "Getting status for blocker wait queue";
    const std::string kWaiterReason = "Getting status for waiter in wait queue";
    std::vector<WaiterDataPtr> waiters;
    std::vector<TransactionId> blockers;
    std::vector<WaiterDataPtr> stale_single_shard_waiters;

    {
      UniqueLock l(mutex_);
      if (shutting_down_) {
        return;
      }
      for (auto it = waiter_status_.begin(); it != waiter_status_.end(); ++it) {
        auto& waiter = it->second;
        blockers_per_waiter_->Increment(waiter->blockers.size());
        DCHECK(!waiter->IsSingleShard());
        DCHECK(!waiter->status_tablet.empty());
        waiters.push_back(waiter);
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

    for (const auto& waiter : waiters) {
      auto duration_us =
          clock_->Now().GetPhysicalValueMicros() - waiter->wait_start.GetPhysicalValueMicros();
      auto seconds = duration_us * 1us / 1s;
      VLOG_WITH_PREFIX_AND_FUNC(4) << waiter->id << " waiting for " << seconds << " seconds";
      pending_time_waiting_->Increment(duration_us);
      if (waiter->ShouldReRunConflictResolution()) {
        InvokeWaiterCallback(kRefreshWaiterTimeout, waiter);
        continue;
      }
      auto transaction_id = waiter->id;
      StatusRequest request {
        .id = &transaction_id,
        .read_ht = now,
        .global_limit_ht = now,
        .serial_no = 0,
        .reason = &kWaiterReason,
        .flags = TransactionLoadFlags {},
        .callback = [waiter, this](Result<TransactionStatusResult> res) {
          HandleWaiterStatusFromParticipant(waiter, res);
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
      waiter->InvokeCallback(kRetrySingleShardOp);
    }
  }

  void UpdateWaitersOnBlockerPromotion(const TransactionId& id,
                                       TransactionStatusResult res) EXCLUDES(mutex_) {
    WaiterDataPtr waiter_data = nullptr;
    std::shared_ptr<BlockerData> promoted_blocker = nullptr;
    {
      SharedLock r_lock(mutex_);
      if (shutting_down_) {
        return;
      }

      auto waiter_it = waiter_status_.find(id);
      if (waiter_it != waiter_status_.end()) {
        waiter_data = waiter_it->second;
      }

      auto blocker_it = blocker_status_.find(id);
      if (blocker_it != blocker_status_.end()) {
        promoted_blocker = blocker_it->second.lock();
      }
    }

    // Check if the promoted transaction is an active waiter, and make it re-enter the wait queue
    // to ensure its wait-for relationships are re-registered with its new transaction coordinator,
    // corresponding to its new status tablet.
    if (waiter_data) {
      InvokeWaiterCallback(Status::OK(), waiter_data, res.status_time);
    }

    // Check if the promoted transaction is a blocker, and make all of its waiters re-enter the wait
    // queue to ensure their wait-for relationships are re-registered with their coordinator
    // pointing to the correct blocker status tablet.
    if (promoted_blocker) {
      for (const auto& waiter : promoted_blocker->Signal(std::move(res), clock_->Now())) {
        InvokeWaiterCallback(Status::OK(), waiter, res.status_time);
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

    waiter_runner_.Shutdown();

    for (const auto& [_, waiter_data] : waiter_status_copy) {
      waiter_data->ShutdownStatusRequest();
      waiter_data->InvokeCallback(kShuttingDownError);
    }
    for (const auto& waiter_data : single_shard_waiters_copy) {
      waiter_data->InvokeCallback(kShuttingDownError);
    }
    return !shutdown_complete_;
  }

  void CompleteShutdown() EXCLUDES(mutex_) {
    bool expected = false;
    if (!shutdown_complete_.compare_exchange_strong(expected, true)) {
      VLOG_WITH_PREFIX(1) << "Attempted to shutdown wait queue that is already shutdown";
      return;
    }
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
      out << "Shutting down...";
      return;
    }

    out << "<h2>Txn Waiters:</h2>" << std::endl;

    out << "<table>" << std::endl;
    out << "<tr><th>WaiterId</th><th>BlockerId</th></tr>" << std::endl;
    for (const auto& [txn_id, data] : waiter_status_) {
      for (const auto& blocker : data->blockers) {
        out << "<tr>"
              << "<td>|" << txn_id << "</td>"
              << "<td>|" << blocker.first->id() << "</td>"
            << "</tr>" << std::endl;
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

  Status GetLockStatus(const std::set<TransactionId>& transaction_ids,
                       const TableInfoProvider& table_info_provider,
                       TabletLockInfoPB* tablet_lock_info) const {
    std::vector<WaiterLockStatusInfo> waiter_lock_entries;
    {
      SharedLock l(mutex_);
      // If the wait-queue is being shutdown, waiter_status_ would  be empty. No need to
      // explicitly check 'shutting_down_' and return.

      if (transaction_ids.empty()) {
        // When transaction_ids is empty, return awaiting locks info of all waiters.
        for (const auto& [_, waiter_data] : waiter_status_) {
          waiter_lock_entries.push_back(waiter_data->GetWaiterLockStatusInfo());
        }
        for (const auto& waiter_data : single_shard_waiters_) {
          waiter_lock_entries.push_back(waiter_data->GetWaiterLockStatusInfo());
        }
      } else {
        for (const auto& txn_id : transaction_ids) {
          const auto& it = waiter_status_.find(txn_id);
          if (it != waiter_status_.end()) {
            waiter_lock_entries.push_back(it->second->GetWaiterLockStatusInfo());
          }
        }
      }
    }

    for (const auto& lock_status_info : waiter_lock_entries) {
      const auto& txn_id = lock_status_info.id;
      TabletLockInfoPB::WaiterInfoPB* waiter_info = nullptr;
      if (txn_id.IsNil()) {
        waiter_info = tablet_lock_info->add_single_shard_waiters();
      } else {
        auto* lock_entry = &(*tablet_lock_info->mutable_transaction_locks())[txn_id.ToString()];
        DCHECK(!lock_entry->has_waiting_locks());
        waiter_info = lock_entry->mutable_waiting_locks();
      }

      waiter_info->set_wait_start_ht(lock_status_info.wait_start.ToUint64());
      for (const auto& blocker : lock_status_info.blockers) {
        auto& id = blocker->id();
        waiter_info->add_blocking_txn_ids(id.data(), id.size());
      }

      for (const auto& lock_batch_entry : lock_status_info.locks) {
        const auto& partial_doc_key_slice = lock_batch_entry.key.as_slice();
        std::string doc_key_str;
        doc_key_str.reserve(partial_doc_key_slice.size() + 1);
        partial_doc_key_slice.AppendTo(&doc_key_str);
        // We shouldn't append kGroupEnd when the sub dockey ends with a column id.
        if (!DocKey::EncodedSize(partial_doc_key_slice, DocKeyPart::kWholeDocKey).ok()) {
          DCHECK(partial_doc_key_slice.empty() ||
                 !partial_doc_key_slice.ends_with(KeyEntryTypeAsChar::kGroupEnd));
          // kGroupEnd suffix is stripped from the DocKey as part of DetermineKeysToLock.
          // Append kGroupEnd for the decoder to work as expected and not error out.
          doc_key_str.append(&KeyEntryTypeAsChar::kGroupEnd, 1);
        }

        auto parsed_intent = ParsedIntent {
          .doc_path = Slice(doc_key_str.c_str(), doc_key_str.size()),
          .types = lock_batch_entry.intent_types,
          .doc_ht = Slice(),
        };
        // TODO(pglocks): Populate 'subtransaction_id' & 'is_explicit' info of waiter txn(s) in
        // the LockInfoPB response. Currently we don't track either for waiter txn(s).
        auto* lock = waiter_info->add_locks();
        RETURN_NOT_OK(docdb::PopulateLockInfoFromParsedIntent(
            parsed_intent, DecodedIntentValue{}, table_info_provider, lock,
            /* intent_has_ht */ false));
        lock->set_subtransaction_id(lock_status_info.subtxn_id);
      }
    }
    return Status::OK();
  }

 private:
  void HandleWaiterStatusFromParticipant(
      WaiterDataPtr waiter, Result<TransactionStatusResult> res) {
    if (!res.ok() && res.status().IsNotFound()) {
      {
        SharedLock l(mutex_);
        if (shutting_down_) {
          VLOG_WITH_PREFIX_AND_FUNC(1) << "Skipping status RPC for waiter in shutdown wait queue.";
          return;
        }
      }
      // Currently it may be the case that a waiting txn has not yet registered with the local
      // txn participant if it has not yet operated on the local tablet. The semantics of
      // txn_status_manager_->RequestStatusAt assume that any requested txn_id has participated on
      // the local tablet at least once before, and if there is no state in the local participant
      // relating to this txn_id it returns a NotFound status. So in that case, we send an RPC
      // directly to the status tablet to determine the status of this transaction.
      waiter->TriggerStatusRequest(
          clock_->Now(), &client(),
          std::bind(&Impl::HandleWaiterStatusRpcResponse, this, waiter->id, _1, _2));
      return;
    }
    HandleWaiterStatusResponse(waiter, res);
  }

  void HandleWaiterStatusRpcResponse(
      const TransactionId& waiter_id, const Status& status,
      const tserver::GetTransactionStatusResponsePB& resp) {
    if (status.ok() && resp.status(0) == PENDING) {
      VLOG_WITH_PREFIX(4) << "Waiter status pending " << waiter_id;
      return;
    }
    WaiterDataPtr waiter;
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
      waiter->InvokeCallback(
          status.CloneAndPrepend("Failed to get txn status while waiting"));
      return;
    }
    if (resp.has_error()) {
      waiter->InvokeCallback(StatusFromPB(resp.error().status()).CloneAndPrepend(
          "Failed to get txn status while waiting"));
      return;
    }
    if (resp.status(0) == ABORTED) {
      VLOG_WITH_PREFIX(1) << "Waiter status aborted " << waiter_id;
      if (resp.deadlock_reason().empty() || resp.deadlock_reason(0).code() == AppStatusPB::OK) {
        waiter->InvokeCallback(
            // Return InternalError so that TabletInvoker does not retry.
            STATUS_EC_FORMAT(InternalError, TransactionError(TransactionErrorCode::kConflict),
                             "Transaction $0 was aborted while waiting for locks", waiter_id));
      } else {
        waiter->InvokeCallback(StatusFromPB(resp.deadlock_reason(0)));
      }
      return;
    }
    LOG(DFATAL) << "Waiting transaction " << waiter_id
                << " found in unexpected state " << resp.status(0);
  }

  void HandleWaiterStatusResponse(
      const WaiterDataPtr& waiter, Result<TransactionStatusResult> res) {
    auto status = UnwrapResult(res);
    VLOG_WITH_PREFIX(4) << "Got waiter " << waiter->id << " status result " << res.ToString();
    if (!status.ok()) {
      InvokeWaiterCallback(status.status(), waiter);
    } else if (*status == ResolutionStatus::kAborted) {
      // TODO(wait-queues): We might hit this branch when a waiter transaction undergoes promotion.
      //
      // Refer issue: https://github.com/yugabyte/yugabyte-db/issues/16375
      InvokeWaiterCallback(
          STATUS_EC_FORMAT(
            InternalError, TransactionError(TransactionErrorCode::kConflict),
            "Transaction was aborted while waiting for locks $0", waiter->id),
          waiter);
    } else if (*status == ResolutionStatus::kDeadlocked) {
      DCHECK(!res->expected_deadlock_status.ok());
      InvokeWaiterCallback(res->expected_deadlock_status, waiter);
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
    WaiterDataPtr found_waiter = nullptr;
    {
      UniqueLock l(mutex_);
      auto it = waiter_status_.find(waiter_data->id);
      if (it != waiter_status_.end()) {
        found_waiter = it->second;
        waiter_status_.erase(it);
      }
    }

    LOG_IF(WARNING, !found_waiter)
      << "Tried to invoke callback on waiter which has already been removed. "
      << "This should be rare but is not an error otherwise.";

    // Note -- it's important that we remove the waiter from waiter_status_ before invoking it's
    // callback. Otherwise, the callback will re-run conflict resolution, end up back in the wait
    // queue, and attempt to reuse the WaiterData still present in waiter_status_.
    if (found_waiter) {
      waiter_runner_.Submit(found_waiter, status, resume_ht);
    }
  }

  void SignalWaiter(const WaiterDataPtr& waiter_data) {
    VLOG_WITH_PREFIX(4) << "Signaling waiter " << waiter_data->id;
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
      // possible, e.g. if the blocking transaction was not a lock-only conflict and was commited.
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
      WaiterDataPtr,
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
  scoped_refptr<Histogram> pending_time_waiting_;
  scoped_refptr<Histogram> finished_waiting_latency_;
  scoped_refptr<Histogram> blockers_per_waiter_;
  scoped_refptr<Histogram> waiters_per_blocker_;
  scoped_refptr<AtomicGauge<uint64_t>> total_waiters_;
  scoped_refptr<AtomicGauge<uint64_t>> total_blockers_;
};

WaitQueue::WaitQueue(
    TransactionStatusManager* txn_status_manager,
    const std::string& permanent_uuid,
    WaitingTxnRegistry* waiting_txn_registry,
    const std::shared_future<client::YBClient*>& client_future,
    const server::ClockPtr& clock,
    const MetricEntityPtr& metrics,
    std::unique_ptr<ThreadPoolToken> thread_pool_token):
  impl_(new Impl(txn_status_manager, permanent_uuid, waiting_txn_registry, client_future, clock,
                 metrics, std::move(thread_pool_token))) {}

WaitQueue::~WaitQueue() = default;

Status WaitQueue::WaitOn(
    const TransactionId& waiter, SubTransactionId subtxn_id, LockBatch* locks,
    std::shared_ptr<ConflictDataManager> blockers, const TabletId& status_tablet_id,
    uint64_t serial_no, WaitDoneCallback callback) {
  return impl_->WaitOn(
      waiter, subtxn_id, locks, std::move(blockers), status_tablet_id, serial_no, callback);
}


Result<bool> WaitQueue::MaybeWaitOnLocks(
    const TransactionId& waiter, SubTransactionId subtxn_id, LockBatch* locks,
    const TabletId& status_tablet_id, uint64_t serial_no, WaitDoneCallback callback) {
  return impl_->MaybeWaitOnLocks(waiter, subtxn_id, locks, status_tablet_id, serial_no, callback);
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

Status WaitQueue::GetLockStatus(const std::set<TransactionId>& transaction_ids,
                                const TableInfoProvider& table_info_provider,
                                TabletLockInfoPB* tablet_lock_info) const {
  return impl_->GetLockStatus(transaction_ids, table_info_provider, tablet_lock_info);
}

}  // namespace docdb
}  // namespace yb
