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

// Introduction of SyncPoint effectively disabled building and running this test
// in Release build.
// which is a pity, it is a good test
#include <fcntl.h>
#include <algorithm>
#include <thread>
#include <unordered_set>
#include <utility>
#ifndef OS_WIN
#include <unistd.h>
#endif
#ifdef OS_SOLARIS
#include <alloca.h>
#endif

#include "yb/rocksdb/db/db_test_util.h"
#include "yb/rocksdb/port/stack_trace.h"
#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/experimental.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/perf_context.h"
#include "yb/rocksdb/perf_level.h"
#include "yb/rocksdb/snapshot.h"
#include "yb/rocksdb/sst_file_writer.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/wal_filter.h"
#include "yb/rocksdb/utilities/write_batch_with_index.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/util/rate_limiter.h"

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/format.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/random_util.h"
#include "yb/util/slice.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

using std::unique_ptr;
using std::shared_ptr;

DECLARE_bool(use_priority_thread_pool_for_compactions);
DECLARE_bool(use_priority_thread_pool_for_flushes);
DECLARE_bool(TEST_allow_table_option_compressed_block_cache);

namespace rocksdb {

// A helper function that ensures the table properties returned in
// `GetPropertiesOfAllTablesTest` is correct.
// This test assumes entries size is different for each of the tables.
namespace {

uint64_t GetNumberOfSstFilesForColumnFamily(DB* db,
                                            std::string column_family_name) {
  std::vector<LiveFileMetaData> metadata;
  db->GetLiveFilesMetaData(&metadata);
  uint64_t result = 0;
  for (auto& fileMetadata : metadata) {
    result += (fileMetadata.column_family_name == column_family_name);
  }
  return result;
}

}  // namespace

class DBTest : public DBTestBase {
 public:
  DBTest() : DBTestBase("/db_test") {}
};

class DBTestWithParam
    : public DBTest,
      public testing::WithParamInterface<std::tuple<uint32_t, bool>> {
 public:
  DBTestWithParam() {
    max_subcompactions_ = std::get<0>(GetParam());
    exclusive_manual_compaction_ = std::get<1>(GetParam());
  }

  // Required if inheriting from testing::WithParamInterface<>
  static void SetUpTestCase() {}
  static void TearDownTestCase() {}

  uint32_t max_subcompactions_;
  bool exclusive_manual_compaction_;
};

TEST_F(DBTest, MockEnvTest) {
  unique_ptr<MockEnv> env{new MockEnv(Env::Default())};
  Options options;
  options.create_if_missing = true;
  options.env = env.get();
  DB* db;

  const Slice keys[] = {Slice("aaa"), Slice("bbb"), Slice("ccc")};
  const Slice vals[] = {Slice("foo"), Slice("bar"), Slice("baz")};

  ASSERT_OK(DB::Open(options, "/dir/db", &db));
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_OK(db->Put(WriteOptions(), keys[i], vals[i]));
  }

  for (size_t i = 0; i < 3; ++i) {
    std::string res;
    ASSERT_OK(db->Get(ReadOptions(), keys[i], &res));
    ASSERT_TRUE(res == vals[i]);
  }

  Iterator* iterator = db->NewIterator(ReadOptions());
  iterator->SeekToFirst();
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(ASSERT_RESULT(iterator->CheckedValid()));
    ASSERT_TRUE(keys[i] == iterator->key());
    ASSERT_TRUE(vals[i] == iterator->value());
    iterator->Next();
  }
  ASSERT_TRUE(!ASSERT_RESULT(iterator->CheckedValid()));
  delete iterator;

  DBImpl* dbi = reinterpret_cast<DBImpl*>(db);
  ASSERT_OK(dbi->TEST_FlushMemTable());

  for (size_t i = 0; i < 3; ++i) {
    std::string res;
    ASSERT_OK(db->Get(ReadOptions(), keys[i], &res));
    ASSERT_TRUE(res == vals[i]);
  }

  delete db;
}

TEST_F(DBTest, MemEnvTest) {
  unique_ptr<Env> env{NewMemEnv(Env::Default())};
  Options options;
  options.create_if_missing = true;
  options.env = env.get();
  DB* db;

  const Slice keys[] = {Slice("aaa"), Slice("bbb"), Slice("ccc")};
  const Slice vals[] = {Slice("foo"), Slice("bar"), Slice("baz")};

  ASSERT_OK(DB::Open(options, "/dir/db", &db));
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_OK(db->Put(WriteOptions(), keys[i], vals[i]));
  }

  for (size_t i = 0; i < 3; ++i) {
    std::string res;
    ASSERT_OK(db->Get(ReadOptions(), keys[i], &res));
    ASSERT_TRUE(res == vals[i]);
  }

  Iterator* iterator = db->NewIterator(ReadOptions());
  iterator->SeekToFirst();
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(ASSERT_RESULT(iterator->CheckedValid()));
    ASSERT_TRUE(keys[i] == iterator->key());
    ASSERT_TRUE(vals[i] == iterator->value());
    iterator->Next();
  }
  ASSERT_TRUE(!ASSERT_RESULT(iterator->CheckedValid()));
  delete iterator;

  DBImpl* dbi = reinterpret_cast<DBImpl*>(db);
  ASSERT_OK(dbi->TEST_FlushMemTable());

  for (size_t i = 0; i < 3; ++i) {
    std::string res;
    ASSERT_OK(db->Get(ReadOptions(), keys[i], &res));
    ASSERT_TRUE(res == vals[i]);
  }

  delete db;

  options.create_if_missing = false;
  ASSERT_OK(DB::Open(options, "/dir/db", &db));
  for (size_t i = 0; i < 3; ++i) {
    std::string res;
    ASSERT_OK(db->Get(ReadOptions(), keys[i], &res));
    ASSERT_TRUE(res == vals[i]);
  }
  delete db;
}

TEST_F(DBTest, WriteEmptyBatch) {
  Options options;
  options.env = env_;
  options.write_buffer_size = 100000;
  options = CurrentOptions(options);
  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "foo", "bar"));
  WriteOptions wo;
  wo.sync = true;
  wo.disableWAL = false;
  WriteBatch empty_batch;
  ASSERT_OK(dbfull()->Write(wo, &empty_batch));

  // make sure we can re-open it.
  ASSERT_OK(TryReopenWithColumnFamilies({"default", "pikachu"}, options));
  ASSERT_EQ("bar", Get(1, "foo"));
}

TEST_F(DBTest, ReadOnlyDB) {
  ASSERT_OK(Put("foo", "v1"));
  ASSERT_OK(Put("bar", "v2"));
  ASSERT_OK(Put("foo", "v3"));
  Close();

  auto options = CurrentOptions();
  assert(options.env = env_);
  ASSERT_OK(ReadOnlyReopen(options));
  ASSERT_EQ("v3", Get("foo"));
  ASSERT_EQ("v2", Get("bar"));
  Iterator* iter = db_->NewIterator(ReadOptions());
  int count = 0;
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    ++count;
  }
  ASSERT_EQ(count, 2);
  delete iter;
  Close();

  // Reopen and flush memtable.
  Reopen(options);
  ASSERT_OK(Flush());
  Close();
  // Now check keys in read only mode.
  ASSERT_OK(ReadOnlyReopen(options));
  ASSERT_EQ("v3", Get("foo"));
  ASSERT_EQ("v2", Get("bar"));
  ASSERT_TRUE(db_->SyncWAL().IsNotSupported());
}

TEST_F(DBTest, CompactedDB) {
  const uint64_t kFileSize = 1 << 20;
  Options options;
  options.disable_auto_compactions = true;
  options.write_buffer_size = kFileSize;
  options.target_file_size_base = kFileSize;
  options.max_bytes_for_level_base = 1 << 30;
  options.compression = kNoCompression;
  options = CurrentOptions(options);
  Reopen(options);
  // 1 L0 file, use CompactedDB if max_open_files = -1
  ASSERT_OK(Put("aaa", DummyString(kFileSize / 2, '1')));
  ASSERT_OK(Flush());
  Close();
  ASSERT_OK(ReadOnlyReopen(options));
  Status s = Put("new", "value");
  ASSERT_EQ(s.ToString(false),
            "Not implemented: Not supported operation in read only mode.");
  ASSERT_EQ(DummyString(kFileSize / 2, '1'), Get("aaa"));
  Close();
  options.max_open_files = -1;
  ASSERT_OK(ReadOnlyReopen(options));
  s = Put("new", "value");
  ASSERT_EQ(s.ToString(false),
            "Not implemented: Not supported in compacted db mode.");
  ASSERT_EQ(DummyString(kFileSize / 2, '1'), Get("aaa"));
  Close();
  Reopen(options);
  // Add more L0 files
  ASSERT_OK(Put("bbb", DummyString(kFileSize / 2, '2')));
  ASSERT_OK(Flush());
  ASSERT_OK(Put("aaa", DummyString(kFileSize / 2, 'a')));
  ASSERT_OK(Flush());
  ASSERT_OK(Put("bbb", DummyString(kFileSize / 2, 'b')));
  ASSERT_OK(Put("eee", DummyString(kFileSize / 2, 'e')));
  ASSERT_OK(Flush());
  Close();

  ASSERT_OK(ReadOnlyReopen(options));
  // Fallback to read-only DB
  s = Put("new", "value");
  ASSERT_EQ(s.ToString(false),
            "Not implemented: Not supported operation in read only mode.");
  Close();

  // Full compaction
  Reopen(options);
  // Add more keys
  ASSERT_OK(Put("fff", DummyString(kFileSize / 2, 'f')));
  ASSERT_OK(Put("hhh", DummyString(kFileSize / 2, 'h')));
  ASSERT_OK(Put("iii", DummyString(kFileSize / 2, 'i')));
  ASSERT_OK(Put("jjj", DummyString(kFileSize / 2, 'j')));
  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ(3, NumTableFilesAtLevel(1));
  Close();

  // CompactedDB
  ASSERT_OK(ReadOnlyReopen(options));
  s = Put("new", "value");
  ASSERT_EQ(s.ToString(false),
            "Not implemented: Not supported in compacted db mode.");
  ASSERT_EQ("NOT_FOUND", Get("abc"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'a'), Get("aaa"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'b'), Get("bbb"));
  ASSERT_EQ("NOT_FOUND", Get("ccc"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'e'), Get("eee"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'f'), Get("fff"));
  ASSERT_EQ("NOT_FOUND", Get("ggg"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'h'), Get("hhh"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'i'), Get("iii"));
  ASSERT_EQ(DummyString(kFileSize / 2, 'j'), Get("jjj"));
  ASSERT_EQ("NOT_FOUND", Get("kkk"));

  // MultiGet
  std::vector<std::string> values;
  std::vector<Status> status_list = dbfull()->MultiGet(ReadOptions(),
      std::vector<Slice>({Slice("aaa"), Slice("ccc"), Slice("eee"),
                          Slice("ggg"), Slice("iii"), Slice("kkk")}),
      &values);
  ASSERT_EQ(status_list.size(), static_cast<uint64_t>(6));
  ASSERT_EQ(values.size(), static_cast<uint64_t>(6));
  ASSERT_OK(status_list[0]);
  ASSERT_EQ(DummyString(kFileSize / 2, 'a'), values[0]);
  ASSERT_TRUE(status_list[1].IsNotFound());
  ASSERT_OK(status_list[2]);
  ASSERT_EQ(DummyString(kFileSize / 2, 'e'), values[2]);
  ASSERT_TRUE(status_list[3].IsNotFound());
  ASSERT_OK(status_list[4]);
  ASSERT_EQ(DummyString(kFileSize / 2, 'i'), values[4]);
  ASSERT_TRUE(status_list[5].IsNotFound());
}

// Make sure that when options.block_cache is set, after a new table is
// created its index/filter blocks are added to block cache.
TEST_F(DBTest, IndexAndFilterBlocksOfNewTableAddedToCache) {
  Options options = CurrentOptions();
  options.create_if_missing = true;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  BlockBasedTableOptions table_options;
  table_options.cache_index_and_filter_blocks = true;
  table_options.filter_policy.reset(NewBloomFilterPolicy(20));
  options.table_factory.reset(new BlockBasedTableFactory(table_options));
  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "key", "val"));
  // Create a new table.
  ASSERT_OK(Flush(1));

  // index/filter blocks added to block cache right after table creation.
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_INDEX_MISS));
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_FILTER_MISS));
  ASSERT_EQ(2, /* only index/filter were added */
            TestGetTickerCount(options, BLOCK_CACHE_ADD));
  ASSERT_EQ(0, TestGetTickerCount(options, BLOCK_CACHE_DATA_MISS));
  uint64_t int_num;
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-table-readers-mem", &int_num));
  ASSERT_EQ(int_num, 0U);

  // Make sure filter block is in cache.
  std::string value;
  ReadOptions ropt;
  db_->KeyMayExist(ReadOptions(), handles_[1], "key", &value);

  // Miss count should remain the same.
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_FILTER_MISS));
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_FILTER_HIT));

  db_->KeyMayExist(ReadOptions(), handles_[1], "key", &value);
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_FILTER_MISS));
  ASSERT_EQ(2, TestGetTickerCount(options, BLOCK_CACHE_FILTER_HIT));

  // Make sure index block is in cache.
  auto index_block_hit = TestGetTickerCount(options, BLOCK_CACHE_FILTER_HIT);
  value = Get(1, "key");
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_FILTER_MISS));
  ASSERT_EQ(index_block_hit + 1,
            TestGetTickerCount(options, BLOCK_CACHE_FILTER_HIT));

  value = Get(1, "key");
  ASSERT_EQ(1, TestGetTickerCount(options, BLOCK_CACHE_FILTER_MISS));
  ASSERT_EQ(index_block_hit + 2,
            TestGetTickerCount(options, BLOCK_CACHE_FILTER_HIT));
}

TEST_F(DBTest, ParanoidFileChecks) {
  Options options = CurrentOptions();
  options.create_if_missing = true;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.level0_file_num_compaction_trigger = 2;
  options.paranoid_file_checks = true;
  BlockBasedTableOptions table_options;
  table_options.cache_index_and_filter_blocks = false;
  table_options.filter_policy.reset(NewBloomFilterPolicy(20));
  options.table_factory.reset(new BlockBasedTableFactory(table_options));
  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "1_key", "val"));
  ASSERT_OK(Put(1, "9_key", "val"));
  // Create a new table.
  ASSERT_OK(Flush(1));
  ASSERT_EQ(1, /* read and cache data block */
            TestGetTickerCount(options, BLOCK_CACHE_ADD));

  ASSERT_OK(Put(1, "1_key2", "val2"));
  ASSERT_OK(Put(1, "9_key2", "val2"));
  // Create a new SST file. This will further trigger a compaction
  // and generate another file.
  ASSERT_OK(Flush(1));
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ(3, /* Totally 3 files created up to now */
            TestGetTickerCount(options, BLOCK_CACHE_ADD));

  // After disabling options.paranoid_file_checks. NO further block
  // is added after generating a new file.
  ASSERT_OK(
      dbfull()->SetOptions(handles_[1], {{"paranoid_file_checks", "false"}}));

  ASSERT_OK(Put(1, "1_key3", "val3"));
  ASSERT_OK(Put(1, "9_key3", "val3"));
  ASSERT_OK(Flush(1));
  ASSERT_OK(Put(1, "1_key4", "val4"));
  ASSERT_OK(Put(1, "9_key4", "val4"));
  ASSERT_OK(Flush(1));
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ(3, /* Totally 3 files created up to now */
            TestGetTickerCount(options, BLOCK_CACHE_ADD));
}

TEST_F(DBTest, LevelLimitReopen) {
  Options options = CurrentOptions();
  CreateAndReopenWithCF({"pikachu"}, options);

  const std::string value(1024 * 1024, ' ');
  int i = 0;
  while (NumTableFilesAtLevel(2, 1) == 0) {
    ASSERT_OK(Put(1, Key(i++), value));
  }

  options.num_levels = 1;
  options.max_bytes_for_level_multiplier_additional.resize(1, 1);
  Status s = TryReopenWithColumnFamilies({"default", "pikachu"}, options);
  ASSERT_EQ(s.IsInvalidArgument(), true);
  ASSERT_EQ(s.ToString(false),
            "Invalid argument: db has more levels than options.num_levels");

  options.num_levels = 10;
  options.max_bytes_for_level_multiplier_additional.resize(10, 1);
  ASSERT_OK(TryReopenWithColumnFamilies({"default", "pikachu"}, options));
}

TEST_F(DBTest, PutDeleteGet) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_OK(Put(1, "foo", "v2"));
    ASSERT_EQ("v2", Get(1, "foo"));
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_EQ("NOT_FOUND", Get(1, "foo"));
  } while (ChangeOptions());
}

TEST_F(DBTest, PutSingleDeleteGet) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_OK(Put(1, "foo2", "v2"));
    ASSERT_EQ("v2", Get(1, "foo2"));
    ASSERT_OK(SingleDelete(1, "foo"));
    ASSERT_EQ("NOT_FOUND", Get(1, "foo"));
    // FIFO and universal compaction do not apply to the test case.
    // Skip MergePut because single delete does not get removed when it encounters a merge.
  } while (ChangeOptions(kSkipFIFOCompaction |
                         kSkipUniversalCompaction | kSkipMergePut));
}

TEST_F(DBTest, ReadFromPersistedTier) {
  do {
    Random rnd(301);
    Options options = CurrentOptions();
    for (int disableWAL = 0; disableWAL <= 1; ++disableWAL) {
      CreateAndReopenWithCF({"pikachu"}, options);
      WriteOptions wopt;
      wopt.disableWAL = (disableWAL == 1);
      // 1st round: put but not flush
      ASSERT_OK(db_->Put(wopt, handles_[1], "foo", "first"));
      ASSERT_OK(db_->Put(wopt, handles_[1], "bar", "one"));
      ASSERT_EQ("first", Get(1, "foo"));
      ASSERT_EQ("one", Get(1, "bar"));

      // Read directly from persited data.
      ReadOptions ropt;
      ropt.read_tier = kPersistedTier;
      std::string value;
      if (wopt.disableWAL) {
        // as data has not yet being flushed, we expect not found.
        ASSERT_TRUE(db_->Get(ropt, handles_[1], "foo", &value).IsNotFound());
        ASSERT_TRUE(db_->Get(ropt, handles_[1], "bar", &value).IsNotFound());
      } else {
        ASSERT_OK(db_->Get(ropt, handles_[1], "foo", &value));
        ASSERT_OK(db_->Get(ropt, handles_[1], "bar", &value));
      }

      // Multiget
      std::vector<ColumnFamilyHandle*> multiget_cfs;
      multiget_cfs.push_back(handles_[1]);
      multiget_cfs.push_back(handles_[1]);
      std::vector<Slice> multiget_keys;
      multiget_keys.push_back("foo");
      multiget_keys.push_back("bar");
      std::vector<std::string> multiget_values;
      auto statuses =
          db_->MultiGet(ropt, multiget_cfs, multiget_keys, &multiget_values);
      if (wopt.disableWAL) {
        ASSERT_TRUE(statuses[0].IsNotFound());
        ASSERT_TRUE(statuses[1].IsNotFound());
      } else {
        ASSERT_OK(statuses[0]);
        ASSERT_OK(statuses[1]);
      }

      // 2nd round: flush and put a new value in memtable.
      ASSERT_OK(Flush(1));
      ASSERT_OK(db_->Put(wopt, handles_[1], "rocksdb", "hello"));

      // once the data has been flushed, we are able to get the
      // data when kPersistedTier is used.
      ASSERT_TRUE(db_->Get(ropt, handles_[1], "foo", &value).ok());
      ASSERT_EQ(value, "first");
      ASSERT_TRUE(db_->Get(ropt, handles_[1], "bar", &value).ok());
      ASSERT_EQ(value, "one");
      if (wopt.disableWAL) {
        ASSERT_TRUE(
            db_->Get(ropt, handles_[1], "rocksdb", &value).IsNotFound());
      } else {
        ASSERT_OK(db_->Get(ropt, handles_[1], "rocksdb", &value));
        ASSERT_EQ(value, "hello");
      }

      // Expect same result in multiget
      multiget_cfs.push_back(handles_[1]);
      multiget_keys.push_back("rocksdb");
      statuses =
          db_->MultiGet(ropt, multiget_cfs, multiget_keys, &multiget_values);
      ASSERT_TRUE(statuses[0].ok());
      ASSERT_EQ("first", multiget_values[0]);
      ASSERT_TRUE(statuses[1].ok());
      ASSERT_EQ("one", multiget_values[1]);
      if (wopt.disableWAL) {
        ASSERT_TRUE(statuses[2].IsNotFound());
      } else {
        ASSERT_OK(statuses[2]);
      }

      // 3rd round: delete and flush
      ASSERT_OK(db_->Delete(wopt, handles_[1], "foo"));
      ASSERT_OK(Flush(1));
      ASSERT_OK(db_->Delete(wopt, handles_[1], "bar"));

      ASSERT_TRUE(db_->Get(ropt, handles_[1], "foo", &value).IsNotFound());
      if (wopt.disableWAL) {
        // Still expect finding the value as its delete has not yet being
        // flushed.
        ASSERT_TRUE(db_->Get(ropt, handles_[1], "bar", &value).ok());
        ASSERT_EQ(value, "one");
      } else {
        ASSERT_TRUE(db_->Get(ropt, handles_[1], "bar", &value).IsNotFound());
      }
      ASSERT_TRUE(db_->Get(ropt, handles_[1], "rocksdb", &value).ok());
      ASSERT_EQ(value, "hello");

      statuses =
          db_->MultiGet(ropt, multiget_cfs, multiget_keys, &multiget_values);
      ASSERT_TRUE(statuses[0].IsNotFound());
      if (wopt.disableWAL) {
        ASSERT_TRUE(statuses[1].ok());
        ASSERT_EQ("one", multiget_values[1]);
      } else {
        ASSERT_TRUE(statuses[1].IsNotFound());
      }
      ASSERT_TRUE(statuses[2].ok());
      ASSERT_EQ("hello", multiget_values[2]);
      if (wopt.disableWAL == 0) {
        DestroyAndReopen(options);
      }
    }
  } while (ChangeOptions());
}

TEST_F(DBTest, IteratorProperty) {
  // The test needs to be changed if kPersistedTier is supported in iterator.
  Options options = CurrentOptions();
  CreateAndReopenWithCF({"pikachu"}, options);
  ASSERT_OK(Put(1, "1", "2"));
  ReadOptions ropt;
  ropt.pin_data = false;
  {
    unique_ptr<Iterator> iter(db_->NewIterator(ropt, handles_[1]));
    iter->SeekToFirst();
    std::string prop_value;
    ASSERT_NOK(iter->GetProperty("non_existing.value", &prop_value));
    ASSERT_OK(iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
    ASSERT_EQ("0", prop_value);
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    iter->Next();
    ASSERT_FALSE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_OK(iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
    ASSERT_EQ("Iterator is not valid.", prop_value);
  }
  Close();
}

TEST_F(DBTest, PersistedTierOnIterator) {
  // The test needs to be changed if kPersistedTier is supported in iterator.
  Options options = CurrentOptions();
  CreateAndReopenWithCF({"pikachu"}, options);
  ReadOptions ropt;
  ropt.read_tier = kPersistedTier;

  auto* iter = db_->NewIterator(ropt, handles_[1]);
  ASSERT_TRUE(iter->status().IsNotSupported());
  delete iter;

  std::vector<Iterator*> iters;
  ASSERT_TRUE(db_->NewIterators(ropt, {handles_[1]}, &iters).IsNotSupported());
  Close();
}

TEST_F(DBTest, SingleDeleteFlush) {
  // Test to check whether flushing preserves a single delete hidden
  // behind a put.
  do {
    Random rnd(301);

    Options options = CurrentOptions();
    options.disable_auto_compactions = true;
    CreateAndReopenWithCF({"pikachu"}, options);

    // Put values on second level (so that they will not be in the same
    // compaction as the other operations.
    ASSERT_OK(Put(1, "foo", "first"));
    ASSERT_OK(Put(1, "bar", "one"));
    ASSERT_OK(Flush(1));
    MoveFilesToLevel(2, 1);

    // (Single) delete hidden by a put
    ASSERT_OK(SingleDelete(1, "foo"));
    ASSERT_OK(Put(1, "foo", "second"));
    ASSERT_OK(Delete(1, "bar"));
    ASSERT_OK(Put(1, "bar", "two"));
    ASSERT_OK(Flush(1));

    ASSERT_OK(SingleDelete(1, "foo"));
    ASSERT_OK(Delete(1, "bar"));
    ASSERT_OK(Flush(1));

    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));

    ASSERT_EQ("NOT_FOUND", Get(1, "bar"));
    ASSERT_EQ("NOT_FOUND", Get(1, "foo"));
    // FIFO and universal compaction do not apply to the test case.
    // Skip MergePut because merges cannot be combined with single deletions.
  } while (ChangeOptions(kSkipFIFOCompaction | kSkipUniversalCompaction | kSkipMergePut));
}

TEST_F(DBTest, SingleDeletePutFlush) {
  // Single deletes that encounter the matching put in a flush should get
  // removed.
  do {
    Random rnd(301);

    Options options = CurrentOptions();
    options.disable_auto_compactions = true;
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_OK(Put(1, "foo", Slice()));
    ASSERT_OK(Put(1, "a", Slice()));
    ASSERT_OK(SingleDelete(1, "a"));
    ASSERT_OK(Flush(1));

    ASSERT_EQ("[ ]", AllEntriesFor("a", 1));
    // FIFO and universal compaction do not apply to the test case.
    // Skip MergePut because merges cannot be combined with single deletions.
  } while (ChangeOptions(kSkipFIFOCompaction | kSkipUniversalCompaction | kSkipMergePut));
}

TEST_F(DBTest, EmptyFlush) {
  // It is possible to produce empty flushes when using single deletes. Tests
  // whether empty flushes cause issues.
  do {
    Random rnd(301);

    Options options = CurrentOptions();
    options.disable_auto_compactions = true;
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_OK(Put(1, "a", Slice()));
    ASSERT_OK(SingleDelete(1, "a"));
    ASSERT_OK(Flush(1));

    ASSERT_EQ("[ ]", AllEntriesFor("a", 1));
    // FIFO and universal compaction do not apply to the test case.
    // Skip MergePut because merges cannot be combined with single deletions.
  } while (ChangeOptions(kSkipFIFOCompaction | kSkipUniversalCompaction | kSkipMergePut));
}

// Disable because not all platform can run it.
// It requires more than 9GB memory to run it, With single allocation
// of more than 3GB.
TEST_F(DBTest, DISABLED_VeryLargeValue) {
  const size_t kValueSize = 3221225472u;  // 3GB value
  const size_t kKeySize = 8388608u;       // 8MB key
  std::string raw(kValueSize, 'v');
  std::string key1(kKeySize, 'c');
  std::string key2(kKeySize, 'd');

  Options options;
  options.env = env_;
  options.write_buffer_size = 100000;  // Small write buffer
  options.paranoid_checks = true;
  options = CurrentOptions(options);
  DestroyAndReopen(options);

  ASSERT_OK(Put("boo", "v1"));
  ASSERT_OK(Put("foo", "v1"));
  ASSERT_OK(Put(key1, raw));
  raw[0] = 'w';
  ASSERT_OK(Put(key2, raw));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());

  ASSERT_EQ(1, NumTableFilesAtLevel(0));

  std::string value;
  Status s = db_->Get(ReadOptions(), key1, &value);
  ASSERT_OK(s);
  ASSERT_EQ(kValueSize, value.size());
  ASSERT_EQ('v', value[0]);

  s = db_->Get(ReadOptions(), key2, &value);
  ASSERT_OK(s);
  ASSERT_EQ(kValueSize, value.size());
  ASSERT_EQ('w', value[0]);

  // Compact all files.
  ASSERT_OK(Flush());
  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));

  // Check DB is not in read-only state.
  ASSERT_OK(Put("boo", "v1"));

  s = db_->Get(ReadOptions(), key1, &value);
  ASSERT_OK(s);
  ASSERT_EQ(kValueSize, value.size());
  ASSERT_EQ('v', value[0]);

  s = db_->Get(ReadOptions(), key2, &value);
  ASSERT_OK(s);
  ASSERT_EQ(kValueSize, value.size());
  ASSERT_EQ('w', value[0]);
}

TEST_F(DBTest, GetFromImmutableLayer) {
  do {
    Options options;
    options.env = env_;
    options.write_buffer_size = 100000;  // Small write buffer
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_EQ("v1", Get(1, "foo"));

    // Block sync calls
    env_->delay_sstable_sync_.store(true, std::memory_order_release);
    ASSERT_OK(Put(1, "k1", std::string(100000, 'x')));          // Fill memtable
    ASSERT_OK(Put(1, "k2", std::string(100000, 'y')));          // Trigger flush
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_EQ("NOT_FOUND", Get(0, "foo"));
    // Release sync calls
    env_->delay_sstable_sync_.store(false, std::memory_order_release);
  } while (ChangeOptions());
}

TEST_F(DBTest, GetFromVersions) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_EQ("NOT_FOUND", Get(0, "foo"));
  } while (ChangeOptions());
}

TEST_F(DBTest, GetSnapshot) {
  anon::OptionsOverride options_override;
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions(options_override));
    // Try with both a short key and a long key
    for (int i = 0; i < 2; i++) {
      std::string key = (i == 0) ? std::string("foo") : std::string(200, 'x');
      ASSERT_OK(Put(1, key, "v1"));
      const Snapshot* s1 = db_->GetSnapshot();
      ASSERT_OK(Put(1, key, "v2"));
      ASSERT_EQ("v2", Get(1, key));
      ASSERT_EQ("v1", Get(1, key, s1));
      ASSERT_OK(Flush(1));
      ASSERT_EQ("v2", Get(1, key));
      ASSERT_EQ("v1", Get(1, key, s1));
      db_->ReleaseSnapshot(s1);
    }
  } while (ChangeOptions());
}

TEST_F(DBTest, GetLevel0Ordering) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    // Check that we process level-0 files in correct order.  The code
    // below generates two level-0 files where the earlier one comes
    // before the later one in the level-0 file list since the earlier
    // one has a smaller "smallest" key.
    ASSERT_OK(Put(1, "bar", "b"));
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(Put(1, "foo", "v2"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ("v2", Get(1, "foo"));
  } while (ChangeOptions());
}

TEST_F(DBTest, WrongLevel0Config) {
  Options options = CurrentOptions();
  Close();
  ASSERT_OK(DestroyDB(dbname_, options));
  options.level0_stop_writes_trigger = 1;
  options.level0_slowdown_writes_trigger = 2;
  options.level0_file_num_compaction_trigger = 3;
  ASSERT_OK(DB::Open(options, dbname_, &db_));
}

TEST_F(DBTest, GetOrderedByLevels) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    Compact(1, "a", "z");
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_OK(Put(1, "foo", "v2"));
    ASSERT_EQ("v2", Get(1, "foo"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ("v2", Get(1, "foo"));
  } while (ChangeOptions());
}

TEST_F(DBTest, GetPicksCorrectFile) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    // Arrange to have multiple files in a non-level-0 level.
    ASSERT_OK(Put(1, "a", "va"));
    Compact(1, "a", "b");
    ASSERT_OK(Put(1, "x", "vx"));
    Compact(1, "x", "y");
    ASSERT_OK(Put(1, "f", "vf"));
    Compact(1, "f", "g");
    ASSERT_EQ("va", Get(1, "a"));
    ASSERT_EQ("vf", Get(1, "f"));
    ASSERT_EQ("vx", Get(1, "x"));
  } while (ChangeOptions());
}

TEST_F(DBTest, GetEncountersEmptyLevel) {
  do {
    Options options = CurrentOptions();
    options.disableDataSync = true;
    CreateAndReopenWithCF({"pikachu"}, options);
    // Arrange for the following to happen:
    //   * sstable A in level 0
    //   * nothing in level 1
    //   * sstable B in level 2
    // Then do enough Get() calls to arrange for an automatic compaction
    // of sstable A.  A bug would cause the compaction to be marked as
    // occurring at level 1 (instead of the correct level 0).

    // Step 1: First place sstables in levels 0 and 2
    ASSERT_OK(Put(1, "a", "begin"));
    ASSERT_OK(Put(1, "z", "end"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr, handles_[1]));
    ASSERT_OK(dbfull()->TEST_CompactRange(1, nullptr, nullptr, handles_[1]));
    ASSERT_OK(Put(1, "a", "begin"));
    ASSERT_OK(Put(1, "z", "end"));
    ASSERT_OK(Flush(1));
    ASSERT_GT(NumTableFilesAtLevel(0, 1), 0);
    ASSERT_GT(NumTableFilesAtLevel(2, 1), 0);

    // Step 2: clear level 1 if necessary.
    ASSERT_OK(dbfull()->TEST_CompactRange(1, nullptr, nullptr, handles_[1]));
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 1);
    ASSERT_EQ(NumTableFilesAtLevel(1, 1), 0);
    ASSERT_EQ(NumTableFilesAtLevel(2, 1), 1);

    // Step 3: read a bunch of times
    for (int i = 0; i < 1000; i++) {
      ASSERT_EQ("NOT_FOUND", Get(1, "missing"));
    }

    // Step 4: Wait for compaction to finish
    ASSERT_OK(dbfull()->TEST_WaitForCompact());

    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 1);  // XXX
  } while (ChangeOptions(kSkipUniversalCompaction | kSkipFIFOCompaction));
}

TEST_F(DBTest, NonBlockingIteration) {
  do {
    ReadOptions non_blocking_opts, regular_opts;
    Options options = CurrentOptions();
    options.statistics = rocksdb::CreateDBStatisticsForTests();
    non_blocking_opts.read_tier = kBlockCacheTier;
    CreateAndReopenWithCF({"pikachu"}, options);
    // write one kv to the database.
    ASSERT_OK(Put(1, "a", "b"));

    // scan using non-blocking iterator. We should find it because
    // it is in memtable.
    Iterator* iter = db_->NewIterator(non_blocking_opts, handles_[1]);
    int count = 0;
    for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
      count++;
    }
    ASSERT_EQ(count, 1);
    delete iter;

    // flush memtable to storage. Now, the key should not be in the
    // memtable neither in the block cache.
    ASSERT_OK(Flush(1));

    // verify that a non-blocking iterator does not find any
    // kvs. Neither does it do any IOs to storage.
    uint64_t numopen = TestGetTickerCount(options, NO_FILE_OPENS);
    uint64_t cache_added = TestGetTickerCount(options, BLOCK_CACHE_ADD);
    iter = db_->NewIterator(non_blocking_opts, handles_[1]);
    count = 0;
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      count++;
    }
    ASSERT_EQ(count, 0);
    ASSERT_TRUE(iter->status().IsIncomplete());
    ASSERT_EQ(numopen, TestGetTickerCount(options, NO_FILE_OPENS));
    ASSERT_EQ(cache_added, TestGetTickerCount(options, BLOCK_CACHE_ADD));
    delete iter;

    // read in the specified block via a regular get
    ASSERT_EQ(Get(1, "a"), "b");

    // verify that we can find it via a non-blocking scan
    numopen = TestGetTickerCount(options, NO_FILE_OPENS);
    cache_added = TestGetTickerCount(options, BLOCK_CACHE_ADD);
    iter = db_->NewIterator(non_blocking_opts, handles_[1]);
    count = 0;
    for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
      count++;
    }
    ASSERT_EQ(count, 1);
    ASSERT_EQ(numopen, TestGetTickerCount(options, NO_FILE_OPENS));
    ASSERT_EQ(cache_added, TestGetTickerCount(options, BLOCK_CACHE_ADD));
    delete iter;

    // This test verifies block cache behaviors, which is not used by plain
    // table format.
  } while (ChangeOptions(kSkipPlainTable | kSkipNoSeekToLast | kSkipMmapReads));
}

TEST_F(DBTest, ManagedNonBlockingIteration) {
  do {
    ReadOptions non_blocking_opts, regular_opts;
    Options options = CurrentOptions();
    options.statistics = rocksdb::CreateDBStatisticsForTests();
    non_blocking_opts.read_tier = kBlockCacheTier;
    non_blocking_opts.managed = true;
    CreateAndReopenWithCF({"pikachu"}, options);
    // write one kv to the database.
    ASSERT_OK(Put(1, "a", "b"));

    // scan using non-blocking iterator. We should find it because
    // it is in memtable.
    Iterator* iter = db_->NewIterator(non_blocking_opts, handles_[1]);
    int count = 0;
    for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
      count++;
    }
    ASSERT_EQ(count, 1);
    delete iter;

    // flush memtable to storage. Now, the key should not be in the
    // memtable neither in the block cache.
    ASSERT_OK(Flush(1));

    // verify that a non-blocking iterator does not find any
    // kvs. Neither does it do any IOs to storage.
    int64_t numopen = TestGetTickerCount(options, NO_FILE_OPENS);
    int64_t cache_added = TestGetTickerCount(options, BLOCK_CACHE_ADD);
    iter = db_->NewIterator(non_blocking_opts, handles_[1]);
    count = 0;
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      count++;
    }
    ASSERT_EQ(count, 0);
    ASSERT_TRUE(iter->status().IsIncomplete());
    ASSERT_EQ(numopen, TestGetTickerCount(options, NO_FILE_OPENS));
    ASSERT_EQ(cache_added, TestGetTickerCount(options, BLOCK_CACHE_ADD));
    delete iter;

    // read in the specified block via a regular get
    ASSERT_EQ(Get(1, "a"), "b");

    // verify that we can find it via a non-blocking scan
    numopen = TestGetTickerCount(options, NO_FILE_OPENS);
    cache_added = TestGetTickerCount(options, BLOCK_CACHE_ADD);
    iter = db_->NewIterator(non_blocking_opts, handles_[1]);
    count = 0;
    for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
      count++;
    }
    ASSERT_EQ(count, 1);
    ASSERT_EQ(numopen, TestGetTickerCount(options, NO_FILE_OPENS));
    ASSERT_EQ(cache_added, TestGetTickerCount(options, BLOCK_CACHE_ADD));
    delete iter;

    // This test verifies block cache behaviors, which is not used by plain
    // table format.
  } while (ChangeOptions(kSkipPlainTable | kSkipNoSeekToLast | kSkipMmapReads));
}

