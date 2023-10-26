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

#include <shared_mutex>
#include <chrono>

#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_defaults.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/secure_stream.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/xcluster_poller.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/cdc_util.h"

#include "yb/client/error.h"
#include "yb/client/client.h"

#include "yb/rocksdb/rate_limiter.h"
#include "yb/rocksdb/util/rate_limiter.h"

#include "yb/gutil/map-util.h"
#include "yb/server/secure.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/thread.h"

using std::string;

DEFINE_UNKNOWN_int32(cdc_consumer_handler_thread_pool_size, 0,
    "Override the max thread pool size for CDCConsumerHandler, which is used by "
    "CDCPollers. If set to 0, then the thread pool will use the default size (number of "
    "cpus on the system).");
TAG_FLAG(cdc_consumer_handler_thread_pool_size, advanced);

DEFINE_RUNTIME_int32(xcluster_safe_time_update_interval_secs, 1,
    "The interval at which xcluster safe time is computed. This controls the staleness of the data "
    "seen when performing database level xcluster consistent reads. If there is any additional lag "
    "in the replication, then it will add to the overall staleness of the data.");

DEFINE_RUNTIME_int32(apply_changes_max_send_rate_mbps, 200,
                    "Server-wide max apply rate for xcluster traffic from a consumer node.");

static bool ValidateXClusterSafeTimeUpdateInterval(const char* flagname, int32 value) {
  if (value <= 0) {
    fprintf(stderr, "Invalid value for --%s: %d, must be greater than 0\n", flagname, value);
    return false;
  }
  return true;
}

DEFINE_validator(xcluster_safe_time_update_interval_secs, &ValidateXClusterSafeTimeUpdateInterval);

DEFINE_test_flag(
    bool, xcluster_disable_delete_old_pollers, false,
    "Disables the deleting of old xcluster pollers that are no longer needed.");

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_for_cdc_dir);

using namespace std::chrono_literals;

namespace yb {

namespace tserver {
using cdc::ProducerTabletInfo;

XClusterClient::~XClusterClient() {
  if (messenger) {
    messenger->Shutdown();
  }
}

void XClusterClient::Shutdown() {
  if (client) {
    client->Shutdown();
  }
  if (messenger) {
    messenger->Shutdown();
  }
}

Result<std::unique_ptr<XClusterConsumer>> XClusterConsumer::Create(
    std::function<bool(const std::string&)> is_leader_for_tablet,
    std::function<int64_t(const TabletId&)>
        get_leader_term,
    rpc::ProxyCache* proxy_cache,
    TabletServer* tserver) {
  auto master_addrs = tserver->options().GetMasterAddresses();
  std::vector<std::string> hostport_strs;
  hostport_strs.reserve(master_addrs->size());
  for (const auto& hp : *master_addrs) {
    hostport_strs.push_back(HostPort::ToCommaSeparatedString(hp));
  }

  auto local_client = std::make_unique<XClusterClient>();
  if (FLAGS_use_node_to_node_encryption) {
    rpc::MessengerBuilder messenger_builder("cdc-consumer");

    local_client->secure_context = VERIFY_RESULT(server::SetupSecureContext(
        "", "", server::SecureContextType::kInternal, &messenger_builder));

    local_client->messenger = VERIFY_RESULT(messenger_builder.Build());
  }

  local_client->client = VERIFY_RESULT(
      client::YBClientBuilder()
          .master_server_addrs(hostport_strs)
          .set_client_name("CDCConsumerLocal")
          .default_rpc_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms))
          .Build(local_client->messenger.get()));

  local_client->client->SetLocalTabletServer(tserver->permanent_uuid(), tserver->proxy(), tserver);
  auto xcluster_consumer = std::make_unique<XClusterConsumer>(
      std::move(is_leader_for_tablet), std::move(get_leader_term), proxy_cache,
      tserver->permanent_uuid(), std::move(local_client), &tserver->TransactionManager());

