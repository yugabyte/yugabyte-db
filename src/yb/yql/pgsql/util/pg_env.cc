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
//
// PgEnv represents the environment where SQL statements are being processed.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/util/pg_env.h"

#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/yb_op.h"

#include "yb/master/catalog_manager.h"
#include "yb/util/trace.h"

using namespace std::literals;  // NOLINT

namespace yb {
namespace pgsql {

using std::string;
using std::shared_ptr;
using std::weak_ptr;

using client::YBClient;
using client::YBError;
using client::YBOperation;
using client::YBStatusMemberCallback;
using client::YBTable;
using client::YBMetaDataCache;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableName;

//--------------------------------------------------------------------------------------------------

PgEnv::PgEnv(shared_ptr<YBClient> client) : client_(client) {
}

PgEnv::~PgEnv() {}

//--------------------------------------------------------------------------------------------------

Status PgEnv::CreateDatabase(const std::string& db_name) {
  return client_->CreateNamespace(db_name, YQL_DATABASE_PGSQL);
}

Status PgEnv::DeleteDatabase(const string& db_name) {
  return client_->DeleteNamespace(db_name, YQL_DATABASE_PGSQL);
}

Result<bool> PgEnv::ConnectDatabase(const string& db_name) {
  return client_->NamespaceExists(db_name, YQL_DATABASE_PGSQL);
}

//--------------------------------------------------------------------------------------------------

Status PgEnv::CreateSchema(const std::string& schema_name) {
  return client_->CreateNamespace(schema_name);
}

Status PgEnv::DeleteSchema(const string& schema_name) {
  return client_->DeleteNamespace(schema_name);
}

//--------------------------------------------------------------------------------------------------

YBTableCreator *PgEnv::NewTableCreator() {
  return client_->NewTableCreator();
}

shared_ptr<YBTable> PgEnv::GetTableDesc(const YBTableName& table_name) {
  // Hide tables in system_redis schema.
  if (table_name.is_redis_namespace()) {
    return nullptr;
  }

  shared_ptr<YBTable> yb_table;
  Status s = client_->OpenTable(table_name, &yb_table);
  if (!s.ok()) {
    VLOG(3) << "GetTableDesc: Server returns an error: " << s.ToString();
    return nullptr;
  }
  return yb_table;
}

CHECKED_STATUS PgEnv::DeleteTable(const YBTableName& name) {
  return client_->DeleteTable(name);
}

}  // namespace pgsql
}  // namespace yb
