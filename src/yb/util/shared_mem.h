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

#include <sys/mman.h>

#include "yb/util/logging.h"

#include "yb/util/result.h"

namespace yb {

class SharedMemorySegment {
 public:
  // Represents a mode of access to shared memory.
  enum AccessMode {
    kReadOnly = PROT_READ,
    kReadWrite = PROT_READ | PROT_WRITE,
  };

  // Creates a new anonymous shared memory segment with the given size.
  static Result<SharedMemorySegment> Create(size_t segment_size);

  // Opens an existing shared memory segment pointed to by a file descriptor.
  static Result<SharedMemorySegment> Open(
      int fd,
      AccessMode access_mode,
      size_t segment_size);

  SharedMemorySegment(SharedMemorySegment&& other);

  SharedMemorySegment(const SharedMemorySegment& other) = delete;

  ~SharedMemorySegment();

  // Returns the address of the start of the shared memory segment.
  void* GetAddress() const;

  // Returns the file descriptor of the shared memory segment.
  int GetFd() const;

 private:
  SharedMemorySegment(void* base_address, int fd, size_t segment_size);

  // The address of the start of the shared memory segment.
  void* base_address_;

  // The file descriptor of the shared memory segment.
  int fd_;

  // The size, in bytes, of the shared memory segment.
  size_t segment_size_;
};

// Utility wrapper for sharing object of specified type.
template <class Object>
class SharedMemoryObject {
 public:
  SharedMemoryObject(SharedMemoryObject&& rhs)
      : segment_(std::move(rhs.segment_)), owned_(rhs.owned_) {
    rhs.owned_ = false;
  }

  ~SharedMemoryObject() {
    if (owned_) {
      get()->~Object();
    }
  }

  // See SharedMemorySegment::GetFd
  int GetFd() const {
    return segment_.GetFd();
  }

  Object* get() const {
    return static_cast<Object*>(segment_.GetAddress());
  }

  Object* operator->() const {
    return get();
  }

  Object& operator*() const {
    return *get();
  }

  template <class... Args>
  static Result<SharedMemoryObject> Create(Args&&... args) {
    return SharedMemoryObject(
       VERIFY_RESULT(SharedMemorySegment::Create(sizeof(Object))),
       std::forward<Args>(args)...);
  }

  static Result<SharedMemoryObject> OpenReadOnly(int fd) {
    return SharedMemoryObject(VERIFY_RESULT(SharedMemorySegment::Open(
        fd, SharedMemorySegment::AccessMode::kReadOnly, sizeof(Object))), NotOwnedTag());
  }

  static Result<SharedMemoryObject> OpenReadWrite(int fd) {
    return SharedMemoryObject(VERIFY_RESULT(SharedMemorySegment::Open(
        fd, SharedMemorySegment::AccessMode::kReadWrite, sizeof(Object))), NotOwnedTag());
  }

 private:
  template <class... Args>
  explicit SharedMemoryObject(SharedMemorySegment&& segment, Args&&... args)
      : segment_(std::move(segment)), owned_(true) {
    new (DCHECK_NOTNULL(segment_.GetAddress())) Object(std::forward<Args>(args)...);
  }

  class NotOwnedTag {};

  explicit SharedMemoryObject(SharedMemorySegment&& segment, NotOwnedTag tag)
      : segment_(std::move(segment)), owned_(false) {
  }

  SharedMemorySegment segment_;
  bool owned_;
};

}  // namespace yb
