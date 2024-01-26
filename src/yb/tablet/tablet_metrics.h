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
#pragma once

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"
#include "yb/util/monotime.h"

namespace yb {

class AggregateStats;
template<class T>
class AtomicGauge;
class Counter;
class EventStats;
class MetricEntity;
class PgsqlResponsePB;

namespace tablet {

YB_DEFINE_ENUM(TabletEventStats,
  (kCommitWaitDuration)
  (kSnapshotReadInflightWaitDuration)
  (kQlReadLatency)
  (kWriteLockLatency)
  (kQlWriteLatency)
  (kWriteOpDurationCommitWaitConsistency)
  (kReadTimeWait)
  (kTotalWaitQueueTime))

// Make sure to add new counters to the list in src/yb/yql/pggate/pg_metrics_list.h as well.
YB_DEFINE_ENUM(TabletCounters,
  (kNotLeaderRejections)
  (kLeaderMemoryPressureRejections)
  (kMajoritySstFilesRejections)
  (kTransactionConflicts)
  (kExpiredTransactions)
  (kRestartReadRequests)
  (kConsistentPrefixReadRequests)
  (kPickReadTimeOnDocDB)
  (kPgsqlConsistentPrefixReadRows)
  (kTabletDataCorruptions)
  (kRowsInserted)
  (kFailedBatchLock)
  (kDocDBKeysFound)
  (kDocDBObsoleteKeysFound)
  (kDocDBObsoleteKeysFoundPastCutoff))

YB_DEFINE_ENUM(TabletGauges, (kActiveWriteQueryObjects))

// Container for all metrics specific to a single tablet.
class TabletMetrics {
 public:
  TabletMetrics();
  virtual ~TabletMetrics() {}

  uint64_t InstanceId() const { return instance_id_; }

  virtual uint64_t Get(TabletCounters counter) const = 0;

  virtual int64_t Get(TabletGauges gauge) const = 0;

  void Increment(TabletCounters counter) {
    IncrementBy(counter, 1);
  }

  virtual void IncrementBy(TabletCounters counter, uint64_t amount) = 0;

  void Increment(TabletGauges gauge) {
    IncrementBy(gauge, 1);
  }

  void Decrement(TabletGauges gauge) {
    IncrementBy(gauge, -1);
  }

  virtual void IncrementBy(TabletGauges gauge, int64_t amount) = 0;

  void Increment(TabletEventStats event_stats, uint64_t value) {
    IncrementBy(event_stats, value, 1);
  }

  virtual void IncrementBy(TabletEventStats event_stats, uint64_t value, uint64_t amount) = 0;

  virtual void AddAggregateStats(TabletEventStats event_stats, const AggregateStats& other) = 0;

 private:
  // Keeps track of the number of instances created for verification that the metrics belong
  // to the same tablet instance.
  const uint64_t instance_id_;
};

std::unique_ptr<TabletMetrics> CreateTabletMetrics(
    const scoped_refptr<MetricEntity>& table_metric_entity,
    const scoped_refptr<MetricEntity>& tablet_metric_entity);

class ScopedTabletMetrics final : public TabletMetrics {
 public:
  ScopedTabletMetrics();
  ~ScopedTabletMetrics();

  uint64_t Get(TabletCounters counter) const override;

  int64_t Get(TabletGauges gauge) const override;

  void IncrementBy(TabletCounters counter, uint64_t amount) override;

  void IncrementBy(TabletGauges gauge, int64_t amount) override;

  void IncrementBy(TabletEventStats event_stats, uint64_t value, uint64_t amount) override;

  void AddAggregateStats(TabletEventStats event_stats, const AggregateStats& other) override;

  void CopyToPgsqlResponse(PgsqlResponsePB* response) const;

  // Returns number of metric changes dumped.
  size_t Dump(std::stringstream* out) const;

  void MergeAndClear(TabletMetrics* target);

 private:
  std::vector<uint64_t> counters_;
  std::vector<int64_t> gauges_;
  std::vector<AggregateStats> stats_;
};

class ScopedTabletMetricsLatencyTracker {
 public:
  ScopedTabletMetricsLatencyTracker(TabletMetrics* tablet_metrics, TabletEventStats event_stats);
  ~ScopedTabletMetricsLatencyTracker();

 private:
  TabletMetrics* metrics_;
  TabletEventStats event_stats_;
  MonoTime start_time_;
};

} // namespace tablet
} // namespace yb