  // TODO(NIC): Unify xcluster_consumer thread_pool & remote_client_ threadpools
  RETURN_NOT_OK(yb::Thread::Create(
      "XClusterConsumer", "Poll", &XClusterConsumer::RunThread, xcluster_consumer.get(),
      &xcluster_consumer->run_trigger_poll_thread_));
  ThreadPoolBuilder cdc_consumer_thread_pool_builder("CDCConsumerHandler");
  if (FLAGS_cdc_consumer_handler_thread_pool_size > 0) {
    cdc_consumer_thread_pool_builder.set_max_threads(FLAGS_cdc_consumer_handler_thread_pool_size);
  }
  RETURN_NOT_OK(cdc_consumer_thread_pool_builder.Build(&xcluster_consumer->thread_pool_));

  return xcluster_consumer;
}

XClusterConsumer::XClusterConsumer(
    std::function<bool(const std::string&)> is_leader_for_tablet,
    std::function<int64_t(const TabletId&)>
        get_leader_term,
    rpc::ProxyCache* proxy_cache,
    const string& ts_uuid,
    std::unique_ptr<XClusterClient>
        local_client,
    client::TransactionManager* transaction_manager)
    : is_leader_for_tablet_(std::move(is_leader_for_tablet)),
      get_leader_term_(std::move(get_leader_term)),
      rpcs_(new rpc::Rpcs),
      log_prefix_(Format("[TS $0]: ", ts_uuid)),
      local_client_(std::move(local_client)),
      last_safe_time_published_at_(MonoTime::Now()),
      transaction_manager_(transaction_manager),
      rate_limiter_(std::unique_ptr<rocksdb::RateLimiter>(rocksdb::NewGenericRateLimiter(
          GetAtomicFlag(&FLAGS_apply_changes_max_send_rate_mbps) *
          MonoTime::kMicrosecondsPerSecond))) {
        rate_limiter_->EnableLoggingWithDescription("XCluster Output Client");
      }

XClusterConsumer::~XClusterConsumer() {
  Shutdown();
  SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);
  DCHECK(producer_pollers_map_.empty());
}

void XClusterConsumer::Shutdown() {
  LOG_WITH_PREFIX(INFO) << "Shutting down CDC Consumer";
  {
    std::lock_guard<std::mutex> l(should_run_mutex_);
    should_run_ = false;
  }
  cond_.notify_all();

  if (thread_pool_) {
    thread_pool_->Shutdown();
  }

  // Shutdown the pollers outside of the master_data_mutex lock to keep lock ordering the same.
  std::vector<std::shared_ptr<XClusterPoller>> pollers_to_shutdown;
  {
    std::lock_guard<rw_spinlock> write_lock(master_data_mutex_);
    producer_consumer_tablet_map_from_master_.clear();
    uuid_master_addrs_.clear();
    {
      std::lock_guard<rw_spinlock> producer_pollers_map_write_lock(producer_pollers_map_mutex_);
      // Shutdown the remote and local clients, and abort any of their ongoing rpcs.
      for (auto& uuid_and_client : remote_clients_) {
        uuid_and_client.second->Shutdown();
      }

      // Fetch all the pollers.
      pollers_to_shutdown.reserve(pollers_to_shutdown.size());
      for (const auto& poller : producer_pollers_map_) {
        pollers_to_shutdown.push_back(poller.second);
      }
      producer_pollers_map_.clear();
    }
    local_client_->client->Shutdown();
  }

  // Now can shutdown the pollers.
  for (const auto& poller : pollers_to_shutdown) {
    poller->Shutdown();
  }

  if (run_trigger_poll_thread_) {
    WARN_NOT_OK(ThreadJoiner(run_trigger_poll_thread_.get()).Join(), "Could not join thread");
  }
}

void XClusterConsumer::RunThread() {
  while (true) {
    {
      std::unique_lock<std::mutex> l(should_run_mutex_);
      if (!should_run_) {
        return;
      }
      cond_.wait_for(l, 1000ms);
      if (!should_run_) {
        return;
      }
    }

    TriggerDeletionOfOldPollers();
    TriggerPollForNewTablets();

    auto s = PublishXClusterSafeTime();
    YB_LOG_IF_EVERY_N(WARNING, !s.ok(), 10) << "PublishXClusterSafeTime failed: " << s;
  }
}

