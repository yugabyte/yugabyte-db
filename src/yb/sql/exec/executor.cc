//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/client/callbacks.h"
#include "yb/common/partition.h"
#include "yb/sql/sql_processor.h"
#include "yb/util/decimal.h"

namespace yb {
namespace sql {

using std::string;
using std::shared_ptr;

using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableCreator;
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

Executor::Executor(const SqlMetrics* sql_metrics) : sql_metrics_(sql_metrics) {
}

Executor::~Executor() {
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecuteAsync(
    const string &sql_stmt, const ParseTree &parse_tree, const StatementParameters &params,
    SqlEnv *sql_env, StatementExecutedCallback cb) {
  // Prepare execution context.
  exec_context_ = ExecContext::UniPtr(new ExecContext(sql_stmt.c_str(),
                                                      sql_stmt.length(),
                                                      sql_env));
  params_ = &params;
  // Execute the parse tree.
  ExecPTreeAsync(
      parse_tree, Bind(&Executor::ExecuteDone, Unretained(this), Unretained(&parse_tree),
      MonoTime::Now(MonoTime::FINE), cb));
}

void Executor::ExecuteDone(
    const ParseTree *ptree, MonoTime start, StatementExecutedCallback cb, const Status &s,
    ExecutedResult::SharedPtr result) {
  if (!s.ok()) {
    // Before leaving the execution step, collect all errors and place them in return status.
    VLOG(3) << "Failed to execute parse-tree <" << ptree << ">";
    CB_RETURN(cb, exec_context_->GetStatus());
  } else {
    VLOG(3) << "Successfully executed parse-tree <" << ptree << ">";
    if (sql_metrics_ != nullptr) {
      MonoDelta delta = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start);
      const auto* lnode = static_cast<const PTListNode *>(ptree->root().get());
      if (lnode->size() > 0) {
        switch (lnode->element(0)->opcode()) {
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
    cb.Run(Status::OK(), result);
  }
}

void Executor::Done() {
  exec_context_ = nullptr;
  params_ = nullptr;
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTreeAsync(const ParseTree &ptree, StatementExecutedCallback cb) {
  ExecTreeNodeAsync(ptree.root().get(), cb);
}

void Executor::ExecTreeNodeAsync(const TreeNode *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(tnode);

  switch (tnode->opcode()) {
    case TreeNodeOpcode::kPTListNode:
      return ExecPTNodeAsync(static_cast<const PTListNode *>(tnode), cb);

    case TreeNodeOpcode::kPTCreateTable:
      return ExecPTNodeAsync(static_cast<const PTCreateTable *>(tnode), cb);

    case TreeNodeOpcode::kPTDropStmt:
      return ExecPTNodeAsync(static_cast<const PTDropStmt *>(tnode), cb);

    case TreeNodeOpcode::kPTSelectStmt:
      return ExecPTNodeAsync(static_cast<const PTSelectStmt *>(tnode), cb);

    case TreeNodeOpcode::kPTInsertStmt:
      return ExecPTNodeAsync(static_cast<const PTInsertStmt *>(tnode), cb);

    case TreeNodeOpcode::kPTDeleteStmt:
      return ExecPTNodeAsync(static_cast<const PTDeleteStmt *>(tnode), cb);

    case TreeNodeOpcode::kPTUpdateStmt:
      return ExecPTNodeAsync(static_cast<const PTUpdateStmt *>(tnode), cb);

    case TreeNodeOpcode::kPTCreateKeyspace:
      return ExecPTNodeAsync(static_cast<const PTCreateKeyspace *>(tnode), cb);

    case TreeNodeOpcode::kPTUseKeyspace:
      return ExecPTNodeAsync(static_cast<const PTUseKeyspace *>(tnode), cb);

    default:
      return ExecPTNodeAsync(tnode, cb);
  }
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const TreeNode *tnode, StatementExecutedCallback cb) {
  CB_RETURN(cb, exec_context_->Error(tnode->loc(), ErrorCode::FEATURE_NOT_SUPPORTED));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTListNode *lnode, StatementExecutedCallback cb, int idx) {
  // Done if the list is empty.
  if (lnode->size() == 0) {
    CB_RETURN(cb, Status::OK());
  }
  ExecTreeNodeAsync(
      lnode->element(idx).get(),
      Bind(&Executor::PTNodeAsyncDone, Unretained(this), Unretained(lnode), idx, cb));
}

void Executor::PTNodeAsyncDone(
    const PTListNode *lnode, int index, StatementExecutedCallback cb, const Status &s,
    ExecutedResult::SharedPtr result) {
  CB_RETURN_NOT_OK(cb, s);
  cb.Run(Status::OK(), result);
  if (++index < lnode->size()) {
    ExecPTNodeAsync(lnode, cb, index);
  }
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTCreateTable *tnode, StatementExecutedCallback cb) {
  YBTableName table_name = tnode->yb_table_name();

  if (!table_name.has_namespace()) {
    table_name.set_namespace_name(exec_context_->CurrentKeyspace());
  }

  if (table_name.is_system() && client::FLAGS_yb_system_namespace_readonly) {
    CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::SYSTEM_NAMESPACE_READONLY));
  }

  // Setting up columns.
  Status exec_status;
  YBSchema schema;
  YBSchemaBuilder b;

  const MCList<PTColumnDefinition *>& hash_columns = tnode->hash_columns();
  for (const auto& column : hash_columns) {
    if (column->sorting_type() != ColumnSchema::SortingType::kNotSpecified) {
      CB_RETURN(
          cb, exec_context_->Error(
                  tnode->columns_loc(), exec_status.ToString().c_str(),
                  ErrorCode::INVALID_TABLE_DEFINITION));
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
                  tnode->columns_loc(), exec_status.ToString().c_str(),
                  ErrorCode::INVALID_TABLE_DEFINITION));
    }
    b.AddColumn(column->yb_name())->Type(column->yql_type())
                                  ->Nullable()
                                  ->Order(column->order());
  }

  TableProperties table_properties;
  if(!tnode->ToTableProperties(&table_properties).ok()) {
    CB_RETURN(
        cb, exec_context_->Error(
                tnode->columns_loc(), exec_status.ToString().c_str(),
                ErrorCode::INVALID_TABLE_DEFINITION));
  }

  b.SetTableProperties(table_properties);

  exec_status = b.Build(&schema);
  if (!exec_status.ok()) {
    CB_RETURN(
        cb, exec_context_->Error(
                tnode->columns_loc(), exec_status.ToString().c_str(),
                ErrorCode::INVALID_TABLE_DEFINITION));
  }

  // Create table.
  shared_ptr<YBTableCreator> table_creator(exec_context_->NewTableCreator());
  exec_status = table_creator->table_name(table_name).table_type(YBTableType::YQL_TABLE_TYPE)
                              .schema(&schema)
                              .Create();
  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;
    if (exec_status.IsAlreadyPresent()) {
      error_code = ErrorCode::DUPLICATE_TABLE;
    } else if (exec_status.IsInvalidArgument()) {
      error_code = ErrorCode::INVALID_TABLE_DEFINITION;
    }

    if (tnode->create_if_not_exists() && error_code == ErrorCode::DUPLICATE_TABLE) {
      CB_RETURN(cb, Status::OK());
    }

    CB_RETURN(
        cb, exec_context_->Error(tnode->name_loc(), exec_status.ToString().c_str(), error_code));
  }
  CB_RETURN(cb, Status::OK());
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTDropStmt *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  Status exec_status;
  ErrorCode error_not_found = ErrorCode::EXEC_ERROR;

