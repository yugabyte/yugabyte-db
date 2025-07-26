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

#include "yb/rocksdb/table/two_level_iterator.h"

#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/iterator_wrapper.h"
#include "yb/rocksdb/util/arena.h"

#include "yb/rocksdb/db/dbformat.h"

namespace rocksdb {

namespace {

class TwoLevelIterator final : public InternalIterator {
 public:
  TwoLevelIterator(
      TwoLevelIteratorState* state, InternalIterator* first_level_iter,
      bool need_free_iter_and_state, SkipCorruptDataBlocksUnsafe skip_corrupt_data_blocks_unsafe);

  virtual ~TwoLevelIterator() {
    first_level_iter_.DeleteIter(!need_free_iter_and_state_);
    second_level_iter_.DeleteIter(false);
    if (need_free_iter_and_state_) {
      delete state_;
    } else {
      state_->~TwoLevelIteratorState();
    }
  }

  const KeyValueEntry& Seek(Slice target) override;
  const KeyValueEntry& SeekToFirst() override;
  const KeyValueEntry& SeekToLast() override;
  const KeyValueEntry& Next() override;
  const KeyValueEntry& Prev() override;

  const KeyValueEntry& Entry() const override { return second_level_iter_.Entry(); }

  Status status() const override {
    // It'd be nice if status() returned a const Status& instead of a Status
    if (!first_level_iter_.status().ok()) {
      return first_level_iter_.status();
    } else if (second_level_iter_.iter() != nullptr &&
               !second_level_iter_.status().ok()) {
      return second_level_iter_.status();
    } else {
      return status_;
    }
  }

  Status PinData() override { return second_level_iter_.PinData(); }
  Status ReleasePinnedData() override {
    return second_level_iter_.ReleasePinnedData();
  }
  bool IsKeyPinned() const override {
    return second_level_iter_.iter() ? second_level_iter_.IsKeyPinned() : false;
  }

  bool MatchFilter(
      const IteratorFilter* filter, const QueryOptions& options, Slice key,
      FilterKeyCache* cache) override {
    return state_->MatchFilter(filter, options, key, cache);
  }

 private:
  bool ShouldReturnError(const Status& s) {
    return !s.ok() && !(skip_corrupt_data_blocks_unsafe_ && s.IsCorruption());
  }

  void SaveError(const Status& s) {
    if (status_.ok() && ShouldReturnError(s)) {
      status_ = s;
    }
  }
  const KeyValueEntry& DoSkipEmptyDataBlocksForward(const KeyValueEntry* entry);
  void SkipEmptyDataBlocksBackward();
  void SetSecondLevelIterator(InternalIterator* iter);
  bool InitDataBlock();

  const KeyValueEntry& SkipEmptyDataBlocksForward(const KeyValueEntry& entry) {
    if (PREDICT_TRUE(entry.Valid())) {
      return entry;
    }
    return DoSkipEmptyDataBlocksForward(&entry);
  }

