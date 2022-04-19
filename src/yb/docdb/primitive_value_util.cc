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
#include "yb/docdb/primitive_value_util.h"

#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/docdb/primitive_value.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::vector;

namespace yb {
namespace docdb {

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
Status QLKeyColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<QLExpressionPB> &column_values,
    const Schema &schema, size_t column_idx, const size_t column_count,
    vector<KeyEntryValue> *components) {
  for (const auto& column_value : column_values) {
    if (!schema.is_key_column(column_idx)) {
      auto status = STATUS_FORMAT(
          Corruption, "Column at $0 is not key column in $1", column_idx, schema.ToString());
      LOG(DFATAL) << status;
      return status;
    }

    if (!column_value.has_value() || IsNull(column_value.value())) {
      components->emplace_back(KeyEntryType::kNullLow);
    } else {
      components->push_back(KeyEntryValue::FromQLValuePB(
          column_value.value(), schema.column(column_idx).sorting_type()));
    }
    column_idx++;
  }
  return Status::OK();
}

namespace {

Result<KeyEntryValue> EvalExpr(
    const PgsqlExpressionPB& expr, const Schema& schema, SortingType sorting_type) {
  // TODO(neil) The current setup only works for CQL as it assumes primary key value must not
  // be dependent on any column values. This needs to be fixed as PostgreSQL expression might
  // require a read from a table.
  //
  // Use regular executor for now.
  QLExprExecutor executor;
  QLExprResult result;
  RETURN_NOT_OK(executor.EvalExpr(expr, nullptr, result.Writer(), &schema));

  return KeyEntryValue::FromQLValuePB(result.Value(), sorting_type);
}

Result<KeyEntryValue> EvalExpr(
    const LWPgsqlExpressionPB& expr, const Schema& schema, SortingType sorting_type) {
  QLExprExecutor executor;
  LWExprResult result(&expr.arena());
  RETURN_NOT_OK(executor.EvalExpr(expr, nullptr, result.Writer(), &schema));

  return KeyEntryValue::FromQLValuePB(result.Value(), sorting_type);
}

// ------------------------------------------------------------------------------------------------
template <class Col>
Result<vector<KeyEntryValue>> DoInitKeyColumnPrimitiveValues(
    const Col &column_values, const Schema &schema, size_t start_idx) {
  vector<KeyEntryValue> values;
  values.reserve(column_values.size());
  size_t column_idx = start_idx;
  for (const auto& column_value : column_values) {
    if (!schema.is_key_column(column_idx)) {
      auto status = STATUS_FORMAT(
          Corruption, "Column at $0 is not key column in $1", column_idx, schema.ToString());
      LOG(DFATAL) << status;
      return status;
    }
    const auto sorting_type = schema.column(column_idx).sorting_type();
    if (column_value.has_value()) {
      const auto& value = column_value.value();
      values.push_back(IsNull(value) ? KeyEntryValue::NullValue(sorting_type)
                                     : KeyEntryValue::FromQLValuePB(value, sorting_type));
    } else {
      values.push_back(VERIFY_RESULT(EvalExpr(column_value, schema, sorting_type)));
    }
    column_idx++;
  }
  return std::move(values);
}

} // namespace

Result<vector<KeyEntryValue>> InitKeyColumnPrimitiveValues(
    const google::protobuf::RepeatedPtrField<PgsqlExpressionPB> &column_values,
    const Schema &schema, size_t start_idx) {
  return DoInitKeyColumnPrimitiveValues(column_values, schema, start_idx);
}

Result<vector<KeyEntryValue>> InitKeyColumnPrimitiveValues(
    const ArenaList<LWPgsqlExpressionPB> &column_values, const Schema &schema, size_t start_idx) {
  return DoInitKeyColumnPrimitiveValues(column_values, schema, start_idx);
}

}  // namespace docdb
}  // namespace yb
