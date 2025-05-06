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
#include <cstdio>
#include <fstream>
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

  Result<PGConn> CreateColocatedDB(const std::string& db_name) {
    PGConn conn = VERIFY_RESULT(ConnectToDB("yugabyte"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", db_name));
    return ConnectToDB(db_name);
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
  // TODO(#18566): In READ COMMITTED we flush buffered operations for each PL/PGSQL statement, so
  //               there won't be any batching.
  auto conn =
      ASSERT_RESULT(SetDefaultTransactionIsolation(Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
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
  // TODO(#18566): In READ COMMITTED we flush buffered operations for each PL/PGSQL statement, so
  //               there won't be any batching.
  auto conn =
      ASSERT_RESULT(SetDefaultTransactionIsolation(Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
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
  //
  // TODO: In READ COMMITTED we flush buffered operations for each PL/PGSQL statement, so there
  //       won't be any batching. This is added here intentionally so that this test starts failing
  //       as soon as we resolve #18566. Enable read committed on all tests in this file, and remove
  //       this this as soon as we resolve #18566
  if (ASSERT_RESULT(EffectiveIsolationLevel(&conn)) == IsolationLevel::READ_COMMITTED)
    ASSERT_EQ(write_rpc_count, 3);
  else
    ASSERT_EQ(write_rpc_count, 2);
}

// The test checks that the 'duplicate key value violates unique constraint' error is correctly
// handled for buffered operations. In the test row with existing ybctid is used (conflict by PK).
TEST_F(PgOpBufferingTest, PKConstraintConflict) {
  // TODO(#18566): In READ COMMITTED we flush buffered operations for each PL/PGSQL statement, so
  //               there won't be any batching.
  auto conn =
      ASSERT_RESULT(SetDefaultTransactionIsolation(Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
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

Status CreateTableWithIndex(PGConn* conn, const std::string& name, int num_indices = 1,
                            bool fk = false, int num_rows = 0, bool temporary_table = false) {
  if (fk) {
    RETURN_NOT_OK(conn->Execute("CREATE TABLE t(k INT PRIMARY KEY)"));
    RETURN_NOT_OK(conn->ExecuteFormat("INSERT INTO t VALUES(generate_series(0, $0))", num_rows));
  }

  // Start building the column definitions (k column + dynamic v columns)
  std::string column_defs = "k INT CONSTRAINT " + PKConstraintName(name) + " PRIMARY KEY";

  // Add dynamic columns v0, v1, v2, ..., vn
  for (int i = 0; i <= num_indices; ++i) {
    column_defs += ", v" + std::to_string(i) + " INT";
  }
  if (fk) {
    // add fk to last column
    column_defs += " REFERENCES t(k)";
  }

  const std::string create_table = temporary_table ?  "CREATE TEMPORARY TABLE " : "CREATE TABLE ";
  const std::string sql = create_table + name + "(" + column_defs + ")";

  // Execute the query to create the table
  RETURN_NOT_OK(conn->ExecuteFormat(sql));

  // After the table is created, create an index for each of the columns v1, v2, ..., vn
  for (int i = 1; i <= num_indices; ++i) {
    std::string index_constraint = name + "_v" + std::to_string(i) + "_idx";
    std::string create_index_sql = "CREATE INDEX " + index_constraint + " ON " + name + "(v" +
                                   std::to_string(i) + ")";
    // Create the index for the column
    RETURN_NOT_OK(conn->ExecuteFormat(create_index_sql));
  }

  return Status::OK();
}

void generateCSVFileForCopy(const std::string& filename, int num_rows, int num_columns) {
  std::remove(filename.c_str());
  std::ofstream temp_file(filename);
  temp_file << "k";
  for (int c = 0; c < num_columns - 1; ++c) {
    temp_file << ",v" << c;
  }
  temp_file << std::endl;
  for (int i = 0; i < num_rows; ++i) {
    temp_file << i;
    for (int c = 0; c < num_columns - 1; ++c) {
      temp_file << "," << i + c;
    }
    temp_file << std::endl;
  }
  temp_file.close();
}

void TestBulkLoadUseFastPathForColocated(PGConn* conn, const std::string& table_name,
    int num_rows, int num_indices, std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  std::string csv_filename = "/tmp/PgOpBufferingTest_copy_test.tmp";
  generateCSVFileForCopy(csv_filename, num_rows, num_indices + 2);
  const int total_write_entries = num_rows * (num_indices + 1);
  ASSERT_OK(CreateTableWithIndex(conn, table_name, num_indices));
  ASSERT_OK(conn->Execute("SET yb_fast_path_for_colocated_copy=true"));
  // will take 2 buffers if not adjusted
  ASSERT_OK(SetMaxBatchSize(conn, total_write_entries - 1));
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn->ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
            table_name, csv_filename);
      }));

  if (num_indices > 0) {
    // the buffer size is adjusted to total_write_entries, all writes will take 1 rpc.
    ASSERT_EQ(write_rpc_count, 1);
  } else {
    // Without index, the buffer size is not adjusted.
    ASSERT_EQ(write_rpc_count, 2);
  }

  // For colocated table, if ROWS_PER_TRANSACTION is set explicitly, distributed transaction
  // will be used, so the buffer batch size will not be adjusted.
  write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, table_name](){
        return conn->ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, ROWS_PER_TRANSACTION 20000, REPLACE)",
            table_name, csv_filename);
      }));
  // the buffer size is not adjusted, all write will take 2 rpc.
  ASSERT_EQ(write_rpc_count, 2);
}

