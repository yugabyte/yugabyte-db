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
#include "yb/tablet/tablet_metrics.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/metrics.h"
#include "yb/util/trace.h"

// Tablet-specific metrics.
METRIC_DEFINE_counter(tablet, rows_inserted, "Rows Inserted",
    yb::MetricUnit::kRows,
    "Number of rows inserted into this tablet since service start");
METRIC_DEFINE_counter(tablet, rows_updated, "Rows Updated",
    yb::MetricUnit::kRows,
    "Number of row update operations performed on this tablet since service start");
METRIC_DEFINE_counter(tablet, rows_deleted, "Rows Deleted",
    yb::MetricUnit::kRows,
    "Number of row delete operations performed on this tablet since service start");

METRIC_DEFINE_counter(tablet, scanner_rows_returned, "Scanner Rows Returned",
                      yb::MetricUnit::kRows,
                      "Number of rows returned by scanners to clients. This count "
                      "is measured after predicates are applied, and thus is not "
                      "a reflection of the amount of work being done by scanners.");
METRIC_DEFINE_counter(tablet, scanner_cells_returned, "Scanner Cells Returned",
                      yb::MetricUnit::kCells,
                      "Number of table cells returned by scanners to clients. This count "
                      "is measured after predicates are applied, and thus is not "
                      "a reflection of the amount of work being done by scanners.");
METRIC_DEFINE_counter(tablet, scanner_bytes_returned, "Scanner Bytes Returned",
                      yb::MetricUnit::kBytes,
                      "Number of bytes returned by scanners to clients. This count "
                      "is measured after predicates are applied and the data is decoded "
                      "for consumption by clients, and thus is not "
                      "a reflection of the amount of work being done by scanners.");


METRIC_DEFINE_counter(tablet, scanner_rows_scanned, "Scanner Rows Scanned",
                      yb::MetricUnit::kRows,
                      "Number of rows processed by scan requests. This is measured "
                      "as a raw count prior to application of predicates, deleted data,"
                      "or MVCC-based filtering. Thus, this is a better measure of actual "
                      "table rows that have been processed by scan operations compared "
                      "to the Scanner Rows Returned metric.");

METRIC_DEFINE_counter(tablet, scanner_cells_scanned_from_disk, "Scanner Cells Scanned From Disk",
                      yb::MetricUnit::kCells,
                      "Number of table cells processed by scan requests. This is measured "
                      "as a raw count prior to application of predicates, deleted data,"
                      "or MVCC-based filtering. Thus, this is a better measure of actual "
                      "table cells that have been processed by scan operations compared "
                      "to the Scanner Cells Returned metric.\n"
                      "Note that this only counts data that has been flushed to disk, "
                      "and does not include data read from in-memory stores. However, it"
                      "includes both cache misses and cache hits.");

METRIC_DEFINE_counter(tablet, scanner_bytes_scanned_from_disk, "Scanner Bytes Scanned From Disk",
                      yb::MetricUnit::kBytes,
                      "Number of bytes read by scan requests. This is measured "
                      "as a raw count prior to application of predicates, deleted data,"
                      "or MVCC-based filtering. Thus, this is a better measure of actual "
                      "IO that has been caused by scan operations compared "
                      "to the Scanner Bytes Returned metric.\n"
                      "Note that this only counts data that has been flushed to disk, "
                      "and does not include data read from in-memory stores. However, it"
                      "includes both cache misses and cache hits.");


METRIC_DEFINE_counter(tablet, insertions_failed_dup_key, "Duplicate Key Inserts",
                      yb::MetricUnit::kRows,
                      "Number of inserts which failed because the key already existed");
METRIC_DEFINE_counter(tablet, scans_started, "Scans Started",
                      yb::MetricUnit::kScanners,
                      "Number of scanners which have been started on this tablet");

METRIC_DEFINE_histogram(tablet, write_op_duration_client_propagated_consistency,
  "Write Op Duration with Propagated Consistency",
  yb::MetricUnit::kMicroseconds,
  "Duration of writes to this tablet with external consistency set to CLIENT_PROPAGATED.",
  60000000LU, 2);

