//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/gutil/stl_util.h"

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

constexpr size_t kNumIterReserve = 4;

template <typename IteratorWrapperType>
using MergerMaxIterHeap =
    BinaryHeap<IteratorWrapperType*, MaxIteratorComparator<IteratorWrapperType>>;

template <typename IteratorWrapperType>
using MergerMinIterHeap =
    BinaryHeap<IteratorWrapperType*, MinIteratorComparator<IteratorWrapperType>>;

template <class IteratorWrapperType>
class IteratorWithSeekBackwardCounter : public IteratorWrapperType {
 public:
  template <class... Args>
  explicit IteratorWithSeekBackwardCounter(Args&&... args)
      : IteratorWrapperType(std::forward<Args>(args)...) {}

  size_t seek_backward_counter() const {
    return seek_backward_counter_;
  }

  void set_seek_backward_counter(size_t value) {
    seek_backward_counter_ = value;
  }

 private:
  size_t seek_backward_counter_ = 0;
};

template <class IteratorType>
class VariableFilter {
 public:
  VariableFilter(
      const IteratorFilter* filter, const QueryOptions& filter_options) {
    filter_context_.filter = DCHECK_NOTNULL(filter);
    filter_context_.options = filter_options;
  }

  // Checks whether filter was updated and recheck it on filtered out entries.
  // If entry matches new filter it is readded to provided heap.
  // Returns true if heap top should be updated.
  template <class Heap>
  bool SeekUpdatingHeap(Slice target, Heap& heap) {
    if (filter_context_.version <= checked_user_key_for_filter_version_) {
      return false;
    }
    checked_user_key_for_filter_version_ = filter_context_.version;
    yb::EraseIf([this, target, &heap](auto* child) {
      if (!child->UpdateUserKeyForFilter(filter_context_)) {
        return false;
      }
      if (child->seek_backward_counter() == seek_backward_counter_) {
        if (child->Valid() && child->key().compare(target) < 0) {
          Seek(*child, target);
        }
      } else {
        Seek(*child, target);
      }
      if (child->Valid()) {
        heap.push(child);
      }
      return true;
    }, &iterators_not_matching_filter_);
    return true;
  }

  // Recheck filter for iterators that were not matching it previously.
  // If iterator matches updated filter, then add this iterator to the heap.
  // If it is the first time when iterator being added to the heap, then seek_key is used
  // to position the iterator.
  //
  // Returns true iff heap was updated, false otherwise.
  template <class Heap>
  bool RecheckFilter(Slice seek_key, Heap& heap) {
    if (filter_context_.version <= checked_user_key_for_filter_version_) {
      return false;
    }
    checked_user_key_for_filter_version_ = filter_context_.version;
    bool result = false;
    yb::EraseIf([this, &heap, &result, seek_key](auto* child) {
      if (!child->UpdateUserKeyForFilter(filter_context_)) {
        return false;
      }
      if (!child->Valid()) {
        const auto& entry = child->Seek(seek_key);
        if (!entry.Valid()) {
          return true;
        }
      }
      result = true;
      heap.push(child);
      return true;
    }, &iterators_not_matching_filter_);
    return result;
  }

  void ResetMatchedState() {
    iterators_not_matching_filter_.clear();
    checked_user_key_for_filter_version_ = filter_context_.version;
    ++seek_backward_counter_;
  }

  // Returns true if iterator matches this filter.
  bool CheckIterator(IteratorType& child) {
    if (child.UpdateUserKeyForFilter(filter_context_)) {
      return true;
    }
    iterators_not_matching_filter_.push_back(&child);
    return false;
  }

  void UpdateUserKeyForFilter(Slice user_key) {
    filter_context_.user_key = user_key;
    filter_context_.cache.Reset(user_key);
    ++filter_context_.version;
  }

  const KeyValueEntry& Seek(IteratorType& iter, Slice key) {
    iter.set_seek_backward_counter(seek_backward_counter_);
    return iter.Seek(key);
  }

  const KeyValueEntry& SeekToFirst(IteratorType& iter) {
    iter.set_seek_backward_counter(seek_backward_counter_);
    return iter.SeekToFirst();
  }

  const KeyValueEntry& SeekToLast(IteratorType& iter) {
    LOG(DFATAL) << "Backward scan is not supported in conjunction with filters";
    return iter.SeekToLast();
  }

