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

#include "yb/master/xcluster/xcluster_manager.h"

#include <string>

#include "yb/common/hybrid_time.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/xcluster/xcluster_status.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/master/xcluster/xcluster_config.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"

DEFINE_RUNTIME_PREVIEW_bool(enable_xcluster_api_v2, false,
    "Allow the usage of v2 xCluster APIs that support DB Scoped replication groups");

DEFINE_RUNTIME_bool(disable_xcluster_db_scoped_new_table_processing, false,
    "When set disables automatic checkpointing of newly created tables on the source and adding "
    "table to inbound replication group on target");
TAG_FLAG(disable_xcluster_db_scoped_new_table_processing, advanced);

#define LOG_FUNC_AND_RPC \
  LOG_WITH_FUNC(INFO) << req->ShortDebugString() << ", from: " << RequestorString(rpc)

namespace yb::master {

XClusterManager::XClusterManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : XClusterSourceManager(master, catalog_manager, sys_catalog),
      XClusterTargetManager(master, catalog_manager, sys_catalog),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog) {
  xcluster_config_ = std::make_unique<XClusterConfig>(&sys_catalog_);
}

XClusterManager::~XClusterManager() {}

void XClusterManager::Shutdown() { XClusterTargetManager::Shutdown(); }

Status XClusterManager::Init() {
  RETURN_NOT_OK(XClusterSourceManager::Init());
  RETURN_NOT_OK(XClusterTargetManager::Init());

  return Status::OK();
}

void XClusterManager::Clear() {
  xcluster_config_->ClearState();
  XClusterSourceManager::Clear();
  XClusterTargetManager::Clear();

  in_memory_state_cleared_ = true;
}

Status XClusterManager::RunLoaders(const TabletInfos& hidden_tablets) {
  RSTATUS_DCHECK(
      in_memory_state_cleared_, IllegalState,
      "Attempt to load in-memory state before it is fully cleared");

  in_memory_state_cleared_ = false;

  RETURN_NOT_OK(
      sys_catalog_.Load<XClusterConfigLoader>("xcluster configuration", *xcluster_config_));

  RETURN_NOT_OK(XClusterSourceManager::RunLoaders(hidden_tablets));
  RETURN_NOT_OK(XClusterTargetManager::RunLoaders());

  return Status::OK();
}

void XClusterManager::SysCatalogLoaded(const LeaderEpoch& epoch) {
  XClusterSourceManager::SysCatalogLoaded(epoch);
  XClusterTargetManager::SysCatalogLoaded();
}

void XClusterManager::RunBgTasks(const LeaderEpoch& epoch) {
  XClusterTargetManager::RunBgTasks(epoch);
}

void XClusterManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  if (on_disk_dump) {
    xcluster_config_->DumpState(out);
  }
  XClusterSourceManager::DumpState(*out, on_disk_dump);
  XClusterTargetManager::DumpState(*out, on_disk_dump);
}

Status XClusterManager::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const {
  RETURN_NOT_OK(XClusterTargetManager::FillHeartbeatResponse(req, resp));

  return xcluster_config_->FillHeartbeatResponse(req, resp);
}

Status XClusterManager::PrepareDefaultXClusterConfig(int64_t term, bool recreate) {
  return xcluster_config_->PrepareDefault(term, recreate);
}

Result<XClusterStatus> XClusterManager::GetXClusterStatus() const {
  XClusterStatus status;
  RETURN_NOT_OK(XClusterSourceManager::PopulateXClusterStatus(
      status, VERIFY_RESULT(xcluster_config_->GetXClusterConfigEntryPB())));
  status.inbound_replication_group_statuses =
      VERIFY_RESULT(XClusterTargetManager::GetXClusterStatus());
  return status;
}

