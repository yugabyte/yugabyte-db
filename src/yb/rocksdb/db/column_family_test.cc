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

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/options_parser.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/utilities/merge_operators.h"

#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"

DECLARE_int32(memstore_arena_size_kb);

using std::atomic;
using std::unique_ptr;

namespace rocksdb {

// counts how many operations were performed
class EnvCounter : public EnvWrapper {
 public:
  explicit EnvCounter(Env* base)
      : EnvWrapper(base) {
    num_new_writable_file_.store(0);
  }

  int GetNumberOfNewWritableFileCalls() {
    return num_new_writable_file_.load();
  }
  Status NewWritableFile(const std::string& f, unique_ptr<WritableFile>* r,
                         const EnvOptions& soptions) override {
    num_new_writable_file_.fetch_add(1);
    return EnvWrapper::NewWritableFile(f, r, soptions);
  }

 private:
  atomic<int> num_new_writable_file_;
};

class ColumnFamilyTest : public RocksDBTest {
 public:
  ColumnFamilyTest() : rnd_(139) {
    env_ = new EnvCounter(Env::Default());
    dbname_ = test::TmpDir() + "/column_family_test";
    db_options_.create_if_missing = true;
    db_options_.fail_if_options_file_error = true;
    db_options_.env = env_;
    CHECK_OK(DestroyDB(dbname_, Options(db_options_, column_family_options_)));
  }

  ~ColumnFamilyTest() {
    Close();
    yb::SyncPoint::GetInstance()->DisableProcessing();
    Destroy();
    delete env_;
  }

  void Close() {
    for (auto h : handles_) {
      if (h) {
        delete h;
      }
    }
    handles_.clear();
    names_.clear();
    delete db_;
    db_ = nullptr;
  }

  Status TryOpen(std::vector<std::string> cf,
                 std::vector<ColumnFamilyOptions> options = {}) {
    std::vector<ColumnFamilyDescriptor> column_families;
    names_.clear();
    for (size_t i = 0; i < cf.size(); ++i) {
      column_families.push_back(ColumnFamilyDescriptor(
          cf[i], options.size() == 0 ? column_family_options_ : options[i]));
      names_.push_back(cf[i]);
    }
    return DB::Open(db_options_, dbname_, column_families, &handles_, &db_);
  }

  Status OpenReadOnly(std::vector<std::string> cf,
                         std::vector<ColumnFamilyOptions> options = {}) {
    std::vector<ColumnFamilyDescriptor> column_families;
    names_.clear();
    for (size_t i = 0; i < cf.size(); ++i) {
      column_families.push_back(ColumnFamilyDescriptor(
          cf[i], options.size() == 0 ? column_family_options_ : options[i]));
      names_.push_back(cf[i]);
    }
    return DB::OpenForReadOnly(db_options_, dbname_, column_families, &handles_,
                               &db_);
  }

  void AssertOpenReadOnly(std::vector<std::string> cf,
                    std::vector<ColumnFamilyOptions> options = {}) {
    ASSERT_OK(OpenReadOnly(cf, options));
  }


  void Open(std::vector<std::string> cf,
            std::vector<ColumnFamilyOptions> options = {}) {
    ASSERT_OK(TryOpen(cf, options));
  }

  void Open() {
    Open({"default"});
  }

  DBImpl* dbfull() { return reinterpret_cast<DBImpl*>(db_); }

  int GetProperty(int cf, std::string property) {
    std::string value;
    EXPECT_TRUE(dbfull()->GetProperty(handles_[cf], property, &value));
#ifndef CYGWIN
    return std::stoi(value);
#else
    return std::strtol(value.c_str(), 0 /* off */, 10 /* base */);
#endif
  }

  void Destroy() {
    Close();
    ASSERT_OK(DestroyDB(dbname_, Options(db_options_, column_family_options_)));
  }

  void CreateColumnFamilies(
      const std::vector<std::string>& cfs,
      const std::vector<ColumnFamilyOptions>& options = {}) {
    int cfi = static_cast<int>(handles_.size());
    handles_.resize(cfi + cfs.size());
    names_.resize(cfi + cfs.size());
    for (size_t i = 0; i < cfs.size(); ++i) {
      const auto& current_cf_opt =
          options.empty() ? column_family_options_ : options[i];
      ASSERT_OK(
          db_->CreateColumnFamily(current_cf_opt, cfs[i], &handles_[cfi]));
      names_[cfi] = cfs[i];

      // Verify the CF options of the returned CF handle.
      ColumnFamilyDescriptor desc;
      ASSERT_OK(handles_[cfi]->GetDescriptor(&desc));
      if (current_cf_opt.arena_block_size == 0) {
        // When column family is created and specified arena_block_size is 0, we modify
        // arena_block_size to value specified by flags. See SanitizeOptions for info.
        // So reset it back to 0, to make verifier happy.
        desc.options.arena_block_size = 0;
      }
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(desc.options, current_cf_opt));
      cfi++;
    }
  }

  void Reopen(const std::vector<ColumnFamilyOptions> options = {}) {
    std::vector<std::string> names;
    for (auto name : names_) {
      if (name != "") {
        names.push_back(name);
      }
    }
    Close();
    assert(options.size() == 0 || names.size() == options.size());
    Open(names, options);
  }

  void CreateColumnFamiliesAndReopen(const std::vector<std::string>& cfs) {
    CreateColumnFamilies(cfs);
    Reopen();
  }

  void DropColumnFamilies(const std::vector<int>& cfs) {
    for (auto cf : cfs) {
      ASSERT_OK(db_->DropColumnFamily(handles_[cf]));
      delete handles_[cf];
      handles_[cf] = nullptr;
      names_[cf] = "";
    }
  }

  void PutRandomData(int cf, int num, int key_value_size, bool save = false) {
    for (int i = 0; i < num; ++i) {
      // 10 bytes for key, rest is value
      if (!save) {
        ASSERT_OK(Put(cf, test::RandomKey(&rnd_, 11),
                      RandomString(&rnd_, key_value_size - 10)));
      } else {
        std::string key = test::RandomKey(&rnd_, 11);
        keys_.insert(key);
        ASSERT_OK(Put(cf, key, RandomString(&rnd_, key_value_size - 10)));
      }
    }
  }

