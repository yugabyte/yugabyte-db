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
#include "yb/cdc/xcluster_types.h"
#include "yb/client/xcluster_client.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_status.h"
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

void XClusterSourceManager::CleanupStreamFromMaps(const CDCStreamInfo& stream) {
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

void XClusterSourceManager::SysCatalogLoaded(const LeaderEpoch& epoch) {
  for (const auto& outbound_rg : GetAllOutboundGroups()) {
    outbound_rg->StartPostLoadTasks(epoch);
  }
}

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
      .get_namespace_func =
          [&catalog_manager = catalog_manager_](const NamespaceIdentifierPB& ns_identifier) {
            return catalog_manager.FindNamespace(ns_identifier);
          },
      .get_tables_func =
          [&catalog_manager = catalog_manager_](const NamespaceId& namespace_id) {
            return GetTablesEligibleForXClusterReplication(catalog_manager, namespace_id);
          },
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

std::vector<std::shared_ptr<PostTabletCreateTaskBase>>
XClusterSourceManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  if (!IsTableEligibleForXClusterReplication(*table_info)) {
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

Status XClusterSourceManager::CreateOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<NamespaceId>& namespace_ids, const LeaderEpoch& epoch) {
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
  RETURN_NOT_OK(outbound_replication_group->AddNamespaces(epoch, namespace_ids));

  se.Cancel();
  {
    std::lock_guard l(outbound_replication_group_map_mutex_);
    outbound_replication_group_map_[replication_group_id] = std::move(outbound_replication_group);
  }

  return Status::OK();
}

Status XClusterSourceManager::AddNamespaceToOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  RETURN_NOT_OK(outbound_replication_group->AddNamespace(epoch, namespace_id));

  return Status::OK();
}

Status XClusterSourceManager::RemoveNamespaceFromOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  return outbound_replication_group->RemoveNamespace(epoch, namespace_id, target_master_addresses);
}

Status XClusterSourceManager::DeleteOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));

  // This will remove the group from SysCatalog.
  RETURN_NOT_OK(outbound_replication_group->Delete(target_master_addresses, epoch));

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

Status XClusterSourceManager::AddNamespaceToTarget(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& target_master_addresses, const NamespaceId& source_namespace_id,
    const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  return outbound_replication_group->AddNamespaceToTarget(
      target_master_addresses, source_namespace_id, epoch);
}

Result<IsOperationDoneResult> XClusterSourceManager::IsAlterXClusterReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  return outbound_replication_group->IsAlterXClusterReplicationDone(target_master_addresses, epoch);
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
      xcluster_manager_.CleanupStreamFromMaps(*stream);
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
  return CreateStreamsInternal(
      table_ids, SysCDCStreamEntryPB::INITIATED, cdc::StreamModeTransactional::kTrue, epoch);
}

Result<std::unique_ptr<XClusterCreateStreamsContext>> XClusterSourceManager::CreateStreamsInternal(
    const std::vector<TableId>& table_ids, SysCDCStreamEntryPB::State state,
    cdc::StreamModeTransactional transactional, const LeaderEpoch& epoch) {
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

    // We use a static set of options for all xCluster streams.
    *metadata.mutable_options() = client::GetXClusterStreamOptions();
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
    for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
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
      auto ts_desc_result = VERIFY_RESULT(table_info->GetTablets()).front()->GetLeader();
      if (!ts_desc_result) {
        // After a master failover we may not yet have the leader info, so we need to try again.
        if (ts_desc_result.status().IsNotFound()) {
          return STATUS(TryAgain, "Tablet leader not found", ts_desc_result.status().ToString());
        }
        RETURN_NOT_OK(ts_desc_result.status());
      }
      ts_desc = std::move(*ts_desc_result);
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
  // For xCluster, the only time we try to delete a tablet that is part of an active stream is
  // during tablet splitting, where we need to keep the parent tablet around until we have
  // replicated its SPLIT_OP record.

  auto outbound_streams = GetStreamsForTable(tablet_info.table()->id());
  if (!outbound_streams.empty()) {
    LOG(INFO) << "Retaining dropped tablet " << tablet_info.id()
              << " since it has active xCluster streams " << AsString(outbound_streams);
    delete_retainer.active_xcluster = true;
  }
}

