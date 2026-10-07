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

#include <string>

#include "yb/util/dynamic_bitset.h"

namespace yb::docdb {

// The storages of a tablet a write is applied to: the regular DB and each vector index.
//
// A tablet may have any number of vector indexes (GH#33923). All() covers every storage without
// knowing how many there are. Any other set is sized with Resize() before its bits are set, and
// testing a storage past that size is a CHECK failure.
//
// TODO(vector_index): A vector index is addressed by its position in the tablet's vector index
// list, which is not stable: TabletVectorIndexes inserts a new index ordered by column id,
// possibly in front of existing ones, and RemoveTableFromList shifts the later indexes down.
// A set is valid only against the list it was computed for. That holds today because the only sets
// other than All() are computed during bootstrap and consumed right away against the same list.
// Key the set by table id before a set can outlive a list change, e.g. when a large transaction
// applies in deferred chunks (see the TODO in ApplyIntentsContext).
class StorageSet {
 public:
  StorageSet() = default;

  static StorageSet All() {
    StorageSet result;
    result.bits_ = DynamicBitSet::All();
    return result;
  }

  // Sizes the set for the regular DB and num_vector_indexes vector indexes. Required before
  // SetRegularDB() and SetVectorIndex(). On a default-constructed set every bit starts unset;
  // on All() every bit starts set; on an already sized set the bits below the new size are kept.
  void Resize(size_t num_vector_indexes) {
    bits_.resize(VectorIndexBit(num_vector_indexes));
  }

  bool Any() const {
    return bits_.any();
  }

  bool TestRegularDB() const {
    return bits_.test(kRegularDBBit);
  }

  void SetRegularDB() {
    bits_.set(kRegularDBBit);
  }

  bool TestVectorIndex(size_t index) const {
    return bits_.test(VectorIndexBit(index));
  }

  void SetVectorIndex(size_t index) {
    bits_.set(VectorIndexBit(index));
  }

  std::string ToString() const {
    return bits_.ToString();
  }

 private:
  static constexpr size_t kRegularDBBit = 0;

  static constexpr size_t VectorIndexBit(size_t index) {
    return 1 + index;
  }

  // Bit 0 is the regular DB, bit 1 + i is vector index i.
  DynamicBitSet bits_;
};

}  // namespace yb::docdb