  void WaitForFlush(int cf) {
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[cf]));
  }

  void WaitForCompaction() {
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
  }

  uint64_t MaxTotalInMemoryState() {
    return dbfull()->TEST_MaxTotalInMemoryState();
  }

  void AssertMaxTotalInMemoryState(uint64_t value) {
    ASSERT_EQ(value, MaxTotalInMemoryState());
  }

  Status Put(int cf, const std::string& key, const std::string& value) {
    return db_->Put(WriteOptions(), handles_[cf], Slice(key), Slice(value));
  }
  Status Merge(int cf, const std::string& key, const std::string& value) {
    return db_->Merge(WriteOptions(), handles_[cf], Slice(key), Slice(value));
  }
  Status Flush(int cf) {
    return db_->Flush(FlushOptions(), handles_[cf]);
  }

  std::string Get(int cf, const std::string& key) {
    ReadOptions options;
    options.verify_checksums = true;
    std::string result;
    Status s = db_->Get(options, handles_[cf], Slice(key), &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  void CompactAll(int cf) {
    ASSERT_OK(db_->CompactRange(CompactRangeOptions(), handles_[cf], nullptr,
                                nullptr));
  }

  void Compact(int cf, const Slice& start, const Slice& limit) {
    ASSERT_OK(
        db_->CompactRange(CompactRangeOptions(), handles_[cf], &start, &limit));
  }

  int NumTableFilesAtLevel(int level, int cf) {
    return GetProperty(cf,
                       "rocksdb.num-files-at-level" + ToString(level));
  }

  // Return spread of files per level
  std::string FilesPerLevel(int cf) {
    std::string result;
    int last_non_zero_offset = 0;
    for (int level = 0; level < dbfull()->NumberLevels(handles_[cf]); level++) {
      int f = NumTableFilesAtLevel(level, cf);
      char buf[100];
      snprintf(buf, sizeof(buf), "%s%d", (level ? "," : ""), f);
      result += buf;
      if (f > 0) {
        last_non_zero_offset = static_cast<int>(result.size());
      }
    }
    result.resize(last_non_zero_offset);
    return result;
  }

  void AssertFilesPerLevel(const std::string& value, int cf) {
    ASSERT_EQ(value, FilesPerLevel(cf));
  }

  int CountLiveFiles() {
    std::vector<LiveFileMetaData> metadata;
    db_->GetLiveFilesMetaData(&metadata);
    return static_cast<int>(metadata.size());
  }

  void AssertCountLiveFiles(int expected_value) {
    ASSERT_EQ(expected_value, CountLiveFiles());
  }

  // Do n memtable flushes, each of which produces an sstable
  // covering the range [small,large].
  void MakeTables(int cf, int n, const std::string& small,
                  const std::string& large) {
    for (int i = 0; i < n; i++) {
      ASSERT_OK(Put(cf, small, "begin"));
      ASSERT_OK(Put(cf, large, "end"));
      ASSERT_OK(db_->Flush(FlushOptions(), handles_[cf]));
    }
  }

  int CountLiveLogFiles() {
    int micros_wait_for_log_deletion = 20000;
    env_->SleepForMicroseconds(micros_wait_for_log_deletion);
    int ret = 0;
    VectorLogPtr wal_files;
    Status s;
    // GetSortedWalFiles is a flakey function -- it gets all the wal_dir
    // children files and then later checks for their existence. if some of the
    // log files doesn't exist anymore, it reports an error. it does all of this
    // without DB mutex held, so if a background process deletes the log file
    // while the function is being executed, it returns an error. We retry the
    // function 10 times to avoid the error failing the test
    for (int retries = 0; retries < 10; ++retries) {
      wal_files.clear();
      s = db_->GetSortedWalFiles(&wal_files);
      if (s.ok()) {
        break;
      }
    }
    EXPECT_OK(s);
    for (const auto& wal : wal_files) {
      if (wal->Type() == kAliveLogFile) {
        ++ret;
      }
    }
    return ret;
    return 0;
  }

  void AssertCountLiveLogFiles(int value) {
    ASSERT_EQ(value, CountLiveLogFiles());
  }

  void AssertNumberOfImmutableMemtables(std::vector<int> num_per_cf) {
    assert(num_per_cf.size() == handles_.size());

    for (size_t i = 0; i < num_per_cf.size(); ++i) {
      ASSERT_EQ(num_per_cf[i], GetProperty(static_cast<int>(i),
                                           "rocksdb.num-immutable-mem-table"));
    }
  }

  void CopyFile(const std::string& source, const std::string& destination,
                uint64_t size = 0) {
    const EnvOptions soptions;
    unique_ptr<SequentialFile> srcfile;
    ASSERT_OK(env_->NewSequentialFile(source, &srcfile, soptions));
    unique_ptr<WritableFile> destfile;
    ASSERT_OK(env_->NewWritableFile(destination, &destfile, soptions));

    if (size == 0) {
      // default argument means copy everything
      ASSERT_OK(env_->GetFileSize(source, &size));
    }

    uint8_t buffer[4096];
    Slice slice;
    while (size > 0) {
      uint64_t one = std::min(uint64_t(sizeof(buffer)), size);
      ASSERT_OK(srcfile->Read(one, &slice, buffer));
      ASSERT_OK(destfile->Append(slice));
      size -= slice.size();
    }
    ASSERT_OK(destfile->Close());
  }

  std::vector<ColumnFamilyHandle*> handles_;
  std::vector<std::string> names_;
  std::set<std::string> keys_;
  ColumnFamilyOptions column_family_options_;
  DBOptions db_options_;
  std::string dbname_;
  DB* db_ = nullptr;
  EnvCounter* env_;
  Random rnd_;
};

TEST_F(ColumnFamilyTest, DontReuseColumnFamilyID) {
  for (int iter = 0; iter < 3; ++iter) {
    Open();
    CreateColumnFamilies({"one", "two", "three"});
    for (size_t i = 0; i < handles_.size(); ++i) {
      auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(handles_[i]);
      ASSERT_EQ(i, cfh->GetID());
    }
    if (iter == 1) {
      Reopen();
    }
    DropColumnFamilies({3});
    Reopen();
    if (iter == 2) {
      // this tests if max_column_family is correctly persisted with
      // WriteSnapshot()
      Reopen();
    }
    CreateColumnFamilies({"three2"});
    // ID 3 that was used for dropped column family "three" should not be reused
    auto cfh3 = reinterpret_cast<ColumnFamilyHandleImpl*>(handles_[3]);
    ASSERT_EQ(4U, cfh3->GetID());
    Close();
    Destroy();
  }
}

TEST_F(ColumnFamilyTest, AddDrop) {
  Open();
  CreateColumnFamilies({"one", "two", "three"});
  ASSERT_EQ("NOT_FOUND", Get(1, "fodor"));
  ASSERT_EQ("NOT_FOUND", Get(2, "fodor"));
  DropColumnFamilies({2});
  ASSERT_EQ("NOT_FOUND", Get(1, "fodor"));
  CreateColumnFamilies({"four"});
  ASSERT_EQ("NOT_FOUND", Get(3, "fodor"));
  ASSERT_OK(Put(1, "fodor", "mirko"));
  ASSERT_EQ("mirko", Get(1, "fodor"));
  ASSERT_EQ("NOT_FOUND", Get(3, "fodor"));
  Close();
  ASSERT_TRUE(TryOpen({"default"}).IsInvalidArgument());
  Open({"default", "one", "three", "four"});
  DropColumnFamilies({1});
  Reopen();
  Close();

  std::vector<std::string> families;
  ASSERT_OK(DB::ListColumnFamilies(db_options_, dbname_, &families));
  sort(families.begin(), families.end());
  ASSERT_TRUE(families ==
              std::vector<std::string>({"default", "four", "three"}));
}

TEST_F(ColumnFamilyTest, DropTest) {
  // first iteration - dont reopen DB before dropping
  // second iteration - reopen DB before dropping
  for (int iter = 0; iter < 2; ++iter) {
    Open({"default"});
    CreateColumnFamiliesAndReopen({"pikachu"});
    for (int i = 0; i < 100; ++i) {
      ASSERT_OK(Put(1, ToString(i), "bar" + ToString(i)));
    }
    ASSERT_OK(Flush(1));

    if (iter == 1) {
      Reopen();
    }
    ASSERT_EQ("bar1", Get(1, "1"));

    AssertCountLiveFiles(1);
    DropColumnFamilies({1});
    // make sure that all files are deleted when we drop the column family
    AssertCountLiveFiles(0);
    Destroy();
  }
}

TEST_F(ColumnFamilyTest, WriteBatchFailure) {
  Open();
  CreateColumnFamiliesAndReopen({"one", "two"});
  WriteBatch batch;
  batch.Put(handles_[0], Slice("existing"), Slice("column-family"));
  batch.Put(handles_[1], Slice("non-existing"), Slice("column-family"));
  ASSERT_OK(db_->Write(WriteOptions(), &batch));
  DropColumnFamilies({1});
  WriteOptions woptions_ignore_missing_cf;
  woptions_ignore_missing_cf.ignore_missing_column_families = true;
  batch.Put(handles_[0], Slice("still here"), Slice("column-family"));
  ASSERT_OK(db_->Write(woptions_ignore_missing_cf, &batch));
  ASSERT_EQ("column-family", Get(0, "still here"));
  Status s = db_->Write(WriteOptions(), &batch);
  ASSERT_TRUE(s.IsInvalidArgument());
  Close();
}

TEST_F(ColumnFamilyTest, ReadWrite) {
  Open();
  CreateColumnFamiliesAndReopen({"one", "two"});
  ASSERT_OK(Put(0, "foo", "v1"));
  ASSERT_OK(Put(0, "bar", "v2"));
  ASSERT_OK(Put(1, "mirko", "v3"));
  ASSERT_OK(Put(0, "foo", "v2"));
  ASSERT_OK(Put(2, "fodor", "v5"));

  for (int iter = 0; iter <= 3; ++iter) {
    ASSERT_EQ("v2", Get(0, "foo"));
    ASSERT_EQ("v2", Get(0, "bar"));
    ASSERT_EQ("v3", Get(1, "mirko"));
    ASSERT_EQ("v5", Get(2, "fodor"));
    ASSERT_EQ("NOT_FOUND", Get(0, "fodor"));
    ASSERT_EQ("NOT_FOUND", Get(1, "fodor"));
    ASSERT_EQ("NOT_FOUND", Get(2, "foo"));
    if (iter <= 1) {
      Reopen();
    }
  }
  Close();
}

TEST_F(ColumnFamilyTest, IgnoreRecoveredLog) {
  std::string backup_logs = dbname_ + "/backup_logs";

  // delete old files in backup_logs directory
  ASSERT_OK(env_->CreateDirIfMissing(dbname_));
  ASSERT_OK(env_->CreateDirIfMissing(backup_logs));
  std::vector<std::string> old_files;
  ASSERT_OK(env_->GetChildren(backup_logs, &old_files));
  for (auto& file : old_files) {
    if (file != "." && file != "..") {
      ASSERT_OK(env_->DeleteFile(backup_logs + "/" + file));
    }
  }

  column_family_options_.merge_operator =
      MergeOperators::CreateUInt64AddOperator();
  db_options_.wal_dir = dbname_ + "/logs";
  Destroy();
  Open();
  CreateColumnFamilies({"cf1", "cf2"});

  // fill up the DB
  std::string one, two, three;
  PutFixed64(&one, 1);
  PutFixed64(&two, 2);
  PutFixed64(&three, 3);
  ASSERT_OK(Merge(0, "foo", one));
  ASSERT_OK(Merge(1, "mirko", one));
  ASSERT_OK(Merge(0, "foo", one));
  ASSERT_OK(Merge(2, "bla", one));
  ASSERT_OK(Merge(2, "fodor", one));
  ASSERT_OK(Merge(0, "bar", one));
  ASSERT_OK(Merge(2, "bla", one));
  ASSERT_OK(Merge(1, "mirko", two));
  ASSERT_OK(Merge(1, "franjo", one));

  // copy the logs to backup
  std::vector<std::string> logs;
  ASSERT_OK(env_->GetChildren(db_options_.wal_dir, &logs));
  for (auto& log : logs) {
    if (log != ".." && log != ".") {
      CopyFile(db_options_.wal_dir + "/" + log, backup_logs + "/" + log);
    }
  }

  // recover the DB
  Close();

  // 1. check consistency
  // 2. copy the logs from backup back to WAL dir. if the recovery happens
  // again on the same log files, this should lead to incorrect results
  // due to applying merge operator twice
  // 3. check consistency
  for (int iter = 0; iter < 2; ++iter) {
    // assert consistency
    Open({"default", "cf1", "cf2"});
    ASSERT_EQ(two, Get(0, "foo"));
    ASSERT_EQ(one, Get(0, "bar"));
    ASSERT_EQ(three, Get(1, "mirko"));
    ASSERT_EQ(one, Get(1, "franjo"));
    ASSERT_EQ(one, Get(2, "fodor"));
    ASSERT_EQ(two, Get(2, "bla"));
    Close();

    if (iter == 0) {
      // copy the logs from backup back to wal dir
      for (auto& log : logs) {
        if (log != ".." && log != ".") {
          CopyFile(backup_logs + "/" + log, db_options_.wal_dir + "/" + log);
        }
      }
    }
  }
}

TEST_F(ColumnFamilyTest, FlushTest) {
  Open();
  CreateColumnFamiliesAndReopen({"one", "two"});
  ASSERT_OK(Put(0, "foo", "v1"));
  ASSERT_OK(Put(0, "bar", "v2"));
  ASSERT_OK(Put(1, "mirko", "v3"));
  ASSERT_OK(Put(0, "foo", "v2"));
  ASSERT_OK(Put(2, "fodor", "v5"));

  for (int j = 0; j < 2; j++) {
    ReadOptions ro;
    std::vector<Iterator*> iterators;
    // Hold super version.
    if (j == 0) {
      ASSERT_OK(db_->NewIterators(ro, handles_, &iterators));
    }

    for (int i = 0; i < 3; ++i) {
      uint64_t max_total_in_memory_state =
          MaxTotalInMemoryState();
      ASSERT_OK(Flush(i));
      AssertMaxTotalInMemoryState(max_total_in_memory_state);
    }
    ASSERT_OK(Put(1, "foofoo", "bar"));
    ASSERT_OK(Put(0, "foofoo", "bar"));

    for (auto* it : iterators) {
      delete it;
    }
  }
  Reopen();

  for (int iter = 0; iter <= 2; ++iter) {
    ASSERT_EQ("v2", Get(0, "foo"));
    ASSERT_EQ("v2", Get(0, "bar"));
    ASSERT_EQ("v3", Get(1, "mirko"));
    ASSERT_EQ("v5", Get(2, "fodor"));
    ASSERT_EQ("NOT_FOUND", Get(0, "fodor"));
    ASSERT_EQ("NOT_FOUND", Get(1, "fodor"));
    ASSERT_EQ("NOT_FOUND", Get(2, "foo"));
    if (iter <= 1) {
      Reopen();
    }
  }
  Close();
}

// Makes sure that obsolete log files get deleted
TEST_F(ColumnFamilyTest, LogDeletionTest) {
  db_options_.max_total_wal_size = std::numeric_limits<uint64_t>::max();
  column_family_options_.arena_block_size = 4 * 1024;
  column_family_options_.write_buffer_size = 100000;  // 100KB
  Open();
  CreateColumnFamilies({"one", "two", "three", "four"});
  // Each bracket is one log file. if number is in (), it means
  // we don't need it anymore (it's been flushed)
  // []
  AssertCountLiveLogFiles(0);
  PutRandomData(0, 1, 100);
  // [0]
  PutRandomData(1, 1, 100);
  // [0, 1]
  PutRandomData(1, 1000, 100);
  WaitForFlush(1);
  // [0, (1)] [1]
  AssertCountLiveLogFiles(2);
  PutRandomData(0, 1, 100);
  // [0, (1)] [0, 1]
  AssertCountLiveLogFiles(2);
  PutRandomData(2, 1, 100);
  // [0, (1)] [0, 1, 2]
  PutRandomData(2, 1000, 100);
  WaitForFlush(2);
  // [0, (1)] [0, 1, (2)] [2]
  AssertCountLiveLogFiles(3);
  PutRandomData(2, 1000, 100);
  WaitForFlush(2);
  // [0, (1)] [0, 1, (2)] [(2)] [2]
  AssertCountLiveLogFiles(4);
  PutRandomData(3, 1, 100);
  // [0, (1)] [0, 1, (2)] [(2)] [2, 3]
  PutRandomData(1, 1, 100);
  // [0, (1)] [0, 1, (2)] [(2)] [1, 2, 3]
  AssertCountLiveLogFiles(4);
  PutRandomData(1, 1000, 100);
  WaitForFlush(1);
  // [0, (1)] [0, (1), (2)] [(2)] [(1), 2, 3] [1]
  AssertCountLiveLogFiles(5);
  PutRandomData(0, 1000, 100);
  WaitForFlush(0);
  // [(0), (1)] [(0), (1), (2)] [(2)] [(1), 2, 3] [1, (0)] [0]
  // delete obsolete logs -->
  // [(1), 2, 3] [1, (0)] [0]
  AssertCountLiveLogFiles(3);
  PutRandomData(0, 1000, 100);
  WaitForFlush(0);
  // [(1), 2, 3] [1, (0)], [(0)] [0]
  AssertCountLiveLogFiles(4);
  PutRandomData(1, 1000, 100);
  WaitForFlush(1);
  // [(1), 2, 3] [(1), (0)] [(0)] [0, (1)] [1]
  AssertCountLiveLogFiles(5);
  PutRandomData(2, 1000, 100);
  WaitForFlush(2);
  // [(1), (2), 3] [(1), (0)] [(0)] [0, (1)] [1, (2)], [2]
  AssertCountLiveLogFiles(6);
  PutRandomData(3, 1000, 100);
  WaitForFlush(3);
  // [(1), (2), (3)] [(1), (0)] [(0)] [0, (1)] [1, (2)], [2, (3)] [3]
  // delete obsolete logs -->
  // [0, (1)] [1, (2)], [2, (3)] [3]
  AssertCountLiveLogFiles(4);
  Close();
}

// Makes sure that obsolete log files get deleted
TEST_F(ColumnFamilyTest, DifferentWriteBufferSizes) {
  // disable flushing stale column families
  db_options_.max_total_wal_size = std::numeric_limits<uint64_t>::max();
  Open();
  CreateColumnFamilies({"one", "two", "three"});
  ColumnFamilyOptions default_cf, one, two, three;
  // setup options. all column families have max_write_buffer_number setup to 10
  // "default" -> 100KB memtable, start flushing immediatelly
  // "one" -> 200KB memtable, start flushing with two immutable memtables
  // "two" -> 1MB memtable, start flushing with three immutable memtables
  // "three" -> 90KB memtable, start flushing with four immutable memtables
  default_cf.write_buffer_size = 100000;
  default_cf.arena_block_size = 4 * 4096;
  default_cf.max_write_buffer_number = 10;
  default_cf.min_write_buffer_number_to_merge = 1;
  default_cf.max_write_buffer_number_to_maintain = 0;
  one.write_buffer_size = 200000;
  one.arena_block_size = 4 * 4096;
  one.max_write_buffer_number = 10;
  one.min_write_buffer_number_to_merge = 2;
  one.max_write_buffer_number_to_maintain = 1;
  two.write_buffer_size = 1000000;
  two.arena_block_size = 4 * 4096;
  two.max_write_buffer_number = 10;
  two.min_write_buffer_number_to_merge = 3;
  two.max_write_buffer_number_to_maintain = 2;
  three.write_buffer_size = 4096 * 22;
  three.arena_block_size = 4096;
  three.max_write_buffer_number = 10;
  three.min_write_buffer_number_to_merge = 4;
  three.max_write_buffer_number_to_maintain = -1;

  Reopen({default_cf, one, two, three});

  int micros_wait_for_flush = 10000;
  PutRandomData(0, 100, 1000);
  WaitForFlush(0);
  AssertNumberOfImmutableMemtables({0, 0, 0, 0});
  AssertCountLiveLogFiles(1);
  PutRandomData(1, 200, 1000);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 0, 0});
  AssertCountLiveLogFiles(2);
  PutRandomData(2, 1000, 1000);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 1, 0});
  AssertCountLiveLogFiles(3);
  PutRandomData(2, 1000, 1000);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 2, 0});
  AssertCountLiveLogFiles(4);
  PutRandomData(3, 93, 990);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 2, 1});
  AssertCountLiveLogFiles(5);
  PutRandomData(3, 88, 990);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 2, 2});
  AssertCountLiveLogFiles(6);
  PutRandomData(3, 88, 990);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 2, 3});
  AssertCountLiveLogFiles(7);
  PutRandomData(0, 100, 1000);
  WaitForFlush(0);
  AssertNumberOfImmutableMemtables({0, 1, 2, 3});
  AssertCountLiveLogFiles(8);
  PutRandomData(2, 100, 10000);
  WaitForFlush(2);
  AssertNumberOfImmutableMemtables({0, 1, 0, 3});
  AssertCountLiveLogFiles(9);
  PutRandomData(3, 88, 990);
  WaitForFlush(3);
  AssertNumberOfImmutableMemtables({0, 1, 0, 0});
  AssertCountLiveLogFiles(10);
  PutRandomData(3, 88, 990);
  env_->SleepForMicroseconds(micros_wait_for_flush);
  AssertNumberOfImmutableMemtables({0, 1, 0, 1});
  AssertCountLiveLogFiles(11);
  PutRandomData(1, 200, 1000);
  WaitForFlush(1);
  AssertNumberOfImmutableMemtables({0, 0, 0, 1});
  AssertCountLiveLogFiles(5);
  PutRandomData(3, 88 * 3, 990);
  WaitForFlush(3);
  PutRandomData(3, 88 * 4, 990);
  WaitForFlush(3);
  AssertNumberOfImmutableMemtables({0, 0, 0, 0});
  AssertCountLiveLogFiles(12);
  PutRandomData(0, 100, 1000);
  WaitForFlush(0);
  AssertNumberOfImmutableMemtables({0, 0, 0, 0});
  AssertCountLiveLogFiles(12);
  PutRandomData(2, 3 * 1000, 1000);
  WaitForFlush(2);
  AssertNumberOfImmutableMemtables({0, 0, 0, 0});
  AssertCountLiveLogFiles(12);
  PutRandomData(1, 2*200, 1000);
  WaitForFlush(1);
  AssertNumberOfImmutableMemtables({0, 0, 0, 0});
  AssertCountLiveLogFiles(7);
  Close();
}

