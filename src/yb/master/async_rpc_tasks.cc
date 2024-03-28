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

#include "yb/master/async_rpc_tasks.h"
#include <memory>

#include "yb/common/common_types.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_meta.h"

#include "yb/consensus/metadata.pb.h"
#include "yb/gutil/map-util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_health_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/source_location.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
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

DEFINE_RUNTIME_int32(
    ysql_clone_pg_schema_rpc_timeout_ms, 10 * 60 * 1000,  // 10 min.
    "Timeout used by the master when attempting to clone PG Schema objects using an async task to "
    "tserver");
TAG_FLAG(ysql_clone_pg_schema_rpc_timeout_ms, advanced);

DEFINE_test_flag(int32, slowdown_master_async_rpc_tasks_by_ms, 0,
                 "For testing purposes, slow down the run method to take longer.");

DEFINE_test_flag(bool, stuck_add_tablet_to_table_task_enabled, false, "description");

DEFINE_test_flag(bool, fail_async_delete_replica_task, false,
                 "When set, transition all delete replica tasks to a failed state.");

// The flags are defined in catalog_manager.cc.
DECLARE_int32(master_ts_rpc_timeout_ms);
DECLARE_int32(tablet_creation_timeout_ms);
DECLARE_int32(TEST_slowdown_alter_table_rpcs_ms);

