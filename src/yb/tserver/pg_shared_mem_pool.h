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

#include <cstddef>
#include <memory>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/mem_tracker.h"

namespace yb::tserver {

class SharedMemorySegmentHolder {
 public:
  virtual void Freed(uint64_t id) = 0;
  virtual ~SharedMemorySegmentHolder() = default;
};

class SharedMemorySegmentHandle {
 public:
  SharedMemorySegmentHandle() = default;

  template <class Segment>
  SharedMemorySegmentHandle(
      SharedMemorySegmentHolder& holder, const Segment& segment)
      : holder_(&holder), id_(segment.id()), address_(segment.address()), size_(segment.size()) {}

  SharedMemorySegmentHandle(SharedMemorySegmentHandle&& rhs)
      : holder_(std::exchange(rhs.holder_, nullptr)), id_(rhs.id_), address_(rhs.address_),
        size_(rhs.size_) {
  }

  SharedMemorySegmentHandle& operator=(SharedMemorySegmentHandle&& rhs) {
    if (&rhs == this) {
      return *this;
    }
    Reset();
    holder_ = std::exchange(rhs.holder_, nullptr);
    id_ = rhs.id_;
    address_ = rhs.address_;
    size_ = rhs.size_;
    return *this;
  }

  SharedMemorySegmentHandle(const SharedMemorySegmentHandle&) = delete;
  void operator=(const SharedMemorySegmentHandle&) = delete;

  ~SharedMemorySegmentHandle() {
    Reset();
  }

  explicit operator bool() const {
    return holder_ != nullptr;
  }

  size_t id() const {
    return id_;
  }

  std::byte* address() const {
    return address_;
  }

  size_t size() const {
    return size_;
  }

  void TruncateLeft(size_t amount) {
    address_ += amount;
    size_ -= amount;
  }

 private:
  void Reset();

  SharedMemorySegmentHolder* holder_ = nullptr;
  size_t id_ = 0;
  std::byte* address_ = nullptr;
  size_t size_;
};

class PgSharedMemoryPool {
 public:
  static const std::string kAllocatedMemTrackerId;
  static const std::string kAvailableMemTrackerId;

  PgSharedMemoryPool(const MemTrackerPtr& parent_mem_tracker, const std::string& instance_id);
  ~PgSharedMemoryPool();

  SharedMemorySegmentHandle Obtain(size_t size);

  void Start(rpc::Scheduler& scheduler);

  void Freed(uint64_t id);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
