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

#include "yb/tserver/xcluster_poller.h"
#include "yb/client/client_fwd.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/strings/split.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/xcluster_ddl_queue_handler.h"

#include "yb/cdc/xcluster_rpc.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/client/client.h"

#include "yb/consensus/opid_util.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/util/callsite_profiling.h"
#include "yb/tserver/xcluster_consumer_auto_flags_info.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/threadpool.h"
#include "yb/util/unique_lock.h"

// Similar heuristic to heartbeat_interval in heartbeater.cc.
DEFINE_RUNTIME_int32(async_replication_polling_delay_ms, 0,
    "How long to delay in ms between applying and polling.");

DEFINE_RUNTIME_int32(async_replication_idle_delay_ms, 100,
    "How long to delay between polling when we expect no data at the destination.");

DEFINE_RUNTIME_uint32(async_replication_max_idle_wait, 3,
    "Maximum number of consecutive empty GetChanges until the poller "
    "backs off to the idle interval, rather than immediately retrying.");

DEFINE_RUNTIME_uint32(replication_failure_delay_exponent, 16 /* ~ 2^16/1000 ~= 65 sec */,
    "Max number of failures (N) to use when calculating exponential backoff (2^N-1).");

DEFINE_RUNTIME_bool(cdc_consumer_use_proxy_forwarding, false,
    "When enabled, read requests from the XClusterConsumer that go to the wrong node are "
    "forwarded to the correct node by the Producer.");

DEFINE_RUNTIME_uint32(xcluster_poller_task_delay_considered_stuck_secs, 3600 /* 1 hours */,
    "Maximum amount of time between tasks of a xcluster poller above which it is considered as "
    "stuck.");

DEFINE_test_flag(int32, xcluster_simulated_lag_ms, 0,
    "Simulate lag in xcluster replication. Replication is paused if set to -1.");
DEFINE_test_flag(string, xcluster_simulated_lag_tablet_filter, "",
    "Comma separated list of producer tablet ids. If non empty, simulate lag in only applied to "
    "this list of tablets.");

DEFINE_test_flag(bool, cdc_skip_replication_poll, false,
                 "If true, polling will be skipped.");

DEFINE_test_flag(bool, xcluster_simulate_get_changes_response_error, false,
                 "Simulate xCluster GetChanges returning an error.");

DEFINE_test_flag(bool, xcluster_disable_poller_term_check, false,
    "If true, the poller will not check the leader term.");

DEFINE_test_flag(
    double, xcluster_simulate_random_failure_after_apply, 0,
    "If non-zero, simulate a random failure after writing rows to the target tablet.");

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_bool(enable_xcluster_stat_collection);
DECLARE_bool(enable_xcluster_auto_flag_validation);
DECLARE_int32(xcluster_safe_time_update_interval_secs);

using namespace std::placeholders;

#define RETURN_WHEN_OFFLINE \
  do { \
    if (IsOffline()) { \
      LOG_WITH_PREFIX(WARNING) << "XCluster Poller went offline"; \
      return; \
    } \
  } while (false)

// CompleteShutdown gets and releases the lock after setting shutdown_. Here we lock the mutex
// before checking shutdown_ to make sure we do not run after CompleteShutdown.
#define ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN \
  std::lock_guard data_mutex_lock(data_mutex_); \
  RETURN_WHEN_OFFLINE; \
  do { \
    if (!IsLeaderTermValid()) { \
      return; \
    } \
  } while (false)

using namespace std::chrono_literals;

