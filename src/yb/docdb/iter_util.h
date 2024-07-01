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

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/slice.h"

namespace yb::docdb {

// Seek to a rocksdb point that is at least sub_doc_key.
// If the iterator is already positioned far enough, does not perform a seek.
const rocksdb::KeyValueEntry& SeekForward(Slice slice, rocksdb::Iterator *iter);

const rocksdb::KeyValueEntry& SeekForward(
    const dockv::KeyBytes& key_bytes, rocksdb::Iterator *iter);

struct SeekStats {
  int next = 0;
  int seek = 0;
};

// Seek forward using Next call.
SeekStats SeekPossiblyUsingNext(rocksdb::Iterator* iter, Slice seek_key);

// Seeks to the latest record which is strictly less than upper_bound_key (in other words, seeks
// to a record most closest to upper_bound_key but strictly less than upper_bound_key).
// Does not perform a seek if the iterator is already positioned before the upper_bound_key.
const rocksdb::KeyValueEntry& SeekBackward(Slice upper_bound_key, rocksdb::Iterator& iter);

// When we replace HybridTime::kMin in the end of seek key, next seek will skip older versions of
// this key, but will not skip any subkeys in its subtree. If the iterator is already positioned far
// enough, does not perform a seek.
const rocksdb::KeyValueEntry& SeekPastSubKey(Slice key, rocksdb::Iterator* iter);

// Seek out of the given SubDocKey. For efficiency, the method that takes a non-const KeyBytes
// pointer avoids memory allocation by using the KeyBytes buffer to prepare the key to seek to by
// appending an extra byte. The appended byte is removed when the method returns.
const rocksdb::KeyValueEntry& SeekOutOfSubKey(dockv::KeyBytes* key_bytes, rocksdb::Iterator* iter);

// A wrapper around the RocksDB seek operation that uses Next() up to the configured number of
// times to avoid invalidating iterator state. In debug mode it also allows printing detailed
// information about RocksDB seeks.
const rocksdb::KeyValueEntry& PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    Slice seek_key,
    const char* file_name,
    int line);

// TODO: is there too much overhead in passing file name and line here in release mode?
#define ROCKSDB_SEEK(iter, key) PerformRocksDBSeek((iter), (key), __FILE__, __LINE__)

}  // namespace yb::docdb
