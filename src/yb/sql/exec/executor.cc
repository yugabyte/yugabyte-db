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

#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/common/partition.h"
#include "yb/sql/sql_processor.h"
#include "yb/util/decimal.h"
#include "yb/util/yb_partition.h"

namespace yb {
namespace sql {

using std::string;
using std::shared_ptr;

using client::YBColumnSpec;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableType;
using client::YBTableName;
using client::YBqlWriteOp;
using client::YBqlReadOp;
using strings::Substitute;

//--------------------------------------------------------------------------------------------------

Executor::Executor(SqlEnv *sql_env, const SqlMetrics* sql_metrics)
    : sql_env_(sql_env),
      sql_metrics_(sql_metrics),
      flush_async_cb_(Bind(&Executor::FlushAsyncDone, Unretained(this))) {
}

Executor::~Executor() {
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecuteAsync(const string &sql_stmt, const ParseTree &parse_tree,
                            const StatementParameters* params, StatementExecutedCallback cb) {
  DCHECK(cb_.is_null()) << "Another execution is in progress.";
  cb_ = std::move(cb);
  sql_env_->Reset();
  // Execute the statement and invoke statement-executed callback either when there is an error or
  // no async operation is pending.
  const Status s = Execute(sql_stmt, parse_tree, params);
  if (PREDICT_FALSE(!s.ok())) {
    return StatementExecuted(s);
  }
  if (!sql_env_->FlushAsync(&flush_async_cb_)) {
    return StatementExecuted(Status::OK());
  }
}

//--------------------------------------------------------------------------------------------------

void Executor::BeginBatch(StatementExecutedCallback cb) {
  DCHECK(cb_.is_null()) << "Another execution is in progress.";
  cb_ = std::move(cb);
  sql_env_->Reset();
}

void Executor::ExecuteBatch(const std::string &sql_stmt, const ParseTree &parse_tree,
                            const StatementParameters* params) {
  Status s;

  // Batch execution is supported for non-conditional DML statements only currently.
  const TreeNode* tnode = parse_tree.root().get();
  if (tnode != nullptr) {
    switch (tnode->opcode()) {
      case TreeNodeOpcode::kPTInsertStmt: FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTUpdateStmt: FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTDeleteStmt: {
        const PTExpr::SharedPtr& if_clause = static_cast<const PTDmlStmt*>(tnode)->if_clause();
        if (if_clause != nullptr) {
          s = ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                          "batch execution of conditional DML statement not supported yet");
        }
        break;
      }
      default:
        s = ErrorStatus(ErrorCode::CQL_STATEMENT_INVALID,
                        "batch execution of non-DML statement not supported yet");
        break;
    }
  }

  // Execute the statement and invoke statement-executed callback when there is an error.
  if (s.ok()) {
    s = Execute(sql_stmt, parse_tree, params);
  }
  if (PREDICT_FALSE(!s.ok())) {
    return StatementExecuted(s);
  }
}

void Executor::ApplyBatch() {
  // Invoke statement-executed callback when no async operation is pending.
  if (!sql_env_->FlushAsync(&flush_async_cb_)) {
    return StatementExecuted(Status::OK());
  }
}

void Executor::AbortBatch() {
  sql_env_->AbortOps();
  Reset();
}

//--------------------------------------------------------------------------------------------------

Status Executor::Execute(const string &sql_stmt, const ParseTree &parse_tree,
                         const StatementParameters* params) {
  // Prepare execution context and execute the parse tree's root node.
  exec_contexts_.emplace_back(sql_stmt.c_str(), sql_stmt.length(), &parse_tree, params, sql_env_);
  exec_context_ = &exec_contexts_.back();
  return ProcessStatementStatus(parse_tree, ExecTreeNode(exec_context_->tnode()));
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecTreeNode(const TreeNode *tnode) {
  if (tnode == nullptr) {
    return Status::OK();
  }
  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTCreateTable:
      return ExecPTNode(static_cast<const PTCreateTable *>(tnode));

    case TreeNodeOpcode::kPTAlterTable:
      return ExecPTNode(static_cast<const PTAlterTable *>(tnode));

    case TreeNodeOpcode::kPTCreateType:
      return ExecPTNode(static_cast<const PTCreateType *>(tnode));

    case TreeNodeOpcode::kPTDropStmt:
      return ExecPTNode(static_cast<const PTDropStmt *>(tnode));

    case TreeNodeOpcode::kPTSelectStmt:
      return ExecPTNode(static_cast<const PTSelectStmt *>(tnode));

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNode(static_cast<const PTInsertStmt *>(tnode));

    case TreeNodeOpcode::kPTDeleteStmt:
      return ExecPTNode(static_cast<const PTDeleteStmt *>(tnode));

    case TreeNodeOpcode::kPTUpdateStmt:
      return ExecPTNode(static_cast<const PTUpdateStmt *>(tnode));

    case TreeNodeOpcode::kPTCreateKeyspace:
      return ExecPTNode(static_cast<const PTCreateKeyspace *>(tnode));

    case TreeNodeOpcode::kPTUseKeyspace:
      return ExecPTNode(static_cast<const PTUseKeyspace *>(tnode));

    default:
      return exec_context_->Error(ErrorCode::FEATURE_NOT_SUPPORTED);
  }
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateType *tnode) {
  YBTableName yb_name = tnode->yb_type_name();

  const std::string& type_name = yb_name.table_name();
  std::string keyspace_name = yb_name.namespace_name();
  if (!yb_name.has_namespace()) {
    if (exec_context_->CurrentKeyspace().empty()) {
      return exec_context_->Error(tnode->type_name(), ErrorCode::NO_NAMESPACE_USED);
    }
    keyspace_name = exec_context_->CurrentKeyspace();
  }

  std::vector<std::string> field_names;
  std::vector<std::shared_ptr<YQLType>> field_types;

  for (const PTTypeField::SharedPtr field : tnode->fields()->node_list()) {
    field_names.emplace_back(field->yb_name());
    field_types.push_back(field->yql_type());
  }

  Status s = exec_context_->CreateUDType(keyspace_name, type_name, field_names, field_types);
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TYPE;
    } else if (s.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TYPE) {
      return Status::OK();
    }

