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
// Classes for memory management, used by materializations
// (arenas, segments, and STL collections parametrized via arena allocators)
// so that memory usage can be controlled at the application level.
//
// Materializations can be parametrized by specifying an instance of a
// BufferAllocator. The allocator implements
// memory management policy (e.g. setting allocation limits). Allocators may
// be shared between multiple materializations; e.g. you can designate a
// single allocator per a single user request, thus setting bounds on memory
// usage on a per-request basis.

#pragma once

#include <stddef.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include <boost/mpl/if.hpp>
#include <boost/type_traits/is_const.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/logging-inl.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/strings/stringpiece.h"

#include "yb/util/boost_mutex_utils.h"
#include "yb/util/mutex.h"


namespace yb {

class BufferAllocator;
class MemTracker;

void OverwriteWithPattern(char* p, size_t len, GStringPiece pattern);

// Wrapper for a block of data allocated by a BufferAllocator. Owns the block.
// (To release the block, destroy the buffer - it will then return it via the
// same allocator that has been used to create it).
class Buffer {
 public:
  Buffer() : data_(nullptr), size_(0), allocator_(nullptr) {}

  Buffer(void* data, size_t size, BufferAllocator* allocator)
      : data_(CHECK_NOTNULL(data)),
        size_(size),
        allocator_(allocator) {
#ifndef NDEBUG
    OverwriteWithPattern(reinterpret_cast<char*>(data_), size_,
                         "NEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEW"
                         "NEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEW"
                         "NEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEW");
#endif
  }

  Buffer(Buffer&& rhs)
    : data_(rhs.data_), size_(rhs.size_), allocator_(rhs.allocator_) {
    rhs.Release();
  }

  Buffer(const Buffer& rhs) = delete;
  void operator=(const Buffer& rhs) = delete;

  ~Buffer();

  void* data() const { return data_; }   // The data buffer.
  uint8_t* udata() const { return static_cast<uint8_t*>(data_); }
  uint8_t* end() const { return udata() + size_; }
  size_t size() const { return size_; }  // In bytes.

  void Release() {
    data_ = nullptr;
    allocator_ = nullptr;
  }

  explicit operator bool() const {
    return data_ != nullptr;
  }

  bool operator!() const {
    return data_ == nullptr;
  }
 private:
  friend class BufferAllocator;

  // Called by a successful realloc.
  void Update(void* new_data, size_t new_size) {
#ifndef NDEBUG
    if (new_size > size_) {
      OverwriteWithPattern(reinterpret_cast<char*>(new_data) + size_,
                           new_size - size_, "NEW");
    }
#endif
    data_ = new_data;
    size_ = new_size;
  }

  void* data_;
  size_t size_;
  BufferAllocator* allocator_;
};

// Allocators allow applications to control memory usage. They are
// used by materializations to allocate blocks of memory arenas.
// BufferAllocator is an abstract class that defines a common contract of
// all implementations of allocators. Specific allocators provide specific
// features, e.g. enforced resource limits, thread safety, etc.
class BufferAllocator {
 public:
  virtual ~BufferAllocator() {}

  // Called by the user when a new block of memory is needed. The 'requested'
  // parameter specifies how much memory (in bytes) the user would like to get.
  // The 'minimal' parameter specifies how much he is willing to settle for.
  // The allocator returns a buffer sized in the range [minimal, requested],
  // or NULL if the request can't be satisfied. When the buffer is destroyed,
  // its destructor calls the FreeInternal() method on its allocator.
  // CAVEAT: The allocator must outlive all buffers returned by it.
  //
  // Corner cases:
  // 1. If requested == 0, the allocator will always return a valid Buffer
  //    with a non-NULL data pointer and zero capacity.
  // 2. If minimal == 0, the allocator will always return a valid Buffer
  //    with a non-NULL data pointer, possibly with zero capacity.
  Buffer BestEffortAllocate(size_t requested, size_t minimal) {
    DCHECK_LE(minimal, requested);
    Buffer result = AllocateInternal(requested, minimal, this);
    LogAllocation(requested, minimal, result);
    return result;
  }

