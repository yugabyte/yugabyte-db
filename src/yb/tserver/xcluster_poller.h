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

#include <string>

#include "yb/cdc/cdc_types.h"
#include "yb/tserver/xcluster_async_executor.h"
#include "yb/tserver/xcluster_ddl_queue_handler.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/common/hybrid_time.h"
#include "yb/tserver/xcluster_poller_id.h"
#include "yb/tserver/xcluster_poller_stats.h"
#include "yb/util/locks.h"
#include "yb/util/status_fwd.h"

#pragma once

namespace yb {

class ThreadPool;

namespace rpc {

class RpcController;

} // namespace rpc

namespace cdc {

class CDCServiceProxy;

} // namespace cdc

namespace client {
class XClusterRemoteClientHolder;
}  // namespace client

namespace tserver {

class AutoFlagsCompatibleVersion;
class XClusterConsumer;
class XClusterDDLQueueHandler;

class XClusterPoller : public XClusterAsyncExecutor {
 public:
  XClusterPoller(
      const xcluster::ProducerTabletInfo& producer_tablet_info,
      const xcluster::ConsumerTabletInfo& consumer_tablet_info,
      const NamespaceId& consumer_namespace_id,
      std::shared_ptr<const AutoFlagsCompatibleVersion> auto_flags_version, ThreadPool* thread_pool,
      rpc::Rpcs* rpcs, client::YBClient& local_client,
      const std::shared_ptr<client::XClusterRemoteClientHolder>& source_client,
      XClusterConsumer* xcluster_consumer, SchemaVersion last_compatible_consumer_schema_version,
      int64_t leader_term, std::function<int64_t(const TabletId&)> get_leader_term);
  ~XClusterPoller();

  void Init(bool use_local_tserver, rocksdb::RateLimiter* rate_limiter);
  void InitDDLQueuePoller(
      bool use_local_tserver, rocksdb::RateLimiter* rate_limiter,
      const NamespaceName& namespace_name, TserverXClusterContextIf& xcluster_context,
      ConnectToPostgresFunc connect_to_pg_func);

  void StartShutdown() override;
  void CompleteShutdown() override;

  bool ShouldContinuePolling() const;

  // Begins poll process for a producer tablet.
  void SchedulePoll();

  void ScheduleSetSchemaVersionIfNeeded(
      SchemaVersion cur_version, SchemaVersion last_compatible_consumer_schema_version);

  void UpdateSchemaVersions(const cdc::XClusterSchemaVersionMap& schema_versions)
      EXCLUDES(schema_version_lock_);

  void UpdateColocatedSchemaVersionMap(
      const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map)
      EXCLUDES(schema_version_lock_);

  std::string LogPrefix() const override;
  const XClusterPollerId& GetPollerId() const { return poller_id_; }

  HybridTime GetSafeTime() const EXCLUDES(safe_time_lock_);

  const xcluster::ConsumerTabletInfo& GetConsumerTabletInfo() const {
    return consumer_tablet_info_;
  }
  const NamespaceId& GetConsumerNamespaceId() const { return consumer_namespace_id_; }
  const xcluster::ProducerTabletInfo& GetProducerTabletInfo() const {
    return producer_tablet_info_;
  }

  bool IsStuck() const;
  std::string State() const;

  XClusterPollerStats GetStats() const;

  int64_t GetLeaderTerm() const { return poller_id_.leader_term; }

  void MarkFailed(const std::string& reason, const Status& status = Status::OK()) override;
  // Stores a replication error and detail. This overwrites a previously stored 'error'.
  void StoreReplicationError(ReplicationErrorPb error) EXCLUDES(replication_error_mutex_);
  // Stores a non OK replication error if one has not already been set.
  void StoreNOKReplicationError() EXCLUDES(replication_error_mutex_);
  void ClearReplicationError() EXCLUDES(replication_error_mutex_);
  void TEST_IncrementNumSuccessfulWriteRpcs();
  void ApplyChangesCallback(XClusterOutputClientResponse&& response);

 private:
  const xcluster::ReplicationGroupId& GetReplicationGroupId() const {
    return producer_tablet_info_.replication_group_id;
  }

  bool IsOffline() override;

  void DoSetSchemaVersion(SchemaVersion cur_version, SchemaVersion current_consumer_schema_version)
      EXCLUDES(data_mutex_);

  void DoPoll() EXCLUDES(data_mutex_);

  void HandleGetChangesResponse(Status status, std::shared_ptr<cdc::GetChangesResponsePB> resp)
      EXCLUDES(data_mutex_);
  void ScheduleApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response);
  void ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> get_changes_response)
      EXCLUDES(data_mutex_);
  void VerifyApplyChangesResponse(XClusterOutputClientResponse response);
  void HandleApplyChangesResponse(XClusterOutputClientResponse response) EXCLUDES(data_mutex_);
  void UpdateSafeTime(int64 new_time) EXCLUDES(safe_time_lock_);
  void UpdateSchemaVersionsForApply() EXCLUDES(schema_version_lock_);
  bool IsLeaderTermValid() REQUIRES(data_mutex_);
  Status ProcessGetChangesResponseError(const cdc::GetChangesResponsePB& resp);

  const xcluster::ProducerTabletInfo producer_tablet_info_;
  const xcluster::ConsumerTabletInfo consumer_tablet_info_;
  const NamespaceId consumer_namespace_id_;
  const XClusterPollerId poller_id_;
  const std::shared_ptr<const AutoFlagsCompatibleVersion> auto_flags_version_;

  mutable rw_spinlock schema_version_lock_;
  cdc::XClusterSchemaVersionMap schema_version_map_ GUARDED_BY(schema_version_lock_);
  cdc::ColocatedSchemaVersionMap colocated_schema_version_map_ GUARDED_BY(schema_version_lock_);

  mutable std::mutex data_mutex_;

  std::atomic<bool> shutdown_ = false;
  // In failed state we do not poll for changes and are awaiting shutdown.
  std::atomic<bool> is_failed_ = false;
  std::mutex shutdown_mutex_;
  std::condition_variable shutdown_cv_;

  OpIdPB op_id_ GUARDED_BY(data_mutex_);
  std::atomic<SchemaVersion> validated_schema_version_;
  std::atomic<SchemaVersion> last_compatible_consumer_schema_version_;
  std::function<int64_t(const TabletId&)> get_leader_term_;

  client::YBClient& local_client_;
  std::shared_ptr<XClusterOutputClient> output_client_;
  std::shared_ptr<client::XClusterRemoteClientHolder> source_client_;
  std::shared_ptr<XClusterDDLQueueHandler> ddl_queue_handler_;

  // Unsafe to use after shutdown.
  XClusterConsumer* const xcluster_consumer_;

  mutable rw_spinlock safe_time_lock_;
  HybridTime producer_safe_time_ GUARDED_BY(safe_time_lock_);

  std::atomic<bool> is_polling_ = true;
  std::atomic<uint32> poll_failures_ = 0;
  std::atomic<uint32> apply_failures_ = 0;
  std::atomic<uint32> idle_polls_ = 0;

  // Replication errors that are tracked and reported to the master.
  std::mutex replication_error_mutex_;
  ReplicationErrorPb previous_replication_error_ GUARDED_BY(replication_error_mutex_) =
      ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED;

  PollStatsHistory poll_stats_history_;
};

} // namespace tserver
} // namespace yb
