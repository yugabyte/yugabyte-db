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

using client::YBColumnSpec;
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
    const string &sql_stmt, const ParseTree &parse_tree, const StatementParameters& params,
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
  DCHECK_ONLY_NOTNULL(tnode);

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
    if (exec_context_->CurrentKeyspace().empty()) {
      CB_RETURN(cb, exec_context_->Error(tnode->name_loc(), ErrorCode::NO_NAMESPACE_USED));
    }

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
    YBColumnSpec *column_spec = b.AddColumn(column->yb_name())->Type(column->yql_type())
                                                              ->Nullable()
                                                              ->Order(column->order());
    if (column->is_static()) {
      column_spec->StaticColumn();
    }
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
  cb.Run(
      Status::OK(),
      std::make_shared<SchemaChangeResult>(
          "CREATED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name()));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTDropStmt *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  Status exec_status;
  ErrorCode error_not_found = ErrorCode::EXEC_ERROR;
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
      exec_status = exec_context_->DeleteTable(table_name);
      error_not_found = ErrorCode::TABLE_NOT_FOUND;
      result = std::make_shared<SchemaChangeResult>(
          "DROPPED", "TABLE", table_name.resolved_namespace_name(), table_name.table_name());
      break;
    }

    case OBJECT_SCHEMA:
      // Drop the keyspace.
      exec_status = exec_context_->DeleteKeyspace(tnode->name());
      error_not_found = ErrorCode::KEYSPACE_NOT_FOUND;
      result = std::make_shared<SchemaChangeResult>("DROPPED", "KEYSPACE", tnode->name());
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

  cb.Run(Status::OK(), result);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::SetupPartialRow(const ColumnDesc *col_desc,
                                         const YQLExpressionPB *expr_pb,
                                         YBPartialRow *row) {
  DCHECK(expr_pb->has_value()) << "Expecting literals for hash columns";

  const YQLValuePB& value_pb = expr_pb->value();
  if (YQLValue::IsNull(value_pb)) {
    return Status::OK();
  }

  switch (YQLValue::type(value_pb)) {
    case InternalType::kInt8Value:
      RETURN_NOT_OK(row->SetInt8(col_desc->index(), YQLValue::int8_value(value_pb)));
      break;
    case InternalType::kInt16Value:
      RETURN_NOT_OK(row->SetInt16(col_desc->index(), YQLValue::int16_value(value_pb)));
      break;
    case InternalType::kInt32Value:
      RETURN_NOT_OK(row->SetInt32(col_desc->index(), YQLValue::int32_value(value_pb)));
      break;
    case InternalType::kInt64Value:
      RETURN_NOT_OK(row->SetInt64(col_desc->index(), YQLValue::int64_value(value_pb)));
      break;
    case InternalType::kDecimalValue: {
      const string& decimal_value = YQLValue::decimal_value(value_pb);
      RETURN_NOT_OK(row->SetDecimal(col_desc->index(),
                                    Slice(decimal_value.data(), decimal_value.size())));
      break;
    }
    case InternalType::kStringValue:
      RETURN_NOT_OK(row->SetString(col_desc->index(), YQLValue::string_value(value_pb)));
      break;
    case InternalType::kTimestampValue:
      RETURN_NOT_OK(row->SetTimestamp(col_desc->index(),
                                      YQLValue::timestamp_value(value_pb).ToInt64()));
      break;
    case InternalType::kInetaddressValue: {
      std::string bytes;
      RETURN_NOT_OK(YQLValue::inetaddress_value(value_pb).ToBytes(&bytes));
      RETURN_NOT_OK(row->SetInet(col_desc->index(), Slice(bytes)));
      break;
    }
    case InternalType::kUuidValue: {
      std::string bytes;
      RETURN_NOT_OK(YQLValue::uuid_value(value_pb).ToBytes(&bytes));
      RETURN_NOT_OK(row->SetUuidCopy(col_desc->index(), Slice(bytes)));
      break;
    }
    case InternalType::kTimeuuidValue: {
      std::string bytes;
      RETURN_NOT_OK(YQLValue::timeuuid_value(value_pb).ToBytes(&bytes));
      RETURN_NOT_OK(row->SetTimeUuidCopy(col_desc->index(), Slice(bytes)));
      break;
    }
    case InternalType::kBinaryValue:
      RETURN_NOT_OK(row->SetBinary(col_desc->index(), YQLValue::binary_value(value_pb)));
      break;

    case InternalType::kBoolValue: FALLTHROUGH_INTENDED;
    case InternalType::kFloatValue: FALLTHROUGH_INTENDED;
    case InternalType::kDoubleValue: FALLTHROUGH_INTENDED;
    case InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case InternalType::kListValue:
      LOG(FATAL) << "Invalid datatype for partition column";

    case InternalType::kVarintValue: FALLTHROUGH_INTENDED;
    default:
      LOG(FATAL) << "DataType not yet supported";
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
    YQLExpressionPB ttl_pb;
    RETURN_NOT_OK(PTExprToPB(tnode->ttl_seconds(), &ttl_pb));
    if (ttl_pb.has_value() && YQLValue::IsNull(ttl_pb.value())) {
      return exec_context_->Error(tnode->loc(),
                                  "TTL value cannot be null.",
                                  ErrorCode::INVALID_ARGUMENTS);
    }

    // this should be ensured by checks before getting here
    DCHECK(ttl_pb.has_value() && ttl_pb.value().has_int64_value())
        << "Integer constant expected for USING TTL clause";

    int64_t ttl_seconds = ttl_pb.value().int64_value();

    if (!yb::common::IsValidTTLSeconds(ttl_seconds)) {
      return exec_context_->Error(tnode->ttl_seconds()->loc(),
          strings::Substitute("Valid ttl range : [$0, $1]",
              yb::common::kMinTtlSeconds,
              yb::common::kMaxTtlSeconds).c_str(),
          ErrorCode::INVALID_ARGUMENTS);
    }
    req->set_ttl(static_cast<uint64_t>(ttl_seconds * MonoTime::kMillisecondsPerSecond));
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
    YQLExpressionPB *expr_pb = col_pb->mutable_expr();
    if (col_desc->is_hash()) {
      RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
      RETURN_NOT_OK(SetupPartialRow(col_desc, expr_pb, row));
    } else {
      RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
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
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
    RETURN_NOT_OK(SetupPartialRow(col_desc, col_pb->mutable_expr(), row));
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
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereClauseToPB(YQLReadRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops,
                                         const MCList<PartitionKeyOp>& partition_key_ops) {

  // Setup the lower/upper bounds on the partition key -- if any
  for (const auto& op : partition_key_ops) {
    YQLExpressionPB hash_code_pb;
    RETURN_NOT_OK(PTExprToPB(op.expr(), &hash_code_pb));
    DCHECK(hash_code_pb.has_value()) << "Integer constant expected";

    // TODO(mihnea) check for overflow
    uint16_t hash_code = static_cast<uint16_t>(hash_code_pb.value().int64_value());
    // internally we use [start, end) intervals -- start-inclusive, end-exclusive
    switch (op.yb_op()) {
      case YQL_OP_GREATER_THAN:
        req->set_hash_code(hash_code + 1);
        break;
      case YQL_OP_GREATER_THAN_EQUAL:
        req->set_hash_code(hash_code);
        break;
      case YQL_OP_LESS_THAN:
        req->set_max_hash_code(hash_code);
        break;
      case YQL_OP_LESS_THAN_EQUAL:
        req->set_max_hash_code(hash_code + 1);
        break;
      case YQL_OP_EQUAL:
        req->set_hash_code(hash_code);
        req->set_max_hash_code(hash_code + 1);
        break;

      default:
        LOG(FATAL) << "Unsupported operator for token-based partition key condition";
    }
  }

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
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
    RETURN_NOT_OK(SetupPartialRow(col_desc, col_pb->mutable_expr(), row));
  }

  // Not generate any code if where clause is empty.
  if (where_ops.empty()) {
    return Status::OK();
  }

  // Setup the rest of the where clause.
  YQLConditionPB *cond_pb = req->mutable_where_expr()->mutable_condition();
  for (const auto& col_op : where_ops) {
    if (&col_op == &where_ops.back()) {
      // This is the last operator. Use the current ConditionPB.
      RETURN_NOT_OK(WhereOpToPB(cond_pb, col_op));

    } else {
      // Current ConditionPB would be AND of this op and the next one.
      cond_pb->set_op(YQL_OP_AND);
      YQLExpressionPB *op = cond_pb->add_operands();
      RETURN_NOT_OK(WhereOpToPB(op->mutable_condition(), col_op));

      // Create a new the ConditionPB for the next operand.
      cond_pb = cond_pb->add_operands()->mutable_condition();
    }
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  YQLExpressionPB *expr_pb = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  expr_pb->set_column_id(col_desc->id());

  // Operand 2: The expression.
  expr_pb = condition->add_operands();
  return PTExprToPB(col_op.expr(), expr_pb);
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

  Status st = WhereClauseToPB(req, row, tnode->key_where_ops(), tnode->where_ops(),
                              tnode->partition_key_ops());
  if (!st.ok()) {
    CB_RETURN(
        cb,
        exec_context_->Error(tnode->loc(), st.ToString().c_str(), ErrorCode::INVALID_ARGUMENTS));
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

  // Specify selected columns.
  for (const ColumnDesc *col_desc : tnode->selected_columns()) {
    req->add_column_ids(col_desc->id());
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

  // Apply the operator. Call SelectAsyncDone when done to try to fetch more rows and buffer locally
  // before returning the result to the client.
  exec_context_->ApplyReadAsync(select_op, tnode,
                                Bind(&Executor::SelectAsyncDone, Unretained(this), tnode, cb,
                                     current_result));
}

void Executor::SelectAsyncDone(
    const PTSelectStmt *tnode, StatementExecutedCallback cb, RowsResult::SharedPtr current_result,
    const Status &s, ExecutedResult::SharedPtr new_result) {

  // If an error occurs, return current result if present and ignore the error. Otherwise, return
  // the error.
  if (PREDICT_FALSE(!s.ok())) {
    cb.Run(current_result != nullptr ? Status::OK() : s, current_result);
    return;
  }

  // Process the new result.
  if (new_result != nullptr) {
    // If there is a new non-rows result. Return the current result if present (shouldn't happend)
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
    ExecPTNodeAsync(tnode, cb, current_result);
    return;
  }

  cb.Run(Status::OK(), current_result);
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
    s = PTExprToPB(tnode->if_clause(), insert_op->mutable_request()->mutable_if_expr());
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
    Status s = PTExprToPB(tnode->if_clause(), delete_op->mutable_request()->mutable_if_expr());
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
    Status s = PTExprToPB(tnode->if_clause(), update_op->mutable_request()->mutable_if_expr());
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

  cb.Run(Status::OK(), std::make_shared<SchemaChangeResult>("CREATED", "KEYSPACE", tnode->name()));
}

//--------------------------------------------------------------------------------------------------

void Executor::ExecPTNodeAsync(const PTUseKeyspace *tnode, StatementExecutedCallback cb) {
  DCHECK_NOTNULL(exec_context_.get());
  const Status exec_status = exec_context_->UseKeyspace(tnode->name());

  if (!exec_status.ok()) {
    ErrorCode error_code = ErrorCode::EXEC_ERROR;

    if(exec_status.IsNotFound()) {
      error_code = ErrorCode::KEYSPACE_NOT_FOUND;
    }

    CB_RETURN(cb, exec_context_->Error(tnode->loc(), exec_status.ToString().c_str(), error_code));
  }
  cb.Run(Status::OK(), std::make_shared<SetKeyspaceResult>(tnode->name()));
}

}  // namespace sql
}  // namespace yb
