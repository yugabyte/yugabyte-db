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

#include "yb/docdb/local_waiting_txn_registry.h"

#include <algorithm>
#include <memory>

#include <boost/optional/optional.hpp>
#include <glog/vlog_is_on.h>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/rpc/rpc.h"
#include "yb/server/clock.h"
#include "yb/tserver/tserver_service.fwd.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/util/atomic.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_log.h"
#include "yb/util/unique_lock.h"
#include "yb/client/transaction_rpc.h"

using namespace std::placeholders;
using namespace std::literals;

DECLARE_bool(enable_wait_queues);
DECLARE_bool(disable_deadlock_detection);
DEFINE_test_flag(uint64, inject_process_update_resp_delay_ms, 0,
                 "Injects a delay in the response handler for full wait-for update requests. Used "
                 "to test that lifetimes of StatusTabletData are valid even if these responses are "
                 "delayed.");

namespace yb {
namespace docdb {

class StatusTabletData;
using StatusTabletDataPtr = std::shared_ptr<StatusTabletData>;

// Container for metadata describing a waiting transaction that is being tracked and reported to the
// transaction coordinator. A shared_ptr is held to status_tablet_data to extend the lifetime of the
// StatusTabletData instance being tracked by the LocalWaitingTxnRegistry.
struct WaitingTransactionData {
  TransactionId id;
  std::shared_ptr<ConflictDataManager> blockers;
  StatusTabletDataPtr status_tablet_data;
  HybridTime wait_start_time;
  rpc::Rpcs::Handle rpc_handle;
};

// Container for holding StatusTabletData and it's corresponding ThreadPoolToken. StatusTabletData
// instance is automatically destroyed when there are no active waiter transactions waiting on it
// and all shared_ptr references from oustanding RPCs have been released. The corresponding
// StatusTabletEntry is then removed from the status_tablets_ in the same thread which sends the
// wait-for graph periodically to the txn coordinator.
//
// Note: The same wait-queue threadpool is used to send partial wait-for updates from
// LocalWaitingTxnRegistry. As a result, thread_pool_token cannot be moved to StatusTabletData as it
// will result in a thread from the wait-queue pool calling Shutdown on another token created from
// the same pool, leading to a deadlock. Instead, StatusTabletEntry instances are cleared in
// LocalWaitingTxnRegistry::SendWaitForGraph which runs from a poller thread.
struct StatusTabletEntry {
  std::weak_ptr<StatusTabletData> status_tablet_data;
  std::unique_ptr<ThreadPoolToken> thread_pool_token;
};

void AttachWaitingTransaction(
    const WaitingTransactionData& data, tserver::UpdateTransactionWaitingForStatusRequestPB* req) {
  auto* txn = req->add_waiting_transactions();
  txn->set_transaction_id(data.id.data(), data.id.size());
  txn->set_wait_start_time(data.wait_start_time.ToUint64());
  for (const auto& blocker : data.blockers->RemainingTransactions()) {
    auto* blocking_txn = txn->add_blocking_transaction();
    blocking_txn->set_transaction_id(blocker.id.data(), blocker.id.size());
    blocking_txn->set_status_tablet_id(blocker.status_tablet);
    SubtxnSet subtxn_set;
    for (const auto& [subtxn_id, _] : blocker.conflict_info->subtransactions) {
      WARN_NOT_OK(
          subtxn_set.SetRange(subtxn_id, subtxn_id),
          Format("Failed to set index $0 of SubtxnSet", subtxn_id));
    }
    subtxn_set.ToPB(blocking_txn->mutable_subtxn_set()->mutable_set());
  }
}

// Container for metadata corresponding to a status tablet to which we are reporting waiting
// transactions. WaitingTransactionData data indicates which waiters to report to this status tablet
// and is weakly held -- WaitingTransactionData instances are kept alive by clients of the
// LocalWaitingTxnRegistry which returns a wrapped WaitingTransactionData to clients to keep alive.
class StatusTabletData : public std::enable_shared_from_this<StatusTabletData> {
 public:
  explicit StatusTabletData(
      rpc::Rpcs* rpcs, client::YBClient* client, const TabletId& status_tablet_id,
      const std::string& tserver_uuid, ThreadPoolToken* thread_pool_token) :
        rpcs_(rpcs), client_(client), status_tablet_id_(status_tablet_id),
        rpc_handle_(rpcs_->InvalidHandle()), tserver_uuid_(tserver_uuid),
        thread_pool_token_(DCHECK_NOTNULL(thread_pool_token)) {}