 private:
  UserKeyFilterContext filter_context_;
  int64_t checked_user_key_for_filter_version_ = 0;
  boost::container::small_vector<IteratorType*, kNumIterReserve> iterators_not_matching_filter_;
  // Merged iterator Seek target key could be after current (forward seek) key and before
  // current (backward seek) key.
  //
  // In forward case we have optimization to avoid fully rebuilding child iterators heap, and also
  // don't call Seek on child iterators which are already positioned after the target key.
  //
  // In backward case we call seek only on child iterators matching the filter and fully rebuild
  // child iterators heap with child iterators which are valid after such seek.
  //
  // When filter is used we could get into situation where backward seek on child iterator was not
  // called, since child iterator was filtered out.
  // So if after that child iterator started to match updated filter during forward seek, there are
  // 2 possible scenarios:
  // 1) Child iterator performed last backward seek. In this case we could check whether child
  //    iterator current key is after target key, and if this is the case we can skip Seek on this
  //    child iterator.
  // 2) Child iterator missing last backward seek. In this case we should trigger seek on this child
  //    iterator w/o respecting its current key.
  // We use VariableFilter::seek_backward_counter_ to track how many backward seeks happened on
  // merged iterator, and store current value of seek_backward_counter_ in each child iterator when
  // Seek is called on this child iterator. So we can compare those counters and understand whether
  // this particular child iterator missing backward seek or not.
  size_t seek_backward_counter_ = 1;
};

class NoFilter {
 public:
  template <class Heap>
  bool SeekUpdatingHeap(Slice target, Heap& heap) {
    return false;
  }

  template <class Heap>
  bool RecheckFilter(Slice seek_key, Heap& heap) {
    return false;
  }

  template <class IteratorType>
  const KeyValueEntry& Seek(IteratorType& iter, Slice key) {
    return iter.Seek(key);
  }

  template <class IteratorType>
  const KeyValueEntry& SeekToFirst(IteratorType& iter) {
    return iter.SeekToFirst();
  }

  template <class IteratorType>
  const KeyValueEntry& SeekToLast(IteratorType& iter) {
    return iter.SeekToLast();
  }

  void ResetMatchedState() {}

  template <class Iterator>
  bool CheckIterator(Iterator&) {
    return true;
  }

  void UpdateUserKeyForFilter(Slice key) {}
};

}  // namespace

