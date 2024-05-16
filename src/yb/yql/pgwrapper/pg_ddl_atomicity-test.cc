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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "yb/client/client_fwd.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-test-util.h"

#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"

#include "yb/master/master_client.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/timestamp.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_ddl_atomicity_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using std::string;
using std::vector;

namespace yb {
namespace pgwrapper {

class PgDdlAtomicityTest : public PgDdlAtomicityTestBase {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_transaction_bg_task_wait_ms=5000");
  }

  void CreateTable(const string& tablename) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute(CreateTableStmt(tablename)));
  }

  void RestartMaster() {
    LOG(INFO) << "Restarting Master";
    auto master = cluster_->GetLeaderMaster();
    master->Shutdown();
    ASSERT_OK(master->Restart());
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      auto s = cluster_->GetIsMasterLeaderServiceReady(master);
      return s.ok();
    }, MonoDelta::FromSeconds(60), "Wait for Master to be ready."));
    SleepFor(5s);
  }

  void SetFlagOnAllProcessesWithRollingRestart(const string& flag) {
    LOG(INFO) << "Restart the cluster and set " << flag;
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      cluster_->master(i)->mutable_flags()->push_back(flag);
    }

    RestartMaster();

    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->tablet_server(i)->mutable_flags()->push_back(flag);
    }
    for (auto* tserver : cluster_->tserver_daemons()) {
      tserver->Shutdown();
      ASSERT_OK(tserver->Restart());
      SleepFor(5s);
    }
  }
};

TEST_F(PgDdlAtomicityTest, TestDatabaseGC) {
  TableName test_name = "test_pgsql";
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.TestFailDdl("CREATE DATABASE " + test_name));

  // Verify DocDB Database creation, even though it failed in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  VerifyNamespaceExists(client.get(), test_name);

  // After bg_task_wait, DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the namespace.
  VerifyNamespaceNotExists(client.get(), test_name);
}

TEST_F(PgDdlAtomicityTest, TestCreateDbFailureAndRestartGC) {
  NamespaceName test_name = "test_pgsql";
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.TestFailDdl("CREATE DATABASE " + test_name));

  // Verify DocDB Database creation, even though it fails in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  VerifyNamespaceExists(client.get(), test_name);

  // Restart the master before the BG task can kick in and GC the failed transaction.
  RestartMaster();

  // Re-init client after restart.
  client = ASSERT_RESULT(cluster_->CreateClient());

  // Confirm that Catalog Loader deletes the namespace on master restart.
  VerifyNamespaceNotExists(client.get(), test_name);
}

TEST_F(PgDdlAtomicityTest, TestIndexTableGC) {
  TableName test_name = "test_pgsql_table";
  TableName test_name_idx = test_name + "_idx";

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Lower the delays so we successfully create this first table.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "10"));

  // Create Table that Index will be set on.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(CreateTableStmt(test_name)));

  // After successfully creating the first table, set flags to delay the background task.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "13000"));
  ASSERT_OK(conn.TestFailDdl(CreateIndexStmt(test_name_idx, test_name, "key")));

  // Wait for DocDB index creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  VerifyTableExists(client.get(), kDatabase, test_name_idx, 10);

  // DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the index.
  VerifyTableNotExists(client.get(), kDatabase, test_name_idx, 40);
}

TEST_F(
    PgDdlAtomicityTest, TestRaceIndexDeletionAndReadQueryOnColocatedDB) {
  TableName table_name = "test_pgsql_table";
  TableName index_name = Format("$0_idx", table_name);
  NamespaceName db_name = "test_db";

  ASSERT_OK(
      ASSERT_RESULT(Connect()).ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", db_name));

  auto conn = ASSERT_RESULT(ConnectToDB("test_db"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON test_pgsql_table(value)", index_name));
  for (int i = 0; i < 5000; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES($1, '$2')", table_name, i, i));
  }
  TestThreadHolder threads;
  std::mutex m;
  std::condition_variable cv;
  bool cache_warm = false;
  bool index_deletion_sent = false;
  threads.AddThreadFunctor([this, &m, &cv, &cache_warm, &index_deletion_sent, &table_name] {
    auto conn = ASSERT_RESULT(ConnectToDB("test_db"));
    ASSERT_RESULT(
        conn.FetchRow<int32_t>(Format("SELECT key FROM $0 where value = '1000'", table_name)));
    LOG(INFO) << "queryer - setting cache to be warm";
    {
      std::unique_lock lk(m);
      cache_warm = true;
    }
    cv.notify_one();
    LOG(INFO) << "queryer - waiting for index deletion to be sent";
    {
      std::unique_lock lk(m);
      cv.wait(lk, [&index_deletion_sent] { return index_deletion_sent; });
    }
    auto result =
        conn.FetchRow<int32_t>(Format("SELECT key FROM $0 where value = '3000'", table_name));
    if (!result.ok() &&
        !(result.status().IsNetworkError() &&
          result.status().ToString().find("OBJECT_NOT_FOUND") != std::string::npos)) {
      FAIL() << "Expected ok or network error from query, received: " << result.status().ToString();
    }
    LOG(INFO) << "queryer - complete";
  });
  threads.AddThreadFunctor([this, &m, &cv, &cache_warm, &index_deletion_sent, &index_name] {
    auto conn = ASSERT_RESULT(ConnectToDB("test_db"));
    LOG(INFO) << "index deleter - waiting for cache to be warm";
    {
      std::unique_lock lk(m);
      cv.wait(lk, [&cache_warm] { return cache_warm; });
    }
    ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", index_name));
    LOG(INFO) << "index deleter - setting index deletion request sent to true";
    {
      std::unique_lock lk(m);
      index_deletion_sent = true;
    }
    cv.notify_one();
    LOG(INFO) << "index deleter - finished";
  });
  threads.JoinAll();
  threads.Stop();
}

// Class for sanity test.
class PgDdlAtomicitySanityTest : public PgDdlAtomicityTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
      "--allowed_preview_flags_csv=ysql_ddl_rollback_enabled");
    options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
    options->extra_tserver_flags.push_back("--report_ysql_ddl_txn_status_to_master=true");
    // TODO (#19975): Enable read committed isolation
    options->extra_tserver_flags.push_back("--yb_enable_read_committed_isolation=false");
  }
};

TEST_F(PgDdlAtomicitySanityTest, BasicTest) {
  auto conn = ASSERT_RESULT(Connect());
  const int num_rows = 5;
  ASSERT_OK(SetupTablesForAllDdls(&conn, num_rows));

  ASSERT_OK(RunAllDdlsWithErrorInjection(&conn));

  // Wait for rollback to happen.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(WaitForDdlVerificationAfterDdlFailure(client.get(), kDatabase));

  // Verify that all the above operations are rolled back.
  ASSERT_OK(VerifyAllFailingDdlsRolledBack(&conn, client.get(), kDatabase));

  // Verify that data is still intact.
  ASSERT_OK(VerifyRowsAfterDdlErrorInjection(&conn, num_rows));

  // Now test successful DDLs.
  ASSERT_OK(RunAllDdls(&conn));
  ASSERT_OK(VerifyAllSuccessfulDdls(&conn, client.get(), kDatabase));

  // Verify that data is still intact.
  ASSERT_OK(VerifyRowsAfterDdlSuccess(&conn, num_rows));
}

TEST_F(PgDdlAtomicitySanityTest, CreateFailureRollback) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "TEST_fail_table_creation_at_preparing_state", "true"));
  ASSERT_NOK(conn.Execute(CreateTableStmt(kCreateTable)));
  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 20);
}