void XClusterConsumer::RefreshWithNewRegistryFromMaster(
    const cdc::ConsumerRegistryPB* consumer_registry, int32_t cluster_config_version) {
  UpdateInMemoryState(consumer_registry, cluster_config_version);
  cond_.notify_all();
}

std::vector<std::string> XClusterConsumer::TEST_producer_tablets_running() {
  SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);

  std::vector<string> tablets;
  for (const auto& producer : producer_pollers_map_) {
    tablets.push_back(producer.first.tablet_id);
  }
  return tablets;
}

std::vector<std::shared_ptr<XClusterPoller>> XClusterConsumer::TEST_ListPollers() {
  std::vector<std::shared_ptr<XClusterPoller>> ret;
  {
    SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);
    for (const auto& producer : producer_pollers_map_) {
      ret.push_back(producer.second);
    }
  }
  return ret;
}

std::vector<XClusterPollerStats> XClusterConsumer::GetPollerStats() const {
  std::vector<XClusterPollerStats> ret;
  {
    SharedLock read_lock(producer_pollers_map_mutex_);
    for (const auto& [_, poller] : producer_pollers_map_) {
      ret.push_back(poller->GetStats());
    }
  }
  return ret;
}

// NOTE: This happens on TS.heartbeat, so it needs to finish quickly
void XClusterConsumer::UpdateInMemoryState(
    const cdc::ConsumerRegistryPB* consumer_registry, int32_t cluster_config_version) {
  {
    std::lock_guard<std::mutex> l(should_run_mutex_);
    if (!should_run_) {
      return;
    }
  }
  std::lock_guard<rw_spinlock> write_lock_master(master_data_mutex_);

  // Only update it if the version is newer.
  if (cluster_config_version <= cluster_config_version_.load(std::memory_order_acquire)) {
    return;
  }

  if (consumer_registry->enable_replicate_transaction_status_table() &&
      global_transaction_status_tablets_.empty()) {
    auto global_transaction_status_table_name = client::YBTableName(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);

    std::vector<TabletId> tablets;
    const auto get_tablets_status = local_client_->client->GetTablets(
        global_transaction_status_table_name, 0 /* max_tablets */, &tablets, nullptr /* ranges */);

    if (!get_tablets_status.ok() || tablets.empty()) {
      // We could not open the transaction status table, so return without setting any in-memory
      // state.
      LOG(WARNING) << "Error getting global transaction status tablets: " << get_tablets_status;
      cond_.notify_all();
      return;
    }

    // Sort tablets to ensure we have the same order across all XClusterConsumers.
    sort(tablets.begin(), tablets.end());

    // TODO handle adding of new txn status tablets (GH #16307).
    // Currently we block add_transaction_tablet for xCluster enabled clusters, since adding in a
    // new txn status tablet would disrupt the deterministic mapping we have for txn id -> status
    // tablet (since we would need to support existing txns and new ones).
    global_transaction_status_tablets_ = std::move(tablets);
  }

  cluster_config_version_.store(cluster_config_version, std::memory_order_release);
  producer_consumer_tablet_map_from_master_.clear();
  decltype(uuid_master_addrs_) old_uuid_master_addrs;
  uuid_master_addrs_.swap(old_uuid_master_addrs);

  if (!consumer_registry) {
    LOG_WITH_PREFIX(INFO) << "Given empty CDC consumer registry: removing Pollers";
    consumer_role_ = cdc::XClusterRole::ACTIVE;
    // Clear the tablets list in case users want to increase number of txn status tablets in between
    // having active replication setups.
    global_transaction_status_tablets_.clear();
    cond_.notify_all();
    return;
  }

  LOG_WITH_PREFIX(INFO) << "Updating CDC consumer registry: " << consumer_registry->DebugString();

  consumer_role_ = consumer_registry->role();
  streams_with_local_tserver_optimization_.clear();
  stream_schema_version_map_.clear();
  stream_colocated_schema_version_map_.clear();
  stream_to_schema_version_.clear();
  min_schema_version_map_.clear();

  for (const auto& producer_map : DCHECK_NOTNULL(consumer_registry)->producer_map()) {
    const auto& producer_entry_pb = producer_map.second;
    // recreate the UUID connection information
    if (!ContainsKey(uuid_master_addrs_, producer_map.first)) {
      std::vector<HostPort> hp;
      HostPortsFromPBs(producer_map.second.master_addrs(), &hp);
      uuid_master_addrs_[producer_map.first] = HostPort::ToCommaSeparatedString(hp);

      // If master addresses changed, mark for YBClient update.
      if (ContainsKey(old_uuid_master_addrs, producer_map.first) &&
          uuid_master_addrs_[producer_map.first] != old_uuid_master_addrs[producer_map.first]) {
        changed_master_addrs_.insert(producer_map.first);
      }
    }
    // recreate the set of CDCPollers
    for (const auto& stream_entry : producer_entry_pb.stream_map()) {
      const auto& stream_entry_pb = stream_entry.second;
      if (stream_entry_pb.local_tserver_optimized()) {
        LOG_WITH_PREFIX(INFO) << Format(
            "Stream $0 will use local tserver optimization", stream_entry.first);
        streams_with_local_tserver_optimization_.insert(stream_entry.first);
      }
      if (stream_entry_pb.has_producer_schema()) {
        stream_to_schema_version_[stream_entry.first] = std::make_pair(
            stream_entry_pb.producer_schema().validated_schema_version(),
            stream_entry_pb.producer_schema().last_compatible_consumer_schema_version());
      }

      if (stream_entry_pb.has_schema_versions()) {
        auto& schema_version_map = stream_schema_version_map_[stream_entry.first];
        auto schema_versions = stream_entry_pb.schema_versions();
        schema_version_map[schema_versions.current_producer_schema_version()] =
            schema_versions.current_consumer_schema_version();
        // Update the old producer schema version, only if it is not the same as
        // current producer schema version.
        if (schema_versions.old_producer_schema_version() !=
            schema_versions.current_producer_schema_version()) {
          DCHECK(schema_versions.old_producer_schema_version() <
              schema_versions.current_producer_schema_version());
          schema_version_map[schema_versions.old_producer_schema_version()] =
              schema_versions.old_consumer_schema_version();
        }

        auto min_schema_version = schema_versions.old_consumer_schema_version() > 0 ?
            schema_versions.old_consumer_schema_version() :
            schema_versions.current_consumer_schema_version();
        min_schema_version_map_[std::make_pair(
            stream_entry_pb.consumer_table_id(), kColocationIdNotSet)] = min_schema_version;
      }

      for (const auto& [colocated_id, versions] : stream_entry_pb.colocated_schema_versions()) {
        auto& schema_version_map =
            stream_colocated_schema_version_map_[stream_entry.first][colocated_id];
        schema_version_map[versions.current_producer_schema_version()] =
            versions.current_consumer_schema_version();

        // Update the old producer schema version, only if it is not the same as
        // current producer schema version - handles the case where versions are 0.
        if (versions.old_producer_schema_version() != versions.current_producer_schema_version()) {
          DCHECK(versions.old_producer_schema_version() <
              versions.current_producer_schema_version());
          schema_version_map[versions.old_producer_schema_version()] =
              versions.old_consumer_schema_version();
        }

        auto min_schema_version = versions.old_consumer_schema_version() > 0 ?
            versions.old_consumer_schema_version() : versions.current_consumer_schema_version();
        min_schema_version_map_[std::make_pair(
            stream_entry_pb.consumer_table_id(), colocated_id)] = min_schema_version;
      }

      for (const auto& tablet_entry : stream_entry_pb.consumer_producer_tablet_map()) {
        const auto& consumer_tablet_id = tablet_entry.first;
        for (const auto& producer_tablet_id : tablet_entry.second.tablets()) {
          ProducerTabletInfo producer_tablet_info(
              {producer_map.first, stream_entry.first, producer_tablet_id});
          cdc::ConsumerTabletInfo consumer_tablet_info(
              {consumer_tablet_id, stream_entry_pb.consumer_table_id()});
          auto xCluster_tablet_info = cdc::XClusterTabletInfo{
              .producer_tablet_info = producer_tablet_info,
              .consumer_tablet_info = consumer_tablet_info,
              .disable_stream = producer_entry_pb.disable_stream()};
          producer_consumer_tablet_map_from_master_.emplace(xCluster_tablet_info);
        }
      }
    }
  }
  enable_replicate_transaction_status_table_ =
      consumer_registry->enable_replicate_transaction_status_table();
  cond_.notify_all();
}

