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

#include "yb/common/colocated_util.h"
#include "yb/common/hybrid_time.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_config.h"
#include "yb/master/xcluster/xcluster_status.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flag_validators.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"

DEFINE_RUNTIME_AUTO_bool(enable_xcluster_api_v2, kExternal, false, true,
    "Allow the usage of v2 xCluster APIs that support DB Scoped replication groups");

DEFINE_RUNTIME_bool(disable_xcluster_db_scoped_new_table_processing, false,
    "When set disables automatic checkpointing of newly created tables on the source and adding "
    "table to inbound replication group on target");
TAG_FLAG(disable_xcluster_db_scoped_new_table_processing, advanced);

DEFINE_RUNTIME_AUTO_bool(enable_tablet_split_of_xcluster_replicated_tables, kExternal, false, true,
    "When set, it enables automatic tablet splitting for tables that are part of an "
    "xCluster replication setup");

DEFINE_RUNTIME_uint32(xcluster_ysql_statement_timeout_sec, 120,
    "Timeout for YSQL statements executed during xCluster operations.");

DEFINE_RUNTIME_AUTO_bool(xcluster_enable_ddl_replication, kExternal, false, true,
    "Enables xCluster automatic DDL replication.");

DEFINE_test_flag(bool, force_automatic_ddl_replication_mode, false,
    "Make XClusterCreateOutboundReplicationGroup always use automatic instead of semi-automatic "
    "xCluster replication mode.");

DEFINE_RUNTIME_AUTO_bool(ysql_auto_add_new_index_to_bidirectional_xcluster_infra, kExternal,
    false, true,
    "Determines if the system supports the capability of automatically adding ysql index to "
    "bi-directional xCluster. NOTE: Do not change this flag directly. If you want to disable the "
    "feature, use ysql_auto_add_new_index_to_bidirectional_xcluster instead.");

DEFINE_RUNTIME_bool(ysql_auto_add_new_index_to_bidirectional_xcluster, true,
    "If the indexed table is part of a bi-directional xCluster setup, then automatically add new "
    "indexes for this table to replication. This flag must be set on both universes, and the index "
    "must be created concurrently on both universes.");

DEFINE_RUNTIME_int32(xcluster_cleanup_tables_frequency_secs, /*half an hour*/ 30 * 60,
    "Frequency at which we will clean up old xCluster automatic mode"
    "DDL replication table entries.  0 means do not perform cleanup.");

DEFINE_RUNTIME_uint32(xcluster_ddl_tables_retention_secs, /*1 week*/ 7 * 24 * 60 * 60,
    "Age of DDL replication table entries beyond which they are cleaned up.  This needs to be "
    "at least one day.");

DEFINE_validator(xcluster_ddl_tables_retention_secs, FLAG_GE_VALUE_VALIDATOR(1 * 24 * 60 * 60));

#define LOG_FUNC_AND_RPC \
  LOG_WITH_FUNC(INFO) << req->ShortDebugString() << ", from: " << RequestorString(rpc)

namespace yb::master {

using namespace std::literals;

namespace {

template <typename RequestType>
Status ValidateUniverseUUID(const RequestType& req, CatalogManager& catalog_manager) {
  if (req->has_universe_uuid() && !req->universe_uuid().empty()) {
    auto universe_uuid = catalog_manager.GetUniverseUuidIfExists();
    SCHECK(
        universe_uuid && universe_uuid->ToString() == req->universe_uuid(), InvalidArgument,
        "Invalid Universe UUID $0. Expected $1", req->universe_uuid(),
        (universe_uuid ? universe_uuid->ToString() : "empty"));
  }

  return Status::OK();
}

bool IsYsqlAutoAddNewIndexToBiDirectionalXClusterEnabled() {
  return FLAGS_ysql_auto_add_new_index_to_bidirectional_xcluster_infra &&
         FLAGS_ysql_auto_add_new_index_to_bidirectional_xcluster;
}

}  // namespace

XClusterManager::XClusterManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : XClusterSourceManager(master, catalog_manager, sys_catalog),
      XClusterTargetManager(master, catalog_manager, sys_catalog),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog) {
  xcluster_config_ = std::make_unique<XClusterConfig>(&sys_catalog_);

  // No need to run cleaning task as soon as we start; delay it to make startup faster.
  time_of_last_clean_tables_task_run_ = CoarseMonoClock::Now();
}

XClusterManager::~XClusterManager() {}

