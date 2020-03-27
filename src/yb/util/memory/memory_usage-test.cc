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
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

DEFINE_bool(print_memory_usage, false, "Print real memory usage instead of assert.");

size_t last_allocated_bytes = 0;

#if !defined(ADDRESS_SANITIZER) && !defined(THREAD_SANITIZER)

void* operator new(std::size_t n) {
  last_allocated_bytes = n;
  return malloc(n);
}

#endif

namespace yb {

class MemoryUsageTest : public YBTest {};

#if !defined(ADDRESS_SANITIZER) && !defined(THREAD_SANITIZER)
TEST_F(MemoryUsageTest, String) {
#else
TEST_F(MemoryUsageTest, DISABLED_String) {
#endif
  size_t prev_allocated_bytes = 0;
  size_t length_start = 0;
  for (auto length = 0; length <= 1_KB; ++length) {
    last_allocated_bytes = 0;
    std::string result(length, 'X');
    const auto allocated_bytes = last_allocated_bytes;
    const auto memory_usage_estimate = DynamicMemoryUsageOf(result);
    if (FLAGS_print_memory_usage) {
      if (allocated_bytes != prev_allocated_bytes) {
        LOG(INFO) << "std::string length: " << length_start << "-" << length - 1
                  << " (delta: " << length - length_start << ")"
                  << ", bytes allocated: " << prev_allocated_bytes;
        length_start = length;
        prev_allocated_bytes = allocated_bytes;
      }
    } else {
      ASSERT_EQ(memory_usage_estimate, allocated_bytes)
          << "std::string length: " << length << ", bytes allocated: " << allocated_bytes
          << ", yb::DynamicMemoryUsage: " << memory_usage_estimate;
    }
  }
}

}  // namespace yb

#endif  // YB_UTIL_MEMORY_MEMORY_USAGE_TEST_H