TEST_F(DBTest, IterSeekBeforePrev) {
  ASSERT_OK(Put("a", "b"));
  ASSERT_OK(Put("c", "d"));
  ASSERT_OK(dbfull()->Flush(FlushOptions()));
  ASSERT_OK(Put("0", "f"));
  ASSERT_OK(Put("1", "h"));
  ASSERT_OK(dbfull()->Flush(FlushOptions()));
  ASSERT_OK(Put("2", "j"));
  auto iter = db_->NewIterator(ReadOptions());
  iter->Seek(Slice("c"));
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
  iter->Prev();
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
  iter->Seek(Slice("a"));
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
  iter->Prev();
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
  delete iter;
}

namespace {
std::string MakeLongKey(size_t length, char c) {
  return std::string(length, c);
}
}  // namespace

TEST_F(DBTest, IterLongKeys) {
  ASSERT_OK(Put(MakeLongKey(20, 0), "0"));
  ASSERT_OK(Put(MakeLongKey(32, 2), "2"));
  ASSERT_OK(Put("a", "b"));
  ASSERT_OK(dbfull()->Flush(FlushOptions()));
  ASSERT_OK(Put(MakeLongKey(50, 1), "1"));
  ASSERT_OK(Put(MakeLongKey(127, 3), "3"));
  ASSERT_OK(Put(MakeLongKey(64, 4), "4"));
  auto iter = db_->NewIterator(ReadOptions());

  // Create a key that needs to be skipped for Seq too new
  iter->Seek(MakeLongKey(20, 0));
  ASSERT_EQ(IterStatus(iter), MakeLongKey(20, 0) + "->0");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), MakeLongKey(50, 1) + "->1");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), MakeLongKey(32, 2) + "->2");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), MakeLongKey(127, 3) + "->3");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), MakeLongKey(64, 4) + "->4");
  delete iter;

  iter = db_->NewIterator(ReadOptions());
  iter->Seek(MakeLongKey(50, 1));
  ASSERT_EQ(IterStatus(iter), MakeLongKey(50, 1) + "->1");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), MakeLongKey(32, 2) + "->2");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), MakeLongKey(127, 3) + "->3");
  delete iter;
}

TEST_F(DBTest, IterNextWithNewerSeq) {
  ASSERT_OK(Put("0", "0"));
  ASSERT_OK(dbfull()->Flush(FlushOptions()));
  ASSERT_OK(Put("a", "b"));
  ASSERT_OK(Put("c", "d"));
  ASSERT_OK(Put("d", "e"));
  auto iter = db_->NewIterator(ReadOptions());

  // Create a key that needs to be skipped for Seq too new
  for (uint64_t i = 0; i < last_options_.max_sequential_skip_in_iterations + 1;
       i++) {
    ASSERT_OK(Put("b", "f"));
  }

  iter->Seek(Slice("a"));
  ASSERT_EQ(IterStatus(iter), "a->b");
  iter->Next();
  ASSERT_EQ(IterStatus(iter), "c->d");
  delete iter;
}

TEST_F(DBTest, IterPrevWithNewerSeq) {
  ASSERT_OK(Put("0", "0"));
  ASSERT_OK(dbfull()->Flush(FlushOptions()));
  ASSERT_OK(Put("a", "b"));
  ASSERT_OK(Put("c", "d"));
  ASSERT_OK(Put("d", "e"));
  auto iter = db_->NewIterator(ReadOptions());

  // Create a key that needs to be skipped for Seq too new
  for (uint64_t i = 0; i < last_options_.max_sequential_skip_in_iterations + 1;
       i++) {
    ASSERT_OK(Put("b", "f"));
  }

  iter->Seek(Slice("d"));
  ASSERT_EQ(IterStatus(iter), "d->e");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "c->d");
  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "a->b");

  iter->Prev();
  delete iter;
}

TEST_F(DBTest, IterPrevWithNewerSeq2) {
  ASSERT_OK(Put("0", "0"));
  ASSERT_OK(dbfull()->Flush(FlushOptions()));
  ASSERT_OK(Put("a", "b"));
  ASSERT_OK(Put("c", "d"));
  ASSERT_OK(Put("d", "e"));
  auto iter = db_->NewIterator(ReadOptions());
  iter->Seek(Slice("c"));
  ASSERT_EQ(IterStatus(iter), "c->d");

  // Create a key that needs to be skipped for Seq too new
  for (uint64_t i = 0; i < last_options_.max_sequential_skip_in_iterations + 1;
      i++) {
    ASSERT_OK(Put("b", "f"));
  }

  iter->Prev();
  ASSERT_EQ(IterStatus(iter), "a->b");

  iter->Prev();
  delete iter;
}

TEST_F(DBTest, IterEmpty) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);

    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->Seek("foo");
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    delete iter;
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, IterSingle) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "a", "va"));
    Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);

    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");
    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");
    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->Seek("");
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->Seek("a");
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->Seek("b");
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    delete iter;
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, IterMulti) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "a", "va"));
    ASSERT_OK(Put(1, "b", "vb"));
    ASSERT_OK(Put(1, "c", "vc"));
    Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);

    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "b->vb");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");
    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "b->vb");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");
    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->Seek("");
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Seek("a");
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Seek("ax");
    ASSERT_EQ(IterStatus(iter), "b->vb");

    iter->Seek("b");
    ASSERT_EQ(IterStatus(iter), "b->vb");
    iter->Seek("z");
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    // Switch from reverse to forward
    iter->SeekToLast();
    iter->Prev();
    iter->Prev();
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "b->vb");

    // Switch from forward to reverse
    iter->SeekToFirst();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "b->vb");

    // Make sure iter stays at snapshot
    ASSERT_OK(Put(1, "a", "va2"));
    ASSERT_OK(Put(1, "a2", "va3"));
    ASSERT_OK(Put(1, "b", "vb2"));
    ASSERT_OK(Put(1, "c", "vc2"));
    ASSERT_OK(Delete(1, "b"));
    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "b->vb");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");
    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "b->vb");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    delete iter;
  } while (ChangeCompactOptions());
}

// Check that we can skip over a run of user keys
// by using reseek rather than sequential scan
TEST_F(DBTest, IterReseek) {
  anon::OptionsOverride options_override;
  Options options = CurrentOptions(options_override);
  options.max_sequential_skip_in_iterations = 3;
  options.create_if_missing = true;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  DestroyAndReopen(options);
  CreateAndReopenWithCF({"pikachu"}, options);

  // insert three keys with same userkey and verify that
  // reseek is not invoked. For each of these test cases,
  // verify that we can find the next key "b".
  ASSERT_OK(Put(1, "a", "zero"));
  ASSERT_OK(Put(1, "a", "one"));
  ASSERT_OK(Put(1, "a", "two"));
  ASSERT_OK(Put(1, "b", "bone"));
  Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);
  iter->SeekToFirst();
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 0);
  ASSERT_EQ(IterStatus(iter), "a->two");
  iter->Next();
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 0);
  ASSERT_EQ(IterStatus(iter), "b->bone");
  delete iter;

  // insert a total of three keys with same userkey and verify
  // that reseek is still not invoked.
  ASSERT_OK(Put(1, "a", "three"));
  iter = db_->NewIterator(ReadOptions(), handles_[1]);
  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->three");
  iter->Next();
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 0);
  ASSERT_EQ(IterStatus(iter), "b->bone");
  delete iter;

  // insert a total of four keys with same userkey and verify
  // that reseek is invoked.
  ASSERT_OK(Put(1, "a", "four"));
  iter = db_->NewIterator(ReadOptions(), handles_[1]);
  iter->SeekToFirst();
  ASSERT_EQ(IterStatus(iter), "a->four");
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 0);
  iter->Next();
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION), 1);
  ASSERT_EQ(IterStatus(iter), "b->bone");
  delete iter;

  // Testing reverse iterator
  // At this point, we have three versions of "a" and one version of "b".
  // The reseek statistics is already at 1.
  int num_reseeks =
      static_cast<int>(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION));

  // Insert another version of b and assert that reseek is not invoked
  ASSERT_OK(Put(1, "b", "btwo"));
  iter = db_->NewIterator(ReadOptions(), handles_[1]);
  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "b->btwo");
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION),
            num_reseeks);
  iter->Prev();
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION),
            num_reseeks + 1);
  ASSERT_EQ(IterStatus(iter), "a->four");
  delete iter;

  // insert two more versions of b. This makes a total of 4 versions
  // of b and 4 versions of a.
  ASSERT_OK(Put(1, "b", "bthree"));
  ASSERT_OK(Put(1, "b", "bfour"));
  iter = db_->NewIterator(ReadOptions(), handles_[1]);
  iter->SeekToLast();
  ASSERT_EQ(IterStatus(iter), "b->bfour");
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION),
            num_reseeks + 2);
  iter->Prev();

  // the previous Prev call should have invoked reseek
  ASSERT_EQ(TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION),
            num_reseeks + 3);
  ASSERT_EQ(IterStatus(iter), "a->four");
  delete iter;
}

TEST_F(DBTest, IterSmallAndLargeMix) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "a", "va"));
    ASSERT_OK(Put(1, "b", std::string(100000, 'b')));
    ASSERT_OK(Put(1, "c", "vc"));
    ASSERT_OK(Put(1, "d", std::string(100000, 'd')));
    ASSERT_OK(Put(1, "e", std::string(100000, 'e')));

    Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);

    iter->SeekToFirst();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "b->" + std::string(100000, 'b'));
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "d->" + std::string(100000, 'd'));
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "e->" + std::string(100000, 'e'));
    iter->Next();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    iter->SeekToLast();
    ASSERT_EQ(IterStatus(iter), "e->" + std::string(100000, 'e'));
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "d->" + std::string(100000, 'd'));
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "c->vc");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "b->" + std::string(100000, 'b'));
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "a->va");
    iter->Prev();
    ASSERT_EQ(IterStatus(iter), "(invalid) OK");

    delete iter;
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, IterMultiWithDelete) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "ka", "va"));
    ASSERT_OK(Put(1, "kb", "vb"));
    ASSERT_OK(Put(1, "kc", "vc"));
    ASSERT_OK(Delete(1, "kb"));
    ASSERT_EQ("NOT_FOUND", Get(1, "kb"));

    Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);
    iter->Seek("kc");
    ASSERT_EQ(IterStatus(iter), "kc->vc");
    if (!CurrentOptions().merge_operator) {
      // TODO: merge operator does not support backward iteration yet
      if (kPlainTableAllBytesPrefix != option_config_&&
          kBlockBasedTableWithWholeKeyHashIndex != option_config_ &&
          kHashLinkList != option_config_) {
        iter->Prev();
        ASSERT_EQ(IterStatus(iter), "ka->va");
      }
    }
    delete iter;
  } while (ChangeOptions());
}

TEST_F(DBTest, IterPrevMaxSkip) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    for (int i = 0; i < 2; i++) {
      ASSERT_OK(Put(1, "key1", "v1"));
      ASSERT_OK(Put(1, "key2", "v2"));
      ASSERT_OK(Put(1, "key3", "v3"));
      ASSERT_OK(Put(1, "key4", "v4"));
      ASSERT_OK(Put(1, "key5", "v5"));
    }

    VerifyIterLast("key5->v5", 1);

    ASSERT_OK(Delete(1, "key5"));
    VerifyIterLast("key4->v4", 1);

    ASSERT_OK(Delete(1, "key4"));
    VerifyIterLast("key3->v3", 1);

    ASSERT_OK(Delete(1, "key3"));
    VerifyIterLast("key2->v2", 1);

    ASSERT_OK(Delete(1, "key2"));
    VerifyIterLast("key1->v1", 1);

    ASSERT_OK(Delete(1, "key1"));
    VerifyIterLast("(invalid) OK", 1);
  } while (ChangeOptions(kSkipMergePut | kSkipNoSeekToLast));
}

TEST_F(DBTest, IterWithSnapshot) {
  anon::OptionsOverride options_override;
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions(options_override));
    ASSERT_OK(Put(1, "key1", "val1"));
    ASSERT_OK(Put(1, "key2", "val2"));
    ASSERT_OK(Put(1, "key3", "val3"));
    ASSERT_OK(Put(1, "key4", "val4"));
    ASSERT_OK(Put(1, "key5", "val5"));

    const Snapshot *snapshot = db_->GetSnapshot();
    ReadOptions options;
    options.snapshot = snapshot;
    Iterator* iter = db_->NewIterator(options, handles_[1]);

    // Put more values after the snapshot
    ASSERT_OK(Put(1, "key100", "val100"));
    ASSERT_OK(Put(1, "key101", "val101"));

    iter->Seek("key5");
    ASSERT_EQ(IterStatus(iter), "key5->val5");
    if (!CurrentOptions().merge_operator) {
      // TODO: merge operator does not support backward iteration yet
      if (kPlainTableAllBytesPrefix != option_config_&&
        kBlockBasedTableWithWholeKeyHashIndex != option_config_ &&
        kHashLinkList != option_config_) {
        iter->Prev();
        ASSERT_EQ(IterStatus(iter), "key4->val4");
        iter->Prev();
        ASSERT_EQ(IterStatus(iter), "key3->val3");

        iter->Next();
        ASSERT_EQ(IterStatus(iter), "key4->val4");
        iter->Next();
        ASSERT_EQ(IterStatus(iter), "key5->val5");
      }
      iter->Next();
      ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
    }
    db_->ReleaseSnapshot(snapshot);
    delete iter;
  } while (ChangeOptions());
}

TEST_F(DBTest, Recover) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Put(1, "baz", "v5"));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_EQ("v1", Get(1, "foo"));

    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_EQ("v5", Get(1, "baz"));
    ASSERT_OK(Put(1, "bar", "v2"));
    ASSERT_OK(Put(1, "foo", "v3"));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_EQ("v3", Get(1, "foo"));
    ASSERT_OK(Put(1, "foo", "v4"));
    ASSERT_EQ("v4", Get(1, "foo"));
    ASSERT_EQ("v2", Get(1, "bar"));
    ASSERT_EQ("v5", Get(1, "baz"));
  } while (ChangeOptions());
}

TEST_F(DBTest, RecoverWithTableHandle) {
  do {
    Options options;
    options.create_if_missing = true;
    options.write_buffer_size = 100;
    options.disable_auto_compactions = true;
    options = CurrentOptions(options);
    DestroyAndReopen(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Put(1, "bar", "v2"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(Put(1, "foo", "v3"));
    ASSERT_OK(Put(1, "bar", "v4"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(Put(1, "big", std::string(100, 'a')));
    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());

    std::vector<std::vector<FileMetaData>> files;
    dbfull()->TEST_GetFilesMetaData(handles_[1], &files);
    size_t total_files = 0;
    for (const auto& level : files) {
      total_files += level.size();
    }
    ASSERT_EQ(total_files, 3);
    for (const auto& level : files) {
      for (const auto& file : level) {
        if (kInfiniteMaxOpenFiles == option_config_) {
          ASSERT_TRUE(file.table_reader_handle != nullptr);
        } else {
          ASSERT_TRUE(file.table_reader_handle == nullptr);
        }
      }
    }
  } while (ChangeOptions());
}

TEST_F(DBTest, IgnoreRecoveredLog) {
  std::string backup_logs = dbname_ + "/backup_logs";

  // delete old files in backup_logs directory
  ASSERT_OK(env_->CreateDirIfMissing(backup_logs));
  std::vector<std::string> old_files;
  ASSERT_OK(env_->GetChildren(backup_logs, &old_files));
  for (auto& file : old_files) {
    if (file != "." && file != "..") {
      ASSERT_OK(env_->DeleteFile(backup_logs + "/" + file));
    }
  }

  do {
    Options options = CurrentOptions();
    options.create_if_missing = true;
    options.merge_operator = MergeOperators::CreateUInt64AddOperator();
    options.wal_dir = dbname_ + "/logs";
    DestroyAndReopen(options);

    // fill up the DB
    std::string one, two;
    PutFixed64(&one, 1);
    PutFixed64(&two, 2);
    ASSERT_OK(db_->Merge(WriteOptions(), Slice("foo"), Slice(one)));
    ASSERT_OK(db_->Merge(WriteOptions(), Slice("foo"), Slice(one)));
    ASSERT_OK(db_->Merge(WriteOptions(), Slice("bar"), Slice(one)));

    // copy the logs to backup
    std::vector<std::string> logs;
    ASSERT_OK(env_->GetChildren(options.wal_dir, &logs));
    for (auto& log : logs) {
      if (log != ".." && log != ".") {
        CopyFile(options.wal_dir + "/" + log, backup_logs + "/" + log);
      }
    }

    // recover the DB
    Reopen(options);
    ASSERT_EQ(two, Get("foo"));
    ASSERT_EQ(one, Get("bar"));
    Close();

    // copy the logs from backup back to wal dir
    for (auto& log : logs) {
      if (log != ".." && log != ".") {
        CopyFile(backup_logs + "/" + log, options.wal_dir + "/" + log);
      }
    }
    // this should ignore the log files, recovery should not happen again
    // if the recovery happens, the same merge operator would be called twice,
    // leading to incorrect results
    Reopen(options);
    ASSERT_EQ(two, Get("foo"));
    ASSERT_EQ(one, Get("bar"));
    Close();
    Destroy(options);
    Reopen(options);
    Close();

    // copy the logs from backup back to wal dir
    ASSERT_OK(env_->CreateDirIfMissing(options.wal_dir));
    for (auto& log : logs) {
      if (log != ".." && log != ".") {
        CopyFile(backup_logs + "/" + log, options.wal_dir + "/" + log);
      }
    }
    // assert that we successfully recovered only from logs, even though we
    // destroyed the DB
    Reopen(options);
    ASSERT_EQ(two, Get("foo"));
    ASSERT_EQ(one, Get("bar"));

    // Recovery will fail if DB directory doesn't exist.
    Destroy(options);
    // copy the logs from backup back to wal dir
    ASSERT_OK(env_->CreateDirIfMissing(options.wal_dir));
    for (auto& log : logs) {
      if (log != ".." && log != ".") {
        CopyFile(backup_logs + "/" + log, options.wal_dir + "/" + log);
        // we won't be needing this file no more
        ASSERT_OK(env_->DeleteFile(backup_logs + "/" + log));
      }
    }
    Status s = TryReopen(options);
    ASSERT_TRUE(!s.ok());
  } while (ChangeOptions());
}

TEST_F(DBTest, CheckLock) {
  do {
    DB* localdb;
    Options options = CurrentOptions();
    ASSERT_OK(TryReopen(options));

    // second open should fail
    ASSERT_TRUE(!(DB::Open(options, dbname_, &localdb)).ok());
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, FlushMultipleMemtable) {
  do {
    Options options = CurrentOptions();
    WriteOptions writeOpt = WriteOptions();
    writeOpt.disableWAL = true;
    options.max_write_buffer_number = 4;
    options.min_write_buffer_number_to_merge = 3;
    options.max_write_buffer_number_to_maintain = -1;
    CreateAndReopenWithCF({"pikachu"}, options);
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v1"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v1"));

    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_EQ("v1", Get(1, "bar"));
    ASSERT_OK(Flush(1));
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, FlushEmptyColumnFamily) {
  // Block flush thread and disable compaction thread
  env_->SetBackgroundThreads(1, Env::HIGH);
  env_->SetBackgroundThreads(1, Env::LOW);
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  test::SleepingBackgroundTask sleeping_task_high;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask,
                 &sleeping_task_high, Env::Priority::HIGH);

  Options options = CurrentOptions();
  // disable compaction
  options.disable_auto_compactions = true;
  WriteOptions writeOpt = WriteOptions();
  writeOpt.disableWAL = true;
  options.max_write_buffer_number = 2;
  options.min_write_buffer_number_to_merge = 1;
  options.max_write_buffer_number_to_maintain = 1;
  CreateAndReopenWithCF({"pikachu"}, options);

  // Compaction can still go through even if no thread can flush the
  // mem table.
  ASSERT_OK(Flush(0));
  ASSERT_OK(Flush(1));

  // Insert can go through
  ASSERT_OK(dbfull()->Put(writeOpt, handles_[0], "foo", "v1"));
  ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v1"));

  ASSERT_EQ("v1", Get(0, "foo"));
  ASSERT_EQ("v1", Get(1, "bar"));

  sleeping_task_high.WakeUp();
  sleeping_task_high.WaitUntilDone();

  // Flush can still go through.
  ASSERT_OK(Flush(0));
  ASSERT_OK(Flush(1));

  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();
}

TEST_F(DBTest, FLUSH) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    WriteOptions writeOpt = WriteOptions();
    writeOpt.disableWAL = true;
    SetPerfLevel(PerfLevel::kEnableTime);;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v1"));
    // this will now also flush the last 2 writes
    ASSERT_OK(Flush(1));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v1"));

    perf_context.Reset();
    Get(1, "foo");
    ASSERT_GT(perf_context.get_from_output_files_time, 0);

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_EQ("v1", Get(1, "bar"));

    writeOpt.disableWAL = true;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v2"));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v2"));
    ASSERT_OK(Flush(1));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_EQ("v2", Get(1, "bar"));
    perf_context.Reset();
    ASSERT_EQ("v2", Get(1, "foo"));
    ASSERT_GT(perf_context.get_from_output_files_time, 0);

    writeOpt.disableWAL = false;
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "bar", "v3"));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "foo", "v3"));
    ASSERT_OK(Flush(1));

    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    // 'foo' should be there because its put
    // has WAL enabled.
    ASSERT_EQ("v3", Get(1, "foo"));
    ASSERT_EQ("v3", Get(1, "bar"));

    SetPerfLevel(PerfLevel::kDisable);
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, RecoveryWithEmptyLog) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Put(1, "foo", "v2"));
    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "v3"));
    ReopenWithColumnFamilies({"default", "pikachu"}, CurrentOptions());
    ASSERT_EQ("v3", Get(1, "foo"));
  } while (ChangeOptions());
}

TEST_F(DBTest, FlushSchedule) {
  Options options = CurrentOptions();
  options.disable_auto_compactions = true;
  options.level0_stop_writes_trigger = 1 << 10;
  options.level0_slowdown_writes_trigger = 1 << 10;
  options.min_write_buffer_number_to_merge = 1;
  options.max_write_buffer_number_to_maintain = 1;
  options.max_write_buffer_number = 2;
  options.write_buffer_size = 120 * 1024;
  CreateAndReopenWithCF({"pikachu"}, options);
  std::vector<std::thread> threads;

  std::atomic<int> thread_num(0);
  // each column family will have 5 thread, each thread generating 2 memtables.
  // each column family should end up with 10 table files
  std::function<void()> fill_memtable_func = [&]() {
    int a = thread_num.fetch_add(1);
    Random rnd(a);
    WriteOptions wo;
    // this should fill up 2 memtables
    for (int k = 0; k < 5000; ++k) {
      ASSERT_OK(db_->Put(wo, handles_[a & 1], RandomString(&rnd, 13), ""));
    }
  };

  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(fill_memtable_func);
  }

  for (auto& t : threads) {
    t.join();
  }

  auto default_tables = GetNumberOfSstFilesForColumnFamily(db_, "default");
  auto pikachu_tables = GetNumberOfSstFilesForColumnFamily(db_, "pikachu");
  ASSERT_LE(default_tables, static_cast<uint64_t>(10));
  ASSERT_GT(default_tables, static_cast<uint64_t>(0));
  ASSERT_LE(pikachu_tables, static_cast<uint64_t>(10));
  ASSERT_GT(pikachu_tables, static_cast<uint64_t>(0));
}

TEST_F(DBTest, ManifestRollOver) {
  do {
    Options options;
    options.max_manifest_file_size = 10; // 10 bytes
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);
    {
      ASSERT_OK(Put(1, "manifest_key1", std::string(1000, '1')));
      ASSERT_OK(Put(1, "manifest_key2", std::string(1000, '2')));
      ASSERT_OK(Put(1, "manifest_key3", std::string(1000, '3')));
      uint64_t manifest_before_flush = dbfull()->TEST_Current_Manifest_FileNo();
      ASSERT_OK(Flush(1));  // This should trigger LogAndApply.
      uint64_t manifest_after_flush = dbfull()->TEST_Current_Manifest_FileNo();
      ASSERT_GT(manifest_after_flush, manifest_before_flush);
      ReopenWithColumnFamilies({"default", "pikachu"}, options);
      ASSERT_GT(dbfull()->TEST_Current_Manifest_FileNo(), manifest_after_flush);
      // check if a new manifest file got inserted or not.
      ASSERT_EQ(std::string(1000, '1'), Get(1, "manifest_key1"));
      ASSERT_EQ(std::string(1000, '2'), Get(1, "manifest_key2"));
      ASSERT_EQ(std::string(1000, '3'), Get(1, "manifest_key3"));
    }
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, IdentityAcrossRestarts) {
  do {
    std::string id1;
    ASSERT_OK(db_->GetDbIdentity(&id1));

    Options options = CurrentOptions();
    Reopen(options);
    std::string id2;
    ASSERT_OK(db_->GetDbIdentity(&id2));
    // id1 should match id2 because identity was not regenerated
    ASSERT_EQ(id1.compare(id2), 0);

    std::string idfilename = IdentityFileName(dbname_);
    ASSERT_OK(env_->DeleteFile(idfilename));
    Reopen(options);
    std::string id3;
    ASSERT_OK(db_->GetDbIdentity(&id3));
    // id1 should NOT match id3 because identity was regenerated
    ASSERT_NE(id1.compare(id3), 0);
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, RecoverWithLargeLog) {
  do {
    {
      Options options = CurrentOptions();
      CreateAndReopenWithCF({"pikachu"}, options);
      ASSERT_OK(Put(1, "big1", std::string(200000, '1')));
      ASSERT_OK(Put(1, "big2", std::string(200000, '2')));
      ASSERT_OK(Put(1, "small3", std::string(10, '3')));
      ASSERT_OK(Put(1, "small4", std::string(10, '4')));
      ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
    }

    // Make sure that if we re-open with a small write buffer size that
    // we flush table files in the middle of a large log file.
    Options options;
    options.write_buffer_size = 100000;
    options = CurrentOptions(options);
    ReopenWithColumnFamilies({"default", "pikachu"}, options);
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 3);
    ASSERT_EQ(std::string(200000, '1'), Get(1, "big1"));
    ASSERT_EQ(std::string(200000, '2'), Get(1, "big2"));
    ASSERT_EQ(std::string(10, '3'), Get(1, "small3"));
    ASSERT_EQ(std::string(10, '4'), Get(1, "small4"));
    ASSERT_GT(NumTableFilesAtLevel(0, 1), 1);
  } while (ChangeCompactOptions());
}

namespace {
class KeepFilter : public CompactionFilter {
 public:
  FilterDecision Filter(int level, const Slice& key, const Slice& value,
                        std::string* new_value, bool* value_changed) override {
    return FilterDecision::kKeep;
  }

  const char* Name() const override { return "KeepFilter"; }
};

class KeepFilterFactory : public CompactionFilterFactory {
 public:
  explicit KeepFilterFactory(bool check_context = false)
      : check_context_(check_context) {}

  virtual std::unique_ptr<CompactionFilter> CreateCompactionFilter(
      const CompactionFilter::Context& context) override {
    if (check_context_) {
      EXPECT_EQ(expect_full_compaction_.load(), context.is_full_compaction);
      EXPECT_EQ(expect_manual_compaction_.load(), context.is_manual_compaction);
    }
    return std::unique_ptr<CompactionFilter>(new KeepFilter());
  }

  const char* Name() const override { return "KeepFilterFactory"; }
  bool check_context_;
  std::atomic_bool expect_full_compaction_;
  std::atomic_bool expect_manual_compaction_;
};

class DelayFilter : public CompactionFilter {
 public:
  explicit DelayFilter(DBTestBase* d) : db_test(d) {}
  FilterDecision Filter(int level, const Slice& key, const Slice& value,
                        std::string* new_value,
                        bool* value_changed) override {
    db_test->env_->addon_time_.fetch_add(1000);
    return FilterDecision::kDiscard;
  }

  const char* Name() const override { return "DelayFilter"; }

 private:
  DBTestBase* db_test;
};

class DelayFilterFactory : public CompactionFilterFactory {
 public:
  explicit DelayFilterFactory(DBTestBase* d) : db_test(d) {}
  virtual std::unique_ptr<CompactionFilter> CreateCompactionFilter(
      const CompactionFilter::Context& context) override {
    return std::unique_ptr<CompactionFilter>(new DelayFilter(db_test));
  }

  const char* Name() const override { return "DelayFilterFactory"; }

 private:
  DBTestBase* db_test;
};
}  // namespace

TEST_F(DBTest, CompressedCache) {
  if (!Snappy_Supported()) {
    return;
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_allow_table_option_compressed_block_cache) = true;
  int num_iter = 80;

  // Run this test three iterations.
  // Iteration 1: only a uncompressed block cache
  // Iteration 2: only a compressed block cache
  // Iteration 3: both block cache and compressed cache
  // Iteration 4: both block cache and compressed cache, but DB is not
  // compressed
  for (int iter = 0; iter < 4; iter++) {
    Options options;
    options.write_buffer_size = 64*1024;        // small write buffer
    options.statistics = rocksdb::CreateDBStatisticsForTests();
    options = CurrentOptions(options);

    BlockBasedTableOptions table_options;
    switch (iter) {
      case 0:
        // only uncompressed block cache
        table_options.block_cache = NewLRUCache(8*1024);
        table_options.block_cache_compressed = nullptr;
        options.table_factory.reset(NewBlockBasedTableFactory(table_options));
        break;
      case 1:
        // no block cache, only compressed cache
        table_options.no_block_cache = true;
        table_options.block_cache = nullptr;
        table_options.block_cache_compressed = NewLRUCache(8*1024);
        options.table_factory.reset(NewBlockBasedTableFactory(table_options));
        break;
      case 2:
        // both compressed and uncompressed block cache
        table_options.block_cache = NewLRUCache(1024);
        table_options.block_cache_compressed = NewLRUCache(8*1024);
        options.table_factory.reset(NewBlockBasedTableFactory(table_options));
        break;
      case 3:
        // both block cache and compressed cache, but DB is not compressed
        // also, make block cache sizes bigger, to trigger block cache hits
        table_options.block_cache = NewLRUCache(1024 * 1024);
        table_options.block_cache_compressed = NewLRUCache(8 * 1024 * 1024);
        options.table_factory.reset(NewBlockBasedTableFactory(table_options));
        options.compression = kNoCompression;
        break;
      default:
        ASSERT_TRUE(false);
    }
    CreateAndReopenWithCF({"pikachu"}, options);
    // default column family doesn't have block cache
    Options no_block_cache_opts;
    no_block_cache_opts.statistics = options.statistics;
    no_block_cache_opts = CurrentOptions(no_block_cache_opts);
    BlockBasedTableOptions table_options_no_bc;
    table_options_no_bc.no_block_cache = true;
    no_block_cache_opts.table_factory.reset(
        NewBlockBasedTableFactory(table_options_no_bc));
    ReopenWithColumnFamilies({"default", "pikachu"},
        std::vector<Options>({no_block_cache_opts, options}));

    Random rnd(301);

    // Write 8MB (80 values, each 100K)
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
    std::vector<std::string> values;
    std::string str;
    for (int i = 0; i < num_iter; i++) {
      if (i % 4 == 0) {        // high compression ratio
        str = RandomString(&rnd, 1000);
      }
      values.push_back(str);
      ASSERT_OK(Put(1, Key(i), values[i]));
    }

    // flush all data from memtable so that reads are from block cache
    ASSERT_OK(Flush(1));

    for (int i = 0; i < num_iter; i++) {
      ASSERT_EQ(Get(1, Key(i)), values[i]);
    }

    // check that we triggered the appropriate code paths in the cache
    switch (iter) {
      case 0:
        // only uncompressed block cache
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_MISS), 0);
        ASSERT_EQ(TestGetTickerCount(options, BLOCK_CACHE_COMPRESSED_MISS), 0);
        break;
      case 1:
        // no block cache, only compressed cache
        ASSERT_EQ(TestGetTickerCount(options, BLOCK_CACHE_MISS), 0);
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_COMPRESSED_MISS), 0);
        break;
      case 2:
        // both compressed and uncompressed block cache
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_MISS), 0);
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_COMPRESSED_MISS), 0);
        break;
      case 3:
        // both compressed and uncompressed block cache
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_MISS), 0);
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_HIT), 0);
        ASSERT_EQ(TestGetTickerCount(options, BLOCK_CACHE_SINGLE_TOUCH_HIT) +
                  TestGetTickerCount(options, BLOCK_CACHE_MULTI_TOUCH_HIT),
                  TestGetTickerCount(options, BLOCK_CACHE_HIT));
        ASSERT_GT(TestGetTickerCount(options, BLOCK_CACHE_COMPRESSED_MISS), 0);
        // compressed doesn't have any hits since blocks are not compressed on
        // storage
        ASSERT_EQ(TestGetTickerCount(options, BLOCK_CACHE_COMPRESSED_HIT), 0);
        break;
      default:
        ASSERT_TRUE(false);
    }

    options.create_if_missing = true;
    DestroyAndReopen(options);
  }
}

static std::string CompressibleString(Random* rnd, int len) {
  std::string r;
  CompressibleString(rnd, 0.8, len, &r);
  return r;
}

TEST_F(DBTest, FailMoreDbPaths) {
  Options options = CurrentOptions();
  options.db_paths.emplace_back(dbname_, 10000000);
  options.db_paths.emplace_back(dbname_ + "_2", 1000000);
  options.db_paths.emplace_back(dbname_ + "_3", 1000000);
  options.db_paths.emplace_back(dbname_ + "_4", 1000000);
  options.db_paths.emplace_back(dbname_ + "_5", 1000000);
  ASSERT_TRUE(TryReopen(options).IsNotSupported());
}

void CheckColumnFamilyMeta(const ColumnFamilyMetaData& cf_meta) {
  uint64_t cf_size = 0;
  size_t file_count = 0;
  for (auto level_meta : cf_meta.levels) {
    uint64_t level_size = 0;
    uint64_t level_csize = 0;
    file_count += level_meta.files.size();
    for (auto file_meta : level_meta.files) {
      level_size += file_meta.total_size;
    }
    ASSERT_EQ(level_meta.size, level_size);
    cf_size += level_size;
  }
  ASSERT_EQ(cf_meta.file_count, file_count);
  ASSERT_EQ(cf_meta.size, cf_size);
}

TEST_F(DBTest, ColumnFamilyMetaDataTest) {
  Options options = CurrentOptions();
  options.create_if_missing = true;
  DestroyAndReopen(options);

  Random rnd(301);
  int key_index = 0;
  ColumnFamilyMetaData cf_meta;
  for (int i = 0; i < 100; ++i) {
    GenerateNewFile(&rnd, &key_index);
    db_->GetColumnFamilyMetaData(&cf_meta);
    CheckColumnFamilyMeta(cf_meta);
  }
}

namespace {
void MinLevelHelper(DBTest* self, const Options& options) {
  Random rnd(301);

  for (int num = 0;
    num < options.level0_file_num_compaction_trigger - 1;
    num++) {
    std::vector<std::string> values;
    // Write 120KB (12 values, each 10K)
    for (int i = 0; i < 12; i++) {
      values.push_back(RandomString(&rnd, 10000));
      ASSERT_OK(self->Put(DBTestBase::Key(i), values[i]));
    }
    ASSERT_OK(self->dbfull()->TEST_WaitForFlushMemTable());
    ASSERT_EQ(self->NumTableFilesAtLevel(0), num + 1);
  }

  // generate one more file in level-0, and should trigger level-0 compaction
  std::vector<std::string> values;
  for (int i = 0; i < 12; i++) {
    values.push_back(RandomString(&rnd, 10000));
    ASSERT_OK(self->Put(DBTestBase::Key(i), values[i]));
  }
  ASSERT_OK(self->dbfull()->TEST_WaitForCompact());

  ASSERT_EQ(self->NumTableFilesAtLevel(0), 0);
  ASSERT_EQ(self->NumTableFilesAtLevel(1), 1);
}

// returns false if the calling-Test should be skipped
bool MinLevelToCompress(int wbits, int lev, int strategy,
    CompressionType* type, Options* options) {
  fprintf(stderr,
      "Test with compression options : window_bits = %d, level =  %d, strategy = %d}\n",
      wbits, lev, strategy);
  options->write_buffer_size = 100 << 10; // 100KB
  options->arena_block_size = 4096;
  options->num_levels = 3;
  options->level0_file_num_compaction_trigger = 3;
  options->create_if_missing = true;

  if (Snappy_Supported()) {
    *type = kSnappyCompression;
    fprintf(stderr, "using snappy\n");
  } else if (Zlib_Supported()) {
    *type = kZlibCompression;
    fprintf(stderr, "using zlib\n");
  } else if (BZip2_Supported()) {
    *type = kBZip2Compression;
    fprintf(stderr, "using bzip2\n");
  } else if (LZ4_Supported()) {
    *type = kLZ4Compression;
    fprintf(stderr, "using lz4\n");
  } else {
    fprintf(stderr, "skipping test, compression disabled\n");
    return false;
  }
  options->compression_per_level.resize(options->num_levels);

  // do not compress L0
  for (int i = 0; i < 1; i++) {
    options->compression_per_level[i] = kNoCompression;
  }
  for (int i = 1; i < options->num_levels; i++) {
    options->compression_per_level[i] = *type;
  }
  return true;
}
}  // namespace

