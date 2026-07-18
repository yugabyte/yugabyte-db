// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/async_rpc_tasks_base.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_meta.h"

#include "yb/master/master.h"
#include "yb/master/master_test.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/source_location.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/threadpool.h"

using namespace std::literals;

DEFINE_RUNTIME_int32(unresponsive_ts_rpc_timeout_ms, 15 * 60 * 1000,  // 15 minutes
             "After this amount of time (or after we have retried unresponsive_ts_rpc_retry_limit "
             "times, whichever happens first), the master will stop attempting to contact a tablet "
             "server or master in order to perform operations such as deleting a tablet.");
TAG_FLAG(unresponsive_ts_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(unresponsive_ts_rpc_retry_limit, 20,
             "After this number of retries (or unresponsive_ts_rpc_timeout_ms expires, whichever "
             "happens first), the master will stop attempting to contact a tablet server or master "
             "in order to perform operations such as deleting a tablet.");
TAG_FLAG(unresponsive_ts_rpc_retry_limit, advanced);

DEFINE_RUNTIME_int32(retrying_ts_rpc_max_delay_ms, 60 * 1000,
             "Maximum delay between successive attempts to contact an unresponsive tablet server "
             "or master.");
TAG_FLAG(retrying_ts_rpc_max_delay_ms, advanced);

DEFINE_RUNTIME_int32(retrying_rpc_max_jitter_ms, 50,
    "Maximum random delay to add between rpc retry attempts.");
TAG_FLAG(retrying_rpc_max_jitter_ms, advanced);

DEFINE_test_flag(int32, slowdown_master_async_rpc_tasks_by_ms, 0,
                 "For testing purposes, slow down the run method to take longer.");

DECLARE_int32(master_ts_rpc_timeout_ms);

namespace yb {
namespace master {

using namespace std::placeholders;

using server::MonitoredTaskState;
using tserver::TabletServerErrorPB;

// ============================================================================
//  Class PickSpecificUUID.
// ============================================================================
Result<TSDescriptorPtr> PickSpecificUUID::PickReplica() {
  return master_->ts_manager()->LookupTSByUUID(ts_uuid_);
}

std::string ReplicaMapToString(const TabletReplicaMap& replicas) {
  std::string ret = "";
  for (const auto& [ts_uuid, _] : replicas) {
    if (!ret.empty()) {
      ret += ", ";
    } else {
      ret += "(";
    }
    ret += ts_uuid;
  }
  ret += ")";
  return ret;
}

// ============================================================================
//  Class PickLeaderReplica.
// ============================================================================
PickLeaderReplica::PickLeaderReplica(const TabletInfoPtr& tablet)
    : tablet_(tablet) {
}

Result<TSDescriptorPtr> PickLeaderReplica::PickReplica() {
  return tablet_->GetLeader();
}

// ============================================================================
//  Class RetryingRpcTask.
// ============================================================================

// Constructor. The 'async_task_throttler' parameter is optional and may be null if the task does
// not throttle.
RetryingRpcTask::RetryingRpcTask(
    Master* master,
    ThreadPool* callback_pool,
    AsyncTaskThrottlerBase* async_task_throttler)
    : master_(master),
      callback_pool_(callback_pool),
      async_task_throttler_(async_task_throttler),
      deadline_(UnresponsiveDeadline()) {}

RetryingRpcTask::~RetryingRpcTask() {
  auto state = state_.load(std::memory_order_acquire);
  LOG_IF(DFATAL, !IsStateTerminal(state))
      << "Destroying " << this << " task in a wrong state: " << AsString(state);
  VLOG_WITH_FUNC(1) << "Destroying " << this << " in " << AsString(state);
}

std::string RetryingRpcTask::LogPrefix() const {
  return Format("$0 (task=$1, state=$2): ", description(), static_cast<const void*>(this), state());
}

// Send the subclass RPC request.
Status RetryingRpcTask::Run() {
  VLOG_WITH_PREFIX(1) << "Start Running";
  attempt_start_ts_ = MonoTime::Now();
  ++attempt_;
  VLOG_WITH_PREFIX(1) << "Start Running, attempt: " << attempt_;
  for (;;) {
    auto task_state = state();
    if (task_state == MonitoredTaskState::kAborted) {
      return STATUS(IllegalState, "Unable to run task because it has been aborted");
    }
    if (task_state == MonitoredTaskState::kWaiting) {
      break;
    }

    LOG_IF_WITH_PREFIX(DFATAL, task_state != MonitoredTaskState::kScheduling)
        << "Expected task to be in kScheduling state but found: " << AsString(task_state);

    // We expect this case to be very rare, since we switching to waiting state right after
    // scheduling task on messenger. So just busy wait.
    std::this_thread::yield();
  }

  auto s = PickReplica();
  if (!s.ok()) {
    LOG_WITH_PREFIX(INFO) << "Unable to pick replica: " << s;
    auto opt_transition = HandleReplicaLookupFailure(s);
    if (opt_transition) {
      TransitionToTerminalState(
          MonitoredTaskState::kWaiting, opt_transition->first, opt_transition->second);
      UnregisterAsyncTask();
      return opt_transition->second;
    }
  } else {
    s = ResetProxies();
  }
  if (!s.ok()) {
    s = s.CloneAndPrepend("Failed to reset proxies");
    LOG_WITH_PREFIX(INFO) << s;
    if (s.IsExpired()) {
      TransitionToTerminalState(MonitoredTaskState::kWaiting, MonitoredTaskState::kFailed, s);
      UnregisterAsyncTask();
      return s;
    }
    if (RescheduleWithBackoffDelay()) {
      return Status::OK();
    }

    auto current_state = state();
    UnregisterAsyncTask(); // May call 'delete this'.

    if (current_state == MonitoredTaskState::kFailed) {
      return s;
    }
    if (current_state == MonitoredTaskState::kAborted) {
      return STATUS(IllegalState, "Unable to run task because it has been aborted");
    }

    LOG_WITH_PREFIX(FATAL) << "Failed to change task to MonitoredTaskState::kFailed state from "
                           << current_state;
  } else {
    rpc_.Reset();
  }

  // Calculate and set the timeout deadline.
  const MonoTime deadline = ComputeDeadline();
  rpc_.set_deadline(deadline);

  if (!PerformStateTransition(MonitoredTaskState::kWaiting, MonitoredTaskState::kRunning)) {
    if (state() == MonitoredTaskState::kAborted) {
      return STATUS(Aborted, "Unable to run task because it has been aborted");
    }

    LOG_WITH_PREFIX(DFATAL) <<
        "Task transition MonitoredTaskState::kWaiting -> MonitoredTaskState::kRunning failed";
    return Failed(STATUS_FORMAT(IllegalState, "Task in invalid state $0", state()));
  }

  auto slowdown_flag_val = FLAGS_TEST_slowdown_master_async_rpc_tasks_by_ms;
  if (PREDICT_FALSE(slowdown_flag_val> 0)) {
    VLOG_WITH_PREFIX(1) << "Slowing down by " << slowdown_flag_val << " ms.";
    bool old_thread_restriction = ThreadRestrictions::SetWaitAllowed(true);
    SleepFor(MonoDelta::FromMilliseconds(slowdown_flag_val));
    ThreadRestrictions::SetWaitAllowed(old_thread_restriction);
    VLOG_WITH_PREFIX(2) << "Slowing down done. Resuming.";
  }

  bool sent_request = false;
  if (async_task_throttler_ == nullptr || !async_task_throttler_->Throttle()) {
    sent_request = SendRequest(attempt_);

    // If the request failed to send, remove the task that was added in
    // async_task_throttler_->Throttle().
    if (async_task_throttler_ != nullptr && !sent_request) {
      async_task_throttler_->RemoveOutstandingTask();
    }
  } else {
    VLOG_WITH_PREFIX(2) << "Throttled request";
  }

  if (!sent_request && !RescheduleWithBackoffDelay()) {
    UnregisterAsyncTask();  // May call 'delete this'.
  }
  return Status::OK();
}

MonoTime RetryingRpcTask::UnresponsiveDeadline() const {
  return start_timestamp_ + FLAGS_unresponsive_ts_rpc_timeout_ms * 1ms;
}

MonoTime RetryingRpcTask::ComputeDeadline() const {
  MonoTime timeout = MonoTime::Now();
  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms));
  return MonoTime::Earliest(timeout, deadline_);
}

// Abort this task and return its value before it was successfully aborted. If the task entered
// a different terminal state before we were able to abort it, return that state.
MonitoredTaskState RetryingRpcTask::AbortAndReturnPrevState(
    const Status& status, bool call_task_finisher) {
  auto prev_state = state();
  while (!IsStateTerminal(prev_state)) {
    auto expected = prev_state;
    if (state_.compare_exchange_weak(expected, MonitoredTaskState::kAborted)) {
      VLOG_WITH_PREFIX_AND_FUNC(1)
          << "Aborted with: " << status << ", prev state: " << AsString(prev_state)
          << ", call_task_finisher: " << call_task_finisher;
      AbortIfScheduled();
      if (call_task_finisher) {
        Finished(status);
      }
      UnregisterAsyncTask();
      return prev_state;
    }
    prev_state = state();
  }
  VLOG_WITH_PREFIX_AND_FUNC(1)
      << "Already terminated, prev state: " << AsString(prev_state);
  UnregisterAsyncTask();
  return prev_state;
}

void RetryingRpcTask::AbortTask(const Status& status) {
  AbortAndReturnPrevState(status);
}

void RetryingRpcTask::RpcCallback() {
  if (async_task_throttler_ != nullptr) {
    async_task_throttler_->RemoveOutstandingTask();
  }

  // Defer the actual work of the callback off of the reactor thread.
  // This is necessary because our callbacks often do synchronous writes to
  // the catalog table, and we can't do synchronous IO on the reactor.
  //
  // Note: This can fail on shutdown, so just print a warning for it.
  Status s = callback_pool_->SubmitFunc(
      std::bind(&RetryingRpcTask::DoRpcCallback, shared_from(this)));
  VLOG_WITH_PREFIX_AND_FUNC(3) << "Submit status: " << s;
  if (!s.ok()) {
    WARN_NOT_OK(s, "Could not submit to queue, probably shutting down");
    AbortTask(s);
  }
}

int RetryingRpcTask::num_max_retries() { return FLAGS_unresponsive_ts_rpc_retry_limit; }

int RetryingRpcTask::max_delay_ms() {
  return FLAGS_retrying_ts_rpc_max_delay_ms;
}

bool RetryingRpcTask::RescheduleWithBackoffDelay() {
  auto task_state = state();
  if (task_state != MonitoredTaskState::kRunning &&
      // Allow kWaiting for task(s) that have never successfully ResetTSProxy().
      task_state != MonitoredTaskState::kWaiting) {
    if (task_state != MonitoredTaskState::kComplete) {
      LOG_WITH_PREFIX(INFO) << "No reschedule for this task: " << AsString(task_state);
    }
    return false;
  }

  int attempt_threshold = std::numeric_limits<int>::max();
  if (NoRetryTaskType()) {
    attempt_threshold = 0;
  } else if (RetryLimitTaskType()) {
    attempt_threshold = num_max_retries();
  }

  if (attempt_ > attempt_threshold) {
    auto status = STATUS_FORMAT(
        Aborted, "Reached maximum number of retries ($0)", attempt_threshold);
    LOG_WITH_PREFIX(WARNING)
        << status << " for request " << description()
        << ", task=" << this << " state=" << state();
    TransitionToFailedState(task_state, status);
    return false;
  }

  MonoTime now = MonoTime::Now();
  // We assume it might take 10ms to process the request in the best case,
  // fail if we have less than that amount of time remaining.
  int64_t millis_remaining = deadline_.GetDeltaSince(now).ToMilliseconds() - 10;
  // Exponential backoff with jitter.
  int64_t base_delay_ms;
  if (attempt_ <= 12) {
    // 1st retry delayed 2^4 ms, 2nd 2^5, etc.
    base_delay_ms = std::min(1 << (attempt_ + 3), max_delay_ms());
  } else {
    base_delay_ms = max_delay_ms();
  }

  // Normal rand is seeded by default with 1. Using the same for rand_r seed.
  unsigned int seed = 1;
  // Add up to FLAGS_retrying_rpc_max_jitter_ms of additional random delay.
  int64_t jitter_ms = rand_r(&seed) % FLAGS_retrying_rpc_max_jitter_ms;
  int64_t delay_millis = std::min<int64_t>(base_delay_ms + jitter_ms, millis_remaining);

  if (delay_millis <= 0) {
    auto status = STATUS(TimedOut, "Request timed out");
    LOG_WITH_PREFIX(WARNING) << status;
    TransitionToFailedState(task_state, status);
    return false;
  }

  LOG_WITH_PREFIX(INFO) << "Scheduling retry with a delay of " << delay_millis
                        << "ms (attempt = " << attempt_ << " / " << attempt_threshold << ")...";

  if (!PerformStateTransition(task_state, MonitoredTaskState::kScheduling)) {
    LOG_WITH_PREFIX(WARNING) << "Unable to mark this task as MonitoredTaskState::kScheduling";
    return false;
  }
  auto task_id_result = master_->messenger()->ScheduleOnReactor(
      std::bind(&RetryingRpcTask::RunDelayedTask, shared_from(this), _1),
      MonoDelta::FromMilliseconds(delay_millis), SOURCE_LOCATION());
  if (!task_id_result.ok()) {
    AbortTask(task_id_result.status());
    UnregisterAsyncTask();
    return false;
  }
  auto task_id = *task_id_result;

  VLOG_WITH_PREFIX_AND_FUNC(4) << "Task id: " << task_id;
  reactor_task_id_.store(task_id, std::memory_order_release);

  return TransitionToWaitingState(MonitoredTaskState::kScheduling);
}

void RetryingRpcTask::RunDelayedTask(const Status& status) {
  if (state() == MonitoredTaskState::kAborted) {
    UnregisterAsyncTask();  // May delete this.
    return;
  }

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Async tablet task failed or was cancelled: " << status;
    if (status.IsAborted() || status.IsServiceUnavailable()) {
      AbortTask(status);
    }
    UnregisterAsyncTask();  // May delete this.
    return;
  }

  auto log_prefix = LogPrefix(); // Save in case we need to log after deletion.
  Status s = Run();  // May delete this.
  if (!s.ok()) {
    LOG(WARNING) << log_prefix << "Async tablet task failed: " << s;
  }
}

