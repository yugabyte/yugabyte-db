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

std::unique_ptr<pggate::PgApiImpl> pgapi;

} // anonymous namespace

//--------------------------------------------------------------------------------------------------
// C API.
//--------------------------------------------------------------------------------------------------
extern "C" {

void YBCInitPgGate() {
  CHECK(pgapi.get() == nullptr) << __PRETTY_FUNCTION__ << " can only be called once";
  pgapi = std::make_unique<pggate::PgApiImpl>();
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
  return ToYBCStatus(pgapi->CreateSession(pg_env, database_name, pg_session));
}

YBCStatus YBCPgDestroySession(YBCPgSession pg_session) {
  return ToYBCStatus(pgapi->DestroySession(pg_session));
}

// Database Operations -----------------------------------------------------------------------------

YBCStatus YBCPgConnectDatabase(YBCPgSession pg_session, const char *database_name) {
  return ToYBCStatus(pgapi->ConnectDatabase(pg_session, database_name));
}

YBCStatus YBCPgAllocCreateDatabase(YBCPgSession pg_session,
                                    const char *database_name,
                                    YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->AllocCreateDatabase(pg_session, database_name, handle));
}

YBCStatus YBCPgExecCreateDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecCreateDatabase(handle));
}

YBCStatus YBCPgAllocDropDatabase(YBCPgSession pg_session,
                                  const char *database_name,
                                  bool if_exist,
                                  YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->AllocDropDatabase(pg_session, database_name, if_exist, handle));
}

YBCStatus YBCPgExecDropDatabase(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropDatabase(handle));
}

// Schema Operations -------------------------------------------------------------------------------
YBCStatus YBCPgAllocCreateSchema(YBCPgSession pg_session,
                                  const char *database_name,
                                  const char *schema_name,
                                  bool if_not_exist,
                                  YBCPgStatement *handle) {
  return YBCStatusNotSupport("SCHEMA");
#if (0)
  // TODO(neil) Turn this ON when schema is supported.
  return ToYBCStatus(pgapi->AllocCreateSchema(pg_session, schema_name, database_name,
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

YBCStatus YBCPgAllocDropSchema(YBCPgSession pg_session,
                                const char *database_name,
                                const char *schema_name,
                                bool if_exist,
                                YBCPgStatement *handle) {
  return YBCStatusNotSupport("SCHEMA");
#if (0)
  // TODO(neil) Turn this ON when schema is supported.
  return ToYBCStatus(pgapi->AllocDropSchema(pg_session, database_name, schema_name,
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

YBCStatus YBCPgAllocCreateTable(YBCPgSession pg_session,
                                 const char *database_name,
                                 const char *schema_name,
                                 const char *table_name,
                                 bool if_not_exist,
                                 YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->AllocCreateTable(pg_session, database_name, schema_name, table_name,
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

YBCStatus YBCPgAllocDropTable(YBCPgSession pg_session,
                               const char *database_name,
                               const char *schema_name,
                               const char *table_name,
                               bool if_exist,
                               YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->AllocDropTable(pg_session, database_name, schema_name, table_name,
                                           if_exist, handle));
}

YBCStatus YBCPgExecDropTable(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecDropTable(handle));
}

// INSERT Operations -------------------------------------------------------------------------------
YBCStatus YBCPgAllocInsert(YBCPgSession pg_session,
                           const char *database_name,
                           const char *schema_name,
                           const char *table_name,
                           YBCPgStatement *handle) {
  return ToYBCStatus(pgapi->AllocInsert(pg_session,
                                        database_name,
                                        schema_name,
                                        table_name,
                                        handle));
}

YBCStatus YBCPgInsertSetColumnInt2(YBCPgStatement handle, int attr_num, int16_t attr_value) {
  return ToYBCStatus(pgapi->InsertSetColumnInt2(handle, attr_num, attr_value));
}

YBCStatus YBCPgInsertSetColumnInt4(YBCPgStatement handle, int attr_num, int32_t attr_value) {
  return ToYBCStatus(pgapi->InsertSetColumnInt4(handle, attr_num, attr_value));
}

YBCStatus YBCPgInsertSetColumnInt8(YBCPgStatement handle, int attr_num, int64_t attr_value) {
  return ToYBCStatus(pgapi->InsertSetColumnInt8(handle, attr_num, attr_value));
}

YBCStatus YBCPgInsertSetColumnFloat4(YBCPgStatement handle, int attr_num, float attr_value) {
  return ToYBCStatus(pgapi->InsertSetColumnFloat4(handle, attr_num, attr_value));
}

YBCStatus YBCPgInsertSetColumnFloat8(YBCPgStatement handle, int attr_num, double attr_value) {
  return ToYBCStatus(pgapi->InsertSetColumnFloat8(handle, attr_num, attr_value));
}

YBCStatus YBCPgInsertSetColumnText(YBCPgStatement handle, int attr_num, const char *attr_value,
                                   int attr_bytes) {
  return ToYBCStatus(pgapi->InsertSetColumnText(handle, attr_num, attr_value, attr_bytes));
}

YBCStatus YBCPgInsertSetColumnSerializedData(YBCPgStatement handle, int attr_num,
                                             const char *attr_value, int attr_bytes) {
  return ToYBCStatus(pgapi->InsertSetColumnSerializedData(handle, attr_num, attr_value,
                                                          attr_bytes));
}

YBCStatus YBCPgExecInsert(YBCPgStatement handle) {
  return ToYBCStatus(pgapi->ExecInsert(handle));
}

// UPDATE Operations -------------------------------------------------------------------------------

// DELETE Operations -------------------------------------------------------------------------------

// SELECT Operations -------------------------------------------------------------------------------

} // extern "C"

} // namespace pggate
} // namespace yb
