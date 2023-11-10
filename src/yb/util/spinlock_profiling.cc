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

#include "yb/util/spinlock_profiling.h"

#include <functional>
#include <string>
#include <vector>

#include "yb/util/logging.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/once.h"
#include "yb/gutil/spinlock.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/sysinfo.h"

#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/slice.h"
#include "yb/util/stack_trace.h"
#include "yb/util/striped64.h"
#include "yb/util/trace.h"

DEFINE_UNKNOWN_int32(lock_contention_trace_threshold_cycles,
             2000000, // 2M cycles should be about 1ms
             "If acquiring a spinlock takes more than this number of "
             "cycles, and a Trace is currently active, then the current "
             "stack trace is logged to the trace buffer.");
TAG_FLAG(lock_contention_trace_threshold_cycles, hidden);

METRIC_DEFINE_gauge_uint64(server, spinlock_contention_time,
    "Spinlock Contention Time", yb::MetricUnit::kMicroseconds,
    "Amount of time consumed by contention on internal spinlocks since the server "
    "started. If this increases rapidly, it may indicate a performance issue in YB "
    "internals triggered by a particular workload and warrant investigation.",
    yb::EXPOSE_AS_COUNTER);

using base::SpinLockHolder;

namespace yb {

static const double kMicrosPerSecond = 1000000.0;

static LongAdder* g_contended_cycles = nullptr;

namespace {

// Implements a very simple linear-probing hashtable of stack traces with
// a fixed number of entries.
//
// Threads experiencing contention record their stacks into this hashtable,
// or increment an already-existing entry. Each entry has its own lock,
// but we can "skip" an entry under contention, and spread out a single stack
// into multiple buckets if necessary.
//
// A thread collecting a profile collects stack traces out of the hash table
// and resets the counts to 0 as they are collected.
class ContentionStacks {
 public:
  ContentionStacks()
    : dropped_samples_(0) {
  }

  // Add a stack trace to the table.
  void AddStack(const StackTrace& s, int64_t cycles);

  // Flush stacks from the buffer to 'out'. See the docs for FlushSynchronizationProfile()
  // in spinlock_profiling.h for details on format.
  //
  // On return, guarantees that any stack traces that were present at the beginning of
  // the call have been flushed. However, new stacks can be added concurrently with this call.
  void Flush(std::stringstream* out, int64_t* dropped);

 private:

  // Collect the next sample from the underlying buffer, and set it back to 0 count
  // (thus marking it as "empty").
  //
  // 'iterator' serves as a way to keep track of the current position in the buffer.
  // Callers should initially set it to 0, and then pass the same pointer to each
  // call to CollectSample. This serves to loop through the collected samples.
  bool CollectSample(uint64_t* iterator, StackTrace* s, int64_t* trip_count, int64_t* cycles);

  // Hashtable entry.
  struct Entry {
    Entry() : trip_count(0),
              cycle_count(0) {
    }

    // Protects all other entries.
    base::SpinLock lock;

    // The number of times we've experienced contention with a stack trace equal
    // to 'trace'.
    //
    // If this is 0, then the entry is "unclaimed" and the other fields are not
    // considered valid.
    int64_t trip_count;

    // The total number of cycles spent waiting at this stack trace.
    int64_t cycle_count;

    // A cached hashcode of the trace.
    uint64_t hash;

    // The actual stack trace.
    StackTrace trace;
  };

  enum {
    kNumEntries = 1024,
    kNumLinearProbeAttempts = 4
  };
  Entry entries_[kNumEntries];

