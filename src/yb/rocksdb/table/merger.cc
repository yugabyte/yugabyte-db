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

#include "yb/rocksdb/table/merger.h"

#include <vector>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/iter_heap.h"
#include "yb/rocksdb/table/iterator_wrapper.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/autovector.h"
#include "yb/rocksdb/util/heap.h"
#include "yb/rocksdb/util/perf_context_imp.h"

#include "yb/rocksdb/db/dbformat.h"

#include "yb/util/stats/perf_step_timer.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"

namespace rocksdb {
// Without anonymous namespace here, we fail the warning -Wmissing-prototypes
namespace {
typedef BinaryHeap<IteratorWrapper*, MaxIteratorComparator> MergerMaxIterHeap;
typedef BinaryHeap<IteratorWrapper*, MinIteratorComparator> MergerMinIterHeap;
}  // namespace

const size_t kNumIterReserve = 4;

class MergingIterator final : public InternalIterator {
 public:
  MergingIterator(const Comparator* comparator, InternalIterator** children,
                  int n, bool is_arena_mode)
      : data_pinned_(false),
        is_arena_mode_(is_arena_mode),
        comparator_(comparator),
        current_(nullptr),
        direction_(kForward),
        minHeap_(comparator_) {
    children_.resize(n);
    for (int i = 0; i < n; i++) {
      children_[i].Set(children[i]);
    }
    for (auto& child : children_) {
      if (child.Valid()) {
        minHeap_.push(&child);
      }
    }
    CurrentForward();
  }

  virtual void AddIterator(InternalIterator* iter) {
    assert(direction_ == kForward);
    children_.emplace_back(iter);
    if (data_pinned_) {
      Status s = iter->PinData();
      assert(s.ok());
    }
    auto new_wrapper = children_.back();
    if (new_wrapper.Valid()) {
      minHeap_.push(&new_wrapper);
      CurrentForward();
    }
  }

  virtual ~MergingIterator() {
    for (auto& child : children_) {
      child.DeleteIter(is_arena_mode_);
    }
  }

  const KeyValueEntry& SeekToFirst() override {
    ClearHeaps();
    for (auto& child : children_) {
      child.SeekToFirst();
      if (child.Valid()) {
        minHeap_.push(&child);
      }
    }
    direction_ = kForward;
    return CurrentForward();
  }

  const KeyValueEntry& SeekToLast() override {
    ClearHeaps();
    InitMaxHeap();
    for (auto& child : children_) {
      child.SeekToLast();
      if (child.Valid()) {
        maxHeap_->push(&child);
      }
    }
    direction_ = kReverse;
    CurrentReverse();
    return Entry();
  }

  const KeyValueEntry& Seek(Slice target) override {
    if (direction_ == kForward && current_ && current_->Valid()) {
      int key_vs_target = comparator_->Compare(current_->key(), target);
      if (key_vs_target == 0) {
        // We're already at the right key.
        return Entry();
      }
      if (key_vs_target < 0) {
        // This is a "seek forward" operation, and the current key is less than the target. Keep
        // doing a seek on the top iterator and re-adding it to the min heap, until the top iterator
        // gives is a key >= target.
        while (key_vs_target < 0) {
          // For the heap modifications below to be correct, current_ must be the current top of the
          // heap.
          DCHECK_EQ(current_, minHeap_.top());
          const auto& entry = UpdateHeapAfterCurrentAdvancement(current_->Seek(target));
          if (!entry.Valid()) {
            return entry; // Reached the end.
          }
          key_vs_target = comparator_->Compare(entry.key, target);
        }

        // The current key is >= target, this is what we're looking for.
        return Entry();
      }

      // The current key is already greater than the target, so this is not a forward seek.
      // Fall back to a full rebuild of the heap.
    }

    ClearHeaps();
    for (auto& child : children_) {
      {
        PERF_TIMER_GUARD(seek_child_seek_time);
        child.Seek(target);
      }
      PERF_COUNTER_ADD(seek_child_seek_count, 1);

      if (child.Valid()) {
        PERF_TIMER_GUARD(seek_min_heap_time);
        minHeap_.push(&child);
      }
    }
    direction_ = kForward;
    {
      PERF_TIMER_GUARD(seek_min_heap_time);
      CurrentForward();
    }
    return Entry();
  }

  const KeyValueEntry& Next() override {
    assert(Valid());

    // Ensure that all children are positioned after key().
    // If we are moving in the forward direction, it is already
    // true for all of the non-current children since current_ is
    // the smallest child and key() == current_->key().
    if (PREDICT_FALSE(direction_ != kForward)) {
      RebuildForward();
    }

    // For the heap modifications below to be correct, current_ must be the current top of the heap.
    DCHECK_EQ(current_, minHeap_.top());

    // As current_ points to the current record, move the iterator forward.
    return UpdateHeapAfterCurrentAdvancement(current_->Next());
  }