void XClusterManager::StartShutdown() {
  shutdown_started_ = true;
  XClusterTargetManager::StartShutdown();

  // Copy the tasks out since Abort will trigger UnRegister which needs the mutex.
  decltype(monitored_tasks_) tasks;
  {
    std::lock_guard l(monitored_tasks_mutex_);
    tasks = monitored_tasks_;
  }
  for (auto& task : tasks) {
    task->AbortAndReturnPrevState(
        STATUS(Aborted, "Master is shutting down"), /* call_task_finisher */ true);
  }
}

void XClusterManager::CompleteShutdown() {
  XClusterTargetManager::CompleteShutdown();

  CHECK_OK(WaitFor(
      [this]() {
        std::lock_guard l(monitored_tasks_mutex_);
        YB_LOG_EVERY_N_SECS(WARNING, 10)
            << "Waiting for " << monitored_tasks_.size() << " monitored tasks to complete";
        return monitored_tasks_.empty();
      },
      MonoDelta::kMax, "Waiting for monitored tasks to complete",
      MonoDelta::FromMilliseconds(100)));
}

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
  if (shutdown_started_) {
    return;
  }

  XClusterTargetManager::RunBgTasks(epoch);

  ProcessCleanupTablesPeriodically();
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

Status XClusterManager::SetXClusterRole(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    XClusterNamespaceInfoPB_XClusterRole role) {
  return xcluster_config_->SetXClusterRole(epoch, namespace_id, role);
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

Status XClusterManager::SetUniverseReplicationEnabled(
    const SetUniverseReplicationEnabledRequestPB* req,
    SetUniverseReplicationEnabledResponsePB* resp, rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);
  SCHECK_PB_FIELDS_SET(*req, is_enabled);

  return XClusterTargetManager::SetReplicationGroupEnabled(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->is_enabled(), epoch,
      rpc->GetClientDeadline());
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

XClusterNamespaceToSafeTimeMap XClusterManager::GetXClusterNamespaceToSafeTimeMap() const {
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

Result<std::optional<HybridTime>> XClusterManager::TryGetXClusterSafeTimeForBackfill(
    const std::vector<TableId>& index_table_ids, const TableInfoPtr& indexed_table,
    const LeaderEpoch& epoch) const {
  return XClusterTargetManager::TryGetXClusterSafeTimeForBackfill(
      index_table_ids, indexed_table, epoch);
}

Status XClusterManager::GetXClusterSafeTimeForNamespace(
    const GetXClusterSafeTimeForNamespaceRequestPB* req,
    GetXClusterSafeTimeForNamespaceResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  return XClusterTargetManager::GetXClusterSafeTimeForNamespace(req, resp, epoch);
}

Result<HybridTime> XClusterManager::GetXClusterSafeTimeForNamespace(
    const NamespaceId& namespace_id, const XClusterSafeTimeFilter& filter) const {
  return XClusterTargetManager::GetXClusterSafeTimeForNamespace(namespace_id, filter);
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
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_ids);
  bool automatic_ddl_mode =
      req->automatic_ddl_mode() || FLAGS_TEST_force_automatic_ddl_replication_mode;
  SCHECK(
      !automatic_ddl_mode || FLAGS_xcluster_enable_ddl_replication, InvalidArgument,
      "Automatic DDL replication (xcluster_enable_ddl_replication) is not enabled.");

  std::vector<NamespaceId> namespace_ids;
  for (const auto& namespace_id : req->namespace_ids()) {
    namespace_ids.emplace_back(namespace_id);
  }

  RETURN_NOT_OK(CreateOutboundReplicationGroup(
      xcluster::ReplicationGroupId(req->replication_group_id()), namespace_ids,
      automatic_ddl_mode, epoch));

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

Status XClusterManager::XClusterEnsureSequenceUpdatesAreInWal(
    const XClusterEnsureSequenceUpdatesAreInWalRequestPB* req,
    XClusterEnsureSequenceUpdatesAreInWalResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id, namespace_ids);

  std::vector<NamespaceId> namespace_ids{req->namespace_ids().begin(), req->namespace_ids().end()};
  return EnsureSequenceUpdatesAreInWal(
      xcluster::ReplicationGroupId(req->replication_group_id()), namespace_ids);
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

  resp->set_automatic_ddl_mode(group_info.automatic_ddl_mode);

  for (const auto& [namespace_id, table_streams] : group_info.namespace_table_map) {
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
  LOG_FUNC_AND_RPC;

  const auto& namespace_filter = req->namespace_id();

  const auto replication_groups = XClusterTargetManager::GetUniverseReplications(namespace_filter);
  for (const auto& replication_group : replication_groups) {
    resp->add_replication_group_ids(replication_group.ToString());
  }

  return Status::OK();
}

Status XClusterManager::GetReplicationStatus(
    const GetReplicationStatusRequestPB* req, GetReplicationStatusResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG_FUNC_AND_RPC;

  return XClusterTargetManager::GetReplicationStatus(req, resp);
}

Status XClusterManager::GetUniverseReplicationInfo(
    const GetUniverseReplicationInfoRequestPB* req, GetUniverseReplicationInfoResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
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

Status XClusterManager::XClusterReportNewAutoFlagConfigVersion(
    const XClusterReportNewAutoFlagConfigVersionRequestPB* req,
    XClusterReportNewAutoFlagConfigVersionResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  const xcluster::ReplicationGroupId replication_group_id(req->replication_group_id());
  const auto new_version = req->auto_flag_config_version();

  return XClusterTargetManager::ReportNewAutoFlagConfigVersion(
      replication_group_id, new_version, epoch);
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

Status XClusterManager::ClearXClusterFieldsAfterYsqlDDL(
    TableInfoPtr table_info, SysTablesEntryPB& table_pb, const LeaderEpoch& epoch) {
  return XClusterTargetManager::ClearXClusterFieldsAfterYsqlDDL(table_info, table_pb, epoch);
}

void XClusterManager::NotifyAutoFlagsConfigChanged() {
  XClusterTargetManager::NotifyAutoFlagsConfigChanged();
}

void XClusterManager::StoreConsumerReplicationStatus(
    const XClusterConsumerReplicationStatusPB& consumer_replication_status) {
  XClusterTargetManager::StoreReplicationStatus(consumer_replication_status);
}

void XClusterManager::SyncConsumerReplicationStatusMap(
    const xcluster::ReplicationGroupId& replication_group_id,
    const google::protobuf::Map<std::string, cdc::ProducerEntryPB>& producer_map) {
  XClusterTargetManager::SyncReplicationStatusMap(replication_group_id, producer_map);
}

Result<bool> XClusterManager::HasReplicationGroupErrors(
    const xcluster::ReplicationGroupId& replication_group_id) {
  return XClusterTargetManager::HasReplicationGroupErrors(replication_group_id);
}

void XClusterManager::RecordTableConsumerStream(
    const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id) {
  XClusterTargetManager::RecordTableStream(table_id, replication_group_id, stream_id);
}

void XClusterManager::RemoveTableConsumerStream(
    const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id) {
  XClusterTargetManager::RemoveTableStream(table_id, replication_group_id);
}

void XClusterManager::RemoveTableConsumerStreams(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::set<TableId>& tables_to_clear) {
  XClusterTargetManager::RemoveTableStreams(replication_group_id, tables_to_clear);
}

Result<TableId> XClusterManager::GetConsumerTableIdForStreamId(
    const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id) const {
  return XClusterTargetManager::GetTableIdForStreamId(replication_group_id, stream_id);
}

bool XClusterManager::IsTableReplicationConsumer(const TableId& table_id) const {
  return XClusterTargetManager::IsTableReplicated(table_id);
}

bool XClusterManager::IsTableReplicated(const TableId& table_id) const {
  return XClusterSourceManager::IsTableReplicated(table_id) ||
         XClusterTargetManager::IsTableReplicated(table_id);
}

bool XClusterManager::IsNamespaceInAutomaticDDLMode(const NamespaceId& namespace_id) const {
  return XClusterSourceManager::IsNamespaceInAutomaticDDLMode(namespace_id) ||
         XClusterTargetManager::IsNamespaceInAutomaticDDLMode(namespace_id);
}

bool XClusterManager::IsNamespaceInAutomaticModeSource(const NamespaceId& namespace_id) const {
  return XClusterSourceManager::IsNamespaceInAutomaticDDLMode(namespace_id);
}

bool XClusterManager::IsNamespaceInAutomaticModeTarget(const NamespaceId& namespace_id) const {
  return XClusterTargetManager::IsNamespaceInAutomaticDDLMode(namespace_id);
}

bool XClusterManager::IsTableBiDirectionallyReplicated(const TableId& table_id) const {
  // In theory this would return true for B in the case of chaining A -> B -> C, but we don't
  // support chaining in xCluster.
  // Replicating between 3 universes will need bi-directional xCluster A <=> B <=> C <=> A.
  return XClusterSourceManager::IsTableReplicated(table_id) &&
         XClusterTargetManager::IsTableReplicated(table_id);
}

bool XClusterManager::ShouldAutoAddIndexesToBiDirectionalXCluster(
    const TableInfo& indexed_table) const {
  if (!IsYsqlAutoAddNewIndexToBiDirectionalXClusterEnabled() ||
      indexed_table.GetTableType() != PGSQL_TABLE_TYPE) {
    return false;
  }

  auto table_id = indexed_table.id();
  if (indexed_table.colocated()) {
    table_id = indexed_table.LockForRead()->pb.parent_table_id();
  }

  return IsTableBiDirectionallyReplicated(table_id);
}

Status XClusterManager::HandleTabletSplit(
    const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids,
    const LeaderEpoch& epoch) {
  return XClusterTargetManager::HandleTabletSplit(consumer_table_id, split_tablet_ids, epoch);
}

Status XClusterManager::ValidateSplitCandidateTable(const TableId& table_id) const {
  if (!FLAGS_enable_tablet_split_of_xcluster_replicated_tables && IsTableReplicated(table_id)) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for table $0 since it is part of xCluster replication",
        table_id);
  }

  RETURN_NOT_OK(XClusterSourceManager::ValidateSplitCandidateTable(table_id));

  return Status::OK();
}

Status XClusterManager::SetupUniverseReplication(
    const SetupUniverseReplicationRequestPB* req, SetupUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK(
      !req->automatic_ddl_mode() || FLAGS_xcluster_enable_ddl_replication, InvalidArgument,
      "Automatic DDL replication (xcluster_enable_ddl_replication) is not enabled.");

  return XClusterTargetManager::SetupUniverseReplication(req, resp, epoch);
}

Status XClusterManager::SetupUniverseReplication(
    XClusterSetupUniverseReplicationData&& data, const LeaderEpoch& epoch) {
  SCHECK(
      !data.automatic_ddl_mode || FLAGS_xcluster_enable_ddl_replication, InvalidArgument,
      "Automatic DDL replication (xcluster_enable_ddl_replication) is not enabled.");
  return XClusterTargetManager::SetupUniverseReplication(std::move(data), epoch);
}

/*
 * Checks if the universe replication setup has completed.
 * Returns Status::OK() if this call succeeds, and uses resp->done() to determine if the setup has
 * completed (either failed or succeeded). If the setup has failed, then resp->replication_error()
 * is also set. If it succeeds, replication_error() gets set to OK.
 */
Status XClusterManager::IsSetupUniverseReplicationDone(
    const IsSetupUniverseReplicationDoneRequestPB* req,
    IsSetupUniverseReplicationDoneResponsePB* resp, rpc::RpcContext* rpc) {
  LOG_FUNC_AND_RPC;

  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  auto is_operation_done = VERIFY_RESULT(IsSetupUniverseReplicationDone(
      xcluster::ReplicationGroupId(req->replication_group_id()), /*skip_health_check=*/false));

  resp->set_done(is_operation_done.done());
  StatusToPB(is_operation_done.status(), resp->mutable_replication_error());
  return Status::OK();
}

Result<IsOperationDoneResult> XClusterManager::IsSetupUniverseReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id, bool skip_health_check) {
  return XClusterTargetManager::IsSetupUniverseReplicationDone(
      replication_group_id, skip_health_check);
}

Status XClusterManager::SetupNamespaceReplicationWithBootstrap(
    const SetupNamespaceReplicationWithBootstrapRequestPB* req,
    SetupNamespaceReplicationWithBootstrapResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  return XClusterTargetManager::SetupNamespaceReplicationWithBootstrap(req, resp, epoch);
}

Status XClusterManager::IsSetupNamespaceReplicationWithBootstrapDone(
    const IsSetupNamespaceReplicationWithBootstrapDoneRequestPB* req,
    IsSetupNamespaceReplicationWithBootstrapDoneResponsePB* resp, rpc::RpcContext* rpc) {
  LOG_FUNC_AND_RPC;

  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  *resp = VERIFY_RESULT(XClusterTargetManager::IsSetupNamespaceReplicationWithBootstrapDone(
      xcluster::ReplicationGroupId(req->replication_group_id())));

  return Status::OK();
}

Status XClusterManager::AlterUniverseReplication(
    const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  RETURN_NOT_OK(ValidateUniverseUUID(req, catalog_manager_));

  return XClusterTargetManager::AlterUniverseReplication(req, resp, epoch);
}

Status XClusterManager::DeleteUniverseReplication(
    const DeleteUniverseReplicationRequestPB* req, DeleteUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  SCHECK_PB_FIELDS_NOT_EMPTY(*req, replication_group_id);

  RETURN_NOT_OK(ValidateUniverseUUID(req, catalog_manager_));

  std::unordered_map<NamespaceId, uint32_t> source_namespace_id_to_oid_to_bump_above;
  for (const auto& [consumer_namespace_id, oid_to_bump_above] : req->producer_namespace_oids()) {
    source_namespace_id_to_oid_to_bump_above[consumer_namespace_id] = oid_to_bump_above;
  }

  RETURN_NOT_OK(XClusterTargetManager::DeleteUniverseReplication(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->ignore_errors(),
      req->skip_producer_stream_deletion(), resp, epoch, source_namespace_id_to_oid_to_bump_above));

  LOG(INFO) << "Successfully completed DeleteUniverseReplication request from "
            << RequestorString(rpc);

  return Status::OK();
}

Status XClusterManager::AddTableToReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& source_table_id,
    const xrepl::StreamId& bootstrap_id, const std::optional<TableId>& target_table_id,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "Adding table " << source_table_id << " to replication group "
            << replication_group_id
            << (target_table_id ? " with target table " + *target_table_id : "");

  return XClusterTargetManager::AddTableToReplicationGroup(
      replication_group_id, source_table_id, bootstrap_id, target_table_id, epoch);
}

