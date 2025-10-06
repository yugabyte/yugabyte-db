// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/rocksdb/table/block_hash_index.h"

#include <algorithm>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/util/coding.h"

namespace rocksdb {

Status CreateBlockHashIndex(const SliceTransform* hash_key_extractor,
                            const Slice& prefixes, const Slice& prefix_meta,
                            BlockHashIndex** hash_index) {
  uint64_t pos = 0;
  auto meta_pos = prefix_meta;
  Status s;
  *hash_index = new BlockHashIndex(
      hash_key_extractor,
      false /* external module manages memory space for prefixes */);

  while (!meta_pos.empty()) {
    uint32_t prefix_size = 0;
    uint32_t entry_index = 0;
    uint32_t num_blocks = 0;
    if (!GetVarint32(&meta_pos, &prefix_size) ||
        !GetVarint32(&meta_pos, &entry_index) ||
        !GetVarint32(&meta_pos, &num_blocks)) {
      s = STATUS(Corruption,
          "Corrupted prefix meta block: unable to read from it.");
      break;
    }
    Slice prefix(prefixes.data() + pos, prefix_size);
    (*hash_index)->Add(prefix, entry_index, num_blocks);

    pos += prefix_size;
  }

  if (s.ok() && pos != prefixes.size()) {
    s = STATUS(Corruption, "Corrupted prefix meta block");
  }

  if (!s.ok()) {
    delete *hash_index;
  }

  return s;
}

BlockHashIndex* CreateBlockHashIndexOnTheFly(
    InternalIterator* index_iter, InternalIterator* data_iter,
    const uint32_t num_restarts, const Comparator* comparator,
    const SliceTransform* hash_key_extractor) {
  assert(hash_key_extractor);
  auto hash_index = new BlockHashIndex(
      hash_key_extractor,
      true /* hash_index will copy prefix when Add() is called */);
  uint32_t current_restart_index = 0;

  std::string pending_entry_prefix;
  // pending_block_num == 0 also implies there is no entry inserted at all.
  uint32_t pending_block_num = 0;
  uint32_t pending_entry_index = 0;

  // scan all the entries and create a hash index based on their prefixes.
  data_iter->SeekToFirst();
  for (index_iter->SeekToFirst();
       index_iter->Valid() && current_restart_index < num_restarts;
       index_iter->Next()) {
    Slice last_key_in_block = index_iter->key();
    assert(data_iter->Valid() && data_iter->status().ok());

    // scan through all entries within a data block.
    while (data_iter->Valid() &&
           comparator->Compare(data_iter->key(), last_key_in_block) <= 0) {
      auto key_prefix = hash_key_extractor->Transform(data_iter->key());
      bool is_first_entry = pending_block_num == 0;

      // Keys may share the prefix
      if (is_first_entry || pending_entry_prefix != key_prefix) {
        if (!is_first_entry) {
          bool succeeded = hash_index->Add(
              pending_entry_prefix, pending_entry_index, pending_block_num);
          if (!succeeded) {
            delete hash_index;
            return nullptr;
          }
        }

        // update the status.
        // needs a hard copy otherwise the underlying data changes all the time.
        pending_entry_prefix = key_prefix.ToString();
        pending_block_num = 1;
        pending_entry_index = current_restart_index;
      } else {
        // entry number increments when keys share the prefix reside in
        // different data blocks.
        auto last_restart_index = pending_entry_index + pending_block_num - 1;
        assert(last_restart_index <= current_restart_index);
        if (last_restart_index != current_restart_index) {
          ++pending_block_num;
        }
      }
      data_iter->Next();
    }

    ++current_restart_index;
  }

  // make sure all entries has been scaned.
  assert(!index_iter->Valid());
  assert(!data_iter->Valid());

  if (pending_block_num > 0) {
    auto succeeded = hash_index->Add(pending_entry_prefix, pending_entry_index,
                                     pending_block_num);
    if (!succeeded) {
      delete hash_index;
      return nullptr;
    }
  }

  return hash_index;
}

bool BlockHashIndex::Add(const Slice& prefix, uint32_t restart_index,
                         uint32_t num_blocks) {
  auto prefix_to_insert = prefix;
  if (kOwnPrefixes) {
    auto prefix_ptr = arena_.Allocate(prefix.size());
    // MSVC reports C4996 Function call with parameters that may be
    // unsafe when using std::copy with a output iterator - pointer
    memcpy(prefix_ptr, prefix.data(), prefix.size());
    prefix_to_insert = Slice(prefix_ptr, prefix.size());
  }
  auto result = restart_indices_.insert(
      {prefix_to_insert, RestartIndex(restart_index, num_blocks)});
  return result.second;
}

const BlockHashIndex::RestartIndex* BlockHashIndex::GetRestartIndex(const Slice& key) const {
  auto key_prefix = hash_key_extractor_->Transform(key);

  auto pos = restart_indices_.find(key_prefix);
  if (pos == restart_indices_.end()) {
    return nullptr;
  }

  return &pos->second;
}

}  // namespace rocksdb
