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
#include "yb/dockv/primitive_value_util.h"

#include "yb/qlexpr/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::vector;

namespace yb::dockv {

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

}  // namespace yb::dockv
