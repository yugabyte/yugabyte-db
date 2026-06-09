// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#pragma once

#include <algorithm>
#include <atomic>
#include <thread>

#include <boost/atomic.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"

#include "yb/util/cast.h"
#include "yb/util/random_util.h"

namespace yb {

// See top-level comments in yb/gutil/atomicops.h for further
// explanations of these levels.
enum MemoryOrder {
  // Relaxed memory ordering, doesn't use any barriers.
  kMemOrderNoBarrier = 0,

  // Ensures that no later memory access by the same thread can be
  // reordered ahead of the operation.
  kMemOrderAcquire = 1,

  // Ensures that no previous memory access by the same thread can be
  // reordered after the operation.
  kMemOrderRelease = 2,

  // Ensures that neither previous NOR later memory access by the same
  // thread can be reordered after the operation.
  kMemOrderBarrier = 3,
};

enum class AtomicOpKind { kLoad, kStore, kOther };
constexpr std::memory_order ToStdMemoryOrder(MemoryOrder mem_order, AtomicOpKind op) {
  switch (mem_order) {
    case kMemOrderNoBarrier:
      return std::memory_order_relaxed;
    case kMemOrderAcquire:
      return (op == AtomicOpKind::kStore) ? std::memory_order_seq_cst : std::memory_order_acquire;
    case kMemOrderRelease:
      return (op == AtomicOpKind::kLoad) ? std::memory_order_acquire : std::memory_order_release;
    case kMemOrderBarrier:
      if (op == AtomicOpKind::kLoad) {
        return std::memory_order_acquire;
      }
      if (op == AtomicOpKind::kStore) {
        return std::memory_order_release;
      }
      return std::memory_order_acq_rel;
  }
  return std::memory_order_relaxed;
}

// Atomic integer class wrapping std::atomic<T>.
template<typename T>
class AtomicInt {
 public:
  // Initialize the underlying value to 'initial_value'.
  explicit AtomicInt(T initial_value) : value_(initial_value) {}

  // Returns the underlying value.
  T Load(MemoryOrder mem_order = kMemOrderNoBarrier) const {
    return value_.load(ToStdMemoryOrder(mem_order, AtomicOpKind::kLoad));
  }

  // Sets the underlying value to 'new_value'.
  void Store(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier) {
    value_.store(new_value, ToStdMemoryOrder(mem_order, AtomicOpKind::kStore));
  }

  // Iff the underlying value is equal to 'expected_val', sets the
  // underlying value to 'new_value' and returns true; returns false
  // otherwise.
  bool CompareAndSet(T expected_val, T new_value, MemoryOrder mem_order = kMemOrderNoBarrier) {
    return value_.compare_exchange_strong(
        expected_val, new_value, ToStdMemoryOrder(mem_order, AtomicOpKind::kOther));
  }

  // Iff the underlying value is equal to 'expected_val', sets the
  // underlying value to 'new_value' and returns 'expected_val'.
  // Otherwise, returns the current underlying value.
  T CompareAndSwap(T expected_val, T new_value, MemoryOrder mem_order = kMemOrderNoBarrier) {
    value_.compare_exchange_strong(
        expected_val, new_value, ToStdMemoryOrder(mem_order, AtomicOpKind::kOther));
    return expected_val;
  }

