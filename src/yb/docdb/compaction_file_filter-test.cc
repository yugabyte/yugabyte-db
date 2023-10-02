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

#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"

#include "yb/docdb/compaction_file_filter.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/dockv/doc_ttl_util.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/dockv/primitive_value.h"

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/db/version_edit.h"

#include "yb/util/monotime.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(file_expiration_ignore_value_ttl);
DECLARE_bool(file_expiration_value_ttl_overrides_table_ttl);

namespace yb {
namespace docdb {

using dockv::kNoExpiration;
using dockv::kUseDefaultTTL;
using rocksdb::FilterDecision;

static const Schema kTableSchema({
    ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
    ColumnSchema("v1", DataType::UINT64),
    ColumnSchema("v2", DataType::STRING) });

class ExpirationFilterTest : public YBTest {
 public:
  ExpirationFilterTest() {}

  void TestFilterFilesAgainstResults(
      DocDBCompactionFileFilterFactory* filter_factory,
      const std::vector<ConsensusFrontier>& frontiers,
      const std::vector<FilterDecision>& expected_results
  );

  void SetUp() override {
    YBTest::SetUp();
    clock_.reset(new server::HybridClock());
    ASSERT_OK(clock_->Init());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_ignore_value_ttl) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_value_ttl_overrides_table_ttl) = false;

    retention_policy_ = std::make_shared<ManualHistoryRetentionPolicy>();
    retention_policy_->SetHistoryCutoff(HybridTime::kMax);  // no history retention by default
  }

 protected:
  scoped_refptr<server::Clock> clock_;
  std::shared_ptr<ManualHistoryRetentionPolicy> retention_policy_;
};

ConsensusFrontier CreateConsensusFrontier(
    HybridTime ht, HybridTime expire_ht = HybridTime::kInvalid) {
  ConsensusFrontier f{{0, 1}, ht, { HybridTime::kInvalid, HybridTime::kInvalid }};
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

std::vector<rocksdb::FileMetaData*> CreateFilePtrs(
    const std::vector<ConsensusFrontier>& frontiers) {
  auto file_ptrs = std::vector<rocksdb::FileMetaData*>(frontiers.size());
  for (size_t i = 0; i < frontiers.size(); i++) {
    file_ptrs[i] = new rocksdb::FileMetaData();
    *file_ptrs[i] = CreateFile(frontiers[i].Clone());
  }
  return file_ptrs;
}

void DeleteFilePtrs(std::vector<rocksdb::FileMetaData*>* file_ptrs) {
  for (auto file_ptr : *file_ptrs) {
    delete file_ptr;
  }
}

void SetRetentionPolicy(
    std::shared_ptr<ManualHistoryRetentionPolicy> manual_policy,
    MonoDelta table_ttl,
    HybridTime history_cutoff = HybridTime::kMax) {
  manual_policy->SetTableTTLForTests(table_ttl);
  manual_policy->SetHistoryCutoff(history_cutoff);
}

void ExpirationFilterTest::TestFilterFilesAgainstResults(
    DocDBCompactionFileFilterFactory* filter_factory,
    const std::vector<ConsensusFrontier>& frontiers,
    const std::vector<FilterDecision>& expected_results) {
  auto file_ptrs = CreateFilePtrs(frontiers);
  auto filter = filter_factory->CreateCompactionFileFilter(file_ptrs);
  for(size_t i = 0; i < file_ptrs.size(); i++) {
    auto result = filter->Filter(file_ptrs[i]);
    EXPECT_EQ(result, expected_results[i]);
  }
  DeleteFilePtrs(&file_ptrs);
}

TEST_F(ExpirationFilterTest, ExtractFromNullFileOrFrontier) {
  EXPECT_EQ(ExtractExpirationTime(nullptr), ExpirationTime{});
  // create a file without a userfrontier
  auto file = CreateFile();
  EXPECT_EQ(ExtractExpirationTime(&file), ExpirationTime{});
}

TEST_F(ExpirationFilterTest, ExtractFromFileWithNoDefinedExpiration) {
  const auto ht = 1000_usec_ht;
  auto frontier = CreateConsensusFrontier(ht);
  auto file = CreateFile(frontier.Clone());
  EXPECT_EQ(ExtractExpirationTime(&file), (ExpirationTime{kNoExpiration, ht}));
}

TEST_F(ExpirationFilterTest, ExtractFromFileWithExpiration) {
  auto ht = 1000_usec_ht;
  auto exp_ht = 2000_usec_ht;
  auto expected_expiry = ExpirationTime{exp_ht, ht};
  auto frontier = CreateConsensusFrontier(ht, exp_ht);
  auto file = CreateFile(frontier.Clone());
  EXPECT_EQ(ExtractExpirationTime(&file), expected_expiry);
}

TEST_F(ExpirationFilterTest, TestExpirationNoTableTTL) {
  const auto current_time = clock_->Now();
  const auto future_time = current_time.AddSeconds(1000);
  const auto past_time = 1000_usec_ht;
  // Use maximum table TTL
  const MonoDelta table_ttl_sec = dockv::ValueControlFields::kMaxTtl;
  // Check 1: File with maximum hybrid time and value non-expiration. (keep)
  auto expiry = ExpirationTime{kNoExpiration, HybridTime::kMax};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 2: File with a HT after the current time and value non-expiration. (keep)
  expiry = ExpirationTime{kNoExpiration, future_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 3: File with a HT before the current time and value non-expiration. (keep)
  expiry = ExpirationTime{kNoExpiration, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 4: File with TTL expiration time that has not expired. (keep)
  expiry = ExpirationTime{kNoExpiration, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 5: File with TTL expiration time that has expired. (discard)
  expiry = ExpirationTime{past_time, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), true);

  // Check 6: File with TTL expiration time that defers to table TTL. (keep)
  expiry = ExpirationTime{kUseDefaultTTL, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 7: File with invalid TTL expiration time. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 8: File with invalid TTL expiration time, but use table TTL only. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TABLE_ONLY), false);

  // Check 9: File with expired TTL expiration time, but use table TTL only. (keep)
  expiry = ExpirationTime{past_time, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TABLE_ONLY), false);

  // Check 10: File with invalid TTL expiration time, and trusting value TTL. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), false);

  // Check 11: File with "use table" expiration time, and trusting value TTL. (keep)
  expiry = ExpirationTime{kUseDefaultTTL, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), false);

  // Check 12: File with expired TTL expiration time, and trusting value TTL. (discard)
  expiry = ExpirationTime{past_time, past_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), true);
}

