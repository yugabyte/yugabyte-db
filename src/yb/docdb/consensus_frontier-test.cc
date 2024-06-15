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
#include <google/protobuf/any.pb.h>

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/gutil/casts.h"
#include "yb/rocksdb/metadata.h"
#include "yb/util/test_util.h"

using rocksdb::UpdateUserValueType;

namespace yb {
namespace docdb {

class ConsensusFrontierTest : public YBTest {
};

namespace {

std::string PbToString(const ConsensusFrontierPB& pb) {
  google::protobuf::Any any;
  any.PackFrom(pb);
  ConsensusFrontier frontier;
  CHECK_OK(frontier.FromPB(any));
  return frontier.ToString();
}

static const HistoryCutoff kInvalidCutoffInformation =
    { HybridTime::kInvalid, HybridTime::kInvalid };

}  // anonymous namespace

TEST_F(ConsensusFrontierTest, TestUpdates) {
  {
    ConsensusFrontier frontier;
    EXPECT_TRUE(frontier.Equals(frontier));
    EXPECT_EQ(
        "{ op_id: 0.0 hybrid_time: <invalid> "
        "history_cutoff: { cotables cutoff: <invalid>, primary cutoff: <invalid> } "
        "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
        "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }",
        frontier.ToString());
    EXPECT_TRUE(frontier.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));

    ConsensusFrontier opid1{{0, 1}, HybridTime::kInvalid, kInvalidCutoffInformation};
    EXPECT_TRUE(frontier.IsUpdateValid(opid1, UpdateUserValueType::kLargest));
  }