  TwoLevelIteratorState* state_;
  IteratorWrapper first_level_iter_;
  IteratorWrapper second_level_iter_;  // May be nullptr
  bool need_free_iter_and_state_;
  SkipCorruptDataBlocksUnsafe skip_corrupt_data_blocks_unsafe_;
  Status status_;
  // If second_level_iter is non-nullptr, then "data_block_handle_" holds the
  // "index_value" passed to block_function_ to create the second_level_iter.
  std::string data_block_handle_;
};

TwoLevelIterator::TwoLevelIterator(
    TwoLevelIteratorState* state, InternalIterator* first_level_iter, bool need_free_iter_and_state,
    SkipCorruptDataBlocksUnsafe skip_corrupt_data_blocks_unsafe)
    : state_(state),
      first_level_iter_(first_level_iter),
      need_free_iter_and_state_(need_free_iter_and_state),
      skip_corrupt_data_blocks_unsafe_(skip_corrupt_data_blocks_unsafe) {}

const KeyValueEntry& TwoLevelIterator::Seek(Slice target) {
  if (state_->check_prefix_may_match &&
      !state_->PrefixMayMatch(target)) {
    SetSecondLevelIterator(nullptr);
    return Entry();
  }
  first_level_iter_.Seek(target);

  if (!InitDataBlock()) {
    return KeyValueEntry::Invalid();
  }
  return SkipEmptyDataBlocksForward(second_level_iter_.Seek(target));
}

const KeyValueEntry& TwoLevelIterator::SeekToFirst() {
  first_level_iter_.SeekToFirst();
  if (!InitDataBlock()) {
    return KeyValueEntry::Invalid();
  }
  return SkipEmptyDataBlocksForward(second_level_iter_.SeekToFirst());
}

const KeyValueEntry& TwoLevelIterator::SeekToLast() {
  first_level_iter_.SeekToLast();
  InitDataBlock();
  if (second_level_iter_.iter() != nullptr) {
    second_level_iter_.SeekToLast();
  }
  SkipEmptyDataBlocksBackward();
  return Entry();
}

const KeyValueEntry& TwoLevelIterator::Next() {
  assert(Valid());
  return SkipEmptyDataBlocksForward(second_level_iter_.Next());
}

const KeyValueEntry& TwoLevelIterator::Prev() {
  assert(Valid());
  second_level_iter_.Prev();
  SkipEmptyDataBlocksBackward();
  return Entry();
}

const KeyValueEntry& TwoLevelIterator::DoSkipEmptyDataBlocksForward(const KeyValueEntry* entry) {
  for (;;) {
    if (entry->Valid() || ShouldReturnError(second_level_iter_.status())) {
      return *entry;
    }
    // Move to next block
    if (!first_level_iter_.Valid()) {
      SetSecondLevelIterator(nullptr);
      return KeyValueEntry::Invalid();
    }
    first_level_iter_.Next();
    if (!InitDataBlock()) {
      return KeyValueEntry::Invalid();
    }
    entry = &second_level_iter_.SeekToFirst();
  }
}

void TwoLevelIterator::SkipEmptyDataBlocksBackward() {
  while (second_level_iter_.iter() == nullptr ||
         (!second_level_iter_.Valid() && !ShouldReturnError(second_level_iter_.status()))) {
    // Move to next block
    if (!first_level_iter_.Valid()) {
      SetSecondLevelIterator(nullptr);
      return;
    }
    first_level_iter_.Prev();
    if (InitDataBlock()) {
      second_level_iter_.SeekToLast();
    }
  }
}

void TwoLevelIterator::SetSecondLevelIterator(InternalIterator* iter) {
  if (second_level_iter_.iter() != nullptr) {
    SaveError(second_level_iter_.status());
  }
  second_level_iter_.Set(iter);
}

bool TwoLevelIterator::InitDataBlock() {
  if (PREDICT_FALSE(!first_level_iter_.Valid())) {
    SetSecondLevelIterator(nullptr);
    return false;
  }

  Slice handle = first_level_iter_.value();
  if (second_level_iter_.iter() != nullptr &&
      !second_level_iter_.status().IsIncomplete() &&
      handle.compare(data_block_handle_) == 0) {
    // second_level_iter is already constructed with this iterator, so
    // no need to change anything
  } else {
    // TODO(secondary_iter): consider updating existing iterator rather than recreating, measure
    // potential perf impact.
    InternalIterator* iter = state_->NewSecondaryIterator(handle);
    data_block_handle_.assign(handle.cdata(), handle.size());
    SetSecondLevelIterator(iter);
  }

  return true;
}

}  // namespace

InternalIterator* NewTwoLevelIterator(
    TwoLevelIteratorState* state, InternalIterator* first_level_iter, Arena* arena,
    bool need_free_iter_and_state, SkipCorruptDataBlocksUnsafe skip_corrupt_data_blocks_unsafe) {
  if (arena == nullptr) {
    return new TwoLevelIterator(
        state, first_level_iter, need_free_iter_and_state, skip_corrupt_data_blocks_unsafe);
  } else {
    auto mem = arena->AllocateAligned(sizeof(TwoLevelIterator));
    return new (mem) TwoLevelIterator(
        state, first_level_iter, need_free_iter_and_state, skip_corrupt_data_blocks_unsafe);
  }
}

}  // namespace rocksdb
