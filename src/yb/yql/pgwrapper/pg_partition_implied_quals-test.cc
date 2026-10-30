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

// Tests for yb_enable_partition_implied_qual_removal: index scans of a partition skip quals that
// the partition constraint implies.

#include <optional>
#include <string>
#include <vector>

#include "yb/gutil/strings/join.h"

#include "yb/util/json_document.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::pgwrapper {
namespace {

struct ScanStats {
  double read_requests;
  double rows_scanned;
};

Result<ScanStats> GetScanStats(PGConn* conn, const std::string& query) {
  auto doc = VERIFY_RESULT(conn->FetchRow<JsonDocument>(
      "EXPLAIN (ANALYZE, DIST, FORMAT JSON) " + query));
  auto root = doc.Root()[0];
  return ScanStats{
      VERIFY_RESULT(root["Storage Read Requests"].GetDouble()),
      VERIFY_RESULT(root["Storage Rows Scanned"].GetDouble())};
}

Result<std::string> GetPlan(PGConn* conn, const std::string& query) {
  return JoinStrings(
      VERIFY_RESULT(conn->FetchRows<std::string>("EXPLAIN (COSTS OFF) " + query)), "\n");
}

Status SetRemoval(PGConn* conn, bool enabled) {
  return conn->ExecuteFormat(
      "SET yb_enable_partition_implied_qual_removal = $0", enabled ? "on" : "off");
}

} // namespace

class PgPartitionImpliedQualsTest : public PgMiniTestBase {};

// The partition key is filtered on but is not in the index. Skipping the implied filter makes the
// scan a distinct index-only scan that reads one index entry per distinct value, instead of every
// matching index entry plus its table row.
TEST_F(PgPartitionImpliedQualsTest, DistinctScanSkipsTableReads) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (k int, region smallint NOT NULL, a int NOT NULL, b text NOT NULL,"
      " PRIMARY KEY (k HASH, region)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t_r1 PARTITION OF t FOR VALUES IN (1)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON t_r1 (a ASC, b ASC)"));
  // a = 1 matches odd i, so b is one of b1, b3, b5, each on 1000 rows.
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1, i % 2, 'b' || (i % 6) FROM generate_series(1, 6000) i"));
  ASSERT_OK(conn.Execute("ANALYZE t"));

  const std::string query =
      "SELECT DISTINCT b FROM t WHERE region = 1 AND a = 1 AND b IN ('b1', 'b3', 'b5')";
  const std::vector<std::string> expected = {"b1", "b3", "b5"};

  // Off by default.
  ASSERT_STR_NOT_CONTAINS(ASSERT_RESULT(GetPlan(&conn, query)), "Distinct Index Only Scan");
  const auto off = ASSERT_RESULT(GetScanStats(&conn, query));
  ASSERT_GE(off.rows_scanned, 3000);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRows<std::string>(query + " ORDER BY b")), expected);

  ASSERT_OK(SetRemoval(&conn, true));
  ASSERT_STR_CONTAINS(ASSERT_RESULT(GetPlan(&conn, query)), "Distinct Index Only Scan");
  const auto on = ASSERT_RESULT(GetScanStats(&conn, query));
  ASSERT_EQ(on.rows_scanned, 3);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRows<std::string>(query + " ORDER BY b")), expected);
}

