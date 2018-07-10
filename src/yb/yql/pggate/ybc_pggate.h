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
YBCStatus YBCPgAllocCreateDatabase(YBCPgSession pg_session,
                                   const char *database_name,
                                   YBCPgStatement *handle);
YBCStatus YBCPgExecCreateDatabase(YBCPgStatement handle);

// Drop database.
YBCStatus YBCPgAllocDropDatabase(YBCPgSession pg_session,
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
YBCStatus YBCPgAllocCreateSchema(YBCPgSession pg_session,
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
YBCStatus YBCPgAllocCreateTable(YBCPgSession pg_session,
                                const char *database_name,
                                const char *schema_name,
                                const char *table_name,
                                bool if_not_exist,
                                YBCPgStatement *handle);

YBCStatus YBCPgCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                    int attr_type, bool is_hash, bool is_range);

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle);

YBCStatus YBCPgAllocDropTable(YBCPgSession pg_session,
                              const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              bool if_exist,
                              YBCPgStatement *handle);

YBCStatus YBCPgExecDropTable(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
#if 0
// TODO(neil) On next diff.
// - Read how Postgres process SELECT and other DMLs and define the binding interface accordingly.
// - Read structures for all datatypes and define appropricate YBCPgBind.
// - Postfix the bind function name with datatype help avoiding "switch(datatype){}" for every call.
//   Do we need this? This only helps for execute-immediate.
//
// BINDING
// - These functions bind an input and/or output value to a memory space to be read and/or written
// respectively. They can be applied to all DML statements but not DDL statements.
// - As of now (7/27/2018) we can bind either a column of a table or a placeholder in SQL statements
// (such as ?, :1, :x) to a memory space.

// Binding columns: These functions are mostly only useful for partition or range columns (primary).
// - YugaByte storage requires information on partitiion and range columns to operate DML
//   statements. When setting up DML statements, the values that are associated with primary
//   columns must be specified.
// - If values for primary columns are not specified, full scan will be applied.

typedef struct YBCPgBind {
  int64_t data_header;  // indicators for NULL, IN, OUT, IN/OUT value.
  void *data;  // data buffer.
  size_t data_bytes;  // data buffer size in bytes.
} YBCPgBind;

// We might need to define one Bind function for all column types.
YBCStatus YBCPgBindColumn(YBCPgStatement, int attr_num, yb::DataType dtype, YBCPgBind *bind);

// Since template is not in C, postfixing the datatype with function name speeds up processing.
// For example, the following prototype postfixes function name with "Int2".
YBCStatus YBCPgBindColumnInt2(YBCPgStatement handle, int attr_num, YBCPgBind *bind);

// Binding placeholders: These functions are useful when processing expression.
YBCStatus YBCPgBindPlaceholder(YBCPgStatement, int attr_num, yb::DataType dtype, YBCPgBind *bind);

// Since template is not in C, postfixing the datatype with function name speeds up processing.
// For example, the following prototype postfixes function name with "Int2".
YBCStatus YBCPgBindPlaceholderInt2(YBCPgStatement handle, int var_index, YBCPgBind *bind);
#endif

//--------------------------------------------------------------------------------------------------
// INSERT
YBCStatus YBCPgAllocInsert(YBCPgSession pg_session,
                           const char *database_name,
                           const char *schema_name,
                           const char *table_name,
                           YBCPgStatement *handle);

YBCStatus YBCPgExecInsert(YBCPgStatement handle);

// TODO(neil) Once binding routines are defined, we can remove the "*Set*" functions.
YBCStatus YBCPgInsertSetColumnInt2(YBCPgStatement handle, int attr_num, int16_t attr_value);

YBCStatus YBCPgInsertSetColumnInt4(YBCPgStatement handle, int attr_num, int32_t attr_value);

YBCStatus YBCPgInsertSetColumnInt8(YBCPgStatement handle, int attr_num, int64_t attr_value);

YBCStatus YBCPgInsertSetColumnFloat4(YBCPgStatement handle, int attr_num, float attr_value);

YBCStatus YBCPgInsertSetColumnFloat8(YBCPgStatement handle, int attr_num, double attr_value);

YBCStatus YBCPgInsertSetColumnText(YBCPgStatement handle, int attr_num, const char *attr_value,
                                   int attr_bytes);

YBCStatus YBCPgInsertSetColumnSerializedData(YBCPgStatement handle, int attr_num,
                                             const char *attr_value, int attr_bytes);

//--------------------------------------------------------------------------------------------------
// SELECT
YBCStatus YBCPgAllocSelect(YBCPgSession pg_session,
                           const char *database_name,
                           const char *schema_name,
                           const char *table_name,
                           YBCPgStatement *handle);

// API for setting partition and range columns.
// When reading and writing, DocDB requires that the values of hash and range columns are provided.
YBCStatus YBCPgSelectSetColumnInt2(YBCPgStatement handle, int attr_num, int16_t attr_value);

YBCStatus YBCPgSelectSetColumnInt4(YBCPgStatement handle, int attr_num, int32_t attr_value);

YBCStatus YBCPgSelectSetColumnInt8(YBCPgStatement handle, int attr_num, int64_t attr_value);

YBCStatus YBCPgSelectSetColumnFloat4(YBCPgStatement handle, int attr_num, float attr_value);

YBCStatus YBCPgSelectSetColumnFloat8(YBCPgStatement handle, int attr_num, double attr_value);

YBCStatus YBCPgSelectSetColumnText(YBCPgStatement handle, int attr_num, const char *attr_value,
                                   int attr_bytes);

YBCStatus YBCPgSelectSetColumnSerializedData(YBCPgStatement handle, int attr_num,
                                             const char *attr_value, int attr_bytes);

// API for binding SQL expression with either values or memory spaces.
YBCStatus YBCPgSelectBindExprInt2(YBCPgStatement handle, int attr_num, int16_t *attr_value);

YBCStatus YBCPgSelectBindExprInt4(YBCPgStatement handle, int attr_num, int32_t *attr_value);

YBCStatus YBCPgSelectBindExprInt8(YBCPgStatement handle, int attr_num, int64_t *attr_value);

YBCStatus YBCPgSelectBindExprFloat4(YBCPgStatement handle, int attr_num, float *attr_value);

YBCStatus YBCPgSelectBindExprFloat8(YBCPgStatement handle, int attr_num, double *attr_value);

YBCStatus YBCPgSelectBindExprText(YBCPgStatement handle, int attr_num, char *attr_value,
                                  int64_t *attr_bytes);

YBCStatus YBCPgSelectBindExprSerializedData(YBCPgStatement handle, int attr_num,
                                            char *attr_value, int64_t *attr_bytes);

YBCStatus YBCPgExecSelect(YBCPgStatement handle);

YBCStatus YBCPgSelectFetch(YBCPgStatement handle, int64_t *row_count);

//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/if_macros_c_wrapper_decl.h"
#include "yb/yql/pggate/pggate_if.h"
#include "yb/yql/pggate/if_macros_undef.h"

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // YB_YQL_PGGATE_YBC_PGGATE_H
