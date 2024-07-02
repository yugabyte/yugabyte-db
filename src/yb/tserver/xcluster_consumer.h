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

#include <boost/functional/hash.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/client/client_fwd.h"
#include "yb/common/common_types.pb.h"

#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_consumer_replication_error.h"
#include "yb/tserver/xcluster_poller_stats.h"

#include "yb/gutil/ref_counted.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"

namespace rocksdb {
class RateLimiter;
}  // namespace rocksdb

namespace yb {
class HostPort;
class Thread;
class ThreadPool;

namespace rpc {
class Messenger;
class Rpcs;
}  // namespace rpc

namespace client {
class XClusterRemoteClientHolder;
}  // namespace client

namespace tserver {
class AutoFlagsVersionHandler;
class XClusterPoller;
class TabletServer;
class TserverXClusterContextIf;

class XClusterConsumer : public XClusterConsumerIf {
 public:
  XClusterConsumer(
      std::function<int64_t(const TabletId&)> get_leader_term, const std::string& ts_uuid,
      client::YBClient& local_client, ConnectToPostgresFunc connect_to_pg_func,
      GetNamespaceInfoFunc get_namespace_info_func,
      TserverXClusterContextIf& xcluster_context,
      const scoped_refptr<MetricEntity>& server_metric_entity);

  ~XClusterConsumer();
  Status Init();
  void Shutdown() override EXCLUDES(shutdown_mutex_);

  // Refreshes the in memory state when we receive a new registry from master.
  void HandleMasterHeartbeatResponse(
      const cdc::ConsumerRegistryPB* consumer_registry, int32_t cluster_config_version) override;

  std::vector<TabletId> TEST_producer_tablets_running() const override;

  std::vector<std::shared_ptr<XClusterPoller>> TEST_ListPollers() const override;

  std::vector<XClusterPollerStats> GetPollerStats() const override;

  std::string LogPrefix();

  // Return the value stored in cluster_config_version_. Since we are reading an atomic variable,
  // we don't need to hold the mutex.
  int32_t cluster_config_version() const override NO_THREAD_SAFETY_ANALYSIS;

  void TEST_IncrementNumSuccessfulWriteRpcs() { TEST_num_successful_write_rpcs_++; }

  uint32_t TEST_GetNumSuccessfulWriteRpcs() override {
    return TEST_num_successful_write_rpcs_.load(std::memory_order_acquire);
  }

  void WriteServerMetaCacheAsJson(JsonWriter& writer) const override;

  Status ReloadCertificates() override;

  Status PublishXClusterSafeTime();

  SchemaVersion GetMinXClusterSchemaVersion(
      const TableId& table_id, const ColocationId& colocation_id) override;

  void PopulateMasterHeartbeatRequest(
      master::TSHeartbeatRequestPB* req, bool needs_full_tablet_report) override;

  void StoreReplicationError(const XClusterPollerId& poller_id, ReplicationErrorPb error);

  Status ReportNewAutoFlagConfigVersion(
      const xcluster::ReplicationGroupId& replication_group_id, uint32_t new_version) const;

  void ClearAllClientMetaCaches() const override;

  void IncrementApplyFailureCount() { metric_apply_failure_count_->Increment(); }

  void IncrementPollFailureCount() { metric_poll_failure_count_->Increment(); }

  scoped_refptr<Counter> TEST_metric_replication_error_count() const override {
    return metric_replication_error_count_;
  }

  scoped_refptr<Counter> TEST_metric_apply_failure_count() const override {
    return metric_apply_failure_count_;
  }

  scoped_refptr<Counter> TEST_metric_poll_failure_count() const override {
    return metric_poll_failure_count_;
  }
 private:
  // Runs a thread that periodically polls for any new threads.
  void RunThread() EXCLUDES(shutdown_mutex_);

  // Loops through all the entries in the registry and creates a producer -> consumer tablet
  // mapping.
  void UpdateReplicationGroupInMemState(
      const xcluster::ReplicationGroupId& replication_group_id,
      const yb::cdc::ProducerEntryPB& producer_entry_pb) REQUIRES(master_data_mutex_);

  // Loops through all entries in registry from master to check if all producer tablets are being
  // polled for.
  void TriggerPollForNewTablets() EXCLUDES (master_data_mutex_);

  // Loop through pollers and check if they should still be polling, if not, shut them down.
  void TriggerDeletionOfOldPollers() EXCLUDES (master_data_mutex_);

  bool ShouldContinuePolling(
      const xcluster::ProducerTabletInfo producer_tablet_info, const XClusterPoller& poller,
      std::string& reason) REQUIRES_SHARED(master_data_mutex_);

  void UpdatePollerSchemaVersionMaps(
      std::shared_ptr<XClusterPoller> xcluster_poller, const xrepl::StreamId& stream_id) const
      REQUIRES_SHARED(master_data_mutex_);

  void RemoveFromPollersMap(const xcluster::ProducerTabletInfo producer_tablet_info);

  void SetRateLimiterSpeed();