TEST_F(PgDdlAtomicitySanityTest, TestMultiRewriteAlterTable) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // First create a table with some data.
  const string table_name = "test_table";
  ASSERT_OK(conn.Execute(CreateTableStmt(table_name)));
  const int num_rows = 5;
  for (int i = 0; i < num_rows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES ($1, 'value$1', 1.1)", table_name, i));
  }

  // Test failure of alter statement with multiple subcommands.
  ASSERT_OK(conn.TestFailDdl("ALTER TABLE test_table ALTER COLUMN key TYPE text USING key::text,"
      " ALTER COLUMN num TYPE text USING num::text"));
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(table_name)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for new DocDB table to be dropped."));
  ASSERT_OK(VerifySchema(client.get(), "yugabyte", table_name, {"key", "value", "num"}));

  // Verify that the data and schema is intact.
  ASSERT_OK(conn.FetchMatrix(Format("SELECT * FROM $0", table_name), num_rows, 3));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (11, 'value11', 11.11)", table_name));

  // Verify successful execution of the above alter commands.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ALTER COLUMN key TYPE text USING key::text,"
      " ALTER COLUMN num TYPE text USING num::text"));
  ASSERT_OK(VerifySchema(client.get(), "yugabyte", table_name, {"key", "value", "num"}));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES ('keytext', 'value$1', '2.231')", table_name));

  // We added two new rows.
  ASSERT_OK(conn.FetchMatrix(Format("SELECT * FROM $0", table_name), num_rows + 2, 3));
}

TEST_F(PgDdlAtomicitySanityTest, TestChangedPkColOrder) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnTServers("report_ysql_ddl_txn_status_to_master", "false"));

  // First create a table with some data.
  const string table_name = "test_table";
  ASSERT_OK(conn.TestFailDdl(Format("CREATE TABLE $0 (key int, value text, value2 float, "
      "PRIMARY KEY(value2, key))", table_name)));
  VerifyTableNotExists(client.get(), "yugabyte", table_name, 10);

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key int, value text, value2 float, "
      "PRIMARY KEY(value2, key))", table_name));
  VerifyTableExists(client.get(), "yugabyte", table_name, 10);

  // Verify Alter Table with switched PK order.
  const string alter_test = "alter_test";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT, value TEXT, value2 float)", alter_test));

  // Verify failure case.
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 ADD PRIMARY KEY(value, key)", alter_test)));
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(alter_test)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for new DocDB table to be dropped."));
  // Insert duplicate rows.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'value1', 1.1), (1, 'value1', 1.1)",
                               alter_test));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE key = 1", alter_test));

  // Verify success case.
  ASSERT_OK(conn.Execute(Format("ALTER TABLE $0 ADD PRIMARY KEY(value, key)", alter_test)));
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(alter_test)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for old DocDB table to be dropped."));
  ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'value1', 1.1), (1, 'value1', 1.1)",
                                alter_test));
}

TEST_F(PgDdlAtomicitySanityTest, IndexRollback) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  const TableName& table = "indexed_table";
  ASSERT_OK(conn.Execute(CreateTableStmt(table)));

  // Test rollback of CREATE INDEX statement.
  ASSERT_OK(conn.TestFailDdl(CreateIndexStmt(kCreateIndex, table)));
  VerifyTableNotExists(client.get(), kDatabase, kCreateIndex, 20);

  // Test rollback of CREATE INDEX NONCONCURRENTLY statement.
  ASSERT_OK(conn.TestFailDdl(
    Format("CREATE INDEX NONCONCURRENTLY $0 ON $1(value)", kCreateIndex, table)));
  VerifyTableNotExists(client.get(), kDatabase, kCreateIndex, 20);

  // Test rollback of CREATE TABLE with index statement.
  const TableName& unique_table = "unique_table";
  const string unique_idx_col = "col";
  ASSERT_OK(conn.TestFailDdl(Format("CREATE TABLE $0($1 text UNIQUE)",
                             unique_table, unique_idx_col)));
  VerifyTableNotExists(client.get(), kDatabase, unique_table, 40);
  VerifyTableNotExists(client.get(),
                       kDatabase,
                       Format("$0_$1_key", unique_table, unique_idx_col),
                       40);

  // Test rollback after failure at different backfill stages.
  const TableName& backfill_idx = "backfill_index_";
  vector<string> backfill_index_stages = {"indisready", "postbackfill"};
  for (const string& idx_stage : backfill_index_stages) {
    ASSERT_OK(conn.ExecuteFormat("SET yb_test_fail_index_state_change TO $0", idx_stage));
    const string idx = backfill_idx + idx_stage;
    ASSERT_NOK(conn.Execute(CreateIndexStmt(idx, table)));
    // If index backfill fails, then the indexes should not be cleaned up.
    // This is because the transaction that creates the index is separate from the transaction that
    // backfills the index.
    VerifyTableExists(client.get(), kDatabase, idx, 20);
  }
  ASSERT_OK(conn.ExecuteFormat("RESET yb_test_fail_index_state_change"));

  // Test failure of alter table add index.
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 ADD UNIQUE(value)", table)));
  VerifyTableNotExists(client.get(), kDatabase, Format("$0_$1_key", table, "value"), 20);

  // Test rollback of Alter Table DROP index
  ASSERT_OK(conn.Execute(Format("ALTER TABLE $0 ADD UNIQUE(key)", table)));
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP CONSTRAINT $0_key_key", table)));
  VerifyTableExists(client.get(), kDatabase, Format("$0_key_key", table), 20);

  // Test rollback of DROP INDEX
  const string drop_idx_test = "drop_idx_test";
  ASSERT_OK(conn.Execute(CreateIndexStmt(drop_idx_test, table)));
  ASSERT_OK(conn.TestFailDdl(DropIndexStmt(drop_idx_test)));
  VerifyTableExists(client.get(), kDatabase, drop_idx_test, 20);

  // Test rollback of DROP TABLE which causes drop of index.
  const TableName drop_test = "drop_unique_idx_table";
  const string drop_col = "col";
  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0($1 text UNIQUE, value text)",
                                drop_test, drop_col)));
  ASSERT_OK(conn.TestFailDdl(DropTableStmt(drop_test)));
  VerifyTableExists(client.get(), kDatabase, drop_test, 20);
  VerifyTableExists(client.get(), kDatabase, Format("$0_$1_key", drop_test, drop_col), 20);

  // Test rollback of rename index.
  const TableName& rename_unique_idx = "rename_unique_idx";
  ASSERT_OK(conn.Execute(CreateIndexStmt(rename_unique_idx, drop_test)));
  ASSERT_OK(conn.TestFailDdl(Format("ALTER INDEX $0 RENAME TO foobar", rename_unique_idx)));
  VerifyTableExists(client.get(), kDatabase, rename_unique_idx, 20);

  // Test successful DROP TABLE with index.
  ASSERT_OK(conn.Execute(DropTableStmt(drop_test)));
  VerifyTableNotExists(client.get(), kDatabase, drop_test, 20);
  VerifyTableNotExists(client.get(), kDatabase, Format("$0_$1_key", drop_test, drop_col), 20);
}

