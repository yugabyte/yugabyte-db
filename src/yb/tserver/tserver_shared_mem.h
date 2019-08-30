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

#ifndef YB_TSERVER_TSERVER_SHARED_MEM_H
#define YB_TSERVER_TSERVER_SHARED_MEM_H

#include <atomic>

#include "yb/util/shared_mem.h"

namespace yb {
namespace tserver {

class TServerSharedMemory {
 public:
  // Creates a new anonymous shared memory segment.
  TServerSharedMemory();

  // Maps an existing shared memory segment pointed at by fd.
  TServerSharedMemory(int fd, SharedMemorySegment::AccessMode access_mode);

  TServerSharedMemory(TServerSharedMemory&& other);

  TServerSharedMemory(const TServerSharedMemory& other) = delete;

  ~TServerSharedMemory();

  // Returns the file descriptor of the shared memory segment.
  int GetFd() const;

  // Atomically set the ysql shared catalog version.
  void SetYSQLCatalogVersion(uint64_t version);

  // Atomically load the ysql catalog version.
  uint64_t GetYSQLCatalogVersion() const;

 private:
  struct Data {
    std::atomic<uint64_t> catalog_version{0};
  };

  // Size, in bytes, to allocate towards the shared memory segment of the tserver.
  static constexpr std::size_t kSegmentSize = sizeof(TServerSharedMemory::Data);

  SharedMemorySegment segment_;

  Data* data_;
};

}  // namespace tserver
}  // namespace yb

#endif // YB_TSERVER_TSERVER_SHARED_MEM_H
