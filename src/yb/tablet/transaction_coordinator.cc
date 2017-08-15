//
// Copyright (c) YugaByte, Inc.
//

#include "yb/tablet/transaction_coordinator.h"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>

#include <boost/uuid/uuid_io.hpp>

#include "rocksdb/write_batch.h"

#include "yb/client/client.h"
#include "yb/client/transaction_rpc.h"

#include "yb/common/entity_ids.h"
#include "yb/common/transaction.h"

#include "yb/consensus/opid_util.h"

#include "yb/server/clock.h"

#include "yb/tablet/transactions/update_txn_transaction.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/enums.h"
#include "yb/util/kernel_stack_watchdog.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"

DECLARE_uint64(transaction_heartbeat_usec);
DEFINE_uint64(transaction_timeout_usec, 1500000, "Transaction expiration timeout in usec.");

using namespace std::placeholders;

namespace yb {
namespace tablet {

namespace {

// Context for transaction state. I.e. access to external facilities required by
// transaction state to do its job.
class TransactionStateContext {
 public:
  virtual TransactionCoordinatorContext& coordinator_context() = 0;

  virtual void NotifyApplying(const TabletId& id, const TransactionId& transaction) = 0;
  virtual void SubmitUpdateTransaction(std::unique_ptr<UpdateTxnTransactionState> state) = 0;
 protected:
  ~TransactionStateContext() {}
};

// DTransactionState keeps state of single transaction.
// User of this class should guarantee that it does NOT invoke methods concurrently.
class DTransactionState { // TODO(dtxn) remove D prefix
 public:
  explicit DTransactionState(TransactionStateContext* context,
                             const TransactionId& id,
                             HybridTime last_touch)
      : context_(*context),
        id_(id),
        log_prefix_(Format("$0: ", to_string(id_))),
        last_touch_(last_touch) {}

  // Id of transaction.
  const TransactionId& id() const {
    return id_;
  }

  // Time when we last heard from transaction. I.e. hybrid time of replicated raft log entry
  // that updates status of this transaction.
  HybridTime last_touch() const {
    return last_touch_;
  }

  // Status of transaction.
  tserver::TransactionStatus status() const {
    return status_;
  }

  // RAFT index of first RAFT log entry required by this transaction.
  int64_t first_entry_raft_index() const {
    return first_entry_raft_index_;
  }

  // Returns debug string this representation of this class.
  std::string ToString() const {
    return Format("{ id: $0 last_touch: $1 status: $2 }",
                  to_string(id_), last_touch_, tserver::TransactionStatus_Name(status_));
  }

  // Whether this transaction expired at specified time.
  bool ExpiredAt(HybridTime now) const {
    if (ShouldBeCommitted()) {
      return false;
    }
    auto passed = now.GetPhysicalValueMicros() - last_touch_.GetPhysicalValueMicros();
    return passed > FLAGS_transaction_timeout_usec;
  }

  // Whether this transaction has completed.
  bool Completed() const {
    return status_ == tserver::TransactionStatus::ABORTED ||
           status_ == tserver::TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS;
  }

  // Applies new state to transaction.
  CHECKED_STATUS ProcessReplicated(const TransactionCoordinator::ReplicatedData& data) {
    if (data.mode == ProcessingMode::LEADER) {
      DCHECK_ONLY_NOTNULL(replicating_);
      DCHECK(consensus::OpIdEquals(replicating_->op_id(), data.op_id));
      replicating_ = nullptr;
    }

    auto status = DoProcessReplicated(data);

    if (data.mode == ProcessingMode::LEADER) {
      ProcessQueue();
    }

    return status;
  }

  // Clear all locks on this transaction.
  // Currently there is only one lock, but user of this function should not care about that.
  void ClearLocks() {
    replicating_ = nullptr;
    request_queue_.clear();
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnTransactionState> request) {
    VLOG(1) << "Handle: " << request->request()->ShortDebugString();
    if (request->request()->status() ==
        tserver::TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS) {
      auto status = AppliedInOneOfInvolvedTablets(*request->request());
      request->completion_callback()->set_error(status);
      request->completion_callback()->TransactionCompleted();
      return;
    }
    if (replicating_) {
      request_queue_.push_back(std::move(request));
      return;
    }
    DoHandle(std::move(request));
  }

