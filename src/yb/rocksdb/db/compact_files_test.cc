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


#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_util.h"

namespace rocksdb {

class CompactFilesTest : public RocksDBTest {
 public:
  CompactFilesTest() {
    env_ = Env::Default();
    db_name_ = test::TmpDir(env_) + "/compact_files_test";
  }

  std::string db_name_;
  Env* env_;
};

TEST_F(CompactFilesTest, ObsoleteFiles) {
  Options options;
  // to trigger compaction more easily
  const int kWriteBufferSize = 10000;
  options.create_if_missing = true;
  // Disable RocksDB background compaction.
  options.compaction_style = kCompactionStyleNone;
  // Small slowdown and stop trigger for experimental purpose.
  options.level0_slowdown_writes_trigger = 20;
  options.level0_stop_writes_trigger = 20;
  options.write_buffer_size = kWriteBufferSize;
  options.max_write_buffer_number = 2;
  options.compression = kNoCompression;

  // Add listener
  auto* collector = new test::FlushedFileCollector();
  options.listeners.emplace_back(collector);

  DB* db = nullptr;
  ASSERT_OK(DestroyDB(db_name_, options));
  Status s = DB::Open(options, db_name_, &db);
  assert(s.ok());
  assert(db);

  // create couple files
  for (int i = 1000; i < 2000; ++i) {
    ASSERT_OK(db->Put(WriteOptions(), ToString(i),
                      std::string(kWriteBufferSize / 10, 'a' + (i % 26))));
  }

  auto l0_files = collector->GetFlushedFiles();
  ASSERT_OK(db->CompactFiles(CompactionOptions(), l0_files, 1));

  // verify all compaction input files are deleted
  for (auto fname : l0_files) {
    ASSERT_TRUE(env_->FileExists(fname).IsNotFound());
  }
  delete db;
}

TEST_F(CompactFilesTest, CapturingPendingFiles) {
  Options options;
  options.create_if_missing = true;
  // Disable RocksDB background compaction.
  options.compaction_style = kCompactionStyleNone;
  // Always do full scans for obsolete files (needed to reproduce the issue).
  options.delete_obsolete_files_period_micros = 0;

  // Add listener.
  auto* collector = new test::FlushedFileCollector();
  options.listeners.emplace_back(collector);

  DB* db = nullptr;
  ASSERT_OK(DestroyDB(db_name_, options));
  Status s = DB::Open(options, db_name_, &db);
  assert(s.ok());
  assert(db);

  // Create 5 files.
  for (int i = 0; i < 5; ++i) {
    ASSERT_OK(db->Put(WriteOptions(), "key" + ToString(i), "value"));
    ASSERT_OK(db->Flush(FlushOptions()));
  }

  auto l0_files = collector->GetFlushedFiles();
  EXPECT_EQ(5, l0_files.size());

  yb::SyncPoint::GetInstance()->LoadDependency({
      {"CompactFilesImpl:2", "CompactFilesTest.CapturingPendingFiles:0"},
      {"CompactFilesTest.CapturingPendingFiles:1", "CompactFilesImpl:3"},
  });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Start compacting files.
  std::thread compaction_thread(
      [&] { EXPECT_OK(db->CompactFiles(CompactionOptions(), l0_files, 1)); });

  // In the meantime flush another file.
  TEST_SYNC_POINT("CompactFilesTest.CapturingPendingFiles:0");
  ASSERT_OK(db->Put(WriteOptions(), "key5", "value"));
  ASSERT_OK(db->Flush(FlushOptions()));
  TEST_SYNC_POINT("CompactFilesTest.CapturingPendingFiles:1");

  compaction_thread.join();

  yb::SyncPoint::GetInstance()->DisableProcessing();

  delete db;

  // Make sure we can reopen the DB.
  s = DB::Open(options, db_name_, &db);
  ASSERT_TRUE(s.ok());
  assert(db);
  delete db;
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
