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

#include "yb/master/xcluster/xcluster_outbound_replication_group.h"
#include "yb/common/xcluster_util.h"
#include "yb/client/xcluster_client.h"
#include "yb/common/colocated_util.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group_tasks.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"

DEFINE_RUNTIME_uint32(max_xcluster_streams_to_checkpoint_in_parallel, 200,
    "Maximum number of xCluster streams to checkpoint in parallel");

using namespace std::placeholders;

namespace yb::master {

namespace {

struct TableSchemaNamePairHash {
  std::size_t operator()(const XClusterOutboundReplicationGroup::TableSchemaNamePair& elem) const {
    std::size_t hash = 0;
    boost::hash_combine(hash, elem.first);
    boost::hash_combine(hash, elem.second);
    return hash;
  }
};

bool TableNeedsInitialCheckpoint(
    const SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::TableInfoPB& table_info) {
  return table_info.is_part_of_initial_bootstrap() && table_info.is_checkpointing();
}

Result<bool> IsReady(
    const SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB& namespace_info) {
  if (namespace_info.state() ==
      SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::FAILED) {
    return StatusFromPB(namespace_info.error_status());
  }

  return namespace_info.state() ==
         SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::READY;
}

template <typename LockType>
bool IsDeleted(const Result<LockType>& lock_result) {
  if (!lock_result.ok()) {
    // The only allowed error is NotFound which indicates the group is deleted.
    LOG_IF(DFATAL, !lock_result.status().IsNotFound())
        << "Unexpected lock outcome: " << lock_result.status();
    return true;
  }

  return false;
}

}  // namespace

XClusterOutboundReplicationGroup::XClusterOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const SysXClusterOutboundReplicationGroupEntryPB& outbound_replication_group_pb,
    HelperFunctions helper_functions, scoped_refptr<TasksTracker> tasks_tracker,
    XClusterOutboundReplicationGroupTaskFactory& task_factory)
    : CatalogEntityWithTasks(std::move(tasks_tracker)),
      helper_functions_(std::move(helper_functions)),
      task_factory_(task_factory) {
  outbound_rg_info_ = std::make_unique<XClusterOutboundReplicationGroupInfo>(replication_group_id);
  outbound_rg_info_->Load(outbound_replication_group_pb);
}

Result<XClusterOutboundReplicationGroupInfo::ReadLock>
XClusterOutboundReplicationGroup::LockForRead() const {
  auto l = outbound_rg_info_->LockForRead();
  SCHECK_NE(
      l->pb.state(), SysXClusterOutboundReplicationGroupEntryPB::DELETED, NotFound,
      ToString() + " is deleted");
  return l;
}

Result<XClusterOutboundReplicationGroupInfo::WriteLock>
XClusterOutboundReplicationGroup::LockForWrite() {
  auto l = outbound_rg_info_->LockForWrite();
  SCHECK_NE(
      l->pb.state(), SysXClusterOutboundReplicationGroupEntryPB::DELETED, NotFound,
      ToString() + " is deleted");
  return l;
}

Result<SysXClusterOutboundReplicationGroupEntryPB> XClusterOutboundReplicationGroup::GetMetadata()
    const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  return l->pb;
}

Status XClusterOutboundReplicationGroup::Upsert(
    XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch,
    const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
  RETURN_NOT_OK_PREPEND(
      helper_functions_.upsert_to_sys_catalog_func(epoch, outbound_rg_info_.get(), streams),
      Format("$0 Failed to write to sys-catalog", LogPrefix()));
  l.Commit();
  return Status::OK();
}

void XClusterOutboundReplicationGroup::StartNamespaceCheckpointTasks(
    const std::vector<NamespaceId>& namespace_ids, const LeaderEpoch& epoch) {
  for (const auto& namespace_id : namespace_ids) {
    // Perform the rest of the work asynchronously.
    // IsBootstrapRequired or GetNamespaceCheckpointInfo can be called to detect when the namespace
    // is ready.
    auto task = task_factory_.CreateCheckpointNamespaceTask(*this, namespace_id, epoch);
    task->Start();
  }
}

