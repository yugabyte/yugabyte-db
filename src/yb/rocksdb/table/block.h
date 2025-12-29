//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#ifdef ROCKSDB_MALLOC_USABLE_SIZE
#include <malloc.h>
#endif

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/table/block_prefix_index.h"
#include "yb/rocksdb/table/block_hash_index.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/table/internal_iterator.h"

#include "yb/util/enums.h"

namespace rocksdb {

struct BlockContents;
class Comparator;
class BlockIter;
class BlockHashIndex;
class BlockPrefixIndex;

// Determines which middle point should be taken in case of even number of total points.
// NOTE! This enum must not be changed unless all the usages are verified!
YB_DEFINE_ENUM(MiddlePointPolicy, ((kMiddleHigh, 0))((kMiddleLow, 1)));


class Block {
 public:
  // Initialize the block with the specified contents.
  explicit Block(BlockContents&& contents);

  ~Block() = default;

  size_t size() const { return size_; }
  const char* data() const { return data_; }
  bool cachable() const { return contents_.cachable; }
  size_t usable_size() const {
#ifdef ROCKSDB_MALLOC_USABLE_SIZE
    if (contents_.allocation.get() != nullptr) {
      return malloc_usable_size(contents_.allocation.get());
    }
#endif  // ROCKSDB_MALLOC_USABLE_SIZE
    return size_;
  }
  uint32_t NumRestarts() const;
  CompressionType compression_type() const {
    return contents_.compression_type;
  }

  // If hash index lookup is enabled and `use_hash_index` is true. This block
  // will do hash lookup for the key prefix.
  //
  // NOTE: for the hash based lookup, if a key prefix doesn't match any key,
  // the iterator will simply be set as "invalid", rather than returning
  // the key that is just pass the target key.
  //
  // If iter is null, return new InternalIterator
  // If iter is not null, update this one and return it as InternalIterator*
  // It MUST return an instance of BlockIter (or derived class) in case of no error.
  //
  // If total_order_seek is true, hash_index_ and prefix_index_ are ignored.
  // This option only applies for index block. For data block, hash_index_
  // and prefix_index_ are null, so this option does not matter.
  // key_value_encoding_format specifies what kind of algorithm to use for decoding entries.
  BlockIter* NewIterator(const Comparator* comparator,
                                KeyValueEncodingFormat key_value_encoding_format,
                                BlockIter* iter = nullptr,
                                bool total_order_seek = true,
                                size_t restart_block_cache_capacity = 0) const;

  inline BlockIter* NewIndexBlockIterator(
      const Comparator* comparator, BlockIter* iter = nullptr, bool total_order_seek = true) const {
    return NewIterator(
        comparator, kIndexBlockKeyValueEncodingFormat, iter, total_order_seek);
  }

  void SetBlockHashIndex(BlockHashIndex* hash_index);
  void SetBlockPrefixIndex(BlockPrefixIndex* prefix_index);

  // Report an approximation of how much memory has been used.
  size_t ApproximateMemoryUsage() const;

  // Returns a middle key for this block. Parameter `middle_entry_policy` denotes which entry should
  // be treated as a "middle entry" in case of even number of entires. This might be important as
  // index blocks and data blocks have a bit different semantics of the stored key. Index block's
  // entry contains a key that is greater or equal to the last key of a data block which is indexed
  // (in most cases it contains a non-existent key which is strictky greater than the last key of
  // a data block); so, for example, if there are two entries k0 and k1, then k0 points exactly to
  // the middle of two data blocks, so we need to take "the lower" of these two keys. The data block
  // always has an exsitent key, so, for example, if there are two entries k0 and k1, and if k0 is
  // taken as a middle key, then the middle is pointing to the beginning of all the data which
  // might be an unwanted effect in terms of tablet splitting; in this case taking k1, "the upper"
  // of these two keys, gives a better middle key result.
  yb::Result<std::string> GetMiddleKey(
      KeyValueEncodingFormat key_value_encoding_format,
      const Comparator* cmp = BytewiseComparator(),
      MiddlePointPolicy middle_entry_policy = MiddlePointPolicy::kMiddleLow
  ) const;

  // Returns key for corresponding restart block.
  yb::Result<Slice> GetRestartKey(
      uint32_t restart_idx, KeyValueEncodingFormat key_value_encoding_format) const;

  static inline uint32_t GetMiddlePointIndex(
      const uint32_t num_points, const MiddlePointPolicy middle_point_policy) {
    return num_points ? (num_points - std::to_underlying(middle_point_policy)) / 2 : 0;
  }

 private:
  // Returns a key of a middle entry for the specificed restart point.
  yb::Result<std::string> GetRestartBlockMiddleEntryKey(
      uint32_t restart_idx, const Comparator* comparator,
      KeyValueEncodingFormat key_value_encoding_format,
      MiddlePointPolicy middle_restart_policy) const;

  BlockContents contents_;
  const char* data_;            // contents_.data.data()
  size_t size_;                 // contents_.data.size()
  uint32_t restart_offset_;     // Offset in data_ of restart array
  std::unique_ptr<BlockHashIndex> hash_index_;
  std::unique_ptr<BlockPrefixIndex> prefix_index_;

  // No copying allowed
  Block(const Block&);
  void operator=(const Block&);
};

class BlockIter : public InternalIterator {
 public:
  class Empty;

  BlockIter()
      : comparator_(nullptr),
        data_(nullptr),
        restarts_(0),
        num_restarts_(0),
        current_(0),
        restart_index_(0),
        block_entry_cache_idx_(0),
        cache_restart_block_keys_(false),
        status_(Status::OK()),
        hash_index_(nullptr),
        prefix_index_(nullptr) {}