namespace yb {
namespace master {

using namespace std::placeholders;

using std::string;
using std::shared_ptr;
using std::vector;

using strings::Substitute;
using consensus::RaftPeerPB;
using server::MonitoredTaskState;
using server::MonitoredTaskType;
using tserver::TabletServerErrorPB;

// ============================================================================
//  Class PickSpecificUUID.
// ============================================================================
Status PickSpecificUUID::PickReplica(TSDescriptor** ts_desc) {
  shared_ptr<TSDescriptor> ts;
  if (!master_->ts_manager()->LookupTSByUUID(ts_uuid_, &ts)) {
    return STATUS(NotFound, "unknown tablet server id", ts_uuid_);
  }
  *ts_desc = ts.get();
  return Status::OK();
}

string ReplicaMapToString(const TabletReplicaMap& replicas) {
  string ret = "";
  for (const auto& r : replicas) {
    if (!ret.empty()) {
      ret += ", ";
    } else {
      ret += "(";
    }
    ret += r.second.ts_desc->permanent_uuid();
  }
  ret += ")";
  return ret;
}

// ============================================================================
//  Class PickLeaderReplica.
// ============================================================================
PickLeaderReplica::PickLeaderReplica(const scoped_refptr<TabletInfo>& tablet)
    : tablet_(tablet) {
}

Status PickLeaderReplica::PickReplica(TSDescriptor** ts_desc) {
  *ts_desc = VERIFY_RESULT(tablet_->GetLeader());
  return Status::OK();
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
      deadline_(start_timestamp_ + FLAGS_unresponsive_ts_rpc_timeout_ms * 1ms) {}

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

  auto slowdown_flag_val = GetAtomicFlag(&FLAGS_TEST_slowdown_master_async_rpc_tasks_by_ms);
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

MonoTime RetryingRpcTask::ComputeDeadline() {
  MonoTime timeout = MonoTime::Now();
  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms));
  return MonoTime::Earliest(timeout, deadline_);
}

// Abort this task and return its value before it was successfully aborted. If the task entered
// a different terminal state before we were able to abort it, return that state.
MonitoredTaskState RetryingRpcTask::AbortAndReturnPrevState(const Status& status) {
  auto prev_state = state();
  while (!IsStateTerminal(prev_state)) {
    auto expected = prev_state;
    if (state_.compare_exchange_weak(expected, MonitoredTaskState::kAborted)) {
      VLOG_WITH_PREFIX_AND_FUNC(1)
          << "Aborted with: " << status << ", prev state: " << AsString(prev_state);
      AbortIfScheduled();
      Finished(status);
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
  Finished(status);
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

  Finished(status);
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
  return replica_picker_->PickReplica(&target_ts_desc_);
}

// Handle the actual work of the RPC callback. This is run on the master's worker
// pool, rather than a reactor thread, so it may do blocking IO operations.
void RetryingTSRpcTask::DoRpcCallback() {
  VLOG_WITH_PREFIX_AND_FUNC(3) << "Rpc status: " << rpc_.status();

  if (!rpc_.status().ok()) {
    // TODO: Move this implementation-specific handling out of the base class.
    LOG_WITH_PREFIX(WARNING) << "TS " << target_ts_desc_->permanent_uuid() << ": "
                             << type_name() << " RPC failed for tablet "
                             << tablet_id() << ": " << rpc_.status().ToString();
    if (type() == MonitoredTaskType::kBackendsCatalogVersionTs && rpc_.status().IsRemoteError() &&
        rpc_.status().message().ToBuffer().find("invalid method name:") != std::string::npos) {
      LOG_WITH_PREFIX(WARNING)
          << "TS " << target_ts_desc_->permanent_uuid() << " is on an older version that doesn't"
          << " support backends catalog version RPC. Ignoring.";
      TransitionToCompleteState();
    } else if (type() == MonitoredTaskType::kDeleteReplica && !target_ts_desc_->IsLive()) {
      LOG_WITH_PREFIX(WARNING)
          << "TS " << target_ts_desc_->permanent_uuid() << ": delete failed for tablet "
          << tablet_id() << ". TS is DEAD. No further retry.";
      TransitionToCompleteState();
    } else if (type() == MonitoredTaskType::kBackendsCatalogVersionTs &&
               !target_ts_desc_->HasYsqlCatalogLease()) {
      // A similar check is done in BackendsCatalogVersionTS::HandleResponse.  This check is hit
      // when this RPC failed and tserver's lease expired.  That check is hit when this RPC
      // succeeded and tserver's lease is expired.
      LOG_WITH_PREFIX(WARNING)
          << "TS " << target_ts_desc_->permanent_uuid() << " catalog lease expired. Assume backends"
          << " on that TS will be resolved to sufficient catalog version";
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

Status RetryingTSRpcTask::ResetProxies() {
  shared_ptr<tserver::TabletServerServiceProxy> ts_proxy;
  shared_ptr<tserver::TabletServerAdminServiceProxy> ts_admin_proxy;
  shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy;
  shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy;

  RETURN_NOT_OK(target_ts_desc_->GetProxy(&ts_proxy));
  RETURN_NOT_OK(target_ts_desc_->GetProxy(&ts_admin_proxy));
  RETURN_NOT_OK(target_ts_desc_->GetProxy(&consensus_proxy));
  RETURN_NOT_OK(target_ts_desc_->GetProxy(&ts_backup_proxy));

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

string RetryingTSRpcTaskWithTable::table_name() const {
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
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)),
          tablet->table(), std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet) {}

AsyncTabletLeaderTask::AsyncTabletLeaderTask(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
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

TabletServerId AsyncTabletLeaderTask::permanent_uuid() const {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

// ============================================================================
//  Class AsyncCreateReplica.
// ============================================================================
AsyncCreateReplica::AsyncCreateReplica(Master *master,
                                       ThreadPool *callback_pool,
                                       const string& permanent_uuid,
                                       const scoped_refptr<TabletInfo>& tablet,
                                       const std::vector<SnapshotScheduleId>& snapshot_schedules,
                                       LeaderEpoch epoch)
  : RetrySpecificTSRpcTaskWithTable(master, callback_pool, permanent_uuid, tablet->table(),
                           std::move(epoch), /* async_task_throttler */ nullptr),
    tablet_id_(tablet->tablet_id()) {
  deadline_ = start_timestamp_;
  deadline_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_tablet_creation_timeout_ms));

  auto table_lock = tablet->table()->LockForRead();
  const auto& table_pb = table_lock->pb;
  const auto& tablet_pb = tablet->metadata().dirty().pb;

  req_.set_dest_uuid(permanent_uuid);
  req_.set_table_id(tablet->table()->id());
  req_.set_tablet_id(tablet->tablet_id());
  req_.set_table_type(tablet->table()->metadata().state().pb.table_type());
  req_.mutable_partition()->CopyFrom(tablet_pb.partition());
  req_.set_namespace_id(table_pb.namespace_id());
  req_.set_namespace_name(table_pb.namespace_name());
  req_.set_pg_table_id(table_pb.pg_table_id());
  req_.set_table_name(table_pb.name());
  req_.mutable_schema()->CopyFrom(table_pb.schema());
  req_.mutable_partition_schema()->CopyFrom(table_pb.partition_schema());
  req_.mutable_config()->CopyFrom(tablet_pb.committed_consensus_state().config());
  req_.set_colocated(tablet_pb.colocated());
  if (table_pb.has_index_info()) {
    req_.mutable_index_info()->CopyFrom(table_pb.index_info());
  }
  if (table_pb.has_wal_retention_secs()) {
    req_.set_wal_retention_secs(table_pb.wal_retention_secs());
  }
  auto& req_schedules = *req_.mutable_snapshot_schedules();
  req_schedules.Reserve(narrow_cast<int>(snapshot_schedules.size()));
  for (const auto& id : snapshot_schedules) {
    req_schedules.Add()->assign(id.AsSlice().cdata(), id.size());
  }

  req_.mutable_hosted_stateful_services()->CopyFrom(table_pb.hosted_stateful_services());
}

std::string AsyncCreateReplica::description() const {
  return Format("CreateTablet RPC for tablet $0 ($1) on TS=$2",
                tablet_id_, table_name(), permanent_uuid_);
}

void AsyncCreateReplica::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (s.IsAlreadyPresent()) {
      LOG_WITH_PREFIX(INFO) << "CreateTablet RPC for tablet " << tablet_id_
                            << " on TS " << permanent_uuid_ << " returned already present: "
                            << s;
      TransitionToCompleteState();
    } else {
      LOG_WITH_PREFIX(WARNING) << "CreateTablet RPC for tablet " << tablet_id_
                               << " on TS " << permanent_uuid_ << " failed: " << s;
    }

    return;
  }

  TransitionToCompleteState();
  VLOG_WITH_PREFIX(1) << "TS " << permanent_uuid_ << ": complete on tablet " << tablet_id_;
}

bool AsyncCreateReplica::SendRequest(int attempt) {
  ts_admin_proxy_->CreateTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send create tablet request to " << permanent_uuid_ << ":\n"
                      << " (attempt " << attempt << "):\n"
                      << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncMasterTabletHealthTask.
// ============================================================================
AsyncMasterTabletHealthTask::AsyncMasterTabletHealthTask(
    Master* master,
    ThreadPool* callback_pool,
    consensus::RaftPeerPB&& peer,
    std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler)
    : RetryingMasterRpcTask(
          master, callback_pool, std::move(peer), /* async_task_throttler */ nullptr),
      cb_handler_{std::move(cb_handler)} {}

void AsyncMasterTabletHealthTask::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "AsyncMasterTabletHealthTask RPC on "
                               << peer_.ShortDebugString() << " failed: " << s;
    }
    return;
  }
  cb_handler_->ReportHealthCheck(resp_, peer_.permanent_uuid());
  TransitionToCompleteState();
}

std::string AsyncMasterTabletHealthTask::description() const {
  return "Check tablet health for tablets on master " + peer_.ShortDebugString();
}

bool AsyncMasterTabletHealthTask::SendRequest(int attempt) {
  master_cluster_proxy_->CheckMasterTabletHealthAsync(req_, &resp_, &rpc_, BindRpcCallback());
  return true;
}

// ============================================================================
//  Class AsyncTserverTabletHealthTask.
// ============================================================================
AsyncTserverTabletHealthTask::AsyncTserverTabletHealthTask(
    Master* master,
    ThreadPool* callback_pool,
    std::string permanent_uuid,
    std::vector<TabletId>&& tablets,
    std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler)
  : RetrySpecificTSRpcTask(
      master, callback_pool, std::move(permanent_uuid), /* async_task_throttler */ nullptr),
    cb_handler_{std::move(cb_handler)} {
  for (auto& tablet_id : tablets) {
    req_.add_tablet_ids(std::move(tablet_id));
  }
}

void AsyncTserverTabletHealthTask::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "CheckTserverTabletHealth RPC on TS "
                               << permanent_uuid_ << " failed: " << s;
    }
    return;
  }
  cb_handler_->ReportHealthCheck(resp_, permanent_uuid_);
  TransitionToCompleteState();
}

std::string AsyncTserverTabletHealthTask::description() const {
  return "Check tablet health for tablets on TS " + permanent_uuid_;
}

bool AsyncTserverTabletHealthTask::SendRequest(int attempt) {
  ts_proxy_->CheckTserverTabletHealthAsync(req_, &resp_, &rpc_, BindRpcCallback());
  return true;
}

// ============================================================================
//  Class AsyncStartElection.
// ============================================================================
AsyncStartElection::AsyncStartElection(Master *master,
                                       ThreadPool *callback_pool,
                                       const string& permanent_uuid,
                                       const scoped_refptr<TabletInfo>& tablet,
                                       bool initial_election,
                                       LeaderEpoch epoch)
  : RetrySpecificTSRpcTaskWithTable(master, callback_pool, permanent_uuid,
        tablet->table(), std::move(epoch), /* async_task_throttler */ nullptr),
    tablet_id_(tablet->tablet_id()) {
  deadline_ = start_timestamp_;
  deadline_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_tablet_creation_timeout_ms));

  req_.set_dest_uuid(permanent_uuid_);
  req_.set_tablet_id(tablet_id_);
  req_.set_initial_election(initial_election);
}

