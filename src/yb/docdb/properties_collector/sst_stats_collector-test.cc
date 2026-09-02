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

#include "yb/common/column_id.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/util/flags.h"
#include "yb/util/test_util.h"

DECLARE_bool(docdb_sst_stats_coprefix_subtotals);
DECLARE_uint32(sst_tombstone_mark_ratio_percent);
DECLARE_uint64(sst_tombstone_mark_min_count);

namespace yb::docdb {

namespace {

// A fixed wall-clock reference for the age bands: 2023-11-14T22:13:20Z.
constexpr int64_t kAnchorMicros = 1700000000LL * 1000000;
constexpr int64_t kMinute = 60LL * 1000000;
constexpr int64_t kHour = 60 * kMinute;
constexpr int64_t kDay = 24 * kHour;

// An entry as the collector sees it: the RocksDB user key (subdoc key + hybrid time) and value.
struct Entry {
  std::string key;
  std::string value;
};

HybridTime AgeToHt(int64_t age_micros) {
  return HybridTime::FromMicros(kAnchorMicros - age_micros);
}

dockv::DocKey Row(int32_t id) {
  return dockv::DocKey(
      /* hash = */ 1000 + id, dockv::MakeKeyEntryValues("h"),
      dockv::MakeKeyEntryValues(id));
}

dockv::DocKey ColocatedRow(ColocationId colocation_id, int32_t id) {
  return dockv::DocKey(
      colocation_id, /* hash = */ 1000 + id, dockv::MakeKeyEntryValues("h"),
      dockv::MakeKeyEntryValues(id));
}

// Row-level key (packed row or row tombstone): the DocKey itself, no subkeys.
std::string RowKey(const dockv::DocKey& row, int64_t age_micros) {
  return dockv::SubDocKey(row, AgeToHt(age_micros)).Encode().ToStringBuffer();
}

std::string ColumnKey(const dockv::DocKey& row, int column, int64_t age_micros) {
  return dockv::SubDocKey(
      row, AgeToHt(age_micros),
      dockv::KeyEntryValues{dockv::KeyEntryValue::MakeColumnId(ColumnId(column))})
      .Encode().ToStringBuffer();
}

std::string Tombstone() {
  return std::string(1, dockv::ValueEntryTypeAsChar::kTombstone);
}

std::string PackedRow(const std::string& payload) {
  return std::string(1, dockv::ValueEntryTypeAsChar::kPackedRowV2) + payload;
}

std::string Str(const std::string& s) {
  return std::string(1, dockv::ValueEntryTypeAsChar::kString) + s;
}

std::string MetaKey() {
  return std::string(1, dockv::KeyEntryTypeAsChar::kTransactionApplyState) + "txn-id-bytes";
}

class TrackerFixture {
 public:
  explicit TrackerFixture(bool subtotals = false) : tracker_(kAnchorMicros, subtotals) {}

  TrackerFixture& Add(const Entry& e) {
    tracker_.Add(e.key, e.value);
    return *this;
  }

  TrackerFixture& Add(const std::vector<Entry>& entries) {
    for (const auto& e : entries) {
      Add(e);
    }
    return *this;
  }

  const SstStats& Finish() { return tracker_.Finish(); }

