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

#include <string>

#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/util/coding.h"

namespace rocksdb {

class PossibleArenaDeleter {
 public:
  explicit PossibleArenaDeleter(bool arena_mode) noexcept : arena_mode_(arena_mode) {}

  template <class P>
  void operator()(P* p) {
    if (arena_mode_) {
      p->~P();
    } else {
      delete p;
    }
  }
 private:
  bool arena_mode_;
};

// Skips the entries of the underlying iterator that do not satisfy the filter.
//
// Moving forward, it stops skipping at the read's upper bound: the read ends there, so skipping on
// would only walk filtered out entries nobody reads, e.g. a whole file of them for a point lookup.
// It then stays on the first filtered out entry at or past the bound, but presents it as a
// deletion with sequence number 0. DBIter stops there because of the bound. If the bound is raised
// later without a seek, DBIter takes the deletion as hiding that one filtered out version and moves
// on, and the next Next() resumes skipping from it. So a filtered out entry is never exposed as a
// value, and sequence number 0 orders the deletion after any other version of the same key.
class FilteringIterator : public InternalIterator {
 public:
  FilteringIterator(
      InternalIterator* iterator, bool arena_mode, const Slice* iterate_upper_bound = nullptr)
      : iterator_(iterator, PossibleArenaDeleter(arena_mode)),
        iterate_upper_bound_(iterate_upper_bound) {}

 private:
  const KeyValueEntry& Entry() const override {
    return stopped_at_bound_ ? bound_entry_ : iterator_->Entry();
  }

  const KeyValueEntry& SeekToFirst() override {
    stopped_at_bound_ = false;
    iterator_->SeekToFirst();
    return ApplyFilter(/* backward = */ false);
  }

  const KeyValueEntry& SeekToLast() override {
    stopped_at_bound_ = false;
    iterator_->SeekToLast();
    return ApplyFilter(/* backward = */ true);
  }

  const KeyValueEntry& Seek(Slice target) override {
    stopped_at_bound_ = false;
    iterator_->Seek(target);
    return ApplyFilter(/* backward = */ false);
  }

  const KeyValueEntry& Next() override {
    // When stopped at the bound, the underlying iterator is on the filtered out entry.
    stopped_at_bound_ = false;
    iterator_->Next();
    return ApplyFilter(/* backward = */ false);
  }

  const KeyValueEntry& Prev() override {
    stopped_at_bound_ = false;
    iterator_->Prev();
    return ApplyFilter(/* backward = */ true);
  }

  Status status() const override {
    return iterator_->status();
  }

  Status PinData() override {
    return iterator_->PinData();
  }

  Status ReleasePinnedData() override {
    return iterator_->ReleasePinnedData();
  }

  bool IsKeyPinned() const override {
    return !stopped_at_bound_ && iterator_->IsKeyPinned();
  }

  Status GetProperty(std::string prop_name, std::string* prop) override {
    return iterator_->GetProperty(std::move(prop_name), prop);
  }

  const KeyValueEntry& UpdateFilterKey(Slice user_key_for_filter, Slice seek_key) override {
    if (stopped_at_bound_ && seek_key.empty()) {
      iterator_->UpdateFilterKey(user_key_for_filter, seek_key);
      return bound_entry_;
    }
    stopped_at_bound_ = false;
    return iterator_->UpdateFilterKey(user_key_for_filter, seek_key);
  }

  const KeyValueEntry& ApplyFilter(bool backward) {
    const auto* entry = &iterator_->Entry();
    while (*entry) {
      auto user_key = ExtractUserKey(entry->key);
      if (Satisfied(user_key)) {
        return *entry;
      }
      if (!backward) {
        if (iterate_upper_bound_ && user_key.compare(*iterate_upper_bound_) >= 0) {
          return StopAtBound(user_key);
        }
        entry = &iterator_->Next();
      } else {
        entry = &iterator_->Prev();
      }
    }
    return *entry;
  }

  const KeyValueEntry& StopAtBound(Slice user_key) {
    bound_key_.assign(user_key.cdata(), user_key.size());
    char tail[kLastInternalComponentSize];
    EncodeFixed64(tail, PackSequenceAndType(0, kTypeDeletion));
    bound_key_.append(tail, sizeof(tail));
    bound_entry_.key = Slice(bound_key_);
    bound_entry_.value = Slice(bound_key_.data(), static_cast<size_t>(0));
    stopped_at_bound_ = true;
    return bound_entry_;
  }

  virtual bool Satisfied(Slice user_key) = 0;

  const std::unique_ptr<InternalIterator, PossibleArenaDeleter> iterator_;
  const Slice* const iterate_upper_bound_;
  bool stopped_at_bound_ = false;
  std::string bound_key_;
  KeyValueEntry bound_entry_;
};

} // namespace rocksdb
