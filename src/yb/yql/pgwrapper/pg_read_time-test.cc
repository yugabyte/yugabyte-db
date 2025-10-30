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
#include <fstream>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/status.h"
#include "yb/util/string_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/tools/tools_test_utils.h"

DECLARE_bool(enable_wait_queues);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(ysql_enable_write_pipelining);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_uint64(max_clock_skew_usec);

METRIC_DECLARE_counter(picked_read_time_on_docdb);

namespace yb::pgwrapper {

using namespace std::literals;

namespace {

class PgReadTimeTest : public PgMiniTestBase {
 protected:
  using StmtExecutor = std::function<void()>;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    // Disable auto analyze because it introduces flakiness to
    // metric: METRIC_picked_read_time_on_docdb.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;

    // TODO: Remove yb_lock_pk_single_rpc once it becomes the default.
    // yb_max_query_layer_retries is required for TestConflictRetriesOnDocdb
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        "yb_lock_pk_single_rpc=true," + MaxQueryLayerRetriesConf(0);

    PgMiniTestBase::SetUp();
  }

  uint64_t GetNumPickedReadTimeOnDocDb() {
    uint64_t num_pick_read_time_on_docdb = 0;
    for (const auto& mini_tablet_server : cluster_->mini_tablet_servers()) {
      auto peers = mini_tablet_server->server()->tablet_manager()->GetTabletPeers();
      for (const auto& peer : peers) {
        auto tablet = peer->shared_tablet_maybe_null();
        if (!tablet) {
          continue;
        }
        auto counter =
            METRIC_picked_read_time_on_docdb.Instantiate(tablet->GetTabletMetricsEntity());
        num_pick_read_time_on_docdb += counter->value();
      }
    }
    return num_pick_read_time_on_docdb;
  }

  void CheckReadTimePickedOnDocdb(
      const StmtExecutor& stmt_executor,
      uint64_t expected_num_picked_read_time_on_doc_db_metric = 1) {
    CheckReadTimePickingLocation(stmt_executor, expected_num_picked_read_time_on_doc_db_metric);
  }

  void CheckReadTimeProvidedToDocdb(const StmtExecutor& stmt_executor) {
    CheckReadTimePickingLocation(
        stmt_executor, 0 /* expected_num_picked_read_time_on_doc_db_metric */);
  }

  static Status ExecuteCopyFromCSV(
      PGConn& conn, const std::string_view& table, const std::string_view& file_name) {
    return conn.ExecuteFormat("COPY $0 FROM '$1' WITH (FORMAT CSV, HEADER)", table, file_name);
  }

 private:
  void CheckReadTimePickingLocation(
      const StmtExecutor& stmt_executor, uint64_t expected_num_picked_read_time_on_doc_db_metric) {
    const auto initial = GetNumPickedReadTimeOnDocDb();
    stmt_executor();
    const auto diff = GetNumPickedReadTimeOnDocDb() - initial;
    ASSERT_EQ(diff, expected_num_picked_read_time_on_doc_db_metric);
  }
};

inline Status SetHighMaxBatchSize(PGConn* conn) {
  return SetMaxBatchSize(conn, 1024);
}

inline Status ResetMaxBatchSize(PGConn* conn) {
  return SetMaxBatchSize(conn, 0);
}

} // namespace