METRIC_DEFINE_histogram(tablet, snapshot_read_inflight_wait_duration,
  "Time Waiting For Snapshot Reads",
  yb::MetricUnit::kMicroseconds,
  "Time spent waiting for in-flight writes to complete for READ_AT_SNAPSHOT scans.",
  60000000LU, 2);

METRIC_DEFINE_histogram(
    tablet, redis_read_latency, "HandleRedisReadRequest latency", yb::MetricUnit::kMicroseconds,
    "Time taken to handle a RedisReadRequest", 60000000LU, 2);

METRIC_DEFINE_histogram(
    tablet, ql_read_latency, "HandleQLReadRequest latency", yb::MetricUnit::kMicroseconds,
    "Time taken to handle a QLReadRequest", 60000000LU, 2);

METRIC_DEFINE_histogram(
    tablet, write_lock_latency, "Write lock latency", yb::MetricUnit::kMicroseconds,
    "Time taken to acquire key locks for a write operation", 60000000LU, 2);

METRIC_DEFINE_gauge_uint32(tablet, compact_rs_running,
  "RowSet Compactions Running",
  yb::MetricUnit::kMaintenanceOperations,
  "Number of RowSet compactions currently running.");

METRIC_DEFINE_gauge_uint32(tablet, delta_minor_compact_rs_running,
  "Minor Delta Compactions Running",
  yb::MetricUnit::kMaintenanceOperations,
  "Number of delta minor compactions currently running.");

METRIC_DEFINE_gauge_uint32(tablet, delta_major_compact_rs_running,
  "Major Delta Compactions Running",
  yb::MetricUnit::kMaintenanceOperations,
  "Number of delta major compactions currently running.");

METRIC_DEFINE_histogram(tablet, compact_rs_duration,
  "RowSet Compaction Duration",
  yb::MetricUnit::kMilliseconds,
  "Time spent compacting RowSets.", 60000LU, 1);

METRIC_DEFINE_histogram(tablet, delta_minor_compact_rs_duration,
  "Minor Delta Compaction Duration",
  yb::MetricUnit::kMilliseconds,
  "Time spent minor delta compacting.", 60000LU, 1);

METRIC_DEFINE_histogram(tablet, delta_major_compact_rs_duration,
  "Major Delta Compaction Duration",
  yb::MetricUnit::kSeconds,
  "Seconds spent major delta compacting.", 60000000LU, 2);

METRIC_DEFINE_counter(tablet, leader_memory_pressure_rejections,
  "Leader Memory Pressure Rejections",
  yb::MetricUnit::kRequests,
  "Number of RPC requests rejected due to memory pressure while LEADER.");

using strings::Substitute;

namespace yb {
namespace tablet {

#define MINIT(x) x(METRIC_##x.Instantiate(entity))
#define GINIT(x) x(METRIC_##x.Instantiate(entity, 0))
TabletMetrics::TabletMetrics(const scoped_refptr<MetricEntity>& entity)
  : MINIT(scanner_rows_returned),
    MINIT(scanner_cells_returned),
    MINIT(scanner_bytes_returned),
    MINIT(scanner_rows_scanned),
    MINIT(scanner_cells_scanned_from_disk),
    MINIT(scanner_bytes_scanned_from_disk),
    MINIT(scans_started),
    MINIT(snapshot_read_inflight_wait_duration),
    MINIT(redis_read_latency),
    MINIT(ql_read_latency),
    MINIT(write_lock_latency),
    MINIT(write_op_duration_client_propagated_consistency),
    MINIT(leader_memory_pressure_rejections) {
}
#undef MINIT
#undef GINIT

ScopedTabletMetricsTracker::ScopedTabletMetricsTracker(scoped_refptr<Histogram> latency)
    : latency_(latency), start_time_(MonoTime::Now()) {}

ScopedTabletMetricsTracker::~ScopedTabletMetricsTracker() {
  latency_->Increment(MonoTime::Now().GetDeltaSince(start_time_).ToMicroseconds());
}
} // namespace tablet
} // namespace yb
