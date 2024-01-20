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
#include "yb/master/master_cluster.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/master.h"
#include "yb/master/post_tablet_create_task_base.h"
#include "yb/master/xcluster/add_table_to_xcluster_target_task.h"
#include "yb/master/xcluster/xcluster_config.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"

#include "yb/cdc/cdc_service.proxy.h"

DEFINE_RUNTIME_bool(disable_auto_add_index_to_xcluster, false,
    "Disables the automatic addition of indexes to transactional xCluster replication.");

DEFINE_test_flag(bool, enable_xcluster_api_v2, false, "Allow the usage of new xCluster APIs");

using namespace std::placeholders;

#define LOG_FUNC_AND_RPC \
  LOG_WITH_FUNC(INFO) << req->ShortDebugString() << ", from: " << RequestorString(rpc)

namespace yb::master {

XClusterManager::XClusterManager(
    Master* master, CatalogManager* catalog_manager, SysCatalogTable* sys_catalog)
    : master_(master), catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {
  xcluster_config_ = std::make_unique<XClusterConfig>(sys_catalog_);
}

XClusterManager::~XClusterManager() {}

void XClusterManager::Shutdown() {
  if (xcluster_safe_time_service_) {
    xcluster_safe_time_service_->Shutdown();
  }
}

Status XClusterManager::Init() {
  DCHECK(!xcluster_safe_time_service_);
  xcluster_safe_time_service_ = std::make_unique<XClusterSafeTimeService>(
      master_, catalog_manager_, master_->metric_registry());
  RETURN_NOT_OK(xcluster_safe_time_service_->Init());

  return Status::OK();
}

void XClusterManager::Clear() {
  xcluster_config_->ClearState();
  xcluster_safe_time_info_.Clear();
  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_.clear();
  }
}

Status XClusterManager::RunLoaders() {
  Clear();

  RETURN_NOT_OK(Load<XClusterConfigLoader>("xcluster configuration", *xcluster_config_));
  RETURN_NOT_OK(Load<XClusterSafeTimeLoader>("XCluster safe time", xcluster_safe_time_info_));

  RETURN_NOT_OK(Load<XClusterOutboundReplicationGroupLoader>(
      "XCluster outbound replication groups",
      std::function<Status(const std::string&, const SysXClusterOutboundReplicationGroupEntryPB&)>(
          std::bind(&XClusterManager::InsertOutboundReplicationGroup, this, _1, _2))));

  return Status::OK();
}

Status XClusterManager::InsertOutboundReplicationGroup(
    const std::string& replication_group_id,
    const SysXClusterOutboundReplicationGroupEntryPB& metadata) {
  xcluster::ReplicationGroupId rg_id(replication_group_id);
  std::lock_guard l(outbound_replication_group_map_mutex_);

  SCHECK(
      !outbound_replication_group_map_.contains(rg_id), IllegalState,
      "Duplicate xClusterOutboundReplicationGroup: $0", replication_group_id);

  auto outbound_replication_group = InitOutboundReplicationGroup(rg_id, metadata);

  outbound_replication_group_map_.emplace(
      replication_group_id, std::move(outbound_replication_group));

  return Status::OK();
}

XClusterOutboundReplicationGroup XClusterManager::InitOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const SysXClusterOutboundReplicationGroupEntryPB& metadata) {
  XClusterOutboundReplicationGroup::HelperFunctions helper_functions = {
      .get_namespace_id_func =
          [catalog_manager = catalog_manager_](
              YQLDatabase db_type, const NamespaceName& namespace_name) {
            return catalog_manager->GetNamespaceId(db_type, namespace_name);
          },
      .get_tables_func =
          [this](const NamespaceId& namespace_id) { return GetTablesToReplicate(namespace_id); },
      .bootstrap_tables_func =
          [this](const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline)
          -> Result<std::vector<xrepl::StreamId>> {
        return BootstrapTables(table_infos, deadline);
      },
      .delete_cdc_stream_func = [catalog_manager = catalog_manager_](
                                    const DeleteCDCStreamRequestPB& req,
                                    const LeaderEpoch& epoch) -> Result<DeleteCDCStreamResponsePB> {
        DeleteCDCStreamResponsePB resp;
        RETURN_NOT_OK(catalog_manager->DeleteCDCStream(&req, &resp, nullptr));
        return resp;
      },
      .upsert_to_sys_catalog_func =
          [sys_catalog = sys_catalog_](
              const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return sys_catalog->Upsert(epoch.leader_term, info);
          },
      .delete_from_sys_catalog_func =
          [sys_catalog = sys_catalog_](
              const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return sys_catalog->Delete(epoch.leader_term, info);
          },
  };

  return XClusterOutboundReplicationGroup(
      replication_group_id, metadata, std::move(helper_functions));
}

