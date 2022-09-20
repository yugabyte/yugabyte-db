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

#include "yb/client/client.h"
#include "yb/client/transaction_rpc.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/server/clock.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/transaction_participant_context.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/memory/memory.h"
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
using namespace std::placeholders;

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
using BlockerDataAndSubtxnInfo = std::pair<std::shared_ptr<BlockerData>,
                                           std::shared_ptr<SubtxnHasNonLockConflict>>;

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
      return ResolutionStatus::kAborted;
    case PENDING:
      return ResolutionStatus::kPending;
    default:
      return STATUS_FORMAT(
        IllegalState,
        "Unexpected transaction status result in wait queue: $0", res->ToString());
  }
}

// Data for an active transaction which is waiting on some number of other transactions with which
// it has detected conflicts. The blockers field owns shared_ptr references to BlockerData of
// pending transactions it's blocked by. These references keep the BlockerData instances alive in
// the wait queue's blocker_status_ field, which stores only weak_ptr references. Invalid references
// in blocker_status_ are presumed to no lonber be of concern to any pending waiting transactions
// and are discarded.
struct WaiterData : public std::enable_shared_from_this<WaiterData> {
  WaiterData(const TransactionId id_, LockBatch* const locks_, const TabletId& status_tablet_,
             const std::vector<BlockerDataAndSubtxnInfo> blockers_,
             const WaitDoneCallback callback_,
             std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration_,
             rpc::Rpcs* rpcs)
      : id(id_),
        locks(locks_),
        status_tablet(status_tablet_),
        blockers(std::move(blockers_)),
        callback(std::move(callback_)),
        waiter_registration(std::move(waiter_registration_)),
        unlocked_(locks->Unlock()),
        rpcs_(*rpcs) {}

  const TransactionId id;
  LockBatch* const locks;
  const TabletId status_tablet;
  const std::vector<BlockerDataAndSubtxnInfo> blockers;
  const WaitDoneCallback callback;
  std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration;
  const CoarseTimePoint created_at = CoarseMonoClock::Now();

  void InvokeCallback(const Status& status) {
    VLOG_WITH_PREFIX(4) << "Invoking waiter callback " << status;
    if (!status.ok() || !unlocked_) {
      callback(status);
      return;
    }
    *locks = std::move(*unlocked_).Lock(GetWaitForRelockUnblockedKeysDeadline());
    unlocked_.reset();
    callback(locks->status());
  }

  std::string LogPrefix() {
    return Format("TxnId: $0 ", id);
  }

  using StatusCb = std::function<
      void(const Status& status, const tserver::GetTransactionStatusResponsePB& resp)>;

  void TriggerStatusRequest(HybridTime now, client::YBClient* client, StatusCb cb) {
    UniqueLock<decltype(mutex_)> l(mutex_);

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
                  UniqueLock<decltype(mutex_)> l(instance->mutex_);
                  if (instance->handle_ != instance->rpcs_.InvalidHandle()) {
                    instance->rpcs_.Unregister(&instance->handle_);
                  }
                }
                cb(status, resp);
            }),
        &handle_);
  }

  void ShutdownStatusRequest() {
    UniqueLock<decltype(mutex_)> l(mutex_);
    if (handle_ != rpcs_.InvalidHandle()) {
      (**handle_).Abort();
      rpcs_.Unregister(&handle_);
    }
  }

  bool IsSingleShard() const {
    return id.IsNil();
  }

 private:
  std::optional<UnlockedBatch> unlocked_ = std::nullopt;

  mutable rw_spinlock mutex_;
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
  std::vector<WaiterDataPtr> Signal(Result<TransactionStatusResult>&& txn_status_response) {
    auto txn_status = UnwrapResult(txn_status_response);
    bool is_txn_pending = txn_status.ok() && *txn_status == ResolutionStatus::kPending;
    bool should_signal = !is_txn_pending;

    UniqueLock<decltype(mutex_)> l(mutex_);
    std::vector<WaiterDataPtr> waiters_to_signal;

    txn_status_ = txn_status;
    if (txn_status_response.ok()) {
      if (aborted_subtransactions_ != txn_status_response->aborted_subtxn_set) {
        // TODO(pessimistic): Avoid copying the subtransaction set. See:
        // https://github.com/yugabyte/yugabyte-db/issues/13823
        aborted_subtransactions_ = std::move(txn_status_response->aborted_subtxn_set);
        should_signal = true;
      }
    }

    if (should_signal) {
      waiters_to_signal.reserve(waiters_.size());
      EraseIf([&waiters_to_signal](const auto& weak_waiter) {
        if (auto waiter = weak_waiter.lock()) {
          waiters_to_signal.push_back(waiter);
          return false;
        }
        return true;
      }, &waiters_);
    }

    return waiters_to_signal;
  }

  void AddWaiter(const WaiterDataPtr& waiter_data) {
    UniqueLock<decltype(mutex_)> blocker_lock(mutex_);
    waiters_.push_back(waiter_data);
  }

  Result<bool> IsResolved() {
    SharedLock<decltype(mutex_)> blocker_lock(mutex_);
    return VERIFY_RESULT(Copy(txn_status_)) != ResolutionStatus::kPending;
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

  bool HasLiveSubtransaction(const SubtxnHasNonLockConflict& subtransaction_info) {
    SharedLock<decltype(mutex_)> blocker_lock(mutex_);
    for (const auto& [id, _] : subtransaction_info) {
      if (!aborted_subtransactions_.Test(id)) {
        return true;
      }
    }
    return false;
  }

 private:
  mutable rw_spinlock mutex_;
  Result<ResolutionStatus> txn_status_ GUARDED_BY(mutex_) = ResolutionStatus::kPending;
  AbortedSubTransactionSet aborted_subtransactions_ GUARDED_BY(mutex_);
  std::vector<std::weak_ptr<WaiterData>> waiters_ GUARDED_BY(mutex_);
};