    return exec_context_->Error(tnode->type_name(), s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>("CREATED", "TYPE", keyspace_name, type_name);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateTable *tnode) {
  YBTableName table_name = tnode->yb_table_name();

  if (!table_name.has_namespace()) {
    if (exec_context_->CurrentKeyspace().empty()) {
      return exec_context_->Error(tnode->table_name(), ErrorCode::NO_NAMESPACE_USED);
    }

    table_name.set_namespace_name(exec_context_->CurrentKeyspace());
  }

  if (table_name.is_system() && client::FLAGS_yb_system_namespace_readonly) {
    return exec_context_->Error(tnode->table_name(), ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  // Setting up columns.
  Status s;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (const auto& column : hash_columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
    }
    b.AddColumn(column->yb_name())->Type(column->yql_type())
        ->HashPrimaryKey()
        ->Order(column->order());
  }
  const MCList<PTColumnDefinition *>& primary_columns = tnode->primary_columns();
  for (const auto& column : primary_columns) {
    b.AddColumn(column->yb_name())->Type(column->yql_type())
        ->PrimaryKey()
        ->Order(column->order())
        ->SetSortingType(column->sorting_type());
  }
  const MCList<PTColumnDefinition *>& columns = tnode->columns();
  for (const auto& column : columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
    }
    YBColumnSpec *column_spec = b.AddColumn(column->yb_name())->Type(column->yql_type())
                                                              ->Nullable()
                                                              ->Order(column->order());
    if (column->is_static()) {
      column_spec->StaticColumn();
    }
    if (column->is_counter()) {
      column_spec->Counter();
    }
  }

  TableProperties table_properties;
  if (!tnode->ToTableProperties(&table_properties).ok()) {
    return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }

  b.SetTableProperties(table_properties);

  s = b.Build(&schema);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Create table.
  shared_ptr<YBTableCreator> table_creator(exec_context_->NewTableCreator());
  s = table_creator->table_name(table_name)
                    .table_type(YBTableType::YQL_TABLE_TYPE)
                    .schema(&schema)
                    .Create();
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (s.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    } else if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TABLE) {
      return Status::OK();
    }