TEST_F(DBTest, MinLevelToCompress1) {
  Options options = CurrentOptions();
  CompressionType type = kSnappyCompression;
  if (!MinLevelToCompress(-14, -1, 0, &type, &options)) {
    return;
  }
  Reopen(options);
  MinLevelHelper(this, options);

  // do not compress L0 and L1
  for (int i = 0; i < 2; i++) {
    options.compression_per_level[i] = kNoCompression;
  }
  for (int i = 2; i < options.num_levels; i++) {
    options.compression_per_level[i] = type;
  }
  DestroyAndReopen(options);
  MinLevelHelper(this, options);
}

TEST_F(DBTest, MinLevelToCompress2) {
  Options options = CurrentOptions();
  CompressionType type = kSnappyCompression;
  if (!MinLevelToCompress(15, -1, 0, &type, &options)) {
    return;
  }
  Reopen(options);
  MinLevelHelper(this, options);

  // do not compress L0 and L1
  for (int i = 0; i < 2; i++) {
    options.compression_per_level[i] = kNoCompression;
  }
  for (int i = 2; i < options.num_levels; i++) {
    options.compression_per_level[i] = type;
  }
  DestroyAndReopen(options);
  MinLevelHelper(this, options);
}

TEST_F(DBTest, RepeatedWritesToSameKey) {
  do {
    Options options;
    options.env = env_;
    options.write_buffer_size = 100000;  // Small write buffer
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // We must have at most one file per level except for level-0,
    // which may have up to kL0_StopWritesTrigger files.
    const int kMaxFiles =
        options.num_levels + options.level0_stop_writes_trigger;

    Random rnd(301);
    std::string value =
        RandomString(&rnd, static_cast<int>(2 * options.write_buffer_size));
    for (int i = 0; i < 5 * kMaxFiles; i++) {
      ASSERT_OK(Put(1, "key", value));
      ASSERT_LE(TotalTableFiles(1), kMaxFiles);
    }
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, SparseMerge) {
  do {
    Options options = CurrentOptions();
    options.compression = kNoCompression;
    CreateAndReopenWithCF({"pikachu"}, options);

    FillLevels("A", "Z", 1);

    // Suppose there is:
    //    small amount of data with prefix A
    //    large amount of data with prefix B
    //    small amount of data with prefix C
    // and that recent updates have made small changes to all three prefixes.
    // Check that we do not do a compaction that merges all of B in one shot.
    const std::string value(1000, 'x');
    ASSERT_OK(Put(1, "A", "va"));
    // Write approximately 100MB of "B" values
    for (int i = 0; i < 100000; i++) {
      char key[100];
      snprintf(key, sizeof(key), "B%010d", i);
      ASSERT_OK(Put(1, key, value));
    }
    ASSERT_OK(Put(1, "C", "vc"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr, handles_[1]));

    // Make sparse update
    ASSERT_OK(Put(1, "A", "va2"));
    ASSERT_OK(Put(1, "B100", "bvalue2"));
    ASSERT_OK(Put(1, "C", "vc2"));
    ASSERT_OK(Flush(1));

    // Compactions should not cause us to create a situation where
    // a file overlaps too much data at the next level.
    ASSERT_LE(dbfull()->TEST_MaxNextLevelOverlappingBytes(handles_[1]),
              20 * 1048576);
    ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr));
    ASSERT_LE(dbfull()->TEST_MaxNextLevelOverlappingBytes(handles_[1]),
              20 * 1048576);
    ASSERT_OK(dbfull()->TEST_CompactRange(1, nullptr, nullptr));
    ASSERT_LE(dbfull()->TEST_MaxNextLevelOverlappingBytes(handles_[1]),
              20 * 1048576);
  } while (ChangeCompactOptions());
}

static bool Between(uint64_t val, uint64_t low, uint64_t high) {
  bool result = (val >= low) && (val <= high);
  if (!result) {
    fprintf(stderr, "Value %" PRIu64 " is not in range [%" PRIu64 ", %" PRIu64 "]\n",
            val,
            low,
            high);
  }
  return result;
}

TEST_F(DBTest, ApproximateSizesMemTable) {
  Options options;
  options.write_buffer_size = 100000000;  // Large write buffer
  options.compression = kNoCompression;
  options.create_if_missing = true;
  options = CurrentOptions(options);
  DestroyAndReopen(options);

  const int N = 128;
  Random rnd(301);
  for (int i = 0; i < N; i++) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 1024)));
  }

  uint64_t size;
  std::string start = Key(50);
  std::string end = Key(60);
  Range r(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_GT(size, 6000);
  ASSERT_LT(size, 204800);
  // Zero if not including mem table
  db_->GetApproximateSizes(&r, 1, &size, false);
  ASSERT_EQ(size, 0);

  start = Key(500);
  end = Key(600);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_EQ(size, 0);

  for (int i = 0; i < N; i++) {
    ASSERT_OK(Put(Key(1000 + i), RandomString(&rnd, 1024)));
  }

  start = Key(500);
  end = Key(600);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_EQ(size, 0);

  start = Key(100);
  end = Key(1020);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_GT(size, 6000);

  options.max_write_buffer_number = 8;
  options.min_write_buffer_number_to_merge = 5;
  options.write_buffer_size = 1024 * N;  // Not very large
  DestroyAndReopen(options);

  int keys[N * 3];
  for (int i = 0; i < N; i++) {
    keys[i * 3] = i * 5;
    keys[i * 3 + 1] = i * 5 + 1;
    keys[i * 3 + 2] = i * 5 + 2;
  }
  std::shuffle(std::begin(keys), std::end(keys), yb::ThreadLocalRandom());

  for (int i = 0; i < N * 3; i++) {
    ASSERT_OK(Put(Key(keys[i] + 1000), RandomString(&rnd, 1024)));
  }

  start = Key(100);
  end = Key(300);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_EQ(size, 0);

  start = Key(1050);
  end = Key(1080);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_GT(size, 6000);

  start = Key(2100);
  end = Key(2300);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size, true);
  ASSERT_EQ(size, 0);

  start = Key(1050);
  end = Key(1080);
  r = Range(start, end);
  uint64_t size_with_mt, size_without_mt;
  db_->GetApproximateSizes(&r, 1, &size_with_mt, true);
  ASSERT_GT(size_with_mt, 6000);
  db_->GetApproximateSizes(&r, 1, &size_without_mt, false);
  ASSERT_EQ(size_without_mt, 0);

  ASSERT_OK(Flush());

  for (int i = 0; i < N; i++) {
    ASSERT_OK(Put(Key(i + 1000), RandomString(&rnd, 1024)));
  }

  start = Key(1050);
  end = Key(1080);
  r = Range(start, end);
  db_->GetApproximateSizes(&r, 1, &size_with_mt, true);
  db_->GetApproximateSizes(&r, 1, &size_without_mt, false);
  ASSERT_GT(size_with_mt, size_without_mt);
  ASSERT_GT(size_without_mt, 6000);
}

TEST_F(DBTest, ApproximateSizes) {
  do {
    Options options;
    options.write_buffer_size = 100000000;        // Large write buffer
    options.compression = kNoCompression;
    options.create_if_missing = true;
    options = CurrentOptions(options);
    DestroyAndReopen(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_TRUE(Between(Size("", "xyz", 1), 0, 0));
    ReopenWithColumnFamilies({"default", "pikachu"}, options);
    ASSERT_TRUE(Between(Size("", "xyz", 1), 0, 0));

    // Write 8MB (80 values, each 100K)
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
    const int N = 80;
    static const int S1 = 100000;
    static const int S2 = 105000;  // Allow some expansion from metadata
    Random rnd(301);
    for (int i = 0; i < N; i++) {
      ASSERT_OK(Put(1, Key(i), RandomString(&rnd, S1)));
    }

    // 0 because GetApproximateSizes() does not account for memtable space
    ASSERT_TRUE(Between(Size("", Key(50), 1), 0, 0));

    // Check sizes across recovery by reopening a few times
    for (int run = 0; run < 3; run++) {
      ReopenWithColumnFamilies({"default", "pikachu"}, options);

      for (int compact_start = 0; compact_start < N; compact_start += 10) {
        for (int i = 0; i < N; i += 10) {
          ASSERT_TRUE(Between(Size("", Key(i), 1), S1 * i, S2 * i));
          ASSERT_TRUE(Between(Size("", Key(i) + ".suffix", 1), S1 * (i + 1),
                              S2 * (i + 1)));
          ASSERT_TRUE(Between(Size(Key(i), Key(i + 10), 1), S1 * 10, S2 * 10));
        }
        ASSERT_TRUE(Between(Size("", Key(50), 1), S1 * 50, S2 * 50));
        ASSERT_TRUE(
            Between(Size("", Key(50) + ".suffix", 1), S1 * 50, S2 * 50));

        std::string cstart_str = Key(compact_start);
        std::string cend_str = Key(compact_start + 9);
        Slice cstart = cstart_str;
        Slice cend = cend_str;
        ASSERT_OK(dbfull()->TEST_CompactRange(0, &cstart, &cend, handles_[1]));
      }

      ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
      ASSERT_GT(NumTableFilesAtLevel(1, 1), 0);
    }
    // ApproximateOffsetOf() is not yet implemented in plain table format.
  } while (ChangeOptions(kSkipUniversalCompaction | kSkipFIFOCompaction |
                         kSkipPlainTable | kSkipHashIndex));
}

TEST_F(DBTest, ApproximateSizes_MixOfSmallAndLarge) {
  do {
    Options options = CurrentOptions();
    options.compression = kNoCompression;
    CreateAndReopenWithCF({"pikachu"}, options);

    Random rnd(301);
    std::string big1 = RandomString(&rnd, 100000);
    ASSERT_OK(Put(1, Key(0), RandomString(&rnd, 10000)));
    ASSERT_OK(Put(1, Key(1), RandomString(&rnd, 10000)));
    ASSERT_OK(Put(1, Key(2), big1));
    ASSERT_OK(Put(1, Key(3), RandomString(&rnd, 10000)));
    ASSERT_OK(Put(1, Key(4), big1));
    ASSERT_OK(Put(1, Key(5), RandomString(&rnd, 10000)));
    ASSERT_OK(Put(1, Key(6), RandomString(&rnd, 300000)));
    ASSERT_OK(Put(1, Key(7), RandomString(&rnd, 10000)));

    // Check sizes across recovery by reopening a few times
    for (int run = 0; run < 3; run++) {
      ReopenWithColumnFamilies({"default", "pikachu"}, options);

      ASSERT_TRUE(Between(Size("", Key(0), 1), 0, 0));
      ASSERT_TRUE(Between(Size("", Key(1), 1), 10000, 11000));
      ASSERT_TRUE(Between(Size("", Key(2), 1), 20000, 21000));
      ASSERT_TRUE(Between(Size("", Key(3), 1), 120000, 121000));
      ASSERT_TRUE(Between(Size("", Key(4), 1), 130000, 131000));
      ASSERT_TRUE(Between(Size("", Key(5), 1), 230000, 231000));
      ASSERT_TRUE(Between(Size("", Key(6), 1), 240000, 241000));
      ASSERT_TRUE(Between(Size("", Key(7), 1), 540000, 541000));
      ASSERT_TRUE(Between(Size("", Key(8), 1), 550000, 560000));

      ASSERT_TRUE(Between(Size(Key(3), Key(5), 1), 110000, 111000));

      ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr, handles_[1]));
    }
    // ApproximateOffsetOf() is not yet implemented in plain table format.
  } while (ChangeOptions(kSkipPlainTable));
}

TEST_F(DBTest, IteratorPinsRef) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "foo", "hello"));

    // Get iterator that will yield the current contents of the DB.
    Iterator* iter = db_->NewIterator(ReadOptions(), handles_[1]);

    // Write to force compactions
    ASSERT_OK(Put(1, "foo", "newvalue1"));
    for (int i = 0; i < 100; i++) {
      // 100K values
      ASSERT_OK(Put(1, Key(i), Key(i) + std::string(100000, 'v')));
    }
    ASSERT_OK(Put(1, "foo", "newvalue2"));

    iter->SeekToFirst();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ("foo", iter->key().ToString());
    ASSERT_EQ("hello", iter->value().ToString());
    iter->Next();
    ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
    delete iter;
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, Snapshot) {
  anon::OptionsOverride options_override;

  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions(options_override));
    ASSERT_OK(Put(0, "foo", "0v1"));
    ASSERT_OK(Put(1, "foo", "1v1"));

    const Snapshot* s1 = db_->GetSnapshot();
    ASSERT_EQ(1U, GetNumSnapshots());
    uint64_t time_snap1 = GetTimeOldestSnapshots();
    ASSERT_GT(time_snap1, 0U);
    ASSERT_OK(Put(0, "foo", "0v2"));
    ASSERT_OK(Put(1, "foo", "1v2"));

    env_->addon_time_.fetch_add(1);

    const Snapshot* s2 = db_->GetSnapshot();
    ASSERT_EQ(2U, GetNumSnapshots());
    ASSERT_EQ(time_snap1, GetTimeOldestSnapshots());
    ASSERT_OK(Put(0, "foo", "0v3"));
    ASSERT_OK(Put(1, "foo", "1v3"));

    {
      ManagedSnapshot s3(db_);
      ASSERT_EQ(3U, GetNumSnapshots());
      ASSERT_EQ(time_snap1, GetTimeOldestSnapshots());

      ASSERT_OK(Put(0, "foo", "0v4"));
      ASSERT_OK(Put(1, "foo", "1v4"));
      ASSERT_EQ("0v1", Get(0, "foo", s1));
      ASSERT_EQ("1v1", Get(1, "foo", s1));
      ASSERT_EQ("0v2", Get(0, "foo", s2));
      ASSERT_EQ("1v2", Get(1, "foo", s2));
      ASSERT_EQ("0v3", Get(0, "foo", s3.snapshot()));
      ASSERT_EQ("1v3", Get(1, "foo", s3.snapshot()));
      ASSERT_EQ("0v4", Get(0, "foo"));
      ASSERT_EQ("1v4", Get(1, "foo"));
    }

    ASSERT_EQ(2U, GetNumSnapshots());
    ASSERT_EQ(time_snap1, GetTimeOldestSnapshots());
    ASSERT_EQ("0v1", Get(0, "foo", s1));
    ASSERT_EQ("1v1", Get(1, "foo", s1));
    ASSERT_EQ("0v2", Get(0, "foo", s2));
    ASSERT_EQ("1v2", Get(1, "foo", s2));
    ASSERT_EQ("0v4", Get(0, "foo"));
    ASSERT_EQ("1v4", Get(1, "foo"));

    db_->ReleaseSnapshot(s1);
    ASSERT_EQ("0v2", Get(0, "foo", s2));
    ASSERT_EQ("1v2", Get(1, "foo", s2));
    ASSERT_EQ("0v4", Get(0, "foo"));
    ASSERT_EQ("1v4", Get(1, "foo"));
    ASSERT_EQ(1U, GetNumSnapshots());
    ASSERT_LT(time_snap1, GetTimeOldestSnapshots());

    db_->ReleaseSnapshot(s2);
    ASSERT_EQ(0U, GetNumSnapshots());
    ASSERT_EQ("0v4", Get(0, "foo"));
    ASSERT_EQ("1v4", Get(1, "foo"));
  } while (ChangeOptions());
}

TEST_F(DBTest, HiddenValuesAreRemoved) {
  anon::OptionsOverride options_override;

  do {
    Options options = CurrentOptions(options_override);
    CreateAndReopenWithCF({"pikachu"}, options);
    Random rnd(301);
    FillLevels("a", "z", 1);

    std::string big = RandomString(&rnd, 50000);
    ASSERT_OK(Put(1, "foo", big));
    ASSERT_OK(Put(1, "pastfoo", "v"));
    const Snapshot* snapshot = db_->GetSnapshot();
    ASSERT_OK(Put(1, "foo", "tiny"));
    ASSERT_OK(Put(1, "pastfoo2", "v2"));  // Advance sequence number one more

    ASSERT_OK(Flush(1));
    ASSERT_GT(NumTableFilesAtLevel(0, 1), 0);

    ASSERT_EQ(big, Get(1, "foo", snapshot));
    ASSERT_TRUE(Between(Size("", "pastfoo", 1), 50000, 60000));
    db_->ReleaseSnapshot(snapshot);
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ tiny, " + big + " ]");
    Slice x("x");
    ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, &x, handles_[1]));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ tiny ]");
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
    ASSERT_GE(NumTableFilesAtLevel(1, 1), 1);
    ASSERT_OK(dbfull()->TEST_CompactRange(1, nullptr, &x, handles_[1]));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ tiny ]");

    ASSERT_TRUE(Between(Size("", "pastfoo", 1), 0, 1000));
    // ApproximateOffsetOf() is not yet implemented in plain table format,
    // which is used by Size().
  } while (ChangeOptions(kSkipUniversalCompaction | kSkipFIFOCompaction | kSkipPlainTable));
}

TEST_F(DBTest, CompactBetweenSnapshots) {
  anon::OptionsOverride options_override;

  do {
    Options options = CurrentOptions(options_override);
    options.disable_auto_compactions = true;
    CreateAndReopenWithCF({"pikachu"}, options);
    Random rnd(301);
    FillLevels("a", "z", 1);

    ASSERT_OK(Put(1, "foo", "first"));
    const Snapshot* snapshot1 = db_->GetSnapshot();
    ASSERT_OK(Put(1, "foo", "second"));
    ASSERT_OK(Put(1, "foo", "third"));
    ASSERT_OK(Put(1, "foo", "fourth"));
    const Snapshot* snapshot2 = db_->GetSnapshot();
    ASSERT_OK(Put(1, "foo", "fifth"));
    ASSERT_OK(Put(1, "foo", "sixth"));

    // All entries (including duplicates) exist
    // before any compaction or flush is triggered.
    ASSERT_EQ(AllEntriesFor("foo", 1),
              "[ sixth, fifth, fourth, third, second, first ]");
    ASSERT_EQ("sixth", Get(1, "foo"));
    ASSERT_EQ("fourth", Get(1, "foo", snapshot2));
    ASSERT_EQ("first", Get(1, "foo", snapshot1));

    // After a flush, "second", "third" and "fifth" should
    // be removed
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ sixth, fourth, first ]");

    // after we release the snapshot1, only two values left
    db_->ReleaseSnapshot(snapshot1);
    FillLevels("a", "z", 1);
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));

    // We have only one valid snapshot snapshot2. Since snapshot1 is
    // not valid anymore, "first" should be removed by a compaction.
    ASSERT_EQ("sixth", Get(1, "foo"));
    ASSERT_EQ("fourth", Get(1, "foo", snapshot2));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ sixth, fourth ]");

    // after we release the snapshot2, only one value should be left
    db_->ReleaseSnapshot(snapshot2);
    FillLevels("a", "z", 1);
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ("sixth", Get(1, "foo"));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ sixth ]");
  } while (ChangeOptions(kSkipFIFOCompaction));
}

TEST_F(DBTest, UnremovableSingleDelete) {
  // If we compact:
  //
  // Put(A, v1) Snapshot SingleDelete(A) Put(A, v2)
  //
  // We do not want to end up with:
  //
  // Put(A, v1) Snapshot Put(A, v2)
  //
  // Because a subsequent SingleDelete(A) would delete the Put(A, v2)
  // but not Put(A, v1), so Get(A) would return v1.
  anon::OptionsOverride options_override;

  do {
    Options options = CurrentOptions(options_override);
    options.disable_auto_compactions = true;
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_OK(Put(1, "foo", "first"));
    const Snapshot* snapshot = db_->GetSnapshot();
    ASSERT_OK(SingleDelete(1, "foo"));
    ASSERT_OK(Put(1, "foo", "second"));
    ASSERT_OK(Flush(1));

    ASSERT_EQ("first", Get(1, "foo", snapshot));
    ASSERT_EQ("second", Get(1, "foo"));

    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ("[ second, SDEL, first ]", AllEntriesFor("foo", 1));

    ASSERT_OK(SingleDelete(1, "foo"));

    ASSERT_EQ("first", Get(1, "foo", snapshot));
    ASSERT_EQ("NOT_FOUND", Get(1, "foo"));

    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));

    ASSERT_EQ("first", Get(1, "foo", snapshot));
    ASSERT_EQ("NOT_FOUND", Get(1, "foo"));
    db_->ReleaseSnapshot(snapshot);
    // FIFO and universal compaction do not apply to the test case.
    // Skip MergePut because single delete does not get removed when it encounters a merge.
  } while (ChangeOptions(kSkipFIFOCompaction | kSkipUniversalCompaction | kSkipMergePut));
}

TEST_F(DBTest, DeletionMarkers1) {
  Options options = CurrentOptions();
  options.max_background_flushes = 0;
  CreateAndReopenWithCF({"pikachu"}, options);
  ASSERT_OK(Put(1, "foo", "v1"));
  ASSERT_OK(Flush(1));
  const int last = 2;
  MoveFilesToLevel(last, 1);
  // foo => v1 is now in last level
  ASSERT_EQ(NumTableFilesAtLevel(last, 1), 1);

  // Place a table at level last-1 to prevent merging with preceding mutation
  ASSERT_OK(Put(1, "a", "begin"));
  ASSERT_OK(Put(1, "z", "end"));
  ASSERT_OK(Flush(1));
  MoveFilesToLevel(last - 1, 1);
  ASSERT_EQ(NumTableFilesAtLevel(last, 1), 1);
  ASSERT_EQ(NumTableFilesAtLevel(last - 1, 1), 1);

  ASSERT_OK(Delete(1, "foo"));
  ASSERT_OK(Put(1, "foo", "v2"));
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2, DEL, v1 ]");
  ASSERT_OK(Flush(1));  // Moves to level last-2
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2, v1 ]");
  Slice z("z");
  ASSERT_OK(dbfull()->TEST_CompactRange(last - 2, nullptr, &z, handles_[1]));
  // DEL eliminated, but v1 remains because we aren't compacting that level
  // (DEL can be eliminated because v2 hides v1).
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactRange(last - 1, nullptr, nullptr, handles_[1]));
  // Merging last-1 w/ last, so we are the base level for "foo", so
  // DEL is removed.  (as is v1).
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2 ]");
}

TEST_F(DBTest, DeletionMarkers2) {
  Options options = CurrentOptions();
  CreateAndReopenWithCF({"pikachu"}, options);
  ASSERT_OK(Put(1, "foo", "v1"));
  ASSERT_OK(Flush(1));
  const int last = 2;
  MoveFilesToLevel(last, 1);
  // foo => v1 is now in last level
  ASSERT_EQ(NumTableFilesAtLevel(last, 1), 1);

  // Place a table at level last-1 to prevent merging with preceding mutation
  ASSERT_OK(Put(1, "a", "begin"));
  ASSERT_OK(Put(1, "z", "end"));
  ASSERT_OK(Flush(1));
  MoveFilesToLevel(last - 1, 1);
  ASSERT_EQ(NumTableFilesAtLevel(last, 1), 1);
  ASSERT_EQ(NumTableFilesAtLevel(last - 1, 1), 1);

  ASSERT_OK(Delete(1, "foo"));
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL, v1 ]");
  ASSERT_OK(Flush(1));  // Moves to level last-2
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactRange(last - 2, nullptr, nullptr, handles_[1]));
  // DEL kept: "last" file overlaps
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL, v1 ]");
  ASSERT_OK(dbfull()->TEST_CompactRange(last - 1, nullptr, nullptr, handles_[1]));
  // Merging last-1 w/ last, so we are the base level for "foo", so
  // DEL is removed.  (as is v1).
  ASSERT_EQ(AllEntriesFor("foo", 1), "[ ]");
}

TEST_F(DBTest, OverlapInLevel0) {
  do {
    Options options = CurrentOptions();
    CreateAndReopenWithCF({"pikachu"}, options);

    // Fill levels 1 and 2 to disable the pushing of new memtables to levels > 0.
    ASSERT_OK(Put(1, "100", "v100"));
    ASSERT_OK(Put(1, "999", "v999"));
    ASSERT_OK(Flush(1));
    MoveFilesToLevel(2, 1);
    ASSERT_OK(Delete(1, "100"));
    ASSERT_OK(Delete(1, "999"));
    ASSERT_OK(Flush(1));
    MoveFilesToLevel(1, 1);
    ASSERT_EQ("0,1,1", FilesPerLevel(1));

    // Make files spanning the following ranges in level-0:
    //  files[0]  200 .. 900
    //  files[1]  300 .. 500
    // Note that files are sorted by smallest key.
    ASSERT_OK(Put(1, "300", "v300"));
    ASSERT_OK(Put(1, "500", "v500"));
    ASSERT_OK(Flush(1));
    ASSERT_OK(Put(1, "200", "v200"));
    ASSERT_OK(Put(1, "600", "v600"));
    ASSERT_OK(Put(1, "900", "v900"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ("2,1,1", FilesPerLevel(1));

    // Compact away the placeholder files we created initially
    ASSERT_OK(dbfull()->TEST_CompactRange(1, nullptr, nullptr, handles_[1]));
    ASSERT_OK(dbfull()->TEST_CompactRange(2, nullptr, nullptr, handles_[1]));
    ASSERT_EQ("2", FilesPerLevel(1));

    // Do a memtable compaction.  Before bug-fix, the compaction would
    // not detect the overlap with level-0 files and would incorrectly place
    // the deletion in a deeper level.
    ASSERT_OK(Delete(1, "600"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ("3", FilesPerLevel(1));
    ASSERT_EQ("NOT_FOUND", Get(1, "600"));
  } while (ChangeOptions(kSkipUniversalCompaction | kSkipFIFOCompaction));
}

TEST_F(DBTest, ComparatorCheck) {
  class NewComparator : public Comparator {
   public:
    const char* Name() const override {
      return "rocksdb.NewComparator";
    }
    int Compare(Slice a, Slice b) const override {
      return BytewiseComparator()->Compare(a, b);
    }
    virtual void FindShortestSeparator(std::string* s,
                                       const Slice& l) const override {
      BytewiseComparator()->FindShortestSeparator(s, l);
    }
    void FindShortSuccessor(std::string* key) const override {
      BytewiseComparator()->FindShortSuccessor(key);
    }
  };
  Options new_options, options;
  NewComparator cmp;
  do {
    options = CurrentOptions();
    CreateAndReopenWithCF({"pikachu"}, options);
    new_options = CurrentOptions();
    new_options.comparator = &cmp;
    // only the non-default column family has non-matching comparator
    Status s = TryReopenWithColumnFamilies({"default", "pikachu"},
        std::vector<Options>({options, new_options}));
    ASSERT_TRUE(!s.ok());
    ASSERT_TRUE(s.ToString().find("comparator") != std::string::npos)
        << s.ToString();
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, CustomComparator) {
  class NumberComparator : public Comparator {
   public:
    const char* Name() const override {
      return "test.NumberComparator";
    }
    int Compare(Slice a, Slice b) const override {
      return ToNumber(a) - ToNumber(b);
    }
    virtual void FindShortestSeparator(std::string* s,
                                       const Slice& l) const override {
      ToNumber(*s);     // Check format
      ToNumber(l);      // Check format
    }
    void FindShortSuccessor(std::string* key) const override {
      ToNumber(*key);   // Check format
    }
   private:
    static int ToNumber(const Slice& x) {
      // Check that there are no extra characters.
      EXPECT_TRUE(x.size() >= 2 && x[0] == '[' && x[x.size() - 1] == ']')
          << EscapeString(x);
      int val;
      char ignored;
      EXPECT_TRUE(sscanf(x.ToString().c_str(), "[%i]%c", &val, &ignored) == 1)
          << EscapeString(x);
      return val;
    }
  };
  Options new_options;
  NumberComparator cmp;
  do {
    new_options = CurrentOptions();
    new_options.create_if_missing = true;
    new_options.comparator = &cmp;
    new_options.write_buffer_size = 4096;  // Compact more often
    new_options.arena_block_size = 4096;
    new_options = CurrentOptions(new_options);
    DestroyAndReopen(new_options);
    CreateAndReopenWithCF({"pikachu"}, new_options);
    ASSERT_OK(Put(1, "[10]", "ten"));
    ASSERT_OK(Put(1, "[0x14]", "twenty"));
    for (int i = 0; i < 2; i++) {
      ASSERT_EQ("ten", Get(1, "[10]"));
      ASSERT_EQ("ten", Get(1, "[0xa]"));
      ASSERT_EQ("twenty", Get(1, "[20]"));
      ASSERT_EQ("twenty", Get(1, "[0x14]"));
      ASSERT_EQ("NOT_FOUND", Get(1, "[15]"));
      ASSERT_EQ("NOT_FOUND", Get(1, "[0xf]"));
      Compact(1, "[0]", "[9999]");
    }

    for (int run = 0; run < 2; run++) {
      for (int i = 0; i < 1000; i++) {
        char buf[100];
        snprintf(buf, sizeof(buf), "[%d]", i*10);
        ASSERT_OK(Put(1, buf, buf));
      }
      Compact(1, "[0]", "[1000000]");
    }
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, DBOpen_Options) {
  Options options = CurrentOptions();
  std::string dbname = test::TmpDir(env_) + "/db_options_test";
  ASSERT_OK(DestroyDB(dbname, options));

  // Does not exist, and create_if_missing == false: error
  DB* db = nullptr;
  options.create_if_missing = false;
  Status s = DB::Open(options, dbname, &db);
  ASSERT_TRUE(strstr(s.ToString().c_str(), "does not exist") != nullptr);
  ASSERT_TRUE(db == nullptr);

  // Does not exist, and create_if_missing == true: OK
  options.create_if_missing = true;
  s = DB::Open(options, dbname, &db);
  ASSERT_OK(s);
  ASSERT_TRUE(db != nullptr);

  delete db;
  db = nullptr;

  // Does exist, and error_if_exists == true: error
  options.create_if_missing = false;
  options.error_if_exists = true;
  s = DB::Open(options, dbname, &db);
  ASSERT_TRUE(strstr(s.ToString().c_str(), "exists") != nullptr);
  ASSERT_TRUE(db == nullptr);

  // Does exist, and error_if_exists == false: OK
  options.create_if_missing = true;
  options.error_if_exists = false;
  s = DB::Open(options, dbname, &db);
  ASSERT_OK(s);
  ASSERT_TRUE(db != nullptr);

  delete db;
  db = nullptr;
}

TEST_F(DBTest, DBOpen_Change_NumLevels) {
  Options options = CurrentOptions();
  options.create_if_missing = true;
  DestroyAndReopen(options);
  ASSERT_TRUE(db_ != nullptr);
  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "a", "123"));
  ASSERT_OK(Put(1, "b", "234"));
  ASSERT_OK(Flush(1));
  MoveFilesToLevel(3, 1);
  Close();

  options.create_if_missing = false;
  options.num_levels = 2;
  Status s = TryReopenWithColumnFamilies({"default", "pikachu"}, options);
  ASSERT_TRUE(strstr(s.ToString().c_str(), "Invalid argument") != nullptr);
  ASSERT_TRUE(db_ == nullptr);
}

TEST_F(DBTest, DestroyDBMetaDatabase) {
  std::string dbname = test::TmpDir(env_) + "/db_meta";
  ASSERT_OK(env_->CreateDirIfMissing(dbname));
  std::string metadbname = MetaDatabaseName(dbname, 0);
  ASSERT_OK(env_->CreateDirIfMissing(metadbname));
  std::string metametadbname = MetaDatabaseName(metadbname, 0);
  ASSERT_OK(env_->CreateDirIfMissing(metametadbname));

  // Destroy previous versions if they exist. Using the long way.
  Options options = CurrentOptions();
  ASSERT_OK(DestroyDB(metametadbname, options));
  ASSERT_OK(DestroyDB(metadbname, options));
  ASSERT_OK(DestroyDB(dbname, options));

  // Setup databases
  DB* db = nullptr;
  ASSERT_OK(DB::Open(options, dbname, &db));
  delete db;
  db = nullptr;
  ASSERT_OK(DB::Open(options, metadbname, &db));
  delete db;
  db = nullptr;
  ASSERT_OK(DB::Open(options, metametadbname, &db));
  delete db;
  db = nullptr;

  // Delete databases
  ASSERT_OK(DestroyDB(dbname, options));

  // Check if deletion worked.
  options.create_if_missing = false;
  ASSERT_TRUE(!(DB::Open(options, dbname, &db)).ok());
  ASSERT_TRUE(!(DB::Open(options, metadbname, &db)).ok());
  ASSERT_TRUE(!(DB::Open(options, metametadbname, &db)).ok());
}

// Check that number of files does not grow when writes are dropped
TEST_F(DBTest, DropWrites) {
  do {
    Options options = CurrentOptions();
    options.env = env_;
    options.paranoid_checks = false;
    Reopen(options);

    ASSERT_OK(Put("foo", "v1"));
    ASSERT_EQ("v1", Get("foo"));
    Compact("a", "z");
    const size_t num_files = CountFiles();
    // Force out-of-space errors
    env_->drop_writes_.store(true, std::memory_order_release);
    env_->sleep_counter_.Reset();
    env_->no_sleep_ = true;
    for (int i = 0; i < 5; i++) {
      if (option_config_ != kUniversalCompactionMultiLevel &&
          option_config_ != kUniversalSubcompactions) {
        for (int level = 0; level < dbfull()->NumberLevels(); level++) {
          if (level > 0 && level == dbfull()->NumberLevels() - 1) {
            break;
          }
          WARN_NOT_OK(
              dbfull()->TEST_CompactRange(
                  level, nullptr, nullptr, nullptr, true /* disallow trivial move */),
              "Compact range failed");
        }
      } else {
        WARN_NOT_OK(
            dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr),
            "Compact range failed");
      }
    }

    std::string property_value;
    ASSERT_TRUE(db_->GetProperty("rocksdb.background-errors", &property_value));
    ASSERT_EQ("5", property_value);

    env_->drop_writes_.store(false, std::memory_order_release);
    ASSERT_LT(CountFiles(), num_files + 3);

    // Check that compaction attempts slept after errors
    // TODO @krad: Figure out why ASSERT_EQ 5 keeps failing in certain compiler
    // versions
    ASSERT_GE(env_->sleep_counter_.Read(), 4);
  } while (ChangeCompactOptions());
}

// Check background error counter bumped on flush failures.
TEST_F(DBTest, DropWritesFlush) {
  do {
    Options options = CurrentOptions();
    options.env = env_;
    options.max_background_flushes = 1;
    Reopen(options);

    ASSERT_OK(Put("foo", "v1"));
    // Force out-of-space errors
    env_->drop_writes_.store(true, std::memory_order_release);

    std::string property_value;
    // Background error count is 0 now.
    ASSERT_TRUE(db_->GetProperty("rocksdb.background-errors", &property_value));
    ASSERT_EQ("0", property_value);

    ASSERT_NOK(dbfull()->TEST_FlushMemTable(true));

    ASSERT_TRUE(db_->GetProperty("rocksdb.background-errors", &property_value));
    ASSERT_EQ("1", property_value);

    env_->drop_writes_.store(false, std::memory_order_release);
  } while (ChangeCompactOptions());
}

// Check that CompactRange() returns failure if there is not enough space left
// on device
TEST_F(DBTest, NoSpaceCompactRange) {
  do {
    Options options = CurrentOptions();
    options.env = env_;
    options.disable_auto_compactions = true;
    Reopen(options);

    // generate 5 tables
    for (int i = 0; i < 5; ++i) {
      ASSERT_OK(Put(Key(i), Key(i) + "v"));
      ASSERT_OK(Flush());
    }

    // Force out-of-space errors
    env_->no_space_.store(true, std::memory_order_release);

    Status s = dbfull()->TEST_CompactRange(0, nullptr, nullptr, nullptr,
                                           true /* disallow trivial move */);
    ASSERT_TRUE(s.IsIOError());

    env_->no_space_.store(false, std::memory_order_release);
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, NonWritableFileSystem) {
  do {
    Options options = CurrentOptions();
    options.write_buffer_size = 4096;
    options.arena_block_size = 4096;
    options.env = env_;
    Reopen(options);
    ASSERT_OK(Put("foo", "v1"));
    env_->non_writeable_rate_.store(100);
    std::string big(100000, 'x');
    int errors = 0;
    for (int i = 0; i < 20; i++) {
      if (!Put("foo", big).ok()) {
        errors++;
        env_->SleepForMicroseconds(100000);
      }
    }
    ASSERT_GT(errors, 0);
    env_->non_writeable_rate_.store(0);
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, ManifestWriteError) {
  // Test for the following problem:
  // (a) Compaction produces file F
  // (b) Log record containing F is written to MANIFEST file, but Sync() fails
  // (c) GC deletes F
  // (d) After reopening DB, reads fail since deleted F is named in log record

  // We iterate twice.  In the second iteration, everything is the
  // same except the log record never makes it to the MANIFEST file.
  for (int iter = 0; iter < 2; iter++) {
    std::atomic<bool>* error_type = (iter == 0)
        ? &env_->manifest_sync_error_
        : &env_->manifest_write_error_;

    // Insert foo=>bar mapping
    Options options = CurrentOptions();
    options.env = env_;
    options.create_if_missing = true;
    options.error_if_exists = false;
    options.paranoid_checks = true;
    DestroyAndReopen(options);
    ASSERT_OK(Put("foo", "bar"));
    ASSERT_EQ("bar", Get("foo"));

    // Memtable compaction (will succeed)
    ASSERT_OK(Flush());
    ASSERT_EQ("bar", Get("foo"));
    const int last = 2;
    MoveFilesToLevel(2);
    ASSERT_EQ(NumTableFilesAtLevel(last), 1);   // foo=>bar is now in last level

    // Merging compaction (will fail)
    error_type->store(true, std::memory_order_release);
    ASSERT_NOK(dbfull()->TEST_CompactRange(last, nullptr, nullptr));  // Should fail
    ASSERT_EQ("bar", Get("foo"));

    error_type->store(false, std::memory_order_release);

    // Since paranoid_checks=true, writes should fail
    ASSERT_NOK(Put("foo2", "bar2"));

    // Recovery: should not lose data
    ASSERT_EQ("bar", Get("foo"));

    // Try again with paranoid_checks=false
    Close();
    options.paranoid_checks = false;
    Reopen(options);

    // Merging compaction (will fail)
    error_type->store(true, std::memory_order_release);
    ASSERT_OK(dbfull()->TEST_CompactRange(last, nullptr, nullptr));  // Should fail
    ASSERT_EQ("bar", Get("foo"));

    // Recovery: should not lose data
    error_type->store(false, std::memory_order_release);
    Reopen(options);
    ASSERT_EQ("bar", Get("foo"));

    // Since paranoid_checks=false, writes should succeed
    ASSERT_OK(Put("foo2", "bar2"));
    ASSERT_EQ("bar", Get("foo"));
    ASSERT_EQ("bar2", Get("foo2"));
  }
}

TEST_F(DBTest, PutFailsParanoid) {
  // Test the following:
  // (a) A random put fails in paranoid mode (simulate by sync fail)
  // (b) All other puts have to fail, even if writes would succeed
  // (c) All of that should happen ONLY if paranoid_checks = true

  Options options = CurrentOptions();
  options.env = env_;
  options.create_if_missing = true;
  options.error_if_exists = false;
  options.paranoid_checks = true;
  DestroyAndReopen(options);
  CreateAndReopenWithCF({"pikachu"}, options);
  Status s;

  ASSERT_OK(Put(1, "foo", "bar"));
  ASSERT_OK(Put(1, "foo1", "bar1"));
  // simulate error
  env_->log_write_error_.store(true, std::memory_order_release);
  s = Put(1, "foo2", "bar2");
  ASSERT_TRUE(!s.ok());
  env_->log_write_error_.store(false, std::memory_order_release);
  s = Put(1, "foo3", "bar3");
  // the next put should fail, too
  ASSERT_TRUE(!s.ok());
  // but we're still able to read
  ASSERT_EQ("bar", Get(1, "foo"));

  // do the same thing with paranoid checks off
  options.paranoid_checks = false;
  DestroyAndReopen(options);
  CreateAndReopenWithCF({"pikachu"}, options);

  ASSERT_OK(Put(1, "foo", "bar"));
  ASSERT_OK(Put(1, "foo1", "bar1"));
  // simulate error
  env_->log_write_error_.store(true, std::memory_order_release);
  s = Put(1, "foo2", "bar2");
  ASSERT_TRUE(!s.ok());
  env_->log_write_error_.store(false, std::memory_order_release);
  s = Put(1, "foo3", "bar3");
  // the next put should NOT fail
  ASSERT_TRUE(s.ok());
}

TEST_F(DBTest, SnapshotFiles) {
  do {
    Options options = CurrentOptions();
    options.write_buffer_size = 100000000;        // Large write buffer
    CreateAndReopenWithCF({"pikachu"}, options);

    Random rnd(301);

    // Write 8MB (80 values, each 100K)
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);
    std::vector<std::string> values;
    for (int i = 0; i < 80; i++) {
      values.push_back(RandomString(&rnd, 100000));
      ASSERT_OK(Put((i < 40), Key(i), values[i]));
    }

    // assert that nothing makes it to disk yet.
    ASSERT_EQ(NumTableFilesAtLevel(0, 1), 0);

    // get a file snapshot
    uint64_t manifest_number = 0;
    uint64_t manifest_size = 0;
    std::vector<std::string> files;
    ASSERT_OK(dbfull()->DisableFileDeletions());
    ASSERT_OK(dbfull()->GetLiveFiles(files, &manifest_size));

    // CURRENT, MANIFEST, *.sst, *.sst.sblock files (one for each CF)
    ASSERT_EQ(files.size(), 6U);

    uint64_t number = 0;
    FileType type;

    // copy these files to a new snapshot directory
    std::string snapdir = dbname_ + ".snapdir/";
    ASSERT_OK(env_->CreateDirIfMissing(snapdir));

    for (size_t i = 0; i < files.size(); i++) {
      // our clients require that GetLiveFiles returns
      // files with "/" as first character!
      ASSERT_EQ(files[i][0], '/');
      std::string src = dbname_ + files[i];
      std::string dest = snapdir + files[i];

      uint64_t size;
      ASSERT_OK(env_->GetFileSize(src, &size));

      // record the number and the size of the
      // latest manifest file
      if (ParseFileName(files[i].substr(1), &number, &type)) {
        if (type == kDescriptorFile) {
          if (number > manifest_number) {
            manifest_number = number;
            ASSERT_GE(size, manifest_size);
            size = manifest_size; // copy only valid MANIFEST data
          }
        }
      }
      CopyFile(src, dest, size);
    }

    // release file snapshot
    ASSERT_OK(dbfull()->DisableFileDeletions());
    // overwrite one key, this key should not appear in the snapshot
    std::vector<std::string> extras;
    for (unsigned int i = 0; i < 1; i++) {
      extras.push_back(RandomString(&rnd, 100000));
      ASSERT_OK(Put(0, Key(i), extras[i]));
    }

    // verify that data in the snapshot are correct
    std::vector<ColumnFamilyDescriptor> column_families;
    column_families.emplace_back("default", ColumnFamilyOptions());
    column_families.emplace_back("pikachu", ColumnFamilyOptions());
    std::vector<ColumnFamilyHandle*> cf_handles;
    DB* snapdb;
    DBOptions opts;
    opts.env = env_;
    opts.create_if_missing = false;
    Status stat =
        DB::Open(opts, snapdir, column_families, &cf_handles, &snapdb);
    ASSERT_OK(stat);

    ReadOptions roptions;
    std::string val;
    for (unsigned int i = 0; i < 80; i++) {
      stat = snapdb->Get(roptions, cf_handles[i < 40], Key(i), &val);
      ASSERT_OK(stat);
      ASSERT_EQ(values[i].compare(val), 0);
    }
    for (auto cfh : cf_handles) {
      delete cfh;
    }
    delete snapdb;

    // look at the new live files after we added an 'extra' key
    // and after we took the first snapshot.
    uint64_t new_manifest_number = 0;
    uint64_t new_manifest_size = 0;
    std::vector<std::string> newfiles;
    ASSERT_OK(dbfull()->DisableFileDeletions());
    ASSERT_OK(dbfull()->GetLiveFiles(newfiles, &new_manifest_size));

    // find the new manifest file. assert that this manifest file is
    // the same one as in the previous snapshot. But its size should be
    // larger because we added an extra key after taking the
    // previous shapshot.
    for (size_t i = 0; i < newfiles.size(); i++) {
      std::string src = dbname_ + "/" + newfiles[i];
      // record the lognumber and the size of the
      // latest manifest file
      if (ParseFileName(newfiles[i].substr(1), &number, &type)) {
        if (type == kDescriptorFile) {
          if (number > new_manifest_number) {
            uint64_t size;
            new_manifest_number = number;
            ASSERT_OK(env_->GetFileSize(src, &size));
            ASSERT_GE(size, new_manifest_size);
          }
        }
      }
    }
    ASSERT_EQ(manifest_number, new_manifest_number);
    ASSERT_GT(new_manifest_size, manifest_size);

    // release file snapshot
    ASSERT_OK(dbfull()->DisableFileDeletions());
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, CompactOnFlush) {
  anon::OptionsOverride options_override;

  do {
    Options options = CurrentOptions(options_override);
    options.disable_auto_compactions = true;
    CreateAndReopenWithCF({"pikachu"}, options);

    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v1 ]");

    // Write two new keys
    ASSERT_OK(Put(1, "a", "begin"));
    ASSERT_OK(Put(1, "z", "end"));
    ASSERT_OK(Flush(1));

    // Case1: Delete followed by a put
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_OK(Put(1, "foo", "v2"));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2, DEL, v1 ]");

    // After the current memtable is flushed, the DEL should
    // have been removed
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2, v1 ]");

    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v2 ]");

    // Case 2: Delete followed by another delete
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL, DEL, v2 ]");
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL, v2 ]");
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ ]");

    // Case 3: Put followed by a delete
    ASSERT_OK(Put(1, "foo", "v3"));
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL, v3 ]");
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ DEL ]");
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ ]");

    // Case 4: Put followed by another Put
    ASSERT_OK(Put(1, "foo", "v4"));
    ASSERT_OK(Put(1, "foo", "v5"));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v5, v4 ]");
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v5 ]");
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v5 ]");

    // clear database
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ ]");

    // Case 5: Put followed by snapshot followed by another Put
    // Both puts should remain.
    ASSERT_OK(Put(1, "foo", "v6"));
    const Snapshot* snapshot = db_->GetSnapshot();
    ASSERT_OK(Put(1, "foo", "v7"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v7, v6 ]");
    db_->ReleaseSnapshot(snapshot);

    // clear database
    ASSERT_OK(Delete(1, "foo"));
    ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), handles_[1], nullptr, nullptr));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ ]");

    // Case 5: snapshot followed by a put followed by another Put
    // Only the last put should remain.
    const Snapshot* snapshot1 = db_->GetSnapshot();
    ASSERT_OK(Put(1, "foo", "v8"));
    ASSERT_OK(Put(1, "foo", "v9"));
    ASSERT_OK(Flush(1));
    ASSERT_EQ(AllEntriesFor("foo", 1), "[ v9 ]");
    db_->ReleaseSnapshot(snapshot1);
  } while (ChangeCompactOptions());
}

