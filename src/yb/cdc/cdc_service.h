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

#pragma once

#include <memory>

#include "yb/cdc/xrepl_types.h"
#include "yb/cdc/xrepl_metrics.h"
#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_service.service.h"
#include "yb/cdc/cdc_types.h"
#include "yb/cdc/cdc_util.h"

#include "yb/master/master_client.fwd.h"

#include "yb/rocksdb/rate_limiter.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_context.h"

#include "yb/util/net/net_util.h"
#include "yb/util/semaphore.h"

#include <boost/optional.hpp>

namespace rocksdb {

class RateLimiter;

}
namespace yb {

class Thread;

namespace cdc {

class CDCServiceContext;
class CDCSDKVirtualWAL;
class CDCStateTableRange;

}  // namespace cdc

namespace client {

class TableHandle;

}

namespace xrepl {

struct StreamTabletStats;

}  // namespace xrepl

namespace cdc {

class CDCStateTable;
struct CDCStateTableEntry;

typedef std::unordered_map<HostPort, std::shared_ptr<CDCServiceProxy>, HostPortHash>
    CDCServiceProxyMap;

YB_STRONGLY_TYPED_BOOL(CreateMetricsEntityIfNotFound);

static const char* const kCDCSDKSnapshotDoneKey = "snapshot_done_key";

struct TabletCheckpoint {
  OpId op_id;
  // Timestamp at which the op ID was last updated.
  CoarseTimePoint last_update_time;
  // Timestamp at which stream polling happen.
  int64_t last_active_time;

  bool ExpiredAt(std::chrono::milliseconds duration, std::chrono::time_point<CoarseMonoClock> now) {
    return !IsInitialized(last_update_time) || (now - last_update_time) >= duration;
  }
};

// Maintain each tablet minimum checkpoint info for
// log cache eviction as well as for intent cleanup.
struct TabletCDCCheckpointInfo {
  OpId cdc_op_id = OpId::Max();
  OpId cdc_sdk_op_id = OpId::Invalid();
  MonoDelta cdc_sdk_op_id_expiration = MonoDelta::kZero;
  int64_t cdc_sdk_latest_active_time = 0;
  HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
};

using TabletIdCDCCheckpointMap = std::unordered_map<TabletId, TabletCDCCheckpointInfo>;
using TabletIdStreamIdSet = std::set<std::pair<TabletId, xrepl::StreamId>>;
using StreamIdSet = std::set<xrepl::StreamId>;
using RollBackTabletIdCheckpointMap =
    std::unordered_map<const std::string*, std::pair<int64_t, OpId>>;
class CDCServiceImpl : public CDCServiceIf {
 public:
  CDCServiceImpl(
      std::unique_ptr<CDCServiceContext> context,
      const scoped_refptr<MetricEntity>& metric_entity_server,
      MetricRegistry* metric_registry);

  CDCServiceImpl(const CDCServiceImpl&) = delete;
  void operator=(const CDCServiceImpl&) = delete;

  ~CDCServiceImpl();

  void CreateCDCStream(
      const CreateCDCStreamRequestPB* req,
      CreateCDCStreamResponsePB* resp,
      rpc::RpcContext rpc) override;
  void DeleteCDCStream(
      const DeleteCDCStreamRequestPB* req,
      DeleteCDCStreamResponsePB* resp,
      rpc::RpcContext rpc) override;
  void ListTablets(
      const ListTabletsRequestPB* req, ListTabletsResponsePB* resp, rpc::RpcContext rpc) override;
  void GetChanges(
      const GetChangesRequestPB* req, GetChangesResponsePB* resp, rpc::RpcContext rpc) override;
  bool IsReplicationPausedForStream(const std::string& stream_id) const EXCLUDES(mutex_);
  void GetCheckpoint(
      const GetCheckpointRequestPB* req,
      GetCheckpointResponsePB* resp,
      rpc::RpcContext rpc) override;