TEST_F(PgOpBufferingTest, BulkLoadUseFastPathForColocatedConsistencyTest) {
  // For colocated table, if ROWS_PER_TRANSACTION is not set explicitly, fast path transaction will
  // be used so the buffer batch size will be adjusted.
  auto conn = ASSERT_RESULT(CreateColocatedDB("colo_db"));
  for (int i = 0; i <= 3; ++i) {
    TestBulkLoadUseFastPathForColocated(&conn, kTable + std::to_string(i), 100,
                                        i, write_rpc_watcher_);
  }
}

// If there is a fk constraint on the colocated table, distributed transaction will be used.
// The buffer size is not adjusted to account for the indexes and hence 2 buffers will be used.
void CheckBulkLoadForColocatedFK(PGConn* conn, const std::string& table_name,
                                 const std::string& csv_filename, int num_rows,
                                 std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 1,
                                 /* fk = */ true, num_rows));
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn->ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
            table_name, csv_filename);
      }));
  ASSERT_EQ(write_rpc_count, 2);
}

// If there is a trigger on the colocated table, distributed transaction will be used.
// The buffer size is not adjusted to account for the indexes and hence 2 buffers will be used.
void CheckBulkLoadForColocatedTrigger(PGConn* conn, const std::string& table_name,
                                      const std::string& csv_filename,
                                      std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 1));
  std::string func_sql =
        "CREATE OR REPLACE FUNCTION update_column() \n"
        "RETURNS TRIGGER AS $$ \n"
        "BEGIN \n"
        "    NEW.v0 := 1; \n"
        "    RETURN NEW; \n"
        "END; \n"
        "$$ LANGUAGE plpgsql;";
  ASSERT_OK(conn->ExecuteFormat(func_sql));
  std::string create_trigger_sql = "CREATE TRIGGER test_trigger AFTER UPDATE ON " + table_name +
      " FOR EACH ROW EXECUTE FUNCTION update_column()";
  ASSERT_OK(conn->ExecuteFormat(create_trigger_sql));

  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn->ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
            table_name, csv_filename);
      }));
  ASSERT_EQ(write_rpc_count, 2);
}

