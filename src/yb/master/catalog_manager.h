// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_fwd.hpp>
#include <boost/functional/hash.hpp>
#include <gtest/internal/gtest-internal.h>

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/restore_sys_catalog_state.h"
#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"
#include "yb/common/transaction.h"
#include "yb/client/client_fwd.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/cdc_split_driver.h"
#include "yb/master/clone/clone_state_manager.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_dcl.fwd.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_encryption.fwd.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_types.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/snapshot_coordinator_context.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_initialization.h"
#include "yb/master/system_tablet.h"
#include "yb/master/table_index.h"
#include "yb/master/tablet_creation_limits.h"
#include "yb/master/tablet_split_candidate_filter.h"
#include "yb/master/tablet_split_driver.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/ysql_tablespace_manager.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/scheduler.h"
#include "yb/server/monitored_task.h"
#include "yb/tserver/tablet_peer_lookup.h"

#include "yb/util/async_task_util.h"
#include "yb/util/debug/lock_debug.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/promise.h"
#include "yb/util/random.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_fwd.h"
#include "yb/util/test_macros.h"
#include "yb/util/version_tracker.h"

namespace yb {

class Schema;
class ThreadPool;
class AddTransactionStatusTabletRequestPB;
class AddTransactionStatusTabletResponsePB;
class UniverseKeyRegistryPB;
class IsOperationDoneResult;

template<class T>
class AtomicGauge;

#define CALL_GTEST_TEST_CLASS_NAME_(...) GTEST_TEST_CLASS_NAME_(__VA_ARGS__)

#define VERIFY_NAMESPACE_FOUND(expr, resp) \
  RESULT_CHECKER_HELPER( \
      expr, \
      if (!__result.ok()) { return SetupError((resp)->mutable_error(), __result.status()); });

namespace pgwrapper {
class CALL_GTEST_TEST_CLASS_NAME_(PgMiniTest, DropDBMarkDeleted);
class CALL_GTEST_TEST_CLASS_NAME_(PgMiniTest, DropDBUpdateSysTablet);
class CALL_GTEST_TEST_CLASS_NAME_(PgMiniTest, DropDBWithTables);
}

class CALL_GTEST_TEST_CLASS_NAME_(MasterPartitionedTest, VerifyOldLeaderStepsDown);
#undef CALL_GTEST_TEST_CLASS_NAME_

namespace tablet {

struct TableInfo;
enum RaftGroupStatePB;

}

namespace cdc {
class CDCStateTable;
}  // namespace cdc

namespace master {

struct DeferredAssignmentActions;
struct SysCatalogLoadingState;
struct KeyRange;

using PlacementId = std::string;

typedef std::unordered_map<TabletId, TabletServerId> TabletToTabletServerMap;

typedef std::unordered_map<TablespaceId, boost::optional<ReplicationInfoPB>>
  TablespaceIdToReplicationInfoMap;

typedef std::unordered_map<TableId, boost::optional<TablespaceId>> TableToTablespaceIdMap;

typedef std::unordered_map<TableId, std::vector<scoped_refptr<TabletInfo>>> TableToTabletInfos;

typedef std::unordered_map<TableId, xrepl::StreamId> TableBootstrapIdsMap;

constexpr int32_t kInvalidClusterConfigVersion = 0;

YB_DEFINE_ENUM(
    CDCSDKStreamCreationState,
    // Stream has been initialized but no in-memory data structures or sys-catalog have been
    // modified.
    (kInitialized)
    // Stream has been added to the in-memory maps.
    (kAddedToMaps)
    // Stream has been added to the in-memory maps and sys-catalog. Also an in-memory mutation has
    // been created but not yet committed.
    (kPreCommitMutation)
    // Stream has been added to the in-memory maps and sys-catalog. The in-memory mutation has
    // been committed.
    (kPostCommitMutation)
    // Stream has been created successfully and is ready to be streamed.
    (kReady)
);

const std::string& GetIndexedTableId(const SysTablesEntryPB& pb);

YB_DEFINE_ENUM(YsqlDdlVerificationState,
    (kDdlInProgress)
    (kDdlPostProcessing)
    (kDdlPostProcessingFailed));

YB_DEFINE_ENUM(TxnState, (kUnknown) (kCommitted) (kAborted));

struct YsqlTableDdlTxnState;

// The component of the master which tracks the state and location
// of tables/tablets in the cluster.
//
// This is the master-side counterpart of TSTabletManager, which tracks
// the state of each tablet on a given tablet-server.
//
// Thread-safe.
class CatalogManager : public tserver::TabletPeerLookupIf,
                       public TabletSplitCandidateFilterIf,
                       public TabletSplitDriverIf,
                       public CatalogManagerIf,
                       public CDCSplitDriverIf,
                       public SnapshotCoordinatorContext {
  typedef std::unordered_map<NamespaceName, scoped_refptr<NamespaceInfo> > NamespaceInfoMap;

  class NamespaceNameMapper {
   public:
    NamespaceInfoMap& operator[](YQLDatabase db_type);
    const NamespaceInfoMap& operator[](YQLDatabase db_type) const;
    void clear();
    std::vector<scoped_refptr<NamespaceInfo>> GetAll() const;

   private:
    std::array<NamespaceInfoMap, 4> typed_maps_;
  };

 public:
  explicit CatalogManager(Master *master);
  virtual ~CatalogManager();

  Status Init();

  bool StartShutdown();
  void CompleteShutdown();

  // Create Postgres sys catalog table.
  // If a non-null value of change_meta_req is passed then it does not
  // add the ysql sys table into the raft metadata but adds it in the request
  // pb. The caller is then responsible for performing the ChangeMetadataOperation.
  Status CreateYsqlSysTable(
      const CreateTableRequestPB* req, CreateTableResponsePB* resp, const LeaderEpoch& epoch,
      tablet::ChangeMetadataRequestPB* change_meta_req = nullptr,
      SysCatalogWriter* writer = nullptr);

  Status ReplicatePgMetadataChange(const tablet::ChangeMetadataRequestPB* req);

  // Reserve Postgres oids for a Postgres database.
  Status ReservePgsqlOids(const ReservePgsqlOidsRequestPB* req,
                          ReservePgsqlOidsResponsePB* resp,
                          rpc::RpcContext* rpc);

  // Get the info (current only version) for the ysql system catalog.
  Status GetYsqlCatalogConfig(const GetYsqlCatalogConfigRequestPB* req,
                              GetYsqlCatalogConfigResponsePB* resp,
                              rpc::RpcContext* rpc);

  // Copy Postgres sys catalog tables into a new namespace.
  Status CopyPgsqlSysTables(
      const NamespaceId& namespace_id, const std::vector<scoped_refptr<TableInfo>>& tables,
      const LeaderEpoch& epoch);

  // Create a new Table with the specified attributes.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status CreateTable(const CreateTableRequestPB* req,
                     CreateTableResponsePB* resp,
                     rpc::RpcContext* rpc,
                     const LeaderEpoch& epoch) override;

  Status CreateTableIfNotFound(
      const std::string& namespace_name, const std::string& table_name,
      std::function<Result<CreateTableRequestPB>()> generate_request, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  // Create a new transaction status table.
  Status CreateTransactionStatusTable(const CreateTransactionStatusTableRequestPB* req,
                                      CreateTransactionStatusTableResponsePB* resp,
                                      rpc::RpcContext *rpc, const LeaderEpoch& epoch);

  // Create a transaction status table with the given name.
  Status CreateTransactionStatusTableInternal(rpc::RpcContext* rpc,
                                              const std::string& table_name,
                                              const TablespaceId* tablespace_id,
                                              const LeaderEpoch& epoch,
                                              const ReplicationInfoPB* replication_info);

  // Add a tablet to a transaction status table.
  Status AddTransactionStatusTablet(const AddTransactionStatusTabletRequestPB* req,
                                    AddTransactionStatusTabletResponsePB* resp,
                                    rpc::RpcContext* rpc,
                                    const LeaderEpoch& epoch);

  // Check if there is a transaction table whose tablespace id matches the given tablespace id.
  bool DoesTransactionTableExistForTablespace(
      const TablespaceId& tablespace_id) EXCLUDES(mutex_);

  // Create a local transaction status table for a tablespace if needed
  // (i.e., if it does not exist already).
  //
  // This is called during CreateTable if the table has transactions enabled and is part
  // of a tablespace with a placement set.
  Status CreateLocalTransactionStatusTableIfNeeded(
      rpc::RpcContext* rpc, const TablespaceId& tablespace_id, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  // Get tablet ids of the global transaction status table.
  Status GetGlobalTransactionStatusTablets(
      GetTransactionStatusTabletsResponsePB* resp) EXCLUDES(mutex_);

  // Get ids of transaction status tables matching a given placement.
  Result<std::vector<TableInfoPtr>> GetPlacementLocalTransactionStatusTables(
      const CloudInfoPB& placement) EXCLUDES(mutex_);

  // Get tablet ids of local transaction status tables matching a given placement.
  Status GetPlacementLocalTransactionStatusTablets(
      const std::vector<TableInfoPtr>& placement_local_tables,
      GetTransactionStatusTabletsResponsePB* resp) EXCLUDES(mutex_);

  // Get tablet ids of the global transaction status table and local transaction status tables
  // matching a given placement.
  Status GetTransactionStatusTablets(const GetTransactionStatusTabletsRequestPB* req,
                                     GetTransactionStatusTabletsResponsePB* resp,
                                     rpc::RpcContext *rpc) EXCLUDES(mutex_);

  // Create the metrics snapshots table if needed (i.e. if it does not exist already).
  //
  // This is called at the end of CreateTable.
  Status CreateMetricsSnapshotsTableIfNeeded(const LeaderEpoch& epoch, rpc::RpcContext *rpc);

  Status CreateStatefulService(
      const StatefulServiceKind& service_kind, const client::YBSchema& yb_schema,
      const LeaderEpoch& epoch);

  Status CreateTestEchoService(const LeaderEpoch& epoch);

  Status CreatePgAutoAnalyzeService(const LeaderEpoch& epoch);

  // Get the information about an in-progress create operation.
  Status IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                           IsCreateTableDoneResponsePB* resp) override;

  Status IsCreateTableInProgress(const TableId& table_id,
                                 CoarseTimePoint deadline,
                                 bool* create_in_progress);

  Status WaitForCreateTableToFinish(const TableId& table_id, CoarseTimePoint deadline);

  Status IsAlterTableInProgress(const TableId& table_id,
                                 CoarseTimePoint deadline,
                                 bool* alter_in_progress);

  Status WaitForAlterTableToFinish(const TableId& table_id, CoarseTimePoint deadline);

  void ScheduleVerifyTablePgLayer(TransactionMetadata txn,
      const TableInfoPtr& table, const LeaderEpoch& epoch);

  // Called when transaction associated with table create finishes. Verifies postgres layer present.
  Status VerifyTablePgLayer(scoped_refptr<TableInfo> table, Result<bool> exists,
    const LeaderEpoch& epoch);

  // Truncate the specified table.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status TruncateTable(const TruncateTableRequestPB* req,
                       TruncateTableResponsePB* resp,
                       rpc::RpcContext* rpc,
                       const LeaderEpoch& epoch);

  // Get the information about an in-progress truncate operation.
  Status IsTruncateTableDone(const IsTruncateTableDoneRequestPB* req,
                             IsTruncateTableDoneResponsePB* resp);

  // Backfill the specified index.  Currently only supported for YSQL.  YCQL does not need this as
  // master automatically runs backfill according to the DocDB permissions.
  Status BackfillIndex(const BackfillIndexRequestPB* req,
                       BackfillIndexResponsePB* resp,
                       rpc::RpcContext* rpc,
                       const LeaderEpoch& epoch);

  // Gets the backfill jobs state associated with the requested table.
  Status GetBackfillJobs(const GetBackfillJobsRequestPB* req,
                         GetBackfillJobsResponsePB* resp,
                         rpc::RpcContext* rpc);

  // Gets the backfilling status of the specified index tables. The response will contain all the
  // specified tables with either their index permissions or an error in the corresponding field.
  Status GetBackfillStatus(const GetBackfillStatusRequestPB* req,
                           GetBackfillStatusResponsePB* resp,
                           rpc::RpcContext* rpc);

  // Gets the backfilling status of the specified index tables. The result is provided via
  // the callback for every index from the indexes argument. If the indexes argument is empty,
  // the result is provided for every index of the specified indexed table. The callback must have
  // the following signature: void (const Status&, const TableId&, IndexStatusPB::BackfillStatus).
  void GetBackfillStatus(const TableId& indexed_table_id, TableIdSet&& indexes, auto&& callback);

  // Backfill the indexes for the specified table.
  // Used for backfilling YCQL defered indexes when triggered from yb-admin.
  Status LaunchBackfillIndexForTable(const LaunchBackfillIndexForTableRequestPB* req,
                                     LaunchBackfillIndexForTableResponsePB* resp,
                                     rpc::RpcContext* rpc,
                                     const LeaderEpoch& epoch);

  // Gets the progress of ongoing index backfills.
  Status GetIndexBackfillProgress(const GetIndexBackfillProgressRequestPB* req,
                                  GetIndexBackfillProgressResponsePB* resp,
                                  rpc::RpcContext* rpc);

  // Schedules a table deletion to run as a background task.
  Status ScheduleDeleteTable(const scoped_refptr<TableInfo>& table, const LeaderEpoch& epoch);

  // Delete the specified table.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status DeleteTable(const DeleteTableRequestPB* req,
                     DeleteTableResponsePB* resp,
                     rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status DeleteTableInternal(
      const DeleteTableRequestPB* req, DeleteTableResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // Get the information about an in-progress delete operation.
  Status IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                           IsDeleteTableDoneResponsePB* resp);

  // Alter the specified table.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status AlterTable(const AlterTableRequestPB* req,
                    AlterTableResponsePB* resp,
                    rpc::RpcContext* rpc,
                    const LeaderEpoch& epoch);

  Status UpdateSysCatalogWithNewSchema(
    const scoped_refptr<TableInfo>& table,
    const std::vector<DdlLogEntry>& ddl_log_entries,
    const std::string& new_namespace_id,
    const std::string& new_table_name,
    const LeaderEpoch& epoch,
    AlterTableResponsePB* resp);

