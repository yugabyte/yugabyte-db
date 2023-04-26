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

#include "yb/common/ybc_util.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/range.h"
#include "yb/util/stopwatch.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"


DECLARE_bool(rocksdb_use_logging_iterator);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row_for_colocated_table);

namespace yb::pgwrapper {

class PgSingleTServerTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 1;
  }

  void SetupColocatedTableAndRunBenchmark(
      const std::string& create_table_cmd, const std::string& insert_cmd,
      const std::string& select_cmd, int rows, int block_size, int reads, bool compact,
      bool aggregate) {
    const std::string kDatabaseName = "testdb";
    auto conn = ASSERT_RESULT(Connect());

    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with COLOCATION = true", kDatabaseName));

    conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

    ASSERT_OK(conn.Execute(create_table_cmd));
    auto last_row = 0;
    while (last_row < rows) {
      auto first_row = last_row + 1;
      last_row = std::min(rows, last_row + block_size);
      ASSERT_OK(conn.ExecuteFormat(insert_cmd, first_row, last_row));
    }

    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto tp = peer->tablet()->transaction_participant();
      if (tp) {
        const auto count_intents_result = tp->TEST_CountIntents();
        const auto count_intents = count_intents_result.ok() ? count_intents_result->first : 0;
        LOG(INFO) << peer->LogPrefix() << "Intents: " << count_intents;
      }
    }

    if (compact) {
      FlushAndCompactTablets();
    }

    LOG(INFO) << "Perform read. Row count: " << rows;

    if (VLOG_IS_ON(4)) {
      google::SetVLOGLevel("intent_aware_iterator", 4);
      google::SetVLOGLevel("docdb_rocksdb_util", 4);
      google::SetVLOGLevel("docdb", 4);
    }

    for (int i = 0; i != reads; ++i) {
      int64_t fetched_rows;
      auto start = MonoTime::Now();
      if (aggregate) {
        fetched_rows = ASSERT_RESULT(conn.FetchValue<PGUint64>(select_cmd));
      } else {
        auto res = ASSERT_RESULT(conn.Fetch(select_cmd));
        fetched_rows = PQntuples(res.get());
      }
      auto finish = MonoTime::Now();
      ASSERT_EQ(rows, fetched_rows);
      LOG(INFO) << i << ") Full Time: " << finish - start;
    }
  }
};

TEST_F(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(ManyRowsInsert)) {
  constexpr int kRows = RegularBuildVsDebugVsSanitizers(100000, 10000, 1000);
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

  auto start = MonoTime::Now();
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t SELECT generate_series(1, $0)", kRows));
  auto finish = MonoTime::Now();
  LOG(INFO) << "Time: " << finish - start;
}

class PgMiniBigPrefetchTest : public PgSingleTServerTest {
 public:
  void SetUp() override {
    yb_fetch_row_limit = 20000000;
    PgSingleTServerTest::SetUp();
  }

  void Run(int rows, int block_size, int reads, bool compact = false, bool select = false) {
    const std::string create_cmd = "CREATE TABLE t (a int PRIMARY KEY)";
    const std::string insert_cmd = "INSERT INTO t SELECT generate_series($0, $1)";
    const std::string select_cmd = select ? "SELECT * FROM t" : "SELECT count(*) FROM t";
    SetupColocatedTableAndRunBenchmark(
        create_cmd, insert_cmd, select_cmd, rows, block_size, reads, compact,
        /* aggregate = */ !select);
  }
};

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(BigRead), PgMiniBigPrefetchTest) {
  constexpr int kRows = RegularBuildVsDebugVsSanitizers(1000000, 100000, 10000);
  constexpr int kBlockSize = 1000;
  constexpr int kReads = 3;

  Run(kRows, kBlockSize, kReads);
}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(BigReadWithCompaction),
          PgMiniBigPrefetchTest) {
  constexpr int kRows = RegularBuildVsDebugVsSanitizers(1000000, 100000, 10000);
  constexpr int kBlockSize = 1000;
  constexpr int kReads = 3;

  Run(kRows, kBlockSize, kReads, /* compact= */ true);
}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(SmallRead), PgMiniBigPrefetchTest) {
  constexpr int kRows = 10;
  constexpr int kBlockSize = kRows;
  constexpr int kReads = 1;

  Run(kRows, kBlockSize, kReads);
}

