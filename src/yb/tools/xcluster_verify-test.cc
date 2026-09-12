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

#include <vector>

#include "yb/client/schema.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/tools/table_hash.h"
#include "yb/tools/xcluster_verify.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb::tools {

class XClusterVerifyTest : public YBTest {};

// kTryAgain is the verdict a driver retries; everything else it treats as a problem to report.
TEST_F(XClusterVerifyTest, ClassifyRetryableStatuses) {
  ASSERT_EQ(
      ClassifyStatus(STATUS(SnapshotTooOld, "history cutoff"), XClusterClassifyContext::kHash),
      XClusterVerifyResult::kTryAgain);
  ASSERT_EQ(
      ClassifyStatus(STATUS(TimedOut, "deadline"), XClusterClassifyContext::kHash),
      XClusterVerifyResult::kTryAgain);
  // NotFound is a catalog difference only when a catalog fetch produced it; from a hash it is a
  // missing leader.
  ASSERT_EQ(
      ClassifyStatus(STATUS(NotFound, "Leader replica not found"), XClusterClassifyContext::kHash),
      XClusterVerifyResult::kError);
}

// A replica still catching up to the read time is lag, and a driver has to be told to retry it.
// The pair below is the whole point: the same TryAgain code means kError without the tserver error
// code and kTryAgain with it, so the classification rests on the code rather than on the sentence
// the two statuses share.
TEST_F(XClusterVerifyTest, ClassifyReadTimeNotReached) {
  const auto message = "Requested read time 1 is not yet safe on this replica";
  ASSERT_EQ(
      ClassifyStatus(STATUS(TryAgain, message), XClusterClassifyContext::kHash),
      XClusterVerifyResult::kError);
  ASSERT_EQ(
      ClassifyStatus(
          STATUS(
              TryAgain, message,
              tserver::TabletServerError(tserver::TabletServerErrorPB::READ_TIME_NOT_REACHED)),
          XClusterClassifyContext::kHash),
      XClusterVerifyResult::kTryAgain);
}

TEST_F(XClusterVerifyTest, ClassifySchemaNotFound) {
  ASSERT_EQ(
      ClassifyStatus(STATUS(NotFound, "Table not found"), XClusterClassifyContext::kSchema),
      XClusterVerifyResult::kSchemaMismatch);
  // Same text, opposite context.
  ASSERT_EQ(
      ClassifyStatus(
          STATUS(IllegalState, "table does not exist"), XClusterClassifyContext::kHash),
      XClusterVerifyResult::kError);
}

// ComputeTableXorHash contacts exactly the tablets this says overlap, and the two directions of
// error are not equally bad: a wrong false silently drops that tablet's rows from the total and
// turns a matching pair into a kDiverged report, while a wrong true only wastes an RPC.
//
// Arguments are (tablet_start, tablet_end, range_start, range_end). Both ranges are half open, and
// an empty bound means unbounded rather than the empty string. PartitionRangeOverlaps ships with
// get_table_hash; this is the only place it is exercised in isolation.
TEST_F(XClusterVerifyTest, PartitionRangeOverlapsBounds) {
  ASSERT_TRUE(PartitionRangeOverlaps("a", "c", "b", "d"));
  // Touching but not overlapping. Off by one here would pull in a neighbour for every bound landing
  // on a tablet boundary, which is where bounds usually land.
  ASSERT_FALSE(PartitionRangeOverlaps("a", "b", "b", "c"));
  ASSERT_TRUE(PartitionRangeOverlaps("", "c", "b", ""));
  // The mirror, so neither empty bound is handled correctly by accident while the other is not.
  ASSERT_TRUE(PartitionRangeOverlaps("a", "", "", "b"));
  // A table that was never split. An empty bound compared as a literal empty string fails here.
  ASSERT_TRUE(PartitionRangeOverlaps("", "", "x", "y"));
}

// The servers salt each value's hash with the id of the column it came from, so a pair whose ids
// differ cannot produce comparable hashes.
TEST_F(XClusterVerifyTest, FingerprintDistinguishesColumnIds) {
  auto build = [](int32_t first_id, int32_t second_id) {
    Schema schema(
        {ColumnSchema("k", QLType::Create(DataType::INT32), ColumnKind::HASH),
         ColumnSchema("v", QLType::Create(DataType::STRING), ColumnKind::VALUE, Nullable::kTrue)},
        {ColumnId(first_id), ColumnId(second_id)});
    return client::YBSchema(schema);
  };
  const auto low = build(10, 11);
  const auto high = build(20, 21);
  // Non-vacuity: these schemas carry ids, so the fingerprint reads real ids rather than rejecting
  // them, and the two below differ for the reason under test.
  ASSERT_TRUE(client::internal::GetSchema(low).has_column_ids());
  ASSERT_EQ(ASSERT_RESULT(BuildSchemaFingerprint(low)).columns[0].id, 10);

  ASSERT_NE(
      ASSERT_RESULT(BuildSchemaFingerprint(low)), ASSERT_RESULT(BuildSchemaFingerprint(high)));
  ASSERT_EQ(
      ASSERT_RESULT(BuildSchemaFingerprint(low)),
      ASSERT_RESULT(BuildSchemaFingerprint(build(10, 11))));
}

