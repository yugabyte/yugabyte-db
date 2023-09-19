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

#include "yb/util/debug-util.h"

#include <execinfo.h>
#include <dirent.h>
#include <sys/syscall.h>
#include "yb/util/scope_exit.h"

#ifdef __linux__
#include <link.h>
#include <cxxabi.h>
#endif // __linux__

#include <string>
#include <iostream>
#include <mutex>
#include <regex>

#include <glog/logging.h>

#include "yb/gutil/linux_syscall_support.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/numbers.h"

#include "yb/util/enums.h"
#include "yb/util/errno.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/libbacktrace_util.h"
#include "yb/util/lockfree.h"
#include "yb/util/monotime.h"
#include "yb/util/source_location.h"
#include "yb/util/status.h"
#include "yb/util/symbolize.h"
#include "yb/util/thread.h"

DECLARE_bool(use_libbacktrace);

using namespace std::literals;

using std::string;

// A hack to grab a function from glog.
namespace google {
namespace glog_internal_namespace_ {

extern void DumpStackTraceToString(std::string *s);

}  // namespace glog_internal_namespace_
}  // namespace google

namespace yb {

Status ListThreads(std::vector<pid_t> *tids) {
#if defined(__linux__)
  DIR *dir = opendir("/proc/self/task/");
  if (dir == NULL) {
    return STATUS(IOError, "failed to open task dir", Errno(errno));
  }
  struct dirent *d;
  while ((d = readdir(dir)) != NULL) {
    if (d->d_name[0] != '.') {
      uint32_t tid;
      if (!safe_strtou32(d->d_name, &tid)) {
        LOG(WARNING) << "bad tid found in procfs: " << d->d_name;
        continue;
      }
      tids->push_back(tid);
    }
  }
  closedir(dir);
#endif // defined(__linux__)
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// GetStackTrace implementation
// ------------------------------------------------------------------------------------------------

std::string GetStackTrace(StackTraceLineFormat stack_trace_line_format,
                          int num_top_frames_to_skip) {
  std::string buf;
#ifdef __linux__
  if (FLAGS_use_libbacktrace) {
    libbacktrace::BacktraceContext context;
    context.buf = &buf;
    context.stack_trace_line_format = stack_trace_line_format;

    // Use libbacktrace on Linux because that gives us file names and line numbers.
    auto* global_backtrace_state = Singleton<libbacktrace::GlobalBacktraceState>::get();

    // Avoid multi-threaded access to libbacktrace which causes high memory consumption.
    std::lock_guard l(*global_backtrace_state->mutex());
    struct backtrace_state* const backtrace_state = global_backtrace_state->GetState();

    // TODO: https://yugabyte.atlassian.net/browse/ENG-4729

    const int backtrace_full_rv = backtrace_full(
        backtrace_state, /* skip = */ num_top_frames_to_skip + 1,
        libbacktrace::BacktraceFullCallback, libbacktrace::BacktraceErrorCallback, &context);
    if (backtrace_full_rv != 0) {
      StringAppendF(&buf, "Error: backtrace_full return value is %d", backtrace_full_rv);
    }
    return buf;
  }
#endif
  google::glog_internal_namespace_::DumpStackTraceToString(&buf);
  return buf;
}

// ------------------------------------------------------------------------------------------------

std::string GetStackTraceHex() {
  char buf[1024];
  HexStackTraceToString(buf, 1024);
  return std::string(buf);
}

void HexStackTraceToString(char* buf, size_t size) {
  StackTrace trace;
  trace.Collect(1);
  trace.StringifyToHex(buf, size);
}

string GetLogFormatStackTraceHex() {
  StackTrace trace;
  trace.Collect(1);
  return trace.ToLogFormatHexString();
}

namespace {
#ifdef __linux__
int DynamcLibraryListCallback(struct dl_phdr_info *info, size_t size, void *data) {
  if (*info->dlpi_name != '\0') {
    // We can't use LOG(...) yet because Google Logging might not be initialized.
    // It is also important to write the entire line at once so that it is less likely to be
    // interleaved with pieces of similar lines from other processes.
    std::cerr << StringPrintf(
        "Shared library '%s' loaded at address 0x%" PRIx64 "\n", info->dlpi_name, info->dlpi_addr);
  }
  return 0;
}
#endif

void PrintLoadedDynamicLibraries() {
#ifdef __linux__
  // Supported on Linux only.
  dl_iterate_phdr(DynamcLibraryListCallback, nullptr);
#endif
}

bool PrintLoadedDynamicLibrariesOnceHelper() {
  const char* list_dl_env_var = std::getenv("YB_LIST_LOADED_DYNAMIC_LIBS");
  if (list_dl_env_var != nullptr && *list_dl_env_var != '\0') {
    PrintLoadedDynamicLibraries();
  }
  return true;
}

} // anonymous namespace

string SymbolizeAddress(void *pc, const StackTraceLineFormat stack_trace_line_format) {
  string s;
  SymbolizeAddress(stack_trace_line_format, pc, &s);
  return s;
}

// ------------------------------------------------------------------------------------------------
// Tracing function calls
// ------------------------------------------------------------------------------------------------

namespace {

// List the load addresses of dynamic libraries once on process startup if required.
const bool  __attribute__((unused)) kPrintedLoadedDynamicLibraries =
    PrintLoadedDynamicLibrariesOnceHelper();

}  // namespace

std::string SourceLocation::ToString() const {
  return Format("$0:$1", file_name, line_number);
}

ScopeLogger::ScopeLogger(const std::string& msg, std::function<void()> on_scope_bounds)
    : msg_(msg), on_scope_bounds_(std::move(on_scope_bounds)) {
  LOG(INFO) << ">>> " << msg_;
  on_scope_bounds_();
}

ScopeLogger::~ScopeLogger() {
  on_scope_bounds_();
  LOG(INFO) << "<<< " << msg_;
}

}  // namespace yb
