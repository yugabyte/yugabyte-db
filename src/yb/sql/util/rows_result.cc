//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// RowsResult represents rows resulted from the execution of a SQL statement.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/rows_result.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"
#include "yb/common/wire_protocol.h"

namespace yb {
namespace sql {

using std::string;
using std::vector;
using std::unique_ptr;

using client::YBSqlReadOp;
using client::YBSqlWriteOp;

namespace {

vector<ColumnSchema> GetColumnSchemasFromReadOp(const YBSqlReadOp& op) {
  vector<ColumnSchema> column_schemas;
  column_schemas.reserve(op.request().column_ids_size());
  const auto& schema = op.table()->schema();
  for (const auto column_id : op.request().column_ids()) {
    const auto column = schema.ColumnById(column_id);
    column_schemas.emplace_back(column.name(), ToInternalDataType(column.type()));
  }
  return column_schemas;
}

vector<ColumnSchema> GetColumnSchemasFromWriteOp(const YBSqlWriteOp& op) {
  vector<ColumnSchema> column_schemas;
  column_schemas.reserve(op.response().column_schemas_size());
  for (const auto column_schema : op.response().column_schemas()) {
    column_schemas.emplace_back(ColumnSchemaFromPB(column_schema));
  }
  return column_schemas;
}

} // namespace

RowsResult::RowsResult(YBSqlReadOp* op)
    : table_name_(op->table()->name()),
      column_schemas_(GetColumnSchemasFromReadOp(*op)),
      rows_data_(op->rows_data()),
      client_(op->request().client()) {
}

RowsResult::RowsResult(YBSqlWriteOp* op)
    : table_name_(op->table()->name()),
      column_schemas_(GetColumnSchemasFromWriteOp(*op)),
      rows_data_(op->rows_data()),
      client_(op->request().client()) {
}

YSQLRowBlock* RowsResult::GetRowBlock() const {
  Schema schema(column_schemas_, 0);
  unique_ptr<YSQLRowBlock> rowblock(new YSQLRowBlock(schema));
  Slice data(rows_data_);
  if (!data.empty()) {
    // TODO: a better way to handle errors here?
    CHECK_OK(rowblock->Deserialize(client_, &data));
  }
  return rowblock.release();
}

} // namespace sql
} // namespace yb
