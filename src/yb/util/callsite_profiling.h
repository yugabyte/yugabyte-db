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

#pragma once

#include <cstdint>
#include <atomic>
#include <vector>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/gutil/walltime.h"

#include "yb/util/flags.h"
#include "yb/util/monotime.h"

DECLARE_bool(enable_callsite_profile);
DECLARE_bool(enable_callsite_profile_timing);

namespace yb {

struct CallsiteProfileEntry {
  int64_t count;
  int64_t total_cycles;
  int64_t total_usec;
  double avg_cycles;
  double avg_usec;
  const char* file_path;
  int line_number;
  const char* function_name;
  const char* code_line;
};

// An internal namespace for call site profiling utility code.
namespace callsite_profiling {

class Callsite {
 public:
  Callsite(const char* file, int line, const char* function_name, const char* code_line);

  void Increment(int64_t cycles, int64_t elapsed_usec) {
    count_.fetch_add(1, std::memory_order_relaxed);
    total_cycles_.fetch_add(cycles, std::memory_order_relaxed);
    if (elapsed_usec > 0) {
      total_usec_.fetch_add(elapsed_usec, std::memory_order_relaxed);
    }
  }

  void Reset();
  CallsiteProfileEntry GetProfileEntry() const;
  const char* file_path() const { return file_; }
  int line_number() const { return line_; }
  const char* function_name() const { return function_name_; }

  double AvgMicros() const;

  // A string representation of this call site (file/line/function, no code snippet or stats).
  std::string ToString() const;

 private:
  const char* file_;
  int line_;
  const char* function_name_;
  const char* code_line_;

  std::atomic<int64_t> count_{0};
  std::atomic<int64_t> total_cycles_{0};
  std::atomic<int64_t> total_usec_{0};

  friend std::vector<CallsiteProfileEntry> GetCallsiteProfile();
};

struct ProfilingHelper {
  Callsite* callsite;
  bool use_time;
  MonoTime start_time;
  int64_t start_cycles;

  explicit ProfilingHelper(Callsite* callsite_)
      : callsite(callsite_),
        use_time(FLAGS_enable_callsite_profile_timing),
        start_time(use_time ? MonoTime::Now() : MonoTime()),
        start_cycles(CycleClock::Now()) {
  }

  ~ProfilingHelper();
};

}  // namespace callsite_profiling

// A utility for lightweight profiling of a call site.
#define YB_PROFILE(code) \
    do { \
      if (FLAGS_enable_callsite_profile) { \
        static ::yb::callsite_profiling::Callsite _callsite_static_object( \
            __FILE__, __LINE__, __func__, BOOST_PP_STRINGIZE(code)); \
        ::yb::callsite_profiling::ProfilingHelper _callsite_profiling_helper(\
            &_callsite_static_object); \
        code; \
      } else { \
        code; \
      } \
    } while (false)

std::vector<CallsiteProfileEntry> GetCallsiteProfile();

void ResetCallsiteProfile();

}  // namespace yb
