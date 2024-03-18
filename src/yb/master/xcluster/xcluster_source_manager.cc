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

#include "yb/util/scope_exit.h"

DECLARE_uint32(cdc_wal_retention_time_secs);
DECLARE_bool(TEST_disable_cdc_state_insert_on_setup);

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
  {
    std::lock_guard l(tables_to_stream_map_mutex_);
    tables_to_stream_map_.clear();
  }
  {
    std::lock_guard l(retained_hidden_tablets_mutex_);
    retained_hidden_tablets_.clear();
  }
}

Status XClusterSourceManager::RunLoaders(const TabletInfos& hidden_tablets) {
  RETURN_NOT_OK(sys_catalog_.Load<XClusterOutboundReplicationGroupLoader>(
      "XCluster outbound replication groups",
      std::function<Status(const std::string&, const SysXClusterOutboundReplicationGroupEntryPB&)>(
          std::bind(&XClusterSourceManager::InsertOutboundReplicationGroup, this, _1, _2))));

  for (const auto& hidden_tablet : hidden_tablets) {
    TabletDeleteRetainerInfo delete_retainer;
    PopulateTabletDeleteRetainerInfoForTabletDrop(*hidden_tablet, delete_retainer);
    RecordHiddenTablets({hidden_tablet}, delete_retainer);
  }

  return Status::OK();
}

void XClusterSourceManager::RecordOutboundStream(
    const CDCStreamInfoPtr& stream, const TableId& table_id) {
  std::lock_guard l(tables_to_stream_map_mutex_);
  auto& stream_list = tables_to_stream_map_[table_id];

  // Assert no duplicates exist in Debug, and remove duplicates in retail.
  EraseIf(
      [&stream](const auto& existing_stream) {
        DCHECK_NE(existing_stream->StreamId(), stream->StreamId())
            << "Stream " << existing_stream->StreamId() << " already exists " << existing_stream;
        return existing_stream->StreamId() == stream->StreamId();
      },
      &stream_list);

  stream_list.push_back(stream);
}

void XClusterSourceManager::CleanUpXReplStream(const CDCStreamInfo& stream) {
  std::lock_guard l(tables_to_stream_map_mutex_);
  for (auto& id : stream.table_id()) {
    if (tables_to_stream_map_.contains(id)) {
      EraseIf(
          [&stream](const auto& existing_stream) {
            return existing_stream->StreamId() == stream.StreamId();
          },
          &tables_to_stream_map_[id]);

      if (tables_to_stream_map_[id].empty()) {
        tables_to_stream_map_.erase(id);
      }
    }
  }
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
          std::bind(&XClusterSourceManager::CreateStreamsForDbScoped, this, _1, _2),
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
  explicit XClusterCreateStreamContextImpl(
      CatalogManager& catalog_manager, XClusterSourceManager& xcluster_manager)
      : catalog_manager_(catalog_manager), xcluster_manager_(xcluster_manager) {}
  virtual ~XClusterCreateStreamContextImpl() { Rollback(); }

  void Commit() override {
    for (auto& stream : streams_) {
      stream->mutable_metadata()->CommitMutation();
    }
    streams_.clear();
  }

  void Rollback() {
    for (const auto& stream : streams_) {
      xcluster_manager_.CleanUpXReplStream(*stream);
      catalog_manager_.ReleaseAbandonedXReplStream(stream->StreamId());
    }
  }

  CatalogManager& catalog_manager_;
  XClusterSourceManager& xcluster_manager_;
  std::vector<TableId> table_ids;
};

Result<std::unique_ptr<XClusterCreateStreamsContext>>
XClusterSourceManager::CreateStreamsForDbScoped(
    const std::vector<TableId>& table_ids, const LeaderEpoch& epoch) {
  google::protobuf::RepeatedPtrField<::yb::master::CDCStreamOptionsPB> options;
  auto record_type_option = options.Add();
  record_type_option->set_key(cdc::kRecordType);
  record_type_option->set_value(CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
  auto record_format_option = options.Add();
  record_format_option->set_key(cdc::kRecordFormat);
  record_format_option->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));

  return CreateStreamsInternal(
      table_ids, SysCDCStreamEntryPB::ACTIVE, options, /*transactional=*/true, epoch);
}

