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

#include "yb/common/schema_pbutil.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/master_defaults.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ysql/ysql_initdb_major_upgrade_handler.h"

#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/util/flag_validators.h"
#include "yb/util/is_operation_done_result.h"

// TODO (mbautin, 2019-12): switch the default to true after updating all external callers
// (yb-ctl, YugaWare) and unit tests.
DEFINE_RUNTIME_bool(master_auto_run_initdb, false,
    "Automatically run initdb on master leader initialization");

DEFINE_NON_RUNTIME_uint32(num_advisory_locks_tablets, 1, "Number of advisory lock tablets");
DEFINE_validator(num_advisory_locks_tablets, FLAG_GT_VALUE_VALIDATOR(0));

namespace yb::master {

YsqlManager::YsqlManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master),
      catalog_manager_(catalog_manager),
      sys_catalog_(sys_catalog),
      ysql_catalog_config_(sys_catalog) {
  ysql_initdb_and_major_upgrade_helper_ = std::make_unique<YsqlInitDBAndMajorUpgradeHandler>(
      master, ysql_catalog_config_, catalog_manager, sys_catalog, *catalog_manager.AsyncTaskPool());
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
    const TableId& table_id, bool is_forced_update) const {
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

Result<uint32_t> YsqlManager::GetYqlTableOid(
    const TableId& table_id, const PersistentTableInfo& table_info) const {
  RSTATUS_DCHECK_EQ(table_info.GetTableType(), PGSQL_TABLE_TYPE, InternalError,
      Format("Invalid table type of table $0", table_id));

  // YB_TODO: Review & simplify the code below.
  //          Or describe in details when & why the simple implementation
  //              pg_table_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id))
  //          does not work.
  //          GHI: https://github.com/yugabyte/yugabyte-db/issues/27590
  const auto relfilenode_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
  const auto pg_table_oid = VERIFY_RESULT(table_info.GetPgTableOid(table_id));

  // If this is a rewritten table, confirm that the relfilenode oid of pg_class entry with
  // OID pg_table_oid is table_oid.
  if (pg_table_oid != relfilenode_oid) {
    const auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(table_info.namespace_id()));
    const auto pg_class_relfilenode_oid = VERIFY_RESULT(
        sys_catalog_.ReadPgClassColumnWithOidValue(
            database_oid,
            pg_table_oid,
            kPgClassRelFileNodeColumnName));
    if (pg_class_relfilenode_oid != relfilenode_oid) {
      // This must be an orphaned table from a failed rewrite.
      return STATUS_FORMAT(NotFound, "$0: $1", kRelnamespaceNotFoundErrorStr, pg_table_oid);
    }
  }

  return pg_table_oid;
}

Result<std::string> YsqlManager::GetCachedPgSchemaName(
    const TableId& table_id, const PersistentTableInfo& table_info,
    PgDbRelNamespaceMap& cache) const {
  const auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(table_info.namespace_id()));
  const auto pg_table_oid = VERIFY_RESULT(GetYqlTableOid(table_id, table_info));

  const PgRelNamespaceData* nsp_data_ptr = FindOrNull(cache, database_oid);
  if (nsp_data_ptr == nullptr) {
    PgRelNamespaceData nsp_data;
    // Load the maps for this PG database.
    nsp_data.rel_nsp_name_map = VERIFY_RESULT(sys_catalog_.ReadPgNamespaceNspnameMap(
        database_oid));
    nsp_data.rel_nsp_oid_map = VERIFY_RESULT(sys_catalog_.ReadPgClassColumnWithOidValueMap(
        database_oid, kPgClassRelNamespaceColumnName));
    nsp_data_ptr = &(cache.insert(std::make_pair(std::move(database_oid),
                                                 std::move(nsp_data))).first->second);
  }

  const PgOid* const nsp_oid_ptr =
      FindOrNull(DCHECK_NOTNULL(nsp_data_ptr)->rel_nsp_oid_map, pg_table_oid);
  const PgOid relnamespace_oid = (nsp_oid_ptr ? *nsp_oid_ptr : kPgInvalidOid);
  SCHECK_NE(relnamespace_oid, kPgInvalidOid, NotFound,
      Format("$0: $1", kRelnamespaceNotFoundErrorStr, pg_table_oid));

  const std::string* const pg_schema_name_ptr =
      FindOrNull(nsp_data_ptr->rel_nsp_name_map, relnamespace_oid);
  // Return NotFound error if this relnamespace OID is not found in the pg_namespace table.
  SCHECK_NE(pg_schema_name_ptr, nullptr, NotFound,
      Format("Cannot find nspname for relnamespace oid $0", relnamespace_oid));
  return *pg_schema_name_ptr;
}

Result<std::string> YsqlManager::GetPgSchemaName(
    const TableId& table_id, const PersistentTableInfo& table_info) const {
  const auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(table_info.namespace_id()));
  const auto pg_table_oid = VERIFY_RESULT(GetYqlTableOid(table_id, table_info));
  const auto relnamespace_oid = VERIFY_RESULT(sys_catalog_.ReadPgClassColumnWithOidValue(
      database_oid, pg_table_oid, kPgClassRelNamespaceColumnName));
  SCHECK_FORMAT(relnamespace_oid != kPgInvalidOid, NotFound,
      "$0: $1", kRelnamespaceNotFoundErrorStr, pg_table_oid);
  return sys_catalog_.ReadPgNamespaceNspname(database_oid, relnamespace_oid);
}

}  // namespace yb::master
