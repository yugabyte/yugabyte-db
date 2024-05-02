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

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/master/xcluster/add_table_to_xcluster_source_task.h"
#include "yb/master/xcluster/xcluster_catalog_entity.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group_tasks.h"

#include "yb/rpc/rpc_context.h"
#include "yb/util/scope_exit.h"

DECLARE_uint32(cdc_wal_retention_time_secs);

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

Status XClusterSourceManager::Init() {
  async_task_factory_ = std::make_unique<XClusterOutboundReplicationGroupTaskFactory>(
      catalog_manager_.GetValidateEpochFunc(), *catalog_manager_.AsyncTaskPool(),
      *master_.messenger());

  cdc_state_table_ = std::make_unique<cdc::CDCStateTable>(master_.cdc_state_client_future());
  return Status::OK();
}

void XClusterSourceManager::Clear() {
  CatalogEntityWithTasks::CloseAbortAndWaitForAllTasks(GetAllOutboundGroups());

  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_.clear();
  }
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

  auto all_outbound_groups = GetAllOutboundGroups();
  if (all_outbound_groups.empty()) {
    return;
  }
  out << "XClusterOutboundReplicationGroups:\n";

  for (const auto& outbound_rg : all_outbound_groups) {
    auto metadata = outbound_rg->GetMetadata();
    if (metadata.ok()) {
      out << "  ReplicationGroupId: " << outbound_rg->Id()
          << "\n  metadata: " << metadata->ShortDebugString() << "\n";
    }
    // else deleted.
  }
}

std::vector<std::shared_ptr<XClusterOutboundReplicationGroup>>
XClusterSourceManager::GetAllOutboundGroups() const {
  std::vector<std::shared_ptr<XClusterOutboundReplicationGroup>> result;
  SharedLock l(outbound_replication_group_map_mutex_);
  for (auto& [_, outbound_rg] : outbound_replication_group_map_) {
    if (outbound_rg) {
      result.emplace_back(outbound_rg);
    }
  }

  return result;
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
      .get_tables_func = std::bind(&XClusterSourceManager::GetTablesToReplicate, this, _1),
      .create_xcluster_streams_func =
          std::bind(&XClusterSourceManager::CreateNewXClusterStreams, this, _1, _2),
      .checkpoint_xcluster_streams_func =
          std::bind(&XClusterSourceManager::CheckpointXClusterStreams, this, _1, _2, _3, _4, _5),
      .delete_cdc_stream_func = [&catalog_manager = catalog_manager_](
                                    const DeleteCDCStreamRequestPB& req,
                                    const LeaderEpoch& epoch) -> Result<DeleteCDCStreamResponsePB> {
        DeleteCDCStreamResponsePB resp;
        RETURN_NOT_OK(catalog_manager.DeleteCDCStream(&req, &resp, nullptr));
        return resp;
      },
      .upsert_to_sys_catalog_func =
          [&sys_catalog = sys_catalog_](
              const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info,
              const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
            return sys_catalog.Upsert(epoch.leader_term, info, streams);
          },
      .delete_from_sys_catalog_func =
          [&sys_catalog = sys_catalog_](
              const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return sys_catalog.Delete(epoch.leader_term, info);
          },
  };

  return std::make_shared<XClusterOutboundReplicationGroup>(
      replication_group_id, metadata, std::move(helper_functions),
      catalog_manager_.GetTasksTracker(), *async_task_factory_);
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
  for (const auto& outbound_replication_group : GetAllOutboundGroups()) {
    if (outbound_replication_group->HasNamespace(namespace_id)) {
      tasks.emplace_back(std::make_shared<AddTableToXClusterSourceTask>(
          outbound_replication_group, catalog_manager_, *master_.messenger(), table_info, epoch));
    }
  }

  return tasks;
}

std::optional<uint32> XClusterSourceManager::GetDefaultWalRetentionSec(
    const NamespaceId& namespace_id) const {
  if (namespace_id == kSystemNamespaceId) {
    return std::nullopt;
  }

  for (const auto& outbound_replication_group : GetAllOutboundGroups()) {
    if (outbound_replication_group->HasNamespace(namespace_id)) {
      return FLAGS_cdc_wal_retention_time_secs;
    }
  }

  return std::nullopt;
}

Result<std::vector<NamespaceId>> XClusterSourceManager::CreateOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<NamespaceName>& namespace_names, const LeaderEpoch& epoch) {
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
      VERIFY_RESULT(outbound_replication_group->AddNamespaces(epoch, namespace_names));

  se.Cancel();
  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_[replication_group_id] = std::move(outbound_replication_group);
  }

  return namespace_ids;
}

Result<NamespaceId> XClusterSourceManager::AddNamespaceToOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceName& namespace_name,
    const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->AddNamespace(epoch, namespace_name);
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

class XClusterCreateStreamContextImpl : public XClusterCreateStreamsContext {
 public:
  explicit XClusterCreateStreamContextImpl(CatalogManager& catalog_manager)
      : catalog_manager_(catalog_manager) {}
  virtual ~XClusterCreateStreamContextImpl() { Rollback(); }

  void Commit() override {
    for (auto& stream : streams_) {
      stream->mutable_metadata()->CommitMutation();
    }
    streams_.clear();
  }

  void Rollback() {
    for (const auto& stream : streams_) {
      catalog_manager_.ReleaseAbandonedXReplStream(stream->StreamId());
    }
  }

  CatalogManager& catalog_manager_;
};

