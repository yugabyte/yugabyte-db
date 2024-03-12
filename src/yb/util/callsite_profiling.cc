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
//

#include "yb/util/callsite_profiling.h"

#include <atomic>
#include <vector>

#include <glog/logging.h>

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/source_location.h"

DEFINE_RUNTIME_bool(enable_callsite_profile, false,
    "Enable counting and timing of function calls enclosed in the YB_PROFILE macro. The results "
    "are shown in the /pprof/callsite_profile web UI page.");
DEFINE_RUNTIME_bool(enable_callsite_profile_timing, false,
    "In addition to CPU cycle counts, also measure the actual time in microseconds in profiled "
    "call sites.");
DEFINE_RUNTIME_int64(callsite_profile_stack_trace_threshold_usec, 0,
    "Threshold, in microseconds, for printing stack traces for profiled call sites that take "
    "longer than this amount of time. Regardless of this setting, stack traces at any given call "
    "site will not be printed more than once a second. Set to 0 to disable stack traces.");

using yb::callsite_profiling::Callsite;

namespace yb {

namespace {

constinit simple_spinlock callsites_mutex;
std::vector<Callsite*> callsites GUARDED_BY(callsites_mutex);

}  // namespace

Callsite::Callsite(
    const char* file, int line, const char* function_name, const char* code_line)
    : file_(ShortenSourceFilePath(file)),
      line_(line),
      function_name_(function_name),
      code_line_(code_line) {
  std::lock_guard lock(callsites_mutex);
  callsites.push_back(this);
}

void Callsite::Reset() {
  count_ = 0;
  total_cycles_ = 0;
  total_usec_ = 0;
}

double Callsite::AvgMicros() const {
  auto count = count_.load(std::memory_order_relaxed);
  if (!count)
    return 0;
  return total_usec_.load(std::memory_order_relaxed) * 1.0 / count;
}

std::string Callsite::ToString() const {
  return Format("$0:$1, function $2", file_, line_, function_name_);
}

CallsiteProfileEntry Callsite::GetProfileEntry() const {
  int64_t count = count_.load(std::memory_order_relaxed);
  int64_t total_cycles = total_cycles_.load(std::memory_order_relaxed);
  int64_t total_usec = total_usec_.load(std::memory_order_relaxed);
  return CallsiteProfileEntry {
    .count = count,
    .total_cycles = total_cycles,
    .total_usec = total_usec,
    .avg_cycles = count == 0 ? 0 : (total_cycles * 1.0 / count),
    .avg_usec = count == 0 ? 0 : (total_usec * 1.0 / count),
    .file_path = file_,
    .line_number = line_,
    .function_name = function_name_,
    .code_line = code_line_
  };
}

namespace callsite_profiling {

ProfilingHelper::~ProfilingHelper() {
  int64_t elapsed_cycles = ::CycleClock::Now() - start_cycles;
  int64_t elapsed_usec = use_time ? (MonoTime::Now() - start_time).ToMicroseconds() : 0;
  callsite->Increment(elapsed_cycles, elapsed_usec);
  int64_t stack_trace_threshold_usec = FLAGS_callsite_profile_stack_trace_threshold_usec;
  if (PREDICT_FALSE(stack_trace_threshold_usec > 0  &&
                    elapsed_usec >= stack_trace_threshold_usec)) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << "Call took " << elapsed_usec << " usec (>= " << stack_trace_threshold_usec << " usec). "
        << callsite->ToString()
        << ", avg time: "
        << StringPrintf("%.3f", callsite->AvgMicros())
        << " usec, stack trace:\n"
        << GetStackTrace();
  }
}

}  // namespace callsite_profiling

std::vector<CallsiteProfileEntry> GetCallsiteProfile() {
  std::vector<CallsiteProfileEntry> result;
  std::lock_guard lock(callsites_mutex);
  result.reserve(callsites.size());
  for (const auto* callsite : callsites) {
    result.push_back(callsite->GetProfileEntry());
  }
  return result;
}

void ResetCallsiteProfile() {
  std::lock_guard lock(callsites_mutex);
  for (auto* callsite : callsites) {
    callsite->Reset();
  }
}

}  // namespace yb