  switch (tnode->drop_type()) {
    case OBJECT_TABLE: {
      YBTableName table_name = tnode->yb_table_name();

      if (!table_name.has_namespace()) {
        table_name.set_namespace_name(exec_context_->CurrentKeyspace());
      }
      // Drop the table.
      exec_status = exec_context_->DeleteTable(table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      break;
    }

    case OBJECT_SCHEMA:
      // Drop the keyspace.
      exec_status = exec_context_->DeleteKeyspace(tnode->name());
      error_not_found = ErrorCode::KEYSPACE_NOT_FOUND;
      break;

    default:
      CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::FEATURE_NOT_SUPPORTED));
  }

  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;

    if (exec_status.IsNotFound()) {
      // Ignore not found error for a DROP IF EXISTS statement.
      if (tnode->drop_if_exists()) {
        CB_RETURN(cb, Status::OK());
      }

      error_code = error_not_found;
    }

    CB_RETURN(
        cb, exec_context_->Error(tnode->name_loc(), exec_status.ToString().c_str(), error_code));
  }

  CB_RETURN(cb, Status::OK());
}

//--------------------------------------------------------------------------------------------------

template<typename PBType>
CHECKED_STATUS Executor::ExprToPB(const PTExpr::SharedPtr& expr,
                                  YQLType col_type,
                                  PBType* col_pb,
                                  YBPartialRow *row,
                                  int col_index) {
  InternalType itype = client::YBColumnSchema::ToInternalDataType(col_type);
  switch (itype) {
    case InternalType::kInt8Value: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      if (int_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        int8_t actual_value = static_cast<int8_t>(int_value.value_);
        VLOG(3) << "Expr actual value = " << actual_value;
        YQLValue::set_int8_value(actual_value, col_pb);
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetInt8(col_index, actual_value));
        }
      }
      break;
    }

    case InternalType::kInt16Value: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      if (int_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        int16_t actual_value = static_cast<int16_t>(int_value.value_);
        VLOG(3) << "Expr actual value = " << actual_value;
        YQLValue::set_int16_value(actual_value, col_pb);
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetInt16(col_index, actual_value));
        }
      }
      break;
    }

    case InternalType::kInt32Value: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      if (int_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        int32_t actual_value = static_cast<int32_t>(int_value.value_);
        VLOG(3) << "Expr actual value = " << actual_value;
        YQLValue::set_int32_value(actual_value, col_pb);
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetInt32(col_index, actual_value));
        }
      }
      break;
    }

    case InternalType::kInt64Value: {
      EvalIntValue int_value;
      RETURN_NOT_OK(EvalExpr(expr, &int_value));

      // TODO(neil): Check for overflow and raise runtime error if needed.
      if (int_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        int64_t actual_value = int_value.value_;
        VLOG(3) << "Expr actual value = " << actual_value;
        YQLValue::set_int64_value(actual_value, col_pb);
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetInt64(col_index, actual_value));
        }
      }
      break;
    }

    case InternalType::kStringValue: {
      EvalStringValue string_value;
      RETURN_NOT_OK(EvalExpr(expr, &string_value));

      if (string_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        YQLValue::set_string_value(string_value.value_->data(),
                                   string_value.value_->size(),
                                   col_pb);
        VLOG(3) << "Expr actual value = " << string_value.value_->c_str();
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetString(
              col_index, Slice(string_value.value_->data(), string_value.value_->size())));
        }
      }
      break;
    }

    case InternalType::kFloatValue: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalExpr(expr, &double_value));

      if (double_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        float actual_value = double_value.value_;
        VLOG(3) << "Expr actual value = " << actual_value;
        YQLValue::set_float_value(actual_value, col_pb);
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetFloat(col_index, actual_value));
        }
      }
      break;
    }

    case InternalType::kDoubleValue: {
      EvalDoubleValue double_value;
      RETURN_NOT_OK(EvalExpr(expr, &double_value));

      if (double_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        double actual_value = double_value.value_;
        VLOG(3) << "Expr actual value = " << actual_value;
        YQLValue::set_double_value(actual_value, col_pb);
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetDouble(col_index, actual_value));
        }
      }
      break;
    }

    case InternalType::kBoolValue: {
      EvalBoolValue bool_value;
      RETURN_NOT_OK(EvalExpr(expr, &bool_value));

      if (bool_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        YQLValue::set_bool_value(bool_value.value_, col_pb);
        VLOG(3) << "Expr actual value = " << bool_value.value_;
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetBool(col_index, bool_value.value_));
        }
      }
      break;
    }

    case InternalType::kTimestampValue: {
      EvalTimestampValue timestamp_value;
      RETURN_NOT_OK(EvalExpr(expr, &timestamp_value));

      int64_t actual_value = timestamp_value.value_;
      VLOG(3) << "Expr actual value = " << actual_value;
      YQLValue::set_timestamp_value(actual_value, col_pb);
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetTimestamp(col_index, actual_value));
      }
      break;
    }

    case InternalType::kInetaddressValue: {
      EvalInetaddressValue inetaddress_value;
      RETURN_NOT_OK(EvalExpr(expr, &inetaddress_value));

      InetAddress &actual_value = inetaddress_value.value_;
      VLOG(3) << "Expr actual value = " << actual_value.ToString();
      YQLValue::set_inetaddress_value(actual_value, col_pb);
      std::string bytes;
      RETURN_NOT_OK(actual_value.ToBytes(&bytes));
      if (row != nullptr) {
        RETURN_NOT_OK(row->SetInet(col_index, Slice(bytes)));
      }
      break;
    }

    case InternalType::kMapValue: {
      if (row != nullptr) {
        return STATUS(NotSupported, "Cannot have collection types in key");
      }
      if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
        // This is a null node -- nothing to do.
        return Status::OK();
      }

      switch (expr->expr_op()) {
        case ExprOperator::kBindVar: {
          if (params_ == nullptr) {
            return STATUS(RuntimeError, "no bind variable supplied");
          }
          const PTBindVar *var = static_cast<const PTBindVar *>(expr.get());
          YQLValueWithPB value;
          RETURN_NOT_OK(GetBindVariable(var, &value));
          // TODO (mihnea) refactor YQLValue to avoid copying here and below
          col_pb->CopyFrom(value.value());
          break;
        }
        case ExprOperator::kCollection: {
          YQLValue::set_map_value(col_pb);
          if (expr->yql_type_id() == MAP) {
            // adding elements
            PTMapExpr *map_expr = static_cast<PTMapExpr *>(expr.get());
            for (auto &key : map_expr->keys()) {
              PBType *key_pb = YQLValue::add_map_key(col_pb);
              RETURN_NOT_OK(ExprToPB(key, col_type.params()->at(0), key_pb));
            }
            for (auto &value : map_expr->values()) {
              PBType *value_pb = YQLValue::add_map_value(col_pb);
              RETURN_NOT_OK(ExprToPB(value, col_type.params()->at(1), value_pb));
            }
          }  // else can be empty SET "{}" so nothing to do -- checking is done before getting here
          break;
        }
        default:
          LOG(FATAL) << "Unsupported expression operator for map";
      }
      break;
    }

    case InternalType::kDecimalValue: {
      EvalDecimalValue decimal_value;
      RETURN_NOT_OK(EvalExpr(expr, &decimal_value));

      if (decimal_value.is_null()) {
        VLOG(3) << "Expr actual value = null";
      } else {
        YQLValue::set_decimal_value(decimal_value.value_->data(),
                                    decimal_value.value_->size(),
                                    col_pb);
        VLOG(3) << "Expr actual value = " << decimal_value.value_->c_str();
        if (row != nullptr) {
          RETURN_NOT_OK(row->SetDecimal(col_index, Slice(decimal_value.value_->data(),
                                                         decimal_value.value_->size())));
        }
      }
      break;
    }

    case InternalType::kSetValue: {
      if (row != nullptr) {
        return STATUS(NotSupported, "Cannot have collection types in key");
      }
      if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
        // This is a null node -- nothing to do.
        return Status::OK();
      }

      switch (expr->expr_op()) {
        case ExprOperator::kBindVar: {
          if (params_ == nullptr) {
            return STATUS(RuntimeError, "no bind variable supplied");
          }
          const PTBindVar *var = static_cast<const PTBindVar *>(expr.get());
          YQLValueWithPB value;
          RETURN_NOT_OK(GetBindVariable(var, &value));
          col_pb->CopyFrom(value.value());
          break;
        }
        case ExprOperator::kCollection: {
          YQLValue::set_set_value(col_pb);
          // adding elements
          PTSetExpr *set_expr = static_cast<PTSetExpr *>(expr.get());
          for (auto &elem : set_expr->elems()) {
            PBType *elem_pb = YQLValue::add_set_elem(col_pb);
            RETURN_NOT_OK(ExprToPB(elem, col_type.params()->at(0), elem_pb));
          }
          break;
        }
        default:
          LOG(FATAL) << "Unsupported expression operator for set";
      }
      break;
    }

    case InternalType::kListValue: {
      if (row != nullptr) {
        return STATUS(NotSupported, "Cannot have collection types in key");
      }
      if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
        // This is a null node -- nothing to do.
        return Status::OK();
      }

      switch (expr->expr_op()) {
        case ExprOperator::kBindVar: {
          if (params_ == nullptr) {
            return STATUS(RuntimeError, "no bind variable supplied");
          }
          const PTBindVar *var = static_cast<const PTBindVar *>(expr.get());
          YQLValueWithPB value;
          RETURN_NOT_OK(GetBindVariable(var, &value));
          col_pb->CopyFrom(value.value());
          break;
        }
        case ExprOperator::kCollection: {
          YQLValue::set_list_value(col_pb);
          // adding elements
          PTListExpr *list_expr = static_cast<PTListExpr *>(expr.get());
          for (auto &elem : list_expr->elems()) {
            PBType *elem_pb = YQLValue::add_list_elem(col_pb);
            RETURN_NOT_OK(ExprToPB(elem, col_type.params()->at(0), elem_pb));
          }
          break;
        }
        default:
          LOG(FATAL) << "Unsupported expression operator for list";
      }
      break;
    }
    // TODO (hector): Add support for varint.
    case InternalType::kVarintValue:
      LOG(FATAL) << "VarInt type is not yet supported";
      break;

    case InternalType::VALUE_NOT_SET: FALLTHROUGH_INTENDED;
    default:
      LOG(FATAL) << "Not a valid type";
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ColumnArgsToWriteRequestPB(const shared_ptr<client::YBTable>& table,
                                                    const PTDmlStmt *tnode,
                                                    YQLWriteRequestPB *req,
                                                    YBPartialRow *row) {
  const MCVector<ColumnArg>& column_args = tnode->column_args();
  // Set the ttl.
  if (tnode->has_ttl()) {
    req->set_ttl(tnode->ttl_msec());
  }

  for (const ColumnArg& col : column_args) {
    if (!col.IsInitialized()) {
      // This column is not assigned a value, ignore it. We don't support default value yet.
      continue;
    }

    const ColumnDesc *col_desc = col.desc();
    YQLColumnValuePB* col_pb;

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
      RETURN_NOT_OK(ExprToPB<YQLValuePB>(
          col.expr(), col_desc->yql_type(), col_pb->mutable_value(), row, col_desc->index()));
    } else {
      RETURN_NOT_OK(ExprToPB<YQLValuePB>(
          col.expr(), col_desc->yql_type(), col_pb->mutable_value()));
    }
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::WhereClauseToPB(YQLWriteRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops) {
  // Setup the key columns.
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
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
      ExprToPB<YQLValuePB>(op.expr(), col_desc->yql_type(), col_pb->mutable_value(), row,
          col_desc->index()));
  }

  // Setup the rest of the columns.
  CHECK(where_ops.empty()) << "Server doesn't support range operation yet";
  for (const auto& op : where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
    if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      col_pb = req->add_column_values();
    }
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(ExprToPB<YQLValuePB>(op.expr(), col_desc->yql_type(), col_pb->mutable_value()));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereClauseToPB(YQLReadRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops) {

  // Setup the hash key columns. This may be empty
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else {
      LOG(FATAL) << "Unexpected non partition column in this context";
    }
    VLOG(3) << "READ request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(ExprToPB<YQLValuePB>(op.expr(),
        col_desc->yql_type(),
                                             col_pb->mutable_value(),
                                             row,
                                             col_desc->index()));
  }

  // Not generate any code if where clause is empty.
  if (where_ops.empty()) {
    return Status::OK();
  }

  // Setup the rest of the where clause.
  YQLConditionPB *current_cond = req->mutable_where_condition();
  for (const auto& col_op : where_ops) {
    if (&col_op == &where_ops.back()) {
      // This is the last operator. Use the current ConditionPB.
      RETURN_NOT_OK(WhereOpToPB(current_cond, col_op));

    } else {
      // Current ConditionPB would be AND of this op and the next one.
      current_cond->set_op(YQL_OP_AND);
      YQLExpressionPB *op = current_cond->add_operands();
      RETURN_NOT_OK(WhereOpToPB(op->mutable_condition(), col_op));

      // Create a new the ConditionPB for the next operand.
      current_cond = current_cond->add_operands()->mutable_condition();
    }
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  YQLExpressionPB *op = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  op->set_column_id(col_desc->id());

  // Operand 2: The expression.
  op = condition->add_operands();
  return ExprToPB<YQLValuePB>(col_op.expr(), col_desc->yql_type(), op->mutable_value());
}

