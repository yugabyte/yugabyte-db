// Copyright (c) Yugabyte, Inc.
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

#include "yb/util/tcmalloc_util.h"

#include <cstdint>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/tcmalloc_impl_util.h"

#if YB_GPERFTOOLS_TCMALLOC
#include <gperftools/heap-profiler.h>
#endif

using yb::operator"" _MB;

DEFINE_NON_RUNTIME_int64(server_tcmalloc_max_total_thread_cache_bytes, -1,
    "Total number of bytes to use for the thread cache for tcmalloc across all threads in the "
    "tserver/master. If this is negative, it has no effect.");
DEPRECATE_FLAG(int64, tserver_tcmalloc_max_total_thread_cache_bytes, "11_2022");

DEFINE_NON_RUNTIME_int32(tcmalloc_max_per_cpu_cache_bytes, -1,
    "Sets the maximum cache size per CPU cache if Google TCMalloc is being used. If this is zero "
    "or less, it has no effect.");

DEPRECATE_FLAG(bool, enable_process_lifetime_heap_sampling, "12_2023");

DEFINE_NON_RUNTIME_bool(enable_process_lifetime_heap_profiling,
    false,
    "WARNING: This flag will cause tcmalloc to sample every allocation. This can significantly "
    "impact performance. For a lighter approach, use sampling (profile_sample_period_bytes). "
    "This option is only supported with gperftools tcmalloc. "
    "Enables heap profiling for the lifetime of the process. Profile output will be stored in the "
    "directory specified by -heap_profile_path, and enabling this option will disable the "
    "on-demand profiling in /pprof/heap.");
TAG_FLAG(enable_process_lifetime_heap_profiling, stable);
TAG_FLAG(enable_process_lifetime_heap_profiling, advanced);

DEFINE_RUNTIME_string(heap_profile_path, "",
    "Output path to store heap profiles. If not set, profiles are stored in the directory "
    "specified by the tmp_dir flag as $FLAGS_tmp_dir/<process-name>.<pid>.<n>.heap.");
TAG_FLAG(heap_profile_path, stable);
TAG_FLAG(heap_profile_path, advanced);

// Assuming 30 lines per stack trace line * 20 bytes for the stack ptr, each sample costs 600 bytes.
// With 1 MB sampling, a 64 GB server would have ~65536 samples, so the samples take ~39 MB, which
// is a reasonable amount of overhead.
DEFINE_NON_RUNTIME_int64(profile_sample_period_bytes, 1_MB, "The interval at which "
    "TCMalloc should sample allocations. Sampling is disabled if this is set to <= 0. "
    "Does not apply to Postgres processes; see ysql_yb_tcmalloc_sample_period instead.");
DEPRECATE_FLAG(int64, profiler_sample_freq_bytes, "02_2025");

DEFINE_RUNTIME_bool(mem_tracker_include_pageheap_free_in_root_consumption, false,
    "Whether to include tcmalloc.pageheap_free_bytes from the consumption of the root memtracker. "
    "tcmalloc.pageheap_free_bytes tracks memory mapped by tcmalloc but not currently used. "
    "If we do include this in consumption, it is possible that we reject requests due to soft "
    "memory limits being hit when we actually have available memory in the pageheap. So we "
    "exclude it by default.");
TAG_FLAG(mem_tracker_include_pageheap_free_in_root_consumption, advanced);

DECLARE_string(tmp_dir);

// ------------------------------------------------------------------------------------------------