TEST_F(PgDdlAtomicitySanityTest, StressTestTableWithIndexRollback) {
  // If we drop a table with index, YSQL will issue drop requests for both of them separately.
  // However YB-Master will also drop the index if the table is being dropped. This can cause the
  // drop of index to be generated from two places. With DDL Atomicity, both the DROP requests can
  // be generated in any order in quick succession. Stress test dropping a table with index with
  // minuscule random sleep to ensure that there are no races.
  constexpr size_t kNumIterations = 25;
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_ysql_max_random_delay_before_ddl_verification_usecs",
                                       "1000"));
  auto conn = ASSERT_RESULT(Connect());

  // Test failure of creating a table with index.
  for (size_t i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(conn.TestFailDdl("CREATE TABLE testable(col text UNIQUE, value text)"));
  }

  // Create tables with indexes.
  for (size_t i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0_$1(col text UNIQUE, value text)", kIndexedTable, i));
  }

  // Drop all tables with indexes.
  for (size_t i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(conn.ExecuteFormat(DropTableStmt(Format("$0_$1", kIndexedTable, i))));
  }

  // Verify that all the tables have been cleared.
  for (size_t i = 0; i < kNumIterations; ++i) {
    auto client = ASSERT_RESULT(cluster_->CreateClient());
    VerifyTableNotExists(client.get(), kDatabase, Format("$0_$1", kIndexedTable, i), 20);
    VerifyTableNotExists(client.get(), kDatabase, Format("$0_$1_col_key", kIndexedTable, i), 20);
  }
}

TEST_F(PgDdlAtomicitySanityTest, DdlRollbackMasterRestart) {
  auto conn = ASSERT_RESULT(Connect());
  const int num_rows = 5;
  ASSERT_OK(SetupTablesForAllDdls(&conn, num_rows));

  // Pause DDL rollback.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  ASSERT_OK(RunAllDdlsWithErrorInjection(&conn));

  // Verify that for now all the incomplete DDLs have taken effect.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifyAllFailingDdlsNotRolledBack(client.get(), kDatabase));

  // Restart the master before the BG task can kick in and GC the failed transaction.
  RestartMaster();

  // Verify that rollback reflected on all the tables after restart.
  client = ASSERT_RESULT(cluster_->CreateClient()); // Reinit the YBClient after restart.

  ASSERT_OK(VerifyAllFailingDdlsRolledBack(&conn, client.get(), kDatabase));
  ASSERT_OK(VerifyRowsAfterDdlErrorInjection(&conn, num_rows));
}

TEST_F(PgDdlAtomicitySanityTest, TestYsqlTxnStatusReporting) {
  auto conn = ASSERT_RESULT(Connect());
  const int num_rows_to_insert = 5;
  ASSERT_OK(SetupTablesForAllDdls(&conn, num_rows_to_insert));

  // Disable YB-Master's background task.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
  // Run some failing Ddl transactions
  ASSERT_OK(RunAllDdlsWithErrorInjection(&conn));

  // The rollback should have succeeded anyway because YSQL would have reported the
  // status.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifyAllFailingDdlsRolledBack(&conn, client.get(), kDatabase));

  // Now test successful DDLs.
  ASSERT_OK(RunAllDdls(&conn));

  // Verify that these DDLs were not rolled back.
  ASSERT_OK(VerifyAllSuccessfulDdls(&conn, client.get(), kDatabase));

  // Verify that rows are still intact.
  ASSERT_OK(VerifyRowsAfterDdlSuccess(&conn, num_rows_to_insert));
}

class PgDdlAtomicityParallelDdlTest : public PgDdlAtomicitySanityTest {
 public:
  template<class... Args>
  Result<bool> RunDdlHelper(const std::string& format, Args&&... args) {
    return DoRunDdlHelper(Format(format, std::forward<Args>(args)...));
  }

 private:
  Result<bool> DoRunDdlHelper(const std::string& ddl) {
    auto conn = VERIFY_RESULT(Connect());
    auto s = conn.Execute(ddl);
    if (s.ok()) {
      return true;
    }

    const auto msg = s.message().ToBuffer();
    static const auto allowed_msgs = {
      "Catalog Version Mismatch"sv,
      SerializeAccessErrorMessageSubstring(),
      "Restart read required"sv,
      "Transaction aborted"sv,
      "Transaction metadata missing"sv,
      "Unknown transaction, could be recently aborted"sv,
      "Flush: Value write after transaction start"sv,
      kDdlVerificationError
    };
    if (HasSubstring(msg, allowed_msgs)) {
      return false;
    }
    LOG(ERROR) << "Unexpected failure status " << s;
    return s;
  }
};

TEST_F(PgDdlAtomicityParallelDdlTest, TestParallelDdl) {
  constexpr size_t kNumIterations = 10;
  const auto tablename = "test_table"s;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(CreateTableStmt(tablename)));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))",
      tablename,
      kNumIterations));

  // Add some columns.
  for (size_t i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(ExecuteWithRetry(&conn,
      Format("ALTER TABLE $0 ADD COLUMN col_$1 TEXT", tablename, i)));
  }

  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "0"));

  // Add columns in the first thread.
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &tablename] {
    for (size_t i = kNumIterations; i < kNumIterations * 2;) {
      if (ASSERT_RESULT(RunDdlHelper("ALTER TABLE $0 ADD COLUMN col_$1 TEXT", tablename, i))) {
       ++i;
      }
    }
  });

  // Rename columns in the second thread.
  thread_holder.AddThreadFunctor([this, &tablename] {
    for (size_t i = 0; i < kNumIterations;) {
      if (ASSERT_RESULT(RunDdlHelper("ALTER TABLE $0 RENAME COLUMN col_$1 TO renamedcol_$2",
                                     tablename, i, i))) {
        ++i;
      }
    }
  });

  thread_holder.JoinAll();
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  vector<string> expected_cols;
  expected_cols.reserve(kNumIterations * 2);
  expected_cols.emplace_back("key");
  expected_cols.emplace_back("value");
  expected_cols.emplace_back("num");
  for (size_t i = 0;  i < kNumIterations * 2; ++i) {
    expected_cols.push_back(Format(i < kNumIterations ? "renamedcol_$0" : "col_$0", i));
  }
  ASSERT_OK(VerifySchema(client.get(), kDatabase, tablename, expected_cols));
}

