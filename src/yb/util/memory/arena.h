// Copyright 2010 Google Inc.  All Rights Reserved
//
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
//
// Memory arena for variable-length datatypes and STL collections.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <boost/signals2/dummy_mutex.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/logging-inl.h"
#include "yb/gutil/macros.h"

#include "yb/util/debug/lock_debug.h"
#include "yb/util/enums.h"
#include "yb/util/memory/arena_fwd.h"
#include "yb/util/memory/memory.h"
#include "yb/util/slice.h"

namespace yb {

YB_DEFINE_ENUM(ResetMode, (kKeepFirst)(kKeepLast));

namespace internal {

struct ThreadSafeArenaTraits {
  typedef std::atomic<uint8_t*> pointer;
  typedef std::mutex mutex_type;

  template<class T>
  struct MakeAtomic {
    using type = std::atomic<T>;
  };

  template<class T>
  static void StoreRelease(const T& t, std::atomic<T>* out) {
    out->store(t, std::memory_order_release);
  }

  template<class T>
  static bool CompareExchange(const T& new_value, std::atomic<T>* out, T* old_value) {
    return out->compare_exchange_strong(*old_value, new_value);
  }
};

struct ArenaTraits {
  using pointer = uint8_t*;
  using mutex_type = SingleThreadedMutex;

  template<class T>
  struct MakeAtomic {
    using type = SingleThreadedAtomic<T>;
  };

  template<class T>
  static void StoreRelease(const T& t, T* out) {
    *out = t;
  }

  template<class T>
  static bool CompareExchange(const T& new_value, T* out, T* old_value) {
    if (*out == *old_value) {
      *out = new_value;
      return true;
    } else {
      *old_value = *out;
      return false;
    }
  }
};

template <class Traits>
class ArenaComponent;

// A helper class for storing variable-length blobs (e.g. strings). Once a blob
// is added to the arena, its index stays fixed. No reallocation happens.
// Instead, the arena keeps a list of buffers. When it needs to grow, it
// allocates a new buffer. Each subsequent buffer is 2x larger, than its
// predecessor, until the maximum specified buffer size is reached.
// The buffers are furnished by a designated allocator.
//
// This class is thread-safe with the fast path lock-free.
template <class Traits>
class ArenaBase {
 public:
  // Constant variable.
  static constexpr size_t kStartBlockSize = 4 * 1024;
  static constexpr size_t kMaxBlockSize = 256 * 1024;

  // Creates a new arena, with a single buffer of size up-to
  // initial_buffer_size, upper size limit for later-allocated buffers capped
  // at max_buffer_size, and maximum capacity (i.e. total sizes of all buffers)
  // possibly limited by the buffer allocator. The allocator might cap the
  // initial allocation request arbitrarily (down to zero). As a consequence,
  // arena construction never fails due to OOM.
  //
  // Calls to AllocateBytes() will then give out bytes from the working buffer
  // until it is exhausted. Then, a subsequent working buffer will be allocated.
  // The size of the next buffer is normally 2x the size of the previous buffer.
  // It might be capped by the allocator, or by the max_buffer_size parameter.
  ArenaBase(BufferAllocator* const buffer_allocator,
            size_t initial_buffer_size = kStartBlockSize,
            size_t max_buffer_size = kMaxBlockSize);

  // Creates an arena using a default (heap) allocator with unbounded capacity.
  // Discretion advised.
  ArenaBase(size_t initial_buffer_size = kStartBlockSize,
            size_t max_buffer_size = kMaxBlockSize);

  ArenaBase(const ArenaBase&) = delete;
  void operator=(const ArenaBase&) = delete;

  virtual ~ArenaBase();

  // Adds content of the specified Slice to the arena, and returns a
  // pointer to it. The pointer is guaranteed to remain valid during the
  // lifetime of the arena. The Slice object itself is not copied. The
  // size information is not stored.
  // (Normal use case is that the caller already has an array of Slices,
  // where it keeps these pointers together with size information).
  // If this request would make the arena grow and the allocator denies that,
  // returns NULL and leaves the arena unchanged.
  uint8_t *AddSlice(const Slice& value);

