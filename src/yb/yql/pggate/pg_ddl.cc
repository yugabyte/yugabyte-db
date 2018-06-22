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
    : PgStatement(pg_session, StmtOp::STMT_CREATE_DATABASE),
      database_name_(database_name) {
}

PgCreateDatabase::~PgCreateDatabase() {
}

YBCPgError PgCreateDatabase::Exec() {
  status_ = pg_session_->CreateDatabase(database_name_);
  return status_.ok() ? YBCPgError::YBC_PGERROR_SUCCESS : YBCPgError::YBC_PGERROR_FAILURE;
}

PgDropDatabase::PgDropDatabase(PgSession::SharedPtr pg_session,
                               const char *database_name,
                               bool if_exist)
    : PgStatement(pg_session, StmtOp::STMT_DROP_DATABASE),
      database_name_(database_name),
      if_exist_(if_exist) {
}

PgDropDatabase::~PgDropDatabase() {
}

YBCPgError PgDropDatabase::Exec() {
  status_ = pg_session_->DropDatabase(database_name_, if_exist_);
  return status_.ok() ? YBCPgError::YBC_PGERROR_SUCCESS : YBCPgError::YBC_PGERROR_FAILURE;
}

//--------------------------------------------------------------------------------------------------
// PgCreateSchema
//--------------------------------------------------------------------------------------------------

PgCreateSchema::PgCreateSchema(PgSession::SharedPtr pg_session,
                               const char *database_name,
                               const char *schema_name,
                               bool if_not_exist)
    : PgStatement(pg_session, StmtOp::STMT_CREATE_SCHEMA),
      database_name_(database_name),
      schema_name_(schema_name),
      if_not_exist_(if_not_exist) {
}

PgCreateSchema::~PgCreateSchema() {
}

YBCPgError PgCreateSchema::Exec() {
  LOG(FATAL) << "Create schema (" << database_name_ << "," << schema_name_ << "," << if_not_exist_
             << ") is underdevelopment";
  return status_.ok() ? YBCPgError::YBC_PGERROR_SUCCESS : YBCPgError::YBC_PGERROR_FAILURE;
}

PgDropSchema::PgDropSchema(PgSession::SharedPtr pg_session,
                           const char *database_name,
                           const char *schema_name,
                           bool if_exist)
    : PgStatement(pg_session, StmtOp::STMT_CREATE_SCHEMA),
      database_name_(database_name),
      schema_name_(schema_name),
      if_exist_(if_exist) {
}

PgDropSchema::~PgDropSchema() {
}

YBCPgError PgDropSchema::Exec() {
  LOG(FATAL) << "Drop schema " << database_name_ << "." << schema_name_ << "," << if_exist_
             << ") is underdevelopment";
  return status_.ok() ? YBCPgError::YBC_PGERROR_SUCCESS : YBCPgError::YBC_PGERROR_FAILURE;
}

//--------------------------------------------------------------------------------------------------
// PgCreateTable
//--------------------------------------------------------------------------------------------------

PgCreateTable::PgCreateTable(PgSession::SharedPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *table_name,
                             bool if_not_exist)
    : PgStatement(pg_session, StmtOp::STMT_CREATE_TABLE),
      table_name_(database_name, table_name),
      if_not_exist_(if_not_exist) {
}

PgCreateTable::~PgCreateTable() {
}

YBCPgError PgCreateTable::AddColumn(const char *col_name, int col_order, int col_type,
                                    bool is_hash, bool is_range) {
  shared_ptr<QLType> yb_type = QLType::Create(static_cast<DataType>(col_type));
  if (is_hash) {
    schema_builder_.AddColumn(col_name)->Type(yb_type)->Order(col_order)->HashPrimaryKey();
  } else if (is_range) {
    schema_builder_.AddColumn(col_name)->Type(yb_type)->Order(col_order)->PrimaryKey();
  } else {
    schema_builder_.AddColumn(col_name)->Type(yb_type)->Order(col_order);
  }
  return YBCPgError::YBC_PGERROR_SUCCESS;
}

YBCPgError PgCreateTable::Exec() {
  // Construct schema.
  client::YBSchema schema;
  status_ = schema_builder_.Build(&schema);
  if (!status_.ok()) {
    return YBCPgError::YBC_PGERROR_FAILURE;
  }

  // Create table.
  shared_ptr<client::YBTableCreator> table_creator(pg_session_->NewTableCreator());
  table_creator->table_name(table_name_).table_type(client::YBTableType::PGSQL_TABLE_TYPE)
                                        .schema(&schema)
                                        .hash_schema(YBHashSchema::kPgsqlHash);

  // TODO(neil) Check for status and if_not_exists flag.
  LOG(DFATAL) << "MUST CHECK FOR if_not_exist_ " << if_not_exist_;

  status_ = table_creator->Create();
  return status_.ok() ? YBCPgError::YBC_PGERROR_SUCCESS : YBCPgError::YBC_PGERROR_FAILURE;
}

PgDropTable::PgDropTable(PgSession::SharedPtr pg_session,
                         const char *database_name,
                         const char *schema_name,
                         const char *table_name,
                         bool if_exist)
    : PgStatement(pg_session, StmtOp::STMT_CREATE_TABLE),
      table_name_(database_name, table_name),
      if_exist_(if_exist) {
}

PgDropTable::~PgDropTable() {
}

YBCPgError PgDropTable::Exec() {
  status_ = pg_session_->DropTable(table_name_);
  if (status_.ok() || (status_.IsNotFound() && if_exist_)) {
    return YBCPgError::YBC_PGERROR_SUCCESS;
  }
  return YBCPgError::YBC_PGERROR_FAILURE;
}

}  // namespace pggate
}  // namespace yb