  // Walsender StartReplication RPCs
  void InitVirtualWALForCDC(
      const InitVirtualWALForCDCRequestPB* req, InitVirtualWALForCDCResponsePB* resp,
      rpc::RpcContext context) override;

  void GetConsistentChanges(
      const GetConsistentChangesRequestPB* req, GetConsistentChangesResponsePB* resp,
      rpc::RpcContext context) override;

  void DestroyVirtualWALForCDC(
      const DestroyVirtualWALForCDCRequestPB* req, DestroyVirtualWALForCDCResponsePB* resp,
      rpc::RpcContext context) override;

  // Destroy a batch of Virtual WAL instances managed by this CDC service.
  // Intended to be called from background jobs and hence only logs warnings in case of errors.
  void DestroyVirtualWALBatchForCDC(const std::vector<uint64_t>& session_ids);

  void UpdateAndPersistLSN(
      const UpdateAndPersistLSNRequestPB* req, UpdateAndPersistLSNResponsePB* resp,
      rpc::RpcContext context) override;

  void UpdatePublicationTableList(
    const UpdatePublicationTableListRequestPB* req,
    UpdatePublicationTableListResponsePB* resp, rpc::RpcContext context) override;


  Result<TabletCheckpoint> TEST_GetTabletInfoFromCache(const TabletStreamInfo& producer_tablet);

  // Update peers in other tablet servers about the latest minimum applied cdc index for a specific
  // tablet.
  void UpdateCdcReplicatedIndex(
      const UpdateCdcReplicatedIndexRequestPB* req,
      UpdateCdcReplicatedIndexResponsePB* resp,
      rpc::RpcContext rpc) override;

  Result<GetLatestEntryOpIdResponsePB> GetLatestEntryOpId(
      const GetLatestEntryOpIdRequestPB& req, CoarseTimePoint deadline) override;

  void BootstrapProducer(
      const BootstrapProducerRequestPB* req,
      BootstrapProducerResponsePB* resp,
      rpc::RpcContext rpc) override;

  void GetCDCDBStreamInfo(
      const GetCDCDBStreamInfoRequestPB* req,
      GetCDCDBStreamInfoResponsePB* resp,
      rpc::RpcContext context) override;

  Status UpdateCdcReplicatedIndexEntry(
      const std::string& tablet_id, int64 replicated_index, const OpId& cdc_sdk_replicated_op,
      const MonoDelta& cdc_sdk_op_id_expiration,
      RollBackTabletIdCheckpointMap* rollback_tablet_id_map,
      const HybridTime cdc_sdk_safe_time = HybridTime::kInvalid,
      bool initial_retention_barrier = false);

  void RollbackCdcReplicatedIndexEntry(
      const std::string& tablet_id, const std::pair<int64_t, OpId>& rollback_checkpoint_info);

  Result<SetCDCCheckpointResponsePB> SetCDCCheckpoint(
      const SetCDCCheckpointRequestPB& req, CoarseTimePoint deadline) override;

  void GetTabletListToPollForCDC(
      const GetTabletListToPollForCDCRequestPB* req,
      GetTabletListToPollForCDCResponsePB* resp,
      rpc::RpcContext context) override;

  void IsBootstrapRequired(
      const IsBootstrapRequiredRequestPB* req,
      IsBootstrapRequiredResponsePB* resp,
      rpc::RpcContext rpc) override;

  void CheckReplicationDrain(
      const CheckReplicationDrainRequestPB* req,
      CheckReplicationDrainResponsePB* resp,
      rpc::RpcContext context) override;

  void Shutdown() override;

  // Gets the associated metrics entity object stored in the additional metadata of the tablet.
  // If the metrics object is not present, then create it if create == true (eg if we have just
  // moved leaders) and not else (used to not recreate deleted metrics).
  Result<std::shared_ptr<xrepl::XClusterTabletMetrics>> GetXClusterTabletMetrics(
      const tablet::TabletPeer& tablet_peer, const xrepl::StreamId& stream_id,
      CreateMetricsEntityIfNotFound create = CreateMetricsEntityIfNotFound::kTrue);
  Result<std::shared_ptr<xrepl::CDCSDKTabletMetrics>> GetCDCSDKTabletMetrics(
      const tablet::TabletPeer& tablet_peer, const xrepl::StreamId& stream_id,
      CreateMetricsEntityIfNotFound create = CreateMetricsEntityIfNotFound::kTrue);

