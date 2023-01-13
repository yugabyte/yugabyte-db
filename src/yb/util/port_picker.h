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

#include <mutex>
#include <memory>

#include "yb/util/env.h"

namespace yb {

// Allows allocating ports that would be safe to start a server on for the lifetime of this object.
class PortPicker {
 public:
  // Allocates a free port and stores a file lock guarding access to that port into an internal
  // array of file locks.
  uint16_t AllocateFreePort();

 private:
  std::vector<std::unique_ptr<FileLock> > free_port_file_locks_;
  std::mutex mutex_;
};

}  // namespace yb
