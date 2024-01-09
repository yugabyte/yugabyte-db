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

#pragma once

#include <condition_variable>
#include <unordered_map>
#include <unordered_set>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/cdc_util.h"
#include "yb/client/client_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/tablet/tablet_types.pb.h"
#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/tserver/xcluster_poller_stats.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"

namespace rocksdb {

class RateLimiter;

}

namespace yb {

class Thread;
class ThreadPool;

namespace rpc {

class Messenger;
class ProxyCache;
class Rpcs;
class SecureContext;

} // namespace rpc

namespace cdc {

class ConsumerRegistryPB;

} // namespace cdc

namespace tserver {
class XClusterPoller;
class TabletServer;

struct XClusterClient {
  std::unique_ptr<rpc::Messenger> messenger;
  std::unique_ptr<rpc::SecureContext> secure_context;
  std::shared_ptr<client::YBClient> client;

  ~XClusterClient();
  void Shutdown();
};

class XClusterConsumer {
 public:
  static Result<std::unique_ptr<XClusterConsumer>> Create(
      std::function<bool(const std::string&)> is_leader_for_tablet,
      std::function<int64_t(const TabletId&)> get_leader_term, rpc::ProxyCache* proxy_cache,
      TabletServer* tserver);

  XClusterConsumer(
      std::function<bool(const std::string&)> is_leader_for_tablet,
      std::function<int64_t(const TabletId&)> get_leader_term, rpc::ProxyCache* proxy_cache,
      const std::string& ts_uuid, std::unique_ptr<XClusterClient> local_client);

  ~XClusterConsumer();
  void Shutdown() EXCLUDES(shutdown_mutex_);

  // Refreshes the in memory state when we receive a new registry from master.
  void RefreshWithNewRegistryFromMaster(const cdc::ConsumerRegistryPB* consumer_registry,
                                        int32_t cluster_config_version);

  std::vector<TabletId> TEST_producer_tablets_running();

  std::vector<std::shared_ptr<XClusterPoller>> TEST_ListPollers();

  std::vector<XClusterPollerStats> GetPollerStats() const;

  std::string LogPrefix();

  // Return the value stored in cluster_config_version_. Since we are reading an atomic variable,
  // we don't need to hold the mutex.
  int32_t cluster_config_version() const NO_THREAD_SAFETY_ANALYSIS;

  void TEST_IncrementNumSuccessfulWriteRpcs() { TEST_num_successful_write_rpcs_++; }

  uint32_t TEST_GetNumSuccessfulWriteRpcs() {
    return TEST_num_successful_write_rpcs_.load(std::memory_order_acquire);
  }

  Status ReloadCertificates();

  Status PublishXClusterSafeTime();

  // Stores a replication error and detail. This overwrites a previously stored 'error'.
  void StoreReplicationError(
      const TabletId& tablet_id, const xrepl::StreamId& stream_id, ReplicationErrorPb error,
      const std::string& detail);

  // Clears replication errors.
  void ClearReplicationError(const TabletId& tablet_id, const xrepl::StreamId& stream_id);

  // Returns the replication error map.
  cdc::TabletReplicationErrorMap GetReplicationErrors() const;

  cdc::XClusterRole TEST_GetXClusterRole() const { return consumer_role_; }

 private:
  // Runs a thread that periodically polls for any new threads.
  void RunThread() EXCLUDES(shutdown_mutex_);

  // Loops through all the entries in the registry and creates a producer -> consumer tablet
  // mapping.
  void UpdateInMemoryState(const cdc::ConsumerRegistryPB* consumer_producer_map,
                           int32_t cluster_config_version);

  // Loops through all entries in registry from master to check if all producer tablets are being
  // polled for.
  void TriggerPollForNewTablets() EXCLUDES (master_data_mutex_);

  // Loop through pollers and check if they should still be polling, if not, shut them down.
  void TriggerDeletionOfOldPollers() EXCLUDES (master_data_mutex_);

  bool ShouldContinuePolling(
      const cdc::ProducerTabletInfo producer_tablet_info,
      const XClusterPoller& poller) REQUIRES_SHARED(master_data_mutex_);

  void UpdatePollerSchemaVersionMaps(
      std::shared_ptr<XClusterPoller> xcluster_poller, const xrepl::StreamId& stream_id) const
      REQUIRES_SHARED(master_data_mutex_);

  void RemoveFromPollersMap(const cdc::ProducerTabletInfo producer_tablet_info);

