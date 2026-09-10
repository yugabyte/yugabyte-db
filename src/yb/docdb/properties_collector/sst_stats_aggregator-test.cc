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

#include "yb/docdb/properties_collector/sst_stats_aggregator.h"

#include "yb/docdb/properties_collector/sst_stats_collector.h"

#include "yb/rocksdb/db/filename.h"

#include "yb/util/test_util.h"

namespace yb::docdb {

namespace {

constexpr auto kDbPath = "/db";

std::string PathOf(uint64_t file_number) {
  return rocksdb::MakeTableFileName(kDbPath, file_number);
}

rocksdb::LiveFileMetaData LiveFile(uint64_t file_number) {
  return rocksdb::LiveFileMetaData(
      "default", /* level = */ 0, file_number, kDbPath, /* total_size = */ 0, /* base_size = */ 0,
      /* uncompressed_size = */ 0, {}, {}, /* imported = */ false, /* being_compacted = */ false);
}

// A file holding `entries` entries of which `reclaimable` are garbage, all in one age band.
SstStats FileStats(uint64_t entries, uint64_t reclaimable) {
  SstStats stats;
  stats.total_entries = entries;
  stats.chain_entries = entries;
  stats.num_subdoc_keys = entries / 2;
  stats.num_rows = entries / 4;
  stats.reclaimable_entries = reclaimable;
  stats.reclaimable_bytes = reclaimable * 10;
  stats.droppable_age_entries[3] = reclaimable;
  stats.droppable_age_bytes[3] = reclaimable * 10;
  return stats;
}

// `stats == nullptr` builds a file that predates the collector: built-in properties only.
rocksdb::TableProperties FileProperties(
    uint64_t entries, uint64_t raw_bytes, const SstStats* stats) {
  rocksdb::TableProperties properties;
  properties.num_entries = entries;
  properties.raw_key_size = raw_bytes / 2;
  properties.raw_value_size = raw_bytes - raw_bytes / 2;
  if (stats != nullptr) {
    SstStatsToProperties(*stats, &properties.user_collected_properties);
  }
  return properties;
}

rocksdb::TableProperties CoveredFile(uint64_t entries, uint64_t reclaimable) {
  const auto stats = FileStats(entries, reclaimable);
  return FileProperties(entries, entries * 20, &stats);
}

rocksdb::TableProperties UncoveredFile(uint64_t entries) {
  return FileProperties(entries, entries * 20, nullptr);
}

rocksdb::FlushJobInfo FlushOf(uint64_t file_number, rocksdb::TableProperties properties) {
  rocksdb::FlushJobInfo info{};
  info.file_path = PathOf(file_number);
  info.table_properties = std::move(properties);
  return info;
}

// A file named in an event or a live file list. Null properties stand for a file whose properties
// the caller could not produce: an input a compaction event did not carry, or a file whose
// properties block could not be read.
using FileEntry = std::pair<uint64_t, const rocksdb::TableProperties*>;
using FileEntries = std::vector<FileEntry>;

void AddFiles(
    const FileEntries& files, std::vector<std::string>* paths,
    rocksdb::TablePropertiesCollection* properties) {
  for (const auto& [file_number, file_properties] : files) {
    const auto path = PathOf(file_number);
    paths->push_back(path);
    if (file_properties != nullptr) {
      (*properties)[path] = std::make_shared<rocksdb::TableProperties>(*file_properties);
    }
  }
}

rocksdb::CompactionJobInfo CompactionOf(const FileEntries& inputs, const FileEntries& outputs) {
  rocksdb::CompactionJobInfo info;
  AddFiles(inputs, &info.input_files, &info.table_properties);
  AddFiles(outputs, &info.output_files, &info.table_properties);
  return info;
}

SstStatsAggregator::SnapshotFn SnapshotOf(FileEntries files) {
  return [files = std::move(files)](
      std::vector<rocksdb::LiveFileMetaData>* live_files,
      rocksdb::TablePropertiesCollection* properties) {
    std::vector<std::string> paths;
    AddFiles(files, &paths, properties);
    for (const auto& [file_number, file_properties] : files) {
      live_files->push_back(LiveFile(file_number));
    }
    return Status::OK();
  };
}

}  // namespace

class SstStatsAggregatorTest : public YBTest {};

TEST_F(SstStatsAggregatorTest, FileContribution) {
  const auto covered =
      SstFileContribution(CoveredFile(/* entries = */ 100, /* reclaimable = */ 40));
  EXPECT_EQ(covered.total_entries, 100);
  EXPECT_EQ(covered.reclaimable_entries, 40);
  EXPECT_EQ(covered.reclaimable_bytes, 400);
  EXPECT_EQ(covered.droppable_age_entries[3], 40);
  EXPECT_EQ(covered.covered_files, 1);
  EXPECT_EQ(covered.covered_raw_bytes, 2000);
  EXPECT_EQ(covered.uncovered_files, 0);
  EXPECT_EQ(covered.partial_files, 0);

  // A file that predates the collector contributes no garbage, only the denominator it withholds.
  const auto uncovered = SstFileContribution(UncoveredFile(/* entries = */ 100));
  EXPECT_EQ(uncovered.total_entries, 0);
  EXPECT_EQ(uncovered.covered_files, 0);
  EXPECT_EQ(uncovered.uncovered_files, 1);
  EXPECT_EQ(uncovered.uncovered_entries, 100);
  EXPECT_EQ(uncovered.uncovered_raw_bytes, 2000);

  auto partial_stats = FileStats(/* entries = */ 10, /* reclaimable = */ 2);
  partial_stats.chain_valid = false;
  const auto partial = SstFileContribution(FileProperties(10, 200, &partial_stats));
  EXPECT_EQ(partial.covered_files, 1);
  EXPECT_EQ(partial.partial_files, 1);
}

TEST_F(SstStatsAggregatorTest, AddAndSubtract) {
  const auto one = SstFileContribution(CoveredFile(/* entries = */ 100, /* reclaimable = */ 40));
  const auto two = SstFileContribution(CoveredFile(/* entries = */ 60, /* reclaimable = */ 10));

  auto sum = one;
  sum += two;
  EXPECT_EQ(sum.total_entries, 160);
  EXPECT_EQ(sum.reclaimable_entries, 50);
  EXPECT_EQ(sum.droppable_age_entries[3], 50);
  EXPECT_EQ(sum.covered_files, 2);

  sum -= two;
  EXPECT_EQ(sum, one);

  // Saturating: a subtraction that was never matched by an addition must not wrap.
  sum -= two;
  sum -= two;
  EXPECT_EQ(sum.total_entries, 0);
  EXPECT_EQ(sum.covered_files, 0);
}

TEST_F(SstStatsAggregatorTest, FlushAndCompaction) {
  SstStatsAggregator aggregator;
  const auto first = CoveredFile(/* entries = */ 100, /* reclaimable = */ 40);
  const auto second = CoveredFile(/* entries = */ 60, /* reclaimable = */ 10);
  aggregator.OnFlushCompleted(FlushOf(1, first));
  aggregator.OnFlushCompleted(FlushOf(2, second));
  EXPECT_EQ(aggregator.Get().aggregate.total_entries, 160);
  EXPECT_EQ(aggregator.Get().aggregate.covered_files, 2);
  // Nothing has looked at the whole file set yet.
  EXPECT_EQ(aggregator.Get().last_resync_micros, 0);

  // A flush replayed on a file already counted must not count it twice.
  aggregator.OnFlushCompleted(FlushOf(1, first));
  EXPECT_EQ(aggregator.Get().aggregate.total_entries, 160);

  const auto merged = CoveredFile(/* entries = */ 120, /* reclaimable = */ 5);
  aggregator.OnCompactionCompleted(
      CompactionOf({{1, &first}, {2, &second}}, {{3, &merged}}));
  const auto after = aggregator.Get().aggregate;
  EXPECT_EQ(after.total_entries, 120);
  EXPECT_EQ(after.reclaimable_entries, 5);
  EXPECT_EQ(after.covered_files, 1);
  EXPECT_EQ(after.unsubtracted_files, 0);
}

TEST_F(SstStatsAggregatorTest, FailedCompactionLeavesInputsCounted) {
  SstStatsAggregator aggregator;
  const auto file = CoveredFile(/* entries = */ 100, /* reclaimable = */ 40);
  aggregator.OnFlushCompleted(FlushOf(1, file));

  auto info = CompactionOf({{1, &file}}, {{2, &file}});
  info.status = STATUS(IOError, "compaction failed");
  aggregator.OnCompactionCompleted(info);
  EXPECT_EQ(aggregator.Get().aggregate.total_entries, 100);
  EXPECT_EQ(aggregator.Get().aggregate.covered_files, 1);
}

TEST_F(SstStatsAggregatorTest, CompactionWithoutInputProperties) {
  SstStatsAggregator aggregator;
  const auto file = CoveredFile(/* entries = */ 100, /* reclaimable = */ 40);
  const auto output = CoveredFile(/* entries = */ 90, /* reclaimable = */ 5);
  aggregator.OnFlushCompleted(FlushOf(1, file));

  aggregator.OnCompactionCompleted(CompactionOf({{1, nullptr}}, {{2, &output}}));
  const auto after = aggregator.Get().aggregate;
  // The consumed file's statistics stay in the sums, and the counter says so.
  EXPECT_EQ(after.total_entries, 190);
  EXPECT_EQ(after.unsubtracted_files, 1);
}

TEST_F(SstStatsAggregatorTest, CompactionOfUncountedFile) {
  SstStatsAggregator aggregator;
  const auto inherited = CoveredFile(/* entries = */ 100, /* reclaimable = */ 40);
  const auto output = CoveredFile(/* entries = */ 90, /* reclaimable = */ 5);
  // File 1 was never reported to the aggregator -- it was on disk before the tablet opened.
  aggregator.OnCompactionCompleted(CompactionOf({{1, &inherited}}, {{2, &output}}));
  const auto after = aggregator.Get().aggregate;
  EXPECT_EQ(after.total_entries, 90);
  EXPECT_EQ(after.covered_files, 1);
  EXPECT_EQ(after.unsubtracted_files, 0);
}

TEST_F(SstStatsAggregatorTest, ResyncCountsUncoveredFiles) {
  SstStatsAggregator aggregator;
  const auto covered = CoveredFile(/* entries = */ 100, /* reclaimable = */ 40);
  const auto uncovered = UncoveredFile(/* entries = */ 900);
  ASSERT_OK(aggregator.Resync(SnapshotOf({{1, &covered}, {2, &uncovered}, {3, nullptr}})));

  const auto snapshot = aggregator.Get();
  EXPECT_GT(snapshot.last_resync_micros, 0);
  EXPECT_EQ(snapshot.aggregate.total_entries, 100);
  EXPECT_EQ(snapshot.aggregate.covered_files, 1);
  // File 2 predates the collector; file 3's properties could not be read at all.
  EXPECT_EQ(snapshot.aggregate.uncovered_files, 2);
  EXPECT_EQ(snapshot.aggregate.uncovered_entries, 900);

  // Resync is authoritative: it drops what the listener reported and the file set no longer has.
  ASSERT_OK(aggregator.Resync(SnapshotOf({{1, &covered}})));
  EXPECT_EQ(aggregator.Get().aggregate.uncovered_files, 0);
}

TEST_F(SstStatsAggregatorTest, ResyncOvertakenByFlushIsDropped) {
  SstStatsAggregator aggregator;
  const auto flushed = CoveredFile(/* entries = */ 100, /* reclaimable = */ 40);
  const auto stale = CoveredFile(/* entries = */ 7, /* reclaimable = */ 7);

  const auto status = aggregator.Resync(
      [&](std::vector<rocksdb::LiveFileMetaData>* live_files,
          rocksdb::TablePropertiesCollection* properties) {
        // The file set the snapshot saw, before the flush that lands underneath it.
        live_files->push_back(LiveFile(9));
        (*properties)[PathOf(9)] = std::make_shared<rocksdb::TableProperties>(stale);
        aggregator.OnFlushCompleted(FlushOf(1, flushed));
        return Status::OK();
      });
  ASSERT_OK(status);

  const auto snapshot = aggregator.Get();
  EXPECT_EQ(snapshot.aggregate.total_entries, 100);
  EXPECT_EQ(snapshot.last_resync_micros, 0);
}

}  // namespace yb::docdb
