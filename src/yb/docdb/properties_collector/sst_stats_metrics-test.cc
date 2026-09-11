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

#include <memory>
#include <string>
#include <vector>

#include "yb/docdb/properties_collector/sst_stats_aggregator.h"
#include "yb/docdb/properties_collector/sst_stats_collector.h"

#include "yb/rocksdb/db/filename.h"

#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

METRIC_DECLARE_gauge_uint64(docdb_sst_total_entries);
METRIC_DECLARE_gauge_uint64(docdb_sst_tombstone_entries);
METRIC_DECLARE_gauge_uint64(docdb_sst_shadowed_entries);
METRIC_DECLARE_gauge_uint64(docdb_sst_repackable_entries);
METRIC_DECLARE_gauge_uint64(docdb_sst_dead_rows);
METRIC_DECLARE_gauge_uint64(docdb_sst_dead_row_entries);
METRIC_DECLARE_gauge_uint64(docdb_sst_reclaimable_entries);
METRIC_DECLARE_gauge_uint64(docdb_sst_reclaimable_bytes);
METRIC_DECLARE_gauge_uint64(docdb_sst_files_without_stats);

// The gauges are tablet-entity metrics and MetricEntity::CheckInstantiation enforces that, but the
// prototype that names the tablet entity lives in yb_tablet, which this test does not link. A
// local one of the same name satisfies the check without the dependency.
METRIC_DEFINE_entity(tablet);

namespace yb::docdb {

namespace {

constexpr auto kDbPath = "/db";

rocksdb::TableProperties FileProperties(uint64_t entries, const SstStats* stats) {
  rocksdb::TableProperties properties;
  properties.num_entries = entries;
  properties.raw_key_size = entries * 10;
  properties.raw_value_size = entries * 10;
  if (stats != nullptr) {
    SstStatsToProperties(*stats, &properties.user_collected_properties);
  }
  return properties;
}

// A file whose counters are all distinct, so that a gauge wired to the wrong field is visible.
SstStats FileStats(uint64_t entries) {
  SstStats stats;
  stats.total_entries = entries;
  stats.chain_entries = entries;
  stats.tombstone_entries = entries / 2;
  stats.num_subdoc_keys = entries / 4;
  stats.num_rows = entries / 8;
  stats.dead_rows = entries / 16;
  stats.dead_row_entries = entries / 32;
  stats.reclaimable_entries = entries / 64;
  stats.reclaimable_bytes = entries / 64 * 10;
  return stats;
}

// Installs `files` as the whole live file set. A null SstStats stands for a file written before
// the collector existed.
SstStatsAggregator::SnapshotSource SnapshotOf(std::vector<const SstStats*> files) {
  auto shared_files = std::make_shared<std::vector<const SstStats*>>(std::move(files));
  return {
      .live_files = [shared_files](std::vector<rocksdb::LiveFileMetaData>* live_files) {
        uint64_t file_number = 1;
        for (size_t i = 0; i != shared_files->size(); ++i) {
          live_files->push_back(rocksdb::LiveFileMetaData(
              "default", /* level = */ 0, file_number++, kDbPath, /* total_size = */ 0,
              /* base_size = */ 0, /* uncompressed_size = */ 0, {}, {},
              /* imported = */ false, /* being_compacted = */ false));
        }
        return Status::OK();
      },
      .properties = [shared_files](rocksdb::TablePropertiesCollection* properties) {
        uint64_t file_number = 1;
        for (const auto* stats : *shared_files) {
          const auto path = rocksdb::MakeTableFileName(kDbPath, file_number++);
          (*properties)[path] = std::make_shared<rocksdb::TableProperties>(
              FileProperties(/* entries = */ 1000, stats));
        }
        return Status::OK();
      },
  };
}

}  // namespace

class SstStatsMetricsTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    entity_ = METRIC_ENTITY_tablet.Instantiate(&registry_, "test-tablet");
  }

  uint64_t Read(const GaugePrototype<uint64_t>& prototype) {
    const auto gauge = entity_->FindOrNull<FunctionGauge<uint64_t>>(prototype);
    CHECK_NOTNULL(gauge.get());
    return gauge->value();
  }

  MetricRegistry registry_;
  MetricEntityPtr entity_;
};

TEST_F(SstStatsMetricsTest, ReportsNothingBeforeFirstResync) {
  auto aggregator = std::make_shared<SstStatsAggregator>();
  SstStatsMetrics metrics(entity_, aggregator);

  const auto stats = FileStats(/* entries = */ 640);
  rocksdb::FlushJobInfo flush{};
  flush.file_path = rocksdb::MakeTableFileName(kDbPath, 1);
  flush.table_properties = FileProperties(/* entries = */ 1000, &stats);
  aggregator->OnFlushCompleted(flush);

  // The aggregate holds the flushed file, but nothing has looked at the whole file set yet, so the
  // gauges must not present it as the tablet's totals.
  ASSERT_EQ(aggregator->Get().aggregate.total_entries, 640);
  EXPECT_EQ(Read(METRIC_docdb_sst_total_entries), 0);
  EXPECT_EQ(Read(METRIC_docdb_sst_reclaimable_entries), 0);
  EXPECT_EQ(Read(METRIC_docdb_sst_files_without_stats), 0);
}

