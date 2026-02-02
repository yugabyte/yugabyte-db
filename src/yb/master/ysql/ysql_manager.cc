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

#include "yb/master/ysql/ysql_manager.h"

#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_flags.h"
#include "yb/common/schema_pbutil.h"

#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_defaults.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ysql/ysql_initdb_major_upgrade_handler.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/util/flag_validators.h"
#include "yb/util/is_operation_done_result.h"

// TODO (mbautin, 2019-12): switch the default to true after updating all external callers
// (yb-ctl, YugaWare) and unit tests.
DEFINE_RUNTIME_bool(master_auto_run_initdb, false,
    "Automatically run initdb on master leader initialization");

DEFINE_NON_RUNTIME_uint32(num_advisory_locks_tablets, 1, "Number of advisory lock tablets");
DEFINE_validator(num_advisory_locks_tablets, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_NON_RUNTIME_int32(ysql_tablespace_info_refresh_secs, 30,
    "Frequency at which the table to tablespace information will be updated in master "
    "from pg catalog tables. A value of -1 disables the refresh task.");

DECLARE_bool(enable_heartbeat_pg_catalog_versions_cache);
DECLARE_bool(enable_ysql_tablespaces_for_placement);
DECLARE_bool(ysql_enable_auto_analyze_infra);

DECLARE_int32(heartbeat_interval_ms);

DECLARE_bool(TEST_ysql_yb_enable_listen_notify);

namespace yb::master {

using namespace std::literals;

namespace {

Status ExecuteStatementsAsync(
    const std::string& database_name, const std::vector<std::string>& statements,
    CatalogManagerIf& catalog_manager, const std::string& failure_warn_prefix,
    std::atomic<bool>* executing, std::atomic<bool>* execution_successful) {
  auto callback = [failure_warn_prefix, executing, execution_successful](const Status& status) {
    if (status.ok()) {
      execution_successful->store(true, std::memory_order_release);
    } else {
      WARN_NOT_OK(status, failure_warn_prefix);
    }
    executing->store(false, std::memory_order_release);
  };
  auto deadline = CoarseMonoClock::now() + MonoDelta::FromSeconds(60);
  RETURN_NOT_OK(
      ExecutePgsqlStatements(database_name, statements, catalog_manager, deadline, callback));
  *executing = true;
  return Status::OK();
}

}  // namespace

YsqlManager::YsqlManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog),
      ysql_catalog_config_(sys_catalog),
      tablespace_bg_task_running_(false) {
  ysql_initdb_and_major_upgrade_helper_ = std::make_unique<YsqlInitDBAndMajorUpgradeHandler>(
      master, ysql_catalog_config_, catalog_manager, sys_catalog, *catalog_manager.AsyncTaskPool());
}

void YsqlManager::StartShutdown() {
  refresh_ysql_pg_catalog_versions_task_.StartShutdown();
  refresh_ysql_tablespace_info_task_.StartShutdown();
}

void YsqlManager::CompleteShutdown() {
  refresh_ysql_pg_catalog_versions_task_.CompleteShutdown();
  refresh_ysql_tablespace_info_task_.CompleteShutdown();
}

void YsqlManager::Clear() { ysql_catalog_config_.Reset(); }

Status YsqlManager::PrepareDefaultSysConfig(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.PrepareDefaultIfNeeded(epoch);
}

void YsqlManager::LoadConfig(scoped_refptr<SysConfigInfo> config) {
  ysql_initdb_and_major_upgrade_helper_->Load(config);
  ysql_catalog_config_.SetConfig(config);
}

void YsqlManager::SysCatalogLoaded(const LeaderEpoch& epoch) {
  ysql_initdb_and_major_upgrade_helper_->SysCatalogLoaded(epoch);
}

Result<bool> YsqlManager::StartRunningInitDbIfNeeded(const LeaderEpoch& epoch) {
  if (IsInitDbDone()) {
    LOG(INFO) << "Cluster configuration indicates that initdb has already completed";
    return false;
  }

  if (pg_proc_exists_.load(std::memory_order_acquire)) {
    LOG(INFO) << "Table pg_proc exists, assuming initdb has already been run";
    // Mark initdb as done, in case it was done externally.
    // We assume pg_proc table means initdb is done.
    // We do NOT handle the case when initdb was terminated mid-run (neither here nor in
    // MakeYsqlSysCatalogTablesTransactional).
    RETURN_NOT_OK(SetInitDbDone(epoch));
    return false;
  }

  if (!FLAGS_master_auto_run_initdb) {
    LOG(INFO) << "--master_auto_run_initdb is set to false, not running initdb";
    return false;
  }

  LOG(INFO) << "initdb has never been run on this cluster, running it";

  RETURN_NOT_OK(ysql_initdb_and_major_upgrade_helper_->StartNewClusterGlobalInitDB(epoch));

  LOG(INFO) << "Successfully started initdb";
  return true;
}

bool YsqlManager::IsTransactionalSysCatalogEnabled() const {
  return ysql_catalog_config_.IsTransactionalSysCatalogEnabled();
}

Status YsqlManager::SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.SetTransactionalSysCatalogEnabled(epoch);
}

