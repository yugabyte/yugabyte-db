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
#include "kudu/tablet/tablet_metrics.h"

#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/metrics.h"
#include "kudu/util/trace.h"

// Tablet-specific metrics.
METRIC_DEFINE_counter(tablet, rows_inserted, "Rows Inserted",
    kudu::MetricUnit::kRows,
    "Number of rows inserted into this tablet since service start");
METRIC_DEFINE_counter(tablet, rows_updated, "Rows Updated",
    kudu::MetricUnit::kRows,
    "Number of row update operations performed on this tablet since service start");
METRIC_DEFINE_counter(tablet, rows_deleted, "Rows Deleted",
    kudu::MetricUnit::kRows,
    "Number of row delete operations performed on this tablet since service start");

METRIC_DEFINE_counter(tablet, scanner_rows_returned, "Scanner Rows Returned",
                      kudu::MetricUnit::kRows,
                      "Number of rows returned by scanners to clients. This count "
                      "is measured after predicates are applied, and thus is not "
                      "a reflection of the amount of work being done by scanners.");
METRIC_DEFINE_counter(tablet, scanner_cells_returned, "Scanner Cells Returned",
                      kudu::MetricUnit::kCells,
                      "Number of table cells returned by scanners to clients. This count "
                      "is measured after predicates are applied, and thus is not "
                      "a reflection of the amount of work being done by scanners.");
METRIC_DEFINE_counter(tablet, scanner_bytes_returned, "Scanner Bytes Returned",
                      kudu::MetricUnit::kBytes,
                      "Number of bytes returned by scanners to clients. This count "
                      "is measured after predicates are applied and the data is decoded "
                      "for consumption by clients, and thus is not "
                      "a reflection of the amount of work being done by scanners.");


METRIC_DEFINE_counter(tablet, scanner_rows_scanned, "Scanner Rows Scanned",
                      kudu::MetricUnit::kRows,
                      "Number of rows processed by scan requests. This is measured "
                      "as a raw count prior to application of predicates, deleted data,"
                      "or MVCC-based filtering. Thus, this is a better measure of actual "
                      "table rows that have been processed by scan operations compared "
                      "to the Scanner Rows Returned metric.");

METRIC_DEFINE_counter(tablet, scanner_cells_scanned_from_disk, "Scanner Cells Scanned From Disk",
                      kudu::MetricUnit::kCells,
                      "Number of table cells processed by scan requests. This is measured "
                      "as a raw count prior to application of predicates, deleted data,"
                      "or MVCC-based filtering. Thus, this is a better measure of actual "
                      "table cells that have been processed by scan operations compared "
                      "to the Scanner Cells Returned metric.\n"
                      "Note that this only counts data that has been flushed to disk, "
                      "and does not include data read from in-memory stores. However, it"
                      "includes both cache misses and cache hits.");

METRIC_DEFINE_counter(tablet, scanner_bytes_scanned_from_disk, "Scanner Bytes Scanned From Disk",
                      kudu::MetricUnit::kBytes,
                      "Number of bytes read by scan requests. This is measured "
                      "as a raw count prior to application of predicates, deleted data,"
                      "or MVCC-based filtering. Thus, this is a better measure of actual "
                      "IO that has been caused by scan operations compared "
                      "to the Scanner Bytes Returned metric.\n"
                      "Note that this only counts data that has been flushed to disk, "
                      "and does not include data read from in-memory stores. However, it"
                      "includes both cache misses and cache hits.");


METRIC_DEFINE_counter(tablet, insertions_failed_dup_key, "Duplicate Key Inserts",
                      kudu::MetricUnit::kRows,
                      "Number of inserts which failed because the key already existed");
METRIC_DEFINE_counter(tablet, scans_started, "Scans Started",
                      kudu::MetricUnit::kScanners,
                      "Number of scanners which have been started on this tablet");

METRIC_DEFINE_counter(tablet, bloom_lookups, "Bloom Filter Lookups",
                      kudu::MetricUnit::kProbes,
                      "Number of times a bloom filter was consulted");
METRIC_DEFINE_counter(tablet, key_file_lookups, "Key File Lookups",
                      kudu::MetricUnit::kProbes,
                      "Number of times a key cfile was consulted");
