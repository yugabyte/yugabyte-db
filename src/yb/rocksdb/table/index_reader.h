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

#include <stddef.h>

#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table/block.h"
#include "yb/rocksdb/table/two_level_iterator.h"

#include "yb/util/logging.h"

namespace rocksdb {

using yb::Result;

class BlockBasedTable;
class BlockHandle;
class BlockIter;
class Comparator;
class Env;
class Footer;
class InternalIterator;
class RandomAccessFileReader;

struct TwoLevelBlockIteratorState : public TwoLevelIteratorState {
  explicit TwoLevelBlockIteratorState(bool _check_prefix_may_match)
      : TwoLevelIteratorState(_check_prefix_may_match) {}
  virtual BlockIter* NewSecondaryIterator(const Slice& handle) = 0;
};

// -- IndexReader and its subclasses
// IndexReader is the interface that provide the functionality for index access.
class IndexReader {
 public:
  explicit IndexReader(const ComparatorPtr& comparator)
      : comparator_(comparator) {}

  virtual ~IndexReader() = default;

  // Create an iterator for index access.
  // If not null iter is passed in, implementation was able to update it and it should be used by
  // caller as an index iterator, then nullptr is returned.
  // Otherwise, new iterator is created and returned.
  //
  // For multi-level index:
  // - top level index block iterator is passed and updated instead of the whole index iterator,
  // but return semantic is the same - the whole index iterator is returned.
  // - index_iterator_state is used to create secondary iterators on index.
  virtual InternalIterator* NewIterator(
      BlockIter* iter,
      std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      bool total_order_seek) = 0;

  // Version of NewIterator that creates an index iterator allowing to get more info from the data
  // block this index iterator is pointing to. See DataBlockAwareIndexInternalIterator API
  // description regarding exact functionality.
  // We don't always return DataBlockAwareIndexInternalIterator from IndexReader because:
  // 1) In most cases we don't care about data block content at this level.
  // As of 2024-11 we only use this for improved YSQL ANALYZE query sampling algorithm.
  // 2) For kBinarySearch and kHashSearch index types this enhanced functionality is provided via
  // wrapping usual InternalIterator of an index block into enhanced wrapper which is less
  // performant and requires more memory.
  // So IndexReader::NewDataBlockAwareIterator API should only be used when you need to get index
  // iterator which allows data block info access. In other cases use IndexReader::NewIterator.
  virtual DataBlockAwareIndexInternalIterator* NewDataBlockAwareIterator(
      BlockIter* iter,
      std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state,
      bool total_order_seek) = 0;

  // Returns approximate middle key from the index. Key from the index might not match any key
  // actually written to SST file, because keys could be shortened and substituted before them are
  // written into the index (see ShortenedIndexBuilder).
  virtual Result<std::string> GetMiddleKey() const = 0;

  // The size of the index.
  virtual size_t size() const = 0;
  // Memory usage of the index block
  virtual size_t usable_size() const = 0;

  // Reports an approximation of how much memory has been used other than memory
  // that was allocated in block cache.
  virtual size_t ApproximateMemoryUsage() const = 0;

 protected:
  ComparatorPtr comparator_;
};

// Index that allows binary search lookup for the first key of each block.
// This class can be viewed as a thin wrapper for `Block` class which already
// supports binary search.
class BinarySearchIndexReader : public IndexReader {
 public:
  // Read index from the file and create an instance for
  // `BinarySearchIndexReader`.
  // On success, index_reader will be populated; otherwise it will remain
  // unmodified.
  static Result<std::unique_ptr<BinarySearchIndexReader>> Create(
      RandomAccessFileReader* file, const Footer& footer, const BlockHandle& index_handle, Env* env,
      const ComparatorPtr& comparator, const std::shared_ptr<yb::MemTracker>& mem_tracker);

  InternalIterator* NewIterator(
      BlockIter* iter,
      // Rest of parameters are ignored by BinarySearchIndexReader.
      std::unique_ptr<TwoLevelBlockIteratorState>,
      bool) override {
    auto new_iter =
        index_block_->NewIndexBlockIterator(comparator_.get(), iter, /* total_order_seek = */ true);
    return iter ? nullptr : new_iter;
  }

  void NewIterator(BlockIter* iter) {
    index_block_->NewIndexBlockIterator(comparator_.get(), iter, /* total_order_seek = */ true);
  }

  DataBlockAwareIndexInternalIterator* NewDataBlockAwareIterator(
      BlockIter* iter,
      std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state,
      bool total_order_seek) override;

  size_t size() const override {
    DCHECK(index_block_);
    return index_block_->size();
  }