Result<cdc::ConsumerTabletInfo> XClusterConsumer::GetConsumerTableInfo(
    const TabletId& producer_tablet_id) {
  SharedLock<rw_spinlock> lock(master_data_mutex_);
  const auto& index_by_tablet = producer_consumer_tablet_map_from_master_.get<TabletTag>();
  auto count = index_by_tablet.count(producer_tablet_id);
  SCHECK(
      count, NotFound,
      Format("No consumer tablets found for producer tablet $0.", producer_tablet_id));

  if (count != 1) {
    return STATUS(
        IllegalState, Format(
                          "For producer tablet $0, found $1 consumer tablets when exactly 1 "
                          "expected for transactional workloads.",
                          producer_tablet_id, count));
  }
  auto it = index_by_tablet.find(producer_tablet_id);
  return it->consumer_tablet_info;
}

SchemaVersion XClusterConsumer::GetMinXClusterSchemaVersion(const TableId& table_id,
    const ColocationId& colocation_id) {
  SharedLock read_lock_master(master_data_mutex_);
  auto iter = min_schema_version_map_.find(make_pair(table_id, colocation_id));
  if (iter != min_schema_version_map_.end()) {
    VLOG_WITH_FUNC(4) << Format("XCluster min schema version for $0,$1:$2",
        table_id, colocation_id, iter->second);
    return iter->second;
  }

  VLOG_WITH_FUNC(4) << Format("XCluster tableid,colocationid pair not found in registry: $0,$1",
      table_id, colocation_id);
  return cdc::kInvalidSchemaVersion;
}