  // Called by the user when a new block of memory is needed. Equivalent to
  // BestEffortAllocate(requested, requested).
  Buffer Allocate(size_t requested) {
    return BestEffortAllocate(requested, requested);
  }

  // Returns the amount of memory (in bytes) still available for this allocator.
  // For unbounded allocators (like raw HeapBufferAllocator) this is the highest
  // size_t value possible.
  // TODO(user): consider making pure virtual.
  virtual size_t Available() const { return std::numeric_limits<size_t>::max(); }

 protected:
  friend class Buffer;

  BufferAllocator() {}

  // Expose the constructor to subclasses of BufferAllocator.
  Buffer CreateBuffer(void* data, size_t size, BufferAllocator* allocator) {
    return Buffer(data, size, allocator);
  }

  // Expose Buffer::Update to subclasses of BufferAllocator.
  void UpdateBuffer(void* new_data, size_t new_size, Buffer* buffer) {
    buffer->Update(new_data, new_size);
  }

  // Called by chained buffer allocators.
  static Buffer DelegateAllocate(BufferAllocator* delegate,
                                 size_t requested,
                                 size_t minimal,
                                 BufferAllocator* originator) {
    return delegate->AllocateInternal(requested, minimal, originator);
  }

  // Called by chained buffer allocators.
  static bool DelegateReallocate(BufferAllocator* delegate,
                                 size_t requested,
                                 size_t minimal,
                                 Buffer* buffer,
                                 BufferAllocator* originator) {
    return delegate->ReallocateInternal(requested, minimal, buffer, originator);
  }

  // Called by chained buffer allocators.
  static void DelegateFree(BufferAllocator* delegate, Buffer* buffer) {
    delegate->FreeInternal(buffer);
  }

 private:
  // Implemented by concrete subclasses.
  virtual Buffer AllocateInternal(size_t requested,
                                  size_t minimal,
                                  BufferAllocator* originator) = 0;

  // Implemented by concrete subclasses. Returns false on failure.
  virtual bool ReallocateInternal(size_t requested,
                                  size_t minimal,
                                  Buffer* buffer,
                                  BufferAllocator* originator) = 0;

  // Implemented by concrete subclasses.
  virtual void FreeInternal(Buffer* buffer) = 0;

  // Logs a warning message if the allocation failed or if it returned less than
  // the required number of bytes.
  void LogAllocation(size_t required, size_t minimal, const Buffer& buffer);

  DISALLOW_COPY_AND_ASSIGN(BufferAllocator);
};

// Allocates buffers on the heap, with no memory limits. Uses standard C
// allocation functions (malloc, realloc, free).
class HeapBufferAllocator : public BufferAllocator {
 public:
  virtual ~HeapBufferAllocator() {}

  // Returns a singleton instance of the heap allocator.
  static HeapBufferAllocator* Get() {
    return Singleton<HeapBufferAllocator>::get();
  }

  size_t Available() const override {
    return std::numeric_limits<size_t>::max();
  }

 private:
  // Allocates memory that is aligned to 16 way.
  // Use if you want to boost SIMD operations on the memory area.
  const bool aligned_mode_;

  friend class Singleton<HeapBufferAllocator>;

  // Always allocates 'requested'-sized buffer, or returns NULL on OOM.
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override;

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override;

  void* Malloc(size_t size);
  void* Realloc(void* previousData, size_t previousSize, size_t newSize);

  void FreeInternal(Buffer* buffer) override;

  HeapBufferAllocator();
  explicit HeapBufferAllocator(bool aligned_mode)
      : aligned_mode_(aligned_mode) {}

  DISALLOW_COPY_AND_ASSIGN(HeapBufferAllocator);
};

// Wrapper around the delegate allocator, that clears all newly allocated
// (and reallocated) memory.
class ClearingBufferAllocator : public BufferAllocator {
 public:
  // Does not take ownership of the delegate.
  explicit ClearingBufferAllocator(BufferAllocator* delegate)
      : delegate_(delegate) {}