TEST_F(PgReadTimeTest, TestConflictRetriesOnDocdb) {
  auto conn = ASSERT_RESULT(Connect());
  const std::string kTable = "test";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT generate_series(1, 100), 0", kTable));

  auto conn_2 = ASSERT_RESULT(Connect());
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor(
      [&stop = thread_holder.stop_flag(), &conn_2]() {
        while (!stop.load(std::memory_order_acquire)) {
          ASSERT_OK(conn_2.Execute("UPDATE test SET v=0 WHERE k=1"));
          // Use a distributed transaction scenario as well.
          ASSERT_OK(conn_2.StartTransaction(IsolationLevel::READ_COMMITTED));
          ASSERT_OK(conn_2.Execute("UPDATE test SET v=0 WHERE k=1"));
          ASSERT_OK(conn_2.CommitTransaction());
        }
      });

  size_t kNumIterations = 100;
  for (size_t i = 0; i < kNumIterations; ++i) {
    // Docdb is able to retry kConflict errors in this case because the read time is not picked on
    // the query layer for the below SELECT FOR UPDATE.
    //
    // If there were no retries for kConflict on docdb, then the following statement would fail
    // because yb_max_query_layer_retries=0 and so there are no query layer retries.
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.Fetch("SELECT * FROM test WHERE k=1 FOR UPDATE"));
    ASSERT_OK(conn.CommitTransaction());
  }

  bool serialization_error_seen = false;
  for (size_t i = 0; i < kNumIterations; ++i) {
    // In the below case, we expect some kConflict errors since the read time is already picked on
    // the first select.
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.Fetch("SELECT * FROM test WHERE k=1"));
    auto res = conn.Fetch("SELECT * FROM test WHERE k=1 FOR UPDATE");
    if (!res.ok()) {
      // ERRCODE_T_R_SERIALIZATION_FAILURE
      const auto& status = res.status();
      ASSERT_TRUE(HasSubstring(status.ToString(), "pgsql error 40001")) << status.ToString();
      serialization_error_seen = true;
      ASSERT_OK(conn.RollbackTransaction());
      continue;
    }
    ASSERT_OK(conn.CommitTransaction());
  }
  ASSERT_TRUE(serialization_error_seen);
}

