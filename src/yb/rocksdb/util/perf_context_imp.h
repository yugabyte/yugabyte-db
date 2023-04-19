//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#pragma once

#include "yb/rocksdb/perf_context.h"

namespace rocksdb {

#if defined(NPERF_CONTEXT) || defined(IOS_CROSS_COMPILE)

#define PERF_TIMER_GUARD(metric)
#define PERF_CONDITIONAL_TIMER_FOR_MUTEX_GUARD(metric, condition)
#define PERF_TIMER_MEASURE(metric)
#define PERF_TIMER_STOP(metric)
#define PERF_TIMER_START(metric)
#define PERF_COUNTER_ADD(metric, value)

#else

// Stop the timer and update the metric
#define PERF_TIMER_STOP(metric)          \
  perf_step_timer_ ## metric.Stop();

#define PERF_TIMER_START(metric)          \
  perf_step_timer_ ## metric.Start();

// Declare and set start time of the timer
#define PERF_TIMER_GUARD(metric)                                          \
  ::yb::PerfStepTimer perf_step_timer_ ## metric(&(perf_context.metric)); \
  perf_step_timer_ ## metric.Start();

#define PERF_CONDITIONAL_TIMER_FOR_MUTEX_GUARD(metric, condition)             \
  ::yb::PerfStepTimer perf_step_timer_##metric(&(perf_context.metric), true); \
  if ((condition)) {                                                          \
    perf_step_timer_##metric.Start();                                         \
  }

// Update metric with time elapsed since last START. start time is reset
// to current timestamp.
#define PERF_TIMER_MEASURE(metric)        \
  perf_step_timer_ ## metric.Measure();

// Increase metric value
#define PERF_COUNTER_ADD(metric, value)     \
  perf_context.metric += value;

#endif

} // namespace rocksdb
