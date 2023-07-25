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
#include "yb/gutil/strings/split.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/xcluster_output_client.h"

#include "yb/cdc/cdc_rpc.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"

#include "yb/consensus/opid_util.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/threadpool.h"
#include "yb/util/unique_lock.h"

// Similar heuristic to heartbeat_interval in heartbeater.cc.
DEFINE_RUNTIME_int32(async_replication_polling_delay_ms, 0,
    "How long to delay in ms between applying and polling.");

DEFINE_RUNTIME_int32(async_replication_idle_delay_ms, 100,
    "How long to delay between polling when we expect no data at the destination.");

DEFINE_RUNTIME_int32(async_replication_max_idle_wait, 3,
    "Maximum number of consecutive empty GetChanges until the poller "
    "backs off to the idle interval, rather than immediately retrying.");

DEFINE_RUNTIME_int32(replication_failure_delay_exponent, 16 /* ~ 2^16/1000 ~= 65 sec */,
    "Max number of failures (N) to use when calculating exponential backoff (2^N-1).");

DEFINE_RUNTIME_bool(cdc_consumer_use_proxy_forwarding, false,
    "When enabled, read requests from the XClusterConsumer that go to the wrong node are "
    "forwarded to the correct node by the Producer.");

DEFINE_test_flag(int32, xcluster_simulated_lag_ms, 0,
    "Simulate lag in xcluster replication. Replication is paused if set to -1.");
DEFINE_test_flag(string, xcluster_simulated_lag_tablet_filter, "",
    "Comma separated list of producer tablet ids. If non empty, simulate lag in only applied to "
    "this list of tablets.");

DEFINE_test_flag(bool, cdc_skip_replication_poll, false,
                 "If true, polling will be skipped.");

DEFINE_test_flag(bool, xcluster_disable_poller_term_check, false,
    "If true, the poller will not check the leader term.");

DECLARE_int32(cdc_read_rpc_timeout_ms);

using namespace std::placeholders;

#define RETURN_WHEN_OFFLINE \
  do { \
    if (CheckOffline()) { \
      LOG_WITH_PREFIX(WARNING) << "XCluster Poller went offline"; \
      return; \
    } \
  } while (false)

#define RETURN_WHEN_TERM_INVALID \
  do { \
    if (!IsLeaderTermValid()) { \
      return; \
    } \
  } while (false)

#define ACQUIRE_MUTEX_IF_ONLINE \
  RETURN_WHEN_OFFLINE; \
  std::lock_guard l(data_mutex_); \
  RETURN_WHEN_TERM_INVALID

#define ACQUIRE_UNIQUE_LOCK_IF_ONLINE \
  RETURN_WHEN_OFFLINE; \
  UniqueLock l(data_mutex_); \
  RETURN_WHEN_TERM_INVALID

// Helper macro to sleep without holding the lock. If the poller goes offline we will return
// immediately. Also retuns if the leader term changes at the end of the sleep.
#define SLEEP_FOR(delay_ms) \
  { \
    TEST_is_sleeping_ = true; \
    auto se = ScopeExit([this]() { TEST_is_sleeping_ = false; }); \
    if (shutdown_cv_.wait_for( \
            GetLockForCondition(&l), delay_ms * 1ms, [this] { return CheckOffline(); })) { \
      return; \
    } \
  } \
  RETURN_WHEN_TERM_INVALID

using namespace std::chrono_literals;

