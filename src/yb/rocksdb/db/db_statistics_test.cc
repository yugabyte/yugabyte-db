// Copyright (c) YugaByte, Inc.
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

#include "yb/rocksdb/db/db_test_util.h"

using namespace std::literals;

namespace rocksdb {

class DBStatisticsTest : public DBTestBase {
 public:
  DBStatisticsTest() : DBTestBase("/db_statistics_test") {}
};

namespace {

Result<size_t> GetManifestFileSize(DB* db) {
  std::vector<std::string> live_files;
  uint64_t manifest_file_size;
  RETURN_NOT_OK(db->GetLiveFiles(live_files, &manifest_file_size, /* flush_memtable = */ false));
  return manifest_file_size;
}

void DebugDump(DB* db) {
  auto& stat = *db->GetOptions().statistics;
  for (auto& ticker : {FLUSH_WRITE_BYTES, COMPACT_READ_BYTES, COMPACT_WRITE_BYTES}) {
    LOG(INFO) << stat.GetTickerName(ticker) << ": " << stat.getTickerCount(ticker);
  }
  LOG(INFO) << "GetCurrentVersionSstFilesSize: " << db->GetCurrentVersionSstFilesSize();
  LOG(INFO) << "GetCurrentVersionDataSstFilesSize: " << db->GetCurrentVersionDataSstFilesSize();
  LOG(INFO) << "GetManifestFileSize: " << ASSERT_RESULT(GetManifestFileSize(db));
}

} // namespace

TEST_F(DBStatisticsTest, FlushAndCompaction) {
  constexpr auto kNumFiles = 5;
  constexpr auto kNumKeysPerFile = 1000;

  Options options = CurrentOptions();
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.compaction_style = kCompactionStyleUniversal;
  options.num_levels = 1;
  options.level0_file_num_compaction_trigger = std::numeric_limits<int>::max();

  ConfigureLoggingToGlog(&options);

  DestroyAndReopen(options);

  auto& stats = *options.statistics;

  DebugDump(db_);
  for (auto file = 0; file < kNumFiles; ++file) {
    for (auto key = 0; key < kNumKeysPerFile; ++key) {
      ASSERT_OK(Put(Key(key), "val"));
    }
    ASSERT_OK(Flush());
    DebugDump(db_);

    ASSERT_EQ(
        stats.getTickerCount(FLUSH_WRITE_BYTES),
        db_->GetCurrentVersionSstFilesSize() + ASSERT_RESULT(GetManifestFileSize(db_)));
    ASSERT_EQ(stats.getTickerCount(COMPACT_READ_BYTES), 0);
    ASSERT_EQ(stats.getTickerCount(COMPACT_WRITE_BYTES), 0);
  }

  const auto before_compaction_flushed_bytes = stats.getTickerCount(FLUSH_WRITE_BYTES);
  const auto before_compaction_sst_files_total_size = db_->GetCurrentVersionSstFilesSize();
  const auto before_compaction_sst_files_data_size = db_->GetCurrentVersionDataSstFilesSize();

  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), /* begin = */ nullptr, /* end = */ nullptr));
  DebugDump(db_);

  ASSERT_EQ(stats.getTickerCount(FLUSH_WRITE_BYTES), before_compaction_flushed_bytes);
  // Compaction doesn't need to read the whole input metadata, but need to read data blocks.
  ASSERT_GE(stats.getTickerCount(COMPACT_READ_BYTES), before_compaction_sst_files_data_size);
  ASSERT_LE(stats.getTickerCount(COMPACT_READ_BYTES), before_compaction_sst_files_total_size);

  ASSERT_EQ(stats.getTickerCount(COMPACT_WRITE_BYTES), db_->GetCurrentVersionSstFilesSize());
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
