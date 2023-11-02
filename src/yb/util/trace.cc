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

#include "yb/util/trace.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <boost/range/adaptor/indirected.hpp>

#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"

#include "yb/util/flags.h"
#include "yb/util/random.h"
#include "yb/util/threadlocal.h"
#include "yb/util/memory/arena.h"
#include "yb/util/memory/memory.h"
#include "yb/util/object_pool.h"
#include "yb/util/size_literals.h"

using std::vector;
using std::string;

DEFINE_RUNTIME_bool(enable_tracing, false, "Flag to enable/disable tracing across the code.");
TAG_FLAG(enable_tracing, advanced);

DEFINE_RUNTIME_uint32(trace_max_dump_size, 30000,
    "The max size of a trace dumped to the logs in Trace::DumpToLogInfo. 0 means unbounded.");
TAG_FLAG(trace_max_dump_size, advanced);

DEFINE_RUNTIME_int32(sampled_trace_1_in_n, 1000,
    "Flag to enable/disable sampled tracing. 0 disables.");
TAG_FLAG(sampled_trace_1_in_n, advanced);

DEFINE_RUNTIME_bool(use_monotime_for_traces, false,
    "Flag to enable use of MonoTime::Now() instead of "
    "CoarseMonoClock::Now(). CoarseMonoClock is much cheaper so it is better to use it. However "
    "if we need more accurate sub-millisecond level breakdown, we could use MonoTime.");
TAG_FLAG(use_monotime_for_traces, advanced);

DEFINE_RUNTIME_int32(tracing_level, 0,
    "verbosity levels (like --v) up to which tracing is enabled.");
TAG_FLAG(tracing_level, advanced);

DEFINE_RUNTIME_int32(print_nesting_levels, 5,
    "controls the depth of the child traces to be printed.");
TAG_FLAG(print_nesting_levels, advanced);

namespace yb {

using strings::internal::SubstituteArg;

__thread Trace* Trace::threadlocal_trace_;

namespace {

const char* kNestedChildPrefix = "..  ";

// Get the part of filepath after the last path separator.
// (Doesn't modify filepath, contrary to basename() in libgen.h.)
// Borrowed from glog.
const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
  return base ? (base + 1) : filepath;
}

template <class Children>
void DumpChildren(
    std::ostream* out, int32_t tracing_depth, bool include_time_deltas, const Children* children) {
  if (tracing_depth > GetAtomicFlag(&FLAGS_print_nesting_levels)) {
    return;
  }
  for (auto &child_trace : *children) {
    for (int i = 0; i < tracing_depth; i++) {
      *out << kNestedChildPrefix;
    }
    *out << "Related trace:" << std::endl;
    *out << (child_trace ? child_trace->DumpToString(tracing_depth, include_time_deltas)
                         : "Not collected");
  }
}

void DumpChildren(
    std::ostream* out, int32_t tracing_depth, bool include_time_deltas, std::nullptr_t children) {}

template <class Entries>
void DumpEntries(
    std::ostream* out,
    int32_t tracing_depth,
    bool include_time_deltas,
    int64_t start,
    const Entries& entries) {
  if (entries.empty()) {
    return;
  }

  auto time_usec = MonoDelta(entries.begin()->timestamp.time_since_epoch()).ToMicroseconds();
  const int64_t time_correction_usec = start - time_usec;
  int64_t prev_usecs = time_usec;
  for (const auto& e : entries) {
    time_usec = MonoDelta(e.timestamp.time_since_epoch()).ToMicroseconds();
    const int64_t usecs_since_prev = time_usec - prev_usecs;
    prev_usecs = time_usec;

    const auto absolute_time_usec = time_usec + time_correction_usec;
    const time_t secs_since_epoch = absolute_time_usec / 1000000;
    const int usecs = absolute_time_usec % 1000000;
    struct tm tm_time;
    localtime_r(&secs_since_epoch, &tm_time);

    for (int i = 0; i < tracing_depth; i++) {
      *out << kNestedChildPrefix;
    }
    // Log format borrowed from glog/logging.cc
    using std::setw;
    out->fill('0');

    *out << setw(2) << (1 + tm_time.tm_mon)
         << setw(2) << tm_time.tm_mday
         << ' '
         << setw(2) << tm_time.tm_hour  << ':'
         << setw(2) << tm_time.tm_min   << ':'
         << setw(2) << tm_time.tm_sec   << '.'
         << setw(6) << usecs << ' ';
    if (include_time_deltas) {
      out->fill(' ');
      *out << "(+" << setw(6) << usecs_since_prev << "us) ";
    }
    e.Dump(out);
    *out << std::endl;
  }
}

template <class Entries, class Children>
void DoDump(
    std::ostream* out,
    int32_t tracing_depth,
    bool include_time_deltas,
    int64_t start,
    const Entries& entries,
    Children children) {
  // Save original flags.
  std::ios::fmtflags save_flags(out->flags());

  DumpEntries(out, tracing_depth, include_time_deltas, start, entries);
  DumpChildren(out, tracing_depth + 1, include_time_deltas, children);

  // Restore stream flags.
  out->flags(save_flags);
}

std::once_flag init_get_current_micros_fast_flag;
int64_t initial_micros_offset;

void InitGetCurrentMicrosFast() {
  auto before = CoarseMonoClock::Now();
  initial_micros_offset = GetCurrentTimeMicros();
  auto after = CoarseMonoClock::Now();
  auto mid = MonoDelta(after.time_since_epoch() + before.time_since_epoch()).ToMicroseconds() / 2;
  initial_micros_offset -= mid;
}

int64_t GetCurrentMicrosFast(CoarseTimePoint now) {
  std::call_once(init_get_current_micros_fast_flag, InitGetCurrentMicrosFast);
  return initial_micros_offset + MonoDelta(now.time_since_epoch()).ToMicroseconds();
}

} // namespace

