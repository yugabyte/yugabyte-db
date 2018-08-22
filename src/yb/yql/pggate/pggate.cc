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
#include "yb/yql/pggate/pg_insert.h"
#include "yb/yql/pggate/pg_update.h"
#include "yb/yql/pggate/pg_delete.h"
#include "yb/yql/pggate/pg_select.h"
#include "yb/util/flag_tags.h"

DEFINE_int32(pgsql_rpc_keepalive_time_ms, 0,
             "If an RPC connection from a client is idle for this amount of time, the server "
             "will disconnect the client. Setting flag to 0 disables this clean up.");
TAG_FLAG(pgsql_rpc_keepalive_time_ms, advanced);

DEFINE_int32(pggate_rpc_timeout_secs, 60,
             "Timeout for RPCs from pggate to YB cluster");

DEFINE_int32(pggate_ybclient_reactor_threads, 2,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the PostgreSQL proxy server");

DEFINE_string(pggate_proxy_bind_address, "",
              "Address to which the PostgreSQL proxy server is bound.");

DEFINE_string(pggate_master_addresses, "",
              "Addresses of the master servers to which the PostgreSQL proxy server connects.");

namespace yb {
namespace pggate {

using std::make_shared;
using client::YBSession;

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

  const char* master_addresses_env_var_value = getenv("FLAGS_pggate_master_addresses");
  if (master_addresses_env_var_value && strlen(master_addresses_env_var_value) > 0) {
    if (!master_addresses_flag.empty()) {
      CHECK_EQ(master_addresses_flag, master_addresses_env_var_value)
          << "--pggate_master_addresses and the FLAGS_pggate_master_addresses env var are set to "
          << "different values: on the command line we have " << FLAGS_pggate_master_addresses
          << " but in the env var we have " << master_addresses_env_var_value;
    }
    master_addresses_flag = master_addresses_env_var_value;
  }

  server::MasterAddresses master_addresses;
  // TODO: we might have to allow setting master_replication_factor similarly to how it is done
  // in tserver to support master auto-discovery on Kubernetes.
  CHECK_OK(server::DetermineMasterAddresses(
      "YB_MASTER_ADDRESSES_FOR_PG", master_addresses_flag, /* master_replication_factor */ 0,
      &master_addresses, &master_addresses_flag));
  SetMasterAddresses(std::make_shared<server::MasterAddresses>(std::move(master_addresses)));
}

//--------------------------------------------------------------------------------------------------

PgApiImpl::PgApiImpl()
    : pggate_options_(),
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

CHECKED_STATUS PgApiImpl::CreateEnv(PgEnv **pg_env) {
  *pg_env = pg_env_.get();
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::DestroyEnv(PgEnv *pg_env) {
  pg_env_ = nullptr;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::CreateSession(const PgEnv *pg_env,
                                        const string& database_name,
                                        PgSession **pg_session) {
  auto session = make_scoped_refptr<PgSession>(client(), database_name);
  if (!database_name.empty()) {
    RETURN_NOT_OK(session->ConnectDatabase(database_name));
  }

  *pg_session = nullptr;
  session.swap(pg_session);
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::DestroySession(PgSession *pg_session) {
  if (pg_session) {
    pg_session->Release();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::DeleteStatement(PgStatement *handle) {
  if (handle) {
    handle->Release();
  }
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::ClearBinds(PgStatement *handle) {
  return handle->ClearBinds();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::ConnectDatabase(PgSession *pg_session, const char *database_name) {
  return pg_session->ConnectDatabase(database_name);
}

CHECKED_STATUS PgApiImpl::AllocCreateDatabase(PgSession *pg_session,
                                              const char *database_name,
                                              PgStatement **handle) {
  PgCreateDatabase* stmt = new PgCreateDatabase(pg_session, database_name);
  stmt->AddRef();
  *handle = stmt;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::ExecCreateDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  return down_cast<PgCreateDatabase*>(handle)->Exec();
}

CHECKED_STATUS PgApiImpl::AllocDropDatabase(PgSession *pg_session,
                                            const char *database_name,
                                            bool if_exist,
                                            PgStatement **handle) {
  PgDropDatabase* stmt = new PgDropDatabase(pg_session, database_name, if_exist);
  stmt->AddRef();
  *handle = stmt;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::ExecDropDatabase(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_DATABASE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropTable*>(handle)->Exec();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::AllocCreateSchema(PgSession *pg_session,
                                            const char *database_name,
                                            const char *schema_name,
                                            bool if_not_exist,
                                            PgStatement **handle) {
  if (pg_session == nullptr) {
    return STATUS(InvalidArgument, "Invalid session handle");
  }

  PgCreateSchema* stmt = new PgCreateSchema(pg_session, database_name, schema_name, if_not_exist);
  stmt->AddRef();
  *handle = stmt;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::ExecCreateSchema(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_SCHEMA)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgCreateSchema*>(handle)->Exec();
}

CHECKED_STATUS PgApiImpl::AllocDropSchema(PgSession *pg_session,
                                          const char *database_name,
                                          const char *schema_name,
                                          bool if_exist,
                                          PgStatement **handle) {
  PgDropSchema* stmt = new PgDropSchema(pg_session, database_name, schema_name, if_exist);
  stmt->AddRef();
  *handle = stmt;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::ExecDropSchema(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_SCHEMA)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropSchema*>(handle)->Exec();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::AllocCreateTable(PgSession *pg_session,
                                           const char *database_name,
                                           const char *schema_name,
                                           const char *table_name,
                                           bool if_not_exist,
                                           PgStatement **handle) {
  if (database_name == nullptr) {
    database_name = pg_session->connected_dbname();
  }

  PgCreateTable* stmt = new PgCreateTable(
      pg_session, database_name, schema_name, table_name, if_not_exist);
  stmt->AddRef();
  *handle = stmt;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::CreateTableAddColumn(PgStatement *handle, const char *attr_name,
                                               int attr_num, int attr_ybtype, bool is_hash,
                                               bool is_range) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgCreateTable *pg_stmt = down_cast<PgCreateTable*>(handle);
  return pg_stmt->AddColumn(attr_name, attr_num, attr_ybtype, is_hash, is_range);
}

CHECKED_STATUS PgApiImpl::ExecCreateTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_CREATE_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgCreateTable*>(handle)->Exec();
}

CHECKED_STATUS PgApiImpl::AllocDropTable(PgSession *pg_session,
                                         const char *database_name,
                                         const char *schema_name,
                                         const char *table_name,
                                         bool if_exist,
                                         PgStatement **handle) {
  if (database_name == nullptr) {
    database_name = pg_session->connected_dbname();
  }

  PgDropTable* stmt = new PgDropTable(pg_session, database_name, schema_name, table_name, if_exist);
  stmt->AddRef();
  *handle = stmt;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::ExecDropTable(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_DROP_TABLE)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }
  return down_cast<PgDropTable*>(handle)->Exec();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::AllocInsert(PgSession *pg_session,
                                      const char *database_name,
                                      const char *schema_name,
                                      const char *table_name,
                                      PgStatement **handle) {
  if (database_name == nullptr) {
    database_name = pg_session->connected_dbname();
  }

  auto stmt = make_scoped_refptr<PgInsert>(pg_session, database_name, schema_name, table_name);
  RETURN_NOT_OK(stmt->Prepare());
  PgInsert* insert = nullptr;
  stmt.swap(&insert);
  *handle = insert;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::InsertSetColumnInt2(PgStatement *handle, int attr_num,
                                              int16_t attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->SetColumnInt2(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::InsertSetColumnInt4(PgStatement *handle, int attr_num,
                                              int32_t attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  return down_cast<PgInsert*>(handle)->SetColumnInt4(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::InsertSetColumnInt8(PgStatement *handle, int attr_num,
                                              int64_t attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->SetColumnInt8(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::InsertSetColumnFloat4(PgStatement *handle, int attr_num,
                                                float attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->SetColumnFloat4(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::InsertSetColumnFloat8(PgStatement *handle, int attr_num,
                                                double attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->SetColumnFloat8(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::InsertSetColumnText(PgStatement *handle, int attr_num,
                                              const char *attr_value, int attr_bytes) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->SetColumnText(attr_num, attr_value, attr_bytes);
}

CHECKED_STATUS PgApiImpl::InsertSetColumnSerializedData(PgStatement *handle, int attr_num,
                                                        const char *attr_value, int attr_bytes) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->SetColumnSerializedData(attr_num, attr_value, attr_bytes);
}

CHECKED_STATUS PgApiImpl::ExecInsert(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_INSERT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgInsert *pg_stmt = down_cast<PgInsert*>(handle);
  return pg_stmt->Exec();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgApiImpl::AllocSelect(PgSession *pg_session,
                                      const char *database_name,
                                      const char *schema_name,
                                      const char *table_name,
                                      PgStatement **handle) {
  if (pg_session == nullptr) {
    return STATUS(InvalidArgument, "Invalid session handle");
  }

  if (database_name == nullptr) {
    database_name = pg_session->connected_dbname();
  }

  PgSelect::ScopedRefPtr stmt = make_scoped_refptr<PgSelect>(
      pg_session, database_name, schema_name, table_name);
  RETURN_NOT_OK(stmt->Prepare());

  PgSelect* select = nullptr;
  stmt.swap(&select);
  *handle = select;
  return Status::OK();
}

CHECKED_STATUS PgApiImpl::SelectSetColumnInt2(PgStatement *handle, int attr_num,
                                              int16_t attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnInt2(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::SelectSetColumnInt4(PgStatement *handle, int attr_num,
                                              int32_t attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnInt4(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::SelectSetColumnInt8(PgStatement *handle, int attr_num,
                                              int64_t attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnInt8(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::SelectSetColumnFloat4(PgStatement *handle, int attr_num,
                                                float attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnFloat4(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::SelectSetColumnFloat8(PgStatement *handle, int attr_num,
                                                double attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnFloat8(attr_num, attr_value);
}

CHECKED_STATUS PgApiImpl::SelectSetColumnText(PgStatement *handle, int attr_num,
                                              const char *attr_value, int attr_bytes) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnText(attr_num, attr_value, attr_bytes);
}

CHECKED_STATUS PgApiImpl::SelectSetColumnSerializedData(PgStatement *handle, int attr_num,
                                                        const char *attr_value, int attr_bytes) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->SetColumnSerializedData(attr_num, attr_value, attr_bytes);
}

Status PgApiImpl::SelectBindExprInt2(PgStatement *handle, int attr_num, int16_t *attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprInt2(attr_num, attr_value);
}

Status PgApiImpl::SelectBindExprInt4(PgStatement *handle, int attr_num, int32_t *attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprInt4(attr_num, attr_value);
}

Status PgApiImpl::SelectBindExprInt8(PgStatement *handle, int attr_num, int64_t *attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprInt8(attr_num, attr_value);
}

Status PgApiImpl::SelectBindExprFloat4(PgStatement *handle, int attr_num, float *attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprFloat4(attr_num, attr_value);
}

Status PgApiImpl::SelectBindExprFloat8(PgStatement *handle, int attr_num, double *attr_value) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprFloat8(attr_num, attr_value);
}

Status PgApiImpl::SelectBindExprText(PgStatement *handle, int attr_num, char *attr_value,
                                     int64_t *attr_bytes) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprText(attr_num, attr_value, attr_bytes);
}

Status PgApiImpl::SelectBindExprSerializedData(PgStatement *handle, int attr_num,
                                               char *attr_value, int64_t *attr_bytes) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->BindExprSerializedData(attr_num, attr_value, attr_bytes);
}

Status PgApiImpl::ExecSelect(PgStatement *handle) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->Exec();
}

Status PgApiImpl::SelectFetch(PgStatement *handle, int64 *row_count) {
  if (!PgStatement::IsValidStmt(handle, StmtOp::STMT_SELECT)) {
    // Invalid handle.
    return STATUS(InvalidArgument, "Invalid statement handle");
  }

  PgSelect *pg_stmt = down_cast<PgSelect*>(handle);
  return pg_stmt->Fetch(row_count);
}

} // namespace pggate
} // namespace yb
