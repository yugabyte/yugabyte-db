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

class IsOperationDoneResult;

namespace client {
class XClusterRemoteClient;
}  // namespace client

namespace master {

class XClusterOutboundReplicationGroupTaskFactory;

class XClusterOutboundReplicationGroup
    : public std::enable_shared_from_this<XClusterOutboundReplicationGroup>,
      public CatalogEntityWithTasks {
 public:
  struct HelperFunctions {
    const std::function<Result<scoped_refptr<NamespaceInfo>>(const NamespaceIdentifierPB&)>
        get_namespace_func;
    const std::function<Result<std::vector<TableInfoPtr>>(const NamespaceId&)> get_tables_func;
    const std::function<Result<std::unique_ptr<XClusterCreateStreamsContext>>(
        const std::vector<TableId>&, const LeaderEpoch&)>
        create_xcluster_streams_func;
    const std::function<Status(
        const std::vector<std::pair<TableId, xrepl::StreamId>>&, StreamCheckpointLocation,
        const LeaderEpoch&, bool, std::function<void(Result<bool>)>)>
        checkpoint_xcluster_streams_func;
    const std::function<Result<DeleteCDCStreamResponsePB>(
        const DeleteCDCStreamRequestPB&, const LeaderEpoch&)>
        delete_cdc_stream_func;

    // SysCatalog functions.
    const std::function<Status(
        const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo*,
        const std::vector<scoped_refptr<CDCStreamInfo>>&)>
        upsert_to_sys_catalog_func;
    const std::function<Status(const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo*)>
        delete_from_sys_catalog_func;
  };

  explicit XClusterOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& outbound_replication_group_pb,
      HelperFunctions helper_functions, scoped_refptr<TasksTracker> tasks_tracker,
      XClusterOutboundReplicationGroupTaskFactory& task_factory);

  virtual ~XClusterOutboundReplicationGroup() = default;

  const xcluster::ReplicationGroupId& Id() const { return outbound_rg_info_->ReplicationGroupId(); }

  std::string ToString() const { return Format("xClusterOutboundReplicationGroup $0", Id()); }
  std::string LogPrefix() const { return Format("$0 :", ToString()); }

  Result<SysXClusterOutboundReplicationGroupEntryPB> GetMetadata() const EXCLUDES(mutex_);

  Status AddNamespaces(const LeaderEpoch& epoch, const std::vector<NamespaceId>& namespace_ids)
      EXCLUDES(mutex_);

  Status AddNamespace(const LeaderEpoch& epoch, const NamespaceId& namespace_id) EXCLUDES(mutex_);

  Status RemoveNamespace(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const std::vector<HostPort>& target_master_addresses) EXCLUDES(mutex_);

