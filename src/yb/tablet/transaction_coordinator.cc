//
// Copyright (c) YugaByte, Inc.
//

#include "yb/tablet/transaction_coordinator.h"

#include "rocksdb/write_batch.h"

#include "yb/client/client.h"
#include "yb/client/transaction_rpc.h"

#include "yb/common/entity_ids.h"
#include "yb/common/transaction.h"

#include "yb/server/clock.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/enums.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"

namespace yb {
namespace tablet {

namespace {

class TransactionState {
 public:
  explicit TransactionState(const scoped_refptr<server::Clock>& clock) : clock_(clock) {}

  // Applies new state to transaction.
  // User of this class should guarantee that it does NOT invoke method concurrently.
  Result<bool> Process(const tserver::TransactionStatePB& state,
                       std::vector<TabletId>* to_notify) {
    LOG(INFO) << this << " Process: " << state.DebugString();
    switch (state.status()) {
      case tserver::TransactionStatus::ABORTED:
        return Aborted(state);
      case tserver::TransactionStatus::COMMITTED:
        return Committed(state, to_notify);
      case tserver::TransactionStatus::PENDING:
        return Pending(state);
      case tserver::TransactionStatus::APPLY:
        // APPLY is handled separately, because it is received for transactions not managed by
        // this tablet as a transaction status tablet, but tablets that are involved in the data
        // path (receive write intents) for this transactions
        FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, state.status());
      case tserver::TransactionStatus::APPLIED:
        return Applied(state);
      case tserver::TransactionStatus::COMMITTING:
        // COMMITTING is intermediate state, while we add record to log,
        // so it should not come via this code path.
        FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, state.status());
    }
    FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, state.status());
  }
 private:
  Result<bool> Aborted(const tserver::TransactionStatePB& state) {
    CHECK_EQ(status_, tserver::PENDING);
    return false;
  }

  Result<bool> Committed(const tserver::TransactionStatePB& state,
                         std::vector<TabletId>* to_notify) {
    CHECK_EQ(status_, tserver::PENDING);
    status_ = tserver::COMMITTED;
    unnotified_tablets_.insert(state.tablets().begin(), state.tablets().end());
    to_notify->assign(unnotified_tablets_.begin(), unnotified_tablets_.end());
    return unnotified_tablets_.empty();
  }

  Result<bool> Pending(const tserver::TransactionStatePB& state) {
    CHECK_EQ(status_, tserver::PENDING);
    last_heartbeat_ = clock_->Now();
    return false;
  }

  Result<bool> Applied(const tserver::TransactionStatePB& state) {
    CHECK_EQ(status_, tserver::COMMITTED);
    DCHECK_EQ(state.tablets_size(), 1);
    unnotified_tablets_.erase(state.tablets(0));
    return unnotified_tablets_.empty();
  }

  scoped_refptr<server::Clock> clock_;
  tserver::TransactionStatus status_ = tserver::PENDING;
  HybridTime last_heartbeat_;
  std::unordered_set<TabletId> unnotified_tablets_;
};

Result<TransactionId> MakeTransactionId(const std::string& input) {
  if (input.size() != TransactionId::static_size()) {
    return STATUS_FORMAT(Corruption,
                         "Invalid length of transaction id: $0",
                         Slice(input).ToDebugHexString());
  }
  TransactionId id;
  memcpy(id.data, input.data(), TransactionId::static_size());
  return id;
}

} // namespace

class TransactionCoordinator::Impl {
 public:
  explicit Impl(TransactionCoordinatorContext* context)
      : context_(context) {}