  void RemoveXReplTabletMetrics(
      const xrepl::StreamId& stream_id, std::shared_ptr<tablet::TabletPeer> tablet_peer);

  std::shared_ptr<xrepl::CDCServerMetrics> GetCDCServerMetrics() { return server_metrics_; }

  // Returns true if this server is a producer of a valid replication stream.
  bool CDCEnabled();

  // Marks the CDC enable flag as true.
  void SetCDCServiceEnabled();

  bool IsCDCSDKSnapshotDone(const GetChangesRequestPB& req);

  static bool IsCDCSDKSnapshotRequest(const CDCSDKCheckpointPB& req_checkpoint);

  static bool IsCDCSDKSnapshotBootstrapRequest(const CDCSDKCheckpointPB& req_checkpoint);

  // Sets paused producer XCluster streams.
  void SetPausedXClusterProducerStreams(
      const ::google::protobuf::Map<std::string, bool>& paused_producer_stream_ids,
      uint32_t xcluster_config_version);

  uint32_t GetXClusterConfigVersion() const;
  std::vector<xrepl::StreamTabletStats> GetAllStreamTabletStats() const EXCLUDES(mutex_);

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const;

  void UpdateTabletMetrics(
      const GetChangesResponsePB& resp, const TabletStreamInfo& producer_tablet,
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const OpId& op_id,
      const CDCRequestSource source_type, int64_t last_readable_index);

  void UpdateTabletXClusterMetrics(
      const GetChangesResponsePB& resp, const TabletStreamInfo& producer_tablet,
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer, const OpId& op_id,
      int64_t last_readable_index);

  void UpdateTabletCDCSDKMetrics(
      const GetChangesResponsePB& resp, const TabletStreamInfo& producer_tablet,
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer);

 private:
  friend class XClusterProducerBootstrap;
  friend class CDCSDKVirtualWAL;
  FRIEND_TEST(CDCServiceTest, TestMetricsOnDeletedReplication);
  FRIEND_TEST(CDCServiceTestMultipleServersOneTablet, TestMetricsAfterServerFailure);
  FRIEND_TEST(CDCServiceTestMultipleServersOneTablet, TestMetricsUponRegainingLeadership);

  class Impl;

  template <class ReqType, class RespType>
  bool CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc);

  Status CheckStreamActive(
      const TabletStreamInfo& producer_tablet, const int64_t& last_active_time_passed = 0);

  Status CheckTabletNotOfInterest(
      const TabletStreamInfo& producer_tablet, int64_t last_active_time_passed = 0,
      bool deletion_check = false);

  Result<int64_t> GetLastActiveTime(
      const TabletStreamInfo& producer_tablet, bool ignore_cache = false);

  Result<OpId> GetLastCheckpoint(
      const TabletStreamInfo& producer_tablet, const CDCRequestSource& request_source,
      const bool ignore_unpolled_tablets = true);

  Result<uint64_t> GetSafeTime(const xrepl::StreamId& stream_id, const TabletId& tablet_id);

  Result<CDCSDKCheckpointPB> GetLastCheckpointFromCdcState(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const CDCRequestSource& request_source, const TableId& colocated_table_id = "",
      const bool ignore_unpolled_tablets = true);

  Status InsertRowForColocatedTableInCDCStateTable(
      const TabletStreamInfo& producer_tablet, const TableId& colocated_table_id,
      const OpId& commit_op_id, const std::optional<HybridTime>& cdc_sdk_safe_time);

  Status UpdateCheckpointAndActiveTime(
      const TabletStreamInfo& producer_tablet, const OpId& sent_op_id, const OpId& commit_op_id,
      uint64_t last_record_hybrid_time, const std::optional<HybridTime>& cdc_sdk_safe_time,
      const CDCRequestSource& request_source = CDCRequestSource::CDCSDK, bool force_update = false,
      const bool is_snapshot = false, const std::string& snapshot_key = "",
      const TableId& colocated_table_id = "");

