//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <future>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "yb/rocksdb/db/db_test_util.h"
#include "yb/rocksdb/db/job_context.h"
#include "yb/rocksdb/port/stack_trace.h"
#include "yb/rocksdb/sst_file_manager.h"
#include "yb/rocksdb/util/sst_file_manager_impl.h"

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/path_util.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

DECLARE_uint64(rocksdb_check_sst_file_tail_for_zeros);
DECLARE_uint64(rocksdb_max_sst_write_retries);
DECLARE_bool(TEST_simulate_fully_zeroed_file);

namespace rocksdb {

class DBTest : public DBTestBase {
 public:
  DBTest() : DBTestBase("/db_test") {}
};

TEST_F(DBTest, DontDeletePendingOutputs) {
  Options options;
  options.env = env_;
  options.create_if_missing = true;
  DestroyAndReopen(options);

  // Every time we write to a table file, call FOF/POF with full DB scan. This
  // will make sure our pending_outputs_ protection work correctly
  std::function<void()> purge_obsolete_files_function = [&]() {
    JobContext job_context(0);
    dbfull()->TEST_LockMutex();
    dbfull()->FindObsoleteFiles(&job_context, true /*force*/);
    dbfull()->TEST_UnlockMutex();
    dbfull()->PurgeObsoleteFiles(job_context);
    job_context.Clean();
  };

  env_->table_write_callback_ = &purge_obsolete_files_function;

  for (int i = 0; i < 2; ++i) {
    ASSERT_OK(Put("a", "begin"));
    ASSERT_OK(Put("z", "end"));
    ASSERT_OK(Flush());
  }

  // If pending output guard does not work correctly, PurgeObsoleteFiles() will
  // delete the file that Compaction is trying to create, causing this: error
  // db/db_test.cc:975: IO error:
  // /tmp/rocksdbtest-1552237650/db_test/000009.sst: No such file or directory
  Compact("a", "b");
}

TEST_F(DBTest, DontDeletePendingOutputsDuringConcurrentFlushes) {
  const auto kConcurrentFlushes = 4;
  const auto kFlushIterationsPerThread = 300;

  Options options;
  options.env = env_;
  options.create_if_missing = true;
  options.max_background_flushes = kConcurrentFlushes;

  DestroyAndReopen(options);

  std::atomic<bool> stop_requested(false);

  std::vector<std::thread> flush_threads;

  auto purge_thread = std::thread([this, &stop_requested] {
    LOG(INFO) << "Started purge thread";
    while (!stop_requested) {
      JobContext job_context(0);
      dbfull()->TEST_LockMutex();
      dbfull()->FindObsoleteFiles(&job_context, true /*force*/);
      dbfull()->TEST_UnlockMutex();
      if (job_context.HaveSomethingToDelete()) {
        dbfull()->PurgeObsoleteFiles(job_context);
      }
      job_context.Clean();
    }
  });

  for (int i = 0; i < kConcurrentFlushes; ++i) {
    flush_threads.emplace_back([this] {
      for (int iter = 0; iter < kFlushIterationsPerThread; ++iter) {
        ASSERT_OK(Put("a", "begin"));
        ASSERT_OK(Put("z", "end"));
        ASSERT_OK(Flush());
      }
    });
  }

  for (auto& thread : flush_threads) {
    thread.join();
  }

  stop_requested = true;
  purge_thread.join();
}

TEST_F(DBTest, DontDeleteMovedFile) {
  // This test triggers move compaction and verifies that the file is not
  // deleted when it's part of move compaction
  Options options = CurrentOptions();
  options.env = env_;
  options.create_if_missing = true;
  options.max_bytes_for_level_base = 1024 * 1024;  // 1 MB
  options.level0_file_num_compaction_trigger =
      2;  // trigger compaction when we have 2 files
  DestroyAndReopen(options);

  Random rnd(301);
  // Create two 1MB sst files
  for (int i = 0; i < 2; ++i) {
    // Create 1MB sst file
    for (int j = 0; j < 100; ++j) {
      ASSERT_OK(Put(Key(i * 50 + j), RandomString(&rnd, 10 * 1024)));
    }
    ASSERT_OK(Flush());
  }
  // this should execute both L0->L1 and L1->(move)->L2 compactions
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ("0,0,1", FilesPerLevel(0));

  // If the moved file is actually deleted (the move-safeguard in
  // ~Version::Version() is not there), we get this failure:
  // Corruption: Can't access /000009.sst
  Reopen(options);
}

// This reproduces a bug where we don't delete a file because when it was
// supposed to be deleted, it was blocked by pending_outputs
// Consider:
// 1. current file_number is 13
// 2. compaction (1) starts, blocks deletion of all files starting with 13
// (pending outputs)
// 3. file 13 is created by compaction (2)
// 4. file 13 is consumed by compaction (3) and file 15 was created. Since file
// 13 has no references, it is put into VersionSet::obsolete_files_
// 5. FindObsoleteFiles() gets file 13 from VersionSet::obsolete_files_. File 13
// is deleted from obsolete_files_ set.
// 6. PurgeObsoleteFiles() tries to delete file 13, but this file is blocked by
// pending outputs since compaction (1) is still running. It is not deleted and
// it is not present in obsolete_files_ anymore. Therefore, we never delete it.
TEST_F(DBTest, DeleteObsoleteFilesPendingOutputs) {
  Options options = CurrentOptions();
  options.env = env_;
  options.write_buffer_size = 2 * 1024 * 1024;     // 2 MB
  options.max_bytes_for_level_base = 1024 * 1024;  // 1 MB
  options.level0_file_num_compaction_trigger =
      2;  // trigger compaction when we have 2 files
  options.max_background_flushes = 2;
  options.max_background_compactions = 2;

  OnFileDeletionListener* listener = new OnFileDeletionListener();
  options.listeners.emplace_back(listener);

  Reopen(options);

  Random rnd(301);
  // Create two 1MB sst files
  for (int i = 0; i < 2; ++i) {
    // Create 1MB sst file
    for (int j = 0; j < 100; ++j) {
      ASSERT_OK(Put(Key(i * 50 + j), RandomString(&rnd, 10 * 1024)));
    }
    ASSERT_OK(Flush());
  }
  // this should execute both L0->L1 and L1->(move)->L2 compactions
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ("0,0,1", FilesPerLevel(0));

  test::SleepingBackgroundTask blocking_thread;
  port::Mutex mutex_;
  bool already_blocked(false);

  // block the flush
  std::function<void()> block_first_time = [&]() {
    bool blocking = false;
    {
      MutexLock l(&mutex_);
      if (!already_blocked) {
        blocking = true;
        already_blocked = true;
      }
    }
    if (blocking) {
      blocking_thread.DoSleep();
    }
  };
  env_->table_write_callback_ = &block_first_time;
  // Create 1MB sst file
  for (int j = 0; j < 256; ++j) {
    ASSERT_OK(Put(Key(j), RandomString(&rnd, 10 * 1024)));
  }
  // this should trigger a flush, which is blocked with block_first_time
  // pending_file is protecting all the files created after

  ASSERT_OK(dbfull()->TEST_CompactRange(2, nullptr, nullptr));

  ASSERT_EQ("0,0,0,1", FilesPerLevel(0));
  std::vector<LiveFileMetaData> metadata;
  db_->GetLiveFilesMetaData(&metadata);
  ASSERT_EQ(metadata.size(), 1U);
  auto file_on_L2 = metadata[0].Name();
  listener->SetExpectedFileName(dbname_ + file_on_L2);

  ASSERT_OK(dbfull()->TEST_CompactRange(3, nullptr, nullptr, nullptr,
                                        true /* disallow trivial move */));
  ASSERT_EQ("0,0,0,0,1", FilesPerLevel(0));

  // finish the flush!
  blocking_thread.WakeUp();
  blocking_thread.WaitUntilDone();
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  ASSERT_EQ("1,0,0,0,1", FilesPerLevel(0));

  metadata.clear();
  db_->GetLiveFilesMetaData(&metadata);
  ASSERT_EQ(metadata.size(), 2U);

  // This file should have been deleted during last compaction
  ASSERT_TRUE(env_->FileExists(dbname_ + file_on_L2).IsNotFound());
  listener->VerifyMatchedCount(1);
}

TEST_F(DBTest, DBWithSstFileManager) {
  std::shared_ptr<SstFileManager> sst_file_manager(ASSERT_RESULT(NewSstFileManager(env_)));
  auto sfm = static_cast<SstFileManagerImpl*>(sst_file_manager.get());

  int files_added = 0;
  int files_deleted = 0;
  int files_moved = 0;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "SstFileManagerImpl::OnAddFile", [&](void* arg) { files_added++; });
  yb::SyncPoint::GetInstance()->SetCallBack(
      "SstFileManagerImpl::OnDeleteFile", [&](void* arg) { files_deleted++; });
  yb::SyncPoint::GetInstance()->SetCallBack(
      "SstFileManagerImpl::OnMoveFile", [&](void* arg) { files_moved++; });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  Options options = CurrentOptions();
  options.sst_file_manager = sst_file_manager;
  DestroyAndReopen(options);

