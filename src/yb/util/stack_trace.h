// Copyright (c) YugaByte, Inc.
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

#include <pthread.h>

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/slice.h"

namespace yb {

#if defined(__linux__)
typedef int64_t ThreadIdForStack;
#else
typedef pthread_t ThreadIdForStack;
#endif

enum class StackTraceLineFormat {
  SHORT,
  CLION_CLICKABLE,
  SYMBOL_ONLY,
#if !defined(NDEBUG)
  DEFAULT = CLION_CLICKABLE
#else
  DEFAULT = SHORT
#endif
};

// Active - thread is performing execution.
// Waiting - thread is waiting on mutex.
// Idle - thread is waiting on condition or epoll.
YB_DEFINE_ENUM(StackTraceGroup, (kActive)(kWaiting)(kIdle));

// Efficient class for collecting and later stringifying a stack trace.
//
// Requires external synchronization.
class StackTrace {
 public:
  // It is OK that frames_ is uninitialized.
  StackTrace() : num_frames_(0) {
  }

  void Reset() {
    num_frames_ = 0;
  }

  // Collect and store the current stack trace. Skips the top 'skip_frames' frames
  // from the stack. For example, a value of '1' will skip the 'Collect()' function
  // call itself.
  //
  // This function is technically not async-safe. However, according to
  // http://lists.nongnu.org/archive/html/libunwind-devel/2011-08/msg00054.html it is "largely
  // async safe" and it would only deadlock in the case that you call it while a dynamic library
  // load is in progress. We assume that dynamic library loads would almost always be completed
  // very early in the application lifecycle, so for now, this is considered "async safe" until
  // it proves to be a problem.
  void Collect(int skip_frames = 1);

  enum Flags {
    // Do not fix up the addresses on the stack to try to point to the 'call'
    // instructions instead of the return address. This is necessary when dumping
    // addresses to be interpreted by 'pprof', which does this fix-up itself.
    NO_FIX_CALLER_ADDRESSES = 1
  };

  // Stringify the trace into the given buffer.
  // The resulting output is hex addresses suitable for passing into 'addr2line'
  // later.
  void StringifyToHex(char* buf, size_t size, int flags = 0) const;

  // Same as above, but returning a std::string.
  // This is not async-safe.
  std::string ToHexString(int flags = 0) const;

  // Return a string with a symbolized backtrace in a format suitable for
  // printing to a log file.
  // This is not async-safe.
  // If group is specified it is filled with value corresponding to this stack trace.
  std::string Symbolize(
      StackTraceLineFormat source_file_path_format = StackTraceLineFormat::DEFAULT,
      StackTraceGroup* group = nullptr) const;

  // Return a string with a hex-only backtrace in the format typically used in
  // log files. Similar to the format given by Symbolize(), but symbols are not
  // resolved (only the hex addresses are given).
  std::string ToLogFormatHexString() const;

  uint64_t HashCode() const;

  explicit operator bool() const {
    return num_frames_ != 0;
  }

  bool operator!() const {
    return num_frames_ == 0;
  }

  int compare(const StackTrace& rhs) const {
    return as_slice().compare(rhs.as_slice());
  }

  Slice as_slice() const {
    return Slice(pointer_cast<const char*>(frames_),
                 pointer_cast<const char*>(frames_ + num_frames_));
  }

 private:
  enum {
    // The maximum number of stack frames to collect.
    kMaxFrames = 16,

    // The max number of characters any frame requires in string form.
    kHexEntryLength = 16
  };

  int num_frames_;
  void* frames_[kMaxFrames];

  friend inline bool operator==(const StackTrace& lhs, const StackTrace& rhs) {
    return lhs.num_frames_ == rhs.num_frames_ && lhs.as_slice() == rhs.as_slice();
  }

  friend inline bool operator!=(const StackTrace& lhs, const StackTrace& rhs) {
    return !(lhs == rhs);
  }

  friend inline bool operator<(const StackTrace& lhs, const StackTrace& rhs) {
    return lhs.compare(rhs) < 0;
  }
};

Result<StackTrace> ThreadStack(ThreadIdForStack tid);
// tids should be ordered
std::vector<Result<StackTrace>> ThreadStacks(const std::vector<ThreadIdForStack>& tids);

// Set which POSIX signal number should be used internally for triggering
// stack traces. If the specified signal handler is already in use, this
// returns an error, and stack traces will be disabled.
// It is not safe to call this after threads have been created.
Status SetStackTraceSignal(int signum);
int GetStackTraceSignal();

}  // namespace yb