  // The number of samples which were dropped due to contention on this structure or
  // due to the hashtable being too full.
  AtomicInt<int64_t> dropped_samples_;
};

Atomic32 g_profiling_enabled = 0;
ContentionStacks* g_contention_stacks = nullptr;

void ContentionStacks::AddStack(const StackTrace& s, int64_t cycles) {
  uint64_t hash = s.HashCode();

  // Linear probe up to 4 attempts before giving up
  for (int i = 0; i < kNumLinearProbeAttempts; i++) {
    Entry* e = &entries_[(hash + i) % kNumEntries];
    if (!e->lock.TryLock()) {
      // If we fail to lock it, we can safely just use a different slot.
      // It's OK if a single stack shows up multiple times, because pprof
      // aggregates them in the end anyway.
      continue;
    }

    if (e->trip_count == 0) {
      // It's an un-claimed slot. Claim it.
      e->hash = hash;
      e->trace = s;
    } else if (e->hash != hash || e->trace != s) {
      // It's claimed by a different stack trace.
      e->lock.Unlock();
      continue;
    }

    // Contribute to the stats for this stack.
    e->cycle_count += cycles;
    e->trip_count++;
    e->lock.Unlock();
    return;
  }

  // If we failed to find a matching hashtable slot, or we hit lock contention
  // trying to record our sample, add it to the dropped sample count.
  dropped_samples_.Increment();
}

void ContentionStacks::Flush(std::stringstream* out, int64_t* dropped) {
  uint64_t iterator = 0;
  StackTrace t;
  int64_t cycles;
  int64_t count;
  *out << "Format: Cycles\tCount @ Call Stack" << std::endl;
  while (g_contention_stacks->CollectSample(&iterator, &t, &count, &cycles)) {
    *out << cycles << "\t" << count
         << " @ " << t.ToHexString(StackTrace::NO_FIX_CALLER_ADDRESSES)
         << "\n" << t.Symbolize()
         << "\n-----------"
         << std::endl;
  }

  *dropped += dropped_samples_.Exchange(0);
}

bool ContentionStacks::CollectSample(uint64_t* iterator, StackTrace* s, int64_t* trip_count,
                                     int64_t* cycles) {
  while (*iterator < kNumEntries) {
    Entry* e = &entries_[(*iterator)++];
    SpinLockHolder l(&e->lock);
    if (e->trip_count == 0) continue;

    *trip_count = e->trip_count;
    *cycles = e->cycle_count;
    *s = e->trace;

    e->trip_count = 0;
    e->cycle_count = 0;
    return true;
  }

  // Looped through the whole array and found nothing.
  return false;
}

// Disable TSAN on this function.
// https://yugabyte.atlassian.net/browse/ENG-354
ATTRIBUTE_NO_SANITIZE_THREAD
void SubmitSpinLockProfileData(const void *contendedlock, int64 wait_cycles) {
  bool profiling_enabled = base::subtle::Acquire_Load(&g_profiling_enabled);
  bool long_wait_time = wait_cycles > FLAGS_lock_contention_trace_threshold_cycles;
  // Short circuit this function quickly in the common case.
  if (PREDICT_TRUE(!profiling_enabled && !long_wait_time)) {
    return;
  }

  static __thread bool in_func = false;
  if (in_func) return; // non-re-entrant
  in_func = true;

  StackTrace stack;
  stack.Collect();

  if (profiling_enabled) {
    DCHECK_NOTNULL(g_contention_stacks)->AddStack(stack, wait_cycles);
  }

  if (PREDICT_FALSE(long_wait_time)) {
    Trace* t = Trace::CurrentTrace();
    if (t) {
      double seconds = static_cast<double>(wait_cycles) / base::CyclesPerSecond();
      char backtrace_buffer[1024];
      stack.StringifyToHex(backtrace_buffer, arraysize(backtrace_buffer));
      TRACE_TO(t, "Waited $0 on lock $1. stack: $2",
               HumanReadableElapsedTime::ToShortString(seconds), contendedlock,
               backtrace_buffer);
    }
  }

  LongAdder* la = reinterpret_cast<LongAdder*>(
      base::subtle::Acquire_Load(reinterpret_cast<AtomicWord*>(&g_contended_cycles)));
  if (la) {
    la->IncrementBy(wait_cycles);
  }

  in_func = false;
}

void DoInit() {
  base::subtle::Release_Store(reinterpret_cast<AtomicWord*>(&g_contention_stacks),
                              reinterpret_cast<uintptr_t>(new ContentionStacks()));
  base::subtle::Release_Store(reinterpret_cast<AtomicWord*>(&g_contended_cycles),
                              reinterpret_cast<uintptr_t>(new LongAdder()));
}

} // anonymous namespace

void InitSpinLockContentionProfiling() {
  static GoogleOnceType once = GOOGLE_ONCE_INIT;
  GoogleOnceInit(&once, DoInit);
}


void RegisterSpinLockContentionMetrics(const scoped_refptr<MetricEntity>& entity) {
  InitSpinLockContentionProfiling();
  entity->NeverRetire(
      METRIC_spinlock_contention_time.InstantiateFunctionGauge(
          entity, Bind(&GetSpinLockContentionMicros)));

}

uint64_t GetSpinLockContentionMicros() {
  int64_t wait_cycles = DCHECK_NOTNULL(g_contended_cycles)->Value();
  double micros = static_cast<double>(wait_cycles) / base::CyclesPerSecond()
    * kMicrosPerSecond;
  return implicit_cast<int64_t>(micros);
}

void StartSynchronizationProfiling() {
  InitSpinLockContentionProfiling();
  base::subtle::Barrier_AtomicIncrement(&g_profiling_enabled, 1);
}

void FlushSynchronizationProfile(std::stringstream* out,
                                 int64_t* drop_count) {
  CHECK_NOTNULL(g_contention_stacks)->Flush(out, drop_count);
}

void StopSynchronizationProfiling() {
  InitSpinLockContentionProfiling();
  CHECK_GE(base::subtle::Barrier_AtomicIncrement(&g_profiling_enabled, -1), 0);
}

} // namespace yb

// The hook expected by gutil is in the gutil namespace. Simply forward into the
// yb namespace so we don't need to qualify everything.
namespace gutil {
void SubmitSpinLockProfileData(const void *contendedlock, int64 wait_cycles) {
  yb::SubmitSpinLockProfileData(contendedlock, wait_cycles);
}
} // namespace gutil