IsOperationDoneResult YsqlManager::IsInitDbDone() const {
  return ysql_catalog_config_.IsInitDbDone();
}

Status YsqlManager::SetInitDbDone(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.SetInitDbDone(Status::OK(), epoch);
}

bool YsqlManager::IsMajorUpgradeInProgress() const {
  return ysql_initdb_and_major_upgrade_helper_->IsMajorUpgradeInProgress();
}

uint64_t YsqlManager::GetYsqlCatalogVersion() const { return ysql_catalog_config_.GetVersion(); }

Result<uint64_t> YsqlManager::IncrementYsqlCatalogVersion(const LeaderEpoch& epoch) {
  return ysql_catalog_config_.IncrementVersion(epoch);
}

void YsqlManager::HandleNewTableId(const TableId& table_id) {
  if (table_id == kPgProcTableId) {
    // Needed to track whether initdb has started running.
    pg_proc_exists_.store(true, std::memory_order_release);
  }
}

Status YsqlManager::StartYsqlMajorCatalogUpgrade(
    const StartYsqlMajorCatalogUpgradeRequestPB* req, StartYsqlMajorCatalogUpgradeResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG(INFO) << "Running ysql major catalog upgrade";
  RETURN_NOT_OK(ysql_initdb_and_major_upgrade_helper_->StartYsqlMajorCatalogUpgrade(epoch));

  return Status::OK();
}

Status YsqlManager::IsYsqlMajorCatalogUpgradeDone(
    const IsYsqlMajorCatalogUpgradeDoneRequestPB* req,
    IsYsqlMajorCatalogUpgradeDoneResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Checking if ysql major catalog upgrade is done";
  auto is_operation_done = ysql_initdb_and_major_upgrade_helper_->IsYsqlMajorCatalogUpgradeDone();
  if (is_operation_done.done()) {
    resp->set_done(true);
    if (!is_operation_done.status().ok()) {
      return is_operation_done.status();
    }
  } else {
    resp->set_done(false);
  }
  return Status::OK();
}

Result<TableId> YsqlManager::GetVersionSpecificCatalogTableId(
    const TableId& current_table_id) const {
  DCHECK(IsCurrentVersionYsqlCatalogTable(current_table_id))
      << "Table id " << current_table_id << " is not a current version YSQL catalog table";

  // Use the current version of the catalog if it is updatable, since if the current version is
  // available in the MONITORING phase, it can be deleted by a Rollback.
  if (!IsMajorUpgradeInProgress()) {
    return current_table_id;
  }

  uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(current_table_id));
  uint32_t table_oid = VERIFY_RESULT(GetPgsqlTableOid(current_table_id));
  return GetPriorVersionYsqlCatalogTableId(database_oid, table_oid);
}

Status YsqlManager::FinalizeYsqlMajorCatalogUpgrade(
    const FinalizeYsqlMajorCatalogUpgradeRequestPB* req,
    FinalizeYsqlMajorCatalogUpgradeResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "Finalizing ysql major catalog upgrade";
  return ysql_initdb_and_major_upgrade_helper_->FinalizeYsqlMajorCatalogUpgrade(epoch);
}

