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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.


#include <stdlib.h>

#include <vector>
#include <map>
#include <string>

#include <boost/function.hpp>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/db_test_util.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/transaction_log.h"

#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/test_macros.h"

using namespace std::chrono_literals;

namespace rocksdb {

YB_STRONGLY_TYPED_BOOL(StopOnMaxFilesDeleted);

class DeleteFileTest : public RocksDBTest {
 public:
  std::string dbname_;
  Options options_;
  std::unique_ptr<DB> db_;
  Env* env_;
  int numlevels_;

  DeleteFileTest() {
    db_.reset();
    env_ = Env::Default();
    options_.delete_obsolete_files_period_micros = 0;  // always do full purge
    options_.enable_thread_tracking = true;
    options_.write_buffer_size = 1024*1024*1000;
    options_.target_file_size_base = 1024*1024*1000;
    options_.max_bytes_for_level_base = 1024*1024*1000;
    options_.WAL_ttl_seconds = 300; // Used to test log files
    options_.WAL_size_limit_MB = 1024; // Used to test log files
    dbname_ = test::TmpDir() + "/deletefile_test";
    options_.wal_dir = dbname_ + "/wal_files";

    // clean up all the files that might have been there before
    CHECK_OK(DeleteRecursively(env_, dbname_));
    CHECK_OK(DestroyDB(dbname_, options_));
    numlevels_ = 7;
    EXPECT_OK(ReopenDB(true));
  }

  Status ReopenDB(bool create) {
    db_.reset();
    if (create) {
      RETURN_NOT_OK(DestroyDB(dbname_, options_));
    }
    options_.create_if_missing = create;
    DB* db;
    auto status = DB::Open(options_, dbname_, &db);
    db_.reset(db);
    return status;
  }

  void CloseDB() {
    db_.reset();
  }

  void AddKeys(int numkeys, int startkey = 0) {
    WriteOptions options;
    options.sync = false;
    ReadOptions roptions;
    for (int i = startkey; i < (numkeys + startkey) ; i++) {
      std::string temp = ToString(i);
      Slice key(temp);
      Slice value(temp);
      ASSERT_OK(db_->Put(options, key, value));
    }
  }

  int numKeysInLevels(
    const std::vector<LiveFileMetaData> &metadata,
    std::vector<int> *keysperlevel = nullptr) {

    if (keysperlevel != nullptr) {
      keysperlevel->resize(numlevels_);
    }

    int numKeys = 0;
    for (size_t i = 0; i < metadata.size(); i++) {
      int startkey = atoi(metadata[i].smallest.key.c_str());
      int endkey = atoi(metadata[i].largest.key.c_str());
      int numkeysinfile = (endkey - startkey + 1);
      numKeys += numkeysinfile;
      if (keysperlevel != nullptr) {
        (*keysperlevel)[static_cast<int>(metadata[i].level)] += numkeysinfile;
      }
      fprintf(stderr, "level %d name %s smallest %s largest %s\n",
              metadata[i].level, metadata[i].Name().c_str(),
              metadata[i].smallest.key.c_str(),
              metadata[i].largest.key.c_str());
    }
    return numKeys;
  }

  void CreateTwoLevels() {
    AddKeys(50000, 10000);
    DBImpl* dbi = dbfull();
    ASSERT_OK(dbi->TEST_FlushMemTable());
    ASSERT_OK(dbi->TEST_WaitForFlushMemTable());
    for (int i = 0; i < 2; ++i) {
      ASSERT_OK(dbi->TEST_CompactRange(i, nullptr, nullptr));
    }

    AddKeys(50000, 10000);
    ASSERT_OK(dbi->TEST_FlushMemTable());
    ASSERT_OK(dbi->TEST_WaitForFlushMemTable());
    ASSERT_OK(dbi->TEST_CompactRange(0, nullptr, nullptr));
  }