Result<XClusterOutboundReplicationGroup*> XClusterManager::GetOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id) {
  return const_cast<XClusterOutboundReplicationGroup*>(VERIFY_RESULT(
      const_cast<const XClusterManager*>(this)->GetOutboundReplicationGroup(replication_group_id)));
}

Result<const XClusterOutboundReplicationGroup*> XClusterManager::GetOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id) const {
  auto outbound_replication_group =
      FindOrNull(outbound_replication_group_map_, replication_group_id);
  SCHECK(
      outbound_replication_group, NotFound,
      Format("xClusterOutboundReplicationGroup $0 not found", replication_group_id));
  return outbound_replication_group;
}

template <template <class> class Loader, typename CatalogEntityWrapper>
Status XClusterManager::Load(const std::string& key, CatalogEntityWrapper& catalog_entity_wrapper) {
  Loader<CatalogEntityWrapper> loader(catalog_entity_wrapper);
  LOG_WITH_FUNC(INFO) << __func__ << ": Loading " << key << " into memory.";
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(&loader), "Failed while visiting " + key + " in sys catalog");
  return Status::OK();
}

template <typename Loader, typename CatalogEntityPB>
Status XClusterManager::Load(
    const std::string& key, std::function<Status(const std::string&, const CatalogEntityPB&)>
                                catalog_entity_inserter_func) {
  Loader loader(catalog_entity_inserter_func);
  LOG_WITH_FUNC(INFO) << __func__ << ": Loading " << key << " into memory.";
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(&loader), "Failed while visiting " + key + " in sys catalog");
  return Status::OK();
}

void XClusterManager::SysCatalogLoaded() {
  xcluster_safe_time_service_->ScheduleTaskIfNeeded();
}

void XClusterManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  if (on_disk_dump) {
    auto l = xcluster_safe_time_info_.LockForRead();
    if (!l->pb.safe_time_map().empty()) {
      *out << "XCluster Safe Time: " << l->pb.ShortDebugString() << "\n";
    }

    xcluster_config_->DumpState(out);
  }
}

Result<XClusterNamespaceToSafeTimeMap> XClusterManager::GetXClusterNamespaceToSafeTimeMap() const {
  XClusterNamespaceToSafeTimeMap result;
  auto l = xcluster_safe_time_info_.LockForRead();

  for (auto& [namespace_id, hybrid_time] : l->pb.safe_time_map()) {
    result[namespace_id] = HybridTime(hybrid_time);
  }
  return result;
}

Status XClusterManager::SetXClusterNamespaceToSafeTimeMap(
    const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) {
  auto l = xcluster_safe_time_info_.LockForWrite();
  auto& safe_time_map_pb = *l.mutable_data()->pb.mutable_safe_time_map();
  safe_time_map_pb.clear();
  for (auto& [namespace_id, hybrid_time] : safe_time_map) {
    safe_time_map_pb[namespace_id] = hybrid_time.ToUint64();
  }

  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Upsert(leader_term, &xcluster_safe_time_info_),
      "Updating XCluster safe time in sys-catalog");

  l.Commit();

  return Status::OK();
}

Result<HybridTime> XClusterManager::GetXClusterSafeTime(const NamespaceId& namespace_id) const {
  auto l = xcluster_safe_time_info_.LockForRead();
  SCHECK(
      l->pb.safe_time_map().count(namespace_id), NotFound,
      "XCluster safe time not found for namespace $0", namespace_id);

  return HybridTime(l->pb.safe_time_map().at(namespace_id));
}