namespace {

constexpr int kScanRows = RegularBuildVsDebugVsSanitizers(1000000, 100000, 10000);
constexpr int kScanBlockSize = 1000;
constexpr int kScanReads = 3;

}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(Scan), PgMiniBigPrefetchTest) {
  FLAGS_ysql_enable_packed_row = false;
  FLAGS_ysql_enable_packed_row_for_colocated_table = false;
  Run(kScanRows, kScanBlockSize, kScanReads, /* compact= */ false, /* select= */ true);
}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(ScanWithPackedRow), PgMiniBigPrefetchTest) {
  constexpr int kNumColumns = 10;

  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;

  std::string create_cmd = "CREATE TABLE t (a int PRIMARY KEY";
  std::string insert_cmd = "INSERT INTO t VALUES (generate_series($0, $1)";
  for (auto column : Range(kNumColumns)) {
    create_cmd += Format(", c$0 INT", column);
    insert_cmd += ", trunc(random()*100000000)";
  }
  create_cmd += ")";
  insert_cmd += ")";
  const std::string select_cmd = "SELECT * FROM t";
  SetupColocatedTableAndRunBenchmark(
      create_cmd, insert_cmd, select_cmd, kScanRows, kScanBlockSize, kScanReads,
      /* compact= */ false, /* aggregate = */ false);
}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(ScanWithCompaction), PgMiniBigPrefetchTest) {
  Run(kScanRows, kScanBlockSize, kScanReads, /* compact= */ true, /* select= */ true);
}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(ScanSkipPK), PgMiniBigPrefetchTest) {
  constexpr auto kNumRows = kScanRows / 2;
  constexpr int kNumKeyColumns = 5;

  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_enable_packed_row_for_colocated_table = true;

  std::string create_cmd = "CREATE TABLE t (";
  std::string pk = "";
  std::string insert_cmd = "INSERT INTO t VALUES (";
  for (const auto column : Range(kNumKeyColumns)) {
    create_cmd += Format("r$0 TEXT, ", column);
    if (!pk.empty()) {
      pk += ", ";
    }
    pk += Format("r$0", column);
    insert_cmd += "MD5(random()::text), ";
  }
  create_cmd += "value INT, PRIMARY KEY (" + pk + "))";
  insert_cmd += "generate_series($0, $1))";
  const std::string select_cmd = "SELECT value FROM t";
  SetupColocatedTableAndRunBenchmark(
      create_cmd, insert_cmd, select_cmd, kNumRows, kScanBlockSize, kScanReads,
      /* compact= */ false, /* aggregate = */ false);
}

TEST_F(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(BigValue)) {
  constexpr size_t kValueSize = 32_MB;
  constexpr int kKey = 42;
  const std::string kValue = RandomHumanReadableString(kValueSize);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (a int PRIMARY KEY, b TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t VALUES ($0, '$1')", kKey, kValue));

  auto start = MonoTime::Now();
  auto result = ASSERT_RESULT(conn.FetchValue<std::string>(
      Format("SELECT md5(b) FROM t WHERE a = $0", kKey)));
  auto finish = MonoTime::Now();
  LOG(INFO) << "Passed: " << finish - start << ", result: " << result;
}

class PgNoPrefetchTest : public PgSingleTServerTest {
 protected:
  void SetUp() override {
    yb_fetch_row_limit = 1;
    PgSingleTServerTest::SetUp();
  }

  void Run(int rows, int block_size, int reads) {
    const std::string create_cmd = "CREATE TABLE t (a int PRIMARY KEY)";
    const std::string insert_cmd = "INSERT INTO t SELECT generate_series($0, $1)";
    const std::string select_cmd = "SELECT * from t where a in (SELECT generate_series(1, 10000))";
    SetupColocatedTableAndRunBenchmark(
        create_cmd, insert_cmd, select_cmd, rows, block_size, reads, /* compact = */ true,
        /* aggregate = */ false);
  }
};

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(SingleRowScan), PgNoPrefetchTest) {
  constexpr int kRows = RegularBuildVsDebugVsSanitizers(10000, 1000, 100);
  constexpr int kBlockSize = 100;
  constexpr int kReads = 3;

  Run(kRows, kBlockSize, kReads);
}