void XClusterSourceManager::PopulateTabletDeleteRetainerInfoForTableDrop(
    const TableInfo& table_info, TabletDeleteRetainerInfo& delete_retainer) const {
  // On table drop, we need to retain the table as long as it has active xCluster streams. When the
  // target table is dropped, it will cleanup the stream on the source. In case the target is down
  // or lagging, the streams of dropped tables are automatically dropped after
  // cdc_wal_retention_time_secs.

  // Only the parent colocated table is replicated via xCluster.
  if (table_info.IsColocatedUserTable()) {
    return;
  }

  const auto& table_id = table_info.id();
  auto outbound_streams = GetStreamsForTable(table_id);
  if (!outbound_streams.empty()) {
    LOG(INFO) << "Retaining dropped table " << table_id << " since it has active xCluster streams "
              << AsString(outbound_streams);
    delete_retainer.active_xcluster = true;
  }
}

bool XClusterSourceManager::IsTableReplicated(const TableId& table_id) const {
  // Check that at least one of these streams is active (ie not being deleted).
  auto streams = GetStreamsForTable(table_id);
  return !streams.empty();
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

  decltype(retained_hidden_tablets_) tablet_to_retain;

  for (const auto& hidden_tablet : hidden_tablets) {
    if (!IsTableReplicated(hidden_tablet->table()->id())) {
      continue;
    }

    decltype(HiddenTabletInfo::split_tablets) split_tablets = {};
    auto tablet_lock = hidden_tablet->LockForRead();
    auto& tablet_pb = tablet_lock->pb;
    if (tablet_pb.split_tablet_ids_size() == kNumSplitParts) {
      split_tablets = {tablet_pb.split_tablet_ids(0), tablet_pb.split_tablet_ids(1)};
    }

    HiddenTabletInfo info{
        .table_id = hidden_tablet->table()->id(),
        .parent_tablet_id = tablet_pb.split_parent_tablet_id(),
        .split_tablets = std::move(split_tablets),
    };

    tablet_to_retain.emplace(hidden_tablet->id(), std::move(info));
  }
  {
    std::lock_guard l(retained_hidden_tablets_mutex_);
    for (const auto& [table_id, hidden_info] : tablet_to_retain) {
      retained_hidden_tablets_.emplace(table_id, std::move(hidden_info));
    }
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
  const auto now = master_.clock()->Now();

  std::unordered_set<TabletId> tablets_to_delete;
  std::vector<cdc::CDCStateTableKey> entries_to_delete;

  struct TableHideInfo {
    std::vector<CDCStreamInfoPtr> outbound_streams;
    bool is_hidden = false;
    bool HasActiveStreams() const { return !outbound_streams.empty(); }
  };
  std::unordered_map<TableId, TableHideInfo> table_hide_infos;

  const auto get_table_hide_info = [this, &table_hide_infos, &now](
                                       const TableId& table_id,
                                       const TabletId& tablet_id) -> Result<const TableHideInfo&> {
    if (table_hide_infos.contains(table_id)) {
      return &table_hide_infos[table_id];
    }

    TableHideInfo table_hide_info;
    table_hide_info.outbound_streams = GetStreamsForTable(table_id);

    auto table = VERIFY_RESULT(catalog_manager_.GetTableById(table_id));
    auto table_lock = table->LockForRead();

    if (table_lock->started_deleting()) {
      LOG(DFATAL) << "Table " << table_id << " is deleting while there is a hidden tablet "
                  << tablet_id << ". Orphaned xCluster streams "
                  << AsString(table_hide_info.outbound_streams);

      // This is not expected. In release builds orphan the streams so that the tablets get
      // correctly cleaned up.
      table_hide_info.outbound_streams = {};
    }

    if (table_hide_info.HasActiveStreams()) {
      table_hide_info.is_hidden = table_lock->started_hiding();

      if (table_hide_info.is_hidden && table_lock->pb.has_hide_hybrid_time()) {
        auto hide_ht = HybridTime(table_lock->pb.hide_hybrid_time());
        if (hide_ht.AddSeconds(FLAGS_cdc_wal_retention_time_secs) < now) {
          LOG(WARNING) << "Table " << table_id << " has been marked hidden for longer than "
                       << FLAGS_cdc_wal_retention_time_secs
                       << "s (cdc_wal_retention_time_secs). Dropping its xCluster streams "
                       << AsString(table_hide_info.outbound_streams);
          RETURN_NOT_OK(catalog_manager_.DropXReplStreams(
              table_hide_info.outbound_streams, SysCDCStreamEntryPB::DELETING));

          table_hide_info.outbound_streams = {};
        } else {
          YB_LOG_EVERY_N_SECS(INFO, 300)
              << "Tablets of Dropped table " << table_id
              << " are being retained since it has active xCluster streams "
              << AsString(table_hide_info.outbound_streams);
        }
      }
    }

    table_hide_infos[table_id] = std::move(table_hide_info);

    return &table_hide_infos[table_id];
  };

  for (auto& [tablet_id, hidden_tablet] : hidden_tablets) {
    const auto& table_id = hidden_tablet.table_id;

    const auto& table_hide_info = VERIFY_RESULT(get_table_hide_info(table_id, tablet_id)).get();

    // If table is no longer replicated then we no longer have to retain its tablets.
    if (!table_hide_info.HasActiveStreams()) {
      tablets_to_delete.insert(tablet_id);
      continue;
    }

    // If the entire table is hidden, then wait for the streams to get dropped.
    if (table_hide_info.is_hidden) {
      VLOG(2) << "Hidden tablet " << tablet_id << " belongs to table " << table_id
              << " that is dropped. Waiting for its xCluster streams to drop.";
      continue;
    }

    if (hidden_tablet.split_tablets.empty()) {
      LOG_WITH_FUNC(DFATAL)
          << "Unexpected state: Tablet " << tablet_id
          << " does not have any split children info, and the table is not hidden";

      // Log warning and skip this tablet in release builds.
      continue;
    }

    const auto parent_tablet_id = hidden_tablet.parent_tablet_id;
    if (!parent_tablet_id.empty() && hidden_tablets.contains(parent_tablet_id)) {
      VLOG(1) << tablet_id << "is waiting for parent tablet " << parent_tablet_id
              << " to get deleted";
      continue;
    }

    if (VERIFY_RESULT(ProcessSplitChildStreams(
            tablet_id, hidden_tablet, table_hide_info.outbound_streams, entries_to_delete))) {
      tablets_to_delete.insert(tablet_id);
    }
  }

  if (!entries_to_delete.empty()) {
    RETURN_NOT_OK_PREPEND(
        cdc_state_table_->DeleteEntries(entries_to_delete),
        "Error deleting xCluster stream rows from cdc_state table");
  }

  // Delete tablets from retained_hidden_tablets_, CatalogManager::CleanupHiddenTablets will do the
  // actual tablet deletion.
  if (!tablets_to_delete.empty()) {
    std::lock_guard l(retained_hidden_tablets_mutex_);
    for (const auto& tablet_id : tablets_to_delete) {
      retained_hidden_tablets_.erase(tablet_id);
    }
  }

  return Status::OK();
}

Result<bool> XClusterSourceManager::ProcessSplitChildStreams(
    const TabletId& tablet_id, const HiddenTabletInfo& hidden_tablet,
    const std::vector<CDCStreamInfoPtr>& outbound_streams,
    std::vector<cdc::CDCStateTableKey>& entries_to_delete) {
  // Check cdc_state table to see if the children tablets are being polled.
  // For each hidden tablet, check if for each stream we have an entry in the mapping for them.

  size_t count_streams_deleted = 0;
  for (const auto& stream : outbound_streams) {
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
      ++count_streams_deleted;
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
      ++count_streams_deleted;
      entries_to_delete.emplace_back(cdc::CDCStateTableKey{tablet_id, stream_id});
    }
  }

  return count_streams_deleted == outbound_streams.size();
}

