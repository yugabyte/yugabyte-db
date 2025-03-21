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

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/tcmalloc_impl_util.h"

namespace yb {

struct SampleInfo {
  // The sum of the sizes of all sampled allocations for this call stack.
  int64_t sampled_allocated_bytes;
  // The number of sampled allocations for this call stack.
  int64_t sampled_count;

  // The expected value of how many bytes were allocated from this call stack.
  // The sum of this value across all call stacks should be approximately equal to the memory used
  // by the process.
  std::optional<int64_t> estimated_bytes;

  // The expected value of how many times we allocated from this call stack.
  std::optional<int64_t> estimated_count;
};

using SampleStack = std::string;
typedef std::pair<SampleStack, SampleInfo> Sample;

YB_DEFINE_ENUM(SampleOrder, (kSampledCount)(kSampledBytes)(kEstimatedBytes));
YB_DEFINE_ENUM(HeapSnapshotType, (kCurrentHeap)(kPeakHeap));
YB_DEFINE_ENUM(SampleFilter, (kAllSamples)(kGrowthOnly));

Result<std::vector<Sample>> GetAggregateAndSortHeapSnapshot(
    SampleOrder order = SampleOrder::kSampledCount,
    HeapSnapshotType snapshot_type = HeapSnapshotType::kCurrentHeap,
    SampleFilter filter = SampleFilter::kAllSamples,
    const std::string& separator = "\n");

#if YB_GOOGLE_TCMALLOC

Result<tcmalloc::Profile> GetHeapProfile(int seconds, int64_t sample_freq_bytes);

// If peak_heap is set, gets the snapshot of the heap at peak memory usage.
tcmalloc::Profile GetHeapSnapshot(HeapSnapshotType snapshot_type);

std::vector<Sample> AggregateAndSortProfile(
    const tcmalloc::Profile& profile, SampleFilter filter, SampleOrder order,
    const std::string& separator = "\n");

#endif // YB_GOOGLE_TCMALLOC

#if YB_GPERFTOOLS_TCMALLOC

std::vector<Sample> GetAggregateAndSortHeapSnapshotGperftools(
    SampleOrder order, const std::string& separator);

#endif // YB_GPERFTOOLS_TCMALLOC

bool DumpHeapSnapshotUnlessThrottled();

SampleOrder GetTCMallocDefaultSampleOrder();

} // namespace yb