TEST_F(PgReadTimeTest, CheckReadTimePickingLocation) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  constexpr auto kTable = "test"sv;
  constexpr auto kSingleTabletTable = "test_with_single_tablet"sv;
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS", kSingleTabletTable));

  for (const auto& table_name : {kTable, kSingleTabletTable}) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 100), 0", table_name));
    ASSERT_OK(conn.ExecuteFormat(
      "CREATE OR REPLACE PROCEDURE insert_rows_$0(first integer, last integer) "
      "LANGUAGE plpgsql "
      "as $$body$$ "
      "BEGIN "
      "  FOR i in first..last LOOP "
      "    INSERT INTO $0 VALUES (i, i); "
      "  END LOOP; "
      "END; "
      "$$body$$", table_name));
  }

  // Cases to test:
  // --------------
  //
  // 1. Batches of operations sent by YSQL to docdb (each is a Perform rpc) might arrive in 2 ways:
  //    Pipeline or not-pipelined (i.e., new one is sent after response is received).
  //
  // 2. Within both cases, the first rpc i.e., batch of operations from YSQL might have:
  //   i) single operation,
  //   ii) multiple operations to various tablets, or
  //   iii) multiple operations but to the same tablet
  //
  // 3. The first batch might start a new distributed transaction or not need one yet.
  //
  // The above result in 12 cases totally, but we have only 9 cases because today there is no case
  // with pipelined operations that don't start a distributed transaction (hence minus those 3
  // cases).
  //
  // The below tests check if the first batch of operations pick a read time on docdb instead of the
  // query layer where applicable.

  // 1. no pipeline, single operation in first batch, no distributed txn
  for (const auto& table_name : {kTable, kSingleTabletTable}) {
    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=1 WHERE k=1", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1000, 1000)", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE k=1000", table_name));
        });
  }

  // 2. no pipeline, multiple operations to various tablets in first batch, no distributed txn
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kTable));
      });

  // 3. no pipeline, multiple operations to the same tablet in first batch, no distributed txn
  CheckReadTimePickedOnDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kSingleTabletTable));
      });

  // 4. no pipeline, single operation in first batch, starts a distributed transation
  //
  // expected_num_picked_read_time_on_doc_db_metric is set because in case of a SELECT FOR UPDATE,
  // a read time is picked in read_query.cc, but an extra picking is done in write_query.cc just
  // after conflict resolution is done (see DoTransactionalConflictsResolved()).
  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1 FOR UPDATE", kTable));
        ASSERT_OK(conn.CommitTransaction());
      }, 2 /* expected_num_picked_read_time_on_doc_db_metric */);

  // 5. no pipeline, multiple operations to various tablets in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetHighMaxBatchSize(&conn));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(101, 110)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 6. no pipeline, multiple operations to the same tablet in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetHighMaxBatchSize(&conn));
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(101, 110)", kSingleTabletTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 7. Pipeline, single operation in first batch, starts a distributed transation
  ASSERT_OK(SetMaxBatchSize(&conn, 1));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(111, 120)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 8. Pipeline, multiple operations to various tablets in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(121, 150)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 9. Pipeline, multiple operations to the same tablet in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(121, 150)", kSingleTabletTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // Test cases in a read committed txn block (in this isolation level each statement uses a new
  // latest read point). For each statement, if the new read time for that statement can be picked
  // on docdb, ensure it is.
  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  // Disable async writes, since the metrics are only updated after the entire write query including
  // the quorum commit completes. The client conn wont wait for these until the final commit.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_write_pipelining) = false;
  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1", kTable));
      });

  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=1 WHERE k=1", kTable));
      });

  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kTable));
      });

  CheckReadTimePickedOnDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kSingleTabletTable));
      });

  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1 FOR UPDATE", kTable));
      }, 2 /* expected_num_picked_read_time_on_doc_db_metric */);

  ASSERT_OK(conn.CommitTransaction());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_write_pipelining) = true;

  // 10. Pipeline, copy a file to a table by fast-path transation. Only single tserver is involved
  // during copy.
  // (1) Copy single row to table with multiple tserver
  ASSERT_OK(SetMaxBatchSize(&conn, 1024));
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = 1"));
  const auto csv_filename = GetTestPath("pg_read_time-test-fastpath-copy.csv");
  GenerateCSVFileForCopy(csv_filename, 1);
  CheckReadTimePickedOnDocdb(
      [&conn, kTable, &csv_filename] {
        ASSERT_OK(ExecuteCopyFromCSV(conn, kTable, csv_filename));
      }, 1);

  // (2) Copy multiple rows to table with single tserver
  GenerateCSVFileForCopy(csv_filename, 100, 2 /* num_columns */, 10000 /* offset */);
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = 1"));
  CheckReadTimePickedOnDocdb(
      [&conn, kSingleTabletTable, &csv_filename]() {
        ASSERT_OK(ExecuteCopyFromCSV(conn, kSingleTabletTable, csv_filename));
      }, 1);

  ASSERT_OK(conn.Execute("RESET yb_disable_transactional_writes"));
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // (3) Copy to colocated table with index. Batch size is 10, so 20 batches will be sent to docdb.
  constexpr auto dbName = "colo_db";
  constexpr auto kColocatedTable = "test_with_colocated_tablet";
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", dbName));
  auto conn_colo = ASSERT_RESULT(ConnectToDB(dbName));
  ASSERT_OK(SetMaxBatchSize(&conn_colo, 10));
  ASSERT_OK(conn_colo.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kColocatedTable));
  ASSERT_OK(conn_colo.ExecuteFormat(
      "CREATE INDEX $0_index ON $1(v)", kColocatedTable, kColocatedTable));
  ASSERT_OK(conn_colo.Execute("SET yb_fast_path_for_colocated_copy = 1"));
  CheckReadTimePickedOnDocdb(
      [&conn_colo, &csv_filename]() {
        ASSERT_OK(ExecuteCopyFromCSV(conn_colo, kColocatedTable, csv_filename));
      }, 20);
  ASSERT_OK(conn_colo.Execute("RESET yb_fast_path_for_colocated_copy"));
  ASSERT_OK(ResetMaxBatchSize(&conn_colo));

  // 11. For index backfill, read time is set by postgres, so should never set read time in docdb
  // (1) create an index on tables which already have some data
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0_index ON $0(v)", kTable));
      });
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0_index ON $0(v)", kSingleTabletTable));
      });
  // (2) Drop index on tables  which already have some data
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0_index", kTable));
      });
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0_index", kSingleTabletTable));
      });
}

