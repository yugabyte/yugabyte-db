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

// C wrappers around "pggate" for PostgreSQL to call.

#ifndef YB_YQL_PGGATE_YBC_PGGATE_H
#define YB_YQL_PGGATE_YBC_PGGATE_H

#include <stdint.h>

#include "yb/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/pg_if_c_decl.h"

#ifdef __cplusplus
extern "C" {
#endif

// This must be called exactly once to initialize the YB/PostgreSQL gateway API before any other
// functions in this API are called.
void YBCInitPgGate();
void YBCDestroyPgGate();

//--------------------------------------------------------------------------------------------------
// Environment and Session.

// Initialize ENV within which PGSQL calls will be executed.
YBCStatus YBCPgCreateEnv(YBCPgEnv *pg_env);
YBCStatus YBCPgDestroyEnv(YBCPgEnv pg_env);

// Initialize a session to process statements that come from the same client connection.
YBCStatus YBCPgCreateSession(const YBCPgEnv pg_env,
                             const char *database_name,
                             YBCPgSession *pg_session);
YBCStatus YBCPgDestroySession(YBCPgSession pg_session);

//--------------------------------------------------------------------------------------------------
// Connect database. Switch the connected database to the given "database_name".
YBCStatus YBCPgConnectDatabase(YBCPgSession pg_session, const char *database_name);

// Create database.
YBCStatus YBCPgNewCreateDatabase(YBCPgSession pg_session,
                                   const char *database_name,
                                   YBCPgStatement *handle);
YBCStatus YBCPgExecCreateDatabase(YBCPgStatement handle);

// Drop database.
YBCStatus YBCPgNewDropDatabase(YBCPgSession pg_session,
                                 const char *database_name,
                                 bool if_exist,
                                 YBCPgStatement *handle);
YBCStatus YBCPgExecDropDatabase(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// Delete statement given its handle.
YBCStatus YBCPgDeleteStatement(YBCPgStatement handle);

// Clear all values and expressions that were bound to the given statement.
YBCStatus YBCPgClearBinds(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// Create schema "database_name.schema_name".
// - When "database_name" is NULL, the connected database name is used.
YBCStatus YBCPgNewCreateSchema(YBCPgSession pg_session,
                                 const char *database_name,
                                 const char *schema_name,
                                 bool if_not_exist,
                                 YBCPgStatement *handle);
YBCStatus YBCPgExecCreateSchema(YBCPgStatement handle);

// Drop schema "database_name.schema_name".
// - When "database_name" is NULL, the connected database name is used.
YBCStatus YBCPgDropSchema(YBCPgSession pg_session,
                          const char *schema_name,
                          bool if_exist,
                          YBCPgStatement *handle);
YBCStatus YBCPgExecDropSchema(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// Create and drop table "database_name.schema_name.table_name()".
// - When "schema_name" is NULL, the table "database_name.table_name" is created.
// - When "database_name" is NULL, the table "connected_database_name.table_name" is created.
YBCStatus YBCPgNewCreateTable(YBCPgSession pg_session,
                                const char *database_name,
                                const char *schema_name,
                                const char *table_name,
                                bool if_not_exist,
                                YBCPgStatement *handle);

YBCStatus YBCPgCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    int attr_type, bool is_hash, bool is_range);

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle);

YBCStatus YBCPgNewDropTable(YBCPgSession pg_session,
                              const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              bool if_exist,
                              YBCPgStatement *handle);

YBCStatus YBCPgExecDropTable(YBCPgStatement handle);


YBCStatus YBCPgGetTableDesc(YBCPgSession pg_session,
                            const char *database_name,
                            const char *table_name,
                            YBCPgTableDesc *handle);

YBCStatus YBCPgDeleteTableDesc(YBCPgTableDesc handle);

YBCStatus YBCPgGetColumnInfo(YBCPgTableDesc table_desc,
                             int16_t attr_number,
                             bool *is_primary,
                             bool *is_hash);

//--------------------------------------------------------------------------------------------------
// All DML statements (select, insert, update, delete)

// This function is for specifying the selected or returned expressions.
// - SELECT target_expr1, target_expr2, ...
// - INSERT / UPDATE / DELETE ... RETURNING target_expr1, target_expr2, ...
YBCStatus YBCPgDmlAppendTarget(YBCPgStatement handle, YBCPgExpr target);

// Bind column with an expression in a statement.
// - INSERT INTO tab(x) VALUES(x_expr)
//   This bind-column function is used to bind "x" with "x_expr", and "x_expr" that can contain
//   bind-variables (placeholders) and contants whose values can be updated for each execution of
//   the same allocated statement.
//
// - SELECT / UPDATE / DELETE .... WHERE key = "key_expr"
//   This bind-column function is used to bind the primary column "key" with "key_expr" that can
//   contain bind-variables (placeholders) and contants whose values can be updated for each
//   execution of the same allocated statement.
YBCStatus YBCPgDmlBindColumn(YBCPgStatement handle,
                             int attr_num,
                             YBCPgExpr attr_value);

YBCStatus YBCPgDmlFetch(YBCPgStatement handle, uint64_t *values, bool *isnulls, bool *has_data);

//--------------------------------------------------------------------------------------------------
// INSERT
YBCStatus YBCPgNewInsert(YBCPgSession pg_session,
                           const char *database_name,
                           const char *schema_name,
                           const char *table_name,
                           YBCPgStatement *handle);

YBCStatus YBCPgExecInsert(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// UPDATE

//--------------------------------------------------------------------------------------------------
// DELETE

//--------------------------------------------------------------------------------------------------
// SELECT
YBCStatus YBCPgNewSelect(YBCPgSession pg_session,
                           const char *database_name,
                           const char *schema_name,
                           const char *table_name,
                           YBCPgStatement *handle);

// API for setting partition and range columns.
YBCStatus YBCPgExecSelect(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// Expressions.

YBCStatus YBCPgNewColumnRef(YBCPgStatement stmt, int attr_num, YBCPgExpr *expr_handle);

YBCStatus YBCPgNewConstantBool(YBCPgStatement stmt, bool value, bool is_null,
                               YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantInt2(YBCPgStatement stmt, int16_t value, bool is_null,
                               YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantInt4(YBCPgStatement stmt, int32_t value, bool is_null,
                               YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantInt8(YBCPgStatement stmt, int64_t value, bool is_null,
                               YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantFloat4(YBCPgStatement stmt, float value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantFloat8(YBCPgStatement stmt, double value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantText(YBCPgStatement stmt, const char *value, bool is_null,
                               YBCPgExpr *expr_handle);
YBCStatus YBCPgNewConstantChar(YBCPgStatement stmt, const char *value, int64_t bytes,
                               bool is_null, YBCPgExpr *expr_handle);

// The following update functions only work for constants.
// Overwriting the constant expression with new value.
YBCStatus YBCPgUpdateConstInt2(YBCPgExpr expr, int16_t value, bool is_null);
YBCStatus YBCPgUpdateConstInt4(YBCPgExpr expr, int32_t value, bool is_null);
YBCStatus YBCPgUpdateConstInt8(YBCPgExpr expr, int64_t value, bool is_null);
YBCStatus YBCPgUpdateConstFloat4(YBCPgExpr expr, float value, bool is_null);
YBCStatus YBCPgUpdateConstFloat8(YBCPgExpr expr, double value, bool is_null);
YBCStatus YBCPgUpdateConstText(YBCPgExpr expr, const char *value, bool is_null);
YBCStatus YBCPgUpdateConstChar(YBCPgExpr expr, const char *value, int64_t bytes, bool is_null);

//------------------------------------------------------------------------------------------------
// Deprecated Code End. The above code should be deleted.
//------------------------------------------------------------------------------------------------

YBCPgTxnManager YBCGetPgTxnManager();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // YB_YQL_PGGATE_YBC_PGGATE_H