  Status UpdateSnapshotDone(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const TableId& colocated_table_id, const CDCSDKCheckpointPB& cdc_sdk_checkpoint);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> GetTablets(
      const xrepl::StreamId& stream_id,
      bool ignore_errors = false);

  Status CreateCDCStreamForTable(
      const TableId& table_id,
      const std::unordered_map<std::string, std::string>& options,
      const xrepl::StreamId& stream_id);

  void RollbackPartialCreate(const CDCCreationState& creation_state);

  Result<NamespaceId> GetNamespaceId(const std::string& ns_name, YQLDatabase db_type);

  Result<std::shared_ptr<StreamMetadata>> GetStream(
      const xrepl::StreamId& stream_id, RefreshStreamMapOption ops = RefreshStreamMapOption::kNone)
      EXCLUDES(mutex_);

  void RemoveStreamFromCache(const xrepl::StreamId& stream_id);

  Status CheckTabletValidForStream(const TabletStreamInfo& producer_info);

  void TabletLeaderGetChanges(
      const GetChangesRequestPB* req,
      GetChangesResponsePB* resp,
      std::shared_ptr<rpc::RpcContext>
          context,
      std::shared_ptr<tablet::TabletPeer>
          peer);

  void TabletLeaderGetCheckpoint(
      const GetCheckpointRequestPB* req, GetCheckpointResponsePB* resp, rpc::RpcContext* context);

  void UpdateTabletPeersWithMaxCheckpoint(
      const std::unordered_set<TabletId>& tablet_ids_with_max_checkpoint,
      std::unordered_set<TabletId>* failed_tablet_ids);

  void UpdateTabletPeersWithMinReplicatedIndex(TabletIdCDCCheckpointMap* tablet_min_checkpoint_map);

  Status UpdateTabletPeerWithCheckpoint(
      const TabletId& tablet_id, TabletCDCCheckpointInfo* tablet_info,
      bool enable_update_local_peer_min_index, bool ignore_rpc_failures = true);

  Result<client::internal::RemoteTabletPtr> GetRemoteTablet(
      const TabletId& tablet_id, const bool use_cache = true);
  Result<client::internal::RemoteTabletServer*> GetLeaderTServer(
      const TabletId& tablet_id, const bool use_cache = true);
  Status GetTServers(const TabletId& tablet_id,
                     std::vector<client::internal::RemoteTabletServer*>* servers);

  std::shared_ptr<CDCServiceProxy> GetCDCServiceProxy(client::internal::RemoteTabletServer* ts);

  std::shared_ptr<CDCServiceProxy> GetCDCServiceProxy(HostPort hostport);

  OpId GetMinSentCheckpointForTablet(const std::string& tablet_id);

  std::shared_ptr<MemTracker> GetMemTracker(
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
      const TabletStreamInfo& producer_info);

  OpId GetMinAppliedCheckpointForTablet(
      const std::string& tablet_id, const client::YBSessionPtr& session);

  Status UpdatePeersCdcMinReplicatedIndex(
      const TabletId& tablet_id, const TabletCDCCheckpointInfo& cdc_checkpoint_min,
      bool ignore_failures = true, bool initial_retention_barrier = false);

  struct ChildrenTabletMeta {
    TabletStreamInfo parent_tablet_info;
    uint64_t last_replication_time;
    std::shared_ptr<yb::xrepl::XClusterTabletMetrics> tablet_metric;
  };
  using EmptyChildrenTabletMap =
      std::unordered_map<TabletStreamInfo, ChildrenTabletMeta, TabletStreamInfo::Hash>;
  using TabletInfoToLastReplicationTimeMap =
      std::unordered_map<TabletStreamInfo, std::optional<uint64_t>, TabletStreamInfo::Hash>;
  // Helper function for processing metrics of children tablets. We need to find an ancestor tablet
  // that has a last replicated time and use that with our latest op's time to find the full lag.
  void ProcessMetricsForEmptyChildrenTablets(
      const EmptyChildrenTabletMap& empty_children_tablets,
      TabletInfoToLastReplicationTimeMap* cdc_state_tablets_to_last_replication_time);

