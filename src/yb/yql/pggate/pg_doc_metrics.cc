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

namespace yb::pggate {
namespace {

inline void IncRead(YBCPgExecReadWriteStats* stat, uint64_t wait_time) {
  ++stat->reads;
  stat->read_wait += wait_time;
}

inline void IncWrite(YBCPgExecReadWriteStats* stat) {
  ++stat->writes;
}

inline void IncRowsScanned(YBCPgExecReadWriteStats* stat, uint64_t count) {
  stat->rows_scanned += count;
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

uint64_t GetNow(bool use_zero_duration, bool use_high_res_timer) {
  if (use_zero_duration) {
    return 0;
  }
  return (PREDICT_TRUE(use_high_res_timer) ? MonoTime::Now().ToSteadyTimePoint().time_since_epoch()
                                           : CoarseMonoClock::Now().time_since_epoch())
      .count();
}

} // namespace

PgDocMetrics::DurationWatcher::DurationWatcher(
    uint64_t* duration, bool use_zero_duration, bool use_high_res_timer)
    : duration_(duration),
      use_zero_duration_(use_zero_duration),
      use_high_res_timer_(use_high_res_timer),
      start_(GetNow(use_zero_duration_, use_high_res_timer_)) {}

PgDocMetrics::DurationWatcher::~DurationWatcher() {
  *duration_ = GetNow(use_zero_duration_, use_high_res_timer_) - start_;
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

void PgDocMetrics::RecordRequestMetrics(const LWPgsqlRequestMetricsPB& metrics) {
  bool has_change = false;
  for (const auto& storage_metric : metrics.gauge_metrics()) {
    auto metric = storage_metric.metric();
    // If there is a rolling restart in progress, it's possible for an unknown metric to be
    // received, but since this is for optional output it's fine to just disregard it.
    if (metric >= YB_STORAGE_GAUGE_COUNT) {
      continue;
    }
    auto value = storage_metric.value();
    if (value) {
      has_change = true;
      state_.stats.storage_gauge_metrics[metric] += value;
    }
  }
  for (const auto& storage_metric : metrics.counter_metrics()) {
    auto metric = storage_metric.metric();
    if (metric >= YB_STORAGE_COUNTER_COUNT) {
      continue;
    }
    auto value = storage_metric.value();
    if (value) {
      has_change = true;
      state_.stats.storage_counter_metrics[metric] += value;
    }
  }
  for (const auto& storage_metric : metrics.event_metrics()) {
    auto metric = storage_metric.metric();
    if (metric >= YB_STORAGE_GAUGE_COUNT) {
      continue;
    }
    auto& stats = state_.stats.storage_event_metrics[metric];
    if (storage_metric.count()) {
      has_change = true;
      stats.sum += storage_metric.sum();
      stats.count += storage_metric.count();
    }
  }
  if (has_change) {
    ++state_.stats.storage_metrics_version;
  }
}

void PgDocMetrics::RecordStorageRowsRead(TableType relation, uint64_t rows) {
  IncRowsScanned(&GetStat(&state_, relation), rows);
}

}  // namespace yb::pggate
