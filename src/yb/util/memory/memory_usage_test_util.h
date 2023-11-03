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

#pragma once

#include <string>

namespace yb {

void StartAllocationsTracking();
void StopAllocationsTracking();
size_t GetHeapRequestedBytes();

struct MemoryUsage {
  size_t heap_requested_bytes = 0;
  size_t heap_allocated_bytes = 0;
  size_t tracked_consumption = 0;
  size_t entities_count = 0;
};

std::string DumpMemoryUsage(const MemoryUsage& memory_usage);

// tcmalloc could allocate more memory than requested due to round up.
const auto kMemoryAllocationAccuracyHighLimit = 1.15;

// DynamicMemoryUsage should track at least 95% of heap memory requested to allocate.
const auto kDynamicMemoryUsageAccuracyLowLimit = 0.95;

}  // namespace yb
