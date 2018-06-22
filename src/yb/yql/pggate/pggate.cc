//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pggate.h"

#include "yb/yql/pggate/pg_ddl.h"
#include "yb/util/flag_tags.h"

DEFINE_int32(pgsql_rpc_keepalive_time_ms, 0,
             "Dummy variable at the moment as we don't use our RPC layer. "
             "If an RPC connection from a client is idle for this amount of time, the server "
             "will disconnect the client. Setting flag to 0 disables this clean up.");
TAG_FLAG(pgsql_rpc_keepalive_time_ms, advanced);

DEFINE_int32(pggate_rpc_timeout_secs, 5,
             "Dummy variable at the moment as we don't use our RPC layer");

DEFINE_int32(pggate_ybclient_reactor_threads, 24,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the PostgreSQL proxy server");

DEFINE_string(pggate_proxy_bind_address, "",
              "Address to which the PostgreSQL proxy server is bound.");

DEFINE_string(pggate_master_addresses, "",
              "Addresses of the master servers to which the PostgreSQL proxy server connects.");

namespace yb {
namespace pggate {

using std::make_shared;

//--------------------------------------------------------------------------------------------------

PggateOptions::PggateOptions() {
  server_type = "tserver";
  rpc_opts.default_port = kDefaultPort;
  rpc_opts.connection_keepalive_time_ms = FLAGS_pgsql_rpc_keepalive_time_ms;
  if (FLAGS_pggate_proxy_bind_address.empty()) {
    FLAGS_pggate_proxy_bind_address = strings::Substitute("0.0.0.0:$0",
                                                          PggateOptions::kDefaultPort);
  }
  rpc_opts.rpc_bind_addresses = FLAGS_pggate_proxy_bind_address;
  master_addresses_flag = FLAGS_pggate_master_addresses;
}

//--------------------------------------------------------------------------------------------------

// Because PgApiImpl global object might be initialized before GFLAGS, or the initialzation order
// is unknown, PbApiImpl() constructor should not use GFLAGS or allocate any objects.
// TODO(neil)
// - Mikhail said he already fixed this bug.
// - Create ONE async_client_init_ for postgres when creating tablet server options and pass it
//   to this API.
// - From PostgreSQL, post_master must call the API and setup valid tablet server addresses
//   before it can use the API.
// - Just for today (June 22, 2018) prototype, we skip this step for now.
PgApiImpl::PgApiImpl() :
  pggate_options_(),
  metric_registry_(new MetricRegistry()),
  metric_entity_(METRIC_ENTITY_server.Instantiate(metric_registry_.get(), "yb.pggate")),
  mem_tracker_(MemTracker::CreateTracker("PostgreSQL")),
  async_client_init_("pggate_ybclient",
                     FLAGS_pggate_ybclient_reactor_threads,
                     FLAGS_pggate_rpc_timeout_secs,
                     "" /* tserver_uuid */,
                     &pggate_options_,
                     metric_entity_,
                     mem_tracker_) {
}

//--------------------------------------------------------------------------------------------------

YBCPgErrorCode PgApiImpl::GetError(YBCPgSession pg_session, const char **error_text) {
  if (sessions_.find(pg_session) != sessions_.end()) {
    // Invalid session.
    *error_text = "Invalid session handle";
    Status s = STATUS(InvalidArgument, *error_text);
    return s.error_code();
  }
  return pg_session->GetError(error_text);
}

//--------------------------------------------------------------------------------------------------

YBCPgError PgApiImpl::CreateEnv(YBCPgEnv *pg_env) {
  *pg_env = pg_env_.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::DestroyEnv(YBCPgEnv pg_env) {
  pg_env_ = nullptr;
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

YBCPgError PgApiImpl::CreateSession(const YBCPgEnv pg_env,
                                    const string& database_name,
                                    YBCPgSession *pg_session) {
  PgSession::SharedPtr session = std::make_shared<PgSession>(client(), database_name);
  *pg_session = session.get();
  sessions_[*pg_session]= session;
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::DestroySession(YBCPgSession pg_session) {
  if (sessions_.find(pg_session) == sessions_.end()) {
    // Invalid session.
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }
  sessions_[pg_session]= nullptr;
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

PgSession::SharedPtr PgApiImpl::GetSession(YBCPgSession handle) {
  std::unordered_map<YBCPgSession, PgSession::SharedPtr>::iterator iter;
  iter = sessions_.find(handle);
  if (iter == sessions_.end()) {
    // Invalid session.
    return nullptr;
  }
  return iter->second;
}

PgStatement::SharedPtr PgApiImpl::GetStatement(YBCPgStatement handle) {
  std::unordered_map<YBCPgStatement, PgStatement::SharedPtr>::iterator iter;
  iter = statements_.find(handle);
  if (iter == statements_.end()) {
    // Invalid session.
    return nullptr;
  }
  return iter->second;
}

//--------------------------------------------------------------------------------------------------

YBCPgError PgApiImpl::ConnectDatabase(YBCPgSession pg_session, const char *database_name) {
  if (sessions_.find(pg_session) == sessions_.end()) {
    // Invalid session.
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }
  return pg_session->ConnectDatabase(database_name);
}

YBCPgError PgApiImpl::AllocCreateDatabase(YBCPgSession pg_session,
                                          const char *database_name,
                                          YBCPgStatement *handle) {
  PgSession::SharedPtr session = GetSession(pg_session);
  if (session == nullptr) {
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }

  PgCreateDatabase::SharedPtr stmt = make_shared<PgCreateDatabase>(session, database_name);
  statements_[stmt.get()] = stmt;

  *handle = stmt.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::ExecCreateDatabase(YBCPgStatement handle) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_CREATE_DATABASE)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }

  return static_cast<PgCreateTable*>(handle)->Exec();
}

YBCPgError PgApiImpl::AllocDropDatabase(YBCPgSession pg_session,
                                        const char *database_name,
                                        bool if_exist,
                                        YBCPgStatement *handle) {
  PgSession::SharedPtr session = GetSession(pg_session);
  if (session == nullptr) {
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }

  PgDropDatabase::SharedPtr stmt = make_shared<PgDropDatabase>(session, database_name, if_exist);
  statements_[stmt.get()] = stmt;

  *handle = stmt.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::ExecDropDatabase(YBCPgStatement handle) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_DROP_DATABASE)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }
  return static_cast<PgDropTable*>(handle)->Exec();
}

//--------------------------------------------------------------------------------------------------

YBCPgError PgApiImpl::AllocCreateSchema(YBCPgSession pg_session,
                                        const char *database_name,
                                        const char *schema_name,
                                        bool if_not_exist,
                                        YBCPgStatement *handle) {
  PgSession::SharedPtr session = GetSession(pg_session);
  if (session == nullptr) {
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }

  PgCreateSchema::SharedPtr stmt =
    make_shared<PgCreateSchema>(session, database_name, schema_name, if_not_exist);
  statements_[stmt.get()] = stmt;

  *handle = stmt.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::ExecCreateSchema(YBCPgStatement handle) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_CREATE_SCHEMA)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }
  return static_cast<PgCreateSchema*>(handle)->Exec();
}

YBCPgError PgApiImpl::AllocDropSchema(YBCPgSession pg_session,
                                      const char *database_name,
                                      const char *schema_name,
                                      bool if_exist,
                                      YBCPgStatement *handle) {
  PgSession::SharedPtr session = GetSession(pg_session);
  if (session == nullptr) {
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }

  PgDropSchema::SharedPtr stmt =
    make_shared<PgDropSchema>(session, database_name, schema_name, if_exist);
  statements_[stmt.get()] = stmt;

  *handle = stmt.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::ExecDropSchema(YBCPgStatement handle) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_DROP_SCHEMA)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }
  return static_cast<PgDropSchema*>(handle)->Exec();
}

