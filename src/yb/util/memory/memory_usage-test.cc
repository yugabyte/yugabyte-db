//
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
//

#ifndef YB_UTIL_MEMORY_MEMORY_USAGE_TEST_H
#define YB_UTIL_MEMORY_MEMORY_USAGE_TEST_H

#include "yb/util/memory/memory_usage.h"
#include "yb/util/memory/memory_usage_test_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_bool(print_memory_usage, false, "Print real memory usage instead of assert.");

namespace yb {

class MemoryUsageTest : public YBTest {};

#if !defined(ADDRESS_SANITIZER) && !defined(THREAD_SANITIZER) && YB_GPERFTOOLS_TCMALLOC
TEST_F(MemoryUsageTest, String) {
#else
TEST_F(MemoryUsageTest, DISABLED_String) {
#endif
  LOG(INFO) << "sizeof(std::string): " << sizeof(std::string);
  for (auto additional_capacity : {false, true}) {
    size_t prev_allocated_bytes = 0;
    size_t length_start = 0;
    size_t capacity_min = std::numeric_limits<size_t>::max();
    size_t capacity_max = std::numeric_limits<size_t>::min();
    for (size_t length = 0; length <= 1_KB; ++length) {
      StartAllocationsTracking();
      std::string result(length, 'X');
      if (additional_capacity) {
        result.reserve(length + 100);
      }
      StopAllocationsTracking();
      const auto allocated_bytes = GetHeapRequestedBytes();
      const auto memory_usage_estimate = DynamicMemoryUsageOf(result);
      const auto capacity = result.capacity();
      if (FLAGS_print_memory_usage) {
        if (allocated_bytes != prev_allocated_bytes) {
          LOG(INFO) << "std::string length: " << length_start << "-" << length - 1
                    << " (range width: " << length - length_start << ")"
                    << ", capacity: " << capacity_min << "-" << capacity_max
                    << " (range width: " << capacity_max - capacity_min + 1 << ")"
                    << ", bytes allocated: " << prev_allocated_bytes
                    << ", allocated - length_start: " << prev_allocated_bytes - length_start
                    << ", allocated - capacity_min: " << prev_allocated_bytes - capacity_min
                    << ", memory_usage_estimate: " << memory_usage_estimate;
          length_start = length;
          prev_allocated_bytes = allocated_bytes;
          capacity_min = std::numeric_limits<size_t>::max();
          capacity_max = std::numeric_limits<size_t>::min();
        }
        capacity_min = std::min(capacity_min, capacity);
        capacity_max = std::max(capacity_max, capacity);
      } else {
        ASSERT_EQ(memory_usage_estimate, allocated_bytes)
            << "std::string length: " << length << ", capacity: " << capacity
            << ", bytes allocated: " << allocated_bytes
            << ", yb::DynamicMemoryUsage: " << memory_usage_estimate;
      }
    }
  }
}

}  // namespace yb

#endif  // YB_UTIL_MEMORY_MEMORY_USAGE_TEST_H
