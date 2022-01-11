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

#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/transaction_cleanup.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_rpc.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction_error.h"
#include "yb/common/ybc_util.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_int32(txn_print_trace_every_n, 0,
             "Controls the rate at which txn traces are printed. Setting this to 0 "
             "disables printing the collected traces.");
TAG_FLAG(txn_print_trace_every_n, advanced);
TAG_FLAG(txn_print_trace_every_n, runtime);

DEFINE_int32(txn_slow_op_threshold_ms, 0,
             "Controls the rate at which txn traces are printed. Setting this to 0 "
             "disables printing the collected traces.");
TAG_FLAG(txn_slow_op_threshold_ms, advanced);
TAG_FLAG(txn_slow_op_threshold_ms, runtime);

DEFINE_uint64(transaction_heartbeat_usec, 500000 * yb::kTimeMultiplier,
              "Interval of transaction heartbeat in usec.");
DEFINE_bool(transaction_disable_heartbeat_in_tests, false, "Disable heartbeat during test.");
DECLARE_uint64(max_clock_skew_usec);

DEFINE_test_flag(int32, transaction_inject_flushed_delay_ms, 0,
                 "Inject delay before processing flushed operations by transaction.");

DEFINE_test_flag(bool, disable_proactive_txn_cleanup_on_abort, false,
                "Disable cleanup of intents in abort path.");

DECLARE_string(placement_cloud);
DECLARE_string(placement_region);

namespace yb {
namespace client {

namespace {

YB_STRONGLY_TYPED_BOOL(Child);
YB_DEFINE_ENUM(TransactionState, (kRunning)(kAborted)(kCommitted)(kReleased)(kSealed));

} // namespace

Result<ChildTransactionData> ChildTransactionData::FromPB(const ChildTransactionDataPB& data) {
  ChildTransactionData result;
  auto metadata = TransactionMetadata::FromPB(data.metadata());
  RETURN_NOT_OK(metadata);
  result.metadata = std::move(*metadata);
  result.read_time = ReadHybridTime::FromReadTimePB(data);
  for (const auto& entry : data.local_limits()) {
    result.local_limits.emplace(entry.first, HybridTime(entry.second));
  }
  return result;
}

YB_DEFINE_ENUM(MetadataState, (kMissing)(kMaybePresent)(kPresent));

YBSubTransaction::YBSubTransaction() {}

void YBSubTransaction::SetActiveSubTransaction(SubTransactionId id) {
  sub_txn_.subtransaction_id = id;
  highest_subtransaction_id_ = std::max(highest_subtransaction_id_, id);
}

CHECKED_STATUS YBSubTransaction::RollbackSubTransaction(SubTransactionId id) {
  // We should abort the range [id, sub_txn_.highest_subtransaction_id]. It's possible that we
  // have created and released savepoints, such that there have been writes with a
  // subtransaction_id greater than sub_txn_.subtransaction_id, and those should be aborted as
  // well.
  SCHECK_GE(
    highest_subtransaction_id_, id,
    InternalError,
    "Attempted to rollback to non-existent savepoint.");
  return sub_txn_.aborted.SetRange(id, highest_subtransaction_id_);
}

const SubTransactionMetadata& YBSubTransaction::get() { return sub_txn_; }

class YBTransaction::Impl final : public internal::TxnBatcherIf {
 public:
  Impl(TransactionManager* manager, YBTransaction* transaction, TransactionLocality locality)
      : trace_(new Trace),
        start_(CoarseMonoClock::Now()),
        manager_(manager),
        transaction_(transaction),
        read_point_(manager->clock()),
        child_(Child::kFalse) {
    metadata_.priority = RandomUniformInt<uint64_t>();
    metadata_.locality = locality;
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Started, metadata: " << metadata_;
  }

  Impl(TransactionManager* manager, YBTransaction* transaction, const TransactionMetadata& metadata)
      : trace_(new Trace),
        start_(CoarseMonoClock::Now()),
        manager_(manager),
        transaction_(transaction),
        metadata_(metadata),
        read_point_(manager->clock()),
        child_(Child::kFalse) {
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Taken, metadata: " << metadata_;
  }

  Impl(TransactionManager* manager, YBTransaction* transaction, ChildTransactionData data)
      : trace_(new Trace),
        start_(CoarseMonoClock::Now()),
        manager_(manager),
        transaction_(transaction),
        read_point_(manager->clock()),
        child_(Child::kTrue),
        child_had_read_time_(data.read_time) {
    // For serializable isolation we use read intents, so could always read most recent
    // version of DB.
    // Otherwise there is possible case when we miss value change that happened after transaction
    // start.
    if (data.metadata.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      read_point_.SetReadTime(std::move(data.read_time), std::move(data.local_limits));
    }
    metadata_ = std::move(data.metadata);
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Started child, metadata: " << metadata_;
    ready_ = true;
  }

  ~Impl() {
    manager_->rpcs().Abort({&heartbeat_handle_, &commit_handle_, &abort_handle_});
    LOG_IF_WITH_PREFIX(DFATAL, !waiters_.empty()) << "Non empty waiters";
    const auto threshold = GetAtomicFlag(&FLAGS_txn_slow_op_threshold_ms);
    const auto now = CoarseMonoClock::Now();
    if (trace_->must_print()
           || (threshold > 0 && ToMilliseconds(now - start_) > threshold)) {
      LOG(INFO) << ToString() << " took " << ToMicroseconds(now - start_) << "us. Trace: \n"
        << trace_->DumpToString(true);
    } else {
      YB_LOG_IF_EVERY_N(INFO, FLAGS_txn_print_trace_every_n > 0, FLAGS_txn_print_trace_every_n)
        << ToString() << " took " << ToMicroseconds(now - start_) << "us. Trace: \n"
        << trace_->DumpToString(true);
    }
  }

  void SetPriority(uint64_t priority) {
    metadata_.priority = priority;
  }

  uint64_t GetPriority() const {
    return metadata_.priority;
  }

