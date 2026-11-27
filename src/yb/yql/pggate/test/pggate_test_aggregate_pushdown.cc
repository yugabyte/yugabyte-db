//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "yb/util/format.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {
namespace pggate {
namespace {

constexpr const char* kDatabaseName = "yugabyte";
constexpr const char* kTableName = "grouped_agg_t";
constexpr const char* kIndexName = "grouped_agg_r_idx";
constexpr const char* kSecondaryIndexName = "grouped_agg_k3_idx";

}  // namespace

// Session and statement helpers. This does not create a table. A test that builds
// its own relation stays here.
class PggateTestGroupedAggregate : public PggateTest {
 protected:
  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
  }

  Result<pgwrapper::PGConn> PgConnect(const std::string& database_name) {
    auto* ts = cluster_->tablet_server(0);
    return pgwrapper::PGConnBuilder({
                                        .host = ts->bind_host(),
                                        .port = ts->ysql_port(),
                                        .dbname = database_name,
                                    })
        .Connect();
  }

  Result<YbcPgOid> RelationOid(pgwrapper::PGConn& conn, const std::string& relation_name) {
    return conn.FetchRow<pgwrapper::PGOid>(Format(
        "SELECT CASE WHEN relfilenode = 0 THEN oid ELSE relfilenode END "
        "FROM pg_class WHERE relname = '$0'",
        relation_name));
  }

  void SetUp() override {
    PggateTest::SetUp();
    CHECK_OK(Init(
        "PggateTestGroupedAggregate", kNumOfTablets, /* replication_factor = */ 0,
        /* should_create_db = */ false));
    auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
    db_oid_ = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        Format("SELECT oid FROM pg_database WHERE datname = '$0'", kDatabaseName)));
    CHECK_NE(db_oid_, 0);
    // Pin parallelism at the tablet count so the plain aggregate's one wave does not
    // depend on TabletServerCount.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_select_parallelism) = 3;
  }

  YbcPgExpr NewColumnRef(YbcPgStatement pg_stmt, int attr, DataType type) {
    YbcPgExpr colref = nullptr;
    CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, attr, type, &colref));
    return colref;
  }

  void AppendColumnRefTarget(YbcPgStatement pg_stmt, int attr, DataType type) {
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(
        pg_stmt, NewColumnRef(pg_stmt, attr, type), false /* is_for_secondary_index */));
  }

  void AppendAggregate(
      YbcPgStatement pg_stmt, const char* op_name, const YbcPgTypeEntity* result_type,
      YbcPgExpr arg, int index) {
    YbcPgExpr expr = nullptr;
    CHECK_YBC_STATUS(
        YBCPgNewOperator(pg_stmt, op_name, result_type, false /* collate_is_valid_non_c */, &expr));
    CHECK_YBC_STATUS(YBCPgOperatorAppendArg(expr, arg));
    CHECK_YBC_STATUS(YBCPgDmlAppendAggregateTarget(pg_stmt, expr, index));
  }

  void BindInt32Column(YbcPgStatement pg_stmt, int attr, int32_t value) {
    YbcPgExpr expr = nullptr;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, value, false /* is_null */, &expr));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, attr, expr));
  }

  // yb_hash_code(h) = code. Both ends are inclusive, so the scan is that one bucket.
  void BindHashCode(YbcPgStatement pg_stmt, int32_t code) {
    CHECK_GE(code, 0);
    CHECK_LE(code, 65535);
    const auto hash_code = static_cast<uint16_t>(code);
    CHECK_YBC_STATUS(YBCPgDmlBindHashCodes(
        pg_stmt, YB_YQL_BOUND_VALID_INCLUSIVE, hash_code, YB_YQL_BOUND_VALID_INCLUSIVE,
        hash_code));
  }

  // reads counts read RPCs that returned, read_ops counts tablet ops sent, and
  // rows_received counts rows returned.
  struct ReadStatDelta {
    uint64_t reads = 0;
    uint64_t read_ops = 0;
    uint64_t rows_received = 0;

    bool operator==(const ReadStatDelta&) const = default;

    ReadStatDelta operator-(const ReadStatDelta& before) const {
      return {
          reads - before.reads, read_ops - before.read_ops, rows_received - before.rows_received};
    }
  };

  ReadStatDelta ReadStats(bool index) const {
    const auto& stats = index ? session_stats_->stats.indices : session_stats_->stats.tables;
    return {stats.reads, stats.read_ops, stats.rows_received};
  }

  template <class Row, class Decode>
  std::vector<Row> FetchRows(
      YbcPgStatement pg_stmt, bool forward, int row_limit, int size_limit, Decode decode,
      bool index, bool set_forward_scan, int natts) {
    // With a direction set, the scan walks tablets one at a time, one op per RPC. That
    // is what the grouped tests count. Leaving the direction unset lets the plain
    // aggregate send every tablet in one RPC.
    if (set_forward_scan) {
      CHECK_YBC_STATUS(YBCPgSetForwardScan(pg_stmt, forward));
    }

    YbcPgExecParameters exec_params{};
    exec_params.yb_fetch_row_limit = row_limit;
    exec_params.yb_fetch_size_limit = size_limit;

    const auto table_before = ReadStats(false /* index */);
    const auto index_before = ReadStats(true /* index */);
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, &exec_params));

    uint64_t values[16] = {};
    bool isnulls[16] = {};
    CHECK_LE(natts, 16);
    std::vector<Row> rows;
    for (;;) {
      bool has_data = false;
      CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, natts, values, isnulls, nullptr, &has_data));
      if (!has_data) {
        break;
      }
      rows.push_back(decode(values, isnulls));
    }
    CommitTransaction();
    last_table_read_stats_ = ReadStats(false /* index */) - table_before;
    last_index_read_stats_ = ReadStats(true /* index */) - index_before;
    last_read_stats_ = index ? last_index_read_stats_ : last_table_read_stats_;
    return rows;
  }

  YbcPgOid db_oid_ = 0;
  // last_read_stats_ is the relation this fetch scanned. A secondary index scan
  // reads both: ybctids from the index, then the aggregate from the base table.
  ReadStatDelta last_read_stats_;
  ReadStatDelta last_table_read_stats_;
  ReadStatDelta last_index_read_stats_;
};

