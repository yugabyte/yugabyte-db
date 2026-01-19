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
#include "yb/rocksdb/types.h"

#include "yb/rocksdb/iterator.h"

#include "yb/rocksdb/db/dbformat.h"

namespace rocksdb {

class UserKeyIterator;

// SST data blocks index entries has structure of (userkey, sequence_number, type) => block handle.
// IndexIterator implements Iterator interface that ignores and hides sequence_number and type from
// the user.
// TODO(index_iter): add arena support.
template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
class IndexIteratorBase : public IndexIteratorBaseType {
 public:
  using Base = IndexIteratorBaseType;
  explicit IndexIteratorBase(
      std::unique_ptr<IndexInternalIteratorType>&& internal_index_iter);

  IndexIteratorBase(const IndexIteratorBase&) = delete;
  IndexIteratorBase& operator=(const IndexIteratorBase&) = delete;

  virtual ~IndexIteratorBase();

  const KeyValueEntry& Entry() const override;

  Status GetProperty(std::string prop_name, std::string* prop) override;
  const KeyValueEntry& Next() override;
  const KeyValueEntry& Prev() override;
  const KeyValueEntry& Seek(Slice target) override;
  const KeyValueEntry& SeekToFirst() override;
  const KeyValueEntry& SeekToLast() override;
  Status status() const override;

  void RevalidateAfterUpperBoundChange() override {
    LOG(FATAL) << "IndexIteratorBase::RevalidateAfterUpperBoundChange is not supported";
  }

  void UseFastNext(bool value) override {
    LOG(FATAL) << "IndexIteratorBase::UseFastNext is not supported";
  }

  void UpdateFilterKey(Slice user_key_for_filter, Slice seek_key) override {
    LOG(FATAL) << "IndexIteratorBase::UpdateFilterKey is not supported";
  }

 protected:
  std::unique_ptr<IndexInternalIteratorType> internal_index_iter_;
  std::unique_ptr<UserKeyIterator> index_iter_;
};

using IndexIteratorImpl = IndexIteratorBase<Iterator, InternalIterator>;

class DataBlockAwareIndexIteratorImpl
    : public IndexIteratorBase<
          DataBlockAwareIndexIterator, MergingIterator<DataBlockAwareIndexInternalIterator>> {
 public:
  explicit DataBlockAwareIndexIteratorImpl(
      std::unique_ptr<MergingIterator<DataBlockAwareIndexInternalIterator>>&& internal_index_iter)
      : IndexIteratorBase(std::move(internal_index_iter)) {}

  yb::Result<std::pair<std::string, std::string>> GetCurrentDataBlockBounds() const override;
};

} // namespace rocksdb
