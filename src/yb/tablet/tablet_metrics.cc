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

METRIC_DEFINE_coarse_histogram(table, ql_write_latency, "Write latency at tserver layer",
  yb::MetricUnit::kMicroseconds,
  "Time taken to handle a batch of writes at tserver layer");

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

METRIC_DEFINE_coarse_histogram(
    table, read_time_wait, "Read Time Wait", yb::MetricUnit::kMicroseconds,
    "Number of microseconds read queries spend waiting for safe time");

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

METRIC_DEFINE_counter(tablet, failed_batch_lock,
  "Batch Lock Timeout",
  yb::MetricUnit::kUnits,
  "Number of times that WriteQuery fails to obtain batch lock");

METRIC_DEFINE_counter(tablet, docdb_keys_found, "Total Keys Found in RocksDB",
    yb::MetricUnit::kKeys,
    "Number of keys found in RocksDB searches (valid and invalid)");

METRIC_DEFINE_counter(tablet, docdb_obsolete_keys_found, "Obsolete Keys Found in RocksDB",
    yb::MetricUnit::kKeys,
    "Number of obsolete keys (e.g. deleted, expired) found in RocksDB searches.");

METRIC_DEFINE_counter(tablet, docdb_obsolete_keys_found_past_cutoff,
    "Obsolete Keys Found in RocksDB",
    yb::MetricUnit::kKeys,
    "Number of obsolete keys found in RocksDB searches that were past history cutoff");

namespace yb {
namespace tablet {

namespace {

// Keeps track of the number of TabletMetrics instances that have been created for the purpose
// of assigning an instance identifier.
std::atomic<uint64_t> tablet_metrics_instance_counter;

} // namespace

#define MINIT(entity, x) x(METRIC_##x.Instantiate(entity))
TabletMetrics::TabletMetrics(const scoped_refptr<MetricEntity>& table_entity,
                             const scoped_refptr<MetricEntity>& tablet_entity)
  : MINIT(table_entity, snapshot_read_inflight_wait_duration),
    MINIT(table_entity, ql_read_latency),
    MINIT(table_entity, write_lock_latency),
    MINIT(table_entity, ql_write_latency),
    MINIT(table_entity, read_time_wait),
    MINIT(tablet_entity, not_leader_rejections),
    MINIT(tablet_entity, leader_memory_pressure_rejections),
    MINIT(tablet_entity, majority_sst_files_rejections),
    MINIT(tablet_entity, transaction_conflicts),
    MINIT(tablet_entity, expired_transactions),
    MINIT(tablet_entity, restart_read_requests),
    MINIT(tablet_entity, consistent_prefix_read_requests),
    MINIT(tablet_entity, pgsql_consistent_prefix_read_rows),
    MINIT(tablet_entity, tablet_data_corruptions),
    MINIT(tablet_entity, rows_inserted),
    MINIT(tablet_entity, failed_batch_lock),
    MINIT(tablet_entity, docdb_keys_found),
    MINIT(tablet_entity, docdb_obsolete_keys_found),
    MINIT(tablet_entity, docdb_obsolete_keys_found_past_cutoff),
    instance_id(tablet_metrics_instance_counter.fetch_add(1, std::memory_order_relaxed)) {
}
#undef MINIT

ScopedTabletMetricsTracker::ScopedTabletMetricsTracker(scoped_refptr<Histogram> latency)
    : latency_(latency), start_time_(MonoTime::Now()) {}

ScopedTabletMetricsTracker::~ScopedTabletMetricsTracker() {
  latency_->Increment(MonoTime::Now().GetDeltaSince(start_time_).ToMicroseconds());
}
} // namespace tablet
} // namespace yb
