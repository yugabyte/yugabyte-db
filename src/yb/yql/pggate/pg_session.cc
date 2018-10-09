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
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pggate_if_cxx_decl.h"

#include "yb/client/yb_op.h"
#include "yb/client/transaction.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using client::YBSchema;
using client::YBColumnSchema;
using client::YBTable;
using client::YBTableName;
using client::YBTableType;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(
    std::shared_ptr<client::YBClient> client,
    const string& database_name,
    scoped_refptr<PgTxnManager> pg_txn_manager)
    : client_(client),
      session_(client_->NewSession()),
      pg_txn_manager_(std::move(pg_txn_manager)) {
  session_->SetTimeout(kSessionTimeout);
}

PgSession::~PgSession() {
}

//--------------------------------------------------------------------------------------------------

void PgSession::Reset() {
  errmsg_.clear();
  status_ = Status::OK();
}

CHECKED_STATUS PgSession::ConnectDatabase(const string& database_name) {
  Result<bool> namespace_exists = client_->NamespaceExists(database_name, YQL_DATABASE_PGSQL);
  if (namespace_exists.ok() && namespace_exists.get()) {
    connected_database_ = database_name;
    return Status::OK();
  }

  // TODO remove this when integrating template1/initdb handling into YB.
  if (database_name == "template1") {
    return Status::OK();
  }

  return STATUS_FORMAT(NotFound, "Database '$0' does not exist", database_name);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgSession::CreateDatabase(const std::string& database_name) {
  return client_->CreateNamespace(database_name, YQL_DATABASE_PGSQL);
}

CHECKED_STATUS PgSession::DropDatabase(const string& database_name, bool if_exist) {
  return client_->DeleteNamespace(database_name, YQL_DATABASE_PGSQL);
}

//--------------------------------------------------------------------------------------------------

client::YBTableCreator *PgSession::NewTableCreator() {
  return client_->NewTableCreator();
}

CHECKED_STATUS PgSession::DropTable(const client::YBTableName& name) {
  return client_->DeleteTable(name);
}

shared_ptr<client::YBTable> PgSession::GetTableDesc(const client::YBTableName& table_name) {
  // Hide tables in system_redis schema.
  if (table_name.is_redis_namespace()) {
    return nullptr;
  }

  shared_ptr<client::YBTable> yb_table;
  Status s = client_->OpenTable(table_name, &yb_table);
  if (!s.ok()) {
    VLOG(3) << "GetTableDesc: Server returns an error: " << s.ToString();
    return nullptr;
  }
  return yb_table;
}

//--------------------------------------------------------------------------------------------------

Result<PgTableDesc::ScopedRefPtr> PgSession::LoadTable(const YBTableName& name,
                                                       const bool write_table) {
  VLOG(3) << "Loading table descriptor for " << name.ToString();
  shared_ptr<YBTable> table = GetTableDesc(name);
  if (table == nullptr) {
    return STATUS_FORMAT(NotFound, "Table $0 does not exist", name.ToString());
  }
  if (table->table_type() != YBTableType::PGSQL_TABLE_TYPE) {
    return STATUS(InvalidArgument, "Cannot access non-postgres table through the PostgreSQL API");
  }
  return make_scoped_refptr<PgTableDesc>(table);
}

CHECKED_STATUS PgSession::Apply(const std::shared_ptr<client::YBPgsqlOp>& op) {
  YBSession* session = GetSession(op->read_only());
  return session->ApplyAndFlush(op);
}

CHECKED_STATUS PgSession::ApplyAsync(const std::shared_ptr<client::YBPgsqlOp>& op) {
  return GetSession(op->read_only())->Apply(op);
}

void PgSession::FlushAsync(StatusFunctor callback) {
  // Even in case of read-write operations, Apply or ApplyAsync would have already been called
  // with that operation, and that would have started the YB transaction.
  GetSession(/* read_only_op */ true)->FlushAsync(callback);
}

YBSession* PgSession::GetSession(bool read_only_op) {
  YBSession* txn_session = pg_txn_manager_->GetTransactionalSession();
  if (txn_session) {
    VLOG(1) << __PRETTY_FUNCTION__
            << ": read_only_op=" << read_only_op << ", returning transactional session";
    if (!read_only_op) {
      pg_txn_manager_->BeginWriteTransactionIfNecessary();
    }
    return txn_session;
  }
  VLOG(1) << __PRETTY_FUNCTION__
          << ": read_only_op=" << read_only_op << ", returning non-transactional session";
  return session_.get();
}

int PgSession::CountPendingErrors() const {
  return session_->CountPendingErrors();
}

std::vector<std::unique_ptr<client::YBError>> PgSession::GetPendingErrors() {
  return session_->GetPendingErrors();
}

}  // namespace pggate
}  // namespace yb