// Group key k is attno 1, so slot 0. The table has 3 columns, so count, min, max, and
// sum start at slot 3. The fetch buffer is 7 datums wide.
struct GroupedWideRow {
  int32_t k;
  int64_t count;
  int32_t min_v;
  int32_t max_v;
  int64_t sum_v;

  bool operator==(const GroupedWideRow&) const = default;
};

TEST_F(PggateTestGroupedAggregate, MoreTargetsThanColumns) {
  constexpr const char* kNarrowTable = "grouped_agg_narrow";
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  ASSERT_OK(conn.Execute(
      Format("CREATE TABLE $0 (k INT, v INT, p INT, PRIMARY KEY (k ASC, p ASC))", kNarrowTable)));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 VALUES (1, 10, 1), (1, 11, 2), (2, 20, 3)", kNarrowTable)));
  const auto narrow_oid = ASSERT_RESULT(RelationOid(conn, kNarrowTable));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  // SELECT k, count(*), min(v), max(v), sum(v) FROM grouped_agg_narrow GROUP BY k.
  YbcPgStatement pg_stmt = nullptr;
  CHECK_YBC_STATUS(YBCPgNewSelect(
      db_oid_, narrow_oid, nullptr /* prepare_params */, kDefaultTableLocality,
      {} /* skip_intents_info */, &pg_stmt));
  AppendColumnRefTarget(pg_stmt, /*attr=*/1, DataType::INT32);
  YbcPgExpr one = nullptr;
  CHECK_YBC_STATUS(YBCPgNewConstant(
      pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
      nullptr /* collation_sortkey */, 1 /* datum */, false /* is_null */, &one));
  AppendAggregate(pg_stmt, "count", YBCPgFindTypeEntity(INT8OID), one, /*index=*/3);
  AppendAggregate(
      pg_stmt, "min", YBCPgFindTypeEntity(INT4OID),
      NewColumnRef(pg_stmt, /*attr=*/2, DataType::INT32), /*index=*/4);
  AppendAggregate(
      pg_stmt, "max", YBCPgFindTypeEntity(INT4OID),
      NewColumnRef(pg_stmt, /*attr=*/2, DataType::INT32), /*index=*/5);
  AppendAggregate(
      pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
      NewColumnRef(pg_stmt, /*attr=*/2, DataType::INT32), /*index=*/6);

  EXPECT_EQ(
      FetchRows<GroupedWideRow>(
          pg_stmt, /*forward=*/true, /*row_limit=*/1024, /*size_limit=*/0,
          [](const uint64_t* values, const bool* isnulls) {
            CHECK(!isnulls[0]);
            CHECK(!isnulls[3]);
            CHECK(!isnulls[4]);
            CHECK(!isnulls[5]);
            CHECK(!isnulls[6]);
            return GroupedWideRow{
                .k = static_cast<int32_t>(values[0]),
                .count = static_cast<int64_t>(values[3]),
                .min_v = static_cast<int32_t>(values[4]),
                .max_v = static_cast<int32_t>(values[5]),
                .sum_v = static_cast<int64_t>(values[6]),
            };
          },
          false /* index */, true /* set_forward_scan */, 7 /* natts */),
      (std::vector<GroupedWideRow>{{1, 2, 10, 11, 21}, {2, 1, 20, 20, 20}}));
}

// PK is (k1, k2). Only k1 is a group key, so it lands at slot 0. Dropping x1..x5
// leaves v at attno 8 and the descriptor width at 8, so sum is slot 8. The fetch
// buffer is 9 datums wide.
struct GroupedDroppedColRow {
  int32_t k1;
  int64_t sum_v;

  bool operator==(const GroupedDroppedColRow&) const = default;
};

TEST_F(PggateTestGroupedAggregate, DroppedColumnsKeepLaterAttnum) {
  constexpr const char* kTable = "grouped_agg_dropped";
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE $0 ("
      "k1 INT, k2 INT, x1 INT, x2 INT, x3 INT, x4 INT, x5 INT, v INT, "
      "PRIMARY KEY (k1 ASC, k2 ASC))",
      kTable)));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 VALUES "
      "(1, 1, 0, 0, 0, 0, 0, 10), (1, 2, 0, 0, 0, 0, 0, 11), (2, 1, 0, 0, 0, 0, 0, 20)",
      kTable)));
  ASSERT_OK(conn.Execute(Format(
      "ALTER TABLE $0 DROP COLUMN x1, DROP COLUMN x2, DROP COLUMN x3, "
      "DROP COLUMN x4, DROP COLUMN x5",
      kTable)));
  const auto table_oid = ASSERT_RESULT(RelationOid(conn, kTable));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  // SELECT k1, sum(v) FROM grouped_agg_dropped GROUP BY k1.
  YbcPgStatement pg_stmt = nullptr;
  CHECK_YBC_STATUS(YBCPgNewSelect(
      db_oid_, table_oid, nullptr /* prepare_params */, kDefaultTableLocality,
      {} /* skip_intents_info */, &pg_stmt));
  // k2 is a key column and is not a group key.
  AppendColumnRefTarget(pg_stmt, /*attr=*/1, DataType::INT32);
  AppendAggregate(
      pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
      NewColumnRef(pg_stmt, /*attr=*/8, DataType::INT32), /*index=*/8);

  EXPECT_EQ(
      FetchRows<GroupedDroppedColRow>(
          pg_stmt, /*forward=*/true, /*row_limit=*/1024, /*size_limit=*/0,
          [](const uint64_t* values, const bool* isnulls) {
            CHECK(!isnulls[0]);
            CHECK(!isnulls[8]);
            return GroupedDroppedColRow{
                .k1 = static_cast<int32_t>(values[0]),
                .sum_v = static_cast<int64_t>(values[8]),
            };
          },
          false /* index */, true /* set_forward_scan */, 9 /* natts */),
      (std::vector<GroupedDroppedColRow>{{1, 21}, {2, 20}}));
}

struct GroupedSumRow {
  int32_t k;
  int64_t sum_v;

  bool operator==(const GroupedSumRow&) const = default;
};

