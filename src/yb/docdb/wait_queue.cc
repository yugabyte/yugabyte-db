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

#include <future>
#include <memory>

#include <boost/algorithm/string/join.hpp>

#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/gutil/stl_util.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/transaction_participant_context.h"
#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/unique_lock.h"

DEFINE_uint64(wait_for_relock_unblocked_txn_keys_ms, 0,
              "If greater than zero, indicates the maximum amount of time to wait to lock keys "
              "needed by a newly unblocked transaction. Otherwise, a default value of 1s is used.");
TAG_FLAG(wait_for_relock_unblocked_txn_keys_ms, advanced);
TAG_FLAG(wait_for_relock_unblocked_txn_keys_ms, hidden);
TAG_FLAG(wait_for_relock_unblocked_txn_keys_ms, runtime);

using namespace std::chrono_literals;

namespace yb {
namespace docdb {

namespace {

CoarseTimePoint GetWaitForRelockUnblockedKeysDeadline() {
  static constexpr auto kDefaultWaitForRelockUnblockedTxnKeys = 1000ms;
  if (FLAGS_wait_for_relock_unblocked_txn_keys_ms > 0) {
    return FLAGS_wait_for_relock_unblocked_txn_keys_ms * 1ms + CoarseMonoClock::Now();
  }
  return kDefaultWaitForRelockUnblockedTxnKeys + CoarseMonoClock::Now();
}

YB_DEFINE_ENUM(ResolutionStatus, (kPending)(kCommitted)(kAborted));

class BlockerData;

// Data for an active transaction which is waiting on some number of other transactions with which
// it has detected conflicts. The blockers field owns shared_ptr references to BlockerData of
// pending transactions it's blocked by. These references keep the BlockerData instances alive in
// the wait queue's blocker_status_ field, which stores only weak_ptr references. Invalid references
// in blocker_status_ are presumed to no lonber be of concern to any pending waiting transactions
// and are discarded.
struct WaiterData {
  WaiterData(const TransactionId id_, LockBatch* const locks_,
             const std::vector<std::shared_ptr<BlockerData>> blockers_,
             const WaitDoneCallback callback_,
             std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration_)
      : id(id_),
        locks(locks_),
        blockers(std::move(blockers_)),
        callback(std::move(callback_)),
        waiter_registration(std::move(waiter_registration_)),
        unlocked_(locks->Unlock()) {}

  const TransactionId id;
  LockBatch* const locks;
  const std::vector<std::shared_ptr<BlockerData>> blockers;
  const WaitDoneCallback callback;
  std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration;
  const CoarseTimePoint created_at = CoarseMonoClock::Now();

  void InvokeCallback(const Status& status) {
    VLOG_WITH_PREFIX(4) << "Invoking waiter callback " << status;
    if (!status.ok()) {
      callback(status);
      return;
    }
    *locks = std::move(unlocked_).Lock(GetWaitForRelockUnblockedKeysDeadline());
    callback(locks->status());
  }

  std::string LogPrefix() {
    return Format("TxnId: $0 ", id);
  }

 private:
  UnlockedBatch unlocked_;
};
using WaiterDataPtr = std::shared_ptr<WaiterData>;

// Data for an active transaction which is blocking another active transaction which is waiting
// in the wait queue. Contains a list of weak_ptr to WaiterData (see below) of corresponding waiting
// transactions. If a weak_ptr stored in this list expires, we can assume the waiter is no longer
// active and has exited the wait queue.
class BlockerData {
 public:
  std::vector<WaiterDataPtr> Signal(const Result<ResolutionStatus>& result) {
    UniqueLock<decltype(mutex_)> l(mutex_);
    std::vector<WaiterDataPtr> waiters_to_signal;
    result_ = result;

    waiters_to_signal.reserve(waiters_.size());
    EraseIf([&waiters_to_signal](const auto& weak_waiter) {
      if (auto waiter = weak_waiter.lock()) {
        waiters_to_signal.push_back(waiter);
        return false;
      }
      return true;
    }, &waiters_);
    return waiters_to_signal;
  }