// TODO (deepthi) : This test is flaky because of #14995. Re-enable it back after #14995 is fixed.
TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST(FailureRecoveryTest)) {
  const auto tables_to_create = {kRenameTable, kRenameCol, kAddCol, kDropTable};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  // Set ysql_transaction_bg_task_wait_ms so high that table rollback is nearly
  // disabled.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "100"));
  ASSERT_OK(conn.TestFailDdl(RenameTableStmt(kRenameTable)));
  ASSERT_OK(conn.TestFailDdl(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn.TestFailDdl(AddColumnStmt(kAddCol)));
  ASSERT_OK(conn.TestFailDdl(DropTableStmt(kDropTable)));

  // Verify that table was created on DocDB.
  VerifyTableExists(client.get(), kDatabase, kDropTable, 10);

  // Verify that the tables were altered on DocDB.
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenamedTable, {"key"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenameCol, {"key2"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));

  // Disable DDL rollback.
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_ddl_rollback_enabled", "false"));

  // Verify that best effort rollback works when ysql_ddl_rollback is disabled.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", kAddCol));
  // The following DDL fails because the table already contains data, so it is not possible to
  // add a new column with a not null constraint.
  ASSERT_NOK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN value2 TEXT NOT NULL"));
  // Verify that the above DDL was rolled back by YSQL.
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));

  // Restart the cluster with DDL rollback disabled.
  SetFlagOnAllProcessesWithRollingRestart("--ysql_ddl_rollback_enabled=false");
  // Verify that rollback did not occur even after the restart.
  client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableExists(client.get(), kDatabase, kDropTable, 10);
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenamedTable, {"key"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenameCol, {"key2"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));

  // Verify that it is still possible to run DDL on an affected table.
  // Tables having unverified transaction state on them can still be altered if the DDL rollback
  // is not enabled.
  ASSERT_OK(conn.Execute(RenameTableStmt(kRenameTable, "foobar2")));
  ASSERT_OK(conn.Execute(AddColumnStmt(kAddCol, "value2")));

  // Re-enable DDL rollback properly with restart.
  SetFlagOnAllProcessesWithRollingRestart("--ysql_ddl_rollback_enabled=true");

  client = ASSERT_RESULT(cluster_->CreateClient());
  conn = ASSERT_RESULT(Connect());
  // The tables with the transaction state on them must have had their state cleared upon restart
  // since the flag is now enabled again.
  ASSERT_OK(conn.Execute(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn.Execute(DropTableStmt(kDropTable)));

  // However add_col_test will be corrupted because we never performed transaction rollback on it.
  // It should ideally have only one added column "value2", but we never rolled back the addition of
  // "value".
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value", "value2"}));

  // Add a column to add_col_test which is now corrupted.
  ASSERT_OK(conn.Execute(AddColumnStmt(kAddCol, "value3")));
  // Wait for transaction verification to run.

  // Future DDLs still succeed even though the schema is corrupted. This is because we do not need
  // to compare schemas to determine whether the transaction is a success. PG backend tells the
  // YB-Master the status of the transaction.
  ASSERT_OK(ExecuteWithRetry(&conn, AddColumnStmt(kAddCol, "value4")));

  // Disable PG reporting to Yb-Master.
  ASSERT_OK(cluster_->SetFlagOnTServers("report_ysql_ddl_txn_status_to_master", "false"));

  // Run a DDL on the corrupted table.
  ASSERT_OK(conn.Execute(RenameTableStmt(kAddCol, "foobar3")));
  // However since the PG schema and DocDB schema have become out-of-sync, the above DDL cannot
  // be verified. All future DDL operations will now fail.
  ASSERT_NOK(conn.Execute(RenameColumnStmt(kAddCol)));
  ASSERT_NOK(conn.Execute(DropTableStmt(kAddCol)));
}

TEST_F(PgDdlAtomicityTest, FailureRecoveryTestWithAbortedTxn) {
  // Make TransactionParticipant::Impl::CheckForAbortedTransactions and TabletLoader::Visit deadlock
  // on the mutex. GH issue #15849.

  // Temporarily disable abort cleanup. This flag will be reset when we RestartMaster.
  ASSERT_OK(cluster_->SetFlagOnMasters("transactions_poll_check_aborted", "true"));

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(CreateTableStmt(kDropTable)));

  // Create an aborted transaction so that TransactionParticipant::Impl::CheckForAbortedTransactions
  // has something to do.
  ASSERT_OK(conn.TestFailDdl(CreateTableStmt(kCreateTable)));

  // Crash in the middle of a DDL so that TabletLoader::Visit will perform some writes to
  // sys_catalog on CatalogManager startup.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_simulate_crash_after_table_marked_deleting", "true"));
  ASSERT_OK(conn.Execute(DropTableStmt(kDropTable)));

  ASSERT_EQ(cluster_->master_daemons().size(), 1);
  // Give enough time for CheckForAbortedTransactions to start and get stuck.
  cluster_->GetLeaderMaster()->mutable_flags()->push_back(
      "--TEST_delay_sys_catalog_reload_secs=10");

  RestartMaster();

  VerifyTableNotExists(client.get(), kDatabase, kDropTable, 40);
}

class PgDdlAtomicityConcurrentDdlTest : public PgDdlAtomicitySanityTest {
 protected:
  bool testFailedDueToTxnVerification(PGConn *conn, const string& cmd) {
    Status s = conn->Execute(cmd);
    if (s.ok()) {
      LOG(ERROR) << "Command " << cmd << " executed successfully when failure expected";
      return false;
    }
    return IsDdlVerificationError(s.ToString());
  }

  void testConcurrentDDL(const string& stmt1, const string& stmt2) {
    // Test that until transaction verification is complete, another DDL operating on
    // the same object cannot go through.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(ExecuteWithRetry(&conn, stmt1));
    // The first DDL was successful, but the second should fail because rollback has
    // been paused using 'TEST_pause_ddl_rollback'.
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_TRUE(testFailedDueToTxnVerification(&conn2, stmt2));
  }

  void testConcurrentFailedDDL(const string& stmt1, const string& stmt2) {
    // Same as 'testConcurrentDDL' but here the first DDL statement is a failure. However
    // other DDLs still cannot happen unless rollback is complete.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.TestFailDdl(stmt1));
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_TRUE(testFailedDueToTxnVerification(&conn2, stmt2));
  }

  const string& table() const {
    return table_;
  }

 private:
  const string database_name_ = "yugabyte";
  const string table_ = "test";
};

TEST_F(PgDdlAtomicityConcurrentDdlTest, ConcurrentDdl) {
  const string kCreateAndAlter = "create_and_alter_test";
  const string kCreateAndDrop = "create_and_drop_test";
  const string kDropAndAlter = "drop_and_alter_test";
  const string kAlterAndAlter = "alter_and_alter_test";
  const string kAlterAndDrop = "alter_and_drop_test";

  const auto tables_to_create = {kDropAndAlter, kAlterAndAlter, kAlterAndDrop};

  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  // Test that we can't run a second DDL on a table until the YsqlTxnVerifierState on the first
  // table is cleared.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  testConcurrentDDL(CreateTableStmt(kCreateAndAlter), AddColumnStmt(kCreateAndAlter));
  testConcurrentDDL(CreateTableStmt(kCreateAndDrop), DropTableStmt(kCreateAndDrop));
  testConcurrentDDL(AddColumnStmt(kAlterAndDrop), DropTableStmt(kAlterAndDrop));
  testConcurrentDDL(RenameColumnStmt(kAlterAndAlter), RenameTableStmt(kAlterAndAlter));

  // Test that we can't run a second DDL on a table until the YsqlTxnVerifierState on the first
  // table is cleared even if the first DDL was a failure.
  testConcurrentFailedDDL(DropTableStmt(kDropAndAlter), RenameColumnStmt(kDropAndAlter));
}

class PgDdlAtomicityTxnTest : public PgDdlAtomicitySanityTest {
 public:
  void RunFailingDdlTransaction(const string& ddl_statements) {
    // Normally every DDL auto-commits, and thus we usually have only one DDL statement in a
    // transaction. However, when DDLs are invoked in a function, all the DDLs will be executed
    // in a single atomic transaction if the function itself was invoked as part of a DDL statement.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE OR REPLACE FUNCTION ddl_txn_func() RETURNS TABLE (k INT) AS $$ "
        "BEGIN " + ddl_statements +
        " CREATE TABLE t AS SELECT (1) AS k;"
        // The following statement will fail due to NOT NULL constraint.
        " ALTER TABLE t ADD COLUMN v3 int not null;"
        " RETURN QUERY SELECT k FROM t;"
        " END $$ LANGUAGE plpgsql"));
    ASSERT_NOK(conn.Execute("CREATE TABLE txntest AS SELECT k FROM ddl_txn_func()"));
  }

  // Run all the statements in 'ddl_statements' in a single transaction. This will happen if
  // the DDL statements are invoked in a function that is executed by a DDL statement.
  Status RunTransaction(const string& ddl_statements) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE OR REPLACE FUNCTION ddl_txn_func() RETURNS TABLE (k INT) AS $$ "
        "BEGIN " + ddl_statements + " RETURN QUERY SELECT (1) AS k;" +
        " END $$ LANGUAGE plpgsql"));
    return conn.Execute("CREATE TABLE txntest AS SELECT k FROM ddl_txn_func()");
  }

  string table() {
    return "test_table";
  }
};