TEST_F(ColumnFamilyTest, DifferentMergeOperators) {
  Open();
  CreateColumnFamilies({"first", "second"});
  ColumnFamilyOptions default_cf, first, second;
  first.merge_operator = MergeOperators::CreateUInt64AddOperator();
  second.merge_operator = MergeOperators::CreateStringAppendOperator();
  Reopen({default_cf, first, second});

  std::string one, two, three;
  PutFixed64(&one, 1);
  PutFixed64(&two, 2);
  PutFixed64(&three, 3);

  ASSERT_OK(Put(0, "foo", two));
  ASSERT_OK(Put(0, "foo", one));
  ASSERT_TRUE(Merge(0, "foo", two).IsNotSupported());
  ASSERT_EQ(Get(0, "foo"), one);

  ASSERT_OK(Put(1, "foo", two));
  ASSERT_OK(Put(1, "foo", one));
  ASSERT_OK(Merge(1, "foo", two));
  ASSERT_EQ(Get(1, "foo"), three);

  ASSERT_OK(Put(2, "foo", two));
  ASSERT_OK(Put(2, "foo", one));
  ASSERT_OK(Merge(2, "foo", two));
  ASSERT_EQ(Get(2, "foo"), one + "," + two);
  Close();
}

TEST_F(ColumnFamilyTest, DifferentCompactionStyles) {
  Open();
  CreateColumnFamilies({"one", "two"});
  ColumnFamilyOptions default_cf, one, two;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  two.compaction_style = kCompactionStyleLevel;
  two.num_levels = 4;
  two.level0_file_num_compaction_trigger = 3;
  two.write_buffer_size = 100000;

  Reopen({default_cf, one, two});

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 1; ++i) {
    PutRandomData(1, 10, 12000);
    PutRandomData(1, 1, 10);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }

  // SETUP column family "two" -- level style with 4 levels
  for (int i = 0; i < two.level0_file_num_compaction_trigger - 1; ++i) {
    PutRandomData(2, 10, 12000);
    PutRandomData(2, 1, 10);
    WaitForFlush(2);
    AssertFilesPerLevel(ToString(i + 1), 2);
  }

  // TRIGGER compaction "one"
  PutRandomData(1, 10, 12000);
  PutRandomData(1, 1, 10);

  // TRIGGER compaction "two"
  PutRandomData(2, 10, 12000);
  PutRandomData(2, 1, 10);

  // WAIT for compactions
  WaitForCompaction();

  // VERIFY compaction "one"
  AssertFilesPerLevel("1", 1);

  // VERIFY compaction "two"
  AssertFilesPerLevel("0,1", 2);
  CompactAll(2);
  AssertFilesPerLevel("0,1", 2);

  Close();
}