  void AddWaiter(const WaiterDataPtr& waiter_data) {
    UniqueLock<decltype(mutex_)> blocker_lock(mutex_);
    waiters_.push_back(waiter_data);
  }

  Result<bool> IsResolved() {
    SharedLock<decltype(mutex_)> blocker_lock(mutex_);
    return VERIFY_RESULT(Copy(result_)) != ResolutionStatus::kPending;
  }

  auto CleanAndGetSize() {
    UniqueLock<decltype(mutex_)> l(mutex_);
    EraseIf([](const auto& weak_waiter) {
      return weak_waiter.expired();
    }, &waiters_);
    return waiters_.size();
  }

  auto DEBUG_GetWaiterIds() const {
    std::vector<std::string> waiters;
    SharedLock<decltype(mutex_)> l(mutex_);
    for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
      if (auto waiter = it->lock()) {
        waiters.push_back(waiter->id.ToString());
      }
    }
    return boost::algorithm::join(waiters, ",");
  }

 private:
  mutable rw_spinlock mutex_;
  Result<ResolutionStatus> result_ GUARDED_BY(mutex_) = ResolutionStatus::kPending;
  std::vector<std::weak_ptr<WaiterData>> waiters_ GUARDED_BY(mutex_);
};

Result<ResolutionStatus> UnwrapResult(const Result<TransactionStatusResult>& res) {
  if (!res.ok()) {
    if (res.status().IsNotFound()) {
      return ResolutionStatus::kAborted;
    } else if (res.status().IsTryAgain()) {
      return ResolutionStatus::kPending;
    } else {
      return res.status();
    }
  }
  switch (res->status) {
    case COMMITTED:
      return ResolutionStatus::kCommitted;
    case ABORTED:
      return ResolutionStatus::kAborted;
    default:
      if (res->status != PENDING) {
        return STATUS_FORMAT(
          IllegalState,
          "Unexpected transaction status result in wait queue: $0", res->ToString());
      }
      return ResolutionStatus::kPending;
  }
}

const Status kShuttingDownError = STATUS(
    IllegalState, "Tablet shutdown in progress - there may be a new leader.");

} // namespace

class WaitQueue::Impl {
 public:
  Impl(TransactionStatusManager* txn_status_manager, const std::string& permanent_uuid,
       WaitingTxnRegistry* waiting_txn_registry)
      : txn_status_manager_(txn_status_manager), permanent_uuid_(permanent_uuid),
        waiting_txn_registry_(waiting_txn_registry) {}

  Status WaitOn(
      const TransactionId& waiter_txn_id, LockBatch* locks,
      std::vector<BlockingTransactionData>&& blockers, const TabletId& status_tablet_id,
      WaitDoneCallback callback) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "waiter_txn_id=" << waiter_txn_id
                                 << " blockers=" << ToString(blockers)
                                 << " status_tablet_id=" << status_tablet_id;