namespace {

// Two ints that share a hash code, and one that does not. lo < hi.
struct HashCodePair {
  int32_t code;
  int32_t lo;
  int32_t hi;
  int32_t other;
};

Result<HashCodePair> FindSharedHashCode(pgwrapper::PGConn& conn) {
  const auto [code, lo, hi] = VERIFY_RESULT((conn.FetchRow<int32_t, int32_t, int32_t>(
      "SELECT code, lo, hi FROM ("
      "SELECT yb_hash_code(i) AS code, min(i) AS lo, max(i) AS hi "
      "FROM generate_series(1, 2000) AS i "
      "GROUP BY 1 HAVING count(*) >= 2 "
      "ORDER BY 1 LIMIT 1) s")));
  const auto other = VERIFY_RESULT(conn.FetchRow<int32_t>(
      Format("SELECT i FROM generate_series(1, 2000) AS i WHERE yb_hash_code(i) <> $0 LIMIT 1",
             code)));
  return HashCodePair{.code = code, .lo = lo, .hi = hi, .other = other};
}

}  // namespace

// PK is (k1 HASH, k2, r). Equality on k1 is one hash bucket, so k2 stays in order.
// lo and hi share that bucket. hi must not be added into lo's groups.
TEST_F(PggateTestGroupedAggregate, HashKeyEqualityGroupsByRangeKey) {
  constexpr const char* kTable = "grouped_agg_hash_range";
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  const auto keys = ASSERT_RESULT(FindSharedHashCode(conn));
  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE $0 (k1 INT, k2 INT, r INT, v INT, "
      "PRIMARY KEY (k1 HASH, k2 ASC, r ASC)) SPLIT INTO 3 TABLETS",
      kTable)));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 VALUES "
      "($1, 1, 0, 10), ($1, 1, 1, 11), ($1, 2, 0, 20), ($1, 2, 1, 21), ($1, 3, 0, 30), "
      "($2, 1, 0, 1000), ($2, 2, 0, 1000), "
      "($3, 1, 0, 7)",
      kTable, keys.lo, keys.hi, keys.other)));
  const auto table_oid = ASSERT_RESULT(RelationOid(conn, kTable));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  const auto fetch = [&](bool forward) {
    // SELECT k2, sum(v) FROM grouped_agg_hash_range WHERE k1 = const GROUP BY k2 ORDER BY k2.
    YbcPgStatement pg_stmt = nullptr;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        db_oid_, table_oid, nullptr /* prepare_params */, kDefaultTableLocality,
        {} /* skip_intents_info */, &pg_stmt));
    // k1 is the bound hash key. k2 is the group key and lands at slot 1.
    // The table has 4 columns, so sum is slot 4.
    AppendColumnRefTarget(pg_stmt, /*attr=*/2, DataType::INT32);
    AppendAggregate(
        pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
        NewColumnRef(pg_stmt, /*attr=*/4, DataType::INT32), /*index=*/4);
    BindInt32Column(pg_stmt, /*attr=*/1, keys.lo);
    return FetchRows<GroupedSumRow>(
        pg_stmt, forward, /*row_limit=*/1024, /*size_limit=*/0,
        [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[1]);
          CHECK(!isnulls[4]);
          return GroupedSumRow{
              .k = static_cast<int32_t>(values[1]),
              .sum_v = static_cast<int64_t>(values[4]),
          };
        },
        false /* index */, true /* set_forward_scan */, 5 /* natts */);
  };

  const std::vector<GroupedSumRow> expected{{1, 21}, {2, 41}, {3, 30}};
  EXPECT_EQ(fetch(true), expected);
  EXPECT_EQ(fetch(false), (std::vector<GroupedSumRow>{expected.rbegin(), expected.rend()}));
}

// PK is (h HASH, r). Equality on the hash code is one bucket, so h stays in order.
// lo and hi share the code. other is a different bucket and must stay out.
TEST_F(PggateTestGroupedAggregate, HashCodeEqualityGroupsByHashKey) {
  constexpr const char* kTable = "grouped_agg_hash_code";
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  const auto keys = ASSERT_RESULT(FindSharedHashCode(conn));
  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE $0 (h INT, r INT, v INT, PRIMARY KEY (h HASH, r ASC)) SPLIT INTO 3 TABLETS",
      kTable)));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 VALUES "
      "($1, 0, 10), ($1, 1, 11), ($2, 0, 30), ($2, 1, 31), ($3, 0, 1000)",
      kTable, keys.lo, keys.hi, keys.other)));
  const auto table_oid = ASSERT_RESULT(RelationOid(conn, kTable));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  const auto fetch = [&](bool forward) {
    // SELECT h, sum(v) FROM grouped_agg_hash_code
    //   WHERE yb_hash_code(h) = const GROUP BY h ORDER BY h.
    YbcPgStatement pg_stmt = nullptr;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        db_oid_, table_oid, nullptr /* prepare_params */, kDefaultTableLocality,
        {} /* skip_intents_info */, &pg_stmt));
    // h is the group key at slot 0. The table has 3 columns, so sum is slot 3.
    AppendColumnRefTarget(pg_stmt, /*attr=*/1, DataType::INT32);
    AppendAggregate(
        pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
        NewColumnRef(pg_stmt, /*attr=*/3, DataType::INT32), /*index=*/3);
    BindHashCode(pg_stmt, keys.code);
    return FetchRows<GroupedSumRow>(
        pg_stmt, forward, /*row_limit=*/1024, /*size_limit=*/0,
        [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[0]);
          CHECK(!isnulls[3]);
          return GroupedSumRow{
              .k = static_cast<int32_t>(values[0]),
              .sum_v = static_cast<int64_t>(values[3]),
          };
        },
        false /* index */, true /* set_forward_scan */, 4 /* natts */);
  };

  const std::vector<GroupedSumRow> expected{{keys.lo, 21}, {keys.hi, 61}};
  EXPECT_EQ(fetch(true), expected);
  EXPECT_EQ(fetch(false), (std::vector<GroupedSumRow>{expected.rbegin(), expected.rend()}));
}