// Sync points not supported in RocksDB Lite

TEST_F(ColumnFamilyTest, MultipleManualCompactions) {
  Open();
  CreateColumnFamilies({"one", "two"});
  ColumnFamilyOptions default_cf, one, two;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  two.compaction_style = kCompactionStyleLevel;
  two.num_levels = 4;
  two.level0_file_num_compaction_trigger = 3;
  two.write_buffer_size = 100000;

  Reopen({default_cf, one, two});

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }
  bool cf_1_1 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::MultiManual:4", "ColumnFamilyTest::MultiManual:1"},
       {"ColumnFamilyTest::MultiManual:2", "ColumnFamilyTest::MultiManual:5"},
       {"ColumnFamilyTest::MultiManual:2", "ColumnFamilyTest::MultiManual:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::MultiManual:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::MultiManual:3");
        }
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();
  std::vector<std::thread> threads;
  threads.emplace_back([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
  });

  // SETUP column family "two" -- level style with 4 levels
  for (int i = 0; i < two.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(2, 10, 12000);
    PutRandomData(2, 1, 10);
    WaitForFlush(2);
    AssertFilesPerLevel(ToString(i + 1), 2);
  }
  threads.emplace_back([&] {
    TEST_SYNC_POINT("ColumnFamilyTest::MultiManual:1");
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[2], nullptr, nullptr));
    TEST_SYNC_POINT("ColumnFamilyTest::MultiManual:2");
  });

  TEST_SYNC_POINT("ColumnFamilyTest::MultiManual:5");
  for (auto& t : threads) {
    t.join();
  }

  // VERIFY compaction "one"
  AssertFilesPerLevel("1", 1);

  // VERIFY compaction "two"
  AssertFilesPerLevel("0,1", 2);
  CompactAll(2);
  AssertFilesPerLevel("0,1", 2);
  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }
  Close();
}

TEST_F(ColumnFamilyTest, AutomaticAndManualCompactions) {
  Open();
  CreateColumnFamilies({"one", "two"});
  ColumnFamilyOptions default_cf, one, two;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  two.compaction_style = kCompactionStyleLevel;
  two.num_levels = 4;
  two.level0_file_num_compaction_trigger = 3;
  two.write_buffer_size = 100000;

  Reopen({default_cf, one, two});

  bool cf_1_1 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::AutoManual:4", "ColumnFamilyTest::AutoManual:1"},
       {"ColumnFamilyTest::AutoManual:2", "ColumnFamilyTest::AutoManual:5"},
       {"ColumnFamilyTest::AutoManual:2", "ColumnFamilyTest::AutoManual:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:4");
          TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:3");
        }
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();
  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }

  TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:1");

  // SETUP column family "two" -- level style with 4 levels
  for (int i = 0; i < two.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(2, 10, 12000);
    PutRandomData(2, 1, 10);
    WaitForFlush(2);
    AssertFilesPerLevel(ToString(i + 1), 2);
  }
  std::thread threads([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[2], nullptr, nullptr));
    TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:2");
  });

  TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:5");
  threads.join();

  // WAIT for compactions
  WaitForCompaction();

  // VERIFY compaction "one"
  AssertFilesPerLevel("1", 1);

  // VERIFY compaction "two"
  AssertFilesPerLevel("0,1", 2);
  CompactAll(2);
  AssertFilesPerLevel("0,1", 2);
  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }
  Close();
}

TEST_F(ColumnFamilyTest, ManualAndAutomaticCompactions) {
  Open();
  CreateColumnFamilies({"one", "two"});
  ColumnFamilyOptions default_cf, one, two;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  two.compaction_style = kCompactionStyleLevel;
  two.num_levels = 4;
  two.level0_file_num_compaction_trigger = 3;
  two.write_buffer_size = 100000;

  Reopen({default_cf, one, two});

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }
  bool cf_1_1 = true;
  bool cf_1_2 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::ManualAuto:4", "ColumnFamilyTest::ManualAuto:1"},
       {"ColumnFamilyTest::ManualAuto:5", "ColumnFamilyTest::ManualAuto:2"},
       {"ColumnFamilyTest::ManualAuto:2", "ColumnFamilyTest::ManualAuto:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:3");
        } else if (cf_1_2) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:2");
          cf_1_2 = false;
        }
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();
  std::thread threads([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
  });

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:1");

  // SETUP column family "two" -- level style with 4 levels
  for (int i = 0; i < two.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(2, 10, 12000);
    PutRandomData(2, 1, 10);
    WaitForFlush(2);
    AssertFilesPerLevel(ToString(i + 1), 2);
  }
  TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:5");
  threads.join();

  // WAIT for compactions
  WaitForCompaction();

  // VERIFY compaction "one"
  AssertFilesPerLevel("1", 1);

  // VERIFY compaction "two"
  AssertFilesPerLevel("0,1", 2);
  CompactAll(2);
  AssertFilesPerLevel("0,1", 2);
  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }
  Close();
}