    // TODO(pessimistic): We can detect tablet-local deadlocks here.
    // See https://github.com/yugabyte/yugabyte-db/issues/13586
    std::vector<std::shared_ptr<BlockerData>> blocker_datas;
    WaiterDataPtr waiter_data = nullptr;
    {
      UniqueLock<decltype(mutex_)> wq_lock(mutex_);
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

      for (const auto& blocker : blockers) {
        auto blocker_data = std::make_shared<BlockerData>();

        auto [iter, did_insert] = blocker_status_.emplace(blocker.id, blocker_data);
        if (!did_insert) {
          if (auto placed_blocker_node = iter->second.lock()) {
            VLOG_WITH_PREFIX_AND_FUNC(4) << "Re-using blocker " << blocker.id;
            blocker_data = placed_blocker_node;
          } else {
            // TODO(pessimistic): We should only ever hit this case if a blocker was resolved and
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
        blocker_datas.push_back(blocker_data);
      }

      // TODO(pessimistic): similar to pg, we can wait 1s or so before beginning deadlock detection.
      // See https://github.com/yugabyte/yugabyte-db/issues/13576
      auto scoped_reporter = waiting_txn_registry_->Create();
      RETURN_NOT_OK(
          scoped_reporter->Register(waiter_txn_id, std::move(blockers), status_tablet_id));
      DCHECK_GE(scoped_reporter->GetDataUseCount(), 1);

      waiter_data = std::make_shared<WaiterData>(
          waiter_txn_id, locks, std::move(blocker_datas), std::move(callback),
          std::move(scoped_reporter));
      waiter_status_[waiter_txn_id] = waiter_data;
    }

    DCHECK(waiter_data);
    for (auto blocker : waiter_data->blockers) {
      blocker->AddWaiter(waiter_data);
    }

    return Status::OK();
  }

  void Poll(HybridTime now) EXCLUDES(mutex_) {
    // TODO(pessimistic): Rely on signaling from the RunningTransaction instance of the blocker
    // rather than this polling-based mechanism. We should also signal from the RunningTransaction
    // instance of the waiting transaction in case the waiter is aborted by deadlock or otherwise.
    // See https://github.com/yugabyte/yugabyte-db/issues/13578
    const std::string kReason = "Getting status for wait queue";
    std::vector<WaiterDataPtr> waiters;
    std::vector<TransactionId> blockers;

    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      if (shutting_down_) {
        return;
      }
      for (auto it = waiter_status_.begin(); it != waiter_status_.end(); ++it) {
        waiters.push_back(it->second);
      }
      for (auto it = blocker_status_.begin(); it != blocker_status_.end();) {
        if (auto blocker = it->second.lock()) {
          if (blocker->CleanAndGetSize() != 0) {
            VLOG_WITH_PREFIX(4)
                << Format("blocker($0) has waiters($1)", it->first, blocker->DEBUG_GetWaiterIds());
            blockers.push_back(it->first);
            it++;
            continue;
          }
          VLOG_WITH_PREFIX(4) << "Erasing blocker with no live waiters " << it->first;
        } else {
          VLOG_WITH_PREFIX(4) << "Erasing blocker with no references " << it->first;
        }
        it = blocker_status_.erase(it);
      }
    }

    for (const auto& waiter : waiters) {
      auto duration = CoarseMonoClock::Now() - waiter->created_at;
      auto seconds = duration / 1s;
      VLOG_WITH_PREFIX_AND_FUNC(4) << waiter->id << " waiting for " << seconds << " seconds";
      // TODO(pessimistic): Allow checking status of waiting txn. Currently it may be the case that
      // a waiting txn has not yet registered with the local txn participant if it has not yet
      // operated on the local tablet. The semantics of txn_status_manager_->RequestStatusAt assume
      // that any requested txn_id has participated on the local tablet at least once before, and if
      // there is no state in the local participant relating to this txn_id it assumes it has been
      // aborted. We should either have some path to request the status of this waiter with
      // relaxed semantics or we should register this txn with the participant before entering the
      // wait queue.
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
        .reason = &kReason,
        .flags = TransactionLoadFlags {},
        .callback = [transaction_id, this](Result<TransactionStatusResult> res) {
          HandleBlockerStatusResponse(transaction_id, res);
        }
      };
      txn_status_manager_->RequestStatusAt(request);
    }
  }

  void StartShutdown() EXCLUDES(mutex_) {
    decltype(waiter_status_) waiter_status_copy;
    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      shutting_down_ = true;
      waiter_status_copy.swap(waiter_status_);
      blocker_status_.clear();
    }