std::vector<CDCStreamInfoPtr> XClusterSourceManager::GetStreamsForTable(
    const TableId& table_id, bool include_dropped) const {
  std::vector<CDCStreamInfoPtr> streams;
  {
    SharedLock lock(tables_to_stream_map_mutex_);
    auto stream_it = FindOrNull(tables_to_stream_map_, table_id);
    if (stream_it) {
      streams = *stream_it;
    }
  }

  if (!include_dropped) {
    EraseIf([](const auto& stream) { return stream->LockForRead()->is_deleting(); }, &streams);
  }

  return streams;
}

Result<xrepl::StreamId> XClusterSourceManager::CreateNewXClusterStreamForTable(
    const TableId& table_id, cdc::StreamModeTransactional transactional,
    const std::optional<SysCDCStreamEntryPB::State>& initial_state, const LeaderEpoch& epoch) {
  auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));

  RETURN_NOT_OK(catalog_manager_.SetXReplWalRetentionForTable(table_info, epoch));
  RETURN_NOT_OK(catalog_manager_.BackfillMetadataForXRepl(table_info, epoch));

  const auto state = initial_state ? *initial_state : SysCDCStreamEntryPB::ACTIVE;
  auto create_context =
      VERIFY_RESULT(CreateStreamsInternal({table_id}, state, transactional, epoch));
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