void RetryingRpcTask::UnregisterAsyncTaskCallback() {}

void RetryingRpcTask::UpdateMetrics(scoped_refptr<Histogram> metric, MonoTime start_time,
                                      const std::string& metric_name,
                                      const std::string& metric_type) {
  metric->Increment(MonoTime::Now().GetDeltaSince(start_time).ToMicroseconds());
}

Status RetryingRpcTask::Failed(const Status& status) {
  LOG_WITH_PREFIX(WARNING) << "Async task failed: " << status;
  SaveFinalStatusAndCallFinished(status);
  UnregisterAsyncTask();
  return status;
}

void RetryingRpcTask::UnregisterAsyncTask() {
  // Retain a reference to the object, in case UnregisterAsyncTaskCallbackInternal would have
  // removed the last one.
  auto self = shared_from_this();
  std::unique_lock<decltype(unregister_mutex_)> lock(unregister_mutex_);
  UpdateMetrics(
      master_->GetMetric(type_name(), Master::TaskMetric, description()), start_timestamp_,
      type_name(), "task metric");

  auto s = state();
  if (!IsStateTerminal(s)) {
    LOG_WITH_PREFIX(FATAL) << "Invalid task state " << s;
  }
  completion_timestamp_.store(MonoTime::Now(), std::memory_order_release);
  UnregisterAsyncTaskCallbackInternal();
  // Make sure to run the callbacks last, in case they rely on the task no longer being tracked
  // by the table.
  UnregisterAsyncTaskCallback();
}

