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

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "yb/util/slice.h"

namespace yb {
namespace tools {

// What hashing one key range of one table produced: the xor of the per-row hashes over the range,
// how many rows went into it, and the read time it was taken at.
struct TableHashTotals {
  uint64_t xor_hash = 0;
  uint64_t row_count = 0;
  uint64_t read_ht = 0;
  // Which hashing scheme the servers that answered were running
  // (tablet::kTabletDataHashSchemeVersion), or 0 for a server too old to report one. Two xor_hash
  // values mean nothing to each other unless this agrees, so it travels with the totals.
  //
  // nullopt means no server was asked, because the range resolved to no tablets. That differs from
  // 0: a side that hashed nothing still has a comparable empty result, while a side that hashed
  // under an unknown scheme does not.
  //
  // The redundant initializer keeps designated initializers that omit this field well-formed under
  // -Wmissing-designated-field-initializers.
  std::optional<uint32_t> hash_scheme_version = std::nullopt;
  // Exclusive continuation key when max_rows stopped the scan, empty if the range was fully hashed.
  // Accepted wherever start_key / end_key are. Always an encoded row key, whether the scan stopped
  // mid-tablet or on a tablet boundary, so that it compares against range bounds in one byte space.
  std::string next_key = "";
};

// True if tablet partition [tablet_start, tablet_end) overlaps requested [range_start, range_end).
// An empty bound is unbounded: -inf for a start, +inf for an end.
//
// Both sides must be in the same byte space. A hash-partitioned table has two of them: bare 2-byte
// partition keys and full encoded row keys. A bound of one kind compares nonsensically against the
// other, so callers lift both into the encoded space first.
inline bool PartitionRangeOverlaps(
    Slice tablet_start, Slice tablet_end, Slice range_start, Slice range_end) {
  // tablet_start < range_end
  const bool below_range_end =
      range_end.empty() || tablet_start.empty() || tablet_start.compare(range_end) < 0;
  // range_start < tablet_end
  const bool above_range_start =
      tablet_end.empty() || range_start.empty() || range_start.compare(tablet_end) < 0;
  return below_range_end && above_range_start;
}

}  // namespace tools
}  // namespace yb