const Status kShuttingDownError = STATUS(
    IllegalState, "Tablet shutdown in progress - there may be a new leader.");

} // namespace

class WaitQueue::Impl {
 public:
  Impl(TransactionStatusManager* txn_status_manager, const std::string& permanent_uuid,
       WaitingTxnRegistry* waiting_txn_registry,
       const std::shared_future<client::YBClient*>& client_future,
       const server::ClockPtr& clock)
      : txn_status_manager_(txn_status_manager), permanent_uuid_(permanent_uuid),
        waiting_txn_registry_(waiting_txn_registry), client_future_(client_future), clock_(clock) {}

  ~Impl() {
    if (StartShutdown()) {
      CompleteShutdown();
    } else {
      LOG_IF_WITH_PREFIX(DFATAL, !shutdown_complete_.load())
          << "Destroying wait queue that did not complete shutdown";
    }
  }

  Status WaitOn(
      const TransactionId& waiter_txn_id, LockBatch* locks,
      std::vector<BlockingTransactionData>&& blockers, const TabletId& status_tablet_id,
      WaitDoneCallback callback) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "waiter_txn_id=" << waiter_txn_id
                                 << " blockers=" << ToString(blockers)
                                 << " status_tablet_id=" << status_tablet_id;

    // TODO(pessimistic): We can detect tablet-local deadlocks here.
    // See https://github.com/yugabyte/yugabyte-db/issues/13586
    std::vector<BlockerDataAndSubtxnInfo> blocker_datas;
    WaiterDataPtr waiter_data = nullptr;
    {
      UniqueLock<decltype(mutex_)> wq_lock(mutex_);
      if (shutting_down_) {
        return kShuttingDownError;
      }

      if (waiter_status_.contains(waiter_txn_id)) {
        // TODO(pessimistic): If two single-shard transactions come to the same tablet, we will hit
        // this branch since they will both have waiter_txn_id=TransactionId::Nil(). We should
        // handle this by storing single-shard transactions separately rather than indexing by
        // TransactionId. See: https://github.com/yugabyte/yugabyte-db/issues/14014
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
        blocker_datas.emplace_back(blocker_data, blocker.subtransactions);
      }

      // TODO(pessimistic): similar to pg, we can wait 1s or so before beginning deadlock detection.
      // See https://github.com/yugabyte/yugabyte-db/issues/13576
      auto scoped_reporter = waiting_txn_registry_->Create();
      if (!waiter_txn_id.IsNil()) {
        // If waiter_txn_id is Nil, then we're processing a single-shard transaction. We do not have
        // to report single shard transactions to transaction coordinators because they can't
        // possibly be involved in a deadlock. This is true because no transactions can wait on
        // single shard transactions, so they only have out edges in the wait-for graph and cannot
        // be a part of a cycle.
        DCHECK(!status_tablet_id.empty());
        RETURN_NOT_OK(scoped_reporter->Register(
            waiter_txn_id, std::move(blockers), status_tablet_id));
        DCHECK_GE(scoped_reporter->GetDataUseCount(), 1);
      }

      waiter_data = std::make_shared<WaiterData>(
          waiter_txn_id, locks, status_tablet_id, std::move(blocker_datas), std::move(callback),
          std::move(scoped_reporter), &rpcs_);
      waiter_status_[waiter_txn_id] = waiter_data;
    }

    DCHECK(waiter_data);
    for (auto [blocker, _] : waiter_data->blockers) {
      blocker->AddWaiter(waiter_data);
    }

