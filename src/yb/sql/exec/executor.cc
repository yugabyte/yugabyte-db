//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/common/partition.h"

namespace yb {
namespace sql {

using std::string;
using std::shared_ptr;

using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableType;
using client::YBSqlWriteOp;
using client::YBSqlReadOp;

//--------------------------------------------------------------------------------------------------

Executor::Executor() {
}

Executor::~Executor() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::Execute(const string& sql_stmt,
                                 ParseTree::UniPtr parse_tree,
                                 SqlEnv *sql_env) {
  // Prepare execution context.
  ParseTree *ptree = parse_tree.get();
  exec_context_ = ExecContext::UniPtr(new ExecContext(sql_stmt.c_str(),
                                                      sql_stmt.length(),
                                                      move(parse_tree),
                                                      sql_env));

  // Execute the parse tree.
  if (!ExecPTree(ptree).ok()) {
    // Before leaving the execution step, collect all errors and place them in return status.
    VLOG(3) << "Failed to execute parse-tree <" << ptree << ">";
    return exec_context_->GetStatus();
  }

  VLOG(3) << "Successfully executed parse-tree <" << ptree << ">";
  return Status::OK();
}

ParseTree::UniPtr Executor::Done() {
  // When releasing the parse tree, we must free the context because it has references to the tree
  // which doesn't belong to this context any longer.
  ParseTree::UniPtr ptree = exec_context_->AcquireParseTree();
  exec_context_ = nullptr;
  return ptree;
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTree(const ParseTree *ptree) {
  return ExecTreeNode(ptree->root().get());
}

CHECKED_STATUS Executor::ExecTreeNode(const TreeNode *tnode) {
  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTListNode:
      return ExecPTNode(static_cast<const PTListNode*>(tnode));

    case TreeNodeOpcode::kPTCreateTable:
      return ExecPTNode(static_cast<const PTCreateTable*>(tnode));

    case TreeNodeOpcode::kPTDropStmt:
      return ExecPTNode(static_cast<const PTDropStmt*>(tnode));

    case TreeNodeOpcode::kPTSelectStmt:
      return ExecPTNode(static_cast<const PTSelectStmt*>(tnode));

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNode(static_cast<const PTInsertStmt*>(tnode));

    case TreeNodeOpcode::kPTDeleteStmt:
      return ExecPTNode(static_cast<const PTDeleteStmt*>(tnode));

    case TreeNodeOpcode::kPTUpdateStmt:
      return ExecPTNode(static_cast<const PTUpdateStmt*>(tnode));

    default:
      return ExecPTNode(tnode);
  }
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const TreeNode *tnode) {
  return exec_context_->Error(tnode->loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTListNode *lnode) {
  for (TreeNode::SharedPtr nodeptr : lnode->node_list()) {
    RETURN_NOT_OK(ExecTreeNode(nodeptr.get()));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTCreateTable *tnode) {
  const char *table_name = tnode->yb_table_name();

  // Setting up columns.
  Status exec_status;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (const auto& column : hash_columns) {
    b.AddColumn(column->yb_name())->Type(column->sql_type())
                                  ->HashPrimaryKey()
                                  ->Order(column->order());
  }
  const MCList<PTColumnDefinition *>& primary_columns = tnode->primary_columns();
  for (const auto& column : primary_columns) {
    b.AddColumn(column->yb_name())->Type(column->sql_type())
                                  ->PrimaryKey()
                                  ->Order(column->order());
  }
  const MCList<PTColumnDefinition *>& columns = tnode->columns();
  for (const auto& column : columns) {
    b.AddColumn(column->yb_name())->Type(column->sql_type())
                                  ->Nullable()
                                  ->Order(column->order());
  }
  exec_status = b.Build(&schema);
  if (!exec_status.ok()) {
    return exec_context_->Error(tnode->columns_loc(),
                                exec_status.ToString().c_str(),
                                ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Create table.
  // TODO(neil): Number of replica should be automatically computed by the master, but it hasn't.
  // We passed '1' for now. Once server is fixed, num_replicas should be removed here.
  shared_ptr<YBTableCreator> table_creator(exec_context_->NewTableCreator());
  exec_status = table_creator->table_name(table_name).table_type(YBTableType::YSQL_TABLE_TYPE)
                                                     .schema(&schema)
                                                     .num_replicas(1)
                                                     .Create();
  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;
    if (exec_status.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (exec_status.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TABLE) {
      return Status::OK();
    }

    return exec_context_->Error(tnode->name_loc(),
                                exec_status.ToString().c_str(),
                                error_code);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTDropStmt *tnode) {
  const char* table_name = tnode->yb_table_name();

  // Drop the table.
  auto exec_status = exec_context_->DeleteTable(table_name);

  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;
    if (exec_status.IsNotFound()) {
      error_code = ErrorCode::TABLE_NOT_FOUND;
    }

    // Ignore not found error for a DROP IF EXISTS statement.
    if (tnode->drop_if_exists() && error_code == ErrorCode::TABLE_NOT_FOUND) {
      return Status::OK();
    }

    return exec_context_->Error(tnode->name_loc(),
                                exec_status.ToString().c_str(),
                                error_code);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

template<typename PBType>
CHECKED_STATUS Executor::ExprToPB(const PTExpr::SharedPtr& expr,
                                  yb::DataType col_type,
                                  PBType* col_pb,
                                  YBPartialRow *row,
                                  int col_index) {

  col_pb->mutable_value()->set_datatype(col_type);
  switch (col_type) {
    case INT8: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      int8_t actual_value = static_cast<int8_t>(int_value.value_);
      VLOG(3) << "Expr actual value = " << actual_value;
      col_pb->mutable_value()->set_int8_value(actual_value);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetInt8(col_index, actual_value));
      }
      break;
    }

    case INT16: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      int16_t actual_value = static_cast<int16_t>(int_value.value_);
      VLOG(3) << "Expr actual value = " << actual_value;
      col_pb->mutable_value()->set_int16_value(actual_value);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetInt16(col_index, actual_value));
      }
      break;
    }

    case INT32: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      int32_t actual_value = static_cast<int32_t>(int_value.value_);
      VLOG(3) << "Expr actual value = " << actual_value;
      col_pb->mutable_value()->set_int32_value(actual_value);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetInt32(col_index, actual_value));
      }
      break;
    }