Status XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap(
    const NamespaceId& namespace_id, const LeaderEpoch& epoch) {
  TEST_SYNC_POINT("XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap");

  std::lock_guard mutex_l(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());

  auto* ns_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  SCHECK_EQ(
      ns_info->state(), NamespaceInfoPB::CHECKPOINTING, IllegalState,
      "Namespace in unexpected state");

  std::vector<TableId> table_ids;
  // Get all the initial tables that do not yet have a stream.
  for (const auto& [table_id, table_info] : ns_info->table_infos()) {
    if (!table_info.is_part_of_initial_bootstrap()) {
      // This is a table that was created after the creation of this replication group.
      continue;
    }

    // Ensure idempotence of the function.
    if (table_info.stream_id().empty()) {
      SCHECK(
          table_info.is_checkpointing(), IllegalState,
          "$0 Table $1 does not have stream but is marked as completed checkpoint", LogPrefix(),
          table_id);
      table_ids.push_back(table_id);
    }
  }

  if (table_ids.empty()) {
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Creating xcluster streams for " << table_ids.size()
                        << " table(s) in namespace " << namespace_id;

  auto create_context =
      VERIFY_RESULT(helper_functions_.create_xcluster_streams_func(table_ids, epoch));

  SCHECK_EQ(
      create_context->streams_.size(), table_ids.size(), IllegalState,
      "Unexpected number of streams");

  for (size_t i = 0; i < table_ids.size(); ++i) {
    const auto& table_id = table_ids[i];
    auto* table_info = VERIFY_RESULT(GetTableInfo(*ns_info, table_id));
    RSTATUS_DCHECK(
        !table_info->has_stream_id(), IllegalState, "$0 Table $1 already has a stream $1",
        LogPrefix(), table_id, table_info->stream_id());
    table_info->set_stream_id(create_context->streams_[i]->id());
  }

  RETURN_NOT_OK(Upsert(l, epoch, create_context->streams_));
  create_context->Commit();

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::CheckpointStreamsForInitialBootstrap(
    const NamespaceId& namespace_id, const LeaderEpoch& epoch,
    std::function<void(XClusterCheckpointStreamsResult)> completion_cb) {
  std::vector<std::pair<TableId, xrepl::StreamId>> table_streams;
  std::vector<TableId> table_ids;
  bool check_if_bootstrap_required = true;

  {
    std::lock_guard mutex_l(mutex_);
    auto l = VERIFY_RESULT(LockForWrite());

    auto* ns_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
    SCHECK_EQ(
        ns_info->state(), NamespaceInfoPB::CHECKPOINTING, IllegalState,
        "Namespace in unexpected state");

    // We only have to check only if bootstrap is not currently required.
    check_if_bootstrap_required = !ns_info->initial_bootstrap_required();

    for (const auto& [table_id, table_info] : ns_info->table_infos()) {
      if (!TableNeedsInitialCheckpoint(table_info)) {
        continue;
      }
      SCHECK(
          table_info.has_stream_id(), IllegalState, "Stream id not found for table $0", table_id);

      if (table_streams.size() >= FLAGS_max_xcluster_streams_to_checkpoint_in_parallel) {
        break;
      }

      table_ids.emplace_back(table_id);
      table_streams.emplace_back(
          table_id, VERIFY_RESULT(xrepl::StreamId::FromString(table_info.stream_id())));
    }
  }

  auto callback = [table_ids, user_cb = std::move(completion_cb)](Result<bool> result) {
    if (!result.ok()) {
      user_cb(result.status());
      return;
    }
    user_cb(std::make_pair(std::move(table_ids), *result));
  };

  if (!table_ids.empty()) {
    LOG_WITH_PREFIX(INFO) << "Checkpointing xcluster streams for " << table_ids.size()
                          << " table(s) in namespace " << namespace_id;

    RETURN_NOT_OK(helper_functions_.checkpoint_xcluster_streams_func(
        table_streams, StreamCheckpointLocation::kCurrentEndOfWAL, epoch,
        check_if_bootstrap_required, std::move(callback)));
  } else {
    callback(false);  // Bootstrap not required if we dont have any tables.
    // When MarkBootstrapTablesAsCheckpointed handles this empty table result it will mark the
    // namespace as READY.
    // So, after all tables have been checkpointed this function and
    // MarkBootstrapTablesAsCheckpointed have to be called one extra time.
  }

  return Status::OK();
}

Result<bool> XClusterOutboundReplicationGroup::MarkBootstrapTablesAsCheckpointed(
    const NamespaceId& namespace_id, XClusterCheckpointStreamsResult checkpoint_result,
    const LeaderEpoch& epoch) {
  auto [table_ids, is_bootstrap_required] = VERIFY_RESULT(std::move(checkpoint_result));

  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto* ns_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  bool done = false;

  SCHECK_EQ(
      ns_info->state(), NamespaceInfoPB::CHECKPOINTING, IllegalState,
      "Namespace in unexpected state");

  if (table_ids.empty()) {
    auto table_infos = VERIFY_RESULT(helper_functions_.get_tables_func(namespace_id));
    std::set<TableId> tables;
    std::transform(
        table_infos.begin(), table_infos.end(), std::inserter(tables, tables.begin()),
        [](const auto& table_info) { return table_info->id(); });

    std::set<TableId> checkpointed_tables;
    std::transform(
        ns_info->table_infos().begin(), ns_info->table_infos().end(),
        std::inserter(checkpointed_tables, checkpointed_tables.begin()),
        [](const auto& table_info) { return table_info.first; });

    auto diff = STLSetSymmetricDifference(tables, checkpointed_tables);
    SCHECK_FORMAT(
        diff.empty(), IllegalState,
        "List of tables changed during xCluster checkpoint of replication group $0: $1", ToString(),
        yb::ToString(diff));

    LOG_WITH_PREFIX(INFO) << "Marking namespace " << namespace_id << " as READY";
    ns_info->set_state(NamespaceInfoPB::READY);
    done = true;
  } else {
    if (!ns_info->initial_bootstrap_required() && is_bootstrap_required) {
      // Dont clear once set.
      ns_info->set_initial_bootstrap_required(true);
    }

    for (const auto& table_id : table_ids) {
      auto* table_info = VERIFY_RESULT(GetTableInfo(*ns_info, table_id));
      SCHECK(
          TableNeedsInitialCheckpoint(*table_info), IllegalState,
          "$0 Table $1 is not checkpointing", LogPrefix(), table_id);
      table_info->set_is_checkpointing(false);
    }
  }

  RETURN_NOT_OK(Upsert(l, epoch));

  return done;
}

void XClusterOutboundReplicationGroup::MarkCheckpointNamespaceAsFailed(
    const NamespaceId& namespace_id, const LeaderEpoch& epoch, const Status& status) {
  DCHECK(!status.ok());
  LOG_WITH_PREFIX(WARNING) << "Failed to checkpoint namespace " << namespace_id << ": " << status;

  std::lock_guard mutex_lock(mutex_);
  auto lock_result = LockForWrite();
  auto ns_info = GetNamespaceInfoSafe(lock_result, namespace_id);
  if (!ns_info || ns_info->state() != NamespaceInfoPB::CHECKPOINTING) {
    LOG_WITH_PREFIX(WARNING) << "Namespace " << namespace_id
                             << " not in CHECKPOINTING state, so wont be marked as failed.";
    return;
  }
  // FAILED is a terminal state. RemoveNamespace or Delete Replication Group is the way to clean it
  // up.
  ns_info->set_state(NamespaceInfoPB::FAILED);
  StatusToPB(status, ns_info->mutable_error_status());

  WARN_NOT_OK(Upsert(*lock_result, epoch), ToString());
}

Result<scoped_refptr<NamespaceInfo>> XClusterOutboundReplicationGroup::GetYbNamespaceInfo(
    const NamespaceId& namespace_id) const {
  NamespaceIdentifierPB ns_id_pb;
  ns_id_pb.set_id(namespace_id);
  return helper_functions_.get_namespace_func(ns_id_pb);
}

Result<NamespaceName> XClusterOutboundReplicationGroup::GetNamespaceName(
    const NamespaceId& namespace_id) const {
  return VERIFY_RESULT(GetYbNamespaceInfo(namespace_id))->name();
}

Result<XClusterOutboundReplicationGroup::NamespaceInfoPB>
XClusterOutboundReplicationGroup::CreateNamespaceInfo(
    const NamespaceId& namespace_id, const LeaderEpoch& epoch) {
  auto table_infos = VERIFY_RESULT(helper_functions_.get_tables_func(namespace_id));
  VLOG_WITH_PREFIX_AND_FUNC(1) << "Tables: " << yb::ToString(table_infos);

  SCHECK(
      !table_infos.empty(), InvalidArgument,
      "Database should have at least one table in order to be part of xCluster replication");

  auto yb_ns_info = VERIFY_RESULT(GetYbNamespaceInfo(namespace_id));
  SCHECK_EQ(
      yb_ns_info->database_type(), YQLDatabase::YQL_DATABASE_PGSQL, InvalidArgument,
      "Only YSQL databases are supported in xCluster DB Scoped replication");

  if (yb_ns_info->colocated()) {
    bool has_any_colocated_table =
        std::any_of(table_infos.begin(), table_infos.end(), [](const TableInfoPtr& table_info) {
          return IsColocatedDbTablegroupParentTableId(table_info->id());
        });
    SCHECK(
        has_any_colocated_table, InvalidArgument,
        "Colocated database should have at least one colocated table in order to be part of "
        "xCluster replication");
  }

  NamespaceInfoPB ns_info;
  ns_info.set_state(NamespaceInfoPB::CHECKPOINTING);

  for (size_t i = 0; i < table_infos.size(); ++i) {
    NamespaceInfoPB::TableInfoPB table_info;
    table_info.set_is_checkpointing(true);
    table_info.set_is_part_of_initial_bootstrap(true);
    ns_info.mutable_table_infos()->insert({table_infos[i]->id(), std::move(table_info)});
  }

  return ns_info;
}

Result<bool> XClusterOutboundReplicationGroup::AddNamespaceInternal(
    const NamespaceId& namespace_id, XClusterOutboundReplicationGroupInfo::WriteLock& l,
    const LeaderEpoch& epoch) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << namespace_id;

  auto& outbound_group_pb = l.mutable_data()->pb;

  if (HasNamespaceUnlocked(namespace_id)) {
    LOG(INFO) << "Skip adding Namespace " << namespace_id << " since it already exists in "
              << ToString();
    return false;
  }

  auto ns_checkpoint_info = VERIFY_RESULT(CreateNamespaceInfo(namespace_id, epoch));
  outbound_group_pb.mutable_namespace_infos()->insert(
      {namespace_id, std::move(ns_checkpoint_info)});

  return true;
}

Status XClusterOutboundReplicationGroup::AddNamespaces(
    const LeaderEpoch& epoch, const std::vector<NamespaceId>& namespace_ids) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());

  std::vector<NamespaceId> namespaces_added_now;
  for (const auto& namespace_id : namespace_ids) {
    auto added_now = VERIFY_RESULT(AddNamespaceInternal(namespace_id, l, epoch));
    if (added_now) {
      namespaces_added_now.push_back(namespace_id);
    }
  }
  if (namespaces_added_now.empty()) {
    return Status::OK();
  }

  // Checkpoint tasks operate on namespace Ids. We currently cannot detect which task works on which
  // namespace so we do not kill any when removing a namespace. Removing and re-adding the same
  // namespace can cause an older task to incorrectly mark stale streams. To prevent this we make
  // sure no tasks are running.
  RETURN_NOT_OK(VerifyNoTasksInProgress());

  RETURN_NOT_OK(Upsert(l, epoch));

  StartNamespaceCheckpointTasks(namespaces_added_now, epoch);

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::AddNamespace(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());

  auto added_now = VERIFY_RESULT(AddNamespaceInternal(namespace_id, l, epoch));
  if (!added_now) {
    // Already exists.
    return Status::OK();
  }

  // Checkpoint tasks operate on namespace Ids. We currently cannot detect which task works on which
  // namespace so we do not kill any when removing a namespace. Removing and re-adding the same
  // namespace can cause an older task to incorrectly mark stale streams. To prevent this we make
  // sure no tasks are running.
  RETURN_NOT_OK(VerifyNoTasksInProgress());

  RETURN_NOT_OK(Upsert(l, epoch));

  StartNamespaceCheckpointTasks({namespace_id}, epoch);

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::DeleteNamespaceStreams(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    const SysXClusterOutboundReplicationGroupEntryPB& outbound_group_pb) {
  if (!HasNamespaceUnlocked(namespace_id)) {
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Deleting streams for namespace " << namespace_id;
  auto& namespace_info = outbound_group_pb.namespace_infos().at(namespace_id);

  DeleteCDCStreamRequestPB req;
  for (const auto& [table_id, table_info] : namespace_info.table_infos()) {
    if (!table_info.has_stream_id()) {
      continue;
    }
    req.add_stream_id(table_info.stream_id());
  }
  if (!req.stream_id_size()) {
    return Status::OK();
  }

  req.set_force_delete(true);
  req.set_ignore_errors(true);  // Ignore errors if stream is not found.
  auto resp = VERIFY_RESULT(helper_functions_.delete_cdc_stream_func(req, epoch));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::RemoveNamespace(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    const std::vector<HostPort>& target_master_addresses) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group_pb = l.mutable_data()->pb;

  if (!target_master_addresses.empty()) {
    LOG(INFO) << "Removing Namespace from target " << AsString(target_master_addresses)
              << " replication group " << Id();

    SCHECK(
        outbound_group_pb.has_target_universe_info(), IllegalState,
        "Target universe info not found. xCluster replication must already be setup in order to "
        "remove namespace from the target.");
    auto target_uuid = VERIFY_RESULT(
        UniverseUuid::FromString(outbound_group_pb.target_universe_info().universe_uuid()));

    auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));
    RETURN_NOT_OK(remote_client->GetXClusterClient().RemoveNamespaceFromUniverseReplication(
        Id(), namespace_id, target_uuid));
  }

  RETURN_NOT_OK(DeleteNamespaceStreams(epoch, namespace_id, outbound_group_pb));

  outbound_group_pb.mutable_namespace_infos()->erase(namespace_id);

  return Upsert(l, epoch);
}

