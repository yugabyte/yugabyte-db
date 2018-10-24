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

#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/util/ybc-internal.h"

#include "yb/yql/pggate/pggate.h"

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

void YBCInitPgGate() {
  CHECK(pgapi == nullptr) << ": " << __PRETTY_FUNCTION__ << " can only be called once";
  pgapi_shutdown_done.exchange(false);
  pgapi = new pggate::PgApiImpl();
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

//--------------------------------------------------------------------------------------------------
// DDL Statements.
//--------------------------------------------------------------------------------------------------
// Database Operations -----------------------------------------------------------------------------

YBCStatus YBCPgConnectDatabase(YBCPgSession pg_session, const char *database_name) {
  return ToYBCStatus(pgapi->ConnectDatabase(pg_session, database_name));
}

YBCStatus YBCPgNewCreateDatabase(YBCPgSession pg_session,
                                 const char *database_name,
                                 YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateDatabase(pg_session, database_name, handle));
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

// Table Operations -------------------------------------------------------------------------------

YBCStatus YBCPgNewCreateTable(YBCPgSession pg_session,
                              const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              bool if_not_exist,
                              YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewCreateTable(pg_session, database_name, schema_name, table_name,
                                           if_not_exist, handle));
}

YBCStatus YBCPgCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    int attr_ybtype, bool is_hash, bool is_range) {
  return ToYBCStatus(pgapi->CreateTableAddColumn(handle, attr_name, attr_num, attr_ybtype,
                                                 is_hash, is_range));
}

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateTable(handle));
}

YBCStatus YBCPgNewDropTable(YBCPgSession pg_session,
                            const char *database_name,
                            const char *schema_name,
                            const char *table_name,
                            bool if_exist,
                            YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDropTable(pg_session, database_name, schema_name, table_name,
                                         if_exist, handle));
}

YBCStatus YBCPgExecDropTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTable(handle));
}

YBCStatus YBCPgGetTableDesc(YBCPgSession pg_session,
                            const char *database_name,
                            const char *table_name,
                            YBCPgTableDesc *handle) {
  return ToYBCStatus(pgapi->GetTableDesc(pg_session, database_name, table_name, handle));

}

YBCStatus YBCPgDeleteTableDesc(YBCPgTableDesc handle) {
  return ToYBCStatus(pgapi->DeleteTableDesc(handle));
}

YBCStatus YBCPgGetColumnInfo(YBCPgTableDesc table_desc,
                             int16_t attr_number,
                             bool *is_primary,
                             bool *is_hash) {
  return ToYBCStatus(pgapi->GetColumnInfo(table_desc,
      attr_number,
      is_primary,
      is_hash));
}

//--------------------------------------------------------------------------------------------------
// DML Statements.
//--------------------------------------------------------------------------------------------------

YBCStatus YBCPgDmlAppendTarget(YBCPgStatement handle, YBCPgExpr target) {
  return ToYBCStatus(pgapi->DmlAppendTarget(handle, target));
}

YBCStatus YBCPgDmlBindColumn(YBCPgStatement handle,
                             int attr_num,
                             YBCPgExpr attr_value) {
  return ToYBCStatus(pgapi->DmlBindColumn(handle, attr_num, attr_value));
}

YBCStatus YBCPgDmlFetch(YBCPgStatement handle, uint64_t *values, bool *isnulls, bool *has_data) {
  return ToYBCStatus(pgapi->DmlFetch(handle, values, isnulls, has_data));
}

// INSERT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewInsert(YBCPgSession pg_session,
                         const char *database_name,
                         const char *schema_name,
                         const char *table_name,
                         YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewInsert(pg_session,
                                      database_name,
                                      schema_name,
                                      table_name,
                                      handle));
}

YBCStatus YBCPgExecInsert(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecInsert(handle));
}

// UPDATE Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewUpdate(YBCPgSession pg_session,
                         const char *database_name,
                         const char *schema_name,
                         const char *table_name,
                         YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewUpdate(pg_session,
                                      database_name,
                                      schema_name,
                                      table_name,
                                      handle));
}

YBCStatus YBCPgExecUpdate(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecUpdate(handle));
}

// DELETE Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewDelete(YBCPgSession pg_session,
                         const char *database_name,
                         const char *schema_name,
                         const char *table_name,
                         YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewDelete(pg_session,
                                      database_name,
                                      schema_name,
                                      table_name,
                                      handle));
}

YBCStatus YBCPgExecDelete(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDelete(handle));
}

// SELECT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgNewSelect(YBCPgSession pg_session,
                         const char *database_name,
                         const char *schema_name,
                         const char *table_name,
                         YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->NewSelect(pg_session,
                                      database_name,
                                      schema_name,
                                      table_name,
                                      handle));
}

YBCStatus YBCPgExecSelect(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecSelect(handle));
}

//--------------------------------------------------------------------------------------------------
// Expression Operations
//--------------------------------------------------------------------------------------------------

YBCStatus YBCPgNewColumnRef(YBCPgStatement stmt, int attr_num, YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewColumnRef(stmt, attr_num, expr_handle));
}

YBCStatus YBCPgNewConstantBool(YBCPgStatement stmt, bool value, bool is_null,
                               YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantInt2(YBCPgStatement stmt, int16_t value, bool is_null,
                               YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantInt4(YBCPgStatement stmt, int32_t value, bool is_null,
                               YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantInt8(YBCPgStatement stmt, int64_t value, bool is_null,
                               YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantFloat4(YBCPgStatement stmt, float value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantFloat8(YBCPgStatement stmt, double value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantText(YBCPgStatement stmt, const char *value, bool is_null,
                               YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, is_null, expr_handle));
}

YBCStatus YBCPgNewConstantChar(YBCPgStatement stmt, const char *value, int64_t bytes, bool is_null,
                               YBCPgExpr *expr_handle) {
  return ToYBCStatus(pgapi->NewConstant(stmt, value, bytes, is_null, expr_handle));
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


YBCStatus YBCPgNewOperator(YBCPgStatement stmt, const char *opname, YBCPgExpr *op_handle) {
  return ToYBCStatus(pgapi->NewOperator(stmt, opname, op_handle));
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

} // extern "C"

} // namespace pggate
} // namespace yb