// PK is (k1 HASH, k2 ASC, v ASC). IN on k1 reads one stream per k1, each ordered by k2. With no
// merge sort keys PgGate returns the streams one after another, so the partials for one k2 are
// not adjacent. k1 = 4 is outside the IN list.
TEST_F(PggateTestGroupedAggregate, HashKeyInReturnsStreamsInTurn) {
  constexpr const char* kTable = "grouped_agg_hash_in";
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE $0 (k1 INT, k2 INT, v INT, PRIMARY KEY (k1 HASH, k2 ASC, v ASC))", kTable)));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 VALUES "
      "(1, 1, 10), (1, 1, 11), (1, 2, 12), "
      "(2, 1, 20), (2, 3, 21), "
      "(3, 2, 30), (3, 2, 31), (3, 3, 32), "
      "(4, 1, 1000)",
      kTable)));
  const auto table_oid = ASSERT_RESULT(RelationOid(conn, kTable));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  // SELECT k2, sum(v) FROM grouped_agg_hash_in WHERE k1 IN (1, 2, 3) GROUP BY k2.
  YbcPgStatement pg_stmt = nullptr;
  CHECK_YBC_STATUS(YBCPgNewSelect(
      db_oid_, table_oid, nullptr /* prepare_params */, kDefaultTableLocality,
      {} /* skip_intents_info */, &pg_stmt));
  std::vector<YbcPgExpr> k1_values(3, nullptr);
  for (int i = 0; i < 3; ++i) {
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, i + 1 /* datum */, false /* is_null */, &k1_values[i]));
  }
  CHECK_YBC_STATUS(YBCPgDmlBindColumnCondIn(
      pg_stmt, NewColumnRef(pg_stmt, /*attr=*/1, DataType::INT32),
      static_cast<int>(k1_values.size()), k1_values.data()));
  // k2 is the group key at slot 1. The table has 3 columns, so sum is slot 3.
  AppendColumnRefTarget(pg_stmt, /*attr=*/2, DataType::INT32);
  AppendAggregate(
      pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
      NewColumnRef(pg_stmt, /*attr=*/3, DataType::INT32), /*index=*/3);
  const auto rows = FetchRows<GroupedSumRow>(
      pg_stmt, /*forward=*/true, /*row_limit=*/1024, /*size_limit=*/0,
      [](const uint64_t* values, const bool* isnulls) {
        CHECK(!isnulls[1]);
        CHECK(!isnulls[3]);
        return GroupedSumRow{
            .k = static_cast<int32_t>(values[1]),
            .sum_v = static_cast<int64_t>(values[3]),
        };
      },
      false /* index */, true /* set_forward_scan */, 4 /* natts */);

  // k1 = 1, then k1 = 2, then k1 = 3.
  const std::vector<GroupedSumRow> expected{{1, 21}, {2, 12}, {1, 20}, {3, 21}, {2, 61}, {3, 32}};
  EXPECT_EQ(rows, expected);
}

// Shared table grouped_agg_t, split at (1, 2) and (2, 0). Tests that read it inherit this.
// v and s do not follow the primary key, so a partial is not its first or last row.
class PggateTestGroupedAggregateTable : public PggateTestGroupedAggregate {
 protected:
  void SetUpTable() {
    auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
    ASSERT_OK(conn.Execute(Format(
        "CREATE TABLE $0 (k INT, r INT, v INT, s TEXT, PRIMARY KEY (k ASC, r ASC)) "
        "SPLIT AT VALUES ((1, 2), (2, 0))",
        kTableName)));
    ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 VALUES "
        "(1, 0, 12, 'd'), (1, 1, 10, 'a'), (1, 2, 13, 'c'), (1, 3, 11, 'b'), "
        "(2, 0, 21, 'g'), (2, 1, 20, 'e'), "
        "(3, 0, 30, 'f')",
        kTableName)));
    table_oid_ = ASSERT_RESULT(RelationOid(conn, kTableName));
    CHECK_NE(table_oid_, 0);
    ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));
  }

  void SetUp() override {
    PggateTestGroupedAggregate::SetUp();
    ASSERT_NO_FATAL_FAILURE(SetUpTable());
  }

  YbcPgStatement NewGroupedSelect() {
    YbcPgStatement pg_stmt = nullptr;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        db_oid_, table_oid_, nullptr /* prepare_params */, kDefaultTableLocality,
        {} /* skip_intents_info */, &pg_stmt));
    AppendColumnRefTarget(pg_stmt, /*attr=*/1, DataType::INT32);
    return pg_stmt;
  }

  std::vector<GroupedSumRow> FetchSumByK(bool forward, int row_limit, int size_limit) {
    // SELECT k, sum(v) FROM grouped_agg_t GROUP BY k.
    // forward is ORDER BY k. backward is ORDER BY k DESC.
    // grouped_agg_t has 4 columns, so sum is slot 4.
    auto pg_stmt = NewGroupedSelect();
    AppendAggregate(
        pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
        NewColumnRef(pg_stmt, /*attr=*/3, DataType::INT32), /*index=*/4);
    return FetchRows<GroupedSumRow>(
        pg_stmt, forward, row_limit, size_limit, [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[0]);
          CHECK(!isnulls[4]);
          return GroupedSumRow{
              .k = static_cast<int32_t>(values[0]),
              .sum_v = static_cast<int64_t>(values[4]),
          };
        },
        false /* index */, true /* set_forward_scan */, 5 /* natts */);
  }

  YbcPgOid table_oid_ = 0;
};

// ------------------------------------------------------------------------------------------------
// SUM(v) GROUP BY k across tablets. Partial groups stay unmerged. Row and size limits page them.
// ------------------------------------------------------------------------------------------------

class GroupedSumTest : public PggateTestGroupedAggregateTable {};