void RetryingRpcTask::AbortIfScheduled() {
  auto reactor_task_id = reactor_task_id_.load(std::memory_order_acquire);
  VLOG_WITH_PREFIX_AND_FUNC(1) << "Reactor task id: " << reactor_task_id;
  if (reactor_task_id != rpc::kInvalidTaskId) {
    master_->messenger()->AbortOnReactor(reactor_task_id);
  }
}

Status RetryingRpcTask::GetStatus() const {
  std::lock_guard<simple_spinlock> l(status_mutex_);
  return final_status_;
}

void RetryingRpcTask::SaveFinalStatusAndCallFinished(const Status& status) {
  {
    std::lock_guard<simple_spinlock> l(status_mutex_);
    final_status_ = status;
  }
  Finished(status);
}

void RetryingRpcTask::TransitionToTerminalState(MonitoredTaskState expected,
                                                  MonitoredTaskState terminal_state,
                                                  const Status& status) {
  if (!PerformStateTransition(expected, terminal_state)) {
    if (terminal_state != MonitoredTaskState::kAborted && state() == MonitoredTaskState::kAborted) {
      LOG_WITH_PREFIX(WARNING) << "Unable to perform transition " << expected << " -> "
                               << terminal_state << ". Task has been aborted";
    } else {
      LOG_WITH_PREFIX(DFATAL) << "State transition " << expected << " -> "
                              << terminal_state << " failed. Current task is in an invalid state: "
                              << state();
    }
    return;
  }

  SaveFinalStatusAndCallFinished(status);
}