Status XClusterOutboundReplicationGroup::Delete(
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  CloseAndWaitForAllTasksToAbort();

  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group_pb = l.mutable_data()->pb;

  if (!target_master_addresses.empty()) {
    LOG(INFO) << "Deleting replication group " << Id() << " from target "
              << AsString(target_master_addresses);

    SCHECK(
        outbound_group_pb.has_target_universe_info(), IllegalState,
        "Target universe info not found. xCluster replication must already be setup in order to "
        "remove namespace from the target.");
    auto target_uuid = VERIFY_RESULT(
        UniverseUuid::FromString(outbound_group_pb.target_universe_info().universe_uuid()));

    auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));
    RETURN_NOT_OK(remote_client->GetXClusterClient().DeleteUniverseReplication(
        Id(), /*ignore_errors=*/true, target_uuid));
  }

  for (const auto& [namespace_id, _] : *outbound_group_pb.mutable_namespace_infos()) {
    RETURN_NOT_OK(DeleteNamespaceStreams(epoch, namespace_id, outbound_group_pb));
  }
  outbound_group_pb.mutable_namespace_infos()->clear();
  outbound_group_pb.set_state(SysXClusterOutboundReplicationGroupEntryPB::DELETED);

  RETURN_NOT_OK_PREPEND(
      helper_functions_.delete_from_sys_catalog_func(epoch, outbound_rg_info_.get()),
      "updating xClusterOutboundReplicationGroup in sys-catalog");
  l.Commit();

  return Status::OK();
}