void XClusterConsumer::TriggerPollForNewTablets() {
  SharedLock read_lock_master(master_data_mutex_);
  int32_t current_cluster_config_version = cluster_config_version();

  for (const auto& entry : producer_consumer_tablet_map_from_master_) {
    if (entry.disable_stream) {
      // Replication for this stream has been paused/disabled, do not create a poller for this
      // tablet.
      continue;
    }
    const auto& producer_tablet_info = entry.producer_tablet_info;
    const auto& consumer_tablet_info = entry.consumer_tablet_info;
    auto uuid = producer_tablet_info.universe_uuid;
    bool start_polling;
    {
      SharedLock<rw_spinlock> read_lock_pollers(producer_pollers_map_mutex_);
      start_polling =
          producer_pollers_map_.find(producer_tablet_info) == producer_pollers_map_.end() &&
          is_leader_for_tablet_(entry.consumer_tablet_info.tablet_id);

      // Update the Master Addresses, if altered after setup.
      if (ContainsKey(remote_clients_, uuid) && changed_master_addrs_.count(uuid) > 0) {
        auto status = remote_clients_[uuid]->client->SetMasterAddresses(uuid_master_addrs_[uuid]);
        if (status.ok()) {
          changed_master_addrs_.erase(uuid);
        } else {
          LOG_WITH_PREFIX(WARNING)
              << "Problem Setting Master Addresses for " << uuid << ": " << status.ToString();
        }
      }
    }
    if (start_polling) {
      std::lock_guard<rw_spinlock> write_lock_pollers(producer_pollers_map_mutex_);

      // Check again, since we unlocked.
      start_polling =
          producer_pollers_map_.find(producer_tablet_info) == producer_pollers_map_.end() &&
          is_leader_for_tablet_(consumer_tablet_info.tablet_id);
      if (start_polling) {
        // This is a new tablet, trigger a poll.
        // See if we need to create a new client connection
        if (!ContainsKey(remote_clients_, uuid)) {
          CHECK(ContainsKey(uuid_master_addrs_, uuid));

          auto remote_client = std::make_unique<XClusterClient>();
          std::string dir;
          if (FLAGS_use_node_to_node_encryption) {
            rpc::MessengerBuilder messenger_builder("cdc-consumer");
            if (!FLAGS_certs_for_cdc_dir.empty()) {
              dir = JoinPathSegments(
                  FLAGS_certs_for_cdc_dir, cdc::GetOriginalReplicationUniverseId(uuid));
            }

            auto secure_context_result = server::SetupSecureContext(
                dir, "", "", server::SecureContextType::kInternal, &messenger_builder);
            if (!secure_context_result.ok()) {
              LOG(WARNING) << "Could not create secure context for " << uuid << ": "
                           << secure_context_result.status().ToString();
              return;  // Don't finish creation.  Try again on the next heartbeat.
            }
            remote_client->secure_context = std::move(*secure_context_result);

            auto messenger_result = messenger_builder.Build();
            if (!messenger_result.ok()) {
              LOG(WARNING) << "Could not build messenger for " << uuid << ": "
                           << secure_context_result.status().ToString();
              return;  // Don't finish creation.  Try again on the next heartbeat.
            }
            remote_client->messenger = std::move(*messenger_result);
          }

          auto client_result =
              yb::client::YBClientBuilder()
                  .set_client_name("CDCConsumerRemote")
                  .add_master_server_addr(uuid_master_addrs_[uuid])
                  .skip_master_flagfile()
                  .default_rpc_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms))
                  .Build(remote_client->messenger.get());
          if (!client_result.ok()) {
            LOG(WARNING) << "Could not create a new YBClient for " << uuid << ": "
                         << client_result.status().ToString();
            return;  // Don't finish creation.  Try again on the next heartbeat.
          }

          remote_client->client = std::move(*client_result);
          remote_clients_[uuid] = std::move(remote_client);
        }

        SchemaVersion last_compatible_consumer_schema_version = cdc::kInvalidSchemaVersion;
        auto schema_version_iter = stream_to_schema_version_.find(producer_tablet_info.stream_id);
        if (schema_version_iter != stream_to_schema_version_.end()) {
          last_compatible_consumer_schema_version = schema_version_iter->second.second;
        }

        // now create the poller
        bool use_local_tserver =
            streams_with_local_tserver_optimization_.find(producer_tablet_info.stream_id) !=
            streams_with_local_tserver_optimization_.end();
        auto xcluster_poller = std::make_shared<XClusterPoller>(
            producer_tablet_info, consumer_tablet_info, thread_pool_.get(), rpcs_.get(),
            local_client_, remote_clients_[uuid], this, use_local_tserver,
            global_transaction_status_tablets_, enable_replicate_transaction_status_table_,
            last_compatible_consumer_schema_version, rate_limiter_.get(), get_leader_term_);

        UpdatePollerSchemaVersionMaps(xcluster_poller, producer_tablet_info.stream_id);

        LOG_WITH_PREFIX(INFO) << Format(
            "Start polling for producer tablet $0, consumer tablet $1", producer_tablet_info,
            consumer_tablet_info.tablet_id);
        producer_pollers_map_[producer_tablet_info] = xcluster_poller;
        xcluster_poller->Poll();
      }
    }

    // Notify existing pollers only if there was a cluster config refresh since last time.
    if (current_cluster_config_version > last_polled_at_cluster_config_version_) {
      SharedLock<rw_spinlock> read_lock_pollers(producer_pollers_map_mutex_);
      auto xcluster_poller_iter = producer_pollers_map_.find(producer_tablet_info);
      if (xcluster_poller_iter != producer_pollers_map_.end()) {
        UpdatePollerSchemaVersionMaps(xcluster_poller_iter->second, producer_tablet_info.stream_id);
      }
    }
  }

  last_polled_at_cluster_config_version_ = current_cluster_config_version;
}