// Inserts the sent schema into the historical packing schema for the target table.
Status XClusterManager::InsertPackedSchemaForXClusterTarget(
    const InsertPackedSchemaForXClusterTargetRequestPB* req,
    InsertPackedSchemaForXClusterTargetResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, table_id);

  TableId table_id(req->table_id());
  if (IsColocationParentTableId(req->table_id())) {
    SCHECK(
        req->has_colocation_id(), InvalidArgument,
        "Missing colocation id for given colocated table $0", req->table_id());
    SCHECK_NE(req->colocation_id(), kColocationIdNotSet, InvalidArgument, "Invalid colocation id");

    // Use the table id matching the colocation id.
    auto tablegroup_id = GetTablegroupIdFromParentTableId(req->table_id());
    table_id =
        VERIFY_RESULT(catalog_manager_.GetColocatedTableId(tablegroup_id, req->colocation_id()));
  }

  return XClusterTargetManager::InsertPackedSchemaForXClusterTarget(
      table_id, req->packed_schema(), req->current_consumer_schema_version(), epoch);
}

Status XClusterManager::HandleNewSchemaForAutomaticXClusterTarget(
    const HandleNewSchemaForAutomaticXClusterTargetRequestPB* req,
    HandleNewSchemaForAutomaticXClusterTargetResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  return XClusterTargetManager::HandleNewSchemaForAutomaticXClusterTarget(req, resp, epoch);
}