  size_t usable_size() const override {
    DCHECK(index_block_);
    return index_block_->usable_size();
  }

  size_t ApproximateMemoryUsage() const override {
    DCHECK(index_block_);
    return index_block_->ApproximateMemoryUsage();
  }

  Result<std::string> GetMiddleKey() const override;

 private:
  BinarySearchIndexReader(const ComparatorPtr& comparator,
                          std::unique_ptr<Block>&& index_block)
      : IndexReader(comparator), index_block_(std::move(index_block)) {
    DCHECK(index_block_);
  }

  const std::unique_ptr<Block> index_block_;
};

// Index that leverages an internal hash table to quicken the lookup for a given
// key.
class HashIndexReader : public IndexReader {
 public:
  static yb::Result<std::unique_ptr<IndexReader>> Create(
      const SliceTransform* hash_key_extractor, const Footer& footer, RandomAccessFileReader* file,
      Env* env, const ComparatorPtr& comparator, const BlockHandle& index_handle,
      InternalIterator* meta_index_iter, bool hash_index_allow_collision,
      const std::shared_ptr<yb::MemTracker>& mem_tracker);

  InternalIterator* NewIterator(
      BlockIter* iter = nullptr, std::unique_ptr<TwoLevelBlockIteratorState> state = nullptr,
      bool total_order_seek = true) override {
    auto new_iter = index_block_->NewIndexBlockIterator(comparator_.get(), iter, total_order_seek);
    return iter ? nullptr : new_iter;
  }

  DataBlockAwareIndexInternalIterator* NewDataBlockAwareIterator(
      BlockIter* iter,
      std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state,
      bool total_order_seek) override;

  size_t size() const override {
    DCHECK(index_block_);
    return index_block_->size();
  }

  size_t usable_size() const override {
    DCHECK(index_block_);
    return index_block_->usable_size();
  }

  size_t ApproximateMemoryUsage() const override {
    DCHECK(index_block_);
    return index_block_->ApproximateMemoryUsage() + prefixes_contents_.data.size();
  }

  Result<std::string> GetMiddleKey() const override;

 private:
  HashIndexReader(const ComparatorPtr& comparator, std::unique_ptr<Block>&& index_block)
      : IndexReader(comparator), index_block_(std::move(index_block)) {
    DCHECK(index_block_);
  }

  void OwnPrefixesContents(BlockContents&& prefixes_contents) {
    prefixes_contents_ = std::move(prefixes_contents);
  }

  const std::unique_ptr<Block> index_block_;
  BlockContents prefixes_contents_;
};

// Index that allows binary search lookup in a multi-level index structure.
class MultiLevelIndexReader : public IndexReader {
 public:
  // Read the top level index from the file and create an instance for `MultiLevelIndexReader`.
  static Result<std::unique_ptr<MultiLevelIndexReader>> Create(
      RandomAccessFileReader* file, const Footer& footer, uint32_t num_levels,
      const BlockHandle& top_level_index_handle, Env* env, const ComparatorPtr& comparator,
      const std::shared_ptr<yb::MemTracker>& mem_tracker);

  MultiLevelIndexReader(
      const ComparatorPtr& comparator, uint32_t num_levels,
      std::unique_ptr<Block> top_level_index_block)
      : IndexReader(comparator),
        num_levels_(num_levels),
        top_level_index_block_(std::move(top_level_index_block)) {
    DCHECK_ONLY_NOTNULL(top_level_index_block_.get());
  }

  ~MultiLevelIndexReader() {}

  InternalIterator* NewIterator(
      BlockIter* iter, std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      // MultiLevelIndexReader ignores total_order_seek and always uses total order seek instead of
      // prefix/hash seek (which is used only by HashIndexBuilder/Reader).
      bool /* total_order_seek */) override;

  DataBlockAwareIndexInternalIterator* NewDataBlockAwareIterator(
      BlockIter* iter,
      std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state,
      bool) override;

  Result<std::string> GetMiddleKey() const override;

  uint32_t TEST_GetNumLevels() const {
    return num_levels_;
  }

  uint32_t TEST_GetTopLevelBlockNumRestarts() const {
    return top_level_index_block_->NumRestarts();
  }

 private:
  size_t size() const override { return top_level_index_block_->size(); }

  size_t usable_size() const override {
    return top_level_index_block_->usable_size();
  }

  size_t ApproximateMemoryUsage() const override {
    return top_level_index_block_->ApproximateMemoryUsage();
  }

  const uint32_t num_levels_;
  const std::unique_ptr<Block> top_level_index_block_;
};

} // namespace rocksdb