  YBTransactionPtr CreateSimilarTransaction() {
    return std::make_shared<YBTransaction>(manager_);
  }

  CHECKED_STATUS Init(IsolationLevel isolation, const ReadHybridTime& read_time) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << __func__ << "(" << IsolationLevel_Name(isolation) << ", "
                        << read_time << ")";
    if (read_point_.GetReadTime().read.is_valid()) {
      return STATUS_FORMAT(IllegalState, "Read point already specified: $0",
                           read_point_.GetReadTime());
    }

    if (read_time.read.is_valid()) {
      read_point_.SetReadTime(read_time, ConsistentReadPoint::HybridTimeMap());
    }
    CompleteInit(isolation);
    return Status::OK();
  }

  void InitWithReadPoint(IsolationLevel isolation, ConsistentReadPoint&& read_point) {
    VLOG_WITH_PREFIX(1) << __func__ << "(" << IsolationLevel_Name(isolation) << ", "
                        << read_point.GetReadTime() << ")";

    read_point_.MoveFrom(&read_point);
    CompleteInit(isolation);
  }

  const IsolationLevel isolation() const {
    return metadata_.isolation;
  }

  // This transaction is a restarted transaction, so we set it up with data from original one.
  CHECKED_STATUS FillRestartedTransaction(Impl* other) EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX(1) << "Setup restart to " << other->ToString();
    auto transaction = transaction_->shared_from_this();
    TRACE_TO(trace_, __func__);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto state = state_.load(std::memory_order_acquire);
      if (state != TransactionState::kRunning) {
        return STATUS_FORMAT(
            IllegalState, "Restart of completed transaction $0: $1",
            metadata_.transaction_id, state);
      }
      if (!read_point_.IsRestartRequired()) {
        return STATUS_FORMAT(
            IllegalState, "Restart of transaction that does not require restart: $0",
            metadata_.transaction_id);
      }
      other->read_point_.MoveFrom(&read_point_);
      other->read_point_.Restart();
      other->metadata_.isolation = metadata_.isolation;
      if (metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
        other->metadata_.start_time = other->read_point_.GetReadTime().read;
      } else {
        other->metadata_.start_time = other->read_point_.Now();
      }
      state_.store(TransactionState::kAborted, std::memory_order_release);
    }
    DoAbort(TransactionRpcDeadline(), transaction);

