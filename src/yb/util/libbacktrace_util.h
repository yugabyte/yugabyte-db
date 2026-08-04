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

// Utilities for working with libbacktrace, a library that allows to find file names and line
// numbers in stack traces.

#ifdef __linux__
#include <backtrace.h>
#endif

#include <mutex>
#include <string>

#include "yb/util/stack_trace.h"

DECLARE_bool(use_libbacktrace);

namespace yb {
namespace libbacktrace {

struct BacktraceContext {
  StackTraceLineFormat stack_trace_line_format = StackTraceLineFormat::DEFAULT;
  std::string* buf = nullptr;
};

void BacktraceErrorCallback(void* data, const char* msg, int errnum);

int BacktraceFullCallback(void *const data, const uintptr_t pc,
                          const char* const filename, const int lineno,
                          const char* const original_function_name);

#ifdef __linux__
class GlobalBacktraceState {
 public:
  GlobalBacktraceState();
  backtrace_state* GetState() { return bt_state_; }
  std::mutex* mutex() { return &mutex_; }

 private:
  static int DummyCallback(void *const data, const uintptr_t pc,
                    const char* const filename, const int lineno,
                    const char* const original_function_name) {
    return 0;
  }

  struct backtrace_state* bt_state_;
  std::mutex mutex_;
};

#else
class GlobalBacktraceState;
#endif  // __linux__

GlobalBacktraceState* GetGlobalBacktraceState();

}
}