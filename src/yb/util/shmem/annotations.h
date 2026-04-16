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

#include <atomic>
#include <type_traits>
#include <utility>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/debug.h"
#include "yb/util/logging.h"

namespace yb {

bool IsChildProcess();

void MarkChildProcess();

#define DCHECK_PARENT_PROCESS() DCHECK(!IsChildProcess()) << "Access from child process not allowed"

class [[nodiscard]] SCOPED_CAPABILITY ParentProcessGuard { // NOLINT(whitespace/braces)
 private:
  struct CAPABILITY("parent process") ParentProcessCapability { };

 public:
  static ParentProcessCapability capability;

  ParentProcessGuard() ACQUIRE_SHARED(capability) {
    DCHECK_PARENT_PROCESS();
  }

  ~ParentProcessGuard() RELEASE() {}
};

#define PARENT_PROCESS_ONLY REQUIRES_SHARED(ParentProcessGuard::capability)

template<typename T, bool AllowChildRead>
class SharedMemoryAnnotatedObject {
 public:
  SharedMemoryAnnotatedObject() = default;

  // Not explicit in order to allow various constructs that work with unannotated fields, like
  // int_field_ = std::exchange(other.int_field_, 0).
  template<typename... Args>
  SharedMemoryAnnotatedObject(Args... args) // NOLINT(runtime/explicit)
      : object_(std::forward<Args>(args)...) {}

  inline T& Get() ATTRIBUTE_ALWAYS_INLINE PARENT_PROCESS_ONLY {
    return object_;
  }

  inline const T& Get()
      const requires(!AllowChildRead) ATTRIBUTE_ALWAYS_INLINE PARENT_PROCESS_ONLY {
    return object_;
  }

  inline const T& Get() const requires(AllowChildRead) ATTRIBUTE_ALWAYS_INLINE {
    return object_;
  }

  inline void SharedMemoryStore(const T& value) ATTRIBUTE_ALWAYS_INLINE PARENT_PROCESS_ONLY {
    // This is only used on parent process, so no special handling needed. This is provided
    // to provide a uniform interface with ChildProcessRW.
    object_ = value;
  }

 private:
  T object_;
};

template<typename T, bool Read>
std::ostream& operator<<(std::ostream& os, const SharedMemoryAnnotatedObject<T, Read>& obj) {
  return os << *obj;
}

template<typename T>
using ChildProcessForbidden =
    SharedMemoryAnnotatedObject<T, false /* AllowChildRead */>;

template<typename T>
using ChildProcessRO =
    SharedMemoryAnnotatedObject<T, true /* AllowChildRead */>;

inline ATTRIBUTE_ALWAYS_INLINE void CompilerBarrier() {
  std::atomic_signal_fence(std::memory_order_acq_rel);
}

// This is explicitly not for concurrent use. std::atomic is only used to avoid torn writes when
// a child process crashes.
template<typename T>
class ChildProcessRW {
 public:
  ChildProcessRW() = default;

  ChildProcessRW(const ChildProcessRW& other) : object_(other.Get()) {}

  // Not explicit in order to allow various constructs that work with unannotated fields, like
  // int_field_ = std::exchange(other.int_field_, 0).
  template<typename... Args>
  ChildProcessRW(Args... args) // NOLINT(runtime/explicit)
      : object_(std::forward<Args>(args)...) {}

  inline ChildProcessRW& operator=(const ChildProcessRW& other) ATTRIBUTE_ALWAYS_INLINE {
    SharedMemoryStore(other.Get());
    return *this;
  }

  inline T Get() const ATTRIBUTE_ALWAYS_INLINE {
    return object_.load(std::memory_order_relaxed);
  }

  inline void SharedMemoryStore(T value) ATTRIBUTE_ALWAYS_INLINE {
    // This is for nonconcurrent use, where we normally do not care about reordering. But we do not
    // want writes split over multiple instructions (what if we crash halfway) or compiler
    // reordering (makes reasoning about post-crash state difficult). CPU reordering is perfectly
    // fine since this is nonconcurrent, so we use barrier + volatile to enforce ordering instead
    // of seq_cst atomics.
    CompilerBarrier();
    static_cast<volatile std::atomic<T>*>(&object_)->store(value, std::memory_order_relaxed);
    CompilerBarrier();
  }

 private:
  static_assert(std::atomic<T>::is_always_lock_free);
  std::atomic<T> object_;
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const ChildProcessRW<T>& obj) {
  return os << *obj;
}

// Stores to shared memory from code callable by PG processes should use these helpers. All stores
// to shared memory should be either annotated with this, or be loaded in a function annotated with
// PARENT_PROCESS_ONLY.
//
// These helpers add compiler barriers to avoid compiler reordering, which is dangerous if the
// process may crash at any point, e.g. for a vector's emplace_back:
//      std::allocator_traits<Allocator>::construct(
//          allocator_, data_ + num_elements_, std::forward<Args>(args)...);
//      ++num_elements_;
//
// may be reordered to:
//      size_t num_elments = num_elements_++;
//      std::allocator_traits<Allocator>::construct(
//          allocator_, data_ + num_elements, std::forward<Args>(args)...);
//
// and crash at:
//      size_t num_elments = num_elements_++;
//      -- CRASH HERE --
//
// resulting in a new element that was never constructed.
//
// This should instead be written as:
//      std::allocator_traits<Allocator>::construct(
//          allocator_, data_ + num_elements_, std::forward<Args>(args)...);
//      SHARED_MEMORY_STORE(num_elements_, num_elements_ + 1);
//
// to ensure that the store to num_elements_ is not reordered.
#define SHARED_MEMORY_STORE(destination, value) (destination).SharedMemoryStore(value)

// Loads don't require any special handling, but this macro is provided to hide
// the use of Get() everywhere for the ChildProcess*<> wrappers, and to force use of
// const overloads (since non-const access of ChildProcess*<> wrappers is treated as a write).
#define SHARED_MEMORY_LOAD(value) std::as_const(value).Get()

} // namespace yb
