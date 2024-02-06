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
#include "yb/client/xcluster_client.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/xcluster_rpc_tasks.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_string(certs_for_cdc_dir);

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

}  // namespace

XClusterOutboundReplicationGroup::XClusterOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const SysXClusterOutboundReplicationGroupEntryPB& outbound_replication_group_pb,
    HelperFunctions helper_functions)
    : helper_functions_(std::move(helper_functions)) {
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
    XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch) {
  auto status = helper_functions_.upsert_to_sys_catalog_func(epoch, outbound_rg_info_.get());
  l.CommitOrWarn(status, "updating xClusterOutboundReplicationGroup in sys-catalog");
  return status;
}

Result<SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB>
XClusterOutboundReplicationGroup::BootstrapTables(
    const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline,
    const LeaderEpoch& epoch) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << yb::ToString(table_infos);

  SCHECK(!table_infos.empty(), InvalidArgument, "No tables to bootstrap");
  SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB ns_info;
  ns_info.set_state(SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::CHECKPOINTING);

  auto bootstrap_ids = VERIFY_RESULT(helper_functions_.bootstrap_tables_func(
      table_infos, deadline, StreamCheckpointLocation::kCurrentEndOfWAL, epoch));

  SCHECK_EQ(
      table_infos.size(), bootstrap_ids.size(), IllegalState,
      "Number of tables to bootstrap and number of bootstrap ids do not match");

  bool initial_bootstrap_required = false;
  for (size_t i = 0; i < table_infos.size(); i++) {
    // TODO(Hari): DB-9417 bootstrap_resp should return if bootstrap is required.
    // initial_bootstrap_required |= bootstrap_resp.bootstrap_required(i);

    SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::TableInfoPB table_info;
    table_info.set_stream_id(bootstrap_ids[i].ToString());
    table_info.set_is_part_of_initial_bootstrap(true);
    ns_info.mutable_table_infos()->insert({table_infos[i]->id(), std::move(table_info)});
  }

  ns_info.set_state(SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::READY);
  ns_info.set_initial_bootstrap_required(initial_bootstrap_required);

  return ns_info;
}

Result<NamespaceId> XClusterOutboundReplicationGroup::AddNamespaceInternal(
    const NamespaceName& namespace_name, CoarseTimePoint deadline,
    XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch) {
  SCHECK(!namespace_name.empty(), InvalidArgument, "Namespace name cannot be empty");
  VLOG_WITH_PREFIX_AND_FUNC(1) << namespace_name;

  auto namespace_id = VERIFY_RESULT(
      helper_functions_.get_namespace_id_func(YQLDatabase::YQL_DATABASE_PGSQL, namespace_name));

  auto& outbound_group_pb = l.mutable_data()->pb;

  if (HasNamespaceUnlocked(namespace_id)) {
    LOG(INFO) << "Skip adding Namespace " << namespace_name << " since it already exists in "
              << ToString();
    return namespace_id;
  }

  auto table_infos = VERIFY_RESULT(helper_functions_.get_tables_func(namespace_id));
  auto ns_checkpoint_info = VERIFY_RESULT(BootstrapTables(table_infos, deadline, epoch));
  (*outbound_group_pb.mutable_namespace_infos())[namespace_id] = std::move(ns_checkpoint_info);

  return namespace_id;
}

Result<std::vector<NamespaceId>> XClusterOutboundReplicationGroup::AddNamespaces(
    const LeaderEpoch& epoch, const std::vector<NamespaceName>& namespace_names,
    CoarseTimePoint deadline) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());

  std::vector<NamespaceId> namespace_ids;
  for (const auto& namespace_name : namespace_names) {
    auto namespace_id = VERIFY_RESULT(AddNamespaceInternal(namespace_name, deadline, l, epoch));
    namespace_ids.push_back(std::move(namespace_id));
  }
  RETURN_NOT_OK(Upsert(l, epoch));
  return namespace_ids;
}

Result<NamespaceId> XClusterOutboundReplicationGroup::AddNamespace(
    const LeaderEpoch& epoch, const NamespaceName& namespace_name, CoarseTimePoint deadline) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto namespace_id = VERIFY_RESULT(AddNamespaceInternal(namespace_name, deadline, l, epoch));
  RETURN_NOT_OK(Upsert(l, epoch));

  return namespace_id;
}

