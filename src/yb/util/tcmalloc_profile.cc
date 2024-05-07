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

#include "yb/util/tcmalloc_profile.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#if YB_ABSL_ENABLED
#include "absl/debugging/symbolize.h"
#endif

#include "yb/gutil/casts.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/monotime.h"
#include "yb/util/symbolize.h"

DEFINE_RUNTIME_int32(dump_heap_snapshot_min_interval_sec, 600,
    "The minimum time to wait between dumping heap snapshots. A value of <= 0 means the logging is "
    "disabled.");
DEFINE_RUNTIME_int32(dump_heap_snapshot_max_call_stacks, 10,
    "The maximum number of call stacks to log from TCMalloc's heap snapshot when TryDumpSnapshot "
    " is called.");

namespace yb {

#if YB_TCMALLOC_ENABLED
namespace {
void SortSamplesByOrder(std::vector<Sample>* samples, SampleOrder order) {
  std::sort(
      samples->begin(),
      samples->end(),
      [order](const Sample& a, const Sample& b) {
        if (order == SampleOrder::kSampledBytes) {
          return a.second.sampled_allocated_bytes > b.second.sampled_allocated_bytes;
        } else if (order == SampleOrder::kEstimatedBytes &&
                   a.second.estimated_bytes &&
                   b.second.estimated_bytes) {
          return *a.second.estimated_bytes > *b.second.estimated_bytes;
        }
        return a.second.sampled_count > b.second.sampled_count;
      });
}
} // namespace
#endif // YB_TCMALLOC_ENABLED

#if YB_GPERFTOOLS_TCMALLOC || YB_GOOGLE_TCMALLOC
namespace {
bool Symbolize(void *pc, char *out, int out_size) {
#if YB_ABSL_ENABLED
  return absl::Symbolize(pc, out, out_size);
#else
  return GlogSymbolize(pc, out, out_size);
#endif
}
}  // namespace
#endif

Result<std::vector<Sample>> GetAggregateAndSortHeapSnapshot(
    SampleOrder order, HeapSnapshotType snapshot_type, SampleFilter filter) {
#if YB_GPERFTOOLS_TCMALLOC
  switch (order) {
    case SampleOrder::kSampledBytes:
    case SampleOrder::kSampledCount:
      break;
    case SampleOrder::kEstimatedBytes:
      return STATUS(
          NotSupported, Format("Invalid sample order $0 used with gperftools tcmalloc", order));
  }
  switch (snapshot_type) {
    case HeapSnapshotType::kPeakHeap:
      return STATUS(NotSupported, "peak_heap is not supported with gperftools tcmalloc");
    case HeapSnapshotType::kCurrentHeap:
      break;
  }
  switch (filter) {
    case SampleFilter::kGrowthOnly:
      return STATUS(NotSupported, "growth-only snapshot is not supported with gperftools tcmalloc");
    case SampleFilter::kAllSamples:
      break;
  }
  return GetAggregateAndSortHeapSnapshotGperftools(order);
#elif YB_GOOGLE_TCMALLOC
  auto current_profile = GetHeapSnapshot(snapshot_type);
  return AggregateAndSortProfile(current_profile, filter, order);
#else
  return STATUS(NotSupported, "Heap snapshot is only available if tcmalloc is enabled.");
#endif
}

#if YB_GOOGLE_TCMALLOC
Result<tcmalloc::Profile> GetHeapProfile(int seconds, int64_t sample_freq_bytes) {
  static std::atomic<bool> heap_profile_running{false};
  bool expected = false;
  if (!heap_profile_running.compare_exchange_strong(expected, /* desired= */ true)) {
    return STATUS_FORMAT(IllegalState, "A heap profile is already running.");
  }

  auto prev_sample_rate = tcmalloc::MallocExtension::GetProfileSamplingRate();
  tcmalloc::MallocExtension::SetProfileSamplingRate(sample_freq_bytes);
  auto token =
      tcmalloc::MallocExtension::StartLifetimeProfiling(/* seed_with_live_allocs= */ false);

  LOG(INFO) << Format("Sleeping for $0 seconds while profile is collected.", seconds);
  SleepFor(MonoDelta::FromSeconds(seconds));
  tcmalloc::MallocExtension::SetProfileSamplingRate(prev_sample_rate);
  auto profile = std::move(token).Stop();
  heap_profile_running = false;
  return profile;
}

tcmalloc::Profile GetHeapSnapshot(HeapSnapshotType snapshot_type) {
  switch (snapshot_type) {
    case HeapSnapshotType::kPeakHeap:
      return tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kPeakHeap);
    case HeapSnapshotType::kCurrentHeap:
      return tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
    default:
      LOG_WITH_FUNC(DFATAL) << "Invalid snapshot type " << snapshot_type;
      return tcmalloc::Profile();
  }
}

