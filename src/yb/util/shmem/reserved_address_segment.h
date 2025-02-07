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

#include <cstddef>
#include <utility>

#include "yb/util/result.h"
#include "yb/util/size_literals.h"

namespace yb {

void RemapToReservedRegion(void* p, size_t size);

template<typename T>
struct AnonymousMemoryDeleter {
  void operator()(T* p) {
    p->~T();
    RemapToReservedRegion(p, sizeof(T));
  }
};

template<typename T>
using AnonymousMemoryPtr = std::unique_ptr<T, AnonymousMemoryDeleter<T>>;

class ReservedAddressSegment;

// We want to reserve virtual addresses at the same place in both parent and child process. But
// since we use exec()/posix_spawn() for child process, all mappings the parent has before are lost,
// and any address range reserved earlier in the parent may now be used for something else.
//
// AddressSegmentNegotiator synchronizes between parent/child and searches for a good address to
// reserve from in both processes, then creates a ReservedAddressSegment with the result.
//
// In the event of a later fork/exec (e.g. postmaster dies and is restarted), we try to use the
// formerly negotiated address segment, and FATAL in the parent process if it cannot be used.
// AddressSegmentNegotiator uses addresses that are in practice unlikely to be in use in a new
// process, so it is likely that the old address segment can be reused.
class AddressSegmentNegotiator {
 public:
  static constexpr size_t kDefaultRegionSize = 256_GB;

  explicit AddressSegmentNegotiator(size_t region_size = kDefaultRegionSize);

  ~AddressSegmentNegotiator();

  // File descriptor for the shared state used by the negotiator; this must be passed through
  // a different channel to the child since we lose access to this instance on exec().
  int GetFd() const;

  // This should be called on the parent before forking.
  // `old_address_segment` should be nullptr on the initial fork()/exec(), and set to the existing
  // address segment on later fork()/exec()s.
  Status PrepareNegotiation(ReservedAddressSegment* old_address_segment = nullptr);

  // This should be called on the parent after forking. The negotiator can be discarded after this
  // returns. If `old_address_segment` was passed in to `PrepareNegotiation`, it will be returned.
  Result<ReservedAddressSegment> NegotiateParent();

  // This should be called on the child with the fd from GetFd().
  static Result<ReservedAddressSegment> NegotiateChild(int fd);

  // Give up on negotiation. This is used to free the negotiator when the child has died, to avoid
  // waiting forever.
  Status Shutdown();

  static Result<ReservedAddressSegment> ReserveWithoutNegotiation(
      size_t region_size = kDefaultRegionSize);

 private:
  class Impl;

  friend class ReservedAddressSegment; // To be able to declare Impl as friend.

  std::unique_ptr<Impl> impl_;
};

// Manages a region of reserved addresses. This does not actually allocate memory for the region
// (unless AllocateAnonymous is used), just reserves the addresses so that we can lay out mmap calls
// in memory.
//
// This is intended for use with interprocess communication, so that child and parent processes
// may refer to objects within the address segment with normal pointers. It is important that
// child and parent processes Reserve/AllocateAnonymous in the exact same order.
//
// This class is not thread-safe.
class ReservedAddressSegment {
 public:
  ReservedAddressSegment() = default;
  ReservedAddressSegment(ReservedAddressSegment&& other);
  ReservedAddressSegment(const ReservedAddressSegment& other) = delete;
  ~ReservedAddressSegment();

  ReservedAddressSegment& operator=(ReservedAddressSegment&& other);
  ReservedAddressSegment& operator=(const ReservedAddressSegment&) = delete;

  void Destroy();

  bool Active() const { return base_addr_ != nullptr; }
  void* BaseAddr() const { return reinterpret_cast<void*>(base_addr_); }
  size_t RegionSize() const { return region_size_; }

  static size_t PageSize();
  static size_t MinAlignment();

  // Reserve `size` bytes of addresses from the address segment. No memory is allocated.
  Result<void*> Reserve(size_t size);

  // Allocate `size` bytes of read/write private/anonymous memory in the address segment.
  // `addr` if not null must be at a multiple of a page and in a segment of the address space
  // returned by `Reserve` and will be the address we allocate at.
  Result<void*> AllocateAnonymous(size_t size, void* addr = nullptr);

  template<typename T, typename... Args>
  Result<AnonymousMemoryPtr<T>> MakeAnonymous(void* addr, Args&&... args) {
    void* ptr = VERIFY_RESULT(AllocateAnonymous(sizeof(T), addr));
    return AnonymousMemoryPtr<T>{new (ptr) T (std::forward<Args>(args)...)};
  }

 private:
  friend class AddressSegmentNegotiator::Impl;
  ReservedAddressSegment(void* base_addr, size_t region_size);

  std::byte* base_addr_ = nullptr;
  std::byte* next_addr_ = nullptr;
  size_t region_size_ = 0;
};

} // namespace yb