  void AddWaitingTransactionData(const std::shared_ptr<WaitingTransactionData>& waiter) {
    UniqueLock<decltype(mutex_)> tablet_lock(mutex_);
    waiters.emplace_back(waiter);
  }

  void SendPartialUpdate(std::weak_ptr<WaitingTransactionData> waiter, HybridTime now) {
    if (!FLAGS_enable_wait_queues || PREDICT_FALSE(FLAGS_disable_deadlock_detection)) {
      return;
    }
    auto blocked = waiter.lock();
    if (!blocked) {
      return;
    }

    tserver::UpdateTransactionWaitingForStatusRequestPB req;
    AttachWaitingTransaction(*blocked, &req);
    req.set_tablet_id(status_tablet_id_);
    req.set_propagated_hybrid_time(now.ToUint64());
    req.set_tserver_uuid(tserver_uuid_);
    req.set_is_full_update(false);
    WARN_NOT_OK(
        rpcs_->RegisterAndStartStatus(client::UpdateTransactionWaitingForStatus(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client_,
            &req,
            [shared_this = shared_from(this), blocked](const auto& status, const auto& req) {
              shared_this->rpcs_->Unregister(&blocked->rpc_handle);
            }), &blocked->rpc_handle),
        Format("Failed to register waiter with transaction coordinator for status tablet $0",
              status_tablet_id_));
  }

  Status SendPartialUpdateAsync(const std::weak_ptr<WaitingTransactionData>& waiter,
                                const HybridTime& now) {
    return thread_pool_token_->SubmitFunc(
        std::bind(&StatusTabletData::SendPartialUpdate, shared_from(this), waiter, now));
  }

  void SendFullUpdate(HybridTime now) {
    tserver::UpdateTransactionWaitingForStatusRequestPB req;
    UniqueLock<decltype(mutex_)> l(mutex_);

    auto has_pending_request = rpc_handle_ != rpcs_->InvalidHandle();

    // Attach live waiters to req and remove any waiters which are no longer live.
    EraseIf([&req, has_pending_request](const auto& item) {
      if (auto blocked = item.lock()) {
        if (PREDICT_TRUE(!has_pending_request)) {
          AttachWaitingTransaction(*blocked, &req);
        }
        return false;
      }
      return true;
    }, &waiters);

    if (req.waiting_transactions_size() == 0) {
      VLOG(4)
          << "Not sending UpdateTransactionWaitingForStatusRequestPB for"
          << " status_tablet: " << status_tablet_id_
          << " waiting_transactions_size: " << req.waiting_transactions_size();
      return;
    }

    // Initiating an rpc call within the scope of the mutex (post reading latest status tablet data)
    // ensures that this full update contains all data from partial updates that it follows.
    DCHECK(!has_pending_request)
        << "req should only have waiting transactions if there is no pending request.";
    req.set_tablet_id(status_tablet_id_);
    req.set_propagated_hybrid_time(now.ToUint64());
    req.set_tserver_uuid(tserver_uuid_);
    req.set_is_full_update(true);
    auto did_send = rpcs_->RegisterAndStart(
        client::UpdateTransactionWaitingForStatus(
          TransactionRpcDeadline(),
          nullptr /* tablet */,
          client_,
          &req,
          [shared_this = shared_from(this)](const auto& status, const auto& resp) {
            AtomicFlagSleepMs(&FLAGS_TEST_inject_process_update_resp_delay_ms);
            UniqueLock<decltype(mutex_)> l(shared_this->mutex_);
            shared_this->rpcs_->Unregister(&shared_this->rpc_handle_);
          }),
        &rpc_handle_);
    if (did_send) {
      VLOG(1) << "Sent UpdateTransactionWaitingForStatusRequestPB - "
              << req.ShortDebugString();
      return;
    }
    LOG_WITH_FUNC(WARNING) << "Couldn't send full wait-for update for status tablet "
        << status_tablet_id_;
  }

  Status SendFullUpdateAsync(const HybridTime& now) {
    return thread_pool_token_->SubmitFunc(
        std::bind(&StatusTabletData::SendFullUpdate, shared_from(this), now));
  }

