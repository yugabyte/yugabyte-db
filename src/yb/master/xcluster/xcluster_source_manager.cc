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

#include "yb/master/xcluster/xcluster_source_manager.h"

#include "yb/cdc/cdc_service.proxy.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/xcluster/add_table_to_xcluster_source_task.h"
#include "yb/master/xcluster/xcluster_catalog_entity.h"

#include "yb/rpc/rpc_context.h"
#include "yb/util/scope_exit.h"

using namespace std::placeholders;

namespace yb::master {

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

XClusterSourceManager::XClusterSourceManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master), catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {}

XClusterSourceManager::~XClusterSourceManager() {}

void XClusterSourceManager::Clear() {
  std::lock_guard l(outbound_replication_group_map_mutex_);
  outbound_replication_group_map_.clear();
}

Status XClusterSourceManager::RunLoaders() {
  RETURN_NOT_OK(sys_catalog_.Load<XClusterOutboundReplicationGroupLoader>(
      "XCluster outbound replication groups",
      std::function<Status(const std::string&, const SysXClusterOutboundReplicationGroupEntryPB&)>(
          std::bind(&XClusterSourceManager::InsertOutboundReplicationGroup, this, _1, _2))));

  return Status::OK();
}

void XClusterSourceManager::SysCatalogLoaded() {}

void XClusterSourceManager::DumpState(std::ostream& out, bool on_disk_dump) const {
  if (!on_disk_dump) {
    return;
  }

  std::lock_guard l(outbound_replication_group_map_mutex_);
  if (outbound_replication_group_map_.empty()) {
    return;
  }
  out << "XClusterOutboundReplicationGroups:\n";

  for (const auto& [replication_group_id, outbound_rg] : outbound_replication_group_map_) {
    auto metadata = outbound_rg->GetMetadata();
    if (metadata.ok()) {
      out << "  ReplicationGroupId: " << replication_group_id
          << "\n  metadata: " << metadata->ShortDebugString() << "\n";
    }
    // else deleted.
  }
}

Status XClusterSourceManager::InsertOutboundReplicationGroup(
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

std::shared_ptr<XClusterOutboundReplicationGroup>
XClusterSourceManager::InitOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const SysXClusterOutboundReplicationGroupEntryPB& metadata) {
  XClusterOutboundReplicationGroup::HelperFunctions helper_functions = {
      .get_namespace_id_func =
          [&catalog_manager = catalog_manager_](
              YQLDatabase db_type, const NamespaceName& namespace_name) {
            return catalog_manager.GetNamespaceId(db_type, namespace_name);
          },
      .get_namespace_name_func =
          [&catalog_manager = catalog_manager_](const NamespaceId& namespace_id) {
            return catalog_manager.GetNamespaceName(namespace_id);
          },
      .get_tables_func =
          [this](const NamespaceId& namespace_id) { return GetTablesToReplicate(namespace_id); },
      .bootstrap_tables_func =
          [this](
              const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline,
              StreamCheckpointLocation checkpoint_location,
              const LeaderEpoch& epoch) -> Result<std::vector<xrepl::StreamId>> {
        return BootstrapTables(table_infos, deadline, checkpoint_location, epoch);
      },
      .delete_cdc_stream_func = [&catalog_manager = catalog_manager_](
                                    const DeleteCDCStreamRequestPB& req,
                                    const LeaderEpoch& epoch) -> Result<DeleteCDCStreamResponsePB> {
        DeleteCDCStreamResponsePB resp;
        RETURN_NOT_OK(catalog_manager.DeleteCDCStream(&req, &resp, nullptr));
        return resp;
      },
      .upsert_to_sys_catalog_func =
          [&sys_catalog = sys_catalog_](
              const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return sys_catalog.Upsert(epoch.leader_term, info);
          },
      .delete_from_sys_catalog_func =
          [&sys_catalog = sys_catalog_](
              const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return sys_catalog.Delete(epoch.leader_term, info);
          },
  };

  return std::make_shared<XClusterOutboundReplicationGroup>(
      replication_group_id, metadata, std::move(helper_functions));
}

Result<std::shared_ptr<XClusterOutboundReplicationGroup>>
XClusterSourceManager::GetOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id) const {
  SharedLock l(outbound_replication_group_map_mutex_);
  auto outbound_replication_group =
      FindOrNull(outbound_replication_group_map_, replication_group_id);
  // Value can be nullptr during create, before writing to sys_catalog. These should also be treated
  // as NotFound.
  SCHECK(
      outbound_replication_group && *outbound_replication_group, NotFound,
      Format("xClusterOutboundReplicationGroup $0 not found", replication_group_id));
  return *outbound_replication_group;
}

Result<std::vector<TableInfoPtr>> XClusterSourceManager::GetTablesToReplicate(
    const NamespaceId& namespace_id) {
  auto table_infos = VERIFY_RESULT(catalog_manager_.GetTableInfosForNamespace(namespace_id));
  EraseIf([](const TableInfoPtr& table) { return !ShouldReplicateTable(table); }, &table_infos);
  return table_infos;
}

