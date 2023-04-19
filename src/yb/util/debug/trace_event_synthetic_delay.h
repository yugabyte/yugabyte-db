// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
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

// The synthetic delay framework makes it possible to dynamically inject
// arbitrary delays into into different parts of the codebase. This can be used,
// for instance, for testing various task scheduling algorithms.
//
// The delays are specified in terms of a target duration for a given block of
// code. If the code executes faster than the duration, the thread is made to
// sleep until the deadline is met.
//
// Code can be instrumented for delays with two sets of macros. First, for
// delays that should apply within a scope, use the following macro:
//
//   TRACE_EVENT_SYNTHETIC_DELAY("cc.LayerTreeHost.DrawAndSwap");
//
// For delaying operations that span multiple scopes, use:
//
//   TRACE_EVENT_SYNTHETIC_DELAY_BEGIN("cc.Scheduler.BeginMainFrame");
//   ...
//   TRACE_EVENT_SYNTHETIC_DELAY_END("cc.Scheduler.BeginMainFrame");
//
// Here BEGIN establishes the start time for the delay and END executes the
// delay based on the remaining time. If BEGIN is called multiple times in a
// row, END should be called a corresponding number of times. Only the last
// call to END will have an effect.
//
// Note that a single delay may begin on one thread and end on another. This
// implies that a single delay cannot not be applied in several threads at once.

#pragma once

#include "yb/gutil/atomicops.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/monotime.h"

// Apply a named delay in the current scope.
#define TRACE_EVENT_SYNTHETIC_DELAY(name)                                     \
  static AtomicWord INTERNAL_TRACE_EVENT_UID(impl_ptr) = 0;                   \
  trace_event_internal::ScopedSyntheticDelay INTERNAL_TRACE_EVENT_UID(delay)( \
      name, &INTERNAL_TRACE_EVENT_UID(impl_ptr));

// Begin a named delay, establishing its timing start point. May be called
// multiple times as long as the calls to TRACE_EVENT_SYNTHETIC_DELAY_END are
// balanced. Only the first call records the timing start point.
#define TRACE_EVENT_SYNTHETIC_DELAY_BEGIN(name)                          \
  do {                                                                   \
    static AtomicWord impl_ptr = 0;                                      \
    trace_event_internal::GetOrCreateDelay(name, &impl_ptr)->Begin();    \
  } while (false)

// End a named delay. The delay is applied only if this call matches the
// first corresponding call to TRACE_EVENT_SYNTHETIC_DELAY_BEGIN with the
// same delay.
#define TRACE_EVENT_SYNTHETIC_DELAY_END(name)                         \
  do {                                                                \
    static AtomicWord impl_ptr = 0;                                   \
    trace_event_internal::GetOrCreateDelay(name, &impl_ptr)->End();   \
  } while (false)

namespace yb {
namespace debug {

// Time source for computing delay durations. Used for testing.
class TRACE_EVENT_API_CLASS_EXPORT TraceEventSyntheticDelayClock {
 public:
  TraceEventSyntheticDelayClock();
  virtual ~TraceEventSyntheticDelayClock();
  virtual MonoTime Now() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TraceEventSyntheticDelayClock);
};

// Single delay point instance.
class TRACE_EVENT_API_CLASS_EXPORT TraceEventSyntheticDelay {
 public:
  enum Mode {
    STATIC,      // Apply the configured delay every time.
    ONE_SHOT,    // Apply the configured delay just once.
    ALTERNATING  // Apply the configured delay every other time.
  };

  // Returns an existing named delay instance or creates a new one with |name|.
  static TraceEventSyntheticDelay* Lookup(const std::string& name);

  void SetTargetDuration(const MonoDelta& target_duration);
  void SetMode(Mode mode);
  void SetClock(TraceEventSyntheticDelayClock* clock);

  // Begin the delay, establishing its timing start point. May be called
  // multiple times as long as the calls to End() are balanced. Only the first
  // call records the timing start point.
  void Begin();

  // End the delay. The delay is applied only if this call matches the first
  // corresponding call to Begin() with the same delay.
  void End();

  // Begin a parallel instance of the delay. Several parallel instances may be
  // active simultaneously and will complete independently. The computed end
  // time for the delay is stored in |out_end_time|, which should later be
  // passed to EndParallel().
  void BeginParallel(MonoTime* out_end_time);

  // End a previously started parallel delay. |end_time| is the delay end point
  // computed by BeginParallel().
  void EndParallel(const MonoTime& end_time);

 private:
  TraceEventSyntheticDelay();
  ~TraceEventSyntheticDelay();
  friend class TraceEventSyntheticDelayRegistry;

  void Initialize(const std::string& name,
                  TraceEventSyntheticDelayClock* clock);
  MonoTime CalculateEndTimeLocked(const MonoTime& start_time);
  void ApplyDelay(const MonoTime& end_time);

  Mutex lock_;
  Mode mode_;
  std::string name_;
  int begin_count_;
  int trigger_count_;
  MonoTime end_time_;
  MonoDelta target_duration_;
  TraceEventSyntheticDelayClock* clock_;

  DISALLOW_COPY_AND_ASSIGN(TraceEventSyntheticDelay);
};

// Set the target durations of all registered synthetic delay points to zero.
TRACE_EVENT_API_CLASS_EXPORT void ResetTraceEventSyntheticDelays();

}  // namespace debug
}  // namespace yb

namespace trace_event_internal {

// Helper class for scoped delays. Do not use directly.
class TRACE_EVENT_API_CLASS_EXPORT ScopedSyntheticDelay {
 public:
  explicit ScopedSyntheticDelay(const char* name,
                                AtomicWord* impl_ptr);
  ~ScopedSyntheticDelay();

 private:
  yb::debug::TraceEventSyntheticDelay* delay_impl_;
  yb::MonoTime end_time_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSyntheticDelay);
};

// Helper for registering delays. Do not use directly.
TRACE_EVENT_API_CLASS_EXPORT yb::debug::TraceEventSyntheticDelay*
    GetOrCreateDelay(const char* name, AtomicWord* impl_ptr);

}  // namespace trace_event_internal
