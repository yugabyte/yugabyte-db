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

#pragma once

#include <memory>
#include <vector>

#include "yb/common/entity_ids_types.h"

#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster/xcluster_source_manager.h"
#include "yb/master/xcluster/xcluster_target_manager.h"

namespace yb {

namespace master {
class CDCStreamInfo;
class GetMasterXClusterConfigResponsePB;
class PauseResumeXClusterProducerStreamsRequestPB;
class PauseResumeXClusterProducerStreamsResponsePB;
class PostTabletCreateTaskBase;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
class XClusterConfig;
class XClusterSafeTimeService;
struct SysCatalogLoadingState;

// The XClusterManager class is responsible for managing all yb-master related control logic of
// XCluster. All XCluster related RPCs and APIs are handled by this class.
// TODO(#19714): Move XCluster related code from CatalogManager to this class.
class XClusterManager : public XClusterManagerIf,
                        public XClusterSourceManager,
                        public XClusterTargetManager {
 public:
  explicit XClusterManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterManager();

  Status Init();

  void StartShutdown() EXCLUDES(monitored_tasks_mutex_);
  void CompleteShutdown() EXCLUDES(monitored_tasks_mutex_);

  void Clear();

  Status RunLoaders(const TabletInfos& hidden_tablets);

  void SysCatalogLoaded(const LeaderEpoch& epoch);

  void RunBgTasks(const LeaderEpoch& epoch) override;

  void DumpState(std::ostream* out, bool on_disk_dump = false) const;

  Result<XClusterStatus> GetXClusterStatus() const override;
  Status PopulateXClusterStatusJson(JsonWriter& jw) const override;

  Status GetMasterXClusterConfig(GetMasterXClusterConfigResponsePB* resp);

  Result<uint32_t> GetXClusterConfigVersion() const;

  Status PrepareDefaultXClusterConfig(int64_t term, bool recreate);

  Status FillHeartbeatResponse(const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const;

  // Remove deleted xcluster stream IDs from producer stream Id map.
  Status RemoveStreamsFromSysCatalog(
      const LeaderEpoch& epoch, const std::vector<CDCStreamInfo*>& streams);

  Status PauseResumeXClusterProducerStreams(
      const PauseResumeXClusterProducerStreamsRequestPB* req,
      PauseResumeXClusterProducerStreamsResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // XCluster Safe Time.
  Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap() const override;
  Status SetXClusterNamespaceToSafeTimeMap(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) override;
  Status GetXClusterSafeTime(
      const GetXClusterSafeTimeRequestPB* req, GetXClusterSafeTimeResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Result<HybridTime> GetXClusterSafeTime(const NamespaceId& namespace_id) const override;

  Status GetXClusterSafeTimeForNamespace(
      const GetXClusterSafeTimeForNamespaceRequestPB* req,
      GetXClusterSafeTimeForNamespaceResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Result<HybridTime> GetXClusterSafeTimeForNamespace(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const XClusterSafeTimeFilter& filter) override;

  Status RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch) override;

  Status SetupUniverseReplication(
      const SetupUniverseReplicationRequestPB* req, SetupUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status IsSetupUniverseReplicationDone(
      const IsSetupUniverseReplicationDoneRequestPB* req,
      IsSetupUniverseReplicationDoneResponsePB* resp, rpc::RpcContext* rpc);

  Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id, bool skip_health_check) override;

  Status SetupNamespaceReplicationWithBootstrap(
      const SetupNamespaceReplicationWithBootstrapRequestPB* req,
      SetupNamespaceReplicationWithBootstrapResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status IsSetupNamespaceReplicationWithBootstrapDone(
      const IsSetupNamespaceReplicationWithBootstrapDoneRequestPB* req,
      IsSetupNamespaceReplicationWithBootstrapDoneResponsePB* resp, rpc::RpcContext* rpc);

  Status AlterUniverseReplication(
      const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch) override;

  Status DeleteUniverseReplication(
      const DeleteUniverseReplicationRequestPB* req, DeleteUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  // OutboundReplicationGroup RPCs.
  Status XClusterCreateOutboundReplicationGroup(
      const XClusterCreateOutboundReplicationGroupRequestPB* req,
      XClusterCreateOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status XClusterAddNamespaceToOutboundReplicationGroup(
      const XClusterAddNamespaceToOutboundReplicationGroupRequestPB* req,
      XClusterAddNamespaceToOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status XClusterRemoveNamespaceFromOutboundReplicationGroup(
      const XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB* req,
      XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status XClusterDeleteOutboundReplicationGroup(
      const XClusterDeleteOutboundReplicationGroupRequestPB* req,
      XClusterDeleteOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status IsXClusterBootstrapRequired(
      const IsXClusterBootstrapRequiredRequestPB* req, IsXClusterBootstrapRequiredResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status GetXClusterStreams(
      const GetXClusterStreamsRequestPB* req, GetXClusterStreamsResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status CreateXClusterReplication(
      const CreateXClusterReplicationRequestPB* req, CreateXClusterReplicationResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status IsCreateXClusterReplicationDone(
      const IsCreateXClusterReplicationDoneRequestPB* req,
      IsCreateXClusterReplicationDoneResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status AddNamespaceToXClusterReplication(
      const AddNamespaceToXClusterReplicationRequestPB* req,
      AddNamespaceToXClusterReplicationResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status IsAlterXClusterReplicationDone(
      const IsAlterXClusterReplicationDoneRequestPB* req,
      IsAlterXClusterReplicationDoneResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status RepairOutboundXClusterReplicationGroupAddTable(
      const RepairOutboundXClusterReplicationGroupAddTableRequestPB* req,
      RepairOutboundXClusterReplicationGroupAddTableResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status RepairOutboundXClusterReplicationGroupRemoveTable(
      const RepairOutboundXClusterReplicationGroupRemoveTableRequestPB* req,
      RepairOutboundXClusterReplicationGroupRemoveTableResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status GetXClusterOutboundReplicationGroups(
      const GetXClusterOutboundReplicationGroupsRequestPB* req,
      GetXClusterOutboundReplicationGroupsResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status GetXClusterOutboundReplicationGroupInfo(
      const GetXClusterOutboundReplicationGroupInfoRequestPB* req,
      GetXClusterOutboundReplicationGroupInfoResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status GetUniverseReplications(
      const GetUniverseReplicationsRequestPB* req, GetUniverseReplicationsResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status GetUniverseReplicationInfo(
      const GetUniverseReplicationInfoRequestPB* req, GetUniverseReplicationInfoResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status GetReplicationStatus(
      const GetReplicationStatusRequestPB* req, GetReplicationStatusResponsePB* resp,
      rpc::RpcContext* rpc);
  Status XClusterReportNewAutoFlagConfigVersion(
      const XClusterReportNewAutoFlagConfigVersionRequestPB* req,
      XClusterReportNewAutoFlagConfigVersionResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> GetPostTabletCreateTasks(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch);

  Status MarkIndexBackfillCompleted(
      const std::unordered_set<TableId>& index_ids, const LeaderEpoch& epoch) override;

  std::unordered_set<xcluster::ReplicationGroupId> GetInboundTransactionalReplicationGroups()
      const override;

  Status ClearXClusterSourceTableId(TableInfoPtr table_info, const LeaderEpoch& epoch) override;

  void NotifyAutoFlagsConfigChanged() override;

  // TODO: Remove these *Consumer* functions once Universe Replication is fully moved into
  // XClusterManager.

  void StoreConsumerReplicationStatus(
      const XClusterConsumerReplicationStatusPB& consumer_replication_status) override;
  void SyncConsumerReplicationStatusMap(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::Map<std::string, cdc::ProducerEntryPB>& producer_map) override;
  Result<bool> HasReplicationGroupErrors(
      const xcluster::ReplicationGroupId& replication_group_id) override;

  void RecordTableConsumerStream(
      const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id,
      const xrepl::StreamId& stream_id);

  void RemoveTableConsumerStream(
      const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id) override;

  void RemoveTableConsumerStreams(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::set<TableId>& tables_to_clear) override;

  Result<TableId> GetConsumerTableIdForStreamId(
      const xcluster::ReplicationGroupId& replication_group_id,
      const xrepl::StreamId& stream_id) const;

  bool IsTableReplicated(const TableId& table_id) const;

  bool IsTableReplicationConsumer(const TableId& table_id) const override;

  Status HandleTabletSplit(
      const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids,
      const LeaderEpoch& epoch) override;

  Status ValidateNewSchema(const TableInfo& table_info, const Schema& consumer_schema) const;

  Status ValidateSplitCandidateTable(const TableId& table_id) const;

  Status HandleTabletSchemaVersionReport(
      const TableInfo& table_info, SchemaVersion consumer_schema_version, const LeaderEpoch& epoch);

  Status RegisterMonitoredTask(server::MonitoredTaskPtr task) EXCLUDES(monitored_tasks_mutex_);
  void UnRegisterMonitoredTask(server::MonitoredTaskPtr task) EXCLUDES(monitored_tasks_mutex_);

 private:
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  bool in_memory_state_cleared_ = true;

  std::unique_ptr<XClusterConfig> xcluster_config_;

  std::mutex monitored_tasks_mutex_;
  std::unordered_set<server::MonitoredTaskPtr> monitored_tasks_ GUARDED_BY(monitored_tasks_mutex_);
};

}  // namespace master

}  // namespace yb