    return Status::OK();
  }

  Trace* trace() {
    return trace_.get();
  }

  CHECKED_STATUS CheckTransactionLocality(internal::InFlightOpsGroupsWithMetadata* ops_info)
      REQUIRES(mutex_) {
    if (metadata_.locality != TransactionLocality::LOCAL) {
      return Status::OK();
    }
    for (auto& group : ops_info->groups) {
      auto& first_op = *group.begin;
      const auto& tablet = first_op.tablet;
      const auto& tablet_id = tablet->tablet_id();

      auto tservers = tablet->GetRemoteTabletServers(internal::IncludeFailedReplicas::kTrue);
      for (const auto &tserver : tservers) {
        auto pb = tserver->cloud_info();
        if (pb.placement_cloud() != FLAGS_placement_cloud ||
            pb.placement_region() != FLAGS_placement_region) {
          VLOG_WITH_PREFIX(4) << "Aborting (accessing nonlocal tablet in local transaction)";
          return STATUS_FORMAT(
              IllegalState, "Nonlocal tablet accessed in local transaction: tablet $0", tablet_id);
        }
      }
    }
    return Status::OK();
  }

  bool Prepare(internal::InFlightOpsGroupsWithMetadata* ops_info,
               ForceConsistentRead force_consistent_read,
               CoarseTimePoint deadline,
               Initial initial,
               Waiter waiter) override EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX(2) << "Prepare(" << AsString(ops_info->groups) << ", "
                        << force_consistent_read << ", " << initial << ")";
    TRACE_TO(trace_, "Perparing $0 ops", AsString(ops_info->groups.size()));
    VTRACE_TO(2, trace_, "Perparing $0 ops", AsString(ops_info->groups));

    {
      UNIQUE_LOCK(lock, mutex_);
      const bool defer = !ready_;

      if (!defer || initial) {
        Status status = CheckTransactionLocality(ops_info);
        if (!status.ok()) {
          bool abort = false;
          auto state = state_.load(std::memory_order_acquire);
          if (state == TransactionState::kRunning) {
            // State will be changed to aborted in SetErrorUnlocked
            abort = true;
          }
          SetErrorUnlocked(status);
          lock.unlock();
          if (waiter) {
            waiter(status);
          }
          if (abort) {
            DoAbort(TransactionRpcDeadline(), transaction_->shared_from_this());
          }
          return false;
        }
        for (auto& group : ops_info->groups) {
          auto& first_op = *group.begin;
          const auto should_add_intents = first_op.yb_op->should_add_intents(metadata_.isolation);
          const auto& tablet = first_op.tablet;
          const auto& tablet_id = tablet->tablet_id();

          bool has_metadata;
          if (initial && should_add_intents) {
            auto& tablet_state = tablets_[tablet_id];
            // TODO(dtxn) Handle skipped writes, i.e. writes that did not write anything (#3220)
            first_op.batch_idx = tablet_state.num_batches;
            ++tablet_state.num_batches;
            has_metadata = tablet_state.has_metadata;
          } else {
            const auto it = tablets_.find(tablet_id);
            has_metadata = it != tablets_.end() && it->second.has_metadata;
          }
          group.need_metadata = !has_metadata;
        }
      }

      if (defer) {
        if (waiter) {
          waiters_.push_back(std::move(waiter));
        }
        lock.unlock();
        VLOG_WITH_PREFIX(2) << "Prepare, rejected (not ready, requesting status tablet)";
        RequestStatusTablet(deadline);
        return false;
      }

      // For serializable isolation we never choose read time, since it always reads latest
      // snapshot.
      // For snapshot isolation, if read time was not yet picked, we have to choose it now, if there
      // multiple tablets that will process first request.
      SetReadTimeIfNeeded(ops_info->groups.size() > 1 || force_consistent_read);
    }

    ops_info->metadata = {
      .transaction = metadata_,
      .subtransaction = subtransaction_opt_ != boost::none
          ? boost::make_optional(subtransaction_opt_->get())
          : boost::none,
    };

    return true;
  }

  void ExpectOperations(size_t count) EXCLUDES(mutex_) override {
    std::lock_guard<std::mutex> lock(mutex_);
    running_requests_ += count;
  }

  void Flushed(
      const internal::InFlightOps& ops, const ReadHybridTime& used_read_time,
      const Status& status) EXCLUDES(mutex_) override {
    TRACE_TO(trace_, "Flushed $0 ops. with Status $1", ops.size(), status.ToString());
    VLOG_WITH_PREFIX(5)
        << "Flushed: " << yb::ToString(ops) << ", used_read_time: " << used_read_time
        << ", status: " << status;
    if (FLAGS_TEST_transaction_inject_flushed_delay_ms > 0) {
      std::this_thread::sleep_for(FLAGS_TEST_transaction_inject_flushed_delay_ms * 1ms);
    }

    boost::optional<Status> notify_commit_status;
    bool abort = false;

    CommitCallback commit_callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_requests_ -= ops.size();

      if (status.ok()) {
        if (used_read_time && metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
          const bool read_point_already_set = static_cast<bool>(read_point_.GetReadTime());
#ifndef NDEBUG
          if (read_point_already_set) {
            // Display details of operations before crashing in debug mode.
            int op_idx = 1;
            for (const auto& op : ops) {
              LOG(ERROR) << "Operation " << op_idx << ": " << op.ToString();
              op_idx++;
            }
          }
#endif
          LOG_IF_WITH_PREFIX(DFATAL, read_point_already_set)
              << "Read time already picked (" << read_point_.GetReadTime()
              << ", but server replied with used read time: " << used_read_time;
          read_point_.SetReadTime(used_read_time, ConsistentReadPoint::HybridTimeMap());
        }
        const std::string* prev_tablet_id = nullptr;
        for (const auto& op : ops) {
          if (op.yb_op->applied() && op.yb_op->should_add_intents(metadata_.isolation)) {
            const std::string& tablet_id = op.tablet->tablet_id();
            if (prev_tablet_id == nullptr || tablet_id != *prev_tablet_id) {
              prev_tablet_id = &tablet_id;
              tablets_[tablet_id].has_metadata = true;
            }
          }
        }
      } else {
        if (status.IsTryAgain()) {
          auto state = state_.load(std::memory_order_acquire);
          VLOG_WITH_PREFIX(4) << "Abort desired, state: " << AsString(state);
          if (state == TransactionState::kRunning) {
            abort = true;
            // State will be changed to aborted in SetError
          }
        }
        const TransactionError txn_err(status);
        if (txn_err.value() != TransactionErrorCode::kSkipLocking) {
          SetErrorUnlocked(status);
        }
      }

      if (running_requests_ == 0 && commit_replicated_) {
        notify_commit_status = status_;
        commit_callback = std::move(commit_callback_);
      }
    }

    if (notify_commit_status) {
      VLOG_WITH_PREFIX(4) << "Sealing done: " << *notify_commit_status;
      commit_callback(*notify_commit_status);
    }

    if (abort && !child_) {
      DoAbort(TransactionRpcDeadline(), transaction_->shared_from_this());
    }
  }

  void Commit(CoarseTimePoint deadline, SealOnly seal_only, CommitCallback callback)
      EXCLUDES(mutex_) {
    auto transaction = transaction_->shared_from_this();
    TRACE_TO(trace_, __func__);
    {
      UNIQUE_LOCK(lock, mutex_);
      auto status = CheckCouldCommitUnlocked(seal_only);
      if (!status.ok()) {
        lock.unlock();
        callback(status);
        return;
      }
      state_.store(seal_only ? TransactionState::kSealed : TransactionState::kCommitted,
                   std::memory_order_release);
      commit_callback_ = std::move(callback);
      if (!ready_) {
        // If we have not written any intents and do not even have a transaction status tablet,
        // just report the transaction as committed.
        //
        // See https://github.com/yugabyte/yugabyte-db/issues/3105 for details -- we might be able
        // to remove this special case if it turns out there is a bug elsewhere.
        if (tablets_.empty() && running_requests_ == 0) {
          VLOG_WITH_PREFIX(4) << "Committed empty transaction";
          auto commit_callback = std::move(commit_callback_);
          lock.unlock();
          commit_callback(Status::OK());
          return;
        }

        waiters_.emplace_back(std::bind(
            &Impl::DoCommit, this, deadline, seal_only, _1, transaction));
        lock.unlock();
        RequestStatusTablet(deadline);
        return;
      }
    }
    DoCommit(deadline, seal_only, Status::OK(), transaction);
  }

  void Abort(CoarseTimePoint deadline) EXCLUDES(mutex_) {
    auto transaction = transaction_->shared_from_this();

    VLOG_WITH_PREFIX(2) << "Abort";
    TRACE_TO(trace_, __func__);
    {
      UNIQUE_LOCK(lock, mutex_);
      auto state = state_.load(std::memory_order_acquire);
      if (state != TransactionState::kRunning) {
        LOG_IF_WITH_PREFIX(DFATAL, state != TransactionState::kAborted)
            << "Abort of committed transaction: " << AsString(state);
        return;
      }
      if (child_) {
        LOG_WITH_PREFIX(DFATAL) << "Abort of child transaction";
        return;
      }
      state_.store(TransactionState::kAborted, std::memory_order_release);
      if (!ready_) {
        std::vector<Waiter> waiters;
        waiters_.swap(waiters);
        lock.unlock();
        const auto aborted_status = STATUS(Aborted, "Transaction aborted");
        for(const auto& waiter : waiters) {
          waiter(aborted_status);
        }
        return;
      }
    }
    DoAbort(deadline, transaction);
  }

  bool IsRestartRequired() const {
    return read_point_.IsRestartRequired();
  }

  std::shared_future<Result<TransactionMetadata>> GetMetadata() EXCLUDES(mutex_) {
    UNIQUE_LOCK(lock, mutex_);
    if (metadata_future_.valid()) {
      return metadata_future_;
    }
    metadata_future_ = std::shared_future<Result<TransactionMetadata>>(
        metadata_promise_.get_future());
    if (!ready_) {
      auto transaction = transaction_->shared_from_this();
      waiters_.push_back([this, transaction](const Status& status) {
        WARN_NOT_OK(status, "Transaction request failed");
        if (status.ok()) {
          metadata_promise_.set_value(metadata_);
        } else {
          metadata_promise_.set_value(status);
        }
      });
      lock.unlock();
      RequestStatusTablet(TransactionRpcDeadline());
      lock.lock();
      return metadata_future_;
    }

    metadata_promise_.set_value(metadata_);
    return metadata_future_;
  }

  void PrepareChild(
      ForceConsistentRead force_consistent_read, CoarseTimePoint deadline,
      PrepareChildCallback callback) {
    auto transaction = transaction_->shared_from_this();
    TRACE_TO(trace_, __func__);
    UNIQUE_LOCK(lock, mutex_);
    auto status = CheckRunningUnlocked();
    if (!status.ok()) {
      lock.unlock();
      callback(status);
      return;
    }
    if (IsRestartRequired()) {
      lock.unlock();
      callback(STATUS(IllegalState, "Restart required"));
      return;
    }

    SetReadTimeIfNeeded(force_consistent_read);

    if (!ready_) {
      waiters_.emplace_back(std::bind(
          &Impl::DoPrepareChild, this, _1, transaction, std::move(callback)));
      lock.unlock();
      RequestStatusTablet(deadline);
      return;
    }

    ChildTransactionDataPB child_txn_data_pb = PrepareChildTransactionDataUnlocked(transaction);
    lock.unlock();
    callback(child_txn_data_pb);
  }

  Result<ChildTransactionResultPB> FinishChild() {
    TRACE_TO(trace_, __func__);
    UNIQUE_LOCK(lock, mutex_);
    RETURN_NOT_OK(CheckRunningUnlocked());
    if (!child_) {
      return STATUS(IllegalState, "Finish child of non child transaction");
    }
    state_.store(TransactionState::kCommitted, std::memory_order_release);
    ChildTransactionResultPB result;
    auto& tablets = *result.mutable_tablets();
    tablets.Reserve(narrow_cast<int>(tablets_.size()));
    for (const auto& tablet : tablets_) {
      auto& out = *tablets.Add();
      out.set_tablet_id(tablet.first);
      out.set_num_batches(tablet.second.num_batches);
      out.set_metadata_state(
          tablet.second.has_metadata ? InvolvedTabletMetadataState::EXIST
                                     : InvolvedTabletMetadataState::MISSING);
    }
    read_point_.FinishChildTransactionResult(HadReadTime(child_had_read_time_), &result);
    return result;
  }

  Status ApplyChildResult(const ChildTransactionResultPB& result) EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    std::vector<std::string> cleanup_tablet_ids;
    auto se = ScopeExit([this, &cleanup_tablet_ids] {
      if (cleanup_tablet_ids.empty()) {
        return;
      }
      CleanupTransaction(
          manager_->client(), manager_->clock(), metadata_.transaction_id, Sealed::kFalse,
          CleanupType::kImmediate, cleanup_tablet_ids);
    });
    UNIQUE_LOCK(lock, mutex_);
    if (state_.load(std::memory_order_acquire) == TransactionState::kAborted) {
      cleanup_tablet_ids.reserve(result.tablets().size());
      for (const auto& tablet : result.tablets()) {
        cleanup_tablet_ids.push_back(tablet.tablet_id());
      }
    }

    RETURN_NOT_OK(CheckRunningUnlocked());
    if (child_) {
      return STATUS(IllegalState, "Apply child result of child transaction");
    }

    for (const auto& tablet : result.tablets()) {
      auto& tablet_state = tablets_[tablet.tablet_id()];
      tablet_state.num_batches += tablet.num_batches();
      tablet_state.has_metadata =
          tablet_state.has_metadata ||
          tablet.metadata_state() == InvolvedTabletMetadataState::EXIST;
    }
    read_point_.ApplyChildTransactionResult(result);

    return Status::OK();
  }

  const std::string& LogPrefix() {
    return log_prefix_;
  }

  std::string ToString() EXCLUDES(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    return Format("{ metadata: $0 state: $1 }", metadata_, state_.load(std::memory_order_acquire));
  }

  const TransactionId& id() const {
    return metadata_.transaction_id;
  }

  ConsistentReadPoint& read_point() {
    return read_point_;
  }

  Result<TransactionMetadata> Release() {
    UNIQUE_LOCK(lock, mutex_);
    auto state = state_.load(std::memory_order_acquire);
    if (state != TransactionState::kRunning) {
      return STATUS_FORMAT(IllegalState, "Attempt to release transaction in the wrong state $0: $1",
                           metadata_.transaction_id, AsString(state));
    }
    state_.store(TransactionState::kReleased, std::memory_order_release);

    if (!ready_) {
      CountDownLatch latch(1);
      Status pick_status;
      auto transaction = transaction_->shared_from_this();
      waiters_.push_back([&latch, &pick_status](const Status& status) {
        pick_status = status;
        latch.CountDown();
      });
      lock.unlock();
      RequestStatusTablet(TransactionRpcDeadline());
      latch.Wait();
      RETURN_NOT_OK(pick_status);
      lock.lock();
    }
    return metadata_;
  }

  void StartHeartbeat() {
    VLOG_WITH_PREFIX(2) << __PRETTY_FUNCTION__;
    RequestStatusTablet(TransactionRpcDeadline());
  }

  void SetActiveSubTransaction(SubTransactionId id) {
    if (subtransaction_opt_ == boost::none) {
      subtransaction_opt_ = boost::make_optional(YBSubTransaction());
    }
    return subtransaction_opt_->SetActiveSubTransaction(id);
  }

  CHECKED_STATUS RollbackSubTransaction(SubTransactionId id) {
    SCHECK(
        subtransaction_opt_ != boost::none, InternalError,
        "Attempted to rollback to savepoint before creating any savepoints.");
    return subtransaction_opt_->RollbackSubTransaction(id);
  }

  bool HasSubTransactionState() {
    return subtransaction_opt_ != boost::none;
  }

 private:
  void CompleteConstruction() {
    log_prefix_ = Format("$0$1: ", metadata_.transaction_id, child_ ? " (CHILD)" : "");
    heartbeat_handle_ = manager_->rpcs().InvalidHandle();
    commit_handle_ = manager_->rpcs().InvalidHandle();
    abort_handle_ = manager_->rpcs().InvalidHandle();
  }

  void CompleteInit(IsolationLevel isolation) {
    metadata_.isolation = isolation;
    // TODO(Piyush): read_point_ might not represent the correct start time for
    // a READ COMMITTED txn since it might have been updated several times
    // before a YBTransaction is created. Fix this.
    if (read_point_.GetReadTime()) {
      metadata_.start_time = read_point_.GetReadTime().read;
    } else {
      metadata_.start_time = read_point_.Now();
    }
  }

  void SetReadTimeIfNeeded(bool do_it) {
    if (!read_point_.GetReadTime() && do_it &&
        metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
      read_point_.SetCurrentReadTime();
    }
  }

  CHECKED_STATUS CheckRunningUnlocked() REQUIRES(mutex_) {
    if (state_.load(std::memory_order_acquire) != TransactionState::kRunning) {
      auto status = status_;
      if (status.ok()) {
        status = STATUS(IllegalState, "Transaction already completed");
      }
      return status;
    }
    return Status::OK();
  }

  void DoCommit(
      CoarseTimePoint deadline, SealOnly seal_only, const Status& status,
      const YBTransactionPtr& transaction) EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX(1)
        << Format("Commit, seal_only: $0, tablets: $1, status: $2",
                  seal_only, tablets_, status);

    UNIQUE_LOCK(lock, mutex_);
    if (!status.ok()) {
      VLOG_WITH_PREFIX(4) << "Commit failed: " << status;
      auto commit_callback = std::move(commit_callback_);
      lock.unlock();
      commit_callback(status);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
    // TODO(savepoints) -- Attach metadata about aborted subtransactions to commit message.
    state.set_status(seal_only ? TransactionStatus::SEALED : TransactionStatus::COMMITTED);
    state.mutable_tablets()->Reserve(narrow_cast<int>(tablets_.size()));
    for (const auto& tablet : tablets_) {
      // If tablet does not have metadata it should not participate in commit.
      if (!seal_only && !tablet.second.has_metadata) {
        continue;
      }
      state.add_tablets(tablet.first);
      if (seal_only) {
        state.add_tablet_batches(tablet.second.num_batches);
      }
    }

    // If we don't have any tablets that have intents written to them, just abort it.
    // But notify caller that commit was successful, so it is transparent for him.
    if (state.tablets().empty()) {
      VLOG_WITH_PREFIX(4) << "Committed empty";
      auto status_tablet = status_tablet_;
      auto commit_callback = std::move(commit_callback_);
      lock.unlock();
      DoAbort(deadline, transaction, status_tablet);
      commit_callback(Status::OK());
      return;
    }

    if (subtransaction_opt_ != boost::none) {
      subtransaction_opt_->get().aborted.ToPB(state.mutable_aborted()->mutable_set());
    }

    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            deadline,
            status_tablet_.get(),
            manager_->client(),
            &req,
            [this, transaction](const auto& status, const auto& req, const auto& resp) {
              this->CommitDone(status, resp, transaction);
            }),
        &commit_handle_);
  }

  void DoAbort(CoarseTimePoint deadline, const YBTransactionPtr& transaction) EXCLUDES(mutex_) {
    decltype(status_tablet_) status_tablet;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet = status_tablet_;
    }
    DoAbort(deadline, transaction, status_tablet);
  }

  void DoAbort(
      CoarseTimePoint deadline,
      const YBTransactionPtr& transaction,
      internal::RemoteTabletPtr status_tablet) EXCLUDES(mutex_) {
    tserver::AbortTransactionRequestPB req;
    req.set_tablet_id(status_tablet->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    req.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());

    manager_->rpcs().RegisterAndStart(
        AbortTransaction(
            deadline,
            status_tablet.get(),
            manager_->client(),
            &req,
            std::bind(&Impl::AbortDone, this, _1, _2, transaction)),
        &abort_handle_);

    DoAbortCleanup(transaction, CleanupType::kImmediate);
  }

  void DoAbortCleanup(const YBTransactionPtr& transaction, CleanupType cleanup_type)
      EXCLUDES(mutex_) {
    if (FLAGS_TEST_disable_proactive_txn_cleanup_on_abort) {
      return;
    }

    VLOG_WITH_PREFIX(1) << "Cleaning up intents";

    std::vector<std::string> tablet_ids;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tablet_ids.reserve(tablets_.size());
      for (const auto& tablet : tablets_) {
        // We don't check has_metadata here, because intents could be written even in case of
        // failure. For instance in case of conflict on unique index.
        tablet_ids.push_back(tablet.first);
      }
    }

    CleanupTransaction(
        manager_->client(), manager_->clock(), metadata_.transaction_id, Sealed::kFalse,
        cleanup_type, tablet_ids);
  }

  void CommitDone(const Status& status,
                  const tserver::UpdateTransactionResponsePB& response,
                  const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Committed: " << status;

    UpdateClock(response, manager_);
    manager_->rpcs().Unregister(&commit_handle_);

    Status actual_status = status.IsAlreadyPresent() ? Status::OK() : status;
    CommitCallback commit_callback;
    if (state_.load(std::memory_order_acquire) != TransactionState::kCommitted &&
        actual_status.ok()) {
      std::lock_guard<std::mutex> lock(mutex_);
      commit_replicated_ = true;
      if (running_requests_ != 0) {
        return;
      }
      commit_callback = std::move(commit_callback_);
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      commit_callback = std::move(commit_callback_);
    }
    VLOG_WITH_PREFIX(4) << "Commit done: " << actual_status;
    commit_callback(actual_status);

    if (actual_status.IsExpired()) {
      // We can't perform immediate cleanup here because the transaction could be committed,
      // its APPLY records replicated in all participant tablets, and its status record removed
      // from the status tablet.
      DoAbortCleanup(transaction, CleanupType::kGraceful);
    }
  }

  void AbortDone(const Status& status,
                 const tserver::AbortTransactionResponsePB& response,
                 const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Aborted: " << status;

    if (response.has_propagated_hybrid_time()) {
      manager_->UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }
    manager_->rpcs().Unregister(&abort_handle_);
  }

  void RequestStatusTablet(const CoarseTimePoint& deadline) EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    bool expected = false;
    if (!requested_status_tablet_.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel)) {
      return;
    }
    VLOG_WITH_PREFIX(2) << "RequestStatusTablet()";
    auto transaction = transaction_->shared_from_this();
    if (metadata_.status_tablet.empty()) {
      manager_->PickStatusTablet(
          std::bind(&Impl::StatusTabletPicked, this, _1, deadline, transaction),
          metadata_.locality);
    } else {
      LookupStatusTablet(metadata_.status_tablet, deadline, transaction);
    }
  }

  void StatusTabletPicked(const Result<std::string>& tablet,
                          const CoarseTimePoint& deadline,
                          const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(2) << "Picked status tablet: " << tablet;

    if (!tablet.ok()) {
      NotifyWaiters(tablet.status());
      return;
    }

    LookupStatusTablet(*tablet, deadline, transaction);
  }

  void LookupStatusTablet(const std::string& tablet_id,
                          const CoarseTimePoint& deadline,
                          const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);
    manager_->client()->LookupTabletById(
        tablet_id,
        /* table =*/ nullptr,
        master::IncludeInactive::kFalse,
        deadline,
        std::bind(&Impl::LookupTabletDone, this, _1, transaction),
        client::UseCache::kTrue);
  }

  void LookupTabletDone(const Result<client::internal::RemoteTabletPtr>& result,
                        const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << yb::ToString(result);

    if (!result.ok()) {
      NotifyWaiters(result.status());
      return;
    }

    bool precreated;
    std::vector<Waiter> waiters;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet_ = std::move(*result);
      if (metadata_.status_tablet.empty()) {
        metadata_.status_tablet = status_tablet_->tablet_id();
        precreated = false;
      } else {
        precreated = true;
        ready_ = true;
        waiters_.swap(waiters);
      }
    }
    if (precreated) {
      for (const auto& waiter : waiters) {
        waiter(Status::OK());
      }
    }
    SendHeartbeat(precreated ? TransactionStatus::PENDING : TransactionStatus::CREATED,
                  metadata_.transaction_id, transaction_->shared_from_this());
  }

  void NotifyWaiters(const Status& status) {
    std::vector<Waiter> waiters;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (status.ok()) {
        DCHECK(!ready_);
        ready_ = true;
      } else {
        SetErrorUnlocked(status);
      }
      waiters_.swap(waiters);
    }
    for (const auto& waiter : waiters) {
      waiter(status);
    }
  }

  void SendHeartbeat(TransactionStatus status,
                     const TransactionId& id,
                     const std::weak_ptr<YBTransaction>& weak_transaction) {
    auto transaction = weak_transaction.lock();
    if (!transaction) {
      // Cannot use LOG_WITH_PREFIX here, since this was actually destroyed.
      VLOG(1) << id << ": Transaction destroyed";
      return;
    }

    auto current_state = state_.load(std::memory_order_acquire);

    if (!AllowHeartbeat(current_state, status)) {
      VLOG_WITH_PREFIX(1) << " Send heartbeat cancelled: " << yb::ToString(transaction);
      return;
    }

    VLOG_WITH_PREFIX(4) << __func__ << "(" << TransactionStatus_Name(status) << ")";

    MonoDelta timeout;
    if (status != TransactionStatus::CREATED) {
      if (GetAtomicFlag(&FLAGS_transaction_disable_heartbeat_in_tests)) {
        HeartbeatDone(Status::OK(), /* request= */ {}, /* response= */ {}, status, transaction);
        return;
      }
      timeout = std::chrono::microseconds(FLAGS_transaction_heartbeat_usec);
    } else {
      timeout = TransactionRpcTimeout();
    }

    tserver::UpdateTransactionRequestPB req;

    internal::RemoteTabletPtr status_tablet;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_tablet = status_tablet_;
    }

    req.set_tablet_id(status_tablet->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    // TODO(savepoints) -- Attach metadata about aborted subtransactions in heartbeat.
    state.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
    state.set_status(status);
    manager_->rpcs().RegisterAndStart(
        UpdateTransaction(
            CoarseMonoClock::now() + timeout,
            status_tablet.get(),
            manager_->client(),
            &req,
            std::bind(&Impl::HeartbeatDone, this, _1, _2, _3, status, transaction)),
        &heartbeat_handle_);
  }

  static bool AllowHeartbeat(TransactionState current_state, TransactionStatus status) {
    switch (current_state) {
      case TransactionState::kRunning:
        return true;
      case TransactionState::kReleased: FALLTHROUGH_INTENDED;
      case TransactionState::kSealed:
        return status == TransactionStatus::CREATED;
      case TransactionState::kAborted: FALLTHROUGH_INTENDED;
      case TransactionState::kCommitted:
        return false;
    }
    FATAL_INVALID_ENUM_VALUE(TransactionState, current_state);
  }

  void HeartbeatDone(Status status,
                     const tserver::UpdateTransactionRequestPB& request,
                     const tserver::UpdateTransactionResponsePB& response,
                     TransactionStatus transaction_status,
                     const YBTransactionPtr& transaction) {
    UpdateClock(response, manager_);
    manager_->rpcs().Unregister(&heartbeat_handle_);

    if (status.ok() && transaction_status == TransactionStatus::CREATED) {
      auto decode_result = FullyDecodeTransactionId(request.state().transaction_id());
      if (decode_result.ok()) {
        metadata_.transaction_id = *decode_result;
        auto id_str = AsString(metadata_.transaction_id);
        // It is not fully thread safe, since we don't use mutex to access log_prefix_.
        // But here we just replace characters inplace.
        // It would not crash anyway, and could produce wrong id in the logs.
        // It is ok, since one moment before we would output nil id.
        log_prefix_.replace(0, id_str.length(), id_str);
      } else {
        status = decode_result.status();
      }
    }

    VLOG_WITH_PREFIX(4) << __func__ << "(" << status << ", "
                        << TransactionStatus_Name(transaction_status) << ")";

    if (status.ok()) {
      if (transaction_status == TransactionStatus::CREATED) {
        NotifyWaiters(Status::OK());
      }
      std::weak_ptr<YBTransaction> weak_transaction(transaction);
      manager_->client()->messenger()->scheduler().Schedule(
          [this, weak_transaction, id = metadata_.transaction_id](const Status&) {
              SendHeartbeat(TransactionStatus::PENDING, id, weak_transaction);
          },
          std::chrono::microseconds(FLAGS_transaction_heartbeat_usec));
    } else {
      auto state = state_.load(std::memory_order_acquire);
      LOG_WITH_PREFIX(WARNING) << "Send heartbeat failed: " << status << ", state: " << state;
      if (status.IsAborted()) {
        // Service is shutting down, no reason to retry.
        SetError(status);
        if (transaction_status == TransactionStatus::CREATED) {
          NotifyWaiters(status);
        }
        return;
      }
      if (status.IsExpired()) {
        SetError(status);
        // If state is committed, then we should not cleanup.
        if (state == TransactionState::kRunning) {
          DoAbortCleanup(transaction, CleanupType::kImmediate);
        }
        if (transaction_status == TransactionStatus::CREATED) {
          NotifyWaiters(status);
        }
        return;
      }
      // Other errors could have different causes, but we should just retry sending heartbeat
      // in this case.
      SendHeartbeat(transaction_status, metadata_.transaction_id, transaction);
    }
  }

  void SetError(const Status& status) EXCLUDES(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    SetErrorUnlocked(status);
  }

  void SetErrorUnlocked(const Status& status) REQUIRES(mutex_) {
    VLOG_WITH_PREFIX(1) << "Failed: " << status;
    if (status_.ok()) {
      status_ = status;
      state_.store(TransactionState::kAborted, std::memory_order_release);
    }
  }

  ChildTransactionDataPB PrepareChildTransactionDataUnlocked(
      const YBTransactionPtr& transaction) REQUIRES(mutex_) {
    ChildTransactionDataPB data;
    metadata_.ToPB(data.mutable_metadata());
    read_point_.PrepareChildTransactionData(&data);
    return data;
  }

  void DoPrepareChild(const Status& status,
                      const YBTransactionPtr& transaction,
                      PrepareChildCallback callback) EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    if (!status.ok()) {
      callback(status);
      return;
    }

    ChildTransactionDataPB child_txn_data_pb;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      child_txn_data_pb = PrepareChildTransactionDataUnlocked(transaction);
    }

    callback(child_txn_data_pb);
  }

  CHECKED_STATUS CheckCouldCommitUnlocked(SealOnly seal_only) REQUIRES(mutex_) {
    RETURN_NOT_OK(CheckRunningUnlocked());
    if (child_) {
      return STATUS(IllegalState, "Commit of child transaction is not allowed");
    }
    if (IsRestartRequired()) {
      return STATUS(
          IllegalState, "Commit of transaction that requires restart is not allowed");
    }
    if (!seal_only && running_requests_ > 0) {
      return STATUS(IllegalState, "Commit of transaction with running requests");
    }

    return Status::OK();
  }

  // The trace buffer.
  scoped_refptr<Trace> trace_;

  const CoarseTimePoint start_;

  // Manager is created once per service.
  TransactionManager* const manager_;

  // Transaction related to this impl.
  YBTransaction* const transaction_;

  TransactionMetadata metadata_;
  ConsistentReadPoint read_point_;

  // Metadata tracking savepoint-related state for the scope of this transaction.
  boost::optional<YBSubTransaction> subtransaction_opt_ = boost::none;

  std::atomic<bool> requested_status_tablet_{false};
  internal::RemoteTabletPtr status_tablet_ GUARDED_BY(mutex_);
  std::atomic<TransactionState> state_{TransactionState::kRunning};

  // Transaction is successfully initialized and ready to process intents.
  const bool child_;
  const bool child_had_read_time_ = false;
  bool ready_ GUARDED_BY(mutex_) = false;
  CommitCallback commit_callback_ GUARDED_BY(mutex_);
  Status status_ GUARDED_BY(mutex_);

  // The following fields are initialized in CompleteConstruction() and can be used with no locking.
  std::string log_prefix_;
  rpc::Rpcs::Handle heartbeat_handle_;
  rpc::Rpcs::Handle commit_handle_;
  rpc::Rpcs::Handle abort_handle_;

  struct TabletState {
    size_t num_batches = 0;
    bool has_metadata = false;

    std::string ToString() const {
      return Format("{ num_batches: $0 has_metadata: $1 }", num_batches, has_metadata);
    }
  };

  typedef std::unordered_map<TabletId, TabletState> TabletStates;

  std::mutex mutex_;
  TabletStates tablets_ GUARDED_BY(mutex_);
  std::vector<Waiter> waiters_;
  std::promise<Result<TransactionMetadata>> metadata_promise_;
  std::shared_future<Result<TransactionMetadata>> metadata_future_ GUARDED_BY(mutex_);
  // As of 2021-04-05 running_requests_ reflects number of ops in progress within this transaction
  // only if no in-transaction operations have failed.
  // If in-transaction operation has failed during tablet lookup or it has failed and will be
  // retried by YBSession inside the same transaction - Transaction::Flushed is not getting called
  // and running_requests_ is not updated.
  // For YBSession-level retries Transaction::Flushed will be called when operation is finally
  // successfully flushed, if operation fails after retry - Transaction::Flushed is not getting
  // called.
  // We might need to fix this before turning on transactions sealing.
  // https://github.com/yugabyte/yugabyte-db/issues/7984.
  size_t running_requests_ GUARDED_BY(mutex_) = 0;
  // Set to true after commit record is replicated. Used only during transaction sealing.
  bool commit_replicated_ = false;
};

