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

#include <cmath>
#include <optional>
#include <string>

#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Write);

namespace yb::pgwrapper {
namespace {

class PgOpBufferingTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
    write_rpc_watcher_.emplace(
        *cluster_->mini_tablet_server(0)->server(),
        METRIC_handler_latency_yb_tserver_TabletServerService_Write);
  }

  size_t NumTabletServers() override {
    return 1;
  }

  std::optional<SingleMetricWatcher> write_rpc_watcher_;
};

const std::string kTable = "test";

std::string PKConstraintName(const std::string& table) {
  return table + "_pk_constraint";
}

Status CreateTable(PGConn* conn, const std::string& name = kTable) {
  RETURN_NOT_OK(conn->ExecuteFormat(
    "CREATE TABLE $0(k INT CONSTRAINT $1 PRIMARY KEY, v INT DEFAULT 1) SPLIT INTO 1 TABLETS",
    name, PKConstraintName(name)));
  return Status::OK();
}

Status EnsureDupKeyError(Status status, const std::string& constraint_name) {
  SCHECK(!status.ok(), IllegalState, "Duplicate key error is expected");
  const auto msg = status.ToString();
  SCHECK(
      msg.find(Format(
          "duplicate key value violates unique constraint \"$0\"",
          constraint_name)) != std::string::npos,
      IllegalState, Format("Unexpected error message: $0", msg));
  return Status::OK();
}

} // namespace

// The test checks that multiple writes into single table with single tablet
// are performed with single RPC. Multiple scenarios are checked.
TEST_F(PgOpBufferingTest, GeneralOptimization) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));

  const auto series_insert_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return conn.ExecuteFormat("INSERT INTO $0 SELECT s FROM generate_series(1, 5) as s", kTable);
  }));
  ASSERT_EQ(series_insert_rpc_count, 1);

  const auto multi_insert_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return conn.ExecuteFormat("INSERT INTO $0 VALUES(11), (12), (13)", kTable);
  }));
  ASSERT_EQ(multi_insert_rpc_count, 1);

  const auto do_insert_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return conn.ExecuteFormat(
        "DO $$$$" \
        "BEGIN" \
        "  INSERT INTO $0 VALUES(21);" \
        "  INSERT INTO $0 VALUES(22);" \
        "  INSERT INTO $0 VALUES(23);" \
        "END$$$$;",
        kTable);
  }));
  ASSERT_EQ(do_insert_rpc_count, 1);

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE PROCEDURE test() LANGUAGE plpgsql AS $$$$" \
      "BEGIN" \
      "  INSERT INTO $0 SELECT s FROM generate_series(101, 105) AS s; " \
      "  INSERT INTO $0 VALUES (111), (112), (113);" \
      "  INSERT INTO $0 VALUES (121);" \
      "  INSERT INTO $0 VALUES (122);" \
      "  INSERT INTO $0 VALUES (123);" \
      "END$$$$;",
      kTable));
  auto proc_insert_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return conn.Execute("CALL test()");
  }));
  ASSERT_EQ(proc_insert_rpc_count, 1);

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE OR REPLACE PROCEDURE test() LANGUAGE sql AS $$$$" \
      "  INSERT INTO $0 SELECT s FROM generate_series(1001, 1005) AS s; " \
      "  INSERT INTO $0 VALUES (1011), (1012), (1013);" \
      "  INSERT INTO $0 VALUES (1021);" \
      "  INSERT INTO $0 VALUES (1022);" \
      "  INSERT INTO $0 VALUES (1023);$$$$", kTable));
  proc_insert_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return conn.Execute("CALL test()");
  }));
  ASSERT_EQ(proc_insert_rpc_count, 1);
}

// The test checks that buffering mechanism splits operations into batches with respect to
// 'ysql_session_max_batch_size' configuration parameter. This parameter can be changed via GUC.
TEST_F(PgOpBufferingTest, MaxBatchSize) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));
  const size_t max_batch_size = 10;
  const size_t max_insert_count = max_batch_size * 3;
  ASSERT_OK(SetMaxBatchSize(&conn, max_batch_size));
  for (size_t i = 0; i < max_insert_count; ++i) {
    const auto items_for_insert = i + 1;
    const auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta(
        [&conn, start = i * 100 + 1, end = i * 100 + items_for_insert]() {
          return conn.ExecuteFormat(
              "INSERT INTO $0 SELECT s FROM generate_series($1, $2) as s", kTable, start, end);
        }));
    ASSERT_EQ(write_rpc_count, std::ceil(static_cast<double>(items_for_insert) / max_batch_size));
  }

  ASSERT_OK(conn.ExecuteFormat("truncate $0", kTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE OR REPLACE PROCEDURE f(first integer, last integer) "
      "LANGUAGE plpgsql "
      "as $$body$$ "
      "BEGIN "
      "  FOR i in first..last LOOP "
      "    INSERT INTO $0 VALUES (i); "
      "  END LOOP; "
      "END; "
      "$$body$$;", kTable));
  for (size_t i = 0; i < max_insert_count; ++i) {
    const auto items_for_insert = i + 1;
    const auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta(
        [&conn, start = i * 100 + 1, end = i * 100 + items_for_insert]() {
          return conn.ExecuteFormat("CALL f($0, $1)", start, end);
        }));
    ASSERT_EQ(write_rpc_count, std::ceil(static_cast<double>(items_for_insert) / max_batch_size));
  }
}

