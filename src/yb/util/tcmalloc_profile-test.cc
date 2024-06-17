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

#include "yb/util/tcmalloc_profile-test.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gflags/gflags_declare.h>
#include <gtest/gtest.h>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/util/tcmalloc_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_int32(dump_heap_snapshot_min_interval_sec);
DECLARE_int32(v);
DECLARE_string(vmodule);

using std::literals::chrono_literals::operator""s;
using std::vector;

namespace yb {

#if YB_TCMALLOC_ENABLED

void SamplingProfilerTest::SetUp() {
  YBTest::SetUp();
  google::SetVLOGLevel("pprof-path-handlers_util", 2);
}

// Changes to tcmalloc's sample rate only take effect once we take a sample. Upon initialization,
// the rate is set to 0 for gperftools tcmalloc (but it actually samples every 16 MiB in that case),
// and 2 MiB for Google tcmalloc.
// So,allocate enough data after changing the sample rate to cause the new sampling rate to take
// effect (with high probability).
void SamplingProfilerTest::SetProfileSamplingRate(int64_t sample_freq_bytes) {
  int64_t old_rate;
#if YB_GPERFTOOLS_TCMALLOC
  old_rate = MallocExtension::instance()->GetProfileSamplingRate();
  if (old_rate == 0) {
    old_rate = 16_MB;
  }
#else
  old_rate = tcmalloc::MallocExtension::GetProfileSamplingRate();
#endif
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

  auto samples = ASSERT_RESULT(
      GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount, HeapSnapshotType::kCurrentHeap));
  ASSERT_EQ(GetTestAllocs(samples).size(), 0);
}

#if YB_GPERFTOOLS_TCMALLOC
// Basic test that heap snapshot has data.
TEST_F(SamplingProfilerTest, HeapSnapshot) {
  SetProfileSamplingRate(1);
  const int64_t kAllocSize = 1_MB;
  {
    // Make a large allocation. We expect to find it in the heap snapshot.
    std::unique_ptr<char[]> big_alloc = TestAllocArrayOfSize(kAllocSize);

    auto samples = ASSERT_RESULT(GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount));
    ASSERT_EQ(GetTestAllocs(samples).size(), 1);
  }
  // After the deallocation, the stack should no longer be found in the heap snapshot.
  auto samples = ASSERT_RESULT(GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount));
  ASSERT_EQ(GetTestAllocs(samples).size(), 0);
}
#endif

#if YB_GOOGLE_TCMALLOC

// Basic test for pprof/heap_snapshot.
TEST_F(SamplingProfilerTest, HeapSnapshot) {
  SetProfileSamplingRate(1);
  // To ensure we hit a new memory peak, this allocation has to be larger than the allocation we
  // used in SetProfileSamplingRate.
  const int64_t kAllocSize = 100_MB;
  {
    // Make a large allocation. We expect to find it in the current and peak heap snapshots.
    std::unique_ptr<char[]> big_alloc = TestAllocArrayOfSize(kAllocSize);

    auto samples = ASSERT_RESULT(GetAggregateAndSortHeapSnapshot(
        SampleOrder::kSampledCount, HeapSnapshotType::kCurrentHeap));
    ASSERT_EQ(GetTestAllocs(samples).size(), 1);

    samples = ASSERT_RESULT(
        GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount, HeapSnapshotType::kPeakHeap));
    ASSERT_EQ(GetTestAllocs(samples).size(), 1);
  }
  // After the deallocation, the stack should no longer be found in the current heap snapshot,
  // but should be in the peak heap snapshot.
  auto samples = ASSERT_RESULT(
      GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount, HeapSnapshotType::kCurrentHeap));
  ASSERT_EQ(GetTestAllocs(samples).size(), 0);

  samples = ASSERT_RESULT(
      GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount, HeapSnapshotType::kPeakHeap));
  ASSERT_EQ(GetTestAllocs(samples).size(), 1);
}

// Basic test for pprof/heap.
TEST_F(SamplingProfilerTest, HeapProfile) {
  SetProfileSamplingRate(1);
  const int64_t kAllocSizeExcluded = 1_MB;
  const int64_t kAllocSizeAllocated = 2_MB;
  const int64_t kAllocSizeDeallocated = 5_MB;

  tcmalloc::MallocExtension::AllocationProfilingToken token;

  // We do not expect to find this allocation in the profile since we have not started profiling.
  std::unique_ptr<char[]> big_alloc1 = TestAllocArrayOfSize(kAllocSizeExcluded);

  token = tcmalloc::MallocExtension::StartLifetimeProfiling(/* seed_with_live_allocs= */ false);

  // We expect to find this allocation in the profile since it is not deallocated before we stop
  // profiling.
  std::unique_ptr<char[]> big_alloc2 = TestAllocArrayOfSize(kAllocSizeAllocated);

  // We expect to always find this allocation in the profile if and only if only_growth is false,
  // since it is deallocated before we stop profiling.
  std::unique_ptr<char[]> big_alloc3 = TestAllocArrayOfSize(kAllocSizeDeallocated);
  big_alloc3.reset();

  auto profile = std::move(token).Stop();

  // The stack for the allocations is the same so they are aggregated into one.
  auto samples = AggregateAndSortProfile(
      profile, SampleFilter::kAllSamples, SampleOrder::kSampledBytes);
  ASSERT_EQ(GetTestAllocs(samples).size(), 1);
  ASSERT_EQ(samples[0].second.sampled_allocated_bytes, kAllocSizeAllocated + kAllocSizeDeallocated);

  // We only expect to find the non-deallocated allocation here.
  samples = AggregateAndSortProfile(profile, SampleFilter::kGrowthOnly, SampleOrder::kSampledBytes);
  ASSERT_EQ(GetTestAllocs(samples).size(), 1);
  ASSERT_EQ(samples[0].second.sampled_allocated_bytes, kAllocSizeAllocated);
}

