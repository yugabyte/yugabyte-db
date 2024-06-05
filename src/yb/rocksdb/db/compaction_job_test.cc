//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

#include "yb/rocksdb/db/compaction_job.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/file_numbers.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table/mock_table.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/utilities/merge_operators.h"

#include "yb/util/string_util.h"
#include "yb/util/test_util.h"

using std::unique_ptr;

namespace rocksdb {

namespace {

void VerifyInitializationOfCompactionJobStats(
      const CompactionJobStats& compaction_job_stats) {
#if !defined(IOS_CROSS_COMPILE)
  ASSERT_EQ(compaction_job_stats.elapsed_micros, 0U);

  ASSERT_EQ(compaction_job_stats.num_input_records, 0U);
  ASSERT_EQ(compaction_job_stats.num_input_files, 0U);
  ASSERT_EQ(compaction_job_stats.num_input_files_at_output_level, 0U);

  ASSERT_EQ(compaction_job_stats.num_output_records, 0U);
  ASSERT_EQ(compaction_job_stats.num_output_files, 0U);

  ASSERT_EQ(compaction_job_stats.is_manual_compaction, true);

  ASSERT_EQ(compaction_job_stats.total_input_bytes, 0U);
  ASSERT_EQ(compaction_job_stats.total_output_bytes, 0U);

  ASSERT_EQ(compaction_job_stats.total_input_raw_key_bytes, 0U);
  ASSERT_EQ(compaction_job_stats.total_input_raw_value_bytes, 0U);

  ASSERT_EQ(compaction_job_stats.smallest_output_key_prefix[0], 0);
  ASSERT_EQ(compaction_job_stats.largest_output_key_prefix[0], 0);

  ASSERT_EQ(compaction_job_stats.num_records_replaced, 0U);

  ASSERT_EQ(compaction_job_stats.num_input_deletion_records, 0U);
  ASSERT_EQ(compaction_job_stats.num_expired_deletion_records, 0U);

  ASSERT_EQ(compaction_job_stats.num_corrupt_keys, 0U);
#endif  // !defined(IOS_CROSS_COMPILE)
}

}  // namespace

// TODO(icanadi) Make it simpler once we mock out VersionSet
class CompactionJobTest : public RocksDBTest {
 public:
  CompactionJobTest()
      : env_(Env::Default()),
        dbname_(test::TmpDir() + "/compaction_job_test"),
        mutable_cf_options_(Options(), ImmutableCFOptions(Options())),
        table_cache_(NewLRUCache(50000, 16)),
        write_buffer_(db_options_.db_write_buffer_size),
        versions_(new VersionSet(dbname_, &db_options_, env_options_,
                                 table_cache_.get(), &write_buffer_,
                                 &write_controller_)),
        shutting_down_(false),
        mock_table_factory_(new mock::MockTableFactory()) {
    EXPECT_OK(env_->CreateDirIfMissing(dbname_));
    db_options_.boundary_extractor = test::MakeBoundaryValuesExtractor();
    db_options_.db_paths.emplace_back(dbname_,
                                      std::numeric_limits<uint64_t>::max());
  }

  std::string GenerateFileName(uint64_t file_number) {
    FileMetaData meta;
    std::vector<DbPath> db_paths;
    db_paths.emplace_back(dbname_, std::numeric_limits<uint64_t>::max());
    meta.fd = FileDescriptor(file_number, 0, 0, 0);
    return TableFileName(db_paths, meta.fd.GetNumber(), meta.fd.GetPathId());
  }

  std::string KeyStr(const std::string& user_key, const SequenceNumber seq_num,
      const ValueType t) {
    return InternalKey(user_key, seq_num, t).Encode().ToBuffer();
  }

  void AddMockFile(const stl_wrappers::KVMap& contents, int level = 0) {
    ASSERT_GT(contents.size(), 0);

    bool first_key = true;
    FileMetaData::BoundaryValues smallest_values, largest_values;
    smallest_values.seqno = kMaxSequenceNumber;
    largest_values.seqno = 0;
    std::string smallest, largest;
    test::BoundaryTestValues user_values;

    for (auto kv : contents) {
      ParsedInternalKey key;
      std::string skey;
      std::string value;
      std::tie(skey, value) = kv;
      ParseInternalKey(skey, &key);

      user_values.Feed(key.user_key);

      smallest_values.seqno = std::min(smallest_values.seqno, key.sequence);
      largest_values.seqno = std::max(largest_values.seqno, key.sequence);

      if (first_key ||
          cfd_->user_comparator()->Compare(key.user_key, smallest) < 0) {
        smallest = key.user_key.ToBuffer();
        smallest_values.key = InternalKey::DecodeFrom(skey);
      }
      if (first_key ||
          cfd_->user_comparator()->Compare(key.user_key, largest) > 0) {
        largest = key.user_key.ToBuffer();
        largest_values.key = InternalKey::DecodeFrom(skey);
      }

      first_key = false;
    }

    uint64_t file_number = versions_->NewFileNumber();
    EXPECT_OK(mock_table_factory_->CreateMockTable(
        env_, GenerateFileName(file_number), contents));

    VersionEdit edit;
    smallest_values.user_values.push_back(
        test::MakeLeftBoundaryValue(user_values.min_left.AsSlice()));
    smallest_values.user_values.push_back(
        test::MakeRightBoundaryValue(user_values.min_right.AsSlice()));
    smallest_values.user_frontier = test::TestUserFrontier(smallest_values.seqno).Clone();
    largest_values.user_values.push_back(
        test::MakeLeftBoundaryValue(user_values.max_left.AsSlice()));
    largest_values.user_values.push_back(
        test::MakeRightBoundaryValue(user_values.max_right.AsSlice()));
    largest_values.user_frontier = test::TestUserFrontier(largest_values.seqno).Clone();
    edit.AddTestFile(level,
                     FileDescriptor(file_number, 0, 10, 10),
                     smallest_values,
                     largest_values,
                     /* marked_for_compaction */ false);

    mutex_.Lock();
    ASSERT_OK(versions_->LogAndApply(versions_->GetColumnFamilySet()->GetDefault(),
                                     mutable_cf_options_, &edit, &mutex_));
    mutex_.Unlock();
  }