ScopedAdoptTrace::ScopedAdoptTrace(Trace* t)
    : old_trace_(Trace::threadlocal_trace_) {
  trace_ = t;
  Trace::threadlocal_trace_ = t;
  DFAKE_SCOPED_LOCK_THREAD_LOCKED(ctor_dtor_);
}

ScopedAdoptTrace::~ScopedAdoptTrace() {
  Trace::threadlocal_trace_ = old_trace_;
  // It's critical that we Release() the reference count on 't' only
  // after we've unset the thread-local variable. Otherwise, we can hit
  // a nasty interaction with tcmalloc contention profiling. Consider
  // the following sequence:
  //
  //   1. threadlocal_trace_ has refcount = 1
  //   2. we call threadlocal_trace_->Release() which decrements refcount to 0
  //   3. this calls 'delete' on the Trace object
  //   3a. this calls tcmalloc free() on the Trace and various sub-objects
  //   3b. the free() calls may end up experiencing contention in tcmalloc
  //   3c. we try to account the contention in threadlocal_trace_'s TraceMetrics,
  //       but it has already been freed.
  //
  // In the best case, we just scribble into some free tcmalloc memory. In the
  // worst case, tcmalloc would have already re-used this memory for a new
  // allocation on another thread, and we end up overwriting someone else's memory.
  //
  // Waiting to Release() only after 'unpublishing' the trace solves this.
  trace_.reset();
  DFAKE_SCOPED_LOCK_THREAD_LOCKED(ctor_dtor_);
}

// Struct which precedes each entry in the trace.
struct TraceEntry {
  CoarseTimePoint timestamp;

  // The source file and line number which generated the trace message.
  const char* file_path;
  int line_number;

  size_t message_len;
  TraceEntry* next;
  char message[0];

  void Dump(std::ostream* out) const {
    *out << const_basename(file_path) << ':' << line_number
         << "] ";
    out->write(message, message_len);
  }
};

Trace::Trace() {
}

ThreadSafeObjectPool<ThreadSafeArena>& ArenaPool() {
  static ThreadSafeObjectPool<ThreadSafeArena> result([] {
    return new ThreadSafeArena(8_KB, 128_KB);
  });
  return result;
}

Trace::~Trace() {
  auto* arena = arena_.load(std::memory_order_acquire);
  if (arena) {
    arena->Reset(ResetMode::kKeepLast);
    ArenaPool().Release(arena);
  }
}

ThreadSafeArena* Trace::GetAndInitArena() {
  auto* arena = arena_.load(std::memory_order_acquire);
  if (arena == nullptr) {
    arena = ArenaPool().Take();
    ThreadSafeArena* existing_arena = nullptr;
    if (arena_.compare_exchange_strong(existing_arena, arena, std::memory_order_release)) {
      return arena;
    } else {
      ArenaPool().Release(arena);
      return existing_arena;
    }
  }
  return arena;
}

