//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Different results of processing a statement.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/statement_result.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"
#include "yb/common/wire_protocol.h"
#include "yb/util/pb_util.h"
#include "yb/sql/ptree/pt_select.h"

namespace yb {
namespace sql {

using std::string;
using std::vector;
using std::unique_ptr;
using strings::Substitute;

using client::YBOperation;
using client::YBqlOp;
using client::YBqlReadOp;
using client::YBqlWriteOp;

//------------------------------------------------------------------------------------------------
namespace {

// Get bind column schemas for DML.
vector<ColumnSchema> GetBindVariableSchemasFromDmlStmt(const PTDmlStmt *stmt) {
  vector<ColumnSchema> bind_variable_schemas;
  bind_variable_schemas.reserve(stmt->bind_variables().size());
  const auto& schema = stmt->table()->schema();
  for (const PTBindVar *var : stmt->bind_variables()) {
    const ColumnDesc *col_desc = var->desc();
    const auto column = schema.ColumnById(col_desc->id());
    bind_variable_schemas.emplace_back(string(var->name()->c_str()), column.type());
  }
  return bind_variable_schemas;
}

// Get column schemas from different statements / YQL ops.
vector<ColumnSchema> GetColumnSchemasFromSelectStmt(const PTSelectStmt *stmt) {
  vector<ColumnSchema> column_schemas;
  column_schemas.reserve(stmt->selected_columns().size());
  const auto& schema = stmt->table()->schema();
  for (const ColumnDesc *col_desc : stmt->selected_columns()) {
    const auto column = schema.ColumnById(col_desc->id());
    column_schemas.emplace_back(column.name(), column.type());
  }
  return column_schemas;
}

vector<ColumnSchema> GetColumnSchemasFromOp(const YBqlOp& op) {
  vector<ColumnSchema> column_schemas;
  switch (op.type()) {
    case YBOperation::Type::YQL_READ: {
      const auto& read_op = static_cast<const YBqlReadOp&>(op);
      column_schemas.reserve(read_op.request().column_ids_size());
      const auto& schema = read_op.table()->schema();
      for (const auto column_id : read_op.request().column_ids()) {
        const auto column = schema.ColumnById(column_id);
        column_schemas.emplace_back(column.name(), column.type());
      }
      return column_schemas;
    }
    case YBOperation::Type::YQL_WRITE: {
      const auto& write_op = static_cast<const YBqlWriteOp&>(op);
      column_schemas.reserve(write_op.response().column_schemas_size());
      for (const auto column_schema : write_op.response().column_schemas()) {
        column_schemas.emplace_back(ColumnSchemaFromPB(column_schema));
      }
      return column_schemas;
    }
    case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
    case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
    case YBOperation::Type::DELETE: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_WRITE:
      break;
    // default: fallthrough
  }
  LOG(FATAL) << "Internal error: invalid or unknown YQL operation: " << op.type();
}

YQLClient GetClientFromOp(const YBqlOp& op) {
  switch (op.type()) {
    case YBOperation::Type::YQL_READ:
      return static_cast<const YBqlReadOp&>(op).request().client();
    case YBOperation::Type::YQL_WRITE:
      return static_cast<const YBqlWriteOp&>(op).request().client();
    case YBOperation::Type::INSERT: FALLTHROUGH_INTENDED;
    case YBOperation::Type::UPDATE: FALLTHROUGH_INTENDED;
    case YBOperation::Type::DELETE: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_WRITE:
      break;
    // default: fallthrough
  }
  LOG(FATAL) << "Internal error: invalid or unknown YQL operation: " << op.type();
}

} // namespace

//------------------------------------------------------------------------------------------------
PreparedResult::PreparedResult(const PTDmlStmt *stmt)
    : table_name_(stmt->table()->name()),
      bind_variable_schemas_(GetBindVariableSchemasFromDmlStmt(stmt)),
      column_schemas_(stmt->opcode() == TreeNodeOpcode::kPTSelectStmt ?
                      GetColumnSchemasFromSelectStmt(static_cast<const PTSelectStmt*>(stmt)) :
                      vector<ColumnSchema>()) {
}

PreparedResult::~PreparedResult() {
}

//------------------------------------------------------------------------------------------------
RowsResult::RowsResult(YBqlOp *op)
    : table_name_(op->table()->name()),
      column_schemas_(GetColumnSchemasFromOp(*op)),
      client_(GetClientFromOp(*op)),
      rows_data_(op->rows_data()) {
  // If there is a paging state in the response, fill in the table ID also and serialize the
  // paging state as bytes.
  if (op->response().has_paging_state()) {
    YQLPagingStatePB *paging_state = op->mutable_response()->mutable_paging_state();
    paging_state->set_table_id(op->table()->id());
    faststring serialized_paging_state;
    CHECK(pb_util::SerializeToString(*paging_state, &serialized_paging_state));
    paging_state_ = serialized_paging_state.ToString();
  }
}

RowsResult::RowsResult(client::YBTable* table, const std::string& rows_data)
    : table_name_(table->name()),
      column_schemas_(table->schema().columns()),
      client_(YQLClient::YQL_CLIENT_CQL),
      rows_data_(rows_data) {
}

RowsResult::~RowsResult() {
}

Status RowsResult::Append(const RowsResult& other) {
  if (rows_data_.empty()) {
    rows_data_ = other.rows_data_;
  } else {
    RETURN_NOT_OK(YQLRowBlock::AppendRowsData(other.client_, other.rows_data_, &rows_data_));
  }
  paging_state_ = other.paging_state_;
  return Status::OK();
}

YQLRowBlock *RowsResult::GetRowBlock() const {
  Schema schema(column_schemas_, 0);
  unique_ptr<YQLRowBlock> rowblock(new YQLRowBlock(schema));
  Slice data(rows_data_);
  if (!data.empty()) {
    // TODO: a better way to handle errors here?
    CHECK_OK(rowblock->Deserialize(client_, &data));
  }
  return rowblock.release();
}

} // namespace sql
} // namespace yb
