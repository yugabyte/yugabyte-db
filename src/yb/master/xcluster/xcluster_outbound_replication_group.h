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

#pragma once

#include "yb/master/xcluster/xcluster_catalog_entity.h"

namespace yb {
namespace master {

class XClusterOutboundReplicationGroup {
 public:
  explicit XClusterOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& outbound_replication_group_pb,
      SysCatalogTable* sys_catalog,
      std::function<Result<std::vector<scoped_refptr<TableInfo>>>(const NamespaceId&)>
          get_tables_func,
      std::function<Result<NamespaceId>(YQLDatabase db_type, const NamespaceName& namespace_name)>
          get_namespace_id_func,
      std::function<Result<DeleteCDCStreamResponsePB>(
          const DeleteCDCStreamRequestPB&, const LeaderEpoch& epoch)>
          delete_cdc_stream_func);

  const xcluster::ReplicationGroupId& Id() const { return outbound_rg_info_->ReplicationGroupId(); }

  std::string ToString() const { return Format("xClusterOutboundReplicationGroup $0", Id()); }
  std::string LogPrefix() const { return ToString(); }

  Result<std::vector<NamespaceId>> AddNamespaces(
      const LeaderEpoch& epoch, const std::vector<NamespaceName>& namespace_names,
      CoarseTimePoint deadline);

  Result<NamespaceId> AddNamespace(
      const LeaderEpoch& epoch, const NamespaceName& namespace_name, CoarseTimePoint deadline);

  Status RemoveNamespace(const LeaderEpoch& epoch, const NamespaceId& namespace_id);

  Status Delete(const LeaderEpoch& epoch);

  struct NamespaceCheckpointInfo {
    bool initial_bootstrap_required = false;
    struct TableInfo {
      TableId table_id;
      xrepl::StreamId stream_id;
      TableName table_name;
      PgSchemaName pg_schema_name;
    };
    std::vector<TableInfo> table_infos;
  };

  // Returns std::nullopt if the namespace is not yet ready.
  Result<std::optional<bool>> IsBootstrapRequired(const NamespaceId& namespace_id) const;

  using TableSchemaNamePair = std::pair<TableName, PgSchemaName>;

  // Returns std::nullopt if the namespace is not yet ready.
  Result<std::optional<NamespaceCheckpointInfo>> GetNamespaceCheckpointInfo(
      const NamespaceId& namespace_id,
      const std::vector<TableSchemaNamePair>& table_names = {}) const;

 private:
  // Returns the user table infos for the given namespace.
  Result<std::vector<scoped_refptr<TableInfo>>> GetTables(const NamespaceId& namespace_id) const;

  Result<NamespaceId> AddNamespaceInternal(
      const NamespaceName& namespace_name, CoarseTimePoint deadline,
      XClusterOutboundReplicationGroupInfo::WriteLock& l);

  Status Upsert(XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch);

  // Deletes all the streams for the given namespace.
  Status DeleteNamespaceStreams(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const SysXClusterOutboundReplicationGroupEntryPB& pb);

  std::unique_ptr<XClusterOutboundReplicationGroupInfo> outbound_rg_info_;

  SysCatalogTable* const sys_catalog_;
  std::function<Result<std::vector<scoped_refptr<TableInfo>>>(const NamespaceId&)> get_tables_func_;
  std::function<Result<NamespaceId>(YQLDatabase db_type, const NamespaceName& namespace_name)>
      get_namespace_id_func_;
  std::function<Result<DeleteCDCStreamResponsePB>(
      const DeleteCDCStreamRequestPB&, const LeaderEpoch& epoch)>
      delete_cdc_stream_func_;
};

}  // namespace master
}  // namespace yb