  size_t Available() const override {
    return delegate_->Available();
  }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override;

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override;

  void FreeInternal(Buffer* buffer) override;

  BufferAllocator* delegate_;
  DISALLOW_COPY_AND_ASSIGN(ClearingBufferAllocator);
};

class PreallocatedBufferAllocator : public BufferAllocator {
 public:
  PreallocatedBufferAllocator(BufferAllocator* delegate, char* buffer, size_t buffer_size)
      : delegate_(delegate), buffer_(buffer), buffer_size_(buffer_size) {
  }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override {
    if (make_signed(requested) > buffer_size_) {
      return delegate_->BestEffortAllocate(requested, minimal);
    }
    Buffer result(buffer_, buffer_size_, this);
    buffer_size_ = -buffer_size_;
    return result;
  }

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override {
    CHECK(false); // Not supported.
  }

  void FreeInternal(Buffer* buffer) override {
    if (buffer->data() == buffer_) {
      buffer_size_ = -buffer_size_;
    } else {
      BufferAllocator::DelegateFree(delegate_, buffer);
    }
  }

  BufferAllocator* delegate_;
  char* buffer_;
  int64_t buffer_size_; // Negative value means buffer was allocated.
};

// Abstract policy for modifying allocation requests - e.g. enforcing quotas.
class Mediator {
 public:
  Mediator() {}
  virtual ~Mediator() {}

  // Called by an allocator when a allocation request is processed.
  // Must return a value in the range [minimal, requested], or zero. Returning
  // zero (if minimal is non-zero) indicates denial to allocate. Returning
  // non-zero indicates that the request should be capped at that value.
  virtual size_t Allocate(size_t requested, size_t minimal) = 0;

  // Called by an allocator when the specified amount (in bytes) is released.
  virtual void Free(size_t amount) = 0;

  // TODO(user): consider making pure virtual.
  virtual size_t Available() const { return std::numeric_limits<size_t>::max(); }
};

// Optionally thread-safe skeletal implementation of a 'quota' abstraction,
// providing methods to allocate resources against the quota, and return them.
template<bool thread_safe>
class Quota : public Mediator {
 public:
  explicit Quota(bool enforced) : usage_(0), enforced_(enforced) {}
  virtual ~Quota() {}

  // Returns a value in range [minimal, requested] if not exceeding remaining
  // quota or if the quota is not enforced (soft quota), and adjusts the usage
  // value accordingly.  Otherwise, returns zero. The semantics of 'remaining
  // quota' are defined by subclasses (that must supply GetQuotaInternal()
  // method).
  size_t Allocate(size_t requested, size_t minimal) override;

  void Free(size_t amount) override;

  // Returns memory still available in the quota. For unenforced Quota objects,
  // you are still able to perform _minimal_ allocations when the available
  // quota is 0 (or less than "minimal" param).
  size_t Available() const override {
    lock_guard_maybe<Mutex> lock(Quota<thread_safe>::mutex());
    const size_t quota = GetQuotaInternal();
    return (usage_ >= quota) ? 0 : (quota - usage_);
  }

  // Returns the current quota value.
  size_t GetQuota() const;

  // Returns the current usage value, defined as a sum of all the values
  // granted by calls to Allocate, less these released via calls to Free.
  size_t GetUsage() const;

  bool enforced() const {
    return enforced_;
  }

 protected:
  // Overridden by specific implementations, to define semantics of
  // the quota, i.e. the total amount of resources that the mediator will
  // allocate. Called directly from GetQuota that optionally provides
  // thread safety. An 'Allocate' request will succeed if
  // GetUsage() + minimal <= GetQuota() or if the quota is not enforced (soft
  // quota).
  virtual size_t GetQuotaInternal() const = 0;

  Mutex* mutex() const { return thread_safe ? &mutex_ : NULL; }