Result<std::optional<bool>> XClusterOutboundReplicationGroup::IsBootstrapRequired(
    const NamespaceId& namespace_id) const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  const auto* namespace_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  if (!VERIFY_RESULT(IsReady(*namespace_info))) {
    return std::nullopt;
  }

  return namespace_info->initial_bootstrap_required();
}

Result<std::optional<NamespaceCheckpointInfo>>
XClusterOutboundReplicationGroup::GetNamespaceCheckpointInfo(
    const NamespaceId& namespace_id,
    const std::vector<std::pair<TableName, PgSchemaName>>& table_names) const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  const auto* namespace_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  if (!VERIFY_RESULT(IsReady(*namespace_info))) {
    return std::nullopt;
  }

  NamespaceCheckpointInfo ns_info;
  ns_info.initial_bootstrap_required = namespace_info->initial_bootstrap_required();

  auto all_tables = VERIFY_RESULT(helper_functions_.get_tables_func(namespace_id));
  std::vector<scoped_refptr<TableInfo>> table_infos;

  if (!table_names.empty()) {
    std::unordered_map<TableSchemaNamePair, scoped_refptr<TableInfo>, TableSchemaNamePairHash>
        table_names_map;
    for (auto& table_info : all_tables) {
      table_names_map[{table_info->name(), table_info->pgschema_name()}] = table_info;
    }

    for (auto& table : table_names) {
      SCHECK(
          table_names_map.contains(table), NotFound,
          Format("Table $0.$1 not found in namespace $2", table.second, table.first, namespace_id));

      // Order of elements in table_infos should match the order in input table_names.
      table_infos.push_back(table_names_map[table]);
    }
  } else {
    table_infos = std::move(all_tables);
  }

  for (const auto& table_info : table_infos) {
    const auto& table_id = table_info->id();
    if (namespace_info->table_infos().count(table_id) == 0) {
      // We do not have this table! It has been manually removed using the repair APIs.
      // If user explicitly requested this table then fail the request.
      // Else we are getting all tables in the database so ignore it.
      SCHECK(
          table_names.empty(), NotFound, Format("Table $0 not found in $1", table_id, ToString()));
      continue;
    }

    auto& namespace_table_info = namespace_info->table_infos().at(table_id);
    if (!namespace_table_info.has_stream_id() || namespace_table_info.is_checkpointing()) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "xCluster stream for Table " << table_id << " in Namespace "
                                   << namespace_id << " is not ready yet.";
      return std::nullopt;
    }
    auto stream_id = VERIFY_RESULT(
        xrepl::StreamId::FromString(namespace_info->table_infos().at(table_id).stream_id()));
    SCHECK(
        !stream_id.IsNil(), IllegalState,
        Format("Nil stream id found for table $0 in $1", table_id, ToString()));

    NamespaceCheckpointInfo::TableInfo ns_table_info{
        .table_id = table_id,
        .stream_id = std::move(stream_id),
        .table_name = table_info->name(),
        .pg_schema_name = table_info->pgschema_name()};

    ns_info.table_infos.emplace_back(std::move(ns_table_info));
  }

  return ns_info;
}

