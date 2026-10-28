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

#include <atomic>
#include <utility>
#include <vector>

#include <rapidjson/document.h>

#include "yb/client/schema.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/tools/xcluster_verify.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"
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

namespace {

SchemaFingerprint MakeFingerprint(int32_t id, const std::string& type) {
  SchemaFingerprint fp;
  fp.columns.push_back(ColumnFingerprint{id, type, /* is_key = */ true, /* is_hash_key = */ true,
                                         /* is_nullable = */ false});
  return fp;
}

SliceVerifyRequest MakeRequest() {
  SliceVerifyRequest req;
  req.source_table_id = "src";
  req.target_table_id = "tgt";
  req.read_ht = 99;
  return req;
}

}  // namespace

// The happy path, and the control every other slice test is a deviation from: the catalogs agree,
// both sides hash the same, and the verdict is kMatch.
TEST_F(XClusterVerifyTest, VerifySliceMatch) {
  // One fingerprint for all four schema fetches, so the two sides agree with each other at both
  // ends of the sandwich and each side also agrees with its own pre-hash self.
  auto fp = MakeFingerprint(0, "int32");
  // Neither side names a hash scheme. Left unset it is nullopt, which is usable and comparable
  // against anything, so the scheme gates pass without this test having to choose a version. The
  // versions themselves are covered by HashSchemeDecidesTheVerdictBeforeTheHashesDo.
  auto hash = TableHashTotals{.xor_hash = 7, .row_count = 10, .read_ht = 99};
  auto outcome = VerifyXClusterSlice(
      MakeRequest(),
      [&](const TableId&) { return fp; },
      [&](const TableId&) { return fp; },
      [&](const TableId&, uint64_t, Slice, Slice, uint64_t) { return hash; },
      [&](const TableId&, uint64_t, Slice, Slice, uint64_t) { return hash; });
  ASSERT_EQ(outcome.result, XClusterVerifyResult::kMatch);
  // The totals survive into the outcome instead of being dropped once the verdict is settled.
  ASSERT_TRUE(outcome.source.has_value());
  ASSERT_EQ(outcome.source->xor_hash, 7);
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

  int hash_calls = 0;
  auto hash_fn = [&](const TableId&, uint64_t, Slice, Slice, uint64_t) {
    ++hash_calls;
    return TableHashTotals{
        .xor_hash = 1, .row_count = 10, .read_ht = 99, .hash_scheme_version = 1};
  };
  auto outcome = VerifyXClusterSlice(
      MakeRequest(),
      [&](const TableId&) { return BuildSchemaFingerprint(low); },
      [&](const TableId&) { return BuildSchemaFingerprint(high); },
      hash_fn, hash_fn);
  ASSERT_EQ(outcome.result, XClusterVerifyResult::kSchemaMismatch);
  ASSERT_EQ(hash_calls, 0);
}

TEST_F(XClusterVerifyTest, SchemaMismatchAfterMatchingHash) {
  auto matching = MakeFingerprint(0, "int32");
  auto altered = MakeFingerprint(1, "int32");
  int schema_calls = 0;
  auto outcome = VerifyXClusterSlice(
      MakeRequest(),
      [&](const TableId&) { return matching; },
      [&](const TableId&) {
        ++schema_calls;
        // Only target fetches reach this counter, and there are exactly two of them: one in the
        // pre-hash schema compare and one in the post-hash check. Alter on the second so the
        // drift appears only after the hashes have already matched.
        return schema_calls == 2 ? altered : matching;
      },
      [&](const TableId&, uint64_t, Slice, Slice, uint64_t) {
        return TableHashTotals{.xor_hash = 1, .row_count = 10, .read_ht = 99};
      },
      [&](const TableId&, uint64_t, Slice, Slice, uint64_t) {
        return TableHashTotals{.xor_hash = 1, .row_count = 10, .read_ht = 99};
      });
  ASSERT_EQ(outcome.result, XClusterVerifyResult::kSchemaMismatch);
  ASSERT_TRUE(outcome.source.has_value());
}

