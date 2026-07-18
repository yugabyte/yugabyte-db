// Copyright (c) YugabyteDB, Inc.
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
#include "yb/dockv/primitive_value_util.h"

#include "yb/qlexpr/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb::dockv {

namespace {

template <class Factory, class ColumnValuesPB>
Result<std::vector<typename Factory::ResultType>> DoQLKeyColumnValuesToPrimitiveValues(
    Factory& factory, const ColumnValuesPB& column_values,
    const Schema &schema, size_t column_idx, size_t column_count) {
  std::vector<typename Factory::ResultType> result;
  for (const auto& column_value : column_values) {
    if (!schema.is_key_column(column_idx)) {
      auto status = STATUS_FORMAT(
          Corruption, "Column at $0 is not key column in $1", column_idx, schema.ToString());
      LOG(DFATAL) << status;
      return status;
    }

    if (!column_value.has_value() || IsNull(column_value.value())) {
      result.push_back(factory(KeyEntryType::kNullLow));
    } else {
      result.push_back(factory(
          column_value.value(), schema.column(column_idx).sorting_type()));
    }
    column_idx++;
  }
  return result;
}

} // namespace

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
Result<KeyEntryValues> QLKeyColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<QLExpressionPB> &column_values,
    const Schema &schema, size_t column_idx, const size_t column_count) {
  dockv::KeyEntryValueFactory factory;
  return DoQLKeyColumnValuesToPrimitiveValues(
      factory, column_values, schema, column_idx, column_count);
}

Result<KeyEntryValues> QLKeyColumnValuesToPrimitiveValues(
    const ArenaList<LWQLExpressionPB>& column_values,
    const Schema& schema, size_t column_idx, size_t column_count) {
  dockv::KeyEntryValueFactory factory;
  return DoQLKeyColumnValuesToPrimitiveValues(
      factory, column_values, schema, column_idx, column_count);
}

Result<std::vector<Slice>> QLKeyColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<QLExpressionPB> &column_values,
    const Schema &schema, size_t column_idx, const size_t column_count,
    Arena& arena) {
  dockv::KeyEntryValueAsSliceFactory factory{arena};
  return DoQLKeyColumnValuesToPrimitiveValues(
      factory, column_values, schema, column_idx, column_count);
}

Result<std::vector<Slice>> QLKeyColumnValuesToPrimitiveValues(
    const ArenaList<LWQLExpressionPB>& column_values,
    const Schema& schema, size_t column_idx, size_t column_count,
    Arena& arena) {
  dockv::KeyEntryValueAsSliceFactory factory{arena};
  return DoQLKeyColumnValuesToPrimitiveValues(
      factory, column_values, schema, column_idx, column_count);
}

}  // namespace yb::dockv