 private:
  mutable Mutex mutex_;
  size_t usage_;
  bool enforced_;
  DISALLOW_COPY_AND_ASSIGN(Quota);
};

// Optionally thread-safe static quota implementation (where quota is explicitly
// set to a concrete numeric value).
template<bool thread_safe>
class StaticQuota : public Quota<thread_safe> {
 public:
  explicit StaticQuota(size_t quota)
      : Quota<thread_safe>(true) {
    SetQuota(quota);
  }
  StaticQuota(size_t quota, bool enforced)
      : Quota<thread_safe>(enforced) {
    SetQuota(quota);
  }
  virtual ~StaticQuota() {}

  // Sets quota to the new value.
  void SetQuota(const size_t quota);

 protected:
  virtual size_t GetQuotaInternal() const { return quota_; }

 private:
  size_t quota_;
  DISALLOW_COPY_AND_ASSIGN(StaticQuota);
};

// Places resource limits on another allocator, using the specified Mediator
// (e.g. quota) implementation.
//
// If the mediator and the delegate allocator are thread-safe, this allocator
// is also thread-safe, to the extent that it will not introduce any
// state inconsistencies. However, without additional synchronization,
// allocation requests are not atomic end-to-end. This way, it is deadlock-
// resilient (even if you have cyclic relationships between allocators) and
// allows better concurrency. But, it may cause over-conservative
// allocations under memory contention, if you have multiple levels of
// mediating allocators. For example, if two requests that can't both be
// satisfied are submitted concurrently, it may happen that one of them succeeds
// but gets smaller buffer allocated than it would if the requests were strictly
// ordered. This is usually not a problem, however, as you don't really want to
// operate so close to memory limits that some of your allocations can't be
// satisfied. If you do have a simple, cascading graph of allocators though,
// and want to force requests be atomic end-to-end, put a
// ThreadSafeBufferAllocator at the entry point.
class MediatingBufferAllocator : public BufferAllocator {
 public:
  // Does not take ownership of the delegate, nor the mediator, allowing
  // both to be reused.
  MediatingBufferAllocator(BufferAllocator* const delegate,
                           Mediator* const mediator)
      : delegate_(delegate),
        mediator_(mediator) {}

  virtual ~MediatingBufferAllocator() {}

  size_t Available() const override {
    return std::min(delegate_->Available(), mediator_->Available());
  }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override;

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override;

  void FreeInternal(Buffer* buffer) override;

  BufferAllocator* delegate_;
  Mediator* const mediator_;
};

// Convenience non-thread-safe static memory bounds enforcer.
// Combines MediatingBufferAllocator with a StaticQuota.
class MemoryLimit : public BufferAllocator {
 public:
  // Creates a limiter based on the default, heap allocator. Quota is infinite.
  // (Can be set using SetQuota).
  MemoryLimit()
      : quota_(std::numeric_limits<size_t>::max()),
        allocator_(HeapBufferAllocator::Get(), &quota_) {}

  // Creates a limiter based on the default, heap allocator.
  explicit MemoryLimit(size_t quota)
      : quota_(quota),
        allocator_(HeapBufferAllocator::Get(), &quota_) {}

  // Creates a limiter relaying to the specified delegate allocator.
  MemoryLimit(size_t quota, BufferAllocator* const delegate)
      : quota_(quota),
        allocator_(delegate, &quota_) {}

  // Creates a (possibly non-enforcing) limiter relaying to the specified
  // delegate allocator.
  MemoryLimit(size_t quota, bool enforced, BufferAllocator* const delegate)
      : quota_(quota, enforced),
        allocator_(delegate, &quota_) {}

  virtual ~MemoryLimit() {}

  size_t Available() const override {
    return allocator_.Available();
  }

  size_t GetQuota() const { return quota_.GetQuota(); }
  size_t GetUsage() const { return quota_.GetUsage(); }
  void SetQuota(const size_t quota) { quota_.SetQuota(quota); }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override {
    return DelegateAllocate(&allocator_, requested, minimal, originator);
  }

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override {
    return DelegateReallocate(&allocator_, requested, minimal, buffer, originator);
  }

  void FreeInternal(Buffer* buffer) override {
    DelegateFree(&allocator_, buffer);
  }