Result<std::optional<NamespaceCheckpointInfo>>
XClusterOutboundReplicationGroup::GetNamespaceCheckpointInfoForTableIds(
    const NamespaceId& namespace_id, const std::vector<TableId>& source_table_ids) const {
  SCHECK(!source_table_ids.empty(), InvalidArgument, "Source table ids cannot be empty");
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  const auto* namespace_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  if (!VERIFY_RESULT(IsReady(*namespace_info))) {
    return std::nullopt;
  }

  NamespaceCheckpointInfo ns_info;
  ns_info.initial_bootstrap_required = namespace_info->initial_bootstrap_required();

  for (const auto& table_id : source_table_ids) {
    SCHECK(
        namespace_info->table_infos().count(table_id) > 0, NotFound,
        Format("Table $0 not found in Namespace $1: $2", table_id, namespace_id, ToString()));

    auto& namespace_table_info = namespace_info->table_infos().at(table_id);
    if (!namespace_table_info.has_stream_id() || namespace_table_info.is_checkpointing()) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "xCluster stream for Table " << table_id << " in Namespace "
                                   << namespace_id << " is not ready yet.";
      return std::nullopt;
    }
    auto stream_id = VERIFY_RESULT(
        xrepl::StreamId::FromString(namespace_info->table_infos().at(table_id).stream_id()));
    SCHECK(
        !stream_id.IsNil(), IllegalState,
        Format("Nil stream id found for table $0 in $1", table_id, ToString()));

    NamespaceCheckpointInfo::TableInfo ns_table_info{
        .table_id = table_id,
        .stream_id = std::move(stream_id),
        // Pass in empty values since these are required, but not used.
        .table_name = "",
        .pg_schema_name = ""};

    ns_info.table_infos.emplace_back(std::move(ns_table_info));
  }

  return ns_info;
}

Result<std::shared_ptr<client::XClusterRemoteClientHolder>>
XClusterOutboundReplicationGroup::GetRemoteClient(
    const std::vector<HostPort>& remote_masters) const {
  return client::XClusterRemoteClientHolder::Create(Id(), remote_masters);
}

Status XClusterOutboundReplicationGroup::CreateXClusterReplication(
    const std::vector<HostPort>& source_master_addresses,
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group = l.mutable_data()->pb;

  if (outbound_group.has_target_universe_info()) {
    // Already exists.
    // TODO(#20810): make sure master_addresses atleast partially overlap.
    return Status::OK();
  }

  std::vector<NamespaceName> namespace_names;
  std::vector<NamespaceId> namespace_ids;
  std::vector<TableId> source_table_ids;
  std::vector<xrepl::StreamId> bootstrap_ids;
  for (const auto& [ns_id, ns_info] : outbound_group.namespace_infos()) {
    SCHECK_EQ(
        ns_info.state(), NamespaceInfoPB::READY, TryAgain,
        Format("Namespace $0 is not yet ready to start replicating", ns_id));

    namespace_ids.push_back(ns_id);
    namespace_names.push_back(VERIFY_RESULT(GetNamespaceName(ns_id)));

    for (const auto& [table_id, table_info] : ns_info.table_infos()) {
      if (!table_info.is_part_of_initial_bootstrap()) {
        // Only include tables that were part of the initial bootstrap as only those are backed up
        // and restored on the target. The remaining will get added as DDLs execute.
        continue;
      }
      // This is not expected since the namespace is marked ready.
      RSTATUS_DCHECK(
          !table_info.is_checkpointing(), IllegalState, Format("Table $0 is not yet ready"));

      source_table_ids.push_back(table_id);
      bootstrap_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(table_info.stream_id())));
    }
  }

  auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));

  auto target_uuid =
      VERIFY_RESULT(remote_client->GetXClusterClient().SetupDbScopedUniverseReplication(
          Id(), source_master_addresses, namespace_names, namespace_ids, source_table_ids,
          bootstrap_ids));

  auto* target_universe_info = l.mutable_data()->pb.mutable_target_universe_info();

  target_universe_info->set_universe_uuid(target_uuid.ToString());
  target_universe_info->set_state(
      SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfoPB::CREATING_REPLICATION_GROUP);

  RETURN_NOT_OK(Upsert(l, epoch));

  // TODO(#20810): Start a async task that will poll for IsCreateXClusterReplicationDone and update
  // the state.

  return Status::OK();
}

