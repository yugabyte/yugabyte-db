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

#include "yb/common/read_hybrid_time.h"

#include "yb/master/master_admin.pb.h"
#include "yb/master/ysql/ysql_catalog_config.h"
#include "yb/master/ysql/ysql_manager_if.h"

#include "yb/rpc/scheduler.h"

namespace yb {

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace master {

class YsqlInitDBAndMajorUpgradeHandler;

class YsqlManager : public YsqlManagerIf {
 public:
  YsqlManager(Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~YsqlManager() override = default;

  void StartShutdown();
  void CompleteShutdown();

  void Clear();

  Status PrepareDefaultSysConfig(const LeaderEpoch& epoch);

  void LoadConfig(scoped_refptr<SysConfigInfo> config);

  void SysCatalogLoaded(const LeaderEpoch& epoch);

  uint64_t GetYsqlCatalogVersion() const;

  Result<uint64_t> IncrementYsqlCatalogVersion(const LeaderEpoch& epoch) override;

  // Starts an asynchronous run of initdb. Errors are handled in the callback. Returns true
  // if started running initdb, false if decided that it is not needed.
  Result<bool> StartRunningInitDbIfNeeded(const LeaderEpoch& epoch);

  YsqlCatalogConfig& GetYsqlCatalogConfig() override { return ysql_catalog_config_; }
  const YsqlCatalogConfig& GetYsqlCatalogConfig() const override { return ysql_catalog_config_; }

  IsOperationDoneResult IsInitDbDone() const;

  Status SetInitDbDone(const LeaderEpoch& epoch);

  // Checks if a YSQL major upgrade is in progress. This is derived from the state of the local
  // master and its Ysql config.
  // This will return false until the yb master leader is on the new version, and it will return
  // false as soon as the YsqlMajorCatalog has been finalized.
  // Use IsYsqlMajorVersionUpgradeInProgress if a more comprehensive view of the upgrade state is
  // required.
  bool IsMajorUpgradeInProgress() const override;

  void HandleNewTableId(const TableId& table_id);

  bool IsTransactionalSysCatalogEnabled() const override;
  Status SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) override;

  // Initiates ysql major catalog upgrade which involves global initdb, pg_upgrade, and catalog
  // version fixups.
  // IsYsqlMajorCatalogUpgradeDone must be called to track the completion status of the
  // operation.
  Status StartYsqlMajorCatalogUpgrade(
      const StartYsqlMajorCatalogUpgradeRequestPB* req,
      StartYsqlMajorCatalogUpgradeResponsePB* resp, rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  // Checks if ysql major catalog upgrade has completed.
  Status IsYsqlMajorCatalogUpgradeDone(
      const IsYsqlMajorCatalogUpgradeDoneRequestPB* req,
      IsYsqlMajorCatalogUpgradeDoneResponsePB* resp, rpc::RpcContext* rpc);

  // Returns the prior version's table if we are in the middle of a ysql major upgrade.
  // In all other cases, returns the current version.
  Result<TableId> GetVersionSpecificCatalogTableId(const TableId& current_table_id) const override;

  Status FinalizeYsqlMajorCatalogUpgrade(
      const FinalizeYsqlMajorCatalogUpgradeRequestPB* req,
      FinalizeYsqlMajorCatalogUpgradeResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // Rolls back the major YSQL catalog to the previous version. Deletes all of the new YSQL
  // version's catalog tables, and resets all upgrade-related state to initial values. Blocks until
  // the rollback is finished or fails.
  // Takes a long time to run. Use a timeout of at least 5 minutes when calling.
  Status RollbackYsqlMajorCatalogVersion(
      const RollbackYsqlMajorCatalogVersionRequestPB* req,
      RollbackYsqlMajorCatalogVersionResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status GetYsqlMajorCatalogUpgradeState(
      const GetYsqlMajorCatalogUpgradeStateRequestPB* req,
      GetYsqlMajorCatalogUpgradeStateResponsePB* resp, rpc::RpcContext* rpc);

  Status CreateYbAdvisoryLocksTableIfNeeded(const LeaderEpoch& epoch);

  Status ValidateWriteToCatalogTableAllowed(TableIdView table_id, bool is_forced_update) const;

  Status ValidateTServerVersion(const VersionInfoPB& version) const override;

  // Returns the pg_class.oid of the PostgreSQL table corresponding to the given DocDB table_id.
  // Returns a non-OK status if the DocDB table is uncommitted. Uncommitted DocDB tables can be:
  // - Orphaned: dropped in YSQL but still present in DocDB.
  // - Transient: created during an ongoing DDL operation.
  Result<PgOid> GetPgTableOidIfCommitted(
      const PgTableAllOids& oids, const ReadHybridTime& read_time = ReadHybridTime()) const;

  // Use GetCachedPgSchemaName() with internal cache if you need to get PgSchemaName
  // for several tables.
  // cache : [db oid -> ( [relnamespace oid -> relnamespace name],
  //                      [table oid -> relnamespace oid] )]
  Result<std::string> GetCachedPgSchemaName(
      const PgTableAllOids& oids, PgDbRelNamespaceMap& cache) const override;
  // Use GetPgSchemaName() if you need to get PgSchemaName for a single table.
  Result<std::string> GetPgSchemaName(
      const PgTableAllOids& oids,
      const ReadHybridTime& read_time = ReadHybridTime()) const override;

  Result<bool> GetPgIndexStatus(
      PgOid database_oid, PgOid index_oid, const std::string& status_col_name,
      const ReadHybridTime& read_time = ReadHybridTime()) const override;

  void RunBgTasks(const LeaderEpoch& epoch);

 private:
  Result<bool> StartRunningInitDbIfNeededInternal(const LeaderEpoch& epoch);

  Status CreatePgAutoAnalyzeService(const LeaderEpoch& epoch);

  void StartTablespaceBgTaskIfStopped();

  // Helper function to schedule the next iteration of the tablespace info task.
  void ScheduleRefreshTablespaceInfoTask(const bool schedule_now = false);

  // Background task that refreshes the in-memory state for YSQL tables with their associated
  // tablespace info.
  // Note: This function should only ever be called by StartTablespaceBgTaskIfStopped().
  void RefreshTablespaceInfoPeriodically();

  void StartPgCatalogVersionsBgTaskIfStopped();

  // Helper function to schedule the next iteration of the pg catalog versions task.
  void ScheduleRefreshPgCatalogVersionsTask(bool schedule_now = false);

  // Background task that refreshes the in-memory map for YSQL pg_yb_catalog_version table.
  void RefreshPgCatalogVersionInfoPeriodically();

  // Background task (and its helper functions) related to LISTEN/NOTIFY.
  Status ListenNotifyBgTask();
  Status CreateYbSystemDBIfNeeded();
  Status CreateListenNotifyObjects();

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  YsqlCatalogConfig ysql_catalog_config_;

  std::unique_ptr<YsqlInitDBAndMajorUpgradeHandler> ysql_initdb_and_major_upgrade_helper_;

  // This is used for tracking that initdb has started running previously.
  std::atomic<bool> pg_proc_exists_{false};

  bool advisory_locks_table_created_ = false;

  bool pg_auto_analyze_service_created_ = false;

  // Whether the periodic job to update tablespace info is running.
  std::atomic<bool> tablespace_bg_task_running_;

  rpc::ScheduledTaskTracker refresh_ysql_tablespace_info_task_;

  std::atomic<bool> pg_catalog_versions_bg_task_running_ = {false};
  rpc::ScheduledTaskTracker refresh_ysql_pg_catalog_versions_task_;

  std::atomic<bool> yb_system_db_created_ = {false};
  std::atomic<bool> creating_listen_notify_objects_ = {false};
  std::atomic<bool> created_listen_notify_objects_ = {false};

  DISALLOW_COPY_AND_ASSIGN(YsqlManager);
};

}  // namespace master

}  // namespace yb
