//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/transaction.h"

#include <unordered_set>

#include <boost/uuid/random_generator.hpp>

#include "yb/rpc/rpc.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/random_util.h"

#include "yb/client/async_rpc.h"
#include "yb/client/client.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/tablet_rpc.h"
#include "yb/client/transaction_manager.h"

using namespace std::placeholders;

namespace yb {
namespace client {

namespace {

using TransactionId = boost::uuids::uuid;

TransactionId GenerateId() {
  boost::uuids::basic_random_generator<std::mt19937_64> generator(&ThreadLocalRandom());
  return generator();
}

// TransactionUpdater is used by UpdateTransactionRpc to fill request with transaction data,
// and handle response.
class TransactionUpdater {
 public:
  virtual std::string ToString() const = 0;
  virtual void UpdateDone(const Status& status) = 0;
  virtual void PrepareRequest(tserver::UpdateTransactionRequestPB* req) = 0;
 protected:
  ~TransactionUpdater() {}
};

// TODO(dtxn) correct deadline should be calculated and propagated.
MonoTime TransactionDeadline() {
  return MonoTime::FineNow() + MonoDelta::FromSeconds(5);
}

// UpdateTransactionRpc is used to call UpdateTransaction remote method of appropriate tablet.
class UpdateTransactionRpc : public rpc::Rpc, public internal::TabletRpc {
 public:
  UpdateTransactionRpc(YBClient* client,
                       internal::RemoteTablet* tablet,
                       TransactionUpdater* updater)
      : rpc::Rpc(TransactionDeadline(), client->messenger()),
        updater_(updater),
        trace_(new Trace),
        invoker_(client, this, this, tablet, mutable_retrier(), trace_.get()) {
  }

  void SendRpc() override {
    invoker_.Execute();
  }

  const tserver::TabletServerErrorPB* response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

  virtual ~UpdateTransactionRpc() {}

 private:
  std::string ToString() const override {
    return Format("Commit: $0", updater_->ToString());
  }

  void SendRpcCb(const Status& status) override {
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      updater_->UpdateDone(new_status);
      delete this;
    }
  }

  void Failed(const Status& status) override {}

  void SendRpcToTserver() override {
    updater_->PrepareRequest(&req_);
    invoker_.proxy()->UpdateTransactionAsync(req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&UpdateTransactionRpc::SendRpcCb, this, Status::OK()));
  }

  TransactionUpdater* updater_;
  TracePtr trace_;
  internal::TabletInvoker invoker_;
  tserver::UpdateTransactionRequestPB req_;
  tserver::UpdateTransactionResponsePB resp_;
};

// TODO(dtxn) Temporary class, that should be deleted in upcoming diffs.
class TempTransactionUpdater final : public TransactionUpdater {
 public:
  explicit TempTransactionUpdater(const internal::RemoteTabletPtr& tablet,
                                  TransactionUpdater* updater)
      : tablet_(tablet), updater_(updater) {}

  std::string ToString() const {
    return Format("TempTransactionUpdater: $0", tablet_->tablet_id());
  }

  void UpdateDone(const Status& status) {
    updater_->UpdateDone(status);
    delete this;
  }

  void PrepareRequest(tserver::UpdateTransactionRequestPB* req) {
    updater_->PrepareRequest(req);
    req->set_tablet_id(tablet_->tablet_id());
  }

 private:
  internal::RemoteTabletPtr tablet_;
  TransactionUpdater* updater_;
};

} // namespace

class YBTransaction::Impl final : public TransactionUpdater {
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
        tablets_.insert(op->tablet->tablet_id());
      }
    }

    metadata->set_transaction_id(id_.begin(), id_.size());
    metadata->set_isolation(isolation_);
    return true;
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

  void DoCommit(const Status& status, const YBTransactionPtr& transaction) {
    VLOG_WITH_PREFIX(1) << Format("Commit, tablets: $0, status: $1", tablets_, status);
    if (!status.ok()) {
      commit_callback_(status);
      return;
    }

    // TODO(dtxn) Add to raft queue of status tablet.
    // auto* rpc = new UpdateTransactionRpc(manager_->client().get(), status_tablet_.get(), this);
    // rpc->SendRpc();

    auto deadline = TransactionDeadline();
    pending_updates_.store(tablets_.size(), std::memory_order_release);
    retained_transaction_ = transaction_->shared_from_this();
    for (const auto& tablet : tablets_) {
      auto holder = std::make_shared<internal::RemoteTabletPtr>();
      manager_->client()->LookupTabletById(
          tablet,
          deadline,
          holder.get(),
          Bind(&Impl::LookupInvolvedTabletDone,
               Unretained(this),
               holder,
               transaction_->shared_from_this()));
    }
  }

  void LookupInvolvedTabletDone(const std::shared_ptr<internal::RemoteTabletPtr>& holder,
                                const YBTransactionPtr& transaction,
                                const Status& status) {
    internal::RemoteTabletPtr remote_tablet(*holder);
    CHECK_OK(status); // TODO(dtxn) Handle errors.

    auto* rpc = new UpdateTransactionRpc(manager_->client().get(),
                                         remote_tablet.get(),
                                         new TempTransactionUpdater(remote_tablet, this));
    rpc->SendRpc();
  }

  const std::string& LogPrefix() {
    return log_prefix_;
  }

  std::string ToString() const {
    return Format("Transaction: $0", id_);
  }

 private:
  void UpdateDone(const Status& status) {
    CHECK_OK(status); // TODO(dtxn) Handle errors.

    if (pending_updates_.fetch_sub(1, std::memory_order_acquire) == 1) {
      retained_transaction_.reset();
      VLOG_WITH_PREFIX(1) << "Updated";
      commit_callback_(Status::OK());
    }
  }

  void PrepareRequest(tserver::UpdateTransactionRequestPB* req) {
    req->set_tablet_id(status_tablet_->tablet_id());
    auto& state = *req->mutable_state();
    state.set_transaction_id(id_.begin(), id_.size());
    state.set_status(tserver::COMMITTED);
    for (const auto& tablet : tablets_) {
      state.add_involved_tablets(tablet);
    }
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
  bool complete_ = false;
  CommitCallback commit_callback_;

  // TODO(dtxn) temporary while we send multiple commits
  std::atomic<size_t> pending_updates_{0};

  // Prevent transaction from being destroyed during async calls
  YBTransactionPtr retained_transaction_;

  std::mutex mutex_;
  std::unordered_set<std::string> tablets_;
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

void YBTransaction::Commit(CommitCallback callback) {
  impl_->Commit(std::move(callback));
}

} // namespace client
} // namespace yb