Status XClusterManager::InsertHistoricalColocatedSchemaPacking(
    const InsertHistoricalColocatedSchemaPackingRequestPB* req,
    InsertHistoricalColocatedSchemaPackingResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK_PB_FIELDS_NOT_EMPTY(
      *req, replication_group_id, target_parent_table_id, colocation_id, source_schema_version,
      schema);
  return XClusterTargetManager::InsertHistoricalColocatedSchemaPacking(req, resp, epoch);
}

Status XClusterManager::RegisterMonitoredTask(server::MonitoredTaskPtr task) {
  std::lock_guard l(monitored_tasks_mutex_);
  SCHECK_FORMAT(
      monitored_tasks_.insert(task).second, AlreadyPresent, "Task $0 already registered",
      task->description());
  return Status::OK();
}

void XClusterManager::UnRegisterMonitoredTask(server::MonitoredTaskPtr task) {
  std::lock_guard l(monitored_tasks_mutex_);
  monitored_tasks_.erase(task);
}

Status XClusterManager::ProcessCreateTableReq(
    const CreateTableRequestPB& req, SysTablesEntryPB& table_pb, const TableId& table_id,
    const NamespaceId& namespace_id) const {
  return XClusterTargetManager::ProcessCreateTableReq(req, table_pb, table_id, namespace_id);
}

