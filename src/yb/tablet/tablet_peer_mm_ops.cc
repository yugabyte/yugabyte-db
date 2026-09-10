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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/tablet/tablet_peer_mm_ops.h"

#include <string>

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/metrics.h"
#include "yb/util/logging.h"

METRIC_DEFINE_gauge_uint32(table, log_gc_running,
                           "Log GCs Running",
                           yb::MetricUnit::kOperations,
                           "Number of log GC operations currently running.");
METRIC_DEFINE_event_stats(table, log_gc_duration,
                        "Log GC Duration",
                        yb::MetricUnit::kMilliseconds,
                        "Time (milliseconds) spent garbage collecting the logs.");

METRIC_DEFINE_gauge_uint32(
    table, cdcsdk_reset_retention_barriers_ops_running,
    "CDCSDK Reset Retention Barrier Ops Running", yb::MetricUnit::kOperations,
    "Number of operations currently running to reset retention barriers.");
METRIC_DEFINE_event_stats(
    table, cdcsdk_reset_retention_barriers_op_duration,
    "CDCSDK Reset Retention Barrier Op Duration", yb::MetricUnit::kMilliseconds,
    "Time spent resetting the retention barriers.");

namespace yb::tablet {

//
// LogGCOp.
//

LogGCOp::LogGCOp(TabletPeer* tablet_peer, const TabletPtr& tablet)
    : MaintenanceOp(
          StringPrintf("LogGCOp(%s)", tablet->tablet_id().c_str()),
          MaintenanceOp::LOW_IO_USAGE),
      tablet_(tablet),
      tablet_peer_(tablet_peer),
      log_gc_duration_(
          METRIC_log_gc_duration.Instantiate(tablet->GetTableMetricsEntity())),
      log_gc_running_(
          METRIC_log_gc_running.Instantiate(tablet->GetTableMetricsEntity(), 0)),
      sem_(1) {}

void LogGCOp::UpdateStats(MaintenanceOpStats* stats) {
  int64_t retention_size = 0;

  auto status = tablet_peer_->GetGCableDataSize(&retention_size);
  if (!status.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << tablet_peer_->LogPrefix()
        << "failed to get GC-able data size: " << status;
    return;
  }
  stats->set_logs_retained_bytes(retention_size);
  stats->set_runnable(sem_.GetValue() == 1);
}

bool LogGCOp::Prepare() {
  return sem_.try_lock();
}

void LogGCOp::Perform() {
  CHECK(!sem_.try_lock());

  Status s = tablet_peer_->RunLogGC();
  if (!s.ok()) {
    // Log GC races with tablet shutdown, e.g. a tombstone delete, so shutdown is expected here.
    s = s.CloneAndPrepend("Error while running Log GC from TabletPeer");
    LOG_IF(WARNING, s.IsShutdownInProgress()) << s.ToString();
    LOG_IF(DFATAL, !s.IsShutdownInProgress()) << s.ToString();
  }

  sem_.unlock();
}

scoped_refptr<EventStats> LogGCOp::DurationHistogram() const {
  return log_gc_duration_;
}

scoped_refptr<AtomicGauge<uint32_t> > LogGCOp::RunningGauge() const {
  return log_gc_running_;
}

//
// ResetStaleRetentionBarriersOp.
//

ResetStaleRetentionBarriersOp::ResetStaleRetentionBarriersOp(
    TabletPeer* tablet_peer, const TabletPtr& tablet)
    : MaintenanceOp(
          StringPrintf("ResetStaleRetentionBarriersOp(%s)", tablet->tablet_id().c_str()),
          MaintenanceOp::LOW_IO_USAGE),
      tablet_(tablet),
      tablet_peer_(tablet_peer),
      wal_intent_barrier_last_reset_time_(MonoTime::Min()),
      history_barrier_last_reset_time_(MonoTime::Min()),
      cdcsdk_reset_retention_barriers_op_duration_(
          METRIC_cdcsdk_reset_retention_barriers_op_duration.Instantiate(
              tablet->GetTableMetricsEntity())),
      cdcsdk_reset_retention_barriers_ops_running_(
          METRIC_cdcsdk_reset_retention_barriers_ops_running.Instantiate(
              tablet->GetTableMetricsEntity(), 0)),
      sem_(1) {}

void ResetStaleRetentionBarriersOp::UpdateStats(MaintenanceOpStats* stats) {
  // WAL/intent and history barriers have independent staleness clocks, since one group can be
  // advanced without the other. This op is needed if either of them is stale.
  double seconds_since_wal_intent_barriers_last_refresh;
  double seconds_since_history_barrier_last_refresh;
  bool is_wal_intent_barriers_stale = tablet_peer_->is_cdc_min_replicated_index_stale(
      &seconds_since_wal_intent_barriers_last_refresh);
  bool is_history_barrier_stale =
      tablet_peer_->is_cdc_sdk_safe_time_stale(&seconds_since_history_barrier_last_refresh);
  if (!is_wal_intent_barriers_stale && !is_history_barrier_stale) {
    stats->set_cdcsdk_reset_stale_retention_barrier(false);
    stats->set_runnable(false);
    return;
  }

  // A group's (WAL/intent or history) stale barrier is released only if it has been refreshed since
  // this op last released it.
  // The two groups use independent reset times: releasing the WAL/intent group must not suppress
  // releasing the history barrier once it later goes stale, and vice versa.
  auto now = MonoTime::Now();
  bool should_release_wal_intent_barriers =
      is_wal_intent_barriers_stale &&
      wal_intent_barrier_last_reset_time_ <=
          now - MonoDelta::FromSeconds(seconds_since_wal_intent_barriers_last_refresh);
  bool should_release_history_barrier =
      is_history_barrier_stale &&
      history_barrier_last_reset_time_ <=
          now - MonoDelta::FromSeconds(seconds_since_history_barrier_last_refresh);

  if (should_release_wal_intent_barriers || should_release_history_barrier) {
    stats->set_cdcsdk_reset_stale_retention_barrier(true);
    stats->set_runnable(sem_.GetValue() == 1);
  } else {
    stats->set_cdcsdk_reset_stale_retention_barrier(false);
    stats->set_runnable(false);
  }
}

bool ResetStaleRetentionBarriersOp::Prepare() {
  return sem_.try_lock();
}

void ResetStaleRetentionBarriersOp::Perform() {
  CHECK(!sem_.try_lock());

  auto reset_result = tablet_peer_->reset_cdc_retention_barriers_if_stale();
  if (!reset_result.ok()) {
    auto s = reset_result.status().CloneAndPrepend(
        "Unexpected error while resetting retention barriers from TabletPeer");
    LOG(DFATAL) << s.ToString();
  } else {
    // Stamp the reset time only for the barrier group(s) actually released, so that UpdateStats
    // tracks each group's last release independently and won't try to release a group again until
    // it has been refreshed since this reset.
    auto now = MonoTime::Now();
    if (reset_result->move_cdc_min_replicated_index ||
        reset_result->move_cdc_sdk_min_checkpoint_op_id) {
      wal_intent_barrier_last_reset_time_ = now;
    }
    if (reset_result->move_cdc_sdk_safe_time) {
      history_barrier_last_reset_time_ = now;
    }
  }
  sem_.unlock();
}

scoped_refptr<EventStats> ResetStaleRetentionBarriersOp::DurationHistogram() const {
  return cdcsdk_reset_retention_barriers_op_duration_;
}

scoped_refptr<AtomicGauge<uint32_t> > ResetStaleRetentionBarriersOp::RunningGauge() const {
  return cdcsdk_reset_retention_barriers_ops_running_;
}

} // namespace yb::tablet
