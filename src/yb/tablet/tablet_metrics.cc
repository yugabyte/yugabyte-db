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

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/util/logging.h"
#include "yb/util/metrics.h"

#include "yb/yql/pggate/pg_metrics_list.h"

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

constexpr std::pair<uint32_t, TabletCounters> kCounters[] = {
  {pggate::YB_ANALYZE_METRIC_NOT_LEADER_REJECTIONS, TabletCounters::kNotLeaderRejections},
  {pggate::YB_ANALYZE_METRIC_LEADER_MEMORY_PRESSURE_REJECTIONS,
      TabletCounters::kLeaderMemoryPressureRejections},
  {pggate::YB_ANALYZE_METRIC_MAJORITY_SST_FILES_REJECTIONS,
      TabletCounters::kMajoritySstFilesRejections},
  {pggate::YB_ANALYZE_METRIC_TRANSACTION_CONFLICTS, TabletCounters::kTransactionConflicts},
  {pggate::YB_ANALYZE_METRIC_EXPIRED_TRANSACTIONS, TabletCounters::kExpiredTransactions},
  {pggate::YB_ANALYZE_METRIC_RESTART_READ_REQUESTS, TabletCounters::kRestartReadRequests},
  {pggate::YB_ANALYZE_METRIC_CONSISTENT_PREFIX_READ_REQUESTS,
      TabletCounters::kConsistentPrefixReadRequests},
  {pggate::YB_ANALYZE_METRIC_PGSQL_CONSISTENT_PREFIX_READ_ROWS,
      TabletCounters::kPgsqlConsistentPrefixReadRows},
  {pggate::YB_ANALYZE_METRIC_TABLET_DATA_CORRUPTIONS, TabletCounters::kTabletDataCorruptions},
  {pggate::YB_ANALYZE_METRIC_ROWS_INSERTED, TabletCounters::kRowsInserted},
  {pggate::YB_ANALYZE_METRIC_FAILED_BATCH_LOCK, TabletCounters::kFailedBatchLock},
  {pggate::YB_ANALYZE_METRIC_DOCDB_KEYS_FOUND, TabletCounters::kDocDBKeysFound},
  {pggate::YB_ANALYZE_METRIC_DOCDB_OBSOLETE_KEYS_FOUND,
      TabletCounters::kDocDBObsoleteKeysFound},
  {pggate::YB_ANALYZE_METRIC_DOCDB_OBSOLETE_KEYS_FOUND_PAST_CUTOFF,
      TabletCounters::kDocDBObsoleteKeysFoundPastCutoff},
};

class TabletMetricsImpl final : public TabletMetrics {
 public:
  TabletMetricsImpl(const scoped_refptr<MetricEntity>& table_metric_entity,
                    const scoped_refptr<MetricEntity>& tablet_metric_entity);
  ~TabletMetricsImpl() {}

  uint64_t Get(TabletCounters counter) const override;

  void IncrementBy(TabletCounters counter, uint64_t amount) override;

  void IncrementBy(TabletHistograms histogram, uint64_t value, uint64_t amount) override;

 private:
  std::vector<scoped_refptr<Histogram>> histograms_;
  std::vector<scoped_refptr<yb::Counter>> counters_;
};

TabletMetricsImpl::TabletMetricsImpl(const scoped_refptr<MetricEntity>& table_entity,
                                     const scoped_refptr<MetricEntity>& tablet_entity)
  : histograms_(kElementsInTabletHistograms),
    counters_(kElementsInTabletCounters) {

#define METRIC_INIT(entity, name, metric_array, enum) \
    metric_array[to_underlying(enum)] = METRIC_##name.Instantiate(entity)
#define HISTOGRAM_INIT(entity, name, enum) \
    METRIC_INIT(entity, name, histograms_, TabletHistograms::enum)
#define COUNTER_INIT(entity, name, enum) \
    METRIC_INIT(entity, name, counters_, TabletCounters::enum)

  HISTOGRAM_INIT(table_entity, snapshot_read_inflight_wait_duration,
                 kSnapshotReadInflightWaitDuration);
  HISTOGRAM_INIT(table_entity, ql_read_latency, kQlReadLatency);
  HISTOGRAM_INIT(table_entity, write_lock_latency, kWriteLockLatency);
  HISTOGRAM_INIT(table_entity, ql_write_latency, kQlWriteLatency);
  HISTOGRAM_INIT(table_entity, read_time_wait, kReadTimeWait);

  COUNTER_INIT(tablet_entity, not_leader_rejections, kNotLeaderRejections);
  COUNTER_INIT(tablet_entity, leader_memory_pressure_rejections, kLeaderMemoryPressureRejections);
  COUNTER_INIT(tablet_entity, majority_sst_files_rejections, kMajoritySstFilesRejections);
  COUNTER_INIT(tablet_entity, transaction_conflicts, kTransactionConflicts);
  COUNTER_INIT(tablet_entity, expired_transactions, kExpiredTransactions);
  COUNTER_INIT(tablet_entity, restart_read_requests, kRestartReadRequests);
  COUNTER_INIT(tablet_entity, consistent_prefix_read_requests, kConsistentPrefixReadRequests);
  COUNTER_INIT(tablet_entity, pgsql_consistent_prefix_read_rows, kPgsqlConsistentPrefixReadRows);
  COUNTER_INIT(tablet_entity, tablet_data_corruptions, kTabletDataCorruptions);
  COUNTER_INIT(tablet_entity, rows_inserted, kRowsInserted);
  COUNTER_INIT(tablet_entity, failed_batch_lock, kFailedBatchLock);
  COUNTER_INIT(tablet_entity, docdb_keys_found, kDocDBKeysFound);
  COUNTER_INIT(tablet_entity, docdb_obsolete_keys_found, kDocDBObsoleteKeysFound);
  COUNTER_INIT(tablet_entity, docdb_obsolete_keys_found_past_cutoff,
               kDocDBObsoleteKeysFoundPastCutoff);

#undef COUNTER_INIT
#undef HISTOGRAM_INIT
#undef METRIC_INIT
}

