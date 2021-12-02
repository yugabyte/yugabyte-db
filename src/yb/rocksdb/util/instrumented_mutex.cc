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

#include "yb/rocksdb/util/instrumented_mutex.h"

#include "yb/rocksdb/util/perf_context_imp.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/rocksdb/util/stop_watch.h"

#include "yb/util/stats/perf_step_timer.h"

namespace rocksdb {
namespace {
bool ShouldReportToStats(Env* env, Statistics* stats) {
  return env != nullptr && stats != nullptr &&
         stats->stats_level_ != kExceptTimeForMutex;
}
}  // namespace

void InstrumentedMutex::Lock() {
  PERF_CONDITIONAL_TIMER_FOR_MUTEX_GUARD(db_mutex_lock_nanos,
                                         stats_code_ == DB_MUTEX_WAIT_MICROS);
  uint64_t wait_time_micros = 0;
  if (ShouldReportToStats(env_, stats_)) {
    {
      StopWatch sw(env_, nullptr, 0, &wait_time_micros);
      LockInternal();
    }
    RecordTick(stats_, stats_code_, wait_time_micros);
  } else {
    LockInternal();
  }
}

void InstrumentedMutex::LockInternal() {
  mutex_.Lock();
}

void InstrumentedCondVar::Wait() {
  PERF_CONDITIONAL_TIMER_FOR_MUTEX_GUARD(db_condition_wait_nanos,
                                         stats_code_ == DB_MUTEX_WAIT_MICROS);
  uint64_t wait_time_micros = 0;
  if (ShouldReportToStats(env_, stats_)) {
    {
      StopWatch sw(env_, nullptr, 0, &wait_time_micros);
      WaitInternal();
    }
    RecordTick(stats_, stats_code_, wait_time_micros);
  } else {
    WaitInternal();
  }
}

void InstrumentedCondVar::WaitInternal() {
  cond_.Wait();
}

bool InstrumentedCondVar::TimedWait(uint64_t abs_time_us) {
  PERF_CONDITIONAL_TIMER_FOR_MUTEX_GUARD(db_condition_wait_nanos,
                                         stats_code_ == DB_MUTEX_WAIT_MICROS);
  uint64_t wait_time_micros = 0;
  bool result = false;
  if (ShouldReportToStats(env_, stats_)) {
    {
      StopWatch sw(env_, nullptr, 0, &wait_time_micros);
      result = TimedWaitInternal(abs_time_us);
    }
    RecordTick(stats_, stats_code_, wait_time_micros);
  } else {
    result = TimedWaitInternal(abs_time_us);
  }
  return result;
}

bool InstrumentedCondVar::TimedWaitInternal(uint64_t abs_time_us) {
  return cond_.TimedWait(abs_time_us);
}

}  // namespace rocksdb