// The case the sandwich exists for, and the only one that needs it: the same DDL lands on both
// clusters mid-hash, which under automatic DDL replication is how propagation ordinarily looks.
// Both closing fetches agree with each other, so every side-to-side comparison passes; what catches
// it is each side being compared against its own pre-hash reading. Delete that comparison and this
// pair reports kMatch over rows hashed under two different schemas.
TEST_F(XClusterVerifyTest, SchemaMismatchWhenBothSidesChangeMidHash) {
  auto matching = MakeFingerprint(0, "int32");
  auto altered = MakeFingerprint(0, "string");
  int source_calls = 0;
  int target_calls = 0;
  // The closing fetch is the second on each side: one before the hash, one after.
  auto fetch = [&altered, &matching](int* calls) {
    return [calls, &altered, &matching](const TableId&) -> Result<SchemaFingerprint> {
      ++*calls;
      return *calls == 2 ? altered : matching;
    };
  };
  auto hash = [](const TableId&, uint64_t, Slice, Slice, uint64_t) {
    return TableHashTotals{
        .xor_hash = 1, .row_count = 10, .read_ht = 99, .hash_scheme_version = 1};
  };
  auto outcome = VerifyXClusterSlice(
      MakeRequest(), fetch(&source_calls), fetch(&target_calls), hash, hash);

  ASSERT_EQ(outcome.result, XClusterVerifyResult::kSchemaMismatch);
  // Both sides were read twice, and both closing readings are the same `altered` object, so no
  // side-to-side comparison could have caught this. Were they unequal the test would pass for the
  // reason the case above already covers.
  ASSERT_EQ(source_calls, 2);
  ASSERT_EQ(target_calls, 2);
  ASSERT_STR_CONTAINS(outcome.detail, "schema changed while hashing");
  // The hashes agreed, and the verdict says so while still refusing to call it a match.
  ASSERT_STR_CONTAINS(outcome.detail, "hash=kMatch");
}

// A closing fetch that fails cannot rule out a mid-hash DDL, so it denies kMatch. Over a kDiverged
// hash it must not land on kTryAgain: kTryAgain tells the sweep to abandon the range and come back,
// and it would be abandoning a table already known to disagree. kError keeps the bad news.
TEST_F(XClusterVerifyTest, FailedClosingFetchOverDivergedBecomesInfra) {
  auto fp = MakeFingerprint(0, "int32");
  int target_calls = 0;
  auto outcome = VerifyXClusterSlice(
      MakeRequest(),
      [&fp](const TableId&) -> Result<SchemaFingerprint> { return fp; },
      [&](const TableId&) -> Result<SchemaFingerprint> {
        // TimedOut on its own classifies as kTryAgain, which is the promotion being tested.
        if (++target_calls == 2) {
          return STATUS(TimedOut, "master deadline");
        }
        return fp;
      },
      [](const TableId&, uint64_t, Slice, Slice, uint64_t) {
        return TableHashTotals{
            .xor_hash = 1, .row_count = 10, .read_ht = 99, .hash_scheme_version = 1};
      },
      // Never agrees, so the hash verdict is kDiverged before the closing fetch is reached.
      [](const TableId&, uint64_t, Slice, Slice, uint64_t) {
        return TableHashTotals{
            .xor_hash = 2, .row_count = 10, .read_ht = 99, .hash_scheme_version = 1};
      });

  ASSERT_EQ(outcome.result, XClusterVerifyResult::kError);
  ASSERT_STR_CONTAINS(outcome.detail, "hash=kDiverged");
}

TEST_F(XClusterVerifyTest, VerifySliceMaxRowsFreezesTargetEnd) {
  auto fp = MakeFingerprint(0, "int32");
  auto req = MakeRequest();
  req.start_key = "a";
  req.end_key = "z";
  req.max_rows = 5;
  std::string last_target_end;
  uint64_t last_source_max = 0;
  uint64_t last_target_max = 99;
  auto outcome = VerifyXClusterSlice(
      req,
      [&](const TableId&) { return fp; },
      [&](const TableId&) { return fp; },
      [&](const TableId&, uint64_t, Slice, Slice, uint64_t max_rows) {
        last_source_max = max_rows;
        TableHashTotals hash{.xor_hash = 1, .row_count = 5, .read_ht = 99};
        hash.next_key = "m";
        return hash;
      },
      [&](const TableId&, uint64_t, Slice, Slice end, uint64_t max_rows) {
        last_target_end = end.ToBuffer();
        last_target_max = max_rows;
        return TableHashTotals{.xor_hash = 1, .row_count = 5, .read_ht = 99};
      });
  ASSERT_EQ(outcome.result, XClusterVerifyResult::kMatch);
  ASSERT_EQ(last_source_max, 5);
  ASSERT_EQ(last_target_max, 0);
  ASSERT_EQ(last_target_end, "m");
  ASSERT_EQ(outcome.end_key, "m");
}