  void SetLastSequence(const SequenceNumber sequence_number) {
    versions_->SetLastSequence(sequence_number + 1);
    test::TestUserFrontier frontier(sequence_number + 1);
    versions_->UpdateFlushedFrontier(frontier.Clone());
  }

  // returns expected result after compaction
  stl_wrappers::KVMap CreateTwoFiles(bool gen_corrupted_keys) {
    auto expected_results = mock::MakeMockFile();
    const int kKeysPerFile = 10000;
    const int kCorruptKeysPerFile = 200;
    const int kMatchingKeys = kKeysPerFile / 2;
    SequenceNumber sequence_number = 0;

    auto corrupt_id = [&](int id) {
      return gen_corrupted_keys && id > 0 && id <= kCorruptKeysPerFile;
    };

    for (int i = 0; i < 2; ++i) {
      auto contents = mock::MakeMockFile();
      for (int k = 0; k < kKeysPerFile; ++k) {
        auto key = ToString(i * kMatchingKeys + k);
        auto value = ToString(i * kKeysPerFile + k);
        InternalKey internal_key(key, ++sequence_number, kTypeValue);

        // This is how the key will look like once it's written in bottommost
        // file
        InternalKey bottommost_internal_key(
            key, (key == "9999") ? sequence_number : 0, kTypeValue);

        if (corrupt_id(k)) {
          test::CorruptKeyType(&internal_key);
          test::CorruptKeyType(&bottommost_internal_key);
        }
        contents.insert({ internal_key.Encode().ToString(), value });
        if (i == 1 || k < kMatchingKeys || corrupt_id(k - kMatchingKeys)) {
          expected_results.insert(
              { bottommost_internal_key.Encode().ToString(), value });
        }
      }

      AddMockFile(contents);
    }

    SetLastSequence(sequence_number);

    return expected_results;
  }

  void NewDB() {
    VersionEdit new_db;
    new_db.InitNewDB();

    const std::string manifest = DescriptorFileName(dbname_, 1);
    unique_ptr<WritableFile> file;
    Status s = env_->NewWritableFile(
        manifest, &file, env_->OptimizeForManifestWrite(env_options_));
    ASSERT_OK(s);
    unique_ptr<WritableFileWriter> file_writer(
        new WritableFileWriter(std::move(file), env_options_));
    {
      log::Writer log(std::move(file_writer), 0, false);
      std::string record;
      new_db.AppendEncodedTo(&record);
      s = log.AddRecord(record);
    }
    ASSERT_OK(s);
    // Make "CURRENT" file that points to the new manifest file.
    s = SetCurrentFile(env_, dbname_, 1, /* directory to fsync */ nullptr,
      db_options_.disableDataSync);

    std::vector<ColumnFamilyDescriptor> column_families;
    cf_options_.table_factory = mock_table_factory_;
    cf_options_.merge_operator = merge_op_;
    cf_options_.compaction_filter = compaction_filter_.get();
    column_families.emplace_back(kDefaultColumnFamilyName, cf_options_);

    EXPECT_OK(versions_->Recover(column_families, false));
    cfd_ = versions_->GetColumnFamilySet()->GetDefault();
  }

  void RunCompaction(
      const std::vector<std::vector<FileMetaData*>>& input_files,
      const stl_wrappers::KVMap& expected_results,
      const std::vector<SequenceNumber>& snapshots = {},
      SequenceNumber earliest_write_conflict_snapshot = kMaxSequenceNumber) {
    auto cfd = versions_->GetColumnFamilySet()->GetDefault();

    size_t num_input_files = 0;
    std::vector<CompactionInputFiles> compaction_input_files;
    for (size_t level = 0; level < input_files.size(); level++) {
      auto level_files = input_files[level];
      CompactionInputFiles compaction_level;
      compaction_level.level = static_cast<int>(level);
      compaction_level.files.insert(compaction_level.files.end(),
          level_files.begin(), level_files.end());
      compaction_input_files.push_back(compaction_level);
      num_input_files += level_files.size();
    }

    auto compaction = Compaction::Create(
        cfd->current()->storage_info(), *cfd->GetLatestMutableCFOptions(), compaction_input_files,
        1, 1024 * 1024, 10, 0, kNoCompression, {}, db_options_.info_log.get(), true);
    compaction->SetInputVersion(cfd->current());

    LogBuffer log_buffer(InfoLogLevel::INFO_LEVEL, db_options_.info_log.get());
    mutex_.Lock();
    EventLogger event_logger(db_options_.info_log.get());
    FileNumbersProvider file_numbers_provider(versions_.get());
    CompactionJob compaction_job(
        0, compaction.get(), db_options_, env_options_, versions_.get(),
        &shutting_down_, &log_buffer, nullptr, nullptr, nullptr, &mutex_,
        &bg_error_, snapshots, earliest_write_conflict_snapshot, &file_numbers_provider,
        table_cache_, &event_logger, false, false, dbname_, &compaction_job_stats_);

    VerifyInitializationOfCompactionJobStats(compaction_job_stats_);

    compaction_job.Prepare();
    mutex_.Unlock();
    ASSERT_OK(compaction_job.Run());
    mutex_.Lock();
    ASSERT_OK(compaction_job.Install(*cfd->GetLatestMutableCFOptions()));
    mutex_.Unlock();

    if (expected_results.size() == 0) {
      ASSERT_GE(compaction_job_stats_.elapsed_micros, 0U);
      ASSERT_EQ(compaction_job_stats_.num_input_files, num_input_files);
      ASSERT_EQ(compaction_job_stats_.num_output_files, 0U);
    } else {
      ASSERT_GE(compaction_job_stats_.elapsed_micros, 0U);
      ASSERT_EQ(compaction_job_stats_.num_input_files, num_input_files);
      ASSERT_EQ(compaction_job_stats_.num_output_files, 1U);
      mock_table_factory_->AssertLatestFile(expected_results);
    }
  }