TEST_F(PgReadTimeTest, TestFastPathCopyDuplicateKeyOnColocated) {
  constexpr auto dbName = "colo_db";
  constexpr auto kColocatedTable = "test_fast_path_copy_colocated";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", dbName));
  const auto csv_filename = GetTestPath("pg_read_time-test-fastpath-copy.csv");
  GenerateCSVFileForCopy(csv_filename, 200000);
  auto conn_colo = ASSERT_RESULT(ConnectToDB(dbName));
  ASSERT_OK(SetMaxBatchSize(&conn_colo, 2));
  ASSERT_OK(conn_colo.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kColocatedTable));
  ASSERT_OK(conn_colo.Execute("SET yb_fast_path_for_colocated_copy = 1"));

  auto conn2 = ASSERT_RESULT(ConnectToDB(dbName));
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor(
      [&conn2, kColocatedTable]() {
        // wait 1s for copy to start
        SleepFor(1s);
        LOG(INFO) << "insert started";
        ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(
            conn2.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (199999, 1)", kColocatedTable));
        ASSERT_OK(conn2.CommitTransaction());
        LOG(INFO) << "insert done";
      });

  LOG(INFO) << "copy started";
  const auto start = MonoTime::Now();
  const auto update_status = ExecuteCopyFromCSV(conn_colo, kColocatedTable, csv_filename);
  ASSERT_NOK(update_status);
  LOG(INFO) << "update_status.ToString=" << update_status.ToString();
  ASSERT_STR_CONTAINS(
    update_status.ToString(), "duplicate key value violates unique constraint");
  LOG(INFO) << "copy took " << (MonoTime::Now() - start).ToMilliseconds() << " ms";
  auto val = ASSERT_RESULT(conn_colo.FetchRow<int32_t>(Format(
      "SELECT v FROM $0 WHERE k = 199999", kColocatedTable)));
  // the value should not be changed by copy
  CHECK_EQ(val, 1);
  ASSERT_OK(conn_colo.Execute("RESET yb_fast_path_for_colocated_copy"));
  ASSERT_OK(ResetMaxBatchSize(&conn_colo));
}

// Test the session configuration parameter yb_read_time which reads the data as of a point in time
// in the past.
// 1. Create a table t and insert 10 rows.
// 2. Mark the current time t1.
// 3. Delete 4 rows.
// 4. Check "SELECT count(*) from t" is equal to 6 (as of current time).
// 5. SET yb_read_time TO t1
// 6. Check "SELECT count(*) from t" is equal to 10 (as of t1).
// 7. SET yb_read_time TO 0
// 8. Check "SELECT count(*) from t" is equal to 6 (as of current time).
// 9. Get the value of t1 as a HybridTime (t2)
// 10. SET yb_read_time TO 't2 ht'
// 11. Check "SELECT count(*) from t" is equal to 10 (as of t2=t1).
// 12. SET yb_read_time TO '0ht'
// 13. Check "SELECT count(*) from t" is equal to 2 (as of current time).
// 14. Repeat steps 10 to 13 with some variations in unit syntax.
// 15. Negative test case. Invalid unit.
TEST_F(PgMiniTestBase, CheckReadingDataAsOfPastTime) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT, v INT)"));
  LOG(INFO) << "Inserting 10 rows into table t";
  ASSERT_OK(conn.Execute("INSERT INTO t (k,v) SELECT i,i FROM generate_series(1,10) AS i"));
  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM t"));
  ASSERT_EQ(count, 10);
  auto t1 = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      Format("SELECT ((EXTRACT (EPOCH FROM CURRENT_TIMESTAMP))*1000000)::bigint")));
  LOG(INFO) << "Deleting 4 rows from table t";
  ASSERT_OK(conn.Execute("DELETE FROM t WHERE k>6"));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 6);
  LOG(INFO) << "Setting yb_read_time to " << t1;
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO $0", t1));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 10);
  LOG(INFO) << "Setting yb_read_time to 0";
  ASSERT_OK(conn.Execute("SET yb_read_time TO 0"));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 6);

  LOG(INFO) << "Get the value of t1 as a HybridTime";
  auto t2 = t1 << 12;

  LOG(INFO) << "Setting yb_read_time to '" << t2 << " ht'";
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO '$0 ht'", t2));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 10);
  LOG(INFO) << "Setting yb_read_time to '0ht'";
  ASSERT_OK(conn.Execute("SET yb_read_time TO '0ht'"));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 6);

  LOG(INFO) << "Testing variations in specifying the unit";
  LOG(INFO) << "Setting yb_read_time to '  " << t2 << "   ht   '";
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO '  $0   ht   '", t2));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 10);
  LOG(INFO) << "Setting yb_read_time to '0'";
  ASSERT_OK(conn.Execute("SET yb_read_time TO '0'"));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 6);

  LOG(INFO) <<"Negative test case. Invalid unit";
  ASSERT_NOK(conn.ExecuteFormat("SET yb_read_time TO '$0 ms'", t2));
}