  StaticQuota<false> quota_;
  MediatingBufferAllocator allocator_;
};

// An allocator that allows to bypass the (potential) soft quota below for a
// given amount of memory usage. The goal is to make the allocation methods and
// Available() work as if the allocator below had at least bypassed_amount of
// soft quota. Of course this class doesn't allow to exceed the hard quota.
class SoftQuotaBypassingBufferAllocator : public BufferAllocator {
 public:
  SoftQuotaBypassingBufferAllocator(BufferAllocator* allocator,
                                    size_t bypassed_amount)
      : allocator_(std::numeric_limits<size_t>::max(), allocator),
        bypassed_amount_(bypassed_amount) {}

  size_t Available() const override {
    const size_t usage = allocator_.GetUsage();
    size_t available = allocator_.Available();
    if (bypassed_amount_ > usage) {
      available = std::max(bypassed_amount_ - usage, available);
    }
    return available;
  }

 private:
  // Calculates how much to increase the minimal parameter to allocate more
  // aggressively in the underlying allocator. This is to avoid getting only
  // very small allocations when we exceed the soft quota below. The request
  // with increased minimal size is more likely to fail because of exceeding
  // hard quota, so we also fall back to the original minimal size.
  size_t AdjustMinimal(size_t requested, size_t minimal) const {
    return std::min(requested, std::max(minimal, Available()));
  }

  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override {
    // Try increasing the "minimal" parameter to allocate more aggresively
    // within the bypassed amount of soft quota.
    Buffer result = DelegateAllocate(&allocator_,
                                     requested,
                                     AdjustMinimal(requested, minimal),
                                     originator);
    if (result) {
      return result;
    } else {
      return DelegateAllocate(&allocator_, requested, minimal, originator);
    }
  }

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override {
    size_t adjusted_minimal = AdjustMinimal(requested, minimal);
    if (DelegateReallocate(&allocator_, requested, adjusted_minimal, buffer, originator)) {
      return true;
    } else {
      return DelegateReallocate(&allocator_, requested, minimal, buffer, originator);
    }
  }

  void FreeInternal(Buffer* buffer) override {
    DelegateFree(&allocator_, buffer);
  }

  // Using MemoryLimit with "infinite" limit to get GetUsage().
  MemoryLimit allocator_;
  size_t bypassed_amount_;
};

// An interface for a MemoryStatisticsCollector - an object which collects
// information about the memory usage of the allocator. The collector will
// gather statistics about memory usage based on information received from the
// allocator.
class MemoryStatisticsCollectorInterface {
 public:
  MemoryStatisticsCollectorInterface() {}

  virtual ~MemoryStatisticsCollectorInterface() {}

  // Informs the collector that the allocator granted bytes memory. Note that in
  // the case of reallocation bytes should be the increase in total memory
  // usage, not the total size of the buffer after reallocation.
  virtual void AllocatedMemoryBytes(size_t bytes) = 0;

  // Informs the collector that the allocator received a request for at least
  // bytes memory, and rejected it (meaning that it granted nothing).
  virtual void RefusedMemoryBytes(size_t bytes) = 0;

  // Informs the collector that bytes memory have been released to the
  // allocator.
  virtual void FreedMemoryBytes(size_t bytes) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(MemoryStatisticsCollectorInterface);
};

class MemoryStatisticsCollectingBufferAllocator : public BufferAllocator {
 public:
  // Does not take ownership of the delegate.
  // Takes ownership of memory_stats_collector.
  MemoryStatisticsCollectingBufferAllocator(
      BufferAllocator* const delegate,
      MemoryStatisticsCollectorInterface* const memory_stats_collector)
      : delegate_(delegate),
        memory_stats_collector_(memory_stats_collector) {}

  virtual ~MemoryStatisticsCollectingBufferAllocator() {}

  size_t Available() const override {
    return delegate_->Available();
  }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override;

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override;

  void FreeInternal(Buffer* buffer) override;

