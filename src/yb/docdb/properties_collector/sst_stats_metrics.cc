// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/properties_collector/sst_stats_metrics.h"

#include <iterator>
#include <utility>

#include "yb/docdb/properties_collector/sst_stats_aggregator.h"

#include "yb/util/metrics.h"

// Definitions: properties_collector/README.md, "Vocabulary". The default kSum aggregation provides
// table- and server-level rollups. Table-level visibility also depends on priority_regex.
METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_total_entries,
    "DocDB SST Total Entries", yb::MetricUnit::kEntries,
    "Number of entries measured in the tablet's live SST files, counting every version of every "
    "key. Files without statistics are excluded.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_tombstone_entries,
    "DocDB SST Tombstone Entries", yb::MetricUnit::kEntries,
    "Number of tombstone entries measured in the tablet's live SST files.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_shadowed_entries,
    "DocDB SST Shadowed Entries", yb::MetricUnit::kEntries,
    "Sum of entries hidden by a newer version of the same subdocument key within each live SST. "
    "Cross-SST shadowing is not included. Reads zero while docdb_sst_files_with_partial_stats is "
    "non-zero, where this identity does not hold.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_repackable_entries,
    "DocDB SST Repackable Entries", yb::MetricUnit::kEntries,
    "Sum of subdocument-key heads beyond one per row within each live SST: a measure of what "
    "repacking could collapse, not of garbage. Reads zero while "
    "docdb_sst_files_with_partial_stats is non-zero, where this identity does not hold.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_dead_rows,
    "DocDB SST Dead Rows", yb::MetricUnit::kRows,
    "Sum of rows classified as dead within each live SST: their newest covering write in that file "
    "is a tombstone with nothing newer in the same file.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_dead_row_entries,
    "DocDB SST Dead Row Entries", yb::MetricUnit::kEntries,
    "Number of measured entries belonging to rows classified as dead within their live SST.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_reclaimable_entries,
    "DocDB SST Reclaimable Entries", yb::MetricUnit::kEntries,
    "Number of measured entries identified as garbage within their live SST, independent of "
    "whether history retention currently allows a full compaction to drop them.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_reclaimable_bytes,
    "DocDB SST Reclaimable Bytes", yb::MetricUnit::kBytes,
    "Raw key and value bytes of measured entries identified as garbage within their live SST. "
    "This is retention-independent and not comparable with compressed on-disk size.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_files_without_stats,
    "DocDB SST Files Without Statistics", yb::MetricUnit::kFiles,
    "Number of the tablet's live SST files carrying no statistics, because they were written "
    "before the collector was enabled or their properties could not be read. The other "
    "docdb_sst_* gauges measure the remaining files only, so a ratio built from them reads low "
    "while this is non-zero.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_files_with_partial_stats,
    "DocDB SST Files With Partial Statistics", yb::MetricUnit::kFiles,
    "Number of the tablet's measured live SST files whose chain statistics came out incomplete "
    "because a key did not parse. These files are measured, so they are not counted in "
    "docdb_sst_files_without_stats, but their chain and garbage counters are lower bounds.");

// Every other gauge here reports zero when the tablet has not been measured as a whole, which is
// indistinguishable from a real zero. This one is that distinction, so it is the one gauge that
// must report before the first resync.
METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_stats_available,
    "DocDB SST Statistics Available", yb::MetricUnit::kUnits,
    "1 once the tablet's whole live file set has been measured, 0 until then. Every other "
    "docdb_sst_* gauge reads zero while this is 0, so gate alerts and ratios on it instead of "
    "reading those zeros as measurements.");

namespace yb::docdb {

namespace {
using Snapshot = SstStatsAggregator::Snapshot;
}  // namespace

struct SstStatsMetrics::MetricInfo {
  using ValueFn = uint64_t (*)(const Snapshot&);

