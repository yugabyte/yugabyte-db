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

#include "yb/client/transaction.h"

#include <unordered_set>
#include "yb/rpc/rpc.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"

#include "yb/common/transaction.h"

#include "yb/client/async_rpc.h"
#include "yb/client/client.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/tablet_rpc.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_rpc.h"

using namespace std::placeholders;

DEFINE_uint64(transaction_heartbeat_usec, 500000, "Interval of transaction heartbeat in usec.");
DEFINE_bool(transaction_disable_heartbeat_in_tests, false, "Disable heartbeat during test.");

namespace yb {
namespace client {

namespace {

// TODO(dtxn) correct deadline should be calculated and propagated.
MonoTime TransactionDeadline() {
  return MonoTime::FineNow() + MonoDelta::FromSeconds(5);
}

} // namespace

class YBTransaction::Impl final {
 public:
  Impl(TransactionManager* manager, YBTransaction* transaction, IsolationLevel isolation)
      : manager_(manager),
        transaction_(transaction),
        isolation_(isolation),
        priority_(RandomUniformInt<uint64_t>()),
        id_(Uuid::Generate()),
        log_prefix_(Format("$0: ", to_string(id_))),
        heartbeat_handle_(manager->rpcs().InvalidHandle()),
        commit_handle_(manager->rpcs().InvalidHandle()) {
    VLOG_WITH_PREFIX(1) << "Started, isolation level: " << IsolationLevel_Name(isolation_);
  }

  ~Impl() {
    manager_->rpcs().Abort({&heartbeat_handle_, &commit_handle_});
  }

  bool Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
               Waiter waiter,
               TransactionMetadataPB* metadata) {
    VLOG_WITH_PREFIX(1) << "Prepare";

    bool has_tablets_without_parameters = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!ready_) {
        RequestStatusTablet();
        waiters_.push_back(std::move(waiter));
        VLOG_WITH_PREFIX(1) << "Prepare, rejected";
        return false;
      }

      for (const auto& op : ops) {
        VLOG_WITH_PREFIX(1) << "Prepare, op: " << op->ToString();
        DCHECK(op->tablet != nullptr);
        auto it = tablets_.find(op->tablet->tablet_id());
        if (it == tablets_.end()) {
          tablets_.emplace(op->tablet->tablet_id(), TabletState());
          has_tablets_without_parameters = true;
        } else if (!has_tablets_without_parameters) {
          has_tablets_without_parameters = !it->second.has_parameters;
        }
      }
    }

