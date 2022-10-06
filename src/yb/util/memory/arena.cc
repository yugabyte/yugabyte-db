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

#include "yb/util/memory/arena.h"

#include <algorithm>
#include <mutex>

#include "yb/util/alignment.h"
#include "yb/util/debug-util.h"
#include "yb/util/flag_tags.h"

using std::copy;
using std::max;
using std::min;
using std::reverse;
using std::shared_ptr;
using std::sort;
using std::swap;

DEFINE_uint64(arena_warn_threshold_bytes, 256*1024*1024,
             "Number of bytes beyond which to emit a warning for a large arena");
TAG_FLAG(arena_warn_threshold_bytes, hidden);

namespace yb {
namespace internal {

template <class Traits>
uint8_t* ArenaComponent<Traits>::AllocateBytesAligned(const size_t size, const size_t alignment) {
  // Special case check the allowed alignments. Currently, we only ensure
  // the allocated buffer components are 16-byte aligned, and the code path
  // doesn't support larger alignment.
  DCHECK(alignment == 1 || alignment == 2 || alignment == 4 ||
         alignment == 8 || alignment == 16)
    << "bad alignment: " << alignment;

  for (;;) {
    uint8_t* position = position_;

    const auto aligned = align_up(position, alignment);
    const auto new_position = aligned + size;

    if (PREDICT_TRUE(new_position <= end_)) {
      bool success = Traits::CompareExchange(new_position, &position_, &position);
      if (PREDICT_TRUE(success)) {
        AsanUnpoison(aligned, size);
        return aligned;
      }
    } else {
      return nullptr;
    }
  }
}

template <class Traits>
inline void ArenaComponent<Traits>::AsanUnpoison(const void* addr, size_t size) {
#ifdef ADDRESS_SANITIZER
  std::lock_guard<mutex_type> l(asan_lock_);
  ASAN_UNPOISON_MEMORY_REGION(addr, size);
#endif
}

// Fast-path allocation should get inlined, and fall-back
// to non-inline function call for allocation failure
template <class Traits>
inline void *ArenaBase<Traits>::AllocateBytesAligned(const size_t size, const size_t align) {
  void* result = AcquireLoadCurrent()->AllocateBytesAligned(size, align);
  if (PREDICT_TRUE(result != nullptr)) return result;
  return AllocateBytesFallback(size, align);
}

template <class Traits>
inline uint8_t* ArenaBase<Traits>::AddSlice(const Slice& value) {
  return reinterpret_cast<uint8_t *>(AddBytes(value.data(), value.size()));
}

template <class Traits>
inline void *ArenaBase<Traits>::AddBytes(const void *data, size_t len) {
  void* destination = AllocateBytes(len);
  if (destination == nullptr) return nullptr;
  memcpy(destination, data, len);
  return destination;
}

template <class Traits>
inline bool ArenaBase<Traits>::RelocateSlice(const Slice &src, Slice *dst) {
  void* destination = AllocateBytes(src.size());
  if (destination == nullptr) return false;
  memcpy(destination, src.data(), src.size());
  *dst = Slice(reinterpret_cast<uint8_t *>(destination), src.size());
  return true;
}

template <class Traits>
ArenaBase<Traits>::ArenaBase(
  BufferAllocator* const buffer_allocator,
  size_t initial_buffer_size,
  size_t max_buffer_size)
    : buffer_allocator_(buffer_allocator),
      max_buffer_size_(max_buffer_size) {
  AddComponentUnlocked(NewBuffer(initial_buffer_size, 0));
}

template <class Traits>
ArenaBase<Traits>::ArenaBase(size_t initial_buffer_size, size_t max_buffer_size)
    : buffer_allocator_(HeapBufferAllocator::Get()),
      max_buffer_size_(max_buffer_size) {
  AddComponentUnlocked(NewBuffer(initial_buffer_size, 0));
}

template <class Traits>
ArenaBase<Traits>::~ArenaBase() {
  AcquireLoadCurrent()->Destroy(buffer_allocator_);
}

template <class Traits>
void *ArenaBase<Traits>::AllocateBytesFallback(const size_t size, const size_t align) {
  std::lock_guard<mutex_type> lock(component_lock_);

  // It's possible another thread raced with us and already allocated
  // a new component, in which case we should try the "fast path" again
  Component* cur = CHECK_NOTNULL(AcquireLoadCurrent());
  void * result = cur->AllocateBytesAligned(size, align);
  if (PREDICT_FALSE(result != nullptr)) return result;

  // Really need to allocate more space.
  const size_t buffer_size = size + sizeof(Component);
  // But, allocate enough, even if the request is large. In this case,
  // might violate the max_element_size bound.
  size_t next_component_size = std::max(std::min(2 * cur->full_size(), max_buffer_size_),
                                        buffer_size);

  // If soft quota is exhausted we will only get the "minimal" amount of memory
  // we ask for. In this case if we always use "size" as minimal, we may degrade
  // to allocating a lot of tiny components, one for each string added to the
  // arena. This would be very inefficient, so let's first try something between
  // "size" and "next_component_size". If it fails due to hard quota being
  // exhausted, we'll fall back to using "size" as minimal.
  size_t minimal = (buffer_size + next_component_size) / 2;
  CHECK_LE(buffer_size, minimal);
  CHECK_LE(minimal, next_component_size);
  // Now, just make sure we can actually get the memory.
  Buffer buffer = NewBufferInTwoAttempts(next_component_size, minimal, buffer_size);
  if (!buffer) return nullptr;

  // Now, must succeed. The component has at least 'size' bytes.
  ASAN_UNPOISON_MEMORY_REGION(buffer.data(), sizeof(Component));
  auto component = new (buffer.data()) Component(buffer.size(), AcquireLoadCurrent());
  result = component->AllocateBytesAligned(size, align);
  CHECK(result != nullptr);

  // Now add it to the arena.
  AddComponentUnlocked(std::move(buffer), component);

  return result;
}

template <class Traits>
Buffer ArenaBase<Traits>::NewBufferInTwoAttempts(size_t requested_size,
                                                     size_t mid_size,
                                                     size_t min_size) {
  Buffer buffer = NewBuffer(requested_size, mid_size);
  if (!buffer) {
    return NewBuffer(requested_size, min_size);
  }
  return buffer;
}

template <class Traits>
Buffer ArenaBase<Traits>::NewBuffer(size_t requested_size, size_t minimum_size) {
  const size_t min_possible = sizeof(Component) * 2;
  requested_size = std::max(requested_size, min_possible);
  minimum_size = std::max(minimum_size, min_possible);
  Buffer buffer = buffer_allocator_->BestEffortAllocate(requested_size, minimum_size);
  if (!buffer)
    return buffer;

  CHECK_EQ(reinterpret_cast<uintptr_t>(buffer.data()) & (16 - 1), 0)
      << "Components should be 16-byte aligned: " << buffer.data();

  ASAN_POISON_MEMORY_REGION(buffer.data(), buffer.size());

  return buffer;
}

// LOCKING: component_lock_ must be held by the current thread.
template <class Traits>
void ArenaBase<Traits>::AddComponentUnlocked(Buffer buffer, Component* component) {
  if (!component) {
    ASAN_UNPOISON_MEMORY_REGION(buffer.data(), sizeof(Component));
    component = new (buffer.data()) Component(buffer.size(), AcquireLoadCurrent());
  }

  buffer.Release();
  ReleaseStoreCurrent(component);
  if (!second_) {
    second_ = component;
  }
  arena_footprint_ += component->full_size();
  if (PREDICT_FALSE(arena_footprint_ > FLAGS_arena_warn_threshold_bytes) && !warned_) {
    LOG(WARNING) << "Arena " << reinterpret_cast<const void *>(this)
                 << " footprint (" << arena_footprint_ << " bytes) exceeded warning threshold ("
                 << FLAGS_arena_warn_threshold_bytes << " bytes)\n"
                 << GetStackTrace();
    warned_ = true;
  }
}

template <class Traits>
void ArenaBase<Traits>::Reset(ResetMode mode) {
  std::lock_guard<mutex_type> lock(component_lock_);

  Component* current = CHECK_NOTNULL(AcquireLoadCurrent());
  if (mode == ResetMode::kKeepFirst && second_) {
    auto* first = second_->SetNext(nullptr);
    current->Destroy(buffer_allocator_);
    current = first;
    ReleaseStoreCurrent(first);
    second_ = nullptr;
  }

  current->Reset(buffer_allocator_);
  warned_ = false;

#ifndef NDEBUG
  // In debug mode release the last component too for (hopefully) better
  // detection of memory-related bugs (invalid shallow copies, etc.).
  size_t last_size = current->full_size();
  current->Destroy(buffer_allocator_);
  arena_footprint_ = 0;
  ReleaseStoreCurrent(nullptr);
  AddComponentUnlocked(NewBuffer(last_size, 0));
#else
  arena_footprint_ = current->full_size();
#endif
}

template <class Traits>
size_t ArenaBase<Traits>::memory_footprint() const {
  std::lock_guard<mutex_type> lock(component_lock_);
  return arena_footprint_;
}

// Explicit instantiation.
template class ArenaBase<ThreadSafeArenaTraits>;
template class ArenaBase<ArenaTraits>;

}  // namespace internal

char* AllocatedBuffer::Allocate(size_t bytes, size_t alignment) {
  auto allocation_size = Arena::kStartBlockSize;
  auto* allocated = static_cast<char*>(malloc(allocation_size));
  auto* result = align_up(allocated, alignment);
  address = align_up(pointer_cast<char*>(result + bytes), 16);
  size = allocated + allocation_size - address;
  return result;
}

}  // namespace yb
