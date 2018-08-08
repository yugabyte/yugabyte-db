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
#include "yb/rpc/rpc.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet.h"
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
DEFINE_double(transaction_max_missed_heartbeat_periods, 10.0 * yb::kTimeMultiplier,
              "Maximum heartbeat periods that a pending transaction can miss before the "
              "transaction coordinator expires the transaction. The total expiration time in "
              "microseconds is transaction_heartbeat_usec times "
              "transaction_max_missed_heartbeat_periods. The value passed to this flag may be "
              "fractional.");
DEFINE_uint64(transaction_check_interval_usec, 500000, "Transaction check interval in usec.");
DEFINE_double(transaction_ignore_applying_probability_in_tests, 0,
              "Probability to ignore APPLYING update in tests.");

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace tablet {

std::chrono::microseconds GetTransactionTimeout() {
  const double timeout = GetAtomicFlag(&FLAGS_transaction_max_missed_heartbeat_periods) *
                         GetAtomicFlag(&FLAGS_transaction_heartbeat_usec);
  return timeout >= std::chrono::microseconds::max().count()
      ? std::chrono::microseconds::max()
      : std::chrono::microseconds(static_cast<int64_t>(timeout));
}

namespace {

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

  virtual Counter& expired_metric() = 0;

  // Submits update transaction to the RAFT log. Returns false if was not able to submit.
  virtual MUST_USE_RESULT bool SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperationState> state) = 0;
 protected:
  ~TransactionStateContext() {}
};

std::string BuildLogPrefix(const std::string& parent_log_prefix, const TransactionId& id) {
  auto id_string = boost::uuids::to_string(id);
  return parent_log_prefix.substr(0, parent_log_prefix.length() - 2) + "ID " + id_string + ": ";
}

// TransactionState keeps state of single transaction.
// User of this class should guarantee that it does NOT invoke methods concurrently.
class TransactionState {
 public:
  explicit TransactionState(TransactionStateContext* context,
                            const TransactionId& id,
                            HybridTime last_touch,
                            const std::string& parent_log_prefix)
      : context_(*context),
        id_(id),
        log_prefix_(BuildLogPrefix(parent_log_prefix, id)),
        last_touch_(last_touch) {
  }

