//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/transaction.h"

#include <unordered_set>

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include "yb/rpc/rpc.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/random_util.h"

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

TransactionId GenerateId() {
  boost::uuids::basic_random_generator<std::mt19937_64> generator(&ThreadLocalRandom());
  return generator();
}

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
        id_(GenerateId()),
        log_prefix_(Format("$0: ", to_string(id_))) {
    VLOG_WITH_PREFIX(1) << "Started, isolation level: " << IsolationLevel_Name(isolation_);
  }

  bool Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
               Waiter waiter,
               TransactionMetadataPB* metadata) {
    VLOG_WITH_PREFIX(1) << "Prepare";

    bool has_tablets_without_parameters = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!status_tablet_) {
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
    }
  }

  void Commit(CommitCallback callback) {
    auto transaction = transaction_->shared_from_this();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (complete_) {
        LOG_WITH_PREFIX(DFATAL) << "Commit of already complete transaction";
        return;
      }
      complete_ = true;
      commit_callback_ = std::move(callback);
      if (tablets_.empty()) { // TODO(dtxn) abort empty transaction?
        commit_callback_(Status::OK());
        return;
      }
      if (!status_tablet_) {
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

    UpdateTransaction(TransactionDeadline(),
                      status_tablet_.get(),
                      manager_->client().get(),
                      &req,
                      std::bind(&Impl::CommitDone, this, _1, transaction));
  }

  void CommitDone(const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << "Committed: " << status.ToString();
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
      LOG(DFATAL) << "Failed to pick status tablet"; // TODO(dtxn) Handle errors.
    }
  }

  void LookupTabletDone(const YBTransactionPtr& transaction, const Status& status) {
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << status.ToString();

    std::vector<Waiter> waiters;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet_ = std::move(status_tablet_holder_);
      waiters_.swap(waiters);
    }
    for (const auto& waiter : waiters) {
      waiter(status);
    }
    SendHeartbeat(tserver::TransactionStatus::CREATE, transaction_->shared_from_this());
  }

  void SendHeartbeat(tserver::TransactionStatus status, const YBTransactionPtr& transaction) {
    if (complete_.load(std::memory_order_acquire)) {
      return;
    }

    if (status != tserver::TransactionStatus::CREATE &&
        FLAGS_transaction_disable_heartbeat_in_tests) {
      HeartbeatDone(Status::OK(), status, transaction);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    auto& state = *req.mutable_state();
    state.set_transaction_id(id_.begin(), id_.size());
    state.set_status(status);
    UpdateTransaction(TransactionDeadline(), status_tablet_.get(), manager_->client().get(), &req,
                      std::bind(&Impl::HeartbeatDone, this, _1, status, transaction));
  }

  void HeartbeatDone(const Status& status,
                     tserver::TransactionStatus transaction_status,
                     const YBTransactionPtr& transaction) {
    if (status.ok()) {
      manager_->scheduler().Schedule(
          std::bind(&Impl::SendHeartbeat, this, tserver::TransactionStatus::PENDING, transaction),
          std::chrono::microseconds(FLAGS_transaction_heartbeat_usec));
    } else {
      LOG_WITH_PREFIX(WARNING) << "Send heartbeat failed: " << status;
      SendHeartbeat(transaction_status, transaction);
    }
  }

  // Manager is created once per service.
  TransactionManager* const manager_;

  // Transaction related to this impl.
  YBTransaction* const transaction_;

  const IsolationLevel isolation_;
  const TransactionId id_;
  const std::string log_prefix_;
  bool requested_status_tablet_ = false;
  internal::RemoteTabletPtr status_tablet_;
  internal::RemoteTabletPtr status_tablet_holder_;
  std::atomic<bool> complete_{false};
  CommitCallback commit_callback_;

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