Status XClusterOutboundReplicationGroup::DeleteNamespaceStreams(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    const SysXClusterOutboundReplicationGroupEntryPB& outbound_group_pb) {
  if (!HasNamespaceUnlocked(namespace_id)) {
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Deleting streams for namespace " << namespace_id;
  auto& namespace_info = outbound_group_pb.namespace_infos().at(namespace_id);

  bool has_streams_to_delete = false;
  DeleteCDCStreamRequestPB req;
  for (const auto& [table_id, table_info] : namespace_info.table_infos()) {
    if (!table_info.has_stream_id()) {
      continue;
    }
    has_streams_to_delete = true;
    req.add_stream_id(table_info.stream_id());
  }
  if (!has_streams_to_delete) {
    return Status::OK();
  }

  req.set_force_delete(true);
  req.set_ignore_errors(false);
  auto resp = VERIFY_RESULT(helper_functions_.delete_cdc_stream_func(req, epoch));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status XClusterOutboundReplicationGroup::RemoveNamespace(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group_pb = l.mutable_data()->pb;

  RETURN_NOT_OK(DeleteNamespaceStreams(epoch, namespace_id, outbound_group_pb));

  outbound_group_pb.mutable_namespace_infos()->erase(namespace_id);

  return Upsert(l, epoch);
}

Status XClusterOutboundReplicationGroup::Delete(const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForWrite());
  auto& outbound_group_pb = l.mutable_data()->pb;

  for (const auto& [namespace_id, _] : *outbound_group_pb.mutable_namespace_infos()) {
    RETURN_NOT_OK(DeleteNamespaceStreams(epoch, namespace_id, outbound_group_pb));
  }
  outbound_group_pb.mutable_namespace_infos()->clear();
  outbound_group_pb.set_state(SysXClusterOutboundReplicationGroupEntryPB::DELETED);

  auto status = helper_functions_.delete_from_sys_catalog_func(epoch, outbound_rg_info_.get());
  l.CommitOrWarn(status, "updating xClusterOutboundReplicationGroup in sys-catalog");

  return status;
}

Result<std::optional<bool>> XClusterOutboundReplicationGroup::IsBootstrapRequired(
    const NamespaceId& namespace_id) const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  auto& outbound_group = l->pb;
  SCHECK(
      HasNamespaceUnlocked(namespace_id), NotFound,
      Format("Namespace $0 not found in $1", namespace_id, ToString()));

  auto& namespace_info = outbound_group.namespace_infos().at(namespace_id);
  if (namespace_info.state() ==
      SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::CHECKPOINTING) {
    return std::nullopt;
  }

  return namespace_info.initial_bootstrap_required();
}

Result<std::optional<NamespaceCheckpointInfo>>
XClusterOutboundReplicationGroup::GetNamespaceCheckpointInfo(
    const NamespaceId& namespace_id,
    const std::vector<std::pair<TableName, PgSchemaName>>& table_names) const {
  SharedLock mutex_lock(mutex_);
  auto l = VERIFY_RESULT(LockForRead());
  auto& outbound_group = l->pb;
  SCHECK(
      HasNamespaceUnlocked(namespace_id), NotFound,
      Format("Namespace $0 not found in xClusterOutboundReplicationGroup $1", namespace_id, Id()));

  auto& namespace_info = outbound_group.namespace_infos().at(namespace_id);
  if (namespace_info.state() !=
      SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::READY) {
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Namespace " << namespace_id << " is not ready yet.";
    return std::nullopt;
  }

  NamespaceCheckpointInfo ns_info;
  ns_info.initial_bootstrap_required = namespace_info.initial_bootstrap_required();

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
    SCHECK(
        namespace_info.table_infos().count(table_id), IllegalState,
        Format(
            "Table $0 exists in namespace $1, but not in $2", table_id, namespace_id, ToString()));
    auto& namespace_table_info = namespace_info.table_infos().at(table_id);
    if (!namespace_table_info.has_stream_id() || namespace_table_info.is_checkpointing()) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "xCluster stream for Table " << table_id << " in Namespace "
                                   << namespace_id << " is not ready yet.";
      return std::nullopt;
    }
    auto stream_id = VERIFY_RESULT(
        xrepl::StreamId::FromString(namespace_info.table_infos().at(table_id).stream_id()));
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

