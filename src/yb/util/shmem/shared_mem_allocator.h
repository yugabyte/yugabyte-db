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

#include <memory>
#include <string_view>
#include <type_traits>

#include "yb/util/enums.h"

#include "yb/util/shmem/reserved_address_segment.h"
#include "yb/util/shmem/shared_mem_segment.h"

namespace yb {

class SharedMemoryBackingAllocator;
class SharedMemoryAllocatorImpl;

template<typename T>
struct SharedMemoryDeleter {
  void operator()(T* p) const noexcept;

  SharedMemoryBackingAllocator* allocator;
};

template<typename T>
using SharedMemoryUniquePtr = std::unique_ptr<T, SharedMemoryDeleter<T>>;

class SharedMemoryAllocatorPrepareState {
 public:
  void* UserData() { return user_data_; }

 private:
  friend class SharedMemoryBackingAllocator;

  SharedMemoryAllocatorPrepareState(
      SharedMemSegment&& shared_mem_segment, std::string_view prefix, void* user_data)
      : shared_mem_segment_(std::move(shared_mem_segment)), prefix_(prefix),
        user_data_(user_data) {}

  SharedMemSegment shared_mem_segment_;
  std::string prefix_;
  void* user_data_;
};

class SharedMemoryBackingAllocator {
 public:
  SharedMemoryBackingAllocator();
  ~SharedMemoryBackingAllocator();

  SharedMemoryBackingAllocator(SharedMemoryBackingAllocator&&) = delete;
  SharedMemoryBackingAllocator(const SharedMemoryBackingAllocator&) = delete;
  SharedMemoryBackingAllocator& operator=(SharedMemoryBackingAllocator&&) = delete;
  SharedMemoryBackingAllocator& operator=(const SharedMemoryBackingAllocator&) = delete;

  // This needs to be called on the parent process before child processes are created.
  // The underlying shared memory files will be created, and a block of shared memory of size
  // `user_data_size` will be allocated to help user avoid race conditions with child.
  static Result<SharedMemoryAllocatorPrepareState> Prepare(
      std::string_view prefix, size_t user_data_size);

  Status InitOwner(
      ReservedAddressSegment& address_segment, SharedMemoryAllocatorPrepareState&& prepare_state);

  Status InitChild(
      ReservedAddressSegment& address_segment, std::string_view prefix);

  Result<void*> Allocate(size_t size);
  void Deallocate(void* p, size_t size);

  // User data allocated in `Prepare`.
  template<typename T>
  T* UserData() {
    return pointer_cast<T*>(BasePtr());
  }

  template<typename T>
  SharedMemoryUniquePtr<T> UserDataOwner() {
    return SharedMemoryUniquePtr<T>(UserData<T>(), SharedMemoryDeleter<T>{.allocator = this});
  }

  template<typename T, typename... Args>
  Result<SharedMemoryUniquePtr<T>> MakeUnique(Args&&... args) {
    auto ptr = VERIFY_RESULT(Allocate(sizeof(T)));
    return SharedMemoryUniquePtr<T>(
        new (ptr) T (std::forward<Args>(args)...),
        SharedMemoryDeleter<T>{.allocator = this});
  }

 private:
  class Impl;
  class HeaderSegment;

  friend class SharedMemoryAllocatorImpl;

  void* BasePtr();

  AnonymousMemoryPtr<Impl> impl_;
};

template<typename T>
void SharedMemoryDeleter<T>::operator()(T* p) const noexcept {
  p->~T();
  allocator->Deallocate(p, sizeof(T));
}

// The allocator has to store pointer to Impl, not SharedMemoryBackingAllocator, because
// SharedMemoryBackingAllocator is located outside of the reserved address segment. But since
// SharedMemoryAllocator is a template class, we use SharedMemoryAllocatorImpl as a proxy to avoid
// exposing Impl in the header.
class SharedMemoryAllocatorImpl {
 public:
  explicit SharedMemoryAllocatorImpl(SharedMemoryBackingAllocator& backing);

  Result<void*> Allocate(size_t n);

  void Deallocate(void* p, size_t n);

 private:
  SharedMemoryBackingAllocator::Impl* impl_;
};

template<typename T>
class SharedMemoryAllocator {
 public:
  using pointer = T*;
  using const_pointer = const T*;
  using void_pointer = void*;
  using const_void_pointer = const void*;
  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  template<typename U>
  using rebind = SharedMemoryAllocator<U>;

  explicit SharedMemoryAllocator(SharedMemoryBackingAllocator& backing) : impl_{backing} { }

  template<typename U>
  SharedMemoryAllocator(const SharedMemoryAllocator<U>& other) : impl_{other.impl_} { }

  Result<pointer> Allocate(size_type n = 1) {
    auto ptr = VERIFY_RESULT(impl_.Allocate(n * sizeof(T)));
    return static_cast<pointer>(ptr);
  }

  void Deallocate(pointer p, size_type n = 1) {
    impl_.Deallocate(p, n * sizeof(T));
  }

  pointer allocate(size_type n = 1) {
    auto result = Allocate(n);
    CHECK_OK(result);
    return *result;
  }

  void deallocate(pointer p, size_type n = 1) {
    Deallocate(p, n);
  }

 private:
  template<typename U>
  friend class SharedMemoryAllocator;

  SharedMemoryAllocatorImpl impl_;
};

} // namespace yb
