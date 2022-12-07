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

#pragma once

#include "yb/rocksdb/flush_block_policy.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/block_builder.h"
#include "yb/rocksdb/table/format.h"

#include "yb/util/strongly_typed_bool.h"

namespace rocksdb {

using yb::Result;

// The interface for building index.
// Instruction for adding a new concrete IndexBuilder:
//  1. Create a subclass instantiated from IndexBuilder.
//  2. Add a new entry associated with that subclass in IndexType.
//  3. Add a create function for the new subclass in CreateIndexBuilder.
// Note: we can devise more advanced design to simplify the process for adding
// new subclass, which will, on the other hand, increase the code complexity and
// catch unwanted attention from readers. Given that we won't add/change
// indexes frequently, it makes sense to just embrace a more straightforward
// design that just works.
class IndexBuilder {
 public:
  // Create a index builder based on its type.
  static IndexBuilder* CreateIndexBuilder(
      IndexType index_type,
      const Comparator* comparator,
      const SliceTransform* prefix_extractor,
      const BlockBasedTableOptions& table_opt);

  // Index builder will construct a set of blocks which contain:
  //  1. One primary index block.
  //  2. (Optional) a set of metablocks that contains the metadata of the
  //     primary index.
  struct IndexBlocks {
    Slice index_block_contents;
    std::unordered_map<std::string, Slice> meta_blocks;
  };
  explicit IndexBuilder(const Comparator* comparator)
      : comparator_(comparator) {}

  virtual ~IndexBuilder() {}

  // Add a new index entry to index block.
  // To allow further optimization, we provide `last_key_in_current_block` and
  // `first_key_in_next_block`, based on which the specific implementation can
  // determine the best index key to be used for the index block.
  // @last_key_in_current_block: this parameter maybe overridden with the value
  //                             "substitute key".
  // @first_key_in_next_block: it will be nullptr if the entry being added is
  //                           the last one in the table
  //
  // REQUIRES: Finish() has not yet been called.
  virtual void AddIndexEntry(std::string* last_key_in_current_block,
                             const Slice* first_key_in_next_block,
                             const BlockHandle& block_handle) = 0;

  // This method will be called whenever a key is added. The subclasses may
  // override OnKeyAdded() if they need to collect additional information.
  virtual void OnKeyAdded(const Slice& key) {}

  // Inform the index builder that all entries has been written. Block builder
  // may therefore perform any operation required for block finalization.
  //
  // REQUIRES: Finish() has not yet been called.
  virtual Status Finish(IndexBlocks* index_blocks) = 0;

  // Whether it is time to flush the current index block. Overridden in MultiLevelIndexBuilder.
  // While true is returned the caller should keep calling FlushNextBlock(IndexBlocks*,
  // const BlockHandle&) with handle of the block written with the result of the last call to
  // FlushNextBlock and keep writing flushed blocks.
  virtual bool ShouldFlush() const { return false; }

  // This method can be overridden to build the n-th level index in
  // MultiLevelIndexBuilder.
  //
  // Returns true when it actually flushed entries into index_blocks.
  // Returns false when everything was already flushed by previous call to FlushNextBlock.
  virtual Result<bool> FlushNextBlock(
      IndexBlocks* index_blocks, const BlockHandle& last_partition_block_handle);

  // Get the estimated size for index block.
  virtual size_t EstimatedSize() const = 0;

  // Number of levels for index.
  virtual int NumLevels() const = 0;

 protected:
  const Comparator* comparator_;
};

YB_STRONGLY_TYPED_BOOL(ShortenKeys);

// This index builder builds space-efficient index block.
//
// Optimizations:
//  1. Made block's `block_restart_interval` to be 1, which will avoid linear
//     search when doing index lookup (can be disabled by setting
//     index_block_restart_interval).
//  2. Shorten the key length for index block. Other than honestly using the
//     last key in the data block as the index key, we instead find a shortest
//     substitute key that serves the same function.
class ShortenedIndexBuilder : public IndexBuilder {
 public:
  explicit ShortenedIndexBuilder(const Comparator* comparator,
                                 int index_block_restart_interval)
      : IndexBuilder(comparator),
        index_block_builder_(index_block_restart_interval, kIndexBlockKeyValueEncodingFormat) {}

  void AddIndexEntry(
      std::string* last_key_in_current_block,
      const Slice* first_key_in_next_block,
      const BlockHandle& block_handle) override;

  void AddIndexEntry(
      std::string* last_key_in_current_block,
      const Slice* first_key_in_next_block,
      const std::string& block_handle_encoded,
      ShortenKeys shorten_keys);

  Status Finish(IndexBlocks* index_blocks) override;

  size_t EstimatedSize() const override {
    return index_block_builder_.CurrentSizeEstimate();
  }

  int NumLevels() const override { return 1; }

  void Reset() { index_block_builder_.Reset(); }

  const BlockBuilder& GetIndexBlockBuilder() { return index_block_builder_; }

