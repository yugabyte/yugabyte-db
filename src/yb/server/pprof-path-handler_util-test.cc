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

#include <memory>
#include <string>
#include <thread>

#include <gflags/gflags_declare.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/gutil/dynamic_annotations.h"
#include "yb/server/pprof-path-handlers_util.h"

#include "yb/util/flags.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

DECLARE_int32(v);
DECLARE_string(vmodule);

namespace yb {

#if YB_TCMALLOC_ENABLED

class SamplingProfilerTest : public YBTest {};

std::unique_ptr<char[]> AllocArrayOfSize(int64_t alloc_size) {
  std::unique_ptr<char[]> alloc(new char[alloc_size]);
  // Clang in release mode can optimize out the above allocation unless
  // we do something with the pointer... so we just log it.
  VLOG(8) << static_cast<void*>(alloc.get());
  return alloc;
}

int GetNumAllocsOfSizeAtLeast(int64_t size, const std::vector<Sample>& stacks) {
  int val = 0;
  for (const auto& stack : stacks) {
    if (stack.second.bytes >= size) {
      ++val;
    }
  }
  return val;
}

TEST_F(SamplingProfilerTest, HeapSnapshot) {
  // Gperftools TCMalloc's bytes_until_sample is set upon initialization to approximately 16 MB.
  // Setting the sampling rate below only affects the samples after the first bytes_until_sample
  // bytes are allocated.
  MallocExtension::instance()->SetProfileSamplingRate(1);
  // 220 MB will be sampled with probablility > 99.9999%.
  const int64_t alloc_size = 220_MB;
  {
    // Make a large allocation. We expect to find it in the heap snapshot.
    std::unique_ptr<char[]> big_alloc = AllocArrayOfSize(alloc_size);

    auto stacks = GetAggregateAndSortHeapSnapshot(SampleOrder::kCount);
    ASSERT_EQ(GetNumAllocsOfSizeAtLeast(alloc_size, stacks), 1);
  }
  // After the deallocation, the stack should no longer be found in the heap snapshot.
  auto stacks = GetAggregateAndSortHeapSnapshot(SampleOrder::kCount);
  ASSERT_EQ(GetNumAllocsOfSizeAtLeast(alloc_size, stacks), 0);
}

#endif // YB_TCMALLOC_ENABLED

} // namespace yb
