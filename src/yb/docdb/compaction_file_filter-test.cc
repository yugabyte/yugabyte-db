// Copyright (c) YugaByte, Inc.
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
#include "yb/docdb/compaction_file_filter.h"
#include <cstddef>
#include <future>
#include <memory>
#include <thread>

#include "yb/common/common_fwd.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/util/monotime.h"


namespace yb {
namespace docdb {
using rocksdb::FilterDecision;

static const Schema kTableSchema({ ColumnSchema("key", INT32),
                                   ColumnSchema("v1", UINT64),
                                   ColumnSchema("v2", STRING) },
                                 1);

class ExpirationFilterTest : public YBTest {
 public:
  ExpirationFilterTest() {}

  void SetUp() override {
    YBTest::SetUp();
    clock_.reset(new server::HybridClock());
    ASSERT_OK(clock_->Init());

    schema_.reset(new Schema(kTableSchema));
  }

 protected:
  scoped_refptr<server::Clock> clock_;
  std::shared_ptr<Schema> schema_;
};

ConsensusFrontier CreateConsensusFrontier(
    HybridTime ht, HybridTime expire_ht = HybridTime::kInvalid) {
  ConsensusFrontier f{{0, 1}, ht, HybridTime::kInvalid};
  if (expire_ht != HybridTime::kInvalid) {
    f.set_max_value_level_ttl_expiration_time(expire_ht);
  }
  return f;
}

rocksdb::FileMetaData CreateFile(rocksdb::UserFrontierPtr largest_frontier = nullptr) {
  rocksdb::FileMetaData f;
  f.fd = rocksdb::FileDescriptor(1, 0, 0, 0);
  f.smallest = rocksdb::MakeFileBoundaryValues("smallest", 100, rocksdb::kTypeValue);
  f.largest = rocksdb::MakeFileBoundaryValues("largest", 100, rocksdb::kTypeValue);
  if (largest_frontier) {
    f.largest.user_frontier = largest_frontier;
  }
  return f;
}

FilterDecision FilterHTAndExpiry(
    DocDBCompactionFileFilter* filter,
    HybridTime ht,
    HybridTime expiry,
    bool resetKeptFile = true) {
  auto frontier = CreateConsensusFrontier(ht, expiry);
  auto file = CreateFile(frontier.Clone());
  auto result = filter->Filter(&file);
  // Unless specifically specified, reset the filter after each run.
  if (resetKeptFile) {
    filter->TEST_ResetHasKeptFile();
  }
  return result;
}

TEST_F(ExpirationFilterTest, ExtractFromNullFileOrFrontier) {
  auto filter = DocDBCompactionFileFilter(schema_, 1000_usec_ht);
  EXPECT_EQ(filter.Extract(nullptr), ExpirationTime{});
  // create a file without a userfrontier
  auto file = CreateFile();
  EXPECT_EQ(filter.Extract(&file), ExpirationTime{});
}

TEST_F(ExpirationFilterTest, ExtractFromFileWithNoDefinedExpiration) {
  auto filter = DocDBCompactionFileFilter(schema_, 1000_usec_ht);
  const auto ht = 1000_usec_ht;
  auto frontier = CreateConsensusFrontier(ht);
  auto file = CreateFile(frontier.Clone());
  EXPECT_EQ(filter.Extract(&file), (ExpirationTime{kNoExpiration, ht}));
}

TEST_F(ExpirationFilterTest, ExtractFromFileWithExpiration) {
  auto ht = 1000_usec_ht;
  auto exp_ht = 2000_usec_ht;
  auto expected_expiry = ExpirationTime{exp_ht, ht};
  auto filter = DocDBCompactionFileFilter(schema_, 1000_usec_ht);
  auto frontier = CreateConsensusFrontier(ht, exp_ht);
  auto file = CreateFile(frontier.Clone());
  EXPECT_EQ(filter.Extract(&file), expected_expiry);
}

TEST_F(ExpirationFilterTest, TestFilterNoTableTTL) {
  const auto current_time = clock_->Now();
  const auto future_time = current_time.AddSeconds(1000);
  const auto past_time = 1000_usec_ht;
  auto filter = DocDBCompactionFileFilter(schema_, clock_->Now());
  // Check 1: File with maximum hybrid time and value non-expiration. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, HybridTime::kMax, kNoExpiration), FilterDecision::kKeep);
  // Check 2: File with a HT after the current time and value non-expiration. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, future_time, kNoExpiration), FilterDecision::kKeep);
  // Check 3: File with a HT before the current time and value non-expiration. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, past_time, kNoExpiration), FilterDecision::kKeep);

  // Check 4: File with TTL expiration time that has not expired. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, past_time, future_time), FilterDecision::kKeep);

  // Check 5: File with TTL expiration time that has expired. (discard)
  EXPECT_EQ(FilterHTAndExpiry(&filter, past_time, past_time), FilterDecision::kDiscard);

  // Check 6: File with TTL expiration time that defers to table TTL. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, past_time, kUseDefaultTTL), FilterDecision::kKeep);

  // Check 7: File with invalid TTL expiration time. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, past_time, HybridTime::kInvalid), FilterDecision::kKeep);
}

TEST_F(ExpirationFilterTest, TestFilterTableTTLThatWillNotExpire) {
  const auto current_time = clock_->Now();
  // The key_time is slightly earlier than the current_time, but not early enough to expire.
  const auto key_time = current_time.AddMilliseconds(-100);
  auto filter = DocDBCompactionFileFilter(schema_, current_time);
  // Create a large table TTL (i.e. table TTL that won't cause expirations during test)
  const uint64_t table_ttl_msec = 1000000;
  schema_->SetDefaultTimeToLive(table_ttl_msec);
  // Check 1: File with maximum hybrid time and a value non-expiration. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, HybridTime::kMax, kNoExpiration), FilterDecision::kKeep);

  // Check 2: File with a current HT time and a value non-expiration. (keep)
  EXPECT_EQ(FilterHTAndExpiry(&filter, key_time, kNoExpiration), FilterDecision::kKeep);

  // Check 3: File with current HT and TTL expiration time that has not expired. (keep)
  auto expiry_time = current_time.AddSeconds(1000);
  EXPECT_EQ(FilterHTAndExpiry(&filter, key_time, expiry_time), FilterDecision::kKeep);

  // Check 4: File with TTL expiration time that has expired. (keep to accomodate table TTL)
  expiry_time = current_time.AddSeconds(-1000);
  EXPECT_EQ(FilterHTAndExpiry(&filter, key_time, expiry_time), FilterDecision::kKeep);

  // Check 5: File with TTL expiration time that defers to table TTL. (keep)
  expiry_time = kUseDefaultTTL;
  EXPECT_EQ(FilterHTAndExpiry(&filter, key_time, expiry_time),   FilterDecision::kKeep);

  // Check 6: File with invalid TTL expiration time. (keep)
  expiry_time = HybridTime::kInvalid;
  EXPECT_EQ(FilterHTAndExpiry(&filter, key_time, expiry_time), FilterDecision::kKeep);
}

TEST_F(ExpirationFilterTest, TestFilterTableTTLThatWillExpire) {
  const auto current_time = clock_->Now();
  // The key_time is earlier than the current_time (enough to expire).
  const auto key_time = current_time.AddSeconds(-100);
  auto filter = DocDBCompactionFileFilter(schema_, current_time);
  // Set table TTL to small value that will expire during test.
  const uint64_t table_ttl_msec = 100;
  schema_->SetDefaultTimeToLive(table_ttl_msec);

  // Check 1: File with maximum hybrid time and value non-expiration. (keep)
  auto result = FilterHTAndExpiry(&filter, HybridTime::kMax, kNoExpiration);
  EXPECT_EQ(result, FilterDecision::kKeep);

  // Check 2: File with current hybrid time and value non-expiration. (keep)
  result = FilterHTAndExpiry(&filter, key_time, kNoExpiration);
  EXPECT_EQ(result, FilterDecision::kKeep);

  // Check 3: File with TTL expiration time that has not expired. (keep)
  auto expiry_time = current_time.AddSeconds(1000);
  result = FilterHTAndExpiry(&filter, key_time, expiry_time);
  EXPECT_EQ(result, FilterDecision::kKeep);

  // Check 4: File with TTL expiration time that has expired. (discard)
  expiry_time = current_time.AddSeconds(-100);
  result = FilterHTAndExpiry(&filter, key_time, expiry_time);
  EXPECT_EQ(result, FilterDecision::kDiscard);

  // Check 5: File with TTL expiration time that defers to table TTL. (discard)
  expiry_time = kUseDefaultTTL;
  result = FilterHTAndExpiry(&filter, key_time, expiry_time);
  EXPECT_EQ(result, FilterDecision::kDiscard);

  // Check 6: File with invalid TTL expiration time. (keep)
  expiry_time = HybridTime::kInvalid;
  EXPECT_EQ(FilterHTAndExpiry(&filter, key_time, expiry_time), FilterDecision::kKeep);
}

}  // namespace docdb
}  // namespace yb
