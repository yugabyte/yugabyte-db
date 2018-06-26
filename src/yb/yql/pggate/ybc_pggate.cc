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

#include "yb/util/ybc_util.h"
#include "yb/yql/pggate/pggate.h"

namespace yb {
namespace pggate {

#include "yb/yql/pggate/ybc_pggate.h"

//--------------------------------------------------------------------------------------------------
// C++ Implementation.
// All C++ objects and structures in this module are listed in the following namespace.
//--------------------------------------------------------------------------------------------------
namespace {

std::unique_ptr<yb::pggate::PgApiImpl> pgapi;

} // anonymous namespace

//--------------------------------------------------------------------------------------------------
// C API.
//--------------------------------------------------------------------------------------------------
extern "C" {

void YBCInitPgGate() {
  CHECK(pgapi.get() == nullptr) << __PRETTY_FUNCTION__ << " can only be called once";
  pgapi = std::make_unique<PgApiImpl>();
}

YBCPgError YBCPgCreateEnv(YBCPgEnv *pg_env) {
  return pgapi->CreateEnv(pg_env);
}

YBCPgError YBCPgDestroyEnv(YBCPgEnv pg_env) {
  return pgapi->DestroyEnv(pg_env);
}

YBCPgError YBCPgCreateSession(const YBCPgEnv pg_env,
                              const char *database_name,
                              YBCPgSession *pg_session) {
  return pgapi->CreateSession(pg_env, database_name, pg_session);
}

YBCPgError YBCPgDestroySession(YBCPgSession pg_session) {
  return pgapi->DestroySession(pg_session);
}

// Database Operations -----------------------------------------------------------------------------
YBCPgError YBCPgConnectDatabase(YBCPgSession pg_session, const char *database_name) {
  return pgapi->ConnectDatabase(pg_session, database_name);
}

YBCPgError YBCPgAllocCreateDatabase(YBCPgSession pg_session,
                                    const char *database_name,
                                    YBCPgStatement *handle) {
  return pgapi->AllocCreateDatabase(pg_session, database_name, handle);
}

YBCPgError YBCPgExecCreateDatabase(YBCPgStatement handle) {
  return pgapi->ExecCreateDatabase(handle);
}

YBCPgError YBCPgAllocDropDatabase(YBCPgSession pg_session,
                                  const char *database_name,
                                  bool if_exist,
                                  YBCPgStatement *handle) {
  return pgapi->AllocDropDatabase(pg_session, database_name, if_exist, handle);
}

YBCPgError YBCPgExecDropDatabase(YBCPgStatement handle) {
  return pgapi->ExecDropDatabase(handle);
}

// Schema Operations -------------------------------------------------------------------------------
YBCPgError YBCPgAllocCreateSchema(YBCPgSession pg_session,
                                  const char *database_name,
                                  const char *schema_name,
                                  bool if_not_exist,
                                  YBCPgStatement *handle) {
  return pgapi->AllocCreateSchema(pg_session, schema_name, database_name, if_not_exist, handle);
}

YBCPgError YBCPgExecCreateSchema(YBCPgStatement handle) {
  return pgapi->ExecCreateSchema(handle);
}

YBCPgError YBCPgAllocDropSchema(YBCPgSession pg_session,
                                const char *database_name,
                                const char *schema_name,
                                bool if_exist,
                                YBCPgStatement *handle) {
  return pgapi->AllocDropSchema(pg_session, database_name, schema_name, if_exist, handle);
}

YBCPgError YBCPgExecDropSchema(YBCPgStatement handle) {
  return pgapi->ExecDropSchema(handle);
}

// Table Operations -------------------------------------------------------------------------------

YBCPgError YBCPgAllocCreateTable(YBCPgSession pg_session,
                                 const char *database_name,
                                 const char *schema_name,
                                 const char *table_name,
                                 bool if_not_exist,
                                 YBCPgStatement *handle) {
  return pgapi->AllocCreateTable(pg_session, database_name, schema_name, table_name, if_not_exist,
                                handle);
}

YBCPgError YBCPgAddCreateTableColumn(YBCPgStatement handle, const char *col_name, int col_order,
                                     int col_type, bool is_hash, bool is_range) {
  return pgapi->AddCreateTableColumn(handle, col_name, col_order, col_type, is_hash, is_range);
}

YBCPgError YBCPgExecCreateTable(YBCPgStatement handle) {
  return pgapi->ExecCreateTable(handle);
}

YBCPgError YBCPgAllocDropTable(YBCPgSession pg_session,
                               const char *database_name,
                               const char *schema_name,
                               const char *table_name,
                               bool if_exist,
                               YBCPgStatement *handle) {
  return pgapi->AllocDropTable(pg_session, database_name, schema_name, table_name, if_exist,
                              handle);
}

YBCPgError YBCPgExecDropTable(YBCPgStatement handle) {
  return pgapi->ExecDropTable(handle);
}

} // extern "C"

} // namespace pggate
} // namespace yb
