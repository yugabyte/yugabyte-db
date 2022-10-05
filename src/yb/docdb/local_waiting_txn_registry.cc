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

#include <boost/optional/optional.hpp>
#include <glog/logging.h>
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
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_log.h"
#include "yb/util/unique_lock.h"
#include "yb/client/transaction_rpc.h"

using namespace std::placeholders;
using namespace std::literals;

DECLARE_bool(enable_deadlock_detection);

namespace yb {
namespace tablet {

class StatusTabletData;
using StatusTabletDataPtr = std::shared_ptr<StatusTabletData>;

// Container for metadata describing a waiting transaction that is being tracked and reported to the
// transaction coordinator. A shared_ptr is held to status_tablet_data to extend the lifetime of the
// StatusTabletData instance being tracked by the LocalWaitingTxnRegistry.
struct WaitingTransactionData {
  TransactionId id;
  std::vector<BlockingTransactionData> blockers;
  StatusTabletDataPtr status_tablet_data;
  HybridTime wait_start_time;
  rpc::Rpcs::Handle rpc_handle;
};

void AttachWaitingTransaction(
    const WaitingTransactionData& data, tserver::UpdateTransactionWaitingForStatusRequestPB* req) {
  auto* txn = req->add_waiting_transactions();
  txn->set_transaction_id(data.id.data(), data.id.size());
  txn->set_wait_start_time(data.wait_start_time.ToUint64());
  for (const auto& blocker : data.blockers) {
    auto* blocking_txn = txn->add_blocking_transaction();
    blocking_txn->set_transaction_id(blocker.id.data(), blocker.id.size());
    blocking_txn->set_status_tablet_id(blocker.status_tablet);
  }
}

// Container for metadata corresponding to a status tablet to which we are reporting waiting
// transactions. WaitingTransactionData data indicates which waiters to report to this status tablet
// and is weakly held -- WaitingTransactionData instances are kept alive by clients of the
// LocalWaitingTxnRegistry which returns a wrapped WaitingTransactionData to clients to keep alive.
class StatusTabletData {
 public:
  explicit StatusTabletData(rpc::Rpcs* rpcs, client::YBClient* client):
      rpcs_(rpcs), client_(client), rpc_handle_(rpcs_->InvalidHandle()) {}

  void AddWaitingTransactionData(const std::shared_ptr<WaitingTransactionData>& waiter) {
    UniqueLock<decltype(mutex_)> tablet_lock(mutex_);
    waiters.emplace_back(waiter);
  }

  Status SendFullUpdate(const TabletId& status_tablet_id, HybridTime now) {
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

    if (req.waiting_transactions_size() > 0) {
      DCHECK(!has_pending_request)
          << "req should only have waiting transactions if there is no pending request.";
      req.set_tablet_id(status_tablet_id);
      req.set_propagated_hybrid_time(now.ToUint64());
      auto did_send = rpcs_->RegisterAndStart(
          client::UpdateTransactionWaitingForStatus(
            TransactionRpcDeadline(),
            nullptr /* tablet */,
            client_,
            &req,
            [this](const auto& status, const auto& resp) {
              UniqueLock<decltype(mutex_)> l(mutex_);
              rpcs_->Unregister(&rpc_handle_);
            }),
          &rpc_handle_);
      if (did_send) {
        VLOG(1) << "Sent UpdateTransactionWaitingForStatusRequestPB - "
                << req.ShortDebugString();
        return Status::OK();
      }
      return STATUS(InternalError, "Failed to register waiter with transaction coordinator");
    }

    VLOG(4)
        << "Not sending UpdateTransactionWaitingForStatusRequestPB for"
        << " status_tablet: " << status_tablet_id
        << " waiting_transactions_size: " << req.waiting_transactions_size();
    return Status::OK();
  }

 private:
  rpc::Rpcs* const rpcs_;
  client::YBClient* client_;
  mutable rw_spinlock mutex_;
  std::vector<std::weak_ptr<const WaitingTransactionData>> waiters GUARDED_BY(mutex_);
  rpc::Rpcs::Handle rpc_handle_ GUARDED_BY(mutex_);
};

class LocalWaitingTxnRegistry::Impl {
 public:
  explicit Impl(
      const std::shared_future<client::YBClient*>& client_future, const server::ClockPtr& clock)
      : client_future_(client_future), clock_(clock) {}

  // A wrapper for WaitingTransactionData which determines the lifetime of the
  // LocalWaitingTxnRegistry's metadata associated with this waiter. When this wrapper is
  // destructed, the LocalWaitingTxnRegistry will cease reporting on this waiter.
  class WaitingTransactionDataWrapper : public docdb::ScopedWaitingTxnRegistration {
   public:
    explicit WaitingTransactionDataWrapper(LocalWaitingTxnRegistry::Impl* registry)
        : registry_(registry) {}