CHECKED_STATUS Executor::RelationalOpToPB(YQLConditionPB *condition,
                                          const YQLOperator opr,
                                          const PTExpr *relation) {
  const PTPredicate2* pred = static_cast<const PTPredicate2*>(relation);

  // Set the operator.
  condition->set_op(opr);

  // Operand 1: The column.
  const ColumnDesc *col_desc = static_cast<PTRef*>(pred->op1().get())->desc();
  YQLExpressionPB *op = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  op->set_column_id(col_desc->id());

  // Operand 2: The expression.
  const PTExpr::SharedPtr& expr = pred->op2();
  op = condition->add_operands();
  return ExprToPB<YQLValuePB>(expr, col_desc->yql_type(), op->mutable_value());
}

CHECKED_STATUS Executor::ColumnConditionToPB(YQLConditionPB *condition,
                                             const YQLOperator opr,
                                             const PTExpr *cond) {
  const PTPredicate1* pred = static_cast<const PTPredicate1*>(cond);

  // Set the operator.
  condition->set_op(opr);

  // Operand 1: The column.
  const ColumnDesc *col_desc = static_cast<PTRef*>(pred->op1().get())->desc();
  YQLExpressionPB *op = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  op->set_column_id(col_desc->id());
  return Status::OK();
}

CHECKED_STATUS Executor::BetweenToPB(YQLConditionPB *condition,
                                     const YQLOperator opr,
                                     const PTExpr *between) {
  const PTPredicate3* pred = static_cast<const PTPredicate3*>(between);

  // Set the operator.
  condition->set_op(opr);

  // Operand 1: The column.
  const ColumnDesc *col_desc = static_cast<PTRef*>(pred->op1().get())->desc();
  YQLExpressionPB *op = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  op->set_column_id(col_desc->id());

  // Operand 2: The lower-bound expression.
  const PTExpr::SharedPtr& lower_bound = pred->op2();
  op = condition->add_operands();
  RETURN_NOT_OK(ExprToPB<YQLValuePB>(lower_bound, col_desc->yql_type(), op->mutable_value()));

  // Operand 3: The upper-bound expression.
  const PTExpr::SharedPtr& upper_bound = pred->op3();
  op = condition->add_operands();
  return ExprToPB<YQLValuePB>(upper_bound, col_desc->yql_type(), op->mutable_value());
}