void RetryingRpcTask::TransitionToFailedState(server::MonitoredTaskState expected,
                                                const yb::Status& status) {
  TransitionToTerminalState(expected, MonitoredTaskState::kFailed, status);
}

void RetryingRpcTask::TransitionToCompleteState() {
  TransitionToTerminalState(
      MonitoredTaskState::kRunning, MonitoredTaskState::kComplete, Status::OK());
}

bool RetryingRpcTask::TransitionToWaitingState(MonitoredTaskState expected) {
  if (!PerformStateTransition(expected, MonitoredTaskState::kWaiting)) {
    // The only valid reason for state not being MonitoredTaskState is because the task got
    // aborted.
    if (state() != MonitoredTaskState::kAborted) {
      LOG_WITH_PREFIX(FATAL) << "Unable to mark task as MonitoredTaskState::kWaiting";
    }
    AbortIfScheduled();
    return false;
  } else {
    return true;
  }
}

// ============================================================================
//  Class RetryingMasterRpcTask.
// ============================================================================

// Constructor. The 'async_task_throttler' parameter is optional and may be null if the task does
// not throttle.
RetryingMasterRpcTask::RetryingMasterRpcTask(
    Master* master,
    ThreadPool* callback_pool,
    consensus::RaftPeerPB&& peer,
    AsyncTaskThrottlerBase* async_task_throttler)
    : RetryingRpcTask(master, callback_pool, async_task_throttler),
      peer_{std::move(peer)} {}

