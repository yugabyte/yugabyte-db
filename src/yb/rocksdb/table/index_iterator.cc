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

#include "yb/rocksdb/table/index_iterator.h"

#include "yb/rocksdb/table/internal_iterator.h"

namespace rocksdb {

const KeyValueEntry& IndexIterator::UpdateEntryFromInternal(
    const KeyValueEntry& entry) const {
  entry_ = entry;
  if (entry_.Valid()) {
    entry_.key = entry_.key.WithoutSuffix(kLastInternalComponentSize);
  }
  return entry_;
}

const KeyValueEntry& IndexIterator::Entry() const {
  return UpdateEntryFromInternal(internal_index_iter_->Entry());
}

const KeyValueEntry& IndexIterator::Next() {
  return UpdateEntryFromInternal(internal_index_iter_->Next());
}

const KeyValueEntry& IndexIterator::Prev() {
  return UpdateEntryFromInternal(internal_index_iter_->Prev());
}

const KeyValueEntry& IndexIterator::Seek(Slice target) {
  target_key_.SetInternalKey(target, sequence_number_);
  return UpdateEntryFromInternal(internal_index_iter_->Seek(target_key_.GetKey()));
}

const KeyValueEntry& IndexIterator::SeekToFirst() {
  return UpdateEntryFromInternal(internal_index_iter_->SeekToFirst());
}

const KeyValueEntry& IndexIterator::SeekToLast() {
  return UpdateEntryFromInternal(internal_index_iter_->SeekToLast());
}

Status IndexIterator::status() const {
  return internal_index_iter_->status();
}

Status IndexIterator::GetProperty(std::string prop_name, std::string* prop) {
  if (prop == nullptr) {
    return STATUS(InvalidArgument, "prop is nullptr");
  }
  return STATUS(InvalidArgument, "Undentified property.");
}

} // namespace rocksdb