  void RebuildForward() {
    // Otherwise, advance the non-current children.  We advance current_
    // just after the if-block.
    ClearHeaps();
    Slice key = current_->key();
    for (auto& child : children_) {
      if (&child == current_) {
        minHeap_.push(&child);
        continue;
      }
      child.Seek(key);
      if (!child.Valid()) {
        continue;
      }
      if (!comparator_->Equal(key, child.key())) {
        minHeap_.push(&child);
        continue;
      }
      child.Next();
      if (child.Valid()) {
        minHeap_.push(&child);
      }
    }
    min_heap_best_root_child_ = 0;
    direction_ = kForward;

    // The loop advanced all non-current children to be > key() so current_
    // should still be strictly the smallest key.
  }

  const KeyValueEntry& Prev() override {
    assert(Valid());
    // Ensure that all children are positioned before key().
    // If we are moving in the reverse direction, it is already
    // true for all of the non-current children since current_ is
    // the largest child and key() == current_->key().
    if (PREDICT_FALSE(direction_ != kReverse)) {
      // Otherwise, retreat the non-current children.  We retreat current_
      // just after the if-block.
      ClearHeaps();
      InitMaxHeap();
      for (auto& child : children_) {
        if (&child != current_) {
          child.Seek(key());
          if (child.Valid()) {
            // Child is at first entry >= key().  Step back one to be < key()
            TEST_SYNC_POINT_CALLBACK("MergeIterator::Prev:BeforePrev", &child);
            child.Prev();
          } else {
            // Child has no entries >= key().  Position at last entry.
            TEST_SYNC_POINT("MergeIterator::Prev:BeforeSeekToLast");
            child.SeekToLast();
          }
        }
        if (child.Valid()) {
          maxHeap_->push(&child);
        }
      }
      direction_ = kReverse;
      // Note that we don't do assert(current_ == CurrentReverse()) here
      // because it is possible to have some keys larger than the seek-key
      // inserted between Seek() and SeekToLast(), which makes current_ not
      // equal to CurrentReverse().
      CurrentReverse();
    }

    // For the heap modifications below to be correct, current_ must be the
    // current top of the heap.
    DCHECK_EQ(current_, maxHeap_->top());

    current_->Prev();
    if (current_->Valid()) {
      // current is still valid after the Prev() call above.
      // Adjust heap if current_ key goes after other keys.
      if (maxHeap_->down_root().first) {
        return Entry();
      }
    } else {
      // current stopped being valid, remove it from the heap.
      maxHeap_->pop();
    }
    CurrentReverse();
    return Entry();
  }

  const KeyValueEntry& Entry() const override {
    return current_ ? current_->Entry() : KeyValueEntry::Invalid();
  }

  Status status() const override {
    Status s;
    for (auto& child : children_) {
      s = child.status();
      if (!s.ok()) {
        break;
      }
    }
    return s;
  }

  Status PinData() override {
    Status s;
    if (data_pinned_) {
      return s;
    }

    for (size_t i = 0; i < children_.size(); i++) {
      s = children_[i].PinData();
      if (!s.ok()) {
        // We failed to pin an iterator, clean up
        for (size_t j = 0; j < i; j++) {
          WARN_NOT_OK(children_[j].ReleasePinnedData(), "Failed to release pinned data");
        }
        break;
      }
    }
    data_pinned_ = s.ok();
    return s;
  }

  Status ReleasePinnedData() override {
    Status s;
    if (!data_pinned_) {
      return s;
    }

    for (auto& child : children_) {
      Status release_status = child.ReleasePinnedData();
      if (s.ok() && !release_status.ok()) {
        s = release_status;
      }
    }
    data_pinned_ = false;

    return s;
  }

  bool IsKeyPinned() const override {
    assert(Valid());
    return current_->IsKeyPinned();
  }

  ScanForwardResult ScanForward(
      const Comparator* user_key_comparator, const Slice& upperbound,
      KeyFilterCallback* key_filter_callback, ScanCallback* scan_callback) override {
    LOG_IF(DFATAL, !Valid()) << "Iterator should be valid.";

    ScanForwardResult result;
    do {
      const auto key = rocksdb::ExtractUserKey(current_->key());
      if (!upperbound.empty() && user_key_comparator->Compare(key, upperbound) >= 0) {
        break;
      }

      // Compute the next upperbound.
      Slice next_upperbound = upperbound;
      if (minHeap_.size() > 1) {
        auto next_iterator = minHeap_.second_top();
        LOG_IF(DFATAL, !next_iterator->Valid()) << "Second top iterator should be valid.";
        const auto next_user_key = rocksdb::ExtractUserKey(next_iterator->key());
        if (upperbound.empty() || user_key_comparator->Compare(next_user_key, upperbound) < 0) {
          next_upperbound = next_user_key;

          // Handle the duplicate keys. Currently YB RegularDB only has duplicate keys for
          // TransactionApplyState.
          if (key == next_user_key) {
            bool skip_key = false;
            if (key_filter_callback) {
              auto kf_result =
                  (*key_filter_callback)(/*prefixed_key=*/Slice(), /*shared_bytes=*/0, key);
              skip_key = kf_result.skip_key;
            }

            if (!skip_key) {
              if (!(*scan_callback)(key, value())) {
                result.reached_upperbound = false;
                return result;
              }
            }

            Next();
            result.number_of_keys_visited++;
            if (!Valid()) {
              break;
            }
          }
        }
      }

      auto current_result = current_->ScanForward(
          user_key_comparator, next_upperbound, key_filter_callback, scan_callback);
      result.number_of_keys_visited += current_result.number_of_keys_visited;
      if (!current_result.reached_upperbound) {
        result.reached_upperbound = false;
        return result;
      }

      UpdateHeapAfterCurrentAdvancement(current_->Entry());
    } while (Valid());

    result.reached_upperbound = true;
    return result;
  }

