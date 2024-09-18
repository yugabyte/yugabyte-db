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

#include <shared_mutex>

#include "yb/cdc/xcluster_types.h"

#include "yb/common/common_fwd.h"
#include "yb/master/master_fwd.h"
#include "yb/master/multi_step_monitored_task.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"

namespace yb {

namespace client {
class XClusterClient;
class XClusterRemoteClientHolder;
class YBClient;
struct YBTableInfo;
}  // namespace client

namespace master {

class GetTableLocationsResponsePB;
class GetTableSchemaResponsePB;
class UniverseReplicationInfo;

// Table specific setup information.
struct XClusterTableSetupInfo {
  TableId target_table_id;
  cdc::StreamEntryPB stream_entry;

  xrepl::StreamId stream_id = xrepl::StreamId::Nil();
  std::unordered_map<std::string, std::string> stream_options = {};

  std::string ToString() const { return YB_STRUCT_TO_STRING(target_table_id, stream_id); }
};

// Helper container to track colocationId and the source to target schema version mapping.
typedef std::vector<std::tuple<ColocationId, SchemaVersion, SchemaVersion>>
    ColocationSchemaVersions;

class XClusterInboundReplicationGroupSetupTask;

// The task that will setup a single table for xCluster. All the info is collected into the
// XClusterTableSetupInfo object and returned via the callback.
// Task will stop running if parent_task.ValidateRunnable returns false.
//
// The table setup involves the following stages:
// 1. TableSchema: Get the table schema from the source universe and validate it is compatible
//    with the target universe. (GetTableSchemaCallback, GetColocatedTabletSchemaCallback).
// 2. ValidateAndSetupStreams: Check if the source table needs to be bootstrapped. Create new
//    xCluster streams, or validate passed in stream id. (GetStreamCallback,
//    CreateXClusterStreamCallback).
// 4. TableLocations: Get the tablet info for the source table and map it to the target tablets
//    (PopulateTabletMapping).
class XClusterTableSetupTask : public MultiStepMonitoredTask {
 public:
  XClusterTableSetupTask(
      std::shared_ptr<XClusterInboundReplicationGroupSetupTask> parent_task,
      const TableId& source_table_id, const xrepl::StreamId& stream_id);

  ~XClusterTableSetupTask() = default;

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kXClusterTableSetup;
  }

  std::string type_name() const override {
    return "Setup table for xCluster inbound replication group";
  }

  std::string description() const override { return log_prefix_; }

  TableId SourceTableId() const { return source_table_id_; }

 private:
  std::string LogPrefix() const { return log_prefix_; }

  Status FirstStep() override;

  Status RegisterTask() override;
  // Release the parent task reference.
  void UnregisterTask() override;

  Status ValidateRunnable() override;

  void TaskCompleted(const Status& status) override;

  static void GetTableSchemaCallback(
      std::shared_ptr<XClusterTableSetupTask> shared_this,
      const std::shared_ptr<client::YBTableInfo>& source_table_info, const Status& s);
  Status ProcessTable(
      const std::shared_ptr<client::YBTableInfo>& source_table_info, const Status& s);

  static void GetTablegroupSchemaCallback(
      std::shared_ptr<XClusterTableSetupTask> shared_this,
      const std::shared_ptr<std::vector<client::YBTableInfo>>& source_table_infos, const Status& s);
  Status ProcessTablegroup(
      const std::shared_ptr<std::vector<client::YBTableInfo>>& source_table_infos, const Status& s);

  Result<GetTableSchemaResponsePB> ValidateSourceSchemaAndGetTargetSchema(
      const client::YBTableInfo& source_table_info);

  Status ValidateBootstrapAndSetupStreams();

  Status ValidateBootstrapNotRequired();

  void SetupStreams();

  Status PopulateTableStreamEntry(
      const TableId& target_table_id, const SchemaVersion& target_schema_version);

  void GetStreamCallback(
      std::shared_ptr<TableId> received_table_id,
      std::shared_ptr<std::unordered_map<std::string, std::string>> options, const Status& s);
  Status ProcessStreamOptions(
      std::shared_ptr<TableId> received_table_id,
      std::shared_ptr<std::unordered_map<std::string, std::string>> options, const Status& s);

  void CreateXClusterStreamCallback(const Result<xrepl::StreamId>& stream_id);
  Status ProcessNewStream(const Result<xrepl::StreamId>& stream_id);

  void PopulateTabletMapping();
  void PopulateTabletMappingCallback(const Result<master::GetTableLocationsResponsePB*>& result);
  Status ProcessTabletMapping(const Result<master::GetTableLocationsResponsePB>& result);

  std::shared_ptr<XClusterInboundReplicationGroupSetupTask> parent_task_;
  const TableId& source_table_id_;
  XClusterTableSetupInfo table_setup_info_;
  std::string log_prefix_;