TEST_F(PgDdlAtomicityTxnTest, CreateAlterDropTest) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          CreateIndexStmt("idx", table()) + "; " +
                          AddColumnStmt(table()) +  "; " +
                          RenameColumnStmt(table(), "value", "value_renamed") + "; " +
                          DropColumnStmt(table(), "value_renamed") + "; " +
                          RenameTableStmt(table()) + "; " +
                          DropTableStmt(kRenamedTable) + ";";
  RunFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
  VerifyTableNotExists(client.get(), kDatabase, "idx", 10);
}

TEST_F(PgDdlAtomicityTxnTest, CreateDropTest) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          DropTableStmt(table()) + "; ";
  RunFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, CreateDropColTest) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          DropColumnStmt(table()) + "; ";
  RunFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, CreateAlterTest) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          AddColumnStmt(table()) +  "; " +
                          RenameColumnStmt(table()) + "; " +
                          RenameTableStmt(table()) + "; ";
  RunFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, AlterDropTest) {
  CreateTable(table());
  string ddl_statements = RenameColumnStmt(table()) + "; " + DropTableStmt(table()) + "; ";
  RunFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key", "value", "num"}));
}

TEST_F(PgDdlAtomicityTxnTest, AddColRenameColTest) {
  CreateTable(table());
  string ddl_statements = AddColumnStmt(table()) + "; " + RenameColumnStmt(table()) + "; ";
  RunFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key", "value", "num"}));
}

TEST_F(PgDdlAtomicityTxnTest, AddColDropColTest) {
  CreateTable(table());
  // Insert some rows.
  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i < 5; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES($1, '$2')", table(), i, i));
  }
  const string drop_col_stmt = "ALTER TABLE " + table() + " DROP COLUMN value2";
  string ddl_statements = AddColumnStmt(table()) + "; " + drop_col_stmt + "; ";
  RunFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key", "value", "num"}));
  ASSERT_OK(conn.FetchMatrix(Format("SELECT value FROM $0", table()), 5, 1));
}

TEST_F(PgDdlAtomicityTxnTest, AddDropColWithSameNameTest) {
  CreateTable(table());
  // Test that adding and dropping a column with the same name in a single transaction is
  // unsupported.
  Status s = RunTransaction(DropColumnStmt(table()) + ";" + AddColumnStmt(table(), "value") + ";");
  ASSERT_TRUE(s.ToString().find("column value is still in process of deletion") != string::npos);
}

TEST_F(PgDdlAtomicityTxnTest, CreateDropIndexTxn) {
  CreateTable(kIndexedTable);
  string ddl_stmts = CreateIndexStmt(kCreateIndex, kIndexedTable) + "; " +
                     DropIndexStmt(kCreateIndex) + "; ";
  // Create and drop index in the same failing transaction.
  RunFailingDdlTransaction(ddl_stmts);
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, kCreateIndex, 10);
  // Create and drop index in the same successful transaction.
  ASSERT_OK(RunTransaction(ddl_stmts));
  VerifyTableNotExists(client.get(), kDatabase, kCreateIndex, 10);
}

TEST_F(PgDdlAtomicitySanityTest, DmlWithAddColTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string table = "dml_with_add_col_test";
  CreateTable(table);
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  // Conn1: Begin write to the table.
  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (1)"));

  // Conn2: Initiate rollback of the alter.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  ASSERT_OK(conn2.TestFailDdl(AddColumnStmt(table)));

  // Conn1: Since we parallely added a column to the table, the add-column operation would have
  // detected the distributed transaction locks acquired by this transaction on its tablets and
  // aborted it. Add-column operation aborts all ongoing transactions on the table without
  // exception as the transaction could now be in an erroneous state having used the schema
  // without the newly added column.
  ASSERT_NOK(conn1.Execute("COMMIT"));

  // Conn1: Non-transactional insert retry due to Schema version mismatch,
  // refreshing the table cache.
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (1)"));

  // Conn1: Start new transaction.
  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (2)"));

  // Rollback happens while the transaction is in progress.
  // Wait for the rollback to complete.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false"));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key", "value", "num"}));

  // Transaction at conn1 succeeds. Normally, drop-column operation would also have aborted all
  // ongoing transactions on this table. However this is a drop-column operation initiated as part
  // of rollback. The column being dropped by this operation was added as part of an uncommitted
  // transaction, so this column would not have been visible to any transaction. It is safe for
  // this drop-column operation to operate silently without aborting any transaction. Therefore this
  // transaction succeeds.
  ASSERT_OK(conn1.Execute("COMMIT"));
}

// Test that DML transactions concurrent with an aborted DROP TABLE transaction
// can commit successfully (both before and after the rollback is complete).`
TEST_F(PgDdlAtomicitySanityTest, DmlWithDropTableTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string table = "dml_with_drop_test";
  CreateTable(table);
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  auto conn3 = ASSERT_RESULT(Connect());

  // Conn1: Begin write to the table.
  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (1)"));

  // Conn2: Also begin write to the table.
  ASSERT_OK(conn2.Execute("BEGIN"));
  ASSERT_OK(conn2.Execute("INSERT INTO " + table + " VALUES (2)"));

  // Conn3: Initiate rollback of DROP table.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  ASSERT_OK(conn3.TestFailDdl(DropTableStmt(table)));

  // Conn1: This should succeed.
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (3)"));
  ASSERT_OK(conn1.Execute("COMMIT"));

  // Wait for rollback to complete.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false"));
  ASSERT_OK(WaitForDdlVerification(client.get(), kDatabase, table));

  // Conn2 must also commit successfully.
  ASSERT_OK(conn2.Execute("INSERT INTO " + table + " VALUES (4)"));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

// Test that we are able to rollback DROP COLUMN on a column with a missing
// default value.
TEST_F(PgDdlAtomicitySanityTest, DropColumnWithMissingDefaultTest) {
  static const std::string kTable = "drop_column_missing_default_test";
  CreateTable(kTable);
  auto conn = ASSERT_RESULT(Connect());
  // Write some rows to the table.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0(key) VALUES (generate_series(1, 3))", kTable));
  // Add column with missing default value.
  ASSERT_OK(conn.Execute(Format(AddColumnStmt(kTable, "value2", "default"))));
  // Insert rows with the new column set to null.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0(key, value2) VALUES (generate_series(4, 6), null)", kTable));
  // Insert rows with the new column set to a non-default value.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0(key, value2) VALUES (generate_series(7, 9), 'not default')", kTable));
  auto check_table_rows = [&conn] {
    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::optional<std::string>>(Format(
        "SELECT key, value2 FROM $0 ORDER BY key", kTable))));
    size_t idx = 0;
    for (const auto& [key, value] : rows) {
      ASSERT_EQ(key, idx + 1);
      switch(key) {
        case 4: [[fallthrough]];
        case 5: [[fallthrough]];
        case 6:
          ASSERT_EQ(value, std::nullopt);
          break;
        case 7: [[fallthrough]];
        case 8: [[fallthrough]];
        case 9:
          ASSERT_EQ(value, "not default");
          break;
        default:
          ASSERT_EQ(value, "default");
          break;
      }
      ++idx;
    }
  };
  check_table_rows();
  // Fail drop column.
  ASSERT_OK(conn.TestFailDdl(DropColumnStmt(kTable)));
  // Insert more rows.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0(key) VALUES (generate_series(10, 12))", kTable));
  check_table_rows();
}