void XClusterConsumer::UpdatePollerSchemaVersionMaps(
    std::shared_ptr<XClusterPoller> xcluster_poller, const CDCStreamId& stream_id) const {

  auto compatible_schema_version = FindOrNull(stream_to_schema_version_, stream_id);
  if (compatible_schema_version != nullptr) {
      xcluster_poller->SetSchemaVersion(compatible_schema_version->first,
                                        compatible_schema_version->second);
  }

  auto schema_versions = FindOrNull(stream_schema_version_map_, stream_id);
  if (schema_versions != nullptr) {
    xcluster_poller->UpdateSchemaVersions(*schema_versions);
  }

  auto colocated_schema_versions = FindOrNull(stream_colocated_schema_version_map_, stream_id);
  if (colocated_schema_versions != nullptr) {
    xcluster_poller->UpdateColocatedSchemaVersionMap(*colocated_schema_versions);
  }
}

void XClusterConsumer::TriggerDeletionOfOldPollers() {
  // Shutdown outside of master_data_mutex_ lock, to not block any heartbeats.
  std::vector<std::shared_ptr<XClusterClient>> clients_to_delete;
  std::vector<std::shared_ptr<XClusterPoller>> pollers_to_shutdown;
  {
    SharedLock<rw_spinlock> read_lock_master(master_data_mutex_);
    std::lock_guard<rw_spinlock> write_lock_pollers(producer_pollers_map_mutex_);
    for (auto it = producer_pollers_map_.cbegin(); it != producer_pollers_map_.cend();) {
      const ProducerTabletInfo producer_info = it->first;
      const std::shared_ptr<XClusterPoller> poller = it->second;
      // Check if we need to delete this poller.
      if (ShouldContinuePolling(producer_info, *poller)) {
        ++it;
        continue;
      }

      const cdc::ConsumerTabletInfo& consumer_info = poller->GetConsumerTabletInfo();

      LOG_WITH_PREFIX(INFO) << Format(
          "Stop polling for producer tablet $0, consumer tablet $1", producer_info,
          consumer_info.tablet_id);
      pollers_to_shutdown.emplace_back(poller);
      it = producer_pollers_map_.erase(it);

      // Check if no more objects with this UUID exist after registry refresh.
      if (!ContainsKey(uuid_master_addrs_, producer_info.universe_uuid)) {
        auto clients_it = remote_clients_.find(producer_info.universe_uuid);
        if (clients_it != remote_clients_.end()) {
          clients_to_delete.emplace_back(clients_it->second);
          remote_clients_.erase(clients_it);
        }
      }
    }
  }
  for (const auto& poller : pollers_to_shutdown) {
    poller->Shutdown();
  }
  for (const auto& client : clients_to_delete) {
    client->Shutdown();
  }
}