  // Same as above.
  void* AddBytes(const void *data, size_t len);

  Slice DupSlice(const Slice& value) {
    return Slice(AddSlice(value), value.size());
  }

  // Handy wrapper for placement-new
  template<class T, class... Args>
  T *NewObject(Args&&... args);

  template<class T, class... Args>
  T *NewArenaObject(Args&&... args) {
    return NewObject<T>(this, std::forward<Args>(args)...);
  }

  // Allocate shared_ptr object.
  template<class TObject, typename... TypeArgs>
  std::shared_ptr<TObject> AllocateShared(TypeArgs&&... args);

  // Convert raw pointer to shared pointer.
  template<class TObject>
  std::shared_ptr<TObject> ToShared(TObject *raw_ptr);

  // Relocate the given Slice into the arena, setting 'dst' and
  // returning true if successful.
  // It is legal for 'dst' to be a pointer to 'src'.
  // See AddSlice above for detail on memory lifetime.
  bool RelocateSlice(const Slice &src, Slice *dst);

  // Reserves a blob of the specified size in the arena, and returns a pointer
  // to it. The caller can then fill the allocated memory. The pointer is
  // guaranteed to remain valid during the lifetime of the arena.
  // If this request would make the arena grow and the allocator denies that,
  // returns NULL and leaves the arena unchanged.
  void* AllocateBytes(const size_t size) {
    return AllocateBytesAligned(size, 1);
  }

  // Allocate bytes, ensuring a specified alignment.
  // NOTE: alignment MUST be a power of two, or else this will break.
  void* AllocateBytesAligned(size_t size, size_t alignment);

  template <class T>
  T* AllocateArray(size_t size) {
    return static_cast<T*>(AllocateBytesAligned(size * sizeof(T), alignof(T)));
  }

  template <class TObject, typename... Args>
  TObject* CreateArray(size_t size, Args&&... args) {
    auto result = AllocateArray<TObject>(size);
    for (size_t idx = 0; idx != size; ++idx) {
      new (result + idx) TObject(std::forward<Args>(args)...);
    }
    return result;
  }

  // Removes all data from the arena. (Invalidates all pointers returned by
  // AddSlice and AllocateBytes). Does not cause memory allocation.
  // May reduce memory footprint, as it discards all allocated buffers but
  // the one specified by mode.
  // Unless allocations exceed max_buffer_size, repetitive filling up and
  // resetting normally lead to quickly settling memory footprint and ceasing
  // buffer allocations, as the arena keeps reusing a single, large buffer.
  void Reset(ResetMode mode);

  // Returns the memory footprint of this arena, in bytes, defined as a sum of
  // all buffer sizes. Always greater or equal to the total number of
  // bytes allocated out of the arena.
  size_t memory_footprint() const;

  // Returns how many bytes are used by this arena. This excludes any empty space in the last
  // component.
  size_t UsedBytes();

 private:
  typedef typename Traits::mutex_type mutex_type;
  // Encapsulates a single buffer in the arena.
  typedef ArenaComponent<Traits> Component;

  // Fallback for AllocateBytes non-fast-path
  void* AllocateBytesFallback(const size_t size, const size_t align);

  // Tries to allocate of maximal size between minimum_size and requested_size
  Buffer NewBuffer(size_t requested_size, size_t minimum_size);

  // Tries to allocate buffer of size between mid_size and requested_size.
  // If it fails then tries the same between min_size and requested_size.
  Buffer NewBufferInTwoAttempts(size_t requested_size, size_t mid_size, size_t min_size);

  // Add component to component list, makes in current.
  // If component is nullptr then it is created, otherwise it should be component
  // created at this buffer.
  void AddComponentUnlocked(Buffer buffer, Component* component = nullptr);

  // Load the current component, with "Acquire" semantics (see atomicops.h)
  // if the arena is meant to be thread-safe.
  inline Component* AcquireLoadCurrent() {
    return current_.load(std::memory_order_acquire);
  }