Result<std::vector<xrepl::StreamId>> XClusterSourceManager::BootstrapTables(
    const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline,
    StreamCheckpointLocation checkpoint_location, const LeaderEpoch& epoch) {
  if (checkpoint_location == StreamCheckpointLocation::kOpId0) {
    std::vector<xrepl::StreamId> stream_ids;
    for (const auto& table_info : table_infos) {
      const auto& table_id = table_info->id();
      master::CreateCDCStreamRequestPB create_stream_req;
      master::CreateCDCStreamResponsePB create_stream_resp;
      create_stream_req.set_table_id(table_info->id());

      // TODO: #20769 Apply appropriate WAL retention on the table.
      RETURN_NOT_OK(catalog_manager_.CreateNewXReplStream(
          create_stream_req, CreateNewCDCStreamMode::kXClusterTableIds, {table_id},
          /*namespace_id=*/std::nullopt, &create_stream_resp, epoch, /*rpc=*/nullptr));

      if (create_stream_resp.has_error()) {
        return StatusFromPB(create_stream_resp.error().status());
      }
      stream_ids.emplace_back(
          VERIFY_RESULT(xrepl::StreamId::FromString(create_stream_resp.stream_id())));
    }

    return stream_ids;
  }

  LOG_IF(DFATAL, checkpoint_location != StreamCheckpointLocation::kCurrentEndOfWAL)
      << "Not implemented yet. Checkpoint location: " << checkpoint_location;

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

std::vector<std::shared_ptr<PostTabletCreateTaskBase>>
XClusterSourceManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  if (!ShouldReplicateTable(table_info)) {
    return {};
  }

  // Create a AddTableToXClusterSourceTask for each outbound replication group that has the
  // tables namespace.
  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> tasks;
  const auto namespace_id = table_info->namespace_id();
  SharedLock l(outbound_replication_group_map_mutex_);
  for (const auto& [_, outbound_replication_group] : outbound_replication_group_map_) {
    if (outbound_replication_group && outbound_replication_group->HasNamespace(namespace_id)) {
      tasks.emplace_back(std::make_shared<AddTableToXClusterSourceTask>(
          outbound_replication_group, catalog_manager_, *master_.messenger(), table_info, epoch));
    }
  }

  return tasks;
}

Result<std::vector<NamespaceId>> XClusterSourceManager::CreateOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<NamespaceName>& namespace_names, const LeaderEpoch& epoch,
    CoarseTimePoint deadline) {
  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    SCHECK(
        !outbound_replication_group_map_.contains(replication_group_id), IllegalState,
        "xClusterOutboundReplicationGroup $0 already exists", replication_group_id);

    // Insert a temporary nullptr in order to reserve the Id.
    outbound_replication_group_map_.emplace(replication_group_id, nullptr);
  }

  // If we fail anywhere after this we need to return the Id we have reserved.
  auto se = ScopeExit([this, &replication_group_id]() {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_.erase(replication_group_id);
  });

  SysXClusterOutboundReplicationGroupEntryPB metadata;  // Empty metadata.
  auto outbound_replication_group = InitOutboundReplicationGroup(replication_group_id, metadata);

  // This will persist the group to SysCatalog.
  auto namespace_ids =
      VERIFY_RESULT(outbound_replication_group->AddNamespaces(epoch, namespace_names, deadline));

  se.Cancel();
  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_[replication_group_id] = std::move(outbound_replication_group);
  }

  return namespace_ids;
}

Result<NamespaceId> XClusterSourceManager::AddNamespaceToOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceName& namespace_name,
    const LeaderEpoch& epoch, CoarseTimePoint deadline) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->AddNamespace(epoch, namespace_name, deadline);
}

Status XClusterSourceManager::RemoveNamespaceFromOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->RemoveNamespace(epoch, namespace_id);
}

Status XClusterSourceManager::DeleteOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  // This will remove the group from SysCatalog.
  RETURN_NOT_OK(outbound_replication_group->Delete(epoch));

  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_.erase(replication_group_id);
  }

  return Status::OK();
}

Result<std::optional<bool>> XClusterSourceManager::IsBootstrapRequired(
    const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceId& namespace_id) const {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->IsBootstrapRequired(namespace_id);
}

Result<std::optional<NamespaceCheckpointInfo>> XClusterSourceManager::GetXClusterStreams(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    std::vector<std::pair<TableName, PgSchemaName>> opt_table_names) const {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->GetNamespaceCheckpointInfo(namespace_id, opt_table_names);
}

Status XClusterSourceManager::CreateXClusterReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  auto source_master_addresses = VERIFY_RESULT(catalog_manager_.GetMasterAddressHostPorts());
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  return outbound_replication_group->CreateXClusterReplication(
      source_master_addresses, target_master_addresses, epoch);
}

Result<IsOperationDoneResult> XClusterSourceManager::IsCreateXClusterReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  return outbound_replication_group->IsCreateXClusterReplicationDone(
      target_master_addresses, epoch);
}

}  // namespace yb::master
