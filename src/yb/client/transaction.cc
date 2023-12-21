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

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <boost/atomic.hpp>

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

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/scheduler.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/unique_lock.h"

using std::vector;

using namespace std::literals;
using namespace std::placeholders;

DEFINE_RUNTIME_int32(txn_print_trace_every_n, 0,
    "Controls the rate at which txn traces are printed. Setting this to 0 "
    "disables printing the collected traces.");
TAG_FLAG(txn_print_trace_every_n, advanced);

DEFINE_RUNTIME_int32(txn_slow_op_threshold_ms, 0,
    "Controls the rate at which txn traces are printed. Setting this to 0 "
    "disables printing the collected traces.");
TAG_FLAG(txn_slow_op_threshold_ms, advanced);

DEFINE_RUNTIME_bool(txn_print_trace_on_error, false,
    "Controls whether to always print txn traces on error.");
TAG_FLAG(txn_print_trace_on_error, advanced);

DEFINE_UNKNOWN_uint64(transaction_heartbeat_usec, 500000 * yb::kTimeMultiplier,
              "Interval of transaction heartbeat in usec.");
DEFINE_UNKNOWN_bool(transaction_disable_heartbeat_in_tests, false,
    "Disable heartbeat during test.");
DECLARE_uint64(max_clock_skew_usec);

DEFINE_UNKNOWN_bool(auto_promote_nonlocal_transactions_to_global, true,
            "Automatically promote transactions touching data outside of region to global.");

DEFINE_RUNTIME_bool(log_failed_txn_metadata, false, "Log metadata about failed transactions.");
TAG_FLAG(log_failed_txn_metadata, advanced);

DEFINE_test_flag(int32, transaction_inject_flushed_delay_ms, 0,
                 "Inject delay before processing flushed operations by transaction.");

DEFINE_test_flag(bool, disable_proactive_txn_cleanup_on_abort, false,
                "Disable cleanup of intents in abort path.");

DEFINE_test_flag(int32, txn_status_moved_rpc_send_delay_ms, 0,
                 "Inject delay before sending UpdateTransactionStatusLocation RPCs to participant "
                 "tablets.");

DEFINE_test_flag(int32, old_txn_status_abort_delay_ms, 0,
                 "Inject delay before sending abort to old transaction status tablet.");

DEFINE_test_flag(int32, new_txn_status_initial_heartbeat_delay_ms, 0,
                 "Inject delay before sending initial heartbeat to new transaction status tablet.");

DEFINE_test_flag(uint64, override_transaction_priority, 0,
                 "Override priority of transactions if nonzero.");

DEFINE_RUNTIME_bool(disable_heartbeat_send_involved_tablets, false,
                    "If disabled, do not send involved tablets on heartbeats for pending "
                    "transactions. This behavior is needed to support fetching old transactions "
                    "and their involved tablets in order to support yb_lock_status/pg_locks.");

METRIC_DEFINE_counter(server, transaction_promotions,
                      "Number of transactions being promoted to global transactions",
                      yb::MetricUnit::kTransactions,
                      "Number of transactions being promoted to global transactions");

namespace yb {
namespace client {

namespace {

YB_STRONGLY_TYPED_BOOL(Child);
YB_STRONGLY_TYPED_BOOL(SendHeartbeatToNewTablet);
YB_STRONGLY_TYPED_BOOL(SetReady);
YB_STRONGLY_TYPED_BOOL(TransactionPromoting);
YB_DEFINE_ENUM(TransactionState, (kRunning)(kAborted)(kCommitted)(kReleased)(kSealed)(kPromoting));
YB_DEFINE_ENUM(OldTransactionState, (kRunning)(kAborting)(kAborted)(kNone));

struct LogPrefixTag {
  explicit LogPrefixTag(const std::string* name_ = nullptr, uint64_t value_ = 0)
      : name(name_), value(value_)
  {}