bool XClusterConsumer::ShouldContinuePolling(
    const ProducerTabletInfo producer_tablet_info, const XClusterPoller& poller) {
  if (FLAGS_TEST_xcluster_disable_delete_old_pollers) {
    return true;
  }

  if (poller.IsFailed()) {
    // All failed pollers need to be deleted. If the tablet leader is still on this node they will
    // be recreated.
    return false;
  }

  const auto& consumer_tablet_info = poller.GetConsumerTabletInfo();
  const auto& it = producer_consumer_tablet_map_from_master_.find(producer_tablet_info);
  // We either no longer need to poll for this tablet, or a different tablet should be polling
  // for it now instead of this one (due to a local tablet split).
  if (it == producer_consumer_tablet_map_from_master_.end() ||
      it->consumer_tablet_info.tablet_id != consumer_tablet_info.tablet_id || it->disable_stream) {
    // We no longer want to poll for this tablet, abort the cycle.
    return false;
  }
  return is_leader_for_tablet_(it->consumer_tablet_info.tablet_id);
}

std::string XClusterConsumer::LogPrefix() { return log_prefix_; }

int32_t XClusterConsumer::cluster_config_version() const {
  return cluster_config_version_.load(std::memory_order_acquire);
}

client::TransactionManager* XClusterConsumer::TransactionManager() { return transaction_manager_; }

Status XClusterConsumer::ReloadCertificates() {
  if (local_client_->secure_context) {
    RETURN_NOT_OK(server::ReloadSecureContextKeysAndCertificates(
        local_client_->secure_context.get(), "" /* node_name */, "" /* root_dir*/,
        server::SecureContextType::kInternal));
  }

  SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);
  for (const auto& entry : remote_clients_) {
    const auto& client = entry.second;
    if (!client->secure_context) {
      continue;
    }

    std::string cert_dir;
    if (!FLAGS_certs_for_cdc_dir.empty()) {
      cert_dir = JoinPathSegments(
          FLAGS_certs_for_cdc_dir, cdc::GetOriginalReplicationUniverseId(entry.first));
    }
    RETURN_NOT_OK(server::ReloadSecureContextKeysAndCertificates(
        client->secure_context.get(), cert_dir, "" /* node_name */));
  }

  return Status::OK();
}