  Random rnd(301);
  for (int i = 0; i < 25; i++) {
    GenerateNewRandomFile(&rnd);
    ASSERT_OK(Flush());
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
    // Verify that we are tracking all sst files in dbname_
    ASSERT_EQ(sfm->GetTrackedFiles(), GetAllSSTFiles());
  }
  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));

  auto files_in_db = GetAllSSTFiles();
  // Verify that we are tracking all sst files in dbname_
  ASSERT_EQ(sfm->GetTrackedFiles(), files_in_db);
  // Verify the total files size
  uint64_t total_files_size = 0;
  for (auto& file_to_size : files_in_db) {
    total_files_size += file_to_size.second;
  }
  ASSERT_EQ(sfm->GetTotalSize(), total_files_size);
  // We flushed at least 25 files
  ASSERT_GE(files_added, 25);
  // Compaction must have deleted some files
  ASSERT_GT(files_deleted, 0);
  // No files were moved
  ASSERT_EQ(files_moved, 0);

  Close();
  Reopen(options);
  ASSERT_EQ(sfm->GetTrackedFiles(), files_in_db);
  ASSERT_EQ(sfm->GetTotalSize(), total_files_size);

  // Verify that we track all the files again after the DB is closed and opened
  Close();
  sst_file_manager.reset(ASSERT_RESULT(NewSstFileManager(env_)));
  options.sst_file_manager = sst_file_manager;
  sfm = static_cast<SstFileManagerImpl*>(sst_file_manager.get());

  Reopen(options);
  ASSERT_EQ(sfm->GetTrackedFiles(), files_in_db);
  ASSERT_EQ(sfm->GetTotalSize(), total_files_size);

  yb::SyncPoint::GetInstance()->DisableProcessing();
}

TEST_F(DBTest, RateLimitedDelete) {
  yb::SyncPoint::GetInstance()->LoadDependency({
      {"DBTest::RateLimitedDelete:1", "DeleteScheduler::BackgroundEmptyTrash"},
  });

  std::vector<uint64_t> penalties;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DeleteScheduler::BackgroundEmptyTrash:Wait",
      [&](void* arg) { penalties.push_back(*(static_cast<int*>(arg))); });
  yb::SyncPoint::GetInstance()->DisableProcessing();

  Options options = CurrentOptions();
  options.disable_auto_compactions = true;
  options.env = env_;

  std::string trash_dir = test::TmpDir(env_) + "/trash";
  int64_t rate_bytes_per_sec = 1024 * 10;  // 10 Kbs / Sec
  options.sst_file_manager.reset(ASSERT_RESULT(NewSstFileManager(
      env_, nullptr, trash_dir, rate_bytes_per_sec, false)));
  auto sfm = static_cast<SstFileManagerImpl*>(options.sst_file_manager.get());

  Destroy(last_options_);
  yb::SyncPoint::GetInstance()->EnableProcessing();
  ASSERT_OK(TryReopen(options));
  // Create 4 files in L0
  for (char v = 'a'; v <= 'd'; v++) {
    ASSERT_OK(Put("Key2", DummyString(1024, v)));
    ASSERT_OK(Put("Key3", DummyString(1024, v)));
    ASSERT_OK(Put("Key4", DummyString(1024, v)));
    ASSERT_OK(Put("Key1", DummyString(1024, v)));
    ASSERT_OK(Put("Key4", DummyString(1024, v)));
    ASSERT_OK(Flush());
  }
  // We created 4 sst files in L0
  ASSERT_EQ("4", FilesPerLevel(0));

  std::vector<LiveFileMetaData> metadata;
  db_->GetLiveFilesMetaData(&metadata);

  // Compaction will move the 4 files in L0 to trash and create 1 L1 file
  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ("0,1", FilesPerLevel(0));

  uint64_t delete_start_time = env_->NowMicros();
  // Hold BackgroundEmptyTrash
  TEST_SYNC_POINT("DBTest::RateLimitedDelete:1");
  sfm->WaitForEmptyTrash();
  uint64_t time_spent_deleting = env_->NowMicros() - delete_start_time;

  uint64_t total_files_size = 0;
  uint64_t expected_penlty = 0;
  ASSERT_EQ(penalties.size(), metadata.size() * 2);
  for (size_t i = 0; i < metadata.size(); i++) {
    total_files_size += metadata[i].total_size;
    expected_penlty = ((total_files_size * 1000000) / rate_bytes_per_sec);
    ASSERT_EQ(expected_penlty, penalties[i * 2 + 1]);
  }
  ASSERT_GT(time_spent_deleting, expected_penlty * 0.9);

  yb::SyncPoint::GetInstance()->DisableProcessing();
}

// Create a DB with 2 db_paths, and generate multiple files in the 2
// db_paths using CompactRangeOptions, make sure that files that were
// deleted from first db_path were deleted using DeleteScheduler and
// files in the second path were not.
TEST_F(DBTest, DeleteSchedulerMultipleDBPaths) {
  int bg_delete_file = 0;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DeleteScheduler::DeleteTrashFile:DeleteFile",
      [&](void* arg) { bg_delete_file++; });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  Options options = CurrentOptions();
  options.disable_auto_compactions = true;
  options.db_paths.emplace_back(dbname_, 1024 * 100);
  options.db_paths.emplace_back(dbname_ + "_2", 1024 * 100);
  options.env = env_;

  std::string trash_dir = test::TmpDir(env_) + "/trash";
  int64_t rate_bytes_per_sec = 1024 * 1024;  // 1 Mb / Sec
  options.sst_file_manager.reset(ASSERT_RESULT(NewSstFileManager(
      env_, nullptr, trash_dir, rate_bytes_per_sec, false)));
  auto sfm = static_cast<SstFileManagerImpl*>(options.sst_file_manager.get());

  DestroyAndReopen(options);

  // Create 4 files in L0
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(Put("Key" + ToString(i), DummyString(1024, 'A')));
    ASSERT_OK(Flush());
  }
  // We created 4 sst files in L0
  ASSERT_EQ("4", FilesPerLevel(0));
  // Compaction will delete files from L0 in first db path and generate a new
  // file in L1 in second db path
  CompactRangeOptions compact_options;
  compact_options.target_path_id = 1;
  Slice begin("Key0");
  Slice end("Key3");
  ASSERT_OK(db_->CompactRange(compact_options, &begin, &end));
  ASSERT_EQ("0,1", FilesPerLevel(0));

  // Create 4 files in L0
  for (int i = 4; i < 8; i++) {
    ASSERT_OK(Put("Key" + ToString(i), DummyString(1024, 'B')));
    ASSERT_OK(Flush());
  }
  ASSERT_EQ("4,1", FilesPerLevel(0));

  // Compaction will delete files from L0 in first db path and generate a new
  // file in L1 in second db path
  begin = "Key4";
  end  = "Key7";
  ASSERT_OK(db_->CompactRange(compact_options, &begin, &end));
  ASSERT_EQ("0,2", FilesPerLevel(0));

  sfm->WaitForEmptyTrash();
  ASSERT_EQ(bg_delete_file, 16);

  compact_options.bottommost_level_compaction =
      BottommostLevelCompaction::kForce;
  ASSERT_OK(db_->CompactRange(compact_options, nullptr, nullptr));
  ASSERT_EQ("0,1", FilesPerLevel(0));

  sfm->WaitForEmptyTrash();
  ASSERT_EQ(bg_delete_file, 16);

  yb::SyncPoint::GetInstance()->DisableProcessing();
}