// The hash scheme gate as the table it is: the verdict is a function of the two sides' schemes, and
// the hashes only get a say once the schemes allow it.
//
// The cases where the hashes agree are the ones worth having. HashSchemesComparable says yes to
// 0 == 0, so nothing but the usability check stands between an incomparable agreement and a kMatch
// vouching for data the tool never distinguished. The cases where both sides are upgraded are the
// other half: without them the gate could buy upgrade safety by disabling the command outright.
//
TEST_F(XClusterVerifyTest, HashSchemeDecidesTheVerdictBeforeTheHashesDo) {
  struct Case {
    const char* name;
    uint32_t source_scheme;
    uint32_t target_scheme;
    uint64_t source_hash;
    uint64_t target_hash;
    XClusterVerifyResult expected;
    const char* detail;
    int expected_source_calls;
  };
  const std::vector<Case> cases = {
      {"neither universe upgraded, hashes differ", 0, 0, 1, 2, XClusterVerifyResult::kError,
       "unusable row hash scheme", 1},
      {"neither universe upgraded, hashes agree", 0, 0, 7, 7, XClusterVerifyResult::kError,
       "unusable row hash scheme", 1},
      {"source still on an old binary", 0, 1, 7, 7, XClusterVerifyResult::kError,
       "unusable row hash scheme", 1},
      {"target still on an old binary", 1, 0, 7, 7, XClusterVerifyResult::kError,
       "unusable row hash scheme", 1},
      {"upgrade in flight, hashes differ", 2, 1, 1, 2, XClusterVerifyResult::kError,
       "hash scheme mismatch", 1},
      {"upgrade in flight, hashes coincide", 1, 2, 7, 7, XClusterVerifyResult::kError,
       "hash scheme mismatch", 1},
      {"both upgraded, hashes agree", 1, 1, 7, 7, XClusterVerifyResult::kMatch, "", 1},
      {"both upgraded, hashes differ", 1, 1, 1, 2, XClusterVerifyResult::kDiverged, "", 1},
  };

  auto fp = MakeFingerprint(0, "int32");
  for (const auto& test_case : cases) {
    int source_calls = 0;
    auto outcome = VerifyXClusterSlice(
        MakeRequest(),
        [&](const TableId&) { return fp; },
        [&](const TableId&) { return fp; },
        [&](const TableId&, uint64_t, Slice, Slice, uint64_t) {
          ++source_calls;
          return TableHashTotals{
              .xor_hash = test_case.source_hash, .row_count = 10, .read_ht = 99,
              .hash_scheme_version = test_case.source_scheme};
        },
        [&](const TableId&, uint64_t, Slice, Slice, uint64_t) {
          return TableHashTotals{
              .xor_hash = test_case.target_hash, .row_count = 10, .read_ht = 99,
              .hash_scheme_version = test_case.target_scheme};
        });
    SCOPED_TRACE(test_case.name);
    ASSERT_EQ(outcome.result, test_case.expected);
    ASSERT_EQ(source_calls, test_case.expected_source_calls);
    if (*test_case.detail) {
      ASSERT_STR_CONTAINS(outcome.detail, test_case.detail);
    }
    // Both sides hashed in every case here, so their totals and the schemes they hashed under are
    // reported for diagnosis rather than dropped along with the verdict.
    ASSERT_TRUE(outcome.source.has_value());
    ASSERT_TRUE(outcome.target.has_value());
    ASSERT_EQ(outcome.source->hash_scheme_version, test_case.source_scheme);
    ASSERT_EQ(outcome.target->hash_scheme_version, test_case.target_scheme);
    // The action an unusable scheme calls for is an upgrade, not a retry, so the message says so.
    if (std::string(test_case.detail) == "unusable row hash scheme") {
      ASSERT_STR_CONTAINS(outcome.detail, "Upgrade both universes");
    }
  }
}