CoarseTimePoint AdjustDeadline(CoarseTimePoint deadline) {
  if (deadline == CoarseTimePoint()) {
    return TransactionRpcDeadline();
  }
  return deadline;
}

YBTransaction::YBTransaction(TransactionManager* manager, TransactionLocality locality)
    : impl_(new Impl(manager, this, locality)) {
}

YBTransaction::YBTransaction(
    TransactionManager* manager, const TransactionMetadata& metadata, PrivateOnlyTag)
    : impl_(new Impl(manager, this, metadata)) {
}

YBTransaction::YBTransaction(TransactionManager* manager, ChildTransactionData data)
    : impl_(new Impl(manager, this, std::move(data))) {
}

YBTransaction::~YBTransaction() {
}

void YBTransaction::SetPriority(uint64_t priority) {
  impl_->SetPriority(priority);
}

uint64_t YBTransaction::GetPriority() const {
  return impl_->GetPriority();
}

Status YBTransaction::Init(IsolationLevel isolation, const ReadHybridTime& read_time) {
  return impl_->Init(isolation, read_time);
}

void YBTransaction::InitWithReadPoint(
    IsolationLevel isolation,
    ConsistentReadPoint&& read_point) {
  return impl_->InitWithReadPoint(isolation, std::move(read_point));
}