  // Store the current component, with "Release" semantics (see atomicops.h)
  // if the arena is meant to be thread-safe.
  inline void ReleaseStoreCurrent(Component* c) {
    return current_.store(c, std::memory_order_release);
  }

  BufferAllocator* const buffer_allocator_;

  // The current component to allocate from.
  // Use AcquireLoadCurrent and ReleaseStoreCurrent to load/store.
  typename Traits::template MakeAtomic<Component*>::type current_{nullptr};
  const size_t max_buffer_size_;
  size_t arena_footprint_ = 0;
  Component* second_ = nullptr;

  // True if this Arena has already emitted a warning about surpassing
  // the global warning size threshold.
  bool warned_ = false;

  // Lock covering 'slow path' allocation, when new components are
  // allocated and added to the arena's list. Also covers any other
  // mutation of the component data structure (eg Reset).
  mutable mutex_type component_lock_;
};

// STL-compliant allocator, for use with hash_maps and other structures
// which share lifetime with an Arena. Enables memory control and improves
// performance.
template<class T, class Traits>
class ArenaAllocatorBase {
 public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;

  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  pointer index(reference r) const  { return &r; }
  const_pointer index(const_reference r) const  { return &r; }
  size_type max_size() const  { return size_t(-1) / sizeof(T); }

  ArenaAllocatorBase() : arena_(nullptr) {
    // Datatype std::basic_string uses allocator default constructor to create a dummmy allocator
    // instance which is NOT used for any allocation. The legacy code in basic_string uses this
    // dummy value to optimize the case where the string is empty and therefore doesn't need any
    // allocation. Search "_GLIBCXX_FULLY_DYNAMIC_STRING" for more info.
    //
    // We allow default allocator for char-based type (std::basic_string<char>) but not others.
    static_assert(std::is_same<T, char>::value,
                  "Default constructor of ArenaAllocator is available only to char type");
  }

  ArenaAllocatorBase(ArenaBase<Traits>* arena) : arena_(arena) { // NOLINT
    CHECK_NOTNULL(arena_);
  }

  pointer allocate(size_type n) {
    return reinterpret_cast<T*>(arena_->AllocateBytesAligned(n * sizeof(T), alignof(T)));
  }

  void deallocate(pointer p, size_type n) {}

  template<class... Args>
  void construct(pointer p, Args&&... args) {
    new (reinterpret_cast<void*>(p)) T(std::forward<Args>(args)...);
  }

  void destroy(pointer p) { p->~T(); }

  template<class U> struct rebind {
    typedef ArenaAllocatorBase<U, Traits> other;
  };

  template<class U, class TR> ArenaAllocatorBase(const ArenaAllocatorBase<U, TR>& other)
      : arena_(other.arena()) { }

  ArenaBase<Traits> *arena() const {
    return arena_;
  }

 private:
  ArenaBase<Traits>* arena_;
};

template<class T, class Traits>
inline bool operator==(const ArenaAllocatorBase<T, Traits>& lhs,
                       const ArenaAllocatorBase<T, Traits>& rhs) {
  return lhs.arena() == rhs.arena();
}

template<class T, class Traits>
inline bool operator!=(const ArenaAllocatorBase<T, Traits>& lhs,
                       const ArenaAllocatorBase<T, Traits>& rhs) {
  return !(lhs == rhs);
}

template<class Traits>
class ArenaComponent {
 public:
  explicit ArenaComponent(size_t size, ArenaComponent* next)
      : end_(begin_of_this() + size),
        position_(begin()),
        next_(next) {
    CHECK_LE(position_, end_);
  }

  ArenaComponent(const ArenaComponent& rhs) = delete;
  void operator=(const ArenaComponent& rhs) = delete;

  // Tries to reserve space in this component. Returns the pointer to the
  // reserved space if successful; NULL on failure (if there's no more room).
  uint8_t* AllocateBytes(const size_t size) {
    return AllocateBytesAligned(size, 1);
  }

