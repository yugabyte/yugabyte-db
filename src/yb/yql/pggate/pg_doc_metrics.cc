//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_doc_metrics.h"

#include "yb/util/flags.h"

DEFINE_test_flag(bool, use_monotime_for_rpc_wait_time, false,
                 "Flag to enable use of MonoTime::Now() instead of CoarseMonoClock::Now() "
                 "in order to avoid 0 timings in the tests.");

namespace yb::pggate {
namespace {

inline void IncRead(YBCPgExecReadWriteStats* stat, uint64_t wait_time) {
  ++stat->reads;
  stat->read_wait += wait_time;
}

inline void IncWrite(YBCPgExecReadWriteStats* stat) {
  ++stat->writes;
}

YBCPgExecReadWriteStats& GetStat(YBCPgExecStatsState* state, TableType relation) {
  switch (relation) {
    case TableType::SYSTEM:
      return state->stats.catalog;
    case TableType::USER:
      return state->stats.tables;
    case TableType::INDEX:
      return state->stats.indices;
  }

  FATAL_INVALID_ENUM_VALUE(TableType, relation);
}

uint64_t GetNow(bool use_zero_duration) {
  if (use_zero_duration) {
    return 0;
  }
  return (PREDICT_FALSE(FLAGS_TEST_use_monotime_for_rpc_wait_time)
      ? MonoTime::Now().ToSteadyTimePoint().time_since_epoch()
      : CoarseMonoClock::Now().time_since_epoch()).count();
}

} // namespace

PgDocMetrics::DurationWatcher::DurationWatcher(uint64_t* duration, bool use_zero_duration)
    : duration_(duration),
      use_zero_duration_(use_zero_duration),
      start_(GetNow(use_zero_duration_)) {
}

PgDocMetrics::DurationWatcher::~DurationWatcher() {
  *duration_ = GetNow(use_zero_duration_) - start_;
}

PgDocMetrics::PgDocMetrics(YBCPgExecStatsState* state) : state_(*state) {}

void PgDocMetrics::ReadRequest(TableType relation, uint64_t wait_time) {
  IncRead(&GetStat(&state_, relation), wait_time);
}

void PgDocMetrics::WriteRequest(TableType relation) {
  IncWrite(&GetStat(&state_, relation));
}

void PgDocMetrics::FlushRequest(uint64_t wait_time) {
  state_.stats.num_flushes += 1;
  state_.stats.flush_wait += wait_time;
}

}  // namespace yb::pggate