// The whole point of resolving centrally: a request that names no read time must still produce one
// concrete time that BOTH sides hash at. If each side were left to resolve 0 against its own
// cluster's clock the two clusters would pin different instants, and every write in between would
// surface as kDiverged. Asserting on the values the callbacks actually received is what makes that
// impossible to reintroduce.
TEST_F(XClusterVerifyTest, UnpinnedReadTimeIsResolvedOnceForBothSides) {
  auto fp = MakeFingerprint(0, "int32");
  auto req = MakeRequest();
  req.read_ht = 0;
  int resolve_calls = 0;
  std::vector<uint64_t> source_read_hts;
  std::vector<uint64_t> target_read_hts;
  // Every call hands back a different time, so a second resolution cannot coincidentally agree
  // with the first and let an independently-resolving side slip through.
  auto outcome = VerifyXClusterSlice(
      req,
      [&](const TableId&) { return fp; },
      [&](const TableId&) { return fp; },
      [&](const TableId&, uint64_t read_ht, Slice, Slice, uint64_t) {
        source_read_hts.push_back(read_ht);
        return TableHashTotals{.xor_hash = 1, .row_count = 10, .read_ht = read_ht};
      },
      [&](const TableId&, uint64_t read_ht, Slice, Slice, uint64_t) {
        target_read_hts.push_back(read_ht);
        return TableHashTotals{.xor_hash = 1, .row_count = 10, .read_ht = read_ht};
      },
      [&]() -> Result<uint64_t> { return 7000 + ++resolve_calls; });
  ASSERT_EQ(outcome.result, XClusterVerifyResult::kMatch);
  ASSERT_EQ(resolve_calls, 1);
  ASSERT_EQ(source_read_hts, std::vector<uint64_t>{7001});
  ASSERT_EQ(target_read_hts, std::vector<uint64_t>{7001});
  ASSERT_EQ(outcome.read_ht, 7001);
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

// The `result` strings are the command's output contract, documented in yb-admin.md and parsed by
// whatever drives a sweep. Asserted on emitted JSON rather than on ToCString, so that renaming an
// enumerator fails here instead of quietly changing what a driver reads.
TEST_F(XClusterVerifyTest, JsonResultNamesArePinned) {
  const std::vector<std::pair<XClusterVerifyResult, std::string>> expected = {
      {XClusterVerifyResult::kMatch, "kMatch"},
      {XClusterVerifyResult::kDiverged, "kDiverged"},
      {XClusterVerifyResult::kTryAgain, "kTryAgain"},
      {XClusterVerifyResult::kError, "kError"},
      {XClusterVerifyResult::kSchemaMismatch, "kSchemaMismatch"},
  };
  for (const auto& [result, name] : expected) {
    SliceVerifyOutcome outcome;
    outcome.result = result;
    const auto json = SliceVerifyOutcomeToJson(outcome);
    rapidjson::Document document;
    ASSERT_FALSE(document.Parse<0>(json.c_str()).HasParseError()) << json;
    ASSERT_TRUE(document.HasMember("result")) << json;
    ASSERT_EQ(std::string(document["result"].GetString()), name) << json;
  }
}

namespace {

std::vector<TablePairToVerify> MakePairs(int count) {
  std::vector<TablePairToVerify> pairs;
  for (int i = 1; i <= count; ++i) {
    pairs.push_back(
        TablePairToVerify{
            .source_table_id = Format("s$0", i), .target_table_id = Format("t$0", i)});
  }
  return pairs;
}

// The request carries its keys decoded, so a fake recognising the position the sweep handed it
// converts back, the same way a real outcome reports them.
std::string StartKeyHex(const SliceVerifyRequest& req) {
  return strings::b2a_hex(req.start_key);
}

// Each table as one open-ended range, the shape a single-tablet table produces.
Result<std::vector<KeyRange>> WholeTable(const TableId&) {
  return std::vector<KeyRange>{KeyRange{}};
}

// A slice outcome shaped the way VerifyXClusterSlice shapes one: end_key is what was hashed, so an
// empty end means the slice ran to the end of its table. Taken as raw bytes with the hex derived,
// as the real thing does, so a fake cannot present the two forms disagreeing.
SliceVerifyOutcome MakeOutcome(
    const SliceVerifyRequest& req, XClusterVerifyResult result, const std::string& end_key) {
  SliceVerifyOutcome outcome;
  outcome.result = result;
  outcome.source_table_id = req.source_table_id;
  outcome.target_table_id = req.target_table_id;
  outcome.start_key_hex = StartKeyHex(req);
  outcome.end_key = end_key;
  return outcome;
}

}  // namespace

// A table that exists on one cluster only is the divergence comparing pairs cannot see: it is in no
// pair, so every slice can match while the two databases plainly differ. The caller finds it, not
// the sweep, so what is asserted here is that a verdict reached outside the slice loop weighs on
// the summary by the same ordering the slices do -- and that a clean run of matching slices cannot
// come back as kMatch once one has been recorded.
TEST_F(XClusterVerifyTest, UnpairedTablesOverrideAMatchingSweep) {
  auto verify = [](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    return MakeOutcome(req, XClusterVerifyResult::kMatch, "");
  };
  auto summary =
      ASSERT_RESULT(VerifyXClusterTablePairs(
          MakePairs(2), GroupVerifyOptions(), verify, WholeTable));
  ASSERT_EQ(summary.result, XClusterVerifyResult::kMatch);

  summary.unpaired.push_back("public.ghost (abc) is on the target and not the source");
  ApplyVerdict(&summary, XClusterVerifyResult::kSchemaMismatch);
  ASSERT_EQ(summary.result, XClusterVerifyResult::kSchemaMismatch);
  ASSERT_STR_CONTAINS(GroupVerifySummaryToJson(summary), "\"unpaired\":[\"public.ghost");

  // Worse news still wins: an unpaired table must not talk a divergence down into a schema
  // complaint, which reads as a catalog problem rather than as data that is gone.
  summary.result = XClusterVerifyResult::kDiverged;
  ApplyVerdict(&summary, XClusterVerifyResult::kSchemaMismatch);
  ASSERT_EQ(summary.result, XClusterVerifyResult::kDiverged);

  // Nothing unpaired is said by omission, like the other conditional fields.
  GroupVerifySummary clean;
  ASSERT_STR_NOT_CONTAINS(GroupVerifySummaryToJson(clean), "unpaired");
}

// The sweep answers with one verdict, and it must be the one the reader has to act on: divergence
// outranks every excuse for not having verified something.
// The continuation key comes from a tserver and a range's slice loop has no bound of its own, so
// a key that does not advance would spin forever emitting slices.
TEST_F(XClusterVerifyTest, SweepRefusesAContinuationKeyThatDoesNotAdvance) {
  int slices = 0;
  // Hands back the same position every time, so the second slice is asked to start where the first
  // one already ended.
  auto verify = [&slices](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    ++slices;
    return MakeOutcome(req, XClusterVerifyResult::kMatch, "\xaa");
  };

  GroupVerifyOptions options;
  options.max_rows = 1;
  const auto result = VerifyXClusterTablePairs(MakePairs(1), options, verify, WholeTable);
  ASSERT_FALSE(result.ok());
  ASSERT_STR_CONTAINS(result.status().ToString(), "did not advance");
  ASSERT_EQ(slices, 2);
}

TEST_F(XClusterVerifyTest, SweepReportsTheWorstVerdict) {
  auto sweep_with = [](const std::vector<XClusterVerifyResult>& results) {
    size_t index = 0;
    auto verify = [&results, &index](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
      return MakeOutcome(req, results[index++], "");
    };
    return VerifyXClusterTablePairs(
        MakePairs(narrow_cast<int>(results.size())), GroupVerifyOptions(), verify, WholeTable);
  };

  ASSERT_EQ(
      ASSERT_RESULT(sweep_with({XClusterVerifyResult::kTryAgain, XClusterVerifyResult::kDiverged,
                                XClusterVerifyResult::kError}))
          .result,
      XClusterVerifyResult::kDiverged);
  ASSERT_EQ(
      ASSERT_RESULT(sweep_with({XClusterVerifyResult::kTryAgain, XClusterVerifyResult::kError}))
          .result,
      XClusterVerifyResult::kError);
  ASSERT_EQ(
      ASSERT_RESULT(
          sweep_with({XClusterVerifyResult::kTryAgain, XClusterVerifyResult::kSchemaMismatch}))
          .result,
      XClusterVerifyResult::kSchemaMismatch);
}

namespace {

// Three ranges, the shape a three-tablet table produces: open at both ends, closed in the middle.
Result<std::vector<KeyRange>> ThreeRanges(const TableId&) {
  return std::vector<KeyRange>{
      KeyRange{.start = "", .end = "\x20"},
      KeyRange{.start = "\x20", .end = "\x40"},
      KeyRange{.start = "\x40", .end = ""}};
}

// A slice that hashed its whole range, which is what a scan no row cap stopped reports.
Result<SliceVerifyOutcome> HashedWholeRange(const SliceVerifyRequest& req) {
  return MakeOutcome(req, XClusterVerifyResult::kMatch, req.end_key);
}

}  // namespace

// Splitting a table is only sound if the pieces add up to the table, so every range has to be
// verified and each has to be given its own bounds. A sweep that dropped a range would still report
// kMatch, having matched everything it did compare.
TEST_F(XClusterVerifyTest, SweepVerifiesEveryRangeOfATable) {
  std::vector<std::pair<std::string, std::string>> seen;
  auto verify = [&seen](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    seen.emplace_back(strings::b2a_hex(req.start_key), strings::b2a_hex(req.end_key));
    return HashedWholeRange(req);
  };

  const auto summary =
      ASSERT_RESULT(VerifyXClusterTablePairs(MakePairs(1), GroupVerifyOptions(), verify,
                                             ThreeRanges));
  ASSERT_EQ(summary.result, XClusterVerifyResult::kMatch);
  ASSERT_EQ(summary.slices, 3);
  // One table, however many ranges it took.
  ASSERT_EQ(summary.tables_started, 1);
  ASSERT_TRUE(summary.unfinished.empty());
  // The bounds reach the slices intact, so the three cover the key space end to end with no gap and
  // no overlap.
  ASSERT_EQ(seen, (std::vector<std::pair<std::string, std::string>>{
                      {"", "20"}, {"20", "40"}, {"40", ""}}));
}

// A range ends where its bounds say, not where the table does, so a range whose scan is capped
// part-way has to keep going until it reaches its own end -- and then stop, rather than running on
// into the next range's rows and hashing them twice.
TEST_F(XClusterVerifyTest, ABoundedRangeWalksToItsEndAndStops) {
  std::vector<std::string> starts;
  // Stops short of the range end once, then reaches it.
  auto verify = [&starts](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    starts.push_back(strings::b2a_hex(req.start_key));
    const std::string end_key = starts.size() == 1 ? "\x10" : "\x20";
    return MakeOutcome(req, XClusterVerifyResult::kMatch, end_key);
  };

  GroupVerifyOptions options;
  options.max_rows = 1;
  auto one_range = [](const TableId&) -> Result<std::vector<KeyRange>> {
    return std::vector<KeyRange>{KeyRange{.start = "", .end = "\x20"}};
  };
  const auto summary =
      ASSERT_RESULT(VerifyXClusterTablePairs(MakePairs(1), options, verify, one_range));
  ASSERT_EQ(summary.result, XClusterVerifyResult::kMatch);
  ASSERT_EQ(summary.slices, 2);
  // The second slice resumed where the first stopped, and reaching the range's end ended the range
  // rather than starting a third slice at it.
  ASSERT_EQ(starts, (std::vector<std::string>{"", "10"}));
}

// Ranges are independent, so one that cannot be judged must not condemn the rest of its table: the
// ranges that can be verified still are, and the table is named as not wholly covered.
TEST_F(XClusterVerifyTest, AnAbandonedRangeDoesNotStopItsSiblings) {
  auto verify = [](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    if (req.start_key == "\x20") {
      return MakeOutcome(req, XClusterVerifyResult::kTryAgain, "");
    }
    return HashedWholeRange(req);
  };

  const auto summary =
      ASSERT_RESULT(VerifyXClusterTablePairs(MakePairs(1), GroupVerifyOptions(), verify,
                                             ThreeRanges));
  ASSERT_EQ(summary.result, XClusterVerifyResult::kTryAgain);
  ASSERT_EQ(summary.slices, 3);
  ASSERT_EQ(summary.unfinished, std::vector<TableId>{"s1"});
}

// Concurrency is an execution detail, so it must not be visible in the answer. Anything shared
// across the threads -- the counts, the verdict, the unfinished set -- would show up here as a
// count that drifts between the two runs.
TEST_F(XClusterVerifyTest, ConcurrentSweepAgreesWithASequentialOne) {
  // The one piece of the fake the threads share, so it cannot be a plain int.
  std::atomic<int> slices{0};
  auto verify = [&slices](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    slices.fetch_add(1);
    // kTryAgain on one range of one table, so the merged verdict and the unfinished list are
    // compared too, not just the slice count.
    if (req.source_table_id == "s2" && req.start_key == "\x20") {
      return MakeOutcome(req, XClusterVerifyResult::kTryAgain, "");
    }
    return HashedWholeRange(req);
  };

  GroupVerifyOptions sequential;
  const auto expected =
      ASSERT_RESULT(VerifyXClusterTablePairs(MakePairs(4), sequential, verify, ThreeRanges));
  ASSERT_EQ(slices.load(), 12);

  slices.store(0);
  GroupVerifyOptions concurrent;
  concurrent.max_concurrent_ranges = 8;
  const auto actual =
      ASSERT_RESULT(VerifyXClusterTablePairs(MakePairs(4), concurrent, verify, ThreeRanges));

  ASSERT_EQ(slices.load(), 12);
  ASSERT_EQ(actual.result, expected.result);
  ASSERT_EQ(actual.slices, expected.slices);
  ASSERT_EQ(actual.tables_started, expected.tables_started);
  ASSERT_EQ(actual.counts, expected.counts);
  ASSERT_EQ(actual.unfinished, expected.unfinished);
}

// A bad Status is the sweep failing, not news about the data, so it ends the sweep -- and with
// ranges in flight it has to end one that several threads are inside of. What is asserted is that
// the failure survives the merge intact and that the ranges still queued are abandoned rather than
// run for nothing. The count is a bound, not an equality: whatever was already in flight when the
// failure landed still finishes, so the exact number is not the sweep's to promise.
TEST_F(XClusterVerifyTest, AConcurrentSweepAbortsOnABadStatus) {
  constexpr int kPairs = 50;
  constexpr int kRangesPerPair = 3;
  std::atomic<int> slices{0};
  auto verify = [&slices](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    slices.fetch_add(1);
    // The first table's leading range, so the failure lands while most of the work is still queued.
    if (req.source_table_id == "s1" && req.start_key.empty()) {
      return STATUS(IOError, "tserver went away");
    }
    return HashedWholeRange(req);
  };

  GroupVerifyOptions options;
  options.max_concurrent_ranges = 8;
  const auto result = VerifyXClusterTablePairs(MakePairs(kPairs), options, verify, ThreeRanges);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "tserver went away");
  // The early-out did something: a sweep that ignored the failure would have run all 150.
  ASSERT_LT(slices.load(), kPairs * kRangesPerPair);
}

