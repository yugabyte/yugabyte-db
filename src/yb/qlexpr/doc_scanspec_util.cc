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

#include "yb/qlexpr/doc_scanspec_util.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/value_type.h"

#include "yb/qlexpr/ql_scanspec.h"

namespace yb::qlexpr {

dockv::KeyEntryValues GetRangeKeyScanSpec(
    const Schema& schema,
    const dockv::KeyEntryValues* prefixed_range_components,
    const QLScanRange* scan_range,
    std::vector<bool>* inclusivities,
    BoundType bound_type,
    bool include_static_columns,
    bool* trivial_scan_ptr) {
  bool ignored_trivial = false;
  bool& trivial = trivial_scan_ptr ? *trivial_scan_ptr : ignored_trivial;
  dockv::KeyEntryValues range_components;
  range_components.reserve(schema.num_range_key_columns());
  if (prefixed_range_components) {
    range_components.insert(range_components.begin(),
                            prefixed_range_components->begin(),
                            prefixed_range_components->end());
    if (inclusivities) {
      inclusivities->resize(inclusivities->size() + prefixed_range_components->size(), true);
    }
  }
  if (scan_range != nullptr) {
    // Return the lower/upper range components for the scan.
    trivial = true;
    size_t start = schema.num_hash_key_columns() + range_components.size();
    size_t i = start;
    while (i < schema.num_key_columns()) {
      const auto& column = schema.column(i);
      const auto& range = scan_range->RangeFor(schema.column_id(i));
      if (range.Active() && (range.is_not_null || (i > start))) {
        trivial = false;
      }
      ++i;
      range_components.emplace_back(range.BoundAsPVal(column.sorting_type(), bound_type));
      auto is_inclusive = range.BoundIsInclusive(column.sorting_type(), bound_type);
      if (inclusivities) {
        inclusivities->push_back(is_inclusive);
      } else if (!is_inclusive) {
        // If this bound is non-inclusive then we append kHighest/kLowest
        // after and stop to fit the given bound
        range_components.push_back(dockv::KeyEntryValue(
            bound_type == BoundType::kLower ? dockv::KeyEntryType::kHighest
                                            : dockv::KeyEntryType::kLowest));
        break;
      }
    }
    if (trivial_scan_ptr) {
      for (; trivial && i != schema.num_key_columns(); ++i) {
        if (scan_range->RangeFor(schema.column_id(i)).Active()) {
          trivial = false;
          break;
        }
      }
    }
  } else {
    trivial = true;
  }

  if (bound_type != BoundType::kLower) {
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
        static_cast<dockv::KeyEntryType>(std::to_underlying(dockv::KeyEntryType::kHybridTime) + 1));
    if (inclusivities) {
      inclusivities->push_back(true);
    }
  }
  return range_components;
}

}  // namespace yb::qlexpr