Result<std::unique_ptr<XClusterCreateStreamsContext>> XClusterSourceManager::CreateStreamsInternal(
    const std::vector<TableId>& table_ids, SysCDCStreamEntryPB::State state,
    const google::protobuf::RepeatedPtrField<::yb::master::CDCStreamOptionsPB>& options,
    bool transactional, const LeaderEpoch& epoch) {
  SCHECK(
      state == SysCDCStreamEntryPB::ACTIVE || state == SysCDCStreamEntryPB::INITIATED,
      InvalidArgument, "Stream state must be either ACTIVE or INITIATED");

  RETURN_NOT_OK(catalog_manager_.CreateCdcStateTableIfNotFound(epoch));

  auto create_context = std::make_unique<XClusterCreateStreamContextImpl>(catalog_manager_, *this);

  for (const auto& table_id : table_ids) {
    auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));
    SCHECK(
        table_info->LockForRead()->visible_to_client(), NotFound, "Table does not exist", table_id);

    VLOG(1) << "Creating xcluster streams for table: " << table_id;

    auto stream = VERIFY_RESULT(catalog_manager_.InitNewXReplStream());
    auto& metadata = stream->mutable_metadata()->mutable_dirty()->pb;
    metadata.add_table_id(table_id);
    metadata.set_transactional(transactional);

    metadata.mutable_options()->CopyFrom(options);
    metadata.set_state(state);

    RecordOutboundStream(stream, table_id);

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
    auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));
    SCHECK(
        table_info->LockForRead()->visible_to_client(), NotFound, "Table does not exist", table_id);

    RETURN_NOT_OK(catalog_manager_.SetXReplWalRetentionForTable(table_info, epoch));
    RETURN_NOT_OK(catalog_manager_.BackfillMetadataForXRepl(table_info, epoch));

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

void XClusterSourceManager::PopulateTabletDeleteRetainerInfoForTabletDrop(
    const TabletInfo& tablet_info, TabletDeleteRetainerInfo& delete_retainer) const {
  // For xCluster , the only time we try to delete a single tablet that is part of an active stream
  // is during tablet splitting, where we need to keep the parent tablet around until we have
  // replicated its SPLIT_OP record.
  {
    auto tablet_lock = tablet_info.LockForRead();
    if (tablet_lock->pb.split_tablet_ids_size() < 2) {
      return;
    }
  }
  delete_retainer.active_xcluster = IsTableReplicated(tablet_info.table()->id());
}

bool XClusterSourceManager::IsTableReplicated(const TableId& table_id) const {
  auto streams = GetStreamsForTable(table_id);

  // Check that at least one of these streams is active (ie not being deleted).
  return std::any_of(streams.begin(), streams.end(), [](const auto& stream) {
    return !stream->LockForRead()->is_deleting();
  });
}

bool XClusterSourceManager::DoesTableHaveAnyBootstrappingStream(const TableId& table_id) const {
  auto streams = GetStreamsForTable(table_id);

  // Check that at least one of these streams is bootstrapping.
  return std::any_of(streams.begin(), streams.end(), [](const auto& stream) {
    return stream->LockForRead()->pb.state() == SysCDCStreamEntryPB::INITIATED;
  });
}

void XClusterSourceManager::RecordHiddenTablets(
    const TabletInfos& hidden_tablets, const TabletDeleteRetainerInfo& delete_retainer) {
  if (!delete_retainer.active_xcluster) {
    return;
  }

  std::lock_guard l(retained_hidden_tablets_mutex_);

  for (const auto& hidden_tablet : hidden_tablets) {
    auto tablet_lock = hidden_tablet->LockForRead();
    auto& tablet_pb = tablet_lock->pb;
    HiddenTabletInfo info{
        .table_id = hidden_tablet->table()->id(),
        .parent_tablet_id = tablet_pb.split_parent_tablet_id(),
        .split_tablets = {tablet_pb.split_tablet_ids(0), tablet_pb.split_tablet_ids(1)},
    };

    retained_hidden_tablets_.emplace(hidden_tablet->id(), std::move(info));
  }
}

bool XClusterSourceManager::ShouldRetainHiddenTablet(const TabletInfo& tablet_info) const {
  SharedLock l(retained_hidden_tablets_mutex_);
  return retained_hidden_tablets_.contains(tablet_info.tablet_id());
}