namespace yb {
namespace tserver {

XClusterPoller::XClusterPoller(
    const xcluster::ProducerTabletInfo& producer_tablet_info,
    const xcluster::ConsumerTabletInfo& consumer_tablet_info,
    const NamespaceId& consumer_namespace_id,
    std::shared_ptr<const AutoFlagsCompatibleVersion> auto_flags_version, ThreadPool* thread_pool,
    rpc::Rpcs* rpcs, client::YBClient& local_client,
    const std::shared_ptr<XClusterClient>& producer_client, XClusterConsumer* xcluster_consumer,
    SchemaVersion last_compatible_consumer_schema_version, int64_t leader_term,
    std::function<int64_t(const TabletId&)> get_leader_term)
    : XClusterAsyncExecutor(thread_pool, local_client.messenger(), rpcs),
      producer_tablet_info_(producer_tablet_info),
      consumer_tablet_info_(consumer_tablet_info),
      consumer_namespace_id_(consumer_namespace_id),
      poller_id_(
          producer_tablet_info.replication_group_id, consumer_tablet_info.table_id,
          producer_tablet_info.tablet_id, leader_term),
      auto_flags_version_(std::move(auto_flags_version)),
      op_id_(consensus::MinimumOpId()),
      validated_schema_version_(0),
      last_compatible_consumer_schema_version_(last_compatible_consumer_schema_version),
      get_leader_term_(std::move(get_leader_term)),
      local_client_(local_client),
      producer_client_(producer_client),
      xcluster_consumer_(xcluster_consumer),
      producer_safe_time_(HybridTime::kInvalid) {
  DCHECK_NE(GetLeaderTerm(), yb::OpId::kUnknownTerm);
}

XClusterPoller::~XClusterPoller() {
  VLOG(1) << "Destroying XClusterPoller";
  DCHECK(shutdown_);
}

void XClusterPoller::Init(bool use_local_tserver, rocksdb::RateLimiter* rate_limiter) {
  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  output_client_ = CreateXClusterOutputClient(
      this, consumer_tablet_info_, producer_tablet_info_, local_client_, thread_pool_, rpcs_,
      use_local_tserver, rate_limiter);
}

void XClusterPoller::InitDDLQueuePoller(
    bool use_local_tserver, rocksdb::RateLimiter* rate_limiter, const NamespaceName& namespace_name,
    ConnectToPostgresFunc connect_to_pg_func) {
  Init(use_local_tserver, rate_limiter);

  ddl_queue_handler_ = std::make_shared<XClusterDDLQueueHandler>(
      &local_client_, namespace_name, consumer_namespace_id_, std::move(connect_to_pg_func));
}

void XClusterPoller::StartShutdown() {
  // The poller is shutdown in two cases:
  // 1. The regular case where the poller is deleted via XClusterConsumer's
  // TriggerDeletionOfOldPollers.
  //    This happens when the stream is deleted or the consumer tablet leader changes.
  // 2. During XClusterConsumer::Shutdown(). Note that in this scenario, we may still be processing
  //    a GetChanges request / handle callback, so we shutdown what we can here (note that
  //    thread_pool_ is shutdown before we shutdown the pollers, so that will force most
  //    codepaths to exit early). Callbacks use weak pointer so they are safe to run even after we
  //    get destroyed.
  VLOG_WITH_PREFIX(2) << "StartShutdown";

  DCHECK(!shutdown_);
  shutdown_ = true;
  YB_PROFILE(shutdown_cv_.notify_all());

  if (output_client_) {
    output_client_->StartShutdown();
  }
  XClusterAsyncExecutor::StartShutdown();
}

void XClusterPoller::CompleteShutdown() {
  DCHECK(shutdown_);
  VLOG_WITH_PREFIX_AND_FUNC(2) << "Begin";

  // Wait for tasks that started before shutdown to complete. We release mutex as new tasks acquire
  // it before checking for shutdown.
  { std::lock_guard l(data_mutex_); }

  if (output_client_) {
    output_client_->CompleteShutdown();
  }

  XClusterAsyncExecutor::CompleteShutdown();

  VLOG_WITH_PREFIX_AND_FUNC(2) << "End";
}

std::string XClusterPoller::LogPrefix() const {
  return Format(
      "P [$0:$1] C [$2:$3]: ",
      producer_tablet_info_.stream_id,
      producer_tablet_info_.tablet_id,
      consumer_tablet_info_.table_id,
      consumer_tablet_info_.tablet_id);
}

bool XClusterPoller::IsOffline() { return shutdown_ || is_failed_; }

void XClusterPoller::UpdateSchemaVersions(const cdc::XClusterSchemaVersionMap& schema_versions) {
  RETURN_WHEN_OFFLINE;
  {
    std::lock_guard l(schema_version_lock_);
    schema_version_map_ = schema_versions;
  }
  for (const auto& [producer_schema_version, consumer_schema_version] : schema_versions) {
    LOG_WITH_PREFIX(INFO) << Format(
        "Producer Schema Version:$0, Consumer Schema Version:$1", producer_schema_version,
        consumer_schema_version);
  }
}

void XClusterPoller::UpdateColocatedSchemaVersionMap(
    const cdc::ColocatedSchemaVersionMap& input_colocated_schema_version_map) {
  RETURN_WHEN_OFFLINE;
  {
    std::lock_guard l(schema_version_lock_);
    colocated_schema_version_map_ = input_colocated_schema_version_map;
  }
  for (const auto& [colocation_id, schema_versions] : input_colocated_schema_version_map) {
    for (const auto& [producer_schema_version, consumer_schema_version] : schema_versions) {
      LOG_WITH_PREFIX(INFO) << Format(
          "ColocationId:$0 Producer Schema Version:$1, Consumer Schema Version:$2", colocation_id,
          producer_schema_version, consumer_schema_version);
    }
  }
}

void XClusterPoller::ScheduleSetSchemaVersionIfNeeded(
    SchemaVersion cur_version, SchemaVersion last_compatible_consumer_schema_version) {
  RETURN_WHEN_OFFLINE;

  if (last_compatible_consumer_schema_version_ < last_compatible_consumer_schema_version ||
      validated_schema_version_ < cur_version) {
    ScheduleFunc(BIND_FUNCTION_AND_ARGS(
        XClusterPoller::DoSetSchemaVersion, cur_version, last_compatible_consumer_schema_version));
  }
}

void XClusterPoller::DoSetSchemaVersion(
    SchemaVersion cur_version, SchemaVersion current_consumer_schema_version) {
  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  if (last_compatible_consumer_schema_version_ < current_consumer_schema_version) {
    last_compatible_consumer_schema_version_ = current_consumer_schema_version;
  }

  if (validated_schema_version_ < cur_version) {
    validated_schema_version_ = cur_version;
    // Re-enable polling. last_task_schedule_time_ is already current as it was set by the caller
    // function ScheduleSetSchemaVersionIfNeeded.
    if (!is_polling_.exchange(true)) {
      LOG(INFO) << "Restarting polling on " << producer_tablet_info_.tablet_id
                << " Producer schema version : " << validated_schema_version_
                << " Consumer schema version : " << last_compatible_consumer_schema_version_;
      ScheduleFunc(BIND_FUNCTION_AND_ARGS(XClusterPoller::DoPoll));
    }
  }
}

HybridTime XClusterPoller::GetSafeTime() const {
  SharedLock lock(safe_time_lock_);
  return producer_safe_time_;
}

void XClusterPoller::UpdateSafeTime(int64 new_time) {
  HybridTime new_hybrid_time(new_time);
  if (new_hybrid_time.is_special()) {
    LOG(WARNING) << "Received invalid xCluster safe time: " << new_hybrid_time;
    return;
  }

  std::lock_guard l(safe_time_lock_);
  if (producer_safe_time_.is_special() || new_hybrid_time > producer_safe_time_) {
    producer_safe_time_ = new_hybrid_time;
  }
}

void XClusterPoller::SchedulePoll() {
  // determine if we should delay our upcoming poll
  int64_t delay_ms =
      GetAtomicFlag(&FLAGS_async_replication_polling_delay_ms);  // normal throttling.
  if (idle_polls_ >= GetAtomicFlag(&FLAGS_async_replication_max_idle_wait)) {
    delay_ms = std::max(
        delay_ms, (int64_t)GetAtomicFlag(&FLAGS_async_replication_idle_delay_ms));  // idle backoff.
  }
  if (poll_failures_ > 0) {
    delay_ms =
        std::max(delay_ms, (int64_t)1 << poll_failures_);  // exponential backoff for failures.
  }

  ScheduleFuncWithDelay(delay_ms, BIND_FUNCTION_AND_ARGS(XClusterPoller::DoPoll));
}

void XClusterPoller::DoPoll() {
  if (FLAGS_enable_xcluster_stat_collection) {
    poll_stats_history_.RecordBeginPoll();
  }

  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  if (PREDICT_FALSE(FLAGS_TEST_cdc_skip_replication_poll)) {
    idle_polls_ = GetAtomicFlag(&FLAGS_async_replication_max_idle_wait);
    SchedulePoll();
    return;
  }

  const auto xcluster_simulated_lag_ms = GetAtomicFlag(&FLAGS_TEST_xcluster_simulated_lag_ms);
  if (PREDICT_FALSE(xcluster_simulated_lag_ms != 0)) {
    auto flag_info =
        gflags::GetCommandLineFlagInfoOrDie("TEST_xcluster_simulated_lag_tablet_filter");
    const auto& tablet_filter = flag_info.current_value;
    const auto tablet_filter_list = strings::Split(tablet_filter, ",");
    if (tablet_filter.empty() || std::find(
                                     tablet_filter_list.begin(), tablet_filter_list.end(),
                                     producer_tablet_info_.tablet_id) != tablet_filter_list.end()) {
      auto delay = GetAtomicFlag(&FLAGS_async_replication_idle_delay_ms);
      if (xcluster_simulated_lag_ms > 0) {
        delay = xcluster_simulated_lag_ms;
      }

      ANNOTATE_UNPROTECTED_WRITE(TEST_is_sleeping_) = true;
      auto se = ScopeExit([this]() { ANNOTATE_UNPROTECTED_WRITE(TEST_is_sleeping_) = false; });
      UniqueLock shutdown_l(shutdown_mutex_);
      if (shutdown_cv_.wait_for(
              GetLockForCondition(&shutdown_l), delay * 1ms, [this] { return IsOffline(); })) {
        return;
      }

      // If replication is paused skip the GetChanges call
      if (xcluster_simulated_lag_ms < 0) {
        return SchedulePoll();
      }
    }
  }

  cdc::GetChangesRequestPB req;
  req.set_stream_id(producer_tablet_info_.stream_id.ToString());
  req.set_tablet_id(producer_tablet_info_.tablet_id);
  req.set_serve_as_proxy(GetAtomicFlag(&FLAGS_cdc_consumer_use_proxy_forwarding));

  if (FLAGS_enable_xcluster_auto_flag_validation && auto_flags_version_) {
    req.set_auto_flags_config_version(auto_flags_version_->GetCompatibleVersion());
  }

  cdc::CDCCheckpointPB checkpoint;
  *checkpoint.mutable_op_id() = op_id_;
  if (checkpoint.op_id().index() > 0 || checkpoint.op_id().term() > 0) {
    // Only send non-zero checkpoints in request.
    // If we don't know the latest checkpoint, then XClusterProducer can use the checkpoint from
    // cdc_state table.
    // This is useful in scenarios where a new tablet peer becomes replication leader for a
    // producer tablet and is not aware of the last checkpoint.
    *req.mutable_from_checkpoint() = checkpoint;
  }

  auto handle = rpcs_->Prepare();
  if (handle == rpcs_->InvalidHandle()) {
    DCHECK(IsOffline());
    MarkFailed("we could not prepare rpc as it is shutting down");
    return;
  }

  VLOG_WITH_PREFIX(5) << "Sending GetChangesRequest: " << req.ShortDebugString();

  // It is safe to pass rpcs_ raw pointer as the call is guaranteed to complete before Rpcs
  // shutdown.
  *handle = rpc::xcluster::CreateGetChangesRpc(
      CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms),
      nullptr, /* RemoteTablet: will get this from 'req' */
      producer_client_->client.get(), &req,
      [weak_ptr = weak_from_this(), this, handle, rpcs = rpcs_](
          const Status& status, cdc::GetChangesResponsePB&& resp) {
        RpcCallback(
            weak_ptr, handle, rpcs,
            BIND_FUNCTION_AND_ARGS(
                XClusterPoller::HandleGetChangesResponse, status,
                std::make_shared<cdc::GetChangesResponsePB>(std::move(resp))));
      });

