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

#include "yb/util/trace.h"

#include <iomanip>
#include <ios>
#include <iostream>
#include <strstream>
#include <string>
#include <vector>

#include <boost/range/iterator_range.hpp>
#include <boost/range/adaptor/indirected.hpp>

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"

#include "yb/util/memory/arena.h"
#include "yb/util/memory/memory.h"

namespace yb {

using strings::internal::SubstituteArg;

__thread Trace* Trace::threadlocal_trace_;

namespace {

// Get the part of filepath after the last path separator.
// (Doesn't modify filepath, contrary to basename() in libgen.h.)
// Borrowed from glog.
const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
  return base ? (base + 1) : filepath;
}

template<class Children>
void DumpChildren(std::ostream* out, bool include_time_deltas, const Children* children) {
  for (auto &child_trace : *children) {
    *out << "Related trace:" << std::endl;
    *out << child_trace->DumpToString(include_time_deltas);
  }
}

void DumpChildren(std::ostream* out, bool include_time_deltas, std::nullptr_t children) {
}

template<class Entries>
void DumpEntries(std::ostream* out,
                 bool include_time_deltas,
                 int64_t start,
                 const Entries& entries) {
  if (entries.empty()) {
    return;
  }

  auto time_usec = entries.begin()->timestamp.GetDeltaSinceMin().ToMicroseconds();
  const int64_t time_correction_usec = start - time_usec;
  int64_t prev_usecs = time_usec;
  for (const auto& e : entries) {
    time_usec = e.timestamp.GetDeltaSinceMin().ToMicroseconds();
    const int64_t usecs_since_prev = time_usec - prev_usecs;
    prev_usecs = time_usec;

    const auto absolute_time_usec = time_usec + time_correction_usec;
    const time_t secs_since_epoch = absolute_time_usec / 1000000;
    const int usecs = absolute_time_usec % 1000000;
    struct tm tm_time;
    localtime_r(&secs_since_epoch, &tm_time);

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

template<class Entries, class Children>
void DoDump(std::ostream* out,
            bool include_time_deltas,
            int64_t start,
            const Entries& entries,
            Children children) {
  // Save original flags.
  std::ios::fmtflags save_flags(out->flags());

  DumpEntries(out, include_time_deltas, start, entries);
  DumpChildren(out, include_time_deltas, children);

  // Restore stream flags.
  out->flags(save_flags);
}

std::once_flag init_get_current_micros_fast_flag;
int64_t initial_micros_offset;

void InitGetCurrentMicrosFast() {
  auto before = MonoTime::FineNow();
  initial_micros_offset = GetCurrentTimeMicros();
  auto after = MonoTime::FineNow();
  auto mid = after.GetDeltaSinceMin().ToMicroseconds();
  mid += before.GetDeltaSinceMin().ToMicroseconds();
  mid /= 2;
  initial_micros_offset -= mid;
}

int64_t GetCurrentMicrosFast() {
  std::call_once(init_get_current_micros_fast_flag, InitGetCurrentMicrosFast);
  auto now = MonoTime::FineNow();
  return initial_micros_offset + now.GetDeltaSinceMin().ToMicroseconds();
}

} // namespace

ScopedAdoptTrace::ScopedAdoptTrace(Trace* t)
    : old_trace_(Trace::threadlocal_trace_), trace_(t) {
  CHECK(!t || !t->HasOneRef());
  Trace::threadlocal_trace_ = t;
  DFAKE_SCOPED_LOCK_THREAD_LOCKED(ctor_dtor_);
}

ScopedAdoptTrace::~ScopedAdoptTrace() {
  Trace::threadlocal_trace_ = old_trace_;
  DFAKE_SCOPED_LOCK_THREAD_LOCKED(ctor_dtor_);
}

// Struct which precedes each entry in the trace.
struct TraceEntry {
  MonoTime timestamp;

  // The source file and line number which generated the trace message.
  const char* file_path;
  int line_number;

  uint32_t message_len;
  TraceEntry* next;

  // The actual trace message follows the entry header.
  const char* message() const {
    return EndOfObject(this);
  }

  char* message() {
    return EndOfObject(this);
  }

  void Dump(std::ostream* out) const {
    *out << const_basename(file_path) << ':' << line_number
         << "] ";
    out->write(message(), message_len);
  }
};

Trace::Trace()
    : arena_(new ThreadSafeArena(1024, 128*1024)),
      entries_head_(nullptr),
      entries_tail_(nullptr) {
}

Trace::~Trace() {
}

void Trace::SubstituteAndTrace(const char* file_path,
                               int line_number,
                               StringPiece format,
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
  TraceEntry* entry = NewEntry(msg_len, file_path, line_number);
  if (entry == nullptr) return;
  SubstituteToBuffer(format, args_array, entry->message());
  AddEntry(entry);
}

TraceEntry* Trace::NewEntry(int msg_len, const char* file_path, int line_number) {
  int size = sizeof(TraceEntry) + msg_len;
  uint8_t* dst = static_cast<uint8_t*>(arena_->AllocateBytesAligned(size, alignof(TraceEntry)));
  if (dst == nullptr) {
    LOG(ERROR) << "NewEntry(msg_len, " << file_path << ", " << line_number
        << ") received nullptr from AllocateBytes.\n So far:" << DumpToString(true);
    return nullptr;
  }
  TraceEntry* entry = new (dst) TraceEntry;
  entry->timestamp = MonoTime::Now(MonoTime::FINE);
  entry->message_len = msg_len;
  entry->file_path = file_path;
  entry->line_number = line_number;
  return entry;
}

void Trace::AddEntry(TraceEntry* entry) {
  std::lock_guard<simple_spinlock> l(lock_);
  entry->next = nullptr;

  if (entries_tail_ != nullptr) {
    entries_tail_->next = entry;
  } else {
    DCHECK(entries_head_ == nullptr);
    entries_head_ = entry;
    trace_start_time_usec_ = GetCurrentMicrosFast();
  }
  entries_tail_ = entry;
}

void Trace::Dump(std::ostream *out, bool include_time_deltas) const {
  // Gather a copy of the list of entries under the lock. This is fast
  // enough that we aren't worried about stalling concurrent tracers
  // (whereas doing the logging itself while holding the lock might be
  // too slow, if the output stream is a file, for example).
  vector<TraceEntry*> entries;
  vector<scoped_refptr<Trace> > child_traces;
  decltype(trace_start_time_usec_) trace_start_time_usec;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    for (TraceEntry* cur = entries_head_;
        cur != nullptr;
        cur = cur->next) {
      entries.push_back(cur);
    }

    child_traces = child_traces_;
    trace_start_time_usec = trace_start_time_usec_;
  }

  DoDump(out,
         include_time_deltas,
         trace_start_time_usec,
         entries | boost::adaptors::indirected,
         &child_traces);
}

string Trace::DumpToString(bool include_time_deltas) const {
  std::stringstream s;
  Dump(&s, include_time_deltas);
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
    std::lock_guard<simple_spinlock> l(lock_);
    scoped_refptr<Trace> ptr(child_trace);
    child_traces_.push_back(ptr);
  }
  CHECK(!child_trace->HasOneRef());
}

PlainTrace::PlainTrace() {
}

void PlainTrace::Trace(const char *file_path, int line_number, const char *message) {
  auto timestamp = MonoTime::Now(MonoTime::FINE);
  {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    if (size_ < kMaxEntries) {
      if (size_ == 0) {
        trace_start_time_usec_ = GetCurrentMicrosFast();
      }
      entries_[size_] = {file_path, line_number, message, timestamp};
      ++size_;
    }
  }
}

void PlainTrace::Dump(std::ostream *out, bool include_time_deltas) const {
  size_t size;
  decltype(trace_start_time_usec_) trace_start_time_usec;
  {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    size = size_;
    trace_start_time_usec = trace_start_time_usec_;
  }
  auto entries = boost::make_iterator_range(entries_, entries_ + size);
  DoDump(out, include_time_deltas, trace_start_time_usec, entries, /* children */ nullptr);
}

std::string PlainTrace::DumpToString(bool include_time_deltas) const {
  std::stringstream s;
  Dump(&s, include_time_deltas);
  return s.str();
}

void PlainTrace::Entry::Dump(std::ostream *out) const {
  *out << const_basename(file_path) << ':' << line_number << "] " << message;
}

} // namespace yb
