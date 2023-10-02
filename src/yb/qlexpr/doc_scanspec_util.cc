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

#include "yb/qlexpr/doc_scanspec_util.h"

#include "yb/qlexpr/ql_scanspec.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

namespace yb::qlexpr {

dockv::KeyEntryValues GetRangeKeyScanSpec(
    const Schema& schema,
    const dockv::KeyEntryValues* prefixed_hash_components,
    const QLScanRange* scan_range,
    std::vector<bool> *inclusivities,
    bool lower_bound,
    bool include_static_columns,
    bool use_strictness) {
  dockv::KeyEntryValues range_components;
  range_components.reserve(schema.num_range_key_columns());
  if (prefixed_hash_components) {
    range_components.insert(range_components.begin(),
                            prefixed_hash_components->begin(),
                            prefixed_hash_components->end());
    if (inclusivities) {
      for (size_t i = 0; i < prefixed_hash_components->size(); i++) {
        inclusivities->push_back(true);
      }
    }
  }
  if (scan_range != nullptr) {
    // Return the lower/upper range components for the scan.

    for (size_t i = schema.num_hash_key_columns() + range_components.size();
         i < schema.num_key_columns();
         i++) {
      const auto& column = schema.column(i);
      const auto& range = scan_range->RangeFor(schema.column_id(i));
      range_components.emplace_back(
          GetQLRangeBoundAsPVal(range, column.sorting_type(), lower_bound));
      if (inclusivities) {
        inclusivities->push_back(GetQLRangeBoundIsInclusive(range,
                                                            column.sorting_type(),
                                                            lower_bound));
      }

      // If this bound in non-inclusive then we append kHighest/kLowest
      // after and stop to fit the given bound
      if (use_strictness
          && !GetQLRangeBoundIsInclusive(range, column.sorting_type(), lower_bound)) {
        range_components.push_back(dockv::KeyEntryValue(
            lower_bound ? dockv::KeyEntryType::kHighest : dockv::KeyEntryType::kLowest));
        if (inclusivities) {
          inclusivities->push_back(true);
        }
        break;
      }
    }
  }

  if (!lower_bound) {
    // We add +inf as an extra component to make sure this is greater than all keys in range.
    // For lower bound, this is true already, because dockey + suffix is > dockey
    range_components.emplace_back(dockv::KeyEntryType::kHighest);
    if (inclusivities) {
      inclusivities->push_back(true);
    }
  } else if (schema.has_statics() && !include_static_columns && range_components.empty()) {
    // If we want to skip static columns, make sure the range components are non-empty.
    // We use kMinPrimitiveValueType instead of kLowest because it compares as higher than
    // kHybridTime in RocksDB.
    range_components.emplace_back(
        static_cast<dockv::KeyEntryType>(to_underlying(dockv::KeyEntryType::kHybridTime) + 1));
    if (inclusivities) {
      inclusivities->push_back(true);
    }
  }
  return range_components;
}

const boost::optional<QLScanRange::QLBound> &GetQLRangeBound(
    const QLScanRange::QLRange& ql_range,
    SortingType sorting_type,
    bool lower_bound) {
  const auto sort_order = dockv::SortOrderFromColumnSchemaSortingType(sorting_type);

  // lower bound for ASC column and upper bound for DESC column -> min value
  // otherwise -> max value
  // for ASC col: lower -> min_bound; upper -> max_bound
  // for DESC   :       -> max_bound;       -> min_bound
  bool min_bound = lower_bound ^ (sort_order == SortOrder::kDescending);
  const auto& ql_bound = min_bound ? ql_range.min_bound : ql_range.max_bound;
  return ql_bound;
}

bool GetQLRangeBoundIsInclusive(const QLScanRange::QLRange& ql_range,
                                SortingType sorting_type,
                                bool lower_bound) {
  const auto &ql_bound = GetQLRangeBound(ql_range, sorting_type, lower_bound);
  if (ql_bound) {
    return ql_bound->IsInclusive();
  }

  return false;
}

dockv::KeyEntryValue GetQLRangeBoundAsPVal(const QLScanRange::QLRange& ql_range,
                                    SortingType sorting_type,
                                    bool lower_bound) {
  const auto &ql_bound = GetQLRangeBound(ql_range, sorting_type, lower_bound);
  if (ql_bound) {
    // Special case for nulls because FromQLValuePB will turn them into kTombstone instead.
    if (IsNull(ql_bound->GetValue())) {
      return dockv::KeyEntryValue::NullValue(sorting_type);
    }
    return dockv::KeyEntryValue::FromQLValuePB(ql_bound->GetValue(), sorting_type);
  }

  // If there is any constraint on this range, or if this is explicitly an IS NOT NULL condition,
  // then NULLs should not be included in this range. Note that GetQLRangeBoundIsInclusive defaults
  // to false in the absence of a range.
  if (ql_range.min_bound || ql_range.max_bound || ql_range.is_not_null) {
    return dockv::KeyEntryValue(
        lower_bound ? dockv::KeyEntryType::kNullLow : dockv::KeyEntryType::kNullHigh);
  }

  // For unset use kLowest/kHighest to ensure we cover entire scanned range.
  return dockv::KeyEntryValue(
      lower_bound ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest);
}

}  // namespace yb::qlexpr