// Microbenchmark, see
// https://github.com/yugabyte/benchbase/blob/main/config/yugabyte/scan_workloads/yb_colocated/
// scanG7_colo_pkey_rangescan_fullTableScan_increasingColumn.yaml
TEST_F(PgSingleTServerTest, YB_DISABLE_TEST(PerfScanG7RangePK100Columns)) {
    FLAGS_ysql_enable_packed_row = true;
    FLAGS_ysql_enable_packed_row_for_colocated_table = true;

  constexpr auto kDatabaseName = "testdb";
  constexpr auto kNumColumns = 100;
  constexpr auto kNumRows = RegularBuildVsDebugVsSanitizers(10'000, 1000, 100);
  constexpr auto kNumScansPerIteration = 10;
  constexpr auto kNumIterations = 3;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  {
    std::string create_stmt = "CREATE TABLE t(col_bigint_id_1 bigint,";
    for (int i = 1; i <= kNumColumns; ++i) {
      create_stmt += Format("col_bigint_$0 bigint, ", i);
    }
    create_stmt += "PRIMARY KEY(col_bigint_id_1 ASC));";
    ASSERT_OK(conn.Execute(create_stmt));
  }

  ASSERT_OK(
      conn.Execute("CREATE FUNCTION random_between(low INT, high INT) RETURNS INT AS \n"
                   "$$\n"
                   "BEGIN\n"
                   "   RETURN floor(random()*(high-low+1)+low);\n"
                   "END;\n"
                   "$$ LANGUAGE plpgsql STRICT;"));

  {
    LOG(INFO) << "Loading data...";

    Stopwatch s(Stopwatch::ALL_THREADS);
    s.start();

    std::string load_stmt = "INSERT INTO t SELECT i";
    for (int i = 1; i <= kNumColumns; ++i) {
      load_stmt += ", random_between(1, 1000000)";
    }
    load_stmt += Format(" FROM generate_series(1, $0) as i;", kNumRows);
    ASSERT_OK(conn.ExecuteFormat(load_stmt));

    s.stop();
    LOG(INFO) << "Load took: " << AsString(s.elapsed());
  }

  FlushAndCompactTablets();

  const auto rows_inserted = ASSERT_RESULT(conn.FetchValue<int64_t>("SELECT COUNT(*) FROM t"));
  LOG(INFO) << "Rows inserted: " << rows_inserted;
  ASSERT_EQ(rows_inserted, kNumRows);

  for (int i = 0; i < kNumIterations; ++i) {
    Stopwatch s(Stopwatch::ALL_THREADS);
    s.start();

    for (int j = 0; j < kNumScansPerIteration; ++j) {
      auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t WHERE col_bigint_id_1>1"));
      ASSERT_EQ(PQntuples(res.get()), kNumRows - 1);
    }

    s.stop();
    LOG(INFO) << kNumScansPerIteration << " scan(s) took: " << AsString(s.elapsed());
  }
}

TEST_F_EX(PgSingleTServerTest, YB_DISABLE_TEST_IN_TSAN(ColocatedJoinPerformance),
          PgNoPrefetchTest) {
  const std::string kDatabaseName = "testdb";
  constexpr int kNumRows = RegularBuildVsDebugVsSanitizers(10000, 1000, 100);
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));

  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  ASSERT_OK(conn.Execute("CREATE TABLE t1(k INT PRIMARY KEY, v1 INT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2(k INT PRIMARY KEY, v2 INT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t2 SELECT s, s FROM generate_series(1, $0) AS s", kNumRows));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT s, s FROM generate_series(1, $0) AS s", kNumRows));

  auto start = MonoTime::Now();
  auto res = ASSERT_RESULT(conn.FetchValue<int32_t>(
      "SELECT v1 + v2 FROM t1 INNER JOIN t2 ON (t1.k = t2.k) WHERE v2 < 2 OR v1 < 2"));
  auto finish = MonoTime::Now();
  ASSERT_EQ(res, 2);
  LOG(INFO) << "Time: " << finish - start;
}