  Env* env_;
  std::string dbname_;
  EnvOptions env_options_;
  MutableCFOptions mutable_cf_options_;
  std::shared_ptr<Cache> table_cache_;
  WriteController write_controller_;
  DBOptions db_options_;
  ColumnFamilyOptions cf_options_;
  WriteBuffer write_buffer_;
  std::unique_ptr<VersionSet> versions_;
  InstrumentedMutex mutex_;
  std::atomic<bool> shutting_down_;
  std::shared_ptr<mock::MockTableFactory> mock_table_factory_;
  CompactionJobStats compaction_job_stats_;
  ColumnFamilyData* cfd_;
  std::unique_ptr<CompactionFilter> compaction_filter_;
  std::shared_ptr<MergeOperator> merge_op_;
  Status bg_error_;
};

TEST_F(CompactionJobTest, Simple) {
  NewDB();

  auto expected_results = CreateTwoFiles(false);
  auto cfd = versions_->GetColumnFamilySet()->GetDefault();
  auto files = cfd->current()->storage_info()->LevelFiles(0);
  ASSERT_EQ(2U, files.size());
  RunCompaction({ files }, expected_results);
}

TEST_F(CompactionJobTest, SimpleCorrupted) {
  NewDB();

  auto expected_results = CreateTwoFiles(true);
  auto cfd = versions_->GetColumnFamilySet()->GetDefault();
  auto files = cfd->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
  ASSERT_EQ(compaction_job_stats_.num_corrupt_keys, 400U);
}

TEST_F(CompactionJobTest, SimpleDeletion) {
  NewDB();

  auto file1 = mock::MakeMockFile({{KeyStr("c", 4U, kTypeDeletion), ""},
                                   {KeyStr("c", 3U, kTypeValue), "val"}});
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({{KeyStr("b", 2U, kTypeValue), "val"},
                                   {KeyStr("b", 1U, kTypeValue), "val"}});
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("b", 0U, kTypeValue), "val"}});

  SetLastSequence(4U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

namespace {

void VerifyFrontier(const FileMetaData& meta, SequenceNumber min, SequenceNumber max) {
  const auto& sfront = down_cast<test::TestUserFrontier&>(*meta.smallest.user_frontier);
  const auto& lfront = down_cast<test::TestUserFrontier&>(*meta.largest.user_frontier);
  ASSERT_EQ(min, sfront.Value());
  ASSERT_EQ(max, lfront.Value());
}

} // namespace

TEST_F(CompactionJobTest, SeqNoTrackingWithFewDeletes) {
  NewDB();

  auto file1 = mock::MakeMockFile({{KeyStr("c", 4U, kTypeDeletion), ""},
                                      {KeyStr("c", 3U, kTypeValue), "val"}});
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({{KeyStr("b", 2U, kTypeValue), "val"},
                                      {KeyStr("b", 1U, kTypeValue), "val"}});
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("b", 0U, kTypeValue), "val"}});

  SetLastSequence(4U);
  auto level0_files = cfd_->current()->storage_info()->LevelFiles(0);
  auto level1_files = cfd_->current()->storage_info()->LevelFiles(1);
  ASSERT_EQ(2, level0_files.size());
  ASSERT_EQ(0, level1_files.size());
  auto min_left = std::min(test::GetBoundaryLeft(level0_files[0]->smallest.user_values),
                           test::GetBoundaryLeft(level0_files[1]->smallest.user_values));
  auto max_left = std::max(test::GetBoundaryLeft(level0_files[0]->largest.user_values),
                           test::GetBoundaryLeft(level0_files[1]->largest.user_values));
  ASSERT_LE(min_left, max_left);

  auto min_right = std::min(test::GetBoundaryRight(level0_files[0]->smallest.user_values),
                            test::GetBoundaryRight(level0_files[1]->smallest.user_values));
  auto max_right = std::max(test::GetBoundaryRight(level0_files[0]->largest.user_values),
                            test::GetBoundaryRight(level0_files[1]->largest.user_values));
  ASSERT_LE(min_right, max_right);
  ASSERT_NO_FATAL_FAILURE(VerifyFrontier(*level0_files[0], 3, 4));
  ASSERT_NO_FATAL_FAILURE(VerifyFrontier(*level0_files[1], 1, 2));

  RunCompaction({level0_files}, expected_results);

  level0_files = cfd_->current()->storage_info()->LevelFiles(0);
  level1_files = cfd_->current()->storage_info()->LevelFiles(1);

  ASSERT_EQ(0, level0_files.size());
  ASSERT_EQ(1, level1_files.size());
  ASSERT_EQ(4, level1_files[0]->largest.seqno);
  ASSERT_EQ(0, level1_files[0]->smallest.seqno);  // kv's seq number gets zeroed out.
  ASSERT_EQ(min_left, test::GetBoundaryLeft(level1_files[0]->smallest.user_values));
  ASSERT_EQ(max_left, test::GetBoundaryLeft(level1_files[0]->largest.user_values));
  ASSERT_EQ(min_right, test::GetBoundaryRight(level1_files[0]->smallest.user_values));
  ASSERT_EQ(max_right, test::GetBoundaryRight(level1_files[0]->largest.user_values));
  ASSERT_NO_FATAL_FAILURE(VerifyFrontier(*level1_files[0], 1, 4));
}


