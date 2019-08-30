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

#ifndef YB_UTIL_SHARED_MEM_H
#define YB_UTIL_SHARED_MEM_H

#include <sys/mman.h>

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

}  // namespace yb

#endif // YB_UTIL_SHARED_MEM_H
