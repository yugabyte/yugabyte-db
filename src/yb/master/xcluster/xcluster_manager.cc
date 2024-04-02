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

DEFINE_test_flag(bool, enable_xcluster_api_v2, false, "Allow the usage of new xCluster APIs");

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

void XClusterManager::SysCatalogLoaded() {
  XClusterSourceManager::SysCatalogLoaded();
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
  RETURN_NOT_OK(XClusterTargetManager::PopulateXClusterStatus(status));
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
  SCHECK(FLAGS_TEST_enable_xcluster_api_v2, IllegalState, "xCluster API v2 is not enabled.");

  LOG_FUNC_AND_RPC;

  SCHECK(
      !req->replication_group_id().empty(), InvalidArgument,
      "Replication group id cannot be empty");
  SCHECK(req->namespace_names_size() > 0, InvalidArgument, "Namespace names must be specified");

  std::vector<NamespaceName> namespace_names;
  for (const auto& namespace_name : req->namespace_names()) {
    namespace_names.emplace_back(namespace_name);
  }

  auto namespace_ids = VERIFY_RESULT(CreateOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), namespace_names, epoch));
  for (const auto& namespace_id : namespace_ids) {
    *resp->add_namespace_ids() = namespace_id;
  }

  return Status::OK();
}

Status XClusterManager::XClusterAddNamespaceToOutboundReplicationGroup(
    const XClusterAddNamespaceToOutboundReplicationGroupRequestPB* req,
    XClusterAddNamespaceToOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK(req->has_namespace_name(), InvalidArgument, "Namespace name must be specified");

  auto namespace_id = VERIFY_RESULT(AddNamespaceToOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_name(), epoch));

  resp->set_namespace_id(namespace_id);
  return Status::OK();
}

Status XClusterManager::XClusterRemoveNamespaceFromOutboundReplicationGroup(
    const XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB* req,
    XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK(req->has_namespace_id(), InvalidArgument, "Namespace id must be specified");

  return RemoveNamespaceFromOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id(), epoch);
}

Status XClusterManager::XClusterDeleteOutboundReplicationGroup(
    const XClusterDeleteOutboundReplicationGroupRequestPB* req,
    XClusterDeleteOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  return DeleteOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), epoch);
}

Status XClusterManager::IsXClusterBootstrapRequired(
    const IsXClusterBootstrapRequiredRequestPB* req, IsXClusterBootstrapRequiredResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  SCHECK_PB_FIELDS_ARE_SET(*req, replication_group_id, namespace_id);

  LOG_FUNC_AND_RPC;

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
  SCHECK(req->has_namespace_id(), InvalidArgument, "Namespace id must be specified");

  LOG_FUNC_AND_RPC;

  std::vector<std::pair<TableName, PgSchemaName>> table_names;
  for (const auto& table_name : req->table_infos()) {
    table_names.emplace_back(table_name.table_name(), table_name.pg_schema_name());
  }

  auto ns_info = VERIFY_RESULT(XClusterSourceManager::GetXClusterStreams(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->namespace_id(), table_names));

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

std::vector<std::shared_ptr<PostTabletCreateTaskBase>> XClusterManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> result;
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

}  // namespace yb::master