  // Aborts this transaction.
  void Abort() {
    if (ShouldBeCommitted()) {
      LOG_WITH_PREFIX(DFATAL) << "Transaction abort in wrong state: " << status_;
      return;
    }
    if (ShouldBeAborted()) {
      return;
    }
    CHECK_EQ(status_, tserver::TransactionStatus::PENDING);
    SubmitUpdateStatus(tserver::TransactionStatus::ABORTED);
  }

  // Returns logs prefix for this transaction.
  const std::string& LogPrefix() {
    return log_prefix_;
  }

 private:
  // Checks whether we in specified status or going to be in this status when replication is
  // finished.
  bool ShouldBeInStatus(tserver::TransactionStatus status) const {
    return status_ == status ||
           (replicating_ && replicating_->request()->status() == status);
  }

  bool ShouldBeCommitted() const {
    return ShouldBeInStatus(tserver::TransactionStatus::COMMITTED);
  }

  bool ShouldBeAborted() const {
    return ShouldBeInStatus(tserver::TransactionStatus::ABORTED);
  }

  // Process operation that was replicated in RAFT.
  CHECKED_STATUS DoProcessReplicated(const TransactionCoordinator::ReplicatedData& data) {
    switch (data.state.status()) {
      case tserver::TransactionStatus::ABORTED:
        return AbortedReplicationFinished(data);
      case tserver::TransactionStatus::COMMITTED:
        return CommittedReplicationFinished(data);
      case tserver::TransactionStatus::CREATE: FALLTHROUGH_INTENDED;
      case tserver::TransactionStatus::PENDING:
        return PendingReplicationFinished(data);
      case tserver::TransactionStatus::APPLYING:
        // APPLYING is handled separately, because it is received for transactions not managed by
        // this tablet as a transaction status tablet, but tablets that are involved in the data
        // path (receive write intents) for this transactions
        FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, data.state.status());
      case tserver::TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS:
        // APPLIED_IN_ONE_OF_INVOLVED_TABLETS handled w/o use of RAFT log
        FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, data.state.status());
      case tserver::TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS:
        return AppliedInAllInvolvedTabletsReplicationFinished(data);
    }
    FATAL_INVALID_ENUM_VALUE(tserver::TransactionStatus, data.state.status());
  }

  void DoHandle(std::unique_ptr<tablet::UpdateTxnTransactionState> request) {
    const auto& state = *request->request();

    Status status;
    if (state.status() == tserver::TransactionStatus::COMMITTED) {
      status = HandleCommit();
    } else {
      status = Status::OK();
    }

    if (!status.ok()) {
      request->completion_callback()->set_error(status);
      request->completion_callback()->TransactionCompleted();
      return;
    }

    replicating_ = request.get();
    context_.SubmitUpdateTransaction(std::move(request));
  }

  CHECKED_STATUS HandleCommit() {
    auto hybrid_time = context_.coordinator_context().clock().Now();
    if (ExpiredAt(hybrid_time)) {
      Abort();
      return STATUS(Expired, "Commit of expired transaction");;
    }
    if (status_ != tserver::TransactionStatus::PENDING) {
      return STATUS_FORMAT(IllegalState,
                           "Transaction in wrong state when starting to commit: $0",
                           tserver::TransactionStatus_Name(status_));
    }

    return Status::OK();
  }

  void SubmitUpdateStatus(tserver::TransactionStatus status) {
    tserver::TransactionStatePB state;
    state.set_transaction_id(id_.begin(), id_.size());
    state.set_status(status);

    auto request = context_.coordinator_context().CreateUpdateTransactionState(&state);
    if (replicating_) {
      request_queue_.push_back(std::move(request));
    } else {
      replicating_ = request.get();
      context_.SubmitUpdateTransaction(std::move(request));
    }
  }

  void ProcessQueue() {
    while (!replicating_ && !request_queue_.empty()) {
      auto request = std::move(request_queue_.front());
      request_queue_.pop_front();
      DoHandle(std::move(request));
    }
  }