  SetHandleAndSendRpc(handle);
}

void XClusterPoller::UpdateSchemaVersionsForApply() {
  SharedLock lock(schema_version_lock_);
  output_client_->SetLastCompatibleConsumerSchemaVersion(last_compatible_consumer_schema_version_);
  output_client_->UpdateSchemaVersionMappings(schema_version_map_, colocated_schema_version_map_);
}

void XClusterPoller::HandleGetChangesResponse(
    Status status, std::shared_ptr<cdc::GetChangesResponsePB> resp) {
  if (FLAGS_enable_xcluster_stat_collection) {
    poll_stats_history_.RecordEndGetChanges();
  }

  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

    if (status.ok()) {
      status = ProcessGetChangesResponseError(*resp);
    }

    if (!status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "XClusterPoller GetChanges failure: " << status.ToString();

      if (FLAGS_enable_xcluster_stat_collection) {
        poll_stats_history_.SetError(std::move(status));
      }

      // In case of errors, try polling again with backoff
      poll_failures_ =
          std::min(poll_failures_ + 1, GetAtomicFlag(&FLAGS_replication_failure_delay_exponent));
      xcluster_consumer_->IncrementPollFailureCount();
      return SchedulePoll();
    }
    // Recover slowly if we're congested.
    poll_failures_ = static_cast<uint32>(
        std::max(static_cast<int64>(poll_failures_) - 2, static_cast<int64>(0)));
  }

