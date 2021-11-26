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

std::vector<PrimitiveValue> GetRangeKeyScanSpec(
    const Schema& schema,
    const std::vector<PrimitiveValue>* prefixed_hash_components,
    const QLScanRange* scan_range,
    bool lower_bound,
    bool include_static_columns) {
  std::vector<PrimitiveValue> range_components;
  range_components.reserve(schema.num_range_key_columns());
  if (prefixed_hash_components) {
    range_components.insert(range_components.begin(),
                            prefixed_hash_components->begin(),
                            prefixed_hash_components->end());
  }
  if (scan_range != nullptr) {
    // Return the lower/upper range components for the scan.

    for (size_t i = schema.num_hash_key_columns() + range_components.size();
         i < schema.num_key_columns();
         i++) {
      const auto& column = schema.column(i);
      const auto& range = scan_range->RangeFor(schema.column_id(i));
      range_components.emplace_back(GetQLRangeBoundAsPVal(range,
                                                          column.sorting_type(),
                                                          lower_bound));
    }
  }

  if (!lower_bound) {
    // We add +inf as an extra component to make sure this is greater than all keys in range.
    // For lower bound, this is true already, because dockey + suffix is > dockey
    range_components.emplace_back(PrimitiveValue(ValueType::kHighest));
  } else if (schema.has_statics() && !include_static_columns && range_components.empty()) {
    // If we want to skip static columns, make sure the range components are non-empty.
    // We use kMinPrimitiveValueType instead of kLowest because it compares as higher than
    // kHybridTime in RocksDB.
    range_components.emplace_back(kMinPrimitiveValueType);
  }
  return range_components;
}

PrimitiveValue GetQLRangeBoundAsPVal(const QLScanRange::QLRange& ql_range,
                                     SortingType sorting_type,
                                     bool lower_bound) {
  const auto sort_order = PrimitiveValue::SortOrderFromColumnSchemaSortingType(sorting_type);

  // lower bound for ASC column and upper bound for DESC column -> min value
  // otherwise -> max value
  // for ASC col: lower -> min_value; upper -> max_value
  // for DESC   :       -> max_value;       -> min_value
  bool min_bound = lower_bound ^ (sort_order == SortOrder::kDescending);
  const auto& ql_bound = min_bound ? ql_range.min_value : ql_range.max_value;
  if (ql_bound) {
    // Special case for nulls because FromQLValuePB will turn them into kTombstone instead.
    if (IsNull(*ql_bound)) {
      return PrimitiveValue::NullValue(sorting_type);
    }
    return PrimitiveValue::FromQLValuePB(*ql_bound, sorting_type);
  }

  // For unset use kLowest/kHighest to ensure we cover entire scanned range.
  return lower_bound ? PrimitiveValue(ValueType::kLowest)
                     : PrimitiveValue(ValueType::kHighest);
}

}  // namespace docdb
}  // namespace yb