// Fixture for DB::ScheduleDBPathMove, the primitive that relocates one SST between db_paths.
// The DB is opened with two db_paths ("home" plus a second one) and a priority thread pool,
// which the API requires since it runs moves as background compaction-pool tasks.
class DBPathMoveCompactionTest : public DBTestBase {
 public:
  DBPathMoveCompactionTest() : DBTestBase("/db_path_move_compaction_test") {}

  static constexpr uint32_t kHomePathId = 0;
  static constexpr uint32_t kTargetPathId = 1;

  void SetUp() override {
    DBTestBase::SetUp();
    target_path_ = dbname_ + "_path1";

    Options options = CurrentOptions();
    options.env = env_;
    // Keeps every Flush() in its own SST so tests control exactly which files exist.
    options.disable_auto_compactions = true;
    options.db_paths.emplace_back(dbname_, std::numeric_limits<uint64_t>::max());
    options.db_paths.emplace_back(target_path_, std::numeric_limits<uint64_t>::max());
    options.priority_thread_pool_for_compactions_and_flushes = &thread_pool_;
    db_paths_ = options.db_paths;
    DestroyAndReopen(options);
  }

  void TearDown() override {
    // The DB must be gone before thread_pool_, which it holds a raw pointer to.
    Close();
    DBTestBase::TearDown();
  }

  // Schedules a move and blocks until its callback fires, returning the outcome of the move.
  // Fails the test if the move could not even be scheduled.
  Status MoveFileAndWait(uint64_t file_number, uint32_t target_path_id) {
    std::promise<Status> done;
    const Status scheduled = db_->ScheduleDBPathMove(
        file_number, target_path_id, [&done](const Status& status) { done.set_value(status); });
    EXPECT_OK(scheduled);
    if (!scheduled.ok()) {
      return scheduled;
    }
    return done.get_future().get();
  }

  // Writes one key and flushes it into a brand new SST, returning that SST's file number.
  uint64_t WriteFileWithKey(const std::string& key, const std::string& value) {
    EXPECT_OK(Put(key, value));
    EXPECT_OK(Flush());
    for (const auto& file : db_->GetLiveFilesMetaData()) {
      if (known_files_.insert(file.name_id).second) {
        return file.name_id;
      }
    }
    ADD_FAILURE() << "Flush did not produce a new SST for key " << key;
    return 0;
  }

  // Directory each live SST currently sits in, keyed by file number.
  std::unordered_map<uint64_t, std::string> LiveFilePaths() {
    std::unordered_map<uint64_t, std::string> result;
    for (const auto& file : db_->GetLiveFilesMetaData()) {
      result.emplace(file.name_id, file.db_path);
    }
    return result;
  }

  // True if the target path's directory holds no files, i.e. nothing was left behind by an
  // in-progress or abandoned move.
  bool TargetDirIsEmpty() {
    std::vector<std::string> children;
    if (!env_->GetChildren(target_path_, &children).ok()) {
      return true;
    }
    for (const auto& child : children) {
      if (child != "." && child != "..") {
        return false;
      }
    }
    return true;
  }

 protected:
  yb::PriorityThreadPool thread_pool_{/* max_running_tasks = */ 2};
  std::string target_path_;
  std::vector<DbPath> db_paths_;
  std::unordered_set<uint64_t> known_files_;
};

// Unlike CompactFiles, a db path move never decodes the SST it moves, so the file that lands on
// the target path must be byte-for-byte identical to the source. That is the core property here;
// the rest checks the file is tracked correctly in its new location.
TEST_F(DBPathMoveCompactionTest, ByteForByteCopy) {
  // Hold snapshots between Puts so this single Flush's CompactionIterator cannot collapse the
  // hidden versions of "k": each snapshot pins one version as potentially still visible, forcing
  // all three into one SST. This lets the test prove that hidden versions, not just the live
  // value, survive the raw-copy move untouched.
  ASSERT_OK(Put("k", "v1"));
  const Snapshot* snap1 = db_->GetSnapshot();
  ASSERT_OK(Put("k", "v2"));
  const Snapshot* snap2 = db_->GetSnapshot();
  ASSERT_OK(Put("k", "v3"));
  ASSERT_OK(Flush());
  db_->ReleaseSnapshot(snap1);
  db_->ReleaseSnapshot(snap2);

  auto files_before = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files_before.size());
  const auto& file_before = files_before[0];
  ASSERT_EQ(dbname_, file_before.db_path);
  const int level = file_before.level;
  const uint64_t original_file_number = file_before.name_id;

  {
    TablePropertiesCollection props;
    ASSERT_OK(db_->GetPropertiesOfAllTables(&props));
    ASSERT_EQ(1U, props.size());
    ASSERT_EQ(3U, props.begin()->second->num_entries);
  }

  // Snapshot the exact bytes of the source SST before the move, so we can prove the destination
  // is byte-for-byte identical to it.
  const std::string original_path = TableFileName(db_paths_, original_file_number, kHomePathId);
  std::string original_contents;
  ASSERT_OK(ReadFileToString(env_, original_path, &original_contents));
  ASSERT_FALSE(original_contents.empty());

  ASSERT_OK(MoveFileAndWait(original_file_number, kTargetPathId));

  auto files_after = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files_after.size());
  const auto& file_after = files_after[0];
  // New file number: the table cache is keyed only by file number, so a moved file always gets a
  // fresh one -- never reuses the source's number, even though the content is identical.
  ASSERT_NE(original_file_number, file_after.name_id);
  ASSERT_EQ(level, file_after.level);
  ASSERT_EQ(target_path_, file_after.db_path);

  const std::string new_path = TableFileName(db_paths_, file_after.name_id, kTargetPathId);
  std::string new_contents;
  ASSERT_OK(ReadFileToString(env_, new_path, &new_contents));
  ASSERT_EQ(original_contents, new_contents);

  // The old file is actually gone from disk, not just dropped from the live set. This only holds
  // because nothing else in this test keeps the old file number alive: no snapshot/iterator holds
  // a table-cache reference to it (both snapshots above were released before the move) and this
  // fixture has no sst_file_manager, so PurgeObsoleteFiles deletes it synchronously instead of
  // handing it to a rate-limited trash directory. PurgeObsoleteFiles runs inside
  // ExecuteDBPathMoveCompaction, strictly before MoveFileAndWait's callback fires, so this is
  // deterministic rather than racy.
  ASSERT_TRUE(env_->FileExists(original_path).IsNotFound());

  {
    // All 3 versions of "k" survived the move, exactly as they were in the source file.
    TablePropertiesCollection props;
    ASSERT_OK(db_->GetPropertiesOfAllTables(&props));
    ASSERT_EQ(1U, props.size());
    ASSERT_EQ(3U, props.begin()->second->num_entries);
  }

  ASSERT_EQ("v3", Get("k"));
}

// ScheduleDBPathMove never inspects the file, so a request for a file that is not live, or that
// is already where it was asked to go, is still queued and returns OK; the task discovers the
// situation when it picks and reports it through the callback. That keeps the contract to one
// rule: "returned OK" means "the callback will fire exactly once", with no second set of outcomes
// to handle synchronously. An out-of-range target_path_id is deliberately not covered: that is a
// caller bug, so ScheduleDBPathMove checks it with RSTATUS_DCHECK and would abort this test in a
// debug build.
TEST_F(DBPathMoveCompactionTest, NoOpOutcomesArriveThroughCallback) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  // A file number that was never live: gone as far as the task can tell.
  ASSERT_TRUE(MoveFileAndWait(file_number + 1000, kTargetPathId).IsNotFound());
  // Already where it was asked to go: distinguishable from a real failure so callers can treat it
  // as a no-op rather than retrying forever.
  ASSERT_TRUE(MoveFileAndWait(file_number, kHomePathId).IsAlreadyPresent());

  // Neither no-op left the file claimed, so a real move still works.
  ASSERT_FALSE(db_->GetLiveFilesMetaData()[0].being_compacted);
  ASSERT_OK(MoveFileAndWait(file_number, kTargetPathId));
  ASSERT_EQ(target_path_, LiveFilePaths().begin()->second);
}

