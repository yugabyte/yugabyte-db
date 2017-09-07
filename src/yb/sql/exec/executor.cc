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

// Runs the StatementExecutedCallback cb with no result and returns.
#define CB_RETURN(cb, s)  \
  do {                    \
    (cb).Run(s, nullptr); \
    return;               \
  } while (0)

// Runs the StatementExecutedCallback cb and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)    \
  do {                             \
    ::yb::Status _s = (s);         \
    if (PREDICT_FALSE(!_s.ok())) { \
      (cb).Run(_s, nullptr);       \
      return;                      \
    }                              \
  } while (0)

//--------------------------------------------------------------------------------------------------

Executor::Executor(SqlEnv *sql_env, const SqlMetrics* sql_metrics)
    : sql_env_(sql_env),
      sql_metrics_(sql_metrics) {
}

Executor::~Executor() {
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecuteAsync(
    const string &sql_stmt, const ParseTree &parse_tree, const StatementParameters& params,
    StatementExecutedCallback cb) {
  // Prepare execution context.
  exec_context_ = ExecContext::UniPtr(new ExecContext(sql_stmt.c_str(),
                                                      sql_stmt.length(),
                                                      sql_env_));
  params_ = &params;
  // Execute the parse tree's root node.
  ExecTreeNodeAsync(parse_tree.root().get(), std::move(cb));
}

void Executor::Done() {
  exec_context_ = nullptr;
  params_ = nullptr;
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecTreeNodeAsync(const TreeNode *tnode, StatementExecutedCallback cb) {
  DCHECK_ONLY_NOTNULL(tnode);

  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTListNode:
      return ExecPTNodeAsync(static_cast<const PTListNode *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTCreateTable:
      return ExecPTNodeAsync(static_cast<const PTCreateTable *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTAlterTable:
      return ExecPTNodeAsync(static_cast<const PTAlterTable *>(tnode), cb);

    case TreeNodeOpcode::kPTCreateType:
      return ExecPTNodeAsync(static_cast<const PTCreateType *>(tnode), cb);

    case TreeNodeOpcode::kPTDropStmt:
      return ExecPTNodeAsync(static_cast<const PTDropStmt *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTSelectStmt:
      return ExecPTNodeAsync(static_cast<const PTSelectStmt *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNodeAsync(static_cast<const PTInsertStmt *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTDeleteStmt:
      return ExecPTNodeAsync(static_cast<const PTDeleteStmt *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTUpdateStmt:
      return ExecPTNodeAsync(static_cast<const PTUpdateStmt *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTCreateKeyspace:
      return ExecPTNodeAsync(static_cast<const PTCreateKeyspace *>(tnode), std::move(cb));

    case TreeNodeOpcode::kPTUseKeyspace:
      return ExecPTNodeAsync(static_cast<const PTUseKeyspace *>(tnode), std::move(cb));

    default:
      CB_RETURN(cb, exec_context_->Error(tnode->loc(), ErrorCode::FEATURE_NOT_SUPPORTED));
  }
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTListNode *lnode, StatementExecutedCallback cb, int idx) {
  // Done if the list is empty.
  if (lnode->size() == 0) {
    CB_RETURN(cb, Status::OK());
  }
  ExecTreeNodeAsync(
      lnode->element(idx).get(),
      Bind(&Executor::PTNodeAsyncDone, Unretained(this), Unretained(lnode), idx,
           MonoTime::Now(MonoTime::FINE), std::move(cb)));
}

void Executor::PTNodeAsyncDone(
    const PTListNode *lnode, int index, MonoTime start, StatementExecutedCallback cb,
    const Status &s, const ExecutedResult::SharedPtr& result) {
  const TreeNode *tnode = lnode->element(index).get();
  if (PREDICT_FALSE(!s.ok())) {
    // Before leaving the execution step, collect all errors and place them in return status.
    VLOG(3) << "Failed to execute tree node <" << tnode << ">";
    CB_RETURN(cb, exec_context_->GetStatus());
  }

  VLOG(3) << "Successfully executed tree node <" << tnode << ">";
  if (sql_metrics_ != nullptr) {
    MonoDelta delta = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start);
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
  cb.Run(Status::OK(), result);
  if (++index < lnode->size()) {
    ExecPTNodeAsync(lnode, std::move(cb), index);
  }
}

//--------------------------------------------------------------------------------------------------
void Executor::ExecPTNodeAsync(const PTCreateType *tnode, StatementExecutedCallback cb) {
  YBTableName yb_name = tnode->yb_type_name();

  const std::string& type_name = yb_name.table_name();
  std::string keyspace_name = yb_name.namespace_name();
  if (!yb_name.has_namespace()) {
    if (exec_context_->CurrentKeyspace().empty()) {
      CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::NO_NAMESPACE_USED));
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
  if (!s.ok()) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TYPE;
    } else if (s.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TYPE) {
      CB_RETURN(cb, Status::OK());
    }

    CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), s.ToString().c_str(), error_code));
  }
  cb.Run(Status::OK(),
         std::make_shared<SchemaChangeResult>("CREATED", "TYPE", keyspace_name, type_name));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTCreateTable *tnode, StatementExecutedCallback cb) {
  YBTableName table_name = tnode->yb_table_name();

  if (!table_name.has_namespace()) {
    if (exec_context_->CurrentKeyspace().empty()) {
      CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::NO_NAMESPACE_USED));
    }

    table_name.set_namespace_name(exec_context_->CurrentKeyspace());
  }

  if (table_name.is_system() && client::FLAGS_yb_system_namespace_readonly) {
    CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::SYSTEM_NAMESPACE_READONLY));
  }

  // Setting up columns.
  Status s;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (const auto& column : hash_columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      CB_RETURN(
          cb, exec_context_->Error(
              tnode->columns_loc(), s.ToString().c_str(), ErrorCode::INVALID_TABLE_DEFINITION));
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
      CB_RETURN(
          cb, exec_context_->Error(
              tnode->columns_loc(), s.ToString().c_str(), ErrorCode::INVALID_TABLE_DEFINITION));
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
    CB_RETURN(
        cb, exec_context_->Error(
            tnode->columns_loc(), s.ToString().c_str(), ErrorCode::INVALID_TABLE_DEFINITION));
  }

  b.SetTableProperties(table_properties);

  s = b.Build(&schema);
  if (!s.ok()) {
    CB_RETURN(
        cb, exec_context_->Error(
            tnode->columns_loc(), s.ToString().c_str(), ErrorCode::INVALID_TABLE_DEFINITION));
  }

  // Create table.
  shared_ptr<YBTableCreator> table_creator(exec_context_->NewTableCreator());
  s = table_creator->table_name(table_name)
                    .table_type(YBTableType::YQL_TABLE_TYPE)
                    .schema(&schema)
                    .Create();
  if (!s.ok()) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (s.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    } else if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TABLE) {
      CB_RETURN(cb, Status::OK());
    }

    CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), s.ToString().c_str(), error_code));
  }
  cb.Run(
      Status::OK(),
      std::make_shared<SchemaChangeResult>(
          "CREATED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name()));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTAlterTable *tnode, StatementExecutedCallback cb) {
  YBTableName table_name = tnode->yb_table_name();

  if (!table_name.has_namespace()) {
    if (exec_context_->CurrentKeyspace().empty()) {
      CB_RETURN(cb, exec_context_->Error(tnode->loc(), ErrorCode::NO_NAMESPACE_USED));
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
        CB_RETURN(cb, exec_context_->Error(tnode->loc(), ErrorCode::FEATURE_NOT_YET_IMPLEMENTED));
    }
  }

  if (!tnode->mod_props().empty()) {
    TableProperties table_properties;
    Status s = tnode->ToTableProperties(&table_properties);
    if(!s.ok()) {
      CB_RETURN(cb, exec_context_->Error(tnode->loc(), s.ToString().c_str(),
                                         ErrorCode::INVALID_ARGUMENTS));
    }

    table_alterer->SetTableProperties(table_properties);
  }

  Status s = table_alterer->Alter();

  if (!s.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;

    CB_RETURN(cb, exec_context_->Error(tnode->loc(), s.ToString().c_str(), error_code));
  }

  cb.Run(
    Status::OK(),
    std::make_shared<SchemaChangeResult>(
        "UPDATED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name()));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTDropStmt *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  Status s;
  ErrorCode error_not_found = ErrorCode::SERVER_ERROR;
  SchemaChangeResult::SharedPtr result;

  switch (tnode->drop_type()) {
    case OBJECT_TABLE: {
      YBTableName table_name = tnode->yb_table_name();

      if (!table_name.has_namespace()) {
        if (exec_context_->CurrentKeyspace().empty()) {
          CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::NO_NAMESPACE_USED));
        }

        table_name.set_namespace_name(exec_context_->CurrentKeyspace());
      }
      // Drop the table.
      s = exec_context_->DeleteTable(table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result = std::make_shared<SchemaChangeResult>(
          "DROPPED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name());
      break;
    }

    case OBJECT_SCHEMA: {
      // Drop the keyspace.
      const string &keyspace_name(tnode->name()->last_name().c_str());
      s = exec_context_->DeleteKeyspace(keyspace_name);
      error_not_found = ErrorCode::KEYSPACE_NOT_FOUND;
      result = std::make_shared<SchemaChangeResult>("DROPPED", "KEYSPACE", keyspace_name);
      break;
    }

    case OBJECT_TYPE: {
      const string& type_name(tnode->name()->last_name().c_str());
      string namespace_name(tnode->name()->first_name().c_str());

      // If name has no explicit keyspace use default
      if (tnode->name()->IsSimpleName()) {
        if (exec_context_->CurrentKeyspace().empty()) {
          CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::NO_NAMESPACE_USED));
        }
        namespace_name = exec_context_->CurrentKeyspace();
      }

      // Drop the type.
      s = exec_context_->DeleteUDType(namespace_name, type_name);
      error_not_found = ErrorCode::TYPE_NOT_FOUND;
      result = std::make_shared<SchemaChangeResult>(
          "DROPPED", "TYPE", namespace_name, type_name);
      break;
    }

    default:
      CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::FEATURE_NOT_SUPPORTED));
  }

  if (!s.ok()) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsNotFound()) {
      // Ignore not found error for a DROP IF EXISTS statement.
      if (tnode->drop_if_exists()) {
        CB_RETURN(cb, Status::OK());
      }

      error_code = error_not_found;
    }

    CB_RETURN(
        cb, exec_context_->Error(tnode->name_loc(), s.ToString().c_str(), error_code));
  }

  cb.Run(Status::OK(), result);
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(
    const PTSelectStmt *tnode, StatementExecutedCallback cb, RowsResult::SharedPtr current_result) {
  const shared_ptr<client::YBTable>& table = tnode->table();

  if (table == nullptr) {
    // If this is a system table but the table does not exist, it is okay. Just return OK with void
    // result.
    CB_RETURN(cb,
              tnode->is_system() ? Status::OK() :
              exec_context_->Error(tnode->loc(), ErrorCode::TABLE_NOT_FOUND));
  }

  // If there are rows buffered in current_result, use the paging state in it. Otherwise, use the
  // paging state from the client.
  const StatementParameters *paging_params = params_;
  StatementParameters current_params;
  if (current_result != nullptr) {
    CB_RETURN_NOT_OK(cb, current_params.set_paging_state(current_result->paging_state()));
    paging_params = &current_params;
  }

  // If there is a table id in the statement parameter's paging state, this is a continuation of
  // a prior SELECT statement. Verify that the same table still exists.
  const bool continue_select = !paging_params->table_id().empty();
  if (continue_select && paging_params->table_id() != table->id()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), "Table no longer exists.", ErrorCode::TABLE_NOT_FOUND));
  }

  // See if there is any rows buffered in current_result locally.
  size_t current_row_count = 0;
  if (current_result != nullptr) {
    CB_RETURN_NOT_OK(
        cb, YQLRowBlock::GetRowCount(current_result->client(), current_result->rows_data(),
                                     &current_row_count));
  }

  // If the current result hits the request page size already, return the result.
  if (current_row_count >= params_->page_size()) {
    cb.Run(Status::OK(), current_result);
    return;
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

  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // If where clause restrictions guarantee no rows could match, return empty result immediately.
  if (no_results) {
    return SelectAsyncDone(tnode, select_op, cb, current_result, Status::OK(), nullptr);
  }

  if (req->has_max_hash_code()) {
    uint16_t next_hash_code = paging_params->next_partition_key().empty() ? 0
        : PartitionSchema::DecodeMultiColumnHashValue(paging_params->next_partition_key());

    // if reached max_hash_code stop and return the current result
    if (next_hash_code >= req->max_hash_code()) {
      current_result->clear_paging_state();
      cb.Run(Status::OK(), current_result);
      return;
    }
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Specify distinct columns or non.
  req->set_distinct(tnode->distinct());

  // Default row count limit is the page size less the rows buffered in current_result locally.
  // And we should return paging state when page size limit is hit.
  req->set_limit(params_->page_size() - current_row_count);
  req->set_return_paging_state(true);

  // Check if there is a limit and compute the new limit based on the number of returned rows.
  if (tnode->has_limit()) {
    YQLExpressionPB limit_pb;
    CB_RETURN_NOT_OK(cb, PTExprToPB(tnode->limit(), &limit_pb));
    if (limit_pb.has_value() && YQLValue::IsNull(limit_pb.value())) {
      CB_RETURN(cb, exec_context_->Error(tnode->loc(),
                                         "LIMIT value cannot be null.",
                                         ErrorCode::INVALID_ARGUMENTS));
    }

    // this should be ensured by checks before getting here
    DCHECK(limit_pb.has_value() && limit_pb.value().has_int64_value())
        << "Integer constant expected for LIMIT clause";

    int64_t limit = limit_pb.value().int64_value();
    if (limit < 0) {
      CB_RETURN(
          cb, exec_context_->Error(
              tnode->loc(), "LIMIT clause cannot be a negative value.",
              ErrorCode::INVALID_ARGUMENTS));
    }
    if (limit == 0 || paging_params->total_num_rows_read() >= limit) {
      CB_RETURN(cb, Status::OK());
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
    select_op->set_yb_consistency_level(params_->yb_consistency_level());
  }

  // Apply the operator. Call SelectAsyncDone when done to try to fetch more rows and buffer locally
  // before returning the result to the client.
  exec_context_->ApplyReadAsync(select_op, tnode,
                                Bind(&Executor::SelectAsyncDone, Unretained(this),
                                     Unretained(tnode), select_op, std::move(cb), current_result));
}

void Executor::SelectAsyncDone(const PTSelectStmt *tnode, std::shared_ptr<YBqlReadOp> select_op,
                               StatementExecutedCallback cb, RowsResult::SharedPtr current_result,
                               const Status &s, const ExecutedResult::SharedPtr& new_result) {

  // If an error occurs, return current result if present and ignore the error. Otherwise, return
  // the error.
  if (PREDICT_FALSE(!s.ok())) {
    cb.Run(current_result != nullptr ? Status::OK() : s, current_result);
    return;
  }

  // Process the new result.
  if (new_result != nullptr) {
    // If there is a new non-rows result. Return the current result if present (shouldn't happen)
    // and ignore the new one. Otherwise, return the new result.
    if (new_result->type() != ExecutedResult::Type::ROWS) {
      if (current_result != nullptr) {
        LOG(WARNING) <<
            Substitute("New execution result $0 ignored", static_cast<int>(new_result->type()));
        cb.Run(Status::OK(), current_result);
      } else {
        cb.Run(Status::OK(), new_result);
      }
      return;
    }
    // If there is a new rows result, append the rows and merge the paging state into the current
    // result if present.
    const auto* rows_result = static_cast<RowsResult*>(new_result.get());
    if (current_result == nullptr) {
      current_result = std::static_pointer_cast<RowsResult>(new_result);
    } else {
      CB_RETURN_NOT_OK(cb, current_result->Append(*rows_result));
    }
  }

  // If there is a paging state, try fetching more rows and buffer locally. ExecPTNodeAsync() will
  // ensure we do not exceed the page size.
  if (current_result != nullptr && !current_result->paging_state().empty()) {
    ExecPTNodeAsync(tnode, std::move(cb), current_result);
    return;
  }

  // If current_result is null, producing an empty result.
  if (current_result == nullptr) {
    YQLRowBlock empty_row_block(tnode->table()->InternalSchema(), {});
    faststring buffer;
    empty_row_block.Serialize(select_op->request().client(), &buffer);
    *select_op->mutable_rows_data() = buffer.ToString();
    current_result = std::make_shared<RowsResult>(select_op.get());
  }

  cb.Run(Status::OK(), current_result);
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTInsertStmt *tnode, StatementExecutedCallback cb) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> insert_op(table->NewYQLInsert());
  YQLWriteRequestPB *req = insert_op->mutable_request();

  // Set the ttl
  CB_RETURN_NOT_OK(cb, TtlToPB(tnode, insert_op->mutable_request()));

  // Set the values for columns.
  Status s = ColumnArgsToPB(table, tnode, req, insert_op->mutable_row());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), insert_op->mutable_request()->mutable_if_expr());
    if (!s.ok()) {
      CB_RETURN(
          cb,
          exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
    }
  }

  // Apply the operator.
  exec_context_->ApplyWriteAsync(insert_op, tnode, std::move(cb));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTDeleteStmt *tnode, StatementExecutedCallback cb) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> delete_op(table->NewYQLDelete());
  YQLWriteRequestPB *req = delete_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  YBPartialRow *row = delete_op->mutable_row();
  Status s = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops(),
      tnode->subscripted_col_where_ops());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }
  s = ColumnArgsToPB(table, tnode, req, delete_op->mutable_row());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), delete_op->mutable_request()->mutable_if_expr());
    if (!s.ok()) {
      CB_RETURN(
          cb,
          exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
    }
  }

  // Apply the operator.
  exec_context_->ApplyWriteAsync(delete_op, tnode, std::move(cb));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTUpdateStmt *tnode, StatementExecutedCallback cb) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> update_op(table->NewYQLUpdate());
  YQLWriteRequestPB *req = update_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  YBPartialRow *row = update_op->mutable_row();
  Status s = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops(),
      tnode->subscripted_col_where_ops());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the ttl
  CB_RETURN_NOT_OK(cb, TtlToPB(tnode, update_op->mutable_request()));

  // Setup the columns' new values.
  s = ColumnArgsToPB(table, tnode, update_op->mutable_request(), update_op->mutable_row());

  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (!s.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), update_op->mutable_request()->mutable_if_expr());
    if (!s.ok()) {
      CB_RETURN(
          cb,
          exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
    }
  }

  // Apply the operator.
  exec_context_->ApplyWriteAsync(update_op, tnode, std::move(cb));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTCreateKeyspace *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  Status s = exec_context_->CreateKeyspace(tnode->name());

  if (!s.ok()) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsAlreadyPresent()) {
      if (tnode->create_if_not_exists()) {
        // Case: CREATE KEYSPACE IF NOT EXISTS name;
        CB_RETURN(cb, Status::OK());
      }

      error_code = ErrorCode::KEYSPACE_ALREADY_EXISTS;
    }

    CB_RETURN(cb, exec_context_->Error(tnode->loc(), s.ToString().c_str(), error_code));
  }

  cb.Run(Status::OK(), std::make_shared<SchemaChangeResult>("CREATED", "KEYSPACE", tnode->name()));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTUseKeyspace *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  const Status s = exec_context_->UseKeyspace(tnode->name());

  if (!s.ok()) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    }

    CB_RETURN(cb, exec_context_->Error(tnode->loc(), s.ToString().c_str(), error_code));
  }
  cb.Run(Status::OK(), std::make_shared<SetKeyspaceResult>(tnode->name()));
}

}  // namespace sql
}  // namespace yb