    for (const auto& [_, waiter_data] : waiter_status_copy) {
      waiter_data->InvokeCallback(kShuttingDownError);
    }
  }

  void CompleteShutdown() EXCLUDES(mutex_) {
    SharedLock<decltype(mutex_)> l(mutex_);
    LOG_IF(DFATAL, !shutting_down_)
        << "Called CompleteShutdown() while not in shutting_down_ state.";
    LOG_IF(DFATAL, !blocker_status_.empty())
        << "Called CompleteShutdown without empty blocker_status_";
    LOG_IF(DFATAL, !waiter_status_.empty())
        << "Called CompleteShutdown without empty waiter_status_";
  }

 private:
  void MarkBlockingTransactionComplete(
      const TransactionId& transaction, Result<ResolutionStatus> status) EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "transaction: " << transaction << " - res: " << status;

    std::shared_ptr<BlockerData> resolved_blocker = nullptr;
    {
      SharedLock<decltype(mutex_)> l(mutex_);
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

    if (resolved_blocker) {
      VLOG_WITH_PREFIX(4) << "Signaling waiters for - " << transaction
                          << " - " << " is_committed: " << status;
      for (const auto& waiter : resolved_blocker->Signal(status)) {
        // TODO(pessimistic): Resolve these waiters in parallel.
        SignalWaiter(waiter);
      }
    }
  }

  void InvokeWaiterCallback(
      const Status& status, const WaiterDataPtr& waiter_data) EXCLUDES(mutex_) {
    auto res = 0ul;
    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      res = waiter_status_.erase(waiter_data->id);
    }

    LOG_IF(WARNING, res != 1)
      << "Tried to invoke callback on waiter which has already been removed. "
      << "This should be rare but is not an error otherwise.";

    // Note -- it's important that we remove the waiter from waiter_status_ before invoking it's
    // callback. Otherwise, the callback will re-run conflict resolution, end up back in the wait
    // queue, and attempt to reuse the WaiterData still present in waiter_status_.
    if (res == 1) {
      waiter_data->InvokeCallback(status);
    }
  }

  void SignalWaiter(const WaiterDataPtr& waiter_data) {
    VLOG_WITH_PREFIX(4) << "Signaling waiter " << waiter_data->id;
    Status status = Status::OK();
    size_t num_resolved_blockers = 0;

    for (const auto& blocker_data : waiter_data->blockers) {
      auto is_resolved = blocker_data->IsResolved();
      if (!is_resolved.ok()) {
        status = is_resolved.status();
        break;
      }
      if (*is_resolved) {
        num_resolved_blockers++;
      }
    }

    if (waiter_data->blockers.size() == num_resolved_blockers || !status.ok()) {
      // TODO(pessimistic): Abort transactions without re-invoking conflict resolution when
      // possible, e.g. if the blocking transaction was not a lock-only conflict and was commited.
      // See https://github.com/yugabyte/yugabyte-db/issues/13577
      InvokeWaiterCallback(status, waiter_data);
    }
  }

  void HandleBlockerStatusResponse(
      const TransactionId& txn_id, Result<TransactionStatusResult> res) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "txn_id: " << txn_id << " - res " << res.ToString();
    auto status = UnwrapResult(res);
    if (!status.ok() || *status != ResolutionStatus::kPending) {
      MarkBlockingTransactionComplete(txn_id, status);
    }
  }

  std::string LogPrefix() const {
    return Format("T $0 P $1 - ", txn_status_manager_->tablet_id(), permanent_uuid_);
  }

  mutable rw_spinlock mutex_;

  bool shutting_down_ GUARDED_BY(mutex_) = false;

  std::unordered_map<
      TransactionId,
      std::weak_ptr<BlockerData>,
      TransactionIdHash>
    blocker_status_ GUARDED_BY(mutex_);

  std::unordered_map<
      TransactionId,
      WaiterDataPtr,
      TransactionIdHash>
    waiter_status_ GUARDED_BY(mutex_);

  TransactionStatusManager* const txn_status_manager_;
  const std::string& permanent_uuid_;
  WaitingTxnRegistry* const waiting_txn_registry_;
};

WaitQueue::WaitQueue(
    TransactionStatusManager* txn_status_manager,
    const std::string& permanent_uuid,
    WaitingTxnRegistry* waiting_txn_registry):
  impl_(new Impl(txn_status_manager, permanent_uuid, waiting_txn_registry)) {}

WaitQueue::~WaitQueue() = default;

Status WaitQueue::WaitOn(
    const TransactionId& waiter, LockBatch* locks,
    std::vector<BlockingTransactionData>&& blockers, const TabletId& status_tablet_id,
    WaitDoneCallback callback) {
  return impl_->WaitOn(waiter, locks, std::move(blockers), status_tablet_id, callback);
}

void WaitQueue::Poll(HybridTime now) {
  return impl_->Poll(now);
}

void WaitQueue::StartShutdown() {
  return impl_->StartShutdown();
}

void WaitQueue::CompleteShutdown() {
  return impl_->CompleteShutdown();
}

}  // namespace docdb
}  // namespace yb