TEST_F(CompactionJobTest, SeqNoTrackingWithDeleteAll) {
  NewDB();

  auto file1 = mock::MakeMockFile({{KeyStr("b", 4U, kTypeDeletion), ""},
                                      {KeyStr("c", 3U, kTypeDeletion), "val"}});
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({{KeyStr("b", 2U, kTypeValue), "val"},
                                      {KeyStr("c", 1U, kTypeValue), "val"}});
  AddMockFile(file2);

  stl_wrappers::KVMap empty_map;
  auto expected_results = empty_map;

  SetLastSequence(4U);
  auto level0_files = cfd_->current()->storage_info()->LevelFiles(0);
  auto level1_files = cfd_->current()->storage_info()->LevelFiles(1);
  ASSERT_EQ(2, level0_files.size());
  ASSERT_EQ(0, level1_files.size());
  ASSERT_EQ(4, level0_files[0]->largest.seqno);
  ASSERT_EQ(3, level0_files[0]->smallest.seqno);
  ASSERT_EQ(2, level0_files[1]->largest.seqno);
  ASSERT_EQ(1, level0_files[1]->smallest.seqno);
  RunCompaction({level0_files}, expected_results);

  level0_files = cfd_->current()->storage_info()->LevelFiles(0);
  level1_files = cfd_->current()->storage_info()->LevelFiles(1);
  ASSERT_EQ(0, level0_files.size());
  ASSERT_EQ(0, level1_files.size());
}

TEST_F(CompactionJobTest, SimpleOverwrite) {
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("a", 3U, kTypeValue), "val2"},
      {KeyStr("b", 4U, kTypeValue), "val3"},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({{KeyStr("a", 1U, kTypeValue), "val"},
                                   {KeyStr("b", 2U, kTypeValue), "val"}});
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("a", 0U, kTypeValue), "val2"},
                          {KeyStr("b", 4U, kTypeValue), "val3"}});

  SetLastSequence(4U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

TEST_F(CompactionJobTest, SimpleNonLastLevel) {
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("a", 5U, kTypeValue), "val2"},
      {KeyStr("b", 6U, kTypeValue), "val3"},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({{KeyStr("a", 3U, kTypeValue), "val"},
                                   {KeyStr("b", 4U, kTypeValue), "val"}});
  AddMockFile(file2, 1);

  auto file3 = mock::MakeMockFile({{KeyStr("a", 1U, kTypeValue), "val"},
                                   {KeyStr("b", 2U, kTypeValue), "val"}});
  AddMockFile(file3, 2);

  // Because level 1 is not the last level, the sequence numbers of a and b
  // cannot be set to 0
  auto expected_results =
      mock::MakeMockFile({{KeyStr("a", 5U, kTypeValue), "val2"},
                          {KeyStr("b", 6U, kTypeValue), "val3"}});

  SetLastSequence(6U);
  auto lvl0_files = cfd_->current()->storage_info()->LevelFiles(0);
  auto lvl1_files = cfd_->current()->storage_info()->LevelFiles(1);
  RunCompaction({lvl0_files, lvl1_files}, expected_results);
}