std::unordered_map<TableId, std::vector<CDCStreamInfoPtr>> XClusterSourceManager::GetAllStreams()
    const {
  SharedLock lock(tables_to_stream_map_mutex_);
  return tables_to_stream_map_;
}

Status XClusterSourceManager::PopulateXClusterStatus(
    XClusterStatus& xcluster_status, const SysXClusterConfigEntryPB& xcluster_config) const {
  std::set<xrepl::StreamId> paused_streams;
  if (xcluster_config.has_xcluster_producer_registry()) {
    for (const auto& [stream_id, paused] :
         xcluster_config.xcluster_producer_registry().paused_producer_stream_ids()) {
      if (paused) {
        paused_streams.insert(VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)));
      }
    }
  }

  std::unordered_map<xrepl::StreamId, XClusterOutboundTableStreamStatus> stream_status_map;
  for (const auto& [table_id, streams] : GetAllStreams()) {
    for (const auto& stream : streams) {
      XClusterOutboundTableStreamStatus table_stream_status;
      auto table_info_res = catalog_manager_.GetTableById(table_id);
      if (table_info_res) {
        table_stream_status.full_table_name = GetFullTableName(*table_info_res.get());
      }
      table_stream_status.table_id = table_id;
      table_stream_status.stream_id = stream->StreamId();
      table_stream_status.state =
          SysCDCStreamEntryPB::State_Name(stream->LockForRead()->pb.state());
      if (paused_streams.contains(stream->StreamId())) {
        table_stream_status.state += " (PAUSED)";
      }
      stream_status_map.emplace(stream->StreamId(), std::move(table_stream_status));
    }
  }

  auto all_outbound_groups = GetAllOutboundGroups();
  for (const auto& replication_info : all_outbound_groups) {
    auto metadata_result = replication_info->GetMetadata();
    if (!metadata_result && metadata_result.status().IsNotFound()) {
      continue;
    }
    RETURN_NOT_OK(metadata_result);
    const auto& metadata = *metadata_result;

    XClusterOutboundReplicationGroupStatus group_status;
    group_status.replication_group_id = replication_info->Id();
    group_status.state = SysXClusterOutboundReplicationGroupEntryPB::State_Name(metadata.state());
    group_status.target_universe_info = metadata.target_universe_info().DebugString();

    for (const auto& [namespace_id, namespace_status] : metadata.namespace_infos()) {
      XClusterOutboundReplicationGroupNamespaceStatus ns_status;
      ns_status.namespace_id = namespace_id;
      ns_status.namespace_name = catalog_manager_.GetNamespaceName(namespace_id);
      ns_status.state =
          SysXClusterOutboundReplicationGroupEntryPB::SysXClusterOutboundReplicationGroupEntryPB::
              NamespaceInfoPB::State_Name(namespace_status.state());
      ns_status.initial_bootstrap_required = namespace_status.initial_bootstrap_required();
      if (namespace_status.has_error_status()) {
        ns_status.status = namespace_status.error_status().ShortDebugString();
      } else {
        ns_status.status = "OK";
      }

      for (const auto& [table_id, table_info] : namespace_status.table_infos()) {
        XClusterOutboundReplicationGroupTableStatus table_status;
        if (table_info.has_stream_id()) {
          auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(table_info.stream_id()));
          auto stream_status = FindOrNull(stream_status_map, stream_id);
          if (stream_status) {
            SCHECK_EQ(
                stream_status->table_id, table_id, IllegalState,
                Format(
                    "Expected xCluster stream $0 to belongs to table $1 but outbound replication "
                    "group $2 has it linked to table $3",
                    stream_id, stream_status->table_id, replication_info->Id(), table_id));
            table_status = stream_status_map.at(stream_id);
            stream_status_map.erase(stream_id);
          } else {
            table_status.table_id = table_id;
            table_status.stream_id = stream_id;
            table_status.state = "DELETED";
          }
        }
        table_status.is_checkpointing = table_info.is_checkpointing();
        table_status.is_part_of_initial_bootstrap = table_info.is_part_of_initial_bootstrap();
        ns_status.table_statuses.push_back(std::move(table_status));
      }
      group_status.namespace_statuses.push_back(std::move(ns_status));
    }
    xcluster_status.outbound_replication_group_statuses.emplace_back(std::move(group_status));
  }

  for (auto& [_, stream_status] : stream_status_map) {
    xcluster_status.outbound_table_stream_statuses.emplace_back(std::move(stream_status));
  }

  return Status::OK();
}