class PgDdlAtomicityColocatedTestBase : public PgDdlAtomicitySanityTest {
 protected:
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  // Test function.
  void sanityTest(const string& tablegroup = "");

  std::unique_ptr<PGConn> conn_;
  string kDatabaseName = "yugabyte";
};

void PgDdlAtomicityColocatedTestBase::sanityTest(const string& tablegroup) {
  const int num_rows = 5;
  ASSERT_OK(SetupTablesForAllDdls(conn_.get(), num_rows, tablegroup));

  ASSERT_OK(RunAllDdlsWithErrorInjection(conn_.get()));

  // Wait for rollback to happen.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(WaitForDdlVerificationAfterDdlFailure(client.get(), kDatabaseName));

  // Verify that all the above operations are rolled back.
  ASSERT_OK(VerifyAllFailingDdlsRolledBack(conn_.get(), client.get(), kDatabaseName));
  ASSERT_OK(VerifyRowsAfterDdlErrorInjection(conn_.get(), num_rows));
}

class PgDdlAtomicityColocatedDbTest : public PgDdlAtomicityColocatedTestBase {
  void SetUp() override {
    LibPqTestBase::SetUp();

    kDatabaseName = "colocateddbtest";
    PGConn conn_init = ASSERT_RESULT(Connect());
    ASSERT_OK(conn_init.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }
};

TEST_F(PgDdlAtomicityColocatedDbTest, ColocatedTest) {
  sanityTest();
}

class PgDdlAtomicityTablegroupTest : public PgDdlAtomicityColocatedTestBase {
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(Connect()));
    ASSERT_OK(conn_->ExecuteFormat("CREATE TABLEGROUP $0", kTablegroup));
  }

 public:
  Result<int> NumTablegroupParentTables (client::YBClient* client) {
    const auto tables = VERIFY_RESULT(client->ListTables());
    int count = 0;
    for (const auto& t : tables) {
      if (t.table_name().find("tablegroup.parent") != string::npos) {
        ++count;
      }
    }
    return count;
  }

  const string kTablegroup = "test_tgroup";
};

TEST_F(PgDdlAtomicityTablegroupTest, TablegroupTableTest) {
  sanityTest(kTablegroup);
}

TEST_F(PgDdlAtomicityTablegroupTest, TablegroupTest) {
  const string tablegroup = "test_tgrp";
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  // Test that CREATE TABLEGROUP is rolled back.
  ASSERT_OK(conn_->TestFailDdl(Format("CREATE TABLEGROUP $0", tablegroup)));
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(NumTablegroupParentTables(client.get())) == 1;
  }, MonoDelta::FromSeconds(10), "Verify number of tablegroups"));

  // Test that failed DROP TABLEGROUP is rolled back.
  ASSERT_OK(conn_->TestFailDdl(Format("DROP TABLEGROUP $0", kTablegroup)));
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(NumTablegroupParentTables(client.get())) == 1;
  }, MonoDelta::FromSeconds(10), "Verify number of tablegroups"));

  // Create many tables in the tablegroup.
  const int num_tables = 10;
  for (int i = 0; i < num_tables; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE test_$0 (key INT PRIMARY KEY) TABLEGROUP $1",
                                    i, kTablegroup));
  }

  // Wait for DDL verification to complete.
  SleepFor(5 * 1s);

  // Verify that DROP TABLEGROUP CASCADE can get rolled back successfully.
  ASSERT_OK(conn_->TestFailDdl(Format("DROP TABLEGROUP $0 CASCADE", kTablegroup)));
  // Wait to verify that the drop actually did not go through.
  SleepFor(5 * 1s);
  ASSERT_EQ(ASSERT_RESULT(NumTablegroupParentTables(client.get())), 1);
  for (int i = 0; i < num_tables; ++i) {
    VerifyTableExists(client.get(), kDatabase, Format("test_$0", i), 20);
  }

  // Verify that DROP TABLEGROUP CASCADE can finish successfully.
  ASSERT_OK(conn_->ExecuteFormat("DROP TABLEGROUP $0 CASCADE", kTablegroup));
  // Wait for DDL verification.
  SleepFor(5 * 1s);
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(NumTablegroupParentTables(client.get())) == 0;
  }, MonoDelta::FromSeconds(30), "Verify number of tablegroups"));

  for (int i = 0; i < num_tables; ++i) {
    VerifyTableNotExists(client.get(), kDatabase, Format("test_$0", i), 20);
  }
}

class PgDdlAtomicitySnapshotTest : public PgDdlAtomicitySanityTest {
  void SetUp() override {
    LibPqTestBase::SetUp();

    snapshot_util_ = std::make_unique<client::SnapshotTestUtil>();
    client_ = ASSERT_RESULT(cluster_->CreateClient());
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }

 public:
  std::unique_ptr<client::SnapshotTestUtil> snapshot_util_;
  std::unique_ptr<client::YBClient> client_;

  YB_STRONGLY_TYPED_BOOL(ExpectSuccess);
  Status testListSnapshots(yb::pgwrapper::PGConn *conn, DdlErrorInjection inject_error,
      const string& ddl, const TxnSnapshotId& snapshot_id,
      ExpectSuccess expect_success = ExpectSuccess::kTrue) {

    RETURN_NOT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
    if (inject_error) {
      RETURN_NOT_OK(conn->TestFailDdl(ddl));
    } else {
      RETURN_NOT_OK(conn->ExecuteFormat(ddl));
    }
    auto res = snapshot_util_->ListSnapshots(
        snapshot_id, client::ListDeleted::kFalse, client::PrepareForBackup::kTrue);
    if (expect_success) {
      RETURN_NOT_OK(res);
    } else if (res.ok()) {
      return STATUS_FORMAT(IllegalState, "ListSnapshots should have failed for DDL $0", ddl);
    }
    return cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false");
  }

  Status ListSnapshotTest(DdlErrorInjection inject_error);
};

