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

#include "yb/docdb/bounded_rocksdb_iterator.h"

#include "yb/docdb/key_bounds.h"

#include "yb/rocksdb/db.h"

namespace yb {
namespace docdb {

BoundedRocksDbIterator::BoundedRocksDbIterator(
    rocksdb::DB* rocksdb, const rocksdb::ReadOptions& read_opts,
    const KeyBounds* key_bounds)
    : iterator_(rocksdb->NewIterator(read_opts)), key_bounds_(key_bounds) {
  CHECK_NOTNULL(key_bounds_);
  VLOG(3) << "key_bounds_ = " << AsString(key_bounds_);
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::SeekToFirst() {
  if (key_bounds_->lower.empty()) {
    return FilterEntry(iterator_->SeekToFirst());
  }

  return FilterEntry(iterator_->Seek(key_bounds_->lower));
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::SeekToLast() {
  if (key_bounds_->upper.empty()) {
    return FilterEntry(iterator_->SeekToLast());
  }
  // TODO(tsplit): this code path is only used for post-split tablets, particularly during
  // reverse scan for range-partitioned tables.
  // Need to add unit-test for this scenario when adding unit-tests for tablet splitting of
  // range-partitioned tables.
  const auto& entry = iterator_->Seek(key_bounds_->upper);
  if (entry) {
    return FilterEntry(iterator_->Prev());
  }
  if (iterator_->status().ok()) {
    return FilterEntry(iterator_->SeekToLast());
  }
  return entry;
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::Seek(Slice target) {
  if (!key_bounds_->lower.empty() && target.compare(key_bounds_->lower) < 0) {
    return FilterEntry(iterator_->Seek(key_bounds_->lower));
  }

  if (!key_bounds_->upper.empty() && target.compare(key_bounds_->upper) > 0) {
    return FilterEntry(iterator_->Seek(key_bounds_->upper));
  }

  return FilterEntry(iterator_->Seek(target));
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::Next() {
  return FilterEntry(iterator_->Next());
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::Prev() {
  return FilterEntry(iterator_->Prev());
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::Entry() const {
  return FilterEntry(iterator_->Entry());
}

const rocksdb::KeyValueEntry& BoundedRocksDbIterator::FilterEntry(
    const rocksdb::KeyValueEntry& entry) const {
  if (!entry) {
    return entry;
  }
  if (!key_bounds_->IsWithinBounds(entry.key)) {
    return rocksdb::KeyValueEntry::Invalid();
  }
  return entry;
}

Status BoundedRocksDbIterator::status() const {
  return iterator_->status();
}

void BoundedRocksDbIterator::UseFastNext(bool value) {
  iterator_->UseFastNext(value);
}

}  // namespace docdb
}  // namespace yb
