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

#include "yb/util/libbacktrace_util.h"

#include <regex>
#include <string>
#include <thread>

#include "yb/gutil/singleton.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/source_location.h"
#include "yb/util/symbolize.h"

using std::string;

#if defined(__linux__) && !defined(NDEBUG)
constexpr bool kDefaultUseLibbacktrace = true;
#else
constexpr bool kDefaultUseLibbacktrace = false;
#endif

DEFINE_NON_RUNTIME_bool(use_libbacktrace, kDefaultUseLibbacktrace,
    "Whether to use the libbacktrace library for symbolizing stack traces");

namespace yb {
namespace libbacktrace {

#ifdef __linux__

void BacktraceErrorCallback(void* data, const char* msg, int errnum) {
  string* buf_ptr = nullptr;
  if (data) {
    auto* context = static_cast<BacktraceContext*>(data);
    if (context->buf) {
      buf_ptr = context->buf;
      buf_ptr->append(StringPrintf("Backtrace error: %s (errnum=%d)\n", msg, errnum));
      return;
    }
  }

  // A backup mechanism for error reporting.
  fprintf(stderr, "%s called with data=%p, msg=%s, errnum=%d, buf_ptr=%p\n", __func__,
          data, msg, errnum, buf_ptr);
}

int BacktraceFullCallback(void *const data, const uintptr_t pc,
                          const char* const filename, const int lineno,
                          const char* const original_function_name) {
  assert(data != nullptr);
  const BacktraceContext& context =
      *pointer_cast<BacktraceContext*>(data);
  string* const buf = context.buf;
  int demangle_status = 0;
  char* const demangled_function_name =
      original_function_name != nullptr ?
      abi::__cxa_demangle(original_function_name,
                          nullptr,  // output_buffer
                          nullptr,  // length
                          &demangle_status) :
      nullptr;
  const char* function_name_to_use = original_function_name;
  if (original_function_name != nullptr) {
    if (demangle_status != kDemangleOk) {
      if (demangle_status != kDemangleInvalidMangledName) {
        // -2 means the mangled name is not a valid name under the C++ ABI mangling rules.
        // This happens when the name is e.g. "main", so we don't report the error.
        StringAppendF(buf, "Error: __cxa_demangle failed for '%s' with error code %d\n",
            original_function_name, demangle_status);
      }
      // Regardless of the exact reason for demangle failure, we use the original function name
      // provided by libbacktrace.
    } else if (demangled_function_name != nullptr) {
      // If __cxa_demangle returns 0 and a non-null string, we use that instead of the original
      // function name.
      function_name_to_use = demangled_function_name;
    } else {
      StringAppendF(buf,
          "Error: __cxa_demangle returned zero status but nullptr demangled function for '%s'\n",
          original_function_name);
    }
  }

  string pretty_function_name;
  if (function_name_to_use == nullptr) {
    pretty_function_name = kUnknownSymbol;
  } else {
    // Allocating regexes on the heap so that they would never get deallocated. This is because
    // the kernel watchdog thread could still be symbolizing stack traces as global destructors
    // are being called.
    static const std::regex* kStdColonColonOneRE = new std::regex("\\bstd::__1::\\b");
    pretty_function_name = std::regex_replace(function_name_to_use, *kStdColonColonOneRE, "std::");

    static const std::regex* kStringRE = new std::regex(
        "\\bstd::basic_string<char, std::char_traits<char>, std::allocator<char> >");
    pretty_function_name = std::regex_replace(pretty_function_name, *kStringRE, "string");

    static const std::regex* kRemoveStdPrefixRE =
        new std::regex("\\bstd::(string|tuple|shared_ptr|unique_ptr)\\b");
    pretty_function_name = std::regex_replace(pretty_function_name, *kRemoveStdPrefixRE, "$1");
  }

  const bool is_symbol_only_fmt =
      context.stack_trace_line_format == StackTraceLineFormat::SYMBOL_ONLY;

  // We have not appended an end-of-line character yet. Let's see if we have file name / line number
  // information first. BTW kStackTraceEntryFormat is used both here and in glog-based
  // symbolization.
  if (filename == nullptr) {
    // libbacktrace failed to produce an address. We still need to produce a line of the form:
    // "    @     0x7f2d98f9bd87  "
    char hex_pc_buf[32];
    snprintf(hex_pc_buf, sizeof(hex_pc_buf), "0x%" PRIxPTR, pc);
    if (is_symbol_only_fmt) {
      // At least print out the hex address, otherwise we'd end up with an empty string.
      *buf += hex_pc_buf;
    } else {
      StringAppendF(buf, "    @ %18s ", hex_pc_buf);
    }
  } else {
    const string frame_without_file_line =
        is_symbol_only_fmt ? pretty_function_name
                           : StringPrintf(
                               kStackTraceEntryFormat, kPrintfPointerFieldWidth,
                               reinterpret_cast<void*>(pc), pretty_function_name.c_str());

    // Got filename and line number from libbacktrace! No need to filter the output through
    // addr2line, etc.
    switch (context.stack_trace_line_format) {
      case StackTraceLineFormat::CLION_CLICKABLE: {
        const string file_line_prefix = StringPrintf("%s:%d: ", filename, lineno);
        StringAppendF(buf, "%-100s", file_line_prefix.c_str());
        *buf += frame_without_file_line;
        break;
      }
      case StackTraceLineFormat::SHORT: {
        *buf += frame_without_file_line;
        StringAppendF(buf, " (%s:%d)", ShortenSourceFilePath(filename), lineno);
        break;
      }
      case StackTraceLineFormat::SYMBOL_ONLY: {
        *buf += frame_without_file_line;
        StringAppendF(buf, " (%s:%d)", ShortenSourceFilePath(filename), lineno);
        break;
      }
    }
  }

  buf->push_back('\n');

  // No need to check for nullptr, free is a no-op in that case.
  free(demangled_function_name);
  return 0;
}

GlobalBacktraceState::GlobalBacktraceState() {
  bt_state_ = backtrace_create_state(
      /* filename */ nullptr,
      /* threaded = */ 0,
      BacktraceErrorCallback,
      /* data */ nullptr);

  // To complete initialization we should call backtrace, otherwise it could fail in case of
  // concurrent initialization.
  // We do this in a separate thread to avoid deadlock.
  // TODO: implement proper fix to avoid other potential deadlocks/stuck threads related
  // to locking common mutexes from signal handler. See
  // https://github.com/yugabyte/yugabyte-db/issues/6672 for more details.
  // OK to use std::thread here because this thread is very short-lived.
  std::thread backtrace_thread([&] {  // NOLINT
      backtrace_full(bt_state_, /* skip = */ 1, DummyCallback,
                     BacktraceErrorCallback, nullptr);
  });
  backtrace_thread.join();
}

GlobalBacktraceState* GetGlobalBacktraceState() {
  return FLAGS_use_libbacktrace ? Singleton<GlobalBacktraceState>::get() : nullptr;
}

#else

GlobalBacktraceState* GetGlobalBacktraceState() {
  return nullptr;
}

#endif  // __linux__


}  // namespace libbacktrace
}  // namespace yb