 private:
  rpc::Rpcs* const rpcs_;
  client::YBClient* client_;
  mutable rw_spinlock mutex_;
  const TabletId status_tablet_id_;
  std::vector<std::weak_ptr<const WaitingTransactionData>> waiters GUARDED_BY(mutex_);
  rpc::Rpcs::Handle rpc_handle_ GUARDED_BY(mutex_);
  const std::string tserver_uuid_;
  ThreadPoolToken* thread_pool_token_;
};

class LocalWaitingTxnRegistry::Impl {
 public:
  explicit Impl(
      const std::shared_future<client::YBClient*>& client_future, const server::ClockPtr& clock,
      const std::string& tserver_uuid, ThreadPool* thread_pool) :
        client_future_(client_future), clock_(clock), tserver_uuid_(tserver_uuid),
        thread_pool_(DCHECK_NOTNULL(thread_pool)) {}

  // A wrapper for WaitingTransactionData which determines the lifetime of the
  // LocalWaitingTxnRegistry's metadata associated with this waiter. When this wrapper is
  // destructed, the LocalWaitingTxnRegistry will cease reporting on this waiter.
  class WaitingTransactionDataWrapper : public ScopedWaitingTxnRegistration {
   public:
    explicit WaitingTransactionDataWrapper(LocalWaitingTxnRegistry::Impl* registry)
        : registry_(registry) {}

    Status Register(
        const TransactionId& waiting,
        std::shared_ptr<ConflictDataManager> blockers,
        const TabletId& status_tablet) override {
      return registry_->RegisterWaitingFor(waiting, std::move(blockers), status_tablet, this);
    }

    void SetData(std::shared_ptr<WaitingTransactionData>&& blocked_data) {
      blocked_data_ = std::move(blocked_data);
    }

    int64 GetDataUseCount() const override {
      return blocked_data_.use_count();
    }

   private:
    LocalWaitingTxnRegistry::Impl* const registry_;
    std::shared_ptr<WaitingTransactionData> blocked_data_ = nullptr;
  };

  std::unique_ptr<WaitingTransactionDataWrapper> Create() {
    return std::make_unique<WaitingTransactionDataWrapper>(this);
  }

  void SendWaitForGraph() EXCLUDES(mutex_) {
    if (!FLAGS_enable_wait_queues || PREDICT_FALSE(FLAGS_disable_deadlock_detection)) {
      return;
    }

    UniqueLock<decltype(mutex_)> l(mutex_);
    if (shutting_down_) {
      YB_LOG_EVERY_N_SECS(INFO, 1)
          << "Skipping LocalWaitingTxnRegistry::SendWaitForGraph. Shutting down.";
      return;
    }
    auto it = status_tablets_.begin();
    while (it != status_tablets_.end()) {
      if (auto data = it->second.status_tablet_data.lock()) {
        WARN_NOT_OK(
            data->SendFullUpdateAsync(clock_->Now()),
            Format("Failed to submit async full wait-for update for status tablet: $0",
                    it->first));
        ++it;
      } else {
        VLOG_WITH_FUNC(1) << "Erasing status tablet data for " << it->first << ".";
        it = status_tablets_.erase(it);
      }
    }
  }

  void StartShutdown() {
    UniqueLock<decltype(mutex_)> l(mutex_);
    shutting_down_ = true;
  }

  void CompleteShutdown() {
#ifndef NDEBUG
    SharedLock<decltype(mutex_)> l(mutex_);
    DCHECK(shutting_down_);
#endif
    rpcs_.Shutdown();
  }

 private:
  std::shared_ptr<StatusTabletData> NewStatusTabletData(const TabletId& status_tablet_id,
                                                        ThreadPoolToken* thread_pool_token) {
    return std::make_shared<StatusTabletData>(
        &rpcs_, &client(), status_tablet_id, tserver_uuid_, thread_pool_token);
  }