scoped_refptr<Trace> Trace::MaybeGetNewTrace() {
  if (GetAtomicFlag(&FLAGS_enable_tracing)) {
    return scoped_refptr<Trace>(new Trace());
  }
  const int32_t sampling_freq = GetAtomicFlag(&FLAGS_sampled_trace_1_in_n);
  if (sampling_freq <= 0) {
    VLOG(2) << "Sampled tracing returns nullptr";
    return nullptr;
  }

  BLOCK_STATIC_THREAD_LOCAL(yb::Random, rng_ptr, static_cast<uint32_t>(GetCurrentTimeMicros()));
  auto ret = scoped_refptr<Trace>(rng_ptr->OneIn(sampling_freq) ? new Trace() : nullptr);
  VLOG(2) << "Sampled tracing returns " << (ret ? "non-null" : "nullptr");
  if (ret) {
    TRACE_TO(ret.get(), "Sampled trace created probabilistically");
  }
  return ret;
}

scoped_refptr<Trace>  Trace::MaybeGetNewTraceForParent(Trace* parent) {
  if (parent) {
    scoped_refptr<Trace> trace(new Trace);
    parent->AddChildTrace(trace.get());
    return trace;
  }
  return MaybeGetNewTrace();
}

void Trace::SubstituteAndTrace(
    const char* file_path, int line_number, CoarseTimePoint now, GStringPiece format) {
  auto msg_len = format.size();
  DCHECK_NE(msg_len, 0) << "Bad format specification";
  TraceEntry* entry = NewEntry(msg_len, file_path, line_number, now);
  if (entry == nullptr) return;
  memcpy(entry->message, format.data(), msg_len);
  AddEntry(entry);
}

void Trace::SubstituteAndTrace(const char* file_path,
                               int line_number,
                               CoarseTimePoint now,
                               GStringPiece format,
                               const SubstituteArg& arg0, const SubstituteArg& arg1,
                               const SubstituteArg& arg2, const SubstituteArg& arg3,
                               const SubstituteArg& arg4, const SubstituteArg& arg5,
                               const SubstituteArg& arg6, const SubstituteArg& arg7,
                               const SubstituteArg& arg8, const SubstituteArg& arg9) {
  const SubstituteArg* const args_array[] = {
    &arg0, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7, &arg8, &arg9, nullptr
  };

  int msg_len = strings::internal::SubstitutedSize(format, args_array);
  DCHECK_NE(msg_len, 0) << "Bad format specification";
  TraceEntry* entry = NewEntry(msg_len, file_path, line_number, now);
  if (entry == nullptr) return;
  SubstituteToBuffer(format, args_array, entry->message);
  AddEntry(entry);
}

TraceEntry* Trace::NewEntry(
    size_t msg_len, const char* file_path, int line_number, CoarseTimePoint now) {
  auto* arena = GetAndInitArena();
  size_t size = offsetof(TraceEntry, message) + msg_len;
  void* dst = arena->AllocateBytesAligned(size, alignof(TraceEntry));
  if (dst == nullptr) {
    LOG(ERROR) << "NewEntry(msg_len, " << file_path << ", " << line_number
        << ") received nullptr from AllocateBytes.\n So far:" << DumpToString(true);
    return nullptr;
  }
  TraceEntry* entry = new (dst) TraceEntry;
  entry->timestamp = now;
  entry->message_len = msg_len;
  entry->file_path = file_path;
  entry->line_number = line_number;
  return entry;
}

void Trace::AddEntry(TraceEntry* entry) {
  std::lock_guard l(lock_);
  entry->next = nullptr;

  if (entries_tail_ != nullptr) {
    entries_tail_->next = entry;
  } else {
    DCHECK(entries_head_ == nullptr);
    entries_head_ = entry;
    trace_start_time_usec_ = GetCurrentMicrosFast(entry->timestamp);
  }
  entries_tail_ = entry;
}

void Trace::Dump(std::ostream *out, bool include_time_deltas) const {
  Dump(out, 0, include_time_deltas);
}