Status XClusterConsumer::PublishXClusterSafeTime() {
  if (consumer_role_ == cdc::XClusterRole::ACTIVE) {
    return Status::OK();
  }

  const client::YBTableName safe_time_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kXClusterSafeTimeTableName);

  std::lock_guard<std::mutex> l(safe_time_update_mutex_);

  int wait_time = GetAtomicFlag(&FLAGS_xcluster_safe_time_update_interval_secs);
  if (wait_time <= 0 || MonoTime::Now() - last_safe_time_published_at_ < wait_time * 1s) {
    return Status::OK();
  }

  auto& client = local_client_->client;

  if (!xcluster_safe_time_table_ready_) {
    // Master has not created the table yet. Nothing to do for now.
    if (!VERIFY_RESULT(client->TableExists(safe_time_table_name))) {
      return Status::OK();
    }
    RETURN_NOT_OK(client->WaitForCreateTableToFinish(safe_time_table_name));

    xcluster_safe_time_table_ready_ = true;
  }

  if (!safe_time_table_) {
    auto table = std::make_unique<client::TableHandle>();
    RETURN_NOT_OK(table->Open(safe_time_table_name, client.get()));
    safe_time_table_.swap(table);
  }

  std::unordered_map<ProducerTabletInfo, HybridTime, ProducerTabletInfo::Hash> safe_time_map;

  {
    SharedLock<rw_spinlock> read_lock(producer_pollers_map_mutex_);
    for (auto& poller : producer_pollers_map_) {
      safe_time_map[poller.first] = poller.second->GetSafeTime();
    }
  }

  std::shared_ptr<client::YBSession> session = client->NewSession();
  session->SetTimeout(client->default_rpc_timeout());
  for (auto& safe_time_info : safe_time_map) {
    const auto op = safe_time_table_->NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    auto* const req = op->mutable_request();
    QLAddStringHashValue(req, safe_time_info.first.universe_uuid);
    QLAddStringHashValue(req, safe_time_info.first.tablet_id);
    safe_time_table_->AddInt64ColumnValue(
        req, master::kXCSafeTime, safe_time_info.second.ToUint64());

    VLOG_WITH_FUNC(2) << "UniverseID: " << safe_time_info.first.universe_uuid
                      << ", TabletId: " << safe_time_info.first.tablet_id
                      << ", SafeTime: " << safe_time_info.second.ToDebugString();
    session->Apply(op);
  }

  auto future = session->FlushFuture();
  auto future_status = future.wait_for(client->default_rpc_timeout().ToChronoMilliseconds());
  SCHECK(
      future_status == std::future_status::ready, IOError,
      "Timed out waiting for flush to XClusterSafeTime table");
  RETURN_NOT_OK_PREPEND(future.get().status, "Failed to flush to XClusterSafeTime table");

  last_safe_time_published_at_ = MonoTime::Now();

  return Status::OK();
}

void XClusterConsumer::StoreReplicationError(
    const TabletId& tablet_id,
    const CDCStreamId& stream_id,
    const ReplicationErrorPb error,
    const std::string& detail) {
  std::lock_guard<simple_spinlock> lock(tablet_replication_error_map_lock_);
  tablet_replication_error_map_[tablet_id][stream_id][error] = detail;
}

void XClusterConsumer::ClearReplicationError(
    const TabletId& tablet_id,
    const CDCStreamId& stream_id) {
  std::lock_guard l(tablet_replication_error_map_lock_);
  if (!tablet_replication_error_map_.contains(tablet_id)) {
    return;
  }

  tablet_replication_error_map_[tablet_id].erase(stream_id);
  tablet_replication_error_map_.erase(tablet_id);
}

cdc::TabletReplicationErrorMap XClusterConsumer::GetReplicationErrors() const {
  std::lock_guard<simple_spinlock> lock(tablet_replication_error_map_lock_);
  return tablet_replication_error_map_;
}

}  // namespace tserver
}  // namespace yb