  // Success Case.
  ScheduleApplyChanges(std::move(resp));
}

Status XClusterPoller::ProcessGetChangesResponseError(const cdc::GetChangesResponsePB& resp) {
  if (PREDICT_FALSE(FLAGS_TEST_xcluster_simulate_get_changes_response_error)) {
    return STATUS(IllegalState, "Simulate get change failure for testing");
  }

  if (!resp.has_error()) {
    SCHECK(
        resp.has_checkpoint(), NotFound, "XClusterPoller GetChanges failure: No checkpoint found");
    return Status::OK();
  }

  const auto& error_code = resp.error().code();

  LOG_WITH_PREFIX(WARNING) << "XClusterPoller GetChanges failure response: code=" << error_code
                           << ", status=" << resp.error().status().DebugString();

  if (resp.error().code() == cdc::CDCErrorPB::AUTO_FLAGS_CONFIG_VERSION_MISMATCH) {
    StoreReplicationError(ReplicationErrorPb::REPLICATION_AUTO_FLAG_CONFIG_VERSION_MISMATCH);

    SCHECK(
        resp.has_auto_flags_config_version(), NotFound,
        "New AutoFlags config version not found in response when error code is "
        "AUTO_FLAGS_CONFIG_VERSION_MISMATCH");

    auto new_version = resp.auto_flags_config_version();
    RETURN_NOT_OK_PREPEND(
        xcluster_consumer_->ReportNewAutoFlagConfigVersion(GetReplicationGroupId(), new_version),
        Format("Reporting new AutoFlags config version $0", new_version));

    return STATUS_FORMAT(
        IllegalState, "AutoFlags config version mismatch. New version: $0", new_version);
  }

  if (resp.error().code() == cdc::CDCErrorPB::CHECKPOINT_TOO_OLD) {
    StoreReplicationError(ReplicationErrorPb::REPLICATION_MISSING_OP_ID);
  }

  return StatusFromPB(resp.error().status())
      .CloneAndPrepend("Unable to find expected op id on the producer");
}