// The one field guarding against a false match rather than a false alarm, and nothing else here
// would notice it leaving the fingerprint.
TEST_F(XClusterVerifyTest, FingerprintDistinguishesSortingType) {
  auto build = [](ColumnKind range_kind) {
    Schema schema(
        {ColumnSchema("h", QLType::Create(DataType::INT32), ColumnKind::HASH),
         ColumnSchema("r", QLType::Create(DataType::INT32), range_kind)},
        {ColumnId(10), ColumnId(11)});
    return client::YBSchema(schema);
  };
  const auto ascending =
      ASSERT_RESULT(BuildSchemaFingerprint(build(ColumnKind::RANGE_ASC_NULL_FIRST)));
  const auto descending =
      ASSERT_RESULT(BuildSchemaFingerprint(build(ColumnKind::RANGE_DESC_NULL_FIRST)));
  // Both kinds are keys and neither is a hash key, so the key flags cannot be what separates them:
  // without sorting_type the fingerprint would call these equal.
  ASSERT_EQ(ascending.columns[1].is_key, descending.columns[1].is_key);
  ASSERT_EQ(ascending.columns[1].is_hash_key, descending.columns[1].is_hash_key);
  ASSERT_NE(ascending, descending);
}

// Substituting positions for absent ids would make two sides whose ids differ fingerprint equal,
// hiding the difference the fingerprint is here to surface. YBSchemaBuilder is the one producer of
// an id-less schema, so it is what the rejection is tested with.
TEST_F(XClusterVerifyTest, FingerprintRejectsSchemaWithoutColumnIds) {
  client::YBSchemaBuilder builder;
  builder.AddColumn("h")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn("v")->Type(DataType::STRING)->Nullable();
  client::YBSchema schema;
  ASSERT_OK(builder.Build(&schema));

  ASSERT_FALSE(client::internal::GetSchema(schema).has_column_ids());
  ASSERT_NOK(BuildSchemaFingerprint(schema));
}

// The three fields below sit outside column identity, and none is compared by the checks xCluster
// setup applies: TableProperties::Equivalent skips partitioning_version and default_time_to_live,
// and ColumnSchema::Equals does not look at is_static. A pair differing in one is therefore not
// ruled out when replication is established.
TEST_F(XClusterVerifyTest, FingerprintDistinguishesPartitioningVersion) {
  auto build = [](uint32_t partitioning_version) {
    TableProperties properties;
    properties.set_partitioning_version(partitioning_version);
    Schema schema(
        {ColumnSchema("h", QLType::Create(DataType::INT32), ColumnKind::HASH),
         ColumnSchema("r", QLType::Create(DataType::INT32), ColumnKind::RANGE_ASC_NULL_FIRST)},
        {ColumnId(10), ColumnId(11)}, properties);
    return client::YBSchema(schema);
  };
  ASSERT_NE(
      ASSERT_RESULT(BuildSchemaFingerprint(build(0))),
      ASSERT_RESULT(BuildSchemaFingerprint(build(1))));
  ASSERT_EQ(
      ASSERT_RESULT(BuildSchemaFingerprint(build(1))),
      ASSERT_RESULT(BuildSchemaFingerprint(build(1))));
}

TEST_F(XClusterVerifyTest, FingerprintDistinguishesDefaultTimeToLive) {
  auto build = [](uint64_t ttl_ms) {
    TableProperties properties;
    properties.SetDefaultTimeToLive(ttl_ms);
    Schema schema(
        {ColumnSchema("h", QLType::Create(DataType::INT32), ColumnKind::HASH),
         ColumnSchema("v", QLType::Create(DataType::INT32), ColumnKind::VALUE, Nullable::kTrue)},
        {ColumnId(10), ColumnId(11)}, properties);
    return client::YBSchema(schema);
  };
  ASSERT_NE(
      ASSERT_RESULT(BuildSchemaFingerprint(build(1000))),
      ASSERT_RESULT(BuildSchemaFingerprint(build(2000))));
}

TEST_F(XClusterVerifyTest, FingerprintDistinguishesStaticColumns) {
  auto build = [](bool is_static) {
    Schema schema(
        {ColumnSchema("h", QLType::Create(DataType::INT32), ColumnKind::HASH),
         ColumnSchema("v", QLType::Create(DataType::INT32), ColumnKind::VALUE, Nullable::kTrue,
                      is_static)},
        {ColumnId(10), ColumnId(11)});
    return client::YBSchema(schema);
  };
  // A static column's value lives under the hash-key-only DocKey, so the two sides hold the same
  // value in different places.
  ASSERT_NE(
      ASSERT_RESULT(BuildSchemaFingerprint(build(false))),
      ASSERT_RESULT(BuildSchemaFingerprint(build(true))));
}

// A dropped table does not always arrive as a clean NotFound: when a schema fetch retries and then
// hits its deadline, Rpc::Finished (rpc.cc) folds the last error's text into a fresh TimedOut, so
// the answer survives as prose while the inner NotFound code does not.
//
// The status is built by hand, so this pins the classifier's half of that contract and not the RPC
// layer's.
TEST_F(XClusterVerifyTest, ClassifyMissingTableWrappedInTimeout) {
  const auto wrapped = STATUS(
      TimedOut,
      "GetTableSchemaRpc passed its deadline: Not found: The object does not exist: table with id "
      "0000300000003000800000000000400a");
  ASSERT_EQ(
      ClassifyStatus(wrapped, XClusterClassifyContext::kSchema),
      XClusterVerifyResult::kSchemaMismatch);
  // A timeout naming no missing object is still lag, which keeps a slow master retryable.
  ASSERT_EQ(
      ClassifyStatus(STATUS(TimedOut, "passed its deadline"), XClusterClassifyContext::kSchema),
      XClusterVerifyResult::kTryAgain);
  // Only a catalog fetch is read this way.
  ASSERT_EQ(
      ClassifyStatus(wrapped, XClusterClassifyContext::kHash), XClusterVerifyResult::kTryAgain);
}

}  // namespace yb::tools