uint64_t TabletMetricsImpl::Get(TabletCounters counter) const {
  return counters_[to_underlying(counter)]->value();
}

void TabletMetricsImpl::IncrementBy(TabletCounters counter, uint64_t amount) {
  counters_[to_underlying(counter)]->IncrementBy(amount);
}

void TabletMetricsImpl::IncrementBy(
    TabletHistograms histogram, uint64_t value, uint64_t amount) {
  histograms_[to_underlying(histogram)]->IncrementBy(value, amount);
}

} // namespace

TabletMetrics::TabletMetrics()
  : instance_id_(tablet_metrics_instance_counter.fetch_add(1, std::memory_order_relaxed)) {}

ScopedTabletMetrics::ScopedTabletMetrics(): counters_(kElementsInTabletCounters, 0) { }

ScopedTabletMetrics::~ScopedTabletMetrics() { }

#if DCHECK_IS_ON()
#define DCHECK_IN_USE() DCHECK(in_use_)
#else
#define DCHECK_IN_USE()
#endif

uint64_t ScopedTabletMetrics::Get(TabletCounters counter) const {
  DCHECK_IN_USE();
  return counters_[to_underlying(counter)];
}

void ScopedTabletMetrics::IncrementBy(TabletCounters counter, uint64_t amount) {
  DCHECK_IN_USE();
  counters_[to_underlying(counter)] += amount;
}

void ScopedTabletMetrics::IncrementBy(
    TabletHistograms histogram, uint64_t value, uint64_t amount) {
  DCHECK_IN_USE();
  histogram_context_->IncrementBy(histogram, value, amount);
}

void ScopedTabletMetrics::Prepare() {
#if DCHECK_IS_ON()
  DCHECK(!in_use_);
  in_use_ = true;
#endif
}

void ScopedTabletMetrics::SetHistogramContext(TabletMetrics* histogram_context) {
  histogram_context_ = histogram_context;
}

void ScopedTabletMetrics::CopyToPgsqlResponse(PgsqlResponsePB* response) const {
  DCHECK_IN_USE();
  auto* metrics = response->mutable_metrics();
  for (const auto& [pggate_index, tablet_counter] : kCounters) {
    auto counter = counters_[to_underlying(tablet_counter)];
    // Don't send unchanged statistics.
    if (counter == 0) {
      continue;
    }
    auto* metric = metrics->add_gauge_metrics();
    metric->set_metric(pggate_index);
    metric->set_value(counter);
  }
}

void ScopedTabletMetrics::MergeAndClear(TabletMetrics* target) {
  DCHECK_IN_USE();

  for (size_t i = 0; i < counters_.size(); ++i) {
    if (counters_[i] > 0) {
      target->IncrementBy(static_cast<TabletCounters>(i), counters_[i]);
      counters_[i] = 0;
    }
  }

#if DCHECK_IS_ON()
  in_use_ = false;
#endif
  histogram_context_ = nullptr;
}

std::unique_ptr<TabletMetrics> CreateTabletMetrics(
    const scoped_refptr<MetricEntity>& table_metric_entity,
    const scoped_refptr<MetricEntity>& tablet_metric_entity) {
  return std::make_unique<TabletMetricsImpl>(table_metric_entity, tablet_metric_entity);
}

ScopedTabletMetricsLatencyTracker::ScopedTabletMetricsLatencyTracker(
    TabletMetrics* tablet_metrics, TabletHistograms histogram)
    : metrics_(tablet_metrics), histogram_(histogram), start_time_(MonoTime::Now()) {}

ScopedTabletMetricsLatencyTracker::~ScopedTabletMetricsLatencyTracker() {
  metrics_->Increment(
      histogram_,
      MonoTime::Now().GetDeltaSince(start_time_).ToMicroseconds());
}
} // namespace tablet
} // namespace yb
