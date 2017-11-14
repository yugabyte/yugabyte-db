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

#include "yb/tablet/transaction_coordinator.h"

#include <condition_variable>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>

#include <boost/scope_exit.hpp>

#include <boost/uuid/uuid_io.hpp>

#include "yb/client/client.h"
#include "yb/client/transaction_rpc.h"

#include "yb/common/entity_ids.h"
#include "yb/common/transaction.h"

#include "yb/consensus/opid_util.h"

#include "yb/rpc/messenger.h"

#include "yb/server/clock.h"

#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/operations/update_txn_operation.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/enums.h"
#include "yb/util/kernel_stack_watchdog.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/tsan_util.h"

DECLARE_uint64(transaction_heartbeat_usec);
DEFINE_uint64(transaction_timeout_usec, 1500000, "Transaction expiration timeout in usec.");
DEFINE_uint64(transaction_check_interval_usec, 500000, "Transaction check interval in usec.");
DEFINE_double(transaction_ignore_applying_probability_in_tests, 0,
              "Probability to ignore APPLYING update in tests.");

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace tablet {

namespace {

void InvokeAbortCallback(const TransactionAbortCallback& callback,
                         TransactionStatus status,
                         HybridTime time = HybridTime::kInvalidHybridTime) {
  DCHECK((!time.is_valid()) || (status == TransactionStatus::COMMITTED))
      << "Status: " << TransactionStatus_Name(status) << ", time: " << time;
  callback(TransactionStatusResult{status, time});
}

struct NotifyApplyingData {
  TabletId tablet;
  TransactionId transaction;
  HybridTime commit_time;
};

// Context for transaction state. I.e. access to external facilities required by
// transaction state to do its job.
class TransactionStateContext {
 public:
  virtual TransactionCoordinatorContext& coordinator_context() = 0;

  virtual void NotifyApplying(NotifyApplyingData data) = 0;

  // Submits update transaction to the RAFT log. Returns false if was not able to submit.
  virtual MUST_USE_RESULT bool SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperationState> state) = 0;
 protected:
  ~TransactionStateContext() {}
};

// TransactionState keeps state of single transaction.
// User of this class should guarantee that it does NOT invoke methods concurrently.
class TransactionState {
 public:
  explicit TransactionState(TransactionStateContext* context,
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
  TransactionStatus status() const {
    return status_;
  }

  // RAFT index of first RAFT log entry required by this transaction.
  int64_t first_entry_raft_index() const {
    return first_entry_raft_index_;
  }

  // Returns debug string this representation of this class.
  std::string ToString() const {
    return Format("{ id: $0 last_touch: $1 status: $2 unnotified_tablets: $3 }",
                  to_string(id_), last_touch_, TransactionStatus_Name(status_),
                  unnotified_tablets_);
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
    return status_ == TransactionStatus::ABORTED ||
           status_ == TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS;
  }

  // Applies new state to transaction.
  CHECKED_STATUS ProcessReplicated(const TransactionCoordinator::ReplicatedData& data) {
    if (data.mode == ProcessingMode::LEADER) {
      DCHECK(replicating_ == nullptr || consensus::OpIdEquals(replicating_->op_id(), data.op_id));
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
    if (replicating_ != nullptr || !request_queue_.empty() || !abort_waiters_.empty()) {
      auto status = STATUS(TryAgain, "Leader changed during abort");
      if (replicating_ != nullptr) {
        replicating_->completion_callback()->CompleteWithStatus(status);
        replicating_ = nullptr;
      }

      for (auto& entry : request_queue_) {
        entry->completion_callback()->CompleteWithStatus(status);
      }
      request_queue_.clear();

      for (auto& waiter : abort_waiters_) {
        waiter(status);
      }
      abort_waiters_.clear();
    }
  }

  CHECKED_STATUS GetStatus(tserver::GetTransactionStatusResponsePB* response) const {
    if (status_ == TransactionStatus::COMMITTED) {
      response->set_status(TransactionStatus::COMMITTED);
      response->set_status_hybrid_time(commit_time_.ToUint64());
    } else if (status_ == TransactionStatus::ABORTED) {
      response->set_status(TransactionStatus::ABORTED);
    } else {
      CHECK_EQ(TransactionStatus::PENDING, status_);
      response->set_status(TransactionStatus::PENDING);
      response->set_status_hybrid_time(
          context_.coordinator_context().LastCommittedHybridTime().ToUint64());
    }
    return Status::OK();
  }

  void Abort(TransactionAbortCallback callback, std::unique_lock<std::mutex>* lock) {
    if (ShouldBeCommitted()) {
      lock->unlock();
      InvokeAbortCallback(callback, TransactionStatus::COMMITTED);
    } else if (status_ == TransactionStatus::ABORTED) {
      lock->unlock();
      InvokeAbortCallback(callback, TransactionStatus::ABORTED);
    } else {
      CHECK_EQ(TransactionStatus::PENDING, status_);
      abort_waiters_.emplace_back(std::move(callback));
      Abort();
    }
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperationState> request) {
    auto& state = *request->request();
    VLOG(1) << "Handle: " << state.ShortDebugString();
    if (state.status() == TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS) {
      auto status = AppliedInOneOfInvolvedTablets(state);
      request->completion_callback()->CompleteWithStatus(status);
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
    CHECK_EQ(status_, TransactionStatus::PENDING);
    SubmitUpdateStatus(TransactionStatus::ABORTED);
  }

  // Returns logs prefix for this transaction.
  const std::string& LogPrefix() {
    return log_prefix_;
  }

  void Poll() const {
    if (status_ == TransactionStatus::COMMITTED) {
      DCHECK(!unnotified_tablets_.empty() ||
             ShouldBeInStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS));
      for (auto& tablet : unnotified_tablets_) {
        context_.NotifyApplying({tablet, id_, commit_time_});
      }
    }
  }

 private:
  // Checks whether we in specified status or going to be in this status when replication is
  // finished.
  bool ShouldBeInStatus(TransactionStatus status) const {
    return status_ == status ||
           (replicating_ && replicating_->request()->status() == status);
  }

  bool ShouldBeCommitted() const {
    return ShouldBeInStatus(TransactionStatus::COMMITTED);
  }

  bool ShouldBeAborted() const {
    return ShouldBeInStatus(TransactionStatus::ABORTED);
  }

  // Process operation that was replicated in RAFT.
  CHECKED_STATUS DoProcessReplicated(const TransactionCoordinator::ReplicatedData& data) {
    switch (data.state.status()) {
      case TransactionStatus::ABORTED:
        return AbortedReplicationFinished(data);
      case TransactionStatus::COMMITTED:
        return CommittedReplicationFinished(data);
      case TransactionStatus::CREATED: FALLTHROUGH_INTENDED;
      case TransactionStatus::PENDING:
        return PendingReplicationFinished(data);
      case TransactionStatus::APPLYING:
        // APPLYING is handled separately, because it is received for transactions not managed by
        // this tablet as a transaction status tablet, but tablets that are involved in the data
        // path (receive write intents) for this transactions
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
      case TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS:
        // APPLIED_IN_ONE_OF_INVOLVED_TABLETS handled w/o use of RAFT log
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
      case TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS:
        return AppliedInAllInvolvedTabletsReplicationFinished(data);
    }
    FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
  }

  void DoHandle(std::unique_ptr<tablet::UpdateTxnOperationState> request) {
    const auto& state = *request->request();

    Status status;
    if (state.status() == TransactionStatus::COMMITTED) {
      status = HandleCommit();
    } else {
      status = Status::OK();
    }

    if (!status.ok()) {
      request->completion_callback()->CompleteWithStatus(status);
      return;
    }

    replicating_ = request.get();
    auto submitted = context_.SubmitUpdateTransaction(std::move(request));
    CHECK(submitted);
  }

  CHECKED_STATUS HandleCommit() {
    auto hybrid_time = context_.coordinator_context().clock().Now();
    if (ExpiredAt(hybrid_time)) {
      Abort();
      return STATUS(Expired, "Commit of expired transaction");;
    }
    if (status_ != TransactionStatus::PENDING) {
      return STATUS_FORMAT(IllegalState,
                           "Transaction in wrong state when starting to commit: $0",
                           TransactionStatus_Name(status_));
    }

    return Status::OK();
  }

  void SubmitUpdateStatus(TransactionStatus status) {
    tserver::TransactionStatePB state;
    state.set_transaction_id(id_.begin(), id_.size());
    state.set_status(status);

    auto request = context_.coordinator_context().CreateUpdateTransactionState(&state);
    if (replicating_) {
      request_queue_.push_back(std::move(request));
    } else {
      replicating_ = request.get();
      if (!context_.SubmitUpdateTransaction(std::move(request))) {
        // Was not able to submit update transaction, for instance we are not leader.
        // So we are not replicating.
        replicating_ = nullptr;
      }
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
    if (status_ != TransactionStatus::ABORTED &&
        status_ != TransactionStatus::PENDING) {
      LOG(DFATAL) << "Invalid status of aborted transaction: "
                  << TransactionStatus_Name(status_);
    }

    status_ = TransactionStatus::ABORTED;
    first_entry_raft_index_ = data.op_id.index();
    for (auto& waiter : abort_waiters_) {
      InvokeAbortCallback(waiter, status_);
    }
    abort_waiters_.clear();
    return Status::OK();
  }

  CHECKED_STATUS CommittedReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    CHECK_EQ(status_, TransactionStatus::PENDING);
    last_touch_ = data.hybrid_time;
    commit_time_ = data.hybrid_time;
    status_ = TransactionStatus::COMMITTED;
    unnotified_tablets_.insert(data.state.tablets().begin(), data.state.tablets().end());
    for (const auto& tablet : unnotified_tablets_) {
      context_.NotifyApplying({tablet, id_, commit_time_});
    }
    first_entry_raft_index_ = data.op_id.index();
    return Status::OK();
  }

  CHECKED_STATUS AppliedInAllInvolvedTabletsReplicationFinished(
      const TransactionCoordinator::ReplicatedData& data) {
    CHECK_EQ(status_, TransactionStatus::COMMITTED);
    last_touch_ = data.hybrid_time;
    status_ = TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS;
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
    CHECK_EQ(status_, TransactionStatus::PENDING);
    last_touch_ = data.hybrid_time;
    first_entry_raft_index_ = data.op_id.index();
    return Status::OK();
  }

  CHECKED_STATUS AppliedInOneOfInvolvedTablets(const tserver::TransactionStatePB& state) {
    CHECK_EQ(status_, TransactionStatus::COMMITTED);
    DCHECK_EQ(state.tablets_size(), 1);
    unnotified_tablets_.erase(state.tablets(0));
    if (unnotified_tablets_.empty()) {
      SubmitUpdateStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS);
    }
    return Status::OK();
  }

  TransactionStateContext& context_;
  const TransactionId id_;
  const std::string log_prefix_;
  TransactionStatus status_ = TransactionStatus::PENDING;
  HybridTime last_touch_;
  // It should match last_touch_, but it is possible that because of some code errors it
  // would not be so. To add stability we introduce a separate field for it.
  HybridTime commit_time_;
  std::unordered_set<TabletId> unnotified_tablets_;
  int64_t first_entry_raft_index_ = std::numeric_limits<int64_t>::max();

  // The operation that we a currently replicating in RAFT.
  // It is owned by TransactionDriver (that will be renamed to OperationDriver).
  tablet::UpdateTxnOperationState* replicating_ = nullptr;
  std::deque<std::unique_ptr<tablet::UpdateTxnOperationState>> request_queue_;

  std::vector<TransactionAbortCallback> abort_waiters_;
};

// Contains actions that should be executed after lock in transaction coordinator is released.
struct PostponedLeaderActions {
  bool leader = false;
  // List of tablets with transaction id, that should be notified that this transaction
  // is applying.
  std::vector<NotifyApplyingData> notify_applying;
  // List of update transaction records, that should be replicated via RAFT.
  std::vector<std::unique_ptr<UpdateTxnOperationState>> updates;

  void Swap(PostponedLeaderActions* other) {
    std::swap(leader, other->leader);
    notify_applying.swap(other->notify_applying);
    updates.swap(other->updates);
  }
};

} // namespace

// Real implementation of transaction coordinator, as in PImpl idiom.
class TransactionCoordinator::Impl : public TransactionStateContext {
 public:
  Impl(TransactionCoordinatorContext* context, TransactionParticipant* transaction_participant)
      : context_(*context), transaction_participant_(*transaction_participant) {
  }

  virtual ~Impl() {
    Shutdown();
  }

  void Shutdown() {
    {
      std::unique_lock<std::mutex> lock(managed_mutex_);
      if (!closing_) {
        closing_ = true;
        if (poll_task_id_ != rpc::kUninitializedScheduledTaskId) {
          context_.client_future().get()->messenger()->scheduler().Abort(poll_task_id_);
        }
      }
      cond_.wait(lock, [this] { return poll_task_id_ == rpc::kUninitializedScheduledTaskId; });
      // There could be rare case when poll left mutex and is executing postponed actions.
      // So we should wait that it also completed.
      while (running_polls_ != 0) {
        std::this_thread::sleep_for(10ms);
      }
    }
    rpcs_.Shutdown();
  }

  CHECKED_STATUS GetStatus(const std::string& transaction_id,
                           tserver::GetTransactionStatusResponsePB* response) {
    auto id = FullyDecodeTransactionId(transaction_id);
    if (!id.ok()) {
      return std::move(id.status());
    }

    std::lock_guard<std::mutex> lock(managed_mutex_);
    auto it = managed_transactions_.find(*id);
    if (it == managed_transactions_.end()) {
      response->set_status(TransactionStatus::ABORTED);
      return Status::OK();
    }
    return it->GetStatus(response);
  }

  void Abort(const std::string& transaction_id, TransactionAbortCallback callback) {
    auto id = FullyDecodeTransactionId(transaction_id);
    if (!id.ok()) {
      callback(id.status());
      return;
    }

    PostponedLeaderActions actions;
    {
      std::unique_lock<std::mutex> lock(managed_mutex_);
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        lock.unlock();
        InvokeAbortCallback(callback, TransactionStatus::ABORTED);
        return;
      }
      postponed_leader_actions_.leader = true;
      managed_transactions_.modify(it, [&callback, &lock](TransactionState& transaction) {
        transaction.Abort(std::move(callback), &lock);
      });
      actions.Swap(&postponed_leader_actions_);
    }

    ExecutePostponedLeaderActions(&actions);
  }


  size_t test_count_transactions() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    return managed_transactions_.size();
  }

  CHECKED_STATUS ProcessReplicated(const ReplicatedData& data) {
    auto id = FullyDecodeTransactionId(data.state.transaction_id());
    if (!id.ok()) {
      return std::move(id.status());
    }

    // APPLY is handled separately, because it is received for transactions not managed by
    // this tablet as a transaction status tablet, but tablets that are involved in the data
    // path (receive write intents) for this transaction.
    if (data.state.status() == TransactionStatus::APPLYING) {
      // data.state.tablets contains only status tablet.
      DCHECK_EQ(data.state.tablets_size(), 1);
      HybridTime commit_time(data.state.commit_hybrid_time());
      return transaction_participant_.ProcessApply(
          { data.mode, data.applier, *id, data.op_id, commit_time, data.state.tablets(0) });
    }

    PostponedLeaderActions actions;
    Status result;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader = data.mode == ProcessingMode::LEADER;
      auto it = get(*id, data.state.status(), data.hybrid_time);
      if (it == managed_transactions_.end()) {
        return Status::OK();
      }
      managed_transactions_.modify(
          it, [&](TransactionState& ts) {
            result = ts.ProcessReplicated(data);
          });
      if (it->Completed()) {
        VLOG(1) << Format("Erase: $0", *it);
        managed_transactions_.erase(it);
      }
      actions.Swap(&postponed_leader_actions_);
    }
    ExecutePostponedLeaderActions(&actions);

    VLOG(1) << "Processed: " << data.mode << ", " << data.state.ShortDebugString();
    return result;
  }

  void ClearLocks() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    for (auto it = managed_transactions_.begin(); it != managed_transactions_.end(); ++it) {
      managed_transactions_.modify(it, std::bind(&TransactionState::ClearLocks, _1));
    }
  }

  void Start() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    if (!closing_) {
      SchedulePoll();
    }
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperationState> request) {
    auto& state = *request->request();
    auto id = FullyDecodeTransactionId(state.transaction_id());
    CHECK_OK(id);

    if (state.status() == TransactionStatus::APPLYING) {
      if (RandomActWithProbability(GetAtomicFlag(
          &FLAGS_transaction_ignore_applying_probability_in_tests))) {
        request->completion_callback()->CompleteWithStatus(Status::OK());
        return;
      }
      context_.SubmitUpdateTransaction(std::move(request));
      return;
    }

    PostponedLeaderActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader = true;
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        if (state.status() == TransactionStatus::CREATED) {
          it = managed_transactions_.emplace(this, *id, context_.clock().Now()).first;
        } else {
          LOG(WARNING) << "Request to unknown transaction " << id << ": "
                       << state.ShortDebugString();
          request->completion_callback()->CompleteWithStatus(
              STATUS(Expired, "Transaction expired"));
          return;
        }
      }

      managed_transactions_.modify(it, [&request](TransactionState& state) {
        state.Handle(std::move(request));
      });
      postponed_leader_actions_.Swap(&actions);
    }

    ExecutePostponedLeaderActions(&actions);
  }

  int64_t PrepareGC() {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    if (!managed_transactions_.empty()) {
      return managed_transactions_.get<FirstEntryIndexTag>().begin()->first_entry_raft_index();
    }
    return std::numeric_limits<int64_t>::max();
  }

 private:
  class LastTouchTag;
  class FirstEntryIndexTag;

  typedef boost::multi_index_container<TransactionState,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::const_mem_fun<TransactionState,
                                                const TransactionId&,
                                                &TransactionState::id>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<LastTouchTag>,
              boost::multi_index::const_mem_fun<TransactionState,
                                                HybridTime,
                                                &TransactionState::last_touch>
          >,
          boost::multi_index::ordered_non_unique <
              boost::multi_index::tag<FirstEntryIndexTag>,
              boost::multi_index::const_mem_fun<TransactionState,
                                                int64_t,
                                                &TransactionState::first_entry_raft_index>
          >
      >
  > ManagedTransactions;

  void ExecutePostponedLeaderActions(PostponedLeaderActions* actions) {
    if (!actions->leader) {
      return;
    }

    if (!actions->notify_applying.empty()) {
      auto deadline = TransactionRpcDeadline();
      for (const auto& p : actions->notify_applying) {
        tserver::UpdateTransactionRequestPB req;
        req.set_tablet_id(p.tablet);
        auto& state = *req.mutable_state();
        state.set_transaction_id(p.transaction.begin(), p.transaction.size());
        state.set_status(TransactionStatus::APPLYING);
        state.add_tablets(context_.tablet_id());
        state.set_commit_hybrid_time(p.commit_time.ToUint64());

        auto handle = rpcs_.Prepare();
        *handle = UpdateTransaction(
            deadline,
            nullptr /* remote_tablet */,
            context_.client_future().get().get(),
            &req,
            [this, handle](const Status& status, HybridTime propagated_hybrid_time) {
              if (propagated_hybrid_time.is_valid()) {
                context_.UpdateClock(propagated_hybrid_time);
              }
              rpcs_.Unregister(handle);
              LOG_IF(WARNING, !status.ok()) << "Failed to send apply: " << status;
            });
        (**handle).SendRpc();
      }
    }

    for (auto& update : actions->updates) {
      context_.SubmitUpdateTransaction(std::move(update));
    }
  }

  ManagedTransactions::iterator get(const TransactionId& id,
                                    TransactionStatus status,
                                    HybridTime hybrid_time) {
    auto it = managed_transactions_.find(id);
    if (it == managed_transactions_.end()) {
      if (status != TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS) {
        it = managed_transactions_.emplace(this, id, hybrid_time).first;
        LOG(INFO) << Format("Added: $0", *it);
      }
    }
    return it;
  }

  TransactionCoordinatorContext& coordinator_context() override {
    return context_;
  }

  void NotifyApplying(NotifyApplyingData data) override {
    postponed_leader_actions_.notify_applying.push_back(std::move(data));
  }

  MUST_USE_RESULT bool SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperationState> state) override {
    if (postponed_leader_actions_.leader) {
      postponed_leader_actions_.updates.push_back(std::move(state));
      return true;
    } else {
      return false;
    }
  }

  void SchedulePoll() {
    poll_task_id_ = context_.client_future().get()->messenger()->scheduler().Schedule(
        std::bind(&Impl::Poll, this, _1),
        std::chrono::microseconds(NonTsanVsTsan(1, 4) * FLAGS_transaction_check_interval_usec));
  }

  void Poll(const Status& status) {
    ++running_polls_;
    BOOST_SCOPE_EXIT(&running_polls_) {
      --running_polls_;
    } BOOST_SCOPE_EXIT_END;
    auto now = context_.clock().Now();

    bool leader = context_.LeaderStatus() == consensus::Consensus::LeaderStatus::LEADER_AND_READY;
    PostponedLeaderActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      if (!status.ok() || closing_) {
        LOG(INFO) << "Poll stopped: " << status << ", " << this;
        poll_task_id_ = rpc::kUninitializedScheduledTaskId;
        cond_.notify_one();
        return;
      }
      postponed_leader_actions_.leader = leader;

      auto& index = managed_transactions_.get<LastTouchTag>();

      for (auto it = index.begin(); it != index.end() && it->ExpiredAt(now);) {
        if (it->status() == TransactionStatus::ABORTED) {
          it = index.erase(it);
        } else {
          bool modified = index.modify(it, [](TransactionState& state) {
            state.Abort();
          });
          DCHECK(modified);
          ++it;
        }
      }
      for (auto& transaction : managed_transactions_) {
        transaction.Poll();
      }
      postponed_leader_actions_.Swap(&actions);

      SchedulePoll();
    }
    ExecutePostponedLeaderActions(&actions);
  }

  TransactionCoordinatorContext& context_;
  TransactionParticipant& transaction_participant_;

  std::mutex managed_mutex_;
  ManagedTransactions managed_transactions_;

  // Actions that should be executed after mutex is unlocked.
  PostponedLeaderActions postponed_leader_actions_;

  bool closing_ = false;
  rpc::ScheduledTaskId poll_task_id_ = rpc::kUninitializedScheduledTaskId;
  std::atomic<int64_t> running_polls_{0};
  std::condition_variable cond_;

  rpc::Rpcs rpcs_;
};

