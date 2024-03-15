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
#include "yb/master/catalog_entity_info.h"

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

Status XClusterOutboundReplicationGroup::Upsert(
    XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch) {
  auto status = helper_functions_.upsert_to_sys_catalog_func(epoch, outbound_rg_info_.get());
  l.CommitOrWarn(status, "updating xClusterOutboundReplicationGroup in sys-catalog");
  return status;
}

Result<SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB>
XClusterOutboundReplicationGroup::BootstrapTables(
    const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline) {
  SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB ns_info;
  ns_info.set_state(SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::CHECKPOINTING);

  auto bootstrap_ids =
      VERIFY_RESULT(helper_functions_.bootstrap_tables_func(table_infos, deadline));

  SCHECK_EQ(
      table_infos.size(), bootstrap_ids.size(), IllegalState,
      "Number of tables to bootstrap and number of bootstrap ids do not match");

  bool initial_bootstrap_required = false;
  for (size_t i = 0; i < table_infos.size(); i++) {
    // TODO(Hari): DB-9417 bootstrap_resp should return if bootstrap is required.
    // initial_bootstrap_required |= bootstrap_resp.bootstrap_required(i);

    SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::TableInfoPB table_info;
    table_info.set_stream_id(bootstrap_ids[i].ToString());
    ns_info.mutable_table_infos()->insert({table_infos[i]->id(), std::move(table_info)});
  }

  ns_info.set_state(SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::READY);
  ns_info.set_initial_bootstrap_required(initial_bootstrap_required);

  return ns_info;
}

Result<NamespaceId> XClusterOutboundReplicationGroup::AddNamespaceInternal(
    const NamespaceName& namespace_name, CoarseTimePoint deadline,
    XClusterOutboundReplicationGroupInfo::WriteLock& l) {
  SCHECK(!namespace_name.empty(), InvalidArgument, "Namespace name cannot be empty");
  auto namespace_id =
      VERIFY_RESULT(helper_functions_.get_namespace_id_func(YQL_DATABASE_PGSQL, namespace_name));

  auto& outbound_group_pb = l.mutable_data()->pb;

  if (outbound_group_pb.namespace_infos().count(namespace_id) > 0) {
    LOG(INFO) << "Skip adding Namespace " << namespace_name << " since it already exists in "
              << ToString();
    return namespace_id;
  }

  auto table_infos = VERIFY_RESULT(helper_functions_.get_tables_func(namespace_id));
  auto ns_checkpoint_info = VERIFY_RESULT(BootstrapTables(table_infos, deadline));
  (*outbound_group_pb.mutable_namespace_infos())[namespace_id] = std::move(ns_checkpoint_info);

  return namespace_id;
}

Result<std::vector<NamespaceId>> XClusterOutboundReplicationGroup::AddNamespaces(
    const LeaderEpoch& epoch, const std::vector<NamespaceName>& namespace_names,
    CoarseTimePoint deadline) {
  auto l = outbound_rg_info_->LockForWrite();

  std::vector<NamespaceId> namespace_ids;
  for (const auto& namespace_name : namespace_names) {
    auto namespace_id = VERIFY_RESULT(AddNamespaceInternal(namespace_name, deadline, l));
    namespace_ids.push_back(std::move(namespace_id));
  }
  RETURN_NOT_OK(Upsert(l, epoch));
  return namespace_ids;
}

Result<NamespaceId> XClusterOutboundReplicationGroup::AddNamespace(
    const LeaderEpoch& epoch, const NamespaceName& namespace_name, CoarseTimePoint deadline) {
  auto l = outbound_rg_info_->LockForWrite();
  auto namespace_id = VERIFY_RESULT(AddNamespaceInternal(namespace_name, deadline, l));
  RETURN_NOT_OK(Upsert(l, epoch));

  return namespace_id;
}

Status XClusterOutboundReplicationGroup::DeleteNamespaceStreams(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    const SysXClusterOutboundReplicationGroupEntryPB& outbound_group_pb) {
  if (!outbound_group_pb.namespace_infos().count(namespace_id)) {
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
  auto l = outbound_rg_info_->LockForWrite();
  auto& outbound_group_pb = l.mutable_data()->pb;

  RETURN_NOT_OK(DeleteNamespaceStreams(epoch, namespace_id, outbound_group_pb));

  outbound_group_pb.mutable_namespace_infos()->erase(namespace_id);

  return Upsert(l, epoch);
}

Status XClusterOutboundReplicationGroup::Delete(const LeaderEpoch& epoch) {
  auto l = outbound_rg_info_->LockForWrite();
  auto& outbound_group_pb = l.mutable_data()->pb;

  for (const auto& [namespace_id, _] : *outbound_group_pb.mutable_namespace_infos()) {
    RETURN_NOT_OK(DeleteNamespaceStreams(epoch, namespace_id, outbound_group_pb));
  }
  outbound_group_pb.mutable_namespace_infos()->clear();

  auto status = helper_functions_.delete_from_sys_catalog_func(epoch, outbound_rg_info_.get());
  l.CommitOrWarn(status, "updating xClusterOutboundReplicationGroup in sys-catalog");

  return status;
}

Result<std::optional<bool>> XClusterOutboundReplicationGroup::IsBootstrapRequired(
    const NamespaceId& namespace_id) const {
  auto l = outbound_rg_info_->LockForRead();
  auto& outbound_group = l->pb;
  SCHECK(
      outbound_group.namespace_infos().count(namespace_id) > 0, NotFound,
      Format("Namespace $0 not found in $1", namespace_id, ToString()));

  auto& namespace_info = outbound_group.namespace_infos().at(namespace_id);
  if (namespace_info.state() !=
      SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB::READY) {
    return std::nullopt;
  }

  return namespace_info.initial_bootstrap_required();
}

Result<std::optional<NamespaceCheckpointInfo>>
XClusterOutboundReplicationGroup::GetNamespaceCheckpointInfo(
    const NamespaceId& namespace_id,
    const std::vector<std::pair<TableName, PgSchemaName>>& table_names) const {
  auto l = outbound_rg_info_->LockForRead();
  auto& outbound_group = l->pb;
  SCHECK(
      outbound_group.namespace_infos().count(namespace_id) > 0, NotFound,
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

}  // namespace yb::master