// If a transaction is started explicitly, should use distributed transaction.
// The buffer size is not adjusted to account for the indexes and hence 2 buffers will be used.
void CheckBulkLoadForColocatedExplicitTxn(PGConn* conn, const std::string& table_name,
                                          const std::string& csv_filename,
                                          std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 1));
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn->ExecuteFormat(
            "begin transaction isolation level repeatable read;"
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
            table_name, csv_filename);
      }));
  ASSERT_EQ(write_rpc_count, 2);
}

// For temporary table, should not use fast-path transaction. The data doesn't go through tserver
// so rpc should be 0.
void CheckBulkLoadForColocatedTempTable(PGConn* conn, const std::string& table_name,
                                        const std::string& csv_filename,
                                        std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 1, /* fk = */ false,
      /* num_rows*/ 0, /* temporary_table */ true));
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn->ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
            table_name, csv_filename);
      }));
  ASSERT_EQ(write_rpc_count, 0);
}

// Test the cases which should always use distributed transaction on colocated table.
TEST_F(PgOpBufferingTest, BulkLoadForColocatedUseDistributedTxnTest) {
  std::string csv_filename = "/tmp/PgOpBufferingTest_copy_test.tmp";
  const int num_rows = 100;
  const std::string table_name = kTable;
  generateCSVFileForCopy(csv_filename, num_rows, /* num_columns = */ 3);
  auto conn = ASSERT_RESULT(CreateColocatedDB("colo_db"));
  ASSERT_OK(SetMaxBatchSize(&conn, num_rows * 2 - 1));
  ASSERT_OK(conn.Execute("SET yb_fast_path_for_colocated_copy=true"));

  CheckBulkLoadForColocatedFK(&conn, table_name + "_fk", csv_filename, num_rows,
      write_rpc_watcher_);
  CheckBulkLoadForColocatedTrigger(&conn, table_name + "_trigger", csv_filename,
      write_rpc_watcher_);
  CheckBulkLoadForColocatedExplicitTxn(&conn, table_name + "_txn", csv_filename,
      write_rpc_watcher_);
  CheckBulkLoadForColocatedTempTable(&conn, table_name + "_temporary", csv_filename,
      write_rpc_watcher_);
}

// Test that distributed transaction should be used in the non-colocated case even if rows per
// transaction isn't specified.
TEST_F(PgOpBufferingTest, BulkLoadForNonColocatedTest) {
  std::string csv_filename = "/tmp/PgOpBufferingTest_copy_test.tmp";
  const int num_rows = 100;
  generateCSVFileForCopy(csv_filename, num_rows, /* num_columns = */ 3);
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_name = kTable;
  ASSERT_OK(CreateTableWithIndex(&conn, table_name, /* num_indices = */ 1));
  ASSERT_OK(SetMaxBatchSize(&conn, num_rows * 2 - 1));
  ASSERT_OK(conn.Execute("SET yb_fast_path_for_colocated_copy=true"));
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher_->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn.ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
            table_name, csv_filename);
      }));
  // For non-colcated table, table and index may be located in separate tables, so it takes
  // at least 2 rpc.
  ASSERT_GE(write_rpc_count, 2);
}