#ifndef NDEBUG
// The tests below pin down the order in which a move and other work run by making the move wait
// at a DEBUG_ONLY_TEST_SYNC_POINT, which compiles to nothing in release builds.

// The call returns as soon as the move is queued; the copy itself happens later on the compaction
// pool. Blocking the task after it has picked its file but before it copies anything lets us
// observe that in-between state.
TEST_F(DBPathMoveCompactionTest, SchedulingIsAsynchronous) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"DBPathMoveCompactionTest::SchedulingIsAsynchronous:Observed",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::promise<Status> done;
  ASSERT_OK(db_->ScheduleDBPathMove(
      file_number, kTargetPathId, [&done](const Status& status) { done.set_value(status); }));

  // The claim on the source file appears only once the task has started and picked it; wait for
  // that so the second move below is guaranteed to arrive while the first one holds the file.
  // (Scheduling the two moves back to back would let them race for the pick, and whichever won
  // would be the one held at BeforeCopy.)
  ASSERT_OK(yb::WaitFor(
      [this]() -> Result<bool> { return db_->GetLiveFilesMetaData()[0].being_compacted; },
      yb::MonoDelta::FromSeconds(10), "first move to pick and claim the file"));
  // The task is running but has not copied anything yet, so the file is still on the home disk.
  ASSERT_EQ(dbname_, LiveFilePaths()[file_number]);
  // The running task holds the file, so a second move of it arriving now backs off at its own
  // pick instead of racing the first one. That outcome comes through the second move's callback:
  // ScheduleDBPathMove itself never looks at the file, so it still returns OK.
  std::promise<Status> second_done;
  ASSERT_OK(db_->ScheduleDBPathMove(
      file_number, kTargetPathId,
      [&second_done](const Status& status) { second_done.set_value(status); }));
  ASSERT_TRUE(second_done.get_future().get().IsAborted());

  TEST_SYNC_POINT("DBPathMoveCompactionTest::SchedulingIsAsynchronous:Observed");
  ASSERT_OK(done.get_future().get());

  yb::SyncPoint::GetInstance()->DisableProcessing();

  auto files_after = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files_after.size());
  ASSERT_EQ(target_path_, files_after[0].db_path);
  ASSERT_EQ("v", Get("k"));
}

// The scenario the deferred pick exists for. A move is scheduled for a file and then held before
// it picks anything, standing in for a move stuck behind higher priority work. Meanwhile a
// compaction whose output goes to the target path -- what every regular compaction does once the
// column family's target_path_id is set -- runs over the same file. Because the queued move has
// not claimed the file, the compaction is free to take it and moves it as a side effect; the move
// then wakes up, finds its file gone, and reports a no-op instead of having stood in the way.
TEST_F(DBPathMoveCompactionTest, QueuedMoveDoesNotBlockCompactionFromMovingTheFile) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"DBPathMoveCompactionTest::QueuedMoveDoesNotBlockCompaction:CompactionDone",
        "DBImpl::DBPathMoveCompactionTask::DoRun:BeforePick"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::promise<Status> done;
  ASSERT_OK(db_->ScheduleDBPathMove(
      file_number, kTargetPathId, [&done](const Status& status) { done.set_value(status); }));

  // Scheduling touched nothing on the file: it is still on the home disk and, crucially, not
  // claimed, so it is available to any compaction that comes along.
  auto files = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files.size());
  ASSERT_EQ(dbname_, files[0].db_path);
  ASSERT_FALSE(files[0].being_compacted);

  // A compaction writing to the target path takes the file. CompactionPicker refuses inputs that
  // are being_compacted, so this would have failed had scheduling claimed the file.
  ASSERT_OK(db_->CompactFiles(
      CompactionOptions(), {files[0].Name()}, /* output_level = */ 0, kTargetPathId));
  files = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files.size());
  ASSERT_NE(file_number, files[0].name_id);
  ASSERT_EQ(target_path_, files[0].db_path);

  TEST_SYNC_POINT("DBPathMoveCompactionTest::QueuedMoveDoesNotBlockCompaction:CompactionDone");
  // The move finds that its file is no longer live and says so, rather than failing loudly or
  // touching the compaction's output.
  ASSERT_TRUE(done.get_future().get().IsNotFound());
  yb::SyncPoint::GetInstance()->DisableProcessing();

  files = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files.size());
  ASSERT_EQ(target_path_, files[0].db_path);
  ASSERT_EQ("v", Get("k"));
}

// The other way the same race can go: the move starts while a compaction that already holds the
// file is still running. The move backs off with Aborted at its pick instead of waiting on the
// compaction or racing it for the MANIFEST, and the compaction goes on to place the file.
TEST_F(DBPathMoveCompactionTest, PickBacksOffWhenCompactionHoldsTheFile) {
  const uint64_t file_number = WriteFileWithKey("k", "v");
  const std::string file_name = db_->GetLiveFilesMetaData()[0].Name();

  // Order: the compaction claims the file and starts running -> the move picks and backs off ->
  // the move's outcome has been observed -> the compaction is allowed to finish.
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"CompactionJob::Run():Start", "DBImpl::DBPathMoveCompactionTask::DoRun:BeforePick"},
       {"DBPathMoveCompactionTest::PickBacksOff:MoveObserved", "CompactionJob::Run():End"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::promise<Status> done;
  ASSERT_OK(db_->ScheduleDBPathMove(
      file_number, kTargetPathId, [&done](const Status& status) { done.set_value(status); }));

  Status compaction_status;
  yb::TestThreadHolder threads;
  threads.AddThreadFunctor([this, &file_name, &compaction_status] {
    compaction_status = db_->CompactFiles(
        CompactionOptions(), {file_name}, /* output_level = */ 0, kTargetPathId);
  });

  ASSERT_TRUE(done.get_future().get().IsAborted());
  TEST_SYNC_POINT("DBPathMoveCompactionTest::PickBacksOff:MoveObserved");
  threads.JoinAll();
  yb::SyncPoint::GetInstance()->DisableProcessing();
  ASSERT_OK(compaction_status);

  auto files = db_->GetLiveFilesMetaData();
  ASSERT_EQ(1U, files.size());
  ASSERT_EQ(target_path_, files[0].db_path);
  ASSERT_FALSE(files[0].being_compacted);
  ASSERT_EQ("v", Get("k"));
}
// A move whose copy is already in flight when the DB starts shutting down gives up instead of
// running the copy to completion. ~DBImpl waits for every db path move task the way it waits for
// compactions, so without this a tablet shutdown would stall for the remainder of a multi-GB copy.
// The callback reports ShutdownInProgress, the partial destination is removed, and the source
// stays live and unclaimed, exactly as for any other copy failure.
TEST_F(DBPathMoveCompactionTest, ShutdownAbortsCopyInFlight) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  // Start the shutdown from the task itself, right after it has picked and claimed the file and
  // just before the first byte is copied. Calling StartShutdown from the test thread instead would
  // race the pick: a move still queued at that point is simply dropped by the pool, which is the
  // other shutdown path and is covered by AbortedTaskReportsErrorAndLeavesFileUntouched.
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy", [this](void*) { db_->StartShutdown(); });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  const Status move_status = MoveFileAndWait(file_number, kTargetPathId);
  yb::SyncPoint::GetInstance()->DisableProcessing();
  // The callback captures `this`, so it must not outlive the test.
  yb::SyncPoint::GetInstance()->ClearAllCallBacks();

  ASSERT_TRUE(move_status.IsShutdownInProgress()) << move_status;
  ASSERT_TRUE(TargetDirIsEmpty());
  ASSERT_EQ(dbname_, LiveFilePaths()[file_number]);
  ASSERT_FALSE(db_->GetLiveFilesMetaData()[0].being_compacted);
}

