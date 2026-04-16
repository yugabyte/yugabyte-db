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

#include "yb/tserver/xcluster_consumer.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/xcluster_types.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/pg_types.h"
#include "yb/common/ql_protocol.messages.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/xcluster_util.h"
#include "yb/common/ysql_utils.h"

#include "yb/gutil/map-util.h"

#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"

#include "yb/rocksdb/rate_limiter.h"
#include "yb/rpc/rpc.h"

#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/xcluster_consumer_auto_flags_info.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/tserver/xcluster_poller.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/unique_lock.h"

using std::string;

METRIC_DEFINE_counter(server, xcluster_consumer_replication_error_count,
    "XCluster consumer replication error count", yb::MetricUnit::kUnits,
    "Number of replication errors encountered by XClusterConsumer. Replication errors are "
    "errors that require user intervention to fix, such as mismatched schemas or missing op ids.");

METRIC_DEFINE_counter(server, xcluster_consumer_apply_failure_count,
    "XCluster consumer apply failure count", yb::MetricUnit::kUnits,
    "Number of apply failures encountered by XClusterConsumer. Apply failures are "
    "errors with calling GetChanges on the source cluster.");

METRIC_DEFINE_counter(server, xcluster_consumer_poll_failure_count,
    "XCluster consumer poll failure count", yb::MetricUnit::kUnits,
    "Number of poll failures encountered by XClusterConsumer. Poll failures are "
    "errors applying changes to the target cluster.");

DEPRECATE_FLAG(int32, cdc_consumer_handler_thread_pool_size, "05_2024");

DEFINE_NON_RUNTIME_int32(xcluster_consumer_thread_pool_size, 0,
    "Override the max thread pool size for XClusterConsumerHandler, which is used by "
    "XClusterPoller. If set to 0, then the thread pool will use the default size (number of "
    "cpus on the system).");
TAG_FLAG(xcluster_consumer_thread_pool_size, advanced);

DEFINE_RUNTIME_int32(xcluster_safe_time_update_interval_secs, 1,
    "The interval at which xcluster safe time is computed. This controls the staleness of the data "
    "seen when performing database level xcluster consistent reads. If there is any additional lag "
    "in the replication, then it will add to the overall staleness of the data.");

