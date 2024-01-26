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

#pragma once

#include <algorithm>
#include <atomic>
#include <thread>

#include <boost/atomic.hpp>
#include <boost/type_traits/make_signed.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/atomicops.h"
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

// Atomic integer class inspired by Impala's AtomicInt and
// std::atomic<> in C++11.
//
// NOTE: All of public operations use an implicit memory order of
// kMemOrderNoBarrier unless otherwise specified.
//
// Unlike std::atomic<>, overflowing an unsigned AtomicInt via Increment or
// IncrementBy is undefined behavior (it is also undefined for signed types,
// as always).
//
// See also: yb/gutil/atomicops.h
template<typename T>
class AtomicInt {
 public:
  // Initialize the underlying value to 'initial_value'. The
  // initialization performs a Store with 'kMemOrderNoBarrier'.
  explicit AtomicInt(T initial_value);

  // Returns the underlying value.
  //
  // Does not support 'kMemOrderBarrier'.
  T Load(MemoryOrder mem_order = kMemOrderNoBarrier) const;

  // Sets the underlying value to 'new_value'.
  //
  // Does not support 'kMemOrderBarrier'.
  void Store(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Iff the underlying value is equal to 'expected_val', sets the
  // underlying value to 'new_value' and returns true; returns false
  // otherwise.
  //
  // Does not support 'kMemOrderBarrier'.
  bool CompareAndSet(T expected_val, T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Iff the underlying value is equal to 'expected_val', sets the
  // underlying value to 'new_value' and returns
  // 'expected_val'. Otherwise, returns the current underlying
  // value.
  //
  // Does not support 'kMemOrderBarrier'.
  T CompareAndSwap(T expected_val, T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Sets the underlying value to 'new_value' iff 'new_value' is
  // greater than the current underlying value.
  //
  // Does not support 'kMemOrderBarrier'.
  void StoreMax(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Sets the underlying value to 'new_value' iff 'new_value' is less
  // than the current underlying value.
  //
  // Does not support 'kMemOrderBarrier'.
  void StoreMin(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Increments the underlying value by 1 and returns the new
  // underlying value.
  //
  // Does not support 'kMemOrderAcquire' or 'kMemOrderRelease'.
  T Increment(MemoryOrder mem_order = kMemOrderNoBarrier);

  // Increments the underlying value by 'delta' and returns the new
  // underlying value.

  // Does not support 'kKemOrderAcquire' or 'kMemOrderRelease'.
  T IncrementBy(T delta, MemoryOrder mem_order = kMemOrderNoBarrier);

  // Sets the underlying value to 'new_value' and returns the previous
  // underlying value.
  //
  // Does not support 'kMemOrderBarrier'.
  T Exchange(T new_value, MemoryOrder mem_order = kMemOrderNoBarrier);

 private:
  // If a method 'caller' doesn't support memory order described as
  // 'requested', exit by doing perform LOG(FATAL) logging the method
  // called, the requested memory order, and the supported memory
  // orders.
  static void FatalMemOrderNotSupported(const char* caller,
                                        const char* requested = "kMemOrderBarrier",
                                        const char* supported =
                                        "kMemNorderNoBarrier, kMemOrderAcquire, kMemOrderRelease");

  // The gutil/atomicops.h functions only operate on signed types.
  // So, even if the user specializes on an unsigned type, we use a
  // signed type internally.
  typedef typename boost::make_signed<T>::type SignedT;
  SignedT value_;

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
    underlying_.Store(static_cast<int32_t>(n), m);
  }
  bool CompareAndSet(bool e, bool n, MemoryOrder m = kMemOrderNoBarrier) {
    return underlying_.CompareAndSet(static_cast<int32_t>(e), static_cast<int32_t>(n), m);
  }
  bool CompareAndSwap(bool e, bool n, MemoryOrder m = kMemOrderNoBarrier) {
    return underlying_.CompareAndSwap(static_cast<int32_t>(e), static_cast<int32_t>(n), m);
  }
  bool Exchange(bool n, MemoryOrder m = kMemOrderNoBarrier) {
    return underlying_.Exchange(static_cast<int32_t>(n), m);
  }
 private:
  AtomicInt<int32_t> underlying_;

  DISALLOW_COPY_AND_ASSIGN(AtomicBool);
};

template<typename T>
inline T AtomicInt<T>::Load(MemoryOrder mem_order) const {
  switch (mem_order) {
    case kMemOrderNoBarrier: {
      return base::subtle::NoBarrier_Load(&value_);
    }
    case kMemOrderBarrier: {
      FatalMemOrderNotSupported("Load");
      break;
    }
    case kMemOrderAcquire: {
      return base::subtle::Acquire_Load(&value_);
    }
    case kMemOrderRelease: {
      return base::subtle::Release_Load(&value_);
    }
  }
  abort(); // Unnecessary, but avoids gcc complaining.
}

template<typename T>
inline void AtomicInt<T>::Store(T new_value, MemoryOrder mem_order) {
  switch (mem_order) {
    case kMemOrderNoBarrier: {
      base::subtle::NoBarrier_Store(&value_, new_value);
      break;
    }
    case kMemOrderBarrier: {
      FatalMemOrderNotSupported("Store");
      break;
    }
    case kMemOrderAcquire: {
      base::subtle::Acquire_Store(&value_, new_value);
      break;
    }
    case kMemOrderRelease: {
      base::subtle::Release_Store(&value_, new_value);
      break;
    }
  }
}

template<typename T>
inline bool AtomicInt<T>::CompareAndSet(T expected_val, T new_val, MemoryOrder mem_order) {
  return CompareAndSwap(expected_val, new_val, mem_order) == expected_val;
}

template<typename T>
inline T AtomicInt<T>::CompareAndSwap(T expected_val, T new_val, MemoryOrder mem_order) {
  switch (mem_order) {
    case kMemOrderNoBarrier: {
      return base::subtle::NoBarrier_CompareAndSwap(
          &value_, expected_val, new_val);
    }
    case kMemOrderBarrier: {
      FatalMemOrderNotSupported("CompareAndSwap/CompareAndSet");
      break;
    }
    case kMemOrderAcquire: {
      return base::subtle::Acquire_CompareAndSwap(
          &value_, expected_val, new_val);
    }
    case kMemOrderRelease: {
      return base::subtle::Release_CompareAndSwap(
          &value_, expected_val, new_val);
    }
  }
  abort();
}


template<typename T>
inline T AtomicInt<T>::Increment(MemoryOrder mem_order) {
  return IncrementBy(1, mem_order);
}

template<typename T>
inline T AtomicInt<T>::IncrementBy(T delta, MemoryOrder mem_order) {
  switch (mem_order) {
    case kMemOrderNoBarrier: {
      return base::subtle::NoBarrier_AtomicIncrement(&value_, delta);
    }
    case kMemOrderBarrier: {
      return base::subtle::Barrier_AtomicIncrement(&value_, delta);
    }
    case kMemOrderAcquire: {
      FatalMemOrderNotSupported("Increment/IncrementBy",
                                "kMemOrderAcquire",
                                "kMemOrderNoBarrier and kMemOrderBarrier");
      break;
    }
    case kMemOrderRelease: {
      FatalMemOrderNotSupported("Increment/Incrementby",
                                "kMemOrderAcquire",
                                "kMemOrderNoBarrier and kMemOrderBarrier");
      break;
    }
  }
  abort();
}

template<typename T>
inline T AtomicInt<T>::Exchange(T new_value, MemoryOrder mem_order) {
  switch (mem_order) {
    case kMemOrderNoBarrier: {
      return base::subtle::NoBarrier_AtomicExchange(&value_, new_value);
    }
    case kMemOrderBarrier: {
      FatalMemOrderNotSupported("Exchange");
      break;
    }
    case kMemOrderAcquire: {
      return base::subtle::Acquire_AtomicExchange(&value_, new_value);
    }
    case kMemOrderRelease: {
      return base::subtle::Release_AtomicExchange(&value_, new_value);
    }
  }
  abort();
}

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
T GetAtomicFlag(T* flag) {
  std::atomic<T>& atomic_flag = *pointer_cast<std::atomic<T>*>(flag);
  return atomic_flag.load(std::memory_order::relaxed);
}

template <class U, class T>
void SetAtomicFlag(U value, T* flag) {
  std::atomic<T>& atomic_flag = *pointer_cast<std::atomic<T>*>(flag);
  atomic_flag.store(value);
}

template <class T>
void AtomicFlagSleepMs(T* flag) {
  auto value = GetAtomicFlag(flag);
  if (PREDICT_FALSE(value != 0)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(value));
  }
}

template <class T>
void AtomicFlagRandomSleepMs(T* flag) {
  auto value = GetAtomicFlag(flag);
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

} // namespace yb