  // Sets the underlying value to 'new_value' iff 'new_value' is
  // greater than the current underlying value.
  void StoreMax(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Sets the underlying value to 'new_value' iff 'new_value' is less
  // than the current underlying value.
  void StoreMin(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Increments the underlying value by 1 and returns the new
  // underlying value.
  T Increment(MemoryOrder mem_order = kMemOrderNoBarrier) {
    return IncrementBy(1, mem_order);
  }

  // Increments the underlying value by 'delta' and returns the new
  // underlying value.
  T IncrementBy(T delta, MemoryOrder mem_order = kMemOrderNoBarrier) {
    return value_.fetch_add(
               delta, ToStdMemoryOrder(mem_order, AtomicOpKind::kOther)) +
               delta;
  }

  // Sets the underlying value to 'new_value' and returns the previous
  // underlying value.
  T Exchange(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier) {
    return value_.exchange(new_value, ToStdMemoryOrder(mem_order, AtomicOpKind::kOther));
  }

 private:
  std::atomic<T> value_;

  DISALLOW_COPY_AND_ASSIGN(AtomicInt);
};

// Adapts AtomicInt to handle boolean values.
//
// NOTE: All of public operations use an implicit memory order of
// kMemOrderNoBarrier unless otherwise specified.
//
// See AtomicInt above for documentation on individual methods.
class AtomicBool {
 public:
  explicit AtomicBool(bool value);

  bool Load(MemoryOrder m = kMemOrderNoBarrier) const {
    return underlying_.Load(m);
  }
  void Store(bool n, MemoryOrder m = kMemOrderNoBarrier) {
    underlying_.Store(n, m);
  }
  bool CompareAndSet(bool e, bool n, MemoryOrder m = kMemOrderNoBarrier) {
    return underlying_.CompareAndSet(e, n, m);
  }
  bool CompareAndSwap(bool e, bool n, MemoryOrder m = kMemOrderNoBarrier) {
    return underlying_.CompareAndSwap(e, n, m);
  }
  bool Exchange(bool n, MemoryOrder m = kMemOrderNoBarrier) {
    return underlying_.Exchange(n, m);
  }
 private:
  AtomicInt<bool> underlying_;

  DISALLOW_COPY_AND_ASSIGN(AtomicBool);
};

template<typename T>
inline void AtomicInt<T>::StoreMax(T new_value, MemoryOrder mem_order) {
  T old_value = Load(mem_order);
  while (true) {
    T max_value = std::max(old_value, new_value);
    T prev_value = CompareAndSwap(old_value, max_value, mem_order);
    if (PREDICT_TRUE(old_value == prev_value)) {
      break;
    }
    old_value = prev_value;
  }
}

template<typename T>
inline void AtomicInt<T>::StoreMin(T new_value, MemoryOrder mem_order) {
  T old_value = Load(mem_order);
  while (true) {
    T min_value = std::min(old_value, new_value);
    T prev_value = CompareAndSwap(old_value, min_value, mem_order);
    if (PREDICT_TRUE(old_value == prev_value)) {
      break;
    }
    old_value = prev_value;
  }
}

template<typename T>
class AtomicUniquePtr {
 public:
  AtomicUniquePtr() {}
  AtomicUniquePtr(const AtomicUniquePtr<T>&) = delete;
  void operator=(const AtomicUniquePtr&) = delete;

  explicit AtomicUniquePtr(T* ptr) : ptr_(ptr) {}

  AtomicUniquePtr(AtomicUniquePtr<T>&& other) : ptr_(other.release()) {}

  void operator=(AtomicUniquePtr<T>&& other) {
    reset(other.release());
  }

  ~AtomicUniquePtr() {
    delete get();
  }

  T* get(std::memory_order memory_order = std::memory_order_acquire) const {
    return ptr_.load(memory_order);
  }

  void reset(T* ptr = nullptr, std::memory_order memory_order = std::memory_order_acq_rel) {
    delete ptr_.exchange(ptr, memory_order);
  }

  T* release(std::memory_order memory_order = std::memory_order_acq_rel) {
    return ptr_.exchange(nullptr, memory_order);
  }

 private:
  std::atomic<T*> ptr_ = { nullptr };
};

template<class T, class... Args>
AtomicUniquePtr<T> MakeAtomicUniquePtr(Args&&... args) {
  return AtomicUniquePtr<T>(new T(std::forward<Args>(args)...));
}

template <class T>
void AtomicFlagSleepMs(T* flag) {
  auto value = *flag;
  if (PREDICT_FALSE(value != 0)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(value));
  }
}

template <class T>
void AtomicFlagRandomSleepMs(T* flag) {
  auto value = *flag;
  if (value != 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(RandomUniformInt<T>(0, value)));
  }
}

template <class U, class T>
bool CompareAndSetFlag(T* flag, U exp, U desired) {
  std::atomic<T>& atomic_flag = *pointer_cast<std::atomic<T>*>(flag);
  return atomic_flag.compare_exchange_strong(exp, desired);
}

template<typename T>
void UpdateAtomicMax(std::atomic<T>* max_holder, T new_value) {
  auto current_max = max_holder->load(std::memory_order_acquire);
  while (new_value > current_max && !max_holder->compare_exchange_weak(current_max, new_value)) {}
}

template<typename T>
void UpdateAtomicMin(std::atomic<T>* min_holder, T value) {
  auto current_min = min_holder->load(std::memory_order_acquire);
  while (value < current_min && !min_holder->compare_exchange_weak(current_min, value)) {}
}

class AtomicTryMutex {
 public:
  void unlock() {
    auto value = locked_.exchange(false, std::memory_order_acq_rel);
    DCHECK(value);
  }

  bool try_lock() {
    bool expected = false;
    return locked_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
  }

  bool is_locked() const {
    return locked_.load(std::memory_order_acquire);
  }

 private:
  std::atomic<bool> locked_{false};
};

template <class T, class D>
T AddFetch(std::atomic<T>* atomic, const D& delta, std::memory_order memory_order) {
  return atomic->fetch_add(delta, memory_order) + delta;
}

template <class T>
bool MakeAtLeast(std::atomic<T>& atomic, T new_value) {
  auto old_value = atomic.load();
  while (old_value < new_value) {
    if (atomic.compare_exchange_strong(old_value, new_value)) {
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------------------------------------
// A utility for testing if an atomic is lock-free.

namespace atomic_internal {

template <class T>
bool IsAcceptableAtomicImpl(const T& atomic_variable) {
#ifdef __aarch64__
  // TODO: ensure we are using proper 16-byte atomics on aarch64.
  // https://github.com/yugabyte/yugabyte-db/issues/9196
  return true;
#else
  return atomic_variable.is_lock_free();
#endif
}

}  // namespace atomic_internal

template <class T>
bool IsAcceptableAtomicImpl(const boost::atomics::atomic<T>& atomic_variable) {
  return atomic_internal::IsAcceptableAtomicImpl(atomic_variable);
}

template <class T>
bool IsAcceptableAtomicImpl(const std::atomic<T>& atomic_variable) {
  return atomic_internal::IsAcceptableAtomicImpl(atomic_variable);
}

template <class T>
T LoadValue(const T* value) {
  return *value;
}

template <class T>
T LoadValue(const std::atomic<T>* value) {
  return value->load(std::memory_order_acquire);
}

} // namespace yb