//--------------------------------------------------------------------------------------------------

YBCPgError PgApiImpl::AllocCreateTable(YBCPgSession pg_session,
                                       const char *database_name,
                                       const char *schema_name,
                                       const char *table_name,
                                       bool if_not_exist,
                                       YBCPgStatement *handle) {
  PgSession::SharedPtr session = GetSession(pg_session);
  if (session == nullptr) {
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }

  PgCreateTable::SharedPtr stmt =
    make_shared<PgCreateTable>(session, database_name, schema_name, table_name, if_not_exist);
  statements_[stmt.get()] = stmt;

  *handle = stmt.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::AddCreateTableColumn(YBCPgStatement handle, const char *col_name,
                                           int col_order, int col_type, bool is_hash,
                                           bool is_range) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }

  PgCreateTable *pg_stmt = static_cast<PgCreateTable*>(handle);
  return pg_stmt->AddColumn(col_name, col_order, col_type, is_hash, is_range);
}

YBCPgError PgApiImpl::ExecCreateTable(YBCPgStatement handle) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }
  return static_cast<PgCreateTable*>(handle)->Exec();
}

YBCPgError PgApiImpl::AllocDropTable(YBCPgSession pg_session,
                                     const char *database_name,
                                     const char *schema_name,
                                     const char *table_name,
                                     bool if_exist,
                                     YBCPgStatement *handle) {
  PgSession::SharedPtr session = GetSession(pg_session);
  if (session == nullptr) {
    return YBCPgError::YBC_PGERROR_INVALID_SESSION;
  }

  PgDropTable::SharedPtr stmt =
    make_shared<PgDropTable>(session, database_name, schema_name, table_name, if_exist);
  statements_[stmt.get()] = stmt;

  *handle = stmt.get();
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgApiImpl::ExecDropTable(YBCPgStatement handle) {
  PgStatement::SharedPtr stmt = GetStatement(handle);
  if (!PgStatement::IsValidStmt(stmt, StmtOp::STMT_DROP_TABLE)) {
    // Invalid handle.
    return YBCPgError::YBC_PGERROR_INVALID_HANDLE;
  }
  return static_cast<PgDropTable*>(handle)->Exec();
}

} // namespace pggate
} // namespace yb