// For check constraint on the colocated table, fast-path transaction will be used.
void CheckBulkLoadForColocatedCheckConstraint(
    PGConn* conn, const std::string& table_name,
    const std::string& csv_filename,
    std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 0));

  // add a check which will not lead to violation.
  std::string check_constraint_sql_1 =
       "ALTER TABLE " + table_name + " ADD CONSTRAINT check_constraint_test CHECK (v0 < 1000)";
  ASSERT_OK(conn->ExecuteFormat(check_constraint_sql_1));
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
         return conn->ExecuteFormat(
             "copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
             table_name, csv_filename);
      }));
  // The buffer size will be adjusted so the number of rpc should be 1.
  ASSERT_EQ(write_rpc_count, 1);

  // add a check which will lead to violation, so copy should fail with consistent state.
  ASSERT_OK(conn->ExecuteFormat("DROP TABLE " + table_name));
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 0));
  std::string check_constraint_sql_2 =
      "ALTER TABLE " + table_name + " ADD CONSTRAINT check_constraint_test CHECK (v0 < 99)";
  ASSERT_OK(conn->ExecuteFormat(check_constraint_sql_2));
  const auto status = conn->ExecuteFormat("copy $0 from '$1' WITH (FORMAT CSV,HEADER, REPLACE)",
      table_name, csv_filename);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "violates check constraint");
  auto ret = conn->FetchRowAsString("select * from " + table_name);
  const auto table_row_count = ASSERT_RESULT(
      conn->FetchRow<int64_t>("SELECT COUNT(*) FROM " + table_name));
  ASSERT_EQ(table_row_count, 0);
}

// If there is an unique index on the colocated table, fast-path transaction will be used.
void CheckBulkLoadForColocatedUniqueIndex(PGConn* conn, const std::string& table_name,
                                          const std::string& csv_filename, int num_rows,
                                          std::optional<SingleMetricWatcher>& write_rpc_watcher) {
  ASSERT_OK(CreateTableWithIndex(conn, table_name, /* num_indices = */ 0));
  std::string unique_index_sql = "CREATE UNIQUE INDEX unique_index_test on " + table_name + " (v0)";
  ASSERT_OK(conn->ExecuteFormat(unique_index_sql));

  // the data in cvs file doesn't violate unique constraint.
  auto write_rpc_count = ASSERT_RESULT(write_rpc_watcher->Delta(
      [&conn, &csv_filename, &table_name](){
        return conn->ExecuteFormat(
            "copy $0 from '$1' WITH (FORMAT CSV,HEADER)",
            table_name, csv_filename);
      }));
  // The buffer size will be adjusted so the number of rpc should be 1.
  ASSERT_EQ(write_rpc_count, 1);

  // the data in cvs file violates unique constraint.
  std::ofstream temp_file(csv_filename, std::ios::app);
  temp_file << num_rows << ",0";
  temp_file.close();
  std::string truncate_table_sql = "TRUNCATE TABLE " + table_name;
  ASSERT_OK(conn->ExecuteFormat(truncate_table_sql));
  ASSERT_OK(SetMaxBatchSize(conn, 20000));
  auto status = conn->ExecuteFormat("copy $0 from '$1' WITH (FORMAT CSV,HEADER)",
                                    table_name, csv_filename);
  LOG(INFO) << "copy status: " << status;
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "duplicate key value violates unique constraint");
  auto ret = conn->FetchRowAsString("select * from " + table_name);
  const auto table_row_count = ASSERT_RESULT(
      conn->FetchRow<int64_t>("SELECT COUNT(*) FROM " + table_name));
  ASSERT_EQ(table_row_count, 0);
}

// Test the cases which should always use fast-path transaction on colocated table.
TEST_F(PgOpBufferingTest, BulkLoadForColocatedUseFastPathTxnTest) {
  std::string csv_filename = "/tmp/PgOpBufferingTest_copy_test.tmp";
  const int num_rows = 100;
  const std::string table_name = kTable;
  generateCSVFileForCopy(csv_filename, num_rows, /* num_columns = */ 2);
  auto conn = ASSERT_RESULT(CreateColocatedDB("colo_db"));
  ASSERT_OK(SetMaxBatchSize(&conn, num_rows * 2 - 1));
  ASSERT_OK(conn.Execute("SET yb_fast_path_for_colocated_copy=true"));

  CheckBulkLoadForColocatedCheckConstraint(&conn, table_name + "_check", csv_filename,
      write_rpc_watcher_);
  CheckBulkLoadForColocatedUniqueIndex(&conn, table_name + "_unique_index", csv_filename, num_rows,
      write_rpc_watcher_);
}
} // namespace yb::pgwrapper