internal::TxnBatcherIf& YBTransaction::batcher_if() {
  return *impl_;
}

void YBTransaction::Commit(
    CoarseTimePoint deadline, SealOnly seal_only, CommitCallback callback) {
  impl_->Commit(AdjustDeadline(deadline), seal_only, std::move(callback));
}

void YBTransaction::Commit(CoarseTimePoint deadline, CommitCallback callback) {
  Commit(deadline, SealOnly::kFalse, callback);
}

void YBTransaction::Commit(CommitCallback callback) {
  Commit(CoarseTimePoint(), SealOnly::kFalse, std::move(callback));
}

const TransactionId& YBTransaction::id() const {
  return impl_->id();
}

IsolationLevel YBTransaction::isolation() const {
  return impl_->isolation();
}

const ConsistentReadPoint& YBTransaction::read_point() const {
  return impl_->read_point();
}

ConsistentReadPoint& YBTransaction::read_point() {
  return impl_->read_point();
}

std::future<Status> YBTransaction::CommitFuture(
    CoarseTimePoint deadline, SealOnly seal_only) {
  return MakeFuture<Status>([this, deadline, seal_only](auto callback) {
    impl_->Commit(AdjustDeadline(deadline), seal_only, std::move(callback));
  });
}

void YBTransaction::Abort(CoarseTimePoint deadline) {
  impl_->Abort(AdjustDeadline(deadline));
}