METRIC_DEFINE_counter(tablet, delta_file_lookups, "Delta File Lookups",
                      kudu::MetricUnit::kProbes,
                      "Number of times a delta file was consulted");
METRIC_DEFINE_counter(tablet, mrs_lookups, "MemRowSet Lookups",
                      kudu::MetricUnit::kProbes,
                      "Number of times a MemRowSet was consulted.");
METRIC_DEFINE_counter(tablet, bytes_flushed, "Bytes Flushed",
                      kudu::MetricUnit::kBytes,
                      "Amount of data that has been flushed to disk by this tablet.");

METRIC_DEFINE_histogram(tablet, bloom_lookups_per_op, "Bloom Lookups per Operation",
                        kudu::MetricUnit::kProbes,
                        "Tracks the number of bloom filter lookups performed by each "
                        "operation. A single operation may perform several bloom filter "
                        "lookups if the tablet is not fully compacted. High frequency of "
                        "high values may indicate that compaction is falling behind.",
                        20, 2);

METRIC_DEFINE_histogram(tablet, key_file_lookups_per_op, "Key Lookups per Operation",
                        kudu::MetricUnit::kProbes,
                        "Tracks the number of key file lookups performed by each "
                        "operation. A single operation may perform several key file "
                        "lookups if the tablet is not fully compacted and if bloom filters "
                        "are not effectively culling lookups.", 20, 2);

METRIC_DEFINE_histogram(tablet, delta_file_lookups_per_op, "Delta File Lookups per Operation",
                        kudu::MetricUnit::kProbes,
                        "Tracks the number of delta file lookups performed by each "
                        "operation. A single operation may perform several delta file "
                        "lookups if the tablet is not fully compacted. High frequency of "
                        "high values may indicate that compaction is falling behind.", 20, 2);

METRIC_DEFINE_histogram(tablet, write_op_duration_client_propagated_consistency,
  "Write Op Duration with Propagated Consistency",
  kudu::MetricUnit::kMicroseconds,
  "Duration of writes to this tablet with external consistency set to CLIENT_PROPAGATED.",
  60000000LU, 2);

METRIC_DEFINE_histogram(tablet, write_op_duration_commit_wait_consistency,
  "Write Op Duration with Commit-Wait Consistency",
  kudu::MetricUnit::kMicroseconds,
  "Duration of writes to this tablet with external consistency set to COMMIT_WAIT.",
  60000000LU, 2);

METRIC_DEFINE_histogram(tablet, commit_wait_duration,
  "Commit-Wait Duration",
  kudu::MetricUnit::kMicroseconds,
  "Time spent waiting for COMMIT_WAIT external consistency writes for this tablet.",
  60000000LU, 2);

METRIC_DEFINE_histogram(tablet, snapshot_read_inflight_wait_duration,
  "Time Waiting For Snapshot Reads",
  kudu::MetricUnit::kMicroseconds,
  "Time spent waiting for in-flight writes to complete for READ_AT_SNAPSHOT scans.",
  60000000LU, 2);

METRIC_DEFINE_gauge_uint32(tablet, flush_dms_running,
  "DeltaMemStore Flushes Running",
  kudu::MetricUnit::kMaintenanceOperations,
  "Number of delta memstore flushes currently running.");

METRIC_DEFINE_gauge_uint32(tablet, flush_mrs_running,
  "MemRowSet Flushes Running",
  kudu::MetricUnit::kMaintenanceOperations,
  "Number of MemRowSet flushes currently running.");

METRIC_DEFINE_gauge_uint32(tablet, compact_rs_running,
  "RowSet Compactions Running",
  kudu::MetricUnit::kMaintenanceOperations,
  "Number of RowSet compactions currently running.");

METRIC_DEFINE_gauge_uint32(tablet, delta_minor_compact_rs_running,
  "Minor Delta Compactions Running",
  kudu::MetricUnit::kMaintenanceOperations,
  "Number of delta minor compactions currently running.");

METRIC_DEFINE_gauge_uint32(tablet, delta_major_compact_rs_running,
  "Major Delta Compactions Running",
  kudu::MetricUnit::kMaintenanceOperations,
  "Number of delta major compactions currently running.");

