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

#include <utility>

#include "yb/docdb/properties_collector/sst_stats_aggregator.h"

#include "yb/util/metrics.h"

// Definitions of the quantities below are in properties_collector/README.md, "Vocabulary". All of
// these take the default kSum aggregation, which is what makes the table- and server-level
// rollups add up the way the other docdb tablet metrics do. Appearing at table level additionally
// requires the metric name to match the scrape's priority_regex (see prometheus_metric_filter.cc);
// the default is ".*", but a deployment that narrows it has to add docdb_sst_.* to keep these.
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
    "Cross-SST shadowing is not included. Zero means unavailable if a file has partial chain "
    "statistics.");

METRIC_DEFINE_gauge_uint64(tablet, docdb_sst_repackable_entries,
    "DocDB SST Repackable Entries", yb::MetricUnit::kEntries,
    "Sum of subdocument-key heads beyond one per row within each live SST: a measure of what "
    "repacking could collapse, not of garbage. Zero means unavailable if a file has partial chain "
    "statistics.");

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

namespace yb::docdb {

struct SstStatsMetrics::MetricInfo {
  using ValueFn = uint64_t (*)(const SstStatsAggregate&);

  GaugePrototype<uint64_t>& prototype;
  ValueFn value;
};

struct SstStatsMetrics::MetricInfos {
  constexpr static MetricInfo kMetricInfos[] = {
      { METRIC_docdb_sst_total_entries,
        [](const SstStatsAggregate& a) { return a.total_entries; } },
      { METRIC_docdb_sst_tombstone_entries,
        [](const SstStatsAggregate& a) { return a.tombstone_entries; } },
      // The two derived identities hold over the sum only while every covered file was fully
      // chain-tracked. A partial file makes chain_entries a lower bound while num_subdoc_keys and
      // num_rows are not, so report the unavailable result as zero.
      { METRIC_docdb_sst_shadowed_entries,
        [](const SstStatsAggregate& a) -> uint64_t {
          return a.partial_files > 0 ? 0 : a.shadowed_entries();
        } },
      { METRIC_docdb_sst_repackable_entries,
        [](const SstStatsAggregate& a) -> uint64_t {
          return a.partial_files > 0 ? 0 : a.repackable_entries();
        } },
      { METRIC_docdb_sst_dead_rows,
        [](const SstStatsAggregate& a) { return a.dead_rows; } },
      { METRIC_docdb_sst_dead_row_entries,
        [](const SstStatsAggregate& a) { return a.dead_row_entries; } },
      { METRIC_docdb_sst_reclaimable_entries,
        [](const SstStatsAggregate& a) { return a.reclaimable_entries; } },
      { METRIC_docdb_sst_reclaimable_bytes,
        [](const SstStatsAggregate& a) { return a.reclaimable_bytes; } },
      { METRIC_docdb_sst_files_without_stats,
        [](const SstStatsAggregate& a) { return a.uncovered_files; } },
  };
};

SstStatsMetrics::SstStatsMetrics(
    const MetricEntityPtr& entity, std::shared_ptr<const SstStatsAggregator> aggregator)
    : entity_(entity), aggregator_(std::move(aggregator)) {
  for (const auto& metric : MetricInfos::kMetricInfos) {
    // The entity outlives a regular DB reopen. Defensively discard any gauge left by an earlier
    // owner so InstantiateFunctionGauge cannot return a detached one.
    entity_->RemoveFromMetricMap(&metric.prototype);
    metric.prototype.InstantiateFunctionGauge(
        entity_, Bind(&SstStatsMetrics::CalculateMetric, Unretained(this), metric))
      ->AutoDetachToLastValue(&metric_detacher_);
  }
}

SstStatsMetrics::~SstStatsMetrics() {
  // Detach first so a scrape that already retained a gauge cannot call this object after removal.
  metric_detacher_.reset();
  for (const auto& metric : MetricInfos::kMetricInfos) {
    entity_->RemoveFromMetricMap(&metric.prototype);
  }
}

uint64_t SstStatsMetrics::CalculateMetric(const MetricInfo& metric) const {
  // Each gauge takes its own snapshot, so values scraped together can straddle a flush. They are
  // read by humans and the skew is one event wide, which is not worth holding one snapshot across
  // a scrape for.
  const auto snapshot = aggregator_->Get();

  // Before the first resync the aggregate holds only the files the listener reported since open,
  // which on a tablet that inherited files is an arbitrary subset. Reporting that subset would
  // feed a plausible-looking undercount into the kSum rollups, and files_without_stats would read
  // zero while saying nothing about the files it never saw. All-zeros is at least legible as
  // "not measured yet".
  if (snapshot.last_resync_micros == 0) {
    return 0;
  }
  return metric.value(snapshot.aggregate);
}

}  // namespace yb::docdb