namespace yb {

int64_t GetTCMallocProperty(const char* prop) {
#if YB_TCMALLOC_ENABLED
  size_t value = 0;
#if YB_GOOGLE_TCMALLOC
  absl::optional<size_t> value_opt = ::tcmalloc::MallocExtension::GetNumericProperty(prop);
  if (!value_opt.has_value()) {
    LOG(FATAL) << "Failed to get tcmalloc property " << prop << " with Google TCMalloc";
  }
  value = *value_opt;
#endif  // YB_GOOGLE_TCMALLOC
#if YB_GPERFTOOLS_TCMALLOC
  if (!MallocExtension::instance()->GetNumericProperty(prop, &value)) {
    LOG(FATAL) << "Failed to get tcmalloc property " << prop << " with gperftools TCMalloc";
  }
#endif  // YB_GPERFTOOLS_TCMALLOC
  if (value > std::numeric_limits<int64_t>::max()) {
    YB_LOG_EVERY_N_SECS(DFATAL, 1)
        << "Value of tcmalloc property " << prop << " too large for an int64_t: " << value;
    value = std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(value);
#else
  return 0;
#endif  // YB_TCMALLOC_ENABLED
}

int64_t GetTCMallocPhysicalBytesUsed() {
  const char* property_name;
#if YB_GOOGLE_TCMALLOC
  // In Google tcmalloc, this is calculated as
  //
  //   StatSub(VirtualMemoryUsed(stats), UnmappedBytes(stats));
  //
  // which equals
  //
  //   stats.pageheap.system_bytes + stats.metadata_bytes + stats.arena.bytes_unallocated +
  //   stats.arena.bytes_unavailable - stats.pageheap.unmapped_bytes
  property_name = "generic.physical_memory_used";
#endif  // YB_GOOGLE_TCMALLOC
#if YB_GPERFTOOLS_TCMALLOC
  // In gperftools tcmalloc, this is calculated as:
  // stats.pageheap.system_bytes + stats.metadata_bytes - stats.pageheap.unmapped_bytes;
  property_name = "generic.total_physical_bytes";
#endif  // YB_GPERFTOOLS_TCMALLOC
  return GetTCMallocProperty(property_name);
}

int64_t GetTCMallocCurrentAllocatedBytes() {
  return GetTCMallocProperty("generic.current_allocated_bytes");
}

int64_t GetTCMallocCurrentHeapSizeBytes() {
  int64_t value = GetTCMallocProperty("generic.heap_size");
  // In Google TCMalloc, we do not need to subtract unmapped bytes from heap size, it has already
  // been subtracted. Only do the subtraction in case of gperftools TCMalloc.
#if YB_GPERFTOOLS_TCMALLOC
  value -= GetTCMallocPageHeapUnmappedBytes();
#endif
  return value;
}

int64_t GetTCMallocActualHeapSizeBytes() {
#if YB_TCMALLOC_ENABLED
  int64_t value = GetTCMallocCurrentHeapSizeBytes();
  if (!PREDICT_FALSE(FLAGS_mem_tracker_include_pageheap_free_in_root_consumption)) {
    // Set mem_tracker_include_pageheap_free_in_root_consumption to true to avoid this subtraction
    // and get the same behavior as before D24883.
    value -= GetTCMallocPageHeapFreeBytes();
  }
  return value;
#else
  return 0;
#endif  // YB_TCMALLOC_ENABLED
}

int64_t GetTCMallocPageHeapFreeBytes() {
  return GetTCMallocProperty("tcmalloc.pageheap_free_bytes");
}

int64_t GetTCMallocPageHeapUnmappedBytes() {
  return GetTCMallocProperty("tcmalloc.pageheap_unmapped_bytes");
}

void TCMallocReleaseMemoryToSystem(int64_t bytes) {
  if (bytes < 0)
    return;
#if YB_GOOGLE_TCMALLOC
  tcmalloc::MallocExtension::ReleaseMemoryToSystem(bytes);
#endif
#if YB_GPERFTOOLS_TCMALLOC
  MallocExtension::instance()->ReleaseToSystem(bytes);
#endif
}

#if YB_GOOGLE_TCMALLOC
// Sets the given property to the given value in Google tcmalloc using a Set... function and
// immediately calls the corresponding Get... function to verify the effective new value of the
// property. FATALs if the effective new value does not match the given value.

#define SET_AND_VERIFY_GOOGLE_TCMALLOC_PROPERTY(property_name, new_value) do { \
  auto computed_new_value = (new_value); \
  ::tcmalloc::MallocExtension::BOOST_PP_CAT(Set, property_name)((computed_new_value)); \
  auto effective_new_value = ::tcmalloc::MallocExtension::BOOST_PP_CAT(Get, property_name)(); \
  if (effective_new_value != computed_new_value) { \
    LOG(FATAL) << "Invoked Set" << BOOST_PP_STRINGIZE(property_name) << " with " \
               << computed_new_value << " but Get" << BOOST_PP_STRINGIZE(property_name) \
               << " returned " << effective_new_value << " immediately afterwards."; \
  } \
} while (false)
#endif  // YB_GOOGLE_TCMALLOC

void SetTCMallocTotalThreadCacheSize(int64_t size) {
#if YB_GOOGLE_TCMALLOC
  SET_AND_VERIFY_GOOGLE_TCMALLOC_PROPERTY(MaxTotalThreadCacheBytes, size);
#endif
#if YB_GPERFTOOLS_TCMALLOC
  // gperftools tcmalloc
  constexpr const char* const kTcMallocMaxThreadCacheBytes =
      "tcmalloc.max_total_thread_cache_bytes";

  // gperftools tcmalloc will clip the provided value to the [512 KiB, 1 GiB] range.
  if (!MallocExtension::instance()->SetNumericProperty(kTcMallocMaxThreadCacheBytes, size)) {
    LOG(FATAL) << "Failed to set tcmalloc property: " << kTcMallocMaxThreadCacheBytes
               << " to " << size << " in gperftools tcmalloc";
  }
  size_t new_value = 0;
  if (!MallocExtension::instance()->GetNumericProperty(
      kTcMallocMaxThreadCacheBytes, &new_value)) {
    LOG(FATAL) << "Failed to get the value of tcmalloc property "
               << kTcMallocMaxThreadCacheBytes << " in gperftools tcmalloc";
  }
  if (new_value > std::numeric_limits<int64_t>::max() ||
      static_cast<int64_t>(new_value) != size) {
    LOG(WARNING) << "Failed to set tcmalloc property " << kTcMallocMaxThreadCacheBytes
                 << " to " << size << ": got " << new_value << " instead";
  }
#endif  // YB_GPERFTOOLS_TCMALLOC
}

void ConfigureTCMalloc(int64_t mem_limit) {
#if YB_GOOGLE_TCMALLOC
  if (FLAGS_tcmalloc_max_per_cpu_cache_bytes > 0) {
    SET_AND_VERIFY_GOOGLE_TCMALLOC_PROPERTY(
        MaxPerCpuCacheSize, FLAGS_tcmalloc_max_per_cpu_cache_bytes);
  }
#endif  // YB_GOOGLE_TCMALLOC

#ifdef YB_TCMALLOC_ENABLED
  if (FLAGS_server_tcmalloc_max_total_thread_cache_bytes < 0) {
    FLAGS_server_tcmalloc_max_total_thread_cache_bytes =
        std::min(std::max(static_cast<size_t>(2.5 * mem_limit / 100), 32_MB), 2_GB);
  }
  LOG(INFO) << "Setting tcmalloc max thread cache bytes to: "
            << FLAGS_server_tcmalloc_max_total_thread_cache_bytes;
#endif

  if (FLAGS_server_tcmalloc_max_total_thread_cache_bytes >= 0) {
    SetTCMallocTotalThreadCacheSize(FLAGS_server_tcmalloc_max_total_thread_cache_bytes);
  }

  if (FLAGS_heap_profile_path.empty()) {
    const auto path = strings::Substitute(
        "$0/$1.$2", FLAGS_tmp_dir, google::ProgramInvocationShortName(), getpid());
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(heap_profile_path, path));
  }
  SetTCMallocSamplingPeriod(FLAGS_profile_sample_period_bytes);

#if YB_GPERFTOOLS_TCMALLOC
  if (FLAGS_enable_process_lifetime_heap_profiling) {
    HeapProfilerStart(FLAGS_heap_profile_path.c_str());
  }
#endif
}

int64_t GetTCMallocSamplingPeriod() {
#if YB_GOOGLE_TCMALLOC
  return tcmalloc::MallocExtension::GetProfileSamplingRate();
#elif YB_GPERFTOOLS_TCMALLOC
  return MallocExtension::instance()->GetProfileSamplingRate();
#endif
  return 0;
}

void SetTCMallocSamplingPeriod(int64_t sample_period_bytes) {
  bool disabled = sample_period_bytes <= 0;
  LOG(INFO) << Format("Setting TCMalloc profiler sampling period to $0 bytes", sample_period_bytes)
            << (disabled ? " (disabled)" : "");
#if YB_GOOGLE_TCMALLOC
  tcmalloc::MallocExtension::SetProfileSamplingRate(sample_period_bytes);
#elif YB_GPERFTOOLS_TCMALLOC
  MallocExtension::instance()->SetProfileSamplingRate(sample_period_bytes);
#endif
}

}  // namespace yb