void AsyncStartElection::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status s = StatusFromPB(resp_.error().status());
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "RunLeaderElection RPC for tablet " << tablet_id_
                               << " on TS " << permanent_uuid_ << " failed: " << s;
    }

    return;
  }

  TransitionToCompleteState();
}

std::string AsyncStartElection::description() const {
  return Format("RunLeaderElection RPC for tablet $0 ($1) on TS=$2",
                tablet_id_, table_name(), permanent_uuid_);
}

bool AsyncStartElection::SendRequest(int attempt) {
  LOG_WITH_PREFIX(INFO) << Format(
      "Hinted Leader start election at $0 for tablet $1, attempt $2",
      permanent_uuid_, tablet_id_, attempt);
  consensus_proxy_->RunLeaderElectionAsync(req_, &resp_, &rpc_, BindRpcCallback());

  return true;
}

// ============================================================================
//  Class AsyncPrepareDeleteTransactionTablet.
// ============================================================================
AsyncPrepareDeleteTransactionTablet::AsyncPrepareDeleteTransactionTablet(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
    const scoped_refptr<TableInfo>& table, const scoped_refptr<TabletInfo>& tablet,
    const std::string& msg, HideOnly hide_only, LeaderEpoch epoch)
    : RetrySpecificTSRpcTaskWithTable(master, callback_pool, permanent_uuid, table,
                             std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet), msg_(msg), hide_only_(hide_only) {}

void AsyncPrepareDeleteTransactionTablet::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    TabletServerErrorPB::Code code = resp_.error().code();
    switch (code) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": prepare delete failed for tablet "
            << tablet_id() << " because the tablet was not found. No further retry: "
            << status.ToString();
        TransitionToCompleteState();
        break;
      case TabletServerErrorPB::WRONG_SERVER_UUID:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": prepare delete failed for tablet "
            << tablet_id() << " due to an incorrect UUID. No further retry: "
            << status.ToString();
        TransitionToCompleteState();
        break;
      default:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": prepare delete failed for tablet "
            << tablet_id() << " with error code " << TabletServerErrorPB::Code_Name(code)
            << ": " << status.ToString();
        break;
    }
  } else {
    if (table_) {
      LOG_WITH_PREFIX(INFO)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id()
          << " (table " << table_->ToString() << ") successfully done";
    } else {
      LOG_WITH_PREFIX(WARNING)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id()
          << " did not belong to a known table, but was prepared for deletion";
    }
    TransitionToCompleteState();
    VLOG_WITH_PREFIX(1) << "TS " << permanent_uuid_ << ": complete on tablet " << tablet_id();
  }
}

std::string AsyncPrepareDeleteTransactionTablet::description() const {
  return Format("PrepareDeleteTransactionTablet RPC for tablet $0 ($1) on TS=$2",
                tablet_id(), table_name(), permanent_uuid_);
}

TabletId AsyncPrepareDeleteTransactionTablet::tablet_id() const {
  return tablet_->tablet_id();
}

bool AsyncPrepareDeleteTransactionTablet::SendRequest(int attempt) {
  tserver::PrepareDeleteTransactionTabletRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_tablet_id(tablet_id());

  ts_admin_proxy_->PrepareDeleteTransactionTabletAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send prepare delete transaction tablet request for "
                      << tablet_id() << " to " << permanent_uuid_
                      << " (attempt " << attempt << "):\n"
                      << req.DebugString();
  return true;
}

void AsyncPrepareDeleteTransactionTablet::UnregisterAsyncTaskCallback() {
  // Only notify if we are in a success state.
  if (state() == MonitoredTaskState::kComplete) {
    master_->catalog_manager()->NotifyPrepareDeleteTransactionTabletFinished(
        tablet_, msg_, hide_only_, epoch());
  }
}

// ============================================================================
//  Class AsyncDeleteReplica.
// ============================================================================
Status AsyncDeleteReplica::SetPendingDelete(AddPendingDelete add_pending_delete) {
  TSDescriptorPtr ts_desc;
  if (!master_->ts_manager()->LookupTSByUUID(permanent_uuid_, &ts_desc)) {
    return STATUS(IllegalState, Format("Could not find tserver with uuid $0", permanent_uuid_));
  }

  if (add_pending_delete) {
    ts_desc->AddPendingTabletDelete(tablet_id());
  } else {
    ts_desc->ClearPendingTabletDelete(tablet_id());
  }
  return Status::OK();
}

Status AsyncDeleteReplica::BeforeSubmitToTaskPool() {
  return SetPendingDelete(AddPendingDelete::kTrue);
}

Status AsyncDeleteReplica::OnSubmitFailure() {
  return SetPendingDelete(AddPendingDelete::kFalse);
}

void AsyncDeleteReplica::HandleResponse(int attempt) {
  if (FLAGS_TEST_fail_async_delete_replica_task) {
    auto s = STATUS(IllegalState, "TEST_fail_async_delete_replica_task set to true");
    TransitionToFailedState(MonitoredTaskState::kRunning, s);
    return;
  }

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    TabletServerErrorPB::Code code = resp_.error().code();
    switch (code) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " because the tablet was not found. No further retry: "
            << status.ToString();
        TransitionToCompleteState();
        break;
      case TabletServerErrorPB::CAS_FAILED:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " due to a CAS failure. No further retry: " << status.ToString();
        TransitionToCompleteState();
        break;
      case TabletServerErrorPB::WRONG_SERVER_UUID:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " due to an incorrect UUID. No further retry: " << status.ToString();
        TransitionToCompleteState();
        break;
      default:
        LOG_WITH_PREFIX(WARNING)
            << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
            << " with error code " << TabletServerErrorPB::Code_Name(code)
            << ": " << status.ToString();
        break;
    }
  } else {
    if (table_) {
      LOG_WITH_PREFIX(INFO)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id_
          << " (table " << table_->ToString() << ") successfully done";
    } else {
      LOG_WITH_PREFIX(WARNING)
          << "TS " << permanent_uuid_ << ": tablet " << tablet_id_
          << " did not belong to a known table, but was successfully deleted";
    }
    TransitionToCompleteState();
    VLOG_WITH_PREFIX(1) << "TS " << permanent_uuid_ << ": complete on tablet " << tablet_id_;
  }
}

std::string AsyncDeleteReplica::description() const {
  return Format("$0Tablet RPC for tablet $1 ($2) on TS=$3",
                hide_only_ ? "Hide" : "Delete", tablet_id_, table_name(), permanent_uuid_);
}