#endif  // NDEBUG

// Several moves in flight at once share the compaction pool and are applied one MANIFEST edit at
// a time, so all of them land and the data stays readable.
TEST_F(DBPathMoveCompactionTest, ConcurrentMoves) {
  constexpr int kNumFiles = 8;

  std::vector<uint64_t> file_numbers;
  for (int i = 0; i < kNumFiles; ++i) {
    file_numbers.push_back(WriteFileWithKey(Key(i), "v" + std::to_string(i)));
  }
  ASSERT_EQ(kNumFiles, db_->GetLiveFilesMetaData().size());

  yb::CountDownLatch latch(kNumFiles);
  std::mutex status_mutex;
  std::vector<Status> statuses;
  for (const uint64_t file_number : file_numbers) {
    ASSERT_OK(db_->ScheduleDBPathMove(
        file_number, kTargetPathId, [&latch, &status_mutex, &statuses](const Status& status) {
          {
            std::lock_guard lock(status_mutex);
            statuses.push_back(status);
          }
          latch.CountDown();
        }));
  }
  latch.Wait();

  for (const auto& status : statuses) {
    ASSERT_OK(status);
  }

  auto files_after = db_->GetLiveFilesMetaData();
  ASSERT_EQ(kNumFiles, files_after.size());
  for (const auto& file : files_after) {
    ASSERT_EQ(target_path_, file.db_path);
  }
  for (int i = 0; i < kNumFiles; ++i) {
    ASSERT_EQ("v" + std::to_string(i), Get(Key(i)));
  }
}

// If the copy fails partway through, the partially-written destination must not survive and the
// source file's claim must not be left set, or the file would be stuck forever.
TEST_F(DBPathMoveCompactionTest, CopyFailureCleansUpDestinationAndReleasesClaim) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  // SpecialEnv wraps every ".sst" WritableFile in a wrapper whose Append() fails while no_space_
  // is set (see SpecialEnv::NewWritableFile in db_test_util.h). NewWritableFile itself still
  // succeeds, so this reproduces exactly the failure CopyFile can see mid-write: a destination
  // file that got opened, but never finished, at dst_base_path.
  env_->no_space_.store(true, std::memory_order_release);
  const Status move_status = MoveFileAndWait(file_number, kTargetPathId);
  env_->no_space_.store(false, std::memory_order_release);

  ASSERT_NOK(move_status);
  // DeleteAbandonedTableFile cleaned up the partial destination.
  ASSERT_TRUE(TargetDirIsEmpty());
  // The source is untouched and its claim was released, so a real move still works afterward.
  ASSERT_EQ(dbname_, LiveFilePaths()[file_number]);
  ASSERT_OK(MoveFileAndWait(file_number, kTargetPathId));
  ASSERT_EQ(target_path_, LiveFilePaths().begin()->second);
}

// Same as above, but the copy succeeds and the failure happens later, in the MANIFEST write
// (LogAndApply). DeleteAbandonedTableFile alone cleans up the destination: the obsolete-file scan
// cannot see it while ExecuteDBPathMoveCompaction still holds its file number.
TEST_F(DBPathMoveCompactionTest, ManifestFailureCleansUpDestinationAndReleasesClaim) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  env_->manifest_write_error_.store(true, std::memory_order_release);
  const Status move_status = MoveFileAndWait(file_number, kTargetPathId);
  env_->manifest_write_error_.store(false, std::memory_order_release);

  ASSERT_NOK(move_status);
  ASSERT_TRUE(TargetDirIsEmpty());
  ASSERT_EQ(dbname_, LiveFilePaths()[file_number]);
  ASSERT_OK(MoveFileAndWait(file_number, kTargetPathId));
  ASSERT_EQ(target_path_, LiveFilePaths().begin()->second);
}

// If the task cannot even be handed to the thread pool (e.g. the pool cannot start a worker),
// SubmitCompactionOrFlushTask reports it through AbortedUnlocked instead of DoRun. A task that
// never ran has never picked its file, so there is nothing on the file to undo; the only
// obligation left is the "callback fires exactly once" contract.
TEST_F(DBPathMoveCompactionTest, AbortedTaskReportsErrorAndLeavesFileUntouched) {
  const uint64_t file_number = WriteFileWithKey("k", "v");

  // thread_pool_ has not run anything yet in this test, so this forces the very first worker
  // creation attempt to fail, which makes Submit() return an error synchronously and
  // ScheduleDBPathMove's SubmitCompactionOrFlushTask call AbortedUnlocked inline.
  thread_pool_.TEST_SetThreadCreationFailureProbability(1.0);

  bool callback_invoked = false;
  Status callback_status;
  ASSERT_OK(db_->ScheduleDBPathMove(
      file_number, kTargetPathId, [&callback_invoked, &callback_status](const Status& status) {
        callback_invoked = true;
        callback_status = status;
      }));
  ASSERT_TRUE(callback_invoked);
  ASSERT_NOK(callback_status);
  ASSERT_EQ(dbname_, LiveFilePaths()[file_number]);
  ASSERT_FALSE(db_->GetLiveFilesMetaData()[0].being_compacted);

  // A real move still works once the pool can create workers again.
  thread_pool_.TEST_SetThreadCreationFailureProbability(0.0);
  ASSERT_OK(MoveFileAndWait(file_number, kTargetPathId));
  ASSERT_EQ(target_path_, LiveFilePaths().begin()->second);
}

TEST_F(DBTest, DestroyDBWithRateLimitedDelete) {
  int bg_delete_file = 0;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DeleteScheduler::DeleteTrashFile:DeleteFile",
      [&](void* arg) { bg_delete_file++; });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  Options options = CurrentOptions();
  options.disable_auto_compactions = true;
  options.env = env_;
  DestroyAndReopen(options);

  // Create 4 files in L0
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(Put("Key" + ToString(i), DummyString(1024, 'A')));
    ASSERT_OK(Flush());
  }
  // We created 4 sst files in L0
  ASSERT_EQ("4", FilesPerLevel(0));

  // Close DB and destroy it using DeleteScheduler
  Close();
  std::string trash_dir = test::TmpDir(env_) + "/trash";
  int64_t rate_bytes_per_sec = 1024 * 1024;  // 1 Mb / Sec
  options.sst_file_manager.reset(ASSERT_RESULT(NewSstFileManager(
      env_, nullptr, trash_dir, rate_bytes_per_sec, false)));
  ASSERT_OK(DestroyDB(dbname_, options));

  auto sfm = static_cast<SstFileManagerImpl*>(options.sst_file_manager.get());
  sfm->WaitForEmptyTrash();
  // We have deleted the 4*2 sst files in the delete_scheduler
  ASSERT_EQ(bg_delete_file, 8);
}

TEST_F(DBTest, DBWithMaxSpaceAllowed) {
  std::shared_ptr<SstFileManager> sst_file_manager(ASSERT_RESULT(NewSstFileManager(env_)));
  auto sfm = static_cast<SstFileManagerImpl*>(sst_file_manager.get());

  Options options = CurrentOptions();
  options.sst_file_manager = sst_file_manager;
  options.disable_auto_compactions = true;
  DestroyAndReopen(options);

  Random rnd(301);

  // Generate a file containing 100 keys.
  for (int i = 0; i < 100; i++) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 50)));
  }
  ASSERT_OK(Flush());

  uint64_t first_file_size = 0;
  auto files_in_db = GetAllSSTFiles(&first_file_size);
  ASSERT_EQ(sfm->GetTotalSize(), first_file_size);

  // Set the maximum allowed space usage to the current total size
  sfm->SetMaxAllowedSpaceUsage(first_file_size + 1);

  ASSERT_OK(Put("key1", "val1"));
  // This flush will cause bg_error_ and will fail
  ASSERT_NOK(Flush());
}

