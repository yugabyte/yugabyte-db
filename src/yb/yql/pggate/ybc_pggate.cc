// Copyright (c) YugaByte, Inc.
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

#include "yb/util/ybc-internal.h"
#include "yb/util/atomic.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/pggate.h"

DECLARE_bool(client_suppress_created_logs);

DEFINE_int32(pggate_num_connections_to_server, 1,
             "Number of underlying connections to each server from a PostgreSQL backend process. "
             "This overrides the value of --num_connections_to_server.");

DECLARE_int32(num_connections_to_server);

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// C++ Implementation.
// All C++ objects and structures in this module are listed in the following namespace.
//--------------------------------------------------------------------------------------------------
namespace {

// Using a raw pointer here to fully control object initialization and destruction.
pggate::PgApiImpl* pgapi;
std::atomic<bool> pgapi_shutdown_done;

} // anonymous namespace

//--------------------------------------------------------------------------------------------------
// C API.
//--------------------------------------------------------------------------------------------------
extern "C" {

void YBCInitPgGate(const YBCPgTypeEntity *YBCDataTypeTable, int count) {
  const char* initdb_mode_env_var_value = getenv("YB_PG_INITDB_MODE");
  if (initdb_mode_env_var_value && strcmp(initdb_mode_env_var_value, "1") == 0) {
    YBCSetInitDbMode();
  }
  CHECK(pgapi == nullptr) << ": " << __PRETTY_FUNCTION__ << " can only be called once";

  SetAtomicFlag(GetAtomicFlag(&FLAGS_pggate_num_connections_to_server),
                &FLAGS_num_connections_to_server);

  pgapi_shutdown_done.exchange(false);
  pgapi = new pggate::PgApiImpl(YBCDataTypeTable, count);
  VLOG(1) << "PgGate open";
}

void YBCDestroyPgGate() {
  if (pgapi_shutdown_done.exchange(true)) {
    LOG(FATAL) << __PRETTY_FUNCTION__ << " can only be called once";
  }
  delete pgapi;
  pgapi = nullptr;
  VLOG(1) << __PRETTY_FUNCTION__ << " finished";
}

YBCStatus YBCPgCreateEnv(YBCPgEnv *pg_env) {
  return ToYBCStatus(pgapi->CreateEnv(pg_env));
}

YBCStatus YBCPgDestroyEnv(YBCPgEnv pg_env) {
  return ToYBCStatus(pgapi->DestroyEnv(pg_env));
}

YBCStatus YBCPgCreateSession(const YBCPgEnv pg_env,
                             const char *database_name,
                             YBCPgSession *pg_session) {
  string db_name = database_name == NULL ? "" : database_name;
  return ToYBCStatus(pgapi->CreateSession(pg_env, database_name, pg_session));
}

YBCStatus YBCPgDestroySession(YBCPgSession pg_session) {
  return ToYBCStatus(pgapi->DestroySession(pg_session));
}

YBCStatus YBCPgInvalidateCache(YBCPgSession pg_session) {
  return ToYBCStatus(pgapi->InvalidateCache(pg_session));
}

const YBCPgTypeEntity *YBCPgFindTypeEntity(int type_oid) {
  return pgapi->FindTypeEntity(type_oid);
}

YBCPgDataType YBCPgGetType(const YBCPgTypeEntity *type_entity) {
  if (type_entity) {
    return type_entity->yb_type;
  }
  return YB_YQL_DATA_TYPE_UNKNOWN_DATA;
}

bool YBCPgAllowForPrimaryKey(const YBCPgTypeEntity *type_entity) {
  if (type_entity) {
    return type_entity->allow_for_primary_key;
  }
  return false;
}

//--------------------------------------------------------------------------------------------------
// DDL Statements.
//--------------------------------------------------------------------------------------------------
// Database Operations -----------------------------------------------------------------------------

YBCStatus YBCPgConnectDatabase(YBCPgSession pg_session, const char *database_name) {
  return ToYBCStatus(pgapi->ConnectDatabase(pg_session, database_name));
}

YBCStatus YBCPgNewCreateDatabase(YBCPgSession pg_session,
                                 const char *database_name,
                                 const YBCPgOid database_oid,
                                 const YBCPgOid source_database_oid,
                                 const YBCPgOid next_oid,
                                 YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateDatabase(pg_session, database_name, database_oid,
                                              source_database_oid, next_oid, handle));
}

YBCStatus YBCPgExecCreateDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateDatabase(handle));
}

YBCStatus YBCPgNewDropDatabase(YBCPgSession pg_session,
                               const char *database_name,
                               bool if_exist,
                               YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropDatabase(pg_session, database_name, if_exist, handle));
}

YBCStatus YBCPgExecDropDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropDatabase(handle));
}