METRIC_DEFINE_histogram(tablet, flush_dms_duration,
  "DeltaMemStore Flush Duration",
  kudu::MetricUnit::kMilliseconds,
  "Time spent flushing DeltaMemStores.", 60000LU, 1);

METRIC_DEFINE_histogram(tablet, flush_mrs_duration,
  "MemRowSet Flush Duration",
  kudu::MetricUnit::kMilliseconds,
  "Time spent flushing MemRowSets.", 60000LU, 1);

METRIC_DEFINE_histogram(tablet, compact_rs_duration,
  "RowSet Compaction Duration",
  kudu::MetricUnit::kMilliseconds,
  "Time spent compacting RowSets.", 60000LU, 1);

METRIC_DEFINE_histogram(tablet, delta_minor_compact_rs_duration,
  "Minor Delta Compaction Duration",
  kudu::MetricUnit::kMilliseconds,
  "Time spent minor delta compacting.", 60000LU, 1);

METRIC_DEFINE_histogram(tablet, delta_major_compact_rs_duration,
  "Major Delta Compaction Duration",
  kudu::MetricUnit::kSeconds,
  "Seconds spent major delta compacting.", 60000000LU, 2);

METRIC_DEFINE_counter(tablet, leader_memory_pressure_rejections,
  "Leader Memory Pressure Rejections",
  kudu::MetricUnit::kRequests,
  "Number of RPC requests rejected due to memory pressure while LEADER.");

using strings::Substitute;

namespace kudu {
namespace tablet {

#define MINIT(x) x(METRIC_##x.Instantiate(entity))
#define GINIT(x) x(METRIC_##x.Instantiate(entity, 0))
TabletMetrics::TabletMetrics(const scoped_refptr<MetricEntity>& entity)
  : MINIT(rows_inserted),
    MINIT(rows_updated),
    MINIT(rows_deleted),
    MINIT(insertions_failed_dup_key),
    MINIT(scanner_rows_returned),
    MINIT(scanner_cells_returned),
    MINIT(scanner_bytes_returned),
    MINIT(scanner_rows_scanned),
    MINIT(scanner_cells_scanned_from_disk),
    MINIT(scanner_bytes_scanned_from_disk),
    MINIT(scans_started),
    MINIT(bloom_lookups),
    MINIT(key_file_lookups),
    MINIT(delta_file_lookups),
    MINIT(mrs_lookups),
    MINIT(bytes_flushed),
    MINIT(bloom_lookups_per_op),
    MINIT(key_file_lookups_per_op),
    MINIT(delta_file_lookups_per_op),
    MINIT(commit_wait_duration),
    MINIT(snapshot_read_inflight_wait_duration),
    MINIT(write_op_duration_client_propagated_consistency),
    MINIT(write_op_duration_commit_wait_consistency),
    GINIT(flush_dms_running),
    GINIT(flush_mrs_running),
    GINIT(compact_rs_running),
    GINIT(delta_minor_compact_rs_running),
    GINIT(delta_major_compact_rs_running),
    MINIT(flush_dms_duration),
    MINIT(flush_mrs_duration),
    MINIT(compact_rs_duration),
    MINIT(delta_minor_compact_rs_duration),
    MINIT(delta_major_compact_rs_duration),
    MINIT(leader_memory_pressure_rejections) {
}
#undef MINIT
#undef GINIT

void TabletMetrics::AddProbeStats(const ProbeStats& stats) {
  bloom_lookups->IncrementBy(stats.blooms_consulted);
  key_file_lookups->IncrementBy(stats.keys_consulted);
  delta_file_lookups->IncrementBy(stats.deltas_consulted);
  mrs_lookups->IncrementBy(stats.mrs_consulted);

  bloom_lookups_per_op->Increment(stats.blooms_consulted);
  key_file_lookups_per_op->Increment(stats.keys_consulted);
  delta_file_lookups_per_op->Increment(stats.deltas_consulted);

  TRACE("ProbeStats: bloom_lookups=$0,key_file_lookups=$1,"
        "delta_file_lookups=$2,mrs_lookups=$3",
        stats.blooms_consulted, stats.keys_consulted,
        stats.deltas_consulted, stats.mrs_consulted);
}

} // namespace tablet
} // namespace kudu
