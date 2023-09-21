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
#ifndef YB_UTIL_TRACE_H
#define YB_UTIL_TRACE_H

#include <atomic>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/threading/thread_collision_warner.h"

#include "yb/util/atomic.h" // For GetAtomicFlag
#include "yb/util/locks.h"
#include "yb/util/memory/arena_fwd.h"
#include "yb/util/monotime.h"

DECLARE_bool(use_monotime_for_traces);
DECLARE_int32(tracing_level);

// Adopt a Trace on the current thread for the duration of the current
// scope. The old current Trace is restored when the scope is exited.
//
// 't' should be a Trace* pointer.
#define ADOPT_TRACE(t) yb::ScopedAdoptTrace _adopt_trace(t);

// Like the above, but takes the trace pointer as an explicit argument.
#define TRACE_TO_WITH_TIME(trace, time, format, substitutions...) \
  do { \
    if ((trace)) { \
      (trace)->SubstituteAndTrace( \
          __FILE__, __LINE__, (time), (format), ##substitutions); \
    } \
  } while (0)

#define VTRACE_TO(level, trace, format, substitutions...) \
  do { \
    if ((trace) && level <= GetAtomicFlag(&FLAGS_tracing_level)) { \
      const bool use_fine_ts = GetAtomicFlag(&FLAGS_use_monotime_for_traces); \
      auto time = (use_fine_ts ? ToCoarse(MonoTime::Now()) : CoarseMonoClock::Now()); \
      TRACE_TO_WITH_TIME(trace, time, format, ##substitutions); \
    } \
  } while (0)

#define TRACE_TO(trace, format, substitutions...) \
  VTRACE_TO(0, (trace), (format), ##substitutions)

// Issue a trace message, if tracing is enabled in the current thread.
// and the current tracing level flag is >= the specified level.
// See Trace::SubstituteAndTrace for arguments.
// Example:
//  VTRACE(1, "Acquired timestamp $0", timestamp);
#define VTRACE(level, format, substitutions...) \
  VTRACE_TO(level, Trace::CurrentTrace(), format, ##substitutions)

// Issue a trace message, if tracing is enabled in the current thread.
// See Trace::SubstituteAndTrace for arguments.
// Example:
//  TRACE("Acquired timestamp $0", timestamp);
#define TRACE(format, substitutions...) \
  VTRACE(0, (format), ##substitutions)

#define TRACE_FUNC() \
  TRACE(__func__)

#define TRACE_BEGIN_END_FUNC(t) yb::ScopedBeginEndFunc _begin_end_scope(__func__);

#define PLAIN_TRACE_TO(trace, message) \
  do { \
    if ((trace)) { \
      (trace)->Trace(__FILE__, __LINE__, (message)); \
    } \
  } while (0)

// Useful for debugging, where the collected trace will be printed
// regardless of the time it takes, or other print related flags.
#define PRINT_THIS_TRACE() \
  TRACE("Requesting to print this trace"); \
  do { \
    yb::Trace* _trace = Trace::CurrentTrace(); \
    if (_trace) { \
      _trace->set_must_print(true); \
    } \
  } while (0)

namespace yb {

struct TraceEntry;

// A trace for a request or other process. This supports collecting trace entries
// from a number of threads, and later dumping the results to a stream.
//
// Callers should generally not add trace messages directly using the public
// methods of this class. Rather, the TRACE(...) macros defined above should
// be used such that file/line numbers are automatically included, etc.
//
// This class is thread-safe.
class Trace : public RefCountedThreadSafe<Trace> {
 public:
  Trace();

  // Logs a message into the trace buffer.
  //
  // See strings::Substitute for details.
  //
  // N.B.: the file path passed here is not copied, so should be a static
  // constant (eg __FILE__).
  void SubstituteAndTrace(const char* file_path, int line_number,
                          CoarseTimePoint now, GStringPiece format,
                          const strings::internal::SubstituteArg& arg0,
                          const strings::internal::SubstituteArg& arg1 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg2 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg3 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg4 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg5 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg6 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg7 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg8 =
                            strings::internal::SubstituteArg::NoArg,
                          const strings::internal::SubstituteArg& arg9 =
                            strings::internal::SubstituteArg::NoArg);

  void SubstituteAndTrace(
      const char* file_path, int line_number, CoarseTimePoint now, GStringPiece format);

  // Dump the trace buffer to the given output stream.
  //
  // If 'include_time_deltas' is true, calculates and prints the difference between
  // successive trace messages.
  void Dump(std::ostream* out, bool include_time_deltas) const;
  void Dump(std::ostream* out, int32_t tracing_depth, bool include_time_deltas) const;

  // Dump the trace buffer as a string.
  std::string DumpToString(int32_t tracing_depth, bool include_time_deltas) const;
  std::string DumpToString(bool include_time_deltas) const {
    return DumpToString(0, include_time_deltas);
  }

  // Attaches the given trace which will get appended at the end when Dumping.
  void AddChildTrace(Trace* child_trace);

  // Return the current trace attached to this thread, if there is one.
  static Trace* CurrentTrace() {
    return threadlocal_trace_;
  }

  static scoped_refptr<Trace> NewTrace();
  static scoped_refptr<Trace> NewTraceForParent(Trace* parent);

  // Simple function to dump the current trace to stderr, if one is
  // available. This is meant for usage when debugging in gdb via
  // 'call yb::Trace::DumpCurrentTrace();'.
  static void DumpCurrentTrace();

  size_t ObjectSize() const { return sizeof(*this); }
  size_t DynamicMemoryUsage() const;

  bool must_print() const {
    std::lock_guard<simple_spinlock> l(lock_);
    return must_print_;
  }

  void set_must_print(bool flag) {
    std::lock_guard<simple_spinlock> l(lock_);
    must_print_ = flag;
  }

 private:
  friend class ScopedAdoptTrace;
  friend class RefCountedThreadSafe<Trace>;
  ~Trace();

  ThreadSafeArena* GetAndInitArena();

  // The current trace for this thread. Threads should only set this using
  // using ScopedAdoptTrace, which handles reference counting the underlying
  // object.
  static __thread Trace* threadlocal_trace_;

  // Allocate a new entry from the arena, with enough space to hold a
  // message of length 'len'.
  TraceEntry* NewEntry(size_t len, const char* file_path, int line_number, CoarseTimePoint now);

  // Add the entry to the linked list of entries.
  void AddEntry(TraceEntry* entry);

  std::atomic<ThreadSafeArena*> arena_ = {nullptr};

  // Lock protecting the entries linked list.
  mutable simple_spinlock lock_;
  // The head of the linked list of entries (allocated inside arena_)
  TraceEntry* entries_head_ = nullptr;
  // The tail of the linked list of entries (allocated inside arena_)
  TraceEntry* entries_tail_ = nullptr;

  int64_t trace_start_time_usec_ = 0;

  // A hint to request that the collected trace be printed.
  bool must_print_ = false;

  std::vector<scoped_refptr<Trace> > child_traces_;

  DISALLOW_COPY_AND_ASSIGN(Trace);
};

typedef scoped_refptr<Trace> TracePtr;

// Adopt a Trace object into the current thread for the duration
// of this object.
// This should only be used on the stack (and thus created and destroyed
// on the same thread)
class ScopedAdoptTrace {
 public:
  explicit ScopedAdoptTrace(Trace* t);
  ~ScopedAdoptTrace();

 private:
  DFAKE_MUTEX(ctor_dtor_);
  Trace* old_trace_;
  scoped_refptr<Trace> trace_;
  bool is_enabled_ = false;

  DISALLOW_COPY_AND_ASSIGN(ScopedAdoptTrace);
};

// Adds a Begin/End trace call in the constructor/destructor.
// This should only be used on the stack (and thus created and destroyed
// on the same thread).
class ScopedBeginEndFunc {
 public:
  explicit ScopedBeginEndFunc(std::string function) : function_name_(std::move(function)) {
    TRACE("Begin: $0", function_name_);
  }
  ~ScopedBeginEndFunc() {
    TRACE("End: $0", function_name_);
  }

 private:
  std::string function_name_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBeginEndFunc);
};

// PlainTrace could be used in simple cases when we trace only up to 20 entries with const message.
// So it does not allocate memory.
class PlainTrace {
 public:
  static const size_t kMaxEntries = 20;

  PlainTrace();

  void Trace(const char* file_path, int line_number, const char* message);
  void Dump(std::ostream* out, bool include_time_deltas) const;
  void Dump(std::ostream* out, int32_t tracing_depth, bool include_time_deltas) const;
  std::string DumpToString(int32_t tracing_depth, bool include_time_deltas) const;
  std::string DumpToString(bool include_time_deltas) const {
    return DumpToString(0, include_time_deltas);
  }

 private:
  class Entry {
   public:
    const char* file_path;
    int line_number;
    const char* message;
    CoarseTimePoint timestamp;

    void Dump(std::ostream* out) const;
  };

  mutable simple_spinlock mutex_;
  int64_t trace_start_time_usec_ = 0;
  size_t size_ = 0;
  Entry entries_[kMaxEntries];
};

} // namespace yb

#endif /* YB_UTIL_TRACE_H */