TransactionCoordinator::TransactionCoordinator(TransactionCoordinatorContext* context,
                                               TransactionParticipant* transaction_participant)
    : impl_(new Impl(context, transaction_participant)) {
}

TransactionCoordinator::~TransactionCoordinator() {
}

Status TransactionCoordinator::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

int64_t TransactionCoordinator::PrepareGC() {
  return impl_->PrepareGC();
}

size_t TransactionCoordinator::test_count_transactions() const {
  return impl_->test_count_transactions();
}

void TransactionCoordinator::Handle(std::unique_ptr<tablet::UpdateTxnOperationState> request) {
  impl_->Handle(std::move(request));
}

void TransactionCoordinator::ClearLocks() {
  impl_->ClearLocks();
}

void TransactionCoordinator::Start() {
  impl_->Start();
}

void TransactionCoordinator::Shutdown() {
  impl_->Shutdown();
}

Status TransactionCoordinator::GetStatus(const std::string& transaction_id,
                                         tserver::GetTransactionStatusResponsePB* response) {
  return impl_->GetStatus(transaction_id, response);
}

void TransactionCoordinator::Abort(const std::string& transaction_id,
                                   TransactionAbortCallback callback) {
  impl_->Abort(transaction_id, std::move(callback));
}

} // namespace tablet
} // namespace yb