Result<std::unique_ptr<XClusterCreateStreamsContext>>
XClusterSourceManager::CreateNewXClusterStreams(
    const std::vector<TableId>& table_ids, const LeaderEpoch& epoch) {
  const auto state = SysCDCStreamEntryPB::ACTIVE;
  const bool transactional = true;

  RETURN_NOT_OK(catalog_manager_.CreateCdcStateTableIfNotFound(epoch));

  auto create_context = std::make_unique<XClusterCreateStreamContextImpl>(catalog_manager_);

  for (const auto& table_id : table_ids) {
    auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));
    SCHECK(
        table_info->LockForRead()->visible_to_client(), NotFound, "Table does not exist", table_id);

    VLOG(1) << "Creating xcluster streams for table: " << table_id;

    auto stream = VERIFY_RESULT(catalog_manager_.InitNewXReplStream());
    auto& metadata = stream->mutable_metadata()->mutable_dirty()->pb;
    metadata.add_table_id(table_id);
    metadata.set_transactional(transactional);

    std::unordered_map<std::string, std::string> options;
    auto record_type_option = metadata.add_options();
    record_type_option->set_key(cdc::kRecordType);
    record_type_option->set_value(CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
    auto record_format_option = metadata.add_options();
    record_format_option->set_key(cdc::kRecordFormat);
    record_format_option->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));
    metadata.set_state(state);

    create_context->streams_.emplace_back(std::move(stream));
  }

  return create_context;
}

Status XClusterSourceManager::CheckpointXClusterStreams(
    const std::vector<std::pair<TableId, xrepl::StreamId>>& table_streams,
    StreamCheckpointLocation checkpoint_location, const LeaderEpoch& epoch,
    bool check_if_bootstrap_required, std::function<void(Result<bool>)> user_callback) {
  switch (checkpoint_location) {
    case StreamCheckpointLocation::kOpId0:
      SCHECK(!check_if_bootstrap_required, InvalidArgument, "Bootstrap is not required for OpId0");
      return CheckpointStreamsToOp0(
          table_streams, [user_callback = std::move(user_callback)](const Status& status) {
            if (!status.ok()) {
              user_callback(status);
              return;
            }

            // Bootstrap is not required for OpId0.
            user_callback(false);
          });
    case StreamCheckpointLocation::kCurrentEndOfWAL:
      return CheckpointStreamsToEndOfWAL(
          table_streams, epoch, check_if_bootstrap_required, user_callback);
  }

  FATAL_INVALID_ENUM_VALUE(StreamCheckpointLocation, checkpoint_location);
}

Status XClusterSourceManager::CheckpointStreamsToOp0(
    const std::vector<std::pair<TableId, xrepl::StreamId>>& table_streams,
    StdStatusCallback user_callback) {
  std::vector<cdc::CDCStateTableEntry> entries;
  for (const auto& [table_id, stream_id] : table_streams) {
    auto table = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));
    for (const auto& tablet : table->GetTablets()) {
      cdc::CDCStateTableEntry entry(tablet->id(), stream_id);
      entry.checkpoint = OpId().Min();
      entry.last_replication_time = GetCurrentTimeMicros();
      entries.push_back(std::move(entry));
    }
  }

  return cdc_state_table_->InsertEntriesAsync(entries, std::move(user_callback));
}

Status XClusterSourceManager::CheckpointStreamsToEndOfWAL(
    const std::vector<std::pair<TableId, xrepl::StreamId>>& table_streams, const LeaderEpoch& epoch,
    bool check_if_bootstrap_required, std::function<void(Result<bool>)> user_callback) {
  cdc::BootstrapProducerRequestPB bootstrap_req;
  bootstrap_req.set_check_if_bootstrap_required(check_if_bootstrap_required);

  master::TSDescriptor* ts_desc = nullptr;
  for (const auto& [table_id, stream_id] : table_streams) {
    VLOG(1) << "Checkpointing xcluster stream " << stream_id << " of table " << table_id;

    // Set WAL retention here instead of during stream creation as we are processing smaller batches
    // of tables during the checkpoint phase whereas we create all streams in one batch to reduce
    // master IOs.
    RETURN_NOT_OK(catalog_manager_.SetWalRetentionForTable(table_id, /*rpc=*/nullptr, epoch));

    auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));
    SCHECK(
        table_info->LockForRead()->visible_to_client(), NotFound, "Table does not exist", table_id);
    bootstrap_req.add_table_ids(table_info->id());
    bootstrap_req.add_xrepl_stream_ids(stream_id.ToString());

    if (!ts_desc) {
      ts_desc = VERIFY_RESULT(table_info->GetTablets().front()->GetLeader());
    }
  }
  SCHECK(ts_desc, IllegalState, "No valid tserver found to bootstrap from");

  std::shared_ptr<cdc::CDCServiceProxy> proxy;
  RETURN_NOT_OK(ts_desc->GetProxy(&proxy));

  auto bootstrap_resp = std::make_shared<cdc::BootstrapProducerResponsePB>();
  auto bootstrap_rpc = std::make_shared<rpc::RpcController>();

  auto process_response = [bootstrap_resp, bootstrap_rpc]() -> Result<bool> {
    RETURN_NOT_OK(bootstrap_rpc->status());
    if (bootstrap_resp->has_error()) {
      return StatusFromPB(bootstrap_resp->error().status());
    }

    SCHECK_EQ(
        bootstrap_resp->cdc_bootstrap_ids_size(), 0, IllegalState,
        "No new streams should have been created");

    return bootstrap_resp->bootstrap_required();
  };

  proxy->BootstrapProducerAsync(
      bootstrap_req, bootstrap_resp.get(), bootstrap_rpc.get(),
      [user_callback = std::move(user_callback), process_response = std::move(process_response)]() {
        user_callback(process_response());
      });

  return Status::OK();
}

}  // namespace yb::master