  size_t test_count_transactions() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    return managed_transactions_.size();
  }

  CHECKED_STATUS Process(ProcessMode mode,
                         const tserver::TransactionStatePB& state,
                         const consensus::OpId& op_id,
                         HybridTime hybrid_time) {
    auto id = MakeTransactionId(state.transaction_id());
    if (!id.ok()) {
      return std::move(id.status());
    }

    // APPLY is handled separately, because it is received for transactions not managed by
    // this tablet as a transaction status tablet, but tablets that are involved in the data
    // path (receive write intents) for this transactions
    if (state.status() == tserver::APPLY) {
      DCHECK_EQ(state.tablets_size(), 1);
      return ProcessApply(mode, *id, state.tablets(0), op_id, hybrid_time);
    }

    std::vector<TabletId> to_notify;
    Result<bool> result = false;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      auto* transaction = get(*id, state.status());
      if (!transaction) {
        return Status::OK();
      }
      result = transaction->Process(state, &to_notify);
      if (result.ok() && result.get()) {
        managed_transactions_.erase(*id);
      }
    }
    if (mode == ProcessMode::LEADER && !to_notify.empty()) {
      NotifyApply(*id, to_notify);
    }

    LOG(INFO) << "Processed: " << state.ShortDebugString();
    return result.ok() ? Status::OK() : result.status();
  }

  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch) {
    auto id = MakeTransactionId(data.transaction_id());
    if (!id.ok()) {
      LOG(DFATAL) << "Invalid transaction id: " << id.status().ToString();
      return;
    }
    bool store = false;
    {
      std::lock_guard<std::mutex> lock(running_mutex_);
      auto it = running_transactions_.find(*id);
      if (it == running_transactions_.end()) {
        running_transactions_.emplace(*id, data);
        store = true;
      } else {
        DCHECK_EQ(it->second.ShortDebugString(), data.ShortDebugString());
      }
    }
    if (store) {
      // TODO(dtxn) Correct key
      // TODO(dtxn) Load value if it is not loaded.
      auto key = '!' + data.transaction_id() + "!!";
      auto value = data.SerializeAsString();
      write_batch->Put(key, value);
    }
  }
 private:
  CHECKED_STATUS ProcessApply(ProcessMode mode,
                              const TransactionId& id,
                              const TabletId& status_tablet,
                              const consensus::OpId& op_id,
                              HybridTime hybrid_time) {
    CHECK_OK(context_->ApplyIntents(id, op_id, hybrid_time));

    {
      std::lock_guard<std::mutex> lock(running_mutex_);
      auto it = running_transactions_.find(id);
      if (it == running_transactions_.end()) {
        // This situation is normal and could be caused by 2 scenarios:
        // 1) Write batch failed, but originator doesn't know that.
        // 2) Failed to notify status tablet that we applied transaction.
        LOG(WARNING) << "Apply of unknown transaction: " << id;
        return Status::OK();
      } else {
        // TODO(dtxn) cleanup
      }
    }

    if (mode == ProcessMode::LEADER) {
      auto deadline = MonoTime::FineNow() + MonoDelta::FromSeconds(5); // TODO(dtxn)
      tserver::UpdateTransactionRequestPB req;
      req.set_tablet_id(status_tablet);
      auto& state = *req.mutable_state();
      state.set_transaction_id(id.begin(), id.size());
      state.set_status(tserver::APPLIED);
      state.add_tablets(context_->tablet_id());
      UpdateTransaction(
          deadline,
          nullptr /* remote_tablet */,
          context_->client().get(),
          &req,
          [](const Status& status) {
            LOG_IF(WARNING, !status.ok()) << "Failed to send applied: " << status.ToString();
          });
    }
    return Status::OK();
  }

  void NotifyApply(const TransactionId& id, const std::vector<TabletId>& to_notify) {
    auto deadline = MonoTime::FineNow() + MonoDelta::FromSeconds(5); // TODO(dtxn)
    for (const auto& tablet : to_notify) {
      tserver::UpdateTransactionRequestPB req;
      req.set_tablet_id(tablet);
      auto& state = *req.mutable_state();
      state.set_transaction_id(id.begin(), id.size());
      state.set_status(tserver::APPLY);
      state.add_tablets(context_->tablet_id());

      UpdateTransaction(
          deadline,
          nullptr /* remote_tablet */,
          context_->client().get(),
          &req,
          [](const Status& status) {
            LOG_IF(WARNING, !status.ok()) << "Failed to send apply: " << status.ToString();
          });
    }
  }

  TransactionState* get(const TransactionId& id, tserver::TransactionStatus status) {
    auto it = managed_transactions_.find(id);
    if (it == managed_transactions_.end()) {
      if (status == tserver::APPLIED) {
        return nullptr;
      }
      it = managed_transactions_.emplace(id, TransactionState(context_->clock())).first;
    }
    return &it->second;
  }

  TransactionCoordinatorContext* const context_;

  std::mutex managed_mutex_;
  std::unordered_map<TransactionId, TransactionState, TransactionIdHash> managed_transactions_;

  std::mutex running_mutex_;
  std::unordered_map<TransactionId, TransactionMetadataPB, TransactionIdHash> running_transactions_;
};

TransactionCoordinator::TransactionCoordinator(TransactionCoordinatorContext* context)
    : impl_(new Impl(context)) {
}

TransactionCoordinator::~TransactionCoordinator() {
}

Status TransactionCoordinator::Process(ProcessMode mode,
                                       const tserver::TransactionStatePB& state,
                                       const consensus::OpId& op_id,
                                       HybridTime hybrid_time) {
  return impl_->Process(mode, state, op_id, hybrid_time);
}

size_t TransactionCoordinator::test_count_transactions() const {
  return impl_->test_count_transactions();
}

void TransactionCoordinator::Add(const TransactionMetadataPB& data,
                                 rocksdb::WriteBatch *write_batch) {
  impl_->Add(data, write_batch);
}

} // namespace tablet
} // namespace yb