  const std::string* name;
  uint64_t value;
};

static_assert(sizeof(LogPrefixTag) == 2 * sizeof(uint64_t));

struct TaggedLogPrefix {
  std::string prefix;
  boost::atomic<LogPrefixTag> tag;
};

std::ostream& operator<<(std::ostream& str, const TaggedLogPrefix& value) {
  const auto tag = value.tag.load(boost::memory_order_acquire);
  str << value.prefix;
  if (tag.name) {
    str << " [" << *tag.name << tag.value << "]";
  }
  return str << ": ";
}

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

std::string YBSubTransaction::ToString() const {
  return Format(
      "{ sub_txn_: $0 highest_subtransaction_id_: $1 }", sub_txn_, highest_subtransaction_id_);
}

bool YBSubTransaction::operator==(const YBSubTransaction& other) const {
  return highest_subtransaction_id_ == other.highest_subtransaction_id_ &&
      sub_txn_ == other.sub_txn_;
}

void YBSubTransaction::SetActiveSubTransaction(SubTransactionId id) {
  sub_txn_.subtransaction_id = id;
  highest_subtransaction_id_ = std::max(highest_subtransaction_id_, id);
}

bool YBSubTransaction::HasSubTransaction(SubTransactionId id) const {
  // See the condition in YBSubTransaction::RollbackToSubTransaction.
  return highest_subtransaction_id_ >= id;
}

Status YBSubTransaction::RollbackToSubTransaction(SubTransactionId id) {
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

const SubTransactionMetadata& YBSubTransaction::get() const { return sub_txn_; }

class YBTransaction::Impl final : public internal::TxnBatcherIf {
 public:
  Impl(TransactionManager* manager, YBTransaction* transaction, TransactionLocality locality)
      : trace_(Trace::MaybeGetNewTrace()),
        manager_(manager),
        transaction_(transaction),
        read_point_(manager->clock()),
        child_(Child::kFalse) {
    metadata_.priority =
        PREDICT_FALSE(FLAGS_TEST_override_transaction_priority != 0)
            ? FLAGS_TEST_override_transaction_priority
            : RandomUniformInt<uint64_t>();
    metadata_.locality = locality;
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Started, metadata: " << metadata_;
  }

  Impl(TransactionManager* manager, YBTransaction* transaction, const TransactionMetadata& metadata)
      : trace_(Trace::MaybeGetNewTrace()),
        manager_(manager),
        transaction_(transaction),
        metadata_(metadata),
        read_point_(manager->clock()),
        child_(Child::kFalse) {
    CompleteConstruction();
    VLOG_WITH_PREFIX(2) << "Taken, metadata: " << metadata_;
  }

  Impl(TransactionManager* manager, YBTransaction* transaction, ChildTransactionData data)
      : trace_(Trace::MaybeGetNewTrace()),
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
    std::vector<rpc::Rpcs::Handle *> handles{
        &heartbeat_handle_, &new_heartbeat_handle_, &commit_handle_, &abort_handle_,
        &old_abort_handle_};
    handles.reserve(handles.size() + transaction_status_move_handles_.size());
    for (auto& entry : transaction_status_move_handles_) {
      handles.push_back(&entry.second);
    }
    manager_->rpcs().Abort(handles.begin(), handles.end());
    LOG_IF_WITH_PREFIX(DFATAL, !waiters_.empty()) << "Non empty waiters";
    const auto threshold = GetAtomicFlag(&FLAGS_txn_slow_op_threshold_ms);
    const auto print_trace_every_n = GetAtomicFlag(&FLAGS_txn_print_trace_every_n);
    const auto now = manager_->clock()->Now().GetPhysicalValueMicros();
    // start_ is not set if Init is not called - this happens for transactions that get
    // aborted without doing anything, so we set time_spent to 0 for these transactions.
    auto start = start_.load(std::memory_order_relaxed);
    const auto time_spent = (start == 0 ? 0 : now - start) * 1us;
    if ((trace_ && trace_->must_print())
           || (threshold > 0 && ToMilliseconds(time_spent) > threshold)
           || (FLAGS_txn_print_trace_on_error && !status_.ok())) {
      LOG(INFO) << ToString() << " took " << ToMicroseconds(time_spent)
                << "us. Trace: " << (trace_ ? "" : "Not collected");
      if (trace_)
        trace_->DumpToLogInfo(true);
    } else if (trace_) {
      bool was_printed = false;
      YB_LOG_IF_EVERY_N(INFO, print_trace_every_n > 0, print_trace_every_n)
          << ToString() << " took " << ToMicroseconds(time_spent) << "us. Trace: \n"
          << Trace::SetTrue(&was_printed);
      if (was_printed)
        trace_->DumpToLogInfo(true);
    }
  }

  void SetPriority(uint64_t priority) {
    if (PREDICT_FALSE(FLAGS_TEST_override_transaction_priority != 0)) {
      priority = FLAGS_TEST_override_transaction_priority;
    }
    metadata_.priority = priority;
  }

  void EnsureTraceCreated() {
    if (!trace_) {
      trace_ = new Trace;
      TRACE_TO(trace_, "Ensure Trace Created");
    }
  }

  uint64_t GetPriority() const {
    return metadata_.priority;
  }

  YBTransactionPtr CreateSimilarTransaction() {
    return std::make_shared<YBTransaction>(manager_);
  }

  Status Init(IsolationLevel isolation, const ReadHybridTime& read_time) {
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
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << __func__ << "(" << IsolationLevel_Name(isolation) << ", "
                        << read_point.GetReadTime() << ")";

    read_point_.MoveFrom(&read_point);
    CompleteInit(isolation);
  }

  const IsolationLevel isolation() const {
    return metadata_.isolation;
  }

  // This transaction is a restarted transaction, so we set it up with data from original one.
  Status FillRestartedTransaction(Impl* other) EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX(1) << "Setup restart to " << other->ToString();
    auto transaction = transaction_->shared_from_this();
    TRACE_TO(trace_, __func__);
    {
      std::lock_guard lock(mutex_);
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
      // TODO(Piyush): Do we need the below? If yes, prove with a test case and add it.
      // other->metadata_.priority = metadata_.priority;
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

  bool Prepare(internal::InFlightOpsGroupsWithMetadata* ops_info,
               ForceConsistentRead force_consistent_read,
               CoarseTimePoint deadline,
               Initial initial,
               Waiter waiter) override EXCLUDES(mutex_) {
    VLOG_WITH_PREFIX(2) << "Prepare(" << force_consistent_read << ", " << initial << ", "
                        << AsString(ops_info->groups) << ")";
    TRACE_TO(trace_, "Preparing $0 ops", AsString(ops_info->groups.size()));
    VTRACE_TO(2, trace_, "Preparing $0 ops", AsString(ops_info->groups));

    {
      UniqueLock lock(mutex_);
      if (!status_.ok()) {
        auto status = status_;
        lock.unlock();
        VLOG_WITH_PREFIX(2) << "Prepare, transaction already failed: " << status;
        if (waiter) {
          waiter(status);
        }
        return false;
      }

      auto promotion_started = StartPromotionToGlobalIfNecessary(ops_info);
      if (!promotion_started.ok()) {
        QueueWaiter(std::move(waiter));
        NotifyWaitersAndRelease(&lock, promotion_started.status(), "Nonlocal transaction");
        return false;
      }

      const bool defer = !ready_ || *promotion_started;
      if (!defer || initial) {
        PrepareOpsGroups(initial, ops_info->groups);
      }

      if (defer) {
        QueueWaiter(std::move(waiter));
        lock.unlock();

        if (*promotion_started) {
          DoPromoteToGlobal(deadline);
        } else {
          VLOG_WITH_PREFIX(2) << "Prepare, rejected (not ready, requesting status tablet)";
          RequestStatusTablet(deadline);
        }

        return false;
      }

      // For serializable isolation we never choose read time, since it always reads latest
      // snapshot.
      // For snapshot isolation, if read time was not yet picked, we have to choose it now, if
      // there multiple tablets that will process first request.
      SetReadTimeIfNeeded(ops_info->groups.size() > 1 || force_consistent_read);
    }

    {
      ops_info->metadata = {
        .transaction = metadata_,
        .subtransaction = subtransaction_.active()
            ? boost::make_optional(subtransaction_.get())
            : boost::none,
      };
    }

    return true;
  }

  void ExpectOperations(size_t count) EXCLUDES(mutex_) override {
    std::lock_guard lock(mutex_);
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
    bool schedule_status_moved = false;

    CommitCallback commit_callback;
    {
      std::lock_guard lock(mutex_);
      running_requests_ -= ops.size();

      if (status.ok()) {
        if (used_read_time && metadata_.isolation != IsolationLevel::SERIALIZABLE_ISOLATION) {
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
          // TODO: Update local limit for the tablet id which sent back the used read time
          read_point_.SetReadTime(used_read_time, ConsistentReadPoint::HybridTimeMap());
          VLOG_WITH_PREFIX(3)
              << "Update read time from used read time: " << read_point_.GetReadTime();
        }
        const std::string* prev_tablet_id = nullptr;
        for (const auto& op : ops) {
          const std::string& tablet_id = op.tablet->tablet_id();
          if (op.yb_op->applied() && op.yb_op->should_apply_intents(metadata_.isolation)) {
            if (prev_tablet_id == nullptr || tablet_id != *prev_tablet_id) {
              prev_tablet_id = &tablet_id;
              tablets_[tablet_id].has_metadata = true;
            }
            // 'num_completed_batches' for the involved tablet should be updated only when the op
            // performs a write at the tablet (explicit write/read with explicit locks), which is
            // checked for in the above 'if' clause. Else, we could run into scenarios where a txn
            // promotion request is sent to a participant which hasn't yet registered the txn, and
            // would lead to a 40001 being returned to pg.
            ++tablets_[tablet_id].num_completed_batches;
          }
          if (transaction_status_move_tablets_.count(tablet_id)) {
            schedule_status_moved = true;
          }
        }
      } else {
        const TransactionError txn_err(status);
        // We don't abort the txn in case of a kSkipLocking error to make further progress.
        // READ COMMITTED isolation retries errors of kConflict and kReadRestart by restarting
        // statements instead of the whole txn and hence should avoid aborting the txn in this case
        // too.
        bool avoid_abort =
            (txn_err.value() == TransactionErrorCode::kSkipLocking) ||
            (metadata_.isolation == IsolationLevel::READ_COMMITTED &&
              (txn_err.value() == TransactionErrorCode::kReadRestartRequired ||
                txn_err.value() == TransactionErrorCode::kConflict));
        if (!avoid_abort) {
          auto state = state_.load(std::memory_order_acquire);
          VLOG_WITH_PREFIX(4) << "Abort desired, state: " << AsString(state);
          if (state == TransactionState::kRunning) {
            abort = true;
            // State will be changed to aborted in SetError
          }

          SetErrorUnlocked(status, "Flush");
        }
      }

      if (running_requests_ == 0 && commit_replicated_) {
        notify_commit_status = status_;
        commit_callback = std::move(commit_callback_);
      }
    }

    if (schedule_status_moved) {
      SendUpdateTransactionStatusLocationRpcs();
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
      UniqueLock lock(mutex_);
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
      if (!transaction_status_move_handles_.empty()) {
        DCHECK(!commit_waiter_);
        VLOG_WITH_PREFIX(1) << "Waiting for transaction move RPCs to finish";
        commit_waiter_ = Waiter(std::bind(
            &Impl::DoCommit, this, deadline, seal_only, _1, transaction));
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
      UniqueLock lock(mutex_);
      auto state = state_.load(std::memory_order_acquire);
      if (state != TransactionState::kRunning) {
        if (state != TransactionState::kAborted) {
          LOG_WITH_PREFIX(DFATAL)
              << "Abort of committed transaction: " << AsString(state);
        } else {
          VLOG_WITH_PREFIX(2) << "Already aborted";
        }
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
        VLOG_WITH_PREFIX(2) << "Aborted transaction not yet ready";
        return;
      }
    }
    DoAbort(deadline, transaction);
  }

  void PrepareOpsGroups(
      bool initial, decltype(internal::InFlightOpsGroupsWithMetadata::groups)& groups)
      REQUIRES(mutex_) {
    for (auto& group : groups) {
      auto& first_op = *group.begin;
      const auto should_apply_intents = first_op.yb_op->should_apply_intents(metadata_.isolation);
      const auto& tablet = first_op.tablet;
      const auto& tablet_id = tablet->tablet_id();

      bool has_metadata;
      if (initial && should_apply_intents) {
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

  internal::InFlightOp* FindOpWithLocalityViolation(
      internal::InFlightOpsGroupsWithMetadata* ops_info) REQUIRES(mutex_) {
    if (metadata_.locality != TransactionLocality::LOCAL) {
      return nullptr;
    }
    for (auto& group : ops_info->groups) {
      auto& first_op = *group.begin;
      auto tablet = first_op.tablet;
      if (!tablet->IsLocalRegion()) {
        return &first_op;
      }
    }
    return nullptr;
  }

  Result<bool> StartPromotionToGlobalIfNecessary(
      internal::InFlightOpsGroupsWithMetadata* ops_info) REQUIRES(mutex_) {
    auto op = FindOpWithLocalityViolation(ops_info);
    if (!op) {
      return false;
    }

    if (!FLAGS_auto_promote_nonlocal_transactions_to_global) {
      auto tablet_id = op->tablet->tablet_id();
      auto status = STATUS_FORMAT(
            IllegalState, "Nonlocal tablet accessed in local transaction: tablet $0", tablet_id);
      VLOG_WITH_PREFIX(4) << "Prepare, rejected: " << status;
      return status;
    }

    VLOG_WITH_PREFIX(2) << "Prepare, rejected (promotion required)";
    // We start the promotion first, to avoid sending transaction status move RPCs to tablets
    // that were first involved in the transaction with this batch of changes.
    auto status = StartPromotionToGlobal();
    if (!status.ok()) {
      LOG(ERROR) << "Prepare for transaction " << metadata_.transaction_id
                 << " rejected (promotion failed): " << status;
      return status;
    }
    return true;
  }

  Status PromoteToGlobal(const CoarseTimePoint& deadline) EXCLUDES(mutex_) {
    {
      UniqueLock lock(mutex_);
      RETURN_NOT_OK(StartPromotionToGlobal());
    }
    DoPromoteToGlobal(deadline);
    return Status::OK();
  }

  Status StartPromotionToGlobal() REQUIRES(mutex_) {
    if (metadata_.locality == TransactionLocality::GLOBAL) {
      return STATUS(IllegalState, "Global transactions cannot be promoted");
    }

    auto running_state = TransactionState::kRunning;
    if (!state_.compare_exchange_strong(
            running_state, TransactionState::kPromoting, std::memory_order_acq_rel)) {
      LOG_WITH_PREFIX(DFATAL) << "Attempting to promote transaction not in running state";
    }
    ready_ = false;
    metadata_.locality = TransactionLocality::GLOBAL;

    transaction_status_move_tablets_.reserve(tablets_.size());
    transaction_status_move_handles_.reserve(tablets_.size());
    for (const auto& tablet_state : tablets_) {
      const auto& participant_tablet = tablet_state.first;
      transaction_status_move_tablets_.emplace(participant_tablet);
      transaction_status_move_handles_.emplace(participant_tablet,
                                               manager_->rpcs().InvalidHandle());
    }

    return Status::OK();
  }

  bool IsRestartRequired() const {
    return read_point_.IsRestartRequired();
  }

  std::shared_future<Result<TransactionMetadata>> GetMetadata(
      CoarseTimePoint deadline) EXCLUDES(mutex_) {
    UniqueLock lock(mutex_);
    if (metadata_future_.valid()) {
      return metadata_future_;
    }
    metadata_future_ = std::shared_future<Result<TransactionMetadata>>(
        metadata_promise_.get_future());
    if (!ready_) {
      auto transaction = transaction_->shared_from_this();
      waiters_.push_back([this, transaction](const Status& status) {
        WARN_NOT_OK(status, "Transaction request failed");
        UniqueLock lock(mutex_);
        if (status.ok()) {
          metadata_promise_.set_value(metadata_);
        } else {
          metadata_promise_.set_value(status);
        }
      });
      lock.unlock();
      RequestStatusTablet(deadline);
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
    UniqueLock lock(mutex_);
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
    UniqueLock lock(mutex_);
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
    UniqueLock lock(mutex_);
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

  const TaggedLogPrefix& LogPrefix() const {
    return log_prefix_;
  }

  std::string ToString() EXCLUDES(mutex_) {
    SharedLock<std::shared_mutex> lock(mutex_);
    return Format("{ metadata: $0 state: $1 }", metadata_, state_.load(std::memory_order_acquire));
  }

  const TransactionId& id() const {
    return metadata_.transaction_id;
  }

  ConsistentReadPoint& read_point() {
    return read_point_;
  }

  Result<TransactionMetadata> Release() {
    UniqueLock lock(mutex_);
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

  void SetActiveSubTransaction(SubTransactionId id) {
    VLOG_WITH_PREFIX(4) << "set active sub txn=" << id
                        << ", subtransaction_=" << subtransaction_.ToString();
    return subtransaction_.SetActiveSubTransaction(id);
  }

  Status SetPgTxnStart(int64_t pg_txn_start_us) {
    VLOG_WITH_PREFIX(4) << "set pg_txn_start_us_=" << pg_txn_start_us;
    RSTATUS_DCHECK(
        !metadata_.pg_txn_start_us || metadata_.pg_txn_start_us == pg_txn_start_us,
        InternalError,
        Format("Tried to set pg_txn_start_us (= $0) to new value (= $1)",
               metadata_.pg_txn_start_us, pg_txn_start_us));
    metadata_.pg_txn_start_us = pg_txn_start_us;
    return Status::OK();
  }

  std::future<Status> SendHeartBeatOnRollback(
      const CoarseTimePoint& deadline, const internal::RemoteTabletPtr& status_tablet,
      rpc::Rpcs::Handle* handle,
      const SubtxnSet& aborted_sub_txn_set) {
    DCHECK(status_tablet);

    return MakeFuture<Status>([&, handle](auto callback) {
      manager_->rpcs().RegisterAndStart(
          PrepareHeartbeatRPC(
              deadline, status_tablet, TransactionStatus::PENDING,
              [&, callback, handle](const auto& status, const auto& req, const auto& resp) {
                UpdateClock(resp, manager_);
                manager_->rpcs().Unregister(handle);
                callback(status);
              },
              aborted_sub_txn_set), handle);
    });
  }

  bool HasSubTransaction(SubTransactionId id) EXCLUDES(mutex_) {
    SharedLock<std::shared_mutex> lock(mutex_);
    return subtransaction_.active() && subtransaction_.HasSubTransaction(id);
  }

  Status RollbackToSubTransaction(SubTransactionId id, CoarseTimePoint deadline) EXCLUDES(mutex_) {
    SCHECK(
        subtransaction_.active(), InternalError,
        "Attempted to rollback to savepoint before creating any savepoints.");

    // A heartbeat should be sent (& waited for) to the txn status tablet(s) as part of a rollback.
    // This is for updating the list of aborted sub-txns and ensures that other txns don't see false
    // conflicts with this txn.
    //
    // Moreover the following correctness guarantee should always be maintained even if txn 1 and 2
    // are driven from YSQL on different nodes in the cluster:
    //
    //  Txn 1                                       Txn 2
    //  -----                                       -----
    //
    //  savepoint a;
    //  << write some intents
    //     (i.e., provisional writes) >>
    //  rollback to a;
    //                                    << any statement executed "after" (as per "real time"/
    //                                       "wall clock") should be able to see that the intents of
    //                                       the rolled back sub-txn have been aborted and are
    //                                       invalid. >>

    #ifndef NDEBUG
    YBSubTransaction subtransaction_copy_for_dcheck = subtransaction_;
    #endif

    vector<std::future<Status>> heartbeat_futures;
    {
      SharedLock<std::shared_mutex> lock(mutex_);
      if (!ready_) {
        // ready_ can be false in 2 situations:
        //
        //  (a) The transaction hasn't been pre-created and hence not registered yet at the status
        //      tablet.
        //  (b) The transaction is undergoing a promotion i.e., a txn is being registered at a new
        //      "global" status tablet. Once a heartbeat is sent to the new status tablet, promotion
        //      is complete.
        //
        // We don't have to worry sending the aborted sub-txn id list synchronously to the status
        // tablet as part of a rollback in both situations with reasoning as follows for each
        // situation -
        //
        //   (1) For situation (a): if the txn hasn't been pre-created it means no ops have been
        //       performed as part of the txn (because any op would result in Prepare() and would
        //       wait for the txn to be ready_ before issuing the rpc).
        //
        //   (2) For situation (b), we can't reach it ever during a rollback because -
        //         i) a promotion can only be triggered by Prepare() of some ops
        //         ii) the ops will wait for the promotion to complete i.e., a heartbeat is sent to
        //             the new status tablet of promoted txn
        //         iii) PgSession::RollbackToSubTransaction() will wait for all in flight ops via
        //              FlushBufferedOperations().
        return Status::OK();
      }

      // We are making a copy of subtransaction_ so that we can update it with the new range and
      // send its aborted sub txn list in heartbeats before updating the actual subtransaction_.
      YBSubTransaction subtransaction_copy = subtransaction_;
      RETURN_NOT_OK(subtransaction_copy.RollbackToSubTransaction(id));

      if (old_status_tablet_state_.load(std::memory_order_acquire) ==
            OldTransactionState::kRunning) {
        // Don't have to send a heartbeat if the txn in old status tablet is non-existent (i.e., if
        // no promotion activity occurred), or if it is being / has been retired by aborting.
        VLOG_WITH_PREFIX(2) << "Sending heartbeat to old status tablet for sub-txn rollback.";
        heartbeat_futures.push_back(SendHeartBeatOnRollback(
            deadline, old_status_tablet_, &old_rollback_heartbeat_handle_,
            subtransaction_copy.get().aborted));
      }

      auto state = state_.load(std::memory_order_acquire);
      DCHECK(state != TransactionState::kPromoting); // can't happen, see comment above for details
      if (state == TransactionState::kRunning) {
        VLOG_WITH_PREFIX(2) << "Sending heartbeat to status tablet for sub-txn rollback.";
        heartbeat_futures.push_back(SendHeartBeatOnRollback(
            deadline, status_tablet_, &rollback_heartbeat_handle_,
            subtransaction_copy.get().aborted));
      }
    }

    // Wait for the heartbeat response
    for (auto& future : heartbeat_futures) {
      auto status = future.get();
      // If the transaction has been aborted or no longer exists, we don't have to do anything
      // further. The rollback heartbeat which tries to update the list of aborted sub-txns is as
      // good as successful.
      if (!(status.IsAborted() || status.IsExpired()))
        RETURN_NOT_OK(status);
    }

    #ifndef NDEBUG
    DCHECK(subtransaction_copy_for_dcheck == subtransaction_);
    #endif

    RETURN_NOT_OK(subtransaction_.RollbackToSubTransaction(id));
    VLOG_WITH_PREFIX(2) << "Aborted sub-txns from " << id
                        << "; subtransaction_=" << subtransaction_.ToString();

    return Status::OK();
  }

  void IncreaseMutationCounts(
      SubTransactionId subtxn_id, const TableId& table_id, uint64_t mutation_count) {
    auto it = subtxn_table_mutation_counter_map_.find(subtxn_id);
    if (it != subtxn_table_mutation_counter_map_.end()) {
      it->second[table_id] += mutation_count;
      return;
    }
    subtxn_table_mutation_counter_map_[subtxn_id].insert({table_id, mutation_count});
  }

  std::unordered_map<TableId, uint64_t> GetTableMutationCounts() const {
    auto& aborted_sub_txn_set = subtransaction_.get().aborted;
    std::unordered_map<TableId, uint64_t> table_mutation_counts;
    for (const auto& [sub_txn_id, table_mutation_cnt_map] : subtxn_table_mutation_counter_map_) {
      if (aborted_sub_txn_set.Test(sub_txn_id)) {
        continue;
      }

      for (const auto& [table_id, mutation_count] : table_mutation_cnt_map) {
        table_mutation_counts[table_id] += mutation_count;
      }
    }
    return table_mutation_counts;
  }

  void SetLogPrefixTag(const LogPrefixName& name, uint64_t id) {
    log_prefix_.tag.store(LogPrefixTag(&name.Get(), id), boost::memory_order_release);
    VLOG_WITH_PREFIX(2) << "Log prefix tag changed";
  }

 private:
  void CompleteConstruction() {
    LOG_IF(FATAL, !IsAcceptableAtomicImpl(log_prefix_.tag));
    log_prefix_.prefix = Format("$0$1", metadata_.transaction_id, child_ ? " (CHILD)" : "");
    heartbeat_handle_ = manager_->rpcs().InvalidHandle();
    new_heartbeat_handle_ = manager_->rpcs().InvalidHandle();
    commit_handle_ = manager_->rpcs().InvalidHandle();
    abort_handle_ = manager_->rpcs().InvalidHandle();
    old_abort_handle_ = manager_->rpcs().InvalidHandle();
    rollback_heartbeat_handle_ = manager_->rpcs().InvalidHandle();
    old_rollback_heartbeat_handle_ = manager_->rpcs().InvalidHandle();

    auto metric_entity = manager_->client()->metric_entity();
    if (metric_entity) {
      transaction_promotions_ = METRIC_transaction_promotions.Instantiate(metric_entity);
    }
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
    start_.store(manager_->clock()->Now().GetPhysicalValueMicros(), std::memory_order_release);
  }

  void SetReadTimeIfNeeded(bool do_it) {
    if (!read_point_.GetReadTime() && do_it &&
        (metadata_.isolation == IsolationLevel::SNAPSHOT_ISOLATION ||
         metadata_.isolation == IsolationLevel::READ_COMMITTED)) {
      VLOG_WITH_PREFIX(2) << "Setting current read time as read point for distributed txn";
      read_point_.SetCurrentReadTime();
    }
  }

  Status CheckRunningUnlocked() REQUIRES(mutex_) {
    if (state_.load(std::memory_order_acquire) != TransactionState::kRunning) {
      auto status = status_;
      if (status.ok()) {
        status = STATUS(IllegalState, "Transaction already completed");
      }
      return status;
    }
    return Status::OK();
  }

  rpc::RpcCommandPtr PrepareOldStatusTabletFinalHeartbeat(
      CoarseTimePoint deadline, SealOnly seal_only, const Status& status,
      const YBTransactionPtr& transaction) REQUIRES(mutex_) {
    // If the last heartbeats to the old status tablet failed, it's possible that we have lost
    // connection to it. We can't commit until a PENDING heartbeat succeeds, because it's possible
    // that the transaction timed out on the old statuts tablet and a participant already cleaned
    // up intents before receiving the UpdateTransactionStatusLocation RPC, so we send one last
    // UpdateTransaction(PENDING) first, and abort instead if it fails.
    VLOG_WITH_PREFIX(1) << "Last heartbeat to old status tablet failed, resending before COMMIT";
    return PrepareHeartbeatRPC(
        deadline, old_status_tablet_, TransactionStatus::PENDING,
        [this, deadline, seal_only, transaction](
            const auto& status, const auto& req, const auto& resp) {
          UpdateClock(resp, manager_);
          manager_->rpcs().Unregister(&commit_handle_);
          last_old_heartbeat_failed_.store(!status.ok(), std::memory_order_release);
          DoCommit(deadline, seal_only, status, transaction);
        });
  }

  struct TabletState {
    size_t num_batches = 0;
    size_t num_completed_batches = 0;
    bool has_metadata = false;

    std::string ToString() const {
      return Format("{ num_batches: $0 num_completed_batches: $1 has_metadata: $2 }",
                    num_batches, num_completed_batches, has_metadata);
    }
  };

  typedef std::unordered_map<TabletId, TabletState> TabletStates;

  rpc::RpcCommandPtr PrepareHeartbeatRPC(
      CoarseTimePoint deadline, const internal::RemoteTabletPtr& status_tablet,
      TransactionStatus status, UpdateTransactionCallback callback,
      std::optional<SubtxnSet> aborted_set_for_rollback_heartbeat = std::nullopt,
      const TabletStates& tablets_with_locks = {}) {
    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());

    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
    state.set_status(status);

    auto start = start_.load(std::memory_order_acquire);
    if (start > 0) {
      state.set_start_time(start);

      if (!PREDICT_FALSE(FLAGS_disable_heartbeat_send_involved_tablets)) {
        for (const auto& [tablet_id, _] : tablets_with_locks) {
          state.add_tablets(tablet_id);
        }
      }
    }
    auto* local_ts = manager_->client()->GetLocalTabletServer();
    if (local_ts) {
      state.set_host_node_uuid(local_ts->permanent_uuid());
    }

    if (aborted_set_for_rollback_heartbeat) {
      VLOG_WITH_PREFIX(4) << "Setting aborted_set_for_rollback_heartbeat: "
                          << aborted_set_for_rollback_heartbeat.value().ToString();

      aborted_set_for_rollback_heartbeat.value().ToPB(state.mutable_aborted()->mutable_set());
    }

    return UpdateTransaction(
        deadline,
        status_tablet.get(),
        manager_->client(),
        &req,
        std::move(callback));
  }

  void DoCommit(
      CoarseTimePoint deadline, SealOnly seal_only, const Status& status,
      const YBTransactionPtr& transaction) EXCLUDES(mutex_) {
    UniqueLock lock(mutex_);
    VLOG_WITH_PREFIX(1)
        << Format("Commit, seal_only: $0, tablets: $1, status: $2",
                  seal_only, tablets_, status);

    if (!status.ok()) {
      VLOG_WITH_PREFIX(4) << "Commit failed: " << status;
      auto commit_callback = std::move(commit_callback_);
      lock.unlock();
      commit_callback(status);
      return;
    }

    // TODO: In case of transaction promotion, DoCommit is called only after we hearback on all
    // sent transation status move requests. So either the promotion went through, in which case
    // transaction participants of all involved tablets check the txn's state against the new
    // status tablet, or the promotion failed, in which case we anyways abort the transaction.
    // TBD if we do require a successfull PENDING heartbeat to go through.
    if (old_status_tablet_ && last_old_heartbeat_failed_.load(std::memory_order_acquire)) {
      auto rpc = PrepareOldStatusTabletFinalHeartbeat(deadline, seal_only, status, transaction);
      lock.unlock();
      manager_->rpcs().RegisterAndStart(rpc, &commit_handle_);
      return;
    }

    tserver::UpdateTransactionRequestPB req;
    req.set_tablet_id(status_tablet_->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    auto& state = *req.mutable_state();
    state.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
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

    if (subtransaction_.active()) {
      subtransaction_.get().aborted.ToPB(state.mutable_aborted()->mutable_set());
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

    auto old_status_tablet = old_status_tablet_;
    lock.unlock();
    SendAbortToOldStatusTabletIfNeeded(deadline, transaction, old_status_tablet);
  }

  void DoAbort(CoarseTimePoint deadline, const YBTransactionPtr& transaction) EXCLUDES(mutex_) {
    decltype(status_tablet_) status_tablet;
    decltype(old_status_tablet_) old_status_tablet;
    {
      SharedLock<std::shared_mutex> lock(mutex_);
      status_tablet = status_tablet_;
      old_status_tablet = old_status_tablet_;
    }
    SendAbortToOldStatusTabletIfNeeded(deadline, transaction, old_status_tablet);
    DoAbort(deadline, transaction, status_tablet);
  }

  void SendAbortToOldStatusTabletIfNeeded(
      CoarseTimePoint deadline,
      const YBTransactionPtr& transaction,
      const internal::RemoteTabletPtr& old_status_tablet) {
    if (CheckAbortToOldStatusTabletNeeded()) {
      SendAbortToOldStatusTablet(deadline, transaction, old_status_tablet);
    }
  }

  bool CheckAbortToOldStatusTabletNeeded() {
    OldTransactionState old_status_tablet_state = OldTransactionState::kRunning;

    old_status_tablet_state_.compare_exchange_strong(
        old_status_tablet_state, OldTransactionState::kAborting, std::memory_order_acq_rel);

    switch (old_status_tablet_state) {
      case OldTransactionState::kAborting: FALLTHROUGH_INTENDED;
      case OldTransactionState::kAborted:
        VLOG_WITH_PREFIX(1) << "Abort to old status tablet already in progress";
        return false;
      case OldTransactionState::kNone:
        VLOG_WITH_PREFIX(1)
            << "Transaction wasn't promoted, not sending abort to old status tablet";
        return false;
      case OldTransactionState::kRunning:
        VLOG_WITH_PREFIX(1) << "Transaction was promoted, abort to old status tablet needed";
        return true;
    }
    FATAL_INVALID_ENUM_VALUE(OldTransactionState, old_status_tablet_state);
  }

  void SendAbortToOldStatusTablet(
      CoarseTimePoint deadline,
      const YBTransactionPtr& transaction,
      internal::RemoteTabletPtr old_status_tablet) {
    VLOG_WITH_PREFIX(1) << "Sending abort to old status tablet";

    if (PREDICT_FALSE(FLAGS_TEST_old_txn_status_abort_delay_ms > 0)) {
      std::this_thread::sleep_for(FLAGS_TEST_old_txn_status_abort_delay_ms * 1ms);
    }

    tserver::AbortTransactionRequestPB req;
    req.set_tablet_id(old_status_tablet->tablet_id());
    req.set_propagated_hybrid_time(manager_->Now().ToUint64());
    req.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());

    manager_->rpcs().RegisterAndStart(
        AbortTransaction(
            deadline,
            old_status_tablet.get(),
            manager_->client(),
            &req,
            std::bind(&Impl::OldStatusAbortDone, this, _1, _2, transaction)),
        &old_abort_handle_);
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
      VLOG_WITH_PREFIX(1) << "TEST: Disabled proactive transaction cleanup on abort";
      return;
    }

    std::vector<std::string> tablet_ids;
    {
      std::lock_guard lock(mutex_);
      tablet_ids.reserve(tablets_.size());
      for (const auto& tablet : tablets_) {
        // We don't check has_metadata here, because intents could be written even in case of
        // failure. For instance in case of conflict on unique index.
        tablet_ids.push_back(tablet.first);
      }
      VLOG_WITH_PREFIX(1) << "Cleaning up intents from: " << AsString(tablet_ids);
    }

    CleanupTransaction(
        manager_->client(), manager_->clock(), metadata_.transaction_id, Sealed::kFalse,
        cleanup_type, tablet_ids);
  }

  void CommitDone(const Status& status,
                  const tserver::UpdateTransactionResponsePB& response,
                  const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);

    auto old_status_tablet_state = old_status_tablet_state_.load(std::memory_order_acquire);
    if (old_status_tablet_state == OldTransactionState::kAborting) {
      std::lock_guard lock(mutex_);
      VLOG_WITH_PREFIX(1) << "Commit done, but waiting for abort on old status tablet";
      cleanup_waiter_ = Waiter(std::bind(&Impl::CommitDone, this, status, response, transaction));
      return;
    }

    VLOG_WITH_PREFIX(1) << "Committed: " << status;

    UpdateClock(response, manager_);
    manager_->rpcs().Unregister(&commit_handle_);

    Status actual_status = status.IsAlreadyPresent() ? Status::OK() : status;
    CommitCallback commit_callback;
    if (state_.load(std::memory_order_acquire) != TransactionState::kCommitted &&
        actual_status.ok()) {
      std::lock_guard lock(mutex_);
      commit_replicated_ = true;
      if (running_requests_ != 0) {
        return;
      }
      commit_callback = std::move(commit_callback_);
    } else {
      std::lock_guard lock(mutex_);
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

    auto old_status_tablet_state = old_status_tablet_state_.load(std::memory_order_acquire);
    if (old_status_tablet_state == OldTransactionState::kAborting) {
      std::lock_guard lock(mutex_);
      VLOG_WITH_PREFIX(1) << "Abort done, but waiting for abort on old status tablet";
      cleanup_waiter_ = Waiter(std::bind(&Impl::AbortDone, this, status, response, transaction));
      return;
    }

    VLOG_WITH_PREFIX(1) << "Aborted: " << status;

    if (response.has_propagated_hybrid_time()) {
      manager_->UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }
    manager_->rpcs().Unregister(&abort_handle_);
  }

  void OldStatusAbortDone(const Status& status,
                          const tserver::AbortTransactionResponsePB& response,
                          const YBTransactionPtr& transaction) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Aborted on old status tablet: " << status;

    if (response.has_propagated_hybrid_time()) {
      manager_->UpdateClock(HybridTime(response.propagated_hybrid_time()));
    }
    manager_->rpcs().Unregister(&old_abort_handle_);

    Waiter waiter;
    {
      std::lock_guard lock(mutex_);
      cleanup_waiter_.swap(waiter);
      old_status_tablet_state_.store(OldTransactionState::kAborted, std::memory_order_release);
    }

    if (waiter) {
      waiter(status);
    }
  }

  void DoPromoteToGlobal(const CoarseTimePoint& deadline) EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(2) << "DoPromoteToGlobal()";
    auto transaction = transaction_->shared_from_this();

    manager_->PickStatusTablet(
        std::bind(&Impl::StatusTabletPicked, this, _1, deadline, transaction,
                  TransactionPromoting::kTrue),
        metadata_.locality);
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
          std::bind(&Impl::StatusTabletPicked, this, _1, deadline, transaction,
                    TransactionPromoting::kFalse),
          metadata_.locality);
    } else {
      LookupStatusTablet(metadata_.status_tablet, deadline, transaction,
                         TransactionPromoting::kFalse);
    }
  }

  void StatusTabletPicked(const Result<std::string>& tablet,
                          const CoarseTimePoint& deadline,
                          const YBTransactionPtr& transaction,
                          TransactionPromoting promoting) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(2) << "Picked status tablet: " << tablet;

    if (!tablet.ok()) {
      NotifyWaiters(tablet.status(), "Pick status tablet", SetReady::kFalse);
      return;
    }

    LookupStatusTablet(*tablet, deadline, transaction, promoting);
  }

  void LookupStatusTablet(const std::string& tablet_id,
                          const CoarseTimePoint& deadline,
                          const YBTransactionPtr& transaction,
                          TransactionPromoting promoting) {
    TRACE_TO(trace_, __func__);
    manager_->client()->LookupTabletById(
        tablet_id,
        /* table =*/ nullptr,
        master::IncludeInactive::kFalse,
        master::IncludeDeleted::kFalse,
        deadline,
        std::bind(&Impl::LookupTabletDone, this, _1, transaction, promoting),
        client::UseCache::kTrue);
  }

  void LookupTabletDone(
      const Result<client::internal::RemoteTabletPtr>& result, const YBTransactionPtr& transaction,
      TransactionPromoting promoting) EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << yb::ToString(result);

    if (!result.ok()) {
      NotifyWaiters(result.status(), "Lookup tablet", SetReady::kFalse);
      return;
    }

    std::vector<Waiter> waiters;
    auto status = HandleLookupTabletCases(result, &waiters, promoting);

    if (status == TransactionStatus::ABORTED) {
      DCHECK(promoting);
      decltype(old_status_tablet_) old_status_tablet;
      {
        SharedLock lock(mutex_);
        old_status_tablet = old_status_tablet_;
      }
      SendAbortToOldStatusTabletIfNeeded(TransactionRpcDeadline(), transaction, old_status_tablet);
    } else {
      auto send_new_heartbeat = [this, status, promoting](const Status&) {
        SendHeartbeat(status, metadata_.transaction_id, transaction_->shared_from_this(),
                      SendHeartbeatToNewTablet(promoting));
      };
      if (PREDICT_FALSE(FLAGS_TEST_new_txn_status_initial_heartbeat_delay_ms > 0)) {
        manager_->client()->messenger()->scheduler().Schedule(
            std::move(send_new_heartbeat),
            std::chrono::milliseconds(FLAGS_TEST_new_txn_status_initial_heartbeat_delay_ms));
      } else {
        send_new_heartbeat(Status::OK());
      }
    }

    for (const auto& waiter : waiters) {
      waiter(Status::OK());
    }
  }

  TransactionStatus HandleLookupTabletCases(const Result<client::internal::RemoteTabletPtr>& result,
                                            std::vector<Waiter>* waiters,
                                            TransactionPromoting promoting) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    TransactionStatus status;
    bool notify_waiters;
    if (promoting) {
      // From transaction promotion.
      DCHECK(state_.load(std::memory_order_acquire) == TransactionState::kPromoting);

      notify_waiters = false;

      if (!metadata_.status_tablet.empty()) {
        old_status_tablet_state_.store(OldTransactionState::kRunning, std::memory_order_release);
        old_status_tablet_ = std::move(status_tablet_);
        metadata_.old_status_tablet = metadata_.status_tablet;
        status = TransactionStatus::PROMOTED;
      } else {
        // We don't have a status tablet or transaction id yet, so we use CREATED here.
        status = TransactionStatus::CREATED;
        auto state = TransactionState::kPromoting;
        if (!state_.compare_exchange_strong(
            state, TransactionState::kRunning, std::memory_order_acq_rel)) {
          LOG_WITH_PREFIX(DFATAL) << "Transaction was not in promoting state: " << AsString(state);
        }
      }

      status_tablet_ = std::move(*result);
      metadata_.status_tablet = status_tablet_->tablet_id();

      VLOG_WITH_PREFIX(1) << "Transaction status moving from tablet "
                          << (!metadata_.old_status_tablet.empty()
                              ? metadata_.old_status_tablet
                              : "n/a")
                          << " to tablet " << metadata_.status_tablet;

      IncrementCounter(transaction_promotions_);
    } else {
      // If status_tablet_ is set already, then this is the case where first-op promotion
      // finished before lookup of old status tablet.
      bool promotion_lookup_finished{ status_tablet_ };
      auto& status_tablet = promotion_lookup_finished ? old_status_tablet_ : status_tablet_;
      auto& status_tablet_id =
          promotion_lookup_finished ? metadata_.old_status_tablet : metadata_.status_tablet;

      status_tablet = std::move(*result);

      if (status_tablet_id.empty()) {
        // Initial status lookup, not pre-created.
        status_tablet_id = status_tablet_->tablet_id();
        notify_waiters = false;
        status = TransactionStatus::CREATED;
      } else {
        // Pre-created transaction.
        notify_waiters = true;
        status = TransactionStatus::PENDING;
      }

      if (promotion_lookup_finished) {
        // If this was not a pre-created transaction, then we haven't actually sent CREATED
        // to the old status tablet, so we can continue with old_status_tablet_state_ = kNone,
        // and act as if we just simply picked the correct status tablet to begin with.
        // If this was a pre-created transaction, then we have to go through the normal promotion
        // code path and abort the old transaction.
        if (status == TransactionStatus::PENDING) {
          old_status_tablet_state_.store(OldTransactionState::kRunning, std::memory_order_release);
        }

        notify_waiters = false;

        // Return status ABORTED here to trigger abort to old status tablet if needed.
        status = TransactionStatus::ABORTED;
      }
    }

    if (notify_waiters) {
      ready_ = true;
      waiters_.swap(*waiters);
    }

    return status;
  }

  void QueueWaiter(Waiter waiter) REQUIRES(mutex_) {
    if (waiter) {
      waiters_.push_back(std::move(waiter));
    }
  }

  // See NotifyWaitersAndRelease.
  void NotifyWaiters(const Status& status, const char* operation,
                     SetReady set_ready = SetReady::kFalse) EXCLUDES(mutex_) {
    UniqueLock lock(mutex_);
    NotifyWaitersAndRelease(&lock, status, operation, set_ready);
  }

  // Notify all waiters. The transaction will be aborted if it is running and status is not OK.
  // `lock` will be released in this function. If `set_ready` is true and status is OK, `ready_`
  // must be false, and will be set to true.
  void NotifyWaitersAndRelease(UniqueLock<std::shared_mutex>* lock,
                               Status status,
                               const char* operation,
                               SetReady set_ready = SetReady::kFalse) NO_THREAD_SAFETY_ANALYSIS {
    std::vector<Waiter> waiters;
    bool trigger_abort = false;

    if (!status.ok()) {
      auto state = state_.load(std::memory_order_acquire);
      if (state == TransactionState::kRunning) {
        trigger_abort = true;
      }
      SetErrorUnlocked(status, operation);
    } else if (set_ready) {
      DCHECK(!ready_);
      ready_ = true;
    }
    waiters_.swap(waiters);

    lock->unlock();

    for (const auto& waiter : waiters) {
      waiter(status);
    }

    if (trigger_abort) {
      Abort(TransactionRpcDeadline());
    }
  }

  // `send_to_new_tablet` is always false in non-promoted transactions.
  // In promoted transactions:
  // `send_to_new_tablet == false`:
  // - heartbeats to original local status tablet (CREATED [if not precreated], then PENDING)
  // - runs from transaction start until old transaction is aborted
  // `send_to_new_tablet == true`:
  // - heartbeats to the new global status tablet (PROMOTED, then PENDING)
  // - runs from transaction promotion until commit/abort time.
  void SendHeartbeat(TransactionStatus status,
                     const TransactionId& id,
                     const std::weak_ptr<YBTransaction>& weak_transaction,
                     SendHeartbeatToNewTablet send_to_new_tablet) {
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

    VLOG_WITH_PREFIX(4) << __func__ << "(" << TransactionStatus_Name(status) << ", "
                        << send_to_new_tablet << ")";

    if (!ShouldContinueHeartbeats(send_to_new_tablet)) {
      VLOG_WITH_PREFIX(1) << "Old status tablet is no longer in use, cancelling heartbeat";
      return;
    }

    MonoDelta timeout;
    if (status != TransactionStatus::CREATED && status != TransactionStatus::PROMOTED) {
      if (GetAtomicFlag(&FLAGS_transaction_disable_heartbeat_in_tests)) {
        HeartbeatDone(Status::OK(), /* request= */ {}, /* response= */ {}, status, transaction,
                      send_to_new_tablet);
        return;
      }
      timeout = std::chrono::microseconds(FLAGS_transaction_heartbeat_usec);
    } else {
      timeout = TransactionRpcTimeout();
    }

    rpc::RpcCommandPtr rpc;
    {
      SharedLock<std::shared_mutex> lock(mutex_);
      internal::RemoteTabletPtr status_tablet;

      if (!send_to_new_tablet && old_status_tablet_) {
        status_tablet = old_status_tablet_;
      } else {
        status_tablet = status_tablet_;
      }
      rpc = PrepareHeartbeatRPC(
          CoarseMonoClock::now() + timeout, status_tablet, status,
          std::bind(
              &Impl::HeartbeatDone, this, _1, _2, _3, status, transaction, send_to_new_tablet),
          std::nullopt, tablets_);
    }

    auto& handle = send_to_new_tablet ? new_heartbeat_handle_ : heartbeat_handle_;
    manager_->rpcs().RegisterAndStart(rpc, &handle);
  }

  static bool AllowHeartbeat(TransactionState current_state, TransactionStatus status) {
    switch (current_state) {
      case TransactionState::kRunning:
      case TransactionState::kPromoting:
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

  // Returns whether the heartbeater thread should continue heartbeating.
  //
  // When 'send_to_new_tablet' is true, we should continue sending/processing heartbeat requests/
  // responses as they are with the new status tablet stored in 'status_tablet_'.
  //
  // When 'send_to_new_tablet' is false, we should continue heartbeats in the following cases:
  // 1. Heartbeating to 'status_tablet_':
  //    - happens when the transaction started off as a global txn. In this case
  //      'old_status_tablet_state_' remains in 'kNone'.
  //    - transaction started off as local, and promotion hasn't been kicked off yet (new status
  //      tablet hasn't been picked yet). 'old_status_tablet_state_' remains in 'kNone' until the
  //      'status_tablet_' switch happens.
  // 2. Heartbeating to 'old_status_tablet_':
  //    - transaction started off as local, and was promoted. 'old_status_tablet_state_' remains
  //      in 'kRunning' until abort is sent to 'old_status_tablet_'.
  // Once an abort request is sent to the old status tablet, 'old_status_tablet_state_' transitions
  // to 'kAborting', and to 'kAborted' from there. Hence, we should not process heartbeat responses
  // or initiate new heartbeat requests when not in ('kNone', 'kRunning') states, as they wouldn't
  // reflect the actual status of the transaction.
  bool ShouldContinueHeartbeats(SendHeartbeatToNewTablet send_to_new_tablet) {
    if (send_to_new_tablet) {
      return true;
    }
    auto old_status_tablet_state = old_status_tablet_state_.load(std::memory_order_acquire);
    return old_status_tablet_state == OldTransactionState::kNone ||
           old_status_tablet_state == OldTransactionState::kRunning;
  }

  void HeartbeatDone(Status status,
                     const tserver::UpdateTransactionRequestPB& request,
                     const tserver::UpdateTransactionResponsePB& response,
                     TransactionStatus transaction_status,
                     const YBTransactionPtr& transaction,
                     SendHeartbeatToNewTablet send_to_new_tablet) {
    UpdateClock(response, manager_);
    auto& handle = send_to_new_tablet ? new_heartbeat_handle_ : heartbeat_handle_;
    manager_->rpcs().Unregister(&handle);

    if (status.ok() && transaction_status == TransactionStatus::CREATED) {
      auto decode_result = FullyDecodeTransactionId(request.state().transaction_id());
      if (decode_result.ok()) {
        metadata_.transaction_id = *decode_result;
        auto id_str = AsString(metadata_.transaction_id);
        // It is not fully thread safe, since we don't use mutex to access log_prefix_.
        // But here we just replace characters inplace.
        // It would not crash anyway, and could produce wrong id in the logs.
        // It is ok, since one moment before we would output nil id.
        log_prefix_.prefix.replace(0, id_str.length(), id_str);
      } else {
        status = decode_result.status();
      }
    }

    VLOG_WITH_PREFIX(4) << __func__ << "(" << status << ", "
                        << TransactionStatus_Name(transaction_status) << ", "
                        << send_to_new_tablet << ")";

    if (!ShouldContinueHeartbeats(send_to_new_tablet)) {
      // It could have happended that all UpdateTransactionStatusLocation requests went through
      // and we issued an abort to the old status tablet. We should ignore any heartbeat response
      // we receive after that. Since the promotion went through, the involved txn participants
      // would check against the latest status tablet for any course of action. So we need not
      // be concerned on whether this was a genuine heartbeat error and can safely exit without
      // error handling or cleanup (they will be handled by the other heartbeater thread hitting
      // the new status tablet in 'status_tablet_').
      VLOG_WITH_PREFIX_AND_FUNC(1)
          << "Skipping cleanup because the heartbeat response from the old status tablet was "
          << "received after sending abort to old status tablet. "
          << old_status_tablet_state_.load(std::memory_order_relaxed);
      return;
    }
    if (!send_to_new_tablet) {
      last_old_heartbeat_failed_.store(!status.ok(), std::memory_order_release);
    }

    if (status.ok()) {
      std::weak_ptr<YBTransaction> weak_transaction(transaction);
      switch (transaction_status) {
        case TransactionStatus::PROMOTED:
          {
            auto state = TransactionState::kPromoting;
            if (!state_.compare_exchange_strong(
                    state, TransactionState::kRunning, std::memory_order_acq_rel) &&
                state != TransactionState::kAborted) {
              LOG_WITH_PREFIX(DFATAL) << "Transaction status promoted but not in promoting state";
            }
            SendUpdateTransactionStatusLocationRpcs();
          }
          FALLTHROUGH_INTENDED;
        case TransactionStatus::CREATED:
          {
            // If we are in the kPromoted state, this may be a CREATED to the old
            // status tablet finishing after promotion has started, so we don't set ready_ = true.
            auto state = state_.load(std::memory_order_acquire);
            NotifyWaiters(Status::OK(), "Heartbeat",
                          SetReady(state != TransactionState::kPromoting));
          }
          FALLTHROUGH_INTENDED;
        case TransactionStatus::PENDING:
          manager_->client()->messenger()->scheduler().Schedule(
              [this, weak_transaction, send_to_new_tablet, id = metadata_.transaction_id](
                  const Status&) {
                SendHeartbeat(TransactionStatus::PENDING, id, weak_transaction, send_to_new_tablet);
              },
              std::chrono::microseconds(FLAGS_transaction_heartbeat_usec));
          return;
        case TransactionStatus::COMMITTED:
        case TransactionStatus::SEALED:
        case TransactionStatus::APPLIED_IN_ALL_INVOLVED_TABLETS:
        case TransactionStatus::ABORTED:
        case TransactionStatus::APPLYING:
        case TransactionStatus::APPLIED_IN_ONE_OF_INVOLVED_TABLETS:
        case TransactionStatus::IMMEDIATE_CLEANUP:
        case TransactionStatus::GRACEFUL_CLEANUP:
          LOG_WITH_PREFIX(DFATAL) << "Heartbeat received for unusual state: "
                                  << TransactionStatus_Name(transaction_status);
      }
      FATAL_INVALID_ENUM_VALUE(TransactionStatus, transaction_status);
    } else {
      auto state = state_.load(std::memory_order_acquire);
      LOG_WITH_PREFIX(WARNING) << "Send heartbeat failed: " << status << ", txn state: " << state;

      if (status.IsAborted() || status.IsExpired()) {
        // IsAborted - Service is shutting down, no reason to retry.
        // IsExpired - Transaction expired.
        if (transaction_status == TransactionStatus::CREATED ||
            transaction_status == TransactionStatus::PROMOTED) {
          NotifyWaiters(status, "Heartbeat", SetReady::kTrue);
        } else {
          SetError(status, "Heartbeat");
        }
        // If state is committed, then we should not cleanup.
        if (status.IsExpired() && state == TransactionState::kRunning) {
          DoAbortCleanup(transaction, CleanupType::kImmediate);
        }
        return;
      }
      // Other errors could have different causes, but we should just retry sending heartbeat
      // in this case.
      SendHeartbeat(transaction_status, metadata_.transaction_id, transaction, send_to_new_tablet);
    }
  }

  void SendUpdateTransactionStatusLocationRpcs() EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(2) << "SendUpdateTransactionStatusLocationRpcs()";

    std::weak_ptr<YBTransaction> weak_transaction = transaction_->shared_from_this();
    manager_->client()->messenger()->scheduler().Schedule(
        [this, weak_transaction, id = metadata_.transaction_id](const Status&) {
          DoSendUpdateTransactionStatusLocationRpcs(weak_transaction, id);
        },
        std::chrono::milliseconds(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms));
  }

  void DoSendUpdateTransactionStatusLocationRpcs(
      const std::weak_ptr<YBTransaction>& weak_transaction, const TransactionId& id)
      EXCLUDES(mutex_) {
    auto transaction = weak_transaction.lock();
    if (!transaction) {
      VLOG(1) << id << ": " << "Transaction destroyed, not sending status location updates";
      return;
    }

    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(2) << "DoSendUpdateTransactionStatusLocationRpcs()";

    UniqueLock lock(mutex_);

    if (state_.load(std::memory_order_acquire) == TransactionState::kPromoting) {
      VLOG_WITH_PREFIX(1) << "Initial heartbeat to new status tablet has not completed yet, "
                          << "skipping UpdateTransactionStatusLocation rpcs for now";
      return;
    }

    if (transaction_status_move_tablets_.empty()) {
      auto old_status_tablet = old_status_tablet_;
      auto transaction = transaction_->shared_from_this();
      lock.unlock();
      VLOG_WITH_PREFIX(1) << "No participants to send transaction status location updates to";
      SendAbortToOldStatusTabletIfNeeded(TransactionRpcDeadline(), transaction, old_status_tablet);
      return;
    }

    std::vector<TabletId> participant_tablets;
    for (const auto& tablet_id : transaction_status_move_tablets_) {
      const auto& tablet_state = tablets_.find(tablet_id);
      CHECK(tablet_state != tablets_.end());
      if (tablet_state->second.num_completed_batches == 0) {
        VLOG_WITH_PREFIX(1) << "Tablet " << tablet_id
                            << " has no completed batches, skipping UpdateTransactionStatusLocation"
                            << " rpc for now";
        continue;
      }
      participant_tablets.push_back(tablet_id);
    }

    if (participant_tablets.empty()) {
      VLOG_WITH_PREFIX(1) << "No participants ready for transaction status location update yet";
      return;
    }

    for (const auto& tablet_id : participant_tablets) {
      transaction_status_move_tablets_.erase(tablet_id);
    }

    tserver::UpdateTransactionStatusLocationRequestPB req;
    req.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
    req.set_new_status_tablet_id(status_tablet_->tablet_id());

    lock.unlock();

    for (const auto& participant_tablet : participant_tablets) {
      VLOG_WITH_PREFIX(2) << "SendUpdateTransactionStatusLocationRpcs() to tablet "
                          << participant_tablet;
      LookupTabletForTransactionStatusLocationUpdate(weak_transaction, id, req, participant_tablet);
    }
  }

  void LookupTabletForTransactionStatusLocationUpdate(
      const std::weak_ptr<YBTransaction>& weak_transaction,
      const TransactionId& id,
      const tserver::UpdateTransactionStatusLocationRequestPB& request_template,
      const TabletId& tablet_id) EXCLUDES(mutex_) {
    TRACE_TO(trace_, __func__);
    manager_->client()->LookupTabletById(
        tablet_id,
        /* table =*/ nullptr,
        master::IncludeInactive::kFalse,
        master::IncludeDeleted::kFalse,
        TransactionRpcDeadline(),
        std::bind(
            &Impl::LookupTabletForTransactionStatusLocationUpdateDone, this, _1, weak_transaction,
            id, request_template, tablet_id),
        client::UseCache::kTrue);
  }

  rpc::Rpcs::Handle* GetTransactionStatusMoveHandle(const TabletId& tablet_id) REQUIRES(mutex_) {
    auto handle = transaction_status_move_handles_.find(tablet_id);
    DCHECK(handle != transaction_status_move_handles_.end());
    return &handle->second;
  }

  rpc::RpcCommandPtr PrepareUpdateTransactionStatusLocationRpc(
      const std::weak_ptr<YBTransaction>& weak_transaction,
      const TransactionId& id,
      const TabletId &tablet_id, internal::RemoteTabletPtr participant_tablet,
      const tserver::UpdateTransactionStatusLocationRequestPB& request_template) {
    tserver::UpdateTransactionStatusLocationRequestPB request = request_template;
    request.set_propagated_hybrid_time(manager_->Now().ToUint64());
    request.set_tablet_id(tablet_id);

    return UpdateTransactionStatusLocation(
        TransactionRpcDeadline(),
        participant_tablet.get(),
        manager_->client(),
        &request,
        std::bind(
            &Impl::UpdateTransactionStatusLocationDone, this, weak_transaction, id, tablet_id,
            participant_tablet, request_template, _1, _2));
  }

  void LookupTabletForTransactionStatusLocationUpdateDone(
      const Result<internal::RemoteTabletPtr>& result,
      const std::weak_ptr<YBTransaction>& weak_transaction,
      const TransactionId& id,
      const tserver::UpdateTransactionStatusLocationRequestPB& request_template,
      const TabletId& tablet_id) EXCLUDES(mutex_) {
    auto transaction = weak_transaction.lock();
    if (!transaction) {
      VLOG(1) << id << ": " << "Transaction destroyed, not sending status location updates";
      return;
    }

    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Lookup tablet done: " << yb::ToString(result);

    if (!result.ok()) {
      NotifyWaiters(result.status(), "Lookup tablet");
      return;
    }

    UniqueLock lock(mutex_);

    auto handle = GetTransactionStatusMoveHandle(tablet_id);
    auto rpc = PrepareUpdateTransactionStatusLocationRpc(
        weak_transaction, id, tablet_id, *result, request_template);

    lock.unlock();
    manager_->rpcs().RegisterAndStart(rpc, handle);
  }

  void UpdateTransactionStatusLocationDone(
      const std::weak_ptr<YBTransaction>& weak_transaction,
      const TransactionId& id,
      const TabletId &tablet_id, internal::RemoteTabletPtr participant_tablet,
      const tserver::UpdateTransactionStatusLocationRequestPB& request_template,
      const Status& status,
      const tserver::UpdateTransactionStatusLocationResponsePB& response) EXCLUDES(mutex_) {
    auto transaction = weak_transaction.lock();
    if (!transaction) {
      VLOG(1) << id << ": " << "Transaction destroyed before status location update finished";
      return;
    }

    TRACE_TO(trace_, __func__);
    VLOG_WITH_PREFIX(1) << "Transaction status update for participant tablet "
                        << tablet_id << ": " << yb::ToString(status);

    UniqueLock lock(mutex_);
    auto handle = GetTransactionStatusMoveHandle(tablet_id);
    manager_->rpcs().Unregister(handle);

    if (!status.ok()) {
      SetErrorUnlocked(status, "Move transaction status");
    }

    transaction_status_move_handles_.erase(tablet_id);

    if (!status.ok() || transaction_status_move_handles_.empty()) {
      auto old_status_tablet = old_status_tablet_;
      Waiter waiter = std::move(commit_waiter_);
      lock.unlock();
      if (waiter) {
        // Already started commit, so let that handle the old txn abort instead.
        waiter(status);
      } else {
        // Only abort early if status tablet lookup to the old status tablet has finished, and
        // if last heartbeat to old status tablet was successful - otherwise,
        // we might be in the case where first-op promotion has finished before initial status
        // tablet lookup - and we defer to when that has finished, or the old status tablet expired
        // the transaction prematurely, and we defer to the logic at commit time instead.
        if (!old_status_tablet) {
          VLOG_WITH_PREFIX(1) << "Initial status tablet lookup hasn't finished, not aborting yet";
        } else if (last_old_heartbeat_failed_.load(std::memory_order_acquire)) {
          VLOG_WITH_PREFIX(1) << "Heartbeats to old status tablet are failing, not aborting early";
        } else {
          auto transaction = transaction_->shared_from_this();
          SendAbortToOldStatusTabletIfNeeded(
              TransactionRpcDeadline(), transaction, old_status_tablet);
        }
      }
    }
  }

  void SetError(const Status& status, const char* operation) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    SetErrorUnlocked(status, operation);
  }

  void SetErrorUnlocked(const Status& status, const char* operation) REQUIRES(mutex_) {
    VLOG_WITH_PREFIX(1) << operation << " failed: " << status;
    if (status_.ok()) {
      status_ = status.CloneAndPrepend(operation);
      state_.store(TransactionState::kAborted, std::memory_order_release);

      if (FLAGS_log_failed_txn_metadata) {
        LOG_WITH_PREFIX(INFO) << operation << " failed, status=" << status
                              << ", metadata=" << AsString(metadata_)
                              << ", state=" << AsString(state_)
                              << ", old_status_tablet_state=" << AsString(old_status_tablet_state_)
                              << ", tablets=" << AsString(tablets_);
      }
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
      std::lock_guard lock(mutex_);
      child_txn_data_pb = PrepareChildTransactionDataUnlocked(transaction);
    }

    callback(child_txn_data_pb);
  }

  Status CheckCouldCommitUnlocked(SealOnly seal_only) REQUIRES(mutex_) {
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

  std::atomic<MicrosTime> start_;

  // Manager is created once per service.
  TransactionManager* const manager_;

  // Transaction related to this impl.
  YBTransaction* const transaction_;

  TransactionMetadata metadata_;
  ConsistentReadPoint read_point_;

  // Metadata tracking savepoint-related state for the scope of this transaction.
  YBSubTransaction subtransaction_;

  // Map to track number of mutations to different tables in each sub-transaction. On transaction
  // commit, this helps count the total mutations per table which is aggregated to the node level
  // mutation counter that is in turn sent to the auto analyze service that maintains the cluster
  // level mutations (see PgMutationCounter).
  std::unordered_map<SubTransactionId, std::unordered_map<TableId, uint64_t>>
      subtxn_table_mutation_counter_map_;

  std::atomic<bool> requested_status_tablet_{false};
  internal::RemoteTabletPtr status_tablet_ GUARDED_BY(mutex_);
  internal::RemoteTabletPtr old_status_tablet_ GUARDED_BY(mutex_);
  std::atomic<TransactionState> state_{TransactionState::kRunning};
  std::atomic<bool> last_old_heartbeat_failed_{false};

  // Transaction is successfully initialized and ready to process intents.
  const bool child_;
  const bool child_had_read_time_ = false;
  bool ready_ GUARDED_BY(mutex_) = false;
  CommitCallback commit_callback_ GUARDED_BY(mutex_);
  Status status_ GUARDED_BY(mutex_);

  // The following fields are initialized in CompleteConstruction() and can be used with no locking.
  TaggedLogPrefix log_prefix_;
  rpc::Rpcs::Handle heartbeat_handle_;
  rpc::Rpcs::Handle new_heartbeat_handle_;
  rpc::Rpcs::Handle commit_handle_;
  rpc::Rpcs::Handle abort_handle_;
  rpc::Rpcs::Handle old_abort_handle_;
  rpc::Rpcs::Handle rollback_heartbeat_handle_;
  rpc::Rpcs::Handle old_rollback_heartbeat_handle_;

  // Set of participant tablet ids that need to be informed about a move in transaction status
  // location.
  std::unordered_set<TabletId> transaction_status_move_tablets_ GUARDED_BY(mutex_);

  // RPC handles for informing participant tablets about a move in transaction status location.
  std::unordered_map<TabletId, rpc::Rpcs::Handle>
      transaction_status_move_handles_ GUARDED_BY(mutex_);

  std::shared_mutex mutex_;
  TabletStates tablets_ GUARDED_BY(mutex_);
  std::vector<Waiter> waiters_ GUARDED_BY(mutex_);

  // Commit waiter waiting for transaction status move related RPCs to finish.
  Waiter commit_waiter_ GUARDED_BY(mutex_);

  // Waiter for abort on old transaction status tablet to finish before cleaning up.
  Waiter cleanup_waiter_ GUARDED_BY(mutex_);

  std::atomic<OldTransactionState> old_status_tablet_state_{OldTransactionState::kNone};

  std::promise<Result<TransactionMetadata>> metadata_promise_ GUARDED_BY(mutex_);
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
  bool commit_replicated_ GUARDED_BY(mutex_) = false;

  scoped_refptr<Counter> transaction_promotions_;
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

Status YBTransaction::PromoteToGlobal(CoarseTimePoint deadline) {
  return impl_->PromoteToGlobal(AdjustDeadline(deadline));
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

std::shared_future<Result<TransactionMetadata>> YBTransaction::GetMetadata(
    CoarseTimePoint deadline) const {
  return impl_->GetMetadata(deadline);
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

void YBTransaction::EnsureTraceCreated() {
  return impl_->EnsureTraceCreated();
}

void YBTransaction::SetActiveSubTransaction(SubTransactionId id) {
  return impl_->SetActiveSubTransaction(id);
}

Status YBTransaction::RollbackToSubTransaction(SubTransactionId id, CoarseTimePoint deadline) {
  return impl_->RollbackToSubTransaction(id, deadline);
}

bool YBTransaction::HasSubTransaction(SubTransactionId id) {
  return impl_->HasSubTransaction(id);
}

Status YBTransaction::SetPgTxnStart(int64_t pg_txn_start_us) {
  return impl_->SetPgTxnStart(pg_txn_start_us);
}

void YBTransaction::IncreaseMutationCounts(
    SubTransactionId subtxn_id, const TableId& table_id, uint64_t mutation_count) {
  return impl_->IncreaseMutationCounts(subtxn_id, table_id, mutation_count);
}

std::unordered_map<TableId, uint64_t> YBTransaction::GetTableMutationCounts() const {
  return impl_->GetTableMutationCounts();
}

void YBTransaction::SetLogPrefixTag(const LogPrefixName& name, uint64_t value) {
  return impl_->SetLogPrefixTag(name, value);
}

} // namespace client
} // namespace yb