bool YBTransaction::IsRestartRequired() const {
  return impl_->IsRestartRequired();
}

Result<YBTransactionPtr> YBTransaction::CreateRestartedTransaction() {
  auto result = impl_->CreateSimilarTransaction();
  RETURN_NOT_OK(impl_->FillRestartedTransaction(result->impl_.get()));
  return result;
}

Status YBTransaction::FillRestartedTransaction(const YBTransactionPtr& dest) {
  return impl_->FillRestartedTransaction(dest->impl_.get());
}

void YBTransaction::PrepareChild(
    ForceConsistentRead force_consistent_read, CoarseTimePoint deadline,
    PrepareChildCallback callback) {
  return impl_->PrepareChild(force_consistent_read, deadline, std::move(callback));
}

std::future<Result<ChildTransactionDataPB>> YBTransaction::PrepareChildFuture(
    ForceConsistentRead force_consistent_read, CoarseTimePoint deadline) {
  return MakeFuture<Result<ChildTransactionDataPB>>(
      [this, deadline, force_consistent_read](auto callback) {
    impl_->PrepareChild(force_consistent_read, AdjustDeadline(deadline), std::move(callback));
  });
}

Result<ChildTransactionResultPB> YBTransaction::FinishChild() {
  return impl_->FinishChild();
}

std::shared_future<Result<TransactionMetadata>> YBTransaction::GetMetadata() const {
  return impl_->GetMetadata();
}

Status YBTransaction::ApplyChildResult(const ChildTransactionResultPB& result) {
  return impl_->ApplyChildResult(result);
}

std::string YBTransaction::ToString() const {
  return impl_->ToString();
}

Result<TransactionMetadata> YBTransaction::Release() {
  return impl_->Release();
}

Trace* YBTransaction::trace() {
  return impl_->trace();
}

YBTransactionPtr YBTransaction::Take(
    TransactionManager* manager, const TransactionMetadata& metadata) {
  auto result = std::make_shared<YBTransaction>(manager, metadata, PrivateOnlyTag());
  result->impl_->StartHeartbeat();
  return result;
}

void YBTransaction::SetActiveSubTransaction(SubTransactionId id) {
  return impl_->SetActiveSubTransaction(id);
}

Status YBTransaction::RollbackSubTransaction(SubTransactionId id) {
  return impl_->RollbackSubTransaction(id);
}

bool YBTransaction::HasSubTransactionState() {
  return impl_->HasSubTransactionState();
}

} // namespace client
} // namespace yb