    return exec_context_->Error(tnode->table_name(), s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>(
      "CREATED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTAlterTable *tnode) {
  YBTableName table_name = tnode->yb_table_name();

  if (!table_name.has_namespace()) {
    if (exec_context_->CurrentKeyspace().empty()) {
      return exec_context_->Error(ErrorCode::NO_NAMESPACE_USED);
    }

    table_name.set_namespace_name(exec_context_->CurrentKeyspace());
  }

  shared_ptr<YBTableAlterer> table_alterer(exec_context_->NewTableAlterer(table_name));

  for (const auto& mod_column : tnode->mod_columns()) {
    switch (mod_column->mod_type()) {
      case ALTER_ADD:
        table_alterer->AddColumn(mod_column->new_name()->data())
                     ->Type(mod_column->yql_type());
        break;
      case ALTER_DROP:
        table_alterer->DropColumn(mod_column->old_name()->last_name().data());
        break;
      case ALTER_RENAME:
        table_alterer->AlterColumn(mod_column->old_name()->last_name().data())
                     ->RenameTo(mod_column->new_name()->c_str());
        break;
      case ALTER_TYPE:
        // Not yet supported by AlterTableRequestPB.
        return exec_context_->Error(ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
    }
  }

  if (!tnode->mod_props().empty()) {
    TableProperties table_properties;
    Status s = tnode->ToTableProperties(&table_properties);
    if(PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }

    table_alterer->SetTableProperties(table_properties);
  }

  Status s = table_alterer->Alter();
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::EXEC_ERROR);
  }

  result_ = std::make_shared<SchemaChangeResult>(
      "UPDATED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTDropStmt *tnode) {
  Status s;
  ErrorCode error_not_found = ErrorCode::SERVER_ERROR;

  switch (tnode->drop_type()) {
    case OBJECT_TABLE: {
      YBTableName table_name = tnode->yb_table_name();

      if (!table_name.has_namespace()) {
        if (exec_context_->CurrentKeyspace().empty()) {
          return exec_context_->Error(tnode->name(), ErrorCode::NO_NAMESPACE_USED);
        }

        table_name.set_namespace_name(exec_context_->CurrentKeyspace());
      }
      // Drop the table.
      s = exec_context_->DeleteTable(table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>(
          "DROPPED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name());
      break;
    }

    case OBJECT_SCHEMA: {
      // Drop the keyspace.
      const string &keyspace_name(tnode->name()->last_name().c_str());
      s = exec_context_->DeleteKeyspace(keyspace_name);
      error_not_found = ErrorCode::KEYSPACE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>("DROPPED", "KEYSPACE", keyspace_name);
      break;
    }

    case OBJECT_TYPE: {
      const string& type_name(tnode->name()->last_name().c_str());
      string namespace_name(tnode->name()->first_name().c_str());

      // If name has no explicit keyspace use default
      if (tnode->name()->IsSimpleName()) {
        if (exec_context_->CurrentKeyspace().empty()) {
          return exec_context_->Error(tnode->name(), ErrorCode::NO_NAMESPACE_USED);
        }
        namespace_name = exec_context_->CurrentKeyspace();
      }

      // Drop the type.
      s = exec_context_->DeleteUDType(namespace_name, type_name);
      error_not_found = ErrorCode::TYPE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>("DROPPED", "TYPE", namespace_name, type_name);
      break;
    }

    default:
      return exec_context_->Error(ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsNotFound()) {
      // Ignore not found error for a DROP IF EXISTS statement.
      if (tnode->drop_if_exists()) {
        return Status::OK();
      }

      error_code = error_not_found;
    }

    return exec_context_->Error(tnode->name(), s, error_code);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTSelectStmt *tnode) {
  const shared_ptr<client::YBTable>& table = tnode->table();

  if (table == nullptr) {
    // If this is a system table but the table does not exist, it is okay. Just return OK with void
    // result.
    return tnode->is_system() ? Status::OK() : exec_context_->Error(ErrorCode::TABLE_NOT_FOUND);
  }

  // If there are rows buffered in current result, use the paging state in it. Otherwise, use the
  // paging state from the client.
  const StatementParameters *paging_params = exec_context_->params();
  StatementParameters current_params;
  RowsResult::SharedPtr current_result = std::static_pointer_cast<RowsResult>(result_);
  if (current_result != nullptr) {
    RETURN_NOT_OK(current_params.set_paging_state(current_result->paging_state()));
    paging_params = &current_params;
  }

  // If there is a table id in the statement parameter's paging state, this is a continuation of
  // a prior SELECT statement. Verify that the same table still exists.
  const bool continue_select = !paging_params->table_id().empty();
  if (continue_select && paging_params->table_id() != table->id()) {
    return exec_context_->Error("Table no longer exists.", ErrorCode::TABLE_NOT_FOUND);
  }

  // See if there is any rows buffered in current result locally.
  size_t current_row_count = 0;
  if (current_result != nullptr) {
    RETURN_NOT_OK(YQLRowBlock::GetRowCount(current_result->client(),
                                           current_result->rows_data(),
                                           &current_row_count));
  }

  // If the current result hits the request page size already, return the result.
  if (current_row_count >= exec_context_->params()->page_size()) {
    return Status::OK();
  }

  // Create the read request.
  shared_ptr<YBqlReadOp> select_op(table->NewYQLSelect());
  YQLReadRequestPB *req = select_op->mutable_request();
  // Where clause - Hash, range, and regular columns.
  YBPartialRow *row = select_op->mutable_row();

  // Specify selected columns.
  for (const ColumnDesc *col_desc : tnode->selected_columns()) {
    req->add_column_ids(col_desc->id());
  }

  bool no_results = false;
  Status s = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops(),
      tnode->subscripted_col_where_ops(), tnode->partition_key_ops(), tnode->func_ops(),
      &no_results);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // If where clause restrictions guarantee no rows could match, return empty result immediately.
  if (no_results) {
    YQLRowBlock empty_row_block(tnode->table()->InternalSchema(), {});
    faststring buffer;
    empty_row_block.Serialize(select_op->request().client(), &buffer);
    *select_op->mutable_rows_data() = buffer.ToString();
    result_ = std::make_shared<RowsResult>(select_op.get());
    return Status::OK();
  }

  if (req->has_max_hash_code()) {
    uint16_t next_hash_code = paging_params->next_partition_key().empty() ? 0
        : PartitionSchema::DecodeMultiColumnHashValue(paging_params->next_partition_key());

    // if reached max_hash_code stop and return the current result
    if (next_hash_code >= req->max_hash_code()) {
      current_result->clear_paging_state();
      return Status::OK();
    }
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Specify distinct columns or non.
  req->set_distinct(tnode->distinct());

  // Default row count limit is the page size less the rows buffered in current result locally.
  // And we should return paging state when page size limit is hit.
  req->set_limit(exec_context_->params()->page_size() - current_row_count);
  req->set_return_paging_state(true);

  // Check if there is a limit and compute the new limit based on the number of returned rows.
  if (tnode->has_limit()) {
    YQLExpressionPB limit_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->limit(), &limit_pb));
    if (limit_pb.has_value() && YQLValue::IsNull(limit_pb.value())) {
      return exec_context_->Error("LIMIT value cannot be null.", ErrorCode::INVALID_ARGUMENTS);
    }

    // this should be ensured by checks before getting here
    DCHECK(limit_pb.has_value() && limit_pb.value().has_int64_value())
        << "Integer constant expected for LIMIT clause";

    int64_t limit = limit_pb.value().int64_value();
    if (limit < 0) {
      return exec_context_->Error("LIMIT value cannot be negative.", ErrorCode::INVALID_ARGUMENTS);
    }
    if (limit == 0 || paging_params->total_num_rows_read() >= limit) {
      return Status::OK();
    }

    // If the LIMIT clause, subtracting the number of rows we have returned so far, is lower than
    // the page size limit set from above, set the lower limit and do not return paging state when
    // this limit is hit.
    limit -= paging_params->total_num_rows_read();
    if (limit < req->limit()) {
      req->set_limit(limit);
      req->set_return_paging_state(false);
    }
  }

  // If this is a continuation of a prior read, set the next partition key, row key and total number
  // of rows read in the request's paging state.
  if (continue_select) {
    YQLPagingStatePB *paging_state = req->mutable_paging_state();
    paging_state->set_next_partition_key(paging_params->next_partition_key());
    paging_state->set_next_row_key(paging_params->next_row_key());
    paging_state->set_total_num_rows_read(paging_params->total_num_rows_read());
  }

  // Set the correct consistency level for the operation.
  if (tnode->is_system()) {
    // Always use strong consistency for system tables.
    select_op->set_yb_consistency_level(YBConsistencyLevel::STRONG);
  } else {
    select_op->set_yb_consistency_level(exec_context_->params()->yb_consistency_level());
  }

  // Apply the operator.
  return exec_context_->ApplyRead(select_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTInsertStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> insert_op(table->NewYQLInsert());
  YQLWriteRequestPB *req = insert_op->mutable_request();

  // Set the ttl
  RETURN_NOT_OK(TtlToPB(tnode, insert_op->mutable_request()));

  // Set the values for columns.
  Status s = ColumnArgsToPB(table, tnode, req, insert_op->mutable_row());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), insert_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operator.
  return exec_context_->ApplyWrite(insert_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTDeleteStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> delete_op(table->NewYQLDelete());
  YQLWriteRequestPB *req = delete_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  YBPartialRow *row = delete_op->mutable_row();
  Status s = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops(),
                             tnode->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }
  s = ColumnArgsToPB(table, tnode, req, delete_op->mutable_row());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), delete_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operator.
  return exec_context_->ApplyWrite(delete_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTUpdateStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> update_op(table->NewYQLUpdate());
  YQLWriteRequestPB *req = update_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  YBPartialRow *row = update_op->mutable_row();
  Status s = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops(),
      tnode->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the ttl
  RETURN_NOT_OK(TtlToPB(tnode, update_op->mutable_request()));

  // Setup the columns' new values.
  s = ColumnArgsToPB(table, tnode, update_op->mutable_request(), update_op->mutable_row());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), update_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operator.
  return exec_context_->ApplyWrite(update_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateKeyspace *tnode) {
  Status s = exec_context_->CreateKeyspace(tnode->name());

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsAlreadyPresent()) {
      if (tnode->create_if_not_exists()) {
        // Case: CREATE KEYSPACE IF NOT EXISTS name;
        return Status::OK();
      }

      error_code = ErrorCode::KEYSPACE_ALREADY_EXISTS;
    }

    return exec_context_->Error(s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>("CREATED", "KEYSPACE", tnode->name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTUseKeyspace *tnode) {
  const Status s = exec_context_->UseKeyspace(tnode->name());
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = s.IsNotFound() ? ErrorCode::KEYSPACE_NOT_FOUND : ErrorCode::SERVER_ERROR;
    return exec_context_->Error(s, error_code);
  }

  result_ = std::make_shared<SetKeyspaceResult>(tnode->name());
  return Status::OK();
}

void Executor::FlushAsyncDone(const Status &s) {
  Status ss = s;
  if (ss.ok()) {
    ss = ProcessAsyncResults();
    if (ss.ok() && exec_context_->tnode()->opcode() == TreeNodeOpcode::kPTSelectStmt) {
      // If there is a paging state, try fetching more rows and buffer locally. ExecPTNode()
      // will ensure we do not exceed the page size.
      if (result_ != nullptr && !static_cast<const RowsResult&>(*result_).paging_state().empty()) {
        sql_env_->Reset();
        ss = ExecTreeNode(static_cast<const PTSelectStmt*>(exec_context_->tnode()));
        if (ss.ok()) {
          if (sql_env_->FlushAsync(&flush_async_cb_)) {
            return;
          }
        }
      }
    }
  }
  StatementExecuted(ss);
}

Status Executor::ProcessStatementStatus(const ParseTree &parse_tree, const Status& s) {
  if (PREDICT_FALSE(!s.ok() && s.IsSqlError() && !parse_tree.reparsed())) {
    // If execution fails because the statement was analyzed with stale metadata cache, the
    // statement needs to be reparsed and re-analyzed. Symptoms of stale metadata are as listed
    // below. Expand the list in future as new cases arise.
    // - TABLET_NOT_FOUND when the tserver fails to execute the YBSqlOp because the tablet is not
    //   found (ENG-945).
    // - WRONG_METADATA_VERSION when the schema version the tablet holds is different from the one
    //   used by the semantic analyzer.
    // - INVALID_TABLE_DEFINITION when a referenced user-defined type is not found.
    // - INVALID_ARGUMENTS when the column datatype is inconsistent with the supplied value in an
    //   INSERT or UPDATE statement.
    const ErrorCode errcode = GetErrorCode(s);
    if (errcode == ErrorCode::TABLET_NOT_FOUND ||
        errcode == ErrorCode::WRONG_METADATA_VERSION ||
        errcode == ErrorCode::INVALID_TABLE_DEFINITION ||
        errcode == ErrorCode::INVALID_ARGUMENTS) {
      parse_tree.ClearAnalyzedTableCache(sql_env_);
      parse_tree.ClearAnalyzedUDTypeCache(sql_env_);
      parse_tree.set_stale();
      return ErrorStatus(ErrorCode::STALE_METADATA);
    }
  }
  return s;
}

Status Executor::ProcessOpResponse(client::YBqlOp* op, ExecContext* exec_context) {
  const YQLResponsePB &resp = op->response();
  CHECK(resp.has_status()) << "YQLResponsePB status missing";
  switch (resp.status()) {
    case YQLResponsePB::YQL_STATUS_OK:
      // Read the rows result if present.
      return op->rows_data().empty() ?
          Status::OK() : AppendResult(std::make_shared<RowsResult>(op));
    case YQLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH:
      return exec_context->Error(resp.error_message().c_str(), ErrorCode::WRONG_METADATA_VERSION);
    case YQLResponsePB::YQL_STATUS_RUNTIME_ERROR:
      return exec_context->Error(resp.error_message().c_str(), ErrorCode::SERVER_ERROR);
    case YQLResponsePB::YQL_STATUS_USAGE_ERROR:
      return exec_context->Error(resp.error_message().c_str(), ErrorCode::EXEC_ERROR);
    // default: fall-through to below
  }
  LOG(FATAL) << "Unknown status: " << resp.DebugString();
}

Status Executor::ProcessAsyncResults() {
  Status s, ss;
  for (auto& exec_context : exec_contexts_) {
    if (exec_context.tnode() == nullptr) {
      continue; // Skip empty statement.
    }
    client::YBqlOp* op = exec_context.op().get();
    ss = sql_env_->GetOpError(op);
    if (PREDICT_FALSE(!ss.ok())) {
      // YBOperation returns not-found error when the tablet is not found.
      const auto error_code =
          ss.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::SQL_STATEMENT_INVALID;
      ss = exec_context.Error(ss, error_code);
    }
    if (ss.ok()) {
      ss = ProcessOpResponse(op, &exec_context);
    }
    ss = ProcessStatementStatus(*exec_context.parse_tree(), ss);
    if (PREDICT_FALSE(!ss.ok())) {
      s = ss;
    }
  }
  return s;
}

Status Executor::AppendResult(const ExecutedResult::SharedPtr& result) {
  if (result == nullptr) {
    return Status::OK();
  }
  if (result_ == nullptr) {
    result_ = result;
    return Status::OK();
  }
  CHECK(result_->type() == ExecutedResult::Type::ROWS);
  CHECK(result->type() == ExecutedResult::Type::ROWS);
  return std::static_pointer_cast<RowsResult>(result_)->Append(
      static_cast<const RowsResult&>(*result));
}

void Executor::StatementExecuted(const Status& s) {
  // Update metrics for all statements executed.
  if (s.ok() && sql_metrics_ != nullptr) {
    for (const auto& exec_context : exec_contexts_) {
      const TreeNode* tnode = exec_context.tnode();
      if (tnode != nullptr) {
        MonoDelta delta = MonoTime::Now(MonoTime::FINE).GetDeltaSince(exec_context.start_time());

        sql_metrics_->time_to_execute_sql_query_->Increment(delta.ToMicroseconds());

        switch (tnode->opcode()) {
          case TreeNodeOpcode::kPTSelectStmt:
            sql_metrics_->sql_select_->Increment(delta.ToMicroseconds());
            break;
          case TreeNodeOpcode::kPTInsertStmt:
            sql_metrics_->sql_insert_->Increment(delta.ToMicroseconds());
            break;
          case TreeNodeOpcode::kPTUpdateStmt:
            sql_metrics_->sql_update_->Increment(delta.ToMicroseconds());
            break;
          case TreeNodeOpcode::kPTDeleteStmt:
            sql_metrics_->sql_delete_->Increment(delta.ToMicroseconds());
            break;
          default:
            sql_metrics_->sql_others_->Increment(delta.ToMicroseconds());
        }
      }
    }
  }

  // Clean up and invoke statement-executed callback.
  ExecutedResult::SharedPtr result = s.ok() ? std::move(result_) : nullptr;
  StatementExecutedCallback cb = std::move(cb_);
  Reset();
  cb.Run(s, result);
}

void Executor::Reset() {
  exec_contexts_.clear();
  exec_context_ = nullptr;
  result_ = nullptr;
  cb_.Reset();
}

}  // namespace sql
}  // namespace yb