// Note that this function should be able to be called any number of times while in upgrade mode.
Status YsqlManager::RollbackYsqlMajorCatalogVersion(
    const RollbackYsqlMajorCatalogVersionRequestPB* req,
    RollbackYsqlMajorCatalogVersionResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "YSQL major catalog upgrade rollback initiated";

  RETURN_NOT_OK(ysql_initdb_and_major_upgrade_helper_->RollbackYsqlMajorCatalogVersion(epoch));

  LOG(INFO) << "YSQL major catalog upgrade rollback completed";
  return Status::OK();
}

Status YsqlManager::GetYsqlMajorCatalogUpgradeState(
    const GetYsqlMajorCatalogUpgradeStateRequestPB* req,
    GetYsqlMajorCatalogUpgradeStateResponsePB* resp, rpc::RpcContext* rpc) {
  auto state =
      VERIFY_RESULT(ysql_initdb_and_major_upgrade_helper_->GetYsqlMajorCatalogUpgradeState());
  resp->set_state(state);
  return Status::OK();
}

Status YsqlManager::CreateYbAdvisoryLocksTableIfNeeded(const LeaderEpoch& epoch) {
  if (advisory_locks_table_created_ || !FLAGS_enable_ysql) {
    return Status::OK();
  }

  TableProperties table_properties;
  table_properties.SetTransactional(true);
  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn("dbid")->Type(DataType::UINT32)->HashPrimaryKey();
  schema_builder.AddColumn("classid")->Type(DataType::UINT32)->PrimaryKey();
  schema_builder.AddColumn("objid")->Type(DataType::UINT32)->PrimaryKey();
  schema_builder.AddColumn("objsubid")->Type(DataType::UINT32)->PrimaryKey();
  schema_builder.SetTableProperties(table_properties);
  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(std::string(tserver::kPgAdvisoryLocksTableName));
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::YQL_TABLE_TYPE);
  req.set_num_tablets(FLAGS_num_advisory_locks_tablets);

  auto schema = yb::client::internal::GetSchema(yb_schema);

  SchemaToPB(schema, req.mutable_schema());

  Status s = catalog_manager_.CreateTable(&req, &resp, /* RpcContext */ nullptr, epoch);
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }
  advisory_locks_table_created_ = true;
  return Status::OK();
}

Status YsqlManager::ValidateWriteToCatalogTableAllowed(
    TableIdView table_id, bool is_forced_update) const {
  SCHECK(
      ysql_initdb_and_major_upgrade_helper_->IsWriteToCatalogTableAllowed(
          table_id, is_forced_update),
      InternalError,
      "YSQL DDLs, and catalog modifications are not allowed during a major YSQL upgrade");

  return Status::OK();
}

Status YsqlManager::ValidateTServerVersion(const VersionInfoPB& version) const {
  return ysql_initdb_and_major_upgrade_helper_->ValidateTServerVersion(version);
}

Result<PgOid> YsqlManager::GetPgTableOidIfCommitted(
    const PgTableAllOids& oids, const ReadHybridTime& read_time) const {
  const auto committed_relfilenode_oid = VERIFY_RESULT(sys_catalog_.ReadPgClassColumnWithOidValue(
      oids.database_oid, oids.pg_table_oid, kPgClassRelFileNodeColumnName, read_time));
  if (committed_relfilenode_oid != oids.relfilenode_oid) {
    return STATUS(
        NotFound, Format("$0: $1", kCommittedPgsqlTableNotFoundErrorStr, oids.pg_table_oid),
        MasterError(MasterErrorPB::DOCDB_TABLE_NOT_COMMITTED));
  }
  return oids.pg_table_oid;
}

