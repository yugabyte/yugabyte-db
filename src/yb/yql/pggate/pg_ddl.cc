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

#include "yb/yql/pggate/pg_ddl.h"
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
// PgCreateDatabase
//--------------------------------------------------------------------------------------------------

PgCreateDatabase::PgCreateDatabase(PgSession::SharedPtr pg_session, const char *database_name)
    : PgDdl(pg_session, StmtOp::STMT_CREATE_DATABASE),
      database_name_(database_name) {
}

PgCreateDatabase::~PgCreateDatabase() {
}

CHECKED_STATUS PgCreateDatabase::Exec() {
  return pg_session_->CreateDatabase(database_name_);
}

PgDropDatabase::PgDropDatabase(PgSession::SharedPtr pg_session,
                               const char *database_name,
                               bool if_exist)
    : PgDdl(pg_session, StmtOp::STMT_DROP_DATABASE),
      database_name_(database_name),
      if_exist_(if_exist) {
}

PgDropDatabase::~PgDropDatabase() {
}

CHECKED_STATUS PgDropDatabase::Exec() {
  return pg_session_->DropDatabase(database_name_, if_exist_);
}

//--------------------------------------------------------------------------------------------------
// PgCreateSchema
//--------------------------------------------------------------------------------------------------

PgCreateSchema::PgCreateSchema(PgSession::SharedPtr pg_session,
                               const char *database_name,
                               const char *schema_name,
                               bool if_not_exist)
    : PgDdl(pg_session, StmtOp::STMT_CREATE_SCHEMA),
      database_name_(database_name),
      schema_name_(schema_name),
      if_not_exist_(if_not_exist) {
}

PgCreateSchema::~PgCreateSchema() {
}

CHECKED_STATUS PgCreateSchema::Exec() {
  LOG(FATAL) << "Create schema (" << database_name_ << "," << schema_name_ << "," << if_not_exist_
             << ") is underdevelopment";
  return STATUS(NotSupported, "SCHEMA is not yet implemented");
}

PgDropSchema::PgDropSchema(PgSession::SharedPtr pg_session,
                           const char *database_name,
                           const char *schema_name,
                           bool if_exist)
    : PgDdl(pg_session, StmtOp::STMT_CREATE_SCHEMA),
      database_name_(database_name),
      schema_name_(schema_name),
      if_exist_(if_exist) {
}

PgDropSchema::~PgDropSchema() {
}

CHECKED_STATUS PgDropSchema::Exec() {
  LOG(FATAL) << "Drop schema " << database_name_ << "." << schema_name_ << "," << if_exist_
             << ") is underdevelopment";
  return STATUS(NotSupported, "SCHEMA is not yet implemented");
}

//--------------------------------------------------------------------------------------------------
// PgCreateTable
//--------------------------------------------------------------------------------------------------

PgCreateTable::PgCreateTable(PgSession::SharedPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *table_name,
                             bool if_not_exist)
    : PgDdl(pg_session, StmtOp::STMT_CREATE_TABLE),
      table_name_(database_name, table_name),
      if_not_exist_(if_not_exist) {
}

PgCreateTable::~PgCreateTable() {
}

CHECKED_STATUS PgCreateTable::AddColumn(const char *attr_name, int attr_num, int attr_ybtype,
                                        bool is_hash, bool is_range) {
  shared_ptr<QLType> yb_type = QLType::Create(static_cast<DataType>(attr_ybtype));
  if (is_hash) {
    schema_builder_.AddColumn(attr_name)->Type(yb_type)->Order(attr_num)->HashPrimaryKey();
  } else if (is_range) {
    schema_builder_.AddColumn(attr_name)->Type(yb_type)->Order(attr_num)->PrimaryKey();
  } else {
    schema_builder_.AddColumn(attr_name)->Type(yb_type)->Order(attr_num);
  }
  return Status::OK();
}

CHECKED_STATUS PgCreateTable::Exec() {
  // Construct schema.
  client::YBSchema schema;
  RETURN_NOT_OK(schema_builder_.Build(&schema));

  // Create table.
  shared_ptr<client::YBTableCreator> table_creator(pg_session_->NewTableCreator());
  table_creator->table_name(table_name_).table_type(client::YBTableType::PGSQL_TABLE_TYPE)
                                        .schema(&schema)
                                        .hash_schema(YBHashSchema::kPgsqlHash);

  Status s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    const char *errmsg = "Server error";
    if (s.IsAlreadyPresent()) {
      if (if_not_exist_) {
        return Status::OK();
      }
      errmsg = "Duplicate table";
    } else if (s.IsNotFound()) {
      errmsg = "Schema not found";
    } else if (s.IsInvalidArgument()) {
      errmsg = "Invalid table definition";
    }
    return STATUS(InvalidArgument, errmsg);
  }

  return Status::OK();
}

PgDropTable::PgDropTable(PgSession::SharedPtr pg_session,
                         const char *database_name,
                         const char *schema_name,
                         const char *table_name,
                         bool if_exist)
    : PgDdl(pg_session, StmtOp::STMT_DROP_TABLE),
      table_name_(database_name, table_name),
      if_exist_(if_exist) {
}

PgDropTable::~PgDropTable() {
}

CHECKED_STATUS PgDropTable::Exec() {
  Status s = pg_session_->DropTable(table_name_);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    return Status::OK();
  }
  return s;
}

}  // namespace pggate
}  // namespace yb