  CHECKED_STATUS AbortedReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    if (status_ != tserver::TransactionStatus::ABORTED &&
        status_ != tserver::TransactionStatus::PENDING) {
      LOG(DFATAL) << "Invalid status of aborted transaction: "
                  << tserver::TransactionStatus_Name(status_);
    }
    status_ = tserver::TransactionStatus::ABORTED;
    first_entry_raft_index_ = data.op_id.index();
    return Status::OK();
  }

  CHECKED_STATUS CommittedReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    CHECK_EQ(status_, tserver::TransactionStatus::PENDING);
    last_touch_ = data.hybrid_time;
    status_ = tserver::TransactionStatus::COMMITTED;
    unnotified_tablets_.insert(data.state.tablets().begin(), data.state.tablets().end());
    for (const auto& tablet : unnotified_tablets_) {
      context_.NotifyApplying(tablet, id_);
    }
    first_entry_raft_index_ = data.op_id.index();
    return Status::OK();
  }

  CHECKED_STATUS AppliedInAllInvolvedTabletsReplicationFinished(
      const TransactionCoordinator::ReplicatedData& data) {
    CHECK_EQ(status_, tserver::TransactionStatus::COMMITTED);
    last_touch_ = data.hybrid_time;
    status_ = tserver::TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS;
    first_entry_raft_index_ = data.op_id.index();
    return Status::OK();
  }

  // Used for PENDING and CREATED records. Because when we apply replicated operations they have
  // the same meaning.
  CHECKED_STATUS PendingReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    if (data.mode == ProcessingMode::LEADER && ExpiredAt(data.hybrid_time)) {
      Abort();
      return Status::OK();
    }
    CHECK_EQ(status_, tserver::TransactionStatus::PENDING);
    last_touch_ = data.hybrid_time;
    first_entry_raft_index_ = data.op_id.index();
    return Status::OK();
  }

  CHECKED_STATUS AppliedInOneOfInvolvedTablets(const tserver::TransactionStatePB& state) {
    CHECK_EQ(status_, tserver::TransactionStatus::COMMITTED);
    DCHECK_EQ(state.tablets_size(), 1);
    unnotified_tablets_.erase(state.tablets(0));
    if (unnotified_tablets_.empty()) {
      SubmitUpdateStatus(tserver::TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS);
    }
    return Status::OK();
  }

  TransactionStateContext& context_;
  const TransactionId id_;
  const std::string log_prefix_;
  tserver::TransactionStatus status_ = tserver::TransactionStatus::PENDING;
  HybridTime last_touch_;
  std::unordered_set<TabletId> unnotified_tablets_;
  int64_t first_entry_raft_index_ = std::numeric_limits<int64_t>::max();

  // The operation that we a currently replicating in RAFT.
  // It is owned by TransactionDriver (that will be renamed to OperationDriver).
  tablet::UpdateTxnTransactionState* replicating_ = nullptr;
  std::deque<std::unique_ptr<tablet::UpdateTxnTransactionState>> request_queue_;
};

// Makes transaction id from string.
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

// Contains actions that should be executed after lock in transaction coordinator is released.
struct PostponedActions {
  // List of tablets with transaction id, that should be notified that this transaction
  // is applying.
  std::vector<std::pair<TabletId, TransactionId>> notify_applying;
  // List of update transaction records, that should be replicated via RAFT.
  std::vector<std::unique_ptr<UpdateTxnTransactionState>> updates;

  void Swap(PostponedActions* other) {
    notify_applying.swap(other->notify_applying);
    updates.swap(other->updates);
  }
};

} // namespace

// Real implementation of transaction coordinator, as in PImpl idiom.
class TransactionCoordinator::Impl : public TransactionStateContext {
 public:
  explicit Impl(TransactionCoordinatorContext* context)
      : context_(*context) {}

  virtual ~Impl() {}

