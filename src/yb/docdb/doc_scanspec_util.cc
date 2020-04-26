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

#include "yb/docdb/doc_scanspec_util.h"

#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

namespace yb {
namespace docdb {

using common::QLScanRange;

std::vector<PrimitiveValue> GetRangeKeyScanSpec(
    const Schema& schema, const QLScanRange* scan_range, bool lower_bound) {
  std::vector<PrimitiveValue> range_components;
  if (scan_range != nullptr) {
    const std::vector<QLValuePB> range_values = scan_range->range_values(lower_bound);
    range_components.reserve(range_values.size());
    size_t column_idx = schema.num_hash_key_columns();
    for (const auto &value : range_values) {
      const auto &column = schema.column(column_idx);
      if (IsNull(value)) {
        range_components.emplace_back(
            lower_bound ? ValueType::kLowest : ValueType::kHighest);
      } else {
        range_components.emplace_back(
            PrimitiveValue::FromQLValuePB(value, column.sorting_type()));
      }
      column_idx++;
    }
  }

  if (!lower_bound) {
    // We add +inf as an extra component to make sure this is greater than all keys in range.
    // For lower bound, this is true already, because dockey + suffix is > dockey
    range_components.emplace_back(PrimitiveValue(ValueType::kHighest));
  }
  return range_components;
}

}  // namespace docdb
}  // namespace yb
