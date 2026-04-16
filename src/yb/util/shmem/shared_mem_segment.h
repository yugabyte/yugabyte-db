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

#include <string>
#include <memory>

#include "yb/util/status.h"

namespace yb {

class SharedMemSegment {
  class Impl;

 public:
  SharedMemSegment();
  SharedMemSegment(
      const std::string& log_prefix, const std::string& name, bool owner,
      size_t block_size, size_t max_size);
  SharedMemSegment(SharedMemSegment&& other);
  SharedMemSegment(const SharedMemSegment& other) = delete;
  ~SharedMemSegment();

  SharedMemSegment& operator=(SharedMemSegment&& other);
  SharedMemSegment& operator=(const SharedMemSegment& other) = delete;

  void ChangeLogPrefix(const std::string& log_prefix);

  void* BaseAddress() const;

  // Create the shared memory file. Once this has been called, any process may call Init() and
  // use the segment. The shared memory is mapped in the parent process at an arbitrary address
  // until Init() is called.
  Status Prepare();

  Status Init(void* address);

  // Unmap shared memory from the temporary address Prepare() mapped it to.
  Status CleanupPrepareState();

  // Caller must ensure this is only called in one process at a time.
  Status Grow(size_t new_size);

 private:
  std::unique_ptr<Impl> impl_;
};

} // namespace yb