// Handle the actual work of the RPC callback. This is run on the master's worker
// pool, rather than a reactor thread, so it may do blocking IO operations.
void RetryingMasterRpcTask::DoRpcCallback() {
  VLOG_WITH_PREFIX_AND_FUNC(3) << "Rpc status: " << rpc_.status();

  if (!rpc_.status().ok()) {
    LOG_WITH_PREFIX(WARNING) << "Master RPC " << type_name() << " failed: "
                             << rpc_.status().ToString();
  } else if (state() != MonitoredTaskState::kAborted) {
    HandleResponse(attempt_);  // Modifies state_.
  }
  UpdateMetrics(master_->GetMetric(type_name(), Master::AttemptMetric, description()),
                attempt_start_ts_, type_name(), "attempt metric");

  // Schedule a retry if the RPC call was not successful.
  if (RescheduleWithBackoffDelay()) {
    return;
  }

  UnregisterAsyncTask();  // May call 'delete this'.
}

Status RetryingMasterRpcTask::ResetProxies() {
  HostPort hostport = HostPortFromPB(DesiredHostPort(peer_, master_->MakeCloudInfoPB()));
  master_test_proxy_ = std::make_unique<MasterTestProxy>(&master_->proxy_cache(), hostport);
  master_cluster_proxy_ = std::make_unique<MasterClusterProxy>(&master_->proxy_cache(), hostport);
  return Status::OK();
}

// ============================================================================
//  Class RetryingTSRpcTask.
// ============================================================================

// Constructor. The 'async_task_throttler' parameter is optional and may be null if the task does
// not throttle.
RetryingTSRpcTask::RetryingTSRpcTask(
    Master* master,
    ThreadPool* callback_pool,
    std::unique_ptr<TSPicker>
        replica_picker,
    AsyncTaskThrottlerBase* async_task_throttler)
    : RetryingRpcTask(master, callback_pool, async_task_throttler),
      replica_picker_(std::move(replica_picker)) {}

Status RetryingTSRpcTask::PickReplica() {
  std::lock_guard l(target_ts_mutex_);
  target_ts_desc_ = VERIFY_RESULT(replica_picker_->PickReplica());
  return Status::OK();
}

TSDescriptorPtr RetryingTSRpcTask::target_ts_desc() const {
  std::lock_guard l(target_ts_mutex_);
  return target_ts_desc_;
}

