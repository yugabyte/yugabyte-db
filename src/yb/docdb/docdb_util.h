// Copyright (c) YugaByte, Inc.

// Utilities for docdb operations.

#ifndef YB_DOCDB_DOCDB_UTIL_H
#define YB_DOCDB_DOCDB_UTIL_H

#include "yb/common/schema.h"
#include "yb/common/yql_value.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
CHECKED_STATUS YQLKeyColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<YQLColumnValuePB> &column_values,
    const Schema &schema, size_t column_idx, const size_t column_count,
    vector<PrimitiveValue> *components);

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOCDB_UTIL_H
