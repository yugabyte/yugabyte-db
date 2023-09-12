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

METRIC_DEFINE_event_stats(table, ql_write_latency, "Write latency at tserver layer",
  yb::MetricUnit::kMicroseconds,
  "Time taken to handle a batch of writes at tserver layer");

METRIC_DEFINE_event_stats(table, snapshot_read_inflight_wait_duration,
  "Time Waiting For Snapshot Reads",
  yb::MetricUnit::kMicroseconds,
  "Time spent waiting for in-flight writes to complete for READ_AT_SNAPSHOT scans.");

METRIC_DEFINE_event_stats(
    table, ql_read_latency, "Handle ReadRequest latency at tserver layer",
    yb::MetricUnit::kMicroseconds,
    "Time taken to handle the read request at the tserver layer.");

METRIC_DEFINE_event_stats(
    table, write_lock_latency, "Write lock latency", yb::MetricUnit::kMicroseconds,
    "Time taken to acquire key locks for a write operation");

METRIC_DEFINE_event_stats(
    table, read_time_wait, "Read Time Wait", yb::MetricUnit::kMicroseconds,
    "Number of microseconds read queries spend waiting for safe time");

METRIC_DEFINE_event_stats(
    table, total_wait_queue_time, "Wait Queue Time", yb::MetricUnit::kMicroseconds,
    "Number of microseconds spent in the wait queue for requests which enter the wait queue");

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

METRIC_DEFINE_counter(tablet, picked_read_time_on_docdb,
    "Picked read time on docdb",
    yb::MetricUnit::kRequests,
    "Number of times, a read time was picked on docdb instead of the query layer for read/write "
    "requests");

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

struct CounterEntry {
  uint32_t pggate_index;
  TabletCounters counter;
  CounterPrototype* prototype;
};

struct EventStatsEntry {
  TabletEventStats event_stat;
  EventStatsPrototype* prototype;
};

const CounterEntry kCounters[] = {
  {pggate::YB_ANALYZE_METRIC_NOT_LEADER_REJECTIONS,
      TabletCounters::kNotLeaderRejections,
      &METRIC_not_leader_rejections},
  {pggate::YB_ANALYZE_METRIC_LEADER_MEMORY_PRESSURE_REJECTIONS,
      TabletCounters::kLeaderMemoryPressureRejections,
      &METRIC_leader_memory_pressure_rejections},
  {pggate::YB_ANALYZE_METRIC_MAJORITY_SST_FILES_REJECTIONS,
      TabletCounters::kMajoritySstFilesRejections,
      &METRIC_majority_sst_files_rejections},
  {pggate::YB_ANALYZE_METRIC_TRANSACTION_CONFLICTS,
      TabletCounters::kTransactionConflicts,
      &METRIC_transaction_conflicts},
  {pggate::YB_ANALYZE_METRIC_EXPIRED_TRANSACTIONS,
      TabletCounters::kExpiredTransactions,
      &METRIC_expired_transactions},
  {pggate::YB_ANALYZE_METRIC_RESTART_READ_REQUESTS,
      TabletCounters::kRestartReadRequests,
      &METRIC_restart_read_requests},
  {pggate::YB_ANALYZE_METRIC_CONSISTENT_PREFIX_READ_REQUESTS,
      TabletCounters::kConsistentPrefixReadRequests,
      &METRIC_consistent_prefix_read_requests},
  {pggate::YB_ANALYZE_METRIC_PICKED_READ_TIME_ON_DOCDB,
      TabletCounters::kPickReadTimeOnDocDB,
      &METRIC_picked_read_time_on_docdb},
  {pggate::YB_ANALYZE_METRIC_PGSQL_CONSISTENT_PREFIX_READ_ROWS,
      TabletCounters::kPgsqlConsistentPrefixReadRows,
      &METRIC_pgsql_consistent_prefix_read_rows},
  {pggate::YB_ANALYZE_METRIC_TABLET_DATA_CORRUPTIONS,
      TabletCounters::kTabletDataCorruptions,
      &METRIC_tablet_data_corruptions},
  {pggate::YB_ANALYZE_METRIC_ROWS_INSERTED,
      TabletCounters::kRowsInserted,
      &METRIC_rows_inserted},
  {pggate::YB_ANALYZE_METRIC_FAILED_BATCH_LOCK,
      TabletCounters::kFailedBatchLock,
      &METRIC_failed_batch_lock},
  {pggate::YB_ANALYZE_METRIC_DOCDB_KEYS_FOUND,
      TabletCounters::kDocDBKeysFound,
      &METRIC_docdb_keys_found},
  {pggate::YB_ANALYZE_METRIC_DOCDB_OBSOLETE_KEYS_FOUND,
      TabletCounters::kDocDBObsoleteKeysFound,
      &METRIC_docdb_obsolete_keys_found},
  {pggate::YB_ANALYZE_METRIC_DOCDB_OBSOLETE_KEYS_FOUND_PAST_CUTOFF,
      TabletCounters::kDocDBObsoleteKeysFoundPastCutoff,
      &METRIC_docdb_obsolete_keys_found_past_cutoff},
};