void XClusterPoller::ApplyChangesCallback(XClusterOutputClientResponse&& response) {
  if (FLAGS_enable_xcluster_stat_collection) {
    poll_stats_history_.RecordEndApplyChanges();
  }

  ScheduleFunc(
      BIND_FUNCTION_AND_ARGS(XClusterPoller::VerifyApplyChangesResponse, std::move(response)));
}

void XClusterPoller::VerifyApplyChangesResponse(XClusterOutputClientResponse response) {
  // Verify if the ApplyChanges failed, in which case we need to reschedule it.
  if (!response.status.ok() ||
      RandomActWithProbability(FLAGS_TEST_xcluster_simulate_random_failure_after_apply)) {
    LOG_WITH_PREFIX(WARNING) << "ApplyChanges failure: " << response.status;

    if (FLAGS_enable_xcluster_stat_collection) {
      poll_stats_history_.SetError(std::move(response.status));
    }

    // Repeat the ApplyChanges step, with exponential backoff.
    apply_failures_ =
        std::min(apply_failures_ + 1, GetAtomicFlag(&FLAGS_replication_failure_delay_exponent));
    xcluster_consumer_->IncrementApplyFailureCount();
    ScheduleApplyChanges(std::move(response.get_changes_response));
    return;
  }

  HandleApplyChangesResponse(std::move(response));
}

void XClusterPoller::HandleApplyChangesResponse(XClusterOutputClientResponse response) {
  DCHECK(response.get_changes_response);

  if (ddl_queue_handler_) {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;
    auto s = ddl_queue_handler_->ProcessDDLQueueTable(response);
    if (!s.ok()) {
      // If processing ddl_queue table fails, then retry just this part (don't repeat ApplyChanges).
      YB_LOG_EVERY_N(WARNING, 30) << "ProcessDDLQueueTable Error: " << s << " " << THROTTLE_MSG;
      if (FLAGS_enable_xcluster_stat_collection) {
        poll_stats_history_.SetError(std::move(s));
      }
      ScheduleFuncWithDelay(
          GetAtomicFlag(&FLAGS_xcluster_safe_time_update_interval_secs),
          BIND_FUNCTION_AND_ARGS(XClusterPoller::HandleApplyChangesResponse, std::move(response)));
      return;
    }
  }

  if (FLAGS_enable_xcluster_stat_collection) {
    size_t resp_size = 0;
    auto received_index = response.last_applied_op_id.index();
    const auto& num_records = response.get_changes_response->records_size();
    if (num_records > 0) {
      resp_size = response.get_changes_response->ByteSizeLong();
    }
    poll_stats_history_.RecordEndPoll(num_records, received_index, resp_size);
  }

  ClearReplicationError();

  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

    // Recover slowly if we've gotten congested.
    apply_failures_ = static_cast<uint32>(
        std::max(static_cast<int64>(apply_failures_) - 2, static_cast<int64>(0)));

    op_id_ = response.last_applied_op_id;

    idle_polls_ = (response.processed_record_count == 0) ? idle_polls_ + 1 : 0;

    if (validated_schema_version_ < response.wait_for_version) {
      LOG(WARNING) << "Pausing Poller since producer schema version " << response.wait_for_version
                   << " is higher than consumer schema version " << validated_schema_version_;
      is_polling_ = false;
      validated_schema_version_ = response.wait_for_version - 1;
      return;
    }

    if (response.get_changes_response->has_safe_hybrid_time()) {
      // Once all changes have been successfully applied we can update the safe time.
      UpdateSafeTime(response.get_changes_response->safe_hybrid_time());
    }
  }

  SchedulePoll();
}