CHECKED_STATUS Executor::BoolExprToPB(YQLConditionPB *cond, const PTExpr* expr) {

  switch (expr->expr_op()) {

    case ExprOperator::kAND: {
      cond->set_op(YQL_OP_AND);
      const PTPredicate2 *pred = static_cast<const PTPredicate2*>(expr);
      RETURN_NOT_OK(BoolExprToPB(cond->add_operands()->mutable_condition(), pred->op1().get()));
      RETURN_NOT_OK(BoolExprToPB(cond->add_operands()->mutable_condition(), pred->op2().get()));
      break;
    }
    case ExprOperator::kOR: {
      cond->set_op(YQL_OP_OR);
      const PTPredicate2 *pred = static_cast<const PTPredicate2*>(expr);
      RETURN_NOT_OK(BoolExprToPB(cond->add_operands()->mutable_condition(), pred->op1().get()));
      RETURN_NOT_OK(BoolExprToPB(cond->add_operands()->mutable_condition(), pred->op2().get()));
      break;
    }
    case ExprOperator::kNot: {
      cond->set_op(YQL_OP_NOT);
      const PTPredicate1 *pred = static_cast<const PTPredicate1*>(expr);
      RETURN_NOT_OK(BoolExprToPB(cond->add_operands()->mutable_condition(), pred->op1().get()));
      break;
    }

    case ExprOperator::kEQ:
      RETURN_NOT_OK(RelationalOpToPB(cond, YQL_OP_EQUAL, expr));
      break;
    case ExprOperator::kLT:
      RETURN_NOT_OK(RelationalOpToPB(cond, YQL_OP_LESS_THAN, expr));
      break;
    case ExprOperator::kGT:
      RETURN_NOT_OK(RelationalOpToPB(cond, YQL_OP_GREATER_THAN, expr));
      break;
    case ExprOperator::kLE:
      RETURN_NOT_OK(RelationalOpToPB(cond, YQL_OP_LESS_THAN_EQUAL, expr));
      break;
    case ExprOperator::kGE:
      RETURN_NOT_OK(RelationalOpToPB(cond, YQL_OP_GREATER_THAN_EQUAL, expr));
      break;
    case ExprOperator::kNE:
      RETURN_NOT_OK(RelationalOpToPB(cond, YQL_OP_NOT_EQUAL, expr));
      break;

    case ExprOperator::kIsNull:
      RETURN_NOT_OK(ColumnConditionToPB(cond, YQL_OP_IS_NULL, expr));
      break;
    case ExprOperator::kIsNotNull:
      RETURN_NOT_OK(ColumnConditionToPB(cond, YQL_OP_IS_NOT_NULL, expr));
      break;
    case ExprOperator::kIsTrue:
      RETURN_NOT_OK(ColumnConditionToPB(cond, YQL_OP_IS_TRUE, expr));
      break;
    case ExprOperator::kIsFalse:
      RETURN_NOT_OK(ColumnConditionToPB(cond, YQL_OP_IS_FALSE, expr));
      break;

    case ExprOperator::kBetween:
      RETURN_NOT_OK(BetweenToPB(cond, YQL_OP_BETWEEN, expr));
      break;

    case ExprOperator::kNotBetween:
      RETURN_NOT_OK(BetweenToPB(cond, YQL_OP_NOT_BETWEEN, expr));
      break;

    case ExprOperator::kExists:
      cond->set_op(YQL_OP_EXISTS);
      break;
    case ExprOperator::kNotExists:
      cond->set_op(YQL_OP_NOT_EXISTS);
      break;

    default:
      LOG(FATAL) << "Illegal op = " << int(expr->expr_op());
      break;
  }

  return Status::OK();
}