Result<std::string> YsqlManager::GetCachedPgSchemaName(
    const PgTableAllOids& oids, PgDbRelNamespaceMap& cache) const {
  const PgRelNamespaceData* nsp_data_ptr = FindOrNull(cache, oids.database_oid);
  if (nsp_data_ptr == nullptr) {
    PgRelNamespaceData nsp_data;
    // Load the maps for this PG database.
    nsp_data.rel_nsp_name_map = VERIFY_RESULT(sys_catalog_.ReadPgNamespaceNspnameMap(
        oids.database_oid));
    nsp_data.rel_nsp_oid_map = VERIFY_RESULT(sys_catalog_.ReadPgClassColumnWithOidValueMap(
        oids.database_oid, kPgClassRelNamespaceColumnName));
    nsp_data_ptr = &(cache.insert(std::make_pair(oids.database_oid,
                                                 std::move(nsp_data))).first->second);
  }

  const PgOid* const nsp_oid_ptr =
      FindOrNull(DCHECK_NOTNULL(nsp_data_ptr)->rel_nsp_oid_map, oids.pg_table_oid);
  const PgOid relnamespace_oid = (nsp_oid_ptr ? *nsp_oid_ptr : kPgInvalidOid);
  SCHECK_NE(relnamespace_oid, kPgInvalidOid, NotFound,
      Format("$0: $1", kRelnamespaceNotFoundErrorStr, oids.pg_table_oid));

  const std::string* const pg_schema_name_ptr =
      FindOrNull(nsp_data_ptr->rel_nsp_name_map, relnamespace_oid);
  // Return NotFound error if this relnamespace OID is not found in the pg_namespace table.
  SCHECK_NE(pg_schema_name_ptr, nullptr, NotFound,
      Format("Cannot find nspname for relnamespace oid $0", relnamespace_oid));
  return *pg_schema_name_ptr;
}

Result<std::string> YsqlManager::GetPgSchemaName(
    const PgTableAllOids& oids, const ReadHybridTime& read_time) const {
  const auto pg_table_oid =
      (read_time ? VERIFY_RESULT(GetPgTableOidIfCommitted(oids, read_time)) : oids.pg_table_oid);
  const auto relnamespace_oid = VERIFY_RESULT(sys_catalog_.ReadPgClassColumnWithOidValue(
      oids.database_oid, pg_table_oid, kPgClassRelNamespaceColumnName, read_time));
  if (relnamespace_oid == kPgInvalidOid) {
    return STATUS(
        NotFound, Format("$0: $1", kRelnamespaceNotFoundErrorStr, pg_table_oid),
        MasterError(MasterErrorPB::DOCDB_TABLE_NOT_COMMITTED));
  }
  return sys_catalog_.ReadPgNamespaceNspname(oids.database_oid, relnamespace_oid, read_time);
}

Result<bool> YsqlManager::GetPgIndexStatus(
    PgOid database_oid, PgOid index_oid, const std::string& status_col_name,
    const ReadHybridTime& read_time) const {
  return sys_catalog_.ReadPgIndexBoolColumn(database_oid, index_oid, status_col_name, read_time);
}

void YsqlManager::RunBgTasks(const LeaderEpoch& epoch) {
  if (!FLAGS_enable_ysql) {
    return;
  }

  // Avoid creating system tables if we are in the middle of upgrade.
  if (!IsMajorUpgradeInProgress()) {
    WARN_NOT_OK(
        CreateYbAdvisoryLocksTableIfNeeded(epoch), "Failed to create YB advisory locks table");

    if (FLAGS_ysql_enable_auto_analyze_infra)
      WARN_NOT_OK(CreatePgAutoAnalyzeService(epoch), "Failed to create Auto Analyze service");

    if (FLAGS_TEST_ysql_yb_enable_listen_notify) {
      WARN_NOT_OK(ListenNotifyBgTask(), "Failed to complete LISTEN/NOTIFY background task");
    }
  }

  StartTablespaceBgTaskIfStopped();
  StartPgCatalogVersionsBgTaskIfStopped();
}

Status YsqlManager::CreatePgAutoAnalyzeService(const LeaderEpoch& epoch) {
  if (pg_auto_analyze_service_created_ || !FLAGS_enable_ysql) {
    return Status::OK();
  }

  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kPgAutoAnalyzeTableId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(kPgAutoAnalyzeMutations)->Type(DataType::INT64);
  schema_builder.AddColumn(kPgAutoAnalyzeLastAnalyzeInfo)->Type(DataType::JSONB);
  schema_builder.AddColumn(kPgAutoAnalyzeCurrentAnalyzeInfo)->Type(DataType::JSONB);

  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  auto s = catalog_manager_.CreateStatefulService(
      StatefulServiceKind::PG_AUTO_ANALYZE, yb_schema, epoch);
  // It is possible that the table was already created. If so, there is nothing to do so we just
  // ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }

  pg_auto_analyze_service_created_ = true;
  return Status::OK();
}

