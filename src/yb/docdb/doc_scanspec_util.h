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

// Utilities for encoding and decoding key/value pairs that are used in the DocDB code.

#pragma once

#include "yb/common/ql_scanspec.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

// Get the scanspec for range key components.
std::vector<KeyEntryValue> GetRangeKeyScanSpec(
    const Schema& schema,
    const std::vector<KeyEntryValue>* prefixed_range_components,
    const QLScanRange* scan_range,
    std::vector<bool> *inclusivities,
    bool lower_bound,
    bool include_static_columns = false,
    bool use_strictness = true);

// Gets the lower/upper bound value of the given range
KeyEntryValue GetQLRangeBoundAsPVal(const QLScanRange::QLRange& ql_range,
                                    SortingType sorting_type,
                                    bool lower_bound);

const boost::optional<QLScanRange::QLBound> &GetQLRangeBound(
    const QLScanRange::QLRange& ql_range,
    SortingType sorting_type,
    bool lower_bound);

// Gets whether the lower/upper bound of the given range is inclusive
bool GetQLRangeBoundIsInclusive(
    const QLScanRange::QLRange& ql_range,
    SortingType sorting_type,
    bool lower_bound);

}  // namespace docdb
}  // namespace yb
