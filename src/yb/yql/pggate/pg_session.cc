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

Status PgSession::LoadTable(
    const YBTableName& name,
    const bool write_table,
    shared_ptr<YBTable> *table,
    vector<ColumnDesc> *col_descs,
    int *num_key_columns,
    int *num_partition_columns) {

  *table = nullptr;
  shared_ptr<YBTable> pg_table;

  VLOG(3) << "Loading table descriptor for " << name.ToString();
  pg_table = GetTableDesc(name);
  if (pg_table == nullptr) {
    return STATUS_FORMAT(NotFound, "Table $0 does not exist", name.ToString());
  }
  if (pg_table->table_type() != YBTableType::PGSQL_TABLE_TYPE) {
    return STATUS(InvalidArgument, "Cannot access non-postgres table");
  }

  const YBSchema& schema = pg_table->schema();
  const int num_columns = schema.num_columns();
  if (num_key_columns != nullptr) {
    *num_key_columns = schema.num_key_columns();
  }
  if (num_partition_columns != nullptr) {
    *num_partition_columns = schema.num_hash_key_columns();
  }

  if (col_descs != nullptr) {
    col_descs->resize(num_columns);
    for (int idx = 0; idx < num_columns; idx++) {
      // Find the column descriptor.
      const YBColumnSchema col = schema.Column(idx);
      // TODO(neil) It would make a lot more sense if we index by attr_num instead of ID.
      (*col_descs)[idx].Init(idx,
                             schema.ColumnId(idx),
                             col.name(),
                             idx < *num_partition_columns,
                             idx < *num_key_columns,
                             col.order() /* attr_num */,
                             col.type(),
                             YBColumnSchema::ToInternalDataType(col.type()));
    }
  }

  *table = pg_table;
  return Status::OK();
}

CHECKED_STATUS PgSession::Apply(const std::shared_ptr<client::YBPgsqlOp>& op) {
  return session_->ApplyAndFlush(op);
}

}  // namespace pggate
}  // namespace yb