namespace {
std::vector<std::uint64_t> ListSpecificFiles(
    Env* env, const std::string& path, const FileType expected_file_type) {
  std::vector<std::string> files;
  std::vector<uint64_t> file_numbers;
  CHECK_OK(env->GetChildren(path, &files));
  uint64_t number;
  FileType type;
  for (size_t i = 0; i < files.size(); ++i) {
    if (ParseFileName(files[i], &number, &type)) {
      if (type == expected_file_type) {
        file_numbers.push_back(number);
      }
    }
  }
  return file_numbers;
}

std::vector<std::uint64_t> ListTableFiles(Env* env, const std::string& path) {
  return ListSpecificFiles(env, path, kTableFile);
}
}  // namespace

TEST_F(DBTest, FlushOneColumnFamily) {
  Options options = CurrentOptions();
  CreateAndReopenWithCF({"pikachu", "ilya", "muromec", "dobrynia", "nikitich",
                         "alyosha", "popovich"},
                        options);

  ASSERT_OK(Put(0, "Default", "Default"));
  ASSERT_OK(Put(1, "pikachu", "pikachu"));
  ASSERT_OK(Put(2, "ilya", "ilya"));
  ASSERT_OK(Put(3, "muromec", "muromec"));
  ASSERT_OK(Put(4, "dobrynia", "dobrynia"));
  ASSERT_OK(Put(5, "nikitich", "nikitich"));
  ASSERT_OK(Put(6, "alyosha", "alyosha"));
  ASSERT_OK(Put(7, "popovich", "popovich"));

  for (int i = 0; i < 8; ++i) {
    ASSERT_OK(Flush(i));
    auto tables = ListTableFiles(env_, dbname_);
    ASSERT_EQ(tables.size(), i + 1U);
  }
}

// In https://reviews.facebook.net/D20661 we change
// recovery behavior: previously for each log file each column family
// memtable was flushed, even it was empty. Now it's changed:
// we try to create the smallest number of table files by merging
// updates from multiple logs
TEST_F(DBTest, RecoverCheckFileAmountWithSmallWriteBuffer) {
  Options options = CurrentOptions();
  options.write_buffer_size = 5000000;
  CreateAndReopenWithCF({"pikachu", "dobrynia", "nikitich"}, options);

  // Since we will reopen DB with smaller write_buffer_size,
  // each key will go to new SST file
  ASSERT_OK(Put(1, Key(10), DummyString(1000000)));
  ASSERT_OK(Put(1, Key(10), DummyString(1000000)));
  ASSERT_OK(Put(1, Key(10), DummyString(1000000)));
  ASSERT_OK(Put(1, Key(10), DummyString(1000000)));

  ASSERT_OK(Put(3, Key(10), DummyString(1)));
  // Make 'dobrynia' to be flushed and new WAL file to be created
  ASSERT_OK(Put(2, Key(10), DummyString(7500000)));
  ASSERT_OK(Put(2, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[2]));
  {
    auto tables = ListTableFiles(env_, dbname_);
    ASSERT_EQ(tables.size(), static_cast<size_t>(1));
    // Make sure 'dobrynia' was flushed: check sst files amount
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(1));
  }
  // New WAL file
  ASSERT_OK(Put(1, Key(1), DummyString(1)));
  ASSERT_OK(Put(1, Key(1), DummyString(1)));
  ASSERT_OK(Put(3, Key(10), DummyString(1)));
  ASSERT_OK(Put(3, Key(10), DummyString(1)));
  ASSERT_OK(Put(3, Key(10), DummyString(1)));

  options.write_buffer_size = 4096;
  options.arena_block_size = 4096;
  ReopenWithColumnFamilies({"default", "pikachu", "dobrynia", "nikitich"},
                           options);
  {
    // No inserts => default is empty
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(0));
    // First 4 keys goes to separate SSTs + 1 more SST for 2 smaller keys
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(5));
    // 1 SST for big key + 1 SST for small one
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(2));
    // 1 SST for all keys
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(1));
  }
}

// In https://reviews.facebook.net/D20661 we change
// recovery behavior: previously for each log file each column family
// memtable was flushed, even it wasn't empty. Now it's changed:
// we try to create the smallest number of table files by merging
// updates from multiple logs
TEST_F(DBTest, RecoverCheckFileAmount) {
  Options options = CurrentOptions();
  options.write_buffer_size = 100000;
  options.arena_block_size = 4 * 1024;
  CreateAndReopenWithCF({"pikachu", "dobrynia", "nikitich"}, options);

  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(Put(1, Key(1), DummyString(1)));
  ASSERT_OK(Put(2, Key(1), DummyString(1)));

  // Make 'nikitich' memtable to be flushed
  ASSERT_OK(Put(3, Key(10), DummyString(1002400)));
  ASSERT_OK(Put(3, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  // 4 memtable are not flushed, 1 sst file
  {
    auto tables = ListTableFiles(env_, dbname_);
    ASSERT_EQ(tables.size(), static_cast<size_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(1));
  }
  // Memtable for 'nikitich' has flushed, new WAL file has opened
  // 4 memtable still not flushed

  // Write to new WAL file
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(Put(1, Key(1), DummyString(1)));
  ASSERT_OK(Put(2, Key(1), DummyString(1)));

  // Fill up 'nikitich' one more time
  ASSERT_OK(Put(3, Key(10), DummyString(1002400)));
  // make it flush
  ASSERT_OK(Put(3, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  // There are still 4 memtable not flushed, and 2 sst tables
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(Put(1, Key(1), DummyString(1)));
  ASSERT_OK(Put(2, Key(1), DummyString(1)));

  {
    auto tables = ListTableFiles(env_, dbname_);
    ASSERT_EQ(tables.size(), static_cast<size_t>(2));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(2));
  }

  ReopenWithColumnFamilies({"default", "pikachu", "dobrynia", "nikitich"},
                           options);
  {
    std::vector<uint64_t> table_files = ListTableFiles(env_, dbname_);
    // Check, that records for 'default', 'dobrynia' and 'pikachu' from
    // first, second and third WALs  went to the same SST.
    // So, there is 6 SSTs: three  for 'nikitich', one for 'default', one for
    // 'dobrynia', one for 'pikachu'
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(3));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(1));
  }
}

TEST_F(DBTest, SharedWriteBuffer) {
  Options options = CurrentOptions();
  options.db_write_buffer_size = 100000;  // this is the real limit
  options.write_buffer_size    = 500000;  // this is never hit
  CreateAndReopenWithCF({"pikachu", "dobrynia", "nikitich"}, options);

  // Trigger a flush on CF "nikitich"
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(Put(1, Key(1), DummyString(1)));
  ASSERT_OK(Put(3, Key(1), DummyString(90000)));
  ASSERT_OK(Put(2, Key(2), DummyString(20000)));
  ASSERT_OK(Put(2, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[0]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[1]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[2]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  {
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(1));
  }

  // "dobrynia": 20KB
  // Flush 'dobrynia'
  ASSERT_OK(Put(3, Key(2), DummyString(40000)));
  ASSERT_OK(Put(2, Key(2), DummyString(70000)));
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[1]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[2]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  {
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(1));
  }

  // "nikitich" still has has data of 80KB
  // Inserting Data in "dobrynia" triggers "nikitich" flushing.
  ASSERT_OK(Put(3, Key(2), DummyString(40000)));
  ASSERT_OK(Put(2, Key(2), DummyString(40000)));
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[1]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[2]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  {
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(2));
  }

  // "dobrynia" still has 40KB
  ASSERT_OK(Put(1, Key(2), DummyString(20000)));
  ASSERT_OK(Put(0, Key(1), DummyString(10000)));
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[0]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[1]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[2]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  // This should triggers no flush
  {
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(2));
  }

  // "default": 10KB, "pikachu": 20KB, "dobrynia": 40KB
  ASSERT_OK(Put(1, Key(2), DummyString(40000)));
  ASSERT_OK(Put(0, Key(1), DummyString(1)));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[0]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[1]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[2]));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable(handles_[3]));
  // This should triggers flush of "pikachu"
  {
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(0));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(2));
  }

  // "default": 10KB, "dobrynia": 40KB
  // Some remaining writes so 'default', 'dobrynia' and 'nikitich' flush on
  // closure.
  ASSERT_OK(Put(3, Key(1), DummyString(1)));
  ReopenWithColumnFamilies({"default", "pikachu", "dobrynia", "nikitich"},
                           options);
  {
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "default"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "pikachu"),
              static_cast<uint64_t>(1));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "dobrynia"),
              static_cast<uint64_t>(2));
    ASSERT_EQ(GetNumberOfSstFilesForColumnFamily(db_, "nikitich"),
              static_cast<uint64_t>(3));
  }
}

TEST_F(DBTest, DISABLED_PurgeInfoLogs) {
  Options options = CurrentOptions();
  options.keep_log_file_num = 5;
  options.create_if_missing = true;
  for (int mode = 0; mode <= 1; mode++) {
    if (mode == 1) {
      options.db_log_dir = dbname_ + "_logs";
      ASSERT_OK(env_->CreateDirIfMissing(options.db_log_dir));
    } else {
      options.db_log_dir = "";
    }
    for (int i = 0; i < 8; i++) {
      Reopen(options);
    }

    std::vector<std::string> files;
    ASSERT_OK(env_->GetChildren(options.db_log_dir.empty() ? dbname_ : options.db_log_dir, &files));
    int info_log_count = 0;
    for (std::string file : files) {
      if (file.find("LOG") != std::string::npos) {
        info_log_count++;
      }
    }
    ASSERT_EQ(5, info_log_count);

    Destroy(options);
    // For mode (1), test DestroyDB() to delete all the logs under DB dir.
    // For mode (2), no info log file should have been put under DB dir.
    std::vector<std::string> db_files;
    ASSERT_OK(env_->GetChildren(dbname_, &db_files));
    for (std::string file : db_files) {
      ASSERT_TRUE(file.find("LOG") == std::string::npos);
    }

    if (mode == 1) {
      // Cleaning up
      ASSERT_OK(env_->GetChildren(options.db_log_dir, &files));
      for (std::string file : files) {
        ASSERT_OK(env_->DeleteFile(options.db_log_dir + "/" + file));
      }
      ASSERT_OK(env_->DeleteDir(options.db_log_dir));
    }
  }
}

TEST_F(DBTest, SyncMultipleLogs) {
  const uint64_t kNumBatches = 2;
  const int kBatchSize = 1000;

  Options options = CurrentOptions();
  options.create_if_missing = true;
  options.write_buffer_size = 4096;
  Reopen(options);

  WriteBatch batch;
  WriteOptions wo;
  wo.sync = true;

  for (uint64_t b = 0; b < kNumBatches; b++) {
    batch.Clear();
    for (int i = 0; i < kBatchSize; i++) {
      batch.Put(Key(i), DummyString(128));
    }

    ASSERT_OK(dbfull()->Write(wo, &batch));
  }

  ASSERT_OK(dbfull()->SyncWAL());
}

//
// Test WAL recovery for the various modes available
//
class RecoveryTestHelper {
 public:
  // Number of WAL files to generate
  // Lower number of wals for tsan due to low perf.
  static constexpr int kWALFilesCount = yb::NonTsanVsTsan(10, 5);
  // Starting number for the WAL file name like 00010.log
  static const int kWALFileOffset = 10;
  // Keys to be written per WAL file
  static const int kKeysPerWALFile = 1024;
  // Size of the value
  static const int kValueSize = 10;

  // Create WAL files with values filled in
  static void FillData(DBTest* test, Options* db_options, const size_t wal_count,
                       size_t* count) {
    *count = 0;

    shared_ptr<Cache> table_cache = NewLRUCache(50000, 16);
    EnvOptions env_options;
    WriteBuffer write_buffer(db_options->db_write_buffer_size);

    unique_ptr<VersionSet> versions;
    unique_ptr<WalManager> wal_manager;
    WriteController write_controller;

    versions.reset(new VersionSet(test->dbname_, db_options, env_options,
                                  table_cache.get(), &write_buffer,
                                  &write_controller));

    wal_manager.reset(new WalManager(*db_options, env_options));

    std::unique_ptr<log::Writer> current_log_writer;

    for (size_t j = kWALFileOffset; j < wal_count + kWALFileOffset; j++) {
      uint64_t current_log_number = j;
      std::string fname = LogFileName(test->dbname_, current_log_number);
      unique_ptr<WritableFile> file;
      ASSERT_OK(db_options->env->NewWritableFile(fname, &file, env_options));
      unique_ptr<WritableFileWriter> file_writer(
          new WritableFileWriter(std::move(file), env_options));
      current_log_writer.reset(
          new log::Writer(std::move(file_writer), current_log_number,
                          db_options->recycle_log_file_num > 0));

      for (int i = 0; i < kKeysPerWALFile; i++) {
        std::string key = "key" + ToString((*count)++);
        std::string value = test->DummyString(kValueSize);
        assert(current_log_writer.get() != nullptr);
        uint64_t seq = versions->LastSequence() + 1;
        WriteBatch batch;
        batch.Put(key, value);
        WriteBatchInternal::SetSequence(&batch, seq);
        ASSERT_OK(current_log_writer->AddRecord(WriteBatchInternal::Contents(&batch)));
        versions->SetLastSequence(seq);
      }
    }


  }

  // Recreate and fill the store with some data
  static size_t FillData(DBTest* test, Options* options) {
    options->create_if_missing = true;
    test->DestroyAndReopen(*options);
    test->Close();

    size_t count = 0;
    FillData(test, options, kWALFilesCount, &count);
    return count;
  }

  // Read back all the keys we wrote and return the number of keys found
  static size_t GetData(DBTest* test) {
    size_t count = 0;
    for (size_t i = 0; i < kWALFilesCount * kKeysPerWALFile; i++) {
      if (test->Get("key" + ToString(i)) != "NOT_FOUND") {
        ++count;
      }
    }
    return count;
  }

  // Manuall corrupt the specified WAL
  static void CorruptWAL(DBTest* test, const Options& options, const double off,
                         const double len, const int wal_file_id,
                         const bool trunc = false) {
    Env* env = options.env;
    std::string fname = LogFileName(test->dbname_, wal_file_id);
    uint64_t size;
    ASSERT_OK(env->GetFileSize(fname, &size));
    ASSERT_GT(size, 0);
#ifdef OS_WIN
    // Windows disk cache behaves differently. When we truncate
    // the original content is still in the cache due to the original
    // handle is still open. Generally, in Windows, one prohibits
    // shared access to files and it is not needed for WAL but we allow
    // it to induce corruption at various tests.
    test->Close();
#endif
    if (trunc) {
      ASSERT_EQ(0, truncate(fname.c_str(),
        static_cast<int64_t>(size * off)));
    } else {
      InduceCorruption(fname, static_cast<size_t>(size * off),
        static_cast<size_t>(size * len));
    }
  }

  // Overwrite data with 'a' from offset for length len
  static void InduceCorruption(const std::string& filename, size_t offset,
                               size_t len) {
    ASSERT_GT(len, 0U);

    int fd = open(filename.c_str(), O_RDWR);

    ASSERT_GT(fd, 0);
    ASSERT_EQ(offset, lseek(fd, offset, SEEK_SET));

    void* buf = alloca(len);
    memset(buf, 'a', len);
    ASSERT_EQ(len, write(fd, buf, len));

    close(fd);
  }
};

// Test scope:
// - We expect to open the data store when there is incomplete trailing writes
// at the end of any of the logs
// - We do not expect to open the data store for corruption
TEST_F(DBTest, kTolerateCorruptedTailRecords) {
  const int jstart = RecoveryTestHelper::kWALFileOffset;
  const int jend = jstart + RecoveryTestHelper::kWALFilesCount;

  for (auto trunc : {true, false}) {        /* Corruption style */
    for (int i = 0; i < 4; i++) {           /* Corruption offset position */
      for (int j = jstart; j < jend; j++) { /* WAL file */
        // Fill data for testing
        Options options = CurrentOptions();
        const size_t row_count = RecoveryTestHelper::FillData(this, &options);
        // test checksum failure or parsing
        RecoveryTestHelper::CorruptWAL(this, options, /*off=*/i * .3,
                                       /*len%=*/.1, /*wal=*/j, trunc);

        if (trunc) {
          options.wal_recovery_mode =
              WALRecoveryMode::kTolerateCorruptedTailRecords;
          options.create_if_missing = false;
          ASSERT_OK(TryReopen(options));
          const size_t recovered_row_count = RecoveryTestHelper::GetData(this);
          ASSERT_TRUE(i == 0 || recovered_row_count > 0);
          ASSERT_LT(recovered_row_count, row_count);
        } else {
          options.wal_recovery_mode =
              WALRecoveryMode::kTolerateCorruptedTailRecords;
          ASSERT_NOK(TryReopen(options));
        }
      }
    }
  }
}

// Test scope:
// We don't expect the data store to be opened if there is any corruption
// (leading, middle or trailing -- incomplete writes or corruption)
TEST_F(DBTest, kAbsoluteConsistency) {
  const int jstart = RecoveryTestHelper::kWALFileOffset;
  const int jend = jstart + RecoveryTestHelper::kWALFilesCount;

  // Verify clean slate behavior
  Options options = CurrentOptions();
  const size_t row_count = RecoveryTestHelper::FillData(this, &options);
  options.wal_recovery_mode = WALRecoveryMode::kAbsoluteConsistency;
  options.create_if_missing = false;
  ASSERT_OK(TryReopen(options));
  ASSERT_EQ(RecoveryTestHelper::GetData(this), row_count);

  for (auto trunc : {true, false}) { /* Corruption style */
    for (int i = 0; i < 4; i++) {    /* Corruption offset position */
      if (trunc && i == 0) {
        continue;
      }

      for (int j = jstart; j < jend; j++) { /* wal files */
        // fill with new date
        RecoveryTestHelper::FillData(this, &options);
        // corrupt the wal
        RecoveryTestHelper::CorruptWAL(this, options, /*off=*/i * .3,
                                       /*len%=*/.1, j, trunc);
        // verify
        options.wal_recovery_mode = WALRecoveryMode::kAbsoluteConsistency;
        options.create_if_missing = false;
        ASSERT_NOK(TryReopen(options));
      }
    }
  }
}

// Test scope:
// - We expect to open data store under all circumstances
// - We expect only data upto the point where the first error was encountered
TEST_F(DBTest, kPointInTimeRecovery) {
  const int jstart = RecoveryTestHelper::kWALFileOffset;
  const int jend = jstart + RecoveryTestHelper::kWALFilesCount;
  const int maxkeys =
      RecoveryTestHelper::kWALFilesCount * RecoveryTestHelper::kKeysPerWALFile;

  for (auto trunc : {true, false}) {        /* Corruption style */
    for (int i = 0; i < 4; i++) {           /* Offset of corruption */
      for (int j = jstart; j < jend; j++) { /* WAL file */
        // Fill data for testing
        Options options = CurrentOptions();
        const size_t row_count = RecoveryTestHelper::FillData(this, &options);

        // Corrupt the wal
        RecoveryTestHelper::CorruptWAL(this, options, /*off=*/i * .3,
                                       /*len%=*/.1, j, trunc);

        // Verify
        options.wal_recovery_mode = WALRecoveryMode::kPointInTimeRecovery;
        options.create_if_missing = false;
        ASSERT_OK(TryReopen(options));

        // Probe data for invariants
        size_t recovered_row_count = RecoveryTestHelper::GetData(this);
        ASSERT_LT(recovered_row_count, row_count);

        bool expect_data = true;
        for (size_t k = 0; k < maxkeys; ++k) {
          bool found = Get("key" + ToString(i)) != "NOT_FOUND";
          if (expect_data && !found) {
            expect_data = false;
          }
          ASSERT_EQ(found, expect_data);
        }

        const size_t min = RecoveryTestHelper::kKeysPerWALFile *
                           (j - RecoveryTestHelper::kWALFileOffset);
        ASSERT_GE(recovered_row_count, min);
        if (!trunc && i != 0) {
          const size_t max = RecoveryTestHelper::kKeysPerWALFile *
                             (j - RecoveryTestHelper::kWALFileOffset + 1);
          ASSERT_LE(recovered_row_count, max);
        }
      }
    }
  }
}

// Test scope:
// - We expect to open the data store under all scenarios
// - We expect to have recovered records past the corruption zone
TEST_F(DBTest, kSkipAnyCorruptedRecords) {
  const int jstart = RecoveryTestHelper::kWALFileOffset;
  const int jend = jstart + RecoveryTestHelper::kWALFilesCount;

  for (auto trunc : {true, false}) {        /* Corruption style */
    for (int i = 0; i < 4; i++) {           /* Corruption offset */
      for (int j = jstart; j < jend; j++) { /* wal files */
        // Fill data for testing
        Options options = CurrentOptions();
        const size_t row_count = RecoveryTestHelper::FillData(this, &options);

        // Corrupt the WAL
        RecoveryTestHelper::CorruptWAL(this, options, /*off=*/i * .3,
                                       /*len%=*/.1, j, trunc);

        // Verify behavior
        options.wal_recovery_mode = WALRecoveryMode::kSkipAnyCorruptedRecords;
        options.create_if_missing = false;
        ASSERT_OK(TryReopen(options));

        // Probe data for invariants
        size_t recovered_row_count = RecoveryTestHelper::GetData(this);
        ASSERT_LT(recovered_row_count, row_count);

        if (!trunc) {
          ASSERT_TRUE(i != 0 || recovered_row_count > 0);
        }
      }
    }
  }
}

// Multi-threaded test:
namespace {

static const int kColumnFamilies = 10;
static const int kNumThreads = 10;
static const int kTestSeconds = 10;
static const int kNumKeys = 1000;

struct MTState {
  DBTest* test;
  std::atomic<bool> stop;
  std::atomic<int> counter[kNumThreads];
  std::atomic<bool> thread_done[kNumThreads];
};

struct MTThread {
  MTState* state;
  int id;
};

static void MTThreadBody(void* arg) {
  MTThread* t = reinterpret_cast<MTThread*>(arg);
  int id = t->id;
  DB* db = t->state->test->db_;
  int counter = 0;
  fprintf(stderr, "... starting thread %d\n", id);
  Random rnd(1000 + id);
  char valbuf[1500];
  while (t->state->stop.load(std::memory_order_acquire) == false) {
    t->state->counter[id].store(counter, std::memory_order_release);

    int key = rnd.Uniform(kNumKeys);
    char keybuf[20];
    snprintf(keybuf, sizeof(keybuf), "%016d", key);

    if (rnd.OneIn(2)) {
      // Write values of the form <key, my id, counter, cf, unique_id>.
      // into each of the CFs
      // We add some padding for force compactions.
      int unique_id = rnd.Uniform(1000000);

      // Half of the time directly use WriteBatch. Half of the time use
      // WriteBatchWithIndex.
      if (rnd.OneIn(2)) {
        WriteBatch batch;
        for (int cf = 0; cf < kColumnFamilies; ++cf) {
          snprintf(valbuf, sizeof(valbuf), "%d.%d.%d.%d.%-1000d", key, id,
                   static_cast<int>(counter), cf, unique_id);
          batch.Put(t->state->test->handles_[cf], Slice(keybuf), Slice(valbuf));
        }
        ASSERT_OK(db->Write(WriteOptions(), &batch));
      } else {
        WriteBatchWithIndex batch(db->GetOptions().comparator);
        for (int cf = 0; cf < kColumnFamilies; ++cf) {
          snprintf(valbuf, sizeof(valbuf), "%d.%d.%d.%d.%-1000d", key, id,
                   static_cast<int>(counter), cf, unique_id);
          batch.Put(t->state->test->handles_[cf], Slice(keybuf), Slice(valbuf));
        }
        ASSERT_OK(db->Write(WriteOptions(), batch.GetWriteBatch()));
      }
    } else {
      // Read a value and verify that it matches the pattern written above
      // and that writes to all column families were atomic (unique_id is the
      // same)
      std::vector<Slice> keys(kColumnFamilies, Slice(keybuf));
      std::vector<std::string> values;
      std::vector<Status> statuses =
          db->MultiGet(ReadOptions(), t->state->test->handles_, keys, &values);
      Status s = statuses[0];
      // all statuses have to be the same
      for (size_t i = 1; i < statuses.size(); ++i) {
        // they are either both ok or both not-found
        ASSERT_TRUE((s.ok() && statuses[i].ok()) ||
                    (s.IsNotFound() && statuses[i].IsNotFound()));
      }
      if (s.IsNotFound()) {
        // Key has not yet been written
      } else {
        // Check that the writer thread counter is >= the counter in the value
        ASSERT_OK(s);
        int unique_id = -1;
        for (int i = 0; i < kColumnFamilies; ++i) {
          int k, w, c, cf, u;
          ASSERT_EQ(5, sscanf(values[i].c_str(), "%d.%d.%d.%d.%d", &k, &w,
                              &c, &cf, &u))
              << values[i];
          ASSERT_EQ(k, key);
          ASSERT_GE(w, 0);
          ASSERT_LT(w, kNumThreads);
          ASSERT_LE(c, t->state->counter[w].load(std::memory_order_acquire));
          ASSERT_EQ(cf, i);
          if (i == 0) {
            unique_id = u;
          } else {
            // this checks that updates across column families happened
            // atomically -- all unique ids are the same
            ASSERT_EQ(u, unique_id);
          }
        }
      }
    }
    counter++;
  }
  t->state->thread_done[id].store(true, std::memory_order_release);
  fprintf(stderr, "... stopping thread %d after %d ops\n", id, counter);
}

}  // namespace

class MultiThreadedDBTest : public DBTest,
                            public ::testing::WithParamInterface<int> {
 public:
  void SetUp() override { option_config_ = GetParam(); }

  static std::vector<int> GenerateOptionConfigs() {
    std::vector<int> optionConfigs;
    for (int optionConfig = kDefault; optionConfig < kEnd; ++optionConfig) {
      optionConfigs.push_back(optionConfig);
    }
    return optionConfigs;
  }
};

TEST_P(MultiThreadedDBTest, MultiThreaded) {
  anon::OptionsOverride options_override;

  std::vector<std::string> cfs;
  for (int i = 1; i < kColumnFamilies; ++i) {
    cfs.push_back(ToString(i));
  }
  CreateAndReopenWithCF(cfs, CurrentOptions(options_override));
  // Initialize state
  MTState mt;
  mt.test = this;
  mt.stop.store(false, std::memory_order_release);
  for (int id = 0; id < kNumThreads; id++) {
    mt.counter[id].store(0, std::memory_order_release);
    mt.thread_done[id].store(false, std::memory_order_release);
  }

  // Start threads
  MTThread thread[kNumThreads];
  for (int id = 0; id < kNumThreads; id++) {
    thread[id].state = &mt;
    thread[id].id = id;
    env_->StartThread(MTThreadBody, &thread[id]);
  }

  // Let them run for a while
  env_->SleepForMicroseconds(kTestSeconds * 1000000);

  // Stop the threads and wait for them to finish
  mt.stop.store(true, std::memory_order_release);
  for (int id = 0; id < kNumThreads; id++) {
    while (mt.thread_done[id].load(std::memory_order_acquire) == false) {
      env_->SleepForMicroseconds(100000);
    }
  }
}

