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

#include "yb/cdc/xcluster_types.h"
#include "yb/cdc/xrepl_types.h"
#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/gutil/ref_counted.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/util/cow_object.h"
#include "yb/util/status_fwd.h"
#include <google/protobuf/repeated_field.h>

namespace yb {

class HostPortPB;

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace client {
struct YBTableInfo;
class YBTableName;
}  // namespace client

namespace master {

class GetTableSchemaResponsePB;
struct PersistentUniverseReplicationInfo;
class SetupUniverseReplicationRequestPB;
class SetupUniverseReplicationResponsePB;
class SysCatalogTable;
class UniverseReplicationInfo;
class XClusterRpcTasks;

// Helper class to handle SetupUniverseReplication RPC.
// This object will only live as long as the operation is in progress.
class SetupUniverseReplicationHelper : public RefCountedThreadSafe<SetupUniverseReplicationHelper> {
 public:
  ~SetupUniverseReplicationHelper();

  static Status Setup(
      Master& master, CatalogManager& catalog_manager, const SetupUniverseReplicationRequestPB* req,
      SetupUniverseReplicationResponsePB* resp, const LeaderEpoch& epoch);

  static Status ValidateMasterAddressesBelongToDifferentCluster(
      Master& master, const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses);

 private:
  SetupUniverseReplicationHelper(
      Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch);

  struct SetupReplicationInfo {
    std::unordered_map<TableId, xrepl::StreamId> table_bootstrap_ids;
    bool transactional;
  };

  // Helper container to track colocationId and the producer to consumer schema version mapping.
  typedef std::vector<std::tuple<ColocationId, SchemaVersion, SchemaVersion>>
      ColocationSchemaVersions;

  typedef std::vector<
      std::tuple<xrepl::StreamId, TableId, std::unordered_map<std::string, std::string>>>
      StreamUpdateInfos;

  Status SetupUniverseReplication(
      const SetupUniverseReplicationRequestPB* req, SetupUniverseReplicationResponsePB* resp);

  void MarkUniverseReplicationFailed(
      scoped_refptr<UniverseReplicationInfo> universe, const Status& failure_status);
  // Sets the appropriate failure state and the error status on the universe and commits the
  // mutation to the sys catalog.
  void MarkUniverseReplicationFailed(
      const Status& failure_status, CowWriteLock<PersistentUniverseReplicationInfo>* universe_lock,
      scoped_refptr<UniverseReplicationInfo> universe);

  void GetTableSchemaCallback(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::shared_ptr<client::YBTableInfo>& producer_info,
      const SetupReplicationInfo& setup_info, const Status& s);

  Status GetTablegroupSchemaCallbackInternal(
      scoped_refptr<UniverseReplicationInfo>& universe,
      const std::vector<client::YBTableInfo>& infos, const TablegroupId& producer_tablegroup_id,
      const SetupReplicationInfo& setup_info, const Status& s);

  void GetTablegroupSchemaCallback(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
      const TablegroupId& producer_tablegroup_id, const SetupReplicationInfo& setup_info,
      const Status& s);

  void GetColocatedTabletSchemaCallback(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::shared_ptr<std::vector<client::YBTableInfo>>& info,
      const SetupReplicationInfo& setup_info, const Status& s);

  Status ValidateTableAndCreateStreams(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::shared_ptr<client::YBTableInfo>& producer_info,
      const SetupReplicationInfo& setup_info);

  // Validates a single table's schema with the corresponding table on the consumer side, and
  // updates consumer_table_id with the new table id. Return the consumer table schema if the
  // validation is successful.
  Status ValidateTableSchemaForXCluster(
      const client::YBTableInfo& info, const SetupReplicationInfo& setup_info,
      GetTableSchemaResponsePB* resp);

  // Consumer API: Find out if bootstrap is required for the Producer tables.
  Status IsBootstrapRequiredOnProducer(
      scoped_refptr<UniverseReplicationInfo> universe, const TableId& producer_table,
      const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids);

  Status AddValidatedTableAndCreateStreams(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids,
      const TableId& producer_table, const TableId& consumer_table,
      const ColocationSchemaVersions& colocated_schema_versions);

  // Adds a validated table to the sys catalog table map for the given universe
  Status AddValidatedTableToUniverseReplication(
      scoped_refptr<UniverseReplicationInfo> universe, const TableId& producer_table,
      const TableId& consumer_table, const SchemaVersion& producer_schema_version,
      const SchemaVersion& consumer_schema_version,
      const ColocationSchemaVersions& colocated_schema_versions);

  // If all tables have been validated, creates a CDC stream for each table.
  Status CreateStreamsIfReplicationValidated(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids);

  void GetStreamCallback(
      const xrepl::StreamId& bootstrap_id, std::shared_ptr<TableId> table_id,
      std::shared_ptr<std::unordered_map<std::string, std::string>> options,
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table,
      std::shared_ptr<XClusterRpcTasks> xcluster_rpc, const Status& s,
      std::shared_ptr<StreamUpdateInfos> stream_update_infos,
      std::shared_ptr<std::mutex> update_infos_lock);

  void AddStreamToUniverseAndInitConsumer(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table,
      const Result<xrepl::StreamId>& stream_id, std::function<Status()> on_success_cb = nullptr);

  Status AddStreamToUniverseAndInitConsumerInternal(
      scoped_refptr<UniverseReplicationInfo> universe, const TableId& table,
      const xrepl::StreamId& stream_id, std::function<Status()> on_success_cb);

  Status InitXClusterConsumer(
      const std::vector<XClusterConsumerStreamInfo>& consumer_info, const std::string& master_addrs,
      UniverseReplicationInfo& replication_info,
      std::shared_ptr<XClusterRpcTasks> xcluster_rpc_tasks);

  Status MergeUniverseReplication(
      scoped_refptr<UniverseReplicationInfo> info, xcluster::ReplicationGroupId original_id,
      std::function<Status()> on_success_cb);

  Result<scoped_refptr<UniverseReplicationInfo>> CreateUniverseReplicationInfo(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses,
      const std::vector<NamespaceId>& producer_namespace_ids,
      const std::vector<NamespaceId>& consumer_namespace_ids,
      const google::protobuf::RepeatedPtrField<std::string>& table_ids, bool transactional);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  XClusterManager& xcluster_manager_;
  const LeaderEpoch epoch_;

  DISALLOW_COPY_AND_ASSIGN(SetupUniverseReplicationHelper);
};

}  // namespace master
}  // namespace yb