  BlockIter(
      const Comparator* comparator, const char* data,
      KeyValueEncodingFormat key_value_encoding_format, uint32_t restarts, uint32_t num_restarts,
      const BlockHashIndex* hash_index, const BlockPrefixIndex* prefix_index,
      size_t restart_block_cache_capacity)
      : BlockIter() {
    Initialize(
        comparator, data, key_value_encoding_format, restarts, num_restarts, hash_index,
        prefix_index, restart_block_cache_capacity);
  }

  void Initialize(
      const Comparator* comparator, const char* data,
      KeyValueEncodingFormat key_value_encoding_format, uint32_t restarts, uint32_t num_restarts,
      const BlockHashIndex* hash_index, const BlockPrefixIndex* prefix_index,
      size_t restart_block_cache_capacity);

  void SetStatus(Status s) {
    status_ = s;
  }

  virtual Status status() const override { return status_; }

  virtual const KeyValueEntry& Entry() const override {
    // The following invariand holds (except intermediary state changes inside private methods):
    // current_ < restarts_, restart_index_ < num_restarts_ and CurrentEntry().entry_.Valid() are
    // either all true or false. This is validated in Entry() method.
    // NB! Is it a valid state to have an empty key.

    DCHECK((current_ == restarts_) == (restart_index_ == num_restarts_));
    DCHECK((current_ == restarts_) == !CurrentEntry().entry_.Valid());
    return CurrentEntry().entry_;
  }

  virtual const KeyValueEntry& Next() override;

  virtual const KeyValueEntry& Prev() override;

  virtual const KeyValueEntry& Seek(Slice target) override;

  virtual const KeyValueEntry& SeekToFirst() override;

  virtual const KeyValueEntry& SeekToLast() override;

  virtual Status PinData() override {
    // block data is always pinned.
    return Status::OK();
  }

  virtual Status ReleasePinnedData() override {
    // Block data is always pinned.
    return Status::OK();
  }

  virtual bool IsKeyPinned() const override {
      return CurrentEntry().key_.IsKeyPinned();
  }

  void SeekToRestart(uint32_t index);

  inline uint32_t GetCurrentRestart() const {
    return restart_index_;
  }

  inline uint32_t GetNumRestarts() const {
    return num_restarts_;
  }

 private:
  // It is required to store at least one entry, even in case if restart block caching is off.
  static constexpr const size_t kRestartBlockCacheMinSize = 1;

  // Used as a cache node for a current restart block.
  struct BlockEntry {
    // Stores a decoded key.
    IterKey key_ {};

    // References a key from BlockEntry::key_ and a value from a current block.
    KeyValueEntry entry_ {};
  };

  const Comparator* comparator_;
  const char* data_;       // Underlying block contents.
  KeyValueEncodingFormat key_value_encoding_format_;
  uint32_t restarts_;      // Offset of restart array (list of fixed32).
  uint32_t num_restarts_;  // Number of uint32_t entries in restart array.

  uint32_t current_;       // Offset in data_ where current entry starts.
  uint32_t restart_index_; // Index of a restart block in which current entry falls.
  boost::container::small_vector<BlockEntry, kRestartBlockCacheMinSize> block_entry_cache_;
  uint32_t block_entry_cache_idx_; // Index of the current element from block entry cache.
  bool cache_restart_block_keys_;
  Status status_;
  const BlockHashIndex* hash_index_;
  const BlockPrefixIndex* prefix_index_;

  inline int Compare(const Slice& a, const Slice& b) const {
    return comparator_->Compare(a, b);
  }

  inline BlockEntry& CurrentEntry() {
    return block_entry_cache_[block_entry_cache_idx_];
  }

  inline const BlockEntry& CurrentEntry() const {
    return block_entry_cache_[block_entry_cache_idx_];
  }

  // Return the offset in data_ just past the end of the current entry.
  inline uint32_t NextEntryOffset() const {
    // NOTE: We don't support files bigger than 2GB
    return static_cast<uint32_t>(CurrentEntry().entry_.value.cend() - data_);
  }

  uint32_t GetRestartBlockOffset(uint32_t index) {
    assert(index < num_restarts_);
    return DecodeFixed32(data_ + restarts_ + index * sizeof(uint32_t));
  }

  inline void MoveToRestartBlock(uint32_t index) {
    restart_index_ = index;
    // current_ will be fixed by ParseNextKey();

    block_entry_cache_idx_ = 0;
    CurrentEntry().key_.Clear();
    CurrentEntry().entry_.Reset();
    CurrentEntry().entry_.value = Slice(data_ + GetRestartBlockOffset(index), 0UL);
  }

  inline void Reset() {
    current_ = restarts_;
    restart_index_ = num_restarts_;
    block_entry_cache_idx_ = 0;
  }

  inline void SetInvalid() {
    Reset();
    CurrentEntry().entry_.Reset();
    CurrentEntry().key_.Clear();
  }

  void SetError(const Status& error);
  void CorruptionError(const std::string& error_details);

  bool ParseNextKey();

  bool BinarySeek(const Slice& target, uint32_t left, uint32_t right, uint32_t* index);

  int CompareBlockKey(uint32_t block_index, const Slice& target);

  bool BinaryBlockIndexSeek(const Slice& target, uint32_t* block_ids,
                            uint32_t left, uint32_t right, uint32_t* index);

  bool HashSeek(const Slice& target, uint32_t* index);

  bool PrefixSeek(const Slice& target, uint32_t* index);
};

// Return an empty iterator (yields nothing) allocated from arena if specified.
BlockIter* NewEmptyBlockIterator(Arena* arena = nullptr);

// Return an empty iterator with the specified status, allocated arena if specified.
BlockIter* NewErrorBlockIterator(const Status& status, Arena* arena = nullptr);

}  // namespace rocksdb