// Two ranges fail, and the sweep reports one failure. Which one is fixed only when the ranges run
// in a known order, so the ordering is asserted sequentially; the merge it exercises is the same
// one the concurrent path uses, and the test above covers reaching it from several threads.
TEST_F(XClusterVerifyTest, TheFirstBadStatusIsTheOneReported) {
  std::vector<TableId> attempted;
  auto verify = [&attempted](const SliceVerifyRequest& req) -> Result<SliceVerifyOutcome> {
    attempted.push_back(req.source_table_id);
    if (req.source_table_id == "s1") {
      return STATUS(IOError, "first failure");
    }
    if (req.source_table_id == "s2") {
      return STATUS(IOError, "second failure");
    }
    return HashedWholeRange(req);
  };

  const auto result =
      VerifyXClusterTablePairs(MakePairs(3), GroupVerifyOptions(), verify, WholeTable);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "first failure");
  ASSERT_STR_NOT_CONTAINS(result.status().ToString(), "second failure");
  // s2 and s3 are never asked, so the sweep stopped rather than merely remembering the first error.
  ASSERT_EQ(attempted, std::vector<TableId>{"s1"});
}

// A table with no ranges would be verified by doing nothing, and a sweep that verified nothing
// reports kMatch.
TEST_F(XClusterVerifyTest, SweepRefusesATableWithNoRanges) {
  auto no_ranges = [](const TableId&) -> Result<std::vector<KeyRange>> {
    return std::vector<KeyRange>{};
  };
  const auto result = VerifyXClusterTablePairs(
      MakePairs(1), GroupVerifyOptions(), HashedWholeRange, no_ranges);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "no key ranges to verify");
}

}  // namespace yb::tools