Result<IsOperationDoneResult> XClusterOutboundReplicationGroup::IsCreateXClusterReplicationDone(
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group = l.mutable_data()->pb;
  SCHECK(outbound_group.has_target_universe_info(), IllegalState, "Target universe info not found");

  auto& target_universe = *outbound_group.mutable_target_universe_info();

  if (target_universe.state() ==
      SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfoPB::REPLICATING) {
    return IsOperationDoneResult::Done();
  }

  auto setup_result = IsOperationDoneResult::NotDone();
  if (target_universe.state() ==
      SysXClusterOutboundReplicationGroupEntryPB_TargetUniverseInfoPB::FAILED) {
    Status status;
    if (target_universe.has_error_status()) {
      status = StatusFromPB(target_universe.error_status());
    } else {
      status = STATUS(
          IllegalState, "Failed to create replication group on target cluster",
          target_universe.universe_uuid());
    }

    setup_result = IsOperationDoneResult::Done(std::move(status));
  } else {
    // TODO(#20810): Remove this once async task that polls for IsCreateXClusterReplicationDone gets
    // added.
    auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));
    setup_result =
        VERIFY_RESULT(remote_client->GetXClusterClient().IsSetupUniverseReplicationDone(Id()));
  }

  if (!setup_result.done()) {
    return setup_result;
  }

  if (setup_result.status().ok()) {
    target_universe.set_state(
        SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfoPB::REPLICATING);
  } else {
    LOG_WITH_PREFIX(WARNING) << "Failed to create replication group on target cluster: "
                             << setup_result.status();
    // Clear the target info so that it can be retried later.
    outbound_group.clear_target_universe_info();
  }
  RETURN_NOT_OK(Upsert(l, epoch));

  return setup_result;
}

Status XClusterOutboundReplicationGroup::AddNamespaceToTarget(
    const std::vector<HostPort>& target_master_addresses, const NamespaceId& source_namespace_id,
    const LeaderEpoch& epoch) const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  auto& outbound_group = l.data().pb;

  SCHECK(
      outbound_group.has_target_universe_info(), IllegalState,
      "Target universe info not found. xCluster replication must already be setup in order to add "
      "a new namespace to the target.");
  auto target_uuid = VERIFY_RESULT(
      UniverseUuid::FromString(outbound_group.target_universe_info().universe_uuid()));

  SCHECK(
      outbound_group.namespace_infos().count(source_namespace_id), NotFound,
      Format("Namespace $0 not found in replication group $1", source_namespace_id, ToString()));

  const auto& ns_info = outbound_group.namespace_infos().at(source_namespace_id);
  SCHECK_EQ(
      ns_info.state(), NamespaceInfoPB::READY, TryAgain,
      Format("Namespace $0 is not yet ready to start replicating", source_namespace_id));

  auto namespace_name = VERIFY_RESULT(GetNamespaceName(source_namespace_id));

  std::vector<TableId> source_table_ids;
  std::vector<xrepl::StreamId> bootstrap_ids;
  for (const auto& [table_id, table_info] : ns_info.table_infos()) {
    if (!table_info.is_part_of_initial_bootstrap()) {
      // Only include tables that were part of the initial bootstrap as only those are backed up
      // and restored on the target. The remaining will get added as DDLs execute.
      continue;
    }
    // This is not expected since the namespace is marked ready.
    RSTATUS_DCHECK(
        !table_info.is_checkpointing(), IllegalState, Format("Table $0 is not yet ready"));

    source_table_ids.push_back(table_id);
    bootstrap_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(table_info.stream_id())));
  }

  auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));

  RETURN_NOT_OK(remote_client->GetXClusterClient().AddNamespaceToDbScopedUniverseReplication(
      Id(), target_uuid, namespace_name, source_namespace_id, source_table_ids, bootstrap_ids));

  // TODO(#20810): Start a async task that will poll for IsCreateXClusterReplicationDone and update
  // the state.

  return Status::OK();
}

Result<IsOperationDoneResult> XClusterOutboundReplicationGroup::IsAlterXClusterReplicationDone(
    const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch) {
  auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));
  return remote_client->GetXClusterClient().IsSetupUniverseReplicationDone(
      xcluster::GetAlterReplicationGroupId(Id()));
}

bool XClusterOutboundReplicationGroup::HasNamespace(const NamespaceId& namespace_id) const {
  SharedLock mutex_lock(mutex_);
  auto lock_result = LockForRead();
  if (IsDeleted(lock_result)) {
    return false;
  }
  return HasNamespaceUnlocked(namespace_id);
}

bool XClusterOutboundReplicationGroup::HasNamespaceUnlocked(const NamespaceId& namespace_id) const {
  return outbound_rg_info_->old_pb().namespace_infos().count(namespace_id) > 0;
}