  Result<std::shared_ptr<StatusTabletData>> GetOrAdd(const TabletId& status_tablet_id) {
    UniqueLock<decltype(mutex_)> l(mutex_);

    if (shutting_down_) {
      return STATUS(ShutdownInProgress, "Tablet server is shutting down.");
    }

    auto it = status_tablets_.find(status_tablet_id);
    if (it != status_tablets_.end()) {
      if (auto existing_data = it->second.status_tablet_data.lock()) {
        VLOG_WITH_FUNC(4) << "Re-using existing status tablet data " << status_tablet_id;
        return existing_data;
      }
      VLOG_WITH_FUNC(4) << "Overwriting existing status tablet data " << status_tablet_id;

      // Note: Resetting it->second would lead to destruction of the thread_pool_token in
      // StatusTabletEntry, which would lead to a deadlock when this function is called from
      // wait-queue threadpool. Refer StatusTabletEntry structure for details.
      auto* thread_pool_token = it->second.thread_pool_token.get();
      auto new_data = NewStatusTabletData(status_tablet_id, thread_pool_token);
      it->second.status_tablet_data = new_data;
      return new_data;
    }

    VLOG_WITH_FUNC(4) << "Inserting new status tablet data " << status_tablet_id;
    auto thread_pool_token = thread_pool_->NewToken(ThreadPool::ExecutionMode::SERIAL);
    auto new_data = NewStatusTabletData(status_tablet_id, thread_pool_token.get());
    auto status_tablet_entry = StatusTabletEntry {
      .status_tablet_data = new_data,
      .thread_pool_token = std::move(thread_pool_token),
    };
    auto did_insert =
      status_tablets_.emplace(status_tablet_id, std::move(status_tablet_entry)).second;
    DCHECK(did_insert);
    return new_data;
  }

  Status RegisterWaitingFor(
      const TransactionId& waiting, std::shared_ptr<ConflictDataManager> blockers,
      const TabletId& status_tablet_id, WaitingTransactionDataWrapper* wrapper) EXCLUDES(mutex_) {
    DCHECK(!status_tablet_id.empty());
    auto shared_tablet_data = VERIFY_RESULT(GetOrAdd(status_tablet_id));

    auto blocked_data = std::make_shared<WaitingTransactionData>(WaitingTransactionData {
      .id = waiting,
      .blockers = std::move(blockers),
      .status_tablet_data = shared_tablet_data,
      .wait_start_time = clock_->Now(),
      .rpc_handle = rpcs_.InvalidHandle(),
    });

    // Waiting txn data needs to be attached before submitting a task of type SendPartialUpdateAsync
    // (partial update) to the queue. If not, this record could be erased at the deadlock detector
    // by a full update that is enqueued after this partial update, but is processed before the
    // updated waiter data is attached to the tablet. Doing it this way ensures that a full update
    // includes data from all partial updates that it follows.
    shared_tablet_data->AddWaitingTransactionData(blocked_data);

    // Immediately trigger an update for this status tablet.
    // TODO(wait-queues): We probably don't need to send this immediately, vs. allowing this waiter
    // to be sent with the next full update. This also aligns with postgres' approach of only
    // starting deadlock detection after 1s.
    // See: https://github.com/yugabyte/yugabyte-db/issues/13576
    RETURN_NOT_OK(shared_tablet_data->SendPartialUpdateAsync(blocked_data, clock_->Now()));

    wrapper->SetData(std::move(blocked_data));
    return Status::OK();
  }

  client::YBClient& client() { return *client_future_.get(); }

  const std::shared_future<client::YBClient*>& client_future_;

  const server::ClockPtr clock_;

  rpc::Rpcs rpcs_;

  mutable rw_spinlock mutex_;

  std::unordered_map<TabletId, StatusTabletEntry> status_tablets_ GUARDED_BY(mutex_);

  bool shutting_down_ GUARDED_BY(mutex_) = false;

  const std::string tserver_uuid_;

  ThreadPool* thread_pool_;
};

LocalWaitingTxnRegistry::LocalWaitingTxnRegistry(
    const std::shared_future<client::YBClient*>& client_future, const server::ClockPtr& clock,
    const std::string& tserver_uuid, ThreadPool* thread_pool) :
      impl_(new Impl(client_future, clock, tserver_uuid, thread_pool)) {}

LocalWaitingTxnRegistry::~LocalWaitingTxnRegistry() {}

std::unique_ptr<ScopedWaitingTxnRegistration> LocalWaitingTxnRegistry::Create() {
  return impl_->Create();
}

void LocalWaitingTxnRegistry::SendWaitForGraph() {
  impl_->SendWaitForGraph();
}

void LocalWaitingTxnRegistry::StartShutdown() {
  return impl_->StartShutdown();
}

void LocalWaitingTxnRegistry::CompleteShutdown() {
  return impl_->CompleteShutdown();
}

} // namespace docdb
} // namespace yb