  BufferAllocator* delegate_;
  std::unique_ptr<MemoryStatisticsCollectorInterface>
      memory_stats_collector_;
};

// BufferAllocator which uses MemTracker to keep track of and optionally
// (if a limit is set on the MemTracker) regulate memory consumption.
class MemoryTrackingBufferAllocator : public BufferAllocator {
 public:
  // Does not take ownership of the delegate. The delegate must remain
  // valid for the lifetime of this allocator. Increments reference
  // count for 'mem_tracker'.
  // If 'mem_tracker' has a limit and 'enforce_limit' is true, then
  // the classes calling this buffer allocator (whether directly, or
  // through an Arena) must be able to handle the case when allocation
  // fails. If 'enforce_limit' is false (this is the default), then
  // allocation will always succeed.
  MemoryTrackingBufferAllocator(BufferAllocator* const delegate,
                                std::shared_ptr<MemTracker> mem_tracker,
                                bool enforce_limit = false)
      : delegate_(delegate),
        mem_tracker_(std::move(mem_tracker)),
        enforce_limit_(enforce_limit) {}

  virtual ~MemoryTrackingBufferAllocator() {}

  // If enforce limit is false, this always returns maximum possible value
  // for int64_t (std::numeric_limits<int64_t>::max()). Otherwise, this
  // is equivalent to calling mem_tracker_->SpareCapacity();
  size_t Available() const override;

 private:

  // If enforce_limit_ is true, this is equivalent to calling
  // mem_tracker_->TryConsume(bytes). If enforce_limit_ is false and
  // mem_tracker_->TryConsume(bytes) is false, we call
  // mem_tracker_->Consume(bytes) and always return true.
  bool TryConsume(int64_t bytes);

  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override;

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override;

  void FreeInternal(Buffer* buffer) override;

  BufferAllocator* delegate_;
  std::shared_ptr<MemTracker> mem_tracker_;
  bool enforce_limit_;
};

// Synchronizes access to AllocateInternal and FreeInternal, and exposes the
// mutex for use by subclasses. Allocation requests performed through this
// allocator are atomic end-to-end. Template parameter DelegateAllocatorType
// allows to specify a subclass of BufferAllocator for the delegate, to allow
// subclasses of ThreadSafeBufferAllocator to access additional methods provided
// by the allocator subclass. If this is not needed, it can be set to
// BufferAllocator.
template <class DelegateAllocatorType>
class ThreadSafeBufferAllocator : public BufferAllocator {
 public:
  // Does not take ownership of the delegate.
  explicit ThreadSafeBufferAllocator(DelegateAllocatorType* delegate)
      : delegate_(delegate) {}
  virtual ~ThreadSafeBufferAllocator() {}

  size_t Available() const override {
    lock_guard_maybe<Mutex> lock(mutex());
    return delegate()->Available();
  }

 protected:
  Mutex* mutex() const { return &mutex_; }
  // Expose the delegate allocator, with the precise type of the allocator
  // specified by the template parameter. The delegate() methods themselves
  // don't give any thread-safety guarantees. Protect all uses taking the Mutex
  // exposed by the mutex() method.
  DelegateAllocatorType* delegate() { return delegate_; }
  const DelegateAllocatorType* delegate() const { return delegate_; }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override {
    lock_guard_maybe<Mutex> lock(mutex());
    return DelegateAllocate(delegate(), requested, minimal, originator);
  }

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override {
    lock_guard_maybe<Mutex> lock(mutex());
    return DelegateReallocate(delegate(), requested, minimal, buffer, originator);
  }

  void FreeInternal(Buffer* buffer) override {
    lock_guard_maybe<Mutex> lock(mutex());
    DelegateFree(delegate(), buffer);
  }