XClusterOutboundReplicationGroup::NamespaceInfoPB*
XClusterOutboundReplicationGroup::GetNamespaceInfoSafe(
    const Result<XClusterOutboundReplicationGroupInfo::WriteLock>& lock_result,
    const NamespaceId& namespace_id) const {
  if (IsDeleted(lock_result)) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Replication group deleted";
    return nullptr;
  }

  auto& outbound_group_pb = lock_result->mutable_data()->pb;
  if (!HasNamespaceUnlocked(namespace_id)) {
    // Namespace was deleted between between locks. Since this is used in
    // AddTableToXClusterSourceTask, which runs asynchronously and we do not want to fail the task,
    // we can just return OK.
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Namespace " << namespace_id
                                 << " is not part of this replication group";
    return nullptr;
  }

  return &outbound_group_pb.mutable_namespace_infos()->at(namespace_id);
}

Result<XClusterOutboundReplicationGroup::NamespaceInfoPB*>
XClusterOutboundReplicationGroup::GetNamespaceInfo(const NamespaceId& namespace_id) {
  CHECK(outbound_rg_info_->metadata().HasWriteLock());
  SCHECK(
      HasNamespaceUnlocked(namespace_id), NotFound,
      Format("$0 Namespace $1 not found", LogPrefix(), namespace_id));

  return &outbound_rg_info_->mutable_metadata()->mutable_dirty()->pb.mutable_namespace_infos()->at(
      namespace_id);
}

Result<const XClusterOutboundReplicationGroup::NamespaceInfoPB*>
XClusterOutboundReplicationGroup::GetNamespaceInfo(const NamespaceId& namespace_id) const {
  SCHECK(
      HasNamespaceUnlocked(namespace_id), NotFound,
      Format("$0 Namespace $1 not found", LogPrefix(), namespace_id));

  return &outbound_rg_info_->old_pb().namespace_infos().at(namespace_id);
}

Result<XClusterOutboundReplicationGroup::NamespaceInfoPB::TableInfoPB*>
XClusterOutboundReplicationGroup::GetTableInfo(NamespaceInfoPB& ns_info, const TableId& table_id) {
  auto table_info = FindOrNull(*ns_info.mutable_table_infos(), table_id);
  SCHECK(table_info, NotFound, "$0 Table $1 not found", LogPrefix(), table_id);
  return table_info;
}

Status XClusterOutboundReplicationGroup::CreateStreamForNewTable(
    const NamespaceId& namespace_id, const TableId& table_id, const LeaderEpoch& epoch) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << YB_STRUCT_TO_STRING(namespace_id, table_id);

  std::lock_guard mutex_lock(mutex_);
  auto lock_result = LockForWrite();
  auto ns_info = GetNamespaceInfoSafe(lock_result, namespace_id);
  if (!ns_info || ns_info->state() == NamespaceInfoPB::FAILED) {
    return Status::OK();
  }

  if (ns_info->table_infos().count(table_id)) {
    DCHECK(ns_info->table_infos().at(table_id).has_stream_id());
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Table " << table_id << " already has a stream "
                                 << ns_info->table_infos().at(table_id).stream_id();
    return Status::OK();
  }

  {
    NamespaceInfoPB::TableInfoPB ns_table_info;
    ns_table_info.set_is_checkpointing(true);
    ns_table_info.set_is_part_of_initial_bootstrap(false);
    ns_info->mutable_table_infos()->insert({table_id, std::move(ns_table_info)});
  }

  auto create_context =
      VERIFY_RESULT(helper_functions_.create_xcluster_streams_func({table_id}, epoch));

  SCHECK_EQ(create_context->streams_.size(), 1, IllegalState, "Unexpected number of streams");

  auto* table_info = VERIFY_RESULT(GetTableInfo(*ns_info, table_id));
  RSTATUS_DCHECK(
      !table_info->has_stream_id(), IllegalState, "$0 Table $1 already has a stream $1",
      LogPrefix(), table_id, table_info->stream_id());
  table_info->set_stream_id(create_context->streams_.front()->id());

  RETURN_NOT_OK(Upsert(*lock_result, epoch, create_context->streams_));
  create_context->Commit();

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::CheckpointNewTable(
    const NamespaceId& namespace_id, const TableId& table_id, const LeaderEpoch& epoch,
    StdStatusCallback completion_cb) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << YB_STRUCT_TO_STRING(namespace_id, table_id);
  xrepl::StreamId stream_id = xrepl::StreamId::Nil();

  {
    std::lock_guard mutex_lock(mutex_);
    auto lock_result = LockForWrite();
    auto ns_info = GetNamespaceInfoSafe(lock_result, namespace_id);
    if (!ns_info || ns_info->state() == NamespaceInfoPB::FAILED) {
      completion_cb(Status::OK());
      return Status::OK();
    }

    DCHECK_GT(ns_info->table_infos().count(table_id), 0);
    auto& ns_table_info = ns_info->table_infos().at(table_id);
    if (!ns_table_info.is_checkpointing()) {
      SCHECK(
          ns_table_info.has_stream_id(), IllegalState, "$0 Stream id not found for table $1",
          LogPrefix(), table_id);
      VLOG_WITH_PREFIX_AND_FUNC(2)
          << "Table " << table_id << " is already a part of this replication group";
      completion_cb(Status::OK());
      return Status::OK();
    }

    stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(ns_table_info.stream_id()));
  }

  RETURN_NOT_OK(helper_functions_.checkpoint_xcluster_streams_func(
      {{table_id, stream_id}}, StreamCheckpointLocation::kOpId0, epoch,
      /*check_if_bootstrap_required=*/false,
      [user_cb = std::move(completion_cb)](Result<bool> result) {
        if (!result.ok()) {
          user_cb(result.status());
        }
        LOG_IF(DFATAL, result.get()) << "No bootstrap should be needed when checkpointing OpId0";
        user_cb(Status::OK());
      }));

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::MarkNewTablesAsCheckpointed(
    const NamespaceId& namespace_id, const TableId& table_id, const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto lock_result = LockForWrite();
  auto ns_info = GetNamespaceInfoSafe(lock_result, namespace_id);
  if (!ns_info) {
    return Status::OK();
  }

  auto* table_info = VERIFY_RESULT(GetTableInfo(*ns_info, table_id));
  if (!table_info->is_checkpointing()) {
    return Status::OK();
  }

  table_info->set_is_checkpointing(false);
  return Upsert(*lock_result, epoch);
}