// Index scans return the same rows with and without the GUC, across partition layouts and
// predicates that the partition bounds do and do not imply.
TEST_F(PgPartitionImpliedQualsTest, SameResultsAsWithoutRemoval) {
  auto conn = ASSERT_RESULT(Connect());
  for (const auto* stmt : {
      // LIST with single-value, multi-value, and default partitions.
      "CREATE TABLE lp (k int, p int NOT NULL, a int NOT NULL, b int,"
      " PRIMARY KEY (k HASH, p)) PARTITION BY LIST (p)",
      "CREATE TABLE lp_1 PARTITION OF lp FOR VALUES IN (1)",
      "CREATE TABLE lp_23 PARTITION OF lp FOR VALUES IN (2, 3)",
      "CREATE TABLE lp_def PARTITION OF lp DEFAULT",
      "INSERT INTO lp SELECT i, i % 6, i % 10, NULLIF(i % 7, 0) FROM generate_series(1, 600) i",
      // RANGE with unbounded ends.
      "CREATE TABLE rp (k int, p int NOT NULL, a int NOT NULL, b int,"
      " PRIMARY KEY (k HASH, p)) PARTITION BY RANGE (p)",
      "CREATE TABLE rp_lo PARTITION OF rp FOR VALUES FROM (MINVALUE) TO (0)",
      "CREATE TABLE rp_mid PARTITION OF rp FOR VALUES FROM (0) TO (10)",
      "CREATE TABLE rp_hi PARTITION OF rp FOR VALUES FROM (10) TO (MAXVALUE)",
      "INSERT INTO rp SELECT i, i % 19 - 3, i % 10, NULLIF(i % 7, 0)"
      " FROM generate_series(1, 600) i",
      // Two levels: the leaves also carry the parent's LIST bound.
      "CREATE TABLE mp (k int, p int NOT NULL, a int NOT NULL, b int,"
      " PRIMARY KEY (k HASH, p, a)) PARTITION BY LIST (p)",
      "CREATE TABLE mp_1 PARTITION OF mp FOR VALUES IN (1) PARTITION BY RANGE (a)",
      "CREATE TABLE mp_1_lo PARTITION OF mp_1 FOR VALUES FROM (MINVALUE) TO (5)",
      "CREATE TABLE mp_1_hi PARTITION OF mp_1 FOR VALUES FROM (5) TO (MAXVALUE)",
      "CREATE TABLE mp_2 PARTITION OF mp FOR VALUES IN (2)",
      "INSERT INTO mp SELECT i, 1 + i % 2, i % 10, NULLIF(i % 7, 0)"
      " FROM generate_series(1, 600) i",
      "CREATE INDEX ON lp (a ASC, b ASC)",
      "CREATE INDEX ON rp (a ASC, b ASC)",
      "CREATE INDEX ON mp (a ASC, b ASC)",
      // The GUC only changes index scans.
      "SET enable_seqscan = off",
      "SET enable_bitmapscan = off"}) {
    ASSERT_OK(conn.Execute(stmt));
  }

  const std::vector<std::string> tables = {"lp", "rp", "mp"};
  const std::vector<std::string> partition_key_preds = {
      "p = 1", "p = 2", "p = 5", "p = 1::bigint", "p IN (1, 2)", "p IN (2, 3)", "p <> 1",
      "p < 0", "p >= 10", "p >= 0 AND p < 10", "p BETWEEN 1 AND 1", "p > 0 AND p < 2",
      "p IS NOT NULL", "(p = 1 OR a = 3)"};
  const std::vector<std::string> other_preds = {
      "TRUE", "a = 3", "a < 5 AND b IS NULL", "a IN (1, 7) AND b > 3"};

  size_t changed_plans = 0;
  // Runs 'query' with the GUC off and on, and fetches its rows with 'fetch'. The rows must match.
  const auto compare = [&conn, &changed_plans](const std::string& query, const auto& fetch) {
    SCOPED_TRACE(query);
    ASSERT_OK(SetRemoval(&conn, false));
    const auto expected = ASSERT_RESULT(fetch(query));
    const auto plan_without = ASSERT_RESULT(GetPlan(&conn, query));
    ASSERT_OK(SetRemoval(&conn, true));
    ASSERT_EQ(ASSERT_RESULT(fetch(query)), expected);
    if (ASSERT_RESULT(GetPlan(&conn, query)) != plan_without) {
      ++changed_plans;
    }
  };
  const auto fetch_a_b = [&conn](const std::string& query) {
    return conn.FetchRows<int32_t, std::optional<int32_t>>(query);
  };
  const auto fetch_a = [&conn](const std::string& query) {
    return conn.FetchRows<int32_t>(query);
  };

  for (const auto& table : tables) {
    for (const auto& partition_key_pred : partition_key_preds) {
      for (const auto& other_pred : other_preds) {
        const auto from = Format("$0 WHERE $1 AND $2", table, partition_key_pred, other_pred);
        ASSERT_NO_FATALS(compare(Format("SELECT a, b FROM $0 ORDER BY a, b", from), fetch_a_b));
        ASSERT_NO_FATALS(compare(Format("SELECT DISTINCT a FROM $0 ORDER BY a", from), fetch_a));
      }
    }
  }
  // Guards against the loop passing without exercising the change.
  LOG(INFO) << "Plans changed by the GUC: " << changed_plans;
  ASSERT_GT(changed_plans, 0);
}

// A partition-implied qual on an index key column stays an index condition. Here it binds part of
// the hash key, so the lookup reads one row from one tablet instead of scanning every tablet.
TEST_F(PgPartitionImpliedQualsTest, KeepsHashKeyCondition) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(SetRemoval(&conn, true));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE h (region int NOT NULL, k int NOT NULL, v int,"
      " PRIMARY KEY ((region, k) HASH)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE h_r1 PARTITION OF h FOR VALUES IN (1) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO h SELECT 1, i, i FROM generate_series(1, 100) i"));

  const std::string query = "SELECT v FROM h WHERE region = 1 AND k = 5";
  ASSERT_STR_CONTAINS(
      ASSERT_RESULT(GetPlan(&conn, query)), "Index Cond: ((region = 1) AND (k = 5))");
  const auto stats = ASSERT_RESULT(GetScanStats(&conn, query));
  ASSERT_EQ(stats.read_requests, 1);
  ASSERT_EQ(stats.rows_scanned, 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(query)), 5);
}

} // namespace yb::pgwrapper