  DelegateAllocatorType* delegate_;
  mutable Mutex mutex_;
  DISALLOW_COPY_AND_ASSIGN(ThreadSafeBufferAllocator);
};

// A version of ThreadSafeBufferAllocator that owns the supplied delegate
// allocator.
template <class DelegateAllocatorType>
class OwningThreadSafeBufferAllocator
    : public ThreadSafeBufferAllocator<DelegateAllocatorType> {
 public:
  explicit OwningThreadSafeBufferAllocator(DelegateAllocatorType* delegate)
      : ThreadSafeBufferAllocator<DelegateAllocatorType>(delegate),
        delegate_owned_(delegate) {}
  virtual ~OwningThreadSafeBufferAllocator() {}

 private:
  std::unique_ptr<DelegateAllocatorType> delegate_owned_;
};

class ThreadSafeMemoryLimit
    : public OwningThreadSafeBufferAllocator<MemoryLimit> {
 public:
  ThreadSafeMemoryLimit(size_t quota, bool enforced,
                        BufferAllocator* const delegate)
      : OwningThreadSafeBufferAllocator<MemoryLimit>(
            new MemoryLimit(quota, enforced, delegate)) {}
  virtual ~ThreadSafeMemoryLimit() {}

  size_t GetQuota() const {
    lock_guard_maybe<Mutex> lock(mutex());
    return delegate()->GetQuota();
  }
  size_t GetUsage() const {
    lock_guard_maybe<Mutex> lock(mutex());
    return delegate()->GetUsage();
  }
  void SetQuota(const size_t quota) {
    lock_guard_maybe<Mutex> lock(mutex());
    delegate()->SetQuota(quota);
  }
};

// A BufferAllocator that can be given ownership of many objects of given type.
// These objects will then be deleted when the buffer allocator is destroyed.
// The objects added last are deleted first (LIFO).
template <typename OwnedType>
class OwningBufferAllocator : public BufferAllocator {
 public:
  // Doesn't take ownership of delegate.
  explicit OwningBufferAllocator(BufferAllocator* const delegate)
      : delegate_(delegate) {}

  virtual ~OwningBufferAllocator() {
    // Delete elements starting from the end.
    while (!owned_.empty()) {
      OwnedType* p = owned_.back();
      owned_.pop_back();
      delete p;
    }
  }

  // Add to the collection of objects owned by this allocator. The object added
  // last is deleted first.
  OwningBufferAllocator* Add(OwnedType* p) {
    owned_.push_back(p);
    return this;
  }

  size_t Available() const override {
    return delegate_->Available();
  }

 private:
  Buffer AllocateInternal(size_t requested, size_t minimal, BufferAllocator* originator) override {
    return DelegateAllocate(delegate_, requested, minimal, originator);
  }

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override {
    return DelegateReallocate(delegate_, requested, minimal, buffer, originator);
  }

  void FreeInternal(Buffer* buffer) override {
    DelegateFree(delegate_, buffer);
  }

  // Not using PointerVector here because we want to guarantee certain order of
  // deleting elements (starting from the ones added last).
  std::vector<OwnedType*> owned_;
  BufferAllocator* delegate_;
};

// Buffer allocator that tries to guarantee the exact and consistent amount
// of memory. Uses hard MemoryLimit to enforce the upper bound but also
// guarantees consistent allocations by ignoring minimal requested amounts and
// always returning the full amount of memory requested if available.
// Allocations will fail if the memory requested would exceed the quota or if
// the underlying allocator fails to provide the memory.
class GuaranteeMemory : public BufferAllocator {
 public:
  // Doesn't take ownership of 'delegate'.
  GuaranteeMemory(size_t memory_quota,
                  BufferAllocator* delegate)
      : limit_(memory_quota, true, delegate),
        memory_guarantee_(memory_quota) {}

  size_t Available() const override {
    return memory_guarantee_ - limit_.GetUsage();
  }

 private:
  Buffer AllocateInternal(size_t requested,
                          size_t minimal,
                          BufferAllocator* originator) override {
    if (requested > Available()) {
      return Buffer();
    } else {
      return DelegateAllocate(&limit_, requested, requested, originator);
    }
  }

  bool ReallocateInternal(size_t requested,
                          size_t minimal,
                          Buffer* buffer,
                          BufferAllocator* originator) override {
    int64 additional_memory = requested - (buffer != NULL ? buffer->size() : 0);
    return additional_memory <= static_cast<int64>(Available())
        && DelegateReallocate(&limit_, requested, requested,
                              buffer, originator);
  }

  void FreeInternal(Buffer* buffer) override {
    DelegateFree(&limit_, buffer);
  }