// Test that write DMLs are disallowed in a pg session when yb_read_time is set to non-zero.
TEST_F(PgMiniTestBase, DisallowWriteDMLsWithYbReadTime) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT, v INT)"));
  LOG(INFO) << "Inserting 4 rows into table t";
  ASSERT_OK(conn.Execute("INSERT INTO t (k,v) SELECT i,i FROM generate_series(1,4) AS i"));
  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM t"));
  ASSERT_EQ(count, 4);
  auto t1 = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      Format("SELECT ((EXTRACT (EPOCH FROM CURRENT_TIMESTAMP))*1000000)::bigint")));
  LOG(INFO) << "Deleting 2 rows from table t";
  ASSERT_OK(conn.Execute("DELETE FROM t WHERE k>2"));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 2);
  LOG(INFO) << "Setting yb_read_time to " << t1;
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO $0", t1));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 4);
  LOG(INFO) << "Trying to Insert rows when yb_read_time is set to: " << t1;
  ASSERT_NOK(conn.Execute("INSERT INTO t (k,v) SELECT i,i FROM generate_series(11,20) AS i"));
  LOG(INFO) << "Trying to update rows when yb_read_time is set to: " << t1;
  ASSERT_NOK(conn.Execute("UPDATE t SET v=99 WHERE k=2"));
  LOG(INFO) << "Trying to delete rows when yb_read_time is set to: " << t1;
  ASSERT_NOK(conn.Execute("DELETE FROM t WHERE k=2"));
  LOG(INFO) << "Trying to select rows for update when yb_read_time is set to: " << t1;
  ASSERT_NOK(conn.Execute("SELECT * FROM t FOR UPDATE"));
  LOG(INFO) << "Trying to select rows for share when yb_read_time is set to: " << t1;
  ASSERT_NOK(conn.Execute("SELECT * FROM t FOR SHARE"));
  LOG(INFO) << "Trying to select rows in serializable isolation when yb_read_time is set to: "
            << t1;
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
  ASSERT_NOK(conn.Execute("SELECT * FROM t"));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  LOG(INFO) << "Trying to select rows in serializable isolation read only transaction when "
               "yb_read_time is set to: "
            << t1;
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE READ ONLY"));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_OK(conn.Execute("COMMIT"));
  t1 = 0;
  LOG(INFO) << "Setting yb_read_time to " << t1;
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO $0", t1));
  count = ASSERT_RESULT(conn.FetchRow<PGUint64>(Format("SELECT count(*) FROM t")));
  ASSERT_EQ(count, 2);
  // Check that we are able to perform write DMls after resetting yb_read_time to 0.
  LOG(INFO) << "Trying to Insert rows when yb_read_time is set to: " << t1;
  ASSERT_OK(conn.Execute("INSERT INTO t (k,v) SELECT i,i FROM generate_series(5,6) AS i"));
  LOG(INFO) << "Trying to update rows when yb_read_time is set to: " << t1;
  ASSERT_OK(conn.Execute("UPDATE t SET v=99 WHERE k=2"));
  LOG(INFO) << "Trying to delete rows when yb_read_time is set to: " << t1;
  ASSERT_OK(conn.Execute("DELETE FROM t WHERE k=1"));
}