TEST_F(PgDdlAtomicitySnapshotTest, SnapshotTest) {
  // Create requisite tables.
  const auto tables_to_create = {kAddCol, kDropTable, kDropCol};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  const int snapshot_interval_secs = 1;
  // Create a snapshot schedule before running all the failed DDLs.
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(kDatabase,
                                   client::WaitSnapshot::kTrue,
                                   MonoDelta::FromSeconds(snapshot_interval_secs)));

  // Run all the failed DDLs.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  ASSERT_OK(conn.TestFailDdl(CreateTableStmt(kCreateTable)));
  ASSERT_OK(conn.TestFailDdl(AddColumnStmt(kAddCol)));
  ASSERT_OK(conn.TestFailDdl(DropTableStmt(kDropTable)));
  ASSERT_OK(conn.TestFailDdl(DropColumnStmt(kDropCol)));

  // Get the hybrid time before the rollback can happen.
  Timestamp current_time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime hybrid_time_before_rollback = HybridTime::FromMicros(current_time.ToInt64());

  // Ensure that at least one snapshot is taken.
  SleepFor(snapshot_interval_secs * 5s);

  // Verify that rollback for Alter and Create has indeed not happened yet.
  VerifyTableExists(client.get(), kDatabase, kCreateTable, 10);
  VerifyTableExists(client.get(), kDatabase, kDropTable, 10);
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value", "num", "value2"}));

  // Unpause DDL rollback and verify that rollback has happened.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false"));
  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 60);

  // Verify all the other tables are unchanged by all of the DDLs.
  for (const auto& table : tables_to_create) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key", "value", "num"}));
  }

  /*
   TODO (deepthi): Uncomment the following code after #14679 is fixed.
  // Run different failing DDL operations on the tables.
  ASSERT_OK(conn.TestFailDdl(RenameTableStmt(add_col_test)));
  ASSERT_OK(conn.TestFailDdl(RenameColumnStmt(drop_table_test)));
  */

  // Restore to before rollback.
  LOG(INFO) << "Start restoration to timestamp " << hybrid_time_before_rollback;
  auto snapshot_id =
    ASSERT_RESULT(snapshot_util_->PickSuitableSnapshot(schedule_id, hybrid_time_before_rollback));
  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, hybrid_time_before_rollback));

  LOG(INFO) << "Restoration complete";

  // We restored to a point before the first rollback occurred. That DDL transaction should have
  // been re-detected to be a failure and rolled back again.
  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 60);
  for (const string& table : tables_to_create) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key", "value", "num"}));
  }
}

Status PgDdlAtomicitySnapshotTest::ListSnapshotTest(DdlErrorInjection inject_error) {
  auto conn = VERIFY_RESULT(Connect());
  RETURN_NOT_OK(SetupTablesForAllDdls(&conn, 0 /* num_rows_to_insert */));

  const int snapshot_interval_secs = 1;
  // Create a snapshot schedule before running all the failed DDLs.
  auto schedule_id = VERIFY_RESULT(
      snapshot_util_->CreateSchedule(kDatabase,
                                     client::WaitSnapshot::kTrue,
                                     MonoDelta::FromSeconds(snapshot_interval_secs)));

  // Get the hybrid time before the rollback can happen.
  Timestamp current_time(VERIFY_RESULT(WallClock()->Now()).time_point);
  HybridTime hybrid_time_before_rollback = HybridTime::FromMicros(current_time.ToInt64());

  SleepFor(snapshot_interval_secs * 5s);
  auto snapshot_id =
      VERIFY_RESULT(snapshot_util_->PickSuitableSnapshot(schedule_id, hybrid_time_before_rollback));

  // Verify that newly created tables do not cause the snapshot to fail as they are not part
  // of the original snapshot.
  RETURN_NOT_OK(testListSnapshots(&conn, inject_error, CreateTableStmt(kCreateTable), snapshot_id));
  RETURN_NOT_OK(testListSnapshots(&conn, inject_error, CreateIndexStmt(kCreateIndex, kIndexedTable),
      snapshot_id));

  vector<string> ddls = {RenameColumnStmt(kRenameCol), AddColumnStmt(kAddCol),
      DropColumnStmt(kDropCol), AddPkStmt(kAddPk), DropPkStmt(kDropPk),
      RenameTableStmt(kRenameTable), RenameIndexStmt(kRenameIndex, kRenamedIndex)};
  vector<string> tables = {kRenameCol, kAddCol, kDropCol, kAddPk, kDropPk,
      inject_error ? kRenameTable : kRenamedTable, inject_error ? kRenameIndex : kRenamedIndex};

  DCHECK_EQ(ddls.size(), tables.size());

  if (inject_error) {
    // Drop is handled separately below for successful Ddl case.
    ddls.insert(ddls.end(), {DropTableStmt(kDropTable), DropIndexStmt(kDropIndex)});
    tables.insert(tables.end(), {kDropTable, kDropIndex});
  }

  auto client = VERIFY_RESULT(cluster_->CreateClient());
  for (size_t i = 0; i< ddls.size(); ++i) {
    RETURN_NOT_OK(testListSnapshots(&conn, inject_error, ddls[i], snapshot_id,
        ExpectSuccess::kFalse));
    // Wait for Ddl verification to complete so that the ddl state on this table does not affect
    // the list snapshot test for the next ddl.
    RETURN_NOT_OK(WaitForDdlVerification(client.get(), kDatabase, tables[i]));
  }

  if (inject_error) {
    return Status::OK();
  }

  // If there is a table marked for deletion, then the snapshot should fail.
  RETURN_NOT_OK(testListSnapshots(&conn, inject_error, DropTableStmt(kDropTable), snapshot_id,
      ExpectSuccess::kFalse));
  // Wait for the table to be deleted before the next test.
  client::VerifyTableNotExists(client.get(), kDatabase, kDropTable, 20);

  // Verify that an index marked for deletion causes ListSnapshots to fail.
  return testListSnapshots(&conn, inject_error, DropIndexStmt(kDropIndex), snapshot_id,
      ExpectSuccess::kFalse);
}

TEST_F(PgDdlAtomicitySnapshotTest, SuccessfulDdlListSnapshotTest) {
  ASSERT_OK(ListSnapshotTest(DdlErrorInjection::kFalse));
}

TEST_F(PgDdlAtomicitySnapshotTest, DdlRollbackListSnapshotTest) {
  ASSERT_OK(ListSnapshotTest(DdlErrorInjection::kTrue));
}

class PgLibPqMatviewTest: public PgDdlAtomicitySanityTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
      "--allowed_preview_flags_csv=ysql_ddl_rollback_enabled");
    options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
  }
 protected:
  void MatviewTest();
};

void PgLibPqMatviewTest::MatviewTest() {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t(id int)"));

  // Test matview creation failure.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(conn.TestFailDdl("CREATE MATERIALIZED VIEW mv AS SELECT * FROM t"));
  // Verify matview creation is rolled back.
  VerifyTableNotExists(client.get(), kDatabase, "mv", 10);

  // Verify successful materialized view creation.
  const string rename_test = "rename_mv";
  const string renamed_name = "foobar";
  const string rename_col_test = "rename_col_mv";

  // Verify alter materialized view fails.
  ASSERT_OK(conn.ExecuteFormat("CREATE MATERIALIZED VIEW $0 AS SELECT * FROM t", rename_test));
  ASSERT_OK(conn.ExecuteFormat("CREATE MATERIALIZED VIEW $0 AS SELECT * FROM t", rename_col_test));
  ASSERT_OK(conn.TestFailDdl(Format("ALTER MATERIALIZED VIEW $0 RENAME TO $1",
      rename_test, renamed_name)));
  ASSERT_OK(conn.TestFailDdl(Format("ALTER MATERIALIZED VIEW $0 RENAME $1 TO $2", rename_col_test,
      "id", "foobar")));
  VerifyTableNotExists(client.get(), kDatabase, renamed_name, 10);
  VerifyTableExists(client.get(), kDatabase, rename_test, 10);
  ASSERT_OK(VerifySchema(client.get(), kDatabase, rename_col_test, {"ybrowid", "id"}));

  // Verify alter materialized view success.
  ASSERT_OK(conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 RENAME TO $1",
      rename_test, renamed_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 RENAME $1 TO $2", rename_col_test,
      "id", renamed_name));
  VerifyTableNotExists(client.get(), kDatabase, rename_test, 10);
  VerifyTableExists(client.get(), kDatabase, renamed_name, 10);
  ASSERT_OK(VerifySchema(client.get(), kDatabase, rename_col_test, {"ybrowid", renamed_name}));

  // Verify refresh failure.
  ASSERT_OK(conn.Execute("CREATE MATERIALIZED VIEW mv AS SELECT * FROM t"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1), (2)"));
  ASSERT_OK(conn.TestFailDdl(Format("REFRESH MATERIALIZED VIEW mv")));
  // Wait for rollback to complete.
  SleepFor(2s);
  auto curr_rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM mv"));
  ASSERT_EQ(curr_rows, 0);

  // Verify refresh success.
  ASSERT_OK(conn.Execute("REFRESH MATERIALIZED VIEW mv"));
  curr_rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM mv"));
  ASSERT_EQ(curr_rows, 2);

  // Perform another refresh to verify the case where relfilenode of both old and new table no
  // longer matches the oid.
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (3), (4)"));
  ASSERT_OK(conn.Execute("REFRESH MATERIALIZED VIEW mv"));
  curr_rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM mv"));
  ASSERT_EQ(curr_rows, 4);
}

