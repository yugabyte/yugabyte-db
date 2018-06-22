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

#ifdef __cplusplus
extern "C" {
#endif

#include "yb/yql/pggate/ybc_pg_typedefs.h"

//--------------------------------------------------------------------------------------------------
// Environment and Session.

// Initialize ENV within which PGSQL calls will be executed.
YBCPgError YBCPgCreateEnv(YBCPgEnv *pg_env);
YBCPgError YBCPgDestroyEnv(YBCPgEnv pg_env);

// Initialize a session to process statements that come from the same client connection.
YBCPgError YBCPgCreateSession(const YBCPgEnv pg_env,
                              const char *database_name,
                              YBCPgSession *pg_session);
YBCPgError YBCPgDestroySession(YBCPgSession pg_session);

//--------------------------------------------------------------------------------------------------
// Connect database. Switch the connected database to the given "database_name".
YBCPgError YBCPgConnectDatabase(YBCPgSession pg_session, const char *database_name);

// Create database.
YBCPgError YBCPgAllocCreateDatabase(YBCPgSession pg_session,
                                    const char *database_name,
                                    YBCPgStatement *handle);
YBCPgError YBCPgExecCreateDatabase(YBCPgStatement handle);

// Drop database.
YBCPgError YBCPgAllocDropDatabase(YBCPgSession pg_session,
                                  const char *database_name,
                                  bool if_exist,
                                  YBCPgStatement *handle);
YBCPgError YBCPgExecDropDatabase(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// Create schema "database_name.schema_name".
// - When "database_name" is NULL, the connected database name is used.
YBCPgError YBCPgAllocCreateSchema(YBCPgSession pg_session,
                                  const char *database_name,
                                  const char *schema_name,
                                  bool if_not_exist,
                                  YBCPgStatement *handle);
YBCPgError YBCPgExecCreateSchema(YBCPgStatement handle);

// Drop schema "database_name.schema_name".
// - When "database_name" is NULL, the connected database name is used.
YBCPgError YBCPgDropSchema(YBCPgSession pg_session,
                           const char *schema_name,
                           bool if_exist,
                           YBCPgStatement *handle);
YBCPgError YBCPgExecDropSchema(YBCPgStatement handle);

//--------------------------------------------------------------------------------------------------
// Create and drop table "database_name.schema_name.table_name()".
// - When "schema_name" is NULL, the table "database_name.table_name" is created.
// - When "database_name" is NULL, the table "connected_database_name.table_name" is created.
YBCPgError YBCPgAllocCreateTable(YBCPgSession pg_session,
                                 const char *database_name,
                                 const char *schema_name,
                                 const char *table_name,
                                 bool if_not_exist,
                                 YBCPgStatement *handle);

YBCPgError YBCPgAddCreateTableColumn(YBCPgStatement handle, const char *col_name, int col_order,
                                     int col_type, bool is_hash, bool is_range);

YBCPgError YBCPgExecCreateTable(YBCPgStatement handle);

YBCPgError YBCPgAllocDropTable(YBCPgSession pg_session,
                               const char *database_name,
                               const char *schema_name,
                               const char *table_name,
                               bool if_exist,
                               YBCPgStatement *handle);

YBCPgError YBCPgExecDropTable(YBCPgStatement handle);

#ifdef __cplusplus
}
#endif

#endif  // YB_YQL_PGGATE_YBC_PGGATE_H
