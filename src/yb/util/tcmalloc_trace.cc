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

#include "yb/util/tcmalloc_util.h"
#include "yb/util/flags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/random_util.h"
#include "yb/util/debug-util.h"
#include "yb/util/tcmalloc_impl_util.h"

DEFINE_NON_RUNTIME_bool(tcmalloc_trace_enabled, false,
    "Enable tracing of malloc/free calls for tcmalloc.");
TAG_FLAG(tcmalloc_trace_enabled, advanced);

DEFINE_NON_RUNTIME_uint64(tcmalloc_trace_min_threshold, 0,
    "Minimum (inclusive) threshold for tracing malloc/free calls.");
TAG_FLAG(tcmalloc_trace_min_threshold, advanced);

DEFINE_NON_RUNTIME_uint64(tcmalloc_trace_max_threshold, 0,
    "Maximum (exclusive) threshold for tracing malloc/free calls. If 0, no maximum threshold is "
    "used.");
TAG_FLAG(tcmalloc_trace_max_threshold, advanced);

DEFINE_RUNTIME_double(tcmalloc_trace_frequency, 0.0,
    "Frequency at which malloc/free calls should be traced.");
TAG_FLAG(tcmalloc_trace_frequency, advanced);

namespace yb {

namespace {

// Malloc hooks are not suppported in Google's TCMalloc as of Dec 12 2022 (and thus tracing is not
// either). See issue: https://github.com/google/tcmalloc/issues/44.

#if YB_GPERFTOOLS_TCMALLOC

// Memory tracker for tcmalloc tracing.
std::shared_ptr<MemTracker> tcmalloc_trace_tracker;

bool CheckWithinTCMallocTraceThreshold(size_t size) {
  return FLAGS_tcmalloc_trace_min_threshold <= size &&
      (FLAGS_tcmalloc_trace_max_threshold == 0 || size < FLAGS_tcmalloc_trace_max_threshold);
}

void TCMallocNewHook(const void* ptr, size_t) {
  if (!tcmalloc_trace_tracker) return;

  auto size = MallocExtension::instance()->GetAllocatedSize(ptr);
  if (CheckWithinTCMallocTraceThreshold(size)) {
    tcmalloc_trace_tracker->Consume(size);

    // Skip stack trace logging for memory allocations while initializing random, because
    // RandomActWithProbability will cause infinite recursion otherwise.
    if (!IsRandomInitializingInThisThread() &&
        RandomActWithProbability(FLAGS_tcmalloc_trace_frequency)) {
      // Skip four top frames: GetStackTrace, this function, the malloc hook invoker, and the
      // allocation itself.
      LOG(INFO) << "Malloc Call: size = " << size << "\n" <<
          GetStackTrace(StackTraceLineFormat::DEFAULT, 4 /* num_top_frames_to_skip */);
    }
  }
}
void TCMallocDeleteHook(const void* ptr) {
  if (!tcmalloc_trace_tracker) return;

  auto size = MallocExtension::instance()->GetAllocatedSize(ptr);

  // We didn't track any allocations done during program initialization, but some of those may
  // be deleted before the tracker is deleted. Avoid throwing an error by returning early.
  if (static_cast<int64_t>(size) > tcmalloc_trace_tracker->consumption()) return;

  if (CheckWithinTCMallocTraceThreshold(size)) {
    tcmalloc_trace_tracker->Release(size);
  }
}

#endif  // YB_GPERFTOOLS_TCMALLOC

}  // namespace

void RegisterTCMallocTraceHooks() {
#if YB_GPERFTOOLS_TCMALLOC
  if (FLAGS_tcmalloc_trace_enabled && !tcmalloc_trace_tracker) {
    LOG(INFO) << "TCMalloc tracing enabled";
    tcmalloc_trace_tracker = MemTracker::FindOrCreateTracker(
        "TCMalloc Trace", MemTracker::GetRootTracker(), AddToParent::kFalse, CreateMetrics::kTrue);
    MallocHook::AddNewHook(TCMallocNewHook);
    MallocHook::AddDeleteHook(TCMallocDeleteHook);
  }
#endif  // YB_GPERFTOOLS_TCMALLOC
}

}  // namespace yb
