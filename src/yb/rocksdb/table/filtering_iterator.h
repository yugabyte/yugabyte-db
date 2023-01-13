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
  bool Valid() const override {
    return iterator_->Valid();
  }

  void SeekToFirst() override {
    iterator_->SeekToFirst();
    ApplyFilter(/* backward = */ false);
  }

  void SeekToLast() override {
    iterator_->SeekToLast();
    ApplyFilter(/* backward = */ true);
  }

  void Seek(const Slice& target) override {
    iterator_->Seek(target);
    ApplyFilter(/* backward = */ false);
  }

  void Next() override {
    iterator_->Next();
    ApplyFilter(/* backward = */ false);
  }

  void Prev() override {
    iterator_->Prev();
    ApplyFilter(/* backward = */ true);
  }

  Slice key() const override {
    return iterator_->key();
  }

  Slice value() const override {
    return iterator_->value();
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

  bool ScanForward(
      const Comparator* user_key_comparator, const Slice& upperbound,
      KeyFilterCallback* key_filter_callback, ScanCallback* scan_callback) override {
    KeyFilterCallback kf_callback = [this, key_filter_callback](
                                        const Slice& prefixed_key, size_t shared_bytes,
                                        const Slice& delta) -> KeyFilterCallbackResult {
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

  void ApplyFilter(bool backward) {
    while (iterator_->Valid()) {
      if (Satisfied(ExtractUserKey(iterator_->key()))) {
        break;
      }
      if (!backward) {
        iterator_->Next();
      } else {
        iterator_->Prev();
      }
    }
  }

  virtual bool Satisfied(Slice user_key) = 0;

  const std::unique_ptr<InternalIterator, PossibleArenaDeleter> iterator_;
};

} // namespace rocksdb
