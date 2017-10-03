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
#ifndef KUDU_UTIL_DEBUG_UTIL_H
#define KUDU_UTIL_DEBUG_UTIL_H

#include <sys/types.h>

#include <string>
#include <vector>

#include "kudu/gutil/strings/fastmem.h"
#include "kudu/util/status.h"

namespace kudu {

// Return a list of all of the thread IDs currently running in this process.
// Not async-safe.
Status ListThreads(std::vector<pid_t>* tids);

// Set which POSIX signal number should be used internally for triggering
// stack traces. If the specified signal handler is already in use, this
// returns an error, and stack traces will be disabled.
Status SetStackTraceSignal(int signum);

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
std::string DumpThreadStack(int64_t tid);

// Return the current stack trace, stringified.
std::string GetStackTrace();

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
// looks very similar to GetStackTrace() but without symbols. Because it's in
// that format, the tool stacktrace_addr2line.pl in the kudu build-support
// directory can symbolize it automatically (to the extent that addr2line(1)
// is able to find the symbols).
std::string GetLogFormatStackTraceHex();

// Collect the current stack trace in hex form into the given buffer.
//
// The resulting trace just includes the hex addresses, space-separated. This is suitable
// for later stringification by pasting into 'addr2line' for example.
//
// This function is not async-safe, since it uses the libc backtrace() function which
// may invoke the dynamic loader.
void HexStackTraceToString(char* buf, size_t size);

// Efficient class for collecting and later stringifying a stack trace.
//
// Requires external synchronization.
class StackTrace {
 public:
  StackTrace()
    : num_frames_(0) {
  }

  void Reset() {
    num_frames_ = 0;
  }

  void CopyFrom(const StackTrace& s) {
    memcpy(this, &s, sizeof(s));
  }

  bool Equals(const StackTrace& s) {
    return s.num_frames_ == num_frames_ &&
      strings::memeq(frames_, s.frames_,
                     num_frames_ * sizeof(frames_[0]));
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
  std::string Symbolize() const;

  // Return a string with a hex-only backtrace in the format typically used in
  // log files. Similar to the format given by Symbolize(), but symbols are not
  // resolved (only the hex addresses are given).
  std::string ToLogFormatHexString() const;

  uint64_t HashCode() const;

 private:
  enum {
    // The maximum number of stack frames to collect.
    kMaxFrames = 16,

    // The max number of characters any frame requires in string form.
    kHexEntryLength = 16
  };

  int num_frames_;
  void* frames_[kMaxFrames];
};

} // namespace kudu

#endif