TEST_F(ColumnFamilyTest, SameCFManualManualCompactions) {
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyOptions default_cf, one;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  Reopen({default_cf, one});

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }
  bool cf_1_1 = true;
  bool cf_1_2 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::ManualManual:4", "ColumnFamilyTest::ManualManual:2"},
       {"ColumnFamilyTest::ManualManual:4", "ColumnFamilyTest::ManualManual:5"},
       {"ColumnFamilyTest::ManualManual:1", "ColumnFamilyTest::ManualManual:2"},
       {"ColumnFamilyTest::ManualManual:1",
        "ColumnFamilyTest::ManualManual:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualManual:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::ManualManual:3");
        } else if (cf_1_2) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualManual:2");
          cf_1_2 = false;
        }
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();
  std::thread threads([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = true;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
  });

  TEST_SYNC_POINT("ColumnFamilyTest::ManualManual:5");

  WaitForFlush(1);

  // Add more L0 files and force another manual compaction
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(one.level0_file_num_compaction_trigger + i),
                        1);
  }

  std::thread threads1([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
  });

  TEST_SYNC_POINT("ColumnFamilyTest::ManualManual:1");

  threads.join();
  threads1.join();
  WaitForCompaction();
  // VERIFY compaction "one"
  ASSERT_LE(NumTableFilesAtLevel(0, 1), 2);

  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }
  Close();
}

TEST_F(ColumnFamilyTest, SameCFManualAutomaticCompactions) {
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyOptions default_cf, one;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  Reopen({default_cf, one});

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }
  bool cf_1_1 = true;
  bool cf_1_2 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::ManualAuto:4", "ColumnFamilyTest::ManualAuto:2"},
       {"ColumnFamilyTest::ManualAuto:4", "ColumnFamilyTest::ManualAuto:5"},
       {"ColumnFamilyTest::ManualAuto:1", "ColumnFamilyTest::ManualAuto:2"},
       {"ColumnFamilyTest::ManualAuto:1", "ColumnFamilyTest::ManualAuto:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:3");
        } else if (cf_1_2) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:2");
          cf_1_2 = false;
        }
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();
  std::thread threads([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
  });

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:5");

  WaitForFlush(1);

  // Add more L0 files and force automatic compaction
  for (int i = 0; i < one.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(one.level0_file_num_compaction_trigger + i),
                        1);
  }

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:1");

  threads.join();
  WaitForCompaction();
  // VERIFY compaction "one"
  ASSERT_LE(NumTableFilesAtLevel(0, 1), 2);

  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }
  Close();
}

TEST_F(ColumnFamilyTest, SameCFManualAutomaticCompactionsLevel) {
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyOptions default_cf, one;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleLevel;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  Reopen({default_cf, one});

  // SETUP column family "one" -- level style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }
  bool cf_1_1 = true;
  bool cf_1_2 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::ManualAuto:4", "ColumnFamilyTest::ManualAuto:2"},
       {"ColumnFamilyTest::ManualAuto:4", "ColumnFamilyTest::ManualAuto:5"},
       {"ColumnFamilyTest::ManualAuto:3", "ColumnFamilyTest::ManualAuto:2"},
       {"LevelCompactionPicker::PickCompactionBySize:0",
        "ColumnFamilyTest::ManualAuto:3"},
       {"ColumnFamilyTest::ManualAuto:1", "ColumnFamilyTest::ManualAuto:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:3");
        } else if (cf_1_2) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:2");
          cf_1_2 = false;
        }
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();
  std::thread threads([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
  });

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:5");

  // Add more L0 files and force automatic compaction
  for (int i = 0; i < one.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(one.level0_file_num_compaction_trigger + i),
                        1);
  }

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAuto:1");

  threads.join();
  WaitForCompaction();
  // VERIFY compaction "one"
  AssertFilesPerLevel("0,1", 1);

  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }
  Close();
}

// This test checks for automatic getting a conflict if there is a
// manual which has not yet been scheduled.
// The manual compaction waits in NotScheduled
// We generate more files and then trigger an automatic compaction
// This will wait because there is an unscheduled manual compaction.
// Once the conflict is hit, the manual compaction starts and ends
// Then another automatic will start and end.
TEST_F(ColumnFamilyTest, SameCFManualAutomaticConflict) {
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyOptions default_cf, one;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  Reopen({default_cf, one});

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }
  bool cf_1_1 = true;
  bool cf_1_2 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::ManualAutoCon:7",
        "DBImpl::RunManualCompaction()::Conflict"},
       {"ColumnFamilyTest::ManualAutoCon:9",
        "ColumnFamilyTest::ManualAutoCon:8"},
       {"ColumnFamilyTest::ManualAutoCon:2",
        "ColumnFamilyTest::ManualAutoCon:6"},
       {"ColumnFamilyTest::ManualAutoCon:4",
        "ColumnFamilyTest::ManualAutoCon:5"},
       {"ColumnFamilyTest::ManualAutoCon:1",
        "ColumnFamilyTest::ManualAutoCon:2"},
       {"ColumnFamilyTest::ManualAutoCon:1",
        "ColumnFamilyTest::ManualAutoCon:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:3");
        } else if (cf_1_2) {
          cf_1_2 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:2");
        }
      });
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::RunManualCompaction:NotScheduled", [&](void* arg) {
        InstrumentedMutex* mutex = static_cast<InstrumentedMutex*>(arg);
        mutex->Unlock();
        TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:9");
        TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:7");
        mutex->Lock();
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();
  std::thread threads([&] {
    CompactRangeOptions compact_options;
    compact_options.exclusive_manual_compaction = false;
    ASSERT_OK(
        db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));
    TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:6");
  });

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:8");
  WaitForFlush(1);

  // Add more L0 files and force automatic compaction
  for (int i = 0; i < one.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(one.level0_file_num_compaction_trigger + i),
                        1);
  }

  TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:5");
  // Add more L0 files and force automatic compaction
  for (int i = 0; i < one.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
  }
  TEST_SYNC_POINT("ColumnFamilyTest::ManualAutoCon:1");

  threads.join();
  WaitForCompaction();
  // VERIFY compaction "one"
  ASSERT_LE(NumTableFilesAtLevel(0, 1), 3);

  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }

  Close();
}

// In this test, we generate enough files to trigger automatic compactions.
// The automatic compaction waits in NonTrivial:AfterRun
// We generate more files and then trigger an automatic compaction
// This will wait because the automatic compaction has files it needs.
// Once the conflict is hit, the automatic compaction starts and ends
// Then the manual will run and end.
TEST_F(ColumnFamilyTest, SameCFAutomaticManualCompactions) {
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyOptions default_cf, one;
  db_options_.max_open_files = 20;  // only 10 files in file cache
  db_options_.disableDataSync = true;
  db_options_.max_background_compactions = 3;

  default_cf.compaction_style = kCompactionStyleLevel;
  default_cf.num_levels = 3;
  default_cf.write_buffer_size = 64 << 10;  // 64KB
  default_cf.target_file_size_base = 30 << 10;
  default_cf.source_compaction_factor = 100;
  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;
  default_cf.table_factory.reset(NewBlockBasedTableFactory(table_options));

  one.compaction_style = kCompactionStyleUniversal;

  one.num_levels = 1;
  // trigger compaction if there are >= 4 files
  one.level0_file_num_compaction_trigger = 4;
  one.write_buffer_size = 120000;

  Reopen({default_cf, one});

  bool cf_1_1 = true;
  bool cf_1_2 = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"ColumnFamilyTest::AutoManual:4", "ColumnFamilyTest::AutoManual:2"},
       {"ColumnFamilyTest::AutoManual:4", "ColumnFamilyTest::AutoManual:5"},
       {"CompactionPicker::CompactRange:Conflict",
        "ColumnFamilyTest::AutoManual:3"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::BackgroundCompaction:NonTrivial:AfterRun", [&](void* arg) {
        if (cf_1_1) {
          TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:4");
          cf_1_1 = false;
          TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:3");
        } else if (cf_1_2) {
          TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:2");
          cf_1_2 = false;
        }
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();

  // SETUP column family "one" -- universal style
  for (int i = 0; i < one.level0_file_num_compaction_trigger; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
    AssertFilesPerLevel(ToString(i + 1), 1);
  }

  TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:5");

  // Add another L0 file and force automatic compaction
  for (int i = 0; i < one.level0_file_num_compaction_trigger - 2; ++i) {
    PutRandomData(1, 10, 12000, true);
    PutRandomData(1, 1, 10, true);
    WaitForFlush(1);
  }

  CompactRangeOptions compact_options;
  compact_options.exclusive_manual_compaction = false;
  ASSERT_OK(db_->CompactRange(compact_options, handles_[1], nullptr, nullptr));

  TEST_SYNC_POINT("ColumnFamilyTest::AutoManual:1");

  WaitForCompaction();
  // VERIFY compaction "one"
  AssertFilesPerLevel("1", 1);
  // Compare against saved keys
  std::set<std::string>::iterator key_iter = keys_.begin();
  while (key_iter != keys_.end()) {
    ASSERT_NE("NOT_FOUND", Get(1, *key_iter));
    key_iter++;
  }

  Close();
}

