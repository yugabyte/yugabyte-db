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
#include "yb/client/yb_op.h"

using namespace std::placeholders;

DEFINE_uint64(transaction_heartbeat_usec, 500000, "Interval of transaction heartbeat in usec.");
DEFINE_bool(transaction_disable_heartbeat_in_tests, false, "Disable heartbeat during test.");
DEFINE_uint64(max_clock_skew_usec, 50000,
              "Transaction read clock skew in usec. Is maximum allowed time delta between servers "
              "of a single cluster.");

namespace yb {
namespace client {

class YBTransaction::Impl final {
 public:
  Impl(TransactionManager* manager, YBTransaction* transaction, IsolationLevel isolation)
      : manager_(manager),
        transaction_(transaction),
        metadata_{GenerateTransactionId(),
                  isolation,
                  TabletId(),
                  RandomUniformInt<uint64_t>(),
                  manager->Now()},
        log_prefix_(Format("$0: ", to_string(metadata_.transaction_id))),
        heartbeat_handle_(manager->rpcs().InvalidHandle()),
        commit_handle_(manager->rpcs().InvalidHandle()),
        abort_handle_(manager->rpcs().InvalidHandle()) {
    VLOG_WITH_PREFIX(1) << "Started, metadata: " << metadata_;
    read_time_.read = metadata_.start_time;
    read_time_.local_limit = read_time_.read.AddMicroseconds(FLAGS_max_clock_skew_usec);
    read_time_.global_limit = read_time_.local_limit;
    restart_read_time_ = read_time_.read;
  }

  ~Impl() {
    manager_->rpcs().Abort({&heartbeat_handle_, &commit_handle_, &abort_handle_});
  }

  YBTransactionPtr CreateSimilarTransaction() {
    return std::make_shared<YBTransaction>(manager_, metadata_.isolation);
  }

  // This transaction is a restarted transaction, so we set it up with data from original one.
  void SetupRestart(Impl* other) {
    VLOG_WITH_PREFIX(1) << "Setup from " << other->ToString();
    std::lock_guard<std::mutex> lock(mutex_);
    DCHECK(!restarts_.empty());
    other->local_limits_.swap(restarts_);
    other->read_time_ = read_time_;
    other->read_time_.read = restart_read_time_;
    complete_.store(true, std::memory_order_release);
  }

  bool Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
               Waiter waiter,
               TransactionPrepareData* prepare_data) {
    CHECK_NOTNULL(prepare_data);

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

    prepare_data->propagated_ht = manager_->Now();
    if (has_tablets_without_parameters) {
      prepare_data->metadata = metadata_;
    } else {
      prepare_data->metadata.transaction_id = metadata_.transaction_id;
    }
    if (metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      VLOG_WITH_PREFIX(1) << "Set read time: " << read_time_;
      prepare_data->read_time = read_time_;
      prepare_data->local_limits = &local_limits_;
    }
    return true;
  }

  void Flushed(
      const internal::InFlightOps& ops, const Status& status, HybridTime propagated_hybrid_time) {
    if (status.ok()) {
      manager_->UpdateClock(propagated_hybrid_time);
      std::lock_guard<std::mutex> lock(mutex_);
      TabletStates::iterator it = tablets_.end();
      for (const auto& op : ops) {
        if (op->yb_op->succeeded()) {
          const std::string& tablet_id = op->tablet->tablet_id();
          // Usually all ops belong to the same tablet. So we can avoid repeating lookup.
          if (it == tablets_.end() || it->first != tablet_id) {
            auto it = tablets_.find(tablet_id);
            CHECK(it != tablets_.end());
            it->second.has_parameters = true;
          }
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
          status = STATUS(IllegalState, "Transaction already completed");
        }
        callback(status);
        return;
      }
      complete_.store(true, std::memory_order_release);
      commit_callback_ = std::move(callback);
      if (!ready_) {
        RequestStatusTablet();
        waiters_.emplace_back(std::bind(&Impl::DoCommit, this, _1, transaction));
        return;
      }
    }
    DoCommit(Status::OK(), transaction);
  }