Status XClusterManager::PopulateXClusterStatusJson(JsonWriter& jw) const {
  auto xcluster_config = VERIFY_RESULT(xcluster_config_->GetXClusterConfigEntryPB());
  jw.String("xcluster_config");
  jw.Protobuf(xcluster_config);

  RETURN_NOT_OK(XClusterSourceManager::PopulateXClusterStatusJson(jw));
  RETURN_NOT_OK(XClusterTargetManager::PopulateXClusterStatusJson(jw));
  return Status::OK();
}

Status XClusterManager::GetMasterXClusterConfig(GetMasterXClusterConfigResponsePB* resp) {
  *resp->mutable_xcluster_config() = VERIFY_RESULT(xcluster_config_->GetXClusterConfigEntryPB());
  return Status::OK();
}

Result<uint32_t> XClusterManager::GetXClusterConfigVersion() const {
  return xcluster_config_->GetVersion();
}

Status XClusterManager::RemoveStreamsFromSysCatalog(
    const LeaderEpoch& epoch, const std::vector<CDCStreamInfo*>& streams) {
  std::vector<CDCStreamInfo*> xcluster_streams;
  std::copy_if(
      streams.begin(), streams.end(), std::back_inserter(xcluster_streams),
      [](const auto& stream) { return stream->IsXClusterStream(); });

  if (xcluster_streams.empty()) {
    return Status::OK();
  }

  RETURN_NOT_OK(xcluster_config_->RemoveStreams(epoch, xcluster_streams));
  return XClusterSourceManager::RemoveStreamsFromSysCatalog(xcluster_streams, epoch);
}

Status XClusterManager::PauseResumeXClusterProducerStreams(
    const PauseResumeXClusterProducerStreamsRequestPB* req,
    PauseResumeXClusterProducerStreamsResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK(req->has_is_paused(), InvalidArgument, "is_paused must be set in the request");
  bool paused = req->is_paused();
  std::string action = paused ? "Pausing" : "Resuming";
  if (req->stream_ids_size() == 0) {
    LOG(INFO) << action << " replication for all XCluster streams.";
  }

  auto xrepl_stream_ids = catalog_manager_.GetAllXReplStreamIds();
  std::vector<xrepl::StreamId> streams_to_change;

  if (req->stream_ids().empty()) {
    for (const auto& stream_id : xrepl_stream_ids) {
      streams_to_change.push_back(stream_id);
    }
  } else {
    for (const auto& stream_id_str : req->stream_ids()) {
      auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id_str));
      SCHECK(
          xrepl_stream_ids.contains(stream_id), NotFound, "XCluster Stream: $0 does not exists",
          stream_id_str);
      streams_to_change.push_back(stream_id);
    }
  }

  return xcluster_config_->PauseResumeXClusterProducerStreams(epoch, streams_to_change, paused);
}

Result<XClusterNamespaceToSafeTimeMap> XClusterManager::GetXClusterNamespaceToSafeTimeMap() const {
  return XClusterTargetManager::GetXClusterNamespaceToSafeTimeMap();
}

Status XClusterManager::SetXClusterNamespaceToSafeTimeMap(
    const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) {
  return XClusterTargetManager::SetXClusterNamespaceToSafeTimeMap(leader_term, safe_time_map);
}

Status XClusterManager::GetXClusterSafeTime(
    const GetXClusterSafeTimeRequestPB* req, GetXClusterSafeTimeResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  return XClusterTargetManager::GetXClusterSafeTime(resp, epoch);
}

Result<HybridTime> XClusterManager::GetXClusterSafeTime(const NamespaceId& namespace_id) const {
  return XClusterTargetManager::GetXClusterSafeTime(namespace_id);
}

Status XClusterManager::GetXClusterSafeTimeForNamespace(
    const GetXClusterSafeTimeForNamespaceRequestPB* req,
    GetXClusterSafeTimeForNamespaceResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  return XClusterTargetManager::GetXClusterSafeTimeForNamespace(req, resp, epoch);
}

Result<HybridTime> XClusterManager::GetXClusterSafeTimeForNamespace(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    const XClusterSafeTimeFilter& filter) {
  return XClusterTargetManager::GetXClusterSafeTimeForNamespace(epoch, namespace_id, filter);
}

