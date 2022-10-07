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

#include "yb/util/metrics.h"

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

METRIC_DEFINE_counter(tablet, insertions_failed_dup_key, "Duplicate Key Inserts",
                      yb::MetricUnit::kRows,
                      "Number of inserts which failed because the key already existed");

METRIC_DEFINE_coarse_histogram(table, write_op_duration_client_propagated_consistency,
  "Write Op Duration with Propagated Consistency",
  yb::MetricUnit::kMicroseconds,
  "Duration of writes to this tablet with external consistency set to CLIENT_PROPAGATED.");

METRIC_DEFINE_coarse_histogram(table, snapshot_read_inflight_wait_duration,
  "Time Waiting For Snapshot Reads",
  yb::MetricUnit::kMicroseconds,
  "Time spent waiting for in-flight writes to complete for READ_AT_SNAPSHOT scans.");

METRIC_DEFINE_coarse_histogram(
    table, ql_read_latency, "Handle ReadRequest latency at tserver layer",
    yb::MetricUnit::kMicroseconds,
    "Time taken to handle the read request at the tserver layer.");

METRIC_DEFINE_coarse_histogram(
    table, write_lock_latency, "Write lock latency", yb::MetricUnit::kMicroseconds,
    "Time taken to acquire key locks for a write operation");

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

METRIC_DEFINE_counter(tablet, not_leader_rejections,
  "Not Leader Rejections",
  yb::MetricUnit::kRequests,
  "Number of RPC requests rejected due to fact that this node is not LEADER.");

METRIC_DEFINE_counter(tablet, leader_memory_pressure_rejections,
  "Leader Memory Pressure Rejections",
  yb::MetricUnit::kRequests,
  "Number of RPC requests rejected due to memory pressure while LEADER.");

METRIC_DEFINE_counter(tablet, majority_sst_files_rejections,
  "Majority SST files number Rejections",
  yb::MetricUnit::kRequests,
  "Number of RPC requests rejected due to number of majority SST files.");

METRIC_DEFINE_counter(tablet, transaction_conflicts,
  "Distributed Transaction Conflicts",
  yb::MetricUnit::kRequests,
  "Number of conflicts detected among uncommitted distributed transactions.");

METRIC_DEFINE_counter(tablet, expired_transactions,
  "Expired Distributed Transactions",
  yb::MetricUnit::kRequests,
  "Number of expired distributed transactions.");

METRIC_DEFINE_counter(tablet, restart_read_requests,
  "Read Requests Requiring Restart",
  yb::MetricUnit::kRequests,
  "Number of read requests that require restart.");

METRIC_DEFINE_counter(tablet, consistent_prefix_read_requests,
    "Consistent Prefix Read Requests",
    yb::MetricUnit::kRequests,
    "Number of consistent prefix read requests");

METRIC_DEFINE_counter(tablet, pgsql_consistent_prefix_read_rows,
                      "Consistent Prefix Read Requests",
                      yb::MetricUnit::kRequests,
                      "Number of pgsql rows read as part of a consistent prefix request");

METRIC_DEFINE_counter(tablet, tablet_data_corruptions,
  "Tablet Data Corruption Detections",
  yb::MetricUnit::kUnits,
  "Number of times this tablet was flagged for corrupted data");

using strings::Substitute;

namespace yb {
namespace tablet {

#define MINIT(entity, x) x(METRIC_##x.Instantiate(entity))
TabletMetrics::TabletMetrics(const scoped_refptr<MetricEntity>& table_entity,
                             const scoped_refptr<MetricEntity>& tablet_entity)
  : MINIT(table_entity, snapshot_read_inflight_wait_duration),
    MINIT(table_entity, ql_read_latency),
    MINIT(table_entity, write_lock_latency),
    MINIT(table_entity, write_op_duration_client_propagated_consistency),
    MINIT(tablet_entity, not_leader_rejections),
    MINIT(tablet_entity, leader_memory_pressure_rejections),
    MINIT(tablet_entity, majority_sst_files_rejections),
    MINIT(tablet_entity, transaction_conflicts),
    MINIT(tablet_entity, expired_transactions),
    MINIT(tablet_entity, restart_read_requests),
    MINIT(tablet_entity, consistent_prefix_read_requests),
    MINIT(tablet_entity, pgsql_consistent_prefix_read_rows),
    MINIT(tablet_entity, tablet_data_corruptions),
    MINIT(tablet_entity, rows_inserted) {
}
#undef MINIT

ScopedTabletMetricsTracker::ScopedTabletMetricsTracker(scoped_refptr<Histogram> latency)
    : latency_(latency), start_time_(MonoTime::Now()) {}

ScopedTabletMetricsTracker::~ScopedTabletMetricsTracker() {
  latency_->Increment(MonoTime::Now().GetDeltaSince(start_time_).ToMicroseconds());
}
} // namespace tablet
} // namespace yb
