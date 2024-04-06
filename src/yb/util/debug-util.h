// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <sys/types.h>

#include <functional>
#include <string>
#include <vector>

#include "yb/gutil/strings/fastmem.h"

#include "yb/util/debug.h"
#include "yb/util/enums.h"
#include "yb/util/slice.h"
#include "yb/util/stack_trace.h"
#include "yb/util/status_fwd.h"

namespace yb {

// Return a list of all of the thread IDs currently running in this process.
// Not async-safe.
Status ListThreads(std::vector<pid_t>* tids);

// Return the stack trace of the given thread, stringified and symbolized.
//
// Note that the symbolization happens on the calling thread, not the target
// thread, so this is relatively low-impact on the target.
//
// This is safe to use against the current thread, the main thread, or any other
// thread. It requires that the target thread has not blocked POSIX signals. If
// it has, an error message will be returned.
//
// This function is thread-safe but coarsely synchronized: only one "dumper" thread
// may be active at a time.
std::string DumpThreadStack(ThreadIdForStack tid);

// Return the current stack trace, stringified.
std::string GetStackTrace(
    StackTraceLineFormat source_file_path_format = StackTraceLineFormat::DEFAULT,
    int num_top_frames_to_skip = 0);

// Return the current stack trace without the top frame. Useful for implementing functions that
// themselves have a "get/print stack trace at the call site" semantics. This is equivalent to
// calling GetStackTrace(StackTraceLineFormat::DEFAULT, 1).
inline std::string GetStackTraceWithoutTopFrame() {
  // Note that here we specify num_top_frames_to_skip = 2, because we don't want either this
  // function itself or its _caller_ to show up in the stack trace.
  return GetStackTrace(StackTraceLineFormat::DEFAULT, /* num_top_frames_to_skip */ 2);
}

// Return the current stack trace, in hex form. This is significantly
// faster than GetStackTrace() above, so should be used in performance-critical
// places like TRACE() calls. If you really need blazing-fast speed, though,
// use HexStackTraceToString() into a stack-allocated buffer instead --
// this call causes a heap allocation for the std::string.
//
// Note that this is much more useful in the context of a static binary,
// since addr2line wouldn't know where shared libraries were mapped at
// runtime.
//
// NOTE: This inherits the same async-safety issue as HexStackTraceToString()
std::string GetStackTraceHex();

// This is the same as GetStackTraceHex(), except multi-line in a format that
// looks very similar to GetStackTrace() but without symbols.
std::string GetLogFormatStackTraceHex();

// Collect the current stack trace in hex form into the given buffer.
//
// The resulting trace just includes the hex addresses, space-separated. This is suitable
// for later stringification by pasting into 'addr2line' for example.
//
// This function is not async-safe, since it uses the libc backtrace() function which
// may invoke the dynamic loader.
void HexStackTraceToString(char* buf, size_t size);

class NODISCARD_CLASS ScopeLogger {
 public:
  ScopeLogger(const std::string& msg, std::function<void()> on_scope_bounds);

  ~ScopeLogger();

 private:
  const std::string msg_;
  std::function<void()> on_scope_bounds_;
};

#define TEST_PAUSE_IF_FLAG_WITH_PREFIX(flag_name, prefix) \
    if (PREDICT_FALSE(ANNOTATE_UNPROTECTED_READ(BOOST_PP_CAT(FLAGS_, flag_name)))) { \
      LOG(INFO) << prefix << "Pausing due to flag " << #flag_name; \
      do { \
        SleepFor(MonoDelta::FromMilliseconds(100)); \
      } while (ANNOTATE_UNPROTECTED_READ(BOOST_PP_CAT(FLAGS_, flag_name))); \
      LOG(INFO) << prefix << "Resuming due to flag " << #flag_name; \
    }

#define TEST_PAUSE_IF_FLAG(flag_name) \
  TEST_PAUSE_IF_FLAG_WITH_PREFIX(flag_name, "")

#define TEST_PAUSE_IF_FLAG_WITH_LOG_PREFIX(flag_name) \
  TEST_PAUSE_IF_FLAG_WITH_PREFIX(flag_name, LogPrefix())

} // namespace yb