DEFINE_validator(xcluster_safe_time_update_interval_secs, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_RUNTIME_int32(apply_changes_max_send_rate_mbps, 100,
    "Server-wide max apply rate for xcluster traffic.");

DEFINE_test_flag(bool, xcluster_disable_delete_old_pollers, false,
    "Disables the deleting of old xcluster pollers that are no longer needed.");

using namespace std::chrono_literals;

#define ACQUIRE_SHARED_LOCK_IF_ONLINE \
  SharedLock l(master_data_mutex_); \
  do { \
    if (is_shutdown_) { \
      return; \
    } \
  } while (false)

namespace yb {

namespace tserver {

namespace {
Result<SchemaVersion> ProcessSchemaVersionsAndGetMinSchemaVersion(
    const cdc::SchemaVersionsPB& versions,
    std::unordered_map<SchemaVersion, SchemaVersion>& schema_version_map) {
  schema_version_map[versions.current_producer_schema_version()] =
      versions.current_consumer_schema_version();

  auto min_schema_version = versions.current_consumer_schema_version();
  RSTATUS_DCHECK(
      versions.old_producer_schema_versions_size() == versions.old_consumer_schema_versions_size(),
      IllegalState, "Mismatch in old producer and consumer schema versions sizes");

  for (int i = 0; i < versions.old_producer_schema_versions_size(); ++i) {
    RSTATUS_DCHECK(
        versions.old_producer_schema_versions(i) < versions.current_producer_schema_version(),
        IllegalState,
        "Old producer schema version should be less than current producer schema version");
    schema_version_map[versions.old_producer_schema_versions(i)] =
        versions.old_consumer_schema_versions(i);
    min_schema_version = std::min(min_schema_version, versions.old_consumer_schema_versions(i));
  }
  return min_schema_version;
}
}  // namespace

Result<std::unique_ptr<XClusterConsumerIf>> CreateXClusterConsumer(
    std::function<int64_t(const TabletId&)> get_leader_term, const std::string& ts_uuid,
    client::YBClient& local_client, ConnectToPostgresFunc connect_to_pg_func,
    GetNamespaceInfoFunc get_namespace_info_func, TserverXClusterContextIf& xcluster_context,
    const scoped_refptr<MetricEntity>& server_metric_entity) {
  auto xcluster_consumer = std::make_unique<XClusterConsumer>(
      std::move(get_leader_term), ts_uuid, local_client, std::move(connect_to_pg_func),
      std::move(get_namespace_info_func), xcluster_context, server_metric_entity);

  RETURN_NOT_OK(xcluster_consumer->Init());

  return xcluster_consumer;
}

XClusterConsumer::XClusterConsumer(
    std::function<int64_t(const TabletId&)> get_leader_term, const string& ts_uuid,
    client::YBClient& local_client, ConnectToPostgresFunc connect_to_pg_func,
    GetNamespaceInfoFunc get_namespace_info_func, TserverXClusterContextIf& xcluster_context,
    const scoped_refptr<MetricEntity>& server_metric_entity)
    : get_leader_term_func_(std::move(get_leader_term)),
      rpcs_(new rpc::Rpcs),
      log_prefix_(Format("[TS $0]: ", ts_uuid)),
      local_client_(local_client),
      last_safe_time_published_at_(MonoTime::Now()),
      connect_to_pg_func_(std::move(connect_to_pg_func)),
      get_namespace_info_func_(std::move(get_namespace_info_func)),
      xcluster_context_(xcluster_context),
      ts_uuid_(ts_uuid) {
  rate_limiter_ = std::unique_ptr<rocksdb::RateLimiter>(rocksdb::NewGenericRateLimiter(
      FLAGS_apply_changes_max_send_rate_mbps * 1_MB));
  rate_limiter_->EnableLoggingWithDescription("XCluster Output Client");
  SetRateLimiterSpeed();

  rate_limiter_callback_ = CHECK_RESULT(RegisterFlagUpdateCallback(
      &FLAGS_apply_changes_max_send_rate_mbps, "xclusterConsumerRateLimiter",
      std::bind(&XClusterConsumer::SetRateLimiterSpeed, this)));

  auto_flags_version_handler_ = std::make_unique<AutoFlagsVersionHandler>(&local_client_);

  metric_apply_failure_count_ =
      METRIC_xcluster_consumer_apply_failure_count.Instantiate(server_metric_entity);

  metric_poll_failure_count_ =
      METRIC_xcluster_consumer_poll_failure_count.Instantiate(server_metric_entity);

  metric_replication_error_count_ =
      METRIC_xcluster_consumer_replication_error_count.Instantiate(server_metric_entity);
}

XClusterConsumer::~XClusterConsumer() {
  Shutdown();
  SharedLock read_lock(pollers_map_mutex_);
  DCHECK(pollers_map_.empty());
}

Status XClusterConsumer::Init() {
  RETURN_NOT_OK(yb::Thread::Create(
      "XClusterConsumer", "Poll", &XClusterConsumer::RunThread, this, &run_trigger_poll_thread_));
  ThreadPoolBuilder thread_pool_builder("XClusterConsumerHandler");
  if (FLAGS_xcluster_consumer_thread_pool_size > 0) {
    thread_pool_builder.set_max_threads(FLAGS_xcluster_consumer_thread_pool_size);
  }

  return thread_pool_builder.Build(&thread_pool_);
}

void XClusterConsumer::Shutdown() {
  LOG_WITH_PREFIX(INFO) << "Shutting down XClusterConsumer";
  is_shutdown_ = true;

  YB_PROFILE(run_thread_cond_.notify_all());

  // Shutdown the pollers outside of the master_data_mutex lock to keep lock ordering the same.
  std::vector<std::shared_ptr<XClusterPoller>> pollers_to_shutdown;
  decltype(remote_clients_) clients_to_shutdown;
  {
    std::lock_guard write_lock(master_data_mutex_);
    producer_consumer_tablet_map_from_master_.clear();
    uuid_master_addrs_.clear();
    {
      std::lock_guard l(pollers_map_mutex_);

      clients_to_shutdown = std::move(remote_clients_);

      pollers_to_shutdown.reserve(pollers_map_.size());
      for (const auto& poller : pollers_map_) {
        pollers_to_shutdown.emplace_back(std::move(poller.second));
      }
      pollers_map_.clear();
    }
  }

  for (const auto& poller : pollers_to_shutdown) {
    poller->StartShutdown();
  }

  if (thread_pool_) {
    thread_pool_->Shutdown();
  }

  // TODO: Shutdown the client after the thread pool shutdown, otherwise we seem to get stuck. This
  // ordering indicates some bug in the client shutdown code.

  for (auto& [replication_id, client] : clients_to_shutdown) {
    client->Shutdown();
  }

  for (const auto& poller : pollers_to_shutdown) {
    poller->CompleteShutdown();
  }

  if (run_trigger_poll_thread_) {
    WARN_NOT_OK(ThreadJoiner(run_trigger_poll_thread_.get()).Join(), "Could not join thread");
  }

  rate_limiter_callback_.Deregister();

  LOG_WITH_PREFIX(INFO) << "Shut down XClusterConsumer completed";
}

void XClusterConsumer::SetRateLimiterSpeed() {
  WARN_NOT_OK(ResultToStatus(rate_limiter_->SetBytesPerSecond(
      FLAGS_apply_changes_max_send_rate_mbps * 1_MB)),
      "Rate limiter set bytes per second failed");
}

void XClusterConsumer::RunThread() {
  while (true) {
    {
      UniqueLock l(shutdown_mutex_);
      if (run_thread_cond_.wait_for(
              GetLockForCondition(l), 1s, [this]() { return is_shutdown_.load(); })) {
        return;
      }
    }

    TriggerDeletionOfOldPollers();
    TriggerPollForNewTablets();

    PublishXClusterSafeTime();
  }
}

std::vector<std::string> XClusterConsumer::TEST_producer_tablets_running() const {
  SharedLock read_lock(pollers_map_mutex_);

  std::vector<TabletId> tablets;
  for (const auto& producer : pollers_map_) {
    tablets.push_back(producer.first.tablet_id);
  }
  return tablets;
}

std::vector<std::shared_ptr<XClusterPoller>> XClusterConsumer::TEST_ListPollers() const {
  std::vector<std::shared_ptr<XClusterPoller>> ret;
  {
    SharedLock read_lock(pollers_map_mutex_);
    for (const auto& producer : pollers_map_) {
      ret.push_back(producer.second);
    }
  }
  return ret;
}

std::vector<XClusterPollerStats> XClusterConsumer::GetPollerStats() const {
  std::vector<XClusterPollerStats> ret;
  SharedLock read_lock(pollers_map_mutex_);
  for (const auto& [_, poller] : pollers_map_) {
    ret.push_back(poller->GetStats());
  }
  return ret;
}

void XClusterConsumer::HandleMasterHeartbeatResponse(
    const cdc::ConsumerRegistryPB* consumer_registry, int32_t cluster_config_version) {
  // This function is called from the Master heartbeat response which means the errors in sending
  // state have been processed by the Master leader.
  error_collector_.TransitionErrorsFromSendingToSent();

  std::lock_guard write_lock_master(master_data_mutex_);
  if (is_shutdown_) {
    return;
  }

  // Only update it if the version is newer.
  if (cluster_config_version <= cluster_config_version_.load(std::memory_order_acquire)) {
    return;
  }

  cluster_config_version_.store(cluster_config_version, std::memory_order_release);
  producer_consumer_tablet_map_from_master_.clear();
  decltype(uuid_master_addrs_) old_uuid_master_addrs;
  uuid_master_addrs_.swap(old_uuid_master_addrs);

  std::unordered_set<NamespaceId> target_namespaces_in_automatic_mode;
  if (!consumer_registry) {
    LOG_WITH_PREFIX(INFO) << "Given empty xCluster consumer registry: removing Pollers";
    xcluster_context_.UpdateTargetNamespacesInAutomaticModeSet(target_namespaces_in_automatic_mode);
    YB_PROFILE(run_thread_cond_.notify_all());
    return;
  }

  LOG_WITH_PREFIX(INFO) << "Updating xCluster consumer registry: "
                        << consumer_registry->DebugString();

  streams_with_local_tserver_optimization_.clear();
  ddl_queue_streams_.clear();
  stream_schema_version_map_.clear();
  stream_colocated_schema_version_map_.clear();
  min_schema_version_map_.clear();

  for (const auto& [replication_group_id_str, producer_entry_pb] :
       DCHECK_NOTNULL(consumer_registry)->producer_map()) {
    const xcluster::ReplicationGroupId replication_group_id(replication_group_id_str);

    std::vector<HostPort> hp;
    HostPortsFromPBs(producer_entry_pb.master_addrs(), &hp);
    if (ContainsKey(old_uuid_master_addrs, replication_group_id) &&
        old_uuid_master_addrs[replication_group_id] != hp) {
      // If master addresses changed, mark for YBClient update.
      changed_master_addrs_.insert(replication_group_id);
    }
    uuid_master_addrs_[replication_group_id] = std::move(hp);

    if (producer_entry_pb.automatic_ddl_mode()) {
      for (const auto& [_, stream_entry_pb] : producer_entry_pb.stream_map()) {
        const auto& consumer_table_id = stream_entry_pb.consumer_table_id();
        // In automatic mode, every namespace that is being replicated to has a stream for the table
        // sequences_data; we use the alias of that table to extract the namespace ID.
        if (xcluster::IsSequencesDataAlias(consumer_table_id)) {
          auto namespace_id = xcluster::GetReplicationNamespaceBelongsTo(consumer_table_id);
          if (namespace_id) {
            target_namespaces_in_automatic_mode.insert(*namespace_id);
          } else {
            LOG_WITH_FUNC(DFATAL) << "Bad sequences_data alias: " << consumer_table_id;
          }
        }
      }
    }

    UpdateReplicationGroupInMemState(replication_group_id, producer_entry_pb);
  }
  xcluster_context_.UpdateTargetNamespacesInAutomaticModeSet(target_namespaces_in_automatic_mode);
  // Wake up the background thread to stop old pollers and start new ones.
  YB_PROFILE(run_thread_cond_.notify_all());
}

// NOTE: This happens on TS.heartbeat, so it needs to finish quickly
void XClusterConsumer::UpdateReplicationGroupInMemState(
    const xcluster::ReplicationGroupId& replication_group_id,
    const yb::cdc::ProducerEntryPB& producer_entry_pb) {
  auto_flags_version_handler_->InsertOrUpdate(
      replication_group_id, producer_entry_pb.compatible_auto_flag_config_version(),
      producer_entry_pb.validated_auto_flags_config_version());

  for (const auto& [stream_id_str, stream_entry_pb] : producer_entry_pb.stream_map()) {
    auto stream_id_result = xrepl::StreamId::FromString(stream_id_str);
    if (!stream_id_result) {
      LOG_WITH_PREFIX_AND_FUNC(WARNING) << "Invalid stream id: " << stream_id_str;
      continue;
    }
    auto& stream_id = *stream_id_result;

    if (stream_entry_pb.local_tserver_optimized()) {
      LOG_WITH_PREFIX(INFO) << Format("Stream $0 will use local tserver optimization", stream_id);
      streams_with_local_tserver_optimization_.insert(stream_id);
    }
    if (stream_entry_pb.is_ddl_queue_table()) {
      LOG(INFO) << Format("Stream $0 is a ddl_queue stream", stream_id);
      ddl_queue_streams_.insert(stream_id);
    }

    if (stream_entry_pb.has_schema_versions()) {
      auto& schema_version_map = stream_schema_version_map_[stream_id];
      auto schema_versions = stream_entry_pb.schema_versions();
      ProcessStreamSchemaVersions(
          stream_id, stream_entry_pb.consumer_table_id(), schema_versions, schema_version_map);
    }

    for (const auto& [colocated_id, versions] : stream_entry_pb.colocated_schema_versions()) {
      auto& schema_version_map = stream_colocated_schema_version_map_[stream_id][colocated_id];
      ProcessStreamSchemaVersions(
          stream_id, stream_entry_pb.consumer_table_id(), versions, schema_version_map,
          colocated_id);
    }

    for (const auto& [consumer_tablet_id, producer_tablet_list] :
         stream_entry_pb.consumer_producer_tablet_map()) {
      for (const auto& producer_tablet_id : producer_tablet_list.tablets()) {
        auto xCluster_tablet_info = xcluster::XClusterTabletInfo{
            .producer_tablet_info =
                {replication_group_id, stream_id, producer_tablet_id,
                 stream_entry_pb.producer_table_id()},
            .consumer_tablet_info = {consumer_tablet_id, stream_entry_pb.consumer_table_id()},
            .disable_stream = producer_entry_pb.disable_stream(),
            .automatic_ddl_mode = producer_entry_pb.automatic_ddl_mode()};
        producer_consumer_tablet_map_from_master_.emplace(std::move(xCluster_tablet_info));
      }
    }
  }
}

void XClusterConsumer::ProcessStreamSchemaVersions(
    const xrepl::StreamId& stream_id, const TableId& consumer_table_id,
    const cdc::SchemaVersionsPB& schema_versions,
    std::unordered_map<SchemaVersion, SchemaVersion>& schema_version_map,
    const ColocationId& colocation_id) {
  auto min_schema_version_result =
      ProcessSchemaVersionsAndGetMinSchemaVersion(schema_versions, schema_version_map);
  if (!min_schema_version_result) {
    LOG_WITH_PREFIX(ERROR) << Format(
        "Failed to process schema versions for stream $0: $1", stream_id,
        min_schema_version_result);
    return;
  }
  min_schema_version_map_[std::make_pair(consumer_table_id, colocation_id)] =
      *min_schema_version_result;
}

SchemaVersion XClusterConsumer::GetMinXClusterSchemaVersion(const TableId& table_id,
    const ColocationId& colocation_id) {
  SharedLock l(master_data_mutex_);
  if (!is_shutdown_) {
    auto iter = min_schema_version_map_.find(make_pair(table_id, colocation_id));
    if (iter != min_schema_version_map_.end()) {
      VLOG_WITH_FUNC(4) << Format("XCluster min schema version for $0,$1:$2",
          table_id, colocation_id, iter->second);
      return iter->second;
    }

    VLOG_WITH_FUNC(4) << Format("XCluster tableid,colocationid pair not found in registry: $0,$1",
        table_id, colocation_id);
  }

  return cdc::kInvalidSchemaVersion;
}

void XClusterConsumer::TriggerPollForNewTablets() {
  // TODO(#24924): Check if our config is stale before starting new pollers.

  ACQUIRE_SHARED_LOCK_IF_ONLINE;

  int32_t current_cluster_config_version = cluster_config_version();

  for (const auto& entry : producer_consumer_tablet_map_from_master_) {
    const auto& producer_tablet_info = entry.producer_tablet_info;
    const auto& consumer_tablet_info = entry.consumer_tablet_info;
    const auto& replication_group_id = producer_tablet_info.replication_group_id;
    bool start_polling;
    {
      SharedLock read_lock_pollers(pollers_map_mutex_);
      // Iff we are the leader we will get a valid term.
      start_polling =
          !pollers_map_.contains(producer_tablet_info) &&
          get_leader_term_func_(consumer_tablet_info.tablet_id) != yb::OpId::kUnknownTerm;

      // Update the Master Addresses, if altered after setup.
      if (ContainsKey(remote_clients_, replication_group_id) &&
          changed_master_addrs_.count(replication_group_id) > 0) {
        auto status = remote_clients_[replication_group_id]->SetMasterAddresses(
            uuid_master_addrs_[replication_group_id]);
        if (status.ok()) {
          changed_master_addrs_.erase(replication_group_id);
        } else {
          LOG_WITH_PREFIX(WARNING) << "Problem Setting Master Addresses for "
                                   << replication_group_id << ": " << status.ToString();
        }
      }
    }
    if (start_polling) {
      std::lock_guard write_lock_pollers(pollers_map_mutex_);
      const auto leader_term = get_leader_term_func_(consumer_tablet_info.tablet_id);

      // Check again, since we unlocked.
      start_polling =
          !pollers_map_.contains(producer_tablet_info) && leader_term != yb::OpId::kUnknownTerm;
      if (start_polling) {
        // This is a new tablet, trigger a poll.
        // See if we need to create a new client connection
        if (!remote_clients_.contains(replication_group_id)) {
          if (!uuid_master_addrs_.contains(replication_group_id)) {
            LOG(DFATAL) << "Master address not found for " << replication_group_id;
            return;  // Don't finish creation.  Try again on the next heartbeat.
          }

          auto remote_client = client::XClusterRemoteClientHolder::Create(
              replication_group_id, uuid_master_addrs_[replication_group_id]);
          if (!remote_client) {
            LOG(WARNING) << "Could not build messenger for " << replication_group_id << ": "
                         << remote_client.status();
            return;  // Don't finish creation.  Try again on the next heartbeat.
          }
          remote_clients_[replication_group_id] = std::move(*remote_client);
        }

        // Now create the poller.
        bool use_local_tserver =
            streams_with_local_tserver_optimization_.contains(producer_tablet_info.stream_id);

        NamespaceId consumer_namespace_id;
        NamespaceName consumer_namespace_name;
        auto consumer_table_id = consumer_tablet_info.table_id;
        if (xcluster::IsSequencesDataAlias(consumer_table_id)) {
          auto namespace_id_result = xcluster::GetReplicationNamespaceBelongsTo(consumer_table_id);
          if (namespace_id_result) {
            consumer_namespace_id = *namespace_id_result;
            // We don't need consumer_namespace_name for sequence streams so don't bother computing
            // it.
            consumer_namespace_name = "<unknown>";
          } else {
            LOG(DFATAL) << "Malformed sequences_data alias table ID: " << consumer_table_id
                        << "; skipping creation of a poller for a tablet belonging to that table: "
                        << consumer_tablet_info.tablet_id;
            continue;
          }
        } else {
          auto namespace_info_result = get_namespace_info_func_(consumer_tablet_info.tablet_id);
          if (!namespace_info_result.ok()) {
            LOG(WARNING) << "Could not get namespace info for tablet "
                         << consumer_tablet_info.tablet_id << ": "
                         << namespace_info_result.status().ToString();
            continue;  // Don't finish creation.  Try again on the next RunThread().
          }
          consumer_namespace_id = namespace_info_result->first;
          consumer_namespace_name = namespace_info_result->second;
        }

        auto xcluster_poller = std::make_shared<XClusterPoller>(
            producer_tablet_info, consumer_tablet_info, consumer_namespace_id,
            auto_flags_version_handler_->GetAutoFlagsCompatibleVersion(
                producer_tablet_info.replication_group_id),
            thread_pool_.get(), rpcs_.get(), local_client_, remote_clients_[replication_group_id],
            this, leader_term, get_leader_term_func_, entry.automatic_ddl_mode,
            entry.disable_stream, ts_uuid_);

        if (ddl_queue_streams_.contains(producer_tablet_info.stream_id)) {
          auto source_namespace_id = GetNamespaceIdFromYsqlTableId(producer_tablet_info.table_id);
          if (!source_namespace_id.ok()) {
            LOG(WARNING) << "Could not get source namespace id for table "
                         << producer_tablet_info.table_id << ": "
                         << source_namespace_id.status().ToString();
            continue;  // Don't finish creation.  Try again on the next RunThread().
          }
          xcluster_poller->InitDDLQueuePoller(
              use_local_tserver, rate_limiter_.get(), consumer_namespace_name, *source_namespace_id,
              xcluster_context_, connect_to_pg_func_);
        } else {
          xcluster_poller->Init(use_local_tserver, rate_limiter_.get());
        }

        UpdatePollerSchemaVersionMaps(xcluster_poller, producer_tablet_info.stream_id);

        LOG_WITH_PREFIX(INFO) << Format(
            "Start polling for producer tablet $0, consumer tablet $1", producer_tablet_info,
            consumer_tablet_info.tablet_id);
        pollers_map_[producer_tablet_info] = xcluster_poller;

        error_collector_.AddPoller(xcluster_poller->GetPollerId());
        xcluster_poller->SchedulePoll();
      }
    }

    // Notify existing pollers only if there was a cluster config refresh since last time.
    if (current_cluster_config_version > last_polled_at_cluster_config_version_) {
      SharedLock read_lock_pollers(pollers_map_mutex_);
      auto xcluster_poller = FindPtrOrNull(pollers_map_, producer_tablet_info);
      if (xcluster_poller) {
        UpdatePollerSchemaVersionMaps(xcluster_poller, producer_tablet_info.stream_id);
      }
    }
  }

  last_polled_at_cluster_config_version_ = current_cluster_config_version;
}

void XClusterConsumer::UpdatePollerSchemaVersionMaps(
    std::shared_ptr<XClusterPoller> xcluster_poller, const xrepl::StreamId& stream_id) const {
  auto schema_versions = FindOrNull(stream_schema_version_map_, stream_id);
  if (schema_versions) {
    xcluster_poller->UpdateSchemaVersions(*schema_versions);
  }

  auto colocated_schema_versions = FindOrNull(stream_colocated_schema_version_map_, stream_id);
  if (colocated_schema_versions) {
    xcluster_poller->UpdateColocatedSchemaVersionMap(*colocated_schema_versions);
  }
}

void XClusterConsumer::TriggerDeletionOfOldPollers() {
  // Shutdown outside of master_data_mutex_ lock, to not block any heartbeats.
  std::vector<std::shared_ptr<client::XClusterRemoteClientHolder>> clients_to_delete;
  std::vector<std::shared_ptr<XClusterPoller>> pollers_to_shutdown;
  {
    ACQUIRE_SHARED_LOCK_IF_ONLINE;
    std::lock_guard write_lock_pollers(pollers_map_mutex_);
    for (auto it = pollers_map_.cbegin(); it != pollers_map_.cend();) {
      const xcluster::ProducerTabletInfo producer_info = it->first;
      std::shared_ptr<XClusterPoller> poller = it->second;
      // Check if we need to delete this poller.
      std::string reason;
      if (ShouldContinuePolling(producer_info, *poller, reason)) {
        ++it;
        continue;
      }

      const xcluster::ConsumerTabletInfo& consumer_info = poller->GetConsumerTabletInfo();

      LOG_WITH_PREFIX(INFO) << Format(
          "Stop polling for producer tablet $0, consumer tablet $1. Reason: $2.", producer_info,
          consumer_info.tablet_id, reason);
      pollers_to_shutdown.emplace_back(poller);
      it = pollers_map_.erase(it);

      // Check if no more objects with this ReplicationGroup exist after registry refresh.
      if (!ContainsKey(uuid_master_addrs_, producer_info.replication_group_id)) {
        auto_flags_version_handler_->Delete(producer_info.replication_group_id);

        auto clients_it = remote_clients_.find(producer_info.replication_group_id);
        if (clients_it != remote_clients_.end()) {
          clients_to_delete.emplace_back(clients_it->second);
          remote_clients_.erase(clients_it);
        }
      }
    }
  }

  for (const auto& poller : pollers_to_shutdown) {
    poller->StartShutdown();
  }

  for (const auto& poller : pollers_to_shutdown) {
    poller->CompleteShutdown();
  }

  for (const auto& poller : pollers_to_shutdown) {
    error_collector_.RemovePoller(poller->GetPollerId());
  }

  for (const auto& client : clients_to_delete) {
    client->Shutdown();
  }
}

bool XClusterConsumer::ShouldContinuePolling(
    const xcluster::ProducerTabletInfo producer_tablet_info, XClusterPoller& poller,
    std::string& reason) {
  if (FLAGS_TEST_xcluster_disable_delete_old_pollers) {
    return true;
  }

  if (!poller.ShouldContinuePolling()) {
    reason = "the Poller failed or is stuck";
    return false;
  }

  const auto& it = producer_consumer_tablet_map_from_master_.find(producer_tablet_info);
  // We either no longer need to poll for this tablet, or a different tablet should be polling
  // for it now instead of this one (due to a local tablet split).
  if (it == producer_consumer_tablet_map_from_master_.end()) {
    reason = "the Poller is no longer needed";
    return false;
  }
  if (it->consumer_tablet_info.tablet_id != poller.GetConsumerTabletInfo().tablet_id) {
    reason = "the consumer tablet changed";
    return false;
  }

  poller.SetPaused(it->disable_stream);

  return true;
}

std::string XClusterConsumer::LogPrefix() { return log_prefix_; }

int32_t XClusterConsumer::cluster_config_version() const {
  return cluster_config_version_.load(std::memory_order_acquire);
}

Status XClusterConsumer::ReloadCertificates() {
  SharedLock read_lock(pollers_map_mutex_);
  for (const auto& [_, client] : remote_clients_) {
    RETURN_NOT_OK(client->ReloadCertificates());
  }

  return Status::OK();
}

void XClusterConsumer::PublishXClusterSafeTime() {
  typeof(safe_time_callbacks_) callbacks;
  {
    std::lock_guard l(safe_time_callback_mutex_);
    callbacks.swap(safe_time_callbacks_);
  }

  auto s = PublishXClusterSafeTimeInternal();

  if (s.ok()) {
    for (auto& callback : callbacks) {
      callback();
    }
  } else {
    YB_LOG_EVERY_N(WARNING, 10) << "PublishXClusterSafeTime failed: " << s;

    // Retry the callbacks on the next iteration.
    std::lock_guard l(safe_time_callback_mutex_);
    safe_time_callbacks_.insert(
        safe_time_callbacks_.end(), std::make_move_iterator(callbacks.begin()),
        std::make_move_iterator(callbacks.end()));
  }
}

Status XClusterConsumer::PublishXClusterSafeTimeInternal() {
  if (is_shutdown_ || !xcluster_context_.SafeTimeComputationRequired()) {
    return Status::OK();
  }

  std::lock_guard l(safe_time_update_mutex_);

  int wait_time = FLAGS_xcluster_safe_time_update_interval_secs;
  if (wait_time <= 0 || MonoTime::Now() - last_safe_time_published_at_ < wait_time * 1s) {
    return Status::OK();
  }

  std::unordered_map<xcluster::ProducerTabletInfo, HybridTime> safe_time_map;
  {
    SharedLock read_lock(pollers_map_mutex_);
    for (auto& [producer_info, poller] : pollers_map_) {
      if (xcluster_context_.SafeTimeComputationRequired(poller->GetConsumerNamespaceId())) {
        auto safe_time = poller->GetSafeTime();
        VLOG_IF_WITH_FUNC(2, safe_time.is_special()) << Format(
            "Found special safe time for producer tablet $0: $1", producer_info, safe_time);
        if (!safe_time.is_special()) {
          safe_time_map[producer_info] = safe_time;
        }
      }
    }
  }

  if (safe_time_map.empty()) {
    last_safe_time_published_at_ = MonoTime::Now();
    return Status::OK();
  }

  static const client::YBTableName safe_time_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kXClusterSafeTimeTableName);

  if (!xcluster_safe_time_table_ready_) {
    // Master has not created the table yet. Nothing to do for now.
    if (!VERIFY_RESULT(local_client_.TableExists(safe_time_table_name))) {
      return Status::OK();
    }
    RETURN_NOT_OK(local_client_.WaitForCreateTableToFinish(safe_time_table_name));

    xcluster_safe_time_table_ready_ = true;
  }

  if (!safe_time_table_) {
    auto table = std::make_unique<client::TableHandle>();
    RETURN_NOT_OK(table->Open(safe_time_table_name, &local_client_));
    safe_time_table_.swap(table);
  }

  auto session = local_client_.NewSession(local_client_.default_rpc_timeout());
  for (auto& [producer_info, safe_time] : safe_time_map) {
    RSTATUS_DCHECK(
        !safe_time.is_special(), IllegalState,
        Format("Unexpected safe time for $0: $1", producer_info, safe_time.ToDebugString()));
    const auto safe_time_uint64 = safe_time.ToUint64();
    const auto key =
        VERIFY_RESULT(xcluster::SafeTimeTablePK::FromProducerTabletInfo(producer_info));

    const auto op = safe_time_table_->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, key.replication_group_id_column_value().ToString());
    QLAddStringHashValue(req, key.tablet_id_column_value());
    safe_time_table_->AddInt64ColumnValue(req, master::kXCSafeTime, safe_time_uint64);

    // Ensure that we only update the row if the safe time is greater than the current value.
    auto condition = req->mutable_if_expr()->mutable_condition();
    condition->set_op(QL_OP_OR);
    safe_time_table_->AddCondition(condition, QL_OP_NOT_EXISTS);
    safe_time_table_->AddInt64Condition(
        condition, master::kXCSafeTime, QL_OP_LESS_THAN, safe_time_uint64);
    // Add ref for the columns since we need to fetch it to evaluate the condition.
    req->mutable_column_refs()->add_ids(safe_time_table_->ColumnId(master::kXCSafeTime));

    VLOG_WITH_FUNC(2) << "Key: " << key.ToString()
                      << ", Producer TableId: " << producer_info.table_id
                      << ", Producer TabletId: " << producer_info.tablet_id
                      << ", SafeTime: " << safe_time.ToDebugString();
    session->Apply(std::move(op));
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  // We dont use TEST_Flush here since it gets stuck on shutdown (#19402).
  auto future = session->FlushFuture();
  SCHECK(
      future.wait_for(local_client_.default_rpc_timeout().ToSteadyDuration()) ==
          std::future_status::ready,
      IllegalState, "Failed to flush to XClusterSafeTime table");

  RETURN_NOT_OK_PREPEND(future.get().status, "Failed to flush to XClusterSafeTime table");

  last_safe_time_published_at_ = MonoTime::Now();

  return Status::OK();
}

void XClusterConsumer::AddSafeTimePublishCallback(std::function<void()> callback) {
  std::lock_guard l(safe_time_callback_mutex_);
  safe_time_callbacks_.push_back(std::move(callback));
}

void XClusterConsumer::StoreReplicationError(
    const XClusterPollerId& poller_id, ReplicationErrorPb error) {
  error_collector_.StoreError(poller_id, error);
  if (error != ReplicationErrorPb::REPLICATION_OK &&
      error != ReplicationErrorPb::REPLICATION_PAUSED) {
    metric_replication_error_count_->Increment();
  }
}

// This happens on TS.heartbeat request, so it needs to finish quickly.
void XClusterConsumer::PopulateMasterHeartbeatRequest(
    master::TSHeartbeatRequestPB* req, bool needs_full_tablet_report) {
  if (needs_full_tablet_report) {
    // We first fill partial report and then figure out if a full report is needed. Clear out any
    // partially filled state.
    req->clear_xcluster_consumer_replication_status();
  } else {
    DCHECK(req->xcluster_consumer_replication_status().empty()) << "Request populated twice";
  }

  // Map of ReplicationGroupId, consumer TableId, producer TabletId to consumer term and
  // error.
  auto errors_to_send = error_collector_.GetErrorsToSend(needs_full_tablet_report);

  for (const auto& [replication_group_id, table_map] : errors_to_send) {
    auto* replication_group_status = req->add_xcluster_consumer_replication_status();
    replication_group_status->set_replication_group_id(std::move(replication_group_id.ToString()));
    for (const auto& [consumer_table_id, producer_tablet_map] : table_map) {
      auto* table_status = replication_group_status->add_table_status();
      table_status->set_consumer_table_id(std::move(consumer_table_id));
      for (const auto& [producer_tablet_id, error_info] : producer_tablet_map) {
        auto* stream_tablet_status = table_status->add_stream_tablet_status();
        stream_tablet_status->set_producer_tablet_id(std::move(producer_tablet_id));
        stream_tablet_status->set_consumer_term(error_info.consumer_term);
        stream_tablet_status->set_error(error_info.error);
      }
    }
  }
}

Status XClusterConsumer::ReportNewAutoFlagConfigVersion(
    const xcluster::ReplicationGroupId& replication_group_id, uint32_t new_version) const {
  return auto_flags_version_handler_->ReportNewAutoFlagConfigVersion(
      replication_group_id, new_version);
}

void XClusterConsumer::ClearAllClientMetaCaches() const {
  std::lock_guard write_lock_pollers(pollers_map_mutex_);
  for (auto& [group_id, xcluster_client] : remote_clients_) {
    xcluster_client->GetYbClient().ClearAllMetaCachesOnServer();
  }
}

void XClusterConsumer::WriteServerMetaCacheAsJson(JsonWriter& writer) const {
  SharedLock read_lock(pollers_map_mutex_);
  for (const auto& [_, remote_client] : remote_clients_) {
    const auto& client = remote_client->GetYbClient();
    writer.String(client.client_name());
    client.AddMetaCacheInfo(&writer);
  }
}

}  // namespace tserver
}  // namespace yb
