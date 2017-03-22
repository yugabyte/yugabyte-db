//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Different results of processing a statement.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/stmt_result.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"
#include "yb/common/wire_protocol.h"
#include "yb/util/pb_util.h"

namespace yb {
namespace sql {

using std::string;
using std::vector;
using std::unique_ptr;
using strings::Substitute;

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

vector<ColumnSchema> GetColumnSchemasFromReadOp(const YBqlReadOp& op) {
  vector<ColumnSchema> column_schemas;
  column_schemas.reserve(op.request().column_ids_size());
  const auto& schema = op.table()->schema();
  for (const auto column_id : op.request().column_ids()) {
    const auto column = schema.ColumnById(column_id);
    column_schemas.emplace_back(column.name(), column.type());
  }
  return column_schemas;
}

vector<ColumnSchema> GetColumnSchemasFromWriteOp(const YBqlWriteOp& op) {
  vector<ColumnSchema> column_schemas;
  column_schemas.reserve(op.response().column_schemas_size());
  for (const auto column_schema : op.response().column_schemas()) {
    column_schemas.emplace_back(ColumnSchemaFromPB(column_schema));
  }
  return column_schemas;
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

//------------------------------------------------------------------------------------------------
RowsResult::RowsResult(YBqlReadOp* op)
    : table_name_(op->table()->name()),
      column_schemas_(GetColumnSchemasFromReadOp(*op)),
      rows_data_(op->rows_data()),
      client_(op->request().client()) {
  if (op->response().has_paging_state()) {
    YQLPagingStatePB paging_state_pb = op->response().paging_state();
    faststring paging_state_str;
    CHECK(pb_util::SerializeToString(paging_state_pb, &paging_state_str));
    paging_state_ = paging_state_str.ToString();
  }
}

RowsResult::RowsResult(YBqlWriteOp* op)
    : table_name_(op->table()->name()),
      column_schemas_(GetColumnSchemasFromWriteOp(*op)),
      rows_data_(op->rows_data()),
      client_(op->request().client()) {
}

YQLRowBlock* RowsResult::GetRowBlock() const {
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