TEST_F(DBTest, DBWithMaxSpaceAllowedRandomized) {
  // This test will set a maximum allowed space for the DB, then it will
  // keep filling the DB until the limit is reached and bg_error_ is set.
  // When bg_error_ is set we will verify that the DB size is greater
  // than the limit.

  std::vector<int> max_space_limits_mbs = {1, 2, 4, 8, 10};

  bool bg_error_set = false;
  uint64_t total_sst_files_size = 0;

  int reached_max_space_on_flush = 0;
  int reached_max_space_on_compaction = 0;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::FlushMemTableToOutputFile:MaxAllowedSpaceReached",
      [&](void* arg) {
        bg_error_set = true;
        GetAllSSTFiles(&total_sst_files_size);
        reached_max_space_on_flush++;
      });

  yb::SyncPoint::GetInstance()->SetCallBack(
      "CompactionJob::FinishCompactionOutputFile:MaxAllowedSpaceReached",
      [&](void* arg) {
        bg_error_set = true;
        GetAllSSTFiles(&total_sst_files_size);
        reached_max_space_on_compaction++;
      });

  for (auto limit_mb : max_space_limits_mbs) {
    bg_error_set = false;
    total_sst_files_size = 0;
    yb::SyncPoint::GetInstance()->ClearTrace();
    yb::SyncPoint::GetInstance()->EnableProcessing();
    std::shared_ptr<SstFileManager> sst_file_manager(ASSERT_RESULT(NewSstFileManager(env_)));
    auto sfm = static_cast<SstFileManagerImpl*>(sst_file_manager.get());

    Options options = CurrentOptions();
    options.sst_file_manager = sst_file_manager;
    options.write_buffer_size = 1024 * 512;  // 512 Kb
    DestroyAndReopen(options);
    Random rnd(301);

    sfm->SetMaxAllowedSpaceUsage(limit_mb * 1024 * 1024);

    int keys_written = 0;
    uint64_t estimated_db_size = 0;
    while (true) {
      auto s = Put(RandomString(&rnd, 10), RandomString(&rnd, 50));
      if (!s.ok()) {
        break;
      }
      keys_written++;
      // Check the estimated db size vs the db limit just to make sure we
      // dont run into an infinite loop
      estimated_db_size = keys_written * 60;  // ~60 bytes per key
      ASSERT_LT(estimated_db_size, limit_mb * 1024 * 1024 * 2);
    }
    ASSERT_TRUE(bg_error_set);
    ASSERT_GE(total_sst_files_size, limit_mb * 1024 * 1024);
    yb::SyncPoint::GetInstance()->DisableProcessing();
  }

  ASSERT_GT(reached_max_space_on_flush, 0);
  ASSERT_GT(reached_max_space_on_compaction, 0);
}

TEST_F(DBTest, OpenDBWithInfiniteMaxOpenFiles) {
  // Open DB with infinite max open files
  //  - First iteration use 1 thread to open files
  //  - Second iteration use 5 threads to open files
  for (int iter = 0; iter < 2; iter++) {
    Options options;
    options.create_if_missing = true;
    options.write_buffer_size = 100000;
    options.disable_auto_compactions = true;
    options.max_open_files = -1;
    if (iter == 0) {
      options.max_file_opening_threads = 1;
    } else {
      options.max_file_opening_threads = 5;
    }
    options = CurrentOptions(options);
    DestroyAndReopen(options);

    // Create 12 Files in L0 (then move then to L2)
    for (int i = 0; i < 12; i++) {
      std::string k = "L2_" + Key(i);
      ASSERT_OK(Put(k, k + std::string(1000, 'a')));
      ASSERT_OK(Flush());
    }
    CompactRangeOptions compact_options;
    compact_options.change_level = true;
    compact_options.target_level = 2;
    ASSERT_OK(db_->CompactRange(compact_options, nullptr, nullptr));

    // Create 12 Files in L0
    for (int i = 0; i < 12; i++) {
      std::string k = "L0_" + Key(i);
      ASSERT_OK(Put(k, k + std::string(1000, 'a')));
      ASSERT_OK(Flush());
    }
    Close();

    // Reopening the DB will load all exisitng files
    Reopen(options);
    ASSERT_EQ("12,0,12", FilesPerLevel(0));
    std::vector<std::vector<FileMetaData>> files;
    dbfull()->TEST_GetFilesMetaData(db_->DefaultColumnFamily(), &files);

    for (const auto& level : files) {
      for (const auto& file : level) {
        ASSERT_TRUE(file.table_reader_handle != nullptr);
      }
    }

    for (int i = 0; i < 12; i++) {
      ASSERT_EQ(Get("L0_" + Key(i)), "L0_" + Key(i) + std::string(1000, 'a'));
      ASSERT_EQ(Get("L2_" + Key(i)), "L2_" + Key(i) + std::string(1000, 'a'));
    }
  }
}

TEST_F(DBTest, GetTotalSstFilesSize) {
  for (const auto is_compressed : {false, true}) {
    Options options = CurrentOptions();
    options.disable_auto_compactions = true;
    options.compression = is_compressed ? kSnappyCompression : kNoCompression;
    auto stats = rocksdb::CreateDBStatisticsForTests();
    options.statistics = stats;
    DestroyAndReopen(options);
    // Generate 5 files in L0
    for (int i = 0; i < 5; i++) {
      for (int j = 0; j < 10; j++) {
        std::string val = "val_file_" + ToString(i);
        ASSERT_OK(Put(Key(j), val));
      }
      ASSERT_OK(Flush());
    }
    ASSERT_EQ("5", FilesPerLevel(0));

    std::vector<LiveFileMetaData> live_files_meta;
    dbfull()->GetLiveFilesMetaData(&live_files_meta);
    ASSERT_EQ(live_files_meta.size(), 5);
    uint64_t single_file_size = live_files_meta[0].total_size;
    uint64_t single_file_uncompressed_size = live_files_meta[0].uncompressed_size;

    uint64_t live_sst_files_size = 0;
    uint64_t total_sst_files_size = 0;
    for (const auto& file_meta : live_files_meta) {
      live_sst_files_size += file_meta.total_size;
    }

    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 5
    // Total SST files = 5
    // SST metrics will only check current version (live) stats
    // so CURRENT_VERSION_NUM_SST_FILES will match live SST files.
    if (!is_compressed) {
      // Compressed size could be different for each file, so we only test this without compression.
      ASSERT_EQ(live_sst_files_size, 5 * single_file_size);
      ASSERT_EQ(total_sst_files_size, 5 * single_file_size);
      ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 5 * single_file_size);
    }
    ASSERT_EQ(
        stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE),
        5 * single_file_uncompressed_size);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 5);

    // hold current version
    std::unique_ptr<Iterator> iter1(dbfull()->NewIterator(ReadOptions()));

    // Compact 5 files into 1 file in L0
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    ASSERT_EQ("0,1", FilesPerLevel(0));

    live_files_meta.clear();
    dbfull()->GetLiveFilesMetaData(&live_files_meta);
    ASSERT_EQ(live_files_meta.size(), 1);

    live_sst_files_size = 0;
    total_sst_files_size = 0;
    for (const auto& file_meta : live_files_meta) {
      live_sst_files_size += file_meta.total_size;
    }
    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 1 (compacted file)
    // Total SST files = 6 (5 original files + compacted file)
    if (!is_compressed) {
      ASSERT_EQ(live_sst_files_size, 1 * single_file_size);
      ASSERT_EQ(total_sst_files_size, 6 * single_file_size);
      ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 1 * single_file_size);
    }
    ASSERT_EQ(
        stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE),
        1 * single_file_uncompressed_size);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 1);

    // hold current version
    std::unique_ptr<Iterator> iter2(dbfull()->NewIterator(ReadOptions()));

    // Delete all keys and compact, this will delete all live files
    for (int i = 0; i < 10; i++) {
      ASSERT_OK(Delete(Key(i)));
    }
    ASSERT_OK(Flush());
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    ASSERT_EQ("", FilesPerLevel(0));

    live_files_meta.clear();
    dbfull()->GetLiveFilesMetaData(&live_files_meta);
    ASSERT_EQ(live_files_meta.size(), 0);

    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 0
    // Total SST files = 6 (5 original files + compacted file)
    if (!is_compressed) {
      ASSERT_EQ(total_sst_files_size, 6 * single_file_size);
    }
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 0);

    iter1.reset();
    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 0
    // Total SST files = 1 (compacted file)
    if (!is_compressed) {
      ASSERT_EQ(total_sst_files_size, 1 * single_file_size);
    }
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 0);

    iter2.reset();
    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 0
    // Total SST files = 0
    ASSERT_EQ(total_sst_files_size, 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 0);
  }
}