    case INT64: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      int64_t actual_value = int_value.value_;
      VLOG(3) << "Expr actual value = " << actual_value;
      col_pb->mutable_value()->set_int64_value(actual_value);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetInt64(col_index, actual_value));
      }
      break;
    }

    case STRING: {
      EvalStringValue string_value;
      RETURN_NOT_OK(EvalExpr(expr, &string_value));

      col_pb->mutable_value()->set_string_value(string_value.value_->data(),
                                                string_value.value_->size());
      VLOG(3) << "Expr actual value = " << string_value.value_->c_str();
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetString(
            col_index, Slice(string_value.value_->data(), string_value.value_->size())));
      }
      break;
    }

    case FLOAT: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalExpr(expr, &double_value));

      float actual_value = double_value.value_;
      VLOG(3) << "Expr actual value = " << actual_value;
      col_pb->mutable_value()->set_float_value(actual_value);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetFloat(col_index, actual_value));
      }
      break;
    }

    case DOUBLE: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalExpr(expr, &double_value));

      double actual_value = double_value.value_;
      VLOG(3) << "Expr actual value = " << actual_value;
      col_pb->mutable_value()->set_double_value(actual_value);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetDouble(col_index, actual_value));
      }
      break;
    }

    case BOOL: {
      EvalBoolValue bool_value;
      RETURN_NOT_OK(EvalExpr(expr, &bool_value));

      col_pb->mutable_value()->set_bool_value(bool_value.value_);
      VLOG(3) << "Expr actual value = " << bool_value.value_;
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetBool(col_index, bool_value.value_));
      }
      break;
    }

    case BINARY:
      LOG(FATAL) << "BINARY type is not yet supported";
      break;

    case TIMESTAMP:
      LOG(FATAL) << "TIMESTAMP type is not yet supported";
      break;

    case UINT8: FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    default:
      LOG(FATAL) << "Not an SQL type";
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ColumnArgsToWriteRequestPB(const shared_ptr<client::YBTable>& table,
                                                    const PTDmlStmt *tnode,
                                                    YSQLWriteRequestPB *req,
                                                    YBPartialRow *row) {
  const MCVector<ColumnArg>& column_args = tnode->column_args();
  // Set the ttl.
  if (tnode->ttl_msec() != PTDmlStmt::kNoTTL) {
    req->set_ttl(tnode->ttl_msec());
  }
  for (const ColumnArg& col : column_args) {
    if (!col.IsInitialized()) {
      // This column is not assigned a value, ignore it. We don't support default value yet.
      continue;
    }

    const ColumnDesc *col_desc = col.desc();
    YSQLColumnValuePB* col_pb;

    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      col_pb = req->add_column_values();
    }

    VLOG(3) << "WRITE request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    if (col_desc->is_hash()) {
      RETURN_NOT_OK(ExprToPB<YSQLColumnValuePB>(
          col.expr(), col_desc->type_id(), col_pb, row, col_desc->index()));
    } else {
      RETURN_NOT_OK(ExprToPB<YSQLColumnValuePB>(
          col.expr(), col_desc->type_id(), col_pb));
    }
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::WhereClauseToPB(YSQLWriteRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops) {
  // Setup the key columns.
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YSQLColumnValuePB *col_pb;
    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      LOG(FATAL) << "Unexpected non primary key column in this context";
    }
    VLOG(3) << "WRITE request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(
      ExprToPB<YSQLColumnValuePB>(op.expr(), col_desc->type_id(), col_pb, row, col_desc->index()));
  }

  // Setup the rest of the columns.
  CHECK(where_ops.empty()) << "Server doesn't support range operation yet";
  for (const auto& op : where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YSQLColumnValuePB *col_pb;
    if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      col_pb = req->add_column_values();
    }
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(ExprToPB<YSQLColumnValuePB>(op.expr(), col_desc->type_id(), col_pb));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereClauseToPB(YSQLReadRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops) {
  // Setup the hash key columns.
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YSQLColumnValuePB *col_pb;
    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else {
      LOG(FATAL) << "Unexpected non partition column in this context";
    }
    VLOG(3) << "READ request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(ExprToPB<YSQLColumnValuePB>(op.expr(),
                                              col_desc->type_id(),
                                              col_pb,
                                              row,
                                              col_desc->index()));
  }

  // Not generate any code if where clause is empty.
  if (where_ops.empty()) {
    return Status::OK();
  }

  // Setup the rest of the where clause.
  YSQLConditionPB *current_cond = req->mutable_where_condition();
  for (const auto& col_op : where_ops) {
    if (&col_op == &where_ops.back()) {
      // This is the last operator. Use the current ConditionPB.
      RETURN_NOT_OK(WhereOpToPB(current_cond, col_op));

    } else {
      // Current ConditionPB would be AND of this op and the next one.
      current_cond->set_op(YSQL_OP_AND);
      YSQLExpressionPB *op = current_cond->add_operands();
      RETURN_NOT_OK(WhereOpToPB(op->mutable_condition(), col_op));

      // Create a new the ConditionPB for the next operand.
      current_cond = current_cond->add_operands()->mutable_condition();
    }
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereOpToPB(YSQLConditionPB *condition, const ColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  YSQLExpressionPB *op = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  op->set_column_id(col_desc->id());

  // Operand 2: The expression.
  op = condition->add_operands();
  return ExprToPB<YSQLExpressionPB>(col_op.expr(), col_desc->type_id(), op);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTSelectStmt *tnode) {
  shared_ptr<YBSqlReadOp> select_op;

  if (!tnode->is_system()) {

    // Create the read request.
    const shared_ptr<client::YBTable>& table = tnode->table();
    select_op.reset(table->NewYSQLSelect());
    YSQLReadRequestPB *req = select_op->mutable_request();

    // Where clause - Hash, range, and regular columns.
    YBPartialRow *row = select_op->mutable_row();

    RETURN_NOT_OK(WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops()));

    // Specify selected columns.
    for (const ColumnDesc *col_desc : tnode->selected_columns()) {
      req->add_column_ids(col_desc->id());
    }
  }

  // Apply the operator always even when select_op is "null" so that the last read_op saved in
  // exec_context is always cleared.
  RETURN_NOT_OK(exec_context_->ApplyRead(select_op, tnode));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTInsertStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBSqlWriteOp> insert_op(table->NewYSQLInsert());

  // Set the values for columns.
  Status s = ColumnArgsToWriteRequestPB(table,
                                        tnode,
                                        insert_op->mutable_request(),
                                        insert_op->mutable_row());
  if (!s.ok()) {
    return exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS);
  }

  // Apply the operator.
  RETURN_NOT_OK(exec_context_->ApplyWrite(insert_op, tnode));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTDeleteStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBSqlWriteOp> delete_op(table->NewYSQLDelete());
  YSQLWriteRequestPB *req = delete_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  YBPartialRow *row = delete_op->mutable_row();
  RETURN_NOT_OK(WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops()));

  // Apply the operator.
  RETURN_NOT_OK(exec_context_->ApplyWrite(delete_op, tnode));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ExecPTNode(const PTUpdateStmt *tnode) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBSqlWriteOp> update_op(table->NewYSQLUpdate());
  YSQLWriteRequestPB *req = update_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  YBPartialRow *row = update_op->mutable_row();
  RETURN_NOT_OK(WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops()));

  // Setup the columns' new values.
  RETURN_NOT_OK(ColumnArgsToWriteRequestPB(table,
                                           tnode,
                                           update_op->mutable_request(),
                                           update_op->mutable_row()));

  // Apply the operator.
  RETURN_NOT_OK(exec_context_->ApplyWrite(update_op, tnode));
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