Status XClusterManager::RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch) {
  return XClusterTargetManager::RefreshXClusterSafeTimeMap(epoch);
}

Status XClusterManager::XClusterCreateOutboundReplicationGroup(
    const XClusterCreateOutboundReplicationGroupRequestPB* req,
    XClusterCreateOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK(FLAGS_enable_xcluster_api_v2, IllegalState, "xCluster API v2 is not enabled.");
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);
  SCHECK(!req->namespace_ids().empty(), InvalidArgument, "Missing Namespace Ids");

  std::vector<NamespaceId> namespace_ids;
  for (const auto& namespace_id : req->namespace_ids()) {
    namespace_ids.emplace_back(namespace_id);
  }
  RETURN_NOT_OK(CreateOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), namespace_ids, epoch));

  return Status::OK();
}

Status XClusterManager::XClusterAddNamespaceToOutboundReplicationGroup(
    const XClusterAddNamespaceToOutboundReplicationGroupRequestPB* req,
    XClusterAddNamespaceToOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_id);

  RETURN_NOT_OK(AddNamespaceToOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id(), epoch));

  return Status::OK();
}

Status XClusterManager::XClusterRemoveNamespaceFromOutboundReplicationGroup(
    const XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB* req,
    XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_id);

  std::vector<HostPort> target_master_addresses;
  if (!req->target_master_addresses().empty()) {
    HostPortsFromPBs(req->target_master_addresses(), &target_master_addresses);
  }

  return RemoveNamespaceFromOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id(),
      target_master_addresses, epoch);
}

Status XClusterManager::XClusterDeleteOutboundReplicationGroup(
    const XClusterDeleteOutboundReplicationGroupRequestPB* req,
    XClusterDeleteOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  std::vector<HostPort> target_master_addresses;
  if (!req->target_master_addresses().empty()) {
    HostPortsFromPBs(req->target_master_addresses(), &target_master_addresses);
  }

  return DeleteOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), target_master_addresses, epoch);
}

Status XClusterManager::IsXClusterBootstrapRequired(
    const IsXClusterBootstrapRequiredRequestPB* req, IsXClusterBootstrapRequiredResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_id);

  auto bootstrap_required = VERIFY_RESULT(IsBootstrapRequired(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id()));

  if (!bootstrap_required.has_value()) {
    resp->set_not_ready(true);
    return Status::OK();
  }

  resp->set_initial_bootstrap_required(bootstrap_required.value());

  return Status::OK();
}

Status XClusterManager::GetXClusterStreams(
    const GetXClusterStreamsRequestPB* req, GetXClusterStreamsResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_id);

  // Only one of these fields should be set (or neither).
  SCHECK(
      req->table_infos().empty() || req->source_table_ids().empty(), InvalidArgument,
      "Only one of table_infos or table_ids should be set");

  std::optional<yb::master::NamespaceCheckpointInfo> ns_info;
  if (!req->source_table_ids().empty()) {
    // Handle the table_ids case.
    std::vector<TableId> table_ids(req->source_table_ids().begin(), req->source_table_ids().end());
    ns_info = VERIFY_RESULT(XClusterSourceManager::GetXClusterStreamsForTableIds(
        xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id(), table_ids));
  } else {
    // Handle the table_info case and the empty (all tables) case.
    std::vector<std::pair<TableName, PgSchemaName>> table_names;
    for (const auto& table_name : req->table_infos()) {
      table_names.emplace_back(table_name.table_name(), table_name.pg_schema_name());
    }

    ns_info = VERIFY_RESULT(XClusterSourceManager::GetXClusterStreams(
        xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id(),
        table_names));
  }

  if (!ns_info.has_value()) {
    resp->set_not_ready(true);
    return Status::OK();
  }

  resp->set_initial_bootstrap_required(ns_info->initial_bootstrap_required);
  for (const auto& ns_table_info : ns_info->table_infos) {
    auto* table_info = resp->add_table_infos();
    table_info->set_table_id(ns_table_info.table_id);
    table_info->set_xrepl_stream_id(ns_table_info.stream_id.ToString());
    table_info->set_table_name(ns_table_info.table_name);
    table_info->set_pg_schema_name(ns_table_info.pg_schema_name);
  }

  return Status::OK();
}

