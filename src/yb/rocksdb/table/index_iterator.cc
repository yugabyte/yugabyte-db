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

#include "yb/rocksdb/table/index_iterator.h"

#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/merger.h"

namespace rocksdb {

template class MergingIterator<DataBlockAwareIndexInternalIterator>;

// Keys of SST data blocks and data index blocks have structure of (user_key, sequence_number,
// type).
// UserKeyIterator provides implementation of Iterator interface that ignores and hides
// sequence_number and type from the user.
class UserKeyIterator : public Iterator {
 public:
  explicit UserKeyIterator(InternalIterator* internal_iter)
      : internal_iter_(internal_iter) {}

  UserKeyIterator(const UserKeyIterator&) = delete;
  UserKeyIterator& operator=(const UserKeyIterator&) = delete;

  virtual ~UserKeyIterator() {}

  const KeyValueEntry& Entry() const override {
    return UpdateEntryFromInternal(internal_iter_->Entry());
  }

  const KeyValueEntry& Next() override {
    return UpdateEntryFromInternal(internal_iter_->Next());
  }

  const KeyValueEntry& Prev() override {
    return UpdateEntryFromInternal(internal_iter_->Prev());
  }

  const KeyValueEntry& Seek(Slice target) override {
    return UpdateEntryFromInternal(
        internal_iter_->Seek(InternalKey::MaxPossibleForUserKey(target).Encode()));
  }

  const KeyValueEntry& SeekToFirst() override {
    return UpdateEntryFromInternal(internal_iter_->SeekToFirst());
  }

  const KeyValueEntry& SeekToLast() override {
    return UpdateEntryFromInternal(internal_iter_->SeekToLast());
  }

  Status status() const override {
    return internal_iter_->status();
  }

  Status GetProperty(std::string prop_name, std::string* prop) override {
    if (prop == nullptr) {
      return STATUS(InvalidArgument, "prop is nullptr");
    }
    return STATUS(InvalidArgument, "Undentified property");
  }

  void RevalidateAfterUpperBoundChange() override {
    LOG(FATAL) << "UserIterator::RevalidateAfterUpperBoundChange is not supported";
  }

  void UseFastNext(bool value) override {
    LOG(FATAL) << "UserIterator::UseFastNext is not supported";
  }

  void UpdateFilterKey(Slice user_key_for_filter, Slice seek_key) override {
    LOG(FATAL) << "UserIterator::UpdateFilterKey is not supported";
  }

 private:
  const KeyValueEntry& UpdateEntryFromInternal(const KeyValueEntry& entry) const {
    entry_ = entry;
    if (entry_.Valid()) {
      entry_.key = entry_.key.WithoutSuffix(kLastInternalComponentSize);
    }
    return entry_;
  }

  InternalIterator* internal_iter_;
  mutable KeyValueEntry entry_;
};

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::IndexIteratorBase(
    std::unique_ptr<IndexInternalIteratorType>&& internal_index_iter)
    : internal_index_iter_(std::move(internal_index_iter)),
      index_iter_(std::make_unique<UserKeyIterator>(internal_index_iter_.get())) {}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::~IndexIteratorBase() {}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
const KeyValueEntry& IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::Entry()
    const {
  return index_iter_->Entry();
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
const KeyValueEntry& IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::Next() {
  return index_iter_->Next();
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
const KeyValueEntry& IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::Prev() {
  return index_iter_->Prev();
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
const KeyValueEntry& IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::Seek(
    Slice target) {
  return index_iter_->Seek(target);
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
const KeyValueEntry&
IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::SeekToFirst() {
  return index_iter_->SeekToFirst();
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
const KeyValueEntry&
IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::SeekToLast() {
  return index_iter_->SeekToLast();
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
Status IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::status() const {
  return index_iter_->status();
}

template <typename IndexIteratorBaseType, typename IndexInternalIteratorType>
Status IndexIteratorBase<IndexIteratorBaseType, IndexInternalIteratorType>::GetProperty(
    std::string prop_name, std::string* prop) {
  return index_iter_->GetProperty(prop_name, prop);
}

namespace {

inline void TrimLastInternalComponent(std::string* internal_key) {
  DCHECK_GE(internal_key->size(), kLastInternalComponentSize);
  internal_key->resize(std::max<size_t>(0, internal_key->size() - kLastInternalComponentSize));
}

} // namespace

yb::Result<std::pair<std::string, std::string>>
DataBlockAwareIndexIteratorImpl::GetCurrentDataBlockBounds() const {
  DCHECK(internal_index_iter_->Valid());
  auto bounds =
      VERIFY_RESULT(internal_index_iter_->GetCurrentIterator()->GetCurrentDataBlockBounds());
  for (auto* bound : {&bounds.first, &bounds.second}) {
    TrimLastInternalComponent(bound);
  }
  return bounds;
}

template class IndexIteratorBase<Iterator, InternalIterator>;
template class IndexIteratorBase<
    DataBlockAwareIndexIterator, MergingIterator<DataBlockAwareIndexInternalIterator>>;

} // namespace rocksdb