namespace yb {
namespace tserver {

XClusterPoller::XClusterPoller(
    const cdc::ProducerTabletInfo& producer_tablet_info,
    const cdc::ConsumerTabletInfo& consumer_tablet_info, ThreadPool* thread_pool, rpc::Rpcs* rpcs,
    const std::shared_ptr<XClusterClient>& local_client,
    const std::shared_ptr<XClusterClient>& producer_client, XClusterConsumer* xcluster_consumer,
    bool use_local_tserver, SchemaVersion last_compatible_consumer_schema_version,
    rocksdb::RateLimiter* rate_limiter, std::function<int64_t(const TabletId&)> get_leader_term)
    : producer_tablet_info_(producer_tablet_info),
      consumer_tablet_info_(consumer_tablet_info),
      op_id_(consensus::MinimumOpId()),
      validated_schema_version_(0),
      last_compatible_consumer_schema_version_(last_compatible_consumer_schema_version),
      get_leader_term_(std::move(get_leader_term)),
      output_client_(CreateXClusterOutputClient(
          xcluster_consumer, consumer_tablet_info, producer_tablet_info, local_client, thread_pool,
          rpcs, std::bind(&XClusterPoller::ApplyChangesCallback, this, _1), use_local_tserver,
          rate_limiter)),
      producer_client_(producer_client),
      thread_pool_(thread_pool),
      rpcs_(rpcs),
      poll_handle_(rpcs_->InvalidHandle()),
      xcluster_consumer_(xcluster_consumer),
      producer_safe_time_(HybridTime::kInvalid) {}

XClusterPoller::~XClusterPoller() {
  VLOG(1) << "Destroying XClusterPoller";
  DCHECK(shutdown_);
}

void XClusterPoller::StartShutdown() {
  // The poller is shutdown in two cases:
  // 1. The regular case where the poller is deleted via XClusterConsumer's
  // TriggerDeletionOfOldPollers.
  //    This happens when the stream is deleted or the consumer tablet leader changes.
  // 2. During XClusterConsumer::Shutdown(). Note that in this scenario, we may still be processing
  //    a GetChanges request / handle callback, so we shutdown what we can here (note that
  //    thread_pool_ is shutdown before we shutdown the pollers, so that will force most
  //    codepaths to exit early), and then using shared_from_this, destroy the object once all
  //    callbacks are complete.
  VLOG_WITH_PREFIX_AND_FUNC(2);

  DCHECK(!shutdown_);
  shutdown_ = true;
  shutdown_cv_.notify_all();
}

void XClusterPoller::CompleteShutdown() {
  DCHECK(shutdown_);
  VLOG_WITH_PREFIX_AND_FUNC(2) << "Start";
  rpc::RpcCommandPtr rpc_to_abort;
  {
    std::lock_guard l(data_mutex_);
    output_client_->Shutdown();
  }

  {
    std::lock_guard l(poll_handle_mutex_);
    if (poll_handle_ != rpcs_->InvalidHandle()) {
      rpc_to_abort = *poll_handle_;
    }
  }

  if (rpc_to_abort) {
    rpc_to_abort->Abort();
  }

  VLOG_WITH_PREFIX_AND_FUNC(2) << "Complete";
}

std::string XClusterPoller::LogPrefix() const {
  return Format(
      "P [$0:$1] C [$2:$3]: ",
      producer_tablet_info_.stream_id,
      producer_tablet_info_.tablet_id,
      consumer_tablet_info_.table_id,
      consumer_tablet_info_.tablet_id);
}

template <class F>
void XClusterPoller::SubmitFunc(F&& f) {
  RETURN_WHEN_OFFLINE;

  auto s = thread_pool_->SubmitFunc(std::move(f));
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING)
        << "Stopping xCluster Poller as we could not submit function to thread pool: " << s;
    is_failed_ = true;
  }
}

bool XClusterPoller::CheckOffline() { return shutdown_ || is_failed_; }

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
    SubmitFunc(std::bind(
        &XClusterPoller::DoSetSchemaVersion, shared_from_this(), cur_version,
        last_compatible_consumer_schema_version));
  }
}

