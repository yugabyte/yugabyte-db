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

#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/db/dbformat.h"

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

class FilteringIterator : public InternalIterator {
 public:
  explicit FilteringIterator(InternalIterator* iterator, bool arena_mode)
      : iterator_(iterator, PossibleArenaDeleter(arena_mode)) {}

 private:
  const KeyValueEntry& Entry() const override {
    return iterator_->Entry();
  }

  const KeyValueEntry& SeekToFirst() override {
    iterator_->SeekToFirst();
    return ApplyFilter(/* backward = */ false);
  }

  const KeyValueEntry& SeekToLast() override {
    iterator_->SeekToLast();
    return ApplyFilter(/* backward = */ true);
  }

  const KeyValueEntry& Seek(Slice target) override {
    iterator_->Seek(target);
    return ApplyFilter(/* backward = */ false);
  }

  const KeyValueEntry& Next() override {
    iterator_->Next();
    return ApplyFilter(/* backward = */ false);
  }

  const KeyValueEntry& Prev() override {
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
    return iterator_->IsKeyPinned();
  }

  Status GetProperty(std::string prop_name, std::string* prop) override {
    return iterator_->GetProperty(std::move(prop_name), prop);
  }

  ScanForwardResult ScanForward(
      const Comparator* user_key_comparator, const Slice& upperbound,
      KeyFilterCallback* key_filter_callback, ScanCallback* scan_callback) override {
    KeyFilterCallback kf_callback = [this, key_filter_callback](
                                        Slice prefixed_key, size_t shared_bytes,
                                        Slice delta) -> KeyFilterCallbackResult {
      // TODO: add support for shared prefix encoded key filter callback.
      LOG_IF(DFATAL, shared_bytes != 0)
          << "Key filter callback with shared prefix is not supported.";
      if (!Satisfied(delta)) {
        return KeyFilterCallbackResult{.skip_key = true, .cache_key = false};
      }

      return key_filter_callback ? (*key_filter_callback)(prefixed_key, shared_bytes, delta)
                                 : KeyFilterCallbackResult{.skip_key = false, .cache_key = false};
    };

    return iterator_->ScanForward(user_key_comparator, upperbound, &kf_callback, scan_callback);
  }

  const KeyValueEntry& ApplyFilter(bool backward) {
    const auto* entry = &iterator_->Entry();
    while (*entry) {
      if (Satisfied(ExtractUserKey(entry->key))) {
        return *entry;
      }
      if (!backward) {
        entry = &iterator_->Next();
      } else {
        entry = &iterator_->Prev();
      }
    }
    return *entry;
  }

  virtual bool Satisfied(Slice user_key) = 0;

  const std::unique_ptr<InternalIterator, PossibleArenaDeleter> iterator_;
};

} // namespace rocksdb