YBCStatus YBCPgReserveOids(YBCPgSession pg_session,
                           const YBCPgOid database_oid,
                           const YBCPgOid next_oid,
                           const uint32_t count,
                           YBCPgOid *begin_oid,
                           YBCPgOid *end_oid) {
  return ToYBCStatus(pgapi->ReserveOids(pg_session, database_oid, next_oid, count,
                                        begin_oid, end_oid));
}

YBCStatus YBCPgGetCatalogMasterVersion(YBCPgSession pg_session, uint64_t *version) {
  return ToYBCStatus(pgapi->GetCatalogMasterVersion(pg_session, version));
}

// Schema Operations -------------------------------------------------------------------------------

YBCStatus YBCPgDeleteStatement(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->DeleteStatement(handle));
}

YBCStatus YBCPgClearBinds(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ClearBinds(handle));
}

// Schema Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewCreateSchema(YBCPgSession pg_session,
                               const char *database_name,
                               const char *schema_name,
                               bool if_not_exist,
                               YBCPgStatement *handle) {
  return YBCStatusNotSupport("SCHEMA");
#if (0)
  // TODO(neil) Turn this ON when schema is supported.
  return ToYBCStatus(pgapi->NewCreateSchema(pg_session, schema_name, database_name,
                                            if_not_exist, handle));
#endif
}

YBCStatus YBCPgExecCreateSchema(YBCPgStatement handle) {
  return YBCStatusNotSupport("SCHEMA");
#if (0)
  // TODO(neil) Turn this ON when schema is supported.
  return ToYBCStatus(pgapi->ExecCreateSchema(handle));
#endif
}

YBCStatus YBCPgNewDropSchema(YBCPgSession pg_session,
                             const char *database_name,
                             const char *schema_name,
                             bool if_exist,
                             YBCPgStatement *handle) {
  return YBCStatusNotSupport("SCHEMA");
#if (0)
  // TODO(neil) Turn this ON when schema is supported.
  return ToYBCStatus(pgapi->NewDropSchema(pg_session, database_name, schema_name,
                                          if_exist, handle));
#endif
}

YBCStatus YBCPgExecDropSchema(YBCPgStatement handle) {
  return YBCStatusNotSupport("SCHEMA");
#if (0)
  // TODO(neil) Turn this ON when schema is supported.
  return ToYBCStatus(pgapi->ExecDropSchema(handle));
#endif
}

YBCStatus YBCInsertSequenceTuple(YBCPgSession pg_session,
                                 int64_t db_oid,
                                 int64_t seq_oid,
                                 int64_t last_val,
                                 bool is_called) {
  return ToYBCStatus(pgapi->InsertSequenceTuple(pg_session, db_oid, seq_oid, last_val, is_called));
}

YBCStatus YBCUpdateSequenceTuple(YBCPgSession pg_session,
                                 int64_t db_oid,
                                 int64_t seq_oid,
                                 int64_t last_val,
                                 bool is_called,
                                 int64_t expected_last_val,
                                 bool expected_is_called,
                                 bool* skipped) {
  return ToYBCStatus(pgapi->UpdateSequenceTuple(pg_session, db_oid, seq_oid, last_val, is_called,
      expected_last_val, expected_is_called, skipped));
}

YBCStatus YBCReadSequenceTuple(YBCPgSession pg_session,
                               int64_t db_oid,
                               int64_t seq_oid,
                               int64_t *last_val,
                               bool *is_called) {
  return ToYBCStatus(pgapi->ReadSequenceTuple(pg_session, db_oid, seq_oid, last_val, is_called));
}

YBCStatus YBCDeleteSequenceTuple(YBCPgSession pg_session, int64_t db_oid, int64_t seq_oid) {
  return ToYBCStatus(pgapi->DeleteSequenceTuple(pg_session, db_oid, seq_oid));
}

// Table Operations -------------------------------------------------------------------------------

YBCStatus YBCPgNewCreateTable(YBCPgSession pg_session,
                              const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              const YBCPgOid database_oid,
                              const YBCPgOid table_oid,
                              bool is_shared_table,
                              bool if_not_exist,
                              bool add_primary_key,
                              YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewCreateTable(pg_session, database_name, schema_name, table_name,
                                           table_id, is_shared_table, if_not_exist, add_primary_key,
                                           handle));
}

YBCStatus YBCPgCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range) {
  return ToYBCStatus(pgapi->CreateTableAddColumn(handle, attr_name, attr_num, attr_type,
                                                 is_hash, is_range));
}

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateTable(handle));
}

YBCStatus YBCPgNewAlterTable(YBCPgSession pg_session,
                             const YBCPgOid database_oid,
                             const YBCPgOid table_oid,
                             YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewAlterTable(pg_session, table_id, handle));
}