Status XClusterSourceManager::PopulateXClusterStatusJson(JsonWriter& jw) const {
  auto all_outbound_groups = GetAllOutboundGroups();
  jw.String("outbound_replication_groups");
  jw.StartArray();
  for (auto const& replication_info : all_outbound_groups) {
    jw.StartObject();
    jw.String("replication_group_id");
    jw.String(replication_info->Id().ToString());
    auto metadata = replication_info->GetMetadata();
    if (metadata) {
      jw.String("metadata");
      jw.Protobuf(*metadata);
    } else {
      jw.String("error");
      jw.String(metadata.status().ToString());
    }
    jw.EndObject();
  }
  jw.EndArray();

  jw.String("outbound_streams");
  jw.StartArray();
  for (const auto& [table_id, streams] : GetAllStreams()) {
    for (const auto& stream : streams) {
      jw.String("stream_id");
      jw.String(stream->StreamId().ToString());
      jw.String("metadata");
      jw.Protobuf(stream->LockForRead()->pb);
    }
  }
  jw.EndArray();
  return Status::OK();
}

Status XClusterSourceManager::RemoveStreamsFromSysCatalog(
    const std::vector<CDCStreamInfo*>& streams, const LeaderEpoch& epoch) {
  for (auto& outbound_group : GetAllOutboundGroups()) {
    RETURN_NOT_OK(outbound_group->RemoveStreams(streams, epoch));
  }

  return Status::OK();
}