  size_t test_count_transactions() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    return managed_transactions_.size();
  }

  CHECKED_STATUS ProcessReplicated(const ReplicatedData& data) {
    auto id = MakeTransactionId(data.state.transaction_id());
    if (!id.ok()) {
      return std::move(id.status());
    }

    // APPLY is handled separately, because it is received for transactions not managed by
    // this tablet as a transaction status tablet, but tablets that are involved in the data
    // path (receive write intents) for this transaction.
    if (data.state.status() == tserver::TransactionStatus::APPLYING) {
      return ProcessApply(*id, data);
    }

    PostponedActions actions;
    Status result;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      auto it = get(*id, data.state.status(), data.hybrid_time);
      if (it == managed_transactions_.end()) {
        return Status::OK();
      }
      managed_transactions_.modify(
          it, [&](DTransactionState& ts) {
            result = ts.ProcessReplicated(data);
          });
      if (it->Completed()) {
        VLOG(1) << Format("Erase: $0", *it);
        managed_transactions_.erase(it);
      }
      actions.Swap(&postponed_actions_);
    }
    if (data.mode == ProcessingMode::LEADER) {
      ExecutePostponedActions(&actions);
    }

    VLOG(1) << "Processed: " << data.mode << ", " << data.state.ShortDebugString();
    return result;
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

  void ClearLocks() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    for (auto it = managed_transactions_.begin(); it != managed_transactions_.end(); ++it) {
      managed_transactions_.modify(it, std::bind(&DTransactionState::ClearLocks, _1));
    }
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnTransactionState> request) {
    auto& state = *request->request();
    auto id = MakeTransactionId(state.transaction_id());
    CHECK_OK(id);

    if (state.status() == tserver::TransactionStatus::APPLYING) {
      context_.SubmitUpdateTransaction(std::move(request));
      return;
    }

    PostponedActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        if (state.status() == tserver::TransactionStatus::CREATE) {
          it = managed_transactions_.emplace(this, *id, context_.clock().Now()).first;
        } else {
          LOG(WARNING) << "Request to unknown transaction " << id << ": "
                       << state.ShortDebugString();
          request->completion_callback()->set_error(STATUS(Expired, "Transaction expired"));
          request->completion_callback()->TransactionCompleted();
          return;
        }
      }

      managed_transactions_.modify(it, [&request](DTransactionState& state) {
        state.Handle(std::move(request));
      });
      postponed_actions_.Swap(&actions);
    }

    ExecutePostponedActions(&actions);
  }

  int64_t PrepareGC(ProcessingMode mode) {
    auto now = context_.clock().Now();
    HybridTime erase_time(now.GetPhysicalValueMicros() - FLAGS_transaction_heartbeat_usec, 0);
    int64_t min_needed_raft_index = std::numeric_limits<int64_t>::max();
    PostponedActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      auto& index = managed_transactions_.get<LastTouchTag>();
      for (auto it = index.begin(); it != index.end() && it->ExpiredAt(now);) {
        if (it->status() == tserver::TransactionStatus::ABORTED && it->ExpiredAt(erase_time)) {
          it = index.erase(it);
        } else {
          index.modify(it, [](DTransactionState& state) {
            state.Abort();
          });
          ++it;
        }
      }
      if (!managed_transactions_.empty()) {
        auto begin = managed_transactions_.get<FirstEntryIndexTag>().begin();
        min_needed_raft_index = begin->first_entry_raft_index();
      }
      postponed_actions_.Swap(&actions);
    }
    if (mode == ProcessingMode::LEADER) {
      ExecutePostponedActions(&actions);
    }
    return min_needed_raft_index;
  }

 private:
  class LastTouchTag;
  class FirstEntryIndexTag;

  typedef boost::multi_index_container<DTransactionState,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun<DTransactionState,
                                                const TransactionId&,
                                                &DTransactionState::id>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<LastTouchTag>,
              boost::multi_index::const_mem_fun<DTransactionState,
                                                HybridTime,
                                                &DTransactionState::last_touch>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<FirstEntryIndexTag>,
              boost::multi_index::const_mem_fun<DTransactionState,
                                                int64_t,
                                                &DTransactionState::first_entry_raft_index>
          >
      >
  > ManagedTransactions;

  CHECKED_STATUS ProcessApply(const TransactionId& id, const ReplicatedData& data) {
    CHECK_OK(data.applier->ApplyIntents(id, data.op_id, data.hybrid_time));

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

    if (data.mode == ProcessingMode::LEADER) {
      DCHECK_EQ(data.state.tablets_size(), 1);
      auto deadline = MonoTime::FineNow() + MonoDelta::FromSeconds(5); // TODO(dtxn)
      tserver::UpdateTransactionRequestPB req;
      req.set_tablet_id(data.state.tablets(0));
      auto& state = *req.mutable_state();
      state.set_transaction_id(id.begin(), id.size());
      state.set_status(tserver::TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS);
      state.add_tablets(context_.tablet_id());
      UpdateTransaction(
          deadline,
          nullptr /* remote_tablet */,
          context_.client().get(),
          &req,
          [](const Status& status) {
            LOG_IF(WARNING, !status.ok()) << "Failed to send applied: " << status.ToString();
          });
    }
    return Status::OK();
  }

  void ExecutePostponedActions(PostponedActions* actions) {
    if (!actions->notify_applying.empty()) {
      auto deadline = MonoTime::FineNow() + MonoDelta::FromSeconds(5); // TODO(dtxn)
      for (const auto& p : actions->notify_applying) {
        tserver::UpdateTransactionRequestPB req;
        req.set_tablet_id(p.first);
        auto& state = *req.mutable_state();
        state.set_transaction_id(p.second.begin(), p.second.size());
        state.set_status(tserver::TransactionStatus::APPLYING);
        state.add_tablets(context_.tablet_id());

        UpdateTransaction(
            deadline,
            nullptr /* remote_tablet */,
            context_.client().get(),
            &req,
            [](const Status& status) {
              LOG_IF(WARNING, !status.ok()) << "Failed to send apply: " << status.ToString();
            });
      }
    }

    for (auto& update : actions->updates) {
      context_.SubmitUpdateTransaction(std::move(update));
    }
  }

  ManagedTransactions::iterator get(const TransactionId& id,
                                    tserver::TransactionStatus status,
                                    HybridTime hybrid_time) {
    auto it = managed_transactions_.find(id);
    if (it == managed_transactions_.end()) {
      it = managed_transactions_.emplace(this, id, hybrid_time).first;
      LOG(INFO) << Format("Added: $0", *it);
    }
    return it;
  }

  TransactionCoordinatorContext& coordinator_context() override {
    return context_;
  }

  void NotifyApplying(const TabletId& tablet, const TransactionId& transaction) override {
    postponed_actions_.notify_applying.emplace_back(tablet, transaction);
  }

  void SubmitUpdateTransaction(std::unique_ptr<UpdateTxnTransactionState> state) override {
    postponed_actions_.updates.push_back(std::move(state));
  }

  TransactionCoordinatorContext& context_;

  std::mutex managed_mutex_;
  ManagedTransactions managed_transactions_;

  // Actions that should be executed after mutex is unlocked.
  PostponedActions postponed_actions_;

  std::mutex running_mutex_;
  std::unordered_map<TransactionId, TransactionMetadataPB, TransactionIdHash> running_transactions_;
};

TransactionCoordinator::TransactionCoordinator(TransactionCoordinatorContext* context)
    : impl_(new Impl(context)) {
}

TransactionCoordinator::~TransactionCoordinator() {
}

Status TransactionCoordinator::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

int64_t TransactionCoordinator::PrepareGC(ProcessingMode mode) {
  return impl_->PrepareGC(mode);
}

size_t TransactionCoordinator::test_count_transactions() const {
  return impl_->test_count_transactions();
}

void TransactionCoordinator::Add(const TransactionMetadataPB& data,
                                 rocksdb::WriteBatch *write_batch) {
  impl_->Add(data, write_batch);
}

void TransactionCoordinator::Handle(std::unique_ptr<tablet::UpdateTxnTransactionState> request) {
  impl_->Handle(std::move(request));
}

void TransactionCoordinator::ClearLocks() {
  impl_->ClearLocks();
}

} // namespace tablet
} // namespace yb