TEST_F(CompactionJobTest, SimpleMerge) {
  merge_op_ = MergeOperators::CreateStringAppendOperator();
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("a", 5U, kTypeMerge), "5"},
      {KeyStr("a", 4U, kTypeMerge), "4"},
      {KeyStr("a", 3U, kTypeValue), "3"},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile(
      {{KeyStr("b", 2U, kTypeMerge), "2"}, {KeyStr("b", 1U, kTypeValue), "1"}});
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("a", 0U, kTypeValue), "3,4,5"},
                          {KeyStr("b", 2U, kTypeValue), "1,2"}});

  SetLastSequence(5U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

TEST_F(CompactionJobTest, NonAssocMerge) {
  merge_op_ = MergeOperators::CreateStringAppendTESTOperator();
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("a", 5U, kTypeMerge), "5"},
      {KeyStr("a", 4U, kTypeMerge), "4"},
      {KeyStr("a", 3U, kTypeMerge), "3"},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile(
      {{KeyStr("b", 2U, kTypeMerge), "2"}, {KeyStr("b", 1U, kTypeMerge), "1"}});
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("a", 0U, kTypeValue), "3,4,5"},
                          {KeyStr("b", 2U, kTypeMerge), "2"},
                          {KeyStr("b", 1U, kTypeMerge), "1"}});

  SetLastSequence(5U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

// Filters merge operands with value 10.
TEST_F(CompactionJobTest, MergeOperandFilter) {
  merge_op_ = MergeOperators::CreateUInt64AddOperator();
  compaction_filter_.reset(new test::FilterNumber(10U));
  NewDB();

  auto file1 = mock::MakeMockFile(
      {{KeyStr("a", 5U, kTypeMerge), test::EncodeInt(5U)},
       {KeyStr("a", 4U, kTypeMerge), test::EncodeInt(10U)},  // Filtered
       {KeyStr("a", 3U, kTypeMerge), test::EncodeInt(3U)}});
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({
      {KeyStr("b", 2U, kTypeMerge), test::EncodeInt(2U)},
      {KeyStr("b", 1U, kTypeMerge), test::EncodeInt(10U)}  // Filtered
  });
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("a", 0U, kTypeValue), test::EncodeInt(8U)},
                          {KeyStr("b", 2U, kTypeMerge), test::EncodeInt(2U)}});

  SetLastSequence(5U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

TEST_F(CompactionJobTest, FilterSomeMergeOperands) {
  merge_op_ = MergeOperators::CreateUInt64AddOperator();
  compaction_filter_.reset(new test::FilterNumber(10U));
  NewDB();

  auto file1 = mock::MakeMockFile(
      {{KeyStr("a", 5U, kTypeMerge), test::EncodeInt(5U)},
       {KeyStr("a", 4U, kTypeMerge), test::EncodeInt(10U)},  // Filtered
       {KeyStr("a", 3U, kTypeValue), test::EncodeInt(5U)},
       {KeyStr("d", 8U, kTypeMerge), test::EncodeInt(10U)}});
  AddMockFile(file1);

  auto file2 =
      mock::MakeMockFile({{KeyStr("b", 2U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 1U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("c", 2U, kTypeMerge), test::EncodeInt(3U)},
                          {KeyStr("c", 1U, kTypeValue), test::EncodeInt(7U)},
                          {KeyStr("d", 1U, kTypeValue), test::EncodeInt(6U)}});
  AddMockFile(file2);

  auto file3 =
      mock::MakeMockFile({{KeyStr("a", 1U, kTypeMerge), test::EncodeInt(3U)}});
  AddMockFile(file3, 2);

  auto expected_results = mock::MakeMockFile({
      {KeyStr("a", 5U, kTypeValue), test::EncodeInt(10U)},
      {KeyStr("c", 2U, kTypeValue), test::EncodeInt(10U)},
      {KeyStr("d", 1U, kTypeValue), test::EncodeInt(6U)}
      // b does not appear because the operands are filtered
  });

  SetLastSequence(5U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

// Test where all operands/merge results are filtered out.
TEST_F(CompactionJobTest, FilterAllMergeOperands) {
  merge_op_ = MergeOperators::CreateUInt64AddOperator();
  compaction_filter_.reset(new test::FilterNumber(10U));
  NewDB();

  auto file1 =
      mock::MakeMockFile({{KeyStr("a", 11U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("a", 10U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("a", 9U, kTypeMerge), test::EncodeInt(10U)}});
  AddMockFile(file1);

  auto file2 =
      mock::MakeMockFile({{KeyStr("b", 8U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 7U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 6U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 5U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 4U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 3U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 2U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("c", 2U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("c", 1U, kTypeMerge), test::EncodeInt(10U)}});
  AddMockFile(file2);

  auto file3 =
      mock::MakeMockFile({{KeyStr("a", 2U, kTypeMerge), test::EncodeInt(10U)},
                          {KeyStr("b", 1U, kTypeMerge), test::EncodeInt(10U)}});
  AddMockFile(file3, 2);

  SetLastSequence(11U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);

  stl_wrappers::KVMap empty_map;
  RunCompaction({files}, empty_map);
}

TEST_F(CompactionJobTest, SimpleSingleDelete) {
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("a", 5U, kTypeDeletion), ""},
      {KeyStr("b", 6U, kTypeSingleDeletion), ""},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({{KeyStr("a", 3U, kTypeValue), "val"},
                                   {KeyStr("b", 4U, kTypeValue), "val"}});
  AddMockFile(file2);

  auto file3 = mock::MakeMockFile({
      {KeyStr("a", 1U, kTypeValue), "val"},
  });
  AddMockFile(file3, 2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("a", 5U, kTypeDeletion), ""}});

  SetLastSequence(6U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

TEST_F(CompactionJobTest, SingleDeleteSnapshots) {
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("A", 12U, kTypeSingleDeletion), ""},
      {KeyStr("a", 12U, kTypeSingleDeletion), ""},
      {KeyStr("b", 21U, kTypeSingleDeletion), ""},
      {KeyStr("c", 22U, kTypeSingleDeletion), ""},
      {KeyStr("d", 9U, kTypeSingleDeletion), ""},
      {KeyStr("f", 21U, kTypeSingleDeletion), ""},
      {KeyStr("j", 11U, kTypeSingleDeletion), ""},
      {KeyStr("j", 9U, kTypeSingleDeletion), ""},
      {KeyStr("k", 12U, kTypeSingleDeletion), ""},
      {KeyStr("k", 11U, kTypeSingleDeletion), ""},
      {KeyStr("l", 3U, kTypeSingleDeletion), ""},
      {KeyStr("l", 2U, kTypeSingleDeletion), ""},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({
      {KeyStr("0", 2U, kTypeSingleDeletion), ""},
      {KeyStr("a", 11U, kTypeValue), "val1"},
      {KeyStr("b", 11U, kTypeValue), "val2"},
      {KeyStr("c", 21U, kTypeValue), "val3"},
      {KeyStr("d", 8U, kTypeValue), "val4"},
      {KeyStr("e", 2U, kTypeSingleDeletion), ""},
      {KeyStr("f", 1U, kTypeValue), "val1"},
      {KeyStr("g", 11U, kTypeSingleDeletion), ""},
      {KeyStr("h", 2U, kTypeSingleDeletion), ""},
      {KeyStr("m", 12U, kTypeValue), "val1"},
      {KeyStr("m", 11U, kTypeSingleDeletion), ""},
      {KeyStr("m", 8U, kTypeValue), "val2"},
  });
  AddMockFile(file2);

  auto file3 = mock::MakeMockFile({
      {KeyStr("A", 1U, kTypeValue), "val"},
      {KeyStr("e", 1U, kTypeValue), "val"},
  });
  AddMockFile(file3, 2);

  auto expected_results = mock::MakeMockFile({
      {KeyStr("A", 12U, kTypeSingleDeletion), ""},
      {KeyStr("a", 12U, kTypeSingleDeletion), ""},
      {KeyStr("a", 11U, kTypeValue), ""},
      {KeyStr("b", 21U, kTypeSingleDeletion), ""},
      {KeyStr("b", 11U, kTypeValue), "val2"},
      {KeyStr("c", 22U, kTypeSingleDeletion), ""},
      {KeyStr("c", 21U, kTypeValue), ""},
      {KeyStr("e", 2U, kTypeSingleDeletion), ""},
      {KeyStr("f", 21U, kTypeSingleDeletion), ""},
      {KeyStr("f", 1U, kTypeValue), "val1"},
      {KeyStr("g", 11U, kTypeSingleDeletion), ""},
      {KeyStr("j", 11U, kTypeSingleDeletion), ""},
      {KeyStr("k", 11U, kTypeSingleDeletion), ""},
      {KeyStr("m", 12U, kTypeValue), "val1"},
      {KeyStr("m", 11U, kTypeSingleDeletion), ""},
      {KeyStr("m", 8U, kTypeValue), "val2"},
  });

  SetLastSequence(22U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results, {10U, 20U}, 10U);
}

TEST_F(CompactionJobTest, EarliestWriteConflictSnapshot) {
  NewDB();

  // Test multiple snapshots where the earliest snapshot is not a
  // write-conflic-snapshot.

  auto file1 = mock::MakeMockFile({
      {KeyStr("A", 24U, kTypeSingleDeletion), ""},
      {KeyStr("A", 23U, kTypeValue), "val"},
      {KeyStr("B", 24U, kTypeSingleDeletion), ""},
      {KeyStr("B", 23U, kTypeValue), "val"},
      {KeyStr("D", 24U, kTypeSingleDeletion), ""},
      {KeyStr("G", 32U, kTypeSingleDeletion), ""},
      {KeyStr("G", 31U, kTypeValue), "val"},
      {KeyStr("G", 24U, kTypeSingleDeletion), ""},
      {KeyStr("G", 23U, kTypeValue), "val2"},
      {KeyStr("H", 31U, kTypeValue), "val"},
      {KeyStr("H", 24U, kTypeSingleDeletion), ""},
      {KeyStr("H", 23U, kTypeValue), "val"},
      {KeyStr("I", 35U, kTypeSingleDeletion), ""},
      {KeyStr("I", 34U, kTypeValue), "val2"},
      {KeyStr("I", 33U, kTypeSingleDeletion), ""},
      {KeyStr("I", 32U, kTypeValue), "val3"},
      {KeyStr("I", 31U, kTypeSingleDeletion), ""},
      {KeyStr("J", 34U, kTypeValue), "val"},
      {KeyStr("J", 33U, kTypeSingleDeletion), ""},
      {KeyStr("J", 25U, kTypeValue), "val2"},
      {KeyStr("J", 24U, kTypeSingleDeletion), ""},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({
      {KeyStr("A", 14U, kTypeSingleDeletion), ""},
      {KeyStr("A", 13U, kTypeValue), "val2"},
      {KeyStr("C", 14U, kTypeSingleDeletion), ""},
      {KeyStr("C", 13U, kTypeValue), "val"},
      {KeyStr("E", 12U, kTypeSingleDeletion), ""},
      {KeyStr("F", 4U, kTypeSingleDeletion), ""},
      {KeyStr("F", 3U, kTypeValue), "val"},
      {KeyStr("G", 14U, kTypeSingleDeletion), ""},
      {KeyStr("G", 13U, kTypeValue), "val3"},
      {KeyStr("H", 14U, kTypeSingleDeletion), ""},
      {KeyStr("H", 13U, kTypeValue), "val2"},
      {KeyStr("I", 13U, kTypeValue), "val4"},
      {KeyStr("I", 12U, kTypeSingleDeletion), ""},
      {KeyStr("I", 11U, kTypeValue), "val5"},
      {KeyStr("J", 15U, kTypeValue), "val3"},
      {KeyStr("J", 14U, kTypeSingleDeletion), ""},
  });
  AddMockFile(file2);

  auto expected_results = mock::MakeMockFile({
      {KeyStr("A", 24U, kTypeSingleDeletion), ""},
      {KeyStr("A", 23U, kTypeValue), ""},
      {KeyStr("B", 24U, kTypeSingleDeletion), ""},
      {KeyStr("B", 23U, kTypeValue), ""},
      {KeyStr("D", 24U, kTypeSingleDeletion), ""},
      {KeyStr("E", 12U, kTypeSingleDeletion), ""},
      {KeyStr("G", 32U, kTypeSingleDeletion), ""},
      {KeyStr("G", 31U, kTypeValue), ""},
      {KeyStr("H", 31U, kTypeValue), "val"},
      {KeyStr("I", 35U, kTypeSingleDeletion), ""},
      {KeyStr("I", 34U, kTypeValue), ""},
      {KeyStr("I", 31U, kTypeSingleDeletion), ""},
      {KeyStr("I", 13U, kTypeValue), "val4"},
      {KeyStr("J", 34U, kTypeValue), "val"},
      {KeyStr("J", 33U, kTypeSingleDeletion), ""},
      {KeyStr("J", 25U, kTypeValue), "val2"},
      {KeyStr("J", 24U, kTypeSingleDeletion), ""},
      {KeyStr("J", 15U, kTypeValue), "val3"},
      {KeyStr("J", 14U, kTypeSingleDeletion), ""},
  });

  SetLastSequence(24U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results, {10U, 20U, 30U}, 20U);
}

TEST_F(CompactionJobTest, SingleDeleteZeroSeq) {
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("A", 10U, kTypeSingleDeletion), ""},
      {KeyStr("dummy", 5U, kTypeValue), "val2"},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({
      {KeyStr("A", 0U, kTypeValue), "val"},
  });
  AddMockFile(file2);

  auto expected_results = mock::MakeMockFile({
      {KeyStr("dummy", 5U, kTypeValue), "val2"},
  });

  SetLastSequence(22U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results, {});
}

TEST_F(CompactionJobTest, MultiSingleDelete) {
  // Tests three scenarios involving multiple single delete/put pairs:
  //
  // A: Put Snapshot SDel Put SDel -> Put Snapshot SDel
  // B: Snapshot Put SDel Put SDel Snapshot -> Snapshot SDel Snapshot
  // C: SDel Put SDel Snapshot Put -> Snapshot Put
  // D: (Put) SDel Snapshot Put SDel -> (Put) SDel Snapshot SDel
  // E: Put SDel Snapshot Put SDel -> Snapshot SDel
  // F: Put SDel Put Sdel Snapshot -> removed
  // G: Snapshot SDel Put SDel Put -> Snapshot Put SDel
  // H: (Put) Put SDel Put Sdel Snapshot -> Removed
  // I: (Put) Snapshot Put SDel Put SDel -> SDel
  // J: Put Put SDel Put SDel SDel Snapshot Put Put SDel SDel Put
  //      -> Snapshot Put
  // K: SDel SDel Put SDel Put Put Snapshot SDel Put SDel SDel Put SDel
  //      -> Snapshot Put Snapshot SDel
  // L: SDel Put Del Put SDel Snapshot Del Put Del SDel Put SDel
  //      -> Snapshot SDel
  // M: (Put) SDel Put Del Put SDel Snapshot Put Del SDel Put SDel Del
  //      -> SDel Snapshot Del
  NewDB();

  auto file1 = mock::MakeMockFile({
      {KeyStr("A", 14U, kTypeSingleDeletion), ""},
      {KeyStr("A", 13U, kTypeValue), "val5"},
      {KeyStr("A", 12U, kTypeSingleDeletion), ""},
      {KeyStr("B", 14U, kTypeSingleDeletion), ""},
      {KeyStr("B", 13U, kTypeValue), "val2"},
      {KeyStr("C", 14U, kTypeValue), "val3"},
      {KeyStr("D", 12U, kTypeSingleDeletion), ""},
      {KeyStr("D", 11U, kTypeValue), "val4"},
      {KeyStr("G", 15U, kTypeValue), "val"},
      {KeyStr("G", 14U, kTypeSingleDeletion), ""},
      {KeyStr("G", 13U, kTypeValue), "val"},
      {KeyStr("I", 14U, kTypeSingleDeletion), ""},
      {KeyStr("I", 13U, kTypeValue), "val"},
      {KeyStr("J", 15U, kTypeValue), "val"},
      {KeyStr("J", 14U, kTypeSingleDeletion), ""},
      {KeyStr("J", 13U, kTypeSingleDeletion), ""},
      {KeyStr("J", 12U, kTypeValue), "val"},
      {KeyStr("J", 11U, kTypeValue), "val"},
      {KeyStr("K", 16U, kTypeSingleDeletion), ""},
      {KeyStr("K", 15U, kTypeValue), "val1"},
      {KeyStr("K", 14U, kTypeSingleDeletion), ""},
      {KeyStr("K", 13U, kTypeSingleDeletion), ""},
      {KeyStr("K", 12U, kTypeValue), "val2"},
      {KeyStr("K", 11U, kTypeSingleDeletion), ""},
      {KeyStr("L", 16U, kTypeSingleDeletion), ""},
      {KeyStr("L", 15U, kTypeValue), "val"},
      {KeyStr("L", 14U, kTypeSingleDeletion), ""},
      {KeyStr("L", 13U, kTypeDeletion), ""},
      {KeyStr("L", 12U, kTypeValue), "val"},
      {KeyStr("L", 11U, kTypeDeletion), ""},
      {KeyStr("M", 16U, kTypeDeletion), ""},
      {KeyStr("M", 15U, kTypeSingleDeletion), ""},
      {KeyStr("M", 14U, kTypeValue), "val"},
      {KeyStr("M", 13U, kTypeSingleDeletion), ""},
      {KeyStr("M", 12U, kTypeDeletion), ""},
      {KeyStr("M", 11U, kTypeValue), "val"},
  });
  AddMockFile(file1);

  auto file2 = mock::MakeMockFile({
      {KeyStr("A", 10U, kTypeValue), "val"},
      {KeyStr("B", 12U, kTypeSingleDeletion), ""},
      {KeyStr("B", 11U, kTypeValue), "val2"},
      {KeyStr("C", 10U, kTypeSingleDeletion), ""},
      {KeyStr("C", 9U, kTypeValue), "val6"},
      {KeyStr("C", 8U, kTypeSingleDeletion), ""},
      {KeyStr("D", 10U, kTypeSingleDeletion), ""},
      {KeyStr("E", 12U, kTypeSingleDeletion), ""},
      {KeyStr("E", 11U, kTypeValue), "val"},
      {KeyStr("E", 5U, kTypeSingleDeletion), ""},
      {KeyStr("E", 4U, kTypeValue), "val"},
      {KeyStr("F", 6U, kTypeSingleDeletion), ""},
      {KeyStr("F", 5U, kTypeValue), "val"},
      {KeyStr("F", 4U, kTypeSingleDeletion), ""},
      {KeyStr("F", 3U, kTypeValue), "val"},
      {KeyStr("G", 12U, kTypeSingleDeletion), ""},
      {KeyStr("H", 6U, kTypeSingleDeletion), ""},
      {KeyStr("H", 5U, kTypeValue), "val"},
      {KeyStr("H", 4U, kTypeSingleDeletion), ""},
      {KeyStr("H", 3U, kTypeValue), "val"},
      {KeyStr("I", 12U, kTypeSingleDeletion), ""},
      {KeyStr("I", 11U, kTypeValue), "val"},
      {KeyStr("J", 6U, kTypeSingleDeletion), ""},
      {KeyStr("J", 5U, kTypeSingleDeletion), ""},
      {KeyStr("J", 4U, kTypeValue), "val"},
      {KeyStr("J", 3U, kTypeSingleDeletion), ""},
      {KeyStr("J", 2U, kTypeValue), "val"},
      {KeyStr("K", 8U, kTypeValue), "val3"},
      {KeyStr("K", 7U, kTypeValue), "val4"},
      {KeyStr("K", 6U, kTypeSingleDeletion), ""},
      {KeyStr("K", 5U, kTypeValue), "val5"},
      {KeyStr("K", 2U, kTypeSingleDeletion), ""},
      {KeyStr("K", 1U, kTypeSingleDeletion), ""},
      {KeyStr("L", 5U, kTypeSingleDeletion), ""},
      {KeyStr("L", 4U, kTypeValue), "val"},
      {KeyStr("L", 3U, kTypeDeletion), ""},
      {KeyStr("L", 2U, kTypeValue), "val"},
      {KeyStr("L", 1U, kTypeSingleDeletion), ""},
      {KeyStr("M", 10U, kTypeSingleDeletion), ""},
      {KeyStr("M", 7U, kTypeValue), "val"},
      {KeyStr("M", 5U, kTypeDeletion), ""},
      {KeyStr("M", 4U, kTypeValue), "val"},
      {KeyStr("M", 3U, kTypeSingleDeletion), ""},
  });
  AddMockFile(file2);

  auto file3 = mock::MakeMockFile({
      {KeyStr("D", 1U, kTypeValue), "val"},
      {KeyStr("H", 1U, kTypeValue), "val"},
      {KeyStr("I", 2U, kTypeValue), "val"},
  });
  AddMockFile(file3, 2);

  auto file4 = mock::MakeMockFile({
      {KeyStr("M", 1U, kTypeValue), "val"},
  });
  AddMockFile(file4, 2);

  auto expected_results =
      mock::MakeMockFile({{KeyStr("A", 14U, kTypeSingleDeletion), ""},
                          {KeyStr("A", 13U, kTypeValue), ""},
                          {KeyStr("A", 12U, kTypeSingleDeletion), ""},
                          {KeyStr("A", 10U, kTypeValue), "val"},
                          {KeyStr("B", 14U, kTypeSingleDeletion), ""},
                          {KeyStr("B", 13U, kTypeValue), ""},
                          {KeyStr("C", 14U, kTypeValue), "val3"},
                          {KeyStr("D", 12U, kTypeSingleDeletion), ""},
                          {KeyStr("D", 11U, kTypeValue), ""},
                          {KeyStr("D", 10U, kTypeSingleDeletion), ""},
                          {KeyStr("E", 12U, kTypeSingleDeletion), ""},
                          {KeyStr("E", 11U, kTypeValue), ""},
                          {KeyStr("G", 15U, kTypeValue), "val"},
                          {KeyStr("G", 12U, kTypeSingleDeletion), ""},
                          {KeyStr("I", 14U, kTypeSingleDeletion), ""},
                          {KeyStr("I", 13U, kTypeValue), ""},
                          {KeyStr("J", 15U, kTypeValue), "val"},
                          {KeyStr("K", 16U, kTypeSingleDeletion), ""},
                          {KeyStr("K", 15U, kTypeValue), ""},
                          {KeyStr("K", 11U, kTypeSingleDeletion), ""},
                          {KeyStr("K", 8U, kTypeValue), "val3"},
                          {KeyStr("L", 16U, kTypeSingleDeletion), ""},
                          {KeyStr("L", 15U, kTypeValue), ""},
                          {KeyStr("M", 16U, kTypeDeletion), ""},
                          {KeyStr("M", 3U, kTypeSingleDeletion), ""}});

  SetLastSequence(22U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results, {10U}, 10U);
}

// This test documents the behavior where a corrupt key follows a deletion or a
// single deletion and the (single) deletion gets removed while the corrupt key
// gets written out. TODO(noetzli): We probably want a better way to treat
// corrupt keys.
TEST_F(CompactionJobTest, CorruptionAfterDeletion) {
  NewDB();

  auto file1 =
      mock::MakeMockFile({{test::KeyStr("A", 6U, kTypeValue), "val3"},
                          {test::KeyStr("a", 5U, kTypeDeletion), ""},
                          {test::KeyStr("a", 4U, kTypeValue, true), "val"}});
  AddMockFile(file1);

  auto file2 =
      mock::MakeMockFile({{test::KeyStr("b", 3U, kTypeSingleDeletion), ""},
                          {test::KeyStr("b", 2U, kTypeValue, true), "val"},
                          {test::KeyStr("c", 1U, kTypeValue), "val2"}});
  AddMockFile(file2);

  auto expected_results =
      mock::MakeMockFile({{test::KeyStr("A", 0U, kTypeValue), "val3"},
                          {test::KeyStr("a", 0U, kTypeValue, true), "val"},
                          {test::KeyStr("b", 0U, kTypeValue, true), "val"},
                          {test::KeyStr("c", 1U, kTypeValue), "val2"}});

  SetLastSequence(6U);
  auto files = cfd_->current()->storage_info()->LevelFiles(0);
  RunCompaction({files}, expected_results);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
