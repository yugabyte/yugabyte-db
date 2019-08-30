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

#include "yb/tserver/tserver_shared_mem.h"

#include <glog/logging.h>

namespace yb {
namespace tserver {

TServerSharedMemory::TServerSharedMemory()
    : segment_(CHECK_RESULT(SharedMemorySegment::Create(kSegmentSize))),
      data_(new (segment_.GetAddress()) Data) {
  // All atomics stored in shared memory must be lock-free. Non-robust locks
  // in shared memory can lead to deadlock if a processes crashes, and memory
  // access violations if the segment is mapped as read-only.
  // NOTE: this check is NOT sufficient to guarantee that an atomic is safe
  // for shared memory! Some atomics claim to be lock-free but still require
  // read-write access for a `load()`.
  // E.g. for 128 bit objects: https://stackoverflow.com/questions/49816855.
  LOG_IF(FATAL, !data_->catalog_version.is_lock_free())
      << "Shared memory atomics must be lock-free";
}

TServerSharedMemory::TServerSharedMemory(int fd, SharedMemorySegment::AccessMode access_mode)
    : segment_(CHECK_RESULT(SharedMemorySegment::Open(fd, access_mode, kSegmentSize))),
      data_(static_cast<Data*>(segment_.GetAddress())) {
}

TServerSharedMemory::TServerSharedMemory(TServerSharedMemory&& other)
    : segment_(std::move(other.segment_)),
      data_(other.data_) {
  other.data_ = nullptr;
}

TServerSharedMemory::~TServerSharedMemory() {
  data_->~Data();
}

int TServerSharedMemory::GetFd() const {
  return segment_.GetFd();
}

void TServerSharedMemory::SetYSQLCatalogVersion(uint64_t version) {
  data_->catalog_version.store(version, std::memory_order_release);
}

uint64_t TServerSharedMemory::GetYSQLCatalogVersion() const {
  return data_->catalog_version.load(std::memory_order_acquire);
}

}  // namespace tserver
}  // namespace yb