Status XClusterOutboundReplicationGroup::RemoveStreams(
    const std::vector<CDCStreamInfo*>& streams, const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto lock_result = LockForWrite();
  if (IsDeleted(lock_result)) {
    return Status::OK();
  }

  auto& pb = lock_result->mutable_data()->pb;

  bool upsert_needed = false;
  for (const auto& stream : streams) {
    VLOG_WITH_PREFIX(1) << "Removing stream " << stream->ToString();

    for (const auto& table_id : stream->table_id()) {
      for (auto& [ns_id, ns_info] : *pb.mutable_namespace_infos()) {
        auto table_info = FindOrNull(ns_info.table_infos(), table_id);
        if (table_info && table_info->has_stream_id() && table_info->stream_id() == stream->id()) {
          ns_info.mutable_table_infos()->erase(table_id);
          upsert_needed = true;
          break;
        }
      }
    }
  }

  if (upsert_needed) {
    return Upsert(*lock_result, epoch);
  }
  return Status::OK();
}

void XClusterOutboundReplicationGroup::StartPostLoadTasks(const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto lock_result = LockForWrite();
  if (!lock_result.ok()) {
    LOG_WITH_PREFIX_AND_FUNC(WARNING) << "Failed to lock: " << lock_result.status();
    return;
  }

  auto tasks = GetTasks();
  if (!tasks.empty()) {
    LOG_WITH_PREFIX_AND_FUNC(DFATAL) << "Tasks already in progress: " << AsString(tasks);
    return;
  }

  std::vector<NamespaceId> namespace_ids;
  for (const auto& [ns_id, ns_info] : lock_result->mutable_data()->pb.namespace_infos()) {
    if (ns_info.state() == NamespaceInfoPB::CHECKPOINTING) {
      namespace_ids.push_back(ns_id);
    }
  }

  StartNamespaceCheckpointTasks(namespace_ids, epoch);
}

Status XClusterOutboundReplicationGroup::RepairAddTable(
    const NamespaceId& namespace_id, const TableId& table_id, const xrepl::StreamId& stream_id,
    const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());

  auto* ns_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  SCHECK(
      !ns_info->mutable_table_infos()->count(table_id), AlreadyPresent,
      "Table $0 already exists in $1", table_id, ToString());

  NamespaceInfoPB::TableInfoPB table_info;
  table_info.set_stream_id(stream_id.ToString());
  table_info.set_is_checkpointing(false);
  table_info.set_is_part_of_initial_bootstrap(false);
  ns_info->mutable_table_infos()->insert({table_id, std::move(table_info)});

  return Upsert(l, epoch);
}

Status XClusterOutboundReplicationGroup::RepairRemoveTable(
    const TableId& table_id, const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group_pb = l.mutable_data()->pb;

  bool table_removed = false;
  for (auto& [namespace_id, namespace_info] : *outbound_group_pb.mutable_namespace_infos()) {
    if (namespace_info.mutable_table_infos()->erase(table_id)) {
      table_removed = true;
      break;
    }
  }

  SCHECK(table_removed, NotFound, "Table $0 not found in $1", table_id, ToString());

  return Upsert(l, epoch);
}

Status XClusterOutboundReplicationGroup::VerifyNoTasksInProgress() {
  auto tasks = GetTasks();
  SCHECK(tasks.empty(), IllegalState, "$0 has in progress tasks: $1", ToString(), AsString(tasks));
  return Status::OK();
}

Result<std::vector<NamespaceId>> XClusterOutboundReplicationGroup::GetNamespaces() const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  std::vector<NamespaceId> namespace_ids;
  for (const auto& [namespace_id, _] : l->pb.namespace_infos()) {
    namespace_ids.push_back(namespace_id);
  }

  return namespace_ids;
}

Result<std::string> XClusterOutboundReplicationGroup::GetStreamId(
    const NamespaceId& namespace_id, const TableId& table_id) const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());

  auto* ns_info = VERIFY_RESULT(GetNamespaceInfo(namespace_id));
  auto* table_info = FindOrNull(ns_info->table_infos(), table_id);

  SCHECK(table_info, NotFound, "Table $0 not found in $1", table_id, namespace_id);

  return table_info->stream_id();
}

}  // namespace yb::master