std::vector<Sample> AggregateAndSortProfile(
    const tcmalloc::Profile& profile, SampleFilter filter, SampleOrder order) {
  LOG(INFO) << "Analyzing TCMalloc sampling profile";
  int failed_symbolizations = 0;
  std::unordered_map<std::string, SampleInfo> samples_map;

  profile.Iterate([&](const tcmalloc::Profile::Sample& sample) {
    // Deallocation samples are the same as the allocation samples, except with a negative
    // sample.count < 0 and the deallocation stack. Skip since we are not currently interested in
    // printing the deallocation stack.
    if (sample.count <= 0) {
      return;
    }

    // If we only want growth, exclude samples for which we saw a deallocation event.
    // "Censored" means we observed an allocation but not a deallocation. (Deallocation-only events
    // are not reported).
    if (filter == SampleFilter::kGrowthOnly && !sample.is_censored) {
      return;
    }

    std::stringstream sstream;
    // 256 is arbitrary. Symbolize will return false if the symbol is longer than that.
    char buf[256];
    for (int64_t i = 0; i < sample.depth; ++i) {
      if (Symbolize(sample.stack[i], buf, sizeof(buf))) {
        sstream << buf << std::endl;
      } else {
        ++failed_symbolizations;
        sstream << "Failed to symbolize" << std::endl;
      }
    }
    std::string stack = sstream.str();

    // Update the corresponding call stack entry with this sample's information.
    auto& entry = samples_map[stack];
    entry.sampled_allocated_bytes += sample.allocated_size;
    ++entry.sampled_count;
    entry.estimated_bytes = entry.estimated_bytes.value_or(0) + sample.sum;
    entry.estimated_count = entry.estimated_count.value_or(0) + sample.count;

    VLOG(1) << "Sampled stack: " << stack
            << ", sum: " << sample.sum
            << ", count: " << sample.count
            << ", requested_size: " << sample.requested_size
            << ", allocated_size: " << sample.allocated_size
            << ", is_censored: " << sample.is_censored
            << ", avg_lifetime: " << sample.avg_lifetime
            << ", allocator_deallocator_cpu_matched: "
            << sample.allocator_deallocator_cpu_matched.value_or("N/A");
  });
  if (failed_symbolizations > 0) {
    LOG(WARNING) << Format("Failed to symbolize $0 symbols", failed_symbolizations);
  }

  std::vector<Sample> samples_vec;
  samples_vec.reserve(samples_map.size());
  for (auto& entry : samples_map) {
    samples_vec.push_back(std::move(entry));
  }
  SortSamplesByOrder(&samples_vec, order);
  return samples_vec;
}

#endif // YB_GOOGLE_TCMALLOC

#if YB_GPERFTOOLS_TCMALLOC
namespace {
// From gperftools/src/malloc_extension.cc.
uintptr_t GetSampleCount(void** entry) {
  return reinterpret_cast<uintptr_t>(entry[0]);
}
uintptr_t GetSampleSize(void** entry) {
  return reinterpret_cast<uintptr_t>(entry[1]);
}
uintptr_t GetSampleDepth(void** entry) {
  return reinterpret_cast<uintptr_t>(entry[2]);
}
void* GetSampleProgramCounter(void** entry, uintptr_t i) {
  return entry[3 + i];
}
} // namespace

// Do not call this directly, instead use GetAggregateAndSortHeapSnapshot.
// Assumes that the supplied sample order is valid for gperftools tcmalloc.
std::vector<Sample> GetAggregateAndSortHeapSnapshotGperftools(SampleOrder order) {
  int sample_period;
  void** samples = MallocExtension::instance()->ReadStackTraces(&sample_period);

  int failed_symbolizations = 0;
  std::unordered_map<std::string, SampleInfo> samples_map;

  // Samples are stored in a flattened array, where each sample is
  // [count, size, depth, stackframe 0, stackframe 1,...].
  // The end of the array is marked by a count of 0.
  for (void** sample = samples; GetSampleCount(sample) != 0; sample += 3 + GetSampleDepth(sample)) {
    std::stringstream sstream;
    // 256 is arbitrary. Symbolize will return false if the symbol is longer than that.
    char buf[256];
    for (uintptr_t i = 0; i < GetSampleDepth(sample); ++i) {
      if (Symbolize(GetSampleProgramCounter(sample, i), buf, sizeof(buf))) {
        sstream << buf << std::endl;
      } else {
        ++failed_symbolizations;
        sstream << "Failed to symbolize" << std::endl;
      }
    }
    auto stack = sstream.str();

    auto& entry = samples_map[stack];
    entry.sampled_allocated_bytes += GetSampleSize(sample);
    entry.sampled_count += GetSampleCount(sample);

    VLOG(1) << "Sampled stack: " << stack
            << ", size: " << GetSampleSize(sample)
            << ", count: " << GetSampleCount(sample);
  }
  if (failed_symbolizations > 0) {
    LOG(WARNING) << Format("Failed to symbolize $0 symbols", failed_symbolizations);
  }

  std::vector<Sample> samples_vec;
  samples_vec.reserve(samples_map.size());
  for (auto& entry : samples_map) {
    samples_vec.push_back(std::move(entry));
  }
  SortSamplesByOrder(&samples_vec, order);
  delete[] samples;
  return samples_vec;
}

#endif // YB_GPERFTOOLS_TCMALLOC

} // namespace yb
