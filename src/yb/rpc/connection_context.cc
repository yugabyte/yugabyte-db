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

#include "yb/rpc/connection_context.h"

#include "yb/util/mem_tracker.h"

namespace yb {
namespace rpc {

void ConnectionContextFactory::SetParentMemTracker(
    const std::shared_ptr<MemTracker>& parent_mem_tracker) {
  read_buffer_tracker_ = MemTracker::FindOrCreateTracker("Read Buffer", parent_mem_tracker);
  call_tracker_ = MemTracker::FindOrCreateTracker("Call", parent_mem_tracker);
}

} // namespace rpc
} // namespace yb
