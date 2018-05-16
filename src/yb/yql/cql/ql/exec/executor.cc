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

#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/yb_op.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/yql/cql/ql/ql_processor.h"
#include "yb/util/decimal.h"
#include "yb/common/common.pb.h"

namespace yb {
namespace ql {

using std::string;
using std::shared_ptr;
using namespace std::placeholders;

using client::YBColumnSpec;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableType;
using client::YBTableName;
using client::YBqlReadOp;
using client::YBqlWriteOp;
using client::YBqlWriteOpPtr;
using strings::Substitute;

#define RETURN_STMT_NOT_OK(s) do {                                         \
    auto&& _s = (s);                                                       \
    if (PREDICT_FALSE(!_s.ok())) return StatementExecuted(MoveStatus(_s)); \
  } while (false)


//--------------------------------------------------------------------------------------------------

Executor::Executor(QLEnv *ql_env, const QLMetrics* ql_metrics)
    : ql_env_(ql_env),
      ql_metrics_(ql_metrics),
      rescheduled_flush_async_cb_(Bind(&Executor::FlushAsync, Unretained(this))),
      flush_async_cb_(Bind(&Executor::FlushAsyncDone, Unretained(this))) {
}

Executor::~Executor() {
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecuteAsync(const string &ql_stmt, const ParseTree &parse_tree,
                            const StatementParameters* params, StatementExecutedCallback cb) {
  DCHECK(cb_.is_null()) << "Another execution is in progress.";
  cb_ = std::move(cb);
  ql_env_->Reset();
  // Execute the statement and invoke statement-executed callback either when there is an error or
  // no async operation is pending.
  RETURN_STMT_NOT_OK(Execute(ql_stmt, parse_tree, params));
  FlushAsync();
}

//--------------------------------------------------------------------------------------------------

void Executor::BeginBatch(StatementExecutedCallback cb) {
  DCHECK(cb_.is_null()) << "Another execution is in progress.";
  cb_ = std::move(cb);
  ql_env_->Reset();
}

void Executor::ExecuteBatch(const std::string &ql_stmt, const ParseTree &parse_tree,
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
    s = Execute(ql_stmt, parse_tree, params);
  }
  RETURN_STMT_NOT_OK(s);
}

void Executor::ApplyBatch() {
  FlushAsync();
}

void Executor::AbortBatch() {
  ql_env_->AbortOps();
  Reset();
}

//--------------------------------------------------------------------------------------------------

Status Executor::Execute(const string &ql_stmt, const ParseTree &parse_tree,
                         const StatementParameters* params) {
  // Prepare execution context and execute the parse tree's root node.
  exec_contexts_.emplace_back(ql_stmt.c_str(), ql_stmt.length(), &parse_tree, params, ql_env_);
  return ProcessStatementStatus(parse_tree, ExecTreeNode(exec_context().tnode()));
}

ExecContext& Executor::exec_context() {
  CHECK(!exec_contexts_.empty());
  return exec_contexts_.back();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecTreeNode(const TreeNode *tnode) {
  if (tnode == nullptr) {
    return Status::OK();
  }
  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTListNode:
      return ExecPTNode(static_cast<const PTListNode *>(tnode));

    case TreeNodeOpcode::kPTCreateTable: FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPTCreateIndex:
      return ExecPTNode(static_cast<const PTCreateTable *>(tnode));

    case TreeNodeOpcode::kPTAlterTable:
      return ExecPTNode(static_cast<const PTAlterTable *>(tnode));

    case TreeNodeOpcode::kPTCreateType:
      return ExecPTNode(static_cast<const PTCreateType *>(tnode));

    case TreeNodeOpcode::kPTCreateRole:
      return ExecPTNode(static_cast<const PTCreateRole *>(tnode));

    case TreeNodeOpcode::kPTGrantRole:
      return ExecPTNode(static_cast<const PTGrantRole *>(tnode));

    case TreeNodeOpcode::kPTDropStmt:
      return ExecPTNode(static_cast<const PTDropStmt *>(tnode));

    case TreeNodeOpcode::kPTGrantPermission:
      return ExecPTNode(static_cast<const PTGrantPermission *>(tnode));

    case TreeNodeOpcode::kPTSelectStmt:
      return ExecPTNode(static_cast<const PTSelectStmt *>(tnode));

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNode(static_cast<const PTInsertStmt *>(tnode));

    case TreeNodeOpcode::kPTDeleteStmt:
      return ExecPTNode(static_cast<const PTDeleteStmt *>(tnode));

    case TreeNodeOpcode::kPTUpdateStmt:
      return ExecPTNode(static_cast<const PTUpdateStmt *>(tnode));

    case TreeNodeOpcode::kPTStartTransaction:
      return ExecPTNode(static_cast<const PTStartTransaction *>(tnode));

    case TreeNodeOpcode::kPTCommit:
      return ExecPTNode(static_cast<const PTCommit *>(tnode));

    case TreeNodeOpcode::kPTTruncateStmt:
      return ExecPTNode(static_cast<const PTTruncateStmt *>(tnode));

    case TreeNodeOpcode::kPTCreateKeyspace:
      return ExecPTNode(static_cast<const PTCreateKeyspace *>(tnode));

    case TreeNodeOpcode::kPTUseKeyspace:
      return ExecPTNode(static_cast<const PTUseKeyspace *>(tnode));

    default:
      return exec_context().Error(tnode, ErrorCode::FEATURE_NOT_SUPPORTED);
  }
}



Status Executor::ExecPTNode(const PTCreateRole *tnode) {

  const std::string role_name = tnode->role_name();
  const std::string salted_hash = tnode->salted_hash();

  Status s = exec_context().CreateRole(role_name, salted_hash, tnode->login(), tnode->superuser());

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_ROLE;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_ROLE) {
      return Status::OK();
    }
    // TODO (Bristy) : Set result_ properly
    return exec_context().Error(tnode, s, error_code);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTGrantRole *tnode) {

  Status s = exec_context().GrantRole(tnode->granted_role_name(), tnode->recipient_role_name());

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_REQUEST;
    }
    if (s.IsNotFound()) {
      error_code = ErrorCode::ROLE_NOT_FOUND;
    }
    // TODO (Bristy) : Set result_ properly
    return exec_context().Error(tnode, s, error_code);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTListNode *tnode) {
  ExecContext& parent = exec_context();
  for (TreeNode::SharedPtr dml : tnode->node_list()) {
    exec_contexts_.emplace_back(parent, dml.get());
    RETURN_NOT_OK(ProcessStatementStatus(*exec_context().parse_tree(), ExecTreeNode(dml.get())));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateType *tnode) {
  YBTableName yb_name = tnode->yb_type_name();

  const std::string& type_name = yb_name.table_name();
  std::string keyspace_name = yb_name.namespace_name();

  std::vector<std::string> field_names;
  std::vector<std::shared_ptr<QLType>> field_types;

  for (const PTTypeField::SharedPtr field : tnode->fields()->node_list()) {
    field_names.emplace_back(field->yb_name());
    field_types.push_back(field->ql_type());
  }

  Status s = exec_context().CreateUDType(keyspace_name, type_name, field_names, field_types);
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

    return exec_context().Error(tnode->type_name(), s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>("CREATED", "TYPE", keyspace_name, type_name);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateTable *tnode) {
  YBTableName table_name = tnode->yb_table_name();

  if (table_name.is_system() && client::FLAGS_yb_system_namespace_readonly) {
    return exec_context().Error(tnode->table_name(), ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  // Setting up columns.
  Status s;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (const auto& column : hash_columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      return exec_context().Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
    }
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->HashPrimaryKey()
        ->Order(column->order());
  }
  const MCList<PTColumnDefinition *>& primary_columns = tnode->primary_columns();
  for (const auto& column : primary_columns) {
    b.AddColumn(column->yb_name())->Type(column->ql_type())
        ->PrimaryKey()
        ->Order(column->order())
        ->SetSortingType(column->sorting_type());
  }
  const MCList<PTColumnDefinition *>& columns = tnode->columns();
  for (const auto& column : columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      return exec_context().Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
    }
    YBColumnSpec *column_spec = b.AddColumn(column->yb_name())->Type(column->ql_type())
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
  s = tnode->ToTableProperties(&table_properties);
  if (!s.ok()) {
    return exec_context().Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }
  if (tnode->opcode() == TreeNodeOpcode::kPTCreateIndex) {
    table_properties.SetTransactional(true);
  }

  b.SetTableProperties(table_properties);

  s = b.Build(&schema);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode->columns().front(), s, ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Create table.
  shared_ptr<YBTableCreator> table_creator(exec_context().NewTableCreator());
  table_creator->table_name(table_name)
      .table_type(YBTableType::YQL_TABLE_TYPE)
      .schema(&schema);
  if (tnode->opcode() == TreeNodeOpcode::kPTCreateIndex) {
    const PTCreateIndex *index_node = static_cast<const PTCreateIndex*>(tnode);
    table_creator->indexed_table_id(index_node->indexed_table_id());
    table_creator->is_local_index(index_node->is_local());
  }
  s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (s.IsNotFound()) {
      error_code = tnode->opcode() == TreeNodeOpcode::kPTCreateIndex
                   ? ErrorCode::TABLE_NOT_FOUND
                   : ErrorCode::KEYSPACE_NOT_FOUND;
    } else if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TABLE) {
      return Status::OK();
    }

    return exec_context().Error(tnode->table_name(), s, error_code);
  }

  if (tnode->opcode() == TreeNodeOpcode::kPTCreateIndex) {
    const YBTableName indexed_table_name =
        static_cast<const PTCreateIndex*>(tnode)->indexed_table_name();
    result_ = std::make_shared<SchemaChangeResult>(
        "UPDATED", "TABLE", indexed_table_name.namespace_name(), indexed_table_name.table_name());
    ql_env_->RemoveCachedTableDesc(indexed_table_name);
  } else {
    result_ = std::make_shared<SchemaChangeResult>(
        "CREATED", "TABLE", table_name.namespace_name(), table_name.table_name());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTAlterTable *tnode) {
  YBTableName table_name = tnode->yb_table_name();

  shared_ptr<YBTableAlterer> table_alterer(exec_context().NewTableAlterer(table_name));

  for (const auto& mod_column : tnode->mod_columns()) {
    switch (mod_column->mod_type()) {
      case ALTER_ADD:
        table_alterer->AddColumn(mod_column->new_name()->data())
            ->Type(mod_column->ql_type());
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
        return exec_context().Error(tnode, ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
    }
  }

  if (!tnode->mod_props().empty()) {
    TableProperties table_properties;
    Status s = tnode->ToTableProperties(&table_properties);
    if(PREDICT_FALSE(!s.ok())) {
      return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
    }

    table_alterer->SetTableProperties(table_properties);
  }

  Status s = table_alterer->Alter();
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::EXEC_ERROR);
  }

  result_ = std::make_shared<SchemaChangeResult>(
      "UPDATED", "TABLE", table_name.namespace_name(), table_name.table_name());
  ql_env_->RemoveCachedTableDesc(table_name);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTDropStmt *tnode) {
  Status s;
  ErrorCode error_not_found = ErrorCode::SERVER_ERROR;

  switch (tnode->drop_type()) {
    case OBJECT_TABLE: {
      // Drop the table.
      const YBTableName table_name = tnode->yb_table_name();
      s = exec_context().DeleteTable(table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>(
          "DROPPED", "TABLE", table_name.namespace_name(), table_name.table_name());
      ql_env_->RemoveCachedTableDesc(table_name);
      break;
    }

    case OBJECT_INDEX: {
      // Drop the index.
      const YBTableName table_name = tnode->yb_table_name();
      YBTableName indexed_table_name;
      s = exec_context().DeleteIndexTable(table_name, &indexed_table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>(
          "UPDATED", "TABLE", indexed_table_name.namespace_name(), indexed_table_name.table_name());
      ql_env_->RemoveCachedTableDesc(indexed_table_name);
      break;
    }

    case OBJECT_SCHEMA: {
      // Drop the keyspace.
      const string keyspace_name(tnode->name()->last_name().c_str());
      s = exec_context().DeleteKeyspace(keyspace_name);
      error_not_found = ErrorCode::KEYSPACE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>("DROPPED", "KEYSPACE", keyspace_name);
      break;
    }

    case OBJECT_TYPE: {
      // Drop the type.
      const string type_name(tnode->name()->last_name().c_str());
      const string namespace_name(tnode->name()->first_name().c_str());
      s = exec_context().DeleteUDType(namespace_name, type_name);
      error_not_found = ErrorCode::TYPE_NOT_FOUND;
      result_ = std::make_shared<SchemaChangeResult>("DROPPED", "TYPE", namespace_name, type_name);
      ql_env_->RemoveCachedUDType(namespace_name, type_name);
      break;
    }

    case OBJECT_ROLE: {
      // Drop the role
      const string role_name(tnode->name()->QLName());
      s = exec_context().DeleteRole(role_name);
      error_not_found = ErrorCode::ROLE_NOT_FOUND;
      // TODO (Bristy) : Set result_ properly
      break;
    }

    default:
      return exec_context().Error(tnode->name(), ErrorCode::FEATURE_NOT_SUPPORTED);
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

    return exec_context().Error(tnode->name(), s, error_code);
  }

  return Status::OK();
}


Status Executor::ExecPTNode(const PTGrantPermission *tnode) {
  const string role_name = tnode->role_name()->QLName();
  const string canonical_resource = tnode->canonical_resource();
  const char* resource_name = tnode->resource_name();
  const char* namespace_name = tnode->namespace_name();
  ResourceType resource_type = tnode->resource_type();
  PermissionType permission = tnode->permission();

  Status s = exec_context().GrantPermission(permission, resource_type, canonical_resource,
                                            resource_name, namespace_name, role_name);

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;
    if (s.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_ARGUMENTS;
    }
    if (s.IsNotFound()) {
      error_code = ErrorCode::RESOURCE_NOT_FOUND;
    }
    return exec_context().Error(tnode, s, error_code);
  }
  // TODO (Bristy) : Return proper result
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTSelectStmt *tnode) {
  const shared_ptr<client::YBTable>& table = tnode->table();
  if (table == nullptr) {
    // If this is a system table but the table does not exist, it is okay. Just return OK with void
    // result.
    return tnode->is_system() ? Status::OK()
                              : exec_context().Error(tnode, ErrorCode::TABLE_NOT_FOUND);
  }

  const StatementParameters& params = *exec_context().params();
  // If there is a table id in the statement parameter's paging state, this is a continuation of
  // a prior SELECT statement. Verify that the same table still exists.
  const bool continue_select = !params.table_id().empty();
  if (continue_select && params.table_id() != table->id()) {
    return exec_context().Error(tnode, "Table no longer exists.", ErrorCode::TABLE_NOT_FOUND);
  }

  // Create the read request.
  shared_ptr<YBqlReadOp> select_op(table->NewQLSelect());
  QLReadRequestPB *req = select_op->mutable_request();
  // Where clause - Hash, range, and regular columns.

  bool no_results = false;
  req->set_is_aggregate(tnode->is_aggregate());
  Status s = WhereClauseToPB(req, tnode->key_where_ops(), tnode->where_ops(),
                             tnode->subscripted_col_where_ops(), tnode->json_col_where_ops(),
                             tnode->partition_key_ops(), tnode->func_ops(), &no_results);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // If where clause restrictions guarantee no rows could match, return empty result immediately.
  if (no_results && !tnode->is_aggregate()) {
    QLRowBlock empty_row_block(tnode->table()->InternalSchema(), {});
    faststring buffer;
    empty_row_block.Serialize(select_op->request().client(), &buffer);
    *select_op->mutable_rows_data() = buffer.ToString();
    result_ = std::make_shared<RowsResult>(select_op.get());
    return Status::OK();
  }

  req->set_is_forward_scan(tnode->is_forward_scan());

  // Specify selected list by adding the expressions to selected_exprs in read request.
  QLRSRowDescPB *rsrow_desc_pb = req->mutable_rsrow_desc();
  for (const auto& expr : tnode->selected_exprs()) {
    if (expr->opcode() == TreeNodeOpcode::kPTAllColumns) {
      s = PTExprToPB(static_cast<const PTAllColumns*>(expr.get()), req);
    } else {
      s = PTExprToPB(expr, req->add_selected_exprs());
      if (PREDICT_FALSE(!s.ok())) {
        return exec_context().Error(expr, s, ErrorCode::INVALID_ARGUMENTS);
      }

      // Add the expression metadata (rsrow descriptor).
      QLRSColDescPB *rscol_desc_pb = rsrow_desc_pb->add_rscol_descs();
      rscol_desc_pb->set_name(expr->QLName());
      expr->ql_type()->ToQLTypePB(rscol_desc_pb->mutable_ql_type());
    }
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Specify distinct columns or non.
  req->set_distinct(tnode->distinct());

  // Default row count limit is the page size.
  // We should return paging state when page size limit is hit.
  req->set_limit(params.page_size());
  req->set_return_paging_state(true);

  // Check if there is a limit and compute the new limit based on the number of returned rows.
  if (tnode->has_limit()) {
    QLExpressionPB limit_pb;
    s = (PTExprToPB(tnode->limit(), &limit_pb));
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context().Error(tnode->limit(), s, ErrorCode::INVALID_ARGUMENTS);
    }

    if (limit_pb.has_value() && IsNull(limit_pb.value())) {
      return exec_context().Error(tnode->limit(), "LIMIT value cannot be null.",
                                  ErrorCode::INVALID_ARGUMENTS);
    }

    // this should be ensured by checks before getting here
    DCHECK(limit_pb.has_value() && limit_pb.value().has_int32_value())
        << "Integer constant expected for LIMIT clause";

    if (limit_pb.value().int32_value() < 0) {
      return exec_context().Error(tnode->limit(), "LIMIT value cannot be negative.",
                                  ErrorCode::INVALID_ARGUMENTS);
    }

    uint64_t limit = limit_pb.value().int32_value();
    if (limit == 0 || params.total_num_rows_read() >= limit) {
      return Status::OK();
    }

    // If the LIMIT clause, subtracting the number of rows we have returned so far, is lower than
    // the page size limit set from above, set the lower limit and do not return paging state when
    // this limit is hit.
    limit -= params.total_num_rows_read();
    if (limit <= req->limit()) {
      req->set_limit(limit);
      req->set_return_paging_state(false);
    }
  }

  // If this is a continuation of a prior read, set the next partition key, row key and total number
  // of rows read in the request's paging state.
  if (continue_select) {
    QLPagingStatePB *paging_state = req->mutable_paging_state();
    paging_state->set_next_partition_key(params.next_partition_key());
    paging_state->set_next_row_key(params.next_row_key());
    paging_state->set_total_num_rows_read(params.total_num_rows_read());
  }

  // Set the correct consistency level for the operation.
  if (tnode->is_system()) {
    // Always use strong consistency for system tables.
    select_op->set_yb_consistency_level(YBConsistencyLevel::STRONG);
  } else {
    select_op->set_yb_consistency_level(params.yb_consistency_level());
  }

  // If we have several hash partitions (i.e. IN condition on hash columns) we initialize the
  // start partition here, and then iteratively scan the rest in FetchMoreRowsIfNeeded.
  // Otherwise, the request will already have the right hashed column values set.
  if (exec_context().UnreadPartitionsRemaining() > 0) {
    if (continue_select) {
      exec_context().InitializePartition(select_op->mutable_request(),
                                         params.next_partition_index());
    } else {
      exec_context().InitializePartition(select_op->mutable_request(), 0);
    }
  }

  // Apply the operator.
  return exec_context().Apply(select_op);
}

Result<bool> Executor::FetchMoreRowsIfNeeded(const PTSelectStmt* tnode,
                                             const std::shared_ptr<YBqlReadOp>& op,
                                             ExecContext* exec_context) {
  if (result_ == nullptr) {
    return false;
  }

  // Rows read so far: in this fetch, previous fetches (for paging selects), and in total.
  RowsResult::SharedPtr current_result = std::static_pointer_cast<RowsResult>(result_);
  size_t current_fetch_row_count = 0;
  RETURN_NOT_OK(QLRowBlock::GetRowCount(current_result->client(),
                                        current_result->rows_data(),
                                        &current_fetch_row_count));

  size_t previous_fetches_row_count = exec_context->params()->total_num_rows_read();
  size_t total_row_count = previous_fetches_row_count + current_fetch_row_count;

  // Statement (paging) parameters.
  StatementParameters current_params;
  RETURN_NOT_OK(current_params.set_paging_state(current_result->paging_state()));

  // The limit for this select: min of page size and result limit (if set).
  uint64_t fetch_limit = exec_context->params()->page_size(); // default;
  if (tnode->has_limit()) {
    QLExpressionPB limit_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->limit(), &limit_pb));
    int64_t limit = limit_pb.value().int32_value() - previous_fetches_row_count;
    if (limit < fetch_limit) {
      fetch_limit = limit;
    }
  }

  //------------------------------------------------------------------------------------------------
  // Check if we should fetch more rows (return with 'done=true' otherwise).

  // If there is no paging state the current scan has exhausted its results.
  bool finished_current_read_partition = current_result->paging_state().empty();
  if (finished_current_read_partition) {

    // If there or no other partitions to query, we are done.
    if (exec_context->UnreadPartitionsRemaining() <= 1) {
      return false;
    }

    // Otherwise, we continue to the next partition.
    exec_context->AdvanceToNextPartition(op->mutable_request());
    op->mutable_request()->clear_hash_code();
    op->mutable_request()->clear_max_hash_code();
  }

  // If we reached the fetch limit (min of paging state and limit clause) we are done.
  if (current_fetch_row_count >= fetch_limit) {

    // If we reached the paging limit at the end of the previous partition for a multi-partition
    // select the next fetch should continue directly from the current partition.
    // We create a paging state here so that we can resume from the right partition.
    if (finished_current_read_partition && op->request().return_paging_state()) {
      QLPagingStatePB paging_state;
      paging_state.set_total_num_rows_read(total_row_count);
      paging_state.set_table_id(tnode->table()->id());
      paging_state.set_next_partition_index(exec_context->current_partition_index());
      current_result->set_paging_state(paging_state);
    }

    return false;
  }

  //------------------------------------------------------------------------------------------------
  // Fetch more results.

  // Update limit and paging_state information for next scan request.
  op->mutable_request()->set_limit(fetch_limit - current_fetch_row_count);
  QLPagingStatePB *paging_state = op->mutable_request()->mutable_paging_state();
  paging_state->set_next_partition_key(current_params.next_partition_key());
  paging_state->set_next_row_key(current_params.next_row_key());
  paging_state->set_total_num_rows_read(total_row_count);

  // Apply the request.
  RETURN_NOT_OK(exec_context->Apply(op));
  return true;
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTInsertStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> insert_op(table->NewQLInsert());
  QLWriteRequestPB *req = insert_op->mutable_request();

  // Set the ttl.
  Status s = TtlToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the timestamp.
  s = TimestampToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the values for columns.
  s = ColumnArgsToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), insert_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context().Error(tnode->if_clause(), s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operation.
  return ApplyOperation(tnode, insert_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTDeleteStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> delete_op(table->NewQLDelete());
  QLWriteRequestPB *req = delete_op->mutable_request();

  // Set the timestamp.
  Status s = TimestampToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  s = WhereClauseToPB(req, tnode->key_where_ops(), tnode->where_ops(),
                      tnode->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }
  s = ColumnArgsToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), delete_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context().Error(tnode->if_clause(), s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operation.
  return ApplyOperation(tnode, delete_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTUpdateStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> update_op(table->NewQLUpdate());
  QLWriteRequestPB *req = update_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  Status s = WhereClauseToPB(req, tnode->key_where_ops(), tnode->where_ops(),
                             tnode->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the ttl.
  s = TtlToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the timestamp.
  s = TimestampToPB(tnode, req);
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the columns' new values.
  s = ColumnArgsToPB(tnode, update_op->mutable_request());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tnode, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return exec_context().Error(tnode, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = PTExprToPB(tnode->if_clause(), update_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return exec_context().Error(tnode->if_clause(), s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operation.
  return ApplyOperation(tnode, update_op);
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTStartTransaction *tnode) {
  ql_env_->StartTransaction(tnode->isolation_level());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCommit *tnode) {
  // Commit happens after the write operations have been flushed and responded.
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTTruncateStmt *tnode) {
  return exec_context().TruncateTable(tnode->table_id());
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTCreateKeyspace *tnode) {
  Status s = exec_context().CreateKeyspace(tnode->name());

  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = ErrorCode::SERVER_ERROR;

    if (s.IsAlreadyPresent()) {
      if (tnode->create_if_not_exists()) {
        // Case: CREATE KEYSPACE IF NOT EXISTS name;
        return Status::OK();
      }

      error_code = ErrorCode::KEYSPACE_ALREADY_EXISTS;
    }

    return exec_context().Error(tnode, s, error_code);
  }

  result_ = std::make_shared<SchemaChangeResult>("CREATED", "KEYSPACE", tnode->name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status Executor::ExecPTNode(const PTUseKeyspace *tnode) {
  const Status s = exec_context().UseKeyspace(tnode->name());
  if (PREDICT_FALSE(!s.ok())) {
    ErrorCode error_code = s.IsNotFound() ? ErrorCode::KEYSPACE_NOT_FOUND : ErrorCode::SERVER_ERROR;
    return exec_context().Error(tnode, s, error_code);
  }

  result_ = std::make_shared<SetKeyspaceResult>(tnode->name());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

void Executor::FlushAsync() {
  batched_writes_by_primary_key_.clear();
  batched_writes_by_hash_key_.clear();
  if (!ql_env_->FlushAsync(&flush_async_cb_)) {
    StatementExecuted(Status::OK());
  }
}

void Executor::FlushAsyncDone(const Status &s, const bool rescheduled_call) {

  RETURN_STMT_NOT_OK(ProcessAsyncResults(s));

  const MonoTime now = (ql_metrics_ != nullptr) ? MonoTime::Now() : MonoTime();
  for (std::list<ExecContext>::iterator curr = exec_contexts_.begin(), next;
       curr != exec_contexts_.end(); curr = next) {
    next = std::next(curr);
    ExecContext& exec_context = *curr;
    const shared_ptr<client::YBqlOp>& op = exec_context.op();
    const TreeNode *tnode = exec_context.tnode();
    if (op != nullptr) {
      // Apply any operation that has been deferred but can be applied now.
      if (exec_context.IsOperationDeferred()) {
        if (!DeferOperation(static_cast<const PTDmlStmt*>(tnode),
                            std::static_pointer_cast<YBqlWriteOp>(op))) {
          RETURN_STMT_NOT_OK(exec_context.Apply());
        }
        continue;
      }

      // For SELECT statement, check if there are more rows to fetch.
      if (tnode->opcode() == TreeNodeOpcode::kPTSelectStmt) {
        const auto* select_stmt = static_cast<const PTSelectStmt *>(tnode);
        const auto& read_op = std::static_pointer_cast<YBqlReadOp>(op);
        const auto more_rows = FetchMoreRowsIfNeeded(select_stmt, read_op, &exec_context);
        RETURN_STMT_NOT_OK(more_rows);
        if (*more_rows) {
          continue;
        }
        // Evaluate aggregate functions if they are selected.
        RETURN_STMT_NOT_OK(AggregateResultSets(select_stmt));
      }

      // Update the metrics for SELECT/INSERT/UPDATE/DELETE here after the ops have been
      // completed but exclude the time to commit the transaction if any.
      if (ql_metrics_ != nullptr) {
        const auto delta_usec = (now - exec_context.start_time()).ToMicroseconds();
        switch (tnode->opcode()) {
          case TreeNodeOpcode::kPTSelectStmt:
            ql_metrics_->ql_select_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTInsertStmt:
            ql_metrics_->ql_insert_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTUpdateStmt:
            ql_metrics_->ql_update_->Increment(delta_usec);
            break;
          case TreeNodeOpcode::kPTDeleteStmt:
            ql_metrics_->ql_delete_->Increment(delta_usec);
            break;
          default:
            LOG(FATAL) << "unexpected operation";
        }
        ql_metrics_->time_to_execute_ql_query_->Increment(delta_usec);
      }

      // Apply child transaction results if any.
      const QLResponsePB& response = op->response();
      if (response.has_child_transaction_result()) {
        const auto& result = response.child_transaction_result();
        RETURN_STMT_NOT_OK(ql_env_->ApplyChildTransactionResult(result));
      }

      exec_contexts_.erase(curr);
    }
  }

  // If there are additional operations that have been applied above, flush them directly if we are
  // called from a rescheduled call already. Otherwise, reschedule the current CQL call to flush.
  // This is necessary because in an RF1 setup, the flush will be a direct local call and this
  // callback can recurse too deeply hitting the stack limitiation. Rescheduling can also avoid
  // occupying the RPC worker thread for too long starving other CQL calls waiting in the queue.
  if (ql_env_->HasBufferedOperations()) {
    return rescheduled_call ? FlushAsync()
                            : ql_env_->RescheduleCurrentCall(&rescheduled_flush_async_cb_);
  }

  // Commit the transaction if needed.
  if (!exec_contexts_.empty() &&
      exec_contexts_.back().tnode()->opcode() == TreeNodeOpcode::kPTCommit) {
    return ql_env_->CommitTransaction(std::bind(&Executor::CommitDone, this, _1));
  }

  StatementExecuted(Status::OK());
}

//--------------------------------------------------------------------------------------------------

namespace {

// Check if index updates can be issued from CQL proxy directly when executing a DML. Only indexes
// that index primary key columns only may be updated from CQL proxy.
bool UpdateIndexesLocally(const PTDmlStmt *tnode, const QLWriteRequestPB& req) {
  if (req.has_if_expr()) {
    return false;
  }

  switch (req.type()) {
    // For insert, the pk-only indexes can be updated from CQL proxy directly.
    case QLWriteRequestPB::QL_STMT_INSERT:
      return true;

    // For update, the pk-only indexes can be updated from CQL proxy directly only when not all
    // columns are set to null. Otherwise, the row may be removed by the DML if it was created via
    // update (i.e. no liveness column) and the remaining columns are already null.
    case QLWriteRequestPB::QL_STMT_UPDATE: {
      for (const auto& column_value : req.column_values()) {
        switch (column_value.expr().expr_case()) {
          case QLExpressionPB::ExprCase::kValue:
            if (!IsNull(column_value.expr().value())) {
              return true;
            }
            break;
          case QLExpressionPB::ExprCase::kColumnId: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kSubscriptedCol: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kJsonColumn: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kBfcall: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kTscall: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
          case QLExpressionPB::ExprCase::EXPR_NOT_SET:
            return false;
        }
      }
      return false;
    }
    // For delete, the pk-only indexes can be updated from CQL proxy directly only if the whole
    // is deleted and it is not a range delete.
    case QLWriteRequestPB::QL_STMT_DELETE: {
      const Schema& schema = tnode->table()->InternalSchema();
      return (req.column_values().empty() &&
              req.range_column_values_size() == schema.num_range_key_columns());
    }
  }
  return false; // Not feasible
}

} // namespace

Status Executor::UpdateIndexes(const PTDmlStmt *tnode, QLWriteRequestPB *req) {
  if (tnode->table()->index_map().empty()) {
    return Status::OK();
  }

  // DML with TTL is not allowed if indexes are present.
  if (req->has_ttl()) {
    return exec_context().Error(tnode, ErrorCode::FEATURE_NOT_SUPPORTED);
  }

  // If updates of pk-only indexes can be issued from CQL proxy directly, do it. Otherwise, add
  // them to the list of indexes to be updated from tserver.
  if (!tnode->pk_only_indexes().empty()) {
    if (UpdateIndexesLocally(tnode, *req)) {
      RETURN_NOT_OK(ApplyIndexWriteOps(tnode, *req));
    } else {
      for (const auto& index_id : tnode->pk_only_indexes()) {
        req->add_update_index_ids(index_id.first);
      }
    }
  }

  // Add non-pk-only indexes to the list of indexes to be updated from tserver also.
  for (const auto& index_id : tnode->non_pk_only_indexes()) {
    req->add_update_index_ids(index_id);
  }

  // For update/delete, check if it just deletes some columns. If so, add the rest columns to be
  // read so that tserver can check if they are all null also, in which case the row will be
  // removed after the DML.
  if ((req->type() == QLWriteRequestPB::QL_STMT_UPDATE ||
       req->type() == QLWriteRequestPB::QL_STMT_DELETE) &&
      !req->column_values().empty()) {
    bool all_null = true;
    std::set<int32> column_dels;
    for (const QLColumnValuePB& column_value : req->column_values()) {
      if (column_value.has_expr() &&
          column_value.expr().has_value() &&
          !IsNull(column_value.expr().value())) {
        all_null = false;
        break;
      }
      column_dels.insert(column_value.column_id());
    }
    if (all_null) {
      const Schema& schema = tnode->table()->InternalSchema();
      const MCSet<int32>& column_refs = tnode->column_refs();
      for (size_t idx = schema.num_key_columns(); idx < schema.num_columns(); idx++) {
        const int32 column_id = schema.column_id(idx);
        if (!schema.column(idx).is_static() &&
            column_refs.count(column_id) != 0 &&
            column_dels.count(column_id) != 0) {
          req->mutable_column_refs()->add_ids(column_id);
        }
      }
    }
  }

  if (!req->update_index_ids().empty()) {
    RETURN_NOT_OK(ql_env_->PrepareChildTransaction(req->mutable_child_transaction_data()));
  }
  return Status::OK();
}

// Apply the write operations to update the pk-only indexes.
Status Executor::ApplyIndexWriteOps(const PTDmlStmt *tnode, const QLWriteRequestPB& req) {
  const Schema& schema = tnode->table()->InternalSchema();
  const bool is_upsert = (req.type() == QLWriteRequestPB::QL_STMT_INSERT ||
                          req.type() == QLWriteRequestPB::QL_STMT_UPDATE);
  // Populate a column-id to value map.
  std::unordered_map<ColumnId, const QLExpressionPB&> values;
  for (size_t i = 0; i < schema.num_hash_key_columns(); i++) {
    values.emplace(schema.column_id(i), req.hashed_column_values(i));
  }
  for (size_t i = 0; i < schema.num_range_key_columns(); i++) {
    values.emplace(schema.column_id(schema.num_hash_key_columns() + i), req.range_column_values(i));
  }
  if (is_upsert) {
    for (const auto& column_value : req.column_values()) {
      values.emplace(ColumnId(column_value.column_id()), column_value.expr());
    }
  }

  // Create the write operation for each index and populate it using the original operation.
  ExecContext& parent = exec_context();
  for (const auto& pair : tnode->pk_only_indexes()) {
    const IndexInfo* index = VERIFY_RESULT(FindIndex(tnode->table()->index_map(), pair.first));
    const shared_ptr<YBTable>& index_table = pair.second;
    shared_ptr<YBqlWriteOp> index_op(
        is_upsert ? index_table->NewQLInsert() : index_table->NewQLDelete());
    QLWriteRequestPB *index_req = index_op->mutable_request();
    index_req->set_request_id(req.request_id());
    index_req->set_query_id(req.query_id());
    for (size_t i = 0; i < index->columns().size(); i++) {
      const ColumnId indexed_column_id = index->column(i).indexed_column_id;
      if (i < index->hash_column_count()) {
        *index_req->add_hashed_column_values() = values.at(indexed_column_id);
      } else if (i < index->key_column_count()) {
        *index_req->add_range_column_values() = values.at(indexed_column_id);
      } else if (is_upsert) {
        const auto itr = values.find(indexed_column_id);
        if (itr != values.end()) {
          QLColumnValuePB* column_value = index_req->add_column_values();
          column_value->set_column_id(index->column(i).column_id);
          *column_value->mutable_expr() = itr->second;
        }
      }
    }
    exec_contexts_.emplace_back(parent, tnode);
    RETURN_NOT_OK(exec_contexts_.back().Apply(index_op, DeferOperation(tnode, index_op)));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

bool Executor::DeferOperation(const PTDmlStmt *tnode, const YBqlWriteOpPtr& op) {
  // Defer the given write operation if 2 conditions are met:
  // 1) It fails to be added to batched_writes_ because the primary / hash key collides with that
  //    of a prior write operation in the current write batch, and
  // 2) It requires a read. We need to defer this latter colliding write operation that requires
  //    a read because currently we cannot read the results of a prior write operation to the same
  //    primary / hash key until the prior write batch has been applied in tserver. We need to defer
  //    the operation to the next batch.
  // If the latter write operation collides with the prior one but does not require a read, it is
  // okay to apply in the same batch. Our semantics allows the latter write operation to overwrite
  // the prior one.
  const bool has_usertimestamp = op->request().has_user_timestamp_usec();
  const bool defer =
      (tnode->ReadsPrimaryKey(has_usertimestamp) && batched_writes_by_primary_key_.count(op) > 0) ||
      (tnode->ReadsHashKey(has_usertimestamp) && batched_writes_by_hash_key_.count(op) > 0);

  if (!defer) {
    if (tnode->ModifiesPrimaryKey()) batched_writes_by_primary_key_.insert(op);
    if (tnode->ModifiesHashKey()) batched_writes_by_hash_key_.insert(op);
  }

  return defer;
}

Status Executor::ApplyOperation(const PTDmlStmt *tnode, const YBqlWriteOpPtr& op) {
  RETURN_NOT_OK(exec_context().Apply(op, DeferOperation(tnode, op)));

  return UpdateIndexes(tnode, op->mutable_request());
}

//--------------------------------------------------------------------------------------------------

void Executor::CommitDone(const Status &s) {
  StatementExecuted(s);
}

Status Executor::ProcessStatementStatus(const ParseTree &parse_tree, const Status& s) {
  if (PREDICT_FALSE(!s.ok() && s.IsQLError() && !parse_tree.reparsed())) {
    // If execution fails because the statement was analyzed with stale metadata cache, the
    // statement needs to be reparsed and re-analyzed. Symptoms of stale metadata are as listed
    // below. Expand the list in future as new cases arise.
    // - TABLET_NOT_FOUND when the tserver fails to execute the YBQLOp because the tablet is not
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
        errcode == ErrorCode::INVALID_ARGUMENTS ||
        errcode == ErrorCode::TABLE_NOT_FOUND ||
        errcode == ErrorCode::TYPE_NOT_FOUND) {
      parse_tree.ClearAnalyzedTableCache(ql_env_);
      parse_tree.ClearAnalyzedUDTypeCache(ql_env_);
      parse_tree.set_stale();
      return ErrorStatus(ErrorCode::STALE_METADATA);
    }
  }
  return s;
}

Status Executor::ProcessOpResponse(client::YBqlOp* op,
                                   const TreeNode* tnode,
                                   ExecContext* exec_context) {
  const QLResponsePB &resp = op->response();
  CHECK(resp.has_status()) << "QLResponsePB status missing";
  if (resp.status() != QLResponsePB::YQL_STATUS_OK) {
    const ErrorCode errcode = QLStatusToErrorCode(resp.status());
    return exec_context->Error(tnode, resp.error_message().c_str(), errcode);
  }
  return op->rows_data().empty() ? Status::OK() : AppendResult(std::make_shared<RowsResult>(op));
}

Status Executor::ProcessAsyncResults(const Status& s) {
  RETURN_NOT_OK(s);
  for (auto& exec_context : exec_contexts_) {
    client::YBqlOp* op = exec_context.op().get();
    const TreeNode* tnode = exec_context.tnode();
    if (op == nullptr || exec_context.IsOperationDeferred()) {
      continue; // Skip empty or deferred op.
    }
    Status ss = ql_env_->GetOpError(op);
    if (PREDICT_FALSE(!ss.ok() && !ss.IsTryAgain())) {
      // YBOperation returns not-found error when the tablet is not found.
      const auto error_code = ss.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::EXEC_ERROR;
      ss = exec_context.Error(tnode, ss, error_code);
    }
    if (ss.ok()) {
      ss = ProcessOpResponse(op, tnode, &exec_context);
    }
    RETURN_NOT_OK(ProcessStatementStatus(*exec_context.parse_tree(), ss));
  }
  return Status::OK();
}

Status Executor::AppendResult(const RowsResult::SharedPtr& result) {
  if (result == nullptr) {
    return Status::OK();
  }
  if (result_ == nullptr) {
    result_ = result;
    return Status::OK();
  }
  CHECK(result_->type() == ExecutedResult::Type::ROWS);
  return std::static_pointer_cast<RowsResult>(result_)->Append(*result);
}

void Executor::StatementExecuted(const Status& s) {
  // Update metrics for all statements executed.
  if (s.ok() && ql_metrics_ != nullptr) {
    const MonoTime now = MonoTime::Now();
    MonoTime transaction_start_time;
    for (const auto& exec_context : exec_contexts_) {
      const TreeNode* tnode = exec_context.tnode();
      if (tnode != nullptr) {
        switch (tnode->opcode()) {
          case TreeNodeOpcode::kPTSelectStmt: FALLTHROUGH_INTENDED;
          case TreeNodeOpcode::kPTInsertStmt: FALLTHROUGH_INTENDED;
          case TreeNodeOpcode::kPTUpdateStmt: FALLTHROUGH_INTENDED;
          case TreeNodeOpcode::kPTDeleteStmt: FALLTHROUGH_INTENDED;
          case TreeNodeOpcode::kPTListNode:
            // The metrics for SELECT/INSERT/UPDATE/DELETE have been updated when the ops have
            // been completed in FlushAsyncDone(). Exclude PTListNode also as we are interested
            // in the metrics of its consistuent DMLs only.
            break;
          case TreeNodeOpcode::kPTStartTransaction:
            transaction_start_time = exec_context.start_time();
            break;
          case TreeNodeOpcode::kPTCommit: {
            const auto delta_usec = (now - transaction_start_time).ToMicroseconds();
            ql_metrics_->ql_transaction_->Increment(delta_usec);
            break;
          }
          default: {
            const auto delta_usec = (now - exec_context.start_time()).ToMicroseconds();
            ql_metrics_->ql_others_->Increment(delta_usec);
            ql_metrics_->time_to_execute_ql_query_->Increment(delta_usec);
            break;
          }
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
  batched_writes_by_primary_key_.clear();
  batched_writes_by_hash_key_.clear();
  result_ = nullptr;
  cb_.Reset();
  ql_env_->Reset();
}

}  // namespace ql
}  // namespace yb