INSTANTIATE_TEST_CASE_P(
    MultiThreaded, MultiThreadedDBTest,
    ::testing::ValuesIn(MultiThreadedDBTest::GenerateOptionConfigs()));

// Group commit test:
namespace {

static const int kGCNumThreads = 4;
static const int kGCNumKeys = 1000;

struct GCThread {
  DB* db;
  int id;
  std::atomic<bool> done;
};

static void GCThreadBody(void* arg) {
  GCThread* t = reinterpret_cast<GCThread*>(arg);
  int id = t->id;
  DB* db = t->db;
  WriteOptions wo;

  for (int i = 0; i < kGCNumKeys; ++i) {
    std::string kv(ToString(i + id * kGCNumKeys));
    ASSERT_OK(db->Put(wo, kv, kv));
  }
  t->done = true;
}

}  // namespace

TEST_F(DBTest, GroupCommitTest) {
  do {
    Options options = CurrentOptions();
    options.env = env_;
    env_->log_write_slowdown_.store(100);
    options.statistics = rocksdb::CreateDBStatisticsForTests();
    Reopen(options);

    // Start threads
    GCThread thread[kGCNumThreads];
    for (int id = 0; id < kGCNumThreads; id++) {
      thread[id].id = id;
      thread[id].db = db_;
      thread[id].done = false;
      env_->StartThread(GCThreadBody, &thread[id]);
    }

    for (int id = 0; id < kGCNumThreads; id++) {
      while (thread[id].done == false) {
        env_->SleepForMicroseconds(100000);
      }
    }
    env_->log_write_slowdown_.store(0);

    ASSERT_GT(TestGetTickerCount(options, WRITE_DONE_BY_OTHER), 0);

    std::vector<std::string> expected_db;
    for (int i = 0; i < kGCNumThreads * kGCNumKeys; ++i) {
      expected_db.push_back(ToString(i));
    }
    sort(expected_db.begin(), expected_db.end());

    Iterator* itr = db_->NewIterator(ReadOptions());
    itr->SeekToFirst();
    for (auto x : expected_db) {
      ASSERT_TRUE(ASSERT_RESULT(itr->CheckedValid()));
      ASSERT_EQ(itr->key().ToString(), x);
      ASSERT_EQ(itr->value().ToString(), x);
      itr->Next();
    }
    ASSERT_TRUE(!ASSERT_RESULT(itr->CheckedValid()));
    delete itr;

    HistogramData hist_data = {0, 0, 0, 0, 0};
    options.statistics->histogramData(DB_WRITE, &hist_data);
    ASSERT_GT(hist_data.average, 0.0);
  } while (ChangeOptions(kSkipNoSeekToLast));
}

namespace {
typedef std::map<std::string, std::string> KVMap;
}

class ModelDB: public DB {
 public:
  class ModelSnapshot : public Snapshot {
   public:
    KVMap map_;

    SequenceNumber GetSequenceNumber() const override {
      // no need to call this
      assert(false);
      return 0;
    }
  };

  explicit ModelDB(const Options& options) : options_(options) {}
  using DB::Put;
  virtual Status Put(const WriteOptions& o, ColumnFamilyHandle* cf,
                     const Slice& k, const Slice& v) override {
    WriteBatch batch;
    batch.Put(cf, k, v);
    return Write(o, &batch);
  }
  using DB::Delete;
  virtual Status Delete(const WriteOptions& o, ColumnFamilyHandle* cf,
                        const Slice& key) override {
    WriteBatch batch;
    batch.Delete(cf, key);
    return Write(o, &batch);
  }
  using DB::SingleDelete;
  virtual Status SingleDelete(const WriteOptions& o, ColumnFamilyHandle* cf,
                              const Slice& key) override {
    WriteBatch batch;
    batch.SingleDelete(cf, key);
    return Write(o, &batch);
  }
  using DB::Merge;
  virtual Status Merge(const WriteOptions& o, ColumnFamilyHandle* cf,
                       const Slice& k, const Slice& v) override {
    WriteBatch batch;
    batch.Merge(cf, k, v);
    return Write(o, &batch);
  }
  using DB::Get;
  virtual Status Get(const ReadOptions& options, ColumnFamilyHandle* cf,
                     const Slice& key, std::string* value) override {
    return STATUS(NotSupported, key);
  }

  using DB::MultiGet;
  virtual std::vector<Status> MultiGet(
      const ReadOptions& options,
      const std::vector<ColumnFamilyHandle*>& column_family,
      const std::vector<Slice>& keys,
      std::vector<std::string>* values) override {
    std::vector<Status> s(keys.size(),
                          STATUS(NotSupported, "Not implemented."));
    return s;
  }

  using DB::AddFile;
  virtual Status AddFile(ColumnFamilyHandle* column_family,
                         const ExternalSstFileInfo* file_path,
                         bool move_file) override {
    return STATUS(NotSupported, "Not implemented.");
  }
  virtual Status AddFile(ColumnFamilyHandle* column_family,
                         const std::string& file_path,
                         bool move_file) override {
    return STATUS(NotSupported, "Not implemented.");
  }

  using DB::GetPropertiesOfAllTables;
  virtual Status GetPropertiesOfAllTables(
      ColumnFamilyHandle* column_family,
      TablePropertiesCollection* props) override {
    return Status();
  }

  virtual Status GetPropertiesOfTablesInRange(
      ColumnFamilyHandle* column_family, const Range* range, std::size_t n,
      TablePropertiesCollection* props) override {
    return Status();
  }

  using DB::KeyMayExist;
  virtual bool KeyMayExist(const ReadOptions& options,
                           ColumnFamilyHandle* column_family, const Slice& key,
                           std::string* value,
                           bool* value_found = nullptr) override {
    if (value_found != nullptr) {
      *value_found = false;
    }
    return true; // Not Supported directly
  }
  using DB::NewIterator;
  virtual Iterator* NewIterator(const ReadOptions& options,
                                ColumnFamilyHandle* column_family) override {
    if (options.snapshot == nullptr) {
      KVMap* saved = new KVMap;
      *saved = map_;
      return new ModelIter(saved, true);
    } else {
      const KVMap* snapshot_state =
          &(reinterpret_cast<const ModelSnapshot*>(options.snapshot)->map_);
      return new ModelIter(snapshot_state, false);
    }
  }
  virtual Status NewIterators(
      const ReadOptions& options,
      const std::vector<ColumnFamilyHandle*>& column_family,
      std::vector<Iterator*>* iterators) override {
    return STATUS(NotSupported, "Not supported yet");
  }
  const Snapshot* GetSnapshot() override {
    ModelSnapshot* snapshot = new ModelSnapshot;
    snapshot->map_ = map_;
    return snapshot;
  }

  std::unique_ptr<Iterator> NewIndexIterator(
      const ReadOptions& options, SkipLastEntry skip_last_index_entry,
      ColumnFamilyHandle* column_family) override {
    return nullptr;
  }

  std::unique_ptr<DataBlockAwareIndexIterator> NewDataBlockAwareIndexIterator(
      const ReadOptions& options, SkipLastEntry skip_last_index_entry,
      ColumnFamilyHandle* column_family) override {
    return nullptr;
  }

  void ReleaseSnapshot(const Snapshot* snapshot) override {
    delete reinterpret_cast<const ModelSnapshot*>(snapshot);
  }

  virtual Status Write(const WriteOptions& options,
                       WriteBatch* batch) override {
    class Handler : public WriteBatch::Handler {
     public:
      KVMap* map_;
      void Put(const Slice& key, const Slice& value) override {
        (*map_)[key.ToString()] = value.ToString();
      }
      void Merge(const Slice& key, const Slice& value) override {
        // ignore merge for now
        // (*map_)[key.ToString()] = value.ToString();
      }
      void Delete(const Slice& key) override {
        map_->erase(key.ToString());
      }
    };
    Handler handler;
    handler.map_ = &map_;
    return batch->Iterate(&handler);
  }

  using DB::GetProperty;
  virtual bool GetProperty(ColumnFamilyHandle* column_family,
                           const Slice& property, std::string* value) override {
    return false;
  }
  using DB::GetIntProperty;
  virtual bool GetIntProperty(ColumnFamilyHandle* column_family,
                              const Slice& property, uint64_t* value) override {
    return false;
  }
  using DB::GetAggregatedIntProperty;
  virtual bool GetAggregatedIntProperty(const Slice& property,
                                        uint64_t* value) override {
    return false;
  }
  using DB::GetApproximateSizes;
  virtual void GetApproximateSizes(ColumnFamilyHandle* column_family,
                                   const Range* range, int n, uint64_t* sizes,
                                   bool include_memtable) override {
    for (int i = 0; i < n; i++) {
      sizes[i] = 0;
    }
  }
  using DB::CompactRange;
  virtual Status CompactRange(const CompactRangeOptions& options,
                              ColumnFamilyHandle* column_family,
                              const Slice* start, const Slice* end) override {
    return STATUS(NotSupported, "Not supported operation.");
  }

  using DB::CompactFiles;
  virtual Status CompactFiles(
      const CompactionOptions& compact_options,
      ColumnFamilyHandle* column_family,
      const std::vector<std::string>& input_file_names,
      const int output_level, const int output_path_id = -1) override {
    return STATUS(NotSupported, "Not supported operation.");
  }

  Status PauseBackgroundWork() override {
    return STATUS(NotSupported, "Not supported operation.");
  }

  Status ContinueBackgroundWork() override {
    return STATUS(NotSupported, "Not supported operation.");
  }

  Status EnableAutoCompaction(
      const std::vector<ColumnFamilyHandle*>& column_family_handles) override {
    return STATUS(NotSupported, "Not supported operation.");
  }

  using DB::NumberLevels;
  int NumberLevels(ColumnFamilyHandle* column_family) override {
    return 1;
  }

  using DB::MaxMemCompactionLevel;
  virtual int MaxMemCompactionLevel(
      ColumnFamilyHandle* column_family) override {
    return 1;
  }

  using DB::Level0StopWriteTrigger;
  virtual int Level0StopWriteTrigger(
      ColumnFamilyHandle* column_family) override {
    return -1;
  }

  const std::string& GetName() const override { return name_; }

  Env* GetEnv() const override { return nullptr; }

  Env* GetCheckpointEnv() const override { return nullptr; }

  using DB::GetOptions;
  virtual const Options& GetOptions(
      ColumnFamilyHandle* column_family) const override {
    return options_;
  }

  using DB::GetDBOptions;
  const DBOptions& GetDBOptions() const override { return options_; }

  using DB::Flush;
  virtual Status Flush(const rocksdb::FlushOptions& options,
                       ColumnFamilyHandle* column_family) override {
    return Status::OK();
  }

  using DB::WaitForFlush;
  virtual Status WaitForFlush(ColumnFamilyHandle* column_family) override {
    return Status::OK();
  }

  Status SyncWAL() override {
    return Status::OK();
  }

  Status DisableFileDeletions() override { return Status::OK(); }

  Status EnableFileDeletions(bool force) override {
    return Status::OK();
  }
  virtual Status GetLiveFiles(std::vector<std::string>&, uint64_t* size,
                              bool flush_memtable = true) override {
    return Status::OK();
  }

  Status GetSortedWalFiles(VectorLogPtr* files) override {
    return Status::OK();
  }

  Status DeleteFile(std::string name) override { return Status::OK(); }

  virtual Status GetUpdatesSince(
      rocksdb::SequenceNumber, unique_ptr<rocksdb::TransactionLogIterator>*,
      const TransactionLogIterator::ReadOptions&
          read_options = TransactionLogIterator::ReadOptions()) override {
    return NotSupported();
  }

  virtual void GetColumnFamilyMetaData(
      ColumnFamilyHandle* column_family,
      ColumnFamilyMetaData* metadata) override {}

  virtual void GetColumnFamiliesOptions(
      std::vector<std::string>* column_family_names,
      std::vector<ColumnFamilyOptions>* column_family_options) override {}

  Status GetDbIdentity(std::string* identity) const override {
    return Status::OK();
  }

  SequenceNumber GetLatestSequenceNumber() const override { return 0; }

  uint64_t GetNextFileNumber() const override { return 0; }

  ColumnFamilyHandle* DefaultColumnFamily() const override {
    return nullptr;
  }

  Result<std::string> GetMiddleKey() override {
    return NotSupported();
  }

 private:
  Status NotSupported() const {
    return STATUS(NotSupported, "Not supported in Model DB");
  }

  class ModelIter final : public Iterator {
   public:
    ModelIter(const KVMap* map, bool owned)
        : map_(map), owned_(owned), iter_(map_->end()) {
    }
    ~ModelIter() {
      if (owned_) delete map_;
    }
    const KeyValueEntry& SeekToFirst() override {
      iter_ = map_->begin();
      return Entry();
    }
    const KeyValueEntry& SeekToLast() override {
      if (map_->empty()) {
        iter_ = map_->end();
      } else {
        iter_ = map_->find(map_->rbegin()->first);
      }
      return Entry();
    }
    const KeyValueEntry& Seek(Slice k) override {
      iter_ = map_->lower_bound(k.ToString());
      return Entry();
    }
    const KeyValueEntry& Next() override {
      ++iter_;
      return Entry();
    }
    const KeyValueEntry& Prev() override {
      if (iter_ == map_->begin()) {
        iter_ = map_->end();
        return Entry();
      }
      --iter_;
      return Entry();
    }

    const KeyValueEntry& Entry() const override {
      if (iter_ == map_->end()) {
        return KeyValueEntry::Invalid();
      }
      entry_ = {
        .key = iter_->first,
        .value = iter_->second,
      };
      return entry_;
    }

    Status status() const override { return Status::OK(); }

   private:
    const KVMap* const map_;
    const bool owned_;  // Do we own map_
    KVMap::const_iterator iter_;
    mutable KeyValueEntry entry_;
  };
  const Options options_;
  KVMap map_;
  std::string name_ = "";
};

static std::string RandomKey(Random* rnd, int minimum = 0) {
  int len;
  do {
    len = (rnd->OneIn(3)
           ? 1                // Short sometimes to encourage collisions
           : (rnd->OneIn(100) ? rnd->Skewed(10) : rnd->Uniform(10)));
  } while (len < minimum);
  return test::RandomKey(rnd, len);
}

static bool CompareIterators(int step,
                             DB* model,
                             DB* db,
                             const Snapshot* model_snap,
                             const Snapshot* db_snap) {
  ReadOptions options;
  options.snapshot = model_snap;
  Iterator* miter = model->NewIterator(options);
  options.snapshot = db_snap;
  Iterator* dbiter = db->NewIterator(options);
  bool ok = true;
  for (miter->SeekToFirst(), dbiter->SeekToFirst();
       ok && miter->Valid() && dbiter->Valid();
       miter->Next(), dbiter->Next()) {
    if (miter->key().compare(dbiter->key()) != 0) {
      fprintf(stderr, "step %d: Key mismatch: '%s' vs. '%s'\n",
              step,
              EscapeString(miter->key()).c_str(),
              EscapeString(dbiter->key()).c_str());
      ok = false;
      break;
    }

    if (miter->value().compare(dbiter->value()) != 0) {
      fprintf(stderr, "step %d: Value mismatch for key '%s': '%s' vs. '%s'\n",
              step,
              EscapeString(miter->key()).c_str(),
              EscapeString(miter->value()).c_str(),
              EscapeString(miter->value()).c_str());
      ok = false;
    }
  }

  if (ok) {
    if (miter->Valid() != dbiter->Valid()) {
      fprintf(stderr, "step %d: Mismatch at end of iterators: %d vs. %d\n",
              step, miter->Valid(), dbiter->Valid());
      ok = false;
    }
  }

  if (!miter->status().ok()) {
    LOG(ERROR) << "miter->status(): " << miter->status();
    ok = false;
  }

  if (!dbiter->status().ok()) {
    LOG(ERROR) << "dbiter->status(): " << dbiter->status();
    ok = false;
  }

  delete miter;
  delete dbiter;
  return ok;
}

class DBTestRandomized : public DBTest,
                         public ::testing::WithParamInterface<int> {
 public:
  void SetUp() override { option_config_ = GetParam(); }

  static std::vector<int> GenerateOptionConfigs() {
    std::vector<int> option_configs;
    for (int option_config = kDefault; option_config < kEnd; ++option_config) {
      if (!ShouldSkipOptions(option_config, kSkipDeletesFilterFirst | kSkipNoSeekToLast)) {
        option_configs.push_back(option_config);
      }
    }
    option_configs.push_back(kBlockBasedTableWithIndexRestartInterval);
    return option_configs;
  }
};

INSTANTIATE_TEST_CASE_P(
    DBTestRandomized, DBTestRandomized,
    ::testing::ValuesIn(DBTestRandomized::GenerateOptionConfigs()));

TEST_P(DBTestRandomized, Randomized) {
  anon::OptionsOverride options_override;

  Options options = CurrentOptions(options_override);
  DestroyAndReopen(options);

  Random rnd(test::RandomSeed() + GetParam());
  ModelDB model(options);
    const int N = 10000;
    const Snapshot* model_snap = nullptr;
    const Snapshot* db_snap = nullptr;
    std::string k, v;
    for (int step = 0; step < N; step++) {
      // TODO(sanjay): Test Get() works
      int p = rnd.Uniform(100);
      int minimum = 0;
      if (option_config_ == kHashSkipList ||
          option_config_ == kHashLinkList ||
          option_config_ == kPlainTableFirstBytePrefix ||
          option_config_ == kBlockBasedTableWithWholeKeyHashIndex ||
          option_config_ == kBlockBasedTableWithPrefixHashIndex) {
        minimum = 1;
      }
      if (p < 45) {                               // Put
        k = RandomKey(&rnd, minimum);
        v = RandomString(&rnd,
                         rnd.OneIn(20)
                         ? 100 + rnd.Uniform(100)
                         : rnd.Uniform(8));
        ASSERT_OK(model.Put(WriteOptions(), k, v));
        ASSERT_OK(db_->Put(WriteOptions(), k, v));
      } else if (p < 90) {                        // Delete
        k = RandomKey(&rnd, minimum);
        ASSERT_OK(model.Delete(WriteOptions(), k));
        ASSERT_OK(db_->Delete(WriteOptions(), k));
      } else {                                    // Multi-element batch
        WriteBatch b;
        const int num = rnd.Uniform(8);
        for (int i = 0; i < num; i++) {
          if (i == 0 || !rnd.OneIn(10)) {
            k = RandomKey(&rnd, minimum);
          } else {
            // Periodically re-use the same key from the previous iter, so
            // we have multiple entries in the write batch for the same key
          }
          if (rnd.OneIn(2)) {
            v = RandomString(&rnd, rnd.Uniform(10));
            b.Put(k, v);
          } else {
            b.Delete(k);
          }
        }
        ASSERT_OK(model.Write(WriteOptions(), &b));
        ASSERT_OK(db_->Write(WriteOptions(), &b));
      }

      if ((step % 100) == 0) {
        // For DB instances that use the hash index + block-based table, the
        // iterator will be invalid right when seeking a non-existent key, right
        // than return a key that is close to it.
        if (option_config_ != kBlockBasedTableWithWholeKeyHashIndex &&
            option_config_ != kBlockBasedTableWithPrefixHashIndex) {
          ASSERT_TRUE(CompareIterators(step, &model, db_, nullptr, nullptr));
          ASSERT_TRUE(CompareIterators(step, &model, db_, model_snap, db_snap));
        }

        // Save a snapshot from each DB this time that we'll use next
        // time we compare things, to make sure the current state is
        // preserved with the snapshot
        if (model_snap != nullptr) model.ReleaseSnapshot(model_snap);
        if (db_snap != nullptr) db_->ReleaseSnapshot(db_snap);

        Reopen(options);
        ASSERT_TRUE(CompareIterators(step, &model, db_, nullptr, nullptr));

        model_snap = model.GetSnapshot();
        db_snap = db_->GetSnapshot();
      }
    }
    if (model_snap != nullptr) model.ReleaseSnapshot(model_snap);
    if (db_snap != nullptr) db_->ReleaseSnapshot(db_snap);
}

TEST_F(DBTest, MultiGetSimple) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    ASSERT_OK(Put(1, "k1", "v1"));
    ASSERT_OK(Put(1, "k2", "v2"));
    ASSERT_OK(Put(1, "k3", "v3"));
    ASSERT_OK(Put(1, "k4", "v4"));
    ASSERT_OK(Delete(1, "k4"));
    ASSERT_OK(Put(1, "k5", "v5"));
    ASSERT_OK(Delete(1, "no_key"));

    std::vector<Slice> keys({"k1", "k2", "k3", "k4", "k5", "no_key"});

    std::vector<std::string> values(20, "Temporary data to be overwritten");
    std::vector<ColumnFamilyHandle*> cfs(keys.size(), handles_[1]);

    std::vector<Status> s = db_->MultiGet(ReadOptions(), cfs, keys, &values);
    ASSERT_EQ(values.size(), keys.size());
    ASSERT_EQ(values[0], "v1");
    ASSERT_EQ(values[1], "v2");
    ASSERT_EQ(values[2], "v3");
    ASSERT_EQ(values[4], "v5");

    ASSERT_OK(s[0]);
    ASSERT_OK(s[1]);
    ASSERT_OK(s[2]);
    ASSERT_TRUE(s[3].IsNotFound());
    ASSERT_OK(s[4]);
    ASSERT_TRUE(s[5].IsNotFound());
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, MultiGetEmpty) {
  do {
    CreateAndReopenWithCF({"pikachu"}, CurrentOptions());
    // Empty Key Set
    std::vector<Slice> keys;
    std::vector<std::string> values;
    std::vector<ColumnFamilyHandle*> cfs;
    std::vector<Status> s = db_->MultiGet(ReadOptions(), cfs, keys, &values);
    ASSERT_EQ(s.size(), 0U);

    // Empty Database, Empty Key Set
    Options options = CurrentOptions();
    options.create_if_missing = true;
    DestroyAndReopen(options);
    CreateAndReopenWithCF({"pikachu"}, options);
    s = db_->MultiGet(ReadOptions(), cfs, keys, &values);
    ASSERT_EQ(s.size(), 0U);

    // Empty Database, Search for Keys
    keys.resize(2);
    keys[0] = "a";
    keys[1] = "b";
    cfs.push_back(handles_[0]);
    cfs.push_back(handles_[1]);
    s = db_->MultiGet(ReadOptions(), cfs, keys, &values);
    ASSERT_EQ(s.size(), 2);
    ASSERT_TRUE(s[0].IsNotFound() && s[1].IsNotFound());
  } while (ChangeCompactOptions());
}

TEST_F(DBTest, BlockBasedTablePrefixIndexTest) {
  // create a DB with block prefix index
  BlockBasedTableOptions table_options;
  Options options = CurrentOptions();
  table_options.index_type = IndexType::kHashSearch;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  options.prefix_extractor.reset(NewFixedPrefixTransform(1));


  Reopen(options);
  ASSERT_OK(Put("k1", "v1"));
  ASSERT_OK(Flush());
  ASSERT_OK(Put("k2", "v2"));

  // Reopen it without prefix extractor, make sure everything still works.
  // RocksDB should just fall back to the binary index.
  table_options.index_type = IndexType::kBinarySearch;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  options.prefix_extractor.reset();

  Reopen(options);
  ASSERT_EQ("v1", Get("k1"));
  ASSERT_EQ("v2", Get("k2"));
}

TEST_F(DBTest, ChecksumTest) {
  BlockBasedTableOptions table_options;
  Options options = CurrentOptions();

  table_options.checksum = kCRC32c;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  Reopen(options);
  ASSERT_OK(Put("a", "b"));
  ASSERT_OK(Put("c", "d"));
  ASSERT_OK(Flush());  // table with crc checksum

  table_options.checksum = kxxHash;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  Reopen(options);
  ASSERT_OK(Put("e", "f"));
  ASSERT_OK(Put("g", "h"));
  ASSERT_OK(Flush());  // table with xxhash checksum

  table_options.checksum = kCRC32c;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  Reopen(options);
  ASSERT_EQ("b", Get("a"));
  ASSERT_EQ("d", Get("c"));
  ASSERT_EQ("f", Get("e"));
  ASSERT_EQ("h", Get("g"));

  table_options.checksum = kCRC32c;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  Reopen(options);
  ASSERT_EQ("b", Get("a"));
  ASSERT_EQ("d", Get("c"));
  ASSERT_EQ("f", Get("e"));
  ASSERT_EQ("h", Get("g"));
}

TEST_P(DBTestWithParam, FIFOCompactionTest) {
  for (int iter = 0; iter < 2; ++iter) {
    // first iteration -- auto compaction
    // second iteration -- manual compaction
    Options options;
    options.compaction_style = kCompactionStyleFIFO;
    options.write_buffer_size = 100 << 10;                             // 100KB
    options.arena_block_size = 4096;
    options.compaction_options_fifo.max_table_files_size = 500 << 10;  // 500KB
    options.compression = kNoCompression;
    options.create_if_missing = true;
    options.max_subcompactions = max_subcompactions_;
    if (iter == 1) {
      options.disable_auto_compactions = true;
    }
    options = CurrentOptions(options);
    DestroyAndReopen(options);

    Random rnd(301);
    for (int i = 0; i < 6; ++i) {
      for (int j = 0; j < 110; ++j) {
        ASSERT_OK(Put(ToString(i * 100 + j), RandomString(&rnd, 980)));
      }
      // flush should happen here
      ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
    }
    if (iter == 0) {
      ASSERT_OK(dbfull()->TEST_WaitForCompact());
    } else {
      CompactRangeOptions cro;
      cro.exclusive_manual_compaction = exclusive_manual_compaction_;
      ASSERT_OK(db_->CompactRange(cro, nullptr, nullptr));
    }
    // only 5 files should survive
    ASSERT_EQ(NumTableFilesAtLevel(0), 5);
    for (int i = 0; i < 50; ++i) {
      // these keys should be deleted in previous compaction
      ASSERT_EQ("NOT_FOUND", Get(ToString(i)));
    }
  }
}

// verify that we correctly deprecated timeout_hint_us
TEST_F(DBTest, SimpleWriteTimeoutTest) {
  WriteOptions write_opt;
  write_opt.timeout_hint_us = 0;
  ASSERT_OK(Put(Key(1), Key(1) + std::string(100, 'v'), write_opt));
  write_opt.timeout_hint_us = 10;
  ASSERT_NOK(Put(Key(1), Key(1) + std::string(100, 'v'), write_opt));
}

// Class that wraps DB write instructions
class DBWriter : public DBHolder {
 public:
  struct WriteStat {
    int64_t bytes_count_ = 0;
    yb::MonoTime start_time_;
    yb::MonoTime stop_time_;

    Result<double> GetRate() const {
      RETURN_NOT_OK(Verify());
      return bytes_count_ / (stop_time_ - start_time_).ToSeconds();
    }

    Status Verify() const {
      SCHECK_GT(bytes_count_, 0, IllegalState, "Bytes count must be greater than zero");
      SCHECK_LT(start_time_, stop_time_, IllegalState, "Start time must be less than stop time");
      return Status::OK();
    }
  };

  explicit DBWriter(std::string path = "/db_test_write")
      : DBHolder(path) , write_options_(CurrentOptions()) {
    write_options_.write_buffer_size = 1_MB;
    write_options_.level0_file_num_compaction_trigger = 2;
    write_options_.target_file_size_base = 1_MB;
    write_options_.max_bytes_for_level_base = 4_MB;
    write_options_.max_bytes_for_level_multiplier = 4;
    write_options_.compression = kNoCompression;
    write_options_.create_if_missing = true;
    write_options_.env = env_;
    write_options_.IncreaseParallelism(4);
  }

  Status InitRate(boost::optional<double> rate_bytes_per_sec = boost::none) {
    if (rate_bytes_per_sec) {
      write_reference_rate_bytes_per_sec_ = *rate_bytes_per_sec;
      SCHECK_GT(write_reference_rate_bytes_per_sec_, 0.0,
                IllegalState, "Reference rate must be greater than zero");
    } else {
      RETURN_NOT_OK(ExecWrite());
      write_reference_rate_bytes_per_sec_ = VERIFY_RESULT(write_stat_.GetRate());
    }
    return Status::OK();
  }

  Status ExecWrite() {
    env_->bytes_written_ = 0;
    DestroyAndReopen(write_options_);
    WriteOptions wo;
    wo.disableWAL = true;

    // Write ~48M data
    auto rnd = &yb::ThreadLocalRandom();
    write_stat_.start_time_ = yb::MonoTime::Now();
    for (size_t i = 0; i < (48 << 10); ++i) {
      RETURN_NOT_OK_PREPEND(
        Put(yb::RandomString(32, rnd), yb::RandomString(1_KB + 1, rnd), wo),
        "iteration #" + std::to_string(i));
    }
    write_stat_.stop_time_ = yb::MonoTime::Now();
    Close();
    write_stat_.bytes_count_ = env_->bytes_written_.load();
    return write_stat_.Verify();
  }

  Status ExecWriteWithNewRateLimiter(double rate_bytes_per_sec) {
    write_options_.rate_limiter.reset(
      NewGenericRateLimiter(static_cast<int64_t>(rate_bytes_per_sec)));
    RETURN_NOT_OK(ExecWrite());
    SCHECK_EQ(write_stat_.bytes_count_, write_options_.rate_limiter->GetTotalBytesThrough(),
              IllegalState, "Bytes count vs rate limiter total bytes inconsistency");
    return Status::OK();
  }

  Status MeasureWrite(double rate_ratio, double max_rate_ratio) {
    SCHECK_GT(rate_ratio, 0.0, IllegalState, "Rate ratio must be greater than zero");
    SCHECK_LE(rate_ratio, max_rate_ratio,
              IllegalState, "Max rate ratio must be greater than rate ratio");
    RETURN_NOT_OK(ExecWriteWithNewRateLimiter(write_reference_rate_bytes_per_sec_ * rate_ratio));
    return CheckRatio(write_stat_, max_rate_ratio);
  }

  Status CheckRatio(const WriteStat& period, double expected_ratio) const {
    SCHECK_GT(write_reference_rate_bytes_per_sec_, 0.0,
              IllegalState, "Reference rate must be greater than zero");
    auto ratio = VERIFY_RESULT(period.GetRate()) / write_reference_rate_bytes_per_sec_;
    LOG(INFO) << "Write rate ratio = " << std::fixed << std::setprecision(2) << ratio
              << ", expected ratio = " << std::fixed << std::setprecision(2) << expected_ratio;
    SCHECK_LE(ratio, expected_ratio, IllegalState, "Ratio must be less than expected ratio");
    return Status::OK();
  }

 public:
  Options write_options_;
  WriteStat write_stat_;
  double write_reference_rate_bytes_per_sec_ = 0.0;
};

/*
 * This test is not reliable enough as it heavily depends on disk behavior.
 */
TEST_F_EX(DBTest, RateLimitingTest, RocksDBTest) {
  DBWriter dbw;

  // # no rate limiting, initializing
  ASSERT_OK(dbw.InitRate());

  // # rate limiting with 0.7 x threshold
  ASSERT_OK(dbw.MeasureWrite(0.7, 0.8));

  // # rate limiting with half of the raw_rate
  ASSERT_OK(dbw.MeasureWrite(0.5, 0.6));

  // # shared rate limiting with half of the raw rate
  {
    const size_t kNumDBs = yb::RandomUniformInt(4, 8, &yb::ThreadLocalRandom());
    LOG(INFO) << "Number of writers: " << kNumDBs;

    std::vector<std::unique_ptr<DBWriter>> writers;
    yb::TestThreadHolder workers;
    yb::CountDownLatch ready_latch(kNumDBs);
    yb::CountDownLatch done_latch(kNumDBs);
    std::shared_ptr<RateLimiter> shared_limiter(
        NewGenericRateLimiter(static_cast<int64_t>(dbw.write_reference_rate_bytes_per_sec_)));

    while (writers.size() != kNumDBs) {
      writers.push_back(std::make_unique<DBWriter>(std::to_string(writers.size())));
      auto& one_writer = *writers.back();
      one_writer.write_options_.rate_limiter = shared_limiter;
      workers.AddThread([&one_writer, &ready_latch, &done_latch](){
        ready_latch.CountDown();
        ready_latch.Wait();
        EXPECT_OK(one_writer.ExecWrite());
        done_latch.CountDown(); // To make TSAN happy
      });
    }

    // Wait for all jobs are done
    done_latch.Wait();
    workers.JoinAll();

    // Measure rates on writing is done: write rate for multiple parallel writers
    // with the same shared rate limiter should be close to the rate of single writer
    // with the same rate
    DBWriter::WriteStat stat;
    stat.start_time_ = yb::MonoTime::Max();
    stat.stop_time_  = yb::MonoTime::Min();
    for (const auto& w : writers) {
      stat.bytes_count_ += w->write_stat_.bytes_count_;
      stat.start_time_ = std::min(stat.start_time_, w->write_stat_.start_time_);
      stat.stop_time_  = std::max(stat.stop_time_, w->write_stat_.stop_time_);
    }
    ASSERT_EQ(stat.bytes_count_, shared_limiter->GetTotalBytesThrough());
    ASSERT_OK(dbw.CheckRatio(stat, 1.1));
  }
}

TEST_F(DBTest, TableOptionsSanitizeTest) {
  Options options = CurrentOptions();
  options.create_if_missing = true;
  DestroyAndReopen(options);
  ASSERT_EQ(db_->GetOptions().allow_mmap_reads, false);

  options.table_factory.reset(new PlainTableFactory());
  options.prefix_extractor.reset(NewNoopTransform());
  Destroy(options);
  ASSERT_TRUE(!TryReopen(options).IsNotSupported());

  // Test for check of prefix_extractor when hash index is used for
  // block-based table
  BlockBasedTableOptions to;
  to.index_type = IndexType::kHashSearch;
  options = CurrentOptions();
  options.create_if_missing = true;
  options.table_factory.reset(NewBlockBasedTableFactory(to));
  ASSERT_TRUE(TryReopen(options).IsInvalidArgument());
  options.prefix_extractor.reset(NewFixedPrefixTransform(1));
  ASSERT_OK(TryReopen(options));
}

TEST_F(DBTest, ConcurrentMemtableNotSupported) {
  Options options = CurrentOptions();
  options.allow_concurrent_memtable_write = true;
  options.soft_pending_compaction_bytes_limit = 0;
  options.hard_pending_compaction_bytes_limit = 100;
  options.create_if_missing = true;

  Close();
  ASSERT_OK(DestroyDB(dbname_, options));
  options.memtable_factory.reset(NewHashLinkListRepFactory(4, 0, 3, true, 4));
  ASSERT_NOK(TryReopen(options));

  options.memtable_factory.reset(new SkipListFactory);
  ASSERT_OK(TryReopen(options));

  ColumnFamilyOptions cf_options(options);
  cf_options.memtable_factory.reset(
      NewHashLinkListRepFactory(4, 0, 3, true, 4));
  ColumnFamilyHandle* handle;
  ASSERT_NOK(db_->CreateColumnFamily(cf_options, "name", &handle));
}

TEST_F(DBTest, SanitizeNumThreads) {
  for (int attempt = 0; attempt < 2; attempt++) {
    const size_t kTotalTasks = 8;
    test::SleepingBackgroundTask sleeping_tasks[kTotalTasks];

    Options options = CurrentOptions();
    if (attempt == 0) {
      options.max_background_compactions = 3;
      options.max_background_flushes = 2;
    }
    options.create_if_missing = true;
    DestroyAndReopen(options);

    for (size_t i = 0; i < kTotalTasks; i++) {
      // Insert 5 tasks to low priority queue and 5 tasks to high priority queue
      env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask,
                     &sleeping_tasks[i],
                     (i < 4) ? Env::Priority::LOW : Env::Priority::HIGH);
    }

    // Wait 100 milliseconds for they are scheduled.
    env_->SleepForMicroseconds(100000);

    // pool size 3, total task 4. Queue size should be 1.
    ASSERT_EQ(1U, options.env->GetThreadPoolQueueLen(Env::Priority::LOW));
    // pool size 2, total task 4. Queue size should be 2.
    ASSERT_EQ(2U, options.env->GetThreadPoolQueueLen(Env::Priority::HIGH));

    for (size_t i = 0; i < kTotalTasks; i++) {
      sleeping_tasks[i].WakeUp();
      sleeping_tasks[i].WaitUntilDone();
    }

    ASSERT_OK(Put("abc", "def"));
    ASSERT_EQ("def", Get("abc"));
    ASSERT_OK(Flush());
    ASSERT_EQ("def", Get("abc"));
  }
}