    metadata->set_transaction_id(id_.begin(), id_.size());
    if (has_tablets_without_parameters) {
      metadata->set_isolation(isolation_);
      metadata->set_priority(priority_);
      metadata->set_status_tablet(status_tablet_->tablet_id());
    }
    return true;
  }

  void Flushed(const internal::InFlightOps& ops, const Status& status) {
    if (status.ok()) {
      std::lock_guard<std::mutex> lock(mutex_);
      TabletStates::iterator it = tablets_.end();
      for (const auto& op : ops) {
        const std::string& tablet_id = op->tablet->tablet_id();
        // Usually all ops belong to the same tablet. So we can avoid repeating lookup.
        if (it == tablets_.end() || it->first != tablet_id) {
          auto it = tablets_.find(tablet_id);
          CHECK(it != tablets_.end());
          it->second.has_parameters = true;
        }
      }
    } else if (status.IsTryAgain()) {
      SetError(status);
    }
    // We should not handle other errors, because it is just notification that batch was failed.
    // And they are handled during processing of that batch.
  }

  void Commit(CommitCallback callback) {
    auto transaction = transaction_->shared_from_this();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (complete_.load(std::memory_order_acquire)) {
        auto status = error_;
        lock.unlock();
        if (status.ok()) {
          status = STATUS(IllegalState, "Transaction already committed");
        }
        callback(status);
        return;
      }
      complete_.store(true, std::memory_order_release);
      commit_callback_ = std::move(callback);
      if (tablets_.empty()) { // TODO(dtxn) abort empty transaction?
        commit_callback_(Status::OK());
        return;
      }
      if (!ready_) {
        RequestStatusTablet();
        waiters_.emplace_back(std::bind(&Impl::DoCommit, this, _1, transaction));
        return;
      }
    }
    DoCommit(Status::OK(), transaction);
  }

  const std::string& LogPrefix() {
    return log_prefix_;
  }

  std::string ToString() const {
    return Format("Transaction: $0", id_);
  }

 private:
  void DoCommit(const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << Format("Commit, tablets: $0, status: $1", tablets_, status);
    if (!status.ok()) {
      commit_callback_(status);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    auto& state = *req.mutable_state();
    state.set_transaction_id(id_.begin(), id_.size());
    state.set_status(tserver::TransactionStatus::COMMITTED);
    for (const auto& tablet : tablets_) {
      state.add_tablets(tablet.first);
    }

    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            TransactionDeadline(),
            status_tablet_.get(),
            manager_->client().get(),
            &req,
            std::bind(&Impl::CommitDone, this, _1, transaction)),
        &commit_handle_);
  }

  void CommitDone(const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Committed: " << status.ToString();

    manager_->rpcs().Unregister(&commit_handle_);
    commit_callback_(status);
  }

  void RequestStatusTablet() {
    if (requested_status_tablet_) {
      return;
    }
    requested_status_tablet_ = true;
    manager_->PickStatusTablet(
        std::bind(&Impl::StatusTabletPicked, this, _1, transaction_->shared_from_this()));
  }

  void StatusTabletPicked(const Result<std::string>& tablet,
                          const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Picked status tablet: " << tablet;

    if (tablet.ok()) {
      auto deadline = TransactionDeadline();
      manager_->client()->LookupTabletById(
          *tablet,
          deadline,
          &status_tablet_holder_,
          Bind(&Impl::LookupTabletDone, Unretained(this), transaction));
    } else {
      SetError(tablet.status());
    }
  }

  void LookupTabletDone(const YBTransactionPtr& transaction, const Status& status) {
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << status.ToString();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet_ = std::move(status_tablet_holder_);
    }
    SendHeartbeat(tserver::TransactionStatus::CREATED, transaction_->shared_from_this());
  }

  void SendHeartbeat(tserver::TransactionStatus status, const YBTransactionPtr& transaction) {
    if (complete_.load(std::memory_order_acquire)) {
      return;
    }

    if (status != tserver::TransactionStatus::CREATED &&
        FLAGS_transaction_disable_heartbeat_in_tests) {
      HeartbeatDone(Status::OK(), status, transaction);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    auto& state = *req.mutable_state();
    state.set_transaction_id(id_.begin(), id_.size());
    state.set_status(status);
    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            TransactionDeadline(),
            status_tablet_.get(),
            manager_->client().get(),
            &req,
            std::bind(&Impl::HeartbeatDone, this, _1, status, transaction)),
        &heartbeat_handle_);
  }

  void HeartbeatDone(const Status& status,
                     tserver::TransactionStatus transaction_status,
                     const YBTransactionPtr& transaction) {
    manager_->rpcs().Unregister(&heartbeat_handle_);

    if (status.ok()) {
      if (transaction_status == tserver::TransactionStatus::CREATED) {
        std::vector<Waiter> waiters;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          DCHECK(!ready_);
          ready_ = true;
          waiters_.swap(waiters);
        }
        for (const auto& waiter : waiters) {
          waiter(status);
        }
      }
      manager_->client()->messenger()->scheduler().Schedule(
          std::bind(&Impl::SendHeartbeat, this, tserver::TransactionStatus::PENDING, transaction),
          std::chrono::microseconds(FLAGS_transaction_heartbeat_usec));
    } else {
      LOG_WITH_PREFIX(WARNING) << "Send heartbeat failed: " << status;
      if (status.IsExpired()) {
        SetError(status);
        return;
      }
      // Other errors could have different causes, but we should just retry sending heartbeat
      // in this case.
      SendHeartbeat(transaction_status, transaction);
    }
  }

  void SetError(const Status& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (error_.ok()) {
      error_ = status;
      complete_.store(true, std::memory_order_release);
    }
  }

  // Manager is created once per service.
  TransactionManager* const manager_;

  // Transaction related to this impl.
  YBTransaction* const transaction_;

  const IsolationLevel isolation_;
  const uint64_t priority_;
  const TransactionId id_;
  const std::string log_prefix_;
  bool requested_status_tablet_ = false;
  internal::RemoteTabletPtr status_tablet_;
  internal::RemoteTabletPtr status_tablet_holder_;
  std::atomic<bool> complete_{false};
  // Transaction is successfully initialized and ready to process intents.
  bool ready_ = false;
  CommitCallback commit_callback_;
  Status error_;
  rpc::Rpcs::Handle heartbeat_handle_;
  rpc::Rpcs::Handle commit_handle_;

  struct TabletState {
    bool has_parameters = false;

    std::string ToString() const {
      return Format("{ has_parameters $0 }", has_parameters);
    }
  };

  typedef std::unordered_map<std::string, TabletState> TabletStates;

  std::mutex mutex_;
  TabletStates tablets_;
  std::vector<Waiter> waiters_;
};

YBTransaction::YBTransaction(TransactionManager* manager,
                             IsolationLevel isolation)
    : impl_(new Impl(manager, this, isolation)) {
}

YBTransaction::~YBTransaction() {
}

bool YBTransaction::Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
                            Waiter waiter,
                            TransactionMetadataPB* metadata) {
  return impl_->Prepare(ops, std::move(waiter), metadata);
}

void YBTransaction::Flushed(const internal::InFlightOps& ops, const Status& status) {
  impl_->Flushed(ops, status);
}

void YBTransaction::Commit(CommitCallback callback) {
  impl_->Commit(std::move(callback));
}

} // namespace client
} // namespace yb
