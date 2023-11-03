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

#include "yb/rocksdb/db/db_test_util.h"

#include "yb/util/test_macros.h"
#include "yb/rocksdb/util/testutil.h"

using std::unique_ptr;

namespace rocksdb {

class SliceTransformTest : public RocksDBTest {};

TEST_F(SliceTransformTest, CapPrefixTransform) {
  std::string s;
  s = "abcdefge";

  unique_ptr<const SliceTransform> transform;

  transform.reset(NewCappedPrefixTransform(6));
  ASSERT_EQ(transform->Transform(s).ToString(), "abcdef");
  ASSERT_TRUE(transform->SameResultWhenAppended("123456"));
  ASSERT_TRUE(transform->SameResultWhenAppended("1234567"));
  ASSERT_TRUE(!transform->SameResultWhenAppended("12345"));

  transform.reset(NewCappedPrefixTransform(8));
  ASSERT_EQ(transform->Transform(s).ToString(), "abcdefge");

  transform.reset(NewCappedPrefixTransform(10));
  ASSERT_EQ(transform->Transform(s).ToString(), "abcdefge");

  transform.reset(NewCappedPrefixTransform(0));
  ASSERT_EQ(transform->Transform(s).ToString(), "");

  transform.reset(NewCappedPrefixTransform(0));
  ASSERT_EQ(transform->Transform(std::string()).ToString(), "");
}

class SliceTransformDBTest : public RocksDBTest {
 private:
  std::string dbname_;
  Env* env_;
  DB* db_;

 public:
  SliceTransformDBTest() : env_(Env::Default()), db_(nullptr) {
    dbname_ = test::TmpDir() + "/slice_transform_db_test";
    EXPECT_OK(DestroyDB(dbname_, last_options_));
  }

  ~SliceTransformDBTest() {
    delete db_;
    EXPECT_OK(DestroyDB(dbname_, last_options_));
  }

  DB* db() { return db_; }

  // Return the current option configuration.
  Options* GetOptions() { return &last_options_; }

  void DestroyAndReopen() {
    // Destroy using last options
    Destroy();
    ASSERT_OK(TryReopen());
  }

  void Destroy() {
    delete db_;
    db_ = nullptr;
    ASSERT_OK(DestroyDB(dbname_, last_options_));
  }

  Status TryReopen() {
    delete db_;
    db_ = nullptr;
    last_options_.create_if_missing = true;

    return DB::Open(last_options_, dbname_, &db_);
  }

  Options last_options_;
};

TEST_F(SliceTransformDBTest, CapPrefix) {
  last_options_.prefix_extractor.reset(NewCappedPrefixTransform(8));
  last_options_.statistics = rocksdb::CreateDBStatisticsForTests();
  BlockBasedTableOptions bbto;
  bbto.filter_policy.reset(NewBloomFilterPolicy(10, false));
  bbto.whole_key_filtering = false;
  last_options_.table_factory.reset(NewBlockBasedTableFactory(bbto));
  ASSERT_OK(TryReopen());

  ReadOptions ro;
  FlushOptions fo;
  WriteOptions wo;

  ASSERT_OK(db()->Put(wo, "barbarbar", "foo"));
  ASSERT_OK(db()->Put(wo, "barbarbar2", "foo2"));
  ASSERT_OK(db()->Put(wo, "foo", "bar"));
  ASSERT_OK(db()->Put(wo, "foo3", "bar3"));
  ASSERT_OK(db()->Flush(fo));

  unique_ptr<Iterator> iter(db()->NewIterator(ro));

  iter->Seek("foo");
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
  ASSERT_EQ(iter->value().ToString(), "bar");
  ASSERT_EQ(TestGetTickerCount(last_options_, BLOOM_FILTER_PREFIX_USEFUL), 0U);

  iter->Seek("foo2");
  ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
  ASSERT_EQ(TestGetTickerCount(last_options_, BLOOM_FILTER_PREFIX_USEFUL), 1U);

  iter->Seek("barbarbar");
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
  ASSERT_EQ(iter->value().ToString(), "foo");
  ASSERT_EQ(TestGetTickerCount(last_options_, BLOOM_FILTER_PREFIX_USEFUL), 1U);

  iter->Seek("barfoofoo");
  ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
  ASSERT_EQ(TestGetTickerCount(last_options_, BLOOM_FILTER_PREFIX_USEFUL), 2U);

  iter->Seek("foobarbar");
  ASSERT_TRUE(!ASSERT_RESULT(iter->CheckedValid()));
  ASSERT_EQ(TestGetTickerCount(last_options_, BLOOM_FILTER_PREFIX_USEFUL), 3U);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