void XClusterPoller::ScheduleApplyChanges(
    std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response) {
  DCHECK(get_changes_response);

  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  int64_t delay_ms = 0;
  if (apply_failures_ > 0) {
    delay_ms = (1 << apply_failures_) - 1;
  }

  ScheduleFuncWithDelay(
      delay_ms,
      BIND_FUNCTION_AND_ARGS(XClusterPoller::ApplyChanges, std::move(get_changes_response)));
}

void XClusterPoller::ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response) {
  DCHECK(get_changes_response);

  if (FLAGS_enable_xcluster_stat_collection) {
    poll_stats_history_.RecordBeginApplyChanges();
  }

  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  UpdateSchemaVersionsForApply();
  output_client_->ApplyChanges(get_changes_response);
}

bool XClusterPoller::IsLeaderTermValid() {
  if (is_failed_) {
    return false;
  }

  if (FLAGS_TEST_xcluster_disable_poller_term_check) {
    return true;
  }

  auto current_term = get_leader_term_(consumer_tablet_info_.tablet_id);
  if (current_term != GetLeaderTerm()) {
    MarkFailed(Format("leader term changed. Old: $0, New: $1", GetLeaderTerm(), current_term));
    return false;
  }

  return true;
}

bool XClusterPoller::IsStuck() const {
  if (is_polling_) {
    const auto lag = MonoTime::Now() - last_task_schedule_time_;
    if (lag > 1s * GetAtomicFlag(&FLAGS_xcluster_poller_task_delay_considered_stuck_secs)) {
      LOG_WITH_PREFIX(ERROR) << "XCluster Poller has not executed any tasks for " << lag.ToString();
      return true;
    }
  }

  return false;
}

std::string XClusterPoller::State() const {
  if (is_failed_) {
    return "Failed";
  }
  if (!is_polling_) {
    return "Paused";
  }

  return "Running";
}

bool XClusterPoller::ShouldContinuePolling() const {
  if (is_failed_ || IsStuck()) {
    // All failed and stuck pollers need to be deleted. If the tablet leader is still on this node
    // they will be recreated.
    return false;
  }

  // If we are not the leader, we should not poll.
  return GetLeaderTerm() == get_leader_term_(consumer_tablet_info_.tablet_id);
}

void XClusterPoller::MarkFailed(const std::string& reason, const Status& status) {
  LOG_WITH_PREFIX(WARNING) << "Stopping xCluster Poller as " << reason
                           << (status.ok() ? "" : Format(": $0", status));
  is_failed_ = true;
}

XClusterPollerStats XClusterPoller::GetStats() const {
  XClusterPollerStats stats(producer_tablet_info_, consumer_tablet_info_);
  stats.state = State();
  poll_stats_history_.PopulateStats(&stats);

  return stats;
}

void XClusterPoller::StoreReplicationError(ReplicationErrorPb error) {
  DCHECK_NE(error, ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED);

  std::lock_guard l(replication_error_mutex_);
  if (previous_replication_error_ != error) {
    // Avoid unnecessarily storing same errors since this is used in perf critical master heartbeat
    // path.
    xcluster_consumer_->StoreReplicationError(GetPollerId(), error);
    previous_replication_error_ = error;
  }
}

void XClusterPoller::ClearReplicationError() {
  StoreReplicationError(ReplicationErrorPb::REPLICATION_OK);
}

void XClusterPoller::TEST_IncrementNumSuccessfulWriteRpcs() {
  xcluster_consumer_->TEST_IncrementNumSuccessfulWriteRpcs();
}

}  // namespace tserver
}  // namespace yb