  {
    ConsensusFrontier frontier{{1, 1}, 1000_usec_ht, { 500_usec_ht, 500_usec_ht }};
    EXPECT_EQ(
        "{ op_id: 1.1 hybrid_time: { physical: 1000 } "
        "history_cutoff: { cotables cutoff: { physical: 500 }, primary cutoff: { physical: 500 } } "
        "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
        "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }",
        frontier.ToString());
    ConsensusFrontier higher_idx{{1, 2}, 1000_usec_ht, { 500_usec_ht, 500_usec_ht }};
    ConsensusFrontier higher_ht{{1, 1}, 1001_usec_ht, { 500_usec_ht, 500_usec_ht }};
    ConsensusFrontier higher_cutoff{{1, 1}, 1000_usec_ht, { 501_usec_ht, 500_usec_ht }};
    ConsensusFrontier higher_idx_lower_ht{{1, 2}, 999_usec_ht, { 500_usec_ht, 500_usec_ht }};

    EXPECT_TRUE(higher_idx.Dominates(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(higher_ht.Dominates(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(higher_cutoff.Dominates(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_idx.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(higher_ht.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(higher_cutoff.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(frontier.Dominates(higher_idx, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.Dominates(higher_ht, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.Dominates(higher_cutoff, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.Dominates(higher_idx, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(frontier.Dominates(higher_ht, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(frontier.Dominates(higher_cutoff, UpdateUserValueType::kSmallest));

    // frontier and higher_idx_lower_ht are "incomparable" according to the "dominates" ordering.
    EXPECT_FALSE(frontier.Dominates(higher_idx_lower_ht, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(frontier.Dominates(higher_idx_lower_ht, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_idx_lower_ht.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(higher_idx_lower_ht.Dominates(frontier, UpdateUserValueType::kLargest));

    EXPECT_TRUE(frontier.IsUpdateValid(higher_idx, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(higher_ht, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_idx.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_ht.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.IsUpdateValid(higher_idx, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(frontier.IsUpdateValid(higher_ht, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(higher_idx.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(higher_ht.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));

    EXPECT_FALSE(higher_idx_lower_ht.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.IsUpdateValid(higher_idx_lower_ht, UpdateUserValueType::kSmallest));

    // It is OK if a later compaction runs at a lower history_cutoff.
    EXPECT_TRUE(frontier.IsUpdateValid(higher_cutoff, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(higher_cutoff, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(higher_cutoff.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(higher_cutoff.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));

    // Zero OpId should be considered as an undefined value, not causing any errors.
    ConsensusFrontier zero_op_id{{0, 0}, HybridTime::kInvalid, kInvalidCutoffInformation};
    EXPECT_TRUE(frontier.IsUpdateValid(zero_op_id, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(zero_op_id, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(zero_op_id.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(zero_op_id.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));
  }

  ConsensusFrontierPB pb;
  pb.mutable_op_id()->set_term(0);
  pb.mutable_op_id()->set_index(0);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 0.0 hybrid_time: <min> "
      "history_cutoff: { cotables cutoff: <invalid>, primary cutoff: <invalid> } "
      "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
      "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }");

  pb.mutable_op_id()->set_term(2);
  pb.mutable_op_id()->set_index(3);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 2.3 hybrid_time: <min> "
      "history_cutoff: { cotables cutoff: <invalid>, primary cutoff: <invalid> } "
      "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
      "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }");

  pb.set_hybrid_time(100000);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 2.3 hybrid_time: { physical: 24 logical: 1696 } "
      "history_cutoff: { cotables cutoff: <invalid>, primary cutoff: <invalid> } "
      "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
      "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }");

  pb.set_primary_cutoff_ht(200000);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 2.3 hybrid_time: { physical: 24 logical: 1696 } "
      "history_cutoff: { cotables cutoff: <invalid>, "
      "primary cutoff: { physical: 48 logical: 3392 } } "
      "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
      "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }");

  pb.set_cotables_cutoff_ht(200000);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 2.3 hybrid_time: { physical: 24 logical: 1696 } "
      "history_cutoff: { cotables cutoff: { physical: 48 logical: 3392 }, "
      "primary cutoff: { physical: 48 logical: 3392 } } "
      "max_value_level_ttl_expiration_time: <invalid> primary_schema_version: <nullopt> "
      "cotable_schema_versions: [] global_filter: <invalid> cotables_filter: [] }");
}

TEST_F(ConsensusFrontierTest, TestUpdateExpirationTime) {
  const HybridTime smallHT = 1000_usec_ht;
  const HybridTime largeHT = 2000_usec_ht;
  const HybridTime maxHT = HybridTime::kMax;

  // Three frontiers with the same op_id, ht, and history_cuttoff,
  // but different expiration times.
  ConsensusFrontier noExpiry{{1, 1}, 1000_usec_ht, { 500_usec_ht, 500_usec_ht }};
  EXPECT_EQ(noExpiry.max_value_level_ttl_expiration_time(), HybridTime::kInvalid);

  ConsensusFrontier smallExpiry{{1, 1}, 1000_usec_ht, { 500_usec_ht, 500_usec_ht }};
  smallExpiry.set_max_value_level_ttl_expiration_time(smallHT);
  EXPECT_EQ(smallExpiry.max_value_level_ttl_expiration_time(), smallHT);

  ConsensusFrontier largeExpiry{{1, 1}, 1000_usec_ht, { 500_usec_ht, 500_usec_ht }};
  largeExpiry.set_max_value_level_ttl_expiration_time(largeHT);
  EXPECT_EQ(largeExpiry.max_value_level_ttl_expiration_time(), largeHT);

  ConsensusFrontier maxExpiry{{1, 1}, 1000_usec_ht, { 500_usec_ht, 500_usec_ht }};
  maxExpiry.set_max_value_level_ttl_expiration_time(maxHT);
  EXPECT_EQ(maxExpiry.max_value_level_ttl_expiration_time(), maxHT);

  // Update no expiration frontier with another invalid expiration.
  auto expiryClone = noExpiry.Clone();
  ConsensusFrontier noExpiry2 = {{2, 2}, 2000_usec_ht, { 1000_usec_ht, 1000_usec_ht }};
  expiryClone->Update(noExpiry2, UpdateUserValueType::kLargest);
  auto consensusClone = down_cast<ConsensusFrontier&>(*expiryClone);
  EXPECT_EQ(consensusClone.max_value_level_ttl_expiration_time(), HybridTime::kInvalid);

  // Update frontier with no expiration with one with an expiration.
  expiryClone->Update(smallExpiry, UpdateUserValueType::kLargest);
  consensusClone = down_cast<ConsensusFrontier&>(*expiryClone);
  EXPECT_EQ(consensusClone.max_value_level_ttl_expiration_time(), smallHT);

  // Update same frontier with a larger expiration
  expiryClone->Update(largeExpiry, UpdateUserValueType::kLargest);
  consensusClone = down_cast<ConsensusFrontier&>(*expiryClone);
  EXPECT_EQ(consensusClone.max_value_level_ttl_expiration_time(), largeHT);

  // Try to update same frontier with the smaller expiration. Should keep the higher expiration.
  expiryClone->Update(smallExpiry, UpdateUserValueType::kLargest);
  consensusClone = down_cast<ConsensusFrontier&>(*expiryClone);
  EXPECT_EQ(consensusClone.max_value_level_ttl_expiration_time(), largeHT);

  // Update with the maximum expiration.
  expiryClone->Update(maxExpiry, UpdateUserValueType::kLargest);
  consensusClone = down_cast<ConsensusFrontier&>(*expiryClone);
  EXPECT_EQ(consensusClone.max_value_level_ttl_expiration_time(), maxHT);

  // Update with another invalid expiration time.
  expiryClone->Update(noExpiry2, UpdateUserValueType::kLargest);
  consensusClone = down_cast<ConsensusFrontier&>(*expiryClone);
  EXPECT_EQ(consensusClone.max_value_level_ttl_expiration_time(), maxHT);
}

}  // namespace docdb
}  // namespace yb
