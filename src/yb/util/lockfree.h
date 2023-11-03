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

#include <atomic>
#include <iterator>
#include <memory>

#include <boost/atomic.hpp>

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/macros.h"
#include "yb/util/atomic.h" // For IsAcceptableAtomicImpl

namespace yb {

template <class T>
class MPSCQueue;

template <class T>
class MPSCQueueIterator : public std::iterator<std::input_iterator_tag, T> {
 public:
  explicit MPSCQueueIterator(const T* pop_head): next_(pop_head) {}

  bool Equals(const MPSCQueueIterator& other) const {
    return this->next_ == other.next_;
  }

  MPSCQueueIterator& operator++() {
    LOG(INFO) << "incr from " << next_;
    if (!is_end()) {
      next_ = GetNext(next_);
    }
    return *this;
  }

  MPSCQueueIterator operator++(int) {
    MPSCQueueIterator copy = *this;
    ++*this;
    return copy;
  }

  const T& operator*() const { return *next_; }

  const T* operator->() const { return next_; }
 private:
  bool is_end() const {
    return !next_;
  }

  friend bool operator==(const MPSCQueueIterator& lhs, const MPSCQueueIterator& rhs) {
    return lhs.Equals(rhs);
  }

  friend bool operator!=(const MPSCQueueIterator& lhs, const MPSCQueueIterator& rhs) {
    return !lhs.Equals(rhs);
  }

  const T* next_;
};

// Multi producer - singe consumer queue.
template <class T>
class MPSCQueue {
 public:
  typedef MPSCQueueIterator<T> const_iterator;

  const_iterator begin() const {
    return MPSCQueueIterator<T>(push_head_);
  }

  const_iterator end() const {
    return MPSCQueueIterator<T>(nullptr);
  }

  // Thread safe - could be invoked from multiple threads.
  void Push(T* value) {
    T* old_head = push_head_.load(std::memory_order_acquire);
    for (;;) {
      SetNext(value, old_head);
      if (push_head_.compare_exchange_weak(old_head, value, std::memory_order_acq_rel)) {
        break;
      }
    }
  }

  // Could be invoked only by one thread at time.
  T* Pop() {
    if (!pop_head_) {
      PreparePop();
    }
    auto result = pop_head_;
    if (!result) {
      return nullptr;
    }
    pop_head_ = GetNext(result);
    return result;
  }

  void Drain() {
    while (auto* entry = Pop()) {
      delete entry;
    }
  }

 private:
  void PreparePop() {
    T* current = push_head_.exchange(nullptr, std::memory_order_acq_rel);
    // Reverse original list.
    T* prev = nullptr;
    while (current) {
      auto next = GetNext(current);
      SetNext(current, prev);
      prev = current;
      current = next;
    }
    pop_head_ = prev;
  }

  // List of entries ready for pop, pop head points to the entry that should be returned first.
  T* pop_head_ = nullptr;
  // List of push entries, push head points to last pushed entry.
  std::atomic<T*> push_head_{nullptr};

  friend MPSCQueueIterator<T>;
};

template <class T>
class MPSCQueueEntry {
 public:
  void SetNext(T* next) {
    next_ = next;
  }

  T* GetNext() const {
    return next_;
  }

 private:
  T* next_ = nullptr;
};

template <class T>
void SetNext(MPSCQueueEntry<T>* entry, T* next) {
  entry->SetNext(next);
}

template <class T>
T* GetNext(const MPSCQueueEntry<T>* entry) {
  return entry->GetNext();
}

// Intrusive stack implementation based on linked list.
template <class T>
class LockFreeStack {
 public:
  LockFreeStack() {
    CHECK(IsAcceptableAtomicImpl(head_));
  }

  void Push(T* value) {
    Head old_head = head_.load(boost::memory_order_acquire);
    for (;;) {
      ANNOTATE_IGNORE_WRITES_BEGIN();
      SetNext(value, old_head.pointer);
      ANNOTATE_IGNORE_WRITES_END();
      Head new_head{value, old_head.version + 1};
      if (head_.compare_exchange_weak(old_head, new_head, boost::memory_order_acq_rel)) {
        break;
      }
    }
  }

  T* Pop() {
    Head old_head = head_.load(boost::memory_order_acquire);
    for (;;) {
      if (!old_head.pointer) {
        break;
      }
      ANNOTATE_IGNORE_READS_BEGIN();
      Head new_head{GetNext(old_head.pointer), old_head.version + 1};
      ANNOTATE_IGNORE_READS_END();
      if (head_.compare_exchange_weak(old_head, new_head, boost::memory_order_acq_rel)) {
        break;
      }
    }
    return old_head.pointer;
  }

 private:
  // The clang compiler may generate code that requires 16-byte alignment
  // that causes SEGV if this struct is not aligned properly.
  struct Head {
    T* pointer;
    size_t version;
  } __attribute__((aligned(16)));

  boost::atomic<Head> head_{Head{nullptr, 0}};
};

// A weak pointer that can only be written to once, but can be read and written in a lock-free way.
template<class T>
class WriteOnceWeakPtr {
 public:
  WriteOnceWeakPtr() {}

  explicit WriteOnceWeakPtr(const std::shared_ptr<T>& p)
      : state_(p ? State::kSet : State::kUnset),
        weak_ptr_(p) {
  }

  // Set the pointer to the given value. Return true if successful. Setting the value to a null
  // pointer is never considered successful.
  MUST_USE_RESULT bool Set(const std::shared_ptr<T>& p) {
    if (!p)
      return false;
    auto expected_state = State::kUnset;
    if (!state_.compare_exchange_strong(
        expected_state, State::kSetting, std::memory_order_acq_rel)) {
      return false;
    }
    // Only one thread will ever get here.
    weak_ptr_ = p;
    // Use sequential consistency here to prevent unexpected reorderings of future operations before
    // this one.
    state_ = State::kSet;
    return true;
  }

  std::shared_ptr<T> lock() const {
    return IsInitialized() ? weak_ptr_.lock() : nullptr;
  }

  // This always returns a const T*, because the object is not guaranteed to exist, and the return
  // value of this function should only be used for logging/debugging.
  const T* raw_ptr_for_logging() const {
    // This uses the fact that the weak pointer stores the raw pointer as its first word, and avoids
    // storing the raw pointer separately. This is true for libc++ and libstdc++.
    return IsInitialized() ? *reinterpret_cast<const T* const*>(&weak_ptr_) : nullptr;
  }

  bool IsInitialized() const {
    return state_.load(std::memory_order_acquire) == State::kSet;
  }

 private:
  enum class State : uint8_t {
    kUnset,
    kSetting,
    kSet
  };

  std::atomic<State> state_{State::kUnset};
  std::weak_ptr<T> weak_ptr_;

  static_assert(sizeof(weak_ptr_) == 2 * sizeof(void*));

  DISALLOW_COPY_AND_ASSIGN(WriteOnceWeakPtr);
};

} // namespace yb