void XClusterPoller::DoSetSchemaVersion(
    SchemaVersion cur_version, SchemaVersion current_consumer_schema_version) {
  ACQUIRE_MUTEX_IF_ONLINE;

  if (last_compatible_consumer_schema_version_ < current_consumer_schema_version) {
    last_compatible_consumer_schema_version_ = current_consumer_schema_version;
  }

  if (validated_schema_version_ < cur_version) {
    validated_schema_version_ = cur_version;
    // re-enable polling.
    if (!is_polling_) {
      is_polling_ = true;
      LOG(INFO) << "Restarting polling on " << producer_tablet_info_.tablet_id
                << " Producer schema version : " << validated_schema_version_
                << " Consumer schema version : " << last_compatible_consumer_schema_version_;
      SubmitFunc(std::bind(&XClusterPoller::DoPoll, shared_from_this()));
    }
  }
}

HybridTime XClusterPoller::GetSafeTime() const {
  SharedLock lock(safe_time_lock_);
  return producer_safe_time_;
}

cdc::ConsumerTabletInfo XClusterPoller::GetConsumerTabletInfo() const {
  return consumer_tablet_info_;
}

void XClusterPoller::UpdateSafeTime(int64 new_time) {
  HybridTime new_hybrid_time(new_time);
  if (!new_hybrid_time.is_special()) {
    std::lock_guard l(safe_time_lock_);
    if (producer_safe_time_.is_special() || new_hybrid_time > producer_safe_time_) {
      producer_safe_time_ = new_hybrid_time;
    }
  }
}

void XClusterPoller::SchedulePoll() {
  SubmitFunc(std::bind(&XClusterPoller::DoPoll, shared_from_this()));
}

void XClusterPoller::DoPoll() {
  ACQUIRE_UNIQUE_LOCK_IF_ONLINE;

  if (PREDICT_FALSE(FLAGS_TEST_cdc_skip_replication_poll)) {
    SLEEP_FOR(FLAGS_async_replication_idle_delay_ms);
    SchedulePoll();
    return;
  }

  // determine if we should delay our upcoming poll
  int64_t delay = GetAtomicFlag(&FLAGS_async_replication_polling_delay_ms);  // normal throttling.
  if (idle_polls_ >= GetAtomicFlag(&FLAGS_async_replication_max_idle_wait)) {
    delay = std::max(
        delay, (int64_t)GetAtomicFlag(&FLAGS_async_replication_idle_delay_ms));  // idle backoff.
  }
  if (poll_failures_ > 0) {
    delay = std::max(delay, (int64_t)1 << poll_failures_); // exponential backoff for failures.
  }
  SLEEP_FOR(delay);

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

      SLEEP_FOR(delay);

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

  auto poll_handle = rpcs_->Prepare();
  if (poll_handle == rpcs_->InvalidHandle()) {
    DCHECK(CheckOffline());
    LOG_WITH_PREFIX(WARNING) << "Unable to perform poll, rpcs_ is shutdown";
    return;
  }

  VLOG_WITH_PREFIX(5) << "Sending GetChangesRequest: " << req.ShortDebugString();

  *poll_handle = CreateGetChangesCDCRpc(
      CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms),
      nullptr, /* RemoteTablet: will get this from 'req' */
      producer_client_->client.get(),
      &req,
      std::bind(&XClusterPoller::GetChangesCallback, shared_from_this(), _1, _2));

  {
    std::lock_guard l(poll_handle_mutex_);
    poll_handle_ = std::move(poll_handle);
    (**poll_handle_).SendRpc();
  }
}

void XClusterPoller::UpdateSchemaVersionsForApply() {
  SharedLock lock(schema_version_lock_);
  output_client_->SetLastCompatibleConsumerSchemaVersion(last_compatible_consumer_schema_version_);
  output_client_->UpdateSchemaVersionMappings(schema_version_map_, colocated_schema_version_map_);
}

void XClusterPoller::GetChangesCallback(const Status& status, cdc::GetChangesResponsePB&& resp) {
  auto new_resp = std::make_shared<cdc::GetChangesResponsePB>(std::move(resp));

  {
    std::lock_guard l(poll_handle_mutex_);
    rpcs_->Unregister(&poll_handle_);
  }

  SubmitFunc(std::bind(
      &XClusterPoller::HandleGetChangesResponse, shared_from_this(), status, std::move(new_resp)));
}