bool AsyncDeleteReplica::SendRequest(int attempt) {
  tserver::DeleteTabletRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_tablet_id(tablet_id_);
  req.set_reason(reason_);
  req.set_delete_type(delete_type_);
  if (hide_only_) {
    req.set_hide_only(hide_only_);
  }
  if (keep_data_) {
    req.set_keep_data(keep_data_);
  }
  if (cas_config_opid_index_less_or_equal_) {
    req.set_cas_config_opid_index_less_or_equal(*cas_config_opid_index_less_or_equal_);
  }
  bool should_abort_active_txns = !table() ||
                                  table()->LockForRead()->started_deleting();
  req.set_should_abort_active_txns(should_abort_active_txns);

  ts_admin_proxy_->DeleteTabletAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send delete tablet request to " << permanent_uuid_
                      << " (attempt " << attempt << "):\n"
                      << req.DebugString();
  return true;
}

void AsyncDeleteReplica::UnregisterAsyncTaskCallback() {
  // Only notify if we are in a success state.
  if (state() == MonitoredTaskState::kComplete || state() == MonitoredTaskState::kFailed) {
    master_->catalog_manager()->NotifyTabletDeleteFinished(
        permanent_uuid_, tablet_id_, table(), epoch(), state());
  }
}

// ============================================================================
//  Class AsyncAlterTable.
// ============================================================================
void AsyncAlterTable::HandleResponse(int attempt) {
  if (PREDICT_FALSE(FLAGS_TEST_slowdown_alter_table_rpcs_ms > 0)) {
    VLOG_WITH_PREFIX(1) << "Sleeping for " << tablet_->tablet_id()
                        << FLAGS_TEST_slowdown_alter_table_rpcs_ms
                        << "ms before returning response in async alter table request handler";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_alter_table_rpcs_ms));
  }

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    LOG_WITH_PREFIX(WARNING) << "TS " << permanent_uuid() << " failed: "
                             << status << " for version " << schema_version_;

    // Do not retry on a fatal error
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
      case TabletServerErrorPB::MISMATCHED_SCHEMA:
      case TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA:
        TransitionToCompleteState();
        break;
      default:
        break;
    }
  } else {
    // CDC SDK Create Stream Context
    // Technically, handling the CDCSDK snapshot flow is part of AlterTable processing. So it is
    // done before transitioning to complete state.
    // If there is an error while populating the cdc_state table, it can be ignored here
    // as it will be handled in CatalogManager::CreateNewXReplStream
    if (cdc_sdk_stream_id_) {
      auto sync_point_tablet = tablet_id();
      TEST_SYNC_POINT_CALLBACK("AsyncAlterTable::CDCSDKCreateStream", &sync_point_tablet);

      if (resp_.has_cdc_sdk_snapshot_safe_op_id() && resp_.has_propagated_hybrid_time()) {
        WARN_NOT_OK(
            master_->catalog_manager()->PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails(
                table(), tablet_id(), cdc_sdk_stream_id_, resp_.cdc_sdk_snapshot_safe_op_id(),
                HybridTime::FromPB(resp_.propagated_hybrid_time()),
                cdc_sdk_require_history_cutoff_),
            Format(
              "$0 failed while populating cdc_state table in AsyncAlterTable::HandleResponse. "
              "Response $1", description(),
              resp_.ShortDebugString()));
      } else {
        LOG(WARNING) << "Response not as expected. Not inserting any rows into "
                     << "cdc_state table for stream_id: " << cdc_sdk_stream_id_
                     << " and tablet id: " << tablet_id();
      }
    }

    TransitionToCompleteState();
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << " completed: for version " << schema_version_;
  }

  server::UpdateClock(resp_, master_->clock());

  if (state() == MonitoredTaskState::kComplete) {
    // TODO: proper error handling here. Not critical, since TSHeartbeat will retry on failure.
    WARN_NOT_OK(
        master_->catalog_manager()->HandleTabletSchemaVersionReport(
            tablet_.get(), schema_version_, epoch(), table()),
        Format(
            "$0 failed while running AsyncAlterTable::HandleResponse. Response $1", description(),
            resp_.ShortDebugString()));
  } else {
    VLOG_WITH_PREFIX(1) << "Task is not completed " << tablet_->ToString() << " for version "
                        << schema_version_;
  }
}

TableType AsyncAlterTable::table_type() const {
  return tablet_->table()->GetTableType();
}

bool AsyncAlterTable::SendRequest(int attempt) {
  VLOG_WITH_PREFIX(1) << "Send alter table request to " << permanent_uuid() << " for "
                      << tablet_->tablet_id() << " waiting for a read lock.";

  tablet::ChangeMetadataRequestPB req;
  {
    auto l = table_->LockForRead();
    VLOG_WITH_PREFIX(1) << "Send alter table request to " << permanent_uuid() << " for "
                        << tablet_->tablet_id() << " obtained the read lock.";

    req.set_schema_version(l->pb.version());
    req.set_dest_uuid(permanent_uuid());
    req.set_tablet_id(tablet_->tablet_id());
    req.set_alter_table_id(table_->id());

    if (l->pb.has_wal_retention_secs()) {
      req.set_wal_retention_secs(l->pb.wal_retention_secs());
    }

    // Support for CDC SDK Create Stream context
    if (cdc_sdk_stream_id_) {
      req.set_retention_requester_id(cdc_sdk_stream_id_.ToString());
      req.set_cdc_sdk_require_history_cutoff(cdc_sdk_require_history_cutoff_);
    }

    req.mutable_schema()->CopyFrom(l->pb.schema());
    req.set_new_table_name(l->pb.name());
    req.mutable_indexes()->CopyFrom(l->pb.indexes());
    req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());

    if (table_type() == TableType::PGSQL_TABLE_TYPE && !transaction_id_.IsNil()) {
      VLOG_WITH_PREFIX(1) << "Transaction ID is provided for tablet " << tablet_->tablet_id()
          << " with ID " << transaction_id_.ToString() << " for ALTER TABLE operation";
      req.set_should_abort_active_txns(true);
      req.set_transaction_id(transaction_id_.ToString());
    }

    schema_version_ = l->pb.version();
  }

  ts_admin_proxy_->AlterSchemaAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Send alter table request to " << permanent_uuid() << " for " << tablet_->tablet_id()
      << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