namespace {
  void SetStringValue(const std::string& value, const size_t col_idx, YQLRow* row) {
    YQLValuePB value_pb;
    YQLValue::set_string_value(value, &value_pb);
    *(row->mutable_column(col_idx)) = value_pb;
  }

  void SetInetValue(const InetAddress& value, const size_t col_idx, YQLRow* row) {
    YQLValuePB value_pb;
    YQLValue::set_inetaddress_value(value, &value_pb);
    *(row->mutable_column(col_idx)) = value_pb;
  }

  void SetIntValue(const int32_t value, const size_t col_idx, YQLRow* row) {
    YQLValuePB value_pb;
    YQLValue::set_int32_value(value, &value_pb);
    *(row->mutable_column(col_idx)) = value_pb;
  }

  CHECKED_STATUS HandleSystemLocal(const shared_ptr<client::YBTable>& table,
                                   const ExecContext* exec_context,
                                   ExecutedResult::SharedPtr* result) {
    // Retrieve the current address of the node.
    InetAddress rpc_addr;
    InetAddress broadcast_addr;
    rpc::CQLRpcServerEnv* cql_rpcserver_env = exec_context->sql_env()->cql_rpcserver_env();
    if (cql_rpcserver_env != nullptr) {
      RETURN_NOT_OK(rpc_addr.FromString(cql_rpcserver_env->rpc_address()));
      RETURN_NOT_OK(broadcast_addr.FromString(cql_rpcserver_env->broadcast_address()));
    } else {
      // cql_rpcserver_env might be null since this codepath might be invoked without a cql proxy
      // running (ex: unit tests, ybcmd).
      RETURN_NOT_OK(rpc_addr.FromString("127.0.0.1"));
      RETURN_NOT_OK(broadcast_addr.FromString("127.0.0.1"));
    }

    // Now populate the row for system.local.
    YQLRowBlock row_block(table->InternalSchema());
    YQLRow& row = row_block.Extend();
    SetStringValue("local", 0, &row); // key
    SetStringValue("COMPLETED", 1, &row); // bootstrapped
    SetInetValue(broadcast_addr, 2, &row); // broadcast_address
    SetStringValue("local cluster", 3, &row); // cluster_name
    SetStringValue("3.4.2", 4, &row); // cql_version
    SetStringValue("datacenter", 5, &row); // data_center
    SetIntValue(0, 6, &row); // gossip_generation
    SetInetValue(broadcast_addr, 7, &row); // listen_address
    SetStringValue("4", 8, &row); // native_protocol_version
    SetStringValue("org.apache.cassandra.dht.Murmur3Partitioner", 9, &row); // partitioner
    SetStringValue("rack", 10, &row); // rack
    SetStringValue("3.9-SNAPSHOT", 11, &row); // release_version
    SetInetValue(rpc_addr, 12, &row); // rpc_address
    SetStringValue("20.1.0", 13, &row); // thrift_version

    // Serialize the row and return result.
    faststring buffer;
    row_block.Serialize(YQL_CLIENT_CQL, &buffer);
    result->reset(new RowsResult(table.get(), buffer.ToString()));
    return Status::OK();
  }
} // anonymous namespace.

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTSelectStmt *tnode, StatementExecutedCallback cb) {
  const shared_ptr<client::YBTable>& table = tnode->table();

  if (table == nullptr) {
    // If this is a system table but the table does not exist, it is okay. Just return OK with void
    // result.
    CB_RETURN(cb,
              tnode->is_system() ? Status::OK() :
              exec_context_->Error(tnode->loc(), ErrorCode::TABLE_NOT_FOUND));
  }

  if (table->name().namespace_name() == master::kSystemNamespaceName &&
      table->name().table_name() == master::kSystemLocalTableName) {
    // We can return the information for system.local directly here.
    ExecutedResult::SharedPtr result;
    CB_RETURN_NOT_OK(cb, HandleSystemLocal(table, exec_context_.get(), &result));
    cb.Run(Status::OK(), result);
    return;
  }

  // If there is a table id in the statement parameter's paging state, this is a continuation of
  // a prior SELECT statement. Verify that the same table still exists.
  const bool continue_select = !params_->table_id().empty();
  if (continue_select && params_->table_id() != table->id()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), "Table no longer exists.", ErrorCode::TABLE_NOT_FOUND));
  }

  if (params_->page_size() == 0) {
    CB_RETURN(cb, Status::OK());
  }

  // Create the read request.
  shared_ptr<YBqlReadOp> select_op(table->NewYQLSelect());
  YQLReadRequestPB *req = select_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  YBPartialRow *row = select_op->mutable_row();

  Status st = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops());
  if (!st.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), st.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Specify selected columns.
  for (const ColumnDesc *col_desc : tnode->selected_columns()) {
    req->add_column_ids(col_desc->id());
  }

  // Default row count limit is the page size. And we should return paging state when page size
  // limit is hit.
  req->set_limit(params_->page_size());
  req->set_return_paging_state(true);

  // Check if there is a limit and compute the new limit based on the number of returned rows.
  EvalIntValue limit_value;
  EvalVarIntStringValue varint_value;
  if (tnode->has_limit()) {
    const PTExpr::SharedPtr limit_expr = tnode->limit();
    CB_RETURN_NOT_OK(cb, EvalVarIntExpr(limit_expr, &varint_value));
    CB_RETURN_NOT_OK(cb, ConvertFromVarInt(&limit_value, varint_value));
    if (limit_value.value_ < 0) {
      CB_RETURN(
          cb, exec_context_->Error(
              tnode->loc(), "LIMIT clause cannot be a negative value.",
              ErrorCode::INVALID_ARGUMENTS));
    }
    if (limit_value.value_ == 0 || params_->total_num_rows_read() >= limit_value.value_) {
      CB_RETURN(cb, Status::OK());
    }

    // If the LIMIT clause, subtracting the number of rows we have returned so far, is lower than
    // the page size limit set from above, set the lower limit and do not return paging state when
    // this limit is hit.
    uint64_t limit = limit_value.value_ - params_->total_num_rows_read();
    if (limit < req->limit()) {
      req->set_limit(limit);
      req->set_return_paging_state(false);
    }
  }

  // If this is a continuation of a prior read, set the next partition key, row key and total number
  // of rows read in the request's paging state.
  if (continue_select) {
    YQLPagingStatePB *paging_state = req->mutable_paging_state();
    paging_state->set_next_partition_key(params_->next_partition_key());
    paging_state->set_next_row_key(params_->next_row_key());
    paging_state->set_total_num_rows_read(params_->total_num_rows_read());
  }

  // Apply the operator.
  exec_context_->ApplyReadAsync(select_op, tnode, cb);
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTInsertStmt *tnode, StatementExecutedCallback cb) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tnode->table();
  shared_ptr<YBqlWriteOp> insert_op(table->NewYQLInsert());

  // Set the values for columns.
  Status s = ColumnArgsToWriteRequestPB(table,
                                        tnode,
                                        insert_op->mutable_request(),
                                        insert_op->mutable_row());
  if (!s.ok()) {
    CB_RETURN(
        cb, exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    s = BoolExprToPB(insert_op->mutable_request()->mutable_if_condition(), tnode->if_clause());
    if (!s.ok()) {
      CB_RETURN(
          cb,
          exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
    }
  }

  // Apply the operator.
  exec_context_->ApplyWriteAsync(insert_op, tnode, cb);
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
  Status st = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops());
  if (!st.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), st.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    Status s = BoolExprToPB(delete_op->mutable_request()->mutable_if_condition(),
                            tnode->if_clause());
    if (!s.ok()) {
      CB_RETURN(
          cb,
          exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
    }
  }

  // Apply the operator.
  exec_context_->ApplyWriteAsync(delete_op, tnode, cb);
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
  Status st = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops());
  if (!st.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), st.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Setup the columns' new values.
  st = ColumnArgsToWriteRequestPB(table,
                                  tnode,
                                  update_op->mutable_request(),
                                  update_op->mutable_row());
  if (!st.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), st.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
  }

  // Set the IF clause.
  if (tnode->if_clause() != nullptr) {
    Status s = BoolExprToPB(update_op->mutable_request()->mutable_if_condition(),
                            tnode->if_clause());
    if (!s.ok()) {
      CB_RETURN(
          cb,
          exec_context_->Error(tnode->loc(), s.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
    }
  }

  // Apply the operator.
  exec_context_->ApplyWriteAsync(update_op, tnode, cb);
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTCreateKeyspace *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  Status exec_status = exec_context_->CreateKeyspace(tnode->name());

  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;

    if(exec_status.IsAlreadyPresent()) {
      if (tnode->create_if_not_exists()) {
        // Case: CREATE KEYSPACE IF NOT EXISTS name;
        CB_RETURN(cb, Status::OK());
      }

      error_code = ErrorCode::KEYSPACE_ALREADY_EXISTS;
    }

    CB_RETURN(cb, exec_context_->Error(tnode->loc(), exec_status.ToString().c_str(), error_code));
  }

  CB_RETURN(cb, Status::OK());
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTUseKeyspace *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  ExecutedResult::SharedPtr result;
  Status exec_status = exec_context_->UseKeyspace(tnode->name(), &result);

  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;

    if(exec_status.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    }

    CB_RETURN(cb, exec_context_->Error(tnode->loc(), exec_status.ToString().c_str(), error_code));
  }
  cb.Run(Status::OK(), result);
}

}  // namespace sql
}  // namespace yb