namespace {
std::string IterStatus(Iterator* iter) {
  std::string result;
  if (iter->Valid()) {
    result = iter->key().ToString() + "->" + iter->value().ToString();
  } else {
    result = "(invalid) " + iter->status().ToString();
  }
  return result;
}
}  // anonymous namespace

TEST_F(ColumnFamilyTest, NewIteratorsTest) {
  // iter == 0 -- no tailing
  // iter == 2 -- tailing
  for (int iter = 0; iter < 2; ++iter) {
    Open();
    CreateColumnFamiliesAndReopen({"one", "two"});
    ASSERT_OK(Put(0, "a", "b"));
    ASSERT_OK(Put(1, "b", "a"));
    ASSERT_OK(Put(2, "c", "m"));
    ASSERT_OK(Put(2, "v", "t"));
    std::vector<Iterator*> iterators;
    ReadOptions options;
    options.tailing = (iter == 1);
    ASSERT_OK(db_->NewIterators(options, handles_, &iterators));

    for (auto it : iterators) {
      it->SeekToFirst();
    }
    ASSERT_EQ(IterStatus(iterators[0]), "a->b");
    ASSERT_EQ(IterStatus(iterators[1]), "b->a");
    ASSERT_EQ(IterStatus(iterators[2]), "c->m");

    ASSERT_OK(Put(1, "x", "x"));

    for (auto it : iterators) {
      it->Next();
    }

    ASSERT_EQ(IterStatus(iterators[0]), "(invalid) OK");
    if (iter == 0) {
      // no tailing
      ASSERT_EQ(IterStatus(iterators[1]), "(invalid) OK");
    } else {
      // tailing
      ASSERT_EQ(IterStatus(iterators[1]), "x->x");
    }
    ASSERT_EQ(IterStatus(iterators[2]), "v->t");

    for (auto it : iterators) {
      delete it;
    }
    Destroy();
  }
}

TEST_F(ColumnFamilyTest, ReadOnlyDBTest) {
  Open();
  CreateColumnFamiliesAndReopen({"one", "two", "three", "four"});
  ASSERT_OK(Put(0, "a", "b"));
  ASSERT_OK(Put(1, "foo", "bla"));
  ASSERT_OK(Put(2, "foo", "blabla"));
  ASSERT_OK(Put(3, "foo", "blablabla"));
  ASSERT_OK(Put(4, "foo", "blablablabla"));

  DropColumnFamilies({2});
  Close();
  // open only a subset of column families
  AssertOpenReadOnly({"default", "one", "four"});
  ASSERT_EQ("NOT_FOUND", Get(0, "foo"));
  ASSERT_EQ("bla", Get(1, "foo"));
  ASSERT_EQ("blablablabla", Get(2, "foo"));


  // test newiterators
  {
    std::vector<Iterator*> iterators;
    ASSERT_OK(db_->NewIterators(ReadOptions(), handles_, &iterators));
    for (auto it : iterators) {
      it->SeekToFirst();
    }
    ASSERT_EQ(IterStatus(iterators[0]), "a->b");
    ASSERT_EQ(IterStatus(iterators[1]), "foo->bla");
    ASSERT_EQ(IterStatus(iterators[2]), "foo->blablablabla");
    for (auto it : iterators) {
      it->Next();
    }
    ASSERT_EQ(IterStatus(iterators[0]), "(invalid) OK");
    ASSERT_EQ(IterStatus(iterators[1]), "(invalid) OK");
    ASSERT_EQ(IterStatus(iterators[2]), "(invalid) OK");

    for (auto it : iterators) {
      delete it;
    }
  }

  Close();
  // can't open dropped column family
  Status s = OpenReadOnly({"default", "one", "two"});
  ASSERT_TRUE(!s.ok());

  // Can't open without specifying default column family
  s = OpenReadOnly({"one", "four"});
  ASSERT_TRUE(!s.ok());
}

TEST_F(ColumnFamilyTest, DontRollEmptyLogs) {
  Open();
  CreateColumnFamiliesAndReopen({"one", "two", "three", "four"});

  for (size_t i = 0; i < handles_.size(); ++i) {
    PutRandomData(static_cast<int>(i), 10, 100);
  }
  int num_writable_file_start = env_->GetNumberOfNewWritableFileCalls();
  // this will trigger the flushes
  for (int i = 0; i <= 4; ++i) {
    ASSERT_OK(Flush(i));
  }

  for (int i = 0; i < 4; ++i) {
    WaitForFlush(i);
  }
  int total_new_writable_files =
      env_->GetNumberOfNewWritableFileCalls() - num_writable_file_start;
  ASSERT_EQ(static_cast<size_t>(total_new_writable_files), handles_.size() * 2 + 1);
  Close();
}

TEST_F(ColumnFamilyTest, FlushStaleColumnFamilies) {
  Open();
  CreateColumnFamilies({"one", "two"});
  ColumnFamilyOptions default_cf, one, two;
  default_cf.write_buffer_size = 100000;  // small write buffer size
  default_cf.arena_block_size = 4096;
  default_cf.disable_auto_compactions = true;
  one.disable_auto_compactions = true;
  two.disable_auto_compactions = true;
  db_options_.max_total_wal_size = 210000;

  Reopen({default_cf, one, two});

  PutRandomData(2, 1, 10);  // 10 bytes
  for (int i = 0; i < 2; ++i) {
    PutRandomData(0, 100, 1000);  // flush
    WaitForFlush(0);

    AssertCountLiveFiles(i + 1);
  }
  // third flush. now, CF [two] should be detected as stale and flushed
  // column family 1 should not be flushed since it's empty
  PutRandomData(0, 100, 1000);  // flush
  WaitForFlush(0);
  WaitForFlush(2);
  // 3 files for default column families, 1 file for column family [two], zero
  // files for column family [one], because it's empty
  AssertCountLiveFiles(4);
  Close();
}

TEST_F(ColumnFamilyTest, CreateMissingColumnFamilies) {
  Status s = TryOpen({"one", "two"});
  ASSERT_TRUE(!s.ok());
  db_options_.create_missing_column_families = true;
  s = TryOpen({"default", "one", "two"});
  ASSERT_TRUE(s.ok());
  Close();
}

TEST_F(ColumnFamilyTest, SanitizeOptions) {
  DBOptions db_options;
  for (int s = kCompactionStyleLevel; s <= kCompactionStyleUniversal; ++s) {
    for (int l = 0; l <= 2; l++) {
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
          for (int k = 1; k <= 3; k++) {
            ColumnFamilyOptions original;
            original.compaction_style = static_cast<CompactionStyle>(s);
            original.num_levels = l;
            original.level0_stop_writes_trigger = i;
            original.level0_slowdown_writes_trigger = j;
            original.level0_file_num_compaction_trigger = k;
            original.write_buffer_size =
                l * 4 * 1024 * 1024 + i * 1024 * 1024 + j * 1024 + k;

            ColumnFamilyOptions result =
                SanitizeOptions(db_options, nullptr, original);
            ASSERT_TRUE(result.level0_stop_writes_trigger >=
                        result.level0_slowdown_writes_trigger);
            ASSERT_TRUE(result.level0_slowdown_writes_trigger >=
                        result.level0_file_num_compaction_trigger);
            ASSERT_TRUE(result.level0_file_num_compaction_trigger ==
                        original.level0_file_num_compaction_trigger);
            if (s == kCompactionStyleLevel) {
              ASSERT_GE(result.num_levels, 2);
            } else {
              ASSERT_GE(result.num_levels, 1);
              if (original.num_levels >= 1) {
                ASSERT_EQ(result.num_levels, original.num_levels);
              }
            }

            // Make sure Sanitize options sets arena_block_size default of 1MB or to 1/8 of
            // the write_buffer_size, rounded up to a multiple of 4k.
            size_t expected_arena_block_size = l * 4 * 1024 * 1024 / 8 + i * 1024 * 1024 / 8;
            if (j + k != 0) {
              // not a multiple of 4k, round up 4k
              expected_arena_block_size += 4 * 1024;
            }
            expected_arena_block_size = std::min(
                static_cast<size_t>(FLAGS_memstore_arena_size_kb << 10), expected_arena_block_size);
            ASSERT_EQ(expected_arena_block_size, result.arena_block_size);
          }
        }
      }
    }
  }
}