bool AsyncBackfillDone::SendRequest(int attempt) {
  VLOG_WITH_PREFIX(1)
      << "Send alter table request to " << permanent_uuid() << " for " << tablet_->tablet_id()
      << " version " << schema_version_ << " waiting for a read lock.";

  tablet::ChangeMetadataRequestPB req;
  {
    auto l = table_->LockForRead();
    VLOG_WITH_PREFIX(1)
        << "Send alter table request to " << permanent_uuid() << " for " << tablet_->tablet_id()
        << " version " << schema_version_ << " obtained the read lock.";

    req.set_backfill_done_table_id(table_id_);
    req.set_dest_uuid(permanent_uuid());
    req.set_tablet_id(tablet_->tablet_id());
    req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
    req.set_mark_backfill_done(true);
    schema_version_ = l->pb.version();
  }

  ts_admin_proxy_->BackfillDoneAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Send backfill done request to " << permanent_uuid() << " for " << tablet_->tablet_id()
      << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncTruncate.
// ============================================================================
void AsyncTruncate::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const Status s = StatusFromPB(resp_.error().status());
    const TabletServerErrorPB::Code code = resp_.error().code();
    LOG_WITH_PREFIX(WARNING)
        << "TS " << permanent_uuid() << ": truncate failed for tablet " << tablet_id()
        << " with error code " << TabletServerErrorPB::Code_Name(code) << ": " << s;
  } else {
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << ": truncate complete on tablet " << tablet_id();
    TransitionToCompleteState();
  }

  server::UpdateClock(resp_, master_->clock());
}

bool AsyncTruncate::SendRequest(int attempt) {
  tserver::TruncateRequestPB req;
  req.set_tablet_id(tablet_id());
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_proxy_->TruncateAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send truncate tablet request to " << permanent_uuid()
                      << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

// ============================================================================
//  Class CommonInfoForRaftTask.
// ============================================================================
CommonInfoForRaftTask::CommonInfoForRaftTask(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
    const consensus::ConsensusStatePB& cstate, const string& change_config_ts_uuid,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)),
          tablet->table(), std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet),
      cstate_(cstate),
      change_config_ts_uuid_(change_config_ts_uuid) {
  deadline_ = MonoTime::Max();  // Never time out.
}

CommonInfoForRaftTask::~CommonInfoForRaftTask() = default;

TabletId CommonInfoForRaftTask::tablet_id() const {
  return tablet_->tablet_id();
}