// Test the read-time flag of ysql_dump to generate the schema of the database as of a timestamp t
// 1- Create two tables
// 2- Get the current timestamp t
// 3- Run ysql_dump to capture the schema as ground truth for comparison
// 4- Create one more table
// 5- Run ysql_dump --readtime=t to generate the schema as of timestamp t
// 6- Compare the schema of step 5 with the ground truth (should be the same)
TEST_F(PgMiniTestBase, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLDumpAsOfTime)) {
  auto conn = ASSERT_RESULT(Connect());
  // Step 1
  LOG(INFO) << "Create table t1";
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (k1 INT, v1 INT);"));
  LOG(INFO) << "Create table t2";
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (k2 INT primary key, v2 text);"));
  //  Step 2
  auto t1 = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      Format("SELECT ((EXTRACT (EPOCH FROM CURRENT_TIMESTAMP))*1000000)::bigint")));
  LOG(INFO) << "Current timestamp t=" << std::to_string(t1);
  // Step 3
  auto hostport = pg_host_port();
  std::string kHostFlag = "--host=" + hostport.host();
  std::string kPortFlag = "--port=" + std::to_string(hostport.port());
  std::vector<std::string> args = {
      GetPgToolPath("ysql_dump"),
      kHostFlag,
      kPortFlag,
      "--schema-only",
      "--include-yb-metadata",
      "yugabyte"  // Database name
  };
  LOG(INFO) << "Run tool: " << AsString(args);
  std::string ground_truth;
  ASSERT_OK(Subprocess::Call(args, &ground_truth));
  LOG(INFO) << "Tool output: " << ground_truth;

  // Step 4
  LOG(INFO) << "Create table t3";
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (k INT, f INT);"));
  // Step 5
  std::string timestamp_flag = "--read-time=" + std::to_string(t1);
  args = {
      GetPgToolPath("ysql_dump"),
      kHostFlag,
      kPortFlag,
      "--schema-only",
      timestamp_flag,
      "--include-yb-metadata",
      "yugabyte"  // database name
  };
  LOG(INFO) << "Run tool: " << AsString(args);
  std::string dump_as_of_time;
  ASSERT_OK(Subprocess::Call(args, &dump_as_of_time));
  LOG(INFO) << "Tool output: " << dump_as_of_time;
  // Step 6
  ASSERT_STR_EQ(ground_truth, dump_as_of_time);
}

// Mimics the CheckReadTimePickingLocation test for the relaxed
// yb_read_after_commit_visibility case.
//
// There are two primary effects of relaxed yb_read_after_commit_visibility:
// - SELECTs now always pick their read time on local proxy.
// - The read time is clamped whenever it is picked this way (not relevant for this test).
//
// This implies the following changes compared to the vanilla test
// - Case 1: no pipeline, single operation in first batch, no distributed txn.
//           Read time is picked on proxy for plain SELECTs.
// - Case 2: no pipeline, multiple operations to the same tablet in first batch, no distributed txn.
//           Read time is picked on proxy.
TEST_F(PgReadTimeTest, CheckRelaxedReadAfterCommitVisibility) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  constexpr auto kTable = "test"sv;
  constexpr auto kSingleTabletTable = "test_with_single_tablet"sv;
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS", kSingleTabletTable));

  for (const auto& table_name : {kTable, kSingleTabletTable}) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 100), 0", table_name));
    ASSERT_OK(conn.ExecuteFormat(
      "CREATE OR REPLACE PROCEDURE insert_rows_$0(first integer, last integer) "
      "LANGUAGE plpgsql "
      "as $$body$$ "
      "BEGIN "
      "  FOR i in first..last LOOP "
      "    INSERT INTO $0 VALUES (i, i); "
      "  END LOOP; "
      "END; "
      "$$body$$", table_name));
  }

  // Relax read-after-commit-visiblity guarantee.
  ASSERT_OK(conn.Execute("SET yb_read_after_commit_visibility TO relaxed"));
  ASSERT_OK(conn.Execute("SET log_statement = 'all'"));

  // 1. no pipeline, single operation in first batch, no distributed txn
  //
  // relaxed yb_read_after_commit_visibility does not affect DML queries.
  for (const auto& table_name : {kTable, kSingleTabletTable}) {
    CheckReadTimeProvidedToDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=1 WHERE k=1", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1000, 1000)", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE k=1000", table_name));
        });
  }

  // 2. no pipeline, multiple operations to various tablets in first batch, no distributed txn
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kTable));
      });

  // 3. no pipeline, multiple operations to the same tablet in first batch, no distributed txn
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kSingleTabletTable));
      });

  // 4. no pipeline, single operation in first batch, starts a distributed transation
  //
  // expected_num_picked_read_time_on_doc_db_metric is set because in case of a SELECT FOR UPDATE,
  // a read time is picked in read_query.cc, but an extra picking is done in write_query.cc just
  // after conflict resolution is done (see DoTransactionalConflictsResolved()).
  //
  // relaxed yb_read_after_commit_visibility does not affect FOR UPDATE queries.
  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1 FOR UPDATE", kTable));
        ASSERT_OK(conn.CommitTransaction());
      }, 2);

  // 5. no pipeline, multiple operations to various tablets in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetHighMaxBatchSize(&conn));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(101, 110)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 6. no pipeline, multiple operations to the same tablet in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetHighMaxBatchSize(&conn));
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(101, 110)", kSingleTabletTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 7. Pipeline, single operation in first batch, starts a distributed transation
  ASSERT_OK(SetMaxBatchSize(&conn, 1));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(111, 120)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 8. Pipeline, multiple operations to various tablets in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(121, 150)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 9. Pipeline, multiple operations to the same tablet in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(121, 150)", kSingleTabletTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));
}