TEST_F(DBTest, DBIteratorBoundTest) {
  Options options = CurrentOptions();
  options.env = env_;
  options.create_if_missing = true;

  options.prefix_extractor = nullptr;
  DestroyAndReopen(options);
  ASSERT_OK(Put("a", "0"));
  ASSERT_OK(Put("foo", "bar"));
  ASSERT_OK(Put("foo1", "bar1"));
  ASSERT_OK(Put("g1", "0"));

  // testing basic case with no iterate_upper_bound and no prefix_extractor
  {
    ReadOptions ro;
    ro.iterate_upper_bound = nullptr;

    std::unique_ptr<Iterator> iter(db_->NewIterator(ro));

    iter->Seek("foo");

    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("foo")), 0);

    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("foo1")), 0);

    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("g1")), 0);
  }

  // testing iterate_upper_bound and forward iterator
  // to make sure it stops at bound
  {
    ReadOptions ro;
    // iterate_upper_bound points beyond the last expected entry
    Slice prefix("foo2");
    ro.iterate_upper_bound = &prefix;

    std::unique_ptr<Iterator> iter(db_->NewIterator(ro));

    iter->Seek("foo");

    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("foo")), 0);

    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(("foo1")), 0);

    iter->Next();
    // should stop here...
    ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
  }
  // Testing SeekToLast with iterate_upper_bound set
  {
    ReadOptions ro;

    Slice prefix("foo");
    ro.iterate_upper_bound = &prefix;

    std::unique_ptr<Iterator> iter(db_->NewIterator(ro));

    iter->SeekToLast();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("a")), 0);
  }

  // prefix is the first letter of the key
  options.prefix_extractor.reset(NewFixedPrefixTransform(1));

  DestroyAndReopen(options);
  ASSERT_OK(Put("a", "0"));
  ASSERT_OK(Put("foo", "bar"));
  ASSERT_OK(Put("foo1", "bar1"));
  ASSERT_OK(Put("g1", "0"));

  // testing with iterate_upper_bound and prefix_extractor
  // Seek target and iterate_upper_bound are not is same prefix
  // This should be an error
  {
    ReadOptions ro;
    Slice upper_bound("g");
    ro.iterate_upper_bound = &upper_bound;

    std::unique_ptr<Iterator> iter(db_->NewIterator(ro));

    iter->Seek("foo");

    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ("foo", iter->key().ToString());

    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ("foo1", iter->key().ToString());

    iter->Next();
    ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
  }

  // testing that iterate_upper_bound prevents iterating over deleted items
  // if the bound has already reached
  {
    options.prefix_extractor = nullptr;
    DestroyAndReopen(options);
    ASSERT_OK(Put("a", "0"));
    ASSERT_OK(Put("b", "0"));
    ASSERT_OK(Put("b1", "0"));
    ASSERT_OK(Put("c", "0"));
    ASSERT_OK(Put("d", "0"));
    ASSERT_OK(Put("e", "0"));
    ASSERT_OK(Delete("c"));
    ASSERT_OK(Delete("d"));

    // base case with no bound
    ReadOptions ro;
    ro.iterate_upper_bound = nullptr;

    std::unique_ptr<Iterator> iter(db_->NewIterator(ro));

    iter->Seek("b");
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("b")), 0);

    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(("b1")), 0);

    perf_context.Reset();
    iter->Next();

    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(static_cast<int>(perf_context.internal_delete_skipped_count), 2);

    // now testing with iterate_bound
    Slice prefix("c");
    ro.iterate_upper_bound = &prefix;

    iter.reset(db_->NewIterator(ro));

    perf_context.Reset();

    iter->Seek("b");
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Slice("b")), 0);

    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(("b1")), 0);

    iter->Next();
    // the iteration should stop as soon as the the bound key is reached
    // even though the key is deleted
    // hence internal_delete_skipped_count should be 0
    ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(static_cast<int>(perf_context.internal_delete_skipped_count), 0);
  }
}

TEST_F(DBTest, WriteSingleThreadEntry) {
  std::vector<std::thread> threads;
  dbfull()->TEST_LockMutex();
  auto w = dbfull()->TEST_BeginWrite();
  threads.emplace_back([&] { ASSERT_OK(Put("a", "b")); });
  env_->SleepForMicroseconds(10000);
  threads.emplace_back([&] { ASSERT_OK(Flush()); });
  env_->SleepForMicroseconds(10000);
  dbfull()->TEST_UnlockMutex();
  dbfull()->TEST_LockMutex();
  dbfull()->TEST_EndWrite(w);
  dbfull()->TEST_UnlockMutex();

  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(DBTest, DisableDataSyncTest) {
  env_->sync_counter_.store(0);
  // iter 0 -- no sync
  // iter 1 -- sync
  for (int iter = 0; iter < 2; ++iter) {
    Options options = CurrentOptions();
    options.disableDataSync = iter == 0;
    options.create_if_missing = true;
    options.num_levels = 10;
    options.env = env_;
    Reopen(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    MakeTables(10, "a", "z");
    Compact("a", "z");

    if (iter == 0) {
      ASSERT_EQ(env_->sync_counter_.load(), 0);
    } else {
      ASSERT_GT(env_->sync_counter_.load(), 0);
    }
    Destroy(options);
  }
}

TEST_F(DBTest, DynamicMemtableOptions) {
  const uint64_t k64KB = 1 << 16;
  const uint64_t k128KB = 1 << 17;
  const uint64_t k5KB = 5 * 1024;
  const int kNumPutsBeforeWaitForFlush = 64;
  Options options;
  options.env = env_;
  options.create_if_missing = true;
  options.compression = kNoCompression;
  options.max_background_compactions = 1;
  options.write_buffer_size = k64KB;
  options.arena_block_size = 16 * 1024;
  options.max_write_buffer_number = 2;
  // Don't trigger compact/slowdown/stop
  options.level0_file_num_compaction_trigger = 1024;
  options.level0_slowdown_writes_trigger = 1024;
  options.level0_stop_writes_trigger = 1024;
  DestroyAndReopen(options);

  auto gen_l0_kb = [this](int size) {
    Random rnd(301);
    for (int i = 0; i < size; i++) {
      ASSERT_OK(Put(Key(i), RandomString(&rnd, 1024)));

      // The following condition prevents a race condition between flush jobs
      // acquiring work and this thread filling up multiple memtables. Without
      // this, the flush might produce less files than expected because
      // multiple memtables are flushed into a single L0 file. This race
      // condition affects assertion (A).
      if (i % kNumPutsBeforeWaitForFlush == kNumPutsBeforeWaitForFlush - 1) {
        ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
      }
    }
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  };

  // Test write_buffer_size
  gen_l0_kb(64);
  ASSERT_EQ(NumTableFilesAtLevel(0), 1);
  ASSERT_LT(SizeAtLevel(0), k64KB + k5KB);
  ASSERT_GT(SizeAtLevel(0), k64KB - k5KB * 2);

  // Clean up L0
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);

  // Increase buffer size
  ASSERT_OK(dbfull()->SetOptions({
    {"write_buffer_size", "131072"},
  }));

  // The existing memtable is still 64KB in size, after it becomes immutable,
  // the next memtable will be 128KB in size. Write 256KB total, we should
  // have a 64KB L0 file, a 128KB L0 file, and a memtable with 64KB data
  gen_l0_kb(256);
  ASSERT_EQ(NumTableFilesAtLevel(0), 2);  // (A)
  ASSERT_LT(SizeAtLevel(0), k128KB + k64KB + 2 * k5KB);
  ASSERT_GT(SizeAtLevel(0), k128KB + k64KB - 4 * k5KB);

  // Test max_write_buffer_number
  // Block compaction thread, which will also block the flushes because
  // max_background_flushes == 0, so flushes are getting executed by the
  // compaction thread
  env_->SetBackgroundThreads(1, Env::LOW);
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  // Start from scratch and disable compaction/flush. Flush can only happen
  // during compaction but trigger is pretty high
  options.max_background_flushes = 0;
  options.disable_auto_compactions = true;
  DestroyAndReopen(options);

  // Put until writes are stopped, bounded by 256 puts. We should see stop at
  // ~128KB
  int count = 0;
  Random rnd(301);

  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::DelayWrite:Wait",
      [&](void* arg) { sleeping_task_low.WakeUp(); });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  while (!sleeping_task_low.WokenUp() && count < 256) {
    ASSERT_OK(Put(Key(count), RandomString(&rnd, 1024), WriteOptions()));
    count++;
  }
  ASSERT_GT(static_cast<double>(count), 128 * 0.8);
  ASSERT_LT(static_cast<double>(count), 128 * 1.2);

  sleeping_task_low.WaitUntilDone();

  // Increase
  ASSERT_OK(dbfull()->SetOptions({
    {"max_write_buffer_number", "8"},
  }));
  // Clean up memtable and L0
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));

  sleeping_task_low.Reset();
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  count = 0;
  while (!sleeping_task_low.WokenUp() && count < 1024) {
    ASSERT_OK(Put(Key(count), RandomString(&rnd, 1024), WriteOptions()));
    count++;
  }
  // Windows fails this test. Will tune in the future and figure out
  // approp number
#ifndef OS_WIN
  ASSERT_GT(static_cast<double>(count), 512 * 0.8);
  ASSERT_LT(static_cast<double>(count), 512 * 1.2);
#endif
  sleeping_task_low.WaitUntilDone();

  // Decrease
  ASSERT_OK(dbfull()->SetOptions({
    {"max_write_buffer_number", "4"},
  }));
  // Clean up memtable and L0
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));

  sleeping_task_low.Reset();
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);

  count = 0;
  while (!sleeping_task_low.WokenUp() && count < 1024) {
    ASSERT_OK(Put(Key(count), RandomString(&rnd, 1024), WriteOptions()));
    count++;
  }
  // Windows fails this test. Will tune in the future and figure out
  // approp number
#ifndef OS_WIN
  ASSERT_GT(static_cast<double>(count), 256 * 0.8);
  ASSERT_LT(static_cast<double>(count), 266 * 1.2);
#endif
  sleeping_task_low.WaitUntilDone();

  yb::SyncPoint::GetInstance()->DisableProcessing();
}

TEST_F(DBTest, FlushOnDestroy) {
  WriteOptions wo;
  wo.disableWAL = true;
  ASSERT_OK(Put("foo", "v1", wo));
  CancelAllBackgroundWork(db_);
}

TEST_F(DBTest, DynamicLevelCompressionPerLevel) {
  if (!Snappy_Supported()) {
    return;
  }
  const int kNKeys = 120;
  int keys[kNKeys];
  for (int i = 0; i < kNKeys; i++) {
    keys[i] = i;
  }
  std::shuffle(std::begin(keys), std::end(keys), yb::ThreadLocalRandom());

  Random rnd(301);
  Options options;
  options.create_if_missing = true;
  options.db_write_buffer_size = 20480;
  options.write_buffer_size = 20480;
  options.max_write_buffer_number = 2;
  options.level0_file_num_compaction_trigger = 2;
  options.level0_slowdown_writes_trigger = 2;
  options.level0_stop_writes_trigger = 2;
  options.target_file_size_base = 2048;
  options.level_compaction_dynamic_level_bytes = true;
  options.max_bytes_for_level_base = 102400;
  options.max_bytes_for_level_multiplier = 4;
  options.max_background_compactions = 1;
  options.num_levels = 5;

  options.compression_per_level.resize(3);
  options.compression_per_level[0] = kNoCompression;
  options.compression_per_level[1] = kNoCompression;
  options.compression_per_level[2] = kSnappyCompression;

  OnFileDeletionListener* listener = new OnFileDeletionListener();
  options.listeners.emplace_back(listener);

  DestroyAndReopen(options);

  // Insert more than 80K. L4 should be base level. Neither L0 nor L4 should
  // be compressed, so total data size should be more than 80K.
  for (int i = 0; i < 20; i++) {
    ASSERT_OK(Put(Key(keys[i]), CompressibleString(&rnd, 4000)));
  }
  ASSERT_OK(Flush());
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  ASSERT_EQ(NumTableFilesAtLevel(1), 0);
  ASSERT_EQ(NumTableFilesAtLevel(2), 0);
  ASSERT_EQ(NumTableFilesAtLevel(3), 0);
  ASSERT_GT(SizeAtLevel(0) + SizeAtLevel(4), 20U * 4000U);

  // Insert 400KB. Some data will be compressed
  for (int i = 21; i < 120; i++) {
    ASSERT_OK(Put(Key(keys[i]), CompressibleString(&rnd, 4000)));
  }
  ASSERT_OK(Flush());
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ(NumTableFilesAtLevel(1), 0);
  ASSERT_EQ(NumTableFilesAtLevel(2), 0);
  ASSERT_LT(SizeAtLevel(0) + SizeAtLevel(3) + SizeAtLevel(4), 120U * 4000U);
  // Make sure data in files in L3 is not compacted by removing all files
  // in L4 and calculate number of rows
  ASSERT_OK(dbfull()->SetOptions({
      {"disable_auto_compactions", "true"},
  }));
  ColumnFamilyMetaData cf_meta;
  db_->GetColumnFamilyMetaData(&cf_meta);
  for (auto file : cf_meta.levels[4].files) {
    listener->SetExpectedFileName(dbname_ + file.Name());
    ASSERT_OK(dbfull()->DeleteFile(file.Name()));
  }
  listener->VerifyMatchedCount(cf_meta.levels[4].files.size());

  int num_keys = 0;
  std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    num_keys++;
  }
  ASSERT_GT(SizeAtLevel(0) + SizeAtLevel(3), num_keys * 4000U);
}

TEST_F(DBTest, DynamicLevelCompressionPerLevel2) {
  if (!Snappy_Supported() || !LZ4_Supported() || !Zlib_Supported()) {
    return;
  }
  const int kNKeys = 500;
  int keys[kNKeys];
  for (int i = 0; i < kNKeys; i++) {
    keys[i] = i;
  }
  std::shuffle(std::begin(keys), std::end(keys), yb::ThreadLocalRandom());

  Random rnd(301);
  Options options;
  options.create_if_missing = true;
  options.db_write_buffer_size = 6000;
  options.write_buffer_size = 6000;
  options.max_write_buffer_number = 2;
  options.level0_file_num_compaction_trigger = 2;
  options.level0_slowdown_writes_trigger = 2;
  options.level0_stop_writes_trigger = 2;
  options.soft_pending_compaction_bytes_limit = 1024 * 1024;

  // Use file size to distinguish levels
  // L1: 10, L2: 20, L3 40, L4 80
  // L0 is less than 30
  options.target_file_size_base = 10;
  options.target_file_size_multiplier = 2;

  options.level_compaction_dynamic_level_bytes = true;
  options.max_bytes_for_level_base = 200;
  options.max_bytes_for_level_multiplier = 8;
  options.max_background_compactions = 1;
  options.num_levels = 5;
  std::shared_ptr<mock::MockTableFactory> mtf(new mock::MockTableFactory);
  options.table_factory = mtf;

  options.compression_per_level.resize(3);
  options.compression_per_level[0] = kNoCompression;
  options.compression_per_level[1] = kLZ4Compression;
  options.compression_per_level[2] = kZlibCompression;

  DestroyAndReopen(options);
  // When base level is L4, L4 is LZ4.
  std::atomic<int> num_zlib(0);
  std::atomic<int> num_lz4(0);
  std::atomic<int> num_no(0);
  yb::SyncPoint::GetInstance()->SetCallBack(
      "LevelCompactionPicker::PickCompaction:Return", [&](void* arg) {
        Compaction* compaction = reinterpret_cast<Compaction*>(arg);
        if (compaction->output_level() == 4) {
          ASSERT_TRUE(compaction->output_compression() == kLZ4Compression);
          num_lz4.fetch_add(1);
        }
      });
  yb::SyncPoint::GetInstance()->SetCallBack(
      "FlushJob::WriteLevel0Table:output_compression", [&](void* arg) {
        auto* compression = reinterpret_cast<CompressionType*>(arg);
        ASSERT_TRUE(*compression == kNoCompression);
        num_no.fetch_add(1);
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  for (int i = 0; i < 100; i++) {
    ASSERT_OK(Put(Key(keys[i]), RandomString(&rnd, 200)));

    if (i % 25 == 0) {
      ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
    }
  }

  ASSERT_OK(Flush());
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  yb::SyncPoint::GetInstance()->DisableProcessing();
  yb::SyncPoint::GetInstance()->ClearAllCallBacks();

  ASSERT_EQ(NumTableFilesAtLevel(1), 0);
  ASSERT_EQ(NumTableFilesAtLevel(2), 0);
  ASSERT_EQ(NumTableFilesAtLevel(3), 0);
  ASSERT_GT(NumTableFilesAtLevel(4), 0);
  ASSERT_GT(num_no.load(), 2);
  ASSERT_GT(num_lz4.load(), 0);
  int prev_num_files_l4 = NumTableFilesAtLevel(4);

  // After base level turn L4->L3, L3 becomes LZ4 and L4 becomes Zlib
  num_lz4.store(0);
  num_no.store(0);
  yb::SyncPoint::GetInstance()->SetCallBack(
      "LevelCompactionPicker::PickCompaction:Return", [&](void* arg) {
        Compaction* compaction = reinterpret_cast<Compaction*>(arg);
        if (compaction->output_level() == 4 && compaction->start_level() == 3) {
          ASSERT_TRUE(compaction->output_compression() == kZlibCompression);
          num_zlib.fetch_add(1);
        } else {
          ASSERT_TRUE(compaction->output_compression() == kLZ4Compression);
          num_lz4.fetch_add(1);
        }
      });
  yb::SyncPoint::GetInstance()->SetCallBack(
      "FlushJob::WriteLevel0Table:output_compression", [&](void* arg) {
        auto* compression = reinterpret_cast<CompressionType*>(arg);
        ASSERT_TRUE(*compression == kNoCompression);
        num_no.fetch_add(1);
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  for (int i = 101; i < 500; i++) {
    ASSERT_OK(Put(Key(keys[i]), RandomString(&rnd, 200)));
    if (i % 100 == 99) {
      ASSERT_OK(Flush());
      ASSERT_OK(dbfull()->TEST_WaitForCompact());
    }
  }

  yb::SyncPoint::GetInstance()->ClearAllCallBacks();
  yb::SyncPoint::GetInstance()->DisableProcessing();
  ASSERT_EQ(NumTableFilesAtLevel(1), 0);
  ASSERT_EQ(NumTableFilesAtLevel(2), 0);
  ASSERT_GT(NumTableFilesAtLevel(3), 0);
  ASSERT_GT(NumTableFilesAtLevel(4), prev_num_files_l4);
  ASSERT_GT(num_no.load(), 2);
  ASSERT_GT(num_lz4.load(), 0);
  ASSERT_GT(num_zlib.load(), 0);
}

TEST_F(DBTest, DynamicCompactionOptions) {
  // minimum write buffer size is enforced at 64KB
  const uint64_t k32KB = 1 << 15;
  const uint64_t k64KB = 1 << 16;
  const uint64_t k128KB = 1 << 17;
  const uint64_t k1MB = 1 << 20;
  const uint64_t k4KB = 1 << 12;
  Options options;
  options.env = env_;
  options.create_if_missing = true;
  options.compression = kNoCompression;
  options.soft_pending_compaction_bytes_limit = 1024 * 1024;
  options.write_buffer_size = k64KB;
  options.arena_block_size = 4 * k4KB;
  options.max_write_buffer_number = 2;
  // Compaction related options
  options.level0_file_num_compaction_trigger = 3;
  options.level0_slowdown_writes_trigger = 4;
  options.level0_stop_writes_trigger = 8;
  options.max_grandparent_overlap_factor = 10;
  options.expanded_compaction_factor = 25;
  options.source_compaction_factor = 1;
  options.target_file_size_base = k64KB;
  options.target_file_size_multiplier = 1;
  options.max_bytes_for_level_base = k128KB;
  options.max_bytes_for_level_multiplier = 4;

  // Block flush thread and disable compaction thread
  env_->SetBackgroundThreads(1, Env::LOW);
  env_->SetBackgroundThreads(1, Env::HIGH);
  DestroyAndReopen(options);

  auto gen_l0_kb = [this](int start, int size, int stride) {
    Random rnd(301);
    for (int i = 0; i < size; i++) {
      ASSERT_OK(Put(Key(start + stride * i), RandomString(&rnd, 1024)));
    }
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  };

  // Write 3 files that have the same key range.
  // Since level0_file_num_compaction_trigger is 3, compaction should be
  // triggered. The compaction should result in one L1 file
  gen_l0_kb(0, 64, 1);
  ASSERT_EQ(NumTableFilesAtLevel(0), 1);
  gen_l0_kb(0, 64, 1);
  ASSERT_EQ(NumTableFilesAtLevel(0), 2);
  gen_l0_kb(0, 64, 1);
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ("0,1", FilesPerLevel());
  std::vector<LiveFileMetaData> metadata;
  db_->GetLiveFilesMetaData(&metadata);
  ASSERT_EQ(1U, metadata.size());
  ASSERT_LE(metadata[0].total_size, k64KB + k4KB);
  ASSERT_GE(metadata[0].total_size, k64KB - k4KB);

  // Test compaction trigger and target_file_size_base
  // Reduce compaction trigger to 2, and reduce L1 file size to 32KB.
  // Writing to 64KB L0 files should trigger a compaction. Since these
  // 2 L0 files have the same key range, compaction merge them and should
  // result in 2 32KB L1 files.
  ASSERT_OK(dbfull()->SetOptions({
    {"level0_file_num_compaction_trigger", "2"},
    {"target_file_size_base", ToString(k32KB) }
  }));

  gen_l0_kb(0, 64, 1);
  ASSERT_EQ("1,1", FilesPerLevel());
  gen_l0_kb(0, 64, 1);
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ("0,2", FilesPerLevel());
  metadata.clear();
  db_->GetLiveFilesMetaData(&metadata);
  ASSERT_EQ(2U, metadata.size());
  ASSERT_LE(metadata[0].total_size, k32KB + k4KB);
  ASSERT_GE(metadata[0].total_size, k32KB - k4KB);
  ASSERT_LE(metadata[1].total_size, k32KB + k4KB);
  ASSERT_GE(metadata[1].total_size, k32KB - k4KB);

  // Test max_bytes_for_level_base
  // Increase level base size to 256KB and write enough data that will
  // fill L1 and L2. L1 size should be around 256KB while L2 size should be
  // around 256KB x 4.
  ASSERT_OK(dbfull()->SetOptions({
    {"max_bytes_for_level_base", ToString(k1MB) }
  }));

  // writing 96 x 64KB => 6 * 1024KB
  // (L1 + L2) = (1 + 4) * 1024KB
  for (int i = 0; i < 96; ++i) {
    gen_l0_kb(i, 64, 96);
  }
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_GT(SizeAtLevel(1), k1MB / 2);
  ASSERT_LT(SizeAtLevel(1), k1MB + k1MB / 2);

  // Within (0.5, 1.5) of 4MB.
  ASSERT_GT(SizeAtLevel(2), 2 * k1MB);
  ASSERT_LT(SizeAtLevel(2), 6 * k1MB);

  // Test max_bytes_for_level_multiplier and
  // max_bytes_for_level_base. Now, reduce both mulitplier and level base,
  // After filling enough data that can fit in L1 - L3, we should see L1 size
  // reduces to 128KB from 256KB which was asserted previously. Same for L2.
  ASSERT_OK(dbfull()->SetOptions({
    {"max_bytes_for_level_multiplier", "2"},
    {"max_bytes_for_level_base", ToString(k128KB) }
  }));

  // writing 20 x 64KB = 10 x 128KB
  // (L1 + L2 + L3) = (1 + 2 + 4) * 128KB
  for (int i = 0; i < 20; ++i) {
    gen_l0_kb(i, 64, 32);
  }
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  uint64_t total_size =
    SizeAtLevel(1) + SizeAtLevel(2) + SizeAtLevel(3);
  ASSERT_TRUE(total_size < k128KB * 7 * 1.5);

  // Test level0_stop_writes_trigger.
  // Clean up memtable and L0. Block compaction threads. If continue to write
  // and flush memtables. We should see put stop after 8 memtable flushes
  // since level0_stop_writes_trigger = 8
  ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  // Block compaction
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  sleeping_task_low.WaitUntilSleeping();
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  int count = 0;
  Random rnd(301);
  WriteOptions wo;
  while (count < 64) {
    ASSERT_OK(Put(Key(count), RandomString(&rnd, 1024), wo));
    ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
    count++;
    if (dbfull()->TEST_write_controler().IsStopped()) {
      sleeping_task_low.WakeUp();
      break;
    }
  }
  // Stop trigger = 8
  ASSERT_EQ(count, 8);
  // Unblock
  sleeping_task_low.WaitUntilDone();

  // Now reduce level0_stop_writes_trigger to 6. Clear up memtables and L0.
  // Block compaction thread again. Perform the put and memtable flushes
  // until we see the stop after 6 memtable flushes.
  ASSERT_OK(dbfull()->SetOptions({
    {"level0_stop_writes_trigger", "6"}
  }));
  ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);

  // Block compaction again
  sleeping_task_low.Reset();
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  sleeping_task_low.WaitUntilSleeping();
  count = 0;
  while (count < 64) {
    ASSERT_OK(Put(Key(count), RandomString(&rnd, 1024), wo));
    ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
    count++;
    if (dbfull()->TEST_write_controler().IsStopped()) {
      sleeping_task_low.WakeUp();
      break;
    }
  }
  ASSERT_EQ(count, 6);
  // Unblock
  sleeping_task_low.WaitUntilDone();

  // Test disable_auto_compactions
  // Compaction thread is unblocked but auto compaction is disabled. Write
  // 4 L0 files and compaction should be triggered. If auto compaction is
  // disabled, then TEST_WaitForCompact will be waiting for nothing. Number of
  // L0 files do not change after the call.
  ASSERT_OK(dbfull()->SetOptions({
    {"disable_auto_compactions", "true"}
  }));
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);

  for (int i = 0; i < 4; ++i) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 1024)));
    // Wait for compaction so that put won't stop
    ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
  }
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ(NumTableFilesAtLevel(0), 4);

  // Enable auto compaction and perform the same test, # of L0 files should be
  // reduced after compaction.
  ASSERT_OK(dbfull()->SetOptions({
    {"disable_auto_compactions", "false"}
  }));
  ASSERT_OK(dbfull()->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);

  for (int i = 0; i < 4; ++i) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 1024)));
    // Wait for compaction so that put won't stop
    ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
  }
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_LT(NumTableFilesAtLevel(0), 4);
}

TEST_F(DBTest, FileCreationRandomFailure) {
  Options options;
  options.env = env_;
  options.create_if_missing = true;
  options.write_buffer_size = 100000;  // Small write buffer
  options.target_file_size_base = 200000;
  options.max_bytes_for_level_base = 1000000;
  options.max_bytes_for_level_multiplier = 2;

  DestroyAndReopen(options);
  Random rnd(301);

  const int kCDTKeysPerBuffer = 4;
  const int kTestSize = kCDTKeysPerBuffer * 4096;
  const int kTotalIteration = 100;
  // the second half of the test involves in random failure
  // of file creation.
  const int kRandomFailureTest = kTotalIteration / 2;
  std::vector<std::string> values;
  for (int i = 0; i < kTestSize; ++i) {
    values.push_back("NOT_FOUND");
  }
  for (int j = 0; j < kTotalIteration; ++j) {
    if (j == kRandomFailureTest) {
      env_->non_writeable_rate_.store(90);
    }
    for (int k = 0; k < kTestSize; ++k) {
      // here we expect some of the Put fails.
      std::string value = RandomString(&rnd, 100);
      Status s = Put(Key(k), Slice(value));
      if (s.ok()) {
        // update the latest successful put
        values[k] = value;
      }
      // But everything before we simulate the failure-test should succeed.
      if (j < kRandomFailureTest) {
        ASSERT_OK(s);
      }
    }
  }

  // If rocksdb does not do the correct job, internal assert will fail here.
  ASSERT_NOK(dbfull()->TEST_WaitForFlushMemTable());
  ASSERT_NOK(dbfull()->TEST_WaitForCompact());

  // verify we have the latest successful update
  for (int k = 0; k < kTestSize; ++k) {
    auto v = Get(Key(k));
    ASSERT_EQ(v, values[k]);
  }

  // reopen and reverify we have the latest successful update
  env_->non_writeable_rate_.store(0);
  Reopen(options);
  for (int k = 0; k < kTestSize; ++k) {
    auto v = Get(Key(k));
    ASSERT_EQ(v, values[k]);
  }
}

TEST_F(DBTest, DynamicMiscOptions) {
  // Test max_sequential_skip_in_iterations
  Options options;
  options.env = env_;
  options.create_if_missing = true;
  options.max_sequential_skip_in_iterations = 16;
  options.compression = kNoCompression;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  DestroyAndReopen(options);

  auto assert_reseek_count = [this, &options](int key_start, int num_reseek) {
    int key0 = key_start;
    int key1 = key_start + 1;
    int key2 = key_start + 2;
    Random rnd(301);
    ASSERT_OK(Put(Key(key0), RandomString(&rnd, 8)));
    for (int i = 0; i < 10; ++i) {
      ASSERT_OK(Put(Key(key1), RandomString(&rnd, 8)));
    }
    ASSERT_OK(Put(Key(key2), RandomString(&rnd, 8)));
    std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
    iter->Seek(Key(key1));
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Key(key1)), 0);
    iter->Next();
    ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
    ASSERT_EQ(iter->key().compare(Key(key2)), 0);
    ASSERT_EQ(num_reseek,
              TestGetTickerCount(options, NUMBER_OF_RESEEKS_IN_ITERATION));
  };
  // No reseek
  assert_reseek_count(100, 0);

  ASSERT_OK(dbfull()->SetOptions({
    {"max_sequential_skip_in_iterations", "4"}
  }));
  // Clear memtable and make new option effective
  ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
  // Trigger reseek
  assert_reseek_count(200, 1);

  ASSERT_OK(dbfull()->SetOptions({
    {"max_sequential_skip_in_iterations", "16"}
  }));
  // Clear memtable and make new option effective
  ASSERT_OK(dbfull()->TEST_FlushMemTable(true));
  // No reseek
  assert_reseek_count(300, 1);
}

TEST_F(DBTest, L0L1L2AndUpHitCounter) {
  Options options = CurrentOptions();
  options.write_buffer_size = 32 * 1024;
  options.target_file_size_base = 32 * 1024;
  options.level0_file_num_compaction_trigger = 2;
  options.level0_slowdown_writes_trigger = 2;
  options.level0_stop_writes_trigger = 4;
  options.max_bytes_for_level_base = 64 * 1024;
  options.max_write_buffer_number = 2;
  options.max_background_compactions = 8;
  options.max_background_flushes = 8;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  CreateAndReopenWithCF({"mypikachu"}, options);

  int numkeys = 20000;
  for (int i = 0; i < numkeys; i++) {
    ASSERT_OK(Put(1, Key(i), "val"));
  }
  ASSERT_EQ(0, TestGetTickerCount(options, GET_HIT_L0));
  ASSERT_EQ(0, TestGetTickerCount(options, GET_HIT_L1));
  ASSERT_EQ(0, TestGetTickerCount(options, GET_HIT_L2_AND_UP));

  ASSERT_OK(Flush(1));
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  for (int i = 0; i < numkeys; i++) {
    ASSERT_EQ(Get(1, Key(i)), "val");
  }

  ASSERT_GT(TestGetTickerCount(options, GET_HIT_L0), 100);
  ASSERT_GT(TestGetTickerCount(options, GET_HIT_L1), 100);
  ASSERT_GT(TestGetTickerCount(options, GET_HIT_L2_AND_UP), 100);

  ASSERT_EQ(numkeys, TestGetTickerCount(options, GET_HIT_L0) +
                         TestGetTickerCount(options, GET_HIT_L1) +
                         TestGetTickerCount(options, GET_HIT_L2_AND_UP));
}

TEST_F(DBTest, EncodeDecompressedBlockSizeTest) {
  // iter 0 -- zlib
  // iter 1 -- bzip2
  // iter 2 -- lz4
  // iter 3 -- lz4HC
  CompressionType compressions[] = {kZlibCompression, kBZip2Compression,
                                    kLZ4Compression,  kLZ4HCCompression};
  for (int iter = 0; iter < 4; ++iter) {
    if (!CompressionTypeSupported(compressions[iter])) {
      continue;
    }
    // first_table_version 1 -- generate with table_version == 1, read with
    // table_version == 2
    // first_table_version 2 -- generate with table_version == 2, read with
    // table_version == 1
    for (int first_table_version = 1; first_table_version <= 2;
         ++first_table_version) {
      BlockBasedTableOptions table_options;
      table_options.format_version = first_table_version;
      table_options.filter_policy.reset(NewBloomFilterPolicy(10));
      Options options = CurrentOptions();
      options.table_factory.reset(NewBlockBasedTableFactory(table_options));
      options.create_if_missing = true;
      options.compression = compressions[iter];
      DestroyAndReopen(options);

      int kNumKeysWritten = 100000;

      Random rnd(301);
      for (int i = 0; i < kNumKeysWritten; ++i) {
        // compressible string
        ASSERT_OK(Put(Key(i), RandomString(&rnd, 128) + std::string(128, 'a')));
      }

      table_options.format_version = first_table_version == 1 ? 2 : 1;
      options.table_factory.reset(NewBlockBasedTableFactory(table_options));
      Reopen(options);
      for (int i = 0; i < kNumKeysWritten; ++i) {
        auto r = Get(Key(i));
        ASSERT_EQ(r.substr(128), std::string(128, 'a'));
      }
    }
  }
}

TEST_F(DBTest, MutexWaitStatsDisabledByDefault) {
  Options options = CurrentOptions();
  options.create_if_missing = true;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  CreateAndReopenWithCF({"pikachu"}, options);
  const uint64_t kMutexWaitDelay = 100;
  ASSERT_OK(Put("hello", "rocksdb"));
  ASSERT_EQ(TestGetTickerCount(options, DB_MUTEX_WAIT_MICROS), 0);
}

TEST_F(DBTest, CloseSpeedup) {
  Options options = CurrentOptions();
  options.compaction_style = kCompactionStyleLevel;
  options.write_buffer_size = 110 << 10;  // 110KB
  options.arena_block_size = 4 << 10;
  options.level0_file_num_compaction_trigger = 2;
  options.num_levels = 4;
  options.max_bytes_for_level_base = 400 * 1024;
  options.max_write_buffer_number = 16;

  // Block background threads
  env_->SetBackgroundThreads(1, Env::LOW);
  env_->SetBackgroundThreads(1, Env::HIGH);
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  test::SleepingBackgroundTask sleeping_task_high;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask,
                 &sleeping_task_high, Env::Priority::HIGH);

  ASSERT_OK(DeleteRecursively(env_, dbname_));
  DestroyAndReopen(options);

  yb::SyncPoint::GetInstance()->EnableProcessing();
  env_->SetBackgroundThreads(1, Env::LOW);
  env_->SetBackgroundThreads(1, Env::HIGH);
  Random rnd(301);
  int key_idx = 0;

  // First three 110KB files are not going to level 2
  // After that, (100K, 200K)
  for (int num = 0; num < 5; num++) {
    GenerateNewFile(&rnd, &key_idx, true);
  }

  ASSERT_EQ(0, GetSstFileCount(dbname_));

  Close();
  ASSERT_EQ(0, GetSstFileCount(dbname_));

  // Unblock background threads
  sleeping_task_high.WakeUp();
  sleeping_task_high.WaitUntilDone();
  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();

  Destroy(options);
}

class DelayedMergeOperator : public MergeOperator {
 private:
  DBTest* db_test_;

 public:
  explicit DelayedMergeOperator(DBTest* d) : db_test_(d) {}
  virtual bool FullMerge(const Slice& key, const Slice* existing_value,
                         const std::deque<std::string>& operand_list,
                         std::string* new_value,
                         Logger* logger) const override {
    db_test_->env_->addon_time_.fetch_add(1000);
    *new_value = "";
    return true;
  }

  const char* Name() const override { return "DelayedMergeOperator"; }
};

TEST_F(DBTest, MergeTestTime) {
  std::string one, two, three;
  PutFixed64(&one, 1);
  PutFixed64(&two, 2);
  PutFixed64(&three, 3);

  // Enable time profiling
  SetPerfLevel(PerfLevel::kEnableTime);
  this->env_->addon_time_.store(0);
  this->env_->time_elapse_only_sleep_ = true;
  this->env_->no_sleep_ = true;
  Options options;
  options = CurrentOptions(options);
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.merge_operator.reset(new DelayedMergeOperator(this));
  DestroyAndReopen(options);

  ASSERT_EQ(TestGetTickerCount(options, MERGE_OPERATION_TOTAL_TIME), 0);
  ASSERT_OK(db_->Put(WriteOptions(), "foo", one));
  ASSERT_OK(Flush());
  ASSERT_OK(db_->Merge(WriteOptions(), "foo", two));
  ASSERT_OK(Flush());
  ASSERT_OK(db_->Merge(WriteOptions(), "foo", three));
  ASSERT_OK(Flush());

  ReadOptions opt;
  opt.verify_checksums = true;
  opt.snapshot = nullptr;
  std::string result;
  ASSERT_OK(db_->Get(opt, "foo", &result));

  ASSERT_EQ(1000000, TestGetTickerCount(options, MERGE_OPERATION_TOTAL_TIME));

  ReadOptions read_options;
  std::unique_ptr<Iterator> iter(db_->NewIterator(read_options));
  int count = 0;
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    ++count;
  }

  ASSERT_EQ(1, count);
  ASSERT_EQ(2000000, TestGetTickerCount(options, MERGE_OPERATION_TOTAL_TIME));
  this->env_->time_elapse_only_sleep_ = false;
}