TEST_F(GroupedSumTest, ForwardFetchReturnsAllGroupedRowsAcrossPages) {
  EXPECT_EQ(
      FetchSumByK(/*forward=*/true, /*row_limit=*/1, /*size_limit=*/0),
      (std::vector<GroupedSumRow>{{1, 22}, {1, 24}, {2, 41}, {3, 30}}));
  // One group per page. Tablets are walked one at a time, one op per RPC: tablet 1 (k=1),
  // tablet 2 (k=1), then tablet 3 twice because the row limit ends its first page after k=2.
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

TEST_F(GroupedSumTest, BackwardFetchReturnsAllGroupedRowsAcrossPages) {
  EXPECT_EQ(
      FetchSumByK(/*forward=*/false, /*row_limit=*/1, /*size_limit=*/0),
      (std::vector<GroupedSumRow>{{3, 30}, {2, 41}, {1, 24}, {1, 22}}));
  // Same four RPCs as the forward walk, in reverse: the last tablet twice, then the two
  // tablets that hold k=1.
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

TEST_F(GroupedSumTest, MultiTabletFetchLeavesPartialGroupsUnmerged) {
  EXPECT_EQ(
      FetchSumByK(/*forward=*/true, /*row_limit=*/1024, /*size_limit=*/0),
      (std::vector<GroupedSumRow>{{1, 22}, {1, 24}, {2, 41}, {3, 30}}));
  // No limit ends a page. One RPC per tablet, and the last tablet returns both groups.
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 3, .read_ops = 3, .rows_received = 4}));
}

TEST_F(GroupedSumTest, ForwardFetchSizeLimitOnly) {
  EXPECT_EQ(
      FetchSumByK(/*forward=*/true, /*row_limit=*/1024, /*size_limit=*/20),
      (std::vector<GroupedSumRow>{{1, 22}, {1, 24}, {2, 41}, {3, 30}}));
  // One group is 22 bytes, so 20 stops the page after one group. Same four RPCs as the
  // row-limit-1 walk.
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

TEST_F(GroupedSumTest, BackwardFetchSizeLimitOnly) {
  EXPECT_EQ(
      FetchSumByK(/*forward=*/false, /*row_limit=*/1024, /*size_limit=*/20),
      (std::vector<GroupedSumRow>{{3, 30}, {2, 41}, {1, 24}, {1, 22}}));
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

// The last tablet holds groups k=2 and k=3. A row limit of 2 returns both in one
// response: three RPCs, one per tablet. Size limit 1 stops that response after k=2,
// so that tablet is read again. The partial list stays the same.
TEST_F(GroupedSumTest, RowAndSizeLimitEndsPageAndResumes) {
  const std::vector<GroupedSumRow> expected{{1, 22}, {1, 24}, {2, 41}, {3, 30}};
  EXPECT_EQ(FetchSumByK(/*forward=*/true, /*row_limit=*/2, /*size_limit=*/0), expected);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 3, .read_ops = 3, .rows_received = 4}));
  EXPECT_EQ(FetchSumByK(/*forward=*/true, /*row_limit=*/2, /*size_limit=*/1), expected);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

// ------------------------------------------------------------------------------------------------
// COUNT(*) GROUP BY k. Same tablets as SUM, so the same four partials stay unmerged.
// ------------------------------------------------------------------------------------------------

struct GroupedCountRow {
  int32_t k;
  int64_t count;

  bool operator==(const GroupedCountRow&) const = default;
};

class GroupedCountTest : public PggateTestGroupedAggregateTable {
 protected:
  std::vector<GroupedCountRow> Fetch(bool forward, int row_limit, int size_limit) {
    // SELECT k, count(*) FROM grouped_agg_t GROUP BY k ORDER BY k.
    // grouped_agg_t has 4 columns, so count is slot 4.
    auto pg_stmt = NewGroupedSelect();
    YbcPgExpr one = nullptr;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 1 /* datum */, false /* is_null */, &one));
    AppendAggregate(pg_stmt, "count", YBCPgFindTypeEntity(INT8OID), one, /*index=*/4);
    return FetchRows<GroupedCountRow>(
        pg_stmt, forward, row_limit, size_limit, [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[0]);
          CHECK(!isnulls[4]);
          return GroupedCountRow{
              .k = static_cast<int32_t>(values[0]),
              .count = static_cast<int64_t>(values[4]),
          };
        },
        false /* index */, true /* set_forward_scan */, 5 /* natts */);
  }
};

TEST_F(GroupedCountTest, ForwardFetchCountStarAcrossPages) {
  EXPECT_EQ(
      Fetch(/*forward=*/true, /*row_limit=*/1, /*size_limit=*/0),
      (std::vector<GroupedCountRow>{{1, 2}, {1, 2}, {2, 2}, {3, 1}}));
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

// ------------------------------------------------------------------------------------------------
// MIN(s), MAX(s) GROUP BY k. Splits at (1, 2) and (2, 0) put k=1 on two tablets, so
// those partials stay separate: (1, "a", "d") and (1, "b", "c"). The last tablet
// adds (2, "e", "g") and (3, "f", "f"). k=3 has one row, so its min and max are the same.
// ------------------------------------------------------------------------------------------------

struct GroupedMinMaxRow {
  int32_t k;
  std::string min_s;
  std::string max_s;

  bool operator==(const GroupedMinMaxRow&) const = default;
};

class GroupedMinMaxTest : public PggateTestGroupedAggregateTable {
 protected:
  std::vector<GroupedMinMaxRow> Fetch(bool forward, int row_limit, int size_limit) {
    // SELECT k, min(s), max(s) FROM grouped_agg_t GROUP BY k ORDER BY k.
    // grouped_agg_t has 4 columns, so min is slot 4 and max is slot 5.
    auto pg_stmt = NewGroupedSelect();
    AppendAggregate(
        pg_stmt, "min", YBCPgFindTypeEntity(TEXTOID),
        NewColumnRef(pg_stmt, /*attr=*/4, DataType::STRING), /*index=*/4);
    AppendAggregate(
        pg_stmt, "max", YBCPgFindTypeEntity(TEXTOID),
        NewColumnRef(pg_stmt, /*attr=*/4, DataType::STRING), /*index=*/5);
    return FetchRows<GroupedMinMaxRow>(
        pg_stmt, forward, row_limit, size_limit, [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[0]);
          CHECK(!isnulls[4]);
          CHECK(!isnulls[5]);
          return GroupedMinMaxRow{
              .k = static_cast<int32_t>(values[0]),
              .min_s = reinterpret_cast<const char*>(values[4]),
              .max_s = reinterpret_cast<const char*>(values[5]),
          };
        },
        false /* index */, true /* set_forward_scan */, 6 /* natts */);
  }
};