TEST_F(SstStatsMetricsTest, UsesSumAggregation) {
  for (const auto* prototype : {
           &METRIC_docdb_sst_total_entries,
           &METRIC_docdb_sst_tombstone_entries,
           &METRIC_docdb_sst_shadowed_entries,
           &METRIC_docdb_sst_repackable_entries,
           &METRIC_docdb_sst_dead_rows,
           &METRIC_docdb_sst_dead_row_entries,
           &METRIC_docdb_sst_reclaimable_entries,
           &METRIC_docdb_sst_reclaimable_bytes,
           &METRIC_docdb_sst_files_without_stats,
       }) {
    EXPECT_EQ(prototype->aggregation_function(), AggregationFunction::kSum);
  }
}

TEST_F(SstStatsMetricsTest, ReportsAggregateAfterResync) {
  auto aggregator = std::make_shared<SstStatsAggregator>();
  SstStatsMetrics metrics(entity_, aggregator);

  const auto stats = FileStats(/* entries = */ 640);
  ASSERT_OK(aggregator->Resync(SnapshotOf({&stats, &stats})));

  // Two identical files, so every gauge reads twice the per-file field.
  EXPECT_EQ(Read(METRIC_docdb_sst_total_entries), 1280);
  EXPECT_EQ(Read(METRIC_docdb_sst_tombstone_entries), 640);
  EXPECT_EQ(Read(METRIC_docdb_sst_dead_rows), 80);
  EXPECT_EQ(Read(METRIC_docdb_sst_dead_row_entries), 40);
  EXPECT_EQ(Read(METRIC_docdb_sst_reclaimable_entries), 20);
  EXPECT_EQ(Read(METRIC_docdb_sst_reclaimable_bytes), 200);
  EXPECT_EQ(Read(METRIC_docdb_sst_files_without_stats), 0);
  // Derived: chain_entries - num_subdoc_keys and num_subdoc_keys - num_rows over the sum.
  EXPECT_EQ(Read(METRIC_docdb_sst_shadowed_entries), 1280 - 320);
  EXPECT_EQ(Read(METRIC_docdb_sst_repackable_entries), 320 - 160);
}

TEST_F(SstStatsMetricsTest, CountsFilesWithoutStats) {
  auto aggregator = std::make_shared<SstStatsAggregator>();
  SstStatsMetrics metrics(entity_, aggregator);

  const auto stats = FileStats(/* entries = */ 640);
  ASSERT_OK(aggregator->Resync(SnapshotOf({&stats, nullptr, nullptr})));

  EXPECT_EQ(Read(METRIC_docdb_sst_files_without_stats), 2);
  // The measured gauges cover the one file that carries statistics, and nothing pretends the
  // other two contributed.
  EXPECT_EQ(Read(METRIC_docdb_sst_total_entries), 640);
}

TEST_F(SstStatsMetricsTest, PartialFileSuppressesDerivedGauges) {
  auto aggregator = std::make_shared<SstStatsAggregator>();
  SstStatsMetrics metrics(entity_, aggregator);

  auto partial = FileStats(/* entries = */ 640);
  // A key that did not parse: the chain counters are a lower bound, so the identities over the sum
  // no longer hold and their subtraction could wrap.
  partial.chain_valid = false;
  partial.chain_entries = 0;
  ASSERT_OK(aggregator->Resync(SnapshotOf({&partial})));

  ASSERT_EQ(aggregator->Get().aggregate.partial_files, 1);
  EXPECT_EQ(Read(METRIC_docdb_sst_shadowed_entries), 0);
  EXPECT_EQ(Read(METRIC_docdb_sst_repackable_entries), 0);
  // The additive counters are still meaningful.
  EXPECT_EQ(Read(METRIC_docdb_sst_total_entries), 640);
  EXPECT_EQ(Read(METRIC_docdb_sst_reclaimable_entries), 10);
}

TEST_F(SstStatsMetricsTest, GaugesFollowAReopenedDb) {
  const auto stats = FileStats(/* entries = */ 640);

  auto first = std::make_shared<SstStatsAggregator>();
  auto metrics = std::make_unique<SstStatsMetrics>(entity_, first);
  ASSERT_OK(first->Resync(SnapshotOf({&stats})));
  ASSERT_EQ(Read(METRIC_docdb_sst_total_entries), 640);

  // What truncate and snapshot restore do: the tablet reopens its regular DB, building a second
  // aggregator and a second set of gauges against the same entity. The gauges must read the new
  // aggregator rather than stay frozen at the old one's last value.
  auto second = std::make_shared<SstStatsAggregator>();
  metrics.reset();
  metrics = std::make_unique<SstStatsMetrics>(entity_, second);
  ASSERT_OK(second->Resync(SnapshotOf({&stats, &stats, &stats})));
  EXPECT_EQ(Read(METRIC_docdb_sst_total_entries), 1920);

  // Destroying the owner removes its gauges, while a scrape that already retained one sees its
  // detached final value rather than calling a dead aggregator.
  const auto retained_gauge =
      entity_->FindOrNull<FunctionGauge<uint64_t>>(METRIC_docdb_sst_total_entries);
  ASSERT_NE(retained_gauge, nullptr);
  metrics.reset();
  EXPECT_EQ(
      entity_->FindOrNull<FunctionGauge<uint64_t>>(METRIC_docdb_sst_total_entries), nullptr);
  EXPECT_EQ(retained_gauge->value(), 1920);
}

TEST_F(SstStatsMetricsTest, MetricsOwnAggregatorUntilDetached) {
  auto aggregator = std::make_shared<SstStatsAggregator>();
  std::weak_ptr<SstStatsAggregator> weak_aggregator = aggregator;
  auto metrics = std::make_unique<SstStatsMetrics>(entity_, aggregator);

  aggregator.reset();
  EXPECT_FALSE(weak_aggregator.expired());

  metrics.reset();
  EXPECT_TRUE(weak_aggregator.expired());
}

}  // namespace yb::docdb
