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

#include "yb/master/xcluster/master_xcluster_types.h"
#include "yb/master/xcluster/xcluster_catalog_entity.h"

namespace yb {
namespace master {

class XClusterOutboundReplicationGroup {
 public:
  struct HelperFunctions {
    const std::function<Result<NamespaceId>(YQLDatabase, const NamespaceName&)>
        get_namespace_id_func;
    const std::function<Result<std::vector<TableInfoPtr>>(const NamespaceId&)> get_tables_func;
    const std::function<Result<std::vector<xrepl::StreamId>>(
        const std::vector<TableInfoPtr>&, CoarseTimePoint)>
        bootstrap_tables_func;
    const std::function<Result<DeleteCDCStreamResponsePB>(
        const DeleteCDCStreamRequestPB&, const LeaderEpoch&)>
        delete_cdc_stream_func;

    // SysCatalog functions.
    const std::function<Status(const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo*)>
        upsert_to_sys_catalog_func;
    const std::function<Status(const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo*)>
        delete_from_sys_catalog_func;
  };

  explicit XClusterOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& outbound_replication_group_pb,
      HelperFunctions helper_functions);

  const xcluster::ReplicationGroupId& Id() const { return outbound_rg_info_->ReplicationGroupId(); }

  std::string ToString() const { return Format("xClusterOutboundReplicationGroup $0", Id()); }
  std::string LogPrefix() const { return ToString(); }

  SysXClusterOutboundReplicationGroupEntryPB GetMetadata() const;

  Result<std::vector<NamespaceId>> AddNamespaces(
      const LeaderEpoch& epoch, const std::vector<NamespaceName>& namespace_names,
      CoarseTimePoint deadline);

  Result<NamespaceId> AddNamespace(
      const LeaderEpoch& epoch, const NamespaceName& namespace_name, CoarseTimePoint deadline);

  Status RemoveNamespace(const LeaderEpoch& epoch, const NamespaceId& namespace_id);

  Status Delete(const LeaderEpoch& epoch);

  // Returns std::nullopt if the namespace is not yet ready.
  Result<std::optional<bool>> IsBootstrapRequired(const NamespaceId& namespace_id) const;

  using TableSchemaNamePair = std::pair<TableName, PgSchemaName>;

  // Returns std::nullopt if the namespace is not yet ready.
  Result<std::optional<NamespaceCheckpointInfo>> GetNamespaceCheckpointInfo(
      const NamespaceId& namespace_id,
      const std::vector<TableSchemaNamePair>& table_names = {}) const;

 private:
  Result<NamespaceId> AddNamespaceInternal(
      const NamespaceName& namespace_name, CoarseTimePoint deadline,
      XClusterOutboundReplicationGroupInfo::WriteLock& l);

  Status Upsert(XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch);

  // Deletes all the streams for the given namespace.
  Status DeleteNamespaceStreams(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const SysXClusterOutboundReplicationGroupEntryPB& pb);

  Result<SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB> BootstrapTables(
      const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline);

  HelperFunctions helper_functions_;

  std::unique_ptr<XClusterOutboundReplicationGroupInfo> outbound_rg_info_;
};

}  // namespace master
}  // namespace yb
