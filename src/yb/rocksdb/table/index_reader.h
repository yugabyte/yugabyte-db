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

#ifndef YB_ROCKSDB_TABLE_INDEX_READER_H
#define YB_ROCKSDB_TABLE_INDEX_READER_H

#include <stddef.h>

#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table/block.h"

namespace rocksdb {

class BlockHandle;
class BlockIter;
class Comparator;
class Env;
class Footer;
class InternalIterator;
class RandomAccessFileReader;

// -- IndexReader and its subclasses
// IndexReader is the interface that provide the functionality for index access.
class IndexReader {
 public:
  explicit IndexReader(const Comparator* comparator)
      : comparator_(comparator) {}

  virtual ~IndexReader() {}

  // Create an iterator for index access.
  // An iter is passed in, if it is not null, update this one and return it
  // If it is null, create a new Iterator
  virtual InternalIterator* NewIterator(BlockIter* iter = nullptr,
                                        bool total_order_seek = true) = 0;

  // The size of the index.
  virtual size_t size() const = 0;
  // Memory usage of the index block
  virtual size_t usable_size() const = 0;

  // Report an approximation of how much memory has been used other than memory
  // that was allocated in block cache.
  virtual size_t ApproximateMemoryUsage() const = 0;

 protected:
  const Comparator* comparator_;
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
  static CHECKED_STATUS Create(
      RandomAccessFileReader* file, const Footer& footer, const BlockHandle& index_handle, Env* env,
      const Comparator* comparator, std::unique_ptr<IndexReader>* index_reader);

  InternalIterator* NewIterator(BlockIter* iter = nullptr, bool dont_care = true) override {
    return index_block_->NewIterator(comparator_, iter, true);
  }

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

 private:
  BinarySearchIndexReader(const Comparator* comparator,
                          std::unique_ptr<Block>&& index_block)
      : IndexReader(comparator), index_block_(std::move(index_block)) {
    DCHECK(index_block_);
  }

  ~BinarySearchIndexReader() {}

  std::unique_ptr<Block> index_block_;
};

// Index that leverages an internal hash table to quicken the lookup for a given
// key.
class HashIndexReader : public IndexReader {
 public:
  static CHECKED_STATUS Create(
      const SliceTransform* hash_key_extractor, const Footer& footer, RandomAccessFileReader* file,
      Env* env, const Comparator* comparator, const BlockHandle& index_handle,
      InternalIterator* meta_index_iter, std::unique_ptr<IndexReader>* index_reader,
      bool hash_index_allow_collision);

  InternalIterator* NewIterator(BlockIter* iter = nullptr, bool total_order_seek = true) override {
    return index_block_->NewIterator(comparator_, iter, total_order_seek);
  }

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

 private:
  HashIndexReader(const Comparator* comparator, std::unique_ptr<Block>&& index_block)
      : IndexReader(comparator), index_block_(std::move(index_block)) {
    DCHECK(index_block_);
  }

  ~HashIndexReader() {}

  void OwnPrefixesContents(BlockContents&& prefixes_contents) {
    prefixes_contents_ = std::move(prefixes_contents);
  }

  std::unique_ptr<Block> index_block_;
  BlockContents prefixes_contents_;
};

} // namespace rocksdb

#endif  // YB_ROCKSDB_TABLE_INDEX_READER_H
