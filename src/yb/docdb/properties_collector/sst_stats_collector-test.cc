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

#include "yb/docdb/properties_collector/sst_stats_collector.h"

#include "yb/docdb/properties_collector/chain_tracker_test_util.h"

#include "yb/util/flags.h"
#include "yb/util/test_util.h"

DECLARE_uint32(sst_tombstone_mark_ratio_percent);
DECLARE_uint64(sst_tombstone_mark_min_count);

namespace yb::docdb {

// ChainTracker semantics are covered by chain_tracker-test; this file covers the properties
// (de)serialization and the rocksdb::TablePropertiesCollector wrapper.
class SstStatsCollectorTest : public YBTest {};

TEST_F(SstStatsCollectorTest, PropertiesRoundTrip) {
  auto fixture = TrackerFixture(/* subtotals = */ true);
  // One colocated row (two versions) so the coprefix-subtotal property is exercised too: plain
  // rows record no subtotals. The colocation-id prefix byte sorts before hash-partitioned keys,
  // so these entries come first in file order.
  const ColocationId colocation_id = 16385;
  fixture.Add({RowKey(ColocatedRow(colocation_id, 1), 1 * kHour), PackedRow("new")});
  fixture.Add({RowKey(ColocatedRow(colocation_id, 1), 2 * kHour), PackedRow("old")});
  fixture.Add(AnatomyStrip());
  const auto s = fixture.Finish();
  rocksdb::UserCollectedProperties properties;
  SstStatsToProperties(s, &properties);
  EXPECT_EQ(properties.at(std::string(SstStatsPropertyKeys::kCollectorVersion)),
            kSstStatsCollectorVersion);
  // The anatomy strip's 6 plus the shadowed colocated version.
  EXPECT_EQ(properties.at(std::string(SstStatsPropertyKeys::kReclaimableEntries)), "7");
  // The shadowed colocated version's overwriter is 1 h old: the lower edge of band 3 (1-6 h).
  EXPECT_EQ(properties.at(std::string(SstStatsPropertyKeys::kDroppableAgeEntries)),
            "1,1,1,1,0,3,0,0");

  const auto parsed = ASSERT_RESULT(SstStatsFromProperties(properties));
  EXPECT_EQ(parsed.total_entries, s.total_entries);
  EXPECT_EQ(parsed.tombstone_entries, s.tombstone_entries);
  EXPECT_EQ(parsed.chain_entries, s.chain_entries);
  EXPECT_EQ(parsed.chain_bytes, s.chain_bytes);
  EXPECT_EQ(parsed.num_subdoc_keys, s.num_subdoc_keys);
  EXPECT_EQ(parsed.num_rows, s.num_rows);
  EXPECT_EQ(parsed.dead_rows, s.dead_rows);
  EXPECT_EQ(parsed.dead_row_entries, s.dead_row_entries);
  EXPECT_EQ(parsed.reclaimable_entries, s.reclaimable_entries);
  EXPECT_EQ(parsed.reclaimable_bytes, s.reclaimable_bytes);
  EXPECT_EQ(parsed.max_row_chain, s.max_row_chain);
  EXPECT_EQ(parsed.max_stretch, s.max_stretch);
  EXPECT_EQ(parsed.chain_valid, s.chain_valid);
  EXPECT_EQ(parsed.min_write_ht, s.min_write_ht);
  EXPECT_EQ(parsed.max_write_ht, s.max_write_ht);
  EXPECT_EQ(parsed.anchor_micros, s.anchor_micros);
  EXPECT_EQ(parsed.row_chain_hist, s.row_chain_hist);
  EXPECT_EQ(parsed.row_chain_bytes_hist, s.row_chain_bytes_hist);
  EXPECT_EQ(parsed.stretch_hist, s.stretch_hist);
  EXPECT_EQ(parsed.stretch_entries_hist, s.stretch_entries_hist);
  EXPECT_EQ(parsed.stretch_bytes_hist, s.stretch_bytes_hist);
  EXPECT_EQ(parsed.droppable_age_entries, s.droppable_age_entries);
  EXPECT_EQ(parsed.droppable_age_bytes, s.droppable_age_bytes);
  ASSERT_EQ(parsed.coprefix_subtotals.size(), 1);
  EXPECT_EQ(parsed.coprefix_subtotals.begin()->second.rows, 1);
  EXPECT_EQ(parsed.coprefix_subtotals.begin()->second.entries, 2);
  EXPECT_EQ(parsed.coprefix_subtotals.begin()->second.reclaimable_entries, 1);

  EXPECT_NOK(SstStatsFromProperties(rocksdb::UserCollectedProperties()));
}

TEST_F(SstStatsCollectorTest, CollectorEndToEnd) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_sst_tombstone_mark_min_count) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_sst_tombstone_mark_ratio_percent) = 50;

  auto factory = MakeSstStatsCollectorFactory();
  std::unique_ptr<rocksdb::TablePropertiesCollector> collector(
      factory->CreateTablePropertiesCollector(rocksdb::TablePropertiesCollectorFactory::Context()));
  for (const auto& e : AnatomyStrip()) {
    ASSERT_OK(collector->AddUserKey(e.key, e.value, rocksdb::kEntryPut, 0, 0));
  }
  // 1 tombstone out of 9 entries: below the 50% marking threshold.
  EXPECT_FALSE(collector->NeedCompact());

  rocksdb::UserCollectedProperties properties;
  ASSERT_OK(collector->Finish(&properties));
  const auto s = ASSERT_RESULT(SstStatsFromProperties(properties));
  EXPECT_EQ(s.total_entries, 9);
  EXPECT_EQ(s.reclaimable_entries, 6);
  EXPECT_EQ(s.num_rows, 2);
  // The anchor is taken from the wall clock at construction; the test entries are dated relative
  // to a fixed 2023 anchor, so every band lands in the oldest bucket here.
  EXPECT_GT(s.anchor_micros, kAnchorMicros);
  EXPECT_EQ(s.droppable_age_entries[AgeBands::kNumBands - 1], 6);

  // A tombstone-heavy file trips the MANIFEST mark.
  std::unique_ptr<rocksdb::TablePropertiesCollector> heavy(
      factory->CreateTablePropertiesCollector(rocksdb::TablePropertiesCollectorFactory::Context()));
  for (int32_t id = 0; id < 10; ++id) {
    ASSERT_OK(heavy->AddUserKey(RowKey(Row(id), kHour), Tombstone(), rocksdb::kEntryPut, 0, 0));
  }
  EXPECT_TRUE(heavy->NeedCompact());
}

}  // namespace yb::docdb
