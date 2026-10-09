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

// Utilities for converting an program address into file name, line number, and/or function name.

#pragma once

#include "yb/util/stack_trace.h"

namespace yb {
namespace libbacktrace {
class GlobalBacktraceState;
}

// https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
enum DemangleStatus : int {
  kDemangleOk = 0,
  kDemangleMemAllocFailure = -1,
  kDemangleInvalidMangledName = -2,
  kDemangleInvalidArgument = -3
};

// Stack trace addresses are right-aligned to two characters per byte plus two for the leading
// "0x".
constexpr size_t kStackTraceAddressWidth = 2 + 2 * sizeof(void*);

// Returns pc as "0x<hex>", left-padded with spaces to kStackTraceAddressWidth, the way
// printf("%*p", kStackTraceAddressWidth, pc) does.
std::string FormatStackTraceAddress(const void* pc);

// A wrapper around glog's Symbolize function.
// Source available e.g. at
// https://github.com/yugabyte/glog/blob/v0.4.0-yb-5/src/symbolize.h#L154
bool GlogSymbolize(void *pc, char *out, int out_size);

std::string SymbolizeAddress(
    void *pc,
    const StackTraceLineFormat stack_trace_line_format = StackTraceLineFormat::DEFAULT);

void SymbolizeAddress(
    const StackTraceLineFormat stack_trace_line_format,
    void* pc,
    std::string* buf,
    StackTraceGroup* group = nullptr,
    libbacktrace::GlobalBacktraceState* global_backtrace_state = nullptr);

// Demangle a C++-mangled identifier.
std::string DemangleName(const char* name);

constexpr const char* kUnknownSymbol = "(unknown)";

// Format() pattern for a stack trace entry; $0 is FormatStackTraceAddress(pc), $1 the symbol.
constexpr const char* kStackTraceEntryFormat = "    @ $0  $1";

}  // namespace yb