TabletServerId CommonInfoForRaftTask::permanent_uuid() const {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

// ============================================================================
//  Class AsyncChangeConfigTask.
// ============================================================================
string AsyncChangeConfigTask::description() const {
  return Format(
      "$0 RPC for tablet $1 ($2) on peer $3 with cas_config_opid_index $4", type_name(),
      tablet_->tablet_id(), table_name(), permanent_uuid(), cstate_.config().opid_index());
}

bool AsyncChangeConfigTask::SendRequest(int attempt) {
  // Bail if we're retrying in vain.
  int64_t latest_index;
  {
    auto tablet_lock = tablet_->LockForRead();
    latest_index = tablet_lock->pb.committed_consensus_state().config().opid_index();
    // Adding this logic for a race condition that occurs in this scenario:
    // 1. CatalogManager receives a DeleteTable request and sends DeleteTablet requests to the
    // tservers, but doesn't yet update the tablet in memory state to not running.
    // 2. The CB runs and sees that this tablet is still running, sees that it is over-replicated
    // (since the placement now dictates it should have 0 replicas),
    // but before it can send the ChangeConfig RPC to a tserver.
    // 3. That tserver processes the DeleteTablet request.
    // 4. The ChangeConfig RPC now returns tablet not found,
    // which prompts an infinite retry of the RPC.
    bool tablet_running = tablet_lock->is_running();
    if (!tablet_running) {
      AbortTask(STATUS(Aborted, "Tablet is not running"));
      return false;
    }
  }
  if (latest_index > cstate_.config().opid_index()) {
    auto status = STATUS_FORMAT(
        Aborted,
        "Latest config for has opid_index of $0 while this task has opid_index of $1",
        latest_index, cstate_.config().opid_index());
    LOG_WITH_PREFIX(INFO) << status;
    AbortTask(status);
    return false;
  }

  // Logging should be covered inside based on failure reasons.
  auto prepare_status = PrepareRequest(attempt);
  if (!prepare_status.ok()) {
    AbortTask(prepare_status);
    return false;
  }

  consensus_proxy_->ChangeConfigAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Task " << description() << " sent request:\n" << req_.DebugString();
  return true;
}

void AsyncChangeConfigTask::HandleResponse(int attempt) {
  if (!resp_.has_error()) {
    TransitionToCompleteState();
    LOG_WITH_PREFIX(INFO) << Substitute(
        "Change config succeeded on leader TS $0 for tablet $1 with type $2 for replica $3",
        permanent_uuid(), tablet_->tablet_id(), type_name(), change_config_ts_uuid_);
    return;
  }

  Status status = StatusFromPB(resp_.error().status());

  // Do not retry on some known errors, otherwise retry forever or until cancelled.
  switch (resp_.error().code()) {
    case TabletServerErrorPB::CAS_FAILED:
    case TabletServerErrorPB::ADD_CHANGE_CONFIG_ALREADY_PRESENT:
    case TabletServerErrorPB::REMOVE_CHANGE_CONFIG_NOT_PRESENT:
    case TabletServerErrorPB::NOT_THE_LEADER:
      LOG_WITH_PREFIX(WARNING) << "ChangeConfig() failed on leader " << permanent_uuid()
                               << ". No further retry: " << status.ToString();
      TransitionToCompleteState();
      break;
    default:
      LOG_WITH_PREFIX(INFO) << "ChangeConfig() failed on leader " << permanent_uuid()
                            << " due to error "
                            << TabletServerErrorPB::Code_Name(resp_.error().code())
                            << ". This operation will be retried. Error detail: "
                            << status.ToString();
      break;
  }
}

// ============================================================================
//  Class AsyncAddServerTask.
// ============================================================================
Status AsyncAddServerTask::PrepareRequest(int attempt) {
  // Select the replica we wish to add to the config.
  // Do not include current members of the config.
  std::unordered_set<string> replica_uuids;
  for (const RaftPeerPB& peer : cstate_.config().peers()) {
    InsertOrDie(&replica_uuids, peer.permanent_uuid());
  }
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  shared_ptr<TSDescriptor> replacement_replica;
  for (auto ts_desc : ts_descs) {
    if (ts_desc->permanent_uuid() == change_config_ts_uuid_) {
      // This is given by the client, so we assume it is a well chosen uuid.
      replacement_replica = ts_desc;
      break;
    }
  }
  if (PREDICT_FALSE(!replacement_replica)) {
    auto status = STATUS_FORMAT(
        TimedOut, "Could not find desired replica $0 in live set", change_config_ts_uuid_);
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  req_.set_dest_uuid(permanent_uuid());
  req_.set_tablet_id(tablet_->tablet_id());
  req_.set_type(consensus::ADD_SERVER);
  req_.set_cas_config_opid_index(cstate_.config().opid_index());
  RaftPeerPB* peer = req_.mutable_server();
  peer->set_permanent_uuid(replacement_replica->permanent_uuid());
  peer->set_member_type(member_type_);
  TSRegistrationPB peer_reg = replacement_replica->GetRegistration();

  if (peer_reg.common().private_rpc_addresses().empty()) {
    auto status = STATUS_FORMAT(
        IllegalState, "Candidate replacement $0 has no registered rpc address: $1",
        replacement_replica->permanent_uuid(), peer_reg);
    YB_LOG_EVERY_N(WARNING, 100) << LogPrefix() << status;
    return status;
  }

  TakeRegistration(peer_reg.mutable_common(), peer);

  return Status::OK();
}

// ============================================================================
//  Class AsyncRemoveServerTask.
// ============================================================================
Status AsyncRemoveServerTask::PrepareRequest(int attempt) {
  bool found = false;
  for (const RaftPeerPB& peer : cstate_.config().peers()) {
    if (change_config_ts_uuid_ == peer.permanent_uuid()) {
      found = true;
    }
  }

  if (!found) {
    auto status = STATUS_FORMAT(
        NotFound, "Asked to remove TS with uuid $0 but could not find it in config peers!",
        change_config_ts_uuid_);
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  req_.set_dest_uuid(permanent_uuid());
  req_.set_tablet_id(tablet_->tablet_id());
  req_.set_type(consensus::REMOVE_SERVER);
  req_.set_cas_config_opid_index(cstate_.config().opid_index());
  RaftPeerPB* peer = req_.mutable_server();
  peer->set_permanent_uuid(change_config_ts_uuid_);

  return Status::OK();
}

// ============================================================================
//  Class AsyncTryStepDown.
// ============================================================================
Status AsyncTryStepDown::PrepareRequest(int attempt) {
  LOG_WITH_PREFIX(INFO) << Substitute("Prep Leader step down $0, leader_uuid=$1, change_ts_uuid=$2",
                                      attempt, permanent_uuid(), change_config_ts_uuid_);
  if (attempt > 1) {
    return STATUS(RuntimeError, "Retry is not allowed");
  }

  // If we were asked to remove the server even if it is the leader, we have to call StepDown, but
  // only if our current leader is the server we are asked to remove.
  if (permanent_uuid() != change_config_ts_uuid_) {
    auto status = STATUS_FORMAT(
        IllegalState,
        "Incorrect state config leader $0 does not match target uuid $1 for a leader step down op",
        permanent_uuid(), change_config_ts_uuid_);
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  stepdown_req_.set_dest_uuid(change_config_ts_uuid_);
  stepdown_req_.set_tablet_id(tablet_->tablet_id());
  if (!new_leader_uuid_.empty()) {
    stepdown_req_.set_new_leader_uuid(new_leader_uuid_);
  }

  return Status::OK();
}

bool AsyncTryStepDown::SendRequest(int attempt) {
  auto prepare_status = PrepareRequest(attempt);
  if (!prepare_status.ok()) {
    AbortTask(prepare_status);
    return false;
  }

  LOG_WITH_PREFIX(INFO) << Substitute("Stepping down leader $0 for tablet $1",
                                      change_config_ts_uuid_, tablet_->tablet_id());
  consensus_proxy_->LeaderStepDownAsync(
      stepdown_req_, &stepdown_resp_, &rpc_, BindRpcCallback());

  return true;
}

void AsyncTryStepDown::HandleResponse(int attempt) {
  if (!rpc_.status().ok()) {
    AbortTask(rpc_.status());
    LOG_WITH_PREFIX(WARNING) << Substitute(
        "Got error on stepdown for tablet $0 with leader $1, attempt $2 and error $3",
        tablet_->tablet_id(), permanent_uuid(), attempt, rpc_.status().ToString());

    return;
  }

  TransitionToCompleteState();
  const bool stepdown_failed = stepdown_resp_.has_error() &&
                               stepdown_resp_.error().status().code() != AppStatusPB::OK;
  LOG_WITH_PREFIX(INFO) << Format(
      "Leader step down done attempt=$0, leader_uuid=$1, change_uuid=$2, "
      "error=$3, failed=$4, should_remove=$5 for tablet $6.",
      attempt, permanent_uuid(), change_config_ts_uuid_, stepdown_resp_.error(),
      stepdown_failed, should_remove_, tablet_->tablet_id());

  if (stepdown_failed) {
    tablet_->RegisterLeaderStepDownFailure(change_config_ts_uuid_,
        MonoDelta::FromMilliseconds(stepdown_resp_.has_time_since_election_failure_ms() ?
                                    stepdown_resp_.time_since_election_failure_ms() : 0));
  }

  if (should_remove_) {
    auto task = std::make_shared<AsyncRemoveServerTask>(
        master_, callback_pool_, tablet_, cstate_, change_config_ts_uuid_, epoch());
    tablet_->table()->AddTask(task);
    Status status = task->Run();
    WARN_NOT_OK(status, "Failed to send new RemoveServer request");
  }
}

// ============================================================================
//  Class AsyncAddTableToTablet.
// ============================================================================
AsyncAddTableToTablet::AsyncAddTableToTablet(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
    const scoped_refptr<TableInfo>& table, LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<PickLeaderReplica>(tablet), table.get(),
          std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet),
      tablet_id_(tablet->tablet_id()) {
  req_.set_tablet_id(tablet->id());
  auto& add_table = *req_.mutable_add_table();
  add_table.set_table_id(table_->id());
  add_table.set_table_name(table_->name());
  add_table.set_table_type(table_->GetTableType());
  {
    auto l = table->LockForRead();
    add_table.set_schema_version(l->pb.version());
    *add_table.mutable_schema() = l->pb.schema();
    *add_table.mutable_partition_schema() = l->pb.partition_schema();
  }
}

string AsyncAddTableToTablet::description() const {
  return Substitute("AddTableToTablet RPC ($0) ($1)", table_->ToString(), tablet_->ToString());
}

void AsyncAddTableToTablet::HandleResponse(int attempt) {
  if (!rpc_.status().ok()) {
    AbortTask(rpc_.status());
    LOG_WITH_PREFIX(WARNING) << Substitute(
        "Got error when adding table $0 to tablet $1, attempt $2 and error $3",
        table_->ToString(), tablet_->ToString(), attempt, rpc_.status().ToString());
    return;
  }
  if (resp_.has_error()) {
    LOG_WITH_PREFIX(WARNING) << "AddTableToTablet() responded with error code "
                             << TabletServerErrorPB_Code_Name(resp_.error().code());
    switch (resp_.error().code()) {
      case TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE: FALLTHROUGH_INTENDED;
      case TabletServerErrorPB::NOT_THE_LEADER:
        TransitionToWaitingState(MonitoredTaskState::kRunning);
        break;
      default:
        TransitionToCompleteState();
        break;
    }

    return;
  }

  DCHECK(table_->AreAllTabletsRunning());
  VLOG_WITH_FUNC(1) << "Marking table " << table_->ToString() << " as RUNNING";
  Status s = master_->catalog_manager()->PromoteTableToRunningState(table_, epoch());
  if (!s.ok()) {
    LOG(WARNING) << "Error updating table " << table_->ToString() << ": " << s;
    TransitionToFailedState(MonitoredTaskState::kRunning, s);
    return;
  }

  TransitionToCompleteState();
}

bool AsyncAddTableToTablet::SendRequest(int attempt) {
  if (PREDICT_FALSE(FLAGS_TEST_stuck_add_tablet_to_table_task_enabled)) {
    LOG_WITH_FUNC(WARNING) << "Causing the task to get stuck";
    return true;
  }

  ts_admin_proxy_->AddTableToTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Send AddTableToTablet request (attempt " << attempt << "):\n" << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncRemoveTableFromTablet.
// ============================================================================
AsyncRemoveTableFromTablet::AsyncRemoveTableFromTablet(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
    const scoped_refptr<TableInfo>& table, LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<PickLeaderReplica>(tablet), table.get(),
          std::move(epoch), /* async_task_throttler */ nullptr),
      tablet_(tablet),
      tablet_id_(tablet->tablet_id()) {
  req_.set_tablet_id(tablet->id());
  req_.set_remove_table_id(table->id());
}

string AsyncRemoveTableFromTablet::description() const {
  return Substitute("RemoveTableFromTablet RPC ($0) ($1)", table_->ToString(), tablet_->ToString());
}

void AsyncRemoveTableFromTablet::HandleResponse(int attempt) {
  if (!rpc_.status().ok()) {
    AbortTask(rpc_.status());
    LOG_WITH_PREFIX(WARNING) << Substitute(
        "Got error when removing table $0 from tablet $1, attempt $2 and error $3",
        table_->ToString(), tablet_->ToString(), attempt, rpc_.status().ToString());
    return;
  }
  if (resp_.has_error()) {
    LOG_WITH_PREFIX(WARNING) << "RemoveTableFromTablet() responded with error code "
                             << TabletServerErrorPB_Code_Name(resp_.error().code());
    switch (resp_.error().code()) {
      case TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE: FALLTHROUGH_INTENDED;
      case TabletServerErrorPB::NOT_THE_LEADER:
        TransitionToWaitingState(MonitoredTaskState::kRunning);
        break;
      default:
        TransitionToCompleteState();
        break;
    }
  } else {
    TransitionToCompleteState();
  }
}

bool AsyncRemoveTableFromTablet::SendRequest(int attempt) {
  ts_admin_proxy_->RemoveTableFromTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send RemoveTableFromTablet request (attempt " << attempt << "):\n"
                      << req_.DebugString();
  return true;
}

namespace {

// These are errors that we are unlikely to recover from by retrying the GetSplitKey or SplitTablet
// RPC task. Automatic splits that receive these errors may still be retried in the next run, so we
// should try to not trigger splits that might hit these errors.
bool ShouldRetrySplitTabletRPC(const Status& s) {
  return !(s.IsInvalidArgument() || s.IsNotFound() || s.IsNotSupported() || s.IsIncomplete());
}

} // namespace

// ============================================================================
//  Class AsyncGetTabletSplitKey.
// ============================================================================
AsyncGetTabletSplitKey::AsyncGetTabletSplitKey(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
    const ManualSplit is_manual_split, LeaderEpoch epoch, DataCallbackType result_cb)
  : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)), result_cb_(result_cb) {
  req_.set_tablet_id(tablet_id());
  req_.set_is_manual_split(is_manual_split);
}

void AsyncGetTabletSplitKey::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const Status s = StatusFromPB(resp_.error().status());
    const TabletServerErrorPB::Code code = resp_.error().code();
    LOG_WITH_PREFIX(INFO) << "TS " << permanent_uuid() << ": GetSplitKey (attempt " << attempt
                          << ") failed for tablet " << tablet_id() << " with error code "
                          << TabletServerErrorPB::Code_Name(code) << ": " << s;
    if (!ShouldRetrySplitTabletRPC(s) ||
        (s.IsIllegalState() && code != tserver::TabletServerErrorPB::NOT_THE_LEADER)) {
      // It can happen that tablet leader has completed post-split compaction after previous split,
      // but followers have not yet completed post-split compaction.
      // Catalog manager decides to split again and sends GetTabletSplitKey RPC, but tablet leader
      // changes due to some reason and new tablet leader is not yet compacted.
      // In this case we get IllegalState error and we don't want to retry until post-split
      // compaction happened on leader. Once post-split compaction is done, CatalogManager will
      // resend RPC.
      //
      // Another case for IsIllegalState is trying to split a tablet that has all the data with
      // the same hash_code or the same doc_key, in this case we also don't want to retry RPC
      // automatically.
      // See https://github.com/yugabyte/yugabyte-db/issues/9159.
      TransitionToFailedState(state(), s);
    }
  } else {
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << ": got split key for tablet " << tablet_id();
    TransitionToCompleteState();
  }

  server::UpdateClock(resp_, master_->clock());
}

bool AsyncGetTabletSplitKey::SendRequest(int attempt) {
  req_.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_proxy_->GetSplitKeyAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Sent get split key request to " << permanent_uuid() << " (attempt " << attempt << "):\n"
      << req_.DebugString();
  return true;
}

void AsyncGetTabletSplitKey::Finished(const Status& status) {
  if (result_cb_) {
    if (status.ok()) {
      result_cb_(Data{resp_.split_encoded_key(), resp_.split_partition_key()});
    } else {
      result_cb_(status);
    }
  }
}

// ============================================================================
//  Class AsyncSplitTablet.
// ============================================================================
AsyncSplitTablet::AsyncSplitTablet(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
    const std::array<TabletId, kNumSplitParts>& new_tablet_ids,
    const std::string& split_encoded_key, const std::string& split_partition_key,
                                   LeaderEpoch epoch)
  : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)) {
  req_.set_tablet_id(tablet_id());
  req_.set_new_tablet1_id(new_tablet_ids[0]);
  req_.set_new_tablet2_id(new_tablet_ids[1]);
  req_.set_split_encoded_key(split_encoded_key);
  req_.set_split_partition_key(split_partition_key);
}