TEST_F(ColumnFamilyTest, ReadDroppedColumnFamily) {
  // iter 0 -- drop CF, don't reopen
  // iter 1 -- delete CF, reopen
  for (int iter = 0; iter < 2; ++iter) {
    db_options_.create_missing_column_families = true;
    db_options_.max_open_files = 20;
    // delete obsolete files always
    db_options_.delete_obsolete_files_period_micros = 0;
    Open({"default", "one", "two"});
    ColumnFamilyOptions options;
    options.level0_file_num_compaction_trigger = 100;
    options.level0_slowdown_writes_trigger = 200;
    options.level0_stop_writes_trigger = 200;
    options.write_buffer_size = 100000;  // small write buffer size
    Reopen({options, options, options});

    // 1MB should create ~10 files for each CF
    int kKeysNum = 10000;
    PutRandomData(0, kKeysNum, 100);
    PutRandomData(1, kKeysNum, 100);
    PutRandomData(2, kKeysNum, 100);

    {
      std::unique_ptr<Iterator> iterator(
          db_->NewIterator(ReadOptions(), handles_[2]));
      iterator->SeekToFirst();

      if (iter == 0) {
        // Drop CF two
        ASSERT_OK(db_->DropColumnFamily(handles_[2]));
      } else {
        // delete CF two
        delete handles_[2];
        handles_[2] = nullptr;
      }
      // Make sure iterator created can still be used.
      int count = 0;
      for (; ASSERT_RESULT(iterator->CheckedValid()); iterator->Next()) {
        ++count;
      }
      ASSERT_EQ(count, kKeysNum);
    }

    // Add bunch more data to other CFs
    PutRandomData(0, kKeysNum, 100);
    PutRandomData(1, kKeysNum, 100);

    if (iter == 1) {
      Reopen();
    }

    // Since we didn't delete CF handle, RocksDB's contract guarantees that
    // we're still able to read dropped CF
    for (int i = 0; i < 3; ++i) {
      std::unique_ptr<Iterator> iterator(
          db_->NewIterator(ReadOptions(), handles_[i]));
      int count = 0;
      for (iterator->SeekToFirst(); ASSERT_RESULT(iterator->CheckedValid()); iterator->Next()) {
        ++count;
      }
      ASSERT_EQ(count, kKeysNum * ((i == 2) ? 1 : 2));
    }

    Close();
    Destroy();
  }
}

TEST_F(ColumnFamilyTest, FlushAndDropRaceCondition) {
  db_options_.create_missing_column_families = true;
  Open({"default", "one"});
  ColumnFamilyOptions options;
  options.level0_file_num_compaction_trigger = 100;
  options.level0_slowdown_writes_trigger = 200;
  options.level0_stop_writes_trigger = 200;
  options.max_write_buffer_number = 20;
  options.write_buffer_size = 100000;  // small write buffer size
  Reopen({options, options});

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"VersionSet::LogAndApply::ColumnFamilyDrop:0",
        "FlushJob::WriteLevel0Table"},
       {"VersionSet::LogAndApply::ColumnFamilyDrop:1",
        "FlushJob::InstallResults"},
       {"FlushJob::InstallResults",
        "VersionSet::LogAndApply::ColumnFamilyDrop:2"}});

  yb::SyncPoint::GetInstance()->EnableProcessing();
  test::SleepingBackgroundTask sleeping_task;

  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task,
                 Env::Priority::HIGH);

  // 1MB should create ~10 files for each CF
  int kKeysNum = 10000;
  PutRandomData(1, kKeysNum, 100);

  std::vector<std::thread> threads;
  threads.emplace_back([&] { ASSERT_OK(db_->DropColumnFamily(handles_[1])); });

  sleeping_task.WakeUp();
  sleeping_task.WaitUntilDone();
  sleeping_task.Reset();
  // now we sleep again. this is just so we're certain that flush job finished
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task,
                 Env::Priority::HIGH);
  sleeping_task.WakeUp();
  sleeping_task.WaitUntilDone();

  {
    // Since we didn't delete CF handle, RocksDB's contract guarantees that
    // we're still able to read dropped CF
    std::unique_ptr<Iterator> iterator(
        db_->NewIterator(ReadOptions(), handles_[1]));
    int count = 0;
    for (iterator->SeekToFirst(); ASSERT_RESULT(iterator->CheckedValid()); iterator->Next()) {
      ++count;
    }
    ASSERT_EQ(count, kKeysNum);
  }
  for (auto& t : threads) {
    t.join();
  }

  Close();
  Destroy();
}

namespace {
std::atomic<int> test_stage(0);
const int kMainThreadStartPersistingOptionsFile = 1;
const int kChildThreadFinishDroppingColumnFamily = 2;
const int kChildThreadWaitingMainThreadPersistOptions = 3;
void DropSingleColumnFamily(ColumnFamilyTest* cf_test, int cf_id,
                            std::vector<Comparator*>* comparators) {
  while (test_stage < kMainThreadStartPersistingOptionsFile) {
    Env::Default()->SleepForMicroseconds(100);
  }
  cf_test->DropColumnFamilies({cf_id});
  if ((*comparators)[cf_id]) {
    delete (*comparators)[cf_id];
    (*comparators)[cf_id] = nullptr;
  }
  test_stage = kChildThreadFinishDroppingColumnFamily;
}
}  // namespace

TEST_F(ColumnFamilyTest, CreateAndDropRace) {
  const int kCfCount = 5;
  std::vector<ColumnFamilyOptions> cf_opts;
  std::vector<Comparator*> comparators;
  for (int i = 0; i < kCfCount; ++i) {
    cf_opts.emplace_back();
    comparators.push_back(new test::SimpleSuffixReverseComparator());
    cf_opts.back().comparator = comparators.back();
  }
  db_options_.create_if_missing = true;
  db_options_.create_missing_column_families = true;

  auto main_thread_id = std::this_thread::get_id();

  yb::SyncPoint::GetInstance()->SetCallBack("PersistRocksDBOptions:start",
                                                 [&](void* arg) {
    auto current_thread_id = std::this_thread::get_id();
    // If it's the main thread hitting this sync-point, then it
    // will be blocked until some other thread update the test_stage.
    if (main_thread_id == current_thread_id) {
      test_stage = kMainThreadStartPersistingOptionsFile;
      while (test_stage < kChildThreadFinishDroppingColumnFamily) {
        Env::Default()->SleepForMicroseconds(100);
      }
    }
  });

  yb::SyncPoint::GetInstance()->SetCallBack(
      "WriteThread::EnterUnbatched:Wait", [&](void* arg) {
        // This means a thread doing DropColumnFamily() is waiting for
        // other thread to finish persisting options.
        // In such case, we update the test_stage to unblock the main thread.
        test_stage = kChildThreadWaitingMainThreadPersistOptions;

        // Note that based on the test setting, this must not be the
        // main thread.
        ASSERT_NE(main_thread_id, std::this_thread::get_id());
      });

  // Create a database with four column families
  Open({"default", "one", "two", "three"},
       {cf_opts[0], cf_opts[1], cf_opts[2], cf_opts[3]});

  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Start a thread that will drop the first column family
  // and its comparator
  std::thread drop_cf_thread(DropSingleColumnFamily, this, 1, &comparators);

  DropColumnFamilies({2});

  drop_cf_thread.join();
  Close();
  Destroy();
  for (auto* comparator : comparators) {
    if (comparator) {
      delete comparator;
    }
  }
}

TEST_F(ColumnFamilyTest, WriteStallSingleColumnFamily) {
  const uint64_t kBaseRate = 810000u;
  db_options_.delayed_write_rate = kBaseRate;
  db_options_.base_background_compactions = 2;
  db_options_.max_background_compactions = 6;

  Open({"default"});
  ColumnFamilyData* cfd =
      static_cast<ColumnFamilyHandleImpl*>(db_->DefaultColumnFamily())->cfd();

  VersionStorageInfo* vstorage = cfd->current()->storage_info();

  MutableCFOptions mutable_cf_options(
      Options(db_options_, column_family_options_),
      ImmutableCFOptions(Options(db_options_, column_family_options_)));

  mutable_cf_options.level0_slowdown_writes_trigger = 20;
  mutable_cf_options.level0_stop_writes_trigger = 10000;
  mutable_cf_options.soft_pending_compaction_bytes_limit = 200;
  mutable_cf_options.hard_pending_compaction_bytes_limit = 2000;

  vstorage->TEST_set_estimated_compaction_needed_bytes(50);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage->TEST_set_estimated_compaction_needed_bytes(201);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(400);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(500);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(450);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(205);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(202);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(201);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(198);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage->TEST_set_estimated_compaction_needed_bytes(399);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(599);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(2001);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(3001);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage->TEST_set_estimated_compaction_needed_bytes(390);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(100);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage->set_l0_delay_trigger_count(100);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(101);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->set_l0_delay_trigger_count(0);
  vstorage->TEST_set_estimated_compaction_needed_bytes(300);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->set_l0_delay_trigger_count(101);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2 / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(200);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->set_l0_delay_trigger_count(0);
  vstorage->TEST_set_estimated_compaction_needed_bytes(0);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  mutable_cf_options.disable_auto_compactions = true;
  dbfull()->TEST_write_controler().set_delayed_write_rate(kBaseRate);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage->set_l0_delay_trigger_count(50);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->set_l0_delay_trigger_count(60);
  vstorage->TEST_set_estimated_compaction_needed_bytes(300);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->set_l0_delay_trigger_count(70);
  vstorage->TEST_set_estimated_compaction_needed_bytes(500);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  mutable_cf_options.disable_auto_compactions = false;
  vstorage->set_l0_delay_trigger_count(71);
  vstorage->TEST_set_estimated_compaction_needed_bytes(501);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());
}