    return Status::OK();
  }

  void Poll(HybridTime now) EXCLUDES(mutex_) {
    // TODO(pessimistic): Rely on signaling from the RunningTransaction instance of the blocker
    // rather than this polling-based mechanism. We should also signal from the RunningTransaction
    // instance of the waiting transaction in case the waiter is aborted by deadlock or otherwise.
    // See https://github.com/yugabyte/yugabyte-db/issues/13578
    const std::string kBlockerReason = "Getting status for blocker wait queue";
    const std::string kWaiterReason = "Getting status for waiter in wait queue";
    std::vector<WaiterDataPtr> waiters;
    std::vector<TransactionId> blockers;

    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      if (shutting_down_) {
        return;
      }
      for (auto it = waiter_status_.begin(); it != waiter_status_.end(); ++it) {
        auto& waiter = it->second;
        if (!waiter->IsSingleShard()) {
          DCHECK(!waiter->status_tablet.empty());
          waiters.push_back(waiter);
        }
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
  }

  bool StartShutdown() EXCLUDES(mutex_) {
    decltype(waiter_status_) waiter_status_copy;

    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      if (shutting_down_) {
        return false;
      }
      shutting_down_ = true;
      waiter_status_copy.swap(waiter_status_);
      blocker_status_.clear();
    }

    for (const auto& [_, waiter_data] : waiter_status_copy) {
      waiter_data->ShutdownStatusRequest();
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
    SharedLock<decltype(mutex_)> l(mutex_);
    rpcs_.Shutdown();
    LOG_IF(DFATAL, !shutting_down_)
        << "Called CompleteShutdown() while not in shutting_down_ state.";
    LOG_IF(DFATAL, !blocker_status_.empty())
        << "Called CompleteShutdown without empty blocker_status_";
    LOG_IF(DFATAL, !waiter_status_.empty())
        << "Called CompleteShutdown without empty waiter_status_";
  }

 private:
  void HandleWaiterStatusFromParticipant(
      WaiterDataPtr waiter, Result<TransactionStatusResult> res) {
    if (!res.ok() && res.status().IsNotFound()) {
      {
        SharedLock<decltype(mutex_)> l(mutex_);
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
      UniqueLock<decltype(mutex_)> l(mutex_);
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
      waiter->InvokeCallback(
          // We return InternalError so that TabletInvoker does not retry.
          STATUS_FORMAT(InternalError, "Transaction $0 was aborted while waiting for locks",
                        waiter_id));
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
      InvokeWaiterCallback(
          STATUS_FORMAT(Aborted, "Transaction was aborted while waiting for locks $0", waiter->id),
          waiter);
    }
  }

  void MaybeSignalWaitingTransactions(
      const TransactionId& transaction, Result<TransactionStatusResult> res) EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "transaction: " << transaction
                                 << " - res: " << res << " - aborted "
                                 << (res.ok() ? res->aborted_subtxn_set.ToString() : "error");
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

    if (!resolved_blocker) {
      VLOG_WITH_PREFIX(4) << "Could not resolve blocker " << transaction << " for result " << res;
      return;
    }

    for (const auto& waiter : resolved_blocker->Signal(std::move(res))) {
      // TODO(pessimistic): Resolve these waiters in parallel.
      SignalWaiter(waiter);
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

    for (const auto& [blocker_data, subtransaction_info] : waiter_data->blockers) {
      auto is_resolved = blocker_data->IsResolved();
      if (!is_resolved.ok()) {
        status = is_resolved.status();
        break;
      }
      if (*is_resolved ||
          !blocker_data->HasLiveSubtransaction(*DCHECK_NOTNULL(subtransaction_info))) {
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
      TransactionId,
      WaiterDataPtr,
      TransactionIdHash>
    waiter_status_ GUARDED_BY(mutex_);

  rpc::Rpcs rpcs_;

  TransactionStatusManager* const txn_status_manager_;
  const std::string& permanent_uuid_;
  WaitingTxnRegistry* const waiting_txn_registry_;
  const std::shared_future<client::YBClient*>& client_future_;
  const server::ClockPtr& clock_;
};

WaitQueue::WaitQueue(
    TransactionStatusManager* txn_status_manager,
    const std::string& permanent_uuid,
    WaitingTxnRegistry* waiting_txn_registry,
    const std::shared_future<client::YBClient*>& client_future,
    const server::ClockPtr& clock):
  impl_(new Impl(txn_status_manager, permanent_uuid, waiting_txn_registry, client_future, clock)) {}

WaitQueue::~WaitQueue() = default;

Status WaitQueue::WaitOn(
    const TransactionId& waiter, LockBatch* locks,
    std::vector<BlockingTransactionData>&& blockers, const TabletId& status_tablet_id,
    WaitDoneCallback callback) {
  return impl_->WaitOn(
      waiter, locks, std::move(blockers), status_tablet_id, callback);
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

}  // namespace docdb
}  // namespace yb