Status XClusterSourceManager::MarkIndexBackfillCompleted(
    const std::unordered_set<TableId>& index_ids, const LeaderEpoch& epoch) {
  // Checkpoint xCluster streams of indexes after the backfill completes. The backfilled data is not
  // replicated, and the target cluster performs its own backfill, so we can skip streaming changes
  // before the backfill completion.

  std::vector<std::pair<TableId, xrepl::StreamId>> table_streams;
  {
    SharedLock l(tables_to_stream_map_mutex_);
    for (const auto& index_id : index_ids) {
      if (tables_to_stream_map_.contains(index_id)) {
        for (const auto& stream : tables_to_stream_map_.at(index_id)) {
          LOG(INFO) << "Checkpointing xCluster stream " << stream->StreamId() << " of index "
                    << index_id << " to its end of WAL";
          table_streams.push_back({index_id, stream->StreamId()});
        }
      }
    }
  }
  if (table_streams.empty()) {
    return Status::OK();
  }

  std::promise<Result<bool>> promise;

  RETURN_NOT_OK(CheckpointStreamsToEndOfWAL(
      table_streams, epoch, /*check_if_bootstrap_required=*/false,
      [&promise](Result<bool> result) { promise.set_value(std::move(result)); }));
  auto bootstrap_required = VERIFY_RESULT(promise.get_future().get());

  LOG_IF(DFATAL, bootstrap_required)
      << "Unexpectedly found bootstrap required when check_if_bootstrap_required was set to false";

  return Status::OK();
}

Status XClusterSourceManager::RepairOutboundReplicationGroupAddTable(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id,
    const xrepl::StreamId& stream_id, const LeaderEpoch& epoch) {
  auto table_info = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));

  auto stream_info = VERIFY_RESULT(catalog_manager_.GetXReplStreamInfo(stream_id));
  auto stream_table_ids = stream_info->table_id();
  SCHECK(
      stream_info->IsXClusterStream() && stream_table_ids.size() == 1, InvalidArgument,
      Format("Stream $0 is not valid for use in xCluster", stream_id));
  SCHECK_EQ(
      stream_table_ids.Get(0), table_id, InvalidArgument,
      Format("Stream $0 belongs to a different table", stream_id));

  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  return outbound_replication_group->RepairAddTable(
      table_info->namespace_id(), table_id, stream_id, epoch);
}

Status XClusterSourceManager::RepairOutboundReplicationGroupRemoveTable(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id,
    const LeaderEpoch& epoch) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  return outbound_replication_group->RepairRemoveTable(table_id, epoch);
}

std::vector<xcluster::ReplicationGroupId>
XClusterSourceManager::GetXClusterOutboundReplicationGroups(NamespaceId namespace_filter) {
  std::vector<xcluster::ReplicationGroupId> replication_groups;
  for (const auto& outbound_group : GetAllOutboundGroups()) {
    if (namespace_filter.empty() || outbound_group->HasNamespace(namespace_filter)) {
      replication_groups.push_back(outbound_group->Id());
    }
  }

  return replication_groups;
}

Result<std::unordered_map<NamespaceId, std::unordered_map<TableId, xrepl::StreamId>>>
XClusterSourceManager::GetXClusterOutboundReplicationGroupInfo(
    const xcluster::ReplicationGroupId& replication_group_id) {
  auto outbound_replication_group =
      VERIFY_RESULT(GetOutboundReplicationGroup(replication_group_id));
  const auto namespace_ids = VERIFY_RESULT(outbound_replication_group->GetNamespaces());

  std::unordered_map<NamespaceId, std::unordered_map<TableId, xrepl::StreamId>> result;
  for (const auto& namespace_id : namespace_ids) {
    const auto namespace_info =
        VERIFY_RESULT(outbound_replication_group->GetNamespaceCheckpointInfo(namespace_id));
    if (!namespace_info) {
      continue;
    }
    std::unordered_map<TableId, xrepl::StreamId> ns_info;
    for (const auto& table_info : namespace_info->table_infos) {
      ns_info.emplace(table_info.table_id, table_info.stream_id);
    }
    result[namespace_id] = std::move(ns_info);
  }
  return result;
}

}  // namespace yb::master