// ------------------------------------------------------------------------------------------------
// Backward scan on an index
// ------------------------------------------------------------------------------------------------

class PgBackwardIndexScanTest : public PgSingleTServerTest {
 protected:
  void BackwardIndexScanTest(bool uncommitted_intents) {
    auto conn = ASSERT_RESULT(Connect());

    ASSERT_OK(conn.Execute(R"#(
        create table events_backwardscan (

          log       text not null,
          src       text not null,
          inserted  timestamp(3) without time zone not null,
          created   timestamp(3) without time zone not null,
          data      jsonb not null,

          primary key (log, src, created)
        );
      )#"));
    ASSERT_OK(conn.Execute("create index on events_backwardscan (inserted asc);"));

    for (int day = 1; day <= 31; ++day) {
      ASSERT_OK(conn.ExecuteFormat(R"#(
          insert into events_backwardscan

          select
            'log',
            'src',
            t,
            t,
            '{}'

          from generate_series(
            timestamp '2020-01-$0 00:00:00',
            timestamp '2020-01-$0 23:59:59',
            interval  '1 minute'
          )

          as t(day);
      )#", day));
    }

    std::optional<PGConn> uncommitted_intents_conn;
    if (uncommitted_intents) {
      uncommitted_intents_conn = ASSERT_RESULT(Connect());
      ASSERT_OK(uncommitted_intents_conn->Execute("BEGIN"));
      auto ts = "1970-01-01 00:00:00";
      ASSERT_OK(uncommitted_intents_conn->ExecuteFormat(
          "insert into events_backwardscan values ('log', 'src', '$0', '$0', '{}')", ts, ts));
    }

    auto count = ASSERT_RESULT(
        conn.FetchValue<PGUint64>("SELECT COUNT(*) FROM events_backwardscan"));
    LOG(INFO) << "Total rows inserted: " << count;

    auto select_result = ASSERT_RESULT(conn.Fetch(
        "select * from events_backwardscan order by inserted desc limit 100"
    ));
    ASSERT_EQ(PQntuples(select_result.get()), 100);

    if (uncommitted_intents) {
      ASSERT_OK(uncommitted_intents_conn->Execute("ROLLBACK"));
    }
  }
};

TEST_F_EX(PgSingleTServerTest,
          YB_DISABLE_TEST_IN_TSAN(BackwardIndexScanNoIntents),
          PgBackwardIndexScanTest) {
  BackwardIndexScanTest(/* uncommitted_intents */ false);
}

TEST_F_EX(PgSingleTServerTest,
          YB_DISABLE_TEST_IN_TSAN(BackwardIndexScanWithIntents),
          PgBackwardIndexScanTest) {
  BackwardIndexScanTest(/* uncommitted_intents */ true);
}

class PgRocksDbIteratorLoggingTest : public PgSingleTServerTest {
 public:
  struct IteratorLoggingTestConfig {
    int num_non_pk_columns;
    int num_rows;
    int num_overwrites;
    int first_row_to_scan;
    int last_row_to_scan;
  };

  void RunIteratorLoggingTest(const IteratorLoggingTestConfig& config) {
    auto conn = ASSERT_RESULT(Connect());

    std::string non_pk_columns_schema;
    std::string non_pk_column_names;
    for (int i = 0; i < config.num_non_pk_columns; ++i) {
      non_pk_columns_schema += Format(", $0 TEXT", GetNonPkColName(i));
      non_pk_column_names += Format(", $0", GetNonPkColName(i));
    }
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t (pk TEXT, PRIMARY KEY (pk ASC)$0)",
                                 non_pk_columns_schema));
    // Delete and overwrite every row multiple times.
    for (int overwrite_index = 0; overwrite_index < config.num_overwrites; ++overwrite_index) {
      for (int row_index = 0; row_index < config.num_rows; ++row_index) {
        std::string non_pk_values;
        for (int non_pk_col_index = 0;
             non_pk_col_index < config.num_non_pk_columns;
             ++non_pk_col_index) {
          non_pk_values += Format(", '$0'", GetNonPkColValue(
              non_pk_col_index, row_index, overwrite_index));
        }

        const auto pk_value = GetPkForRow(row_index);
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO t(pk$0) VALUES('$1'$2)", non_pk_column_names, pk_value, non_pk_values));
        if (overwrite_index != config.num_overwrites - 1) {
          ASSERT_OK(conn.ExecuteFormat("DELETE FROM t WHERE pk = '$0'", pk_value));
        }
      }
    }
    const auto first_pk_to_scan = GetPkForRow(config.first_row_to_scan);
    const auto last_pk_to_scan = GetPkForRow(config.last_row_to_scan);
    auto count_stmt_str = Format(
        "SELECT COUNT(*) FROM t WHERE pk >= '$0' AND pk <= '$1'",
        first_pk_to_scan,
        last_pk_to_scan);
    // Do the same scan twice, and only turn on iterator logging on the second scan.
    // This way we won't be logging system table operations needed to fetch PostgreSQL metadata.
    for (bool is_warmup : {true, false}) {
      if (!is_warmup) {
        SetAtomicFlag(true, &FLAGS_rocksdb_use_logging_iterator);
      }
      auto actual_num_rows = ASSERT_RESULT(conn.FetchValue<PGUint64>(count_stmt_str));
      const int expected_num_rows = config.last_row_to_scan - config.first_row_to_scan + 1;
      ASSERT_EQ(expected_num_rows, actual_num_rows);
    }
    SetAtomicFlag(false, &FLAGS_rocksdb_use_logging_iterator);
  }

