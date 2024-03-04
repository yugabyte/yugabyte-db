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

#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/source_location.h"

DEFINE_RUNTIME_bool(enable_callsite_profile, false,
                    "Enable counting and timing of function calls enclosed in the YB_PROFILE macro."
                    " The results are shown in the /pprof/callsite_profile web UI page.");
DEFINE_RUNTIME_bool(enable_callsite_profile_timing, false,
                    "In addition to CPU cycle counts, also measure the actual time in microseconds "
                    "in profiled call sites.");

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

CallsiteProfileEntry Callsite::GetProfileEntry() const {
  return CallsiteProfileEntry {
    .count = count_,
    .total_cycles = total_cycles_,
    .total_usec = total_usec_,
    .avg_cycles = count_ == 0 ? 0 : (total_cycles_ * 1.0 / count_),
    .avg_usec = count_ == 0 ? 0 : (total_usec_ * 1.0 / count_),
    .file_path = file_,
    .line_number = line_,
    .function_name = function_name_,
    .code_line = code_line_
  };
}

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
