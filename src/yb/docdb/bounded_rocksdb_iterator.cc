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

bool BoundedRocksDbIterator::Valid() const {
  return iterator_->Valid() && key_bounds_->IsWithinBounds(key());
}

void BoundedRocksDbIterator::SeekToFirst() {
  if (!key_bounds_->lower.empty()) {
    iterator_->Seek(key_bounds_->lower);
  } else {
    iterator_->SeekToFirst();
  }
}

void BoundedRocksDbIterator::SeekToLast() {
  if (!key_bounds_->upper.empty()) {
    // TODO(tsplit): this code path is only used for post-split tablets, particularly during
    // reverse scan for range-partitioned tables.
    // Need to add unit-test for this scenario when adding unit-tests for tablet splitting of
    // range-partitioned tables.
    iterator_->Seek(key_bounds_->upper);
    if (iterator_->Valid()) {
      iterator_->Prev();
    } else {
      iterator_->SeekToLast();
    }
  } else {
    iterator_->SeekToLast();
  }
}

void BoundedRocksDbIterator::Seek(const Slice& target) {
  if (!key_bounds_->lower.empty() && target.compare(key_bounds_->lower) < 0) {
    iterator_->Seek(key_bounds_->lower);
  } else if (!key_bounds_->upper.empty() && target.compare(key_bounds_->upper) > 0) {
    iterator_->Seek(key_bounds_->upper);
  } else {
    iterator_->Seek(target);
  }
}

void BoundedRocksDbIterator::Next() {
  iterator_->Next();
}

void BoundedRocksDbIterator::Prev() {
  iterator_->Prev();
}

Slice BoundedRocksDbIterator::key() const {
  return iterator_->key();
}

Slice BoundedRocksDbIterator::value() const {
  return iterator_->value();
}

Status BoundedRocksDbIterator::status() const {
  return iterator_->status();
}

}  // namespace docdb
}  // namespace yb