TEST_P(DBTestWithParam, MergeCompactionTimeTest) {
  SetPerfLevel(PerfLevel::kEnableTime);
  Options options;
  options = CurrentOptions(options);
  options.compaction_filter_factory = std::make_shared<KeepFilterFactory>();
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.merge_operator.reset(new DelayedMergeOperator(this));
  options.compaction_style = kCompactionStyleUniversal;
  options.max_subcompactions = max_subcompactions_;
  DestroyAndReopen(options);

  for (int i = 0; i < 1000; i++) {
    ASSERT_OK(db_->Merge(WriteOptions(), "foo", "TEST"));
    ASSERT_OK(Flush());
  }
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  ASSERT_NE(TestGetTickerCount(options, MERGE_OPERATION_TOTAL_TIME), 0);
}

TEST_P(DBTestWithParam, FilterCompactionTimeTest) {
  Options options;
  options.compaction_filter_factory =
      std::make_shared<DelayFilterFactory>(this);
  options.disable_auto_compactions = true;
  options.create_if_missing = true;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.max_subcompactions = max_subcompactions_;
  options = CurrentOptions(options);
  DestroyAndReopen(options);

  // put some data
  for (int table = 0; table < 4; ++table) {
    for (int i = 0; i < 10 + table; ++i) {
      ASSERT_OK(Put(ToString(table * 100 + i), "val"));
    }
    ASSERT_OK(Flush());
  }

  CompactRangeOptions cro;
  cro.exclusive_manual_compaction = exclusive_manual_compaction_;
  ASSERT_OK(db_->CompactRange(cro, nullptr, nullptr));
  ASSERT_EQ(0U, CountLiveFiles());

  Reopen(options);

  Iterator* itr = db_->NewIterator(ReadOptions());
  itr->SeekToFirst();
  ASSERT_FALSE(ASSERT_RESULT(itr->CheckedValid()));
  // Stopwatch has been removed from compaction iterator. Disable assert below.
  // ASSERT_NE(TestGetTickerCount(options, FILTER_OPERATION_TOTAL_TIME), 0);
  delete itr;
}

TEST_F(DBTest, TestLogCleanup) {
  Options options = CurrentOptions();
  options.write_buffer_size = 64 * 1024;  // very small
  // only two memtables allowed ==> only two log files
  options.max_write_buffer_number = 2;
  Reopen(options);

  for (int i = 0; i < 100000; ++i) {
    ASSERT_OK(Put(Key(i), "val"));
    // only 2 memtables will be alive, so logs_to_free needs to always be below
    // 2
    ASSERT_LT(dbfull()->TEST_LogsToFreeSize(), static_cast<size_t>(3));
  }
}

TEST_F(DBTest, EmptyCompactedDB) {
  Options options;
  options.max_open_files = -1;
  options = CurrentOptions(options);
  Close();
  ASSERT_OK(ReadOnlyReopen(options));
  Status s = Put("new", "value");
  ASSERT_TRUE(s.IsNotSupported());
  Close();
}

TEST_F(DBTest, SuggestCompactRangeTest) {
  class CompactionFilterFactoryGetContext : public CompactionFilterFactory {
   public:
    virtual std::unique_ptr<CompactionFilter> CreateCompactionFilter(
        const CompactionFilter::Context& context) override {
      saved_context = context;
      std::unique_ptr<CompactionFilter> empty_filter;
      return empty_filter;
    }
    const char* Name() const override {
      return "CompactionFilterFactoryGetContext";
    }
    static bool IsManual(CompactionFilterFactory* compaction_filter_factory) {
      return reinterpret_cast<CompactionFilterFactoryGetContext*>(
                 compaction_filter_factory)->saved_context.is_manual_compaction;
    }
    CompactionFilter::Context saved_context;
  };

  Options options = CurrentOptions();
  options.memtable_factory.reset(
      new SpecialSkipListFactory(DBTestBase::kNumKeysByGenerateNewRandomFile));
  options.compaction_style = kCompactionStyleLevel;
  options.compaction_filter_factory.reset(
      new CompactionFilterFactoryGetContext());
  options.write_buffer_size = 200 << 10;
  options.arena_block_size = 4 << 10;
  options.level0_file_num_compaction_trigger = 4;
  options.num_levels = 4;
  options.compression = kNoCompression;
  options.max_bytes_for_level_base = 450 << 10;
  options.target_file_size_base = 98 << 10;
  options.max_grandparent_overlap_factor = 1 << 20;  // inf

  Reopen(options);

  Random rnd(301);

  for (int num = 0; num < 3; num++) {
    GenerateNewRandomFile(&rnd);
  }

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("0,4", FilesPerLevel(0));
  ASSERT_TRUE(!CompactionFilterFactoryGetContext::IsManual(
                   options.compaction_filter_factory.get()));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("1,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("2,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("3,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("0,4,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("1,4,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("2,4,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("3,4,4", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("0,4,8", FilesPerLevel(0));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ("1,4,8", FilesPerLevel(0));

  // compact it three times
  for (int i = 0; i < 3; ++i) {
    ASSERT_OK(experimental::SuggestCompactRange(db_, nullptr, nullptr));
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
  }

  // All files are compacted
  ASSERT_EQ(0, NumTableFilesAtLevel(0));
  ASSERT_EQ(0, NumTableFilesAtLevel(1));

  GenerateNewRandomFile(&rnd);
  ASSERT_EQ(1, NumTableFilesAtLevel(0));

  // nonoverlapping with the file on level 0
  Slice start("a"), end("b");
  ASSERT_OK(experimental::SuggestCompactRange(db_, &start, &end));
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  // should not compact the level 0 file
  ASSERT_EQ(1, NumTableFilesAtLevel(0));

  start = Slice("j");
  end = Slice("m");
  ASSERT_OK(experimental::SuggestCompactRange(db_, &start, &end));
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_TRUE(CompactionFilterFactoryGetContext::IsManual(
      options.compaction_filter_factory.get()));

  // now it should compact the level 0 file
  ASSERT_EQ(0, NumTableFilesAtLevel(0));
  ASSERT_EQ(1, NumTableFilesAtLevel(1));
}

TEST_F(DBTest, PromoteL0) {
  Options options = CurrentOptions();
  options.disable_auto_compactions = true;
  options.write_buffer_size = 10 * 1024 * 1024;
  DestroyAndReopen(options);

  // non overlapping ranges
  std::vector<std::pair<int32_t, int32_t>> ranges = {
      {81, 160}, {0, 80}, {161, 240}, {241, 320}};

  int32_t value_size = 10 * 1024;  // 10 KB

  Random rnd(301);
  std::map<int32_t, std::string> values;
  for (const auto& range : ranges) {
    for (int32_t j = range.first; j < range.second; j++) {
      values[j] = RandomString(&rnd, value_size);
      ASSERT_OK(Put(Key(j), values[j]));
    }
    ASSERT_OK(Flush());
  }

  int32_t level0_files = NumTableFilesAtLevel(0, 0);
  ASSERT_EQ(level0_files, ranges.size());
  ASSERT_EQ(NumTableFilesAtLevel(1, 0), 0);  // No files in L1

  // Promote L0 level to L2.
  ASSERT_OK(experimental::PromoteL0(db_, db_->DefaultColumnFamily(), 2));
  // We expect that all the files were trivially moved from L0 to L2
  ASSERT_EQ(NumTableFilesAtLevel(0, 0), 0);
  ASSERT_EQ(NumTableFilesAtLevel(2, 0), level0_files);

  for (const auto& kv : values) {
    ASSERT_EQ(Get(Key(kv.first)), kv.second);
  }
}

TEST_F(DBTest, PromoteL0Failure) {
  Options options = CurrentOptions();
  options.disable_auto_compactions = true;
  options.write_buffer_size = 10 * 1024 * 1024;
  DestroyAndReopen(options);

  // Produce two L0 files with overlapping ranges.
  ASSERT_OK(Put(Key(0), ""));
  ASSERT_OK(Put(Key(3), ""));
  ASSERT_OK(Flush());
  ASSERT_OK(Put(Key(1), ""));
  ASSERT_OK(Flush());

  Status status;
  // Fails because L0 has overlapping files.
  status = experimental::PromoteL0(db_, db_->DefaultColumnFamily());
  ASSERT_TRUE(status.IsInvalidArgument());

  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  // Now there is a file in L1.
  ASSERT_GE(NumTableFilesAtLevel(1, 0), 1);

  ASSERT_OK(Put(Key(5), ""));
  ASSERT_OK(Flush());
  // Fails because L1 is non-empty.
  status = experimental::PromoteL0(db_, db_->DefaultColumnFamily());
  ASSERT_TRUE(status.IsInvalidArgument());
}

// Github issue #596
TEST_F(DBTest, HugeNumberOfLevels) {
  Options options = CurrentOptions();
  options.write_buffer_size = 2 * 1024 * 1024;         // 2MB
  options.max_bytes_for_level_base = 2 * 1024 * 1024;  // 2MB
  options.num_levels = 12;
  options.max_background_compactions = 10;
  options.max_bytes_for_level_multiplier = 2;
  options.level_compaction_dynamic_level_bytes = true;
  DestroyAndReopen(options);

  Random rnd(301);
  for (int i = 0; i < 300000; ++i) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 1024)));
  }

  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
}

TEST_F(DBTest, AutomaticConflictsWithManualCompaction) {
  Options options = CurrentOptions();
  options.write_buffer_size = 2 * 1024 * 1024;         // 2MB
  options.max_bytes_for_level_base = 2 * 1024 * 1024;  // 2MB
  options.num_levels = 12;
  options.max_background_compactions = 10;
  options.max_bytes_for_level_multiplier = 2;
  options.level_compaction_dynamic_level_bytes = true;
  DestroyAndReopen(options);

  Random rnd(301);
  for (int i = 0; i < 300000; ++i) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 1024)));
  }

  std::atomic<int> callback_count(0);
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::RunManualCompaction()::Conflict",
      [&](void* arg) { callback_count.fetch_add(1); });
  yb::SyncPoint::GetInstance()->EnableProcessing();
  CompactRangeOptions croptions;
  croptions.exclusive_manual_compaction = false;
  ASSERT_OK(db_->CompactRange(croptions, nullptr, nullptr));
  ASSERT_GE(callback_count.load(), 1);
  yb::SyncPoint::GetInstance()->DisableProcessing();
  for (int i = 0; i < 300000; ++i) {
    ASSERT_NE("NOT_FOUND", Get(Key(i)));
  }
}

// Github issue #595
// Large write batch with column families
TEST_F(DBTest, LargeBatchWithColumnFamilies) {
  Options options;
  options.env = env_;
  options = CurrentOptions(options);
  options.write_buffer_size = 100000;  // Small write buffer
  CreateAndReopenWithCF({"pikachu"}, options);
  int64_t j = 0;
  for (int i = 0; i < 5; i++) {
    for (int pass = 1; pass <= 3; pass++) {
      WriteBatch batch;
      size_t write_size = 1024 * 1024 * (5 + i);
      fprintf(stderr, "prepare: %" ROCKSDB_PRIszt " MB, pass:%d\n", (write_size / 1024 / 1024),
              pass);
      for (;;) {
        std::string data(3000, j++ % 127 + 20);
        data += ToString(j);
        batch.Put(handles_[0], Slice(data), Slice(data));
        if (batch.GetDataSize() > write_size) {
          break;
        }
      }
      fprintf(stderr, "write: %" ROCKSDB_PRIszt " MB\n", (batch.GetDataSize() / 1024 / 1024));
      ASSERT_OK(dbfull()->Write(WriteOptions(), &batch));
      fprintf(stderr, "done\n");
    }
  }
  // make sure we can re-open it.
  ASSERT_OK(TryReopenWithColumnFamilies({"default", "pikachu"}, options));
}

// Make sure that Flushes can proceed in parallel with CompactRange()
TEST_F(DBTest, FlushesInParallelWithCompactRange) {
  // iter == 0 -- leveled
  // iter == 1 -- leveled, but throw in a flush between two levels compacting
  // iter == 2 -- universal
  for (int iter = 0; iter < 3; ++iter) {
    Options options = CurrentOptions();
    if (iter < 2) {
      options.compaction_style = kCompactionStyleLevel;
    } else {
      options.compaction_style = kCompactionStyleUniversal;
    }
    options.write_buffer_size = 110 << 10;
    options.level0_file_num_compaction_trigger = 4;
    options.num_levels = 4;
    options.compression = kNoCompression;
    options.max_bytes_for_level_base = 450 << 10;
    options.target_file_size_base = 98 << 10;
    options.max_write_buffer_number = 2;

    DestroyAndReopen(options);

    Random rnd(301);
    for (int num = 0; num < 14; num++) {
      GenerateNewRandomFile(&rnd);
    }

    if (iter == 1) {
    yb::SyncPoint::GetInstance()->LoadDependency(
        {{"DBImpl::RunManualCompaction()::1",
          "DBTest::FlushesInParallelWithCompactRange:1"},
         {"DBTest::FlushesInParallelWithCompactRange:2",
          "DBImpl::RunManualCompaction()::2"}});
    } else {
      yb::SyncPoint::GetInstance()->LoadDependency(
          {{"CompactionJob::Run():Start",
            "DBTest::FlushesInParallelWithCompactRange:1"},
           {"DBTest::FlushesInParallelWithCompactRange:2",
            "CompactionJob::Run():End"}});
    }
    yb::SyncPoint::GetInstance()->EnableProcessing();

    std::vector<std::thread> threads;
    threads.emplace_back([&]() { Compact("a", "z"); });

    TEST_SYNC_POINT("DBTest::FlushesInParallelWithCompactRange:1");

    // this has to start a flush. if flushes are blocked, this will try to
    // create
    // 3 memtables, and that will fail because max_write_buffer_number is 2
    for (int num = 0; num < 3; num++) {
      GenerateNewRandomFile(&rnd, /* nowait */ true);
    }

    TEST_SYNC_POINT("DBTest::FlushesInParallelWithCompactRange:2");

    for (auto& t : threads) {
      t.join();
    }
    yb::SyncPoint::GetInstance()->DisableProcessing();
  }
}

TEST_F(DBTest, DelayedWriteRate) {
  const int kEntriesPerMemTable = 100;
  const int kTotalFlushes = 20;

  Options options;
  env_->SetBackgroundThreads(1, Env::LOW);
  options.env = env_;
  env_->no_sleep_ = true;
  options = CurrentOptions(options);
  options.write_buffer_size = 100000000;
  options.max_write_buffer_number = 256;
  options.max_background_compactions = 1;
  options.level0_file_num_compaction_trigger = 3;
  options.level0_slowdown_writes_trigger = 3;
  options.level0_stop_writes_trigger = 999999;
  options.delayed_write_rate = 20000000;  // Start with 200MB/s
  options.memtable_factory.reset(
      new SpecialSkipListFactory(kEntriesPerMemTable));

  CreateAndReopenWithCF({"pikachu"}, options);

  // Block compactions
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);

  for (int i = 0; i < 3; i++) {
    ASSERT_OK(Put(Key(i), std::string(10000, 'x')));
    ASSERT_OK(Flush());
  }

  // These writes will be slowed down to 1KB/s
  uint64_t estimated_sleep_time = 0;
  Random rnd(301);
  ASSERT_OK(Put("", ""));
  uint64_t cur_rate = options.delayed_write_rate;
  for (int i = 0; i < kTotalFlushes; i++) {
    uint64_t size_memtable = 0;
    for (int j = 0; j < kEntriesPerMemTable; j++) {
      auto rand_num = rnd.Uniform(20);
      // Spread the size range to more.
      size_t entry_size = rand_num * rand_num * rand_num;
      WriteOptions wo;
      ASSERT_OK(Put(Key(i), std::string(entry_size, 'x'), wo));
      size_memtable += entry_size + 18;
      // Occasionally sleep a while
      if (rnd.Uniform(20) == 6) {
        env_->SleepForMicroseconds(2666);
      }
    }
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
    estimated_sleep_time += size_memtable * 1000000u / cur_rate;
    // Slow down twice. One for memtable switch and one for flush finishes.
    cur_rate = static_cast<uint64_t>(static_cast<double>(cur_rate) /
                                     kSlowdownRatio / kSlowdownRatio);
  }
  // Estimate the total sleep time fall into the rough range.
  ASSERT_GT(env_->addon_time_.load(),
            static_cast<int64_t>(estimated_sleep_time / 2));
  ASSERT_LT(env_->addon_time_.load(),
            static_cast<int64_t>(estimated_sleep_time * 2));

  env_->no_sleep_ = false;
  yb::SyncPoint::GetInstance()->DisableProcessing();
  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();
}

TEST_F(DBTest, HardLimit) {
  Options options;
  options.env = env_;
  env_->SetBackgroundThreads(1, Env::LOW);
  options = CurrentOptions(options);
  options.max_write_buffer_number = 256;
  options.write_buffer_size = 110 << 10;  // 110KB
  options.arena_block_size = 4 * 1024;
  options.level0_file_num_compaction_trigger = 4;
  options.level0_slowdown_writes_trigger = 999999;
  options.level0_stop_writes_trigger = 999999;
  options.hard_pending_compaction_bytes_limit = 800 << 10;
  options.max_bytes_for_level_base = 10000000000u;
  options.max_background_compactions = 1;
  options.memtable_factory.reset(
      new SpecialSkipListFactory(KNumKeysByGenerateNewFile - 1));

  env_->SetBackgroundThreads(1, Env::LOW);
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);

  CreateAndReopenWithCF({"pikachu"}, options);

  std::atomic<int> callback_count(0);
  yb::SyncPoint::GetInstance()->SetCallBack("DBImpl::DelayWrite:Wait",
                                                 [&](void* arg) {
                                                   callback_count.fetch_add(1);
                                                   sleeping_task_low.WakeUp();
                                                 });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  Random rnd(301);
  int key_idx = 0;
  for (int num = 0; num < 5; num++) {
    GenerateNewFile(&rnd, &key_idx, true);
  }

  ASSERT_EQ(0, callback_count.load());

  for (int num = 0; num < 5; num++) {
    GenerateNewFile(&rnd, &key_idx, true);
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  }
  ASSERT_GE(callback_count.load(), 1);

  yb::SyncPoint::GetInstance()->DisableProcessing();
  sleeping_task_low.WaitUntilDone();
}

TEST_F(DBTest, SoftLimit) {
  Options options;
  options.env = env_;
  options = CurrentOptions(options);
  options.write_buffer_size = 100000;  // Small write buffer
  options.max_write_buffer_number = 256;
  options.level0_file_num_compaction_trigger = 1;
  options.level0_slowdown_writes_trigger = 3;
  options.level0_stop_writes_trigger = 999999;
  options.delayed_write_rate = 20000;  // About 200KB/s limited rate
  options.soft_pending_compaction_bytes_limit = 200000;
  options.target_file_size_base = 99999999;  // All into one file
  options.max_bytes_for_level_base = 50000;
  options.max_bytes_for_level_multiplier = 10;
  options.max_background_compactions = 1;
  options.compression = kNoCompression;

  Reopen(options);
  ASSERT_OK(Put(Key(0), ""));

  test::SleepingBackgroundTask sleeping_task_low;
  // Block compactions
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  sleeping_task_low.WaitUntilSleeping();

  // Create 3 L0 files, making score of L0 to be 3.
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(Put(Key(i), std::string(5000, 'x')));
    ASSERT_OK(Put(Key(100 - i), std::string(5000, 'x')));
    // Flush the file. File size is around 30KB.
    ASSERT_OK(Flush());
  }
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());

  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();
  sleeping_task_low.Reset();
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  // Now there is one L1 file but doesn't trigger soft_rate_limit
  // The L1 file size is around 30KB.

  ASSERT_EQ(NumTableFilesAtLevel(1), 1);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  // Only allow one compactin going through.
  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl:BackgroundCompaction:SmallCompaction", [&](void* arg) {
        // Schedule a sleeping task.
        sleeping_task_low.Reset();
        env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask,
                       &sleeping_task_low, Env::Priority::LOW);
      });

  yb::SyncPoint::GetInstance()->EnableProcessing();

  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  sleeping_task_low.WaitUntilSleeping();
  // Create 3 L0 files, making score of L0 to be 3
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(Put(Key(10 + i), std::string(5000, 'x')));
    ASSERT_OK(Put(Key(90 - i), std::string(5000, 'x')));
    // Flush the file. File size is around 30KB.
    ASSERT_OK(Flush());
  }

  // (Twice) Wake up sleep task to enable compaction to run and waits
  // for it to go to sleep state again to make sure one compaction
  // goes through.
  for (int i = 0; i < 2; i++) {
    sleeping_task_low.WakeUp();
    sleeping_task_low.WaitUntilSleeping();
  }

  // Now there is one L1 file (around 60KB) which exceeds 50KB base by 10KB
  // Given level multiplier 10, estimated pending compaction is around 100KB
  // doesn't trigger soft_pending_compaction_bytes_limit. Another compaction
  // promoting the L1 file to L2 is unscheduled.
  ASSERT_EQ(NumTableFilesAtLevel(1), 1);
  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  // Create 3 L0 files, making score of L0 to be 3, higher than L0.
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(Put(Key(20 + i), std::string(5000, 'x')));
    ASSERT_OK(Put(Key(80 - i), std::string(5000, 'x')));
    // Flush the file. File size is around 30KB.
    ASSERT_OK(Flush());
  }
  // Wake up sleep task to enable compaction to run and waits
  // for it to go to sleep state again to make sure one compaction
  // goes through.

  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilSleeping();

  // Now there is one L2 file (around 60KB) which doesn't trigger
  // soft_pending_compaction_bytes_limit but the 3 L0 files do get the delay token
  ASSERT_EQ(NumTableFilesAtLevel(2), 1);
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());

  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilSleeping();

  ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());

  // shrink level base so L2 will hit soft limit easier.
  ASSERT_OK(dbfull()->SetOptions({
      {"max_bytes_for_level_base", "5000"},
  }));

  ASSERT_OK(Put("", ""));
  ASSERT_OK(Flush());
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());

  sleeping_task_low.WaitUntilSleeping();
  yb::SyncPoint::GetInstance()->DisableProcessing();
  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();
}

TEST_F(DBTest, LastWriteBufferDelay) {
  Options options;
  options.env = env_;
  options = CurrentOptions(options);
  options.write_buffer_size = 100000;
  options.max_write_buffer_number = 4;
  options.delayed_write_rate = 20000;
  options.compression = kNoCompression;
  options.disable_auto_compactions = true;
  int kNumKeysPerMemtable = 3;
  options.memtable_factory.reset(
      new SpecialSkipListFactory(kNumKeysPerMemtable));

  Reopen(options);
  test::SleepingBackgroundTask sleeping_task;
  // Block flushes
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task,
                 Env::Priority::HIGH);
  sleeping_task.WaitUntilSleeping();

  // Create 3 L0 files, making score of L0 to be 3.
  for (int i = 0; i < 3; i++) {
    // Fill one mem table
    for (int j = 0; j < kNumKeysPerMemtable; j++) {
      ASSERT_OK(Put(Key(j), ""));
    }
    ASSERT_TRUE(!dbfull()->TEST_write_controler().NeedsDelay());
  }
  // Inserting a new entry would create a new mem table, triggering slow down.
  ASSERT_OK(Put(Key(0), ""));
  ASSERT_TRUE(dbfull()->TEST_write_controler().NeedsDelay());

  sleeping_task.WakeUp();
  sleeping_task.WaitUntilDone();
}

TEST_F(DBTest, FailWhenCompressionNotSupportedTest) {
  CompressionType compressions[] = {kZlibCompression, kBZip2Compression,
                                    kLZ4Compression,  kLZ4HCCompression};
  for (int iter = 0; iter < 4; ++iter) {
    if (!CompressionTypeSupported(compressions[iter])) {
      // not supported, we should fail the Open()
      Options options = CurrentOptions();
      options.compression = compressions[iter];
      ASSERT_TRUE(!TryReopen(options).ok());
      // Try if CreateColumnFamily also fails
      options.compression = kNoCompression;
      ASSERT_OK(TryReopen(options));
      ColumnFamilyOptions cf_options(options);
      cf_options.compression = compressions[iter];
      ColumnFamilyHandle* handle;
      ASSERT_TRUE(!db_->CreateColumnFamily(cf_options, "name", &handle).ok());
    }
  }
}

TEST_F(DBTest, RowCache) {
  Options options = CurrentOptions();
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.row_cache = NewLRUCache(8192);
  DestroyAndReopen(options);

  ASSERT_OK(Put("foo", "bar"));
  ASSERT_OK(Flush());

  ASSERT_EQ(TestGetTickerCount(options, ROW_CACHE_HIT), 0);
  ASSERT_EQ(TestGetTickerCount(options, ROW_CACHE_MISS), 0);
  ASSERT_EQ(Get("foo"), "bar");
  ASSERT_EQ(TestGetTickerCount(options, ROW_CACHE_HIT), 0);
  ASSERT_EQ(TestGetTickerCount(options, ROW_CACHE_MISS), 1);
  ASSERT_EQ(Get("foo"), "bar");
  ASSERT_EQ(TestGetTickerCount(options, ROW_CACHE_HIT), 1);
  ASSERT_EQ(TestGetTickerCount(options, ROW_CACHE_MISS), 1);
}

// TODO(3.13): fix the issue of Seek() + Prev() which might not necessary
//             return the biggest key which is smaller than the seek key.
TEST_F(DBTest, PrevAfterMerge) {
  Options options;
  options.create_if_missing = true;
  options.merge_operator = MergeOperators::CreatePutOperator();
  DestroyAndReopen(options);

  // write three entries with different keys using Merge()
  WriteOptions wopts;
  ASSERT_OK(db_->Merge(wopts, "1", "data1"));
  ASSERT_OK(db_->Merge(wopts, "2", "data2"));
  ASSERT_OK(db_->Merge(wopts, "3", "data3"));

  std::unique_ptr<Iterator> it(db_->NewIterator(ReadOptions()));

  it->Seek("2");
  ASSERT_TRUE(ASSERT_RESULT(it->CheckedValid()));
  ASSERT_EQ("2", it->key().ToString());

  it->Prev();
  ASSERT_TRUE(ASSERT_RESULT(it->CheckedValid()));
  ASSERT_EQ("1", it->key().ToString());
}

TEST_F(DBTest, DeletingOldWalAfterDrop) {
  yb::SyncPoint::GetInstance()->LoadDependency(
      { { "Test:AllowFlushes", "DBImpl::BGWorkFlush" },
        { "DBImpl::BGWorkFlush:done", "Test:WaitForFlush"} });
  yb::SyncPoint::GetInstance()->ClearTrace();

  yb::SyncPoint::GetInstance()->DisableProcessing();
  Options options = CurrentOptions();
  options.max_total_wal_size = 8192;
  options.compression = kNoCompression;
  options.write_buffer_size = 1 << 20;
  options.level0_file_num_compaction_trigger = (1<<30);
  options.level0_slowdown_writes_trigger = (1<<30);
  options.level0_stop_writes_trigger = (1<<30);
  options.disable_auto_compactions = true;
  DestroyAndReopen(options);
  yb::SyncPoint::GetInstance()->EnableProcessing();

  CreateColumnFamilies({"cf1", "cf2"}, options);
  ASSERT_OK(Put(0, "key1", DummyString(8192)));
  ASSERT_OK(Put(0, "key2", DummyString(8192)));
  // the oldest wal should now be getting_flushed
  ASSERT_OK(db_->DropColumnFamily(handles_[0]));
  // all flushes should now do nothing because their CF is dropped
  TEST_SYNC_POINT("Test:AllowFlushes");
  TEST_SYNC_POINT("Test:WaitForFlush");
  uint64_t lognum1 = dbfull()->TEST_LogfileNumber();
  ASSERT_OK(Put(1, "key3", DummyString(8192)));
  ASSERT_OK(Put(1, "key4", DummyString(8192)));
  // new wal should have been created
  uint64_t lognum2 = dbfull()->TEST_LogfileNumber();
  EXPECT_GT(lognum2, lognum1);
}

TEST_F(DBTest, UnsupportedManualSync) {
  DestroyAndReopen(CurrentOptions());
  env_->is_wal_sync_thread_safe_.store(false);
  Status s = db_->SyncWAL();
  ASSERT_TRUE(s.IsNotSupported());
}


TEST_F(DBTest, AddExternalSstFile) {
  do {
    std::string sst_files_folder = test::TmpDir(env_) + "/sst_files/";
    ASSERT_OK(env_->CreateDirIfMissing(sst_files_folder));
    Options options = CurrentOptions();
    options.env = env_;
    const ImmutableCFOptions ioptions(options);

    SstFileWriter sst_file_writer(EnvOptions(), ioptions, options.comparator);

    // file1.sst (0 => 99)
    std::string file1 = sst_files_folder + "file1.sst";
    ASSERT_OK(sst_file_writer.Open(file1));
    for (int k = 0; k < 100; k++) {
      ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val"));
    }
    ExternalSstFileInfo file1_info;
    Status s = sst_file_writer.Finish(&file1_info);
    ASSERT_TRUE(s.ok()) << s.ToString();
    ASSERT_EQ(file1_info.file_path, file1);
    ASSERT_EQ(file1_info.num_entries, 100);
    ASSERT_EQ(file1_info.smallest_key, Key(0));
    ASSERT_EQ(file1_info.largest_key, Key(99));
    // sst_file_writer already finished, cannot add this value
    s = sst_file_writer.Add(Key(100), "bad_val");
    ASSERT_FALSE(s.ok()) << s.ToString();

    // file2.sst (100 => 199)
    std::string file2 = sst_files_folder + "file2.sst";
    ASSERT_OK(sst_file_writer.Open(file2));
    for (int k = 100; k < 200; k++) {
      ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val"));
    }
    // Cannot add this key because it's not after last added key
    s = sst_file_writer.Add(Key(99), "bad_val");
    ASSERT_FALSE(s.ok()) << s.ToString();
    ExternalSstFileInfo file2_info;
    s = sst_file_writer.Finish(&file2_info);
    ASSERT_TRUE(s.ok()) << s.ToString();
    ASSERT_EQ(file2_info.file_path, file2);
    ASSERT_EQ(file2_info.num_entries, 100);
    ASSERT_EQ(file2_info.smallest_key, Key(100));
    ASSERT_EQ(file2_info.largest_key, Key(199));

    // file3.sst (195 => 299)
    // This file values overlap with file2 values
    std::string file3 = sst_files_folder + "file3.sst";
    ASSERT_OK(sst_file_writer.Open(file3));
    for (int k = 195; k < 300; k++) {
      ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val_overlap"));
    }
    ExternalSstFileInfo file3_info;
    s = sst_file_writer.Finish(&file3_info);
    ASSERT_TRUE(s.ok()) << s.ToString();
    ASSERT_EQ(file3_info.file_path, file3);
    ASSERT_EQ(file3_info.num_entries, 105);
    ASSERT_EQ(file3_info.smallest_key, Key(195));
    ASSERT_EQ(file3_info.largest_key, Key(299));

    // file4.sst (30 => 39)
    // This file values overlap with file1 values
    std::string file4 = sst_files_folder + "file4.sst";
    ASSERT_OK(sst_file_writer.Open(file4));
    for (int k = 30; k < 40; k++) {
      ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val_overlap"));
    }
    ExternalSstFileInfo file4_info;
    s = sst_file_writer.Finish(&file4_info);
    ASSERT_TRUE(s.ok()) << s.ToString();
    ASSERT_EQ(file4_info.file_path, file4);
    ASSERT_EQ(file4_info.num_entries, 10);
    ASSERT_EQ(file4_info.smallest_key, Key(30));
    ASSERT_EQ(file4_info.largest_key, Key(39));

    // file5.sst (400 => 499)
    std::string file5 = sst_files_folder + "file5.sst";
    ASSERT_OK(sst_file_writer.Open(file5));
    for (int k = 400; k < 500; k++) {
      ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val"));
    }
    ExternalSstFileInfo file5_info;
    s = sst_file_writer.Finish(&file5_info);
    ASSERT_TRUE(s.ok()) << s.ToString();
    ASSERT_EQ(file5_info.file_path, file5);
    ASSERT_EQ(file5_info.num_entries, 100);
    ASSERT_EQ(file5_info.smallest_key, Key(400));
    ASSERT_EQ(file5_info.largest_key, Key(499));

    // Cannot create an empty sst file
    std::string file_empty = sst_files_folder + "file_empty.sst";
    ExternalSstFileInfo file_empty_info;
    s = sst_file_writer.Finish(&file_empty_info);
    ASSERT_NOK(s);

    DestroyAndReopen(options);
    // Add file using file path
    s = db_->AddFile(file1);
    ASSERT_TRUE(s.ok()) << s.ToString();
    ASSERT_EQ(db_->GetLatestSequenceNumber(), 0U);
    for (int k = 0; k < 100; k++) {
      ASSERT_EQ(Get(Key(k)), Key(k) + "_val");
    }

    // Add file while holding a snapshot will fail
    const Snapshot* s1 = db_->GetSnapshot();
    if (s1 != nullptr) {
      ASSERT_NOK(db_->AddFile(&file2_info));
      db_->ReleaseSnapshot(s1);
    }
    // We can add the file after releaseing the snapshot
    ASSERT_OK(db_->AddFile(&file2_info));

    ASSERT_EQ(db_->GetLatestSequenceNumber(), 0U);
    for (int k = 0; k < 200; k++) {
      ASSERT_EQ(Get(Key(k)), Key(k) + "_val");
    }

    // This file have overlapping values with the exisitng data
    s = db_->AddFile(file3);
    ASSERT_FALSE(s.ok()) << s.ToString();

    // This file have overlapping values with the exisitng data
    s = db_->AddFile(&file4_info);
    ASSERT_FALSE(s.ok()) << s.ToString();

    // Overwrite values of keys divisible by 5
    for (int k = 0; k < 200; k += 5) {
      ASSERT_OK(Put(Key(k), Key(k) + "_val_new"));
    }
    ASSERT_NE(db_->GetLatestSequenceNumber(), 0U);

    // Key range of file5 (400 => 499) dont overlap with any keys in DB
    ASSERT_OK(db_->AddFile(file5));

    // Make sure values are correct before and after flush/compaction
    for (int i = 0; i < 2; i++) {
      for (int k = 0; k < 200; k++) {
        std::string value = Key(k) + "_val";
        if (k % 5 == 0) {
          value += "_new";
        }
        ASSERT_EQ(Get(Key(k)), value);
      }
      for (int k = 400; k < 500; k++) {
        std::string value = Key(k) + "_val";
        ASSERT_EQ(Get(Key(k)), value);
      }
      ASSERT_OK(Flush());
      ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    }

    Close();
    options.disable_auto_compactions = true;
    Reopen(options);

    // Delete keys in range (400 => 499)
    for (int k = 400; k < 500; k++) {
      ASSERT_OK(Delete(Key(k)));
    }
    // We deleted range (400 => 499) but cannot add file5 because
    // of the range tombstones
    ASSERT_NOK(db_->AddFile(file5));

    // Compacting the DB will remove the tombstones
    ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));

    // Now we can add the file
    ASSERT_OK(db_->AddFile(file5));

    // Verify values of file5 in DB
    for (int k = 400; k < 500; k++) {
      std::string value = Key(k) + "_val";
      ASSERT_EQ(Get(Key(k)), value);
    }
  } while (ChangeOptions(kSkipPlainTable | kSkipUniversalCompaction |
                         kSkipFIFOCompaction));
}