  ~TransactionState() {
    DCHECK(abort_waiters_.empty());
    DCHECK(request_queue_.empty());
    DCHECK(replicating_ == nullptr);
  }

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
    return Format("{ id: $0 last_touch: $1 status: $2 unnotified_tablets: $3 replicating: $4 "
                  " request_queue: $5 }",
                  to_string(id_), last_touch_, TransactionStatus_Name(status_),
                  unnotified_tablets_, replicating_, request_queue_);
  }

  // Whether this transaction expired at specified time.
  bool ExpiredAt(HybridTime now) const {
    if (ShouldBeCommitted()) {
      return false;
    }
    const int64_t passed = now.GetPhysicalValueMicros() - last_touch_.GetPhysicalValueMicros();
    if (std::chrono::microseconds(passed) > GetTransactionTimeout()) {
      context_.expired_metric().Increment();
      return true;
    }
    return false;
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

  void ProcessAborted(const TransactionCoordinator::AbortedData& data) {
    if (data.mode == ProcessingMode::LEADER) {
      DCHECK(replicating_ == nullptr || !replicating_->op_id().IsInitialized() ||
             consensus::OpIdEquals(replicating_->op_id(), data.op_id));
      replicating_ = nullptr;
      ProcessQueue();
    }
  }

  // Clear requests of this transaction.
  void ClearRequests(const Status& status) {
    VLOG_WITH_PREFIX(1) << "ClearRequests: " << status;
    if (replicating_ != nullptr) {
      replicating_->completion_callback()->CompleteWithStatus(status);
      replicating_ = nullptr;
    }

    for (auto& entry : request_queue_) {
      entry->completion_callback()->CompleteWithStatus(status);
    }
    request_queue_.clear();

    NotifyAbortWaiters(status);
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
      HybridTime status_ht = context_.coordinator_context().clock().Now();
      if (replicating_) {
        auto replicating_status = replicating_->request()->status();
        if (replicating_status == TransactionStatus::COMMITTED ||
            replicating_status == TransactionStatus::ABORTED) {
          auto replicating_ht = replicating_->hybrid_time_even_if_unset();
          if (replicating_ht.is_valid()) {
            status_ht = replicating_ht;
          }
        }
      }
      status_ht = std::min(status_ht, context_.coordinator_context().HtLeaseExpiration());
      response->set_status_hybrid_time(status_ht.Decremented().ToUint64());
    }
    return Status::OK();
  }

  TransactionStatus Abort(TransactionAbortCallback* callback) {
    if (ShouldBeCommitted()) {
      return TransactionStatus::COMMITTED;
    } else if (status_ == TransactionStatus::ABORTED) {
      return TransactionStatus::ABORTED;
    } else {
      CHECK_EQ(TransactionStatus::PENDING, status_);
      abort_waiters_.emplace_back(std::move(*callback));
      Abort();
      return TransactionStatus::PENDING;
    }
  }

  void Handle(std::unique_ptr<tablet::UpdateTxnOperationState> request) {
    auto& state = *request->request();
    VLOG_WITH_PREFIX(1) << "Handle: " << state.ShortDebugString();
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

  void Poll(bool leader) {
    if (status_ == TransactionStatus::COMMITTED) {
      if (unnotified_tablets_.empty()) {
        if (leader && !ShouldBeInStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS)) {
          SubmitUpdateStatus(TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS);
        }
      } else if (!just_committed_) {
        for (auto& tablet : unnotified_tablets_) {
          context_.NotifyApplying({tablet, id_, commit_time_});
        }
      } else {
        // We skip first attempt to guarantee that at least 500ms passed between requests.
        just_committed_ = false;
      }
    }
  }

 private:
  // Checks whether we in specified status or going to be in this status when replication is
  // finished.
  bool ShouldBeInStatus(TransactionStatus status) const {
    if (status_ == status) {
      return true;
    }
    if (replicating_) {
      if (replicating_->request()->status() == status) {
        return true;
      }

      for (const auto& entry : request_queue_) {
        if (entry->request()->status() == status) {
          return true;
        }
      }
    }

    return false;
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
      case TransactionStatus::CLEANUP:
        // CLEANUP is handled separately, because it is received for transactions not managed by
        // this tablet as a transaction status tablet, but tablets that are involved in the data
        // path (receive write intents) for this transactions
        FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
    }
    FATAL_INVALID_ENUM_VALUE(TransactionStatus, data.state.status());
  }

  void DoHandle(std::unique_ptr<tablet::UpdateTxnOperationState> request) {
    const auto& state = *request->request();

    Status status;
    if (state.status() == TransactionStatus::COMMITTED) {
      status = HandleCommit();
    } else if (state.status() == TransactionStatus::PENDING) {
      if (status_ != TransactionStatus::PENDING) {
        status = STATUS_FORMAT(IllegalState,
            "Transaction in wrong state during heartbeat: $0",
            TransactionStatus_Name(status_));
      } else {
        status = Status::OK();
      }
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
    VLOG_WITH_PREFIX(4) << "SubmitUpdateStatus(" << TransactionStatus_Name(status) << ")";

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
      LOG_WITH_PREFIX(DFATAL) << "Invalid status of aborted transaction: "
                              << TransactionStatus_Name(status_);
    }

    status_ = TransactionStatus::ABORTED;
    first_entry_raft_index_ = data.op_id.index();
    NotifyAbortWaiters(TransactionStatusResult{status_});
    return Status::OK();
  }

  CHECKED_STATUS CommittedReplicationFinished(const TransactionCoordinator::ReplicatedData& data) {
    CHECK_EQ(status_, TransactionStatus::PENDING);
    last_touch_ = data.hybrid_time;
    commit_time_ = data.hybrid_time;
    VLOG_WITH_PREFIX(4) << "Commit time: " << commit_time_;
    status_ = TransactionStatus::COMMITTED;
    just_committed_ = true;
    unnotified_tablets_.insert(data.state.tablets().begin(), data.state.tablets().end());
    for (const auto& tablet : unnotified_tablets_) {
      context_.NotifyApplying({tablet, id_, commit_time_});
    }
    NotifyAbortWaiters(TransactionStatusResult{TransactionStatus::COMMITTED});
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
    CHECK_EQ(status_, TransactionStatus::PENDING) << "Transaction id: " << id_;
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

  void NotifyAbortWaiters(const Result<TransactionStatusResult>& result) {
    for (auto& waiter : abort_waiters_) {
      waiter(result);
    }
    abort_waiters_.clear();
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
  bool just_committed_ = false;
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
  Impl(const std::string& permanent_uuid,
       TransactionCoordinatorContext* context,
       TransactionParticipant* transaction_participant,
       Counter* expired_metric)
      : context_(*context), transaction_participant_(transaction_participant),
        expired_metric_(*expired_metric),
        log_prefix_(Format("T $0 P $1: ", context->tablet_id(), permanent_uuid)) {
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
        callback(TransactionStatusResult{TransactionStatus::ABORTED});
        return;
      }
      postponed_leader_actions_.leader = true;
      auto status = Modify(it).Abort(&callback);
      if (callback) {
        callback(TransactionStatusResult{status});
        return;
      }
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
      DCHECK(transaction_participant_);
      HybridTime commit_time(data.state.commit_hybrid_time());
      return transaction_participant_->ProcessApply(
          { data.mode, *id, data.op_id, commit_time, data.hybrid_time,
            data.state.tablets(0) });
    }

    PostponedLeaderActions actions;
    Status result;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader = data.mode == ProcessingMode::LEADER;
      auto it = GetTransaction(*id, data.state.status(), data.hybrid_time);
      if (it == managed_transactions_.end()) {
        return Status::OK();
      }
      result = Modify(it).ProcessReplicated(data);
      CheckCompleted(it);
      actions.Swap(&postponed_leader_actions_);
    }
    ExecutePostponedLeaderActions(&actions);

    VLOG_WITH_PREFIX(1) << "Processed: " << data.mode << ", " << data.state.ShortDebugString();
    return result;
  }

  void ProcessAborted(const AbortedData& data) {
    auto id = FullyDecodeTransactionId(data.state.transaction_id());
    if (!id.ok()) {
      LOG_WITH_PREFIX(DFATAL) << "Abort of transaction with bad id "
                              << data.state.ShortDebugString() << ": " << id.status();
      return;
    }

    PostponedLeaderActions actions;
    {
      std::lock_guard<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader = data.mode == ProcessingMode::LEADER;
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        LOG_WITH_PREFIX(WARNING) << "Aborted operation for unknown transaction: " << *id;
        return;
      }
      managed_transactions_.modify(
          it, [&](TransactionState& ts) {
            ts.ProcessAborted(data);
          });
      CheckCompleted(it);
      actions.Swap(&postponed_leader_actions_);
    }
    ExecutePostponedLeaderActions(&actions);

    VLOG_WITH_PREFIX(1) << "Aborted, mode: " << yb::ToString(data.mode)
                        << ", state: " << data.state.ShortDebugString();
  }

  void ClearLocks(const Status& status) {
    std::lock_guard<std::mutex> lock(managed_mutex_);
    for (auto it = managed_transactions_.begin(); it != managed_transactions_.end(); ++it) {
      managed_transactions_.modify(it, std::bind(&TransactionState::ClearRequests, _1, status));
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
    if (!id.ok()) {
      LOG(WARNING) << "Failed to decode id from " << state.ShortDebugString() << ": " << id;
      request->completion_callback()->CompleteWithStatus(id.status());
      return;
    }

    if (state.status() == TransactionStatus::APPLYING) {
      if (RandomActWithProbability(GetAtomicFlag(
          &FLAGS_transaction_ignore_applying_probability_in_tests))) {
        request->completion_callback()->CompleteWithStatus(Status::OK());
        return;
      }
      context_.SubmitUpdateTransaction(std::move(request));
      return;
    }

    if (state.status() == TransactionStatus::CLEANUP) {
      HybridTime commit_time(state.commit_hybrid_time());
      TransactionApplyData data = {
          ProcessingMode::LEADER, *id, request->op_id(), commit_time, commit_time, std::string()};
      Status status = transaction_participant_->ProcessCleanup(data);
      request->completion_callback()->CompleteWithStatus(Status::OK());
      return;
    }

    PostponedLeaderActions actions;
    {
      std::unique_lock<std::mutex> lock(managed_mutex_);
      postponed_leader_actions_.leader = true;
      auto it = managed_transactions_.find(*id);
      if (it == managed_transactions_.end()) {
        if (state.status() == TransactionStatus::CREATED) {
          it = managed_transactions_.emplace(
              this, *id, context_.clock().Now(), log_prefix_).first;
        } else {
          lock.unlock();
          LOG_WITH_PREFIX(WARNING) << "Request to unknown transaction " << id << ": "
                                   << state.ShortDebugString();
          request->completion_callback()->CompleteWithStatus(
              STATUS(Expired, "Transaction expired"));
          return;
        }
      }

      Modify(it).Handle(std::move(request));
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

  // Returns logs prefix for this transaction.
  const std::string& LogPrefix() {
    return log_prefix_;
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

  static TransactionState& Modify(const ManagedTransactions::iterator& it) {
    return const_cast<TransactionState&>(*it);
  }

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
        if (handle != rpcs_.InvalidHandle()) {
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
    }

    for (auto& update : actions->updates) {
      context_.SubmitUpdateTransaction(std::move(update));
    }
  }

  ManagedTransactions::iterator GetTransaction(const TransactionId& id,
                                               TransactionStatus status,
                                               HybridTime hybrid_time) {
    auto it = managed_transactions_.find(id);
    if (it == managed_transactions_.end()) {
      if (status != TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS) {
        it = managed_transactions_.emplace(this, id, hybrid_time, log_prefix_).first;
        VLOG_WITH_PREFIX(1) << Format("Added: $0", *it);
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
      auto status = STATUS(IllegalState, "Submit update transaction on non leader");
      VLOG_WITH_PREFIX(1) << status;
      state->completion_callback()->CompleteWithStatus(status);
      return false;
    }
  }

  Counter& expired_metric() override {
    return expired_metric_;
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
        LOG_WITH_PREFIX(INFO) << "Poll stopped: " << status;
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
        const_cast<TransactionState&>(transaction).Poll(leader);
      }
      postponed_leader_actions_.Swap(&actions);

      SchedulePoll();
    }
    ExecutePostponedLeaderActions(&actions);
  }

  void CheckCompleted(ManagedTransactions::iterator it) {
    if (it->Completed()) {
      auto status = STATUS_FORMAT(Aborted, "Transaction completed: $0", *it);
      VLOG_WITH_PREFIX(1) << status;
      Modify(it).ClearRequests(status);
      managed_transactions_.erase(it);
    }
  }

  TransactionCoordinatorContext& context_;
  TransactionParticipant* transaction_participant_;
  Counter& expired_metric_;
  const std::string log_prefix_;

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

TransactionCoordinator::TransactionCoordinator(const std::string& permanent_uuid,
                                               TransactionCoordinatorContext* context,
                                               TransactionParticipant* transaction_participant,
                                               Counter* expired_metric)
    : impl_(new Impl(permanent_uuid, context, transaction_participant, expired_metric)) {
}

TransactionCoordinator::~TransactionCoordinator() {
}

Status TransactionCoordinator::ProcessReplicated(const ReplicatedData& data) {
  return impl_->ProcessReplicated(data);
}

void TransactionCoordinator::ProcessAborted(const AbortedData& data) {
  impl_->ProcessAborted(data);
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

void TransactionCoordinator::ClearLocks(const Status& status) {
  impl_->ClearLocks(status);
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