void AsyncSplitTablet::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    const Status s = StatusFromPB(resp_.error().status());
    const TabletServerErrorPB::Code code = resp_.error().code();
    LOG_WITH_PREFIX(WARNING) << "TS " << permanent_uuid() << ": split (attempt " << attempt
                             << ") failed for tablet " << tablet_id() << " with error code "
                             << TabletServerErrorPB::Code_Name(code) << ": " << s;
    if (s.IsAlreadyPresent()) {
      TransitionToCompleteState();
    } else if (!ShouldRetrySplitTabletRPC(s)) {
      TransitionToFailedState(state(), s);
    }
  } else {
    VLOG_WITH_PREFIX(1)
        << "TS " << permanent_uuid() << ": split complete on tablet " << tablet_id();
    TransitionToCompleteState();
  }

  server::UpdateClock(resp_, master_->clock());
}

bool AsyncSplitTablet::SendRequest(int attempt) {
  req_.set_dest_uuid(permanent_uuid());
  req_.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_admin_proxy_->SplitTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1)
      << "Sent split tablet request to " << permanent_uuid() << " (attempt " << attempt << "):\n"
      << req_.DebugString();
  return true;
}

// ============================================================================
//  Class AsyncUpdateTransactionTablesVersion.
// ============================================================================
AsyncUpdateTransactionTablesVersion::AsyncUpdateTransactionTablesVersion(
    Master* master,
    ThreadPool* callback_pool,
    const TabletServerId& ts_uuid,
    uint64_t version,
    StdStatusCallback callback)
    : RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      version_(version),
      callback_(std::move(callback)) {}