 private:
  bool data_pinned_;
  // Clears heaps for both directions, used when changing direction or seeking
  void ClearHeaps();
  // Ensures that maxHeap_ is initialized when starting to go in the reverse
  // direction
  void InitMaxHeap();

  bool is_arena_mode_;
  const Comparator* comparator_;
  autovector<IteratorWrapper, kNumIterReserve> children_;

  // Cached pointer to child iterator with the current key, or nullptr if no
  // child iterators are valid.  This is the top of minHeap_ or maxHeap_
  // depending on the direction.
  IteratorWrapper* current_;
  size_t min_heap_best_root_child_ = 0;
  Slice min_heap_best_root_child_key_;

  // Which direction is the iterator moving?
  enum Direction {
    kForward,
    kReverse
  };
  Direction direction_;
  MergerMinIterHeap minHeap_;
  // Max heap is used for reverse iteration, which is way less common than
  // forward.  Lazily initialize it to save memory.
  std::unique_ptr<MergerMaxIterHeap> maxHeap_;

  const KeyValueEntry& CurrentForward() {
    DCHECK_EQ(direction_, kForward);
    min_heap_best_root_child_ = 0;
    if (!minHeap_.empty()) {
      current_ = minHeap_.top();
      return current_->Entry();
    }
    current_ = nullptr;
    return KeyValueEntry::Invalid();
  }

  void CurrentReverse() {
    DCHECK_EQ(direction_, kReverse);
    assert(maxHeap_);
    current_ = !maxHeap_->empty() ? maxHeap_->top() : nullptr;
  }

  // This should be called after calling Next() or a forward seek on the top element.
  const KeyValueEntry& UpdateHeapAfterCurrentAdvancement(const KeyValueEntry& entry) {
    // current_ is still valid after the previous Next() / forward Seek() call.
    // Adjust heap if current_ key goes after other keys.
    if (entry.Valid() && min_heap_best_root_child_ &&
        comparator_->Compare(entry.key, min_heap_best_root_child_key_) < 0) {
      return entry;
    }
    return DoUpdateHeapAfterCurrentAdvancement(entry);
  }

  // Putting this code in a separate function helps compiler to generate faster code.
  const KeyValueEntry& DoUpdateHeapAfterCurrentAdvancement(const KeyValueEntry& entry) {
    if (entry.Valid()) {
      auto [new_best_child, child] = minHeap_.down_root(min_heap_best_root_child_);
      min_heap_best_root_child_ = new_best_child;
      if (new_best_child) {
        min_heap_best_root_child_key_ = (**DCHECK_NOTNULL(child)).key();
        return entry;
      }
      return CurrentForward();
    }
    // current_ stopped being valid, remove it from the heap.
    minHeap_.pop();
    return CurrentForward();
  }
};

void MergingIterator::ClearHeaps() {
  minHeap_.clear();
  if (maxHeap_) {
    maxHeap_->clear();
  }
}

void MergingIterator::InitMaxHeap() {
  if (!maxHeap_) {
    maxHeap_.reset(new MergerMaxIterHeap(comparator_));
  }
}

InternalIterator* NewMergingIterator(const Comparator* cmp,
                                     InternalIterator** list, int n,
                                     Arena* arena) {
  assert(n >= 0);
  if (n == 0) {
    return NewEmptyInternalIterator(arena);
  } else if (n == 1) {
    return list[0];
  } else {
    if (arena == nullptr) {
      return new MergingIterator(cmp, list, n, false);
    } else {
      auto mem = arena->AllocateAligned(sizeof(MergingIterator));
      return new (mem) MergingIterator(cmp, list, n, true);
    }
  }
}

MergeIteratorBuilder::MergeIteratorBuilder(const Comparator* comparator,
                                           Arena* a)
    : first_iter(nullptr), use_merging_iter(false), arena(a) {

  auto mem = arena->AllocateAligned(sizeof(MergingIterator));
  merge_iter = new (mem) MergingIterator(comparator, nullptr, 0, true);
}

void MergeIteratorBuilder::AddIterator(InternalIterator* iter) {
  if (!use_merging_iter && first_iter != nullptr) {
    merge_iter->AddIterator(first_iter);
    use_merging_iter = true;
  }
  if (use_merging_iter) {
    merge_iter->AddIterator(iter);
  } else {
    first_iter = iter;
  }
}

InternalIterator* MergeIteratorBuilder::Finish() {
  if (!use_merging_iter) {
    return first_iter;
  } else {
    auto ret = merge_iter;
    merge_iter = nullptr;
    return ret;
  }
}

}  // namespace rocksdb