  void CheckFileTypeCounts(const std::string& dir,
                           int required_log,
                           int required_sst,
                           int required_manifest) {
    std::vector<std::string> filenames;
    ASSERT_OK(env_->GetChildren(dir, &filenames));

    int log_cnt = 0, sst_cnt = 0, manifest_cnt = 0;
    for (auto file : filenames) {
      uint64_t number;
      FileType type;
      if (ParseFileName(file, &number, &type)) {
        log_cnt += (type == kLogFile);
        sst_cnt += (type == kTableFile);
        manifest_cnt += (type == kDescriptorFile);
      }
    }
    ASSERT_EQ(required_log, log_cnt);
    ASSERT_EQ(required_sst, sst_cnt);
    ASSERT_EQ(required_manifest, manifest_cnt);
  }

  DBImpl* dbfull() { return reinterpret_cast<DBImpl*>(db_.get()); }

  Status FlushSync() {
    return dbfull()->TEST_FlushMemTable(/* wait = */ true);
  }

  Result<std::vector<LiveFileMetaData>> AddFiles(int num_sst_files, int num_key_per_sst);

  size_t TryDeleteFiles(
      const std::vector<LiveFileMetaData>& files, size_t max_files_to_delete,
      StopOnMaxFilesDeleted stop_on_max_files_deleted,
      boost::function<bool()> stop_condition);
};

TEST_F(DeleteFileTest, AddKeysAndQueryLevels) {
  CreateTwoLevels();
  std::vector<LiveFileMetaData> metadata;
  db_->GetLiveFilesMetaData(&metadata);

  std::string level1file = "";
  int level1keycount = 0;
  std::string level2file = "";
  int level2keycount = 0;
  int level1index = 0;
  int level2index = 1;

  ASSERT_EQ((int)metadata.size(), 2);
  if (metadata[0].level == 2) {
    level1index = 1;
    level2index = 0;
  }

  level1file = metadata[level1index].Name();
  int startkey = atoi(metadata[level1index].smallest.key.c_str());
  int endkey = atoi(metadata[level1index].largest.key.c_str());
  level1keycount = (endkey - startkey + 1);
  level2file = metadata[level2index].Name();
  startkey = atoi(metadata[level2index].smallest.key.c_str());
  endkey = atoi(metadata[level2index].largest.key.c_str());
  level2keycount = (endkey - startkey + 1);

  // COntrolled setup. Levels 1 and 2 should both have 50K files.
  // This is a little fragile as it depends on the current
  // compaction heuristics.
  ASSERT_EQ(level1keycount, 50000);
  ASSERT_EQ(level2keycount, 50000);

  Status status = db_->DeleteFile("0.sst");
  ASSERT_TRUE(status.IsInvalidArgument());

  // intermediate level files cannot be deleted.
  status = db_->DeleteFile(level1file);
  ASSERT_TRUE(status.IsInvalidArgument());

  // Lowest level file deletion should succeed.
  ASSERT_OK(db_->DeleteFile(level2file));

  CloseDB();
}

TEST_F(DeleteFileTest, PurgeObsoleteFilesTest) {
  CreateTwoLevels();
  // there should be only one (empty) log file because CreateTwoLevels()
  // flushes the memtables to disk
  CheckFileTypeCounts(options_.wal_dir, 1, 0, 0);
  // 2 ssts, 1 manifest
  CheckFileTypeCounts(dbname_, 0, 2, 1);
  std::string first("0"), last("999999");
  CompactRangeOptions compact_options;
  compact_options.change_level = true;
  compact_options.target_level = 2;
  Slice first_slice(first), last_slice(last);
  ASSERT_OK(db_->CompactRange(compact_options, &first_slice, &last_slice));
  // 1 sst after compaction
  CheckFileTypeCounts(dbname_, 0, 1, 1);

  // this time, we keep an iterator alive
  ASSERT_OK(ReopenDB(true));
  Iterator *itr = 0;
  CreateTwoLevels();
  itr = db_->NewIterator(ReadOptions());
  ASSERT_OK(db_->CompactRange(compact_options, &first_slice, &last_slice));
  // 3 sst after compaction with live iterator
  CheckFileTypeCounts(dbname_, 0, 3, 1);
  delete itr;
  // 1 sst after iterator deletion
  CheckFileTypeCounts(dbname_, 0, 1, 1);

  CloseDB();
}

TEST_F(DeleteFileTest, DeleteFileWithIterator) {
  CreateTwoLevels();
  ReadOptions options;
  Iterator* it = db_->NewIterator(options);
  std::vector<LiveFileMetaData> metadata;
  db_->GetLiveFilesMetaData(&metadata);

  std::string level2file = "";

  ASSERT_EQ((int)metadata.size(), 2);
  if (metadata[0].level == 1) {
    level2file = metadata[1].Name();
  } else {
    level2file = metadata[0].Name();
  }

  Status status = db_->DeleteFile(level2file);
  fprintf(stdout, "Deletion status %s: %s\n",
          level2file.c_str(), status.ToString().c_str());
  ASSERT_TRUE(status.ok());
  it->SeekToFirst();
  int numKeysIterated = 0;
  while(ASSERT_RESULT(it->CheckedValid())) {
    numKeysIterated++;
    it->Next();
  }
  ASSERT_EQ(numKeysIterated, 50000);
  delete it;
  CloseDB();
}

TEST_F(DeleteFileTest, DeleteLogFiles) {
  AddKeys(10, 0);
  VectorLogPtr logfiles;
  ASSERT_OK(db_->GetSortedWalFiles(&logfiles));
  ASSERT_GT(logfiles.size(), 0UL);
  // Take the last log file which is expected to be alive and try to delete it
  // Should not succeed because live logs are not allowed to be deleted
  std::unique_ptr<LogFile> alive_log = std::move(logfiles.back());
  ASSERT_EQ(alive_log->Type(), kAliveLogFile);
  ASSERT_OK(env_->FileExists(options_.wal_dir + "/" + alive_log->PathName()));
  fprintf(stdout, "Deleting alive log file %s\n",
          alive_log->PathName().c_str());
  ASSERT_TRUE(!db_->DeleteFile(alive_log->PathName()).ok());
  ASSERT_OK(env_->FileExists(options_.wal_dir + "/" + alive_log->PathName()));
  logfiles.clear();

  // Call Flush to bring about a new working log file and add more keys
  // Call Flush again to flush out memtable and move alive log to archived log
  // and try to delete the archived log file
  FlushOptions fopts;
  ASSERT_OK(db_->Flush(fopts));
  AddKeys(10, 0);
  ASSERT_OK(db_->Flush(fopts));
  ASSERT_OK(db_->GetSortedWalFiles(&logfiles));
  ASSERT_GT(logfiles.size(), 0UL);
  std::unique_ptr<LogFile> archived_log = std::move(logfiles.front());
  ASSERT_EQ(archived_log->Type(), kArchivedLogFile);
  ASSERT_OK(
      env_->FileExists(options_.wal_dir + "/" + archived_log->PathName()));
  fprintf(stdout, "Deleting archived log file %s\n",
          archived_log->PathName().c_str());
  ASSERT_OK(db_->DeleteFile(archived_log->PathName()));
  ASSERT_TRUE(env_->FileExists(options_.wal_dir + "/" + archived_log->PathName()).IsNotFound());
  CloseDB();
}

TEST_F(DeleteFileTest, DeleteNonDefaultColumnFamily) {
  CloseDB();
  DBOptions db_options;
  db_options.create_if_missing = true;
  db_options.create_missing_column_families = true;
  std::vector<ColumnFamilyDescriptor> column_families;
  column_families.emplace_back();
  column_families.emplace_back("new_cf", ColumnFamilyOptions());

  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DB* db;
  ASSERT_OK(DB::Open(db_options, dbname_, column_families, &handles, &db));

  Random rnd(5);
  for (int i = 0; i < 1000; ++i) {
    ASSERT_OK(db->Put(WriteOptions(), handles[1], test::RandomKey(&rnd, 10),
                      test::RandomKey(&rnd, 10)));
  }
  ASSERT_OK(db->Flush(FlushOptions(), handles[1]));
  for (int i = 0; i < 1000; ++i) {
    ASSERT_OK(db->Put(WriteOptions(), handles[1], test::RandomKey(&rnd, 10),
                      test::RandomKey(&rnd, 10)));
  }
  ASSERT_OK(db->Flush(FlushOptions(), handles[1]));

  std::vector<LiveFileMetaData> metadata;
  db->GetLiveFilesMetaData(&metadata);
  ASSERT_EQ(2U, metadata.size());
  ASSERT_EQ("new_cf", metadata[0].column_family_name);
  ASSERT_EQ("new_cf", metadata[1].column_family_name);
  auto old_file = metadata[0].smallest.seqno < metadata[1].smallest.seqno
                      ? metadata[0].Name()
                      : metadata[1].Name();
  auto new_file = metadata[0].smallest.seqno > metadata[1].smallest.seqno
                      ? metadata[0].Name()
                      : metadata[1].Name();
  ASSERT_TRUE(db->DeleteFile(new_file).IsInvalidArgument());
  ASSERT_OK(db->DeleteFile(old_file));

  {
    std::unique_ptr<Iterator> itr(db->NewIterator(ReadOptions(), handles[1]));
    int count = 0;
    for (itr->SeekToFirst(); ASSERT_RESULT(itr->CheckedValid()); itr->Next()) {
      ++count;
    }
    ASSERT_EQ(count, 1000);
  }

  delete handles[0];
  delete handles[1];
  delete db;

  ASSERT_OK(DB::Open(db_options, dbname_, column_families, &handles, &db));
  {
    std::unique_ptr<Iterator> itr(db->NewIterator(ReadOptions(), handles[1]));
    int count = 0;
    for (itr->SeekToFirst(); ASSERT_RESULT(itr->CheckedValid()); itr->Next()) {
      ++count;
    }
    ASSERT_EQ(count, 1000);
  }

  delete handles[0];
  delete handles[1];
  delete db;
}

Result<std::vector<LiveFileMetaData>> DeleteFileTest::AddFiles(
    const int num_sst_files, const int num_key_per_sst) {
  LOG(INFO) << "Writing " << num_sst_files << " SSTs";
  for (auto num = 0; num < num_sst_files; num++) {
    AddKeys(num_key_per_sst, 0);
    RETURN_NOT_OK(FlushSync());
  }
  std::vector<LiveFileMetaData> metadata;
  db_->GetLiveFilesMetaData(&metadata);
  return metadata;
}

size_t DeleteFileTest::TryDeleteFiles(
    const std::vector<LiveFileMetaData>& files, const size_t max_files_to_delete,
    const StopOnMaxFilesDeleted stop_on_max_files_deleted, boost::function<bool()> stop_condition) {
  size_t files_deleted = 0;
  LOG(INFO) << "Starting file deletion loop";
  while (!stop_condition()) {
    for (auto& file : files) {
      if (files_deleted >= max_files_to_delete) {
        if (stop_on_max_files_deleted) {
          return files_deleted;
        }
        // Just wait for stop condition.
        std::this_thread::sleep_for(10ms);
        continue;
      }
      if (db_->DeleteFile(file.Name()).ok()) {
        const auto file_path = file.BaseFilePath();

        std::vector<LiveFileMetaData> current_files;
        dbfull()->GetLiveFilesMetaData(&current_files);
        auto it = std::find_if(
            current_files.begin(), current_files.end(),
            [&](const auto& current_file) { return current_file.name_id == file.name_id; });
        if (it == current_files.end()) {
          LOG(INFO) << "Deleted file: " << file_path;
          ++files_deleted;
        }
      }
    }
  }
  return files_deleted;
}

namespace compaction_race {

constexpr auto kMaxNumSstFiles = 3;
constexpr auto kNumKeysPerSst = 10000;

} // namespace compaction_race

TEST_F(DeleteFileTest, DeleteWithManualCompaction) {
  yb::SyncPoint::GetInstance()->LoadDependency({
      {"DBImpl::DeleteFile:DecidedToDelete", "DBImpl::RunManualCompaction"}
  });

  for (int num_sst_files = 1; num_sst_files <= compaction_race::kMaxNumSstFiles; ++num_sst_files) {
    ASSERT_OK(ReopenDB(/* create = */ true));

    auto metadata = ASSERT_RESULT(AddFiles(num_sst_files, compaction_race::kNumKeysPerSst));

    yb::SyncPoint::GetInstance()->EnableProcessing();

    std::atomic<bool> manual_compaction_done{false};
    Status manual_compaction_status;

    std::thread manual_compaction([this, &manual_compaction_done, &manual_compaction_status] {
      manual_compaction_status = db_->CompactRange(
          rocksdb::CompactRangeOptions(), /* begin = */ nullptr, /* end = */ nullptr);
      manual_compaction_done = true;
    });

    auto files_deleted = TryDeleteFiles(
        metadata, /* max_files_to_delete = */ 1, StopOnMaxFilesDeleted::kFalse,
        [&manual_compaction_done] {
          return manual_compaction_done.load(std::memory_order_acquire);
        });

    manual_compaction.join();
    ASSERT_OK(manual_compaction_status);
    ASSERT_EQ(files_deleted, 1);

    CloseDB();

    yb::SyncPoint::GetInstance()->DisableProcessing();
    yb::SyncPoint::GetInstance()->ClearTrace();
  }
}

TEST_F(DeleteFileTest, DeleteWithBackgroundCompaction) {
  yb::SyncPoint::GetInstance()->LoadDependency({
      {"DBImpl::DeleteFile:DecidedToDelete", "DBImpl::EnableAutoCompaction"},
      {"DBImpl::SchedulePendingCompaction:Done", "VersionSet::LogAndApply:WriteManifest"},
  });

  for (int num_sst_files = 1; num_sst_files <= compaction_race::kMaxNumSstFiles; ++num_sst_files) {
    auto listener = std::make_shared<CompactionStartedListener>();
    options_.level0_file_num_compaction_trigger = 2;
    options_.disable_auto_compactions = true;
    options_.listeners.push_back(listener);
    ASSERT_OK(ReopenDB(/* create = */ true));

    auto metadata = ASSERT_RESULT(AddFiles(num_sst_files, compaction_race::kNumKeysPerSst));

    const bool expect_compaction = num_sst_files > 1;

    yb::SyncPoint::GetInstance()->EnableProcessing();

    std::atomic<bool> pending_compaction{false};
    std::thread enable_compactions([this, &pending_compaction] {
      ColumnFamilyData* cfd =
          static_cast<ColumnFamilyHandleImpl*>(db_->DefaultColumnFamily())->cfd();
      ASSERT_OK(db_->EnableAutoCompaction({db_->DefaultColumnFamily()}));
      {
        dbfull()->TEST_LockMutex();
        pending_compaction = cfd->pending_compaction();
        dbfull()->TEST_UnlockMutex();
      }
      LOG(INFO) << "pending_compaction: " << pending_compaction;
    });

    auto files_deleted = TryDeleteFiles(
        metadata, /* max_files_to_delete = */ 1, StopOnMaxFilesDeleted(!expect_compaction),
        [this, &listener] {
          // Stop after compaction has been started and finished.
          return listener->GetNumCompactionsStarted() > 0 &&
                 dbfull()->TEST_NumTotalRunningCompactions() == 0;
        });

    yb::SyncPoint::GetInstance()->DisableProcessing();
    yb::SyncPoint::GetInstance()->ClearTrace();

    enable_compactions.join();
    EXPECT_EQ(files_deleted, 1);
    if (!expect_compaction) {
      EXPECT_FALSE(pending_compaction);
      EXPECT_EQ(listener->GetNumCompactionsStarted(), 0);
    }

    ASSERT_NO_FATALS(AddKeys(10, 0));
    ASSERT_OK(FlushSync());

    CloseDB();
  }
}

} // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
