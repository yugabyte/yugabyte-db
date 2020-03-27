//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_MEMORY_MEMORY_USAGE_TEST_UTIL_H
#define YB_UTIL_MEMORY_MEMORY_USAGE_TEST_UTIL_H

#include "yb/util/mem_tracker.h"

namespace yb {

#if defined(TCMALLOC_ENABLED)

size_t GetCurrentAllocatedBytes() {
  return MemTracker::GetTCMallocProperty("generic.current_allocated_bytes");
}

#endif

struct MemoryUsage {
  size_t allocated_bytes = 0;
  size_t tracked_consumption = 0;
  size_t entities_count = 0;
};

std::string DumpMemoryUsage(const MemoryUsage& memory_usage) {
  std::ostringstream ss;
  ss << "Entities: " << memory_usage.entities_count << std::endl;
  ss << "Memory allocated: " << memory_usage.allocated_bytes
     << ", per entity: " << memory_usage.allocated_bytes / memory_usage.entities_count << std::endl;
  ss << "Tracked memory: " << memory_usage.tracked_consumption
     << ", per entity: " << memory_usage.tracked_consumption / memory_usage.entities_count
     << std::endl;
  return ss.str();
}

// tcmalloc could allocate more memory than requested due to round up.
const auto kHighMemoryAllocationAccuracyLimit = 1.125;

}  // namespace yb

#endif  // YB_UTIL_MEMORY_MEMORY_USAGE_TEST_UTIL_H