 private:
  std::string GetNonPkColName(int non_pk_col_index) {
    return Format("non_pk_col$0", non_pk_col_index);
  }

  std::string GetPkForRow(int row_index) {
    return Format("PrimaryKeyForRow$0", row_index);
  }

  std::string GetNonPkColValue(int non_pk_col_index, int row_index, int overwrite_index) {
    return Format("NonPkCol$0ValueForRow$1Overwrite$2",
                  non_pk_col_index, row_index, overwrite_index);
  }
};

TEST_F_EX(PgSingleTServerTest,
          YB_DISABLE_TEST_IN_TSAN(IteratorLogPkOnly), PgRocksDbIteratorLoggingTest) {
  RunIteratorLoggingTest({
    .num_non_pk_columns = 0,
    .num_rows = 5,
    .num_overwrites = 100,
    .first_row_to_scan = 1,  // 0-based
    .last_row_to_scan = 3,
  });
}

TEST_F_EX(PgSingleTServerTest,
          YB_DISABLE_TEST_IN_TSAN(IteratorLogTwoNonPkCols), PgRocksDbIteratorLoggingTest) {
  RunIteratorLoggingTest({
    .num_non_pk_columns = 2,
    .num_rows = 5,
    .num_overwrites = 100,
    .first_row_to_scan = 1,  // 0-based
    .last_row_to_scan = 3,
  });
}

}  // namespace yb::pgwrapper
