// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/properties_collector/key_hybrid_time_range_collector.h"

#include "yb/docdb/properties_collector/chain_tracker_test_util.h"

#include "yb/util/test_util.h"

namespace yb::docdb {

class KeyHybridTimeRangeCollectorTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    collector_.reset(MakeKeyHybridTimeRangeCollectorFactory()->CreateTablePropertiesCollector(
        rocksdb::TablePropertiesCollectorFactory::Context()));
  }

  Status Add(const std::string& key) {
    return collector_->AddUserKey(key, Str("v"), rocksdb::kEntryPut, 0, 0);
  }

  std::optional<KeyHybridTimeRange> Finish() {
    rocksdb::UserCollectedProperties properties;
    EXPECT_OK(collector_->Finish(&properties));
    // Finish is called more than once per build and must give the same result.
    rocksdb::UserCollectedProperties again;
    EXPECT_OK(collector_->Finish(&again));
    EXPECT_EQ(properties, again);
    return GetKeyHybridTimeRange(properties);
  }

  std::unique_ptr<rocksdb::TablePropertiesCollector> collector_;
};

TEST_F(KeyHybridTimeRangeCollectorTest, Range) {
  // File order is key order, not time order.
  ASSERT_OK(Add(RowKey(Row(1), 5 * kMinute)));
  ASSERT_OK(Add(ColumnKey(Row(1), 1, 1 * kMinute)));
  ASSERT_OK(Add(ColumnKey(Row(1), 1, 7 * kMinute)));
  ASSERT_OK(Add(RowKey(Row(2), 3 * kMinute)));
  auto range = Finish();
  ASSERT_TRUE(range.has_value());
  ASSERT_EQ(range->min, AgeToHt(7 * kMinute));
  ASSERT_EQ(range->max, AgeToHt(1 * kMinute));
}

TEST_F(KeyHybridTimeRangeCollectorTest, Empty) {
  ASSERT_FALSE(Finish().has_value());
}

TEST_F(KeyHybridTimeRangeCollectorTest, UndecodableKey) {
  ASSERT_OK(Add(RowKey(Row(1), 5 * kMinute)));
  // A key whose hybrid time cannot be decoded must leave the file without a range: it is unknown
  // whether a hybrid time filter would hide it.
  ASSERT_OK(Add(std::string()));
  ASSERT_OK(Add(RowKey(Row(2), 3 * kMinute)));
  ASSERT_FALSE(Finish().has_value());
}

TEST_F(KeyHybridTimeRangeCollectorTest, AddIsIdempotent) {
  rocksdb::Options options;
  AddKeyHybridTimeRangeCollector(&options);
  AddKeyHybridTimeRangeCollector(&options);
  ASSERT_EQ(options.table_properties_collector_factories.size(), 1);
}

}  // namespace yb::docdb
