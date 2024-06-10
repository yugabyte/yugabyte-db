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

#include "yb/qlexpr/ql_expr_util.h"

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/qlexpr/ql_expr.h"

namespace yb::qlexpr {

namespace {

Result<dockv::KeyEntryValue> EvalExpr(
    const PgsqlExpressionPB& expr, const Schema& schema, SortingType sorting_type) {
  // TODO(neil) The current setup only works for CQL as it assumes primary key value must not
  // be dependent on any column values. This needs to be fixed as PostgreSQL expression might
  // require a read from a table.
  //
  // Use regular executor for now.
  QLExprExecutor executor;
  QLExprResult result;
  RETURN_NOT_OK(executor.EvalExpr(expr, nullptr, result.Writer(), &schema));

  return dockv::KeyEntryValue::FromQLValuePB(result.Value(), sorting_type);
}

Result<dockv::KeyEntryValue> EvalExpr(
    const LWPgsqlExpressionPB& expr, const Schema& schema, SortingType sorting_type) {
  QLExprExecutor executor;
  QLExprResult result;
  RETURN_NOT_OK(executor.EvalExpr(expr.ToGoogleProtobuf(), nullptr, result.Writer(), &schema));

  return dockv::KeyEntryValue::FromQLValuePB(result.Value(), sorting_type);
}

// ------------------------------------------------------------------------------------------------
template <class Col>
Result<std::vector<dockv::KeyEntryValue>> DoInitKeyColumnPrimitiveValues(
    const Col &column_values, const Schema &schema, size_t start_idx) {
  std::vector<dockv::KeyEntryValue> values;
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
      values.push_back(IsNull(value) ? dockv::KeyEntryValue::NullValue(sorting_type)
                                     : dockv::KeyEntryValue::FromQLValuePB(value, sorting_type));
    } else {
      values.push_back(VERIFY_RESULT(EvalExpr(column_value, schema, sorting_type)));
    }
    column_idx++;
  }
  return std::move(values);
}

} // namespace

Result<std::vector<dockv::KeyEntryValue>> InitKeyColumnPrimitiveValues(
    const google::protobuf::RepeatedPtrField<PgsqlExpressionPB> &column_values,
    const Schema &schema, size_t start_idx) {
  return DoInitKeyColumnPrimitiveValues(column_values, schema, start_idx);
}

Result<std::vector<dockv::KeyEntryValue>> InitKeyColumnPrimitiveValues(
    const ArenaList<LWPgsqlExpressionPB> &column_values, const Schema &schema, size_t start_idx) {
  return DoInitKeyColumnPrimitiveValues(column_values, schema, start_idx);
}

}  // namespace yb::qlexpr