TEST_F(PgLibPqMatviewTest, MatviewTestWithoutPgOptimization) {
  ASSERT_OK(cluster_->SetFlagOnTServers("report_ysql_ddl_txn_status_to_master", "false"));
  MatviewTest();
}

TEST_F(PgLibPqMatviewTest, MatviewTest) {
  MatviewTest();
}

YB_STRONGLY_TYPED_BOOL(EnableDDLAtomicity);

// TODO(deepthi): Remove the tests for txn GC after 'ysql_ddl_rollback_enabled' is set to true
// by default.
class PgLibPqTableRewrite:
  public PgDdlAtomicitySanityTest,
  public ::testing::WithParamInterface<EnableDDLAtomicity> {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgDdlAtomicitySanityTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
      "--allowed_preview_flags_csv=ysql_ddl_rollback_enabled");
    options->extra_tserver_flags.push_back("--ysql_enable_reindex=true");
    if (!GetParam()) {
      options->extra_master_flags.push_back("--ysql_transaction_bg_task_wait_ms=10000");
      // Disable the current version of DDL rollback so that we can test the
      // transaction GC framework.
      options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=false");
    } else {
      options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
    }
  }
 protected:
  void SetupTestData() {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a int, b int, PRIMARY KEY (a ASC))", kTable));
    // Insert some data.
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 (a, b) VALUES (generate_series(1, 5), generate_series(1, 5))", kTable));
    // Create index.
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (b DESC)", kIndex, kTable));
    // Create materialized view.
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1", kMaterializedView, kTable));
    // Insert some more data.
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (a, b) VALUES (6, 6)", kTable));
    // Set the index as invalid (so that we can test reindex).
    ASSERT_OK(conn.ExecuteFormat(
        "UPDATE pg_index SET indisvalid='f' WHERE indexrelid = '$0'::regclass", kIndex));
  }
  Status WaitForDroppedTablesCleanup() {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    return LoggedWaitFor([this, &client]() -> Result<bool> {
      auto num_tables = VERIFY_RESULT(client->ListTables(kTable)).size();
      auto num_indexes = VERIFY_RESULT(client->ListTables(kIndex)).size();
      auto num_matviews = VERIFY_RESULT(client->ListTables(kMaterializedView)).size();
      return num_tables == 1 && num_indexes == 1 && num_matviews == 1;
    }, MonoDelta::FromSeconds(60), "Verify that we dropped the stale DocDB tables");
  }
  const std::string kTable = "test_table";
  const std::string kIndex = "test_idx";
  const std::string kMaterializedView = "test_mv";
};

INSTANTIATE_TEST_CASE_P(bool, PgLibPqTableRewrite,
    ::testing::Values(EnableDDLAtomicity::kFalse, EnableDDLAtomicity::kTrue));

// Test that orphaned tables left after failed rewrites are cleaned up.
// TODO(deepthi): Re-enable both these tests in TSAN after #16055 is fixed.
TEST_P(PgLibPqTableRewrite,
       YB_DISABLE_TEST_IN_TSAN(TestTableRewriteRollback)) {
  SetupTestData();
  if (GetParam()) {
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  }

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("SET yb_test_fail_table_rewrite_after_creation=true"));
  ASSERT_NOK(conn.ExecuteFormat("REINDEX INDEX $0", kIndex));
  ASSERT_NOK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c SERIAL", kTable));
  ASSERT_NOK(conn.ExecuteFormat("ALTER TABLE $0 DROP CONSTRAINT $0_pkey", kTable));
  ASSERT_NOK(conn.ExecuteFormat("REFRESH MATERIALIZED VIEW $0", kMaterializedView));

  // Verify that we created orphaned DocDB tables.
  const auto client = ASSERT_RESULT(cluster_->CreateClient());
  vector<client::YBTableName> tables = ASSERT_RESULT(client->ListTables(kTable));
  ASSERT_EQ(tables.size(), 3);
  tables = ASSERT_RESULT(client->ListTables(kIndex));
  ASSERT_EQ(tables.size(), 2);
  tables = ASSERT_RESULT(client->ListTables(kMaterializedView));
  ASSERT_EQ(tables.size(), 2);

  // Verify that we drop the new DocDB tables after failed rewrite operations.
  if (GetParam()) {
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false"));
  }
  ASSERT_OK(WaitForDroppedTablesCleanup());

  // Verify the data.
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE pg_index SET indisvalid = 't' WHERE indexrelid = '$0'::regclass", kIndex));
  ASSERT_NOK(conn.ExecuteFormat("SELECT c FROM $0", kTable));
  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
      Format("SELECT * FROM $0", kTable))));
  ASSERT_EQ(rows, (decltype(rows){{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}}));
  rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(Format(
      "SELECT * FROM $0 WHERE b = 1", kTable))));
  ASSERT_EQ(rows, (decltype(rows){{1, 1}}));
  rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(Format(
      "SELECT * FROM $0 ORDER BY a", kMaterializedView))));
  ASSERT_EQ(rows, (decltype(rows){{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}}));
}

// Test that orphaned tables left after successful rewrites are cleaned up.
TEST_P(PgLibPqTableRewrite,
       YB_DISABLE_TEST_IN_TSAN(TestTableRewriteSuccess)) {
  SetupTestData();

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("REINDEX INDEX $0", kIndex));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c SERIAL", kTable));
  ASSERT_OK(conn.ExecuteFormat("REFRESH MATERIALIZED VIEW $0", kMaterializedView));

  // Verify that we drop the old DocDB tables after successful rewrite operations.
  ASSERT_OK(WaitForDroppedTablesCleanup());

  // Sanity check to ensure we can perform ALTERs on the rewritten table/materialized view.
  const auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN d int", kTable));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 RENAME COLUMN c TO c_serial", kTable));
  ASSERT_OK(VerifySchema(client.get(), "yugabyte", kTable, {"a", "b", "c_serial", "d"}));
  ASSERT_OK(conn.ExecuteFormat("ALTER MATERIALIZED VIEW $0 RENAME COLUMN a TO a_renamed",
      kMaterializedView));
  ASSERT_OK(VerifySchema(client.get(), "yugabyte", kMaterializedView,
      {"ybrowid", "a_renamed", "b"}));
}

} // namespace pgwrapper
} // namespace yb