  void SetRateLimiterSpeed();

  // Mutex and cond for should_run_ state.
  std::mutex shutdown_mutex_;
  std::condition_variable run_thread_cond_;
  std::atomic_bool is_shutdown_ = false;

  // Mutex for producer_consumer_tablet_map_from_master_.
  rw_spinlock master_data_mutex_;

  // Mutex for producer_pollers_map_.
  mutable rw_spinlock pollers_map_mutex_ ACQUIRED_AFTER(master_data_mutex_);

  std::function<bool(const std::string&)> is_leader_for_tablet_;
  std::function<int64_t(const TabletId&)> get_leader_term_;

  class TabletTag;
  using ProducerConsumerTabletMap = boost::multi_index_container <
    cdc::XClusterTabletInfo,
    boost::multi_index::indexed_by <
      boost::multi_index::hashed_unique <
          boost::multi_index::member <
              cdc::XClusterTabletInfo, cdc::ProducerTabletInfo,
              &cdc::XClusterTabletInfo::producer_tablet_info>
      >,
      boost::multi_index::hashed_non_unique <
          boost::multi_index::tag <TabletTag>,
          boost::multi_index::const_mem_fun <
              cdc::XClusterTabletInfo, const TabletId&,
              &cdc::XClusterTabletInfo::producer_tablet_id
          >
      >
    >
  >;

  ProducerConsumerTabletMap producer_consumer_tablet_map_from_master_
      GUARDED_BY(master_data_mutex_);

  std::unordered_set<xrepl::StreamId> streams_with_local_tserver_optimization_
      GUARDED_BY(master_data_mutex_);

  // Pair of validated_schema_version and last_compatible_consumer_schema_version.
  using SchemaVersionMapping = std::pair<uint32_t, uint32_t>;
  std::unordered_map<xrepl::StreamId, SchemaVersionMapping> stream_to_schema_version_
      GUARDED_BY(master_data_mutex_);

  cdc::StreamSchemaVersionMap stream_schema_version_map_ GUARDED_BY(master_data_mutex_);

  cdc::StreamColocatedSchemaVersionMap stream_colocated_schema_version_map_
      GUARDED_BY(master_data_mutex_);

  scoped_refptr<Thread> run_trigger_poll_thread_;

  std::unordered_map<
      cdc::ProducerTabletInfo, std::shared_ptr<XClusterPoller>, cdc::ProducerTabletInfo::Hash>
      pollers_map_ GUARDED_BY(pollers_map_mutex_);

  std::unique_ptr<ThreadPool> thread_pool_;
  std::unique_ptr<rpc::Rpcs> rpcs_;

  std::string log_prefix_;
  std::shared_ptr<XClusterClient> local_client_;

  // map: {replication_group_id : ...}.
  std::unordered_map<cdc::ReplicationGroupId, std::shared_ptr<XClusterClient>> remote_clients_
      GUARDED_BY(pollers_map_mutex_);
  std::unordered_map<cdc::ReplicationGroupId, std::string> uuid_master_addrs_
      GUARDED_BY(master_data_mutex_);
  std::unordered_set<cdc::ReplicationGroupId> changed_master_addrs_ GUARDED_BY(master_data_mutex_);

  std::atomic<int32_t> cluster_config_version_ GUARDED_BY(master_data_mutex_) = {-1};
  std::atomic<cdc::XClusterRole> consumer_role_ = cdc::XClusterRole::ACTIVE;

  // This is the cached cluster config version on which the pollers
  // were notified of any changes
  std::atomic<int32_t> last_polled_at_cluster_config_version_ = {-1};

  std::atomic<uint32_t> TEST_num_successful_write_rpcs_{0};

  std::mutex safe_time_update_mutex_;
  MonoTime last_safe_time_published_at_ GUARDED_BY(safe_time_update_mutex_);

  bool xcluster_safe_time_table_ready_ GUARDED_BY(safe_time_update_mutex_) = false;
  std::unique_ptr<client::TableHandle> safe_time_table_ GUARDED_BY(safe_time_update_mutex_);

  mutable simple_spinlock tablet_replication_error_map_lock_;
  cdc::TabletReplicationErrorMap tablet_replication_error_map_
      GUARDED_BY(tablet_replication_error_map_lock_);

  std::unique_ptr<rocksdb::RateLimiter> rate_limiter_;
  FlagCallbackRegistration rate_limiter_callback_;
};

} // namespace tserver
} // namespace yb