void Trace::DumpToLogInfo(bool include_time_deltas) const {
  auto trace_buffer = DumpToString(include_time_deltas);
  const size_t trace_max_dump_size = GetAtomicFlag(&FLAGS_trace_max_dump_size);
  const size_t kMaxDumpSize =
      (trace_max_dump_size > 0 ? trace_max_dump_size : std::numeric_limits<uint32_t>::max());
  size_t start = 0;
  size_t max_to_print = std::min(trace_buffer.size(), kMaxDumpSize);
  const size_t kMaxLogMessageLen = google::LogMessage::kMaxLogMessageLen;
  const string kContinuationMarker("\ntrace continues ...");
  // An upper bound on the overhead due to printing the file name/timestamp etc + continuation
  // marker.
  const size_t kMaxOverhead = 100;
  bool skip_newline = false;
  do {
    size_t length_to_print = max_to_print - start;
    bool has_more = false;
    if (length_to_print > kMaxLogMessageLen) {
      // Try to split a line by \n starting a search from the end of the printable interval till
      // the middle of that interval to not shrink too much.
      auto last_end_of_line_pos = std::string_view(
                                      trace_buffer.c_str() + start + (kMaxLogMessageLen / 2),
                                      (kMaxLogMessageLen / 2) - kMaxOverhead)
                                      .rfind('\n');
      // If we have a really long line, we will just split it.
      if (last_end_of_line_pos == string::npos) {
        length_to_print = kMaxLogMessageLen - kMaxOverhead;
        skip_newline = false;
      } else {
        length_to_print = (kMaxLogMessageLen / 2) + last_end_of_line_pos;
        skip_newline = true;
      }
      has_more = true;
    }
    LOG(INFO) << std::string_view(trace_buffer.c_str() + start, length_to_print)
              << (has_more ? kContinuationMarker : "");
    start += length_to_print;
    // Skip the newline character which would otherwise be at the begining of the next part.
    start += static_cast<size_t>(skip_newline);
  } while (start < max_to_print);
}

void Trace::Dump(std::ostream* out, int32_t tracing_depth, bool include_time_deltas) const {
  // Gather a copy of the list of entries under the lock. This is fast
  // enough that we aren't worried about stalling concurrent tracers
  // (whereas doing the logging itself while holding the lock might be
  // too slow, if the output stream is a file, for example).
  vector<TraceEntry*> entries;
  vector<scoped_refptr<Trace> > child_traces;
  decltype(trace_start_time_usec_) trace_start_time_usec;
  {
    std::lock_guard l(lock_);
    for (TraceEntry* cur = entries_head_;
        cur != nullptr;
        cur = cur->next) {
      entries.push_back(cur);
    }

    child_traces = child_traces_;
    trace_start_time_usec = trace_start_time_usec_;
  }

  DoDump(
      out, tracing_depth, include_time_deltas, trace_start_time_usec,
      entries | boost::adaptors::indirected, &child_traces);
}

string Trace::DumpToString(int32_t tracing_depth, bool include_time_deltas) const {
  std::stringstream s;
  Dump(&s, tracing_depth, include_time_deltas);
  return s.str();
}

void Trace::DumpCurrentTrace() {
  Trace* t = CurrentTrace();
  if (t == nullptr) {
    LOG(INFO) << "No trace is currently active.";
    return;
  }
  t->Dump(&std::cerr, true);
}

void Trace::AddChildTrace(Trace* child_trace) {
  CHECK_NOTNULL(child_trace);
  {
    std::lock_guard l(lock_);
    scoped_refptr<Trace> ptr(child_trace);
    child_traces_.push_back(ptr);
  }
  CHECK(!child_trace->HasOneRef());
}

size_t Trace::DynamicMemoryUsage() const {
  auto arena = arena_.load();
  return arena ? arena->memory_footprint() : 0;
}

PlainTrace::PlainTrace() {
}

void PlainTrace::Trace(const char *file_path, int line_number, const char *message) {
  auto timestamp = CoarseMonoClock::Now();
  {
    std::lock_guard lock(mutex_);
    if (size_ < kMaxEntries) {
      if (size_ == 0) {
        trace_start_time_usec_ = GetCurrentMicrosFast(timestamp);
      }
      entries_[size_] = {file_path, line_number, message, timestamp};
      ++size_;
    }
  }
}

void PlainTrace::Dump(std::ostream *out, bool include_time_deltas) const {
  Dump(out, 0, include_time_deltas);
}

void PlainTrace::Dump(std::ostream* out, int32_t tracing_depth, bool include_time_deltas) const {
  size_t size;
  decltype(trace_start_time_usec_) trace_start_time_usec;
  {
    std::lock_guard lock(mutex_);
    size = size_;
    trace_start_time_usec = trace_start_time_usec_;
  }
  auto entries = boost::make_iterator_range(entries_, entries_ + size);
  DoDump(
      out, tracing_depth, include_time_deltas, trace_start_time_usec, entries,
      /* children */ nullptr);
}

std::string PlainTrace::DumpToString(int32_t tracing_depth, bool include_time_deltas) const {
  std::stringstream s;
  Dump(&s, tracing_depth, include_time_deltas);
  return s.str();
}

void PlainTrace::Entry::Dump(std::ostream *out) const {
  *out << const_basename(file_path) << ':' << line_number << "] " << message;
}

} // namespace yb
