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
#include "yb/client/yb_op.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// Class PgSession
//--------------------------------------------------------------------------------------------------

PgSession::PgSession(std::shared_ptr<client::YBClient> client, const string& database_name)
    : client_(client), session_(client_->NewSession()) {
  session_->SetTimeout(kSessionTimeout);
}

PgSession::~PgSession() {
}

//--------------------------------------------------------------------------------------------------

void PgSession::Reset() {
  errmsg_.clear();
  status_ = Status::OK();
}

CHECKED_STATUS PgSession::SetFlushMode(YBSession::FlushMode m) {
  return session_->SetFlushMode(m);
}

CHECKED_STATUS PgSession::ConnectDatabase(const string& database_name) {
  Result<bool> namespace_exists = client_->NamespaceExists(database_name, YQL_DATABASE_PGSQL);
  if (namespace_exists.ok() && namespace_exists.get()) {
    connected_database_ = database_name;
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

#if (0)
CHECKED_STATUS PgSession::Apply(const std::shared_ptr<client::YBPgsqlOp>& op) {
  return session_->Apply(std::move(op));
}
#endif

}  // namespace pggate
}  // namespace yb