void XClusterManager::CreateXClusterSafeTimeTableAndStartService() {
  WARN_NOT_OK(
      xcluster_safe_time_service_->CreateXClusterSafeTimeTableIfNotFound(),
      "Creation of XClusterSafeTime table failed");

  xcluster_safe_time_service_->ScheduleTaskIfNeeded();
}

Status XClusterManager::GetXClusterSafeTime(
    const GetXClusterSafeTimeRequestPB* req, GetXClusterSafeTimeResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  RETURN_NOT_OK_SET_CODE(
      xcluster_safe_time_service_->GetXClusterSafeTimeInfoFromMap(epoch, resp),
      MasterError(MasterErrorPB::INTERNAL_ERROR));

  // Also fill out the namespace_name for each entry.
  if (resp->namespace_safe_times_size()) {
    for (auto& safe_time_info : *resp->mutable_namespace_safe_times()) {
      const auto namespace_info = VERIFY_RESULT_OR_SET_CODE(
          catalog_manager_->FindNamespaceById(safe_time_info.namespace_id()),
          MasterError(MasterErrorPB::INTERNAL_ERROR));

      safe_time_info.set_namespace_name(namespace_info->name());
    }
  }

  return Status::OK();
}

Result<XClusterNamespaceToSafeTimeMap> XClusterManager::RefreshAndGetXClusterNamespaceToSafeTimeMap(
    const LeaderEpoch& epoch) {
  return xcluster_safe_time_service_->RefreshAndGetXClusterNamespaceToSafeTimeMap(epoch);
}

Status XClusterManager::PrepareDefaultXClusterConfig(int64_t term, bool recreate) {
  return xcluster_config_->PrepareDefault(term, recreate);
}

Status XClusterManager::GetXClusterConfigEntryPB(SysXClusterConfigEntryPB* config) const {
  *config = VERIFY_RESULT(xcluster_config_->GetXClusterConfigEntryPB());
  return Status::OK();
}

Status XClusterManager::GetMasterXClusterConfig(GetMasterXClusterConfigResponsePB* resp) {
  return GetXClusterConfigEntryPB(resp->mutable_xcluster_config());
}

Result<uint32_t> XClusterManager::GetXClusterConfigVersion() const {
  return xcluster_config_->GetVersion();
}

Status XClusterManager::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const {
  {
    auto l = xcluster_safe_time_info_.LockForRead();
    if (!l->pb.safe_time_map().empty()) {
      *resp->mutable_xcluster_namespace_to_safe_time() = l->pb.safe_time_map();
    }
  }

  return xcluster_config_->FillHeartbeatResponse(req, resp);
}

Status XClusterManager::RemoveStreamFromXClusterProducerConfig(
    const LeaderEpoch& epoch, const std::vector<CDCStreamInfo*>& streams) {
  return xcluster_config_->RemoveStreams(epoch, streams);
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

  auto xrepl_stream_ids = catalog_manager_->GetAllXreplStreamIds();
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

  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());

  std::lock_guard l(outbound_replication_group_map_mutex_);
  SCHECK(
      !outbound_replication_group_map_.contains(replication_group_id), IllegalState,
      "xClusterOutboundReplicationGroup $0 already exists", replication_group_id);

  std::vector<NamespaceName> namespace_names;
  for (const auto& namespace_name : req->namespace_names()) {
    namespace_names.emplace_back(namespace_name);
  }

  SysXClusterOutboundReplicationGroupEntryPB metadata;  // Empty metadata.
  auto outbound_replication_group = InitOutboundReplicationGroup(replication_group_id, metadata);

  // This will persist the group to SysCatalog.
  auto namespace_ids = VERIFY_RESULT(
      outbound_replication_group.AddNamespaces(epoch, namespace_names, rpc->GetClientDeadline()));

  outbound_replication_group_map_.emplace(
      replication_group_id, std::move(outbound_replication_group));

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

  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());
  std::lock_guard l(outbound_replication_group_map_mutex_);
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  auto namespace_id = VERIFY_RESULT(outbound_replication_group->AddNamespace(
      epoch, req->namespace_name(), rpc->GetClientDeadline()));

  resp->set_namespace_id(namespace_id);
  return Status::OK();
}