TEST_F(SamplingProfilerTest, OnlyOneHeapProfile) {
  auto StartHeapProfileWithFrequency = [](int64_t sample_freq_bytes, Status* status) {
    Result<tcmalloc::Profile> profile = GetHeapProfile(3 /* seconds */, sample_freq_bytes);
    *status = profile.ok() ? Status::OK() : profile.status();
  };

  SetProfileSamplingRate(1);

  yb::ThreadPtr thread1, thread2;
  Status status1, status2;
  ASSERT_OK(yb::Thread::Create(
      CURRENT_TEST_NAME(), "heap profile",
      std::bind(StartHeapProfileWithFrequency, 2, &status1), &thread1));
  SleepFor(1s);
  ASSERT_OK(yb::Thread::Create(
      CURRENT_TEST_NAME(), "heap profile",
      std::bind(StartHeapProfileWithFrequency, 3, &status2), &thread2));
  thread1->Join();
  ASSERT_OK(status1);
  thread2->Join();
  ASSERT_NOK(status2);
  ASSERT_STR_CONTAINS(status2.message().ToBuffer(), "A heap profile is already running");

  ASSERT_EQ(GetTCMallocSamplingFrequency(), 1);
}

// Verify that the estimated bytes and count are close to their actual values.
TEST_F(SamplingProfilerTest, EstimatedBytesAndCount) {
  const auto kSampleFreqBytes = 10_KB;
  const auto kAllocSize = 10_KB;
  const auto kNumAllocations = 1000;

  SetProfileSamplingRate(kSampleFreqBytes);

  std::vector<std::unique_ptr<char[]>> v;
  for (int i = 0; i < kNumAllocations; ++i) {
    v.push_back(TestAllocArrayOfSize(kAllocSize));
  }

  auto samples = ASSERT_RESULT(
      GetAggregateAndSortHeapSnapshot(SampleOrder::kSampledCount, HeapSnapshotType::kCurrentHeap));

  // Allocations should get aggregated into one sample.
  ASSERT_EQ(GetTestAllocs(samples).size(), 1);

  auto estimated_count = *samples[0].second.estimated_count;
  auto margin = kNumAllocations * 0.2;
  ASSERT_NEAR(kNumAllocations, estimated_count, margin);

  auto estimated_bytes = *samples[0].second.estimated_bytes;
  auto actual_bytes = kAllocSize * kNumAllocations;
  margin = actual_bytes * 0.2;
  ASSERT_NEAR(actual_bytes, estimated_bytes, margin);
}

TEST(ThrottledHeapSnapshotDumperTest, DoNotDumpIfBelowSoftLimit) {
  // Create a fake root tracker that does not use a consumption_functor as its source of truth.
  auto hard_limit = 100000;
  auto root_tracker = std::make_shared<MemTracker>(
      hard_limit, "root", ConsumptionFunctor(), nullptr /* parent */, AddToParent::kFalse,
      CreateMetrics::kFalse, std::string() /* metric_name */, IsRootTracker::kTrue);

  // Should not dump in SoftLimitExceeded because we have not exceeded the soft limit.
  root_tracker->Consume(root_tracker->soft_limit());
  ASSERT_FALSE(root_tracker->AnySoftLimitExceeded(/*score=*/ 1.0).exceeded);

  // Should dump because we did not dump during the SoftLimitExceeded call.
  ASSERT_TRUE(DumpHeapSnapshotUnlessThrottled());
}

TEST(ThrottledHeapSnapshotDumperTest, DumpIfSoftLimitExceeded) {
  // Create a fake root tracker that does not use a consumption_functor as its source of truth.
  auto hard_limit = 100000;
  auto root_tracker = std::make_shared<MemTracker>(
      hard_limit, "root", ConsumptionFunctor(), nullptr /* parent */, AddToParent::kFalse,
      CreateMetrics::kFalse, std::string() /* metric_name */, IsRootTracker::kTrue);

  // Exceed the soft memory limit.
  root_tracker->Consume(root_tracker->soft_limit() + 1);

  // Deterministically choose to reject this request.
  ASSERT_TRUE(root_tracker->AnySoftLimitExceeded(/*score=*/ 1.0).exceeded);

  // The heap snapshot should not dump, because it just did during the SoftLimitExceeded call.
  ASSERT_FALSE(DumpHeapSnapshotUnlessThrottled());
}

TEST(ThrottledHeapSnapshotDumperTest, OnlyDumpOnRootRejection) {
  // Check that only a child tracker hitting its soft memory limit does not cause a dump.
  auto root_hard_limit = 100000;
  auto child_hard_limit = 1000;

  auto root_tracker = std::make_shared<MemTracker>(
      root_hard_limit, "root", ConsumptionFunctor(), nullptr /* parent */, AddToParent::kTrue,
      CreateMetrics::kFalse, std::string() /* metric_name */, IsRootTracker::kTrue);
  auto child_tracker = MemTracker::CreateTracker(
      child_hard_limit, "child", ConsumptionFunctor(), root_tracker);

  // Exceed the soft memory limit.
  child_tracker->Consume(child_tracker->soft_limit() + 1);

  // Deterministically choose to reject this request.
  auto result = child_tracker->AnySoftLimitExceeded(1.0);
  ASSERT_TRUE(result.exceeded);
  ASSERT_STR_CONTAINS(result.tracker_path, "child");

  // The heap snapshot should dump, because it did not dump SoftLimitExceeded call.
  ASSERT_TRUE(DumpHeapSnapshotUnlessThrottled());
}

#endif // YB_GOOGLE_TCMALLOC

#endif // YB_TCMALLOC_ENABLED

} // namespace yb
