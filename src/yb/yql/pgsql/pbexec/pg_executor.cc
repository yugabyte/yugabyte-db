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

#include "yb/yql/pgsql/pbexec/pg_executor.h"

#include "yb/util/logging.h"
#include "yb/util/decimal.h"

#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/yb_op.h"

namespace yb {
namespace pgsql {

using std::string;
using std::shared_ptr;
using strings::Substitute;

using client::YBColumnSpec;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableType;
using client::YBTableName;

using client::YBPgsqlWriteOp;
using client::YBPgsqlReadOp;

//--------------------------------------------------------------------------------------------------

PgExecutor::PgExecutor() {
}

PgExecutor::~PgExecutor() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgExecutor::Run(const PgExecContext::SharedPtr& exec_context) {
  // Prepare execution context and execute the parse tree's root node.
  exec_context_ = exec_context;
  const PgProto::SharedPtr& pg_proto = exec_context->pg_proto();

  // Call appropriate function to execute either a DDL treenode or DML protobuf.
  // TODO(neil) Split the code into three different classes and call virtual functions instead of
  // using these IF checks.
  if (pg_proto->ddl_stmt() != nullptr) {
    return ExecDdl(pg_proto->ddl_stmt());
  }

  if (pg_proto->write_op() != nullptr) {
    return ExecWriteOp(pg_proto->write_op());
  }

  if (pg_proto->read_op() != nullptr) {
    return ExecReadOp(pg_proto->read_op());
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// DDL Statement Execution.
//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgExecutor::ExecDdl(const TreeNode *tstmt) {
  // We get here if the execution for a specific DDL statement is not implemented yet.
  if (tstmt == nullptr) {
    return Status::OK();
  }

  switch (tstmt->opcode()) {
    case TreeNodeOpcode::kPgTCreateDatabase:
      return ExecDdlStmt(static_cast<const PgTCreateDatabase*>(tstmt));
    case TreeNodeOpcode::kPgTDropDatabase:
      return ExecDdlStmt(static_cast<const PgTDropDatabase*>(tstmt));
    case TreeNodeOpcode::kPgTCreateSchema:
      return ExecDdlStmt(static_cast<const PgTCreateSchema*>(tstmt));
    case TreeNodeOpcode::kPgTCreateTable:
      return ExecDdlStmt(static_cast<const PgTCreateTable*>(tstmt));
    case TreeNodeOpcode::kPgTDropStmt:
      return ExecDdlStmt(static_cast<const PgTDropStmt*>(tstmt));
    default:
      return ExecDdlStmt(tstmt);
  }
}

CHECKED_STATUS PgExecutor::ExecDdlStmt(const TreeNode *tstmt) {
  // We get here if the execution for a specific DDL statement is not implemented yet.
  return exec_context_->Error(tstmt, ErrorCode::FEATURE_NOT_SUPPORTED);
}

CHECKED_STATUS PgExecutor::ExecDdlStmt(const PgTCreateDatabase *tstmt) {
  Status s = exec_context_->CreateDatabase(tstmt->name()->QLName());
  if (s.ok()) {
    exec_context_->set_dbstatus("DATABASE CREATED");
    return Status::OK();
  }

  return exec_context_->Error(tstmt, s, s.IsAlreadyPresent() ? ErrorCode::DATABASE_ALREADY_EXISTS
                                                             : ErrorCode::SERVER_ERROR);
}

CHECKED_STATUS PgExecutor::ExecDdlStmt(const PgTDropDatabase *tstmt) {
  // Drop the database.
  Status s = exec_context_->DeleteDatabase(tstmt->name()->QLName());
  if (s.ok() || (s.IsNotFound() && tstmt->drop_if_exists())) {
    exec_context_->set_dbstatus("DATABASE DROPPED");
    return Status::OK();
  }
  return exec_context_->Error(tstmt->name(), s, s.IsNotFound() ? ErrorCode::DATABASE_NOT_FOUND
                                                               : ErrorCode::SERVER_ERROR);
}

CHECKED_STATUS PgExecutor::ExecDdlStmt(const PgTCreateSchema *tstmt) {
  Status s = exec_context_->CreateSchema(tstmt->name());
  if (s.ok() || (s.IsAlreadyPresent() && tstmt->create_if_not_exists())) {
    exec_context_->set_dbstatus("SCHEMA CREATED");
    return Status::OK();
  }

  return exec_context_->Error(tstmt, s, s.IsAlreadyPresent() ? ErrorCode::SCHEMA_ALREADY_EXISTS
                                                             : ErrorCode::SERVER_ERROR);
}

CHECKED_STATUS PgExecutor::ExecDdlStmt(const PgTCreateTable *tstmt) {
  YBTableName table_name = tstmt->yb_table_name();

  if (table_name.is_system() && client::FLAGS_yb_system_namespace_readonly) {
    return exec_context_->Error(tstmt->table_name(), ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  // Setting up columns.
  Status s;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PgTColumnDefinition *>& hash_columns = tstmt->hash_columns();
  for (const auto& column : hash_columns) {
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->HashPrimaryKey()
        ->Order(column->order());
  }
  const MCList<PgTColumnDefinition *>& primary_columns = tstmt->primary_columns();
  for (const auto& column : primary_columns) {
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->PrimaryKey()
        ->Order(column->order());
  }
  const MCList<PgTColumnDefinition *>& columns = tstmt->columns();
  for (const auto& column : columns) {
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->Nullable()
        ->Order(column->order());
  }

  s = b.Build(&schema);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tstmt->elements(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Create table.
  shared_ptr<YBTableCreator> table_creator(exec_context_->NewTableCreator());
  table_creator->table_name(table_name).table_type(YBTableType::PGSQL_TABLE_TYPE)
                                       .schema(&schema)
                                       .hash_schema(YBHashSchema::kPgsqlHash);

  s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      if (tstmt->create_if_not_exists()) {
        return Status::OK();
      }
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (s.IsNotFound()) {
      error_code = ErrorCode::SCHEMA_NOT_FOUND;
    } else if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }
    return exec_context_->Error(tstmt->table_name(), s, error_code);
  }

  exec_context_->set_dbstatus("TABLE CREATED");
  return Status::OK();
}

CHECKED_STATUS PgExecutor::ExecDdlStmt(const PgTDropStmt *tstmt) {
  Status s;
  switch (tstmt->drop_type()) {
    case OBJECT_TABLE: {
      // Drop the table.
      const YBTableName table_name = tstmt->yb_table_name();
      s = exec_context_->DeleteTable(table_name);
      if (s.ok() || (s.IsNotFound() && tstmt->drop_if_exists())) {
        exec_context_->set_dbstatus("TABLE DROPPED");
        return Status::OK();
      }
      return exec_context_->Error(tstmt->name(), s,
          s.IsNotFound() ? ErrorCode::TABLE_NOT_FOUND : ErrorCode::SERVER_ERROR);
    }

    case OBJECT_SCHEMA: {
      // Drop the schema.
      const string schema_name(tstmt->name()->last_name().c_str());
      s = exec_context_->DeleteSchema(schema_name);
      if (s.ok() || (s.IsNotFound() && tstmt->drop_if_exists())) {
        exec_context_->set_dbstatus("SCHEMA DROPPED");
        return Status::OK();
      }
      return exec_context_->Error(tstmt->name(), s,
          s.IsNotFound() ? ErrorCode::SCHEMA_NOT_FOUND : ErrorCode::SERVER_ERROR);
    }

    default:
      return exec_context_->Error(tstmt, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// DML Statements Execution.
//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgExecutor::ExecWriteOp(const shared_ptr<client::YBPgsqlWriteOp>& write_op) {
  Status s = exec_context_->Apply(write_op);
  if (PREDICT_FALSE(!s.ok())) {
    return s;
  }

  // TODO(neil) DocDB should return the number of inserted rows. We can implement this together
  // with UPDATE and DELETE.
  // Fow now, just return 1 row because the current system can only insert one row at a time.
  exec_context_->set_dbstatus("INSERT 0 1");
  return Status::OK();
}

CHECKED_STATUS PgExecutor::ExecReadOp(const shared_ptr<client::YBPgsqlReadOp>& read_op) {
  Status s = exec_context_->Apply(read_op);
  if (PREDICT_FALSE(!s.ok())) {
    return s;
  }

  exec_context_->set_dboutput(read_op->rows_data());
  exec_context_->set_dbstatus("SELECT");
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
