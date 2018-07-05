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

#include "yb/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

// This must be called exactly once to initialize the YB/PostgreSQL gateway API before any other
// functions in this API are called.
void YBCInitPgGate();

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
                                    int attr_ybtype, bool is_hash, bool is_range);

YBCStatus YBCPgExecCreateTable(YBCPgStatement handle);

YBCStatus YBCPgAllocDropTable(YBCPgSession pg_session,
                              const char *database_name,
                              const char *schema_name,
                              const char *table_name,
                              bool if_exist,
                              YBCPgStatement *handle);

YBCStatus YBCPgExecDropTable(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// INSERT
YBCStatus YBCPgAllocInsert(YBCPgSession pg_session,
                           const char *database_name,
                           const char *schema_name,
                           const char *table_name,
                           YBCPgStatement *handle);

YBCStatus YBCPgInsertSetColumnInt2(YBCPgStatement handle, int attr_num, int16_t attr_value);

YBCStatus YBCPgInsertSetColumnInt4(YBCPgStatement handle, int attr_num, int32_t attr_value);

YBCStatus YBCPgInsertSetColumnInt8(YBCPgStatement handle, int attr_num, int64_t attr_value);

YBCStatus YBCPgInsertSetColumnFloat4(YBCPgStatement handle, int attr_num, float attr_value);

YBCStatus YBCPgInsertSetColumnFloat8(YBCPgStatement handle, int attr_num, double attr_value);

YBCStatus YBCPgInsertSetColumnText(YBCPgStatement handle, int attr_num, const char *attr_value,
                                   int attr_bytes);

YBCStatus YBCPgInsertSetColumnSerializedData(YBCPgStatement handle, int attr_num,
                                             const char *attr_value, int attr_bytes);

YBCStatus YBCPgExecInsert(YBCPgStatement handle);

#ifdef __cplusplus
}
#endif

#endif  // YB_YQL_PGGATE_YBC_PGGATE_H
