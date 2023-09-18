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

#include <atomic>
#include <functional>
#include <memory>
#include <string>

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

DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(enable_wait_queues);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_int32(ysql_max_write_restart_attempts);
DECLARE_string(ysql_pg_conf_csv);

METRIC_DECLARE_counter(picked_read_time_on_docdb);

namespace yb::pgwrapper {
namespace {

class PgReadTimeTest : public PgMiniTestBase {
 protected:
  using StmtExecutor = std::function<void()>;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;

    // TODO: Remove the below guc setting once it becomes the default.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = "yb_lock_pk_single_rpc=true";

    // for TestConflictRetriesOnDocdb
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_write_restart_attempts) = 0;
    PgMiniTestBase::SetUp();
  }

  uint64_t GetNumPickedReadTimeOnDocDb() {
    uint64_t num_pick_read_time_on_docdb = 0;
    for (const auto& mini_tablet_server : cluster_->mini_tablet_servers()) {
      auto peers = mini_tablet_server->server()->tablet_manager()->GetTabletPeers();
      for (const auto& peer : peers) {
        auto counter = METRIC_picked_read_time_on_docdb.Instantiate(
            peer->tablet()->GetTabletMetricsEntity());
        num_pick_read_time_on_docdb += counter->value();
      }
    }
    return num_pick_read_time_on_docdb;
  }

  void CheckReadTimePickedOnDocdb(const StmtExecutor& stmt_executor) {
    CheckReadTimePickingLocation(stmt_executor, true /* read_time_picked_on_docdb */);
  }

  void CheckReadTimeProvidedToDocdb(const StmtExecutor& stmt_executor) {
    CheckReadTimePickingLocation(stmt_executor, false /* read_time_picked_on_docdb */);
  }

 private:
  void CheckReadTimePickingLocation(
      const StmtExecutor& stmt_executor, bool read_time_picked_on_docdb) {
    uint64_t initial = GetNumPickedReadTimeOnDocDb();
    stmt_executor();
    uint64_t diff = GetNumPickedReadTimeOnDocDb() - initial;
    ASSERT_EQ(diff, (read_time_picked_on_docdb ? 1 : 0));
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
    // because FLAGS_ysql_max_write_restart_attempts=0 and so there are no query layer retries.
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
  const std::string kTable = "test";
  const std::string kSingleTabletTable = "test_with_single_tablet";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS", kSingleTabletTable));

  for (const auto* table_name : {&kTable, &kSingleTabletTable}) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 100), 0", *table_name));
    ASSERT_OK(conn.ExecuteFormat(
      "CREATE OR REPLACE PROCEDURE insert_rows_$0(first integer, last integer) "
      "LANGUAGE plpgsql "
      "as $$body$$ "
      "BEGIN "
      "  FOR i in first..last LOOP "
      "    INSERT INTO $0 VALUES (i, i); "
      "  END LOOP; "
      "END; "
      "$$body$$", *table_name));
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
  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1", kTable));
      });

  CheckReadTimePickedOnDocdb(
      [&conn, kTable]() {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=1 WHERE k=1", kTable));
      });

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
  CheckReadTimePickedOnDocdb(
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