TEST_F(DBTest, GetTotalSstFilesSizeVersionsFilesShared) {
  for (const auto is_compressed : {false, true}) {
    Options options = CurrentOptions();
    options.disable_auto_compactions = true;
    options.compression = is_compressed ? kSnappyCompression : kNoCompression;
    auto stats = rocksdb::CreateDBStatisticsForTests();
    options.statistics = stats;
    DestroyAndReopen(options);
    // Generate 5 files in L0
    for (int i = 0; i < 5; i++) {
      ASSERT_OK(Put(Key(i), "val"));
      ASSERT_OK(Flush());
    }
    ASSERT_EQ("5", FilesPerLevel(0));

    std::vector<LiveFileMetaData> live_files_meta;
    dbfull()->GetLiveFilesMetaData(&live_files_meta);
    ASSERT_EQ(live_files_meta.size(), 5);
    uint64_t single_file_size = live_files_meta[0].total_size;
    uint64_t single_file_uncompressed_size = live_files_meta[0].uncompressed_size;

    uint64_t live_sst_files_size = 0;
    uint64_t total_sst_files_size = 0;
    for (const auto& file_meta : live_files_meta) {
      live_sst_files_size += file_meta.total_size;
    }

    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));

    // Live SST files = 5
    // Total SST files = 5
    // SST metrics will only check current version (live) stats
    // so CURRENT_VERSION_NUM_SST_FILES will match live SST files.
    if (!is_compressed) {
      // Compressed size could be different for each file, so we only test this without compression.
      ASSERT_EQ(live_sst_files_size, 5 * single_file_size);
      ASSERT_EQ(total_sst_files_size, 5 * single_file_size);
      ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 5 * single_file_size);
    }
    ASSERT_EQ(
        stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE),
        5 * single_file_uncompressed_size);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 5);

    // hold current version
    std::unique_ptr<Iterator> iter1(dbfull()->NewIterator(ReadOptions()));

    // Compaction will do trivial move from L0 to L1
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    ASSERT_EQ("0,5", FilesPerLevel(0));

    live_files_meta.clear();
    dbfull()->GetLiveFilesMetaData(&live_files_meta);
    ASSERT_EQ(live_files_meta.size(), 5);

    live_sst_files_size = 0;
    total_sst_files_size = 0;
    for (const auto& file_meta : live_files_meta) {
      live_sst_files_size += file_meta.total_size;
    }
    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 5
    // Total SST files = 5 (used in 2 version)
    if (!is_compressed) {
      ASSERT_EQ(live_sst_files_size, 5 * single_file_size);
      ASSERT_EQ(total_sst_files_size, 5 * single_file_size);
      ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 5 * single_file_size);
    }
    ASSERT_EQ(
        stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE),
        5 * single_file_uncompressed_size);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 5);

    // hold current version
    std::unique_ptr<Iterator> iter2(dbfull()->NewIterator(ReadOptions()));

    // Delete all keys and compact, this will delete all live files
    for (int i = 0; i < 5; i++) {
      ASSERT_OK(Delete(Key(i)));
    }
    ASSERT_OK(Flush());
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    ASSERT_EQ("", FilesPerLevel(0));

    live_files_meta.clear();
    dbfull()->GetLiveFilesMetaData(&live_files_meta);
    ASSERT_EQ(live_files_meta.size(), 0);

    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 0
    // Total SST files = 5 (used in 2 version)
    if (!is_compressed) {
      ASSERT_EQ(total_sst_files_size, 5 * single_file_size);
    }
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 0);

    iter1.reset();
    iter2.reset();

    ASSERT_TRUE(dbfull()->GetIntProperty(
        "rocksdb.total-sst-files-size",
        &total_sst_files_size));
    // Live SST files = 0
    // Total SST files = 0
    ASSERT_EQ(total_sst_files_size, 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE), 0);
    ASSERT_EQ(stats->getTickerCount(CURRENT_VERSION_NUM_SST_FILES), 0);
  }
}

class SstTailZerosCheckTest : public DBTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_check_sst_file_tail_for_zeros) =
        kSstFileTailSizeToCheck;

    auto& sync_point = *yb::SyncPoint::GetInstance();
    sync_point.SetCallBack("CheckFileTailForZeros:Start", [this](void* arg) {
      std::vector<std::string> db_files;
      ASSERT_OK(env_->GetChildren(dbname_, &db_files));
      for (const auto& db_file : db_files) {
        uint64_t file_number;
        FileType file_type;
        if (!ParseFileName(db_file, &file_number, &file_type)) {
          continue;
        }
        if (file_type != corrupt_file_type) {
          continue;
        }
        const auto file_path = yb::JoinPathSegments(dbname_, db_file);
        if (corrupt_files.contains(file_path)) {
          LOG(INFO) << "Do not corrupt " << file_path << " again";
          continue;
        }
        if (bytes_to_corrupt > 0 && corrupt_files_limit != 0) {
          if (corrupt_files_limit > 0) {
            --corrupt_files_limit;
          }
          ASSERT_OK(CorruptFile(
              file_path, /* offset = */ -bytes_to_corrupt, bytes_to_corrupt,
              yb::CorruptionType::kZero));
        }
        // Still proceed with no-op corruption when bytes_to_corrupt == 0 and insert file path
        // into corrupt_files, so we don't try to corrupt it again for test purposes.
        corrupt_files.insert(file_path);
      }
    });
    sync_point.EnableProcessing();
  }

  Options GetOptions() {
    Options options = CurrentOptions();
    options.num_levels = 2;
    options.disable_auto_compactions = true;
    options.compression = kSnappyCompression;
    options.compaction_style = kCompactionStyleUniversal;
    options.compaction_options_universal.allow_trivial_move = false;

    auto stats = rocksdb::CreateDBStatisticsForTests();
    options.statistics = stats;
    options.info_log_level = InfoLogLevel::INFO_LEVEL;
    options.info_log = std::make_shared<yb::YBRocksDBLogger>(options.log_prefix);
    return options;
  }

  Status GenAndFlushFiles(int num_files) {
    constexpr auto kNumKeysPerFile = 100;

    for (int i = 0; i < num_files; i++) {
      for (int j = 0; j < kNumKeysPerFile; j++) {
        auto val = RandomString(&rnd, 100);
        CHECK_OK(Put(Key(num_keys_written++), val));
      }
      RETURN_NOT_OK(Flush());
    }
    return Status::OK();
  }

  void DestroyAndReopen(const Options& options) {
    num_keys_written = 0;

    corrupt_file_type = FileType::kTableSBlockFile;
    bytes_to_corrupt = 0;

    corrupt_files.clear();
    corrupt_files_limit = -1;
    return DBHolder::DestroyAndReopen(options);
  }

  Result<int> CountKeys() {
    int num_keys = 0;
    std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
    for (iter->SeekToFirst(); VERIFY_RESULT(iter->CheckedValid()); iter->Next()) {
      num_keys++;
    }
    return num_keys;
  }

  static constexpr auto kSstFileTailSizeToCheck = 1536;

  Random rnd{301};
  int num_keys_written = 0;

  uint64_t bytes_to_corrupt = 0;
  FileType corrupt_file_type = FileType::kTableSBlockFile;

  std::unordered_set<std::string> corrupt_files;
  int corrupt_files_limit = -1; // -1 - no limit.
};