  DISALLOW_COPY_AND_ASSIGN(XClusterTableSetupTask);
};

// The replication group level task that performs the setup.
// Uses XClusterTableSetupTask to run the per table steps, then performs batched operations like
// UpdateCDCStream and finally writes/updates the UniverseReplicationInfo and ClusterConfig.
// ValidateInputArguments must be called before StartSetup.
// ALTER: If the replication group id has suffix '.ALTER' then performs an alter operation. Alter
// operations add new tables, and namespaces to an existing replication group.
//
// Object lifetime: The caller (XClusterTargetManager) holds a reference to this task until it gets
// a completion result. In the case of failures the caller is free to release the reference to us,
// but we might still have pending work, and child tasks. We keep ourself alive while there is work
// by registering ourself with XClusterManager. We start the child tasks that hold a shared ref to
// us keeping us alive while they are still running. Child tasks themself also register to
// XClusterManager to keep themself alive.
class XClusterInboundReplicationGroupSetupTask : public XClusterInboundReplicationGroupSetupTaskIf,
                                                 public MultiStepMonitoredTask {
 public:
  XClusterInboundReplicationGroupSetupTask(
      Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch,
      xcluster::ReplicationGroupId&& replication_group_id,
      const google::protobuf::RepeatedPtrField<HostPortPB>& source_masters,
      std::vector<TableId>&& source_table_ids, std::vector<xrepl::StreamId>&& stream_ids,
      bool transactional, std::vector<NamespaceId>&& source_namespace_ids,
      std::vector<NamespaceId>&& target_namespace_ids, bool automatic_ddl_mode);

  ~XClusterInboundReplicationGroupSetupTask() = default;

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kXClusterInboundReplicationGroupSetup;
  }

  std::string type_name() const override { return "Setup xCluster inbound replication group"; }

  std::string description() const override { return log_prefix_; }

  xcluster::ReplicationGroupId Id() const override { return replication_group_id_; }

  Status ValidateInputArguments();

  void StartSetup() override { Start(); }

  IsOperationDoneResult DoneResult() const override EXCLUDES(done_result_mutex_);

  // Returns true if the setup was cancelled or it already failed, and false if the setup
  // already completed successfully.
  bool TryCancel() EXCLUDES(done_result_mutex_) override;

 private:
  friend class XClusterTableSetupTask;

  std::string LogPrefix() const { return log_prefix_; }

  client::YBClient& GetYbClient();
  client::XClusterClient& GetXClusterClient();

  Status ValidateRunnable() override EXCLUDES(done_result_mutex_);

  Status RegisterTask() override;
  void UnregisterTask() override;

  Status FirstStep() override EXCLUDES(mutex_);

  void TaskCompleted(const Status& status) override EXCLUDES(done_result_mutex_);

  // Check if the local AutoFlags config is compatible with the source universe and returns the
  // source universe AutoFlags config version if they are compatible. If they are not
  // compatible, returns a bad status. If the source universe is running an older version which
  // does not support AutoFlags compatiblity check, returns an invalid AutoFlags config version.
  Result<uint32> GetAutoFlagConfigVersionIfCompatible();

  void TableTaskCompletionCallback(
      const TableId& source_table_id, const Result<XClusterTableSetupInfo>& table_setup_result)
      EXCLUDES(mutex_);

  Status SetupReplicationAfterProcessingAllTables() EXCLUDES(mutex_);

  // Update the source to let it know that the bootstrapping is complete. We set the stream
  // state to ACTIVE and set the transactional flag.
  Status UpdateSourceStreamOptions() EXCLUDES(mutex_);

  Status ValidateNamespaceListForDbScoped();
  Status ValidateTableListForDbScoped() REQUIRES(mutex_);

  Status SetupReplicationGroup() EXCLUDES(mutex_);

  Result<scoped_refptr<UniverseReplicationInfo>> CreateNewUniverseReplicationInfo()
      REQUIRES(mutex_);
  void PopulateUniverseReplication(SysUniverseReplicationEntryPB& universe_pb) REQUIRES(mutex_);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  XClusterManager& xcluster_manager_;
  const LeaderEpoch epoch_;

  const xcluster::ReplicationGroupId replication_group_id_;
  const google::protobuf::RepeatedPtrField<HostPortPB> source_masters_;
  // The following lists are preserved in the same order they were provided.
  const std::vector<TableId> source_table_ids_;
  const std::vector<xrepl::StreamId> stream_ids_;
  const std::vector<NamespaceId> source_namespace_ids_;
  const std::vector<NamespaceId> target_namespace_ids_;

  const bool is_alter_replication_;
  const bool stream_ids_provided_;
  const bool transactional_;  // Not used in ALTER.
  const bool is_db_scoped_;  // Not used in ALTER.
  const bool automatic_ddl_mode_;  // Not used in ALTER.

  std::string log_prefix_;

  std::shared_ptr<client::XClusterRemoteClientHolder> remote_client_;

  IF_DEBUG_MODE(bool argument_validation_done_ = false);

  mutable std::shared_mutex done_result_mutex_;
  IsOperationDoneResult done_result_ GUARDED_BY(done_result_mutex_) =
      IsOperationDoneResult::NotDone();

  // Map of source table id to XClusterTableSetupInfo.
  mutable std::shared_mutex mutex_;
  std::unordered_map<TableId, XClusterTableSetupInfo> source_table_infos_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(XClusterInboundReplicationGroupSetupTask);
};

}  // namespace master
}  // namespace yb