 private:
  BlockBuilder index_block_builder_;
};

// HashIndexBuilder contains a binary-searchable primary index and the
// metadata for secondary hash index construction.
// The metadata for hash index consists two parts:
//  - a metablock that compactly contains a sequence of prefixes. All prefixes
//    are stored consecutively without any metadata (like, prefix sizes) being
//    stored, which is kept in the other metablock.
//  - a metablock contains the metadata of the prefixes, including prefix size,
//    restart index and number of block it spans. The format looks like:
//
// +-----------------+---------------------------+---------------------+ <=prefix 1
// | length: 4 bytes | restart interval: 4 bytes | num-blocks: 4 bytes |
// +-----------------+---------------------------+---------------------+ <=prefix 2
// | length: 4 bytes | restart interval: 4 bytes | num-blocks: 4 bytes |
// +-----------------+---------------------------+---------------------+
// |                                                                   |
// | ....                                                              |
// |                                                                   |
// +-----------------+---------------------------+---------------------+ <=prefix n
// | length: 4 bytes | restart interval: 4 bytes | num-blocks: 4 bytes |
// +-----------------+---------------------------+---------------------+
//
// The reason of separating these two metablocks is to enable the efficiently
// reuse the first metablock during hash index construction without unnecessary
// data copy or small heap allocations for prefixes.
class HashIndexBuilder : public IndexBuilder {
 public:
  explicit HashIndexBuilder(const Comparator* comparator,
                            const SliceTransform* hash_key_extractor,
                            int index_block_restart_interval)
      : IndexBuilder(comparator),
        primary_index_builder_(comparator, index_block_restart_interval),
        hash_key_extractor_(hash_key_extractor) {}

  void AddIndexEntry(
      std::string* last_key_in_current_block,
      const Slice* first_key_in_next_block,
      const BlockHandle& block_handle) override;

  void OnKeyAdded(const Slice& key) override;

  Status Finish(IndexBlocks* index_blocks) override;

  size_t EstimatedSize() const override {
    return primary_index_builder_.EstimatedSize() + prefix_block_.size() +
        prefix_meta_block_.size();
  }

  int NumLevels() const override { return 1; }

 private:
  void FlushPendingPrefix();

  ShortenedIndexBuilder primary_index_builder_;
  const SliceTransform* hash_key_extractor_;

  // stores a sequence of prefixes
  std::string prefix_block_;
  // stores the metadata of prefixes
  std::string prefix_meta_block_;

  // The following 3 variables keeps unflushed prefix and its metadata.
  // The details of block_num and entry_index can be found in
  // "block_hash_index.{h,cc}"
  uint32_t pending_block_num_ = 0;
  uint32_t pending_entry_index_ = 0;
  std::string pending_entry_prefix_;

  uint64_t current_restart_index_ = 0;
};

class MultiLevelIndexBuilder : public IndexBuilder {
 public:
  MultiLevelIndexBuilder(const Comparator* comparator, const BlockBasedTableOptions& table_opt);

  void AddIndexEntry(
      std::string* last_key_in_current_block,
      const Slice* first_key_in_next_block,
      const BlockHandle& block_handle) override {
    AddIndexEntry(
        last_key_in_current_block, first_key_in_next_block, block_handle, ShortenKeys::kTrue);
  }

  void AddIndexEntry(
      std::string* last_key_in_current_block,
      const Slice* first_key_in_next_block,
      const BlockHandle& block_handle,
      ShortenKeys shorten_keys);

  Status Finish(IndexBlocks* index_blocks) override {
    return STATUS(NotSupported, "Finishing as single block is not supported by multi-level index.");
  }

  bool ShouldFlush() const override;

  Result<bool> FlushNextBlock(
      IndexBlocks* index_blocks, const BlockHandle& last_partition_block_handle) override;

  size_t EstimatedSize() const override;

  int NumLevels() const override;

 private:
  struct IndexBlockInfo;

  void EnsureCurrentLevelIndexBuilderCreated();
  Status FlushCurrentLevel(IndexBlocks* index_blocks);
  void AddToNextLevelIfReady(
      IndexBlockInfo* block_info, const BlockHandle& block_handle);

  const BlockBasedTableOptions& table_opt_;

  // Builder for an index block at the current level.
  std::unique_ptr<ShortenedIndexBuilder> current_level_index_block_builder_;

  // Already flushed index block builder, which can only be deleted after the corresponding block is
  // written, because it holds flushed data referenced by a slice returned from FlushNextBlock.
  std::unique_ptr<ShortenedIndexBuilder> finished_index_block_builder_;

  // Builder for index blocks at higher levels.
  std::unique_ptr<MultiLevelIndexBuilder> next_level_index_builder_;
  std::unique_ptr<FlushBlockPolicy> flush_policy_;

  // Whether we've started flushing index blocks, i.e. the FlushNextBlock method has been already
  // called.
  bool flushing_indexes_ = false;

  // Size of index blocks at the current level.
  size_t current_level_size_ = 0;

  struct IndexBlockInfo {
    // Whether the index block is ready for processing, other fields are only correct when
    // is_ready is true.
    bool is_ready = false;
    // Last key in the index block.
    std::string last_key;
    // Whether there are keys for the next block at the same level.
    bool has_next_block = false;
    // First key for the next index block at the same level (only correct if has_next_block is
    // true).
    std::string next_block_first_key;

    bool IsReadyAndLast() const { return is_ready && !has_next_block; }
  };

  // A complete current level index block once it is ready for flush.
  IndexBlockInfo current_level_block_;
  struct {
    // A complete current level index block once it is ready to add to next level index.
    IndexBlockInfo block_to_add;
    // Handle of the last index block flushed at a higher level.
    BlockHandle last_flushed_block_;
    // If an index block from a higher level was flushed during the previous call to
    // FlushNextBlock().
    bool just_flushed = false;
  } next_level_;
  // Buffer for block handle encoding.
  std::string block_handle_encoding_;
};

} // namespace rocksdb