  // Update metrics async_replication_sent_lag_micros and async_replication_committed_lag_micros.
  // Called periodically default 1s.
  void UpdateMetrics();

  // This method is used to read the cdc_state table to find the minimum replicated index for each
  // tablet and then update the peers' log objects. Also used to update lag metrics.
  void UpdatePeersAndMetrics();

  Status GetTabletIdsToPoll(
      const xrepl::StreamId stream_id,
      const std::set<TabletId>& active_or_hidden_tablets,
      const std::set<TabletId>& parent_tablets,
      const std::map<TabletId, TabletId>& child_to_parent_mapping,
      std::vector<std::pair<TabletId, CDCSDKCheckpointPB>>* result);

  // This method deletes entries from the cdc_state table that are contained in the set.
  Status DeleteCDCStateTableMetadata(
      const TabletIdStreamIdSet& cdc_state_entries_to_delete,
      const std::unordered_set<TabletId>& failed_tablet_ids,
      const StreamIdSet& slot_entries_to_be_deleted);

  MicrosTime GetLastReplicatedTime(const std::shared_ptr<tablet::TabletPeer>& tablet_peer);

  bool ShouldUpdateMetrics(MonoTime time_since_update_metrics);

  client::YBClient* client();

  // Initialize a new CDCStateTableEntry and adds the tablet stream to tablet_checkpoints_.
  void InitNewTabletStreamEntry(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      std::vector<TabletStreamInfo>* producer_entries_modified,
      std::vector<CDCStateTableEntry>* entries_to_insert);

  Status CreateCDCStreamForNamespace(
      const CreateCDCStreamRequestPB* req,
      CreateCDCStreamResponsePB* resp,
      CoarseTimePoint deadline);

  void FilterOutTabletsToBeDeletedByAllStreams(
      TabletIdCDCCheckpointMap* tablet_checkpoint_map,
      std::unordered_set<TabletId>* tablet_ids_with_max_checkpoint);

  Result<bool> CheckBeforeImageActive(
      const TabletId& tablet_id, const StreamMetadata& stream_metadata,
      const tablet::TabletPeerPtr& tablet_peer);

  Result<std::unordered_map<NamespaceId, uint64_t>> GetNamespaceMinRecordIdCommitTimeMap(
      const CDCStateTableRange& table_range, Status* iteration_status,
      StreamIdSet* slot_entries_to_be_deleted);

  Result<TabletIdCDCCheckpointMap> PopulateTabletCheckPointInfo(
      const TabletId& input_tablet_id = "",
      TabletIdStreamIdSet* tablet_stream_to_be_deleted = nullptr,
      StreamIdSet* slot_entries_to_be_deleted = nullptr);

  Status SetInitialCheckPoint(
      const OpId& checkpoint, const std::string& tablet_id,
      const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
      HybridTime cdc_sdk_safe_time = HybridTime::kInvalid);

  Status UpdateChildrenTabletsOnSplitOpForXCluster(
      const TabletStreamInfo& producer_tablet, const consensus::ReplicateMsg& split_op_msg);

  Status UpdateChildrenTabletsOnSplitOpForCDCSDK(const TabletStreamInfo& info);

  // Get enum map from the cache.
  Result<EnumOidLabelMap> GetEnumMapFromCache(const NamespaceName& ns_name, bool cql_namespace);

  // Update enum map in cache if required and get it.
  Result<EnumOidLabelMap> UpdateEnumCacheAndGetMap(
      const NamespaceName& ns_name, bool cql_namespace);

  // Update enum map in cache.
  Result<EnumOidLabelMap> UpdateEnumMapInCacheUnlocked(const NamespaceName& ns_name)
      REQUIRES(mutex_);