  uint8_t *AllocateBytesAligned(const size_t size, const size_t alignment);

  uint8_t* begin() {
    return const_cast<uint8_t*>(begin_of_this() + sizeof(*this));
  }

  uint8_t* end() { return end_; }
  size_t size() { return end() - begin(); }
  size_t full_size() { return end() - begin_of_this(); }

  size_t free_bytes() { return end() - position_; }

  // Resets used memory of this component, destroys the rest of the component chain.
  // `allocator` should be the same as the one used to allocate memory for this component chain.
  void Reset(BufferAllocator* allocator) {
    ASAN_POISON_MEMORY_REGION(begin(), size());
    position_ = begin();
    if (next_ != nullptr) {
      next_->Destroy(allocator);
      next_ = nullptr;
    }
  }

  // Destroys component chain.
  // `allocator` should be the same as the one used to allocate memory for this component chain.
  void Destroy(BufferAllocator* allocator) {
    ArenaComponent* current = this;
    while (current != nullptr) {
      auto next = current->next_;
      size_t size = current->full_size();
      current->AsanUnpoison(current, size);
      current->~ArenaComponent();
      Buffer buffer(current, size, allocator);
      current = next;
    }
  }

  ArenaComponent* next() const {
    return next_;
  }

  ArenaComponent* SetNext(ArenaComponent* next) {
    auto* result = next_;
    next_ = next;
    return result;
  }

 private:
  uint8_t* begin_of_this() {
    return pointer_cast<uint8_t*>(this);
  }

  // Mark the given range unpoisoned in ASAN.
  // This is a no-op in a non-ASAN build.
  void AsanUnpoison(const void* addr, size_t size);

  uint8_t* end_;
  typename Traits::pointer position_;
  ArenaComponent* next_;

#ifdef ADDRESS_SANITIZER
  // Lock used around unpoisoning memory when ASAN is enabled.
  // ASAN does not support concurrent unpoison calls that may overlap a particular
  // memory word (8 bytes).
  typedef typename Traits::mutex_type mutex_type;
  mutex_type asan_lock_;
#endif
};

template <class Traits>
template <class T, class... Args>
inline T *ArenaBase<Traits>::NewObject(Args&&... args) {
  void *mem = AllocateBytesAligned(sizeof(T), alignof(T));
  if (mem == nullptr) throw std::bad_alloc();
  return new (mem) T(std::forward<Args>(args)...);
}

template <class Traits>
template<class TObject, typename... TypeArgs>
std::shared_ptr<TObject> ArenaBase<Traits>::AllocateShared(TypeArgs&&... args) {
  ArenaAllocatorBase<TObject, Traits> allocator(this);
  return std::allocate_shared<TObject>(allocator, std::forward<TypeArgs>(args)...);
}

//--------------------------------------------------------------------------------------------------
// Arena deleter class for shared_ptr and unique_ptr.
class ArenaObjectDeleter {
 public:
  template<class TObject>
  void operator()(TObject *obj) {
    obj->~TObject();
  }
};

template <class Traits>
template<class TObject>
std::shared_ptr<TObject> ArenaBase<Traits>::ToShared(TObject *raw_ptr) {
  ArenaAllocatorBase<TObject, Traits> allocator(this);
  return std::shared_ptr<TObject>(raw_ptr, ArenaObjectDeleter(), allocator);
}

} // namespace internal

template <class Result, class Traits, class... Args>
std::shared_ptr<Result> ArenaMakeShared(
    const std::shared_ptr<internal::ArenaBase<Traits>>& arena, Args&&... args) {
  auto result = arena->template NewObject<Result>(std::forward<Args>(args)...);
  return std::shared_ptr<Result>(arena, result);
}

std::shared_ptr<ThreadSafeArena> SharedArena();

} // namespace yb

template<class Traits>
void* operator new(size_t bytes, yb::internal::ArenaBase<Traits>* arena) noexcept {
  return arena->AllocateBytesAligned(bytes, sizeof(void*));
}