Status XClusterManager::CreateXClusterReplication(
    const CreateXClusterReplicationRequestPB* req, CreateXClusterReplicationResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);
  SCHECK(
      !req->target_master_addresses().empty(), InvalidArgument, "Missing Target Master addresses");

  std::vector<HostPort> target_master_addresses;
  HostPortsFromPBs(req->target_master_addresses(), &target_master_addresses);

  return XClusterSourceManager::CreateXClusterReplication(
      xcluster::ReplicationGroupId(req->replication_group_id()), target_master_addresses, epoch);
}

Status XClusterManager::IsCreateXClusterReplicationDone(
    const IsCreateXClusterReplicationDoneRequestPB* req,
    IsCreateXClusterReplicationDoneResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);
  SCHECK(
      !req->target_master_addresses().empty(), InvalidArgument, "Missing Target Master addresses");

  std::vector<HostPort> target_master_addresses;
  HostPortsFromPBs(req->target_master_addresses(), &target_master_addresses);

  auto create_result = VERIFY_RESULT(XClusterSourceManager::IsCreateXClusterReplicationDone(
      xcluster::ReplicationGroupId(req->replication_group_id()), target_master_addresses, epoch));

  resp->set_done(create_result.done());
  if (create_result.done()) {
    StatusToPB(create_result.status(), resp->mutable_replication_error());
  }
  return Status::OK();
}

Status XClusterManager::AddNamespaceToXClusterReplication(
    const AddNamespaceToXClusterReplicationRequestPB* req,
    AddNamespaceToXClusterReplicationResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_id);
  SCHECK(
      !req->target_master_addresses().empty(), InvalidArgument, "Missing Target Master addresses");

  std::vector<HostPort> target_master_addresses;
  HostPortsFromPBs(req->target_master_addresses(), &target_master_addresses);

  return XClusterSourceManager::AddNamespaceToTarget(
      xcluster::ReplicationGroupId(req->replication_group_id()), target_master_addresses,
      req->namespace_id(), epoch);
}

Status XClusterManager::IsAlterXClusterReplicationDone(
    const IsAlterXClusterReplicationDoneRequestPB* req,
    IsAlterXClusterReplicationDoneResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);
  SCHECK(
      !req->target_master_addresses().empty(), InvalidArgument, "Missing Target Master addresses");

  std::vector<HostPort> target_master_addresses;
  HostPortsFromPBs(req->target_master_addresses(), &target_master_addresses);

  auto create_result = VERIFY_RESULT(XClusterSourceManager::IsAlterXClusterReplicationDone(
      xcluster::ReplicationGroupId(req->replication_group_id()), target_master_addresses, epoch));

  resp->set_done(create_result.done());
  if (create_result.done()) {
    StatusToPB(create_result.status(), resp->mutable_replication_error());
  }
  return Status::OK();
}

Status XClusterManager::RepairOutboundXClusterReplicationGroupAddTable(
    const RepairOutboundXClusterReplicationGroupAddTableRequestPB* req,
    RepairOutboundXClusterReplicationGroupAddTableResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  return XClusterSourceManager::RepairOutboundReplicationGroupAddTable(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->table_id(),
      VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id())), epoch);
}

Status XClusterManager::RepairOutboundXClusterReplicationGroupRemoveTable(
    const RepairOutboundXClusterReplicationGroupRemoveTableRequestPB* req,
    RepairOutboundXClusterReplicationGroupRemoveTableResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  return XClusterSourceManager::RepairOutboundReplicationGroupRemoveTable(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->table_id(), epoch);
}

