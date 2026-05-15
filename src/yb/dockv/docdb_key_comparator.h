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

#pragma once

#include "yb/rocksdb/comparator.h"

namespace yb::dockv {

// A custom RocksDB comparator for DocDB keys that handles types whose binary
// representation is not byte-order compatible with their logical sort order.
//
// Currently, the only such type is BSON (KeyEntryType::kBson / kBsonDescending).
// BSON documents use little-endian integers and have type-specific comparison
// rules that differ from byte-wise ordering.
//
// For all non-BSON key components, this comparator produces the same results as
// the BytewiseComparator, since DocDB encodes those types in a byte-order
// preserving manner.
//
// The comparator works by:
//   1. Finding the first byte position where two keys differ (fast path).
//   2. Scanning the key structure from the beginning to determine if the
//      differing position falls within a BSON-encoded region.
//   3. If in a BSON region, decoding both BSON values and comparing them
//      using BSON-specific comparison logic (CompareBson).
//   4. Otherwise, returning the byte-wise comparison result.
class DocDBKeyComparator : public rocksdb::Comparator {
 public:
  int Compare(Slice a, Slice b) const override;
  bool Equal(Slice a, Slice b) const override;
  const char* Name() const override;
  void FindShortestSeparator(std::string* start, const Slice& limit) const override;
  void FindShortSuccessor(std::string* key) const override;
};

// Returns a singleton instance of DocDBKeyComparator.
const rocksdb::Comparator* DocDBKeyComparatorInstance();

}  // namespace yb::dockv