  // Get the information about an in-progress alter operation.
  Status IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                          IsAlterTableDoneResponsePB* resp);

  Result<NamespaceId> GetTableNamespaceId(TableId table_id) EXCLUDES(mutex_);

  Status ScheduleYsqlTxnVerification(const TableInfoPtr& table,
                                     const TransactionMetadata& txn, const LeaderEpoch& epoch)
                                     EXCLUDES(ddl_txn_verifier_mutex_);

  // If YsqlDdlVerificationState already exists for 'txn', update it by adding an entry for 'table'.
  // Otherwise, create a new YsqlDdlVerificationState for 'txn' with an entry for 'table'.
  // Returns true if a new YsqlDdlVerificationState was created.
  bool CreateOrUpdateDdlTxnVerificationState(
      const TableInfoPtr& table, const TransactionMetadata& txn)
      EXCLUDES(ddl_txn_verifier_mutex_);

  // Schedules a task to find the status of 'txn' and update the schema of 'table' based on whether
  // 'txn' was committed or aborted. This function should only ever be invoked after using
  // the above CreateOrUpdateDdlTxnVerificationState to verify that no task for the same transaction
  // has already been invoked. Scheduling two tasks for the same transaction will not lead to any
  // correctness issues, but will lead to unnecessary work (i.e. polling the transaction
  // coordinator and performing schema comparison).
  Status ScheduleVerifyTransaction(const TableInfoPtr& table,
                                   const TransactionMetadata& txn, const LeaderEpoch& epoch);

  Status YsqlTableSchemaChecker(TableInfoPtr table,
                                const std::string& pb_txn_id,
                                Result<bool> is_committed,
                                const LeaderEpoch& epoch);

  Status YsqlDdlTxnCompleteCallback(const std::string& pb_txn_id,
                                    bool is_committed,
                                    const LeaderEpoch& epoch);

  Status YsqlDdlTxnCompleteCallbackInternal(
      TableInfo* table, const TransactionId& txn_id, bool success, const LeaderEpoch& epoch);

  Status HandleSuccessfulYsqlDdlTxn(const YsqlTableDdlTxnState txn_data);

  Status HandleAbortedYsqlDdlTxn(const YsqlTableDdlTxnState txn_data);

  Status ClearYsqlDdlTxnState(const YsqlTableDdlTxnState txn_data);

  Status YsqlDdlTxnAlterTableHelper(const YsqlTableDdlTxnState txn_data,
                                    const std::vector<DdlLogEntry>& ddl_log_entries,
                                    const std::string& new_table_name,
                                    bool success);

  Status YsqlDdlTxnDropTableHelper(const YsqlTableDdlTxnState txn_data);

  Status WaitForDdlVerificationToFinish(
      const TableInfoPtr& table, const std::string& pb_txn_id);

  void UpdateDdlVerificationState(const TransactionId& txn, YsqlDdlVerificationState state);

  void RemoveDdlTransactionState(
      const TableId& table_id, const std::vector<TransactionId>& txn_ids);

  Status TriggerDdlVerificationIfNeeded(const TransactionMetadata& txn, const LeaderEpoch& epoch);

  // Get the information about the specified table.
  Status GetTableSchema(const GetTableSchemaRequestPB* req,
                        GetTableSchemaResponsePB* resp) override;
  Status GetTableSchemaInternal(const GetTableSchemaRequestPB* req,
                                GetTableSchemaResponsePB* resp,
                                bool get_fully_applied_indexes = false);

  // Get the information about the specified tablegroup.
  Status GetTablegroupSchema(const GetTablegroupSchemaRequestPB* req,
                             GetTablegroupSchemaResponsePB* resp);

  // Get the information about the specified colocated databsae.
  Status GetColocatedTabletSchema(const GetColocatedTabletSchemaRequestPB* req,
                                  GetColocatedTabletSchemaResponsePB* resp);

  // List all the running tables.
  Status ListTables(const ListTablesRequestPB* req,
                    ListTablesResponsePB* resp) override;

  Status GetTableLocations(const GetTableLocationsRequestPB* req,
                           GetTableLocationsResponsePB* resp) override;

  // Lookup tablet by ID, then call GetTabletLocations below.
  Status GetTabletLocations(
      const TabletId& tablet_id,
      TabletLocationsPB* locs_pb,
      IncludeInactive include_inactive) override;

  // Look up the locations of the given tablet. The locations
  // vector is overwritten (not appended to).
  // If the tablet is not found, returns Status::NotFound.
  // If the tablet is not running, returns Status::ServiceUnavailable.
  // Otherwise, returns Status::OK and puts the result in 'locs_pb'.
  // This only returns tablets which are in RUNNING state.
  Status GetTabletLocations(
      scoped_refptr<TabletInfo> tablet_info,
      TabletLocationsPB* locs_pb,
      IncludeInactive include_inactive) override;

  // Returns the system tablet in catalog manager by the id.
  Result<std::shared_ptr<tablet::AbstractTablet>> GetSystemTablet(const TabletId& id) override;

  // TODO(asrivastava): Get rid of this struct and move the logic for this function into the
  // master heartbeat service code.
  struct ReportedTablet {
    TabletId tablet_id;
    TabletInfoPtr info;
    const ReportedTabletPB* report;
    std::map<TableId, scoped_refptr<TableInfo>> tables;
  };
  using ReportedTablets = std::vector<ReportedTablet>;
  void GetReportedAndOrphanedTabletsFromReport(
      int num_tablets,
      const TabletReportPB& full_report,
      TabletReportUpdatesPB* full_report_update,
      ReportedTablets* reported_tablets,
      std::set<TabletId>* orphaned_tablets);

  // Send the "delete tablet request" to the specified TS/tablet.
  // The specified 'reason' will be logged on the TS.
  void SendDeleteTabletRequest(const TabletId& tablet_id,
                               tablet::TabletDataState delete_type,
                               const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
                               const scoped_refptr<TableInfo>& table,
                               TSDescriptor* ts_desc,
                               const std::string& reason,
                               const LeaderEpoch& epoch,
                               HideOnly hide_only = HideOnly::kFalse,
                               KeepData keep_data = KeepData::kFalse);

  std::shared_ptr<AsyncDeleteReplica> MakeDeleteReplicaTask(
      const TabletServerId& peer_uuid, const TableInfoPtr& table, const TabletId& tablet_id,
      tablet::TabletDataState delete_type,
      boost::optional<int64_t> cas_config_opid_index_less_or_equal, LeaderEpoch epoch,
      const std::string& reason);

  void SetTabletReplicaLocations(
      const TabletInfoPtr& tablet, const std::shared_ptr<TabletReplicaMap>& replica_locations);
  void UpdateTabletReplicaLocations(const TabletInfoPtr& tablet, const TabletReplica& replica);

  void WakeBgTaskIfPendingUpdates();

  // Get the ycql system.partitions vtable. Note that this has EXCLUDES(mutex_), in order to
  // maintain lock ordering.
  const YQLPartitionsVTable& GetYqlPartitionsVtable() const EXCLUDES(mutex_);

  Status HandleTabletSchemaVersionReport(
      TabletInfo *tablet, uint32_t version,
      const LeaderEpoch& epoch,
      const scoped_refptr<TableInfo>& table = nullptr) override;

  // For a table that is currently in PREPARING state, if all its tablets have transitioned to
  // RUNNING state, then collect and start the required post tablet creation async tasks. Table is
  // advanced to the RUNNING state after all of these tasks complete successfully.
  // new_running_tablets is the new set of tablets that are being transitioned to RUNNING state
  // (dirty copy is modified) and yet to be persisted. These should be persisted before the table
  // lock is released. Note:
  //    WriteLock on the table is required.
  void SchedulePostTabletCreationTasks(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch,
      const std::set<TabletId>& new_running_tablets = {});

  void StartElectionIfReady(
      const consensus::ConsensusStatePB& cstate, const LeaderEpoch& epoch, TabletInfo* tablet);

  Result<bool> IsTableUndergoingPitrRestore(const TableInfo& table_info);

  // Register the tablet server with the ts manager using the Raft config. This is called for
  // servers that are part of the Raft config but haven't registered as yet.
  Status RegisterTsFromRaftConfig(const consensus::RaftPeerPB& peer);

  // Create a new Namespace with the specified attributes.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status CreateNamespace(const CreateNamespaceRequestPB* req,
                         CreateNamespaceResponsePB* resp,
                         rpc::RpcContext* rpc, const LeaderEpoch& epoch) override;
  // Get the information about an in-progress create operation.
  Status IsCreateNamespaceDone(const IsCreateNamespaceDoneRequestPB* req,
                               IsCreateNamespaceDoneResponsePB* resp);

  // Delete the specified Namespace.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status DeleteNamespace(const DeleteNamespaceRequestPB* req,
                         DeleteNamespaceResponsePB* resp,
                         rpc::RpcContext* rpc,
                         const LeaderEpoch& epoch);
  // Get the information about an in-progress delete operation.
  Status IsDeleteNamespaceDone(const IsDeleteNamespaceDoneRequestPB* req,
                               IsDeleteNamespaceDoneResponsePB* resp);

  // Alter the specified Namespace.
  Status AlterNamespace(const AlterNamespaceRequestPB* req,
                        AlterNamespaceResponsePB* resp,
                        rpc::RpcContext* rpc);

  // User API to Delete YSQL database tables.
  Status DeleteYsqlDatabase(const DeleteNamespaceRequestPB* req,
                            DeleteNamespaceResponsePB* resp,
                            rpc::RpcContext* rpc,
                            const LeaderEpoch& epoch);

  // Work to delete YSQL database tables, handled asynchronously from the User API call.
  void DeleteYsqlDatabaseAsync(scoped_refptr<NamespaceInfo> database, const LeaderEpoch& epoch);

  // Delete all tables in YSQL database.
  Status DeleteYsqlDBTables(const scoped_refptr<NamespaceInfo>& database, const LeaderEpoch& epoch);

  // List all the current namespaces.
  Status ListNamespaces(const ListNamespacesRequestPB* req,
                        ListNamespacesResponsePB* resp);

  // Get information about a namespace.
  Status GetNamespaceInfo(const GetNamespaceInfoRequestPB* req,
                          GetNamespaceInfoResponsePB* resp,
                          rpc::RpcContext* rpc);

  // Set Redis Config
  Status RedisConfigSet(const RedisConfigSetRequestPB* req,
                        RedisConfigSetResponsePB* resp,
                        rpc::RpcContext* rpc);

  // Get Redis Config
  Status RedisConfigGet(const RedisConfigGetRequestPB* req,
                        RedisConfigGetResponsePB* resp,
                        rpc::RpcContext* rpc);

  Status CreateTablegroup(
      const CreateTablegroupRequestPB* req, CreateTablegroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status DeleteTablegroup(const DeleteTablegroupRequestPB* req,
                          DeleteTablegroupResponsePB* resp,
                          rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  // List all the current tablegroups for a namespace.
  Status ListTablegroups(const ListTablegroupsRequestPB* req,
                         ListTablegroupsResponsePB* resp,
                         rpc::RpcContext* rpc);

  // Create a new User-Defined Type with the specified attributes.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status CreateUDType(const CreateUDTypeRequestPB* req,
                      CreateUDTypeResponsePB* resp,
                      rpc::RpcContext* rpc);

  // Delete the specified UDType.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status DeleteUDType(const DeleteUDTypeRequestPB* req,
                      DeleteUDTypeResponsePB* resp,
                      rpc::RpcContext* rpc);

  // List all user defined types in given namespaces.
  Status ListUDTypes(const ListUDTypesRequestPB* req,
                     ListUDTypesResponsePB* resp);

  // Get the info (id, name, namespace, fields names, field types) of a (user-defined) type.
  Status GetUDTypeInfo(const GetUDTypeInfoRequestPB* req,
                       GetUDTypeInfoResponsePB* resp,
                       rpc::RpcContext* rpc);

  // Disables tablet splitting for a specified amount of time.
  Status DisableTabletSplitting(
      const DisableTabletSplittingRequestPB* req, DisableTabletSplittingResponsePB* resp,
      rpc::RpcContext* rpc);

  void DisableTabletSplittingInternal(const MonoDelta& duration, const std::string& feature);

  // Returns true if there are no outstanding tablets and the tablet split manager is not currently
  // processing tablet splits.
  Status IsTabletSplittingComplete(
      const IsTabletSplittingCompleteRequestPB* req, IsTabletSplittingCompleteResponsePB* resp,
      rpc::RpcContext* rpc);

  bool IsTabletSplittingCompleteInternal(bool wait_for_parent_deletion, CoarseTimePoint deadline);

  // Delete CDC streams metadata for a table.
  Status DropCDCSDKStreams(const std::unordered_set<TableId>& table_ids) EXCLUDES(mutex_);

  // Add new table metadata to all CDCSDK streams of required namespace.
  Status AddNewTableToCDCDKStreamsMetadata(const TableId& table_id, const NamespaceId& ns_id)
      EXCLUDES(mutex_);

  Status XReplValidateSplitCandidateTable(const TableId& table_id) const override;

  Status ChangeEncryptionInfo(
      const ChangeEncryptionInfoRequestPB* req, ChangeEncryptionInfoResponsePB* resp);

  Status GetFullUniverseKeyRegistry(const GetFullUniverseKeyRegistryRequestPB* req,
                                    GetFullUniverseKeyRegistryResponsePB* resp);

  Status UpdateXClusterConsumerOnTabletSplit(
      const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids) override;

  Status UpdateCDCProducerOnTabletSplit(
      const TableId& producer_table_id, const SplitTabletIds& split_tablet_ids) override;

  Result<uint64_t> IncrementYsqlCatalogVersion() override;

  // Records the fact that initdb has succesfully completed.
  Status InitDbFinished(Status initdb_status, int64_t term);

  // Check if the initdb operation has been completed. This is intended for use by whoever wants
  // to wait for the cluster to be fully initialized, e.g. minicluster, YugaWare, etc.
  Status IsInitDbDone(
      const IsInitDbDoneRequestPB* req, IsInitDbDoneResponsePB* resp) override;

  Status GetYsqlCatalogVersion(
      uint64_t* catalog_version, uint64_t* last_breaking_version) override;
  Status GetYsqlAllDBCatalogVersions(
      bool use_cache, DbOidToCatalogVersionMap* versions, uint64_t* fingerprint) override
      EXCLUDES(heartbeat_pg_catalog_versions_cache_mutex_);
  Status GetYsqlDBCatalogVersion(
      uint32_t db_oid, uint64_t* catalog_version, uint64_t* last_breaking_version) override;

  Status IsCatalogVersionTableInPerdbMode(bool* perdb_mode);

  Status InitializeTransactionTablesConfig(int64_t term);

  Status IncrementTransactionTablesVersion();

  uint64_t GetTransactionTablesVersion() override;

  Status WaitForTransactionTableVersionUpdateToPropagate();

  Status FillHeartbeatResponse(const TSHeartbeatRequestPB* req, TSHeartbeatResponsePB* resp);

  SysCatalogTable* sys_catalog() override { return sys_catalog_.get(); }

  // Tablet peer for the sys catalog tablet's peer.
  std::shared_ptr<tablet::TabletPeer> tablet_peer() const override;

  ClusterLoadBalancer* load_balancer() override { return load_balance_policy_.get(); }

  TabletSplitManager* tablet_split_manager() override { return &tablet_split_manager_; }

  CloneStateManager* clone_state_manager() override { return clone_state_manager_.get(); }

  XClusterManagerIf* GetXClusterManager() override;
  XClusterManager* GetXClusterManagerImpl() override { return xcluster_manager_.get(); }

  // Dump all of the current state about tables and tablets to the
  // given output stream. This is verbose, meant for debugging.
  void DumpState(std::ostream* out, bool on_disk_dump = false) const override;

  void SetLoadBalancerEnabled(bool is_enabled);

  bool IsLoadBalancerEnabled() override;

  // Return the table info for the table with the specified UUID, if it exists.
  TableInfoPtr GetTableInfo(const TableId& table_id) override;
  TableInfoPtr GetTableInfoUnlocked(const TableId& table_id) REQUIRES_SHARED(mutex_);

  // Get Table info given namespace id and table name.
  // Very inefficient for YSQL tables.
  scoped_refptr<TableInfo> GetTableInfoFromNamespaceNameAndTableName(
      YQLDatabase db_type, const NamespaceName& namespace_name,
      const TableName& table_name, const PgSchemaName pg_schema_name = {}) override;

  Result<std::vector<scoped_refptr<TableInfo>>> GetTableInfosForNamespace(
      const NamespaceId& namespace_id) const;

  // Return TableInfos according to specified mode.
  virtual std::vector<TableInfoPtr> GetTables(
      GetTablesMode mode, PrimaryTablesOnly = PrimaryTablesOnly::kFalse) override;

  // Return all the available NamespaceInfo. The flag 'includeOnlyRunningNamespaces' determines
  // whether to retrieve all Namespaces irrespective of their state or just 'RUNNING' namespaces.
  // To retrieve all live tables in the system, you should set this flag to true.
  void GetAllNamespaces(std::vector<scoped_refptr<NamespaceInfo> >* namespaces,
                        bool include_only_running_namespaces = false) override;

  // Return all the available (user-defined) types.
  void GetAllUDTypes(std::vector<scoped_refptr<UDTypeInfo>>* types) override;

  // Return the recent tasks.
  std::vector<std::shared_ptr<server::MonitoredTask>> GetRecentTasks() override;

  // Return the recent user-initiated jobs.
  std::vector<std::shared_ptr<server::MonitoredTask>> GetRecentJobs() override;

  Result<NamespaceId> GetNamespaceId(YQLDatabase db_type, const NamespaceName& namespace_name);

  NamespaceName GetNamespaceNameUnlocked(const NamespaceId& id) const REQUIRES_SHARED(mutex_);
  NamespaceName GetNamespaceName(const NamespaceId& id) const override;

  NamespaceName GetNamespaceNameUnlocked(const scoped_refptr<TableInfo>& table) const
      REQUIRES_SHARED(mutex_);
  NamespaceName GetNamespaceName(const scoped_refptr<TableInfo>& table) const;

  // Is the table a system table?
  bool IsSystemTable(const TableInfo& table) const override;

  // Is the table a user created table?
  bool IsUserTable(const TableInfo& table) const override EXCLUDES(mutex_);
  bool IsUserTableUnlocked(const TableInfo& table) const REQUIRES_SHARED(mutex_);

  // Is the table a user created index?
  bool IsUserIndex(const TableInfo& table) const override EXCLUDES(mutex_);
  bool IsUserIndexUnlocked(const TableInfo& table) const REQUIRES_SHARED(mutex_);

  // Is the table a materialized view?
  bool IsMatviewTable(const TableInfo& table) const;

  // Is the table created by user?
  // Note that table can be regular table or index in this case.
  bool IsUserCreatedTable(const TableInfo& table) const override EXCLUDES(mutex_);
  bool IsUserCreatedTableUnlocked(const TableInfo& table) const REQUIRES_SHARED(mutex_);

  // Let the catalog manager know that we have received a response for a prepare delete
  // transaction tablet request. This will trigger delete tablet requests on all replicas.
  void NotifyPrepareDeleteTransactionTabletFinished(
      const scoped_refptr<TabletInfo>& tablet, const std::string& msg, HideOnly hide_only,
      const LeaderEpoch& epoch) override;

  // Let the catalog manager know that we have received a response for a delete tablet request,
  // and that we either deleted the tablet successfully, or we received a fatal error.
  //
  // Async tasks should call this when they finish. The last such tablet peer notification will
  // trigger trying to transition the table from DELETING to DELETED state.
  void NotifyTabletDeleteFinished(
      const TabletServerId& tserver_uuid, const TabletId& tablet_id,
      const TableInfoPtr& table, const LeaderEpoch& epoch,
      server::MonitoredTaskState task_state) override;

  // For a DeleteTable, we first mark tables as DELETING then move them to DELETED once all
  // outstanding tasks are complete and the TS side tablets are deleted.
  // For system tables or colocated tables, we just need outstanding tasks to be done.
  //
  // If all conditions are met, returns a locked write lock on this table.
  // Otherwise lock is default constructed, i.e. not locked.
  TableInfo::WriteLock PrepareTableDeletion(const TableInfoPtr& table);
  bool ShouldDeleteTable(const TableInfoPtr& table);

  // Used by ConsensusService to retrieve the TabletPeer for a system
  // table specified by 'tablet_id'.
  //
  // See also: TabletPeerLookupIf, ConsensusServiceImpl.
  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override;
  Result<tablet::TabletPeerPtr> GetServingTablet(const Slice& tablet_id) const override;

  const NodeInstancePB& NodeInstance() const override;

  Status GetRegistration(ServerRegistrationPB* reg) const override;

  bool IsInitialized() const;

  virtual Status StartRemoteBootstrap(const consensus::StartRemoteBootstrapRequestPB& req)
      override;

  // Checks that placement info can be accommodated by available ts_descs.
  Status CheckValidPlacementInfo(const PlacementInfoPB& placement_info,
                                 const TSDescriptorVector& ts_descs,
                                 ValidateReplicationInfoResponsePB* resp);

  // Loops through the table's placement infos and populates the corresponding config from
  // each placement.
  Status HandlePlacementUsingReplicationInfo(
      const ReplicationInfoPB& replication_info,
      const TSDescriptorVector& all_ts_descs,
      consensus::RaftConfigPB* config,
      CMPerTableLoadState* per_table_state,
      CMGlobalLoadState* global_state);

  // Handles the config creation for a given placement.
  Status HandlePlacementUsingPlacementInfo(const PlacementInfoPB& placement_info,
                                           const TSDescriptorVector& ts_descs,
                                           consensus::PeerMemberType member_type,
                                           consensus::RaftConfigPB* config,
                                           CMPerTableLoadState* per_table_state,
                                           CMGlobalLoadState* global_state);

  // Populates ts_descs with all tservers belonging to a certain placement.
  void GetTsDescsFromPlacementInfo(const PlacementInfoPB& placement_info,
                                   const TSDescriptorVector& all_ts_descs,
                                   TSDescriptorVector* ts_descs);

    // Set the current committed config.
  Status GetCurrentConfig(consensus::ConsensusStatePB *cpb) const override;

  // Return OK if this CatalogManager is a leader in a consensus configuration and if
  // the required leader state (metadata for tables and tablets) has
  // been successfully loaded into memory. CatalogManager must be
  // initialized before calling this method.
  Status CheckIsLeaderAndReady() const override;

  Status IsEpochValid(const LeaderEpoch& epoch) const;

  auto GetValidateEpochFunc() const {
    return std::bind(&CatalogManager::IsEpochValid, this, std::placeholders::_1);
  }

  // Returns this CatalogManager's role in a consensus configuration. CatalogManager
  // must be initialized before calling this method.
  PeerRole Role() const;

  Status PeerStateDump(const std::vector<consensus::RaftPeerPB>& masters_raft,
                       const DumpMasterStateRequestPB* req,
                       DumpMasterStateResponsePB* resp);

  // If we get removed from an existing cluster, leader might ask us to detach ourselves from the
  // cluster. So we enter a shell mode equivalent state, with no bg tasks and no tablet peer
  // nor consensus.
  Status GoIntoShellMode();

  // Setters and getters for the cluster config item.
  //
  // To change the cluster config, a client would need to do a client-side read-modify-write by
  // issuing a get for the latest config, obtaining the current valid config (together with its
  // respective version number), modify the values it wants of said config and issuing a write
  // afterwards, without changing the version number. In case the version number does not match
  // on the server, the change will fail and the client will have to retry the get, as someone
  // must havGetTableInfoe updated the config in the meantime.
  Status GetClusterConfig(GetMasterClusterConfigResponsePB* resp) override;
  Status GetClusterConfig(SysClusterConfigEntryPB* config) override;
  Result<int32_t> GetClusterConfigVersion();

  Status SetClusterConfig(
      const ChangeMasterClusterConfigRequestPB* req,
      ChangeMasterClusterConfigResponsePB* resp) override;

  // Validator for placement information with respect to cluster configuration
  Status ValidateReplicationInfo(
      const ValidateReplicationInfoRequestPB* req, ValidateReplicationInfoResponsePB* resp);

  Status SetPreferredZones(
      const SetPreferredZonesRequestPB* req, SetPreferredZonesResponsePB* resp);

  Result<size_t> GetReplicationFactor() override;
  Result<size_t> GetNumTabletReplicas(const scoped_refptr<TabletInfo>& tablet);

  // Lookup tablet by ID, then call GetExpectedNumberOfReplicasForTable below.
  Status GetExpectedNumberOfReplicasForTablet(const TabletId& tablet_id,
                                              int* num_live_replicas,
                                              int* num_read_replicas);

  // Get the number of live and read replicas for a given table.
  void GetExpectedNumberOfReplicasForTable(const scoped_refptr<TableInfo>& table,
                                           int* num_live_replicas,
                                           int* num_read_replicas);

  // Get the percentage of tablets that have been moved off of the black-listed tablet servers.
  Status GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp);

  // Get the percentage of leaders that have been moved off of the leader black-listed tablet
  // servers.
  Status GetLeaderBlacklistCompletionPercent(GetLoadMovePercentResponsePB* resp);

  // Get the percentage of leaders/tablets that have been moved off of the (leader) black-listed
  // tablet servers.
  Status GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp,
      bool blacklist_leader);

  // API to check if all the live tservers have similar tablet workload.
  Status IsLoadBalanced(const IsLoadBalancedRequestPB* req,
                        IsLoadBalancedResponsePB* resp) override;

  MonoTime LastLoadBalancerRunTime() const;

  Status IsLoadBalancerIdle(const IsLoadBalancerIdleRequestPB* req,
                            IsLoadBalancerIdleResponsePB* resp);

  // API to check that all tservers that shouldn't have leader load do not.
  Status AreLeadersOnPreferredOnly(const AreLeadersOnPreferredOnlyRequestPB* req,
                                   AreLeadersOnPreferredOnlyResponsePB* resp) override;

  // Return the placement uuid of the primary cluster containing this master.
  Result<std::string> placement_uuid() const;

  // Clears out the existing metadata ('table_names_map_', 'table_ids_map_',
  // and 'tablet_map_'), loads tables metadata into memory and if successful
  // loads the tablets metadata.
  // TODO(asrivastava): This is only public because it is used by a test
  // (CreateTableStressTest.TestConcurrentCreateTableAndReloadMetadata). Can we refactor that test
  // to avoid this call and make this private?
  Status VisitSysCatalog(SysCatalogLoadingState* state);
  Status RunLoaders(SysCatalogLoadingState* state) REQUIRES(mutex_);

  // Waits for the worker queue to finish processing, returns OK if worker queue is idle before
  // the provided timeout, TimedOut Status otherwise.
  Status WaitForWorkerPoolTests(
      const MonoDelta& timeout = MonoDelta::FromSeconds(10)) const override;

  // Get the disk size of tables (Used for YSQL \d+ command)
  Status GetTableDiskSize(
      const GetTableDiskSizeRequestPB* req, GetTableDiskSizeResponsePB* resp, rpc::RpcContext* rpc);

  Result<scoped_refptr<UDTypeInfo>> FindUDTypeById(
      const UDTypeId& udt_id) const EXCLUDES(mutex_);

  Result<scoped_refptr<UDTypeInfo>> FindUDTypeByIdUnlocked(
      const UDTypeId& udt_id) const REQUIRES_SHARED(mutex_);

  Result<scoped_refptr<NamespaceInfo>> FindNamespaceUnlocked(
      const NamespaceIdentifierPB& ns_identifier) const REQUIRES_SHARED(mutex_);

  Result<scoped_refptr<NamespaceInfo>> FindNamespace(
      const NamespaceIdentifierPB& ns_identifier) const override EXCLUDES(mutex_);

  Result<scoped_refptr<NamespaceInfo>> FindNamespaceById(
      const NamespaceId& id) const override EXCLUDES(mutex_);

  Result<scoped_refptr<NamespaceInfo>> FindNamespaceByIdUnlocked(
      const NamespaceId& id) const REQUIRES_SHARED(mutex_);

  Result<scoped_refptr<TableInfo>> FindTableUnlocked(
      const TableIdentifierPB& table_identifier) const REQUIRES_SHARED(mutex_);

  Result<scoped_refptr<TableInfo>> FindTable(
      const TableIdentifierPB& table_identifier) const override EXCLUDES(mutex_);

  Result<scoped_refptr<TableInfo>> FindTableById(
      const TableId& table_id) const override EXCLUDES(mutex_);

  Result<scoped_refptr<TableInfo>> FindTableByIdUnlocked(
      const TableId& table_id) const REQUIRES_SHARED(mutex_);

  Result<bool> TableExists(
      const std::string& namespace_name, const std::string& table_name) const EXCLUDES(mutex_);

  Result<TableDescription> DescribeTable(
      const TableIdentifierPB& table_identifier, bool succeed_if_create_in_progress);

  Result<TableDescription> DescribeTable(
      const TableInfoPtr& table_info, bool succeed_if_create_in_progress);

  Result<std::string> GetPgSchemaName(const TableInfoPtr& table_info) REQUIRES_SHARED(mutex_);

  Result<std::unordered_map<std::string, uint32_t>> GetPgAttNameTypidMap(
      const TableInfoPtr& table_info) REQUIRES_SHARED(mutex_);

  Result<std::unordered_map<uint32_t, PgTypeInfo>> GetPgTypeInfo(
      const scoped_refptr<NamespaceInfo>& namespace_info, std::vector<uint32_t>* type_oids)
      REQUIRES_SHARED(mutex_);

  void AssertLeaderLockAcquiredForReading() const override {
    leader_lock_.AssertAcquiredForReading();
  }

  std::string GenerateId() override {
    return GenerateId(boost::none);
  }

  std::string GenerateId(boost::optional<const SysRowEntryType> entity_type);
  std::string GenerateIdUnlocked(boost::optional<const SysRowEntryType> entity_type = boost::none)
      REQUIRES_SHARED(mutex_);

  ThreadPool* AsyncTaskPool() override { return async_task_pool_.get(); }

  PermissionsManager* permissions_manager() override {
    return permissions_manager_.get();
  }

  intptr_t tablets_version() const override NO_THREAD_SAFETY_ANALYSIS {
    // This method should not hold the lock, because Version method is thread safe.
    return tablet_map_.Version() + tables_.Version();
  }

  intptr_t tablet_locations_version() const override {
    return tablet_locations_version_.load(std::memory_order_acquire);
  }

  EncryptionManager& encryption_manager() {
    return *encryption_manager_;
  }

  client::UniverseKeyClient& universe_key_client() {
    return *universe_key_client_;
  }

  Status FlushSysCatalog(const FlushSysCatalogRequestPB* req,
                         FlushSysCatalogResponsePB* resp,
                         rpc::RpcContext* rpc);

  Status CompactSysCatalog(const CompactSysCatalogRequestPB* req,
                           CompactSysCatalogResponsePB* resp,
                           rpc::RpcContext* rpc);

  Status SplitTablet(
      const TabletId& tablet_id, ManualSplit is_manual_split, const LeaderEpoch& epoch) override;

  // Splits tablet specified in the request using middle of the partition as a split point.
  Status SplitTablet(
      const SplitTabletRequestPB* req, SplitTabletResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // Deletes a tablet that is no longer serving user requests. This would require that the tablet
  // has been split and both of its children are now in RUNNING state and serving user requests
  // instead.
  Status DeleteNotServingTablet(
      const DeleteNotServingTabletRequestPB* req, DeleteNotServingTabletResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status DdlLog(
      const DdlLogRequestPB* req, DdlLogResponsePB* resp, rpc::RpcContext* rpc);

  // Test wrapper around protected DoSplitTablet method.
  Status TEST_SplitTablet(
      const scoped_refptr<TabletInfo>& source_tablet_info,
      docdb::DocKeyHash split_hash_code) override;

  Status TEST_SplitTablet(
      const TabletId& tablet_id, const std::string& split_encoded_key,
      const std::string& split_partition_key) override;

  Status TEST_IncrementTablePartitionListVersion(const TableId& table_id) override;

  PitrCount pitr_count() const override {
    return sys_catalog_->pitr_count();
  }

  // Returns the current valid LeaderEpoch.
  // This function should generally be avoided. RPC handlers that require the LeaderEpoch
  // should get the current LeaderEpoch from the leader lock by adding a const ref to LeaderEpoch
  // to their signature. Async work should get the leader epoch by parameter passing from
  // the context that initiated the work.
  // This is present for tests and as an escape hatch.
  LeaderEpoch GetLeaderEpochInternal() const override {
    return LeaderEpoch(leader_ready_term(), pitr_count());
  }

  // Schedule a task to run on the async task thread pool.
  Status ScheduleTask(std::shared_ptr<server::RunnableMonitoredTask> task) override;

  // Time since this peer became master leader. Caller should verify that it is leader before.
  MonoDelta TimeSinceElectedLeader();

  Result<std::vector<TableDescription>> CollectTables(
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
      bool add_indexes,
      bool include_parent_colocated_table = false) override;

  Result<std::vector<TableDescription>> CollectTables(
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
      CollectFlags flags,
      std::unordered_set<NamespaceId>* namespaces = nullptr);

  // Returns 'table_replication_info' itself if set. Else looks up placement info for its
  // 'tablespace_id'. If neither is set, returns the cluster level replication info.
  Result<ReplicationInfoPB> GetTableReplicationInfo(
      const ReplicationInfoPB& table_replication_info,
      const TablespaceId& tablespace_id) override;

  Result<ReplicationInfoPB> GetTableReplicationInfo(const TableInfoPtr& table) override;

  Result<size_t> GetTableReplicationFactor(const TableInfoPtr& table) const override;

  Result<boost::optional<TablespaceId>> GetTablespaceForTable(
      const scoped_refptr<TableInfo>& table) const override;

  void SyncXClusterConsumerReplicationStatusMap(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::Map<std::string, cdc::ProducerEntryPB>& producer_map)
      EXCLUDES(xcluster_consumer_replication_error_map_mutex_);

  void StoreXClusterConsumerReplicationStatus(
      const XClusterConsumerReplicationStatusPB& consumer_replication_status) EXCLUDES(mutex_);

  void CheckTableDeleted(const TableInfoPtr& table, const LeaderEpoch& epoch) override;

  Status ShouldSplitValidCandidate(
      const TabletInfo& tablet_info, const TabletReplicaDriveInfo& drive_info) const override;

  Status GetAllAffinitizedZones(std::vector<AffinitizedZonesSet>* affinitized_zones) override;
  Result<std::vector<BlacklistSet>> GetAffinitizedZoneSet();
  Result<BlacklistSet> BlacklistSetFromPB(bool leader_blacklist = false) const override;

  Status GetUniverseKeyRegistryFromOtherMastersAsync();

  std::vector<std::string> GetMasterAddresses();
  Result<std::vector<HostPort>> GetMasterAddressHostPorts();

  // Returns true if there is at-least one snapshot schedule on any database/keyspace
  // in the cluster.
  Status CheckIfPitrActive(
    const CheckIfPitrActiveRequestPB* req, CheckIfPitrActiveResponsePB* resp);

  // Get the parent table id for a colocated table. The table parameter must be colocated and
  // not satisfy IsColocationParentTableId.
  Result<TableId> GetParentTableIdForColocatedTable(const scoped_refptr<TableInfo>& table);
  Result<TableId> GetParentTableIdForColocatedTableUnlocked(
      const scoped_refptr<TableInfo>& table) REQUIRES_SHARED(mutex_);

  Result<std::optional<cdc::ConsumerRegistryPB>> GetConsumerRegistry();

  Status SubmitToSysCatalog(std::unique_ptr<tablet::Operation> operation);

  Status ReportYsqlDdlTxnStatus(
      const ReportYsqlDdlTxnStatusRequestPB* req,
      ReportYsqlDdlTxnStatusResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status IsYsqlDdlVerificationDone(
      const IsYsqlDdlVerificationDoneRequestPB* req,
      IsYsqlDdlVerificationDoneResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status GetStatefulServiceLocation(
      const GetStatefulServiceLocationRequestPB* req,
      GetStatefulServiceLocationResponsePB* resp);

  // API to start a snapshot creation.
  Status CreateSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status DoCreateSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp, CoarseTimePoint deadline,
      const LeaderEpoch& epoch) override;

  // API to list all available snapshots.
  Status ListSnapshots(const ListSnapshotsRequestPB* req, ListSnapshotsResponsePB* resp);

  Status ListSnapshotRestorations(
      const ListSnapshotRestorationsRequestPB* req,
      ListSnapshotRestorationsResponsePB* resp) override;

  Result<SnapshotInfoPB> GetSnapshotInfoForBackup(const TxnSnapshotId& snapshot_id);

  // Generate the SnapshotInfoPB as of read_time from the provided snapshot schedule, and return
  // the set of tablets that were RUNNING as of read_time but were HIDDEN before the actual snapshot
  // was taken).
  // The SnapshotInfoPB generated by export snapshot as of time should be identical to the
  // SnapshotInfoPB generated by the normal export_snapshot (even ordering of tables/tablets).
  Result<std::pair<SnapshotInfoPB, std::unordered_set<TabletId>>> GenerateSnapshotInfoFromSchedule(
      const SnapshotScheduleId& snapshot_schedule_id, HybridTime read_time,
      CoarseTimePoint deadline) override;

  Result<google::protobuf::RepeatedPtrField<BackupRowEntryPB>> GetBackupEntriesAsOfTime(
      const TxnSnapshotId& snapshot_id, const NamespaceId& source_ns_id, HybridTime read_time);

  Status RestoreSnapshot(
      const RestoreSnapshotRequestPB* req, RestoreSnapshotResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // API to delete a snapshot.
  Status DeleteSnapshot(
      const DeleteSnapshotRequestPB* req, DeleteSnapshotResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // API to abort a snapshot restore.
  Status AbortSnapshotRestore(
      const AbortSnapshotRestoreRequestPB* req,
      AbortSnapshotRestoreResponsePB* resp,
      rpc::RpcContext* rpc);

  Status ImportSnapshotMeta(
      const ImportSnapshotMetaRequestPB* req,
      ImportSnapshotMetaResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status CreateSnapshotSchedule(
      const CreateSnapshotScheduleRequestPB* req,
      CreateSnapshotScheduleResponsePB* resp,
      rpc::RpcContext* rpc);

  Status ListSnapshotSchedules(
      const ListSnapshotSchedulesRequestPB* req,
      ListSnapshotSchedulesResponsePB* resp,
      rpc::RpcContext* rpc);

  Status DeleteSnapshotSchedule(
      const DeleteSnapshotScheduleRequestPB* req,
      DeleteSnapshotScheduleResponsePB* resp,
      rpc::RpcContext* rpc);

  Status EditSnapshotSchedule(
      const EditSnapshotScheduleRequestPB* req,
      EditSnapshotScheduleResponsePB* resp,
      rpc::RpcContext* rpc);

  Status RestoreSnapshotSchedule(
      const RestoreSnapshotScheduleRequestPB* req,
      RestoreSnapshotScheduleResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status InitXClusterConsumer(
      const std::vector<XClusterConsumerStreamInfo>& consumer_info, const std::string& master_addrs,
      UniverseReplicationInfo& replication_info,
      std::shared_ptr<XClusterRpcTasks> xcluster_rpc_tasks);

  void HandleCreateTabletSnapshotResponse(TabletInfo* tablet, bool error) override;

  void HandleRestoreTabletSnapshotResponse(TabletInfo* tablet, bool error) override;

  void HandleDeleteTabletSnapshotResponse(
      const SnapshotId& snapshot_id, TabletInfo* tablet, bool error) override;

  // Is encryption at rest enabled for this cluster.
  Status IsEncryptionEnabled(
      const IsEncryptionEnabledRequestPB* req, IsEncryptionEnabledResponsePB* resp);

  // Create a new CDC stream with the specified attributes.
  Status CreateCDCStream(
      const CreateCDCStreamRequestPB* req, CreateCDCStreamResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails(
      const scoped_refptr<TableInfo>& table,
      const yb::TabletId& tablet_id,
      const xrepl::StreamId& cdc_sdk_stream_id,
      const yb::OpIdPB& safe_opid,
      const yb::HybridTime& proposed_snapshot_time,
      bool require_history_cutoff) override;

  Status PopulateCDCStateTableOnNewTableCreation(
    const scoped_refptr<TableInfo>& table,
    const TabletId& tablet_id,
    const OpId& safe_opid) override;

  Status WaitForSnapshotSafeOpIdToBePopulated(
      const xrepl::StreamId& stream_id,
      const std::vector<TableId>& table_ids,
      CoarseTimePoint deadline) override;

  // Get the Table schema from system catalog table.
  Status GetTableSchemaFromSysCatalog(
      const GetTableSchemaFromSysCatalogRequestPB* req,
      GetTableSchemaFromSysCatalogResponsePB* resp, rpc::RpcContext* rpc);

  // Delete the specified CDCStream.
  Status DeleteCDCStream(
      const DeleteCDCStreamRequestPB* req, DeleteCDCStreamResponsePB* resp, rpc::RpcContext* rpc);

  // List CDC streams (optionally, for a given table).
  Status ListCDCStreams(
      const ListCDCStreamsRequestPB* req, ListCDCStreamsResponsePB* resp) override;

  // Whether there is a CDC stream for a given table.
  Status IsObjectPartOfXRepl(
    const IsObjectPartOfXReplRequestPB* req, IsObjectPartOfXReplResponsePB* resp) override;

  // Fetch CDC stream info corresponding to a db stream id
  Status GetCDCDBStreamInfo(
      const GetCDCDBStreamInfoRequestPB* req, GetCDCDBStreamInfoResponsePB* resp) override;

  // Get CDC stream.
  Status GetCDCStream(
      const GetCDCStreamRequestPB* req, GetCDCStreamResponsePB* resp, rpc::RpcContext* rpc);

  // Update a CDC stream.
  Status UpdateCDCStream(
      const UpdateCDCStreamRequestPB* req, UpdateCDCStreamResponsePB* resp, rpc::RpcContext* rpc);

  Status YsqlBackfillReplicationSlotNameToCDCSDKStream(
      const YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB* req,
      YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB* resp,
      rpc::RpcContext* rpc);

  // Query if Bootstrapping is required for a CDC stream (e.g. Are we missing logs).
  Status IsBootstrapRequired(
      const IsBootstrapRequiredRequestPB* req,
      IsBootstrapRequiredResponsePB* resp,
      rpc::RpcContext* rpc);

  // Get metadata required to decode UDTs in CDCSDK.
  Status GetUDTypeMetadata(
      const GetUDTypeMetadataRequestPB* req, GetUDTypeMetadataResponsePB* resp,
      rpc::RpcContext* rpc);

  // Bootstrap namespace and setup replication to consume data from another YB universe.
  Status SetupNamespaceReplicationWithBootstrap(
      const SetupNamespaceReplicationWithBootstrapRequestPB* req,
      SetupNamespaceReplicationWithBootstrapResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // Setup Universe Replication to consume data from another YB universe.
  Status SetupUniverseReplication(
      const SetupUniverseReplicationRequestPB* req,
      SetupUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc);

  // Delete Universe Replication.
  Status DeleteUniverseReplication(
      const DeleteUniverseReplicationRequestPB* req,
      DeleteUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc);

  // Alter Universe Replication.
  Status AlterUniverseReplication(
      const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status UpdateProducerAddress(
      scoped_refptr<UniverseReplicationInfo> universe,
      const AlterUniverseReplicationRequestPB* req);

  Status AddTablesToReplication(
      scoped_refptr<UniverseReplicationInfo> universe,
      const AlterUniverseReplicationRequestPB* req,
      AlterUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc);

  // Rename an existing Universe Replication.
  Status RenameUniverseReplication(
      scoped_refptr<UniverseReplicationInfo> universe,
      const AlterUniverseReplicationRequestPB* req);

  Status ChangeXClusterRole(
      const ChangeXClusterRoleRequestPB* req,
      ChangeXClusterRoleResponsePB* resp,
      rpc::RpcContext* rpc);

  Status BootstrapProducer(const BootstrapProducerRequestPB* req,
                            BootstrapProducerResponsePB* resp,
                            rpc::RpcContext* rpc);

  // Enable/Disable an Existing Universe Replication.
  Status SetUniverseReplicationEnabled(
      const SetUniverseReplicationEnabledRequestPB* req,
      SetUniverseReplicationEnabledResponsePB* resp,
      rpc::RpcContext* rpc);

  // Get Universe Replication.
  Status GetUniverseReplication(
      const GetUniverseReplicationRequestPB* req,
      GetUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc);

  scoped_refptr<UniverseReplicationInfo> GetUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id);

  std::vector<scoped_refptr<UniverseReplicationInfo>> GetAllUniverseReplications() const;

  // Checks if the universe is in an active state or has failed during setup.
  Status IsSetupUniverseReplicationDone(
      const IsSetupUniverseReplicationDoneRequestPB* req,
      IsSetupUniverseReplicationDoneResponsePB* resp,
      rpc::RpcContext* rpc);

  // Checks if the replication bootstrap is done, or return its current state.
  Status IsSetupNamespaceReplicationWithBootstrapDone(
      const IsSetupNamespaceReplicationWithBootstrapDoneRequestPB* req,
      IsSetupNamespaceReplicationWithBootstrapDoneResponsePB* resp, rpc::RpcContext* rpc);

  // On a producer side split, creates new pollers on the consumer for the new tablet children.
  Status UpdateConsumerOnProducerSplit(
      const UpdateConsumerOnProducerSplitRequestPB* req,
      UpdateConsumerOnProducerSplitResponsePB* resp,
      rpc::RpcContext* rpc);

  // On a producer side metadata change, halts replication until Consumer applies the Meta change.
  Status UpdateConsumerOnProducerMetadata(
      const UpdateConsumerOnProducerMetadataRequestPB* req,
      UpdateConsumerOnProducerMetadataResponsePB* resp,
      rpc::RpcContext* rpc);

  Status XClusterReportNewAutoFlagConfigVersion(
      const XClusterReportNewAutoFlagConfigVersionRequestPB* req,
      XClusterReportNewAutoFlagConfigVersionResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // Wait for replication to drain on CDC streams.
  typedef std::pair<xrepl::StreamId, TabletId> StreamTabletIdPair;
  typedef boost::hash<StreamTabletIdPair> StreamTabletIdHash;
  Status WaitForReplicationDrain(
      const WaitForReplicationDrainRequestPB* req,
      WaitForReplicationDrainResponsePB* resp,
      rpc::RpcContext* rpc);

  // Setup Universe Replication for an entire producer namespace.
  Status SetupNSUniverseReplication(
      const SetupNSUniverseReplicationRequestPB* req,
      SetupNSUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc);

  // Returns the replication status.
  Status GetReplicationStatus(
      const GetReplicationStatusRequestPB* req,
      GetReplicationStatusResponsePB* resp,
      rpc::RpcContext* rpc);

  std::vector<SysUniverseReplicationEntryPB> GetAllXClusterUniverseReplicationInfos();

  typedef std::unordered_map<TableId, std::list<CDCStreamInfoPtr>> TableStreamIdsMap;

  // Find all CDCSDK streams which do not have metadata for the newly added tables.
  Status FindCDCSDKStreamsForAddedTables(TableStreamIdsMap* table_to_unprocessed_streams_map);

  // This method compares all tables in the namespace to all the tables added to a CDCSDK stream,
  // to find tables which are not yet processed by the CDCSDK streams.
  void FindAllTablesMissingInCDCSDKStream(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<std::string>& table_ids, const NamespaceId& ns_id)
      REQUIRES(mutex_);

  Status ValidateCDCSDKRequestProperties(
      const CreateCDCStreamRequestPB& req, const std::string& source_type_option_value,
      const std::string& record_type_option_value, const std::string& id_type_option_value);

  Status RollbackFailedCreateCDCSDKStream(
      const xrepl::StreamId& stream_id, CDCSDKStreamCreationState& cdcsdk_stream_creation_state);

  // Process the newly created tables that are relevant to existing CDCSDK streams.
  Status ProcessNewTablesForCDCSDKStreams(
      const TableStreamIdsMap& table_to_unprocessed_streams_map, const LeaderEpoch& epoch);

  // Find all the CDC streams that have been marked as provided state.
  Result<std::vector<CDCStreamInfoPtr>> FindXReplStreamsMarkedForDeletion(
      SysCDCStreamEntryPB::State deletion_state);

  Status CleanUpDeletedXReplStreams(const LeaderEpoch& epoch);

  void GetValidTabletsAndDroppedTablesForStream(
      const CDCStreamInfoPtr stream, std::set<TabletId>* tablets_with_streams,
      std::set<TableId>* dropped_tables);

  // Delete specified CDC streams metadata.
  Status CleanUpCDCSDKStreamsMetadata(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  using StreamTablesMap = std::unordered_map<xrepl::StreamId, std::set<TableId>>;

  Result<CDCStreamInfoPtr> GetXReplStreamInfo(const xrepl::StreamId& stream_id) EXCLUDES(mutex_);

  Status CleanupCDCSDKDroppedTablesFromStreamInfo(
      const LeaderEpoch& epoch,
      const StreamTablesMap& drop_stream_tablelist) EXCLUDES(mutex_);

  Status CleanupXReplStreamFromMaps(CDCStreamInfoPtr stream) REQUIRES(mutex_);

  Status UpdateCDCStreams(
      const std::vector<xrepl::StreamId>& stream_ids,
      const std::vector<yb::master::SysCDCStreamEntryPB>& update_entries);

  MasterSnapshotCoordinator& snapshot_coordinator() override { return snapshot_coordinator_; }

  Result<size_t> GetNumLiveTServersForActiveCluster() override;

  Status ClearFailedUniverse();

  void SetCDCServiceEnabled();

  void PrepareRestore() override;

  void ReenableTabletSplitting(const std::string& feature) override;

  void RunXReplBgTasks(const LeaderEpoch& epoch);

  Status SetUniverseUuidIfNeeded(const LeaderEpoch& epoch);

  void StartXReplParentTabletDeletionTaskIfStopped();

  void ScheduleXReplParentTabletDeletionTask();

  void ScheduleXClusterNSReplicationAddTableTask();

  Result<scoped_refptr<TableInfo>> GetTableById(const TableId& table_id) const override;

  void AddPendingBackFill(const TableId& id) override {
    std::lock_guard lock(backfill_mutex_);
    pending_backfill_tables_.emplace(id);
  }

  void WriteTableToSysCatalog(const TableId& table_id);

  void WriteTabletToSysCatalog(const TabletId& tablet_id);

  Status UpdateLastFullCompactionRequestTime(
      const TableId& table_id, const LeaderEpoch& epoch) override;

  Status GetCompactionStatus(
      const GetCompactionStatusRequestPB* req, GetCompactionStatusResponsePB* resp) override;

  docdb::HistoryCutoff AllowedHistoryCutoffProvider(
      tablet::RaftGroupMetadata* metadata);

  Result<boost::optional<ReplicationInfoPB>> GetTablespaceReplicationInfoWithRetry(
      const TablespaceId& tablespace_id);

  // Promote the table from a PREPARING state to a RUNNING state, and persist in sys_catalog.
  Status PromoteTableToRunningState(TableInfoPtr table_info, const LeaderEpoch& epoch) override;

  std::unordered_set<xrepl::StreamId> GetAllXReplStreamIds() const EXCLUDES(mutex_);

  void NotifyAutoFlagsConfigChanged();

  // Maps replication group id to the corresponding xrepl stream for a table.
  using XClusterConsumerTableStreamIds =
      std::unordered_map<xcluster::ReplicationGroupId, xrepl::StreamId>;
  // Gets the set of CDC stream info for an xCluster consumer table.
  XClusterConsumerTableStreamIds GetXClusterConsumerStreamIdsForTable(const TableId& table_id) const
      EXCLUDES(mutex_);

  void ClearXClusterConsumerTableStreams(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::set<TableId>& tables_to_clear) EXCLUDES(mutex_);

  std::shared_ptr<ClusterConfigInfo> ClusterConfig() const;

  auto GetTasksTracker() { return tasks_tracker_; }

  void MarkUniverseForCleanup(const xcluster::ReplicationGroupId& replication_group_id)
      EXCLUDES(mutex_);

  Status CreateCdcStateTableIfNotFound(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  // Create a new XRepl stream, start mutation and add it to cdc_stream_map_.
  // Caller is responsible for writing the stream to sys_catalog followed by CommitMutation.
  // or calling ReleaseAbandonedXReplStream to release the unused stream.
  Result<scoped_refptr<CDCStreamInfo>> InitNewXReplStream() EXCLUDES(mutex_);
  void ReleaseAbandonedXReplStream(const xrepl::StreamId& stream_id) EXCLUDES(mutex_);

  Status SetXReplWalRetentionForTable(const TableInfoPtr& table, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  Status BackfillMetadataForXRepl(const TableInfoPtr& table_info, const LeaderEpoch& epoch);

  Result<scoped_refptr<TabletInfo>> GetTabletInfo(const TabletId& tablet_id) override
      EXCLUDES(mutex_);

  // Mark specified CDC streams as DELETING/DELETING_METADATA so they can be removed later.
  Status DropXReplStreams(
      const std::vector<CDCStreamInfoPtr>& streams, SysCDCStreamEntryPB::State delete_state);

  std::unordered_map<TableId, XClusterConsumerTableStreamIds> GetXClusterConsumerTableStreams()
      const EXCLUDES(mutex_);

  std::optional<UniverseUuid> GetUniverseUuidIfExists() const;

 protected:
  // TODO Get rid of these friend classes and introduce formal interface.
  friend class TableLoader;
  friend class TabletLoader;
  friend class NamespaceLoader;
  friend class UDTypeLoader;
  friend class ClusterConfigLoader;
  friend class RoleLoader;
  friend class RedisConfigLoader;
  friend class SysConfigLoader;
  friend class ::yb::master::ScopedLeaderSharedLock;
  friend class PermissionsManager;
  friend class MultiStageAlterTable;
  friend class BackfillTable;
  friend class BackfillTablet;
  friend class YsqlBackendsManager;
  friend class BackendsCatalogVersionJob;
  friend class AddTableToXClusterTargetTask;
  friend class VerifyDdlTransactionTask;

  FRIEND_TEST(yb::MasterPartitionedTest, VerifyOldLeaderStepsDown);

  // Called by SysCatalog::SysCatalogStateChanged when this node
  // becomes the leader of a consensus configuration.
  //
  // Executes LoadSysCatalogDataTask below and marks the current time as time since leader.
  Status ElectedAsLeaderCb();

  // Loops and sleeps until one of the following conditions occurs:
  // 1. The current node is the leader master in the current term
  //    and at least one op from the current term is committed. Returns OK.
  // 2. The current node is not the leader master.
  //    Returns IllegalState.
  // 3. The provided timeout expires. Returns TimedOut.
  //
  // This method is intended to ensure that all operations replicated by
  // previous masters are committed and visible to the local node before
  // reading that data, to ensure consistency across failovers.
  Status WaitUntilCaughtUpAsLeader(const MonoDelta& timeout) override;

  // This method is submitted to 'leader_initialization_pool_' by
  // ElectedAsLeaderCb above. It:
  // 1) Acquired 'lock_'
  // 2) Runs the various Visitors defined below
  // 3) Releases 'lock_' and if successful, updates 'leader_ready_term_'
  // to true (under state_lock_).
  void LoadSysCatalogDataTask();

  // This method checks that resource such as keyspace is available for GrantRevokePermission
  // request.
  // Since this method takes lock on mutex_, it is separated out of permissions manager
  // so that the thread safety relationship between the two managers is easy to reason about.
  Status CheckResource(const GrantRevokePermissionRequestPB* req,
                       GrantRevokePermissionResponsePB* resp);

  // Generated the default entry for the cluster config, that is written into sys_catalog on very
  // first leader election of the cluster.
  //
  // Sets the version field of the SysClusterConfigEntryPB to 0.
  Status PrepareDefaultClusterConfig(int64_t term) REQUIRES(mutex_);

  // Sets up various system configs.
  Status PrepareDefaultSysConfig(int64_t term) REQUIRES(mutex_);

  // Starts an asynchronous run of initdb. Errors are handled in the callback. Returns true
  // if started running initdb, false if decided that it is not needed.
  Result<bool> StartRunningInitDbIfNeeded(int64_t term) REQUIRES_SHARED(mutex_);

  Status PrepareDefaultNamespaces(int64_t term) REQUIRES(mutex_);

  Status PrepareSystemTables(const LeaderEpoch& epoch) REQUIRES(mutex_);

  Status PrepareSysCatalogTable(const LeaderEpoch& epoch) REQUIRES(mutex_);

  template <class T>
  Status PrepareSystemTableTemplate(const TableName& table_name,
                                    const NamespaceName& namespace_name,
                                    const NamespaceId& namespace_id,
                                    const LeaderEpoch& epoch) REQUIRES(mutex_);

  Status PrepareSystemTable(const TableName& table_name,
                            const NamespaceName& namespace_name,
                            const NamespaceId& namespace_id,
                            const Schema& schema,
                            const LeaderEpoch& epoch,
                            YQLVirtualTable* vtable) REQUIRES(mutex_);

  Status PrepareNamespace(YQLDatabase db_type,
                          const NamespaceName& name,
                          const NamespaceId& id,
                          int64_t term) REQUIRES(mutex_);

  void ProcessPendingNamespace(NamespaceId id,
                               std::vector<scoped_refptr<TableInfo>> template_tables,
                               TransactionMetadata txn, const LeaderEpoch& epoch);

  // Called when transaction associated with NS create finishes. Verifies postgres layer present.
  void ScheduleVerifyNamespacePgLayer(TransactionMetadata txn,
      scoped_refptr<NamespaceInfo> ns, const LeaderEpoch& epoch);

  Status VerifyNamespacePgLayer(scoped_refptr<NamespaceInfo> ns, Result<bool> exists,
      const LeaderEpoch& epoch);

  Status ConsensusStateToTabletLocations(const consensus::ConsensusStatePB& cstate,
                                         TabletLocationsPB* locs_pb);

  // Creates the table and associated tablet objects in-memory and updates the appropriate
  // catalog manager maps.
  Status CreateTableInMemory(const CreateTableRequestPB& req,
                             const Schema& schema,
                             const dockv::PartitionSchema& partition_schema,
                             const NamespaceId& namespace_id,
                             const NamespaceName& namespace_name,
                             const std::vector<dockv::Partition>& partitions,
                             bool colocated,
                             IsSystemObject system_table,
                             IndexInfoPB* index_info,
                             TabletInfos* tablets,
                             CreateTableResponsePB* resp,
                             scoped_refptr<TableInfo>* table) REQUIRES(mutex_);

  Result<TabletInfos> CreateTabletsFromTable(
      const std::vector<dockv::Partition>& partitions,
      const TableInfoPtr& table,
      SysTabletsEntryPB::State state = SysTabletsEntryPB::PREPARING) REQUIRES(mutex_);

  // Check that local host is present in master addresses for normal master process start.
  // On error, it could imply that master_addresses is incorrectly set for shell master startup
  // or that this master host info was missed in the master addresses and it should be
  // participating in the very first quorum setup.
  Status CheckLocalHostInMasterAddresses();

  // Helper for initializing 'sys_catalog_'. After calling this
  // method, the caller should call WaitUntilRunning() on sys_catalog_
  // WITHOUT holding 'lock_' to wait for consensus to start for
  // sys_catalog_.
  //
  // This method is thread-safe.
  Status InitSysCatalogAsync();

  // Helper for creating the initial TableInfo state
  // Leaves the table "write locked" with the new info in the
  // "dirty" state field.
  scoped_refptr<TableInfo> CreateTableInfo(const CreateTableRequestPB& req,
                                           const Schema& schema,
                                           const dockv::PartitionSchema& partition_schema,
                                           const NamespaceId& namespace_id,
                                           const NamespaceName& namespace_name,
                                           bool colocated,
                                           IndexInfoPB* index_info) REQUIRES(mutex_);

  // Helper for creating the initial TabletInfo state.
  // Leaves the tablet "write locked" with the new info in the
  // "dirty" state field.
  TabletInfoPtr CreateTabletInfo(TableInfo* table,
                                 const PartitionPB& partition,
                                 SysTabletsEntryPB::State state = SysTabletsEntryPB::PREPARING)
                                 REQUIRES_SHARED(mutex_);

  // Remove the specified entries from the protobuf field table_ids of a TabletInfo.
  Status RemoveTableIdsFromTabletInfo(
      TabletInfoPtr tablet_info, const std::unordered_set<TableId>& tables_to_remove,
      const LeaderEpoch& epoch);

  // Add index info to the indexed table.
  Status AddIndexInfoToTable(const scoped_refptr<TableInfo>& indexed_table,
                             TableInfo::WriteLock* l_ptr,
                             const IndexInfoPB& index_info,
                             const LeaderEpoch& epoch,
                             CreateTableResponsePB* resp);

  struct DeletingTableData {
    TableInfoPtr table_info;
    TableInfo::WriteLock write_lock = TableInfo::WriteLock();

    bool remove_from_name_map = false;
    TabletDeleteRetainerInfo delete_retainer = TabletDeleteRetainerInfo::AlwaysDelete();

    std::string ToString() const;
  };

  // Delete index info from the indexed table.
  Status MarkIndexInfoFromTableForDeletion(
      const TableId& indexed_table_id, const TableId& index_table_id, bool multi_stage,
      const LeaderEpoch& epoch,
      DeleteTableResponsePB* resp,
      std::map<TableId, DeletingTableData>* data_map_ptr);

  // Delete index info from the indexed table.
  Status DeleteIndexInfoFromTable(
      const TableId& indexed_table_id, const TableId& index_table_id, const LeaderEpoch& epoch,
      std::map<TableId, DeletingTableData>* data_map_ptr);

  // Builds the TabletLocationsPB for a tablet based on the provided TabletInfo.
  // Populates locs_pb and returns true on success.
  // Returns Status::ServiceUnavailable if tablet is not running.
  // Set include_inactive to true in order to also get information about hidden tablets.
  Status BuildLocationsForTablet(
      const scoped_refptr<TabletInfo>& tablet,
      TabletLocationsPB* locs_pb,
      IncludeInactive include_inactive = IncludeInactive::kFalse,
      PartitionsOnly partitions_only = PartitionsOnly::kFalse);

  // Extract the set of tablets that can be deleted and the set of tablets
  // that must be processed because not running yet.
  // Returns a map of table_id -> {tablet_info1, tablet_info2, etc.}.
  void ExtractTabletsToProcess(TabletInfos *tablets_to_delete,
                               TableToTabletInfos *tablets_to_process);

  // Determine whether any tables are in the DELETING state.
  bool AreTablesDeletingOrHiding() override;

  // Task that takes care of the tablet assignments/creations.
  // Loops through the "not created" tablets and sends a CreateTablet() request.
  Status ProcessPendingAssignmentsPerTable(
      const TableId& table_id, const TabletInfos& tablets, const LeaderEpoch& epoch,
      CMGlobalLoadState* global_load_state);

  // Select a tablet server from 'ts_descs' on which to place a new replica.
  // Any tablet servers in 'excluded' are not considered.
  // REQUIRES: 'ts_descs' must include at least one non-excluded server.
  std::shared_ptr<TSDescriptor> SelectReplica(
      const TSDescriptorVector& ts_descs,
      std::set<TabletServerId>* excluded,
      CMPerTableLoadState* per_table_state, CMGlobalLoadState* global_state);

  // Select and assign a tablet server as the protege 'config'. This protege is selected from the
  // set of tservers in 'global_state' that have the lowest current protege load.
  Status SelectProtegeForTablet(
      TabletInfo* tablet, consensus::RaftConfigPB *config, CMGlobalLoadState* global_state);

  // Select N Replicas from online tablet servers (as specified by
  // 'ts_descs') for the specified tablet and populate the consensus configuration
  // object. If 'ts_descs' does not specify enough online tablet
  // servers to select the N replicas, return Status::InvalidArgument.
  //
  // This method is called by "ProcessPendingAssignmentsPerTable()".
  Status SelectReplicasForTablet(
      const TSDescriptorVector& ts_descs, TabletInfo* tablet,
      CMPerTableLoadState* per_table_state, CMGlobalLoadState* global_state);

  // Select N Replicas from the online tablet servers that have been chosen to respect the
  // placement information provided. Populate the consensus configuration object with choices and
  // also update the set of selected tablet servers, to not place several replicas on the same TS.
  // member_type indicated what type of replica to select for.
  //
  // This method is called by "SelectReplicasForTablet".
  void SelectReplicas(
      const TSDescriptorVector& ts_descs,
      size_t nreplicas, consensus::RaftConfigPB* config,
      std::set<TabletServerId>* already_selected_ts,
      consensus::PeerMemberType member_type,
      CMPerTableLoadState* per_table_state,
      CMGlobalLoadState* global_state);

  void HandleAssignPreparingTablet(TabletInfo* tablet,
                                   DeferredAssignmentActions* deferred);

  // Assign tablets and send CreateTablet RPCs to tablet servers.
  // The out param 'new_tablets' should have any newly-created TabletInfo
  // objects appended to it.
  void HandleAssignCreatingTablet(TabletInfo* tablet,
                                  DeferredAssignmentActions* deferred,
                                  TabletInfos* new_tablets);

  // Send the create tablet requests to the selected peers of the consensus configurations.
  // The creation is async, and at the moment there is no error checking on the
  // caller side. We rely on the assignment timeout. If we don't see the tablet
  // after the timeout, we regenerate a new one and proceed with a new
  // assignment/creation.
  //
  // This method is part of the "ProcessPendingAssignmentsPerTable()"
  //
  // This must be called after persisting the tablet state as
  // CREATING to ensure coherent state after Master failover.
  Status SendCreateTabletRequests(
      const std::vector<TabletInfo*>& tablets, const LeaderEpoch& epoch);

  // Send the "alter table request" to all tablets of the specified table.
  //
  // Also, initiates the required AlterTable requests to backfill the Index.
  // Initially the index is set to be in a INDEX_PERM_DELETE_ONLY state, then
  // updated to INDEX_PERM_WRITE_AND_DELETE state; followed by backfilling. Once
  // all the tablets have completed backfilling, the index will be updated
  // to be in INDEX_PERM_READ_WRITE_AND_DELETE state.
  Status SendAlterTableRequest(const scoped_refptr<TableInfo>& table,
                               const LeaderEpoch& epoch,
                               const AlterTableRequestPB* req = nullptr);

  Status SendAlterTableRequestInternal(
      const scoped_refptr<TableInfo>& table, const TransactionId& txn_id, const LeaderEpoch& epoch,
      const AlterTableRequestPB* req = nullptr);

  // Starts the background task to send the SplitTablet RPC to the leader for the specified tablet.
  Status SendSplitTabletRequest(
      const scoped_refptr<TabletInfo>& tablet, std::array<TabletId, kNumSplitParts> new_tablet_ids,
      const std::string& split_encoded_key, const std::string& split_partition_key,
      const LeaderEpoch& epoch);

  // Send the "truncate table request" to all tablets of the specified table.
  void SendTruncateTableRequest(const scoped_refptr<TableInfo>& table, const LeaderEpoch& epoch);

  // Start the background task to send the TruncateTable() RPC to the leader for this tablet.
  void SendTruncateTabletRequest(const scoped_refptr<TabletInfo>& tablet, const LeaderEpoch& epoch);

  // Truncate the specified table/index.
  Status TruncateTable(const TableId& table_id,
                       TruncateTableResponsePB* resp,
                       rpc::RpcContext* rpc,
                       const LeaderEpoch& epoch);

  // Helper function to acquire write locks in the order of table_id for table deletion.
  Status DeleteTableInMemoryAcquireLocks(
      const scoped_refptr<master::TableInfo>& table,
      bool is_index_table,
      bool update_indexed_table,
      std::map<TableId, DeletingTableData>* data_map);

  // Delete the specified table in memory. The TableInfo, DeletedTableInfo and lock of the deleted
  // table are appended to the lists. The caller will be responsible for committing the change and
  // deleting the actual table and tablets.
  Status DeleteTableInMemory(
      const TableIdentifierPB& table_identifier,
      bool is_index_table,
      bool update_indexed_table,
      const SnapshotSchedulesToObjectIdsMap& schedules_to_tables_map,
      const LeaderEpoch& epoch,
      std::vector<DeletingTableData>* tables,
      DeleteTableResponsePB* resp,
      rpc::RpcContext* rpc,
      std::map<TableId, DeletingTableData>* data_map_ptr);

  // Request tablet servers to delete all replicas of the tablet.
  void DeleteTabletReplicas(
      TabletInfo* tablet, const std::string& msg, HideOnly hide_only, KeepData keep_data,
      const LeaderEpoch& epoch) override;

  // Returns error if and only if it is forbidden to both:
  // 1) Delete single tablet from table.
  // 2) Delete the whole table.
  // This is used for pre-checks in both `DeleteTablet` and `DeleteOrHideTabletsAndSendRequests`.
  Status CheckIfForbiddenToDeleteTabletOf(const scoped_refptr<TableInfo>& table);

  // Marks each of the tablets in the given table as deleted and triggers requests to the tablet
  // servers to delete them. The table parameter is expected to be given "write locked".
  Status DeleteOrHideTabletsOfTable(
      const TableInfoPtr& table_info, const TabletDeleteRetainerInfo& delete_retainer,
      const LeaderEpoch& epoch);

  // Marks each of the given tablets as deleted and triggers requests to the tablet
  // servers to delete them.
  // Tablets should be sorted by tablet_id to avoid deadlocks.
  Status DeleteOrHideTabletsAndSendRequests(
      const TabletInfos& tablets, const TabletDeleteRetainerInfo& delete_retainer,
      const std::string& reason, const LeaderEpoch& epoch);

  // Sends a prepare delete transaction tablet request to the leader of the status tablet.
  // This will be followed by delete tablet requests to each replica.
  Status SendPrepareDeleteTransactionTabletRequest(
      const scoped_refptr<TabletInfo>& tablet, const std::string& leader_uuid,
      const std::string& reason, HideOnly hide_only, const LeaderEpoch& epoch);

  // Start a task to request the specified tablet leader to step down and optionally to remove
  // the server that is over-replicated. A new tablet server can be specified to start an election
  // immediately to become the new leader. If new_leader_ts_uuid is empty, the election will be run
  // following the protocol's default mechanism.
  void SendLeaderStepDownRequest(
      const scoped_refptr<TabletInfo>& tablet, const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid, bool should_remove, const LeaderEpoch& epoch,
      const std::string& new_leader_ts_uuid = "");

  // Start a task to change the config to remove a certain voter because the specified tablet is
  // over-replicated.
  void SendRemoveServerRequest(
      const scoped_refptr<TabletInfo>& tablet, const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid, const LeaderEpoch& epoch);

  // Start a task to change the config to add an additional voter because the
  // specified tablet is under-replicated.
  void SendAddServerRequest(
      const scoped_refptr<TabletInfo>& tablet, consensus::PeerMemberType member_type,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      const LeaderEpoch& epoch);

  void GetPendingServerTasksUnlocked(const TableId &table_uuid,
                                     TabletToTabletServerMap *add_replica_tasks_map,
                                     TabletToTabletServerMap *remove_replica_tasks_map,
                                     TabletToTabletServerMap *stepdown_leader_tasks)
      REQUIRES_SHARED(mutex_);

  // Abort creation of 'table': abort all mutation for TabletInfo and
  // TableInfo objects (releasing all COW locks), abort all pending
  // tasks associated with the table, and erase any state related to
  // the table we failed to create from the in-memory maps
  // ('table_names_map_', 'table_ids_map_', 'tablet_map_' below).
  Status AbortTableCreation(TableInfo* table,
                            const TabletInfos& tablets,
                            const Status& s,
                            CreateTableResponsePB* resp);

  Status CreateTransactionStatusTablesForTablespaces(
      const TablespaceIdToReplicationInfoMap& tablespace_info,
      const TableToTablespaceIdMap& table_to_tablespace_map, const LeaderEpoch& epoch);

  void StartTablespaceBgTaskIfStopped();

  std::shared_ptr<YsqlTablespaceManager> GetTablespaceManager() const;

  // Report metrics.
  void ReportMetrics();

  // Reset metrics.
  void ResetMetrics();

  // Conventional "T xxx P yyy: " prefix for logging.
  std::string LogPrefix() const;

  // Removes all tasks from jobs_tracker_ and tasks_tracker_.
  void ResetTasksTrackers();
  // Aborts all tasks belonging to 'tables' and waits for them to finish.
  void AbortAndWaitForAllTasks() EXCLUDES(mutex_);
  void AbortAndWaitForAllTasksUnlocked() REQUIRES_SHARED(mutex_);

  // Can be used to create background_tasks_ field for this master.
  // Used on normal master startup or when master comes out of the shell mode.
  Status EnableBgTasks();

  // Helper function for RebuildYQLSystemPartitions to get the system.partitions tablet.
  Status GetYQLPartitionsVTable(std::shared_ptr<SystemTablet>* tablet) REQUIRES(mutex_);
  // Background task for automatically rebuilding system.partitions every
  // partitions_vtable_cache_refresh_secs seconds.
  void RebuildYQLSystemPartitions();

  // Registers `new_tablet` for the same table as `source_tablet_info` tablet.
  // Does not change any other tablets and their partitions.
  Status RegisterNewTabletForSplit(
      TabletInfo* source_tablet_info, const TabletInfoPtr& new_tablet, const LeaderEpoch& epoch,
      TableInfo::WriteLock* table_write_lock, TabletInfo::WriteLock* tablet_write_lock)
      EXCLUDES(mutex_);

  Result<scoped_refptr<TabletInfo>> GetTabletInfoUnlocked(const TabletId& tablet_id)
      REQUIRES_SHARED(mutex_);

  Status DoSplitTablet(
      const scoped_refptr<TabletInfo>& source_tablet_info, std::string split_encoded_key,
      std::string split_partition_key, ManualSplit is_manual_split, const LeaderEpoch& epoch);

  // Splits tablet using specified split_hash_code as a split point.
  Status DoSplitTablet(
      const scoped_refptr<TabletInfo>& source_tablet_info, docdb::DocKeyHash split_hash_code,
      ManualSplit is_manual_split, const LeaderEpoch& epoch);

  // Calculate the total number of replicas which are being handled by servers in state.
  int64_t GetNumRelevantReplicas(const BlacklistPB& state, bool leaders_only);

  int64_t leader_ready_term() const override EXCLUDES(state_lock_) {
    std::lock_guard l(state_lock_);
    return leader_ready_term_;
  }

  // Delete tables from internal map by id, if it has no more active tasks and tablets.
  // This function should only be called from the bg_tasks thread, in a single threaded fashion!
  void CleanUpDeletedTables(const LeaderEpoch& epoch);

  // Called when a new table id is added to table_ids_map_.
  void HandleNewTableId(const TableId& id);

  // Creates a new TableInfo object.
  scoped_refptr<TableInfo> NewTableInfo(TableId id, bool colocated) override;

  template <class Loader>
  Status Load(const std::string& title, SysCatalogLoadingState* state);

  void Started();

  void SysCatalogLoaded(SysCatalogLoadingState&& state);

  // Ensure the sys catalog tablet respects the leader affinity and blacklist configuration.
  // Chooses an unblacklisted master in the highest priority affinity location to step down to. If
  // this master is not blacklisted and there is no unblacklisted master in a higher priority
  // affinity location than this one, does nothing.
  // If there is no unblacklisted master in an affinity zone, chooses an arbitrary master to step
  // down to.
  Status SysCatalogRespectLeaderAffinity();

  Result<bool> IsTablePartOfSomeSnapshotSchedule(const TableInfo& table_info) override;

  // Is this table part of xCluster or CDCSDK?
  bool IsTablePartOfXRepl(const TableId& table_id) const REQUIRES_SHARED(mutex_);

  bool IsTablePartOfXCluster(const TableId& table_id) const EXCLUDES(mutex_);

  bool IsTablePartOfXClusterUnlocked(const TableId& table_id) const REQUIRES_SHARED(mutex_);

  bool IsTablePartOfCDCSDK(const TableId& table_id) const REQUIRES_SHARED(mutex_);

  Status ValidateNewSchemaWithCdc(const TableInfo& table_info, const Schema& new_schema) const;

  Status ResumeXClusterConsumerAfterNewSchema(
      const TableInfo& table_info, SchemaVersion last_compatible_consumer_schema_version);

  bool IsPitrActive();

  // Checks if the database being deleted contains any replicated tables.
  Status CheckIfDatabaseHasReplication(const scoped_refptr<NamespaceInfo>& database);

  Status DoDeleteNamespace(const DeleteNamespaceRequestPB* req,
                           DeleteNamespaceResponsePB* resp,
                           rpc::RpcContext* rpc,
                           const LeaderEpoch& epoch);

  Result<TableInfoPtr> GetGlobalTransactionStatusTable();

  Result<IsOperationDoneResult> IsCreateTableDone(const TableInfoPtr& table);

  // SetupReplicationWithBootstrap
  Status ValidateReplicationBootstrapRequest(
    const SetupNamespaceReplicationWithBootstrapRequestPB* req);

  void DoReplicationBootstrap(
      const xcluster::ReplicationGroupId& replication_id,
      const std::vector<client::YBTableName>& tables,
      Result<TableBootstrapIdsMap> bootstrap_producer_result);

  Result<SnapshotInfoPB> DoReplicationBootstrapCreateSnapshot(
      const std::vector<client::YBTableName>& tables,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  using TableMetaPB = ImportSnapshotMetaResponsePB::TableMetaPB;
  Result<std::vector<TableMetaPB>> DoReplicationBootstrapImportSnapshot(
      const SnapshotInfoPB& snapshot,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  Status DoReplicationBootstrapTransferAndRestoreSnapshot(
    const std::vector<TableMetaPB>& tables_meta,
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  void MarkReplicationBootstrapFailed(
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info, const Status& failure_status);
  // Sets the appropriate failure state and the error status on the replication bootstrap and
  // commits the mutation to the sys catalog.
  void MarkReplicationBootstrapFailed(
      const Status& failure_status,
      CowWriteLock<PersistentUniverseReplicationBootstrapInfo>* bootstrap_info_lock,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  struct CleanupFailedReplicationBootstrapInfo {
    // State that the task failed on.
    SysUniverseReplicationBootstrapEntryPB::State state;

    // Connection to producer universe.
    std::shared_ptr<XClusterRpcTasks> xcluster_rpc_task;

    // Delete CDC streams.
    std::vector<xrepl::StreamId> bootstrap_ids;

    // Delete snapshots.
    TxnSnapshotId old_snapshot_id = TxnSnapshotId::Nil();
    TxnSnapshotId new_snapshot_id = TxnSnapshotId::Nil();

    // Cleanup new snapshot objects.
    NamespaceMap namespace_map;
    UDTypeMap type_map;
    ExternalTableSnapshotDataMap tables_data;
    LeaderEpoch epoch{0};
  };

  Status ClearFailedReplicationBootstrap();

  // Cleanup & delete any objects created during the bootstrap process. This includes things like
  // namespaces, UD types, tables, CDC streams, and snapshots.
  Status DoClearFailedReplicationBootstrap(const CleanupFailedReplicationBootstrapInfo& info);

  // TODO: the maps are a little wasteful of RAM, since the TableInfo/TabletInfo
  // objects have a copy of the string key. But STL doesn't make it
  // easy to make a "gettable set".

  // Lock protecting the various in memory storage structures.
  using MutexType = rw_spinlock;
  using SharedLock = NonRecursiveSharedLock<MutexType>;
  using LockGuard = std::lock_guard<MutexType>;
  mutable MutexType mutex_;

  // Note: Namespaces and tables for YSQL databases are identified by their ids only and therefore
  // are not saved in the name maps below.

  // Data structure containing all tables.
  VersionTracker<TableIndex> tables_ GUARDED_BY(mutex_);

  // Table map: [namespace-id, table-name] -> TableInfo
  // Don't have to use VersionTracker for it, since table_ids_map_ already updated at the same time.
  // Note that this map isn't used for YSQL tables.
  TableInfoByNameMap table_names_map_ GUARDED_BY(mutex_);

  // Set of table ids that are transaction status tables.
  // Don't have to use VersionTracker for it, since table_ids_map_ already updated at the same time.
  TableIdSet transaction_table_ids_set_ GUARDED_BY(mutex_);

  // Don't have to use VersionTracker for it, since table_ids_map_ already updated at the same time.
  // Tablet maps: tablet-id -> TabletInfo
  VersionTracker<TabletInfoMap> tablet_map_ GUARDED_BY(mutex_);

  // Tablets that were hidden instead of deleted. Used to clean up such tablets when they expire.
  std::vector<TabletInfoPtr> hidden_tablets_ GUARDED_BY(mutex_);

  // The set of tablets that have ever been deleted from the cluster. This set is populated on the
  // TabletLoader path in VisitSysCatalog when the loader sees a tablet with DELETED state.
  // This set is used when processing a tablet report. If a reported tablet is not present in
  // tablet_map_, then make sure it is present in deleted_tablets_loaded_from_sys_catalog_ before
  // issuing a DeleteTablet call to tservers. It is possible in the case of corrupted sys catalog or
  // tservers heartbeating into wrong clusters that live data is considered to be orphaned. So make
  // sure that the tablet was explicitly deleted before deleting any on-disk data from tservers.
  std::unordered_set<TabletId> deleted_tablets_loaded_from_sys_catalog_ GUARDED_BY(mutex_);

  // Split parent tablets that are now hidden and still being replicated by some CDC stream. Keep
  // track of these tablets until their children tablets start being polled, at which point they
  // can be deleted and cdc_state metadata can also be cleaned up. retained_by_cdcsdk_ is a
  // subset of hidden_tablets_.
  struct HiddenReplicationParentTabletInfo {
    TableId table_id_;
    std::string parent_tablet_id_;
    std::array<TabletId, kNumSplitParts> split_tablets_;
  };
  std::unordered_map<TabletId, HiddenReplicationParentTabletInfo> retained_by_cdcsdk_
      GUARDED_BY(mutex_);

  // TODO(jhe) Cleanup how we use ScheduledTaskTracker, move is_running and util functions to class.
  // Background task for deleting parent split tablets retained by xCluster streams.
  std::atomic<bool> xrepl_parent_tablet_deletion_task_running_{false};
  rpc::ScheduledTaskTracker cdc_parent_tablet_deletion_task_;

  // Namespace maps: namespace-id -> NamespaceInfo and namespace-name -> NamespaceInfo
  NamespaceInfoMap namespace_ids_map_ GUARDED_BY(mutex_);
  // Used to enforce global uniqueness for names for both YSQL and YCQL databases.
  // Also used to service requests which only include the namespace name and not the id.
  NamespaceNameMapper namespace_names_mapper_ GUARDED_BY(mutex_);

  // User-Defined type maps: udtype-id -> UDTypeInfo and udtype-name -> UDTypeInfo
  UDTypeInfoMap udtype_ids_map_ GUARDED_BY(mutex_);
  UDTypeInfoByNameMap udtype_names_map_ GUARDED_BY(mutex_);

  // RedisConfig map: RedisConfigKey -> RedisConfigInfo
  typedef std::unordered_map<RedisConfigKey, scoped_refptr<RedisConfigInfo>> RedisConfigInfoMap;
  RedisConfigInfoMap redis_config_map_ GUARDED_BY(mutex_);

  // Config information.
  // IMPORTANT: The shared pointer that points to the cluster config
  // is only written to with a new object during a catalog load.
  // At all other times, the address pointed to remains the same
  // (thus the value of this shared ptr remains the same), only
  // the underlying object is read or modified via cow read/write lock mechanism.
  // We don't need a lock guard for changing this pointer value since
  // we already acquire the leader write lock during catalog loading,
  // so all concurrent accesses of this shared ptr -- either external via RPCs or
  // internal by the bg threads (bg_tasks and master_snapshot_coordinator threads)
  // are locked out since they grab the scoped leader shared lock that
  // depends on this leader lock.
  std::shared_ptr<ClusterConfigInfo> cluster_config_ = nullptr; // No GUARD, only write on load.

  // YSQL Catalog information.
  scoped_refptr<SysConfigInfo> ysql_catalog_config_ = nullptr; // No GUARD, only write on Load.

  // Transaction tables information.
  scoped_refptr<SysConfigInfo> transaction_tables_config_ =
      nullptr; // No GUARD, only write on Load.

  Master* const master_;
  Atomic32 closing_;

  std::unique_ptr<SysCatalogTable> sys_catalog_;

  // Mutex to avoid concurrent remote bootstrap sessions.
  std::mutex remote_bootstrap_mtx_;

  // Set to true if this master has received at least the superblock from a remote master.
  bool tablet_exists_;

  // Background thread, used to execute the catalog manager tasks
  // like the assignment and cleaner.
  friend class CatalogManagerBgTasks;
  std::unique_ptr<CatalogManagerBgTasks> background_tasks_;

  // Background threadpool, newer features use this (instead of the Background thread)
  // to execute time-lenient catalog manager tasks.
  std::unique_ptr<yb::ThreadPool> background_tasks_thread_pool_;

  // TODO: convert this to YB_DEFINE_ENUM for automatic pretty-printing.
  enum State {
    kConstructed,
    kStarting,
    kRunning,
    kClosing
  };

  // Lock protecting state_, leader_ready_term_
  mutable simple_spinlock state_lock_;
  State state_ GUARDED_BY(state_lock_);

  // Used to defer Master<->TabletServer work from reactor threads onto a thread where
  // blocking behavior is permissible.
  //
  // NOTE: Presently, this thread pool must contain only a single
  // thread (to correctly serialize invocations of ElectedAsLeaderCb
  // upon closely timed consecutive elections).
  std::unique_ptr<ThreadPool> leader_initialization_pool_;

  // Thread pool to do the async RPC task work.
  std::unique_ptr<ThreadPool> async_task_pool_;

  // This field is updated when a node becomes leader master,
  // waits for all outstanding uncommitted metadata (table and tablet metadata)
  // in the sys catalog to commit, and then reads that metadata into in-memory
  // data structures. This is used to "fence" client and tablet server requests
  // that depend on the in-memory state until this master can respond
  // correctly.
  int64_t leader_ready_term_ GUARDED_BY(state_lock_);

  // This field is set to true when the leader master has completed loading
  // metadata into in-memory structures. This can happen in two cases presently:
  // 1. When a new leader is elected
  // 2. When an existing leader executes a restore_snapshot_schedule
  // In case (1), the above leader_ready_term_ is sufficient to indicate
  // the completion of this stage since the new term is only set after load.
  // However, in case (2), since the before/after term is the same, the above
  // check will succeed even when load is not complete i.e. there's a small
  // window when there's a possibility that the master_service sends RPCs
  // to the leader. This window is after the sys catalog has been restored and
  // all records have been updated on disk and before it starts loading them
  // into the in-memory structures.
  bool is_catalog_loaded_ GUARDED_BY(state_lock_) = false;

  // Lock used to fence operations and leader elections. All logical operations
  // (i.e. create table, alter table, etc.) should acquire this lock for
  // reading. Following an election where this master is elected leader, it
  // should acquire this lock for writing before reloading the metadata.
  //
  // Readers should not acquire this lock directly; use ScopedLeadershipLock
  // instead.
  //
  // Always acquire this lock before state_lock_.
  RWMutex leader_lock_;

  // Async operations are accessing some private methods
  // (TODO: this stuff should be deferred and done in the background thread)
  friend class AsyncAlterTable;

  // Number of live tservers metric.
  scoped_refptr<AtomicGauge<uint32_t>> metric_num_tablet_servers_live_;

  // Number of dead tservers metric.
  scoped_refptr<AtomicGauge<uint32_t>> metric_num_tablet_servers_dead_;

  scoped_refptr<Counter> metric_create_table_too_many_tablets_;
  scoped_refptr<Counter> metric_split_tablet_too_many_tablets_;

  friend class ClusterLoadBalancer;

  // Policy for load balancing tablets on tablet servers.
  std::unique_ptr<ClusterLoadBalancer> load_balance_policy_;

  // Use the Raft config that has been bootstrapped to update the in-memory state of master options
  // and also the on-disk state of the consensus meta object.
  Status UpdateMastersListInMemoryAndDisk();

  // Tablets of system tables on the master indexed by the tablet id.
  std::unordered_map<std::string, std::shared_ptr<tablet::AbstractTablet>> system_tablets_;

  // Tablet of colocated databases indexed by the namespace id.
  std::unordered_map<NamespaceId, scoped_refptr<TabletInfo>> colocated_db_tablets_map_
      GUARDED_BY(mutex_);

  std::unique_ptr<YsqlTablegroupManager> tablegroup_manager_
      GUARDED_BY(mutex_);

  boost::optional<std::future<Status>> initdb_future_;
  boost::optional<InitialSysCatalogSnapshotWriter> initial_snapshot_writer_;

  std::unique_ptr<PermissionsManager> permissions_manager_;

  // This is used for tracking that initdb has started running previously.
  std::atomic<bool> pg_proc_exists_{false};

  // Tracks most recent async tasks.
  scoped_refptr<TasksTracker> tasks_tracker_;

  // Tracks most recent user initiated jobs.
  scoped_refptr<TasksTracker> jobs_tracker_;

  std::unique_ptr<EncryptionManager> encryption_manager_;

  std::unique_ptr<client::UniverseKeyClient> universe_key_client_;

  // A pointer to the system.partitions tablet for the RebuildYQLSystemPartitions bg task.
  std::shared_ptr<SystemTablet> system_partitions_tablet_ = nullptr;

  std::atomic<MonoTime> time_elected_leader_;

  std::unique_ptr<client::YBClient> cdc_state_client_;

  // Mutex to avoid simultaneous creation of transaction tables for a tablespace.
  std::mutex tablespace_transaction_table_creation_mutex_;

  mutable MutexType backfill_mutex_;
  std::unordered_set<TableId> pending_backfill_tables_ GUARDED_BY(backfill_mutex_);

  std::unique_ptr<XClusterManager> xcluster_manager_;

  Status CanAddPartitionsToTable(
      size_t desired_partitions, const PlacementInfoPB& placement_info) override;

  Status CanSupportAdditionalTablet(
      const TableInfoPtr& table, const ReplicationInfoPB& replication_info) const override;

  Status CanSupportAdditionalTabletsForTableCreation(
    int num_tablets, const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& ts_descs);

  void IncrementSplitBlockedByTabletLimitCounter() override;

 private:
  friend class SnapshotLoader;
  friend class yb::master::ClusterLoadBalancer;
  friend class CDCStreamLoader;
  friend class UniverseReplicationLoader;
  friend class UniverseReplicationBootstrapLoader;

  // Performs the provided action with the sys catalog shared tablet instance, or sets up an error
  // if the tablet is not found.
  template <class Req, class Resp, class F>
  Status PerformOnSysCatalogTablet(const Req& req, Resp* resp, const F& f);

  Status CollectTable(
      const TableDescription& table_description,
      CollectFlags flags,
      std::vector<TableDescription>* all_tables,
      std::unordered_set<TableId>* parent_colocated_table_ids);

  Status SplitTablet(
      const scoped_refptr<TabletInfo>& tablet, ManualSplit is_manual_split,
      const LeaderEpoch& epoch);

  void SplitTabletWithKey(
      const scoped_refptr<TabletInfo>& tablet, const std::string& split_encoded_key,
      const std::string& split_partition_key, ManualSplit is_manual_split,
      const LeaderEpoch& epoch);

  Status XReplValidateSplitCandidateTableUnlocked(const TableId& table_id) const
      REQUIRES_SHARED(mutex_);

  Status ValidateSplitCandidate(
      const scoped_refptr<TabletInfo>& tablet, ManualSplit is_manual_split) EXCLUDES(mutex_);
  Status ValidateSplitCandidateUnlocked(
      const scoped_refptr<TabletInfo>& tablet, ManualSplit is_manual_split)
      REQUIRES_SHARED(mutex_);

  // From the list of TServers in 'ts_descs', return the ones that match any placement policy
  // in 'placement_info'. Returns error if there are insufficient TServers to match the
  // required replication factor in placement_info.
  // NOTE: This function will only check whether the total replication factor can be
  // satisfied, and not the individual min_num_replicas in each placement block.
  Result<TSDescriptorVector> FindTServersForPlacementInfo(
      const PlacementInfoPB& placement_info,
      const TSDescriptorVector& ts_descs) const;

  // Using the TServer info in 'ts_descs', return the TServers that match 'pplacement_block'.
  // Returns error if there aren't enough TServers to fulfill the min_num_replicas requirement
  // outlined in 'placement_block'.
  Result<TSDescriptorVector> FindTServersForPlacementBlock(
      const PlacementBlockPB& placement_block,
      const TSDescriptorVector& ts_descs);

  Status ValidateTableReplicationInfo(const ReplicationInfoPB& replication_info) const;

  // Return the id of the tablespace associated with a transaction status table, if any.
  boost::optional<TablespaceId> GetTransactionStatusTableTablespace(
      const scoped_refptr<TableInfo>& table) REQUIRES_SHARED(mutex_);

  // Clears tablespace id for a transaction status table, reverting it back to cluster default
  // if no placement has been set explicitly.
  void ClearTransactionStatusTableTablespace(
      const scoped_refptr<TableInfo>& table) REQUIRES(mutex_);

  // Checks if there are any transaction tables with tablespace id set for a tablespace not in
  // the given tablespace info map.
  bool CheckTransactionStatusTablesWithMissingTablespaces(
      const TablespaceIdToReplicationInfoMap& tablespace_info) EXCLUDES(mutex_);

  // Updates transaction tables' tablespace ids for tablespaces that don't exist.
  Status UpdateTransactionStatusTableTablespaces(
      const TablespaceIdToReplicationInfoMap& tablespace_info, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  // Return the tablespaces in the system and their associated replication info from
  // pg catalog tables.
  Result<std::shared_ptr<TablespaceIdToReplicationInfoMap>> GetYsqlTablespaceInfo();

  // Return the table->tablespace mapping by reading the pg catalog tables.
  Result<std::shared_ptr<TableToTablespaceIdMap>> GetYsqlTableToTablespaceMap(
      const TablespaceIdToReplicationInfoMap& tablespace_info) EXCLUDES(mutex_);

  // Background task that refreshes the in-memory state for YSQL tables with their associated
  // tablespace info.
  // Note: This function should only ever be called by StartTablespaceBgTaskIfStopped().
  void RefreshTablespaceInfoPeriodically();

  // Helper function to schedule the next iteration of the tablespace info task.
  void ScheduleRefreshTablespaceInfoTask(const bool schedule_now = false);

  // Helper function to refresh the tablespace info.
  Status DoRefreshTablespaceInfo(const LeaderEpoch& epoch);

  size_t GetNumLiveTServersForPlacement(const PlacementId& placement_id);

  TSDescriptorVector GetAllLiveNotBlacklistedTServers() const override;

  void InitializeTableLoadState(
      const TableId& table_id, TSDescriptorVector ts_descs, CMPerTableLoadState* state);

  void InitializeGlobalLoadState(
      const TSDescriptorVector& ts_descs, CMGlobalLoadState* state);

  // Send a step down request for the sys catalog tablet to the specified master. If the step down
  // RPC response has an error, returns false. If the step down RPC is successful, returns true.
  // For any other failure, returns a non-OK status.
  Result<bool> SysCatalogLeaderStepDown(const ServerEntryPB& master);

  // Attempts to remove a colocated table from tablegroup.
  // NOOP if the table does not belong to one.
  Status TryRemoveFromTablegroup(const TableId& table_id);

  // Returns an AsyncDeleteReplica task throttler for the given tserver uuid.
  AsyncTaskThrottlerBase* GetDeleteReplicaTaskThrottler(const std::string& ts_uuid)
      EXCLUDES(delete_replica_task_throttler_per_ts_mutex_);

  // Helper function for BuildLocationsForTablet to handle the special case of a system tablet.
  Status BuildLocationsForSystemTablet(
      const scoped_refptr<TabletInfo>& tablet,
      TabletLocationsPB* locs_pb,
      IncludeInactive include_inactive,
      PartitionsOnly partitions_only);

  Status MaybeCreateLocalTransactionTable(
      const CreateTableRequestPB& request, rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Result<int> CalculateNumTabletsForTableCreation(
      const CreateTableRequestPB& request, const Schema& schema,
      const PlacementInfoPB& placement_info);

  Result<std::pair<dockv::PartitionSchema, std::vector<dockv::Partition>>> CreatePartitions(
      const Schema& schema, int num_tablets, bool colocated,
      CreateTableRequestPB* request, CreateTableResponsePB* resp);

  Status RestoreEntry(
      const SysRowEntry& entry, const SnapshotId& snapshot_id, const LeaderEpoch& epoch)
      REQUIRES(mutex_);

  Status DoImportSnapshotMeta(
      const SnapshotInfoPB& snapshot_pb,
      const LeaderEpoch& epoch,
      const std::optional<std::string>& clone_target_namespace_name,
      NamespaceMap* namespace_map,
      UDTypeMap* type_map,
      ExternalTableSnapshotDataMap* tables_data,
      CoarseTimePoint deadline) override;
  Status ImportSnapshotPreprocess(
      const SnapshotInfoPB& snapshot_pb,
      const LeaderEpoch& epoch,
      const std::optional<std::string>& clone_target_namespace_name,
      NamespaceMap* namespace_map,
      UDTypeMap* type_map,
      ExternalTableSnapshotDataMap* tables_data);
  Status ImportSnapshotProcessUDTypes(
      const SnapshotInfoPB& snapshot_pb,
      UDTypeMap* type_map,
      const NamespaceMap& namespace_map);
  Status ImportSnapshotCreateIndexes(
      const SnapshotInfoPB& snapshot_pb,
      const NamespaceMap& namespace_map,
      const UDTypeMap& type_map,
      const LeaderEpoch& epoch,
      bool is_clone,
      ExternalTableSnapshotDataMap* tables_data);
  Status ImportSnapshotCreateAndWaitForTables(
      const SnapshotInfoPB& snapshot_pb,
      const NamespaceMap& namespace_map,
      const UDTypeMap& type_map,
      const LeaderEpoch& epoch,
      bool is_clone,
      ExternalTableSnapshotDataMap* tables_data,
      CoarseTimePoint deadline);
  Status ImportSnapshotProcessTablets(
      const SnapshotInfoPB& snapshot_pb,
      ExternalTableSnapshotDataMap* tables_data);
  void DeleteNewUDtype(
      const UDTypeId& udt_id, const std::unordered_set<UDTypeId>& type_ids_to_delete);
  void DeleteNewSnapshotObjects(
      const NamespaceMap& namespace_map, const UDTypeMap& type_map,
      const ExternalTableSnapshotDataMap& tables_data, const LeaderEpoch& epoch);

  Status RepackSnapshotsForBackup(ListSnapshotsResponsePB* resp);

  // Helper function for ImportTableEntry.
  Result<bool> CheckTableForImport(
      scoped_refptr<TableInfo> table, ExternalTableSnapshotData* snapshot_data)
      REQUIRES_SHARED(mutex_);

  Status ImportNamespaceEntry(
      const SysRowEntry& entry,
      const LeaderEpoch& epoch,
      const std::optional<std::string>& clone_target_namespace_name,
      NamespaceMap* namespace_map);
  Status UpdateUDTypes(QLTypePB* pb_type, const UDTypeMap& type_map);
  Status ImportUDTypeEntry(
      const UDTypeId& udt_id, UDTypeMap* type_map, const NamespaceMap& namespace_map);
  Status RecreateTable(
      const NamespaceId& new_namespace_id,
      const UDTypeMap& type_map,
      const ExternalTableSnapshotDataMap& table_map,
      const LeaderEpoch& epoch,
      bool is_clone,
      ExternalTableSnapshotData* table_data);
  Status RepartitionTable(
      scoped_refptr<TableInfo> table,
      ExternalTableSnapshotData* table_data,
      const LeaderEpoch& epoch,
      bool is_clone);
  Status ImportTableEntry(
      const NamespaceMap& namespace_map,
      const UDTypeMap& type_map,
      const ExternalTableSnapshotDataMap& table_map,
      const LeaderEpoch& epoch,
      bool is_clone,
      ExternalTableSnapshotData* table_data);
  Status PreprocessTabletEntry(const SysRowEntry& entry, ExternalTableSnapshotDataMap* table_map);
  Status ImportTabletEntry(const SysRowEntry& entry, ExternalTableSnapshotDataMap* table_map);

  TabletInfos GetTabletInfos(const std::vector<TabletId>& ids) override;

  Result<std::map<std::string, KeyRange>> GetTableKeyRanges(const TableId& table_id);

  Result<SchemaVersion> GetTableSchemaVersion(const TableId& table_id);

  Result<SysRowEntries> CollectEntries(
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables, CollectFlags flags);

  Result<SysRowEntries> CollectEntriesForSnapshot(
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) override;

  server::Clock* Clock() override;

  const Schema& schema() override;

  const docdb::DocReadContext& doc_read_context();

  Status Submit(std::unique_ptr<tablet::Operation> operation, int64_t leader_term) override;

  AsyncTabletSnapshotOpPtr CreateAsyncTabletSnapshotOp(
      const TabletInfoPtr& tablet, const std::string& snapshot_id,
      tserver::TabletSnapshotOpRequestPB::Operation operation,
      const LeaderEpoch& epoch, TabletSnapshotOperationCallback callback) override;

  void ScheduleTabletSnapshotOp(const AsyncTabletSnapshotOpPtr& operation) override;

  Result<std::unique_ptr<rocksdb::DB>> RestoreSnapshotToTmpRocksDb(
      tablet::Tablet* tablet, const TxnSnapshotId& snapshot_id, HybridTime restore_at);

  Status RestoreSysCatalogCommon(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    RestoreSysCatalogState* state, docdb::DocWriteBatch* write_batch,
    docdb::KeyValuePairPB* restore_kv);

  Status RestoreSysCatalogSlowPitr(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet);

  Status RestoreSysCatalogFastPitr(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet);

  Status RestoreSysCatalog(
      SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
      Status* complete_status) override;

  Status VerifyRestoredObjects(
      const std::unordered_map<std::string, SysRowEntryType>& objects,
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) override;

  void CleanupHiddenObjects(
      const ScheduleMinRestoreTime& schedule_min_restore_time, const LeaderEpoch& epoch) override;
  void CleanupHiddenTablets(
      const ScheduleMinRestoreTime& schedule_min_restore_time, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);
  // Will filter tables content, so pass it by value here.
  void CleanupHiddenTables(
      const ScheduleMinRestoreTime& schedule_min_restore_time, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

  // Checks the colocated table is hidden and is not within the retention of any snapshot schedule
  // or covered by any live snapshot. If the checks pass sends a request to the relevant tserver
  // to remove this table from its metadata for the parent tablet.
  void RemoveHiddenColocatedTableFromTablet(
      const TableInfoPtr& table, const ScheduleMinRestoreTime& schedule_min_restore_time,
      const LeaderEpoch& epoch);

  rpc::Scheduler& Scheduler() override;

  int64_t LeaderTerm() override;

  static void SetTabletSnapshotsState(
      SysSnapshotEntryPB::State state, SysSnapshotEntryPB* snapshot_pb);

  xrepl::StreamId GenerateNewXreplStreamId() EXCLUDES(xrepl_stream_ids_in_use_mutex_);
  void RecoverXreplStreamId(const xrepl::StreamId& stream_id)
      EXCLUDES(xrepl_stream_ids_in_use_mutex_);

  Status CreateNewCDCStreamForNamespace(
      const CreateCDCStreamRequestPB& req,
      CreateCDCStreamResponsePB* resp, rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status CreateNewCdcsdkStream(
      const CreateCDCStreamRequestPB& req, const std::vector<TableId>& table_ids,
      const std::optional<const NamespaceId>& namespace_id, CreateCDCStreamResponsePB* resp,
      const LeaderEpoch& epoch, rpc::RpcContext* rpc);

  Status PopulateCDCStateTable(const xrepl::StreamId& stream_id,
                               const std::vector<TableId>& table_ids,
                               bool has_consistent_snapshot_option,
                               bool consistent_snapshot_option_use,
                               uint64_t consistent_snapshot_time,
                               uint64_t stream_creation_time);

  Status SetAllCDCSDKRetentionBarriers(
      const CreateCDCStreamRequestPB& req, rpc::RpcContext* rpc, const LeaderEpoch& epoch,
      const std::vector<TableId>& table_ids, const xrepl::StreamId& stream_id,
      const bool has_consistent_snapshot_option, bool require_history_cutoff);

  Status ReplicationSlotValidateName(const std::string& replication_slot_name);

  Status TEST_CDCSDKFailCreateStreamRequestIfNeeded(const std::string& sync_point);

  // Create the cdc_state table if needed (i.e. if it does not exist already).
  //
  // This is called at the end of CreateCDCStream.
  Status CreateCdcStateTableIfNeeded(const LeaderEpoch& epoch, rpc::RpcContext* rpc);

  // Check if cdc_state table creation is done.
  Status IsCdcStateTableCreated(IsCreateTableDoneResponsePB* resp);

  // Return all CDC streams.
  void GetAllCDCStreams(std::vector<CDCStreamInfoPtr>* streams);

  // This method returns all tables in the namespace suitable for CDCSDK.
  std::vector<TableInfoPtr> FindAllTablesForCDCSDK(const NamespaceId& ns_id) REQUIRES(mutex_);

  // Find CDC streams for a table.
  std::vector<CDCStreamInfoPtr> GetXReplStreamsForTable(
      const TableId& table_id, const cdc::CDCRequestSource cdc_request_source) const
      REQUIRES_SHARED(mutex_);

  // Find CDC streams for a table to clean its metadata.
  std::vector<CDCStreamInfoPtr> FindCDCSDKStreamsToDeleteMetadata(
      const std::unordered_set<TableId>& table_ids) const REQUIRES_SHARED(mutex_);

  Result<std::optional<CDCStreamInfoPtr>> GetStreamIfValidForDelete(
      const xrepl::StreamId& stream_id, bool force_delete) REQUIRES_SHARED(mutex_);

  Status FillHeartbeatResponseEncryption(
      const SysClusterConfigEntryPB& cluster_config,
      const TSHeartbeatRequestPB* req,
      TSHeartbeatResponsePB* resp);

  Status FillHeartbeatResponseCDC(
      const SysClusterConfigEntryPB& cluster_config,
      const TSHeartbeatRequestPB* req,
      TSHeartbeatResponsePB* resp);

  // Helper functions for GetTableSchemaCallback, GetTablegroupSchemaCallback
  // and GetColocatedTabletSchemaCallback.

  // Helper container to track colocationId and the producer to consumer schema version mapping.
  typedef std::vector<std::tuple<ColocationId, SchemaVersion, SchemaVersion>>
      ColocationSchemaVersions;

  struct SetupReplicationInfo {
    std::unordered_map<TableId, xrepl::StreamId> table_bootstrap_ids;
    bool transactional;
  };

  Status ValidateMasterAddressesBelongToDifferentCluster(
      const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses);

  // Validates a single table's schema with the corresponding table on the consumer side, and
  // updates consumer_table_id with the new table id. Return the consumer table schema if the
  // validation is successful.
  Status ValidateTableSchemaForXCluster(
      const client::YBTableInfo& info, const SetupReplicationInfo& setup_info,
      GetTableSchemaResponsePB* resp);

  // Adds a validated table to the sys catalog table map for the given universe
  Status AddValidatedTableToUniverseReplication(
      scoped_refptr<UniverseReplicationInfo> universe,
      const TableId& producer_table,
      const TableId& consumer_table,
      const SchemaVersion& producer_schema_version,
      const SchemaVersion& consumer_schema_version,
      const ColocationSchemaVersions& colocated_schema_versions);

  Status AddSchemaVersionMappingToUniverseReplication(
      scoped_refptr<UniverseReplicationInfo> universe,
      ColocationId consumer_table,
      const SchemaVersion& producer_schema_version,
      const SchemaVersion& consumer_schema_version);

  // If all tables have been validated, creates a CDC stream for each table.
  Status CreateCdcStreamsIfReplicationValidated(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids);

  Status AddValidatedTableAndCreateCdcStreams(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids,
      const TableId& producer_table,
      const TableId& consumer_table,
      const ColocationSchemaVersions& colocated_schema_versions);

  Status ValidateTableAndCreateCdcStreams(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::shared_ptr<client::YBTableInfo>& producer_info,
      const SetupReplicationInfo& setup_info);

  void GetTableSchemaCallback(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::shared_ptr<client::YBTableInfo>& producer_info,
      const SetupReplicationInfo& setup_info, const Status& s);
  void GetTablegroupSchemaCallback(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
      const TablegroupId& producer_tablegroup_id, const SetupReplicationInfo& setup_info,
      const Status& s);
  Status GetTablegroupSchemaCallbackInternal(
      scoped_refptr<UniverseReplicationInfo>& universe,
      const std::vector<client::YBTableInfo>& infos, const TablegroupId& producer_tablegroup_id,
      const SetupReplicationInfo& setup_info, const Status& s);
  void GetColocatedTabletSchemaCallback(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::shared_ptr<std::vector<client::YBTableInfo>>& info,
      const SetupReplicationInfo& setup_info, const Status& s);

  typedef std::vector<
      std::tuple<xrepl::StreamId, TableId, std::unordered_map<std::string, std::string>>>
      StreamUpdateInfos;

  void GetCDCStreamCallback(
      const xrepl::StreamId& bootstrap_id, std::shared_ptr<TableId> table_id,
      std::shared_ptr<std::unordered_map<std::string, std::string>> options,
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table,
      std::shared_ptr<XClusterRpcTasks> xcluster_rpc, const Status& s,
      std::shared_ptr<StreamUpdateInfos> stream_update_infos,
      std::shared_ptr<std::mutex> update_infos_lock);

  void AddCDCStreamToUniverseAndInitConsumer(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table,
      const Result<xrepl::StreamId>& stream_id, std::function<void()> on_success_cb = nullptr);

  Status AddCDCStreamToUniverseAndInitConsumerInternal(
      scoped_refptr<UniverseReplicationInfo> universe, const TableId& table,
      const xrepl::StreamId& stream_id, std::function<void()> on_success_cb);

  void MergeUniverseReplication(
      scoped_refptr<UniverseReplicationInfo> info, xcluster::ReplicationGroupId original_id);

  Status DeleteUniverseReplicationUnlocked(scoped_refptr<UniverseReplicationInfo> info);
  Status DeleteUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id, bool ignore_errors,
      bool skip_producer_stream_deletion, DeleteUniverseReplicationResponsePB* resp);

  void MarkUniverseReplicationFailed(
      scoped_refptr<UniverseReplicationInfo> universe, const Status& failure_status);
  // Sets the appropriate failure state and the error status on the universe and commits the
  // mutation to the sys catalog.
  void MarkUniverseReplicationFailed(
      const Status& failure_status, CowWriteLock<PersistentUniverseReplicationInfo>* universe_lock,
      scoped_refptr<UniverseReplicationInfo> universe);

  // Sets the appropriate state and on the replication bootstrap and commits the
  // mutation to the sys catalog.
  void SetReplicationBootstrapState(
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info,
      const SysUniverseReplicationBootstrapEntryPB::State& state);

  std::shared_ptr<cdc::CDCServiceProxy> GetCDCServiceProxy(
      client::internal::RemoteTabletServer* ts);

  Result<client::internal::RemoteTabletServer*> GetLeaderTServer(
      client::internal::RemoteTabletPtr tablet);

  // Consumer API: Find out if bootstrap is required for the Producer tables.
  Status IsBootstrapRequiredOnProducer(
      scoped_refptr<UniverseReplicationInfo> universe,
      const TableId& producer_table,
      const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids);

  // Check if bootstrapping is required for a table.
  Status IsTableBootstrapRequired(
      const TableId& table_id,
      const xrepl::StreamId& stream_id,
      CoarseTimePoint deadline,
      bool* const bootstrap_required);

  std::unordered_set<xrepl::StreamId> GetCDCSDKStreamsForTable(const TableId& table_id) const;

  Status CreateTransactionAwareSnapshot(
      const CreateSnapshotRequestPB& req, CreateSnapshotResponsePB* resp, CoarseTimePoint deadline);

  Status CreateNonTransactionAwareSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp, const LeaderEpoch& epoch);

  Status RestoreNonTransactionAwareSnapshot(
      const SnapshotId& snapshot_id, const LeaderEpoch& epoch);

  Status DeleteNonTransactionAwareSnapshot(const SnapshotId& snapshot_id, const LeaderEpoch& epoch);

  Status AddNamespaceEntriesToPB(
      const std::vector<TableDescription>& tables,
      google::protobuf::RepeatedPtrField<SysRowEntry>* out,
      std::unordered_set<NamespaceId>* namespaces);

  Status AddUDTypeEntriesToPB(
      const std::vector<TableDescription>& tables,
      google::protobuf::RepeatedPtrField<SysRowEntry>* out,
      const std::unordered_set<NamespaceId>& namespaces);

  static Status AddTableAndTabletEntriesToPB(
      const std::vector<TableDescription>& tables,
      google::protobuf::RepeatedPtrField<SysRowEntry>* out,
      google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>*
          tablet_snapshot_info = nullptr,
      std::vector<scoped_refptr<TabletInfo>>* all_tablets = nullptr);

  Result<SysRowEntries> CollectEntriesForSequencesDataTable();

  Result<scoped_refptr<UniverseReplicationInfo>> CreateUniverseReplicationInfoForProducer(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses,
      const std::vector<NamespaceId>& producer_namespace_ids,
      const std::vector<NamespaceId>& consumer_namespace_ids,
      const google::protobuf::RepeatedPtrField<std::string>& table_ids, bool transactional);

  Result<scoped_refptr<UniverseReplicationBootstrapInfo>>
  CreateUniverseReplicationBootstrapInfoForProducer(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses,
      const LeaderEpoch& epoch, bool transactional);

  void ProcessXReplParentTabletDeletionPeriodically();

  Status DoProcessCDCSDKTabletDeletion();

  void RecordCDCSDKHiddenTablets(
      const std::vector<TabletInfoPtr>& tablets, const TabletDeleteRetainerInfo& delete_retainer)
      REQUIRES(mutex_);

  // Populate the response with the errors for the given replication group.
  Status PopulateReplicationGroupErrors(
      const xcluster::ReplicationGroupId& replication_group_id,
      GetReplicationStatusResponsePB* resp) const
      REQUIRES_SHARED(mutex_, xcluster_consumer_replication_error_map_mutex_);

  // Update the UniverseReplicationInfo object when toggling replication.
  Status SetUniverseReplicationInfoEnabled(
      const xcluster::ReplicationGroupId& replication_group_id, bool is_enabled) EXCLUDES(mutex_);

  // Update the cluster config and consumer registry objects when toggling replication.
  Status SetConsumerRegistryEnabled(
      const xcluster::ReplicationGroupId& replication_group_id, bool is_enabled,
      ClusterConfigInfo::WriteLock* l);

  void XClusterAddTableToNSReplication(
      const xcluster::ReplicationGroupId& replication_group_id, CoarseTimePoint deadline);

  // Find the list of producer table IDs that can be added to the current NS-level replication.
  Status XClusterNSReplicationSyncWithProducer(
      scoped_refptr<UniverseReplicationInfo> universe,
      std::vector<TableId>* producer_tables_to_add,
      bool* has_non_replicated_consumer_table);

  // Compute the list of producer table IDs that have a name-matching consumer table.
  Result<std::vector<TableId>> XClusterFindProducerConsumerOverlap(
      std::shared_ptr<XClusterRpcTasks> producer_xcluster_rpc,
      NamespaceIdentifierPB* producer_namespace, NamespaceIdentifierPB* consumer_namespace,
      size_t* num_non_matched_consumer_tables);

  // True when the cluster is a consumer of a NS-level replication stream.
  std::atomic<bool> namespace_replication_enabled_{false};

  void RemoveTableFromCDCSDKUnprocessedMap(const TableId& table_id, const NamespaceId& ns_id);

  void ClearXReplState() REQUIRES(mutex_);
  Status LoadXReplStream() REQUIRES(mutex_);
  Status LoadUniverseReplication() REQUIRES(mutex_);
  Status LoadUniverseReplicationBootstrap() REQUIRES(mutex_);

  bool CDCSDKShouldRetainHiddenTablet(const TabletId& tablet_id) EXCLUDES(mutex_);

  void CDCSDKPopulateDeleteRetainerInfoForTabletDrop(
      const TabletInfo& tablet_info, TabletDeleteRetainerInfo& delete_retainer) const
      REQUIRES_SHARED(mutex_);

  using SysCatalogPostLoadTasks = std::vector<std::pair<std::function<void()>, std::string>>;
  void StartPostLoadTasks(SysCatalogPostLoadTasks&& post_load_tasks);

  void StartWriteTableToSysCatalogTasks(TableIdSet&& tables_to_persist);

  bool IsTableXClusterConsumerUnlocked(const TableId& table_id) const REQUIRES_SHARED(mutex_);

  Status DropXClusterStreamsOfTables(const std::unordered_set<TableId>& table_ids) EXCLUDES(mutex_);

  void SchedulePostTabletCreationTasksForPendingTables(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  // Checks if the table is a consumer in an xCluster replication universe.
  bool IsTableXClusterConsumer(const TableId& table_id) const EXCLUDES(mutex_);

  Status BumpVersionAndStoreClusterConfig(
      ClusterConfigInfo* cluster_config, ClusterConfigInfo::WriteLock* l);

  // Background task that refreshes the in-memory map for YSQL pg_yb_catalog_version table.
  void RefreshPgCatalogVersionInfoPeriodically()
      EXCLUDES(heartbeat_pg_catalog_versions_cache_mutex_);
  // Helper function to schedule the next iteration of the pg catalog versions task.
  void ScheduleRefreshPgCatalogVersionsTask(bool schedule_now = false);

  void StartPgCatalogVersionsBgTaskIfStopped();
  void ResetCachedCatalogVersions()
      EXCLUDES(heartbeat_pg_catalog_versions_cache_mutex_);
  Status GetYsqlAllDBCatalogVersionsImpl(DbOidToCatalogVersionMap* versions);

  // Create the global transaction status table if needed (i.e. if it does not exist already).
  Status CreateGlobalTransactionStatusTableIfNeededForNewTable(
      const CreateTableRequestPB& req, rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status CreateGlobalTransactionStatusTableIfNotPresent(
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status XClusterRefreshLocalAutoFlagConfig(const LeaderEpoch& epoch);

  Status XClusterProcessPendingSchemaChanges(const LeaderEpoch& epoch);

  Status MaybeRestoreInitialSysCatalogSnapshotAndReloadSysCatalog(SysCatalogLoadingState* state)
      REQUIRES(mutex_);

  // Validates the indexes backfill status and updates their in-memory state if necessary; adds
  // such indexes to the tables_to_persist to write their updated state on disk. The first input
  // argument should be considered as an indexes collection grouped by the indexed table id, which
  // is required to have more effective validation performance.
  void ValidateIndexTablesPostLoad(std::unordered_map<TableId, TableIdSet>&& indexes_map,
                                   TableIdSet* tables_to_persist) EXCLUDES(mutex_);

  // GetTabletDeleteRetainerInfo, MarkTabletAsHidden, RecordHiddenTablets and
  // ShouldRetainHiddenTablet control the workflow of hiding tablets on drop and eventually deleting
  // them.
  // Any manager that needs to retain deleted tablets as hidden must hook into these methods.
  Result<TabletDeleteRetainerInfo> GetDeleteRetainerInfoForTabletDrop(const TabletInfo& tablet_info)
      EXCLUDES(mutex_);
  Result<TabletDeleteRetainerInfo> GetDeleteRetainerInfoForTableDrop(
      const TableInfo& table_info, const SnapshotSchedulesToObjectIdsMap& schedules_to_tables_map)
      EXCLUDES(mutex_);

  void MarkTabletAsHidden(
      SysTabletsEntryPB& tablet_pb, const HybridTime& hide_ht,
      const TabletDeleteRetainerInfo& delete_retainer) const;

  void RecordHiddenTablets(
      const TabletInfos& new_hidden_tablets, const TabletDeleteRetainerInfo& delete_retainer)
      EXCLUDES(mutex_);

  bool ShouldRetainHiddenTablet(
      const TabletInfo& tablet, const ScheduleMinRestoreTime& schedule_to_min_restore_time)
      EXCLUDES(mutex_);

  // Should be bumped up when tablet locations are changed.
  std::atomic<uintptr_t> tablet_locations_version_{0};

  rpc::ScheduledTaskTracker refresh_yql_partitions_task_;

  mutable MutexType tablespace_mutex_;

  // The tablespace_manager_ encapsulates two maps that are periodically updated by a background
  // task that reads tablespace information from the PG catalog tables. The task creates a new
  // manager instance, populates it with the information read from the catalog tables and updates
  // this shared_ptr. The maps themselves are thus never updated (no inserts/deletes/updates)
  // once populated and are garbage collected once all references to them go out of scope.
  // No clients are expected to update the manager, they take a lock merely to copy the
  // shared_ptr and read from it.
  std::shared_ptr<YsqlTablespaceManager> tablespace_manager_ GUARDED_BY(tablespace_mutex_);

  // Whether the periodic job to update tablespace info is running.
  std::atomic<bool> tablespace_bg_task_running_;

  rpc::ScheduledTaskTracker refresh_ysql_tablespace_info_task_;

  struct YsqlDdlTransactionState {
    // Indicates whether the transaction is committed or aborted or unknown.
    TxnState txn_state;

    // Indicates the verification state of the DDL transaction.
    YsqlDdlVerificationState state;

    // The table info objects of the tables affected by this transaction.
    std::vector<scoped_refptr<TableInfo>> tables;
  };

  // This map stores the transaction ids of all the DDL transactions undergoing verification.
  // For each transaction, it also stores pointers to the table info objects of the tables affected
  // by that transaction.
  mutable MutexType ddl_txn_verifier_mutex_;

  std::unordered_map<TransactionId, YsqlDdlTransactionState>
      ysql_ddl_txn_verfication_state_map_ GUARDED_BY(ddl_txn_verifier_mutex_);

  ServerRegistrationPB server_registration_;

  TabletSplitManager tablet_split_manager_;

  std::unique_ptr<CloneStateManager> clone_state_manager_;

  mutable MutexType delete_replica_task_throttler_per_ts_mutex_;

  // Maps a tserver uuid to the AsyncTaskThrottler instance responsible for throttling outstanding
  // AsyncDeletaReplica tasks per destination.
  std::unordered_map<std::string, std::unique_ptr<DynamicAsyncTaskThrottler>>
    delete_replica_task_throttler_per_ts_ GUARDED_BY(delete_replica_task_throttler_per_ts_mutex_);

  // Snapshot map: snapshot-id -> SnapshotInfo.
  typedef std::unordered_map<SnapshotId, scoped_refptr<SnapshotInfo>> SnapshotInfoMap;
  SnapshotInfoMap non_txn_snapshot_ids_map_;
  SnapshotId current_snapshot_id_;

  // mutex on should_send_universe_key_registry_mutex_.
  mutable simple_spinlock should_send_universe_key_registry_mutex_;
  // Should catalog manager resend latest universe key registry to tserver.
  std::unordered_map<TabletServerId, bool> should_send_universe_key_registry_
      GUARDED_BY(should_send_universe_key_registry_mutex_);

  // CDC Stream map: xrepl::StreamId -> CDCStreamInfo.
  typedef std::unordered_map<xrepl::StreamId, CDCStreamInfoPtr> CDCStreamInfoMap;
  CDCStreamInfoMap cdc_stream_map_ GUARDED_BY(mutex_);
  bool xrepl_maps_loaded_ GUARDED_BY(mutex_) = false;

  std::mutex xrepl_stream_ids_in_use_mutex_;
  std::unordered_set<xrepl::StreamId> xrepl_stream_ids_in_use_
      GUARDED_BY(xrepl_stream_ids_in_use_mutex_);

  mutable MutexType cdcsdk_unprocessed_table_mutex_;
  // In-memory map containing newly created tables which are yet to be added to CDCSDK stream's
  // metadata. Will be refreshed on master restart / leadership change thorugh the function:
  // 'FindAllTablesMissingInCDCSDKStream'.
  std::unordered_map<NamespaceId, std::unordered_set<TableId>>
      namespace_to_cdcsdk_unprocessed_table_map_ GUARDED_BY(cdcsdk_unprocessed_table_mutex_);

  // Map of all consumer tables that are part of xcluster replication, to a map of the stream infos.
  std::unordered_map<TableId, XClusterConsumerTableStreamIds>
      xcluster_consumer_table_stream_ids_map_ GUARDED_BY(mutex_);

  std::unordered_map<TableId, std::unordered_set<xrepl::StreamId>> cdcsdk_tables_to_stream_map_
      GUARDED_BY(mutex_);

  // Maps a ReplicationSlotName to the xrepl::StreamId of the stream it belongs to. Present for
  // CDCSDK streams created from the YSQL syntax.
  std::unordered_map<ReplicationSlotName, xrepl::StreamId> cdcsdk_replication_slots_to_stream_map_
      GUARDED_BY(mutex_);

  typedef std::unordered_map<xcluster::ReplicationGroupId, scoped_refptr<UniverseReplicationInfo>>
      UniverseReplicationInfoMap;
  UniverseReplicationInfoMap universe_replication_map_ GUARDED_BY(mutex_);

  // List of universe ids to universes that must be deleted
  std::deque<xcluster::ReplicationGroupId> universes_to_clear_ GUARDED_BY(mutex_);

  typedef std::unordered_map<
      xcluster::ReplicationGroupId, scoped_refptr<UniverseReplicationBootstrapInfo>>
      UniverseReplicationBootstrapInfoMap;
  UniverseReplicationBootstrapInfoMap universe_replication_bootstrap_map_ GUARDED_BY(mutex_);

  std::deque<xcluster::ReplicationGroupId> replication_bootstraps_to_clear_ GUARDED_BY(mutex_);

  // mutex on should_send_consumer_registry_mutex_.
  mutable simple_spinlock should_send_consumer_registry_mutex_;
  // Should catalog manager resend latest consumer registry to tserver.
  std::unordered_map<TabletServerId, bool> should_send_consumer_registry_
      GUARDED_BY(should_send_consumer_registry_mutex_);

  MasterSnapshotCoordinator snapshot_coordinator_;

  // True when the cluster is a producer of a valid replication stream.
  std::atomic<bool> cdc_enabled_{false};

  // Metadata on namespace-level replication setup. Map producer ID -> metadata.
  struct NSReplicationInfo {
    // Until after this time, no additional add table task will be scheduled.
    // Actively modified by the background thread.
    CoarseTimePoint next_add_table_task_time = CoarseTimePoint::max();
    int num_accumulated_errors;
  };
  std::unordered_map<xcluster::ReplicationGroupId, NSReplicationInfo> namespace_replication_map_
      GUARDED_BY(mutex_);

  std::atomic<bool> pg_catalog_versions_bg_task_running_ = {false};
  rpc::ScheduledTaskTracker refresh_ysql_pg_catalog_versions_task_;

  // For per-database catalog version mode upgrade support: when the gflag
  // --ysql_enable_db_catalog_version_mode is true, whether the table
  // pg_yb_catalog_version has been upgraded to have one row per database.
  // During upgrade the binaries are installed first but before YSQL migration
  // script is run pg_yb_catalog_version only has one row for template1.
  // YB Note:
  // (1) Each time we read the entire pg_yb_catalog_version table if the number
  // of rows is > 1 we assume that the table has exactly one row per database.
  // (2) This is only used to support per-database catalog version mode upgrade.
  // Once set it is never reset back to false. It is an error to change
  // pg_yb_catalog_version back to global catalog version mode when
  // --ysql_enable_db_catalog_version_mode=true.
  std::atomic<bool> catalog_version_table_in_perdb_mode_ = false;

  // mutex on heartbeat_pg_catalog_versions_cache_
  mutable MutexType heartbeat_pg_catalog_versions_cache_mutex_;
  std::optional<DbOidToCatalogVersionMap> heartbeat_pg_catalog_versions_cache_
    GUARDED_BY(heartbeat_pg_catalog_versions_cache_mutex_);
  // Fingerprint is only used when heartbeat_pg_catalog_versions_cache_ contains
  // a value.
  uint64_t heartbeat_pg_catalog_versions_cache_fingerprint_
    GUARDED_BY(heartbeat_pg_catalog_versions_cache_mutex_) = 0;

  std::unique_ptr<cdc::CDCStateTable> cdc_state_table_;

  mutable std::shared_mutex xcluster_consumer_replication_error_map_mutex_ ACQUIRED_AFTER(mutex_);
  std::unordered_map<xcluster::ReplicationGroupId, xcluster::ReplicationGroupErrors>
      xcluster_consumer_replication_error_map_
          GUARDED_BY(xcluster_consumer_replication_error_map_mutex_);

  std::atomic<bool> xcluster_auto_flags_revalidation_needed_{true};

  DISALLOW_COPY_AND_ASSIGN(CatalogManager);
};

}  // namespace master
}  // namespace yb