Status XClusterSourceManager::DoProcessHiddenTablets() {
  decltype(retained_hidden_tablets_) hidden_tablets;
  {
    SharedLock lock(retained_hidden_tablets_mutex_);
    if (retained_hidden_tablets_.empty()) {
      return Status::OK();
    }
    hidden_tablets = retained_hidden_tablets_;
  }

  std::unordered_set<TabletId> tablets_to_delete;
  std::vector<cdc::CDCStateTableKey> entries_to_delete;

  // Check cdc_state table to see if the children tablets being polled.
  for (auto& [tablet_id, hidden_tablet] : hidden_tablets) {
    // If our parent tablet is still around, need to process that one first.
    const auto parent_tablet_id = hidden_tablet.parent_tablet_id;
    if (!parent_tablet_id.empty() && hidden_tablets.contains(parent_tablet_id)) {
      continue;
    }

    // For each hidden tablet, check if for each stream we have an entry in the mapping for them.
    const auto streams = GetStreamsForTable(hidden_tablet.table_id);

    std::vector<xrepl::StreamId> tablet_streams_to_delete;
    size_t count_streams_already_deleted = 0;
    for (const auto& stream : streams) {
      const auto& stream_id = stream->StreamId();
      const cdc::CDCStateTableKey entry_key{tablet_id, stream_id};

      // Check parent entry, if it doesn't exist, then it was already deleted.
      // If the entry for the tablet does not exist, then we can go ahead with deletion of the
      // tablet.
      auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
          entry_key, cdc::CDCStateTableEntrySelector().IncludeLastReplicationTime()));

      // This means we already deleted the entry for this stream in a previous iteration.
      if (!entry_opt) {
        VLOG(2) << "Did not find an entry corresponding to the tablet: " << tablet_id
                << ", and stream: " << stream_id << ", in the cdc_state table";
        ++count_streams_already_deleted;
        continue;
      }

      if (!entry_opt->last_replication_time) {
        // Still haven't processed this tablet since timestamp is null, no need to check children.
        break;
      }

      // This means there was an active stream for the source tablet. In which case if we see
      // that all children tablet entries have started streaming, we can delete the parent tablet.
      bool found_all_children = true;
      for (auto& child_tablet_id : hidden_tablet.split_tablets) {
        auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
            {child_tablet_id, stream_id},
            cdc::CDCStateTableEntrySelector().IncludeLastReplicationTime()));

        if (!entry_opt || !entry_opt->last_replication_time) {
          // Check checkpoint to ensure that there has been a poll for this tablet, or if the split
          // has been reported.
          VLOG(2) << "The stream: " << stream_id
                  << ", has not started polling for the child tablet: " << child_tablet_id
                  << ".Hence we will not delete the hidden parent tablet: " << tablet_id;
          found_all_children = false;
          break;
        }
      }
      if (found_all_children) {
        LOG(INFO) << "Deleting tablet " << tablet_id << " from stream " << stream_id
                  << ". Reason: Consumer finished processing parent tablet after split.";
        tablet_streams_to_delete.push_back(stream_id);
        entries_to_delete.emplace_back(cdc::CDCStateTableKey{tablet_id, stream_id});
      }
    }

    if (tablet_streams_to_delete.size() + count_streams_already_deleted == streams.size()) {
      tablets_to_delete.insert(tablet_id);
    }
  }

  RETURN_NOT_OK_PREPEND(
      cdc_state_table_->DeleteEntries(entries_to_delete),
      "Error deleting cdc stream rows from cdc_state table");

  // Delete tablets from retained_hidden_tablets_, CatalogManager::CleanupHiddenTablets will do the
  // actual tablet deletion.
  {
    std::lock_guard l(retained_hidden_tablets_mutex_);
    for (const auto& tablet_id : tablets_to_delete) {
      retained_hidden_tablets_.erase(tablet_id);
    }
  }

  return Status::OK();
}

std::vector<CDCStreamInfoPtr> XClusterSourceManager::GetStreamsForTable(
    const TableId& table_id) const {
  SharedLock lock(tables_to_stream_map_mutex_);
  auto stream_list = FindOrNull(tables_to_stream_map_, table_id);
  if (!stream_list) {
    return {};
  }
  return *stream_list;
}

Result<xrepl::StreamId> XClusterSourceManager::CreateNewXClusterStreamForTable(
    const TableId& table_id, bool transactional,
    const std::optional<SysCDCStreamEntryPB::State>& initial_state,
    const google::protobuf::RepeatedPtrField<::yb::master::CDCStreamOptionsPB>& options,
    const LeaderEpoch& epoch) {
  auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));

  RETURN_NOT_OK(catalog_manager_.SetXReplWalRetentionForTable(table_info, epoch));
  RETURN_NOT_OK(catalog_manager_.BackfillMetadataForXRepl(table_info, epoch));

  const auto state = initial_state ? *initial_state : SysCDCStreamEntryPB::ACTIVE;
  auto create_context =
      VERIFY_RESULT(CreateStreamsInternal({table_id}, state, options, transactional, epoch));
  RSTATUS_DCHECK_EQ(
      create_context->streams_.size(), 1, IllegalState,
      "Unexpected Expected number of streams created");
  auto stream_id = create_context->streams_.front()->StreamId();

  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(epoch, create_context->streams_),
      "Failed to insert xCluster stream into sys-catalog");
  create_context->Commit();

  // Skip if disable_cdc_state_insert_on_setup is set.
  // If this is a bootstrap (initial state not ACTIVE), let the BootstrapProducer logic take care of
  // populating entries in cdc_state.
  if (PREDICT_FALSE(FLAGS_TEST_disable_cdc_state_insert_on_setup) ||
      state != master::SysCDCStreamEntryPB::ACTIVE) {
    return stream_id;
  }

  Synchronizer sync;
  RETURN_NOT_OK(CheckpointStreamsToOp0({{table_id, stream_id}}, sync.AsStatusFunctor()));
  RETURN_NOT_OK(sync.Wait());

  return stream_id;
}

}  // namespace yb::master
