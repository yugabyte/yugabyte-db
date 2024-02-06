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

#include "yb/cdc/xcluster_types.h"

#include "yb/gutil/thread_annotations.h"

namespace yb {

namespace client {
class XClusterRemoteClient;
}  // namespace client

namespace master {

class XClusterOutboundReplicationGroup
    : public std::enable_shared_from_this<XClusterOutboundReplicationGroup> {
 public:
  struct HelperFunctions {
    const std::function<Result<NamespaceId>(YQLDatabase, const NamespaceName&)>
        get_namespace_id_func;
    const std::function<Result<NamespaceName>(const NamespaceId&)> get_namespace_name_func;
    const std::function<Result<std::vector<TableInfoPtr>>(const NamespaceId&)> get_tables_func;
    const std::function<Result<std::vector<xrepl::StreamId>>(
        const std::vector<TableInfoPtr>&, CoarseTimePoint,
        StreamCheckpointLocation checkpoint_location, const LeaderEpoch& epoch)>
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

  virtual ~XClusterOutboundReplicationGroup() = default;

  const xcluster::ReplicationGroupId& Id() const { return outbound_rg_info_->ReplicationGroupId(); }

  std::string ToString() const { return Format("xClusterOutboundReplicationGroup $0", Id()); }
  std::string LogPrefix() const { return Format("$0 :", ToString()); }

  Result<SysXClusterOutboundReplicationGroupEntryPB> GetMetadata() const EXCLUDES(mutex_);

  Result<std::vector<NamespaceId>> AddNamespaces(
      const LeaderEpoch& epoch, const std::vector<NamespaceName>& namespace_names,
      CoarseTimePoint deadline) EXCLUDES(mutex_);

  Result<NamespaceId> AddNamespace(
      const LeaderEpoch& epoch, const NamespaceName& namespace_name, CoarseTimePoint deadline)
      EXCLUDES(mutex_);

  Status RemoveNamespace(const LeaderEpoch& epoch, const NamespaceId& namespace_id)
      EXCLUDES(mutex_);

  Status Delete(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  // Returns std::nullopt if the namespace is not yet ready.
  Result<std::optional<bool>> IsBootstrapRequired(const NamespaceId& namespace_id) const
      EXCLUDES(mutex_);

  using TableSchemaNamePair = std::pair<TableName, PgSchemaName>;

  // Returns std::nullopt if the namespace is not yet ready.
  Result<std::optional<NamespaceCheckpointInfo>> GetNamespaceCheckpointInfo(
      const NamespaceId& namespace_id,
      const std::vector<TableSchemaNamePair>& table_names = {}) const EXCLUDES(mutex_);

  Status CreateXClusterReplication(
      const std::vector<HostPort>& source_master_addresses,
      const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  Result<IsOperationDoneResult> IsCreateXClusterReplicationDone(
      const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  bool HasNamespace(const NamespaceId& namespace_id) const EXCLUDES(mutex_);

  void AddTable(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch, StdStatusCallback completion_cb)
      EXCLUDES(mutex_);

 private:
  friend class XClusterOutboundReplicationGroupMocked;

  // LockForRead and LockForWrite are used to get a read or write lock on the outbound_rg_info_ and
  // ensure the group is not deleted.
  // The normal Cow object XClusterOutboundReplicationGroupInfo read lock allows concurrent reads
  // and writes. We do not want this behavior, so these functions are guarded with the mutex_.
  Result<XClusterOutboundReplicationGroupInfo::ReadLock> LockForRead() const
      REQUIRES_SHARED(mutex_);
  Result<XClusterOutboundReplicationGroupInfo::WriteLock> LockForWrite() REQUIRES(mutex_);

  Result<NamespaceId> AddNamespaceInternal(
      const NamespaceName& namespace_name, CoarseTimePoint deadline,
      XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch)
      REQUIRES(mutex_);

  Status Upsert(XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch)
      REQUIRES(mutex_);

  // Deletes all the streams for the given namespace.
  Status DeleteNamespaceStreams(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const SysXClusterOutboundReplicationGroupEntryPB& pb) REQUIRES(mutex_);

  Result<SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB> BootstrapTables(
      const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline,
      const LeaderEpoch& epoch) REQUIRES(mutex_);

  virtual Result<std::shared_ptr<client::XClusterRemoteClient>> GetRemoteClient(
      const std::vector<HostPort>& remote_masters) const;

  // Checks if the namespace is part of this replication group. Caller must hold the read or write
  // lock on outbound_rg_info_. Checks the old_pb
  bool HasNamespaceUnlocked(const NamespaceId& namespace_id) const REQUIRES_SHARED(mutex_);

  Status AddTableInternal(TableInfoPtr table_info, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  HelperFunctions helper_functions_;

  // Mutex used to ensure reads are not allowed when writes are happening.
  mutable std::shared_mutex mutex_;
  std::unique_ptr<XClusterOutboundReplicationGroupInfo> outbound_rg_info_;

  DISALLOW_COPY_AND_ASSIGN(XClusterOutboundReplicationGroup);
};

}  // namespace master
}  // namespace yb
