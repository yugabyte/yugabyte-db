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

#include "yb/rocksdb/db/db_test_util.h"
#include "yb/rocksdb/port/stack_trace.h"

#include "yb/util/sync_point.h"

namespace rocksdb {
class DBWALTest : public DBTestBase {
 public:
  DBWALTest() : DBTestBase("/db_wal_test") {}
};

TEST_F(DBWALTest, WAL) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    WriteOptions writeOpt = WriteOptions();
    writeOpt.disableWAL = true;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v1"));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v1"));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_EQ("v1", Get(1, "bar"));

    writeOpt.disableWAL = false;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v2"));
    writeOpt.disableWAL = true;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v2"));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    // Both value's should be present.
    ASSERT_EQ("v2", Get(1, "bar"));
    ASSERT_EQ("v2", Get(1, "foo"));

    writeOpt.disableWAL = true;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v3"));
    writeOpt.disableWAL = false;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v3"));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    // again both values should be present.
    ASSERT_EQ("v3", Get(1, "foo"));
    ASSERT_EQ("v3", Get(1, "bar"));
  } while (ChangeCompactOptions());
}

TEST_F(DBWALTest, RollLog) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Put(1, "baz", "v5"));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    for (int i = 0; i < 10; i++) {
      ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    }
    ASSERT_OK(Put(1, "foo", "v4"));
    for (int i = 0; i < 10; i++) {
      ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    }
  } while (ChangeOptions());
}

TEST_F(DBWALTest, SyncWALNotBlockWrite) {
  Options options = CurrentOptions();
  options.max_write_buffer_number = 4;
  DestroyAndReopen(options);

  ASSERT_OK(Put("foo1", "bar1"));
  ASSERT_OK(Put("foo5", "bar5"));

  yb::SyncPoint::GetInstance()->LoadDependency({
      {"WritableFileWriter::SyncWithoutFlush:1",
       "DBWALTest::SyncWALNotBlockWrite:1"},
      {"DBWALTest::SyncWALNotBlockWrite:2",
       "WritableFileWriter::SyncWithoutFlush:2"},
  });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::thread thread([&]() { ASSERT_OK(db_->SyncWAL()); });

  TEST_SYNC_POINT("DBWALTest::SyncWALNotBlockWrite:1");
  ASSERT_OK(Put("foo2", "bar2"));
  ASSERT_OK(Put("foo3", "bar3"));
  FlushOptions fo;
  fo.wait = false;
  ASSERT_OK(db_->Flush(fo));
  ASSERT_OK(Put("foo4", "bar4"));

  TEST_SYNC_POINT("DBWALTest::SyncWALNotBlockWrite:2");

  thread.join();

  ASSERT_EQ(Get("foo1"), "bar1");
  ASSERT_EQ(Get("foo2"), "bar2");
  ASSERT_EQ(Get("foo3"), "bar3");
  ASSERT_EQ(Get("foo4"), "bar4");
  ASSERT_EQ(Get("foo5"), "bar5");
  yb::SyncPoint::GetInstance()->DisableProcessing();
}

TEST_F(DBWALTest, SyncWALNotWaitWrite) {
  ASSERT_OK(Put("foo1", "bar1"));
  ASSERT_OK(Put("foo3", "bar3"));

  yb::SyncPoint::GetInstance()->LoadDependency({
      {"SpecialEnv::WalFile::Append:1", "DBWALTest::SyncWALNotWaitWrite:1"},
      {"DBWALTest::SyncWALNotWaitWrite:2", "SpecialEnv::WalFile::Append:2"},
  });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::thread thread([&]() { ASSERT_OK(Put("foo2", "bar2")); });
  TEST_SYNC_POINT("DBWALTest::SyncWALNotWaitWrite:1");
  ASSERT_OK(db_->SyncWAL());
  TEST_SYNC_POINT("DBWALTest::SyncWALNotWaitWrite:2");

  thread.join();

  ASSERT_EQ(Get("foo1"), "bar1");
  ASSERT_EQ(Get("foo2"), "bar2");
  yb::SyncPoint::GetInstance()->DisableProcessing();
}
}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
