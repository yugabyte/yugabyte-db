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

#include "yb/util/byte_buffer.h"

#include "yb/util/logging.h"

namespace yb::byte_buffer::internal {

[[noreturn]] void FatalAllocationFailed(size_t capacity, size_t old_capacity) {
  LOG(FATAL) << "Failed to allocate ByteBuffer of capacity " << capacity
             << ", old capacity: " << old_capacity;
  abort();  // LOG(FATAL) is not marked [[noreturn]].
}

} // namespace yb::byte_buffer::internal