void YsqlManager::StartTablespaceBgTaskIfStopped() {
  if (FLAGS_ysql_tablespace_info_refresh_secs <= 0 ||
      !FLAGS_enable_ysql_tablespaces_for_placement || FLAGS_create_initial_sys_catalog_snapshot) {
    // The tablespace bg task is disabled. Nothing to do.
    return;
  }

  const bool is_task_running = tablespace_bg_task_running_.exchange(true);
  if (is_task_running) {
    // Task already running, nothing to do.
    return;
  }

  ScheduleRefreshTablespaceInfoTask(true /* schedule_now */);
}

void YsqlManager::ScheduleRefreshTablespaceInfoTask(const bool schedule_now) {
  int wait_time = 0;

  if (!schedule_now) {
    wait_time = FLAGS_ysql_tablespace_info_refresh_secs;
    if (wait_time <= 0) {
      // The tablespace refresh task has been disabled.
      tablespace_bg_task_running_ = false;
      return;
    }
  }

  refresh_ysql_tablespace_info_task_.Bind(&master_.messenger()->scheduler());
  refresh_ysql_tablespace_info_task_.Schedule(
      [this](const Status& status) {
        Status s =
            catalog_manager_.SubmitBackgroundTask([this] { RefreshTablespaceInfoPeriodically(); });
        if (!s.IsOk()) {
          // Failed to submit task to the thread pool. Mark that the task is now
          // no longer running.
          LOG(WARNING) << "Failed to schedule: RefreshTablespaceInfoPeriodically";
          tablespace_bg_task_running_ = false;
        }
      },
      wait_time * 1s);
}

void YsqlManager::RefreshTablespaceInfoPeriodically() {
  if (!FLAGS_enable_ysql_tablespaces_for_placement) {
    tablespace_bg_task_running_ = false;
    return;
  }

  LeaderEpoch epoch;
  {
    SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
    if (!l.IsInitializedAndIsLeader()) {
      LOG(INFO) << "No longer the leader, so cancelling tablespace info task";
      tablespace_bg_task_running_ = false;
      return;
    }
    epoch = l.epoch();
  }

  // Refresh the tablespace info in memory.
  Status s = catalog_manager_.DoRefreshTablespaceInfo(epoch);
  if (!s.IsOk()) {
    LOG(WARNING) << "Tablespace refresh task failed with error " << s.ToString();
  }

  // Schedule the next iteration of the task.
  ScheduleRefreshTablespaceInfoTask();
}

void YsqlManager::StartPgCatalogVersionsBgTaskIfStopped() {
  // In per-database catalog version mode, if heartbeat PG catalog versions
  // cache is enabled, start a background task to periodically read the
  // pg_yb_catalog_version table and cache the result.
  if (FLAGS_ysql_enable_db_catalog_version_mode &&
      FLAGS_enable_heartbeat_pg_catalog_versions_cache) {
    const bool is_task_running = pg_catalog_versions_bg_task_running_.exchange(true);
    if (is_task_running) {
      // Task already running, nothing to do.
      return;
    }
    ScheduleRefreshPgCatalogVersionsTask(true /* schedule_now */);
  }
}

void YsqlManager::ScheduleRefreshPgCatalogVersionsTask(bool schedule_now) {
  // Schedule the next refresh catalog versions task. Do it twice every
  // tserver to master heartbeat so we have reasonably recent catalog versions
  // used for heartbeat response.
  auto wait_time = schedule_now ? 0 : (FLAGS_heartbeat_interval_ms / 2);
  refresh_ysql_pg_catalog_versions_task_.Bind(&master_.messenger()->scheduler());
  refresh_ysql_pg_catalog_versions_task_.Schedule(
      [this](const Status& status) {
        Status s = catalog_manager_.SubmitBackgroundTask(
            [this] { RefreshPgCatalogVersionInfoPeriodically(); });
        if (!s.ok()) {
          LOG(WARNING) << "Failed to schedule: RefreshPgCatalogVersionInfoPeriodically";
          pg_catalog_versions_bg_task_running_ = false;
          catalog_manager_.ResetCachedCatalogVersions();
        }
      },
      wait_time * 1ms);
}