// This test reporduce a bug that can happen in some cases if the DB started
// purging obsolete files when we are adding an external sst file.
// This situation may result in deleting the file while it's being added.
TEST_F(DBTest, AddExternalSstFilePurgeObsoleteFilesBug) {
  std::string sst_files_folder = test::TmpDir(env_) + "/sst_files/";
  ASSERT_OK(env_->CreateDir(sst_files_folder));
  Options options = CurrentOptions();
  options.env = env_;
  const ImmutableCFOptions ioptions(options);

  SstFileWriter sst_file_writer(EnvOptions(), ioptions, options.comparator);

  // file1.sst (0 => 500)
  std::string sst_file_path = sst_files_folder + "file1.sst";
  Status s = sst_file_writer.Open(sst_file_path);
  ASSERT_OK(s);
  for (int i = 0; i < 500; i++) {
    std::string k = Key(i);
    s = sst_file_writer.Add(k, k + "_val");
    ASSERT_OK(s);
  }

  ExternalSstFileInfo sst_file_info;
  s = sst_file_writer.Finish(&sst_file_info);
  ASSERT_OK(s);

  options.delete_obsolete_files_period_micros = 0;
  options.disable_auto_compactions = true;
  DestroyAndReopen(options);

  yb::SyncPoint::GetInstance()->SetCallBack(
      "DBImpl::AddFile:FileCopied", [&](void* arg) {
        ASSERT_OK(Put("aaa", "bbb"));
        ASSERT_OK(Flush());
        ASSERT_OK(Put("aaa", "xxx"));
        ASSERT_OK(Flush());
        ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  s = db_->AddFile(sst_file_path);
  ASSERT_OK(s);

  for (int i = 0; i < 500; i++) {
    std::string k = Key(i);
    std::string v = k + "_val";
    ASSERT_EQ(Get(k), v);
  }

  yb::SyncPoint::GetInstance()->DisableProcessing();
}

TEST_F(DBTest, AddExternalSstFileNoCopy) {
  std::string sst_files_folder = test::TmpDir(env_) + "/sst_files/";
  ASSERT_OK(env_->CreateDir(sst_files_folder));
  Options options = CurrentOptions();
  options.env = env_;
  const ImmutableCFOptions ioptions(options);

  SstFileWriter sst_file_writer(EnvOptions(), ioptions, options.comparator);

  // file1.sst (0 => 99)
  std::string file1 = sst_files_folder + "file1.sst";
  ASSERT_OK(sst_file_writer.Open(file1));
  for (int k = 0; k < 100; k++) {
    ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val"));
  }
  ExternalSstFileInfo file1_info;
  Status s = sst_file_writer.Finish(&file1_info);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_EQ(file1_info.file_path, file1);
  ASSERT_EQ(file1_info.num_entries, 100);
  ASSERT_EQ(file1_info.smallest_key, Key(0));
  ASSERT_EQ(file1_info.largest_key, Key(99));

  // file2.sst (100 => 299)
  std::string file2 = sst_files_folder + "file2.sst";
  ASSERT_OK(sst_file_writer.Open(file2));
  for (int k = 100; k < 300; k++) {
    ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val"));
  }
  ExternalSstFileInfo file2_info;
  s = sst_file_writer.Finish(&file2_info);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_EQ(file2_info.file_path, file2);
  ASSERT_EQ(file2_info.num_entries, 200);
  ASSERT_EQ(file2_info.smallest_key, Key(100));
  ASSERT_EQ(file2_info.largest_key, Key(299));

  // file3.sst (110 => 124) .. overlap with file2.sst
  std::string file3 = sst_files_folder + "file3.sst";
  ASSERT_OK(sst_file_writer.Open(file3));
  for (int k = 110; k < 125; k++) {
    ASSERT_OK(sst_file_writer.Add(Key(k), Key(k) + "_val_overlap"));
  }
  ExternalSstFileInfo file3_info;
  s = sst_file_writer.Finish(&file3_info);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_EQ(file3_info.file_path, file3);
  ASSERT_EQ(file3_info.num_entries, 15);
  ASSERT_EQ(file3_info.smallest_key, Key(110));
  ASSERT_EQ(file3_info.largest_key, Key(124));

  s = db_->AddFile(&file1_info, true /* move file */);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_TRUE(env_->FileExists(file1).IsNotFound());

  s = db_->AddFile(&file2_info, false /* copy file */);
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_OK(env_->FileExists(file2));

  // This file have overlapping values with the exisitng data
  s = db_->AddFile(&file3_info, true /* move file */);
  ASSERT_FALSE(s.ok()) << s.ToString();
  ASSERT_OK(env_->FileExists(file3));

  for (int k = 0; k < 300; k++) {
    ASSERT_EQ(Get(Key(k)), Key(k) + "_val");
  }
}

TEST_F(DBTest, AddExternalSstFileMultiThreaded) {
  std::string sst_files_folder = test::TmpDir(env_) + "/sst_files/";
  // Bulk load 10 files every file contain 1000 keys
  int num_files = 10;
  int keys_per_file = 1000;

  // Generate file names
  std::vector<std::string> file_names;
  for (int i = 0; i < num_files; i++) {
    std::string file_name = "file_" + ToString(i) + ".sst";
    file_names.push_back(sst_files_folder + file_name);
  }

  do {
    ASSERT_OK(env_->CreateDirIfMissing(sst_files_folder));
    Options options = CurrentOptions();
    const ImmutableCFOptions ioptions(options);

    std::atomic<int> thread_num(0);
    std::function<void()> write_file_func = [&]() {
      int file_idx = thread_num.fetch_add(1);
      int range_start = file_idx * keys_per_file;
      int range_end = range_start + keys_per_file;

      SstFileWriter sst_file_writer(EnvOptions(), ioptions, options.comparator);

      ASSERT_OK(sst_file_writer.Open(file_names[file_idx]));

      for (int k = range_start; k < range_end; k++) {
        ASSERT_OK(sst_file_writer.Add(Key(k), Key(k)));
      }

      Status s = sst_file_writer.Finish();
      ASSERT_TRUE(s.ok()) << s.ToString();
    };
    // Write num_files files in parallel
    std::vector<std::thread> sst_writer_threads;
    for (int i = 0; i < num_files; ++i) {
      sst_writer_threads.emplace_back(write_file_func);
    }

    for (auto& t : sst_writer_threads) {
      t.join();
    }

    fprintf(stderr, "Wrote %d files (%d keys)\n", num_files,
            num_files * keys_per_file);

    thread_num.store(0);
    std::atomic<int> files_added(0);
    std::function<void()> load_file_func = [&]() {
      // We intentionally add every file twice, and assert that it was added
      // only once and the other add failed
      int thread_id = thread_num.fetch_add(1);
      int file_idx = thread_id / 2;
      // sometimes we use copy, sometimes link .. the result should be the same
      bool move_file = (thread_id % 3 == 0);

      Status s = db_->AddFile(file_names[file_idx], move_file);
      if (s.ok()) {
        files_added++;
      }
    };
    // Bulk load num_files files in parallel
    std::vector<std::thread> add_file_threads;
    DestroyAndReopen(options);
    for (int i = 0; i < num_files * 2; ++i) {
      add_file_threads.emplace_back(load_file_func);
    }

    for (auto& t : add_file_threads) {
      t.join();
    }
    ASSERT_EQ(files_added.load(), num_files);
    fprintf(stderr, "Loaded %d files (%d keys)\n", num_files,
            num_files * keys_per_file);

    // Overwrite values of keys divisible by 100
    for (int k = 0; k < num_files * keys_per_file; k += 100) {
      std::string key = Key(k);
      Status s = Put(key, key + "_new");
      ASSERT_TRUE(s.ok());
    }

    for (int i = 0; i < 2; i++) {
      // Make sure the values are correct before and after flush/compaction
      for (int k = 0; k < num_files * keys_per_file; ++k) {
        std::string key = Key(k);
        std::string value = (k % 100 == 0) ? (key + "_new") : key;
        ASSERT_EQ(Get(key), value);
      }
      ASSERT_OK(Flush());
      ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    }

    fprintf(stderr, "Verified %d values\n", num_files * keys_per_file);
  } while (ChangeOptions(kSkipPlainTable | kSkipUniversalCompaction |
                         kSkipFIFOCompaction));
}

TEST_F(DBTest, AddExternalSstFileOverlappingRanges) {
  std::string sst_files_folder = test::TmpDir(env_) + "/sst_files/";
  Random rnd(301);
  do {
    ASSERT_OK(env_->CreateDirIfMissing(sst_files_folder));
    Options options = CurrentOptions();
    DestroyAndReopen(options);
    const ImmutableCFOptions ioptions(options);
    SstFileWriter sst_file_writer(EnvOptions(), ioptions, options.comparator);

    printf("Option config = %d\n", option_config_);
    std::vector<std::pair<int, int>> key_ranges;
    // Lower number of key ranges for tsan due to low perf.
    constexpr int kNumKeyRanges = yb::NonTsanVsTsan(500, 100);
    for (int i = 0; i < kNumKeyRanges; i++) {
      int range_start = rnd.Uniform(20000);
      int keys_per_range = 10 + rnd.Uniform(41);

      key_ranges.emplace_back(range_start, range_start + keys_per_range);
    }

    int memtable_add = 0;
    int success_add_file = 0;
    int failed_add_file = 0;
    std::map<std::string, std::string> true_data;
    for (size_t i = 0; i < key_ranges.size(); i++) {
      int range_start = key_ranges[i].first;
      int range_end = key_ranges[i].second;

      Status s;
      std::string range_val = "range_" + ToString(i);

      // For 20% of ranges we use DB::Put, for 80% we use DB::AddFile
      if (i && i % 5 == 0) {
        // Use DB::Put to insert range (insert into memtable)
        range_val += "_put";
        for (int k = range_start; k <= range_end; k++) {
          s = Put(Key(k), range_val);
          ASSERT_OK(s);
        }
        memtable_add++;
      } else {
        // Use DB::AddFile to insert range
        range_val += "_add_file";

        // Generate the file containing the range
        std::string file_name = sst_files_folder + env_->GenerateUniqueId();
        ASSERT_OK(sst_file_writer.Open(file_name));
        for (int k = range_start; k <= range_end; k++) {
          s = sst_file_writer.Add(Key(k), range_val);
          ASSERT_OK(s);
        }
        ExternalSstFileInfo file_info;
        s = sst_file_writer.Finish(&file_info);
        ASSERT_OK(s);

        // Insert the generated file
        s = db_->AddFile(&file_info);

        auto it = true_data.lower_bound(Key(range_start));
        if (it != true_data.end() && it->first <= Key(range_end)) {
          // This range overlap with data already exist in DB
          ASSERT_NOK(s);
          failed_add_file++;
        } else {
          ASSERT_OK(s);
          success_add_file++;
        }
      }

      if (s.ok()) {
        // Update true_data map to include the new inserted data
        for (int k = range_start; k <= range_end; k++) {
          true_data[Key(k)] = range_val;
        }
      }

      // Flush / Compact the DB
      if (i && i % 50 == 0) {
        ASSERT_OK(Flush());
      }
      if (i && i % 75 == 0) {
        ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
      }
    }

    printf(
        "Total: %zu ranges\n"
        "AddFile()|Success: %d ranges\n"
        "AddFile()|RangeConflict: %d ranges\n"
        "Put(): %d ranges\n",
        key_ranges.size(), success_add_file, failed_add_file, memtable_add);

    // Verify the correctness of the data
    for (const auto& kv : true_data) {
      ASSERT_EQ(Get(kv.first), kv.second);
    }
    printf("keys/values verified\n");
  } while (ChangeOptions(kSkipPlainTable | kSkipUniversalCompaction |
                         kSkipFIFOCompaction));
}


TEST_F(DBTest, PinnedDataIteratorRandomized) {
  enum TestConfig {
    NORMAL,
    CLOSE_AND_OPEN,
    COMPACT_BEFORE_READ,
    FLUSH_EVERY_1000,
    MAX
  };

  // Generate Random data
  Random rnd(301);

  // Lower number of keys for tsan due to low perf.
  constexpr int kNumPuts = yb::NonTsanVsTsan(100000, 10000);
  int key_pool = static_cast<int>(kNumPuts * 0.7);
  int key_size = 100;
  int val_size = 1000;
  int seeks_percentage = 20;   // 20% of keys will be used to test seek()
  int delete_percentage = 20;  // 20% of keys will be deleted
  int merge_percentage = 20;   // 20% of keys will be added using Merge()

  for (int run_config = 0; run_config < TestConfig::MAX; run_config++) {
    Options options = CurrentOptions();
    BlockBasedTableOptions table_options;
    table_options.use_delta_encoding = false;
    options.table_factory.reset(NewBlockBasedTableFactory(table_options));
    options.merge_operator = MergeOperators::CreatePutOperator();
    DestroyAndReopen(options);

    std::vector<std::string> generated_keys(key_pool);
    for (int i = 0; i < key_pool; i++) {
      generated_keys[i] = RandomString(&rnd, key_size);
    }

    std::map<std::string, std::string> true_data;
    std::vector<std::string> random_keys;
    std::vector<std::string> deleted_keys;
    for (int i = 0; i < kNumPuts; i++) {
      auto& k = generated_keys[rnd.Next() % key_pool];
      auto v = RandomString(&rnd, val_size);

      // Insert data to true_data map and to DB
      true_data[k] = v;
      if (rnd.OneIn(static_cast<int>(100.0 / merge_percentage))) {
        ASSERT_OK(db_->Merge(WriteOptions(), k, v));
      } else {
        ASSERT_OK(Put(k, v));
      }

      // Pick random keys to be used to test Seek()
      if (rnd.OneIn(static_cast<int>(100.0 / seeks_percentage))) {
        random_keys.push_back(k);
      }

      // Delete some random keys
      if (rnd.OneIn(static_cast<int>(100.0 / delete_percentage))) {
        deleted_keys.push_back(k);
        true_data.erase(k);
        ASSERT_OK(Delete(k));
      }

      if (run_config == TestConfig::FLUSH_EVERY_1000) {
        if (i && i % 1000 == 0) {
          ASSERT_OK(Flush());
        }
      }
    }

    if (run_config == TestConfig::CLOSE_AND_OPEN) {
      Close();
      Reopen(options);
    } else if (run_config == TestConfig::COMPACT_BEFORE_READ) {
      ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    }

    ReadOptions ro;
    ro.pin_data = true;
    auto iter = db_->NewIterator(ro);

    {
      // Test Seek to random keys
      printf("Testing seek on %zu keys\n", random_keys.size());
      std::vector<Slice> keys_slices;
      std::vector<std::string> true_keys;
      for (auto& k : random_keys) {
        iter->Seek(k);
        if (!ASSERT_RESULT(iter->CheckedValid())) {
          ASSERT_EQ(true_data.lower_bound(k), true_data.end());
          continue;
        }
        std::string prop_value;
        ASSERT_OK(
            iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
        ASSERT_EQ("1", prop_value);
        keys_slices.push_back(iter->key());
        true_keys.push_back(true_data.lower_bound(k)->first);
      }

      for (size_t i = 0; i < keys_slices.size(); i++) {
        ASSERT_EQ(keys_slices[i].ToString(), true_keys[i]);
      }
    }

    {
      // Test iterating all data forward
      printf("Testing iterating forward on all keys\n");
      std::vector<Slice> all_keys;
      for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
        std::string prop_value;
        ASSERT_OK(
            iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
        ASSERT_EQ("1", prop_value);
        all_keys.push_back(iter->key());
      }
      ASSERT_EQ(all_keys.size(), true_data.size());

      // Verify that all keys slices are valid
      auto data_iter = true_data.begin();
      for (size_t i = 0; i < all_keys.size(); i++) {
        ASSERT_EQ(all_keys[i].ToString(), data_iter->first);
        data_iter++;
      }
    }

    {
      // Test iterating all data backward
      printf("Testing iterating backward on all keys\n");
      std::vector<Slice> all_keys;
      for (iter->SeekToLast(); ASSERT_RESULT(iter->CheckedValid()); iter->Prev()) {
        std::string prop_value;
        ASSERT_OK(
            iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
        ASSERT_EQ("1", prop_value);
        all_keys.push_back(iter->key());
      }
      ASSERT_EQ(all_keys.size(), true_data.size());

      // Verify that all keys slices are valid (backward)
      auto data_iter = true_data.rbegin();
      for (size_t i = 0; i < all_keys.size(); i++) {
        ASSERT_EQ(all_keys[i].ToString(), data_iter->first);
        data_iter++;
      }
    }

    delete iter;
  }
}

TEST_F(DBTest, PinnedDataIteratorMultipleFiles) {
  Options options = CurrentOptions();
  BlockBasedTableOptions table_options;
  table_options.use_delta_encoding = false;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  options.disable_auto_compactions = true;
  options.write_buffer_size = 1024 * 1024 * 10;  // 10 Mb
  DestroyAndReopen(options);

  std::map<std::string, std::string> true_data;

  // Generate 4 sst files in L2
  Random rnd(301);
  for (int i = 1; i <= 1000; i++) {
    std::string k = Key(i * 3);
    std::string v = RandomString(&rnd, 100);
    ASSERT_OK(Put(k, v));
    true_data[k] = v;
    if (i % 250 == 0) {
      ASSERT_OK(Flush());
    }
  }
  ASSERT_EQ(FilesPerLevel(0), "4");
  ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
  ASSERT_EQ(FilesPerLevel(0), "0,4");

  // Generate 4 sst files in L0
  for (int i = 1; i <= 1000; i++) {
    std::string k = Key(i * 2);
    std::string v = RandomString(&rnd, 100);
    ASSERT_OK(Put(k, v));
    true_data[k] = v;
    if (i % 250 == 0) {
      ASSERT_OK(Flush());
    }
  }
  ASSERT_EQ(FilesPerLevel(0), "4,4");

  // Add some keys/values in memtables
  for (int i = 1; i <= 1000; i++) {
    std::string k = Key(i);
    std::string v = RandomString(&rnd, 100);
    ASSERT_OK(Put(k, v));
    true_data[k] = v;
  }
  ASSERT_EQ(FilesPerLevel(0), "4,4");

  ReadOptions ro;
  ro.pin_data = true;
  auto iter = db_->NewIterator(ro);

  std::vector<std::pair<Slice, std::string>> results;
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    std::string prop_value;
    ASSERT_OK(iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
    ASSERT_EQ("1", prop_value);
    results.emplace_back(iter->key(), iter->value().ToString());
  }

  ASSERT_EQ(results.size(), true_data.size());
  auto data_iter = true_data.begin();
  for (size_t i = 0; i < results.size(); i++, data_iter++) {
    auto& kv = results[i];
    ASSERT_EQ(kv.first, data_iter->first);
    ASSERT_EQ(kv.second, data_iter->second);
  }

  delete iter;
}

TEST_F(DBTest, PinnedDataIteratorMergeOperator) {
  Options options = CurrentOptions();
  BlockBasedTableOptions table_options;
  table_options.use_delta_encoding = false;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  options.merge_operator = MergeOperators::CreateUInt64AddOperator();
  DestroyAndReopen(options);

  std::string numbers[7];
  for (int val = 0; val <= 6; val++) {
    PutFixed64(numbers + val, val);
  }

  // +1 all keys in range [ 0 => 999]
  for (int i = 0; i < 1000; i++) {
    WriteOptions wo;
    ASSERT_OK(db_->Merge(wo, Key(i), numbers[1]));
  }

  // +2 all keys divisible by 2 in range [ 0 => 999]
  for (int i = 0; i < 1000; i += 2) {
    WriteOptions wo;
    ASSERT_OK(db_->Merge(wo, Key(i), numbers[2]));
  }

  // +3 all keys divisible by 5 in range [ 0 => 999]
  for (int i = 0; i < 1000; i += 5) {
    WriteOptions wo;
    ASSERT_OK(db_->Merge(wo, Key(i), numbers[3]));
  }

  ReadOptions ro;
  ro.pin_data = true;
  auto iter = db_->NewIterator(ro);

  std::vector<std::pair<Slice, std::string>> results;
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    std::string prop_value;
    ASSERT_OK(iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
    ASSERT_EQ("1", prop_value);
    results.emplace_back(iter->key(), iter->value().ToString());
  }

  ASSERT_EQ(results.size(), 1000);
  for (size_t i = 0; i < results.size(); i++) {
    auto& kv = results[i];
    ASSERT_EQ(kv.first, Key(static_cast<int>(i)));
    int expected_val = 1;
    if (i % 2 == 0) {
      expected_val += 2;
    }
    if (i % 5 == 0) {
      expected_val += 3;
    }
    ASSERT_EQ(kv.second, numbers[expected_val]);
  }

  delete iter;
}

TEST_F(DBTest, PinnedDataIteratorReadAfterUpdate) {
  Options options = CurrentOptions();
  BlockBasedTableOptions table_options;
  table_options.use_delta_encoding = false;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  options.write_buffer_size = 100000;
  DestroyAndReopen(options);

  Random rnd(301);

  std::map<std::string, std::string> true_data;
  for (int i = 0; i < 1000; i++) {
    std::string k = RandomString(&rnd, 10);
    std::string v = RandomString(&rnd, 1000);
    ASSERT_OK(Put(k, v));
    true_data[k] = v;
  }

  ReadOptions ro;
  ro.pin_data = true;
  auto iter = db_->NewIterator(ro);

  // Delete 50% of the keys and update the other 50%
  for (auto& kv : true_data) {
    if (rnd.OneIn(2)) {
      ASSERT_OK(Delete(kv.first));
    } else {
      std::string new_val = RandomString(&rnd, 1000);
      ASSERT_OK(Put(kv.first, new_val));
    }
  }

  std::vector<std::pair<Slice, std::string>> results;
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    std::string prop_value;
    ASSERT_OK(iter->GetProperty("rocksdb.iterator.is-key-pinned", &prop_value));
    ASSERT_EQ("1", prop_value);
    results.emplace_back(iter->key(), iter->value().ToString());
  }

  auto data_iter = true_data.begin();
  for (size_t i = 0; i < results.size(); i++, data_iter++) {
    auto& kv = results[i];
    ASSERT_EQ(kv.first, data_iter->first);
    ASSERT_EQ(kv.second, data_iter->second);
  }

  delete iter;
}

INSTANTIATE_TEST_CASE_P(DBTestWithParam, DBTestWithParam,
                        ::testing::Combine(::testing::Values(1, 4),
                                           ::testing::Bool()));

TEST_F(DBTest, PauseBackgroundWorkTest) {
  Options options;
  options.write_buffer_size = 100000;  // Small write buffer
  options = CurrentOptions(options);
  Reopen(options);

  std::vector<std::thread> threads;
  std::atomic<bool> done(false);
  ASSERT_OK(db_->PauseBackgroundWork());
  threads.emplace_back([&]() {
    Random rnd(301);
    for (int i = 0; i < 10000; ++i) {
      ASSERT_OK(Put(RandomString(&rnd, 10), RandomString(&rnd, 10)));
    }
    done.store(true);
  });
  env_->SleepForMicroseconds(200000);
  // make sure the thread is not done
  ASSERT_EQ(false, done.load());
  ASSERT_OK(db_->ContinueBackgroundWork());
  for (auto& t : threads) {
    t.join();
  }
  // now it's done
  ASSERT_EQ(true, done.load());
}

namespace {
void ValidateKeyExistence(DB* db, const std::vector<Slice>& keys_must_exist,
                          const std::vector<Slice>& keys_must_not_exist) {
  // Ensure that expected keys exist
  std::vector<std::string> values;
  if (keys_must_exist.size() > 0) {
    std::vector<Status> status_list =
        db->MultiGet(ReadOptions(), keys_must_exist, &values);
    for (size_t i = 0; i < keys_must_exist.size(); i++) {
      ASSERT_OK(status_list[i]);
    }
  }

  // Ensure that given keys don't exist
  if (keys_must_not_exist.size() > 0) {
    std::vector<Status> status_list =
        db->MultiGet(ReadOptions(), keys_must_not_exist, &values);
    for (size_t i = 0; i < keys_must_not_exist.size(); i++) {
      ASSERT_TRUE(status_list[i].IsNotFound());
    }
  }
}

}  // namespace

TEST_F(DBTest, WalFilterTest) {
  class TestWalFilter : public WalFilter {
   private:
    // Processing option that is requested to be applied at the given index
    WalFilter::WalProcessingOption wal_processing_option_;
    // Index at which to apply wal_processing_option_
    // At other indexes default wal_processing_option::kContinueProcessing is
    // returned.
    size_t apply_option_at_record_index_;
    // Current record index, incremented with each record encountered.
    size_t current_record_index_;

   public:
    TestWalFilter(WalFilter::WalProcessingOption wal_processing_option,
                  size_t apply_option_for_record_index)
        : wal_processing_option_(wal_processing_option),
          apply_option_at_record_index_(apply_option_for_record_index),
          current_record_index_(0) {}

    virtual WalProcessingOption LogRecord(const WriteBatch& batch,
                                          WriteBatch* new_batch,
                                          bool* batch_changed) const override {
      WalFilter::WalProcessingOption option_to_return;

      if (current_record_index_ == apply_option_at_record_index_) {
        option_to_return = wal_processing_option_;
      } else {
        option_to_return = WalProcessingOption::kContinueProcessing;
      }

      // Filter is passed as a const object for RocksDB to not modify the
      // object, however we modify it for our own purpose here and hence
      // cast the constness away.
      (const_cast<TestWalFilter*>(this)->current_record_index_)++;

      return option_to_return;
    }

    const char* Name() const override { return "TestWalFilter"; }
  };

  // Create 3 batches with two keys each
  std::vector<std::vector<std::string>> batch_keys(3);

  batch_keys[0].push_back("key1");
  batch_keys[0].push_back("key2");
  batch_keys[1].push_back("key3");
  batch_keys[1].push_back("key4");
  batch_keys[2].push_back("key5");
  batch_keys[2].push_back("key6");

  // Test with all WAL processing options
  for (int option = 0;
       option < static_cast<int>(
                    WalFilter::WalProcessingOption::kWalProcessingOptionMax);
       option++) {
    Options options = OptionsForLogIterTest();
    DestroyAndReopen(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Write given keys in given batches
    for (size_t i = 0; i < batch_keys.size(); i++) {
      WriteBatch batch;
      for (size_t j = 0; j < batch_keys[i].size(); j++) {
        batch.Put(handles_[0], batch_keys[i][j], DummyString(1024));
      }
      ASSERT_OK(dbfull()->Write(WriteOptions(), &batch));
    }

    WalFilter::WalProcessingOption wal_processing_option =
        static_cast<WalFilter::WalProcessingOption>(option);

    // Create a test filter that would apply wal_processing_option at the first
    // record
    size_t apply_option_for_record_index = 1;
    TestWalFilter test_wal_filter(wal_processing_option,
                                  apply_option_for_record_index);

    // Reopen database with option to use WAL filter
    options = OptionsForLogIterTest();
    options.wal_filter = &test_wal_filter;
    Status status =
        TryReopenWithColumnFamilies({"default", "pikachu"}, options);
    if (wal_processing_option ==
        WalFilter::WalProcessingOption::kCorruptedRecord) {
      assert(!status.ok());
      // In case of corruption we can turn off paranoid_checks to reopen
      // databse
      options.paranoid_checks = false;
      ReopenWithColumnFamilies({"default", "pikachu"}, options);
    } else {
      assert(status.ok());
    }

    // Compute which keys we expect to be found
    // and which we expect not to be found after recovery.
    std::vector<Slice> keys_must_exist;
    std::vector<Slice> keys_must_not_exist;
    switch (wal_processing_option) {
      case WalFilter::WalProcessingOption::kCorruptedRecord:
      case WalFilter::WalProcessingOption::kContinueProcessing: {
        fprintf(stderr, "Testing with complete WAL processing\n");
        // we expect all records to be processed
        for (size_t i = 0; i < batch_keys.size(); i++) {
          for (size_t j = 0; j < batch_keys[i].size(); j++) {
            keys_must_exist.push_back(Slice(batch_keys[i][j]));
          }
        }
        break;
      }
      case WalFilter::WalProcessingOption::kIgnoreCurrentRecord: {
        fprintf(stderr,
                "Testing with ignoring record %" ROCKSDB_PRIszt " only\n",
                apply_option_for_record_index);
        // We expect the record with apply_option_for_record_index to be not
        // found.
        for (size_t i = 0; i < batch_keys.size(); i++) {
          for (size_t j = 0; j < batch_keys[i].size(); j++) {
            if (i == apply_option_for_record_index) {
              keys_must_not_exist.push_back(Slice(batch_keys[i][j]));
            } else {
              keys_must_exist.push_back(Slice(batch_keys[i][j]));
            }
          }
        }
        break;
      }
      case WalFilter::WalProcessingOption::kStopReplay: {
        fprintf(stderr,
                "Testing with stopping replay from record %" ROCKSDB_PRIszt
                "\n",
                apply_option_for_record_index);
        // We expect records beyond apply_option_for_record_index to be not
        // found.
        for (size_t i = 0; i < batch_keys.size(); i++) {
          for (size_t j = 0; j < batch_keys[i].size(); j++) {
            if (i >= apply_option_for_record_index) {
              keys_must_not_exist.push_back(Slice(batch_keys[i][j]));
            } else {
              keys_must_exist.push_back(Slice(batch_keys[i][j]));
            }
          }
        }
        break;
      }
      default:
        assert(false);  // unhandled case
    }

    bool checked_after_reopen = false;

    while (true) {
      // Ensure that expected keys exists
      // and not expected keys don't exist after recovery
      ValidateKeyExistence(db_, keys_must_exist, keys_must_not_exist);

      if (checked_after_reopen) {
        break;
      }

      // reopen database again to make sure previous log(s) are not used
      // (even if they were skipped)
      // reopn database with option to use WAL filter
      options = OptionsForLogIterTest();
      ReopenWithColumnFamilies({"default", "pikachu"}, options);

      checked_after_reopen = true;
    }
  }
}

TEST_F(DBTest, WalFilterTestWithChangeBatch) {
  class ChangeBatchHandler : public WriteBatch::Handler {
   private:
    // Batch to insert keys in
    WriteBatch* new_write_batch_;
    // Number of keys to add in the new batch
    size_t num_keys_to_add_in_new_batch_;
    // Number of keys added to new batch
    size_t num_keys_added_;

   public:
    ChangeBatchHandler(WriteBatch* new_write_batch,
                       size_t num_keys_to_add_in_new_batch)
        : new_write_batch_(new_write_batch),
          num_keys_to_add_in_new_batch_(num_keys_to_add_in_new_batch),
          num_keys_added_(0) {}
    void Put(const Slice& key, const Slice& value) override {
      if (num_keys_added_ < num_keys_to_add_in_new_batch_) {
        new_write_batch_->Put(key, value);
        ++num_keys_added_;
      }
    }
  };

  class TestWalFilterWithChangeBatch : public WalFilter {
   private:
    // Index at which to start changing records
    size_t change_records_from_index_;
    // Number of keys to add in the new batch
    size_t num_keys_to_add_in_new_batch_;
    // Current record index, incremented with each record encountered.
    size_t current_record_index_;

   public:
    TestWalFilterWithChangeBatch(size_t change_records_from_index,
                                 size_t num_keys_to_add_in_new_batch)
        : change_records_from_index_(change_records_from_index),
          num_keys_to_add_in_new_batch_(num_keys_to_add_in_new_batch),
          current_record_index_(0) {}

    virtual WalProcessingOption LogRecord(const WriteBatch& batch,
                                          WriteBatch* new_batch,
                                          bool* batch_changed) const override {
      if (current_record_index_ >= change_records_from_index_) {
        ChangeBatchHandler handler(new_batch, num_keys_to_add_in_new_batch_);
        EXPECT_OK(batch.Iterate(&handler));
        *batch_changed = true;
      }

      // Filter is passed as a const object for RocksDB to not modify the
      // object, however we modify it for our own purpose here and hence
      // cast the constness away.
      (const_cast<TestWalFilterWithChangeBatch*>(this)
           ->current_record_index_)++;

      return WalProcessingOption::kContinueProcessing;
    }

    const char* Name() const override {
      return "TestWalFilterWithChangeBatch";
    }
  };

  std::vector<std::vector<std::string>> batch_keys(3);

  batch_keys[0].push_back("key1");
  batch_keys[0].push_back("key2");
  batch_keys[1].push_back("key3");
  batch_keys[1].push_back("key4");
  batch_keys[2].push_back("key5");
  batch_keys[2].push_back("key6");

  Options options = OptionsForLogIterTest();
  DestroyAndReopen(options);
  CreateAndReopenWithCF({"pikachu"}, options);

  // Write given keys in given batches
  for (size_t i = 0; i < batch_keys.size(); i++) {
    WriteBatch batch;
    for (size_t j = 0; j < batch_keys[i].size(); j++) {
      batch.Put(handles_[0], batch_keys[i][j], DummyString(1024));
    }
    ASSERT_OK(dbfull()->Write(WriteOptions(), &batch));
  }

  // Create a test filter that would apply wal_processing_option at the first
  // record
  size_t change_records_from_index = 1;
  size_t num_keys_to_add_in_new_batch = 1;
  TestWalFilterWithChangeBatch test_wal_filter_with_change_batch(
      change_records_from_index, num_keys_to_add_in_new_batch);

  // Reopen database with option to use WAL filter
  options = OptionsForLogIterTest();
  options.wal_filter = &test_wal_filter_with_change_batch;
  ReopenWithColumnFamilies({"default", "pikachu"}, options);

  // Ensure that all keys exist before change_records_from_index_
  // And after that index only single key exists
  // as our filter adds only single key for each batch
  std::vector<Slice> keys_must_exist;
  std::vector<Slice> keys_must_not_exist;

  for (size_t i = 0; i < batch_keys.size(); i++) {
    for (size_t j = 0; j < batch_keys[i].size(); j++) {
      if (i >= change_records_from_index && j >= num_keys_to_add_in_new_batch) {
        keys_must_not_exist.push_back(Slice(batch_keys[i][j]));
      } else {
        keys_must_exist.push_back(Slice(batch_keys[i][j]));
      }
    }
  }

  bool checked_after_reopen = false;

  while (true) {
    // Ensure that expected keys exists
    // and not expected keys don't exist after recovery
    ValidateKeyExistence(db_, keys_must_exist, keys_must_not_exist);

    if (checked_after_reopen) {
      break;
    }

    // reopen database again to make sure previous log(s) are not used
    // (even if they were skipped)
    // reopn database with option to use WAL filter
    options = OptionsForLogIterTest();
    ReopenWithColumnFamilies({"default", "pikachu"}, options);

    checked_after_reopen = true;
  }
}

TEST_F(DBTest, WalFilterTestWithChangeBatchExtraKeys) {
  class TestWalFilterWithChangeBatchAddExtraKeys : public WalFilter {
   public:
    virtual WalProcessingOption LogRecord(const WriteBatch& batch,
                                          WriteBatch* new_batch,
                                          bool* batch_changed) const override {
      *new_batch = batch;
      new_batch->Put("key_extra", "value_extra");
      *batch_changed = true;
      return WalProcessingOption::kContinueProcessing;
    }

    const char* Name() const override {
      return "WalFilterTestWithChangeBatchExtraKeys";
    }
  };

  std::vector<std::vector<std::string>> batch_keys(3);

  batch_keys[0].push_back("key1");
  batch_keys[0].push_back("key2");
  batch_keys[1].push_back("key3");
  batch_keys[1].push_back("key4");
  batch_keys[2].push_back("key5");
  batch_keys[2].push_back("key6");

  Options options = OptionsForLogIterTest();
  DestroyAndReopen(options);
  CreateAndReopenWithCF({"pikachu"}, options);

  // Write given keys in given batches
  for (size_t i = 0; i < batch_keys.size(); i++) {
    WriteBatch batch;
    for (size_t j = 0; j < batch_keys[i].size(); j++) {
      batch.Put(handles_[0], batch_keys[i][j], DummyString(1024));
    }
    ASSERT_OK(dbfull()->Write(WriteOptions(), &batch));
  }

  // Create a test filter that would add extra keys
  TestWalFilterWithChangeBatchAddExtraKeys test_wal_filter_extra_keys;

  // Reopen database with option to use WAL filter
  options = OptionsForLogIterTest();
  options.wal_filter = &test_wal_filter_extra_keys;
  Status status = TryReopenWithColumnFamilies({"default", "pikachu"}, options);
  ASSERT_TRUE(status.IsNotSupported());

  // Reopen without filter, now reopen should succeed - previous
  // attempt to open must not have altered the db.
  options = OptionsForLogIterTest();
  ReopenWithColumnFamilies({"default", "pikachu"}, options);

  std::vector<Slice> keys_must_exist;
  std::vector<Slice> keys_must_not_exist;  // empty vector

  for (size_t i = 0; i < batch_keys.size(); i++) {
    for (size_t j = 0; j < batch_keys[i].size(); j++) {
      keys_must_exist.push_back(Slice(batch_keys[i][j]));
    }
  }

  ValidateKeyExistence(db_, keys_must_exist, keys_must_not_exist);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/8919.
// Schedules flush after CancelAllBackgroundWork call.
TEST_F(DBTest, CancelBackgroundWorkWithFlush) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_compactions) = true;
  constexpr auto kMaxBackgroundCompactions = 1;
  constexpr auto kWriteBufferSize = 64_KB;
  constexpr auto kValueSize = 2_KB;

  for (const auto use_priority_thread_pool_for_flushes : {false, true}) {
    LOG(INFO) << "use_priority_thread_pool_for_flushes: " << use_priority_thread_pool_for_flushes;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_flushes) =
        use_priority_thread_pool_for_flushes;

    yb::PriorityThreadPool thread_pool(kMaxBackgroundCompactions);
    Options options = CurrentOptions();
    options.create_if_missing = true;
    options.priority_thread_pool_for_compactions_and_flushes = &thread_pool;
    options.compression = kNoCompression;
    options.write_buffer_size = kWriteBufferSize;
    options.arena_block_size = kValueSize;
    options.log_prefix = yb::Format(
        "TEST_use_priority_thread_pool_for_flushes_$0: ", use_priority_thread_pool_for_flushes);
    options.info_log_level = InfoLogLevel::INFO_LEVEL;
    options.info_log = std::make_shared<yb::YBRocksDBLogger>(options.log_prefix);

    DestroyAndReopen(options);

    WriteOptions wo;
    wo.disableWAL = true;

    LOG(INFO) << "Writing data...";
    Random rnd(301);
    int key = 0;
    while (key * kValueSize < kWriteBufferSize) {
      ASSERT_OK(Put(Key(++key), RandomString(&rnd, kValueSize), wo));
    }

    db_->SetDisableFlushOnShutdown(true);
    CancelAllBackgroundWork(db_);

    // Write one more key, that should trigger scheduling flush.
    ASSERT_OK(Put(Key(++key), RandomString(&rnd, kValueSize), wo));
    LOG(INFO) << "Writing data - done";

    Close();
  }
}

}  // namespace rocksdb


int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineNonHelpFlags(&argc, &argv, /* remove_flags */ true);
  return RUN_ALL_TESTS();
}
