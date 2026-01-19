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

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/dockv_fwd.h"

#include "yb/util/slice.h"

namespace yb::docdb {

struct SeekStats {
  int num_seeks = 0;
  int num_nexts = 0;
  int num_next_optimization_useful = 0;
  int num_next_optimization_not_useful = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        num_seeks, num_nexts, num_next_optimization_useful, num_next_optimization_not_useful);
  }
};

template <typename IteratorType>
class OptimizedRocksDbIterator {
 public:
  OptimizedRocksDbIterator() {}
  explicit OptimizedRocksDbIterator(IteratorType&& rocksdb_iter)
      : rocksdb_iter_(std::move(rocksdb_iter)) {}

  IteratorType* operator->() { return &rocksdb_iter_; }
  IteratorType& rocksdb_iter() { return rocksdb_iter_; }

  // Seek to a rocksdb point that is at least sub_doc_key.
  // If the iterator is already positioned far enough, does not perform a seek.
  const rocksdb::KeyValueEntry& SeekForward(Slice target);
  const rocksdb::KeyValueEntry& SeekForward(const dockv::KeyBytes& key_bytes);

  // Seeks to the latest record which is strictly less than upper_bound_key (in other words, seeks
  // to a record the closest to upper_bound_key but strictly less than upper_bound_key).
  // Does not perform a seek if the iterator is already positioned before the upper_bound_key.
  const rocksdb::KeyValueEntry& SeekBackward(Slice upper_bound_key);

  // When we replace HybridTime::kMin in the end of seek key, next seek will skip older versions of
  // this key, but will not skip any subkeys in its subtree. If the iterator is already positioned
  // far enough, does not perform a seek.
  const rocksdb::KeyValueEntry& SeekPastSubKey(Slice key);

  // Seek out of the given SubDocKey. For efficiency, the method that takes a non-const KeyBytes
  // pointer avoids memory allocation by using the KeyBytes buffer to prepare the key to seek to by
  // appending an extra byte. The appended byte is removed when the method returns.
  const rocksdb::KeyValueEntry& SeekOutOfSubKey(dockv::KeyBytes* key_bytes);

  // A wrapper around the RocksDB seek operation that uses Next() up to the configured number of
  // times to avoid invalidating iterator state. In debug mode it also allows printing detailed
  // information about RocksDB seeks.
  const rocksdb::KeyValueEntry& PerformRocksDBSeek(Slice seek_key, const char* file_name, int line);

  void SetAvoidUselessNextInsteadOfSeek(
      AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek) {
    avoid_useless_next_instead_of_seek_ = avoid_useless_next_instead_of_seek;
  }

 private:
  const rocksdb::KeyValueEntry& DoSeekForward(
      Slice target, AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek);

  IteratorType rocksdb_iter_;

  // Whether to avoid trying to use next instead of seek when it is not useful (see
  // SkipTryingNextInsteadOfSeek function).
  AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek_ =
      AvoidUselessNextInsteadOfSeek::kFalse;

  // Statistics for next instead of seek optimization.
  SeekStats seek_stats_;
};

// TODO: is there too much overhead in passing file name and line here in release mode?
#define ROCKSDB_SEEK(iter, key) \
  iter.PerformRocksDBSeek((key), __FILE__, __LINE__)

}  // namespace yb::docdb
