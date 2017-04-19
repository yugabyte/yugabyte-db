// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb_util.h"

namespace yb {
namespace docdb {

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
CHECKED_STATUS YQLColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<YQLColumnValuePB>& column_values,
    const Schema& schema, size_t column_idx, const size_t column_count,
    vector<PrimitiveValue>* components) {
  for (const auto& column_value : column_values) {
    if (schema.column_id(column_idx) != column_value.column_id()) {
      return STATUS(InvalidArgument, "Primary key column id mismatch");
    }

    components->push_back(PrimitiveValue::FromYQLValuePB(
        schema.column(column_idx).type(), column_value.expr().value(),
        schema.column(column_idx).sorting_type()));
    column_idx++;
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
