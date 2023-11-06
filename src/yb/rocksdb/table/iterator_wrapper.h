//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once

#include <set>

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/rocksdb/table/internal_iterator.h"

namespace rocksdb {

// A internal wrapper class with an interface similar to Iterator that
// caches the valid() and key() results for an underlying iterator.
// This can help avoid virtual function calls and also gives better
// cache locality.
template<bool kSkipLastEntry>
class IteratorWrapperBase {
 public:
  IteratorWrapperBase() : entry_(&KeyValueEntry::Invalid()) {}
  explicit IteratorWrapperBase(InternalIterator* _iter) {
    Set(_iter);
  }
  ~IteratorWrapperBase() {}
  InternalIterator* iter() const { return iter_; }

  // Takes the ownership of "_iter" and will delete it when destroyed.
  // Next call to Set() will destroy "_iter" except if PinData() was called.
  void Set(InternalIterator* _iter) {
    if (iters_pinned_ && iter_) {
      // keep old iterator until ReleasePinnedData() is called
      pinned_iters_.insert(iter_);
    } else {
      delete iter_;
    }

    iter_ = _iter;
    if (iter_ == nullptr) {
      entry_ = &KeyValueEntry::Invalid();
    } else {
      Update(iter_->Entry());
      if (iters_pinned_) {
        // Pin new iterator
        Status s = iter_->PinData();
        assert(s.ok());
      }
    }
  }

  Status PinData() {
    Status s;
    if (iters_pinned_) {
      return s;
    }

    if (iter_) {
      s = iter_->PinData();
    }

    if (s.ok()) {
      iters_pinned_ = true;
    }

    return s;
  }

  Status ReleasePinnedData() {
    Status s;
    if (!iters_pinned_) {
      return s;
    }

    if (iter_) {
      s = iter_->ReleasePinnedData();
    }

    if (s.ok()) {
      iters_pinned_ = false;
      // No need to call ReleasePinnedData() for pinned_iters_
      // since we will delete them
      DeletePinnedIterators(false);
    }

    return s;
  }

  bool IsKeyPinned() const {
    assert(iter_);
    return iters_pinned_ && iter_->IsKeyPinned();
  }

  void DeleteIter(bool is_arena_mode) {
    if (iter_ && pinned_iters_.find(iter_) == pinned_iters_.end()) {
      DestroyIterator(iter_, is_arena_mode);
    }
    DeletePinnedIterators(is_arena_mode);
  }

  // Iterator interface methods
  bool Valid() const {
    return entry_->Valid();
  }

  Slice key() const {
    return entry_->key;
  }

  Slice value() const {
    return entry_->value;
  }

  const KeyValueEntry& Entry() const {
    return *entry_;
  }

  // Methods below require iter() != nullptr
  Status status() const     { assert(iter_); return iter_->status(); }

  const KeyValueEntry& Next() {
    DCHECK(iter_);
    return Update(SkipLastIfNecessary(iter_->Next()));
  }

  const KeyValueEntry& Prev() {
    DCHECK(iter_);
    return Update(iter_->Prev());
  }

  const KeyValueEntry& Seek(const Slice& k) {
    DCHECK(iter_);
    return Update(SkipLastIfNecessary(iter_->Seek(k)));
  }

  const KeyValueEntry& SeekToFirst() {
    DCHECK(iter_);
    return Update(SkipLastIfNecessary(iter_->SeekToFirst()));
  }

  const KeyValueEntry& SeekToLast() {
    DCHECK(iter_);
    const auto& last_entry = iter_->SeekToLast();
    if (!kSkipLastEntry || !last_entry.Valid()) {
      return Update(last_entry);
    }
    return Update(iter_->Prev());
  }

  ScanForwardResult ScanForward(
      const Comparator* user_key_comparator, const Slice& upperbound,
      KeyFilterCallback* key_filter_callback, ScanCallback* scan_callback) {
    if (kSkipLastEntry) {
      LOG(FATAL)
          << "IteratorWrapperBase</* kSkipLastEntry = */ true>::ScanForward is not supported";
    }
    LOG_IF(DFATAL, !iter_) << "Iterator is invalid";
    auto result =
        iter_->ScanForward(user_key_comparator, upperbound, key_filter_callback, scan_callback);
    Update(iter_->Entry());
    return result;
  }

 private:
  inline const KeyValueEntry& SkipLastIfNecessary(const KeyValueEntry& entry) {
    if (!kSkipLastEntry || !entry.Valid()) {
      return entry;
    }
    const auto& next_entry = iter_->Next();
    return next_entry.Valid() ? iter_->Prev() : next_entry;
  }

  const KeyValueEntry& Update(const KeyValueEntry& entry) {
    entry_ = &entry;
    return entry;
  }

  void DeletePinnedIterators(bool is_arena_mode) {
    for (auto it : pinned_iters_) {
      DestroyIterator(it, is_arena_mode);
    }
    pinned_iters_.clear();
  }

  inline void DestroyIterator(InternalIterator* it, bool is_arena_mode) {
    if (!is_arena_mode) {
      delete it;
    } else {
      it->~InternalIterator();
    }
  }

  InternalIterator* iter_ = nullptr;
  // If set to true, current and future iterators wont be deleted.
  bool iters_pinned_ = false;
  // List of past iterators that are pinned and wont be deleted as long as
  // iters_pinned_ is true. When we are pinning iterators this set will contain
  // iterators of previous data blocks to keep them from being deleted.
  std::set<InternalIterator*> pinned_iters_;
  const KeyValueEntry* entry_;
};

class Arena;
// Return an empty iterator (yields nothing) allocated from arena.
extern InternalIterator* NewEmptyInternalIterator(Arena* arena);

// Return an empty iterator with the specified status, allocated arena.
extern InternalIterator* NewErrorInternalIterator(const Status& status,
                                                  Arena* arena);

}  // namespace rocksdb