    Status Register(
        const TransactionId& waiting,
        std::vector<BlockingTransactionData>&& blocking,
        const TabletId& status_tablet) override {
      return registry_->RegisterWaitingFor(waiting, std::move(blocking), status_tablet, this);
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

  void SendWaitForGraph() {
    if (!FLAGS_enable_deadlock_detection) {
      return;
    }

    std::unordered_map<TabletId, StatusTabletDataPtr> to_poll;
    {
      UniqueLock<decltype(mutex_)> l(mutex_);
      if (shutting_down_) {
        YB_LOG_EVERY_N_SECS(INFO, 1)
            << "Skipping LocalWaitingTxnRegistry::SendWaitForGraph. Shutting down.";
        return;
      }
      auto it = status_tablets_.begin();
      while (it != status_tablets_.end()) {
        if (auto data = it->second.lock()) {
          to_poll.emplace(it->first, data);
          ++it;
        } else {
          VLOG_WITH_FUNC(1) << "Erasing status tablet data for " << it->first << ".";
          it = status_tablets_.erase(it);
        }
      }
    }

    for (const auto& [status_tablet_id, data] : to_poll) {
      WARN_NOT_OK(
          data->SendFullUpdate(status_tablet_id, clock_->Now()),
          Format("Failed to send WaitFor poll to status tablet: $0", status_tablet_id));
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
  std::shared_ptr<StatusTabletData> NewStatusTabletData() {
    return std::make_shared<StatusTabletData>(&rpcs_, &client());
  }

  Result<std::shared_ptr<StatusTabletData>> GetOrAdd(const TabletId& status_tablet_id) {
    UniqueLock<decltype(mutex_)> l(mutex_);

    if (shutting_down_) {
      return STATUS(ShutdownInProgress, "Tablet server is shutting down.");
    }

    auto it = status_tablets_.find(status_tablet_id);
    if (it != status_tablets_.end()) {
      if (auto existing_data = it->second.lock()) {
        VLOG_WITH_FUNC(4) << "Re-using existing status tablet data " << status_tablet_id;
        return existing_data;
      }
      VLOG_WITH_FUNC(4) << "Overwriting existing status tablet data " << status_tablet_id;
      auto new_data = NewStatusTabletData();
      it->second = new_data;
      return new_data;
    }

    VLOG_WITH_FUNC(4) << "Inserting new status tablet data " << status_tablet_id;

    auto new_data = NewStatusTabletData();
    auto did_insert = status_tablets_.emplace(status_tablet_id, new_data).second;
    DCHECK(did_insert);
    return new_data;
  }

  Status RegisterWaitingFor(
      const TransactionId& waiting, std::vector<BlockingTransactionData>&& blocking,
      const TabletId& status_tablet_id, WaitingTransactionDataWrapper* wrapper) {
    DCHECK(!status_tablet_id.empty());
    auto shared_tablet_data = VERIFY_RESULT(GetOrAdd(status_tablet_id));

    auto blocked_data = std::make_shared<WaitingTransactionData>(WaitingTransactionData {
      .id = waiting,
      .blockers = std::move(blocking),
      .status_tablet_data = shared_tablet_data,
      .wait_start_time = clock_->Now(),
      .rpc_handle = rpcs_.InvalidHandle(),
    });

    // Immediately trigger an update for this status tablet.
    // TODO(pessimistic): We probably don't need to send this immediately, vs. allowing this waiter
    // to be sent with the next full update. This also aligns with postgres' approach of only
    // starting deadlock detection after 1s.
    // See: https://github.com/yugabyte/yugabyte-db/issues/13576
    RETURN_NOT_OK(SendUpdate(status_tablet_id, blocked_data));

    shared_tablet_data->AddWaitingTransactionData(blocked_data);
    wrapper->SetData(std::move(blocked_data));

    return Status::OK();
  }

  Status SendUpdate(
      const TabletId& status_tablet, const std::shared_ptr<WaitingTransactionData>& data) {
    tserver::UpdateTransactionWaitingForStatusRequestPB req;
    if (!FLAGS_enable_deadlock_detection) {
      return Status::OK();
    }

    AttachWaitingTransaction(*data, &req);
    req.set_tablet_id(status_tablet);
    req.set_propagated_hybrid_time(clock_->Now().ToUint64());
    auto s = rpcs_.RegisterAndStartStatus(
        client::UpdateTransactionWaitingForStatus(
          TransactionRpcDeadline(),
          nullptr /* tablet */,
          &client(),
          &req,
          [this, data](const auto& status, const auto& req) {
            rpcs_.Unregister(&data->rpc_handle);
          }),
        &data->rpc_handle);
    if (!s.ok()) {
      s = s.CloneAndAppend("Failed to register waiter with transaction coordinator");
    }
    return s;
  }

  client::YBClient& client() { return *client_future_.get(); }

  const std::shared_future<client::YBClient*>& client_future_;

  const server::ClockPtr clock_;

  rpc::Rpcs rpcs_;

  mutable rw_spinlock mutex_;

  std::unordered_map<TabletId, std::weak_ptr<StatusTabletData>> status_tablets_ GUARDED_BY(mutex_);

  bool shutting_down_ GUARDED_BY(mutex_) = false;
};

LocalWaitingTxnRegistry::LocalWaitingTxnRegistry(
    const std::shared_future<client::YBClient*>& client_future, const server::ClockPtr& clock):
    impl_(new Impl(client_future, clock)) {}

LocalWaitingTxnRegistry::~LocalWaitingTxnRegistry() {}

std::unique_ptr<docdb::ScopedWaitingTxnRegistration> LocalWaitingTxnRegistry::Create() {
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

} // namespace tablet
} // namespace yb