TEST_F(PgReadTimeTest, CheckDeferredReadAfterCommitVisibility) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  constexpr auto kTable = "test"sv;
  constexpr auto kSingleTabletTable = "test_with_single_tablet"sv;
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS", kSingleTabletTable));

  for (const auto& table_name : {kTable, kSingleTabletTable}) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 100), 0", table_name));
    ASSERT_OK(conn.ExecuteFormat(
      "CREATE OR REPLACE PROCEDURE insert_rows_$0(first integer, last integer) "
      "LANGUAGE plpgsql "
      "as $$body$$ "
      "BEGIN "
      "  FOR i in first..last LOOP "
      "    INSERT INTO $0 VALUES (i, i); "
      "  END LOOP; "
      "END; "
      "$$body$$", table_name));
  }

  // Defer read-after-commit-visiblity guarantee.
  ASSERT_OK(conn.Execute("SET yb_read_after_commit_visibility TO deferred"));
  ASSERT_OK(conn.Execute("SET log_statement = 'all'"));

  // 1. no pipeline, single operation in first batch, no distributed txn
  for (const auto& table_name : {kTable, kSingleTabletTable}) {
    CheckReadTimeProvidedToDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=1 WHERE k=1", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1000, 1000)", table_name));
        });

    CheckReadTimePickedOnDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE k=1000", table_name));
        });

    // Not a fast path write.
    CheckReadTimeProvidedToDocdb(
        [&conn, table_name]() {
          ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET k=2000 WHERE k=1", table_name));
        });
  }

  // 2. no pipeline, multiple operations to various tablets in first batch, no distributed txn
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kTable));
      });

  // 3. no pipeline, multiple operations to the same tablet in first batch, no distributed txn
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kSingleTabletTable));
      });

  // 4. no pipeline, single operation in first batch, starts a distributed transation
  //
  // expected_num_picked_read_time_on_doc_db_metric is set because in case of a SELECT FOR UPDATE,
  // a read time is picked in read_query.cc, but an extra picking is done in write_query.cc just
  // after conflict resolution is done (see DoTransactionalConflictsResolved()).
  //
  // deferred yb_read_after_commit_visibility picks read time at pg client even
  // for SELECT FOR UPDATE queries.
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1 FOR UPDATE", kTable));
        ASSERT_OK(conn.CommitTransaction());
      });

  // 5. no pipeline, multiple operations to various tablets in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetHighMaxBatchSize(&conn));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(101, 110)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 6. no pipeline, multiple operations to the same tablet in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetHighMaxBatchSize(&conn));
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(101, 110)", kSingleTabletTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 7. Pipeline, single operation in first batch, starts a distributed transation
  ASSERT_OK(SetMaxBatchSize(&conn, 1));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(111, 120)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 8. Pipeline, multiple operations to various tablets in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  CheckReadTimeProvidedToDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(121, 150)", kTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));

  // 9. Pipeline, multiple operations to the same tablet in first batch, starts a distributed
  //    transation
  ASSERT_OK(SetMaxBatchSize(&conn, 10));
  CheckReadTimeProvidedToDocdb(
      [&conn, kSingleTabletTable]() {
        ASSERT_OK(conn.ExecuteFormat("CALL insert_rows_$0(121, 150)", kSingleTabletTable));
      });
  ASSERT_OK(ResetMaxBatchSize(&conn));
}

} // namespace yb::pgwrapper