// The test checks that buffering mechanism flushes currently buffered operations in case of
// adding new operation for a row which already has a buffered operation on it.
TEST_F(PgOpBufferingTest, ConflictingOps) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));
  ASSERT_OK(SetMaxBatchSize(&conn, 5));
  const auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return EnsureDupKeyError(
        conn.ExecuteFormat(
            "DO $$$$" \
            "BEGIN" \
            "  INSERT INTO $0 VALUES(1);" \
            "  INSERT INTO $0 VALUES(2);" \
            "  INSERT INTO $0 VALUES(1);" \
            "  INSERT INTO $0 VALUES(7);" \
            "END$$$$;",
            kTable),
        PKConstraintName(kTable));
  }));
  ASSERT_EQ(write_rpc_count, 2);
}

// The test checks that the 'duplicate key value violates unique constraint' error is correctly
// handled for buffered operations. In the test row with existing ybctid is used (conflict by PK).
TEST_F(PgOpBufferingTest, PKConstraintConflict) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1)", kTable));
  const auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn]() {
    return EnsureDupKeyError(
        conn.ExecuteFormat(
            "DO $$$$" \
            "BEGIN" \
            "  INSERT INTO $0 VALUES(5), (4), (3);" \
            "  INSERT INTO $0 VALUES(2);" \
            "  INSERT INTO $0 VALUES(1);" \
            "END$$$$;",
            kTable),
        PKConstraintName(kTable));
  }));
  ASSERT_EQ(write_rpc_count, 1);
}

// The test checks that the 'duplicate key value violates unique constraint' error is correctly
// handled for buffered operations. In the test row with existing value for column with unique
// index is used (conflict by unique index).
TEST_F(PgOpBufferingTest, UniqueIndexConstraintConflict) {
  auto conn = ASSERT_RESULT(Connect());
  const std::string index_constraint = "index_constraint";
  ASSERT_OK(CreateTable(&conn));
  ASSERT_OK(conn.ExecuteFormat(
    "CREATE UNIQUE INDEX $0 ON $1(v)", index_constraint, kTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 10)", kTable));
  const auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta(
      [&conn, &index_constraint](){
        return EnsureDupKeyError(
          conn.ExecuteFormat("INSERT INTO $0 VALUES(3, 30), (2, 20), (100, 10)", kTable),
          index_constraint);
      }));
  // Writes will be performed into 2 tablets: table and index.
  // This is the reason why expected write rpc is 2
  ASSERT_EQ(write_rpc_count, 2);
}

// The test checks that the 'duplicate key value violates unique constraint' error is correctly
// handled in case buffer contains multiple operations which violates different constraints.
// In this case the first error should be reported. And also no operations should be flushed
// after error detection.
TEST_F(PgOpBufferingTest, MultipleConstraintsConflict) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));
  const std::string aux_table = "aux_test";
  ASSERT_OK(CreateTable(&conn, aux_table));
  ASSERT_OK(SetMaxBatchSize(&conn, 3));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1)", kTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1)", aux_table));
  const auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta([&conn, &aux_table]() {
    return EnsureDupKeyError(
        conn.ExecuteFormat(
            "DO $$$$" \
            "BEGIN" \
            "  INSERT INTO $0 VALUES(3), (2), (1);" \
            "  INSERT INTO $1 VALUES(1);" \
            "  INSERT INTO $0 VALUES(2);" \
            "END$$$$;",
            kTable,
            aux_table),
        PKConstraintName(kTable));
  }));
  ASSERT_EQ(write_rpc_count, 1);
}

// The test checks that insert into table with FK constraint raises non error in case of
// non-transactional writes is activated.
TEST_F(PgOpBufferingTest, FKCheckWithNonTxnWrites) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("CREATE TABLE ref_t(k INT PRIMARY KEY,"
                         "                   t_pk INT REFERENCES t(k)) SPLIT INTO 1 TABLETS"));
  constexpr auto kInsertRowCount = 300;
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1)"));
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = 1"));
  // Warm-up postgres sys cache (FK triggers)
  ASSERT_OK(conn.Execute("INSERT INTO ref_t VALUES(0, 1)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO ref_t SELECT s, 1 FROM generate_series(1, $0) AS s", kInsertRowCount - 1));

  const auto table_row_count = ASSERT_RESULT(
      conn.FetchRow<int64_t>("SELECT COUNT(*) FROM ref_t"));
  ASSERT_EQ(kInsertRowCount, table_row_count);
}

// The test checks that transaction will be rolled back after completion of all in-flight
// operations.
TEST_F(PgOpBufferingTest, TxnRollbackWithInFlightOperations) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k TEXT PRIMARY KEY)"));
  constexpr size_t kMaxItems = 10000;
  ASSERT_OK(conn.ExecuteFormat("SET ysql_max_in_flight_ops=$0", kMaxItems));
  constexpr size_t kMaxBatchSize = 3072;
  ASSERT_OK(SetMaxBatchSize(&conn, kMaxBatchSize));
  // Next statement will fail due to 'division by zero' after performing some amount of write RPCs.
  ASSERT_NOK(conn.ExecuteFormat(
      "INSERT INTO t SELECT CONCAT('k_', (s+($0-s)/($0-s))::text) FROM generate_series(1, $1) AS s",
       kMaxBatchSize * 3 + 1,
       kMaxItems));
  ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
}

} // namespace yb::pgwrapper