void YsqlManager::RefreshPgCatalogVersionInfoPeriodically() {
  DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
  DCHECK(FLAGS_enable_heartbeat_pg_catalog_versions_cache);
  DCHECK(pg_catalog_versions_bg_task_running_);

  {
    SCOPED_LEADER_SHARED_LOCK(l, &catalog_manager_);
    if (!l.IsInitializedAndIsLeader()) {
      VLOG(2) << "No longer the leader, skipping catalog versions task";
      pg_catalog_versions_bg_task_running_ = false;
      catalog_manager_.ResetCachedCatalogVersions();
      return;
    }
  }

  // Refresh the catalog versions in memory.
  VLOG(2) << "Running " << __func__ << " task";
  catalog_manager_.RefreshPgCatalogVersionInfo();

  ScheduleRefreshPgCatalogVersionsTask();
}

Status YsqlManager::ListenNotifyBgTask() {
  if (!FLAGS_enable_ysql || created_listen_notify_objects_) {
    return Status::OK();
  }
  auto num_live_tservers = VERIFY_RESULT(catalog_manager_.GetNumLiveTServersForActiveCluster());

  if (num_live_tservers == 0) {
    LOG(INFO) << "No live tservers found, skipping LISTEN/NOTIFY background task for now";
  } else {
    RETURN_NOT_OK(CreateYbSystemDBIfNeeded());
    RETURN_NOT_OK(CreateListenNotifyObjects());
  }
  return Status::OK();
}

Status YsqlManager::CreateYbSystemDBIfNeeded() {
  DCHECK(FLAGS_enable_ysql);

  if (yb_system_db_created_.load(std::memory_order_acquire) ||
      creating_listen_notify_objects_.load(std::memory_order_acquire)) {
    return Status::OK();
  }

  // Check if kYbSystemDbName namespace already exists.
  auto db_oid = VERIFY_RESULT(catalog_manager_.sys_catalog()->GetYsqlDatabaseOid(kYbSystemDbName));
  if (db_oid != kPgInvalidOid) {
    yb_system_db_created_ = true;
    return Status::OK();
  }

  auto statement = Format("CREATE DATABASE $0", kYbSystemDbName);
  auto failure_warn_prefix = Format("Failed to create database $0", kYbSystemDbName);

  return ExecuteStatementsAsync(
      "yugabyte", {statement}, catalog_manager_, failure_warn_prefix,
      &creating_listen_notify_objects_, &yb_system_db_created_);
}

Status YsqlManager::CreateListenNotifyObjects() {
  DCHECK(FLAGS_enable_ysql);

  if (created_listen_notify_objects_.load(std::memory_order_acquire) ||
      creating_listen_notify_objects_.load(std::memory_order_acquire) ||
      !yb_system_db_created_.load(std::memory_order_acquire)) {
    return Status::OK();
  }

  std::vector<std::string> statements;
  statements.emplace_back(Format(
      R"(CREATE TABLE IF NOT EXISTS $0 (
           notif_uuid uuid NOT NULL,
           sender_node_uuid uuid NOT NULL,
           sender_pid int NOT NULL,
           db_oid oid NOT NULL,
           is_listen bool NOT NULL,
           data bytea NOT NULL,
           extra_options jsonb,
           CONSTRAINT $0_pkey PRIMARY KEY (notif_uuid HASH)
         ) SPLIT INTO 1 TABLETS)",
      kPgYbNotificationsTableName));
  statements.emplace_back(Format(
      R"(DO $$$$
        BEGIN
            IF NOT EXISTS (
                SELECT 1
                FROM pg_publication
                WHERE pubname = '$0'
            ) THEN CREATE PUBLICATION $0 FOR TABLE $1;
            END IF;
        END
        $$$$)",
      kPgYbNotificationsPublicationName, kPgYbNotificationsTableName));

  auto failure_warn_prefix = Format("Failed to create LISTEN/NOTIFY objects");

  return ExecuteStatementsAsync(
      kYbSystemDbName, statements, catalog_manager_, failure_warn_prefix,
      &creating_listen_notify_objects_, &created_listen_notify_objects_);
}

}  // namespace yb::master