  GaugePrototype<uint64_t>& prototype;
  ValueFn value;
  // Cleared only for the gauge that reports whether the others mean anything.
  bool needs_resync = true;
};

struct SstStatsMetrics::MetricInfos {
  constexpr static MetricInfo kMetricInfos[] = {
      { METRIC_docdb_sst_total_entries,
        [](const Snapshot& s) { return s.aggregate.total_entries; } },
      { METRIC_docdb_sst_tombstone_entries,
        [](const Snapshot& s) { return s.aggregate.tombstone_entries; } },
      // The two derived identities hold over the sum only while every covered file was fully
      // chain-tracked. Once a key fails to parse, ChainTracker stops counting the whole chain, so
      // chain_entries, num_subdoc_keys and num_rows are all truncated by unrelated amounts and
      // their differences measure nothing. The subtraction itself is safe -- the helpers saturate.
      { METRIC_docdb_sst_shadowed_entries,
        [](const Snapshot& s) -> uint64_t {
          return s.aggregate.partial_files > 0 ? 0 : s.aggregate.shadowed_entries();
        } },
      { METRIC_docdb_sst_repackable_entries,
        [](const Snapshot& s) -> uint64_t {
          return s.aggregate.partial_files > 0 ? 0 : s.aggregate.repackable_entries();
        } },
      { METRIC_docdb_sst_dead_rows,
        [](const Snapshot& s) { return s.aggregate.dead_rows; } },
      { METRIC_docdb_sst_dead_row_entries,
        [](const Snapshot& s) { return s.aggregate.dead_row_entries; } },
      { METRIC_docdb_sst_reclaimable_entries,
        [](const Snapshot& s) { return s.aggregate.reclaimable_entries; } },
      { METRIC_docdb_sst_reclaimable_bytes,
        [](const Snapshot& s) { return s.aggregate.reclaimable_bytes; } },
      { METRIC_docdb_sst_files_without_stats,
        [](const Snapshot& s) { return s.aggregate.uncovered_files; } },
      { METRIC_docdb_sst_files_with_partial_stats,
        [](const Snapshot& s) { return s.aggregate.partial_files; } },
      { METRIC_docdb_sst_stats_available,
        [](const Snapshot& s) -> uint64_t { return s.last_resync_micros != 0 ? 1 : 0; },
        /* needs_resync = */ false },
  };
};

SstStatsMetrics::SstStatsMetrics(
    const MetricEntityPtr& entity, std::shared_ptr<const SstStatsAggregator> aggregator)
    : entity_(entity), aggregator_(std::move(aggregator)) {
  gauges_.reserve(std::size(MetricInfos::kMetricInfos));
  for (const auto& metric : MetricInfos::kMetricInfos) {
    // The entity outlives a regular DB reopen, and it keys gauges by prototype, so
    // InstantiateFunctionGauge would otherwise hand back a previous instance's gauge -- bound to
    // that instance's callback, and frozen once it detached.
    entity_->RemoveFromMetricMap(&metric.prototype);
    auto gauge = metric.prototype.InstantiateFunctionGauge(
        entity_, Bind(&SstStatsMetrics::CalculateMetric, Unretained(this), metric));
    gauge->AutoDetachToLastValue(&metric_detacher_);
    gauges_.push_back(std::move(gauge));
  }
}

SstStatsMetrics::~SstStatsMetrics() {
  // Detach first so a scrape that already retained a gauge cannot call this object after removal.
  metric_detacher_.reset();
  for (size_t i = 0; i != gauges_.size(); ++i) {
    const auto& metric = MetricInfos::kMetricInfos[i];
    // Only this instance's own gauges. A second instance constructed on the same entity replaced
    // them, and removing by prototype alone would strip that instance's gauges instead.
    if (entity_->FindOrNull<FunctionGauge<uint64_t>>(metric.prototype).get() == gauges_[i].get()) {
      entity_->RemoveFromMetricMap(&metric.prototype);
    }
  }
}

uint64_t SstStatsMetrics::CalculateMetric(const MetricInfo& metric) const {
  // Values scraped together can straddle one listener event because each gauge snapshots alone.
  const auto snapshot = aggregator_->Get();

  // Before the first resync, listener events may cover an arbitrary subset of inherited files.
  // Publishing it would undercount while files_without_stats could misleadingly remain zero.
  if (metric.needs_resync && snapshot.last_resync_micros == 0) {
    return 0;
  }
  return metric.value(snapshot);
}

}  // namespace yb::docdb