  Status RemoveStreams(const std::vector<CDCStreamInfo*>& streams, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  Status Delete(const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

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

  Status AddNamespaceToTarget(
      const std::vector<HostPort>& target_master_addresses, const NamespaceId& source_namespace_id,
      const LeaderEpoch& epoch) const EXCLUDES(mutex_);

  Result<IsOperationDoneResult> IsAlterXClusterReplicationDone(
      const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  bool HasNamespace(const NamespaceId& namespace_id) const EXCLUDES(mutex_);

  void StartPostLoadTasks(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  Status RepairAddTable(
      const NamespaceId& namespace_id, const TableId& table_id, const xrepl::StreamId& stream_id,
      const LeaderEpoch& epoch) EXCLUDES(mutex_);

  Status RepairRemoveTable(const TableId& table_id, const LeaderEpoch& epoch) EXCLUDES(mutex_);

  Result<std::vector<NamespaceId>> GetNamespaces() const EXCLUDES(mutex_);

 private:
  friend class XClusterOutboundReplicationGroupMocked;
  friend class AddTableToXClusterSourceTask;
  friend class XClusterCheckpointNamespaceTask;

  using NamespaceInfoPB = SysXClusterOutboundReplicationGroupEntryPB::NamespaceInfoPB;

  // LockForRead and LockForWrite are used to get a read or write lock on the outbound_rg_info_ and
  // ensure the group is not deleted.
  // The normal Cow object XClusterOutboundReplicationGroupInfo read lock allows concurrent reads
  // and writes. We do not want this behavior, so these functions are guarded with the mutex_.
  Result<XClusterOutboundReplicationGroupInfo::ReadLock> LockForRead() const
      REQUIRES_SHARED(mutex_);
  Result<XClusterOutboundReplicationGroupInfo::WriteLock> LockForWrite() REQUIRES(mutex_);

  Result<scoped_refptr<NamespaceInfo>> GetYbNamespaceInfo(const NamespaceId& namespace_id) const;
  Result<NamespaceName> GetNamespaceName(const NamespaceId& namespace_id) const;

  // Returns true if we added the table. Returns false if the table already existed.
  Result<bool> AddNamespaceInternal(
      const NamespaceId& namespace_id, XClusterOutboundReplicationGroupInfo::WriteLock& l,
      const LeaderEpoch& epoch) REQUIRES(mutex_);

  Status Upsert(
      XClusterOutboundReplicationGroupInfo::WriteLock& l, const LeaderEpoch& epoch,
      const std::vector<scoped_refptr<CDCStreamInfo>>& streams = {}) REQUIRES(mutex_);

  // Deletes all the streams for the given namespace.
  Status DeleteNamespaceStreams(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const SysXClusterOutboundReplicationGroupEntryPB& pb) REQUIRES(mutex_);

  virtual Result<std::shared_ptr<client::XClusterRemoteClient>> GetRemoteClient(
      const std::vector<HostPort>& remote_masters) const;

  // Checks if the namespace is part of this replication group. Caller must hold the read or write
  // lock on outbound_rg_info_. Checks the old_pb
  bool HasNamespaceUnlocked(const NamespaceId& namespace_id) const REQUIRES_SHARED(mutex_);

  Status AddTableInternal(TableInfoPtr table_info, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  Result<NamespaceInfoPB> CreateNamespaceInfo(
      const NamespaceId& namespace_id, const LeaderEpoch& epoch) REQUIRES(mutex_);

  // Returns the NamespaceInfoPB for the given namespace_id. If its not found returns a NotFound
  // status. Caller must hold the WriteLock.
  Result<NamespaceInfoPB*> GetNamespaceInfo(const NamespaceId& namespace_id) REQUIRES(mutex_);
  // Const version of the above. Caller must hold the ReadLock.
  Result<const NamespaceInfoPB*> GetNamespaceInfo(const NamespaceId& namespace_id) const
      REQUIRES_SHARED(mutex_);

  // Graceful version of the above that does not return a bad status.
  // Validates the WriteLock result and returns the NamespaceInfoPB for the given namespace_id. If
  // WriteLock failed due to the replication group being dropped of if the namespace is not found
  // returns nullptr.
  NamespaceInfoPB* GetNamespaceInfoSafe(
      const Result<XClusterOutboundReplicationGroupInfo::WriteLock>& lock_result,
      const NamespaceId& namespace_id) const REQUIRES(mutex_);

  Result<NamespaceInfoPB::TableInfoPB*> GetTableInfo(
      NamespaceInfoPB& ns_info, const TableId& table_id) REQUIRES(mutex_);

  Status CreateStreamForNewTable(
      const NamespaceId& namespace_id, const TableId& table_id, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  Status CheckpointNewTable(
      const NamespaceId& namespace_id, const TableId& table_id, const LeaderEpoch& epoch,
      StdStatusCallback completion_cb) EXCLUDES(mutex_);

  Status MarkNewTablesAsCheckpointed(
      const NamespaceId& namespace_id, const TableId& table_id, const LeaderEpoch& epoch);

  void StartNamespaceCheckpointTasks(
      const std::vector<NamespaceId>& namespace_ids, const LeaderEpoch& epoch);

  // Create streams for tables that were part of the initial bootstrap and do not yet have a stream.
  // This function is idempotent.
  Status CreateStreamsForInitialBootstrap(const NamespaceId& namespace_id, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  // Checkpoint streams for tables that were part of the initial bootstrap and have not yet been
  // checkpointed. This function is idempotent.
  Status CheckpointStreamsForInitialBootstrap(
      const NamespaceId& namespace_id, const LeaderEpoch& epoch,
      std::function<void(XClusterCheckpointStreamsResult)> completion_cb) EXCLUDES(mutex_);

  // Returns False if there are any more table from the initial bootstrap that are pending
  // checkpointing.
  // Returns True if all tables have been checkpointed and namespace is marked as READY.
  //  This function is idempotent.
  Result<bool> MarkBootstrapTablesAsCheckpointed(
      const NamespaceId& namespace_id, XClusterCheckpointStreamsResult checkpoint_result,
      const LeaderEpoch& epoch);

  // If the checkpointing operation failed then set the namespace to FAILED state and record the
  // error.
  void MarkCheckpointNamespaceAsFailed(
      const NamespaceId& namespace_id, const LeaderEpoch& epoch, const Status& status);

  Status VerifyNoTasksInProgress() REQUIRES(mutex_);

  HelperFunctions helper_functions_;

  // Mutex used to ensure reads are not allowed when writes are happening.
  mutable std::shared_mutex mutex_;
  std::unique_ptr<XClusterOutboundReplicationGroupInfo> outbound_rg_info_;

  XClusterOutboundReplicationGroupTaskFactory& task_factory_;

  DISALLOW_COPY_AND_ASSIGN(XClusterOutboundReplicationGroup);
};

}  // namespace master
}  // namespace yb