  MemoryLimit limit_;
  size_t memory_guarantee_;
  DISALLOW_COPY_AND_ASSIGN(GuaranteeMemory);
};

// Implementation of inline and template methods

template<bool thread_safe>
size_t Quota<thread_safe>::Allocate(const size_t requested,
                                    const size_t minimal) {
  lock_guard_maybe<Mutex> lock(mutex());
  DCHECK_LE(minimal, requested)
      << "\"minimal\" shouldn't be bigger than \"requested\"";
  const size_t quota = GetQuotaInternal();
  size_t allocation;
  if (usage_ > quota || minimal > quota - usage_) {
    // OOQ (Out of quota).
    if (!enforced() && minimal <= std::numeric_limits<size_t>::max() - usage_) {
      // The quota is unenforced and the value of "minimal" won't cause an
      // overflow. Perform a minimal allocation.
      allocation = minimal;
    } else {
      allocation = 0;
    }
    LOG(WARNING) << "Out of quota. Requested: " << requested
                 << " bytes, or at least minimal: " << minimal
                 << ". Current quota value is: " << quota
                 << " while current usage is: " << usage_
                 << ". The quota is " << (enforced() ? "" : "not ")
                 << "enforced. "
                 << ((allocation == 0) ? "Did not allocate any memory."
                 : "Allocated the minimal value requested.");
  } else {
    allocation = std::min(requested, quota - usage_);
  }
  usage_ += allocation;
  return allocation;
}

template<bool thread_safe>
void Quota<thread_safe>::Free(size_t amount) {
  lock_guard_maybe<Mutex> lock(mutex());
  usage_ -= amount;
  // threads allocate/free memory concurrently via the same Quota object that is
  // not protected with a mutex (thread_safe == false).
  if (usage_ > (std::numeric_limits<size_t>::max() - (1 << 28))) {
    LOG(ERROR) << "Suspiciously big usage_ value: " << usage_
               << " (could be a result size_t wrapping around below 0, "
               << "for example as a result of race condition).";
  }
}

template<bool thread_safe>
size_t Quota<thread_safe>::GetQuota() const {
  lock_guard_maybe<Mutex> lock(mutex());
  return GetQuotaInternal();
}

template<bool thread_safe>
size_t Quota<thread_safe>::GetUsage() const {
  lock_guard_maybe<Mutex> lock(mutex());
  return usage_;
}

template<bool thread_safe>
void StaticQuota<thread_safe>::SetQuota(const size_t quota) {
  lock_guard_maybe<Mutex> lock(Quota<thread_safe>::mutex());
  quota_ = quota;
}

template <class T>
using EndOfObjectResultType =
  typename boost::mpl::if_<boost::is_const<T>, const char*, char*>::type;

template <class T>
EndOfObjectResultType<T>
EndOfObject(T* t) {
  typedef EndOfObjectResultType<T> ResultType;
  return reinterpret_cast<ResultType>(t) + sizeof(T);
}

// There is a shared_from_this() standard function injected into class by extending
// std::enable_shared_from_this template. We use this for ReactorTask and MonitoredTask base
// classes. But for their subclasses we sometimes need to get shared pointer to specific class
// type, for example for DelayedTask we need to get shared_ptr<DelayedTask>.
// shared_from_this defined in the base ReactorTask class will return shared_ptr<ReactorTask>.
// That is why we defined template free function shared_from which will downcast to shared_pointer
// to type deduced from whatever we pass as an argument, shared_ptr<DelayedTask> in this case.
template <typename U>
std::shared_ptr<U> shared_from(U* u) {
  return std::static_pointer_cast<U>(u->shared_from_this());
}

template <class U>
std::shared_ptr<U> FakeSharedPtr(U* u) {
  return std::shared_ptr<U>(std::shared_ptr<U>(), u);
}

class LazySharedPtrFactory {
 public:
  template <class T>
  operator std::shared_ptr<T>() const { return std::make_shared<T>(); }
};

// Returns empty string if TCMalloc is not enabled.
std::string TcMallocStats();

}  // namespace yb