YBCStatus YBCPgAlterTableAddColumn(YBCPgStatement handle, const char *name, int order,
                                   const YBCPgTypeEntity *attr_type, bool is_not_null) {
  return ToYBCStatus(pgapi->AlterTableAddColumn(handle, name, order, attr_type, is_not_null));
}

YBCStatus YBCPgAlterTableRenameColumn(YBCPgStatement handle, const char *oldname,
                                      const char *newname) {
  return ToYBCStatus(pgapi->AlterTableRenameColumn(handle, oldname, newname));
}

YBCStatus YBCPgAlterTableDropColumn(YBCPgStatement handle, const char *name) {
  return ToYBCStatus(pgapi->AlterTableDropColumn(handle, name));
}

YBCStatus YBCPgAlterTableRenameTable(YBCPgStatement handle, const char *db_name,
                                     const char *newname) {
  return ToYBCStatus(pgapi->AlterTableRenameTable(handle, db_name, newname));
}

YBCStatus YBCPgExecAlterTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecAlterTable(handle));
}

YBCStatus YBCPgNewDropTable(YBCPgSession pg_session,
                            const YBCPgOid database_oid,
                            const YBCPgOid table_oid,
                            bool if_exist,
                            YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewDropTable(pg_session, table_id, if_exist, handle));
}

YBCStatus YBCPgExecDropTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTable(handle));
}

YBCStatus YBCPgGetTableDesc(YBCPgSession pg_session,
                            const YBCPgOid database_oid,
                            const YBCPgOid table_oid,
                            YBCPgTableDesc *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->GetTableDesc(pg_session, table_id, handle));
}

YBCStatus YBCPgDeleteTableDesc(YBCPgTableDesc handle) {
  return ToYBCStatus(pgapi->DeleteTableDesc(handle));
}

YBCStatus YBCPgGetColumnInfo(YBCPgTableDesc table_desc,
                             int16_t attr_number,
                             bool *is_primary,
                             bool *is_hash) {
  return ToYBCStatus(pgapi->GetColumnInfo(table_desc, attr_number, is_primary, is_hash));
}

YBCStatus YBCPgSetCatalogCacheVersion(YBCPgStatement handle,
                                      uint64_t catalog_cache_version) {
  return ToYBCStatus(pgapi->SetCatalogCacheVersion(handle, catalog_cache_version));
}

YBCStatus YBCPgSetIfIsSysCatalogVersionChange(YBCPgStatement handle, bool *is_version_change) {
  return ToYBCStatus(pgapi->SetIfIsSysCatalogVersionChange(handle, is_version_change));
}

YBCStatus YBCPgNewTruncateTable(YBCPgSession pg_session,
                                const YBCPgOid database_oid,
                                const YBCPgOid table_oid,
                                YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewTruncateTable(pg_session, table_id, handle));
}

YBCStatus YBCPgExecTruncateTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecTruncateTable(handle));
}

// Index Operations -------------------------------------------------------------------------------

YBCStatus YBCPgNewCreateIndex(YBCPgSession pg_session,
                              const char *database_name,
                              const char *schema_name,
                              const char *index_name,
                              const YBCPgOid database_oid,
                              const YBCPgOid index_oid,
                              const YBCPgOid table_oid,
                              bool is_shared_index,
                              bool is_unique_index,
                              bool if_not_exist,
                              YBCPgStatement *handle) {
  const PgObjectId index_id(database_oid, index_oid);
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewCreateIndex(pg_session, database_name, schema_name, index_name,
                                           index_id, table_id, is_shared_index, is_unique_index,
                                           if_not_exist, handle));
}

YBCStatus YBCPgCreateIndexAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    const YBCPgTypeEntity *attr_type, bool is_hash, bool is_range) {
  return ToYBCStatus(pgapi->CreateIndexAddColumn(handle, attr_name, attr_num, attr_type,
                                                 is_hash, is_range));
}

YBCStatus YBCPgExecCreateIndex(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateIndex(handle));
}

YBCStatus YBCPgNewDropIndex(YBCPgSession pg_session,
                            const YBCPgOid database_oid,
                            const YBCPgOid index_oid,
                            bool if_exist,
                            YBCPgStatement *handle) {
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->NewDropIndex(pg_session, index_id, if_exist, handle));
}

YBCStatus YBCPgExecDropIndex(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropIndex(handle));
}

//--------------------------------------------------------------------------------------------------
// DML Statements.
//--------------------------------------------------------------------------------------------------

YBCStatus YBCPgDmlAppendTarget(YBCPgStatement handle, YBCPgExpr target) {
  return ToYBCStatus(pgapi->DmlAppendTarget(handle, target));
}

YBCStatus YBCPgDmlBindColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlBindColumn(handle, attr_num, attr_value));
}

YBCStatus YBCPgDmlBindIndexColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlBindIndexColumn(handle, attr_num, attr_value));
}

YBCStatus YBCPgDmlAssignColumn(YBCPgStatement handle,
                               int attr_num,
                               YBCPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlAssignColumn(handle, attr_num, attr_value));
}

YBCStatus YBCPgDmlFetch(YBCPgStatement handle, int32_t natts, uint64_t *values, bool *isnulls,
                        YBCPgSysColumns *syscols, bool *has_data) {
  return ToYBCStatus(pgapi->DmlFetch(handle, natts, values, isnulls, syscols, has_data));
}

YBCStatus YBCPgStartBufferingWriteOperations(YBCPgSession pg_session) {
  return ToYBCStatus(pgapi->StartBufferingWriteOperations(pg_session));
}

YBCStatus YBCPgFlushBufferedWriteOperations(YBCPgSession pg_session) {
  return ToYBCStatus(pgapi->FlushBufferedWriteOperations(pg_session));
}

YBCStatus YBCPgDmlExecWriteOp(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->DmlExecWriteOp(handle));
}

// INSERT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewInsert(YBCPgSession pg_session,
                         const YBCPgOid database_oid,
                         const YBCPgOid table_oid,
                         const bool is_single_row_txn,
                         YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewInsert(pg_session, table_id, is_single_row_txn, handle));
}

YBCStatus YBCPgExecInsert(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecInsert(handle));
}

// UPDATE Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewUpdate(YBCPgSession pg_session,
                         const YBCPgOid database_oid,
                         const YBCPgOid table_oid,
                         YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewUpdate(pg_session, table_id, handle));
}

YBCStatus YBCPgExecUpdate(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecUpdate(handle));
}

// DELETE Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewDelete(YBCPgSession pg_session,
                         const YBCPgOid database_oid,
                         const YBCPgOid table_oid,
                         YBCPgStatement *handle) {
  const PgObjectId table_id(database_oid, table_oid);
  return ToYBCStatus(pgapi->NewDelete(pg_session, table_id, handle));
}

YBCStatus YBCPgExecDelete(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDelete(handle));
}

// SELECT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewSelect(YBCPgSession pg_session,
                         const YBCPgOid database_oid,
                         const YBCPgOid table_oid,
                         const YBCPgOid index_oid,
                         YBCPgStatement *handle,
                         uint64_t* read_time) {
  const PgObjectId table_id(database_oid, table_oid);
  const PgObjectId index_id(database_oid, index_oid);
  return ToYBCStatus(pgapi->NewSelect(pg_session, table_id, index_id, handle, read_time));
}

YBCStatus YBCPgExecSelect(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecSelect(handle));
}

//--------------------------------------------------------------------------------------------------
// Expression Operations
//--------------------------------------------------------------------------------------------------

YBCStatus YBCPgNewColumnRef(YBCPgStatement stmt, int attr_num, const YBCPgTypeEntity *type_entity,
                            const YBCPgTypeAttrs *type_attrs, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewColumnRef(stmt, attr_num, type_entity, type_attrs, expr_handle));
}

YBCStatus YBCPgNewConstant(YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
                           uint64_t datum, bool is_null, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, type_entity, datum, is_null, expr_handle));
}

// Overwriting the expression's result with any desired values.
YBCStatus YBCPgUpdateConstInt2(YBCPgExpr expr, int16_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstInt4(YBCPgExpr expr, int32_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstInt8(YBCPgExpr expr, int64_t value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstFloat4(YBCPgExpr expr, float value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstFloat8(YBCPgExpr expr, double value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstText(YBCPgExpr expr, const char *value, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, is_null));
}

YBCStatus YBCPgUpdateConstChar(YBCPgExpr expr, const char *value,  int64_t bytes, bool is_null) {
  return ToYBCStatus(pgapi->UpdateConstant(expr, value, bytes, is_null));
}

YBCStatus YBCPgNewOperator(YBCPgStatement stmt, const char *opname,
                           const YBCPgTypeEntity *type_entity,
                           YBCPgExpr *op_handle) {
  return ToYBCStatus(pgapi->NewOperator(stmt, opname, type_entity, op_handle));
}

YBCStatus YBCPgOperatorAppendArg(YBCPgExpr op_handle, YBCPgExpr arg) {
  return ToYBCStatus(pgapi->OperatorAppendArg(op_handle, arg));
}

//------------------------------------------------------------------------------------------------
// Transaction operation.
//------------------------------------------------------------------------------------------------

YBCPgTxnManager YBCGetPgTxnManager() {
  return pgapi->GetPgTxnManager();
}

void YBCSetInitDbMode() {
  // Suppress log spew during initdb.
  FLAGS_client_suppress_created_logs = true;
}

} // extern "C"

} // namespace pggate
} // namespace yb