TEST_F(GroupedMinMaxTest, RowLimitStopsAfterOneGroup) {
  EXPECT_EQ(
      Fetch(/*forward=*/true, /*row_limit=*/1, /*size_limit=*/0),
      (std::vector<GroupedMinMaxRow>{{1, "a", "d"}, {1, "b", "c"}, {2, "e", "g"}, {3, "f", "f"}}));
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

TEST_F(GroupedMinMaxTest, SizeLimitStopsBeforeNextGroup) {
  EXPECT_EQ(
      Fetch(/*forward=*/true, /*row_limit=*/1024, /*size_limit=*/34),
      (std::vector<GroupedMinMaxRow>{{1, "a", "d"}, {1, "b", "c"}, {2, "e", "g"}, {3, "f", "f"}}));
  // One group is 35 bytes: 8-byte row count, 5-byte k, 11-byte min, 11-byte max.
  // 34 stops after that group, before k=3 on the last tablet.
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

TEST_F(GroupedMinMaxTest, BackwardSizeLimitStopsBeforeNextGroup) {
  EXPECT_EQ(
      Fetch(/*forward=*/false, /*row_limit=*/1024, /*size_limit=*/34),
      (std::vector<GroupedMinMaxRow>{{3, "f", "f"}, {2, "e", "g"}, {1, "b", "c"}, {1, "a", "d"}}));
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 4}));
}

// After the UPDATE and INSERT below, grouped_agg_t holds:
//
//   k  r  s       tablet
//   1  0  d       before (1, 2)
//   1  1  a
//   1  2  c       from (1, 2) to (2, 0)
//   1  3  b
//   2  0  g       from (2, 0), the last tablet
//   2  1  e
//   3  0  z
//   3  1  cc
//   3  2  zzzz
//   3  3  {a
//   3  4  a
//
// The size limit applies to one tablet response. The last tablet writes the k=2 group
// first: min "e", max "g". That group is 35 bytes (8-byte row count, 5-byte k, 11-byte
// "e", 11-byte "g"), so the buffer is 35 bytes before any k=3 row.
//
// A k=3 row is folded only when 35 + 5 + min bound + max bound fits in the limit. The 5
// is the k column. A string is 10 bytes plus its length. The bound of min or of max is
// the larger of the value already folded and the value on this row. Nothing is folded
// yet on the first k=3 row, so that bound is just the incoming string.
//
// "z" is 11 bytes. Both bounds are 11. 35 + 5 + 11 + 11 = 62.
// "cc" is 12 bytes. Min and max are still "z" (11 bytes), so both bounds become 12.
// 35 + 5 + 12 + 12 = 64. "cc" is less than "z", so min becomes "cc" and max stays "z".
// "zzzz" is 14 bytes. Min is "cc" (12 bytes) and max is "z" (11 bytes), so both bounds
// become 14. 35 + 5 + 14 + 14 = 68. "zzzz" is greater than "z", so max would become
// "zzzz" and min would stay "cc".
// "{a" is 12 bytes. Min is "cc" (12 bytes) and max is "zzzz" (14 bytes).
// 35 + 5 + 12 + 14 = 66. '{' is greater than 'z', so max becomes "{a" and min stays "cc".
// "a" is 11 bytes. Min is "cc" (12 bytes) and max is "{a" (12 bytes).
// 35 + 5 + 12 + 12 = 64. "a" is less than "cc", so min becomes "a" and max stays "{a".
//
// Limit 67 folds "z" and "cc", then stops because "zzzz" needs 68 bytes. This response
// emits (3, "cc", "z"). The next read of the last tablet returns the remaining rows as
// (3, "a", "{a"). Limit 68 also folds "zzzz", "{a", and "a", so k=3 is one partial,
// (3, "a", "{a").
TEST_F(GroupedMinMaxTest, SizeLimitStringWireSizeUpAndDown) {
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  ASSERT_OK(conn.Execute(Format("UPDATE $0 SET s = 'z' WHERE k = 3 AND r = 0", kTableName)));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 VALUES (3, 1, 0, 'cc'), (3, 2, 0, 'zzzz'), (3, 3, 0, '{a'), (3, 4, 0, 'a')",
      kTableName)));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  // 67 folds "z" and "cc" only. "zzzz" needs 68 bytes, so it starts the next partial.
  const std::vector<GroupedMinMaxRow> stopped{
      {1, "a", "d"}, {1, "b", "c"}, {2, "e", "g"}, {3, "cc", "z"}, {3, "a", "{a"}};
  EXPECT_EQ(Fetch(/*forward=*/true, /*row_limit=*/1024, /*size_limit=*/67), stopped);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 4, .read_ops = 4, .rows_received = 5}));

  const std::vector<GroupedMinMaxRow> folded{
      {1, "a", "d"}, {1, "b", "c"}, {2, "e", "g"}, {3, "a", "{a"}};
  EXPECT_EQ(Fetch(/*forward=*/true, /*row_limit=*/1024, /*size_limit=*/68), folded);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 3, .read_ops = 3, .rows_received = 4}));
}

// ------------------------------------------------------------------------------------------------
// Index-only scan of (r, k) INCLUDE (v), grouped by r. The index is ordered by r, not by the
// primary key (k, r), so each r group gathers rows from different primary-key tablets. It is
// split at (0, 2) and (2, 0):
//   tablet 1: (r=0, k=1, v=12)                                    -> (0, 12)
//   tablet 2: (0, 2, 21), (0, 3, 30), (1, 1, 10), (1, 2, 20)      -> (0, 51), (1, 30)
//   tablet 3: (2, 1, 13), (3, 1, 11)                              -> (2, 13), (3, 11)
// The aggregate reads the index and does not open the base table. Tablets are walked one at a
// time, one op per RPC. Row limit 1 ends the page after one partial, so the two tablets that
// hold two groups are read twice. The partial list is the same either way.
// ------------------------------------------------------------------------------------------------