  // Get composite attributes map from the cache.
  Result<CompositeAttsMap> GetCompositeAttsMapFromCache(
      const NamespaceName& ns_name, bool cql_namespace);

  // Update composite attributes map in cache if required and get it.
  Result<CompositeAttsMap> UpdateCompositeCacheAndGetMap(
      const NamespaceName& ns_name, bool cql_namespace);

  // Update composite map in cache.
  Result<CompositeAttsMap> UpdateCompositeMapInCacheUnlocked(const NamespaceName& ns_name)
      REQUIRES(mutex_);

  void AddTabletCheckpoint(OpId op_id, const xrepl::StreamId& stream_id, const TabletId& tablet_id);

  // Validates the passed in AutoFlags config version and returns error if it does not match the
  // local universe. Returns true if the check passed, and false if it failed. Sets
  // AUTO_FLAGS_CONFIG_VERSION_MISMATCH error and the local_auto_flags_config_version in the resp
  // when the config version does not match.
  bool ValidateAutoFlagsConfigVersion(
      const GetChangesRequestPB& req, GetChangesResponsePB& resp, rpc::RpcContext& context);

  void LogGetChangesLagForCDCSDK(
      const xrepl::StreamId& stream_id, const GetChangesResponsePB& resp);

  rpc::Rpcs rpcs_;

  std::unique_ptr<CDCServiceContext> context_;

  MetricRegistry* metric_registry_;

  std::shared_ptr<xrepl::CDCServerMetrics> server_metrics_;

  // Prevents GetChanges "storms" by rejecting when all permits have been acquired.
  Semaphore get_changes_rpc_sem_;

  // Used to protect tablet_checkpoints_ and stream_metadata_ maps.
  mutable rw_spinlock mutex_;

  std::unique_ptr<rocksdb::RateLimiter> rate_limiter_;

  std::unique_ptr<Impl> impl_;

  std::unique_ptr<CDCStateTable> cdc_state_table_;

  std::unordered_map<xrepl::StreamId, std::shared_ptr<StreamMetadata>> stream_metadata_
      GUARDED_BY(mutex_);

  // Map of namespace name to (map of enum oid to enumlabel).
  EnumLabelCache enumlabel_cache_ GUARDED_BY(mutex_);

  // Map of namespace name to (map of type id to attributes).
  CompositeTypeCache composite_type_cache_ GUARDED_BY(mutex_);

  // Map of HostPort -> CDCServiceProxy. This is used to redirect requests to tablet leader's
  // CDC service proxy.
  CDCServiceProxyMap cdc_service_map_ GUARDED_BY(mutex_);

  // Thread with a few functions:
  //
  // Read the cdc_state table and get the minimum checkpoint for each tablet
  // and then, for each tablet this tserver is a leader, update the log minimum cdc replicated
  // index so we can use this information to correctly keep log files that are needed so we
  // can continue replicating cdc records. This runs periodically to handle
  // leadership changes (FLAGS_update_min_cdc_indices_interval_secs).
  // TODO(hector): It would be better to do this update only when a local peer becomes a leader.
  //
  // Periodically update lag metrics (FLAGS_update_metrics_interval_ms).
  scoped_refptr<Thread> update_peers_and_metrics_thread_;

  // True when this service is stopped. Used to inform
  // get_minimum_checkpoints_and_update_peers_thread_ that it should exit.
  bool cdc_service_stopped_ GUARDED_BY(mutex_){false};

  // True when the server is a producer of a valid replication stream.
  std::atomic<bool> cdc_enabled_{false};

  std::unordered_set<std::string> paused_xcluster_producer_streams_ GUARDED_BY(mutex_);

  uint32_t xcluster_config_version_ GUARDED_BY(mutex_) = 0;

  // Map of session_id (uint64) to VirtualWAL instance.
  std::unordered_map<uint64_t, std::shared_ptr<CDCSDKVirtualWAL>> session_virtual_wal_
      GUARDED_BY(mutex_);
};

}  // namespace cdc
}  // namespace yb