std::string AsyncUpdateTransactionTablesVersion::description() const {
  return "Update transaction tables version RPC";
}

void AsyncUpdateTransactionTablesVersion::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    LOG(WARNING) << "Updating transaction tables version on TS " << permanent_uuid_ << "failed: "
                 << status;
    return;
  }

  TransitionToCompleteState();
}

bool AsyncUpdateTransactionTablesVersion::SendRequest(int attempt) {
  tserver::UpdateTransactionTablesVersionRequestPB req;
  req.set_version(version_);
  ts_admin_proxy_->UpdateTransactionTablesVersionAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Send transaction tables version update to " << permanent_uuid_;
  return true;
}

void AsyncUpdateTransactionTablesVersion::Finished(const Status& status) {
  callback_(status);
}

// ============================================================================
//  Class AsyncTsTestRetry.
// ============================================================================
AsyncTsTestRetry::AsyncTsTestRetry(
    Master* master,
    ThreadPool* callback_pool,
    const TabletServerId& ts_uuid,
    const int32_t num_retries,
    StdStatusCallback callback)
    : RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      num_retries_(num_retries),
      callback_(std::move(callback)) {}

string AsyncTsTestRetry::description() const {
  return Format("$0 TsTestRetry RPC", permanent_uuid_);
}

void AsyncTsTestRetry::HandleResponse(int attempt) {
  server::UpdateClock(resp_, master_->clock());

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    LOG(INFO) << "TEST: TS " << permanent_uuid_ << ": test retry failed: " << status.ToString();
    return;
  }

  callback_(Status::OK());
  TransitionToCompleteState();
}

bool AsyncTsTestRetry::SendRequest(int attempt) {
  tserver::TestRetryRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  req.set_num_retries(num_retries_);

  ts_admin_proxy_->TestRetryAsync(req, &resp_, &rpc_, BindRpcCallback());
  return true;
}


// ============================================================================
//  Class AsyncMasterTestRetry.
// ============================================================================
AsyncMasterTestRetry::AsyncMasterTestRetry(
    Master* master, ThreadPool* callback_pool, consensus::RaftPeerPB&& peer, int32_t num_retries,
    StdStatusCallback callback)
    : RetryingMasterRpcTask(master, callback_pool, std::move(peer)),
      num_retries_(num_retries),
      callback_(std::move(callback)) {}

std::string AsyncMasterTestRetry::description() const {
  return Format("$0 MasterTestRetry RPC", peer_.permanent_uuid());
}

void AsyncMasterTestRetry::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());
    LOG(INFO) << "AsyncMasterTestRetry failed: " << status.ToString();
    return;
  }

  callback_(Status::OK());
  TransitionToCompleteState();
}

bool AsyncMasterTestRetry::SendRequest(int attempt) {
  master::TestRetryRequestPB req;
  req.set_dest_uuid(peer_.permanent_uuid());
  req.set_num_retries(num_retries_);

  master_test_proxy_->TestRetryAsync(req, &resp_, &rpc_, BindRpcCallback());
  return true;
}

// ============================================================================
//  Class AsyncCloneTablet.
// ============================================================================
AsyncCloneTablet::AsyncCloneTablet(
    Master* master,
    ThreadPool* callback_pool,
    const TabletInfoPtr& tablet,
    LeaderEpoch epoch,
    tablet::CloneTabletRequestPB req)
    : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)),
      req_(std::move(req)) {}

std::string AsyncCloneTablet::description() const {
  return "Clone tablet RPC";
}

void AsyncCloneTablet::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());
    LOG(WARNING) << "CloneTablet for tablet " << tablet_id() << "failed: "
                 << status;
    return;
  }

  TransitionToCompleteState();
}

bool AsyncCloneTablet::SendRequest(int attempt) {
  req_.set_dest_uuid(permanent_uuid());
  req_.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_admin_proxy_->CloneTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Sent clone tablets request to " << tablet_id();
  return true;
}

// ============================================================================
//  Class AsyncClonePgSchema.
// ============================================================================
AsyncClonePgSchema::AsyncClonePgSchema(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
    const std::string& source_db_name, const std::string& target_db_name, HybridTime restore_ht,
    ClonePgSchemaCallbackType callback, MonoTime deadline)
    : RetrySpecificTSRpcTask(
          master, callback_pool, std::move(permanent_uuid), /* async_task_throttler */ nullptr),
      source_db_name_(source_db_name),
      target_db_name(target_db_name),
      restore_ht_(restore_ht),
      callback_(callback) {
  deadline_ = deadline;  // Time out according to earliest(deadline_,
                         // time of sending request + ysql_clone_pg_schema_rpc_timeout_ms).
}

std::string AsyncClonePgSchema::description() const { return "Async Clone PG Schema RPC"; }

void AsyncClonePgSchema::HandleResponse(int attempt) {
  Status resp_status;
  if (resp_.has_error()) {
    resp_status = StatusFromPB(resp_.error().status());
    LOG(WARNING) << "Clone PG Schema Objects for source database: " << source_db_name_
                 << " failed: " << resp_status;
    TransitionToFailedState(state(), resp_status);
  } else {
    resp_status = Status::OK();
    TransitionToCompleteState();
  }
  WARN_NOT_OK(callback_(resp_status), "Failed to execute the call back of AsyncClonePgSchema");
}

bool AsyncClonePgSchema::SendRequest(int attempt) {
  tserver::ClonePgSchemaRequestPB req;
  req.set_source_db_name(source_db_name_);
  req.set_target_db_name(target_db_name);
  req.set_restore_ht(restore_ht_.ToUint64());
  ts_admin_proxy_->ClonePgSchemaAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Sent clone tablets request to " << tablet_id();
  return true;
}

MonoTime AsyncClonePgSchema::ComputeDeadline() { return deadline_; }

}  // namespace master
}  // namespace yb