template <typename IteratorWrapperType, typename ChildIteratorFilter>
class MergingIteratorBase final
    : public MergingIterator<typename IteratorWrapperType::IteratorType> {
 public:
  using IteratorType = typename IteratorWrapperType::IteratorType;
  using Base = MergingIterator<IteratorType>;
  using Children = boost::iterator_range<IteratorWrapperType*>;

  MergingIteratorBase(
      const Comparator* comparator, Arena* arena, IteratorType** children, size_t n)
      : comparator_(comparator),
        is_arena_mode_(arena != nullptr),
        minHeap_(MinIteratorComparator<IteratorWrapperType>(comparator_)) {
    AllocateChildren(arena, boost::make_iterator_range_n(children, n));
    minHeap_.reserve(children_.size());
    for (auto& child : children_) {
      if (child.Valid()) {
        minHeap_.push(&child);
      }
    }
    CurrentForward();
  }

  template <class Children, class... FilterArgs>
  MergingIteratorBase(
      const Comparator* comparator, Arena* arena, const Children& children,
      FilterArgs&&... filter_args)
      : comparator_(comparator),
        is_arena_mode_(arena != nullptr),
        child_iterator_filter_(std::forward<FilterArgs>(filter_args)...),
        minHeap_(MinIteratorComparator<IteratorWrapperType>(comparator_)) {
    AllocateChildren(arena, children);
  }

  void AddIterator(IteratorType* iter) {
    DCHECK_EQ(direction_, Direction::kForward);
    children_.emplace_back(iter);
    if (data_pinned_) {
      Status s = iter->PinData();
      DCHECK_OK(s);
    }
    auto new_wrapper = children_.back();
    if (new_wrapper.Valid()) {
      minHeap_.push(&new_wrapper);
      CurrentForward();
    }
  }

  virtual ~MergingIteratorBase() {
    for (auto& child : children_) {
      child.DeleteIter(is_arena_mode_);
    }
    if (!is_arena_mode_) {
      delete [] children_.begin();
    }
  }

  const KeyValueEntry& SeekToFirst() override {
    ClearHeaps();
    for (auto& child : children_) {
      if (child_iterator_filter_.SeekToFirst(child).Valid()) {
        minHeap_.push(&child);
      }
    }
    direction_ = Direction::kForward;
    return CurrentForward();
  }

  const KeyValueEntry& SeekToLast() override {
    ClearHeaps();
    InitMaxHeap();
    for (auto& child : children_) {
      if (child_iterator_filter_.SeekToLast(child).Valid()) {
        maxHeap_->push(&child);
      }
    }
    direction_ = Direction::kReverse;
    CurrentReverse();
    return Entry();
  }

  const KeyValueEntry& Seek(Slice target) override {
    if (direction_ == Direction::kForward && current_ && current_->Valid()) {
      int key_vs_target = comparator_->Compare(current_->key(), target);
      if (key_vs_target == 0) {
        if (child_iterator_filter_.SeekUpdatingHeap(target, minHeap_)) {
          DCHECK_EQ(comparator_->Compare(CurrentForward().key, target), 0);
        }
        // We're already at the right key.
        return Entry();
      }
      if (key_vs_target < 0) {
        if (child_iterator_filter_.SeekUpdatingHeap(target, minHeap_)) {
          const auto& entry = CurrentForward();
          if (!entry.Valid() || comparator_->Compare(entry.key, target) == 0) {
            return entry;
          }
        }
        // This is a "seek forward" operation, and the current key is less than the target. Keep
        // doing a seek on the top iterator and re-adding it to the min heap, until the top iterator
        // gives is a key >= target.
        while (key_vs_target < 0) {
          // For the heap modifications below to be correct, current_ must be the current top of the
          // heap.
          DCHECK_EQ(current_, minHeap_.top());
          if (child_iterator_filter_.CheckIterator(*current_)) {
            const auto& entry = UpdateHeapAfterCurrentAdvancement(
                child_iterator_filter_.Seek(*current_, target));
            if (!entry.Valid()) {
              return entry; // Reached the end.
            }
            key_vs_target = comparator_->Compare(entry.key, target);
          } else {
            minHeap_.pop();
            const auto& entry = CurrentForward();
            if (!entry.Valid()) {
              return entry; // Reached the end.
            }
            key_vs_target = comparator_->Compare(entry.key, target);
          }
        }

        // The current key is >= target, this is what we're looking for.
        return Entry();
      }

      // The current key is already greater than the target, so this is not a forward seek.
      // Fall back to a full rebuild of the heap.
    }

    ClearHeaps();
    child_iterator_filter_.ResetMatchedState();
    for (auto& child : children_) {
      if (!child_iterator_filter_.CheckIterator(child)) {
        continue;
      }
      {
        PERF_TIMER_GUARD(seek_child_seek_time);
        child_iterator_filter_.Seek(child, target);
      }
      PERF_COUNTER_ADD(seek_child_seek_count, 1);

      if (child.Valid()) {
        PERF_TIMER_GUARD(seek_min_heap_time);
        minHeap_.push(&child);
      }
    }
    direction_ = Direction::kForward;
    {
      PERF_TIMER_GUARD(seek_min_heap_time);
      CurrentForward();
    }
    return Entry();
  }

  const KeyValueEntry& Next() override {
    assert(Base::Valid());

    // Ensure that all children are positioned after key().
    // If we are moving in the forward direction, it is already
    // true for all of the non-current children since current_ is
    // the smallest child and key() == current_->key().
    if (PREDICT_FALSE(direction_ != Direction::kForward)) {
      RebuildForward();
    }

    // For the heap modifications below to be correct, current_ must be the current top of the heap.
    DCHECK_EQ(current_, minHeap_.top());

    if (child_iterator_filter_.SeekUpdatingHeap(current_->key(), minHeap_)) {
      min_heap_best_root_child_ = 0;
    }
    if (child_iterator_filter_.CheckIterator(*current_)) {
      // As current_ points to the current record, move the iterator forward.
      return UpdateHeapAfterCurrentAdvancement(current_->Next());
    }

    minHeap_.pop();
    return CurrentForward();
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
      child_iterator_filter_.Seek(child, key);
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
    direction_ = Direction::kForward;

    // The loop advanced all non-current children to be > key() so current_
    // should still be strictly the smallest key.
  }

  const KeyValueEntry& Prev() override {
    assert(Base::Valid());
    // Ensure that all children are positioned before key().
    // If we are moving in the reverse direction, it is already
    // true for all of the non-current children since current_ is
    // the largest child and key() == current_->key().
    if (PREDICT_FALSE(direction_ != Direction::kReverse)) {
      // Otherwise, retreat the non-current children.  We retreat current_
      // just after the if-block.
      ClearHeaps();
      InitMaxHeap();
      for (auto& child : children_) {
        if (&child != current_) {
          child_iterator_filter_.Seek(child, Base::key());
          if (child.Valid()) {
            // Child is at first entry >= key().  Step back one to be < key()
            DEBUG_ONLY_TEST_SYNC_POINT_CALLBACK("MergeIterator::Prev:BeforePrev", &child);
            child.Prev();
          } else {
            // Child has no entries >= key().  Position at last entry.
            DEBUG_ONLY_TEST_SYNC_POINT("MergeIterator::Prev:BeforeSeekToLast");
            child_iterator_filter_.SeekToLast(child);
          }
        }
        if (child.Valid()) {
          maxHeap_->push(&child);
        }
      }
      direction_ = Direction::kReverse;
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

  IteratorType* GetCurrentIterator() override {
    return current_->iter();
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
    assert(Base::Valid());
    return current_->IsKeyPinned();
  }

  const KeyValueEntry& UpdateFilterKey(Slice user_key_for_filter, Slice seek_key) override {
    LOG_IF(DFATAL, direction_ != Direction::kForward) << "Update filter with wrong direction";
    child_iterator_filter_.UpdateUserKeyForFilter(user_key_for_filter);
    if (!seek_key.empty() && child_iterator_filter_.RecheckFilter(seek_key, minHeap_)) {
      return CurrentForward();
    }
    return Entry();
  }

 private:
  template <class Children>
  void AllocateChildren(Arena* arena, const Children& children) {
    auto num_children = children.size();
    IteratorWrapperType* start;
    if (arena) {
      start = pointer_cast<IteratorWrapperType*>(
          arena->AllocateAligned(sizeof(IteratorWrapperType) * num_children));
    } else {
      start = new IteratorWrapperType[num_children];
    }
    for (size_t i = 0; i != num_children; ++i) {
      new (start + i) IteratorWrapperType(children[i]);
    }
    children_ = boost::make_iterator_range_n(start, num_children);
  }

  // Clears heaps for both directions, used when changing direction or seeking
  void ClearHeaps() {
    minHeap_.clear();
    if (maxHeap_) {
      maxHeap_->clear();
    }
  }

  // Ensures that maxHeap_ is initialized when starting to go in the reverse
  // direction
  void InitMaxHeap() {
    if (maxHeap_) {
      return;
    }
    maxHeap_ = std::make_unique<MergerMaxIterHeap<IteratorWrapperType>>(
        MaxIteratorComparator<IteratorWrapperType>(comparator_));
  }

  const Comparator* comparator_;
  bool is_arena_mode_;
  Children children_;
  bool data_pinned_ = false;

  // Cached pointer to child iterator with the current key, or nullptr if no
  // child iterators are valid.  This is the top of minHeap_ or maxHeap_
  // depending on the direction.
  IteratorWrapperType* current_ = nullptr;
  size_t min_heap_best_root_child_ = 0;
  Slice min_heap_best_root_child_key_;

  ChildIteratorFilter child_iterator_filter_;

  // Which direction is the iterator moving?
  enum class Direction {
    kForward,
    kReverse
  };

  Direction direction_ = Direction::kForward;
  MergerMinIterHeap<IteratorWrapperType> minHeap_;
  // Max heap is used for reverse iteration, which is way less common than
  // forward.  Lazily initialize it to save memory.
  std::unique_ptr<MergerMaxIterHeap<IteratorWrapperType>> maxHeap_;

  // Updates current_ after updating minHeap_ (used when direction_ is kForward).
  // Also removes top iterators from the heap, that does not match filter.
  //
  // Returns entry pointed by current_.
  const KeyValueEntry& CurrentForward() {
    DCHECK(direction_ == Direction::kForward);
    min_heap_best_root_child_ = 0;
    while (!minHeap_.empty()) {
      current_ = minHeap_.top();
      if (child_iterator_filter_.CheckIterator(*current_)) {
        return current_->Entry();
      }
      minHeap_.pop();
    }
    current_ = nullptr;
    return KeyValueEntry::Invalid();
  }

  void CurrentReverse() {
    DCHECK(direction_ == Direction::kReverse);
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

template <typename IteratorType, typename IteratorWrapperType>
InternalIterator* NewMergingIterator(
    const Comparator* cmp, IteratorType** list, size_t n, Arena* arena) {
  if (n == 0) {
    return NewEmptyInternalIterator(arena);
  } else if (n == 1) {
    return list[0];
  } else {
    if (arena == nullptr) {
      return new MergingIteratorBase<IteratorWrapperType, NoFilter>(cmp, arena, list, n);
    }
    return arena->NewObject<MergingIteratorBase<IteratorWrapperType, NoFilter>>(
        cmp, arena, list, n);
  }
}

InternalIterator* NewMergingIterator(
    const Comparator* cmp, InternalIterator** list, int n, Arena* arena) {
  return NewMergingIterator<InternalIterator, IteratorWrapper>(cmp, list, n, arena);
}

template <typename IteratorWrapperType>
MergeIteratorBuilderBase<IteratorWrapperType>::MergeIteratorBuilderBase(
    const Comparator* comparator, Arena* a)
    : comparator_(comparator), arena_(a) {
}

template <typename IteratorWrapperType>
void MergeIteratorBuilderBase<IteratorWrapperType>::AddIterator(InternalIterator* iter) {
  iterators_.push_back(iter);
}

template <typename IteratorWrapperType>
InternalIterator* MergeIteratorBuilderBase<IteratorWrapperType>::Finish() {
  if (iterators_.size() == 1) {
    return iterators_.front();
  }
  CHECK(!iterators_.empty());

  if (filter_) {
    using FilterIteratorType = IteratorWithSeekBackwardCounter<IteratorWrapperType>;
    using Type = MergingIteratorBase<FilterIteratorType, VariableFilter<FilterIteratorType>>;
    return arena_->NewObject<Type>(
        comparator_, arena_, iterators_, filter_, filter_options_);
  }
  using Type = MergingIteratorBase<IteratorWrapperType, NoFilter>;
  return arena_->NewObject<Type>(comparator_, arena_, iterators_);
}

template <typename IteratorWrapperType>
MergeIteratorInHeapBuilder<IteratorWrapperType>::MergeIteratorInHeapBuilder(
    const Comparator* comparator)
    : comparator_(comparator) {}

template <typename IteratorWrapperType>
MergeIteratorInHeapBuilder<IteratorWrapperType>::~MergeIteratorInHeapBuilder() {}

template <typename IteratorWrapperType>
void MergeIteratorInHeapBuilder<IteratorWrapperType>::AddIterator(IteratorType* iter) {
  iterators_.emplace_back(iter);
}

template <typename IteratorWrapperType>
std::unique_ptr<MergingIterator<typename IteratorWrapperType::IteratorType>>
MergeIteratorInHeapBuilder<IteratorWrapperType>::Finish() {
  return std::make_unique<MergingIteratorBase<IteratorWrapperType, NoFilter>>(
      comparator_, nullptr, std::move(iterators_));
}

template class MergeIteratorBuilderBase<
    IteratorWrapperBase<InternalIterator, /* kSkipLastEntry = */ false>>;
template class MergeIteratorBuilderBase<
    IteratorWrapperBase<InternalIterator, /* kSkipLastEntry = */ true>>;

template class MergeIteratorInHeapBuilder<
    IteratorWrapperBase<InternalIterator, /* kSkipLastEntry = */ false>>;
template class MergeIteratorInHeapBuilder<
    IteratorWrapperBase<InternalIterator, /* kSkipLastEntry = */ true>>;

template class MergeIteratorInHeapBuilder<
    IteratorWrapperBase<DataBlockAwareIndexInternalIterator, /* kSkipLastEntry = */ false>>;
template class MergeIteratorInHeapBuilder<
    IteratorWrapperBase<DataBlockAwareIndexInternalIterator, /* kSkipLastEntry = */ true>>;

}  // namespace rocksdb
