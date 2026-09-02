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

#include "yb/docdb/properties_collector/chain_tracker.h"

#include "yb/docdb/properties_collector/chain_tracker_test_util.h"

#include "yb/util/test_util.h"

namespace yb::docdb {

class ChainTrackerTest : public YBTest {};

TEST_F(ChainTrackerTest, AnatomyStrip) {
  const auto s = TrackerFixture().Add(AnatomyStrip()).Finish();
  ASSERT_NO_FATALS(ExpectIdentities(s));

  EXPECT_EQ(s.total_entries, 9);
  EXPECT_EQ(s.chain_entries, 9);
  EXPECT_EQ(s.meta_entries, 0);
  EXPECT_EQ(s.tombstone_entries, 1);
  EXPECT_EQ(s.packed_row_entries, 3);
  // Subdoc keys: r1 packed, r1.a, r1.b, r2 tombstone, r2.a, r2.b.
  EXPECT_EQ(s.num_subdoc_keys, 6);
  EXPECT_EQ(s.num_rows, 2);
  EXPECT_EQ(s.shadowed_entries(), 3);      // packed v2, packed v1, col a v1
  EXPECT_EQ(s.repackable_entries(), 4);    // r1.a, r1.b, r2.a, r2.b (dead-row heads count too)
  EXPECT_EQ(s.dead_rows, 1);
  EXPECT_EQ(s.dead_row_entries, 3);
  // Reclaimable = shadowed entries of live rows (3) + every entry of dead rows (3), no overlap.
  EXPECT_EQ(s.reclaimable_entries, 6);
  EXPECT_EQ(s.max_row_chain, 6);

  // Row-chain histogram: one row of 6, one row of 3.
  EXPECT_EQ(s.row_chain_hist.bucket(ExponentialHistogram::BucketIndex(6)), 1);
  EXPECT_EQ(s.row_chain_hist.bucket(ExponentialHistogram::BucketIndex(3)), 1);
  EXPECT_EQ(s.row_chain_hist.TotalWeight(), 2);

  // Stretches of consecutive reclaimable entries in file order:
  //   [packed v2, packed v1] = 2, [col a v1] = 1, [r2 tombstone, r2.a, r2.b] = 3.
  EXPECT_EQ(s.stretch_hist.TotalWeight(), 3);
  EXPECT_EQ(s.stretch_hist.bucket(ExponentialHistogram::BucketIndex(1)), 1);
  EXPECT_EQ(s.stretch_hist.bucket(ExponentialHistogram::BucketIndex(2)), 1);
  EXPECT_EQ(s.stretch_hist.bucket(ExponentialHistogram::BucketIndex(3)), 1);
  EXPECT_EQ(s.max_stretch, 3);

  // Age bands, by the age of what makes each entry droppable:
  //   packed v2  -> its overwriter packed v3, 1 min old        -> band 0 (< 5 m)
  //   packed v1  -> its overwriter packed v2, 20 min old       -> band 2 (15 m .. 1 h)
  //   col a v1   -> its overwriter col a v2, 10 min old        -> band 1 (5 m .. 15 m)
  //   r2 x 3     -> the row tombstone, 2 days old              -> band 5 (24 h .. 7 d)
  const AgeBandCounts expected_bands = {1, 1, 1, 0, 0, 3, 0, 0};
  EXPECT_EQ(s.droppable_age_entries, expected_bands);

  // Write-time range spans the oldest (r2.b, 6 days) to the newest (1 minute) entry.
  EXPECT_EQ(s.min_write_ht, AgeToHt(6 * kDay));
  EXPECT_EQ(s.max_write_ht, AgeToHt(1 * kMinute));
  EXPECT_EQ(s.anchor_micros, kAnchorMicros);
}

TEST_F(ChainTrackerTest, IdentitiesHoldOverChainEntriesWithMetaRecords) {
  // Meta records are counted in total_entries but not in the chain population; the identities are
  // stated over chain_entries and must survive meta records interleaved between rows.
  auto entries = AnatomyStrip();
  entries.insert(entries.begin() + 6, Entry{MetaKey(), "state"});
  entries.push_back(Entry{MetaKey() + "2", "state"});
  const auto s = TrackerFixture().Add(entries).Finish();
  ASSERT_NO_FATALS(ExpectIdentities(s));
  EXPECT_EQ(s.total_entries, 11);
  EXPECT_EQ(s.meta_entries, 2);
  EXPECT_EQ(s.chain_entries, 9);
  EXPECT_EQ(s.num_rows, 2);
  EXPECT_EQ(s.reclaimable_entries, 6);
  // A meta record between two rows does not create a phantom row or join two stretches.
  EXPECT_EQ(s.row_chain_hist.TotalWeight(), 2);
  EXPECT_EQ(s.stretch_hist.TotalWeight(), 3);
}

TEST_F(ChainTrackerTest, EmptyFileAndFinishIdempotence) {
  TrackerFixture empty;
  const auto& s = empty.Finish();
  EXPECT_EQ(s.total_entries, 0);
  EXPECT_EQ(s.num_rows, 0);
  EXPECT_TRUE(s.row_chain_hist.Empty());
  EXPECT_TRUE(s.stretch_hist.Empty());
  EXPECT_TRUE(s.chain_valid);
  EXPECT_FALSE(s.min_write_ht.is_valid());

  TrackerFixture fixture;
  fixture.Add(AnatomyStrip());
  const auto first = fixture.Finish();
  const auto& second = fixture.Finish();
  EXPECT_EQ(first.num_rows, second.num_rows);
  EXPECT_EQ(first.row_chain_hist, second.row_chain_hist);
  EXPECT_EQ(first.stretch_hist, second.stretch_hist);
  EXPECT_EQ(first.dead_row_entries, second.dead_row_entries);
}

TEST_F(ChainTrackerTest, OnlyMetaRecords) {
  const auto s = TrackerFixture()
      .Add({{MetaKey(), "a"}, {MetaKey() + "1", "b"}, {MetaKey() + "2", "c"}}).Finish();
  EXPECT_EQ(s.total_entries, 3);
  EXPECT_EQ(s.meta_entries, 3);
  EXPECT_EQ(s.chain_entries, 0);
  EXPECT_EQ(s.num_rows, 0);
  EXPECT_TRUE(s.row_chain_hist.Empty());
  EXPECT_TRUE(s.chain_valid);
}

TEST_F(ChainTrackerTest, LongStretchOfDeadRows) {
  // A drained queue: 100 deleted rows, each a tombstone plus two columns. Every row chain is short
  // (3), but a cursor entering this range steps over 300 entries: one stretch of 300.
  std::vector<Entry> entries;
  for (int32_t id = 0; id < 100; ++id) {
    const auto row = Row(id);
    entries.push_back({RowKey(row, 1 * kDay), Tombstone()});
    entries.push_back({ColumnKey(row, 1, 2 * kDay), Str("a")});
    entries.push_back({ColumnKey(row, 2, 2 * kDay), Str("b")});
  }
  const auto s = TrackerFixture().Add(entries).Finish();
  ASSERT_NO_FATALS(ExpectIdentities(s));
  EXPECT_EQ(s.num_rows, 100);
  EXPECT_EQ(s.dead_rows, 100);
  EXPECT_EQ(s.dead_row_entries, 300);
  EXPECT_EQ(s.reclaimable_entries, 300);
  EXPECT_EQ(s.max_row_chain, 3);
  EXPECT_EQ(s.max_stretch, 300);
  EXPECT_EQ(s.stretch_hist.TotalWeight(), 1);
  EXPECT_EQ(s.stretch_hist.bucket(ExponentialHistogram::BucketIndex(300)), 1);
  // All droppable once the 1-day-old tombstones are; an age of exactly 24 h is the lower edge of
  // band 5 (24 h .. 7 d).
  EXPECT_EQ(s.droppable_age_entries[5], 300);
}

TEST_F(ChainTrackerTest, EveryOtherRowDeletedHasShortStretches) {
  // Half the rows deleted, interleaved with live rows: high reclaimable total, short stretches.
  std::vector<Entry> entries;
  for (int32_t id = 0; id < 100; ++id) {
    const auto row = Row(id);
    if (id % 2 == 0) {
      entries.push_back({RowKey(row, 1 * kDay), Tombstone()});
      entries.push_back({ColumnKey(row, 1, 2 * kDay), Str("a")});
    } else {
      entries.push_back({RowKey(row, 1 * kDay), PackedRow("live")});
    }
  }
  const auto s = TrackerFixture().Add(entries).Finish();
  ASSERT_NO_FATALS(ExpectIdentities(s));
  EXPECT_EQ(s.num_rows, 100);
  EXPECT_EQ(s.dead_rows, 50);
  EXPECT_EQ(s.reclaimable_entries, 100);
  EXPECT_EQ(s.chain_entries, 150);
  EXPECT_EQ(s.max_stretch, 2);
  EXPECT_EQ(s.stretch_hist.bucket(ExponentialHistogram::BucketIndex(2)), 50);
}

TEST_F(ChainTrackerTest, TableTombstoneAndCoprefixSubtotals) {
  const ColocationId dropped = 16385;
  const ColocationId live = 16386;
  // Colocated tablet: table `dropped` has a table-level tombstone (empty DocKey under the
  // colocation id) followed by two of its rows; table `live` has one row with a shadowed version.
  std::vector<Entry> entries = {
      {dockv::SubDocKey(dockv::DocKey(dropped), AgeToHt(1 * kHour)).Encode().ToStringBuffer(),
       Tombstone()},
      {RowKey(ColocatedRow(dropped, 1), 2 * kHour), PackedRow("x")},
      {RowKey(ColocatedRow(dropped, 2), 2 * kHour), PackedRow("y")},
      {RowKey(ColocatedRow(live, 1), 1 * kMinute), PackedRow("new")},
      {RowKey(ColocatedRow(live, 1), 1 * kHour), PackedRow("old")},
  };
  const auto s = TrackerFixture(/* subtotals = */ true).Add(entries).Finish();
  ASSERT_NO_FATALS(ExpectIdentities(s));
  // The table tombstone is the head of its own one-entry dead row. The dropped table's rows are
  // separate rows here (the collector does not apply the tombstone to them; the compaction feed
  // does), so they read as live single-entry rows.
  EXPECT_EQ(s.num_rows, 4);
  EXPECT_EQ(s.dead_rows, 1);
  EXPECT_EQ(s.dead_row_entries, 1);
  EXPECT_EQ(s.tombstone_entries, 1);
  EXPECT_EQ(s.shadowed_entries(), 1);
  EXPECT_EQ(s.reclaimable_entries, 2);

  ASSERT_EQ(s.coprefix_subtotals.size(), 2);
  const auto dropped_prefix = dockv::DocKey(dropped).Encode().ToStringBuffer();
  const auto live_prefix = dockv::DocKey(live).Encode().ToStringBuffer();
  // DocKey(colocation_id).Encode() ends with a kGroupEnd; the coprefix is the part before it.
  const auto& dropped_sub =
      s.coprefix_subtotals.at(dropped_prefix.substr(0, dropped_prefix.size() - 1));
  EXPECT_EQ(dropped_sub.rows, 3);
  EXPECT_EQ(dropped_sub.entries, 3);
  EXPECT_EQ(dropped_sub.tombstone_entries, 1);
  EXPECT_EQ(dropped_sub.reclaimable_entries, 1);
  const auto& live_sub = s.coprefix_subtotals.at(live_prefix.substr(0, live_prefix.size() - 1));
  EXPECT_EQ(live_sub.rows, 1);
  EXPECT_EQ(live_sub.entries, 2);
  EXPECT_EQ(live_sub.tombstone_entries, 0);
  EXPECT_EQ(live_sub.reclaimable_entries, 1);
}

TEST_F(ChainTrackerTest, CorruptKeyInvalidatesChainStatsOnly) {
  auto entries = AnatomyStrip();
  // A key with no room for a hybrid time.
  entries.insert(entries.begin() + 3, Entry{std::string(1, dockv::KeyEntryTypeAsChar::kString),
                                            Tombstone()});
  const auto s = TrackerFixture().Add(entries).Finish();
  EXPECT_FALSE(s.chain_valid);
  // The v1 counters keep counting every entry.
  EXPECT_EQ(s.total_entries, 10);
  EXPECT_EQ(s.tombstone_entries, 2);
}

TEST_F(ChainTrackerTest, MergeAcrossFilesIsExactForDisjointRows) {
  // Two files holding disjoint rows: the merged histograms equal the histograms of one file
  // holding all the entries.
  const auto all = AnatomyStrip();
  const std::vector<Entry> first(all.begin(), all.begin() + 6);   // r1
  const std::vector<Entry> second(all.begin() + 6, all.end());    // r2
  const auto a = TrackerFixture().Add(first).Finish();
  const auto b = TrackerFixture().Add(second).Finish();
  const auto both = TrackerFixture().Add(all).Finish();

  auto merged_rows = a.row_chain_hist;
  merged_rows.Merge(b.row_chain_hist);
  EXPECT_EQ(merged_rows, both.row_chain_hist);
  auto merged_stretch = a.stretch_hist;
  merged_stretch.Merge(b.stretch_hist);
  EXPECT_EQ(merged_stretch, both.stretch_hist);
  EXPECT_EQ(a.reclaimable_entries + b.reclaimable_entries, both.reclaimable_entries);
  EXPECT_EQ(a.chain_entries - a.num_rows + b.chain_entries - b.num_rows,
            both.collapsible_entries());
}

}  // namespace yb::docdb