class GroupedIndexSumTest : public PggateTestGroupedAggregateTable {
 protected:
  void SetUpIndex() {
    auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
    ASSERT_OK(conn.Execute(Format(
        "CREATE INDEX $0 ON $1 (r ASC, k ASC) INCLUDE (v) "
        "SPLIT AT VALUES ((0, 2), (2, 0))",
        kIndexName, kTableName)));
    index_oid_ = ASSERT_RESULT(RelationOid(conn, kIndexName));
    CHECK_NE(index_oid_, 0);
    ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));
  }

  void SetUp() override {
    PggateTestGroupedAggregateTable::SetUp();
    ASSERT_NO_FATAL_FAILURE(SetUpIndex());
  }

  std::vector<GroupedSumRow> FetchSumByR(bool forward, int row_limit) {
    // SELECT r, sum(v) FROM grouped_agg_t GROUP BY r.
    // Index (r, k) INCLUDE (v). forward is ORDER BY r. backward is ORDER BY r DESC.
    YbcPgStatement pg_stmt = nullptr;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        db_oid_, index_oid_, nullptr /* prepare_params */, kDefaultTableLocality,
        {} /* skip_intents_info */, &pg_stmt));
    // Index attributes: 1 = r, 2 = k, 3 = included v. Sum starts after that list.
    AppendColumnRefTarget(pg_stmt, /*attr=*/1, DataType::INT32);
    AppendAggregate(
        pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
        NewColumnRef(pg_stmt, /*attr=*/3, DataType::INT32), /*index=*/3);
    return FetchRows<GroupedSumRow>(
        pg_stmt, forward, row_limit, /*size_limit=*/0,
        [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[0]);
          CHECK(!isnulls[3]);
          return GroupedSumRow{
              .k = static_cast<int32_t>(values[0]),
              .sum_v = static_cast<int64_t>(values[3]),
          };
        },
        true /* index */, true /* set_forward_scan */, 4 /* natts */);
  }

  YbcPgOid index_oid_ = 0;
};

TEST_F(GroupedIndexSumTest, ForwardRowLimitEndsPageAndResumes) {
  const std::vector<GroupedSumRow> expected{{0, 12}, {0, 51}, {1, 30}, {2, 13}, {3, 11}};
  EXPECT_EQ(FetchSumByR(/*forward=*/true, /*row_limit=*/1024), expected);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 3, .read_ops = 3, .rows_received = 5}));
  EXPECT_EQ(FetchSumByR(/*forward=*/true, /*row_limit=*/1), expected);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 5, .read_ops = 5, .rows_received = 5}));
}

TEST_F(GroupedIndexSumTest, BackwardRowLimitEndsPageAndResumes) {
  const std::vector<GroupedSumRow> expected{{3, 11}, {2, 13}, {1, 30}, {0, 51}, {0, 12}};
  EXPECT_EQ(FetchSumByR(/*forward=*/false, /*row_limit=*/1024), expected);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 3, .read_ops = 3, .rows_received = 5}));
  EXPECT_EQ(FetchSumByR(/*forward=*/false, /*row_limit=*/1), expected);
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 5, .read_ops = 5, .rows_received = 5}));
}

// ------------------------------------------------------------------------------------------------
// Index scan through the base table: table (k1 HASH, k2 ASC, k3 DESC), index on (k3), 1000
// rows. The index has one tablet, so ybctids come back in k3 order.
// ------------------------------------------------------------------------------------------------

class GroupedSecondaryIndexAggTest : public PggateTestGroupedAggregate {
 protected:
  void SetUp() override {
    PggateTestGroupedAggregate::SetUp();
    auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
    ASSERT_OK(conn.Execute(Format(
        "CREATE TABLE $0 (k1 INT, k2 INT, k3 INT, v INT, "
        "PRIMARY KEY (k1 HASH, k2 ASC, k3 DESC))",
        kTableName)));
    table_oid_ = ASSERT_RESULT(RelationOid(conn, kTableName));
    CHECK_NE(table_oid_, 0);
    ASSERT_OK(
        conn.Execute(Format("CREATE INDEX $0 ON $1 (k3 ASC)", kSecondaryIndexName, kTableName)));
    index_oid_ = ASSERT_RESULT(RelationOid(conn, kSecondaryIndexName));
    CHECK_NE(index_oid_, 0);
    ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 SELECT i1, i2, i3, i1 * 100 + i2 * 10 + i3 FROM "
        "generate_series(0, 9) AS i1, generate_series(0, 9) AS i2, generate_series(0, 9) AS i3",
        kTableName)));
    ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));
  }

  YbcPgOid table_oid_ = 0;
  YbcPgOid index_oid_ = 0;
};

// SELECT k3, sum(v) FROM grouped_agg_t GROUP BY k3 ORDER BY k3.
// The scan reads ybctids from the index in k3 order and aggregates each batch on the table.
// A batch that ends inside a group leaves that group as two adjacent partials.
TEST_F(GroupedSecondaryIndexAggTest, GroupByIndexed) {
  const std::vector<GroupedSumRow> expected{
      {0, 49500}, {1, 12008}, {1, 37592}, {2, 32912}, {2, 16788}, {3, 41112},
      {3, 8688},  {4, 49900}, {5, 5720},  {5, 44280}, {6, 21040}, {6, 29060},
      {7, 35456}, {7, 14744}, {8, 48768}, {8, 1532},  {9, 50400}};

  YbcPgPrepareParameters params = {};
  params.index_relfilenode_oid = index_oid_;
  params.index_only_scan = false;
  YbcPgStatement pg_stmt = nullptr;
  CHECK_YBC_STATUS(YBCPgNewSelect(
      db_oid_, table_oid_, &params, kDefaultTableLocality, {} /* skip_intents_info */, &pg_stmt));
  AppendColumnRefTarget(pg_stmt, /*attr=*/3, DataType::INT32);
  AppendAggregate(
      pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
      NewColumnRef(pg_stmt, /*attr=*/4, DataType::INT32), /*index=*/5);
  const auto fetched_rows = FetchRows<GroupedSumRow>(
      pg_stmt, /*forward=*/true, /*row_limit=*/128, /*size_limit=*/0,
      [](const uint64_t* values, const bool* isnulls) {
        CHECK(!isnulls[2]);
        CHECK(!isnulls[5]);
        return GroupedSumRow{
            .k = static_cast<int32_t>(values[2]),
            .sum_v = static_cast<int64_t>(values[5]),
        };
      },
      false /* index */, true /* set_forward_scan */, 6 /* natts */);
  EXPECT_EQ(fetched_rows, expected);
  // 1000 index rows in batches of 128 is 8 batches. No batch boundary falls on a multiple of
  // 100, so 7 of the 10 groups split into two partials: 17 rows.
  EXPECT_EQ(
      last_table_read_stats_, (ReadStatDelta{.reads = 8, .read_ops = 8, .rows_received = 17}));
  EXPECT_EQ(
      last_index_read_stats_, (ReadStatDelta{.reads = 8, .read_ops = 8, .rows_received = 1000}));
}