TabletServerId RetryingTSRpcTask::permanent_uuid() const {
  auto target_ts = target_ts_desc();
  return target_ts ? target_ts->id() : "";
}

// Handle the actual work of the RPC callback. This is run on the master's worker
// pool, rather than a reactor thread, so it may do blocking IO operations.
void RetryingTSRpcTask::DoRpcCallback() {
  VLOG_WITH_PREFIX_AND_FUNC(3) << "Rpc status: " << rpc_.status();
  auto target_ts = target_ts_desc();

  if (!rpc_.status().ok()) {
    LOG_WITH_PREFIX(WARNING) << "TS " << target_ts_desc() << ": " << type_name()
                             << " RPC failed for tablet " << tablet_id() << ": "
                             << rpc_.status().ToString();
    if (!RetryTaskAfterRPCFailure(rpc_.status())) {
      TransitionToCompleteState();
    }
  } else if (state() != MonitoredTaskState::kAborted) {
    HandleResponse(attempt_);  // Modifies state_.
  }
  UpdateMetrics(master_->GetMetric(type_name(), Master::AttemptMetric, description()),
                attempt_start_ts_, type_name(), "attempt metric");

  // Schedule a retry if the RPC call was not successful.
  if (RescheduleWithBackoffDelay()) {
    return;
  }

  UnregisterAsyncTask();  // May call 'delete this'.
}

bool RetryingTSRpcTask::RetryTaskAfterRPCFailure(const Status& status) {
  return true;
}

Status RetryingTSRpcTask::ResetProxies() {
  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy;
  std::shared_ptr<tserver::TabletServerAdminServiceProxy> ts_admin_proxy;
  std::shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy;
  std::shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy;

  auto target_ts = target_ts_desc();
  RETURN_NOT_OK(target_ts->GetProxy(&ts_proxy));
  RETURN_NOT_OK(target_ts->GetProxy(&ts_admin_proxy));
  RETURN_NOT_OK(target_ts->GetProxy(&consensus_proxy));
  RETURN_NOT_OK(target_ts->GetProxy(&ts_backup_proxy));

  ts_proxy_.swap(ts_proxy);
  ts_admin_proxy_.swap(ts_admin_proxy);
  consensus_proxy_.swap(consensus_proxy);
  ts_backup_proxy_.swap(ts_backup_proxy);

  return Status::OK();
}

// ============================================================================
//  Class RetryingTSRpcTaskWithTable.
// ============================================================================
RetryingTSRpcTaskWithTable::RetryingTSRpcTaskWithTable(Master *master,
    ThreadPool* callback_pool,
    std::unique_ptr<TSPicker> replica_picker,
    scoped_refptr<TableInfo> table,
    LeaderEpoch epoch,
    AsyncTaskThrottlerBase* async_task_throttler)
    : RetryingTSRpcTask(master, callback_pool, std::move(replica_picker), async_task_throttler),
      table_(table),
      epoch_(std::move(epoch)) {}

RetryingTSRpcTaskWithTable::~RetryingTSRpcTaskWithTable() {}

std::string RetryingTSRpcTaskWithTable::table_name() const {
  return table_ ? table_->ToString() : "";
}

void RetryingTSRpcTaskWithTable::UnregisterAsyncTaskCallbackInternal() {
  if (table_ != nullptr && table_->RemoveTask(shared_from_this())) {
    // We don't delete a table while it has running tasks, so we should check whether this task was
    // the last task associated with the table, even it is not a delete table task.
    master_->catalog_manager()->CheckTableDeleted(table_, epoch_);
  }
}

// ============================================================================
//  Class AsyncTabletLeaderTask.
// ============================================================================
AsyncTabletLeaderTask::AsyncTabletLeaderTask(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)),
          tablet->table(), std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet) {}

AsyncTabletLeaderTask::AsyncTabletLeaderTask(
    Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
    const scoped_refptr<TableInfo>& table, LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)), table,
          std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet) {
}

AsyncTabletLeaderTask::~AsyncTabletLeaderTask() = default;

std::string AsyncTabletLeaderTask::description() const {
  return Format("$0 RPC for tablet $1 ($2)", type_name(), tablet_, table_name());
}

TabletId AsyncTabletLeaderTask::tablet_id() const {
  return tablet_->tablet_id();
}

}  // namespace master
}  // namespace yb