Status XClusterManager::XClusterRemoveNamespaceFromOutboundReplicationGroup(
    const XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB* req,
    XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;
  SCHECK(req->has_namespace_id(), InvalidArgument, "Namespace id must be specified");

  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());

  std::lock_guard l(outbound_replication_group_map_mutex_);
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->RemoveNamespace(epoch, req->namespace_id());
}

Status XClusterManager::XClusterDeleteOutboundReplicationGroup(
    const XClusterDeleteOutboundReplicationGroupRequestPB* req,
    XClusterDeleteOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG_FUNC_AND_RPC;

  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());

  std::lock_guard l(outbound_replication_group_map_mutex_);
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  // This will remove the group from SysCatalog.
  RETURN_NOT_OK(outbound_replication_group->Delete(epoch));

  outbound_replication_group_map_.erase(replication_group_id);

  return Status::OK();
}

Status XClusterManager::IsXClusterBootstrapRequired(
    const IsXClusterBootstrapRequiredRequestPB* req, IsXClusterBootstrapRequiredResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  SCHECK(req->has_namespace_id(), InvalidArgument, "Namespace id must be specified");

  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());

  SharedLock l(outbound_replication_group_map_mutex_);
  auto outbound_replication_group = VERIFY_RESULT(
      const_cast<const XClusterManager*>(this)->GetOutboundReplicationGroup(replication_group_id));

  auto bootstrap_required =
      VERIFY_RESULT(outbound_replication_group->IsBootstrapRequired(req->namespace_id()));

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
  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());

  SharedLock l(outbound_replication_group_map_mutex_);
  auto outbound_replication_group = VERIFY_RESULT(
      const_cast<const XClusterManager*>(this)->GetOutboundReplicationGroup(replication_group_id));

  std::vector<std::pair<TableName, PgSchemaName>> table_names;
  for (const auto& table_name : req->table_infos()) {
    table_names.emplace_back(table_name.table_name(), table_name.pg_schema_name());
  }

  auto ns_info = VERIFY_RESULT(
      outbound_replication_group->GetNamespaceCheckpointInfo(req->namespace_id(), table_names));

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

namespace {
// Should the table be part of xCluster replication?
bool ShouldReplicateTable(const TableInfoPtr& table) {
  if (table->GetTableType() != PGSQL_TABLE_TYPE || table->is_system()) {
    // Limited to ysql databases.
    // System tables are not replicated. DDLs statements will be replicated and executed on the
    // target universe to handle catalog changes.
    return false;
  }

  if (table->is_matview()) {
    // Materialized views need not be replicated, since they are not modified. Every time the view
    // is refreshed, new tablets are created. The same refresh can just run on the target universe.
    return false;
  }

  if (table->IsColocatedUserTable()) {
    // Only the colocated parent table needs to be replicated.
    return false;
  }

  return true;
}

}  // namespace

Result<std::vector<TableInfoPtr>> XClusterManager::GetTablesToReplicate(
    const NamespaceId& namespace_id) {
  auto table_infos = VERIFY_RESULT(catalog_manager_->GetTableInfosForNamespace(namespace_id));
  EraseIf([](const TableInfoPtr& table) { return !ShouldReplicateTable(table); }, &table_infos);
  return table_infos;
}

Result<std::vector<xrepl::StreamId>> XClusterManager::BootstrapTables(
    const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline) {
  cdc::BootstrapProducerRequestPB bootstrap_req;
  master::TSDescriptor* ts = nullptr;
  for (const auto& table_info : table_infos) {
    bootstrap_req.add_table_ids(table_info->id());

    if (!ts) {
      ts = VERIFY_RESULT(table_info->GetTablets().front()->GetLeader());
    }
  }
  SCHECK(ts, IllegalState, "No valid tserver found to bootstrap from");

  std::shared_ptr<cdc::CDCServiceProxy> proxy;
  RETURN_NOT_OK(ts->GetProxy(&proxy));

  cdc::BootstrapProducerResponsePB bootstrap_resp;
  rpc::RpcController bootstrap_rpc;
  bootstrap_rpc.set_deadline(deadline);

  // TODO(Hari): DB-9416 Make this async and atomic with upsert of the outbound replication group.
  RETURN_NOT_OK(proxy->BootstrapProducer(bootstrap_req, &bootstrap_resp, &bootstrap_rpc));
  if (bootstrap_resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(bootstrap_resp.error().status()));
  }

  SCHECK_EQ(
      table_infos.size(), bootstrap_resp.cdc_bootstrap_ids_size(), IllegalState,
      "Number of tables to bootstrap and number of bootstrap ids do not match");

  std::vector<xrepl::StreamId> stream_ids;
  for (const auto& bootstrap_id : bootstrap_resp.cdc_bootstrap_ids()) {
    stream_ids.emplace_back(VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_id)));
  }
  return stream_ids;
}