 private:
  ChainTracker tracker_;
};

// The anatomy strip from the design (README "Vocabulary" example).
//
// Live row r1, 6 entries, newest first within each key:
//   [packed v3][packed v2][packed v1][col a v2][col a v1][col b v1]
// Dead row r2, 3 entries:
//   [row tombstone][col a v1][col b v1]
std::vector<Entry> AnatomyStrip() {
  const auto r1 = Row(1);
  const auto r2 = Row(2);
  return {
      {RowKey(r1, 1 * kMinute), PackedRow("v3")},
      {RowKey(r1, 20 * kMinute), PackedRow("v2")},
      {RowKey(r1, 2 * kHour), PackedRow("v1")},
      {ColumnKey(r1, 1, 10 * kMinute), Str("a2")},
      {ColumnKey(r1, 1, 3 * kDay), Str("a1")},
      {ColumnKey(r1, 2, 1 * kMinute), Str("b1")},
      {RowKey(r2, 2 * kDay), Tombstone()},
      {ColumnKey(r2, 1, 5 * kDay), Str("a1")},
      {ColumnKey(r2, 2, 6 * kDay), Str("b1")},
  };
}

void ExpectIdentities(const SstStats& s) {
  ASSERT_TRUE(s.chain_valid);
  ASSERT_EQ(s.chain_entries, s.shadowed_entries() + s.num_subdoc_keys);
  ASSERT_EQ(s.num_subdoc_keys, s.repackable_entries() + s.num_rows);
  ASSERT_EQ(s.collapsible_entries(), s.shadowed_entries() + s.repackable_entries());
  ASSERT_EQ(s.total_entries, s.chain_entries + s.meta_entries);
  ASSERT_EQ(s.row_chain_hist.TotalWeight(), s.num_rows);
  ASSERT_EQ(s.stretch_entries_hist.TotalWeight(), s.reclaimable_entries);
  ASSERT_EQ(s.stretch_bytes_hist.TotalWeight(), s.reclaimable_bytes);
  uint64_t banded_entries = 0, banded_bytes = 0;
  for (size_t i = 0; i < AgeBands::kNumBands; ++i) {
    banded_entries += s.droppable_age_entries[i];
    banded_bytes += s.droppable_age_bytes[i];
  }
  ASSERT_EQ(banded_entries, s.reclaimable_entries);
  ASSERT_EQ(banded_bytes, s.reclaimable_bytes);
}

}  // namespace

class SstStatsCollectorTest : public YBTest {};

TEST_F(SstStatsCollectorTest, AnatomyStrip) {
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

TEST_F(SstStatsCollectorTest, IdentitiesHoldOverChainEntriesWithMetaRecords) {
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

TEST_F(SstStatsCollectorTest, EmptyFileAndFinishIdempotence) {
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

TEST_F(SstStatsCollectorTest, OnlyMetaRecords) {
  const auto s = TrackerFixture()
      .Add({{MetaKey(), "a"}, {MetaKey() + "1", "b"}, {MetaKey() + "2", "c"}}).Finish();
  EXPECT_EQ(s.total_entries, 3);
  EXPECT_EQ(s.meta_entries, 3);
  EXPECT_EQ(s.chain_entries, 0);
  EXPECT_EQ(s.num_rows, 0);
  EXPECT_TRUE(s.row_chain_hist.Empty());
  EXPECT_TRUE(s.chain_valid);
}

TEST_F(SstStatsCollectorTest, LongStretchOfDeadRows) {
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

TEST_F(SstStatsCollectorTest, EveryOtherRowDeletedHasShortStretches) {
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

TEST_F(SstStatsCollectorTest, TableTombstoneAndCoprefixSubtotals) {
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

TEST_F(SstStatsCollectorTest, CorruptKeyInvalidatesChainStatsOnly) {
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

TEST_F(SstStatsCollectorTest, MergeAcrossFilesIsExactForDisjointRows) {
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

TEST_F(SstStatsCollectorTest, PropertiesRoundTrip) {
  const auto s = TrackerFixture(/* subtotals = */ true).Add(AnatomyStrip()).Finish();
  rocksdb::UserCollectedProperties properties;
  SstStatsToProperties(s, &properties);
  EXPECT_EQ(properties.at(std::string(SstStatsPropertyKeys::kCollectorVersion)),
            kSstStatsCollectorVersion);
  EXPECT_EQ(properties.at(std::string(SstStatsPropertyKeys::kReclaimableEntries)), "6");
  EXPECT_EQ(properties.at(std::string(SstStatsPropertyKeys::kDroppableAgeEntries)),
            "1,1,1,0,0,3,0,0");

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
  EXPECT_EQ(parsed.coprefix_subtotals.begin()->second.rows, 2);

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