TEST_F_EX(DBTest, SstTailZerosCheckFlush, SstTailZerosCheckTest) {
  auto options = GetOptions();
  DestroyAndReopen(options);

  // Should be able to detect corruption during Flush.
  bytes_to_corrupt = kSstFileTailSizeToCheck;
  ASSERT_NOK(GenAndFlushFiles(/* num_files = */ 1));

  DestroyAndReopen(options);
  // Should not be able to detect corruption during Flush.
  bytes_to_corrupt = kSstFileTailSizeToCheck - 1;
  ASSERT_OK(GenAndFlushFiles(/* num_files = */ 2));
  ASSERT_EQ("2", FilesPerLevel(0));
  // Compaction should fail.
  ASSERT_NOK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
}

TEST_F_EX(DBTest, SstTailZerosCheckFlushRetries, SstTailZerosCheckTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_sst_write_retries) = 1;

  auto options = GetOptions();

  for (auto corrupt_file_type_value : {kTableSBlockFile, kTableFile}) {
    DestroyAndReopen(options);

    bytes_to_corrupt = kSstFileTailSizeToCheck;
    corrupt_file_type = corrupt_file_type_value;

    // Should be able to flush both after retry.
    ASSERT_OK(GenAndFlushFiles(/* num_files = */ 2));
    ASSERT_EQ("2", FilesPerLevel(0));
    ASSERT_EQ(2, corrupt_files.size());
    ASSERT_EQ(num_keys_written, ASSERT_RESULT(CountKeys()));
  }
}

TEST_F_EX(DBTest, SstTailZerosCheckCompaction, SstTailZerosCheckTest) {
  auto options = GetOptions();
  for (auto compaction_output_bytes_to_corrupt :
     {kSstFileTailSizeToCheck, kSstFileTailSizeToCheck - 1}) {
    DestroyAndReopen(options);

    // Do not corrupt flushed files.
    bytes_to_corrupt = 0;
    ASSERT_OK(GenAndFlushFiles(/* num_files = */ 2));
    ASSERT_EQ("2", FilesPerLevel(0));

    // Corrupt compaction output file.
    bytes_to_corrupt = compaction_output_bytes_to_corrupt;

    if (compaction_output_bytes_to_corrupt >= kSstFileTailSizeToCheck) {
      // Should be able to detect corruption and fail.
      ASSERT_NOK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
      ASSERT_EQ("2", FilesPerLevel(0));
    } else {
      // Should not be able to detect corruption.
      ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
      ASSERT_EQ("0,1", FilesPerLevel(0));

      bytes_to_corrupt = 0;
      ASSERT_OK(GenAndFlushFiles(/* num_files = */ 1));
      ASSERT_EQ("1,1", FilesPerLevel(0));

      // Subsequent compaction should fail.
      ASSERT_NOK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
      ASSERT_EQ("1,1", FilesPerLevel(0));
    }
  }
}

TEST_F_EX(DBTest, SstTailZerosCheckCompactionRetries, SstTailZerosCheckTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_sst_write_retries) = 1;

  auto options = GetOptions();

  for (auto corrupt_file_type_value : {kTableSBlockFile, kTableFile}) {
    DestroyAndReopen(options);

    corrupt_file_type = corrupt_file_type_value;

    // Do not corrupt flushed files.
    bytes_to_corrupt = 0;
    ASSERT_OK(GenAndFlushFiles(/* num_files = */ 2));
    ASSERT_EQ("2", FilesPerLevel(0));
    ASSERT_EQ(2, corrupt_files.size()) << yb::AsString(corrupt_files);

    // Corrupt compaction output file once.
    bytes_to_corrupt = kSstFileTailSizeToCheck;
    corrupt_files_limit = 1;

    // Should be able to compact after retry.
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    ASSERT_EQ("0,1", FilesPerLevel(0));
    ASSERT_EQ(3, corrupt_files.size()) << yb::AsString(corrupt_files);
    ASSERT_EQ(num_keys_written, ASSERT_RESULT(CountKeys()));
  }
}

// Tiered storage: verify that setting target_path_id routes both flushes and
// auto-compaction output to the requested db_paths slot, and that the default
// (0) is backward-compatible (all files land on the home disk).
//
// The per-CF target_path_id controls the *auto-compaction picker*
// (UniversalCompactionPicker::GetPathId in YB's one-level RocksDB setup).
// Manual compactions take an explicit target_path_id from CompactRangeOptions
// and do NOT consult MutableCFOptions. So the compaction half of this test
// explicitly uses universal auto-compaction by exceeding
// level0_file_num_compaction_trigger.
TEST_F(DBTest, TargetPathId) {
  const std::string path2 = dbname_ + "_tier1";

  Options options = CurrentOptions();
  options.compaction_style = kCompactionStyleUniversal;
  options.num_levels = 1;
  options.compaction_options_universal.allow_trivial_move = false;
  // Auto-compaction fires when L0 reaches 4 files; keep it off during setup.
  options.disable_auto_compactions = true;
  options.level0_file_num_compaction_trigger = 4;
  options.db_paths.emplace_back(dbname_, std::numeric_limits<uint64_t>::max());
  options.db_paths.emplace_back(path2,   std::numeric_limits<uint64_t>::max());
  DestroyAndReopen(options);

  // Part 1: default target (0), flushes land on path 0
  ASSERT_OK(Put("k1", "v1"));
  ASSERT_OK(Flush());
  {
    std::vector<LiveFileMetaData> meta;
    db_->GetLiveFilesMetaData(&meta);
    ASSERT_EQ(1u, meta.size());
    ASSERT_EQ(dbname_, meta[0].db_path) << "default flush must land on path 0";
  }

  // Part 2: switch target to slot 1, flush goes to path 1
  ASSERT_OK(db_->SetOptions({{"target_path_id", "1"}}));

  ASSERT_OK(Put("k2", "v2"));
  ASSERT_OK(Flush());
  {
    std::vector<LiveFileMetaData> meta;
    db_->GetLiveFilesMetaData(&meta);
    ASSERT_EQ(2u, meta.size());
    bool found_on_tier1 = false;
    for (const auto& f : meta) {
      if (f.db_path == path2) {
        found_on_tier1 = true;
      }
    }
    ASSERT_TRUE(found_on_tier1) << "flush after SetOptions must land on path 1";
  }

  ASSERT_EQ("v1", Get("k1"));
  ASSERT_EQ("v2", Get("k2"));

  // Part 3: universal auto-compaction picker routes to target_path_id.
  // Add enough L0 files to reach level0_file_num_compaction_trigger while
  // auto-compaction is still disabled, then snapshot the file set and enable
  // auto-compaction. The picker (UniversalCompactionPicker::GetPathId) must
  // place the freshly produced compaction output on target_path_id=1.
  for (int i = 3; i <= 5; ++i) {
    ASSERT_OK(Put("k" + std::to_string(i), "v" + std::to_string(i)));
    ASSERT_OK(Flush());
  }

  // Snapshot the file set before any compaction runs.
  std::unordered_set<std::string> pre_compaction_files;
  {
    std::vector<LiveFileMetaData> meta;
    db_->GetLiveFilesMetaData(&meta);
    for (const auto& f : meta) {
      pre_compaction_files.insert(f.Name());
    }
  }

  // >= level0_file_num_compaction_trigger (4) L0 files now exist,so enabling
  // auto-compaction fires the universal picker (GetPathId) immediately. Use
  // EnableAutoCompaction (not SetOptions) because it both clears
  // disable_auto_compactions and schedules background work.
  ASSERT_OK(dbfull()->EnableAutoCompaction({db_->DefaultColumnFamily()}));
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  {
    std::vector<LiveFileMetaData> meta;
    db_->GetLiveFilesMetaData(&meta);
    bool found_new_compaction_output = false;
    for (const auto& f : meta) {
      if (pre_compaction_files.count(f.Name())) {
        continue;  // pre-existing flushed file, not a compaction output
      }
      // A file that did not exist before we enabled compaction => produced by it.
      found_new_compaction_output = true;
      ASSERT_EQ(path2, f.db_path)
          << "auto-compaction output with target_path_id=1 must land on path 1 "
          << "(file=" << f.Name() << ")";
    }
    ASSERT_TRUE(found_new_compaction_output)
        << "expected a new SST produced by universal auto-compaction";
  }

  ASSERT_EQ("v1", Get("k1"));
  ASSERT_EQ("v2", Get("k2"));
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