bool XClusterManager::ShouldAddTableToXClusterTarget(const TableInfo& table) const {
  if (FLAGS_disable_auto_add_index_to_xcluster) {
    return false;
  }

  const auto& pb = table.metadata().dirty().pb;

  // Only user created YSQL Indexes should be automatically added to xCluster replication.
  // For Colocated tables, this function will return false since it is only called on the parent
  // colocated table, which cannot be an index.
  if (pb.colocated() || pb.table_type() != PGSQL_TABLE_TYPE || !IsIndex(pb) ||
      !catalog_manager_->IsUserCreatedTable(table)) {
    return false;
  }

  auto indexed_table_stream_ids =
      catalog_manager_->GetXClusterConsumerStreamIdsForTable(table.id());
  if (!indexed_table_stream_ids.empty()) {
    VLOG(1) << "Index " << table.ToString() << " is already part of xcluster replication "
            << yb::ToString(indexed_table_stream_ids);
    return false;
  }

  auto indexed_table = catalog_manager_->GetTableInfo(GetIndexedTableId(pb));
  if (!indexed_table) {
    LOG(WARNING) << "Indexed table for " << table.id() << " not found";
    return false;
  }

  auto stream_ids = catalog_manager_->GetXClusterConsumerStreamIdsForTable(indexed_table->id());
  if (stream_ids.empty()) {
    return false;
  }

  if (stream_ids.size() > 1) {
    LOG(WARNING) << "Skipping adding index " << table.ToString()
                 << " to xCluster replication as the base table" << indexed_table->ToString()
                 << " is part of multiple replication streams " << yb::ToString(stream_ids);
    return false;
  }

  const auto& replication_group_id = stream_ids.begin()->first;
  auto cluster_config = catalog_manager_->ClusterConfig();
  {
    auto l = cluster_config->LockForRead();
    const auto& consumer_registry = l.data().pb.consumer_registry();
    // Only add if we are in a transactional replication with STANDBY mode.
    if (consumer_registry.role() != cdc::XClusterRole::STANDBY ||
        !consumer_registry.transactional()) {
      return false;
    }

    auto producer_entry =
        FindOrNull(consumer_registry.producer_map(), replication_group_id.ToString());
    if (producer_entry) {
      // Check if the table is already part of replication.
      // This is needed despite the check for GetXClusterConsumerStreamIdsForTable as the in-memory
      // list is not atomically updated.
      for (auto& stream_info : producer_entry->stream_map()) {
        if (stream_info.second.consumer_table_id() == table.id()) {
          VLOG(1) << "Index " << table.ToString() << " is already part of xcluster replication "
                  << stream_info.first;
          return false;
        }
      }
    }
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    auto universe = catalog_manager_->GetUniverseReplication(replication_group_id);
    if (universe == nullptr) {
      LOG(WARNING) << "Skip adding index " << table.ToString()
                   << " to xCluster replication as the universe " << replication_group_id
                   << " was not found";
      return false;
    }

    if (universe->LockForRead()->is_deleted_or_failed()) {
      LOG(WARNING) << "Skip adding index " << table.ToString()
                   << " to xCluster replication as the universe " << replication_group_id
                   << " is in a deleted or failed state";
      return false;
    }
  }

  return true;
}

std::vector<std::shared_ptr<PostTabletCreateTaskBase>> XClusterManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  if (!ShouldAddTableToXClusterTarget(*table_info)) {
    return {};
  }

  return {std::make_shared<AddTableToXClusterTargetTask>(
      *catalog_manager_, *master_->messenger(), table_info, epoch)};
}
}  // namespace yb::master