TEST_F(ExpirationFilterTest, TestExpirationTableTTLThatWillNotExpire) {
  const auto current_time = clock_->Now();
  // The key_time is slightly earlier than the current_time, but not early enough to expire.
  const auto key_time = current_time.AddMilliseconds(-100);

  // Create a large table TTL (i.e. table TTL that won't cause expirations during test)
  const MonoDelta table_ttl_sec = MonoDelta::FromSeconds(1000);

  // Check 1: File with maximum hybrid time and a value non-expiration. (keep)
  auto expiry = ExpirationTime{kNoExpiration, HybridTime::kMax};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 2: File with a current HT time and a value non-expiration. (keep)
  expiry = ExpirationTime{kNoExpiration, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 3: File with current HT and TTL expiration time that has not expired. (keep)
  expiry = ExpirationTime{current_time.AddSeconds(1000), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 4: File with TTL expiration time that has expired. (keep to accomodate table TTL)
  expiry = ExpirationTime{current_time.AddSeconds(-1000), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 5: File with TTL expiration time that defers to table TTL. (keep)
  expiry = ExpirationTime{kUseDefaultTTL, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 6: File with invalid TTL expiration time. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 7: File with invalid TTL expiration time, but use table TTL only. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TABLE_ONLY), false);

  // Check 8: File with expired TTL expiration time, but use table TTL only. (keep)
  expiry = ExpirationTime{key_time, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TABLE_ONLY), false);

  // Check 9: File with invalid TTL expiration time, and trusting value TTL. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), false);

  // Check 10: File with "use table" expiration time, and trusting value TTL. (keep)
  expiry = ExpirationTime{kUseDefaultTTL, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), false);

  // Check 11: File with expired TTL expiration time, and trusting value TTL. (discard)
  expiry = ExpirationTime{current_time.AddSeconds(-1000), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), true);
}

