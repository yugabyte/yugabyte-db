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

#include <optional>
#include <utility>
#include <vector>

#include "yb/docdb/key_bounds.h"

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/util/kv_util.h"
#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::docdb {

YB_STRONGLY_TYPED_BOOL(EqualKeyComesFirst);

// If encoded_key contains a full encoded DocKey, returns it. Otherwise returns encoded_key itself.
// See ExtractDocKey() in ql_rocksdb_storage.cc for why either can bound a sample block.
Slice TryExtractDocKey(Slice encoded_key);

// Where GetSampleBlocks() seeks the index for a tablet whose rows start at lower_bound_key.
Slice TableRowsStart(Slice lower_bound_key);

using SampleBlock = std::pair<KeyBuffer, KeyBuffer>;

// Resolves item positions in a tablet's SST indexes into the sample blocks that the
// SPLIT_INTERSECTING_BLOCKS_V3 walk in GetSampleBlocks() produces, so ANALYZE can pick sample
// blocks by position instead of walking the whole index.
//
// Items are the index entries the walk visits, numbered file by file (a file's last entry is never
// one), plus one item for the walk's final block, the tail block. Resolve() returns the block
// ending at an item's DocKey only for the first entry with that DocKey in merged order, and drops
// every other item by returning nullopt. Dropping those items, rather than resolving them to the
// same block, keeps every non-empty block equally likely to be picked. The merged index iterator
// orders equal keys in different files arbitrarily; the resolver orders them by file, which gives
// the same blocks.
//
// Unlike the walk, a first block whose lower bound is above its upper bound is dropped, since it
// holds nothing of the tablet.
//
// Init() and Resolve() return NotSupported if an SST file can't be read by position, and the caller
// falls back to the walk.
//
// Not thread-safe: every call moves the SST files' index iterators.
class SampleBlockResolver {
 public:
  // `bounds` are the tablet's partition bounds as GetSampleBlocks() computes them, which also bound
  // the first and last sample blocks. `sst_files` must outlive the resolver.
  SampleBlockResolver(rocksdb::SstFileIndexIterators* sst_files, const KeyBounds& bounds);

  // Call once, before anything else.
  Status Init();

  // Includes the tail block's item and items Resolve() drops, so the number of blocks is
  // num_items() times the share of resolved items that aren't dropped.
  uint64_t num_items() const { return num_boundary_items_ + 1; }

  // Returns nullopt if the item at `position` is dropped.
  Result<std::optional<SampleBlock>> Resolve(uint64_t position);

 private:
  struct File {
    rocksdb::DataBlockAwareIndexInternalIterator* iter = nullptr;
    // Positions of the file's items: [begin, end).
    uint64_t begin = 0;
    uint64_t end = 0;
    // Index keys at begin and end - 1.
    KeyBuffer first_key;
    KeyBuffer last_key;
  };

  static Status EntryExpected(const rocksdb::DataBlockAwareIndexInternalIterator& iter);

  // Updates `lower`, the largest predecessor block bound so far. Returns false if the
  // predecessor's block bound is `upper`, so the item is dropped.
  static bool AddPredecessor(Slice internal_key, Slice upper, std::optional<KeyBuffer>* lower);

  // The file and position in it of an item other than the tail block's.
  Result<std::pair<size_t, uint64_t>> Locate(uint64_t position) const;

  // Adds the item of files_[file_idx] right before `target` in merged order, like AddPredecessor().
  Result<bool> AddPredecessorInFile(
      size_t file_idx, Slice target, EqualKeyComesFirst equal_key_comes_first, Slice upper,
      std::optional<KeyBuffer>* lower);

  std::optional<SampleBlock> MakeBlock(const std::optional<KeyBuffer>& lower, Slice upper) const;

  Result<std::optional<SampleBlock>> ResolveBoundary(size_t file_idx, uint64_t position);

  rocksdb::SstFileIndexIterators& sst_files_;
  const rocksdb::InternalKeyComparator& comparator_;
  const KeyBounds bounds_;
  // Files with items, in file order.
  std::vector<File> files_;
  // item_ends_[i] is the number of items in files_[0..i].
  std::vector<uint64_t> item_ends_;
  uint64_t num_boundary_items_ = 0;
  std::optional<KeyBuffer> tail_lower_;
};

} // namespace yb::docdb