const EventStatsEntry kEventStats[] = {
  {TabletEventStats::kSnapshotReadInflightWaitDuration,
      &METRIC_snapshot_read_inflight_wait_duration},
  {TabletEventStats::kQlReadLatency,
      &METRIC_ql_read_latency},
  {TabletEventStats::kWriteLockLatency,
      &METRIC_write_lock_latency},
  {TabletEventStats::kQlWriteLatency,
      &METRIC_ql_write_latency},
  {TabletEventStats::kReadTimeWait,
      &METRIC_read_time_wait},
  {TabletEventStats::kTotalWaitQueueTime,
      &METRIC_total_wait_queue_time},
};

class TabletMetricsImpl final : public TabletMetrics {
 public:
  TabletMetricsImpl(const scoped_refptr<MetricEntity>& table_metric_entity,
                    const scoped_refptr<MetricEntity>& tablet_metric_entity);
  ~TabletMetricsImpl() {}

  uint64_t Get(TabletCounters counter) const override;

  void IncrementBy(TabletCounters counter, uint64_t amount) override;

  void IncrementBy(TabletEventStats event_stats, uint64_t value, uint64_t amount) override;

 private:
  std::vector<scoped_refptr<EventStats>> event_stats_;
  std::vector<scoped_refptr<Counter>> counters_;
};

TabletMetricsImpl::TabletMetricsImpl(const scoped_refptr<MetricEntity>& table_entity,
                                     const scoped_refptr<MetricEntity>& tablet_entity)
  : event_stats_(kElementsInTabletEventStats),
    counters_(kElementsInTabletCounters) {

  for (const auto& stat : kEventStats) {
    event_stats_[to_underlying(stat.event_stat)] = stat.prototype->Instantiate(table_entity);
  }

  for (const auto& counter : kCounters) {
    counters_[to_underlying(counter.counter)] = counter.prototype->Instantiate(tablet_entity);
  }
}

uint64_t TabletMetricsImpl::Get(TabletCounters counter) const {
  return counters_[to_underlying(counter)]->value();
}

void TabletMetricsImpl::IncrementBy(TabletCounters counter, uint64_t amount) {
  counters_[to_underlying(counter)]->IncrementBy(amount);
}

void TabletMetricsImpl::IncrementBy(
    TabletEventStats event_stats, uint64_t value, uint64_t amount) {
  event_stats_[to_underlying(event_stats)]->IncrementBy(value, amount);
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
    TabletEventStats event_stats, uint64_t value, uint64_t amount) {
  DCHECK_IN_USE();
  histogram_context_->IncrementBy(event_stats, value, amount);
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
  for (const auto& counter : kCounters) {
    auto value = counters_[to_underlying(counter.counter)];
    // Don't send unchanged statistics.
    if (value == 0) {
      continue;
    }
    auto* metric = metrics->add_gauge_metrics();
    metric->set_metric(counter.pggate_index);
    metric->set_value(value);
  }
}

size_t ScopedTabletMetrics::Dump(std::stringstream* out) const {
  DCHECK_IN_USE();
  size_t dumped = 0;
  for (const auto& counter : kCounters) {
    auto value = counters_[to_underlying(counter.counter)];
    // Don't dump unchanged statistics.
    if (value == 0) {
      continue;
    }
    const auto* name = counter.prototype->name();
    (*out) << name << ": " << value << '\n';
    ++dumped;
  }
  return dumped;
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
    TabletMetrics* tablet_metrics, TabletEventStats event_stats)
    : metrics_(tablet_metrics), event_stats_(event_stats), start_time_(MonoTime::Now()) {}

ScopedTabletMetricsLatencyTracker::~ScopedTabletMetricsLatencyTracker() {
  metrics_->Increment(
      event_stats_,
      MonoTime::Now().GetDeltaSince(start_time_).ToMicroseconds());
}
} // namespace tablet
} // namespace yb
