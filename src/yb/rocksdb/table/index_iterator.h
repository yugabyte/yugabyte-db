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
#include "yb/rocksdb/types.h"

#include "yb/rocksdb/iterator.h"

#include "yb/rocksdb/db/dbformat.h"

namespace rocksdb {

// SST data blocks index keys has structure of (userkey, sequence_number, type) => block handle.
// IndexIterator implements Iterator interface that hides sequence_number and type from the user.
// TODO(index_iter): add arena support.
class IndexIterator : public Iterator {
 public:
  IndexIterator(
      std::unique_ptr<InternalIterator>&& internal_index_iter, SequenceNumber sequence_number)
      : internal_index_iter_(std::move(internal_index_iter)),
        sequence_number_(sequence_number) {}

  IndexIterator(const IndexIterator&) = delete;
  IndexIterator& operator=(const IndexIterator&) = delete;

  virtual ~IndexIterator() {}

  const KeyValueEntry& Entry() const override;

  Status GetProperty(std::string prop_name, std::string* prop) override;
  const KeyValueEntry& Next() override;
  const KeyValueEntry& Prev() override;
  const KeyValueEntry& Seek(Slice target) override;
  const KeyValueEntry& SeekToFirst() override;
  const KeyValueEntry& SeekToLast() override;
  Status status() const override;

  bool ScanForward(
      Slice upperbound, KeyFilterCallback* key_filter_callback,
      ScanCallback* scan_callback) override {
    LOG(FATAL) << "IndexIterator::ScanForward is not supported.";
  }

  void RevalidateAfterUpperBoundChange() override {
    LOG(FATAL) << "IndexIterator::RevalidateAfterUpperBoundChange is not supported.";
  }

  void UseFastNext(bool value) override {
    LOG(FATAL) << "IndexIterator::UseFastNext is not supported.";
  }

 private:
  const KeyValueEntry& UpdateEntryFromInternal(const KeyValueEntry& entry) const;

  std::unique_ptr<InternalIterator> internal_index_iter_;
  SequenceNumber const sequence_number_;
  IterKey target_key_;
  mutable KeyValueEntry entry_;
};

} // namespace rocksdb