TEST_F(ExpirationFilterTest, TestExpirationTableTTLThatWillExpire) {
  const auto current_time = clock_->Now();
  // The key_time is earlier than the current_time (enough to expire).
  const auto key_time = current_time.AddSeconds(-100);

  // Set table TTL to small value that will expire during test.
  const MonoDelta table_ttl_sec = MonoDelta::FromSeconds(1);

  // Check 1: File with maximum hybrid time and value non-expiration. (keep)
  auto expiry = ExpirationTime{kNoExpiration, HybridTime::kMax};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 2: File with current hybrid time and value non-expiration. (keep)
  expiry = ExpirationTime{kNoExpiration, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 3: File with TTL expiration time that has not expired. (keep)
  expiry = ExpirationTime{current_time.AddSeconds(1000), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 4: File with TTL expiration time that has expired. (discard)
  expiry = ExpirationTime{current_time.AddSeconds(-100), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), true);

  // Check 5: File with TTL expiration time that defers to table TTL. (discard)
  expiry = ExpirationTime{kUseDefaultTTL, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), true);

  // Check 6: File with invalid TTL expiration time. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time), false);

  // Check 7: File with invalid TTL expiration time, but use table TTL only. (discard)
  expiry = ExpirationTime{HybridTime::kInvalid, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TABLE_ONLY), true);

  // Check 8: File with non-expired TTL expiration time, but use table TTL only. (discard)
  expiry = ExpirationTime{current_time.AddSeconds(1000), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TABLE_ONLY), true);

  // Check 9: File with invalid TTL expiration time, and trusting value TTL. (keep)
  expiry = ExpirationTime{HybridTime::kInvalid, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), false);

  // Check 10: File with "use table" expiration time, and trusting value TTL. (discard)
  expiry = ExpirationTime{kUseDefaultTTL, key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), true);

  // Check 11: File with non-expired TTL expiration time, and trusting value TTL. (keep)
  expiry = ExpirationTime{current_time.AddSeconds(1000), key_time};
  EXPECT_EQ(TtlIsExpired(expiry, table_ttl_sec, current_time, EXP_TRUST_VALUE), false);
}