  void Abort() {
    auto transaction = transaction_->shared_from_this();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (complete_.load(std::memory_order_acquire)) {
        LOG(DFATAL) << "Abort of committed transaction";
        return;
      }
      complete_.store(true, std::memory_order_release);
      if (!ready_) {
        RequestStatusTablet();
        waiters_.emplace_back(std::bind(&Impl::DoAbort, this, _1, transaction));
        return;
      }
    }
    DoAbort(Status::OK(), transaction);
  }

  void RestartRequired(const TabletId& tablet, const ReadHybridTime& restart_time) {
    std::unique_lock<std::mutex> lock(mutex_);
    restart_read_time_.MakeAtLeast(restart_time.read);
    // We should inherit per-tablet restart time limits from original transaction, doing it lazily.
    if (restarts_.empty()) {
      restarts_ = local_limits_;
    }
    auto emplace_result = restarts_.emplace(tablet, restart_time.local_limit);
    bool inserted = emplace_result.second;
    if (!inserted) {
      auto& existing_local_limit = emplace_result.first->second;
      existing_local_limit = std::min(existing_local_limit, restart_time.local_limit);
    }
  }

  std::shared_future<TransactionMetadata> TEST_GetMetadata() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metadata_future_.valid()) {
      return metadata_future_;
    }
    metadata_future_ = std::shared_future<TransactionMetadata>(metadata_promise_.get_future());
    if (!ready_) {
      RequestStatusTablet();
      auto transaction = transaction_->shared_from_this();
      waiters_.push_back([this, transaction](const Status& status) {
        // OK to crash here, because we are in test
        CHECK_OK(status);
        metadata_promise_.set_value(metadata_);
      });
    }
    metadata_promise_.set_value(metadata_);
    return metadata_future_;
  }

  const std::string& LogPrefix() {
    return log_prefix_;
  }

  std::string ToString() const {
    return Format("Transaction: $0", metadata_.transaction_id);
  }

  const TransactionId& id() {
    return metadata_.transaction_id;
  }

 private:
  void DoCommit(const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << Format("Commit, tablets: $0, status: $1", tablets_, status);

    if (!status.ok()) {
      commit_callback_(status);
      return;
    }

    // tablets_.empty() means that transaction does not have writes, so just abort it.
    // But notify caller that commit was successful, so it is transparent for him.
    if (tablets_.empty()) {
      DoAbort(Status::OK(), transaction);
      commit_callback_(Status::OK());
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    state.set_status(TransactionStatus::COMMITTED);
    for (const auto& tablet : tablets_) {
      state.add_tablets(tablet.first);
    }

    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            TransactionRpcDeadline(),
            status_tablet_.get(),
            manager_->client().get(),
            &req,
            std::bind(&Impl::CommitDone, this, _1, _2, transaction)),
        &commit_handle_);
  }

  void DoAbort(const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << Format("Abort, status: $1", status);

    if (!status.ok()) {
      // We already stopped to send heartbeats, so transaction would be aborted anyway.
      LOG(WARNING) << "Failed to abort transaction: " << status;
      return;
    }

    tserver::AbortTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    req.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());

    manager_->rpcs().RegisterAndStart(
        AbortTransaction(
            TransactionRpcDeadline(),
            status_tablet_.get(),
            manager_->client().get(),
            &req,
            std::bind(&Impl::AbortDone, this, _1, _2, transaction)),
        &abort_handle_);
  }

  void CommitDone(const Status& status,
                  HybridTime propagated_hybrid_time,
                  const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Committed: " << status;

    manager_->UpdateClock(propagated_hybrid_time);
    manager_->rpcs().Unregister(&commit_handle_);
    commit_callback_(status);
  }

  void AbortDone(const Status& status,
                 const tserver::AbortTransactionResponsePB& response,
                 const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Aborted: " << status;

    if (response.has_propagated_hybrid_time()) {
      manager_->UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }
    manager_->rpcs().Unregister(&abort_handle_);
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
      manager_->client()->LookupTabletById(
          *tablet,
          TransactionRpcDeadline(),
          &status_tablet_holder_,
          Bind(&Impl::LookupTabletDone, Unretained(this), transaction));
    } else {
      SetError(tablet.status());
    }
  }

  void LookupTabletDone(const YBTransactionPtr& transaction, const Status& status) {
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << status;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet_ = std::move(status_tablet_holder_);
      metadata_.status_tablet = status_tablet_->tablet_id();
    }
    SendHeartbeat(TransactionStatus::CREATED, transaction_->shared_from_this());
  }

  void SendHeartbeat(TransactionStatus status,
                     std::weak_ptr<YBTransaction> weak_transaction) {
    auto transaction = weak_transaction.lock();
    if (!transaction || complete_.load(std::memory_order_acquire)) {
      return;
    }

    if (status != TransactionStatus::CREATED &&
        GetAtomicFlag(&FLAGS_transaction_disable_heartbeat_in_tests)) {
      HeartbeatDone(Status::OK(), HybridTime::kInvalid, status, transaction);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.begin(), metadata_.transaction_id.size());
    state.set_status(status);
    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            TransactionRpcDeadline(),
            status_tablet_.get(),
            manager_->client().get(),
            &req,
            std::bind(&Impl::HeartbeatDone, this, _1, _2, status, transaction)),
        &heartbeat_handle_);
  }

  void HeartbeatDone(const Status& status,
                     HybridTime propagated_hybrid_time,
                     TransactionStatus transaction_status,
                     const YBTransactionPtr& transaction) {
    manager_->UpdateClock(propagated_hybrid_time);
    manager_->rpcs().Unregister(&heartbeat_handle_);

    if (status.ok()) {
      if (transaction_status == TransactionStatus::CREATED) {
        std::vector<Waiter> waiters;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          DCHECK(!ready_);
          ready_ = true;
          waiters_.swap(waiters);
        }
        for (const auto& waiter : waiters) {
          waiter(Status::OK());
        }
      }
      std::weak_ptr<YBTransaction> weak_transaction(transaction);
      manager_->client()->messenger()->scheduler().Schedule(
          std::bind(&Impl::SendHeartbeat, this, TransactionStatus::PENDING, weak_transaction),
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

  TransactionMetadata metadata_;
  ReadHybridTime read_time_;

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
  rpc::Rpcs::Handle abort_handle_;

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
  std::promise<TransactionMetadata> metadata_promise_;
  std::shared_future<TransactionMetadata> metadata_future_;
  HybridTime restart_read_time_;
  // Local limits for separate tablets, does not change during lifetime of transaction.
  // Times such that anything happening at that hybrid time or later is definitely after the
  // original request arrived and therefore does not have to be shown in results.
  std::unordered_map<TabletId, HybridTime> local_limits_;
  // Restarts that happen during transaction lifetime. Used to initialize local_limits for
  // restarted transaction.
  std::unordered_map<TabletId, HybridTime> restarts_;
};

YBTransaction::YBTransaction(TransactionManager* manager,
                             IsolationLevel isolation)
    : impl_(new Impl(manager, this, isolation)) {
}

YBTransaction::~YBTransaction() {
}

bool YBTransaction::Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
                            Waiter waiter,
                            TransactionPrepareData* prepare_data) {
  return impl_->Prepare(ops, std::move(waiter), prepare_data);
}

void YBTransaction::Flushed(
    const internal::InFlightOps& ops, const Status& status, HybridTime propagated_hybrid_time) {
  impl_->Flushed(ops, status, propagated_hybrid_time);
}

void YBTransaction::Commit(CommitCallback callback) {
  impl_->Commit(std::move(callback));
}

const TransactionId& YBTransaction::id() const {
  return impl_->id();
}

std::future<Status> YBTransaction::CommitFuture() {
  return MakeFuture<Status>([this](auto callback) { impl_->Commit(callback); });
}

void YBTransaction::Abort() {
  impl_->Abort();
}

void YBTransaction::RestartRequired(const TabletId& tablet, const ReadHybridTime& restart_time) {
  impl_->RestartRequired(tablet, restart_time);
}

YBTransactionPtr YBTransaction::CreateRestartedTransaction() {
  auto result = impl_->CreateSimilarTransaction();
  impl_->SetupRestart(result->impl_.get());
  return result;
}

std::shared_future<TransactionMetadata> YBTransaction::TEST_GetMetadata() const {
  return impl_->TEST_GetMetadata();
}

} // namespace client
} // namespace yb
