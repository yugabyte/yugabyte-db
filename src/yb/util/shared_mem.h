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

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

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

  SharedMemorySegment() = default;

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

  SharedMemorySegment& operator=(SharedMemorySegment&& other);

  SharedMemorySegment& operator=(const SharedMemorySegment& other) = delete;

  // Returns the address of the start of the shared memory segment.
  void* GetAddress() const;

  // Returns the file descriptor of the shared memory segment.
  int GetFd() const;

 private:
  SharedMemorySegment(void* base_address, int fd, size_t segment_size);

  // The address of the start of the shared memory segment.
  void* base_address_ = nullptr;

  // The file descriptor of the shared memory segment.
  int fd_ = -1;

  // The size, in bytes, of the shared memory segment.
  size_t segment_size_ = 0;
};

// Utility wrapper for sharing object of specified type.
template <class Object>
class SharedMemoryObject {
 public:
  SharedMemoryObject() = default;

  SharedMemoryObject(SharedMemoryObject&& rhs)
      : segment_(std::move(rhs.segment_)), owned_(std::exchange(rhs.owned_, false)) { }

  ~SharedMemoryObject() {
    Reset();
  }

  SharedMemoryObject& operator=(SharedMemoryObject&& rhs) {
    Reset();
    segment_ = std::move(rhs.segment_);
    owned_ = std::exchange(rhs.owned_, false);
    return *this;
  }

  void Reset() {
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
  bool owned_ = false;
};

class InterprocessSharedMemoryObject;

class InterprocessMappedRegion {
 public:
  InterprocessMappedRegion() = default;

  size_t get_size() const noexcept {
    return impl_.get_size();
  }

  void* get_address() const noexcept {
    return impl_.get_address();
  }

 private:
  explicit InterprocessMappedRegion(boost::interprocess::mapped_region&& impl)
      : impl_(std::move(impl)) {}

  friend class InterprocessSharedMemoryObject;

  boost::interprocess::mapped_region impl_;
};

class InterprocessSharedMemoryObject {
 public:
  InterprocessSharedMemoryObject() = default;

  static Result<InterprocessSharedMemoryObject> Create(const std::string& name, size_t size);
  static Result<InterprocessSharedMemoryObject> Open(const std::string& name);

  Result<InterprocessMappedRegion> Map() const;

  explicit operator bool() const noexcept;

  void DestroyAndRemove();

 private:
  explicit InterprocessSharedMemoryObject(boost::interprocess::shared_memory_object&& object)
      : impl_(std::move(object)) {}

  boost::interprocess::shared_memory_object impl_;
};

}  // namespace yb