// ------------------------------------------------------------------------------------------------
// Plain SUM(v): no group columns. One partial per tablet, all three tablets in one wave.
// ------------------------------------------------------------------------------------------------

class GroupedPlainSumTest : public PggateTestGroupedAggregateTable {
 protected:
  std::vector<int64_t> Fetch() {
    // SELECT sum(v) FROM grouped_agg_t. No group keys, so sum is slot 0.
    YbcPgStatement pg_stmt = nullptr;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        db_oid_, table_oid_, nullptr /* prepare_params */, kDefaultTableLocality,
        {} /* skip_intents_info */, &pg_stmt));
    AppendAggregate(
        pg_stmt, "sum", YBCPgFindTypeEntity(INT8OID),
        NewColumnRef(pg_stmt, /*attr=*/3, DataType::INT32), /*index=*/0);
    return FetchRows<int64_t>(
        pg_stmt, /*forward=*/true, /*row_limit=*/1024, /*size_limit=*/0,
        [](const uint64_t* values, const bool* isnulls) {
          CHECK(!isnulls[0]);
          return static_cast<int64_t>(values[0]);
        },
        false /* index */, false /* set_forward_scan */, 1 /* natts */);
  }
};

TEST_F(GroupedPlainSumTest, OnePartialPerTabletInOneWave) {
  auto sums = Fetch();
  std::sort(sums.begin(), sums.end());
  EXPECT_EQ(sums, (std::vector<int64_t>{22, 24, 71}));
  EXPECT_EQ(last_read_stats_, (ReadStatDelta{.reads = 1, .read_ops = 3, .rows_received = 3}));
}

// ------------------------------------------------------------------------------------------------
// Short request deadline. The tserver margin is larger than the RPC window, so each request
// ends after one row and the next fetch resumes. The base fixture does not set this flag.
// ------------------------------------------------------------------------------------------------

namespace {

std::map<int32_t, int64_t> MergeSums(const std::vector<GroupedSumRow>& rows) {
  std::map<int32_t, int64_t> sums;
  for (const auto& row : rows) {
    sums[row.k] += row.sum_v;
  }
  return sums;
}

}  // namespace

class GroupedDeadlineTest : public GroupedIndexSumTest {
 protected:
  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    GroupedIndexSumTest::CustomizeExternalMiniCluster(opts);
    opts->AddTServerFlag("--ysql_scan_deadline_margin_ms=120000");
  }

  // One absolute deadline for every RPC in this fetch. Clear it afterward.
  template <class FetchFn>
  std::vector<GroupedSumRow> FetchWithDeadline(FetchFn fetch) {
    // Sanitizer builds are slower, so the timeout scales with kTimeMultiplier. It stays
    // under the 120s margin above. The tserver stops at deadline minus that margin, which
    // is already past, so each request returns after one row.
    YBCSetTimeout(1000 * kTimeMultiplier);
    auto rows = fetch();
    YBCClearTimeout();
    return rows;
  }

  void ExpectDeadlineResume(
      const std::vector<GroupedSumRow>& rows, const std::map<int32_t, int64_t>& expected,
      uint64_t control_read_ops, bool backward) {
    EXPECT_EQ(rows.size(), 7u);
    EXPECT_EQ(last_read_stats_.rows_received, 7u);
    EXPECT_EQ(MergeSums(rows), expected);
    EXPECT_GT(last_read_stats_.read_ops, control_read_ops);
    for (size_t i = 1; i < rows.size(); ++i) {
      if (backward)
        EXPECT_LE(rows[i].k, rows[i - 1].k);
      else
        EXPECT_GE(rows[i].k, rows[i - 1].k);
    }
  }

  // FetchSumByR groups by r.
  inline static const std::map<int32_t, int64_t> kIndexSums{{0, 63}, {1, 30}, {2, 13}, {3, 11}};
};

TEST_F(GroupedDeadlineTest, PkBackwardDeadlineResumes) {
  const auto fetch = [&] {
    return FetchSumByK(/*forward=*/false, /*row_limit=*/1024, /*size_limit=*/0);
  };
  const auto control = fetch();
  const auto control_read_ops = last_read_stats_.read_ops;
  const std::map<int32_t, int64_t> expected{{1, 46}, {2, 41}, {3, 30}};
  EXPECT_EQ(MergeSums(control), expected);
  const auto rows = FetchWithDeadline(fetch);
  ExpectDeadlineResume(rows, expected, control_read_ops, /*backward=*/true);
}

TEST_F(GroupedDeadlineTest, IndexForwardDeadlineResumes) {
  const auto fetch = [&] { return FetchSumByR(/*forward=*/true, /*row_limit=*/1024); };
  const auto control = fetch();
  const auto control_read_ops = last_read_stats_.read_ops;
  EXPECT_EQ(MergeSums(control), kIndexSums);
  const auto rows = FetchWithDeadline(fetch);
  ExpectDeadlineResume(rows, kIndexSums, control_read_ops, /*backward=*/false);
}

TEST_F(GroupedDeadlineTest, IndexBackwardDeadlineResumes) {
  const auto fetch = [&] { return FetchSumByR(/*forward=*/false, /*row_limit=*/1024); };
  const auto control = fetch();
  const auto control_read_ops = last_read_stats_.read_ops;
  EXPECT_EQ(MergeSums(control), kIndexSums);
  const auto rows = FetchWithDeadline(fetch);
  ExpectDeadlineResume(rows, kIndexSums, control_read_ops, /*backward=*/true);
}

}  // namespace pggate
}  // namespace yb