TEST_F(ColumnFamilyTest, CompactionSpeedupSingleColumnFamily) {
  db_options_.base_background_compactions = 2;
  db_options_.max_background_compactions = 6;
  Open({"default"});
  ColumnFamilyData* cfd =
      static_cast<ColumnFamilyHandleImpl*>(db_->DefaultColumnFamily())->cfd();

  VersionStorageInfo* vstorage = cfd->current()->storage_info();

  MutableCFOptions mutable_cf_options(
      Options(db_options_, column_family_options_),
      ImmutableCFOptions(Options(db_options_, column_family_options_)));

  // Speed up threshold = min(4 * 2, 4 + (36 - 4)/4) = 8
  mutable_cf_options.level0_file_num_compaction_trigger = 4;
  mutable_cf_options.level0_slowdown_writes_trigger = 36;
  mutable_cf_options.level0_stop_writes_trigger = 50;
  // Speedup threshold = 200 / 4 = 50
  mutable_cf_options.soft_pending_compaction_bytes_limit = 200;
  mutable_cf_options.hard_pending_compaction_bytes_limit = 2000;

  vstorage->TEST_set_estimated_compaction_needed_bytes(40);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(50);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(300);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(45);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(7);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(9);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(6);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  // Speed up threshold = min(4 * 2, 4 + (12 - 4)/4) = 6
  mutable_cf_options.level0_file_num_compaction_trigger = 4;
  mutable_cf_options.level0_slowdown_writes_trigger = 16;
  mutable_cf_options.level0_stop_writes_trigger = 30;

  vstorage->set_l0_delay_trigger_count(5);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(7);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(3);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());
}

TEST_F(ColumnFamilyTest, WriteStallTwoColumnFamilies) {
  const uint64_t kBaseRate = 810000u;
  db_options_.delayed_write_rate = kBaseRate;
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyData* cfd =
      static_cast<ColumnFamilyHandleImpl*>(db_->DefaultColumnFamily())->cfd();
  VersionStorageInfo* vstorage = cfd->current()->storage_info();

  ColumnFamilyData* cfd1 =
      static_cast<ColumnFamilyHandleImpl*>(handles_[1])->cfd();
  VersionStorageInfo* vstorage1 = cfd1->current()->storage_info();

  MutableCFOptions mutable_cf_options(
      Options(db_options_, column_family_options_),
      ImmutableCFOptions(Options(db_options_, column_family_options_)));
  mutable_cf_options.level0_slowdown_writes_trigger = 20;
  mutable_cf_options.level0_stop_writes_trigger = 10000;
  mutable_cf_options.soft_pending_compaction_bytes_limit = 200;
  mutable_cf_options.hard_pending_compaction_bytes_limit = 2000;

  MutableCFOptions mutable_cf_options1 = mutable_cf_options;
  mutable_cf_options1.soft_pending_compaction_bytes_limit = 500;

  vstorage->TEST_set_estimated_compaction_needed_bytes(50);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(201);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(600);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(70);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate, dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(800);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(300);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(700);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage->TEST_set_estimated_compaction_needed_bytes(500);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2 / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(600);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().IsStopped());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());
  ASSERT_EQ(kBaseRate / 1.2,
            dbfull()->TEST_write_controler().delayed_write_rate());
}

TEST_F(ColumnFamilyTest, CompactionSpeedupTwoColumnFamilies) {
  db_options_.base_background_compactions = 2;
  db_options_.max_background_compactions = 6;
  column_family_options_.soft_pending_compaction_bytes_limit = 200;
  column_family_options_.hard_pending_compaction_bytes_limit = 2000;
  Open();
  CreateColumnFamilies({"one"});
  ColumnFamilyData* cfd =
      static_cast<ColumnFamilyHandleImpl*>(db_->DefaultColumnFamily())->cfd();
  VersionStorageInfo* vstorage = cfd->current()->storage_info();

  ColumnFamilyData* cfd1 =
      static_cast<ColumnFamilyHandleImpl*>(handles_[1])->cfd();
  VersionStorageInfo* vstorage1 = cfd1->current()->storage_info();

  MutableCFOptions mutable_cf_options(
      Options(db_options_, column_family_options_),
      ImmutableCFOptions(Options(db_options_, column_family_options_)));
  // Speed up threshold = min(4 * 2, 4 + (36 - 4)/4) = 8
  mutable_cf_options.level0_file_num_compaction_trigger = 4;
  mutable_cf_options.level0_slowdown_writes_trigger = 36;
  mutable_cf_options.level0_stop_writes_trigger = 30;
  // Speedup threshold = 200 / 4 = 50
  mutable_cf_options.soft_pending_compaction_bytes_limit = 200;
  mutable_cf_options.hard_pending_compaction_bytes_limit = 2000;

  MutableCFOptions mutable_cf_options1 = mutable_cf_options;
  mutable_cf_options1.level0_slowdown_writes_trigger = 16;

  vstorage->TEST_set_estimated_compaction_needed_bytes(40);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(60);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(30);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(70);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->TEST_set_estimated_compaction_needed_bytes(20);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage1->TEST_set_estimated_compaction_needed_bytes(3);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(9);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage1->set_l0_delay_trigger_count(2);
  cfd1->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(6, dbfull()->BGCompactionsAllowed());

  vstorage->set_l0_delay_trigger_count(0);
  cfd->RecalculateWriteStallConditions(mutable_cf_options);
  ASSERT_EQ(2, dbfull()->BGCompactionsAllowed());
}

TEST_F(ColumnFamilyTest, LogSyncConflictFlush) {
  Open();
  CreateColumnFamiliesAndReopen({"one", "two"});

  ASSERT_OK(Put(0, "", ""));
  ASSERT_OK(Put(1, "foo", "bar"));

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"DBImpl::SyncWAL:BeforeMarkLogsSynced:1",
        "ColumnFamilyTest::LogSyncConflictFlush:1"},
       {"ColumnFamilyTest::LogSyncConflictFlush:2",
        "DBImpl::SyncWAL:BeforeMarkLogsSynced:2"}});

  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::thread thread([&] { ASSERT_OK(db_->SyncWAL()); });

  TEST_SYNC_POINT("ColumnFamilyTest::LogSyncConflictFlush:1");
  ASSERT_OK(Flush(1));
  ASSERT_OK(Put(1, "foo", "bar"));
  ASSERT_OK(Flush(1));

  TEST_SYNC_POINT("ColumnFamilyTest::LogSyncConflictFlush:2");

  thread.join();

  yb::SyncPoint::GetInstance()->DisableProcessing();
  Close();
}

TEST_F(ColumnFamilyTest, GetColumnFamiliesOptions) {
  column_family_options_.arena_block_size = 4096;

  // Source options
  ColumnFamilyOptions src_d = column_family_options_;
  ColumnFamilyOptions src_1;
  ColumnFamilyOptions src_2;
  ColumnFamilyOptions src_3;
  src_1.arena_block_size = 2 * column_family_options_.arena_block_size;
  src_2.arena_block_size = 3 * column_family_options_.arena_block_size;
  src_3.arena_block_size = 4 * column_family_options_.arena_block_size;

  using OptionsRef  = std::reference_wrapper<ColumnFamilyOptions>;
  using Descriptors = std::map<std::string, OptionsRef>;

  auto compare_with = [this](Descriptors src) {
    std::vector<std::string> cf_names;
    std::vector<ColumnFamilyOptions> cf_options;
    db_->GetColumnFamiliesOptions(&cf_names, &cf_options);

    // Comapre sizes
    ASSERT_EQ(cf_names.size(), cf_options.size());
    ASSERT_EQ(cf_names.size(), src.size());

    // Keep sorted order for the case descriptors are stored in an unpredictable way
    Descriptors dst;
    for (size_t i = 0; i < cf_names.size(); ++i) {
      dst.insert(std::make_pair(cf_names[i], std::ref(cf_options[i])));
    }

    // Compare options
    for (auto it1 = src.begin(), it2 = dst.begin();
         it1 != src.end() && it2 != dst.end(); ++it1, ++it2) {
      ASSERT_EQ(it1->first, it2->first);
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(it1->second, it2->second));
    }
  };

  Open();
  compare_with({{"default", std::ref(src_d)}});

  CreateColumnFamilies({"1", "2"}, {src_1, src_2});
  compare_with({{"default", std::ref(src_d)}, {"1", std::ref(src_1)}, {"2", std::ref(src_2)}});

  DropColumnFamilies({1});
  compare_with({{"default", std::ref(src_d)}, {"2", std::ref(src_2)}});

  CreateColumnFamilies({"3"}, {src_3});
  compare_with({{"default", std::ref(src_d)}, {"3", std::ref(src_3)}, {"2", std::ref(src_2)}});
  Close();

  src_3.arena_block_size += 1024;
  Open({"3", "2", "default"}, {src_3, src_2, src_d});
  compare_with({{"default", std::ref(src_d)}, {"2", std::ref(src_2)}, {"3", std::ref(src_3)}});

  DropColumnFamilies({0});
  compare_with({{"default", std::ref(src_d)}, {"2", std::ref(src_2)}});
  Close();

  db_options_.create_missing_column_families = true;
  src_3.arena_block_size -= 1024;
  Open({"1", "default", "2", "3"}, {src_1, src_d, src_2, src_3});
  compare_with({{"default", std::ref(src_d)}, {"2", std::ref(src_2)},
                {"3", std::ref(src_3)}, {"1", std::ref(src_1)}});

  DropColumnFamilies({0, 2});
  compare_with({{"3", std::ref(src_3)}, {"default", std::ref(src_d)}});

  DropColumnFamilies({3});
  compare_with({{"default", std::ref(src_d)}});
  Close();
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