Status XClusterManager::GetXClusterOutboundReplicationGroups(
    const GetXClusterOutboundReplicationGroupsRequestPB* req,
    GetXClusterOutboundReplicationGroupsResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  auto outbound_groups =
      XClusterSourceManager::GetXClusterOutboundReplicationGroups(req->namespace_id());
  for (const auto& group_id : outbound_groups) {
    resp->add_replication_group_ids(group_id.ToString());
  }

  return Status::OK();
}

Status XClusterManager::GetXClusterOutboundReplicationGroupInfo(
    const GetXClusterOutboundReplicationGroupInfoRequestPB* req,
    GetXClusterOutboundReplicationGroupInfoResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  auto group_info = VERIFY_RESULT(XClusterSourceManager::GetXClusterOutboundReplicationGroupInfo(
      xcluster::ReplicationGroupId(req->replication_group_id())));

  for (const auto& [namespace_id, table_streams] : group_info) {
    auto* ns_info = resp->add_namespace_infos();
    ns_info->set_namespace_id(namespace_id);
    for (const auto& [table_id, stream_id] : table_streams) {
      ns_info->mutable_table_streams()->insert({table_id, stream_id.ToString()});
    }
  }

  return Status::OK();
}

Status XClusterManager::GetUniverseReplications(
    const GetUniverseReplicationsRequestPB* req, GetUniverseReplicationsResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  const auto& namespace_filter = req->namespace_id();

  const auto replication_groups = XClusterTargetManager::GetUniverseReplications(namespace_filter);
  for (const auto& replication_group : replication_groups) {
    resp->add_replication_group_ids(replication_group.ToString());
  }

  return Status::OK();
}

Status XClusterManager::GetUniverseReplicationInfo(
    const GetUniverseReplicationInfoRequestPB* req, GetUniverseReplicationInfoResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  const auto replication_info = VERIFY_RESULT(XClusterTargetManager::GetUniverseReplicationInfo(
      xcluster::ReplicationGroupId(req->replication_group_id())));

  resp->set_replication_type(replication_info.replication_type);
  resp->set_source_master_addresses(replication_info.master_addrs);

  for (const auto& [_, tables] : replication_info.table_statuses_by_namespace) {
    for (const auto& table_status : tables) {
      auto* table_info = resp->add_table_infos();
      table_info->set_target_table_id(table_status.target_table_id);
      table_info->set_source_table_id(table_status.source_table_id);
      table_info->set_stream_id(table_status.stream_id.ToString());
    }
  }

  for (const auto& [target_namespace_id, source_namespace_id] :
       replication_info.db_scope_namespace_id_map) {
    auto* ns_info = resp->add_db_scoped_infos();
    ns_info->set_target_namespace_id(target_namespace_id);
    ns_info->set_source_namespace_id(source_namespace_id);
  }

  return Status::OK();
}

std::vector<std::shared_ptr<PostTabletCreateTaskBase>> XClusterManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> result;

  if (FLAGS_disable_xcluster_db_scoped_new_table_processing) {
    return result;
  }

  {
    auto tasks = XClusterSourceManager::GetPostTabletCreateTasks(table_info, epoch);
    MoveCollection(&tasks, &result);
  }
  {
    auto tasks = XClusterTargetManager::GetPostTabletCreateTasks(table_info, epoch);
    MoveCollection(&tasks, &result);
  }

  return result;
}

Status XClusterManager::MarkIndexBackfillCompleted(
    const std::unordered_set<TableId>& index_ids, const LeaderEpoch& epoch) {
  return XClusterSourceManager::MarkIndexBackfillCompleted(index_ids, epoch);
}

std::unordered_set<xcluster::ReplicationGroupId>
XClusterManager::GetInboundTransactionalReplicationGroups() const {
  return XClusterTargetManager::GetTransactionalReplicationGroups();
}

Status XClusterManager::ClearXClusterSourceTableId(
    TableInfoPtr table_info, const LeaderEpoch& epoch) {
  return XClusterTargetManager::ClearXClusterSourceTableId(table_info, epoch);
}

}  // namespace yb::master
