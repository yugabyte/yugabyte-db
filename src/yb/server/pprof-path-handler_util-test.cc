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

#include "yb/server/pprof-path-handler_util-test.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gflags/gflags_declare.h>
#include <gtest/gtest.h>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/server/pprof-path-handlers_util.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/util/tcmalloc_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_int32(v);
DECLARE_string(vmodule);

using std::vector;

namespace yb {

#if YB_TCMALLOC_ENABLED

// Changes to tcmalloc's sample rate only take effect once we take a sample. Upon initialization,
// the rate is set to 0 for gperftools tcmalloc (but it actually samples every 16 MiB in that
// case), and 2 MiB for Google tcmalloc.
// So,allocate enough data after changing the sample rate to cause the new sampling rate to take
// effect (with high probability).
void SamplingProfilerTest::SetProfileSamplingRate(int64_t sample_freq_bytes) {
  int64_t old_rate = MallocExtension::instance()->GetProfileSamplingRate();
  if (old_rate == 0) {
    old_rate = 16_MB;
  }
  SetTCMallocSamplingFrequency(sample_freq_bytes);
  // The probability of sampling an allocation of size X with sampling rate Y is 1 - e^(-X/Y).
  // An allocation of size Y * 14 is thus sampled with probability > 99.9999%.
  InternalAllocArrayOfSize(old_rate * 14);
}

[[nodiscard]] std::unique_ptr<char[]> SamplingProfilerTest::TestAllocArrayOfSize(
    int64_t alloc_size) {
  std::unique_ptr<char[]> alloc(new char[alloc_size]);
  // Clang in release mode can optimize out the above allocation unless
  // we do something with the pointer... so we just log it.
  VLOG(8) << static_cast<void*>(alloc.get());
  return alloc;
}

// Duplicate of TestAllocArrayOfSize which will not be found by GetTestAllocs.
// NB: We cannot just call this from TestAllocArrayOfSize since there does not seem to be a
// cross-compiler way to reliably disable inlining (attribute noinline still results in inlining).
[[nodiscard]] std::unique_ptr<char[]> SamplingProfilerTest::InternalAllocArrayOfSize(
    int64_t alloc_size) {
  std::unique_ptr<char[]> alloc(new char[alloc_size]);
  VLOG(8) << static_cast<void*>(alloc.get());
  return alloc;
}

vector<Sample> SamplingProfilerTest::GetTestAllocs(const vector<Sample>& samples) {
  vector<Sample> test_samples;
  for (const auto& sample : samples) {
    if (sample.first.find("TestAllocArrayOfSize") != std::string::npos) {
      test_samples.push_back(sample);
    }
  }
  return test_samples;
}

TEST_F(SamplingProfilerTest, DisableSampling) {
  SetProfileSamplingRate(0);
  auto v = TestAllocArrayOfSize(10_KB);

  auto samples = GetAggregateAndSortHeapSnapshot(SampleOrder::kCount);
  ASSERT_EQ(GetTestAllocs(samples).size(), 0);
}

// Basic test that heap snapshot has data.
TEST_F(SamplingProfilerTest, HeapSnapshot) {
  SetProfileSamplingRate(1);
  const int64_t kAllocSize = 1_MB;
  {
    // Make a large allocation. We expect to find it in the heap snapshot.
    std::unique_ptr<char[]> big_alloc = TestAllocArrayOfSize(kAllocSize);

    auto samples = GetAggregateAndSortHeapSnapshot(SampleOrder::kCount);
    ASSERT_EQ(GetTestAllocs(samples).size(), 1);
  }
  // After the deallocation, the stack should no longer be found in the heap snapshot.
  auto samples = GetAggregateAndSortHeapSnapshot(SampleOrder::kCount);
  ASSERT_EQ(GetTestAllocs(samples).size(), 0);
}

#endif // YB_TCMALLOC_ENABLED

} // namespace yb