Result<std::shared_ptr<client::XClusterRemoteClient>>
XClusterOutboundReplicationGroup::GetRemoteClient(
    const std::vector<HostPort>& remote_masters) const {
  auto client = std::make_shared<client::XClusterRemoteClient>(
      FLAGS_certs_for_cdc_dir, MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
  RETURN_NOT_OK(client->Init(Id(), remote_masters));
  return client;
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
  std::vector<TableId> source_table_ids;
  std::vector<xrepl::StreamId> bootstrap_ids;
  for (const auto& [ns_id, ns_info] : outbound_group.namespace_infos()) {
    SCHECK_EQ(
        ns_info.state(), SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::READY,
        TryAgain, Format("Namespace $0 is not yet ready to start replicating", ns_id));

    namespace_names.push_back(VERIFY_RESULT(helper_functions_.get_namespace_name_func(ns_id)));

    auto all_tables = VERIFY_RESULT(helper_functions_.get_tables_func(ns_id));

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

  auto target_uuid = VERIFY_RESULT(remote_client->SetupUniverseReplication(
      Id(), source_master_addresses, namespace_names, source_table_ids, bootstrap_ids,
      client::XClusterRemoteClient::Transactional::kTrue));

  auto* target_universe_info = l.mutable_data()->pb.mutable_target_universe_info();

  target_universe_info->set_universe_uuid(target_uuid.ToString());
  target_universe_info->set_state(
      SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfo::CREATING_REPLICATION_GROUP);

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
      SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfo::REPLICATING) {
    return IsOperationDoneResult(true, Status::OK());
  }

  IsOperationDoneResult setup_result;
  if (target_universe.state() ==
      SysXClusterOutboundReplicationGroupEntryPB_TargetUniverseInfo::FAILED) {
    setup_result.done = true;
    if (target_universe.has_error_status()) {
      setup_result.status = StatusFromPB(target_universe.error_status());
    } else {
      setup_result.status = STATUS(
          IllegalState, "Failed to create replication group on target cluster",
          target_universe.universe_uuid());
    }
  } else {
    // TODO(#20810): Remove this once async task that polls for IsCreateXClusterReplicationDone gets
    // added.
    auto remote_client = VERIFY_RESULT(GetRemoteClient(target_master_addresses));
    setup_result = VERIFY_RESULT(remote_client->IsSetupUniverseReplicationDone(Id()));
  }

  if (!setup_result.done) {
    return setup_result;
  }

  if (setup_result.status.ok()) {
    target_universe.set_state(
        SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfo::REPLICATING);
  } else {
    LOG_WITH_PREFIX(WARNING) << "Failed to create replication group on target cluster: "
                             << setup_result.status;
    // Clear the target info so that it can be retried later.
    outbound_group.clear_target_universe_info();
  }
  RETURN_NOT_OK(Upsert(l, epoch));

  return setup_result;
}

bool XClusterOutboundReplicationGroup::HasNamespace(const NamespaceId& namespace_id) const {
  SharedLock mutex_lock(mutex_);
  auto lock_result = LockForRead();
  if (!lock_result.ok()) {
    // The only allowed error is NotFound which indicates the group is deleted.
    LOG_IF_WITH_PREFIX(DFATAL, !lock_result.status().IsNotFound())
        << "Unexpected lock outcome: " << lock_result.status();
    return false;
  }
  return HasNamespaceUnlocked(namespace_id);
}

bool XClusterOutboundReplicationGroup::HasNamespaceUnlocked(const NamespaceId& namespace_id) const {
  return outbound_rg_info_->old_pb().namespace_infos().count(namespace_id) > 0;
}

Status XClusterOutboundReplicationGroup::AddTableInternal(
    TableInfoPtr table_info, const LeaderEpoch& epoch) {
  std::lock_guard mutex_lock(mutex_);
  auto lock_result = LockForWrite();
  if (!lock_result.ok()) {
    RSTATUS_DCHECK(
        lock_result.status().IsNotFound(), IllegalState, "Unexpected lock outcome: $0",
        lock_result.status());
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Replication group deleted";
    return Status::OK();
  }

  const auto table_id = table_info->id();
  const auto namespace_id = table_info->namespace_id();
  auto& outbound_group_pb = lock_result->mutable_data()->pb;
  if (!HasNamespaceUnlocked(namespace_id)) {
    // Namespace was deleted between between locks. Since this is used in
    // AddTableToXClusterSourceTask, which runs asynchronously and we do not want to fail the task,
    // we can just return OK.
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Namespace " << namespace_id
                                 << " is no longer part of this replication group";
    return Status::OK();
  }
  auto& ns_info = outbound_group_pb.mutable_namespace_infos()->at(namespace_id);
  if (ns_info.table_infos().count(table_id)) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Table " << table_info
                                 << " is already a part of this replication group";
    return Status::OK();
  }

  auto stream_ids = VERIFY_RESULT(helper_functions_.bootstrap_tables_func(
      {table_info}, CoarseMonoClock::now(), StreamCheckpointLocation::kOpId0, epoch));
  CHECK_EQ(stream_ids.size(), 1);

  SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::TableInfoPB ns_table_info;
  ns_table_info.set_stream_id(stream_ids.front().ToString());
  ns_info.mutable_table_infos()->insert({table_id, std::move(ns_table_info)});

  return Upsert(*lock_result, epoch);
}

void XClusterOutboundReplicationGroup::AddTable(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch, StdStatusCallback completion_cb) {
  // TODO(#20810): Perform checkpointing step which involves a cdc_state_table write asynchronously
  // and push down the callback.
  completion_cb(AddTableInternal(table_info, epoch));
}

}  // namespace yb::master
