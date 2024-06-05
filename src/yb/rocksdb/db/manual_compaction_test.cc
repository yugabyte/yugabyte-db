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
// Test for issue 178: a manual compaction causes deleted data to reappear.

#include <sstream>

#include <gtest/gtest.h>

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/util/testharness.h"

#include "yb/util/test_macros.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/util/tsan_util.h"

using namespace rocksdb;  // NOLINT

namespace {

const int kNumKeys = yb::NonTsanVsTsan(1100000, 100000);

std::string Key1(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "my_key_%d", i);
  return buf;
}

std::string Key2(int i) {
  return Key1(i) + "_xxx";
}

class ManualCompactionTest : public RocksDBTest {
 public:
  ManualCompactionTest() {
    // Get rid of any state from an old run.
    dbname_ = rocksdb::test::TmpDir() + "/rocksdb_cbug_test";
    CHECK_OK(DestroyDB(dbname_, rocksdb::Options()));
    LOG(INFO) << "Starting test with " << kNumKeys;
  }

  std::string dbname_;
};

class DestroyAllCompactionFilter : public CompactionFilter {
 public:
  DestroyAllCompactionFilter() {}

  FilterDecision Filter(int level, const Slice& key, const Slice& existing_value,
                        std::string* new_value, bool* value_changed) override {
    return existing_value.ToBuffer() == "destroy" ? FilterDecision::kDiscard
                                                  : FilterDecision::kKeep;
  }

  const char* Name() const override {
    return "DestroyAllCompactionFilter";
  }
};

TEST_F(ManualCompactionTest, CompactTouchesAllKeys) {
  for (int iter = 0; iter < 2; ++iter) {
    DB* db;
    Options options;
    if (iter == 0) { // level compaction
      options.num_levels = 3;
      options.compaction_style = kCompactionStyleLevel;
    } else { // universal compaction
      options.compaction_style = kCompactionStyleUniversal;
    }
    options.create_if_missing = true;
    options.compression = rocksdb::kNoCompression;
    options.compaction_filter = new DestroyAllCompactionFilter();
    ASSERT_OK(DB::Open(options, dbname_, &db));

    ASSERT_OK(db->Put(WriteOptions(), Slice("key1"), Slice("destroy")));
    ASSERT_OK(db->Put(WriteOptions(), Slice("key2"), Slice("destroy")));
    ASSERT_OK(db->Put(WriteOptions(), Slice("key3"), Slice("value3")));
    ASSERT_OK(db->Put(WriteOptions(), Slice("key4"), Slice("destroy")));

    Slice key4("key4");
    ASSERT_OK(db->CompactRange(CompactRangeOptions(), nullptr, &key4));
    Iterator* itr = db->NewIterator(ReadOptions());
    itr->SeekToFirst();
    ASSERT_TRUE(ASSERT_RESULT(itr->CheckedValid()));
    ASSERT_EQ("key3", itr->key().ToString());
    itr->Next();
    ASSERT_TRUE(!ASSERT_RESULT(itr->CheckedValid()));
    ASSERT_OK(itr->status());
    delete itr;

    delete options.compaction_filter;
    delete db;
    ASSERT_OK(DestroyDB(dbname_, options));
  }
}

TEST_F(ManualCompactionTest, Test) {
  // Open database.  Disable compression since it affects the creation
  // of layers and the code below is trying to test against a very
  // specific scenario.
  rocksdb::DB* db;
  rocksdb::Options db_options;
  db_options.create_if_missing = true;
  db_options.compression = rocksdb::kNoCompression;
  ASSERT_OK(rocksdb::DB::Open(db_options, dbname_, &db));

  // create first key range
  rocksdb::WriteBatch batch;
  for (int i = 0; i < kNumKeys; i++) {
    batch.Put(Key1(i), "value for range 1 key");
  }
  ASSERT_OK(db->Write(rocksdb::WriteOptions(), &batch));

  // create second key range
  batch.Clear();
  for (int i = 0; i < kNumKeys; i++) {
    batch.Put(Key2(i), "value for range 2 key");
  }
  ASSERT_OK(db->Write(rocksdb::WriteOptions(), &batch));

  // delete second key range
  batch.Clear();
  for (int i = 0; i < kNumKeys; i++) {
    batch.Delete(Key2(i));
  }
  ASSERT_OK(db->Write(rocksdb::WriteOptions(), &batch));

  // compact database
  std::string start_key = Key1(0);
  std::string end_key = Key1(kNumKeys - 1);
  rocksdb::Slice least(start_key.data(), start_key.size());
  rocksdb::Slice greatest(end_key.data(), end_key.size());

  // commenting out the line below causes the example to work correctly
  ASSERT_OK(db->CompactRange(CompactRangeOptions(), &least, &greatest));

  // count the keys
  rocksdb::Iterator* iter = db->NewIterator(rocksdb::ReadOptions());
  int num_keys = 0;
  for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    num_keys++;
  }
  delete iter;
  ASSERT_EQ(kNumKeys, num_keys) << "Bad number of keys";

  // close database
  delete db;
  ASSERT_OK(DestroyDB(dbname_, rocksdb::Options()));
}

}  // anonymous namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
