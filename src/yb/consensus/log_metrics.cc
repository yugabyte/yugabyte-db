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

#include "yb/consensus/log_metrics.h"

#include "yb/util/metrics.h"

METRIC_DEFINE_counter(tablet, log_bytes_logged, "Bytes Written to WAL",
                      yb::MetricUnit::kBytes,
                      "Number of bytes logged since service start");

METRIC_DEFINE_gauge_uint64(tablet, log_wal_size, "Size of WAL Files",
                           yb::MetricUnit::kBytes,
                           "Size of wal files");

METRIC_DEFINE_event_stats(table, log_sync_latency, "Log Sync Latency",
                               yb::MetricUnit::kMicroseconds,
                               "Microseconds spent on synchronizing the log segment file");

METRIC_DEFINE_event_stats(table, log_append_latency, "Log Append Latency",
                        yb::MetricUnit::kMicroseconds,
                        "Microseconds spent on appending to the log segment file");

METRIC_DEFINE_event_stats(table, log_group_commit_latency, "Log Group Commit Latency",
                        yb::MetricUnit::kMicroseconds,
                        "Microseconds spent on committing an entire group");

METRIC_DEFINE_event_stats(table, log_roll_latency, "Log Roll Latency",
                        yb::MetricUnit::kMicroseconds,
                        "Microseconds spent on rolling over to a new log segment file");

METRIC_DEFINE_event_stats(table, log_entry_batches_per_group, "Log Group Commit Batch Size",
                        yb::MetricUnit::kRequests,
                        "Number of log entry batches in a group commit group");

namespace yb {
namespace log {

#define MINIT(metric_entity, x) x(METRIC_log_##x.Instantiate(metric_entity))
#define GINIT(metric_entity, x) x(METRIC_log_##x.Instantiate(metric_entity, 0))
LogMetrics::LogMetrics(const scoped_refptr<MetricEntity>& table_metric_entity,
                       const scoped_refptr<MetricEntity>& tablet_metric_entity)
    : MINIT(tablet_metric_entity, bytes_logged),
      GINIT(tablet_metric_entity, wal_size),
      MINIT(table_metric_entity, sync_latency),
      MINIT(table_metric_entity, append_latency),
      MINIT(table_metric_entity, group_commit_latency),
      MINIT(table_metric_entity, roll_latency),
      MINIT(table_metric_entity, entry_batches_per_group) {
}
#undef MINIT

} // namespace log
} // namespace yb