void XClusterPoller::HandleGetChangesResponse(
    Status status, std::shared_ptr<cdc::GetChangesResponsePB> resp) {
  {
    ACQUIRE_MUTEX_IF_ONLINE;
    status_ = std::move(status);

    bool failed = false;
    if (!status_.ok()) {
      LOG_WITH_PREFIX(INFO) << "XClusterPoller failure: " << status_.ToString();
      failed = true;
    } else if (resp->has_error()) {
      LOG_WITH_PREFIX(WARNING) << "XClusterPoller failure response: code=" << resp->error().code()
                               << ", status=" << resp->error().status().DebugString();
      failed = true;

      if (resp->error().code() == cdc::CDCErrorPB::CHECKPOINT_TOO_OLD) {
        xcluster_consumer_->StoreReplicationError(
            consumer_tablet_info_.tablet_id,
            producer_tablet_info_.stream_id,
            ReplicationErrorPb::REPLICATION_MISSING_OP_ID,
            "Unable to find expected op id on the producer");
      }
    } else if (!resp->has_checkpoint()) {
      LOG_WITH_PREFIX(ERROR) << "XClusterPoller failure: no checkpoint";
      failed = true;
    }
    if (failed) {
      // In case of errors, try polling again with backoff
      poll_failures_ =
          std::min(poll_failures_ + 1, GetAtomicFlag(&FLAGS_replication_failure_delay_exponent));
      return SchedulePoll();
    }
    // Recover slowly if we're congested.
    poll_failures_ = std::max(poll_failures_ - 2, 0);
  }

  // Success Case.
  ApplyChanges(std::move(resp));
}

void XClusterPoller::ApplyChangesCallback(XClusterOutputClientResponse response) {
  SubmitFunc(std::bind(
      &XClusterPoller::HandleApplyChangesResponse, shared_from_this(), std::move(response)));
}

void XClusterPoller::HandleApplyChangesResponse(XClusterOutputClientResponse response) {
  if (!response.status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "ApplyChanges failure: " << response.status;
    // Repeat the ApplyChanges step, with exponential backoff.
    {
      ACQUIRE_MUTEX_IF_ONLINE;
      apply_failures_ =
          std::min(apply_failures_ + 1, GetAtomicFlag(&FLAGS_replication_failure_delay_exponent));
    }
    ApplyChanges(std::move(response.get_changes_response));
    return;
  }

  DCHECK(response.get_changes_response);

  {
    ACQUIRE_MUTEX_IF_ONLINE;

    // Recover slowly if we've gotten congested.
    apply_failures_ = std::max(apply_failures_ - 2, 0);

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

  // We can poll again from the same thread.
  DoPoll();
}

void XClusterPoller::ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response) {
  DCHECK(get_changes_response);

  ACQUIRE_UNIQUE_LOCK_IF_ONLINE;

  if (apply_failures_ > 0) {
    int64_t delay = (1 << apply_failures_) - 1;
    VLOG_WITH_PREFIX(1) << "Retrying ApplyChanges after sleeping for  " << delay;

    SLEEP_FOR(delay);
  }

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
  if (leader_term_ == OpId::kUnknownTerm) {
    leader_term_ = current_term;
  }

  if (current_term == OpId::kUnknownTerm || current_term != leader_term_) {
    LOG_WITH_PREFIX(WARNING) << "Stopping Poller as leader term "
                             << (current_term == OpId::kUnknownTerm
                                     ? "is unknown"
                                     : "changed. Expected: " + std::to_string(leader_term_) +
                                           ", Current: " + std::to_string(current_term));
    is_failed_ = true;
    return false;
  }

  return true;
}

}  // namespace tserver
} // namespace yb
