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

#include "yb/tablet/tablet_peer_mm_ops.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>

#include "yb/util/flags.h"

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
                        "Time spent garbage collecting the logs.");

namespace yb {
namespace tablet {

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
    LOG(ERROR) << s.ToString();
  }

  sem_.unlock();
}

scoped_refptr<EventStats> LogGCOp::DurationHistogram() const {
  return log_gc_duration_;
}

scoped_refptr<AtomicGauge<uint32_t> > LogGCOp::RunningGauge() const {
  return log_gc_running_;
}

}  // namespace tablet
}  // namespace yb