Status XClusterManager::ValidateCreateTableRequest(const CreateTableRequestPB& req) {
  TableId table_id;
  std::string error_str;
  if (!req.old_rewrite_table_id().empty()) {
    table_id = req.old_rewrite_table_id();
    error_str = "Cannot rewrite a table that is a part of non-automatic mode XCluster replication.";
  } else if (IsIndex(req) && req.skip_index_backfill()) {
    table_id = req.indexed_table_id();
    error_str =
        "Cannot create nonconcurrent index on a table that is a part of non-automatic mode "
        "XCluster replication.";
  } else {
    return Status::OK();
  }

  const auto namespace_id =
      VERIFY_RESULT(catalog_manager_.GetTableById(table_id))->LockForRead()->namespace_id();

  SCHECK(
      !IsTableReplicated(table_id) || IsNamespaceInAutomaticDDLMode(namespace_id), NotSupported,
      error_str);

  return Status::OK();
}

void XClusterManager::ProcessCleanupTablesPeriodically() {
  int32_t wait_flag_value = FLAGS_xcluster_cleanup_tables_frequency_secs;
  if (wait_flag_value <= 0) {
    // Cleanup tables work has been disabled.
    return;
  }
  auto now = CoarseMonoClock::Now();
  if (time_of_last_clean_tables_task_run_ + wait_flag_value * 1s > CoarseMonoClock::Now()) {
    return;
  }
  // Cleanup duration is much smaller than cleanup frequency so it doesn't matter whether we save
  // the start or end time of the run.
  time_of_last_clean_tables_task_run_ = now;

  std::vector<scoped_refptr<NamespaceInfo>> namespaces;
  catalog_manager_.GetAllNamespaces(&namespaces, /*include_only_running_namespaces=*/true);
  for (const auto& namespace_info : namespaces) {
    // Skip namespaces not in automatic mode.
    const auto namespace_id = namespace_info->id();
    bool is_source = IsNamespaceInAutomaticModeSource(namespace_id);
    bool is_target = IsNamespaceInAutomaticModeTarget(namespace_id);
    if (!(is_source || is_target)) {
      continue;
    }

    const auto namespace_name = namespace_info->name();
    VLOG(1) << "Attempting to clean up DDL replication tables for namespace " << namespace_name
            << "; is_source: " << is_source << " is_target: " << is_target;

    MicrosecondsInt64 too_old_time =
        GetCurrentTimeMicros() -
        static_cast<int64_t>(FLAGS_xcluster_ddl_tables_retention_secs) * 1000 * 1000;
    std::vector<std::string> statements;
    if (is_source) {
      // Next statement will fail if it runs on a target (mode switch might occur before the
      // statement runs).
      statements.push_back(Format(
          "DELETE FROM yb_xcluster_ddl_replication.ddl_queue "
          "WHERE ddl_end_time < $0;",
          too_old_time));
    }
    statements.push_back("SET yb_non_ddl_txn_for_sys_tables_allowed = 1;");
    // Remaining statements don't fail if run on target.
    statements.push_back(Format(
        "DELETE FROM yb_xcluster_ddl_replication.replicated_ddls "
        "WHERE replicated_ddls.ddl_end_time < $0 "
        // Below skips the special row.
        "  AND yb_xcluster_ddl_replication.replicated_ddls.query_id != 1 "
        // Below checks that a matching row does NOT exist in the ddl_queue table.
        "  AND NOT EXISTS ( "
        "    SELECT 1 "
        "    FROM yb_xcluster_ddl_replication.ddl_queue "
        "    WHERE "
        "      ddl_queue.ddl_end_time = replicated_ddls.ddl_end_time "
        "      AND ddl_queue.query_id = replicated_ddls.query_id "
        "  );",
        too_old_time));

    // Run statements against the namespace best effort asynchronously.
    StdStatusCallback callback = [namespace_name, is_source, is_target](const Status& status) {
      WARN_NOT_OK(
          status, Format(
                      "Error cleaning up xCluster DDL replication tables for namespace $0",
                      namespace_name));
      VLOG(2) << "Finished DDL replication table cleanup for namespace " << namespace_name
              << "; is_source: " << is_source << " is_target: " << is_target;
    };
    WARN_NOT_OK(
        ExecutePgsqlStatements(
            namespace_name, statements, catalog_manager_,
            CoarseMonoClock::now() + MonoDelta::FromSeconds(60), std::move(callback)),
        "Cleanup of xCluster DDL replication tables failed");
  }
}

}  // namespace yb::master
