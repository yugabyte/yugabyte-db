// Copyright 2012 Google Inc. All Rights Reserved.
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
#pragma once

#include <sys/resource.h>
#include <sys/time.h>

#include <string>

#if defined(__APPLE__)
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

#include "yb/gutil/once.h"
#else
#include <time.h>
#endif  // defined(__APPLE__)

#include "yb/gutil/integral_types.h"


typedef double WallTime;

// Append result to a supplied string.
// If an error occurs during conversion 'dst' is not modified.
void StringAppendStrftime(std::string* dst,
                          const char* format,
                          time_t when,
                          bool local);

void StringAppendStrftime(std::string* dst,
                          const char* format,
                          const struct tm* tm);

// Return the local time as a string suitable for user display.
std::string LocalTimeAsString();

// Similar to the WallTime_Parse, but it takes a boolean flag local as
// argument specifying if the time_spec is in local time or UTC
// time. If local is set to true, the same exact result as
// WallTime_Parse is returned.
bool WallTime_Parse_Timezone(const char* time_spec,
                             const char* format,
                             const struct tm* default_time,
                             bool local,
                             WallTime* result);

// Return current time in seconds as a WallTime.
WallTime WallTime_Now();

typedef int64 MicrosecondsInt64;

// Get User & Sys CPU time of current thread since an arbitrary epoch.
void GetThreadUserAndSysCpuTimeMicros(MicrosecondsInt64 *user, MicrosecondsInt64 *sys);

namespace walltime_internal {

#if defined(__APPLE__)

extern GoogleOnceType timebase_info_once;
extern mach_timebase_info_data_t timebase_info;
extern void InitializeTimebaseInfo();

inline void GetCurrentTime(mach_timespec_t* ts) {
  clock_serv_t cclock;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, ts);
  mach_port_deallocate(mach_task_self(), cclock);
}

inline MicrosecondsInt64 GetCurrentTimeMicros() {
  mach_timespec_t ts;
  GetCurrentTime(&ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

inline int64_t GetMonoTimeNanos() {
  // See Apple Technical Q&A QA1398 for further detail on mono time in OS X.
  GoogleOnceInit(&timebase_info_once, &InitializeTimebaseInfo);

  uint64_t time = mach_absolute_time();

  // mach_absolute_time returns ticks, which need to be scaled by the timebase
  // info to get nanoseconds.
  return time * timebase_info.numer / timebase_info.denom;
}

inline MicrosecondsInt64 GetMonoTimeMicros() {
  return GetMonoTimeNanos() / 1e3;
}

inline MicrosecondsInt64 GetThreadCpuTimeMicros() {
  MicrosecondsInt64 thread_user_cpu = 0;
  GetThreadUserAndSysCpuTimeMicros(&thread_user_cpu, nullptr);
  return thread_user_cpu;
}

#else

inline MicrosecondsInt64 GetClockTimeMicros(clockid_t clock) {
  timespec ts;
  clock_gettime(clock, &ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

#endif // defined(__APPLE__)

} // namespace walltime_internal

// Returns the time since the Epoch measured in microseconds.
inline MicrosecondsInt64 GetCurrentTimeMicros() {
#if defined(__APPLE__)
  return walltime_internal::GetCurrentTimeMicros();
#else
  return walltime_internal::GetClockTimeMicros(CLOCK_REALTIME);
#endif  // defined(__APPLE__)
}

// Returns the time since some arbitrary reference point, measured in microseconds.
// Guaranteed to be monotonic (and therefore useful for measuring intervals)
inline MicrosecondsInt64 GetMonoTimeMicros() {
#if defined(__APPLE__)
  return walltime_internal::GetMonoTimeMicros();
#else
  return walltime_internal::GetClockTimeMicros(CLOCK_MONOTONIC);
#endif  // defined(__APPLE__)
}

// Returns the time spent in user CPU on the current thread, measured in microseconds.
inline MicrosecondsInt64 GetThreadCpuTimeMicros() {
#if defined(__APPLE__)
  return walltime_internal::GetThreadCpuTimeMicros();
#else
  return walltime_internal::GetClockTimeMicros(CLOCK_THREAD_CPUTIME_ID);
#endif  // defined(__APPLE__)
}

// A CycleClock yields the value of a cycle counter that increments at a rate
// that is approximately constant.
class CycleClock {
 public:
  // Return the value of the counter.
  static inline int64 Now();

 private:
  CycleClock();
};

#include "yb/gutil/cycleclock-inl.h"  // inline method bodies