  // Mutex and cond for should_run_ state.
  std::mutex shutdown_mutex_;
  std::condition_variable run_thread_cond_;
  std::atomic_bool is_shutdown_ = false;

  // Mutex for producer_consumer_tablet_map_from_master_.
  rw_spinlock master_data_mutex_;

  // Mutex for producer_pollers_map_.
  mutable rw_spinlock pollers_map_mutex_ ACQUIRED_AFTER(master_data_mutex_);

  std::function<int64_t(const TabletId&)> get_leader_term_func_;

  class TabletTag;
  using ProducerConsumerTabletMap = boost::multi_index_container<
      xcluster::XClusterTabletInfo,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<boost::multi_index::member<
              xcluster::XClusterTabletInfo, xcluster::ProducerTabletInfo,
              &xcluster::XClusterTabletInfo::producer_tablet_info>>,
          boost::multi_index::hashed_non_unique<
              boost::multi_index::tag<TabletTag>,
              boost::multi_index::const_mem_fun<
                  xcluster::XClusterTabletInfo, const TabletId&,
                  &xcluster::XClusterTabletInfo::producer_tablet_id>>>>;

  ProducerConsumerTabletMap producer_consumer_tablet_map_from_master_
      GUARDED_BY(master_data_mutex_);

  std::unordered_set<xrepl::StreamId> streams_with_local_tserver_optimization_
      GUARDED_BY(master_data_mutex_);
  std::unordered_set<xrepl::StreamId> ddl_queue_streams_ GUARDED_BY(master_data_mutex_);

  // Pair of validated_schema_version and last_compatible_consumer_schema_version.
  using SchemaVersionMapping = std::pair<uint32_t, uint32_t>;
  std::unordered_map<xrepl::StreamId, SchemaVersionMapping> stream_to_schema_version_
      GUARDED_BY(master_data_mutex_);

  cdc::StreamSchemaVersionMap stream_schema_version_map_ GUARDED_BY(master_data_mutex_);

  cdc::StreamColocatedSchemaVersionMap stream_colocated_schema_version_map_
      GUARDED_BY(master_data_mutex_);

  // Map of tableId,colocationid -> minimum schema version on consumer.
  typedef std::pair<TableId, ColocationId> TableIdWithColocationId;
  std::unordered_map<TableIdWithColocationId, SchemaVersion, boost::hash<TableIdWithColocationId>>
      min_schema_version_map_ GUARDED_BY(master_data_mutex_);

  scoped_refptr<Thread> run_trigger_poll_thread_;

  std::unordered_map<
      xcluster::ProducerTabletInfo, std::shared_ptr<XClusterPoller>,
      xcluster::ProducerTabletInfo::Hash>
      pollers_map_ GUARDED_BY(pollers_map_mutex_);

  std::unique_ptr<ThreadPool> thread_pool_;
  std::unique_ptr<rpc::Rpcs> rpcs_;

  std::string log_prefix_;
  client::YBClient& local_client_;

  // map: {replication_group_id : ...}.
  std::unordered_map<
      xcluster::ReplicationGroupId, std::shared_ptr<client::XClusterRemoteClientHolder>>
      remote_clients_ GUARDED_BY(pollers_map_mutex_);
  std::unordered_map<xcluster::ReplicationGroupId, std::vector<HostPort>> uuid_master_addrs_
      GUARDED_BY(master_data_mutex_);
  std::unordered_set<xcluster::ReplicationGroupId> changed_master_addrs_
      GUARDED_BY(master_data_mutex_);

  std::atomic<int32_t> cluster_config_version_ GUARDED_BY(master_data_mutex_) = {-1};

  // This is the cached cluster config version on which the pollers
  // were notified of any changes
  std::atomic<int32_t> last_polled_at_cluster_config_version_ = {-1};

  std::atomic<uint32_t> TEST_num_successful_write_rpcs_{0};

  std::mutex safe_time_update_mutex_;
  MonoTime last_safe_time_published_at_ GUARDED_BY(safe_time_update_mutex_);

  bool xcluster_safe_time_table_ready_ GUARDED_BY(safe_time_update_mutex_) = false;
  std::unique_ptr<client::TableHandle> safe_time_table_ GUARDED_BY(safe_time_update_mutex_);

  XClusterConsumerReplicationErrorCollector error_collector_;

  std::unique_ptr<rocksdb::RateLimiter> rate_limiter_;
  FlagCallbackRegistration rate_limiter_callback_;

  std::unique_ptr<AutoFlagsVersionHandler> auto_flags_version_handler_;

  ConnectToPostgresFunc connect_to_pg_func_;

  GetNamespaceInfoFunc get_namespace_info_func_;

  TserverXClusterContextIf& xcluster_context_;

  scoped_refptr<Counter> metric_apply_failure_count_;

  scoped_refptr<Counter> metric_poll_failure_count_;

  scoped_refptr<Counter> metric_replication_error_count_;
};

} // namespace tserver
}  // namespace yb
