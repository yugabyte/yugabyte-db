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
    s = s.CloneAndPrepend("Unexpected error while running Log GC from TabletPeer");
    LOG(DFATAL) << s.ToString();
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
      op_last_successful_run_time_(MonoTime::Min()),
      cdcsdk_reset_retention_barriers_op_duration_(
          METRIC_cdcsdk_reset_retention_barriers_op_duration.Instantiate(
              tablet->GetTableMetricsEntity())),
      cdcsdk_reset_retention_barriers_ops_running_(
          METRIC_cdcsdk_reset_retention_barriers_ops_running.Instantiate(
              tablet->GetTableMetricsEntity(), 0)),
      sem_(1) {}

void ResetStaleRetentionBarriersOp::UpdateStats(MaintenanceOpStats* stats) {
  double seconds_since_last_refresh;
  if (!tablet_peer_->is_cdc_min_replicated_index_stale(&seconds_since_last_refresh)) {
    stats->set_cdcsdk_reset_stale_retention_barrier(false);
    stats->set_runnable(false);
    return;
  }

  // If the last successful execution of this op predates the tablet's most recent refresh of the
  // cdc_min_replicated_index, mark the op runnable so it can reset any stale retention barriers.
  auto cdc_min_replicated_index_last_refresh_time =
      MonoTime::Now() - MonoDelta::FromSeconds(seconds_since_last_refresh);
  if (op_last_successful_run_time_ <= cdc_min_replicated_index_last_refresh_time) {
    stats->set_cdcsdk_reset_stale_retention_barrier(true);
    stats->set_runnable(sem_.GetValue() == 1);
  }
}

bool ResetStaleRetentionBarriersOp::Prepare() {
  return sem_.try_lock();
}

void ResetStaleRetentionBarriersOp::Perform() {
  CHECK(!sem_.try_lock());

  Status s = tablet_peer_->reset_all_cdc_retention_barriers_if_stale();
  if (!s.ok()) {
    s = s.CloneAndPrepend("Unexpected error while resetting retention barriers from TabletPeer");
    LOG(DFATAL) << s.ToString();
  } else {
    op_last_successful_run_time_ = MonoTime::Now();
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