TEST_F(ExpirationFilterTest, TestFilterBasedOnTableTTLOnlyNoTableTTL) {
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  // Test with no default time to live (keep all)
  std::vector<ConsensusFrontier> frontiers = {
    CreateConsensusFrontier(now.AddSeconds(-100), kUseDefaultTTL), // keep
    CreateConsensusFrontier(now.AddSeconds(100), kUseDefaultTTL), // keep
    CreateConsensusFrontier(now.AddSeconds(-10000), kUseDefaultTTL), // keep
    CreateConsensusFrontier(now.AddSeconds(10000), kUseDefaultTTL) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterBasedOnTableTTLOnly) {
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  SetRetentionPolicy(retention_policy_, MonoDelta::FromSeconds(1)); // set TTL to 1 second
  std::vector<ConsensusFrontier> frontiers = {
    CreateConsensusFrontier(now.AddSeconds(-100), kUseDefaultTTL), // discard
    CreateConsensusFrontier(now.AddSeconds(100), kUseDefaultTTL), // keep
    CreateConsensusFrontier(now.AddSeconds(-10000), kUseDefaultTTL), // discard
    CreateConsensusFrontier(now.AddSeconds(10000), kUseDefaultTTL) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kDiscard, FilterDecision::kKeep, FilterDecision::kDiscard, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterBasedOnTableTTLNoValueTTLData) {
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  SetRetentionPolicy(retention_policy_, MonoDelta::FromSeconds(1)); // set TTL to 1 second
  std::vector<ConsensusFrontier> frontiers = {
    CreateConsensusFrontier(now.AddSeconds(-100)), // keep
    CreateConsensusFrontier(now.AddSeconds(100)), // keep
    CreateConsensusFrontier(now.AddSeconds(-10000)), // keep
    CreateConsensusFrontier(now.AddSeconds(10000)) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterBasedOnValueTTLData) {
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  std::vector<ConsensusFrontier> frontiers = {
    // keep (key HT later than kept file)
    CreateConsensusFrontier(now.AddSeconds(1), now.AddSeconds(-100)),
    CreateConsensusFrontier(now, now.AddSeconds(100)), // keep
    CreateConsensusFrontier(now.AddSeconds(-1), now.AddSeconds(-10000)), // discard
    CreateConsensusFrontier(now.AddSeconds(2), now.AddSeconds(10000)) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kDiscard, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterMixTableAndValueTTL) {
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  SetRetentionPolicy(retention_policy_, MonoDelta::FromSeconds(1)); // set TTL to 1 second
  std::vector<ConsensusFrontier> frontiers = {
    // keep (key HT later than kept file)
    CreateConsensusFrontier(now.AddSeconds(-100), kUseDefaultTTL), // discard
    CreateConsensusFrontier(now.AddSeconds(-50), kNoExpiration), // keep
    CreateConsensusFrontier(now.AddSeconds(-20), kUseDefaultTTL), // keep (HT after kept file)
    CreateConsensusFrontier(now.AddSeconds(-10), now.AddSeconds(-10)) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kDiscard, FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterNoTableTTLWithIgnoreValueTTLFlag) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_ignore_value_ttl) = true;
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  // Test with no default time to live (keep all)
  std::vector<ConsensusFrontier> frontiers = {
    CreateConsensusFrontier(now.AddSeconds(-100), kUseDefaultTTL), // keep
    CreateConsensusFrontier(now.AddSeconds(100), kNoExpiration), // keep
    CreateConsensusFrontier(now.AddSeconds(-10000), now.AddSeconds(-100)), // keep
    CreateConsensusFrontier(now.AddSeconds(10000), HybridTime::kInvalid) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterMixTableAndValueTTLWithIgnoreValueTTLFlag) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_ignore_value_ttl) = true;
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  SetRetentionPolicy(retention_policy_, MonoDelta::FromSeconds(1)); // set TTL to 1 second
  std::vector<ConsensusFrontier> frontiers = {
    // keep (key HT later than kept file)
    CreateConsensusFrontier(now.AddSeconds(-100), kUseDefaultTTL), // discard
    CreateConsensusFrontier(now.AddSeconds(-50), kNoExpiration), // discard
    CreateConsensusFrontier(now.AddSeconds(-20), HybridTime::kInvalid), // discard
    CreateConsensusFrontier(now.AddSeconds(-10), now.AddSeconds(-10)) // discard
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kDiscard, FilterDecision::kDiscard,
    FilterDecision::kDiscard, FilterDecision::kDiscard
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterNoTableTTLWithTrustValueTTLFlag) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_value_ttl_overrides_table_ttl) = true;
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  // Test with no default time to live (keep all)
  std::vector<ConsensusFrontier> frontiers = {
    CreateConsensusFrontier(now.AddSeconds(-10), kUseDefaultTTL), // keep
    CreateConsensusFrontier(now.AddSeconds(100), now.AddSeconds(-100)), // keep (after a kept file)
    CreateConsensusFrontier(now.AddSeconds(-10000), now.AddSeconds(-100)), // discard
    CreateConsensusFrontier(now.AddSeconds(-100), HybridTime::kInvalid) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kKeep, FilterDecision::kKeep, FilterDecision::kDiscard, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

TEST_F(ExpirationFilterTest, TestFilterMixTableAndValueTTLWithTrustValueTTLFlag) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_value_ttl_overrides_table_ttl) = true;
  DocDBCompactionFileFilterFactory factory =
      DocDBCompactionFileFilterFactory(retention_policy_, clock_);
  auto now = clock_->Now();
  SetRetentionPolicy(retention_policy_, MonoDelta::FromSeconds(1)); // set TTL to 1 second
  std::vector<ConsensusFrontier> frontiers = {
    // keep (key HT later than kept file)
    CreateConsensusFrontier(now.AddSeconds(-100), kUseDefaultTTL), // discard
    CreateConsensusFrontier(now.AddSeconds(50), kNoExpiration), // keep
    CreateConsensusFrontier(now.AddSeconds(10), now.AddSeconds(-10)), // discard
    CreateConsensusFrontier(now.AddSeconds(30), now.AddSeconds(10)) // keep
  };
  std::vector<FilterDecision> expected_results {
    FilterDecision::kDiscard, FilterDecision::kKeep,
    FilterDecision::kDiscard, FilterDecision::kKeep
  };
  TestFilterFilesAgainstResults(&factory, frontiers, expected_results);
}

}  // namespace docdb
}  // namespace yb
