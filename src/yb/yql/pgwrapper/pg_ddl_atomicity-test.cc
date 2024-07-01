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
#include <mutex>

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
#include "yb/util/test_thread_holder.h"
#include "yb/util/timestamp.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(ysql_ddl_rollback_enabled);

using std::string;
using std::vector;

namespace yb {
namespace pgwrapper {

const auto kCreateTable = "create_test"s;
const auto kRenameTable = "rename_table_test"s;
const auto kRenameCol = "rename_col_test"s;
const auto kAddCol = "add_col_test"s;
const auto kDropTable = "drop_test"s;

const auto kDatabase = "yugabyte"s;
const auto kDdlVerificationError = "Table is undergoing DDL transaction verification"s;

class PgDdlAtomicityTest : public LibPqTestBase {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_transaction_bg_task_wait_ms=5000");
  }

 protected:
  void CreateTable(const string& tablename) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute(CreateTableStmt(tablename)));
    // Sleep until the transaction ddl state is cleared from this table.
    // TODO (deepthi): This will no longer be needed after Phase-II.
    sleep(2);
  }

  string CreateTableStmt(const string& tablename) {
    return "CREATE TABLE " + tablename + " (key INT PRIMARY KEY)";
  }

  string RenameTableStmt(const string& tablename) {
    return RenameTableStmt(tablename, "foobar");
  }

  string RenameTableStmt(const string& tablename, const string& table_new_name) {
    return Format("ALTER TABLE $0 RENAME TO $1", tablename, table_new_name);
    }

  string AddColumnStmt(const string& tablename) {
    return AddColumnStmt(tablename, "value");
  }

  string AddColumnStmt(const string& tablename, const string& col_name_to_add) {
    return Format("ALTER TABLE $0 ADD COLUMN $1 TEXT", tablename, col_name_to_add);
  }

  string RenameColumnStmt(const string& tablename) {
    return RenameColumnStmt(tablename, "key", "key2");
  }

  string RenameColumnStmt(const string& tablename,
                          const string& col_to_rename,
                          const string& col_new_name) {
    return Format("ALTER TABLE $0 RENAME COLUMN $1 TO $2", tablename, col_to_rename, col_new_name);
  }

  string DropTableStmt(const string& tablename) {
    return "DROP TABLE " + tablename;
  }

  bool IsDdlVerificationError(const string& error) {
    return error.find(kDdlVerificationError) != string::npos;
  }

  Status ExecuteWithRetry(PGConn *conn, const string& ddl) {
    for (size_t num_retries = 0; num_retries < 5; ++num_retries) {
      auto s = conn->Execute(ddl);
      if (s.ok() || !IsDdlVerificationError(s.ToString())) {
        return s;
      }
      // Sleep before retrying again.
      sleep(1);
    }
    return STATUS_FORMAT(IllegalState, "Failed to execute DDL statement $0", ddl);
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
      sleep(5);
    }
  }

  Status VerifySchema(client::YBClient* client,
                    const string& database_name,
                    const string& table_name,
                    const vector<string>& expected_column_names) {
    return LoggedWaitFor([&] {
      return CheckIfSchemaMatches(client, database_name, table_name, expected_column_names);
    }, MonoDelta::FromSeconds(60), "Wait for schema to match");
  }

  Result<bool> CheckIfSchemaMatches(client::YBClient* client,
                                    const string& database_name,
                                    const string& table_name,
                                    const vector<string>& expected_column_names) {
    std::string table_id = VERIFY_RESULT(GetTableIdByTableName(client, database_name, table_name));

    std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
    Synchronizer sync;
    RETURN_NOT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    RETURN_NOT_OK(sync.Wait());

    const auto& columns = table_info->schema.columns();
    if (expected_column_names.size() != columns.size()) {
      LOG(INFO) << "Expected " << expected_column_names.size() << " for " << table_name << " but "
                << "found " << columns.size() << " columns";
      return false;
    }
    for (size_t i = 0; i < expected_column_names.size(); ++i) {
      if (columns[i].name().compare(expected_column_names[i]) != 0) {
        LOG(INFO) << "Expected column " << expected_column_names[i] << " but found "
                  << columns[i].name();
        return false;
      }
    }
    return true;
  }
};

TEST_F(PgDdlAtomicityTest, YB_DISABLE_TEST_IN_TSAN(TestDatabaseGC)) {
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

TEST_F(PgDdlAtomicityTest, YB_DISABLE_TEST_IN_TSAN(TestCreateDbFailureAndRestartGC)) {
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

TEST_F(PgDdlAtomicityTest, YB_DISABLE_TEST_IN_TSAN(TestIndexTableGC)) {
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

  ASSERT_OK(conn.TestFailDdl("CREATE INDEX " + test_name_idx + " ON " + test_name + "(key)"));

  // Wait for DocDB index creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  VerifyTableExists(client.get(), kDatabase, test_name_idx, 10);

  // DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the index.
  VerifyTableNotExists(client.get(), kDatabase, test_name_idx, 40);
}

TEST_F(
    PgDdlAtomicityTest, YB_DISABLE_TEST_IN_TSAN(TestRaceIndexDeletionAndReadQueryOnColocatedDB)) {
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
        conn.FetchValue<int32_t>(Format("SELECT key FROM $0 where value = '1000'", table_name)));
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
        conn.FetchValue<int32_t>(Format("SELECT key FROM $0 where value = '3000'", table_name));
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
    options->extra_master_flags.push_back("--ysql_ddl_rollback_enabled=true");
    options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
    options->extra_tserver_flags.push_back("--report_ysql_ddl_txn_status_to_master=true");
  }
};

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(AlterDropTableRollback)) {
  const auto tables = {kRenameTable, kRenameCol, kAddCol, kDropTable};
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  // Wait for the transaction state left by create table statement to clear.
  sleep(5);

  // Deliberately cause failure of the following Alter Table statements.
  ASSERT_OK(conn.TestFailDdl(DropTableStmt(kDropTable)));
  ASSERT_OK(conn.TestFailDdl(RenameTableStmt(kRenameTable)));
  ASSERT_OK(conn.TestFailDdl(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn.TestFailDdl(AddColumnStmt(kAddCol)));

  for (const auto& table : tables) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key"}));
  }

  // Verify that DDL succeeds after rollback is complete.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(RenameTableStmt(kRenameTable)));
  ASSERT_OK(conn.Execute(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn.Execute(AddColumnStmt(kAddCol)));
  ASSERT_OK(conn.Execute(DropTableStmt(kDropTable)));
}

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(PrimaryKeyRollback)) {
  TableName add_pk_table = "add_pk_table";
  TableName drop_pk_table = "drop_pk_table";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id int)", add_pk_table));
  CreateTable(drop_pk_table);

  // Delay rollback.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "15"));
  // Fail Alter operation that adds/drops primary key.
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 ADD PRIMARY KEY(id)", add_pk_table)));
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP CONSTRAINT $0_pkey;", drop_pk_table)));

  // Verify presence of temp table created for adding a primary key.
  ASSERT_EQ(ASSERT_RESULT(client->ListTables(add_pk_table, false /* exclude_ysql */)).size(), 2);
  ASSERT_EQ(ASSERT_RESULT(client->ListTables(drop_pk_table, false /* exclude_ysql */)).size(), 2);

  // Verify that the background task detected the failure of the DDL operation and removed the
  // temp table.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      if (VERIFY_RESULT(client->ListTables(add_pk_table, false /* exclude_ysql */)).size() == 1
          && VERIFY_RESULT(client->ListTables(drop_pk_table, false /* exclude_ysql */)).size() == 1)
        return true;
      return false;
  }, MonoDelta::FromSeconds(60), "Wait for new DocDB tables to be dropped."));

  // Verify that PK constraint is not present on the table.
  ASSERT_OK(conn.Execute("INSERT INTO " + add_pk_table + " VALUES (1), (1)"));

  // Verify that PK constraint is still present on the table.
  ASSERT_NOK(conn.Execute("INSERT INTO " + drop_pk_table + " VALUES (1), (1)"));
}

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(DdlRollbackMasterRestart)) {
  const auto tables_to_create = {kRenameTable, kRenameCol, kAddCol, kDropTable};
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  // Set TEST_delay_ysql_ddl_rollback_secs so high that table rollback is nearly
  // disabled.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "100"));
  ASSERT_OK(conn.TestFailDdl(CreateTableStmt(kCreateTable)));
  ASSERT_OK(conn.TestFailDdl(RenameTableStmt(kRenameTable)));
  ASSERT_OK(conn.TestFailDdl(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn.TestFailDdl(AddColumnStmt(kAddCol)));
  ASSERT_OK(conn.TestFailDdl(DropTableStmt(kDropTable)));

  // Verify that table was created on DocDB.
  VerifyTableExists(client.get(), kDatabase, kCreateTable, 10);
  VerifyTableExists(client.get(), kDatabase, kDropTable, 10);

  // Verify that the tables were altered on DocDB.
  ASSERT_OK(VerifySchema(client.get(), kDatabase, "foobar", {"key"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenameCol, {"key2"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));

  // Restart the master before the BG task can kick in and GC the failed transaction.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "0"));
  RestartMaster();

  // Verify that rollback reflected on all the tables after restart.
  client = ASSERT_RESULT(cluster_->CreateClient()); // Reinit the YBClient after restart.

  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 20);
  VerifyTableExists(client.get(), kDatabase, kRenameTable, 20);
  // Verify all the other tables are unchanged by all of the DDLs.
  for (const string& table : tables_to_create) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key"}));
  }
}

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(TestYsqlTxnStatusReporting)) {
  const auto tables_to_create = {kRenameTable, kRenameCol, kAddCol, kDropTable};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  {
    auto conn = ASSERT_RESULT(Connect());
    for (const auto& table : tables_to_create) {
      ASSERT_OK(conn.Execute(CreateTableStmt(table)));
    }

    // Disable YB-Master's background task.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
    // Run some failing Ddl transactions
    ASSERT_OK(conn.TestFailDdl(CreateTableStmt(kCreateTable)));
    ASSERT_OK(conn.TestFailDdl(RenameTableStmt(kRenameTable)));
    ASSERT_OK(conn.TestFailDdl(RenameColumnStmt(kRenameCol)));
    ASSERT_OK(conn.TestFailDdl(AddColumnStmt(kAddCol)));
    ASSERT_OK(conn.TestFailDdl(DropTableStmt(kDropTable)));
  }

  // The rollback should have succeeded anyway because YSQL would have reported the
  // status.
  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 10);

  // Verify all the other tables are unchanged by all of the DDLs.
  for (const auto& table : tables_to_create) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key"}));
  }

  // Now test successful DDLs.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(CreateTableStmt(kCreateTable)));
  ASSERT_OK(conn.Execute(RenameTableStmt(kRenameTable)));
  ASSERT_OK(conn.Execute(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn.Execute(AddColumnStmt(kAddCol)));
  ASSERT_OK(conn.Execute(DropTableStmt(kDropTable)));

  VerifyTableExists(client.get(), kDatabase, kCreateTable, 10);
  VerifyTableNotExists(client.get(), kDatabase, kDropTable, 10);

  // Verify that the tables were altered on DocDB.
  ASSERT_OK(VerifySchema(client.get(), kDatabase, "foobar", {"key"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenameCol, {"key2"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));
}

class PgDdlAtomicityParallelDdlTest : public PgDdlAtomicitySanityTest {
 public:
  template<class... Args>
  Result<bool> RunDdlHelper(const std::string& format, const Args&... args) {
    return DoRunDdlHelper(Format(format, args...));
  }
 private:
  Result<bool> DoRunDdlHelper(const string& ddl) {
    auto conn = VERIFY_RESULT(Connect());
    auto s = conn.Execute(ddl);
    if (s.ok()) {
      return true;
    }

    const auto msg = s.message().ToBuffer();
    static const auto allowed_msgs = {
      "Catalog Version Mismatch"s,
      "could not serialize access due to concurrent update"s,
      "Restart read required"s,
      "Transaction aborted"s,
      "Transaction metadata missing"s,
      "Unknown transaction, could be recently aborted"s,
      "Flush: Value write after transaction start"s,
      kDdlVerificationError
    };
    if (std::find_if(
        std::begin(allowed_msgs),
        std::end(allowed_msgs),
        [&msg] (const string& allowed_msg) {
          return msg.find(allowed_msg) != string::npos;
        }) != std::end(allowed_msgs)) {
      LOG(ERROR) << "Unexpected failure status " << s;
      return false;
    }
    return true;
  }
};

TEST_F(PgDdlAtomicityParallelDdlTest, YB_DISABLE_TEST_IN_TSAN(TestParallelDdl)) {
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
    auto conn = ASSERT_RESULT(Connect());
    for (size_t i = kNumIterations; i < kNumIterations * 2;) {
      if (ASSERT_RESULT(RunDdlHelper("ALTER TABLE $0 ADD COLUMN col_$1 TEXT", tablename, i))) {
       ++i;
      }
    }
  });

  // Rename columns in the second thread.
  thread_holder.AddThreadFunctor([this, &tablename] {
    auto conn = ASSERT_RESULT(Connect());
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
  ASSERT_OK(VerifySchema(client.get(), kDatabase, "foobar", {"key"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kRenameCol, {"key2"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));

  // Disable DDL rollback.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_ddl_rollback_enabled", "false"));
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
  ASSERT_OK(VerifySchema(client.get(), kDatabase, "foobar", {"key"}));
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

TEST_F(PgDdlAtomicityTest, YB_DISABLE_TEST_IN_TSAN(FailureRecoveryTestWithAbortedTxn)) {
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
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgDdlAtomicitySanityTest::UpdateMiniClusterOptions(options);
  }

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
    // been delayed using 'TEST_delay_ysql_ddl_rollback_secs'.
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

TEST_F(PgDdlAtomicityConcurrentDdlTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentDdl)) {
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
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "6"));
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
  void runFailingDdlTransaction(const string& ddl_statements) {
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
    // Sleep 1s for the rollback to complete.
    sleep(1);
  }

  string table() {
    return "test_table";
  }
};

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(CreateAlterDropTest)) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          AddColumnStmt(table()) +  "; " +
                          RenameColumnStmt(table()) + "; " +
                          RenameTableStmt(table()) + "; " +
                          DropTableStmt("foobar") + ";";
  runFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(CreateDropTest)) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          DropTableStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(CreateAlterTest)) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          AddColumnStmt(table()) +  "; " +
                          RenameColumnStmt(table()) + "; " +
                          RenameTableStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), kDatabase, table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(AlterDropTest)) {
  CreateTable(table());
  string ddl_statements = RenameColumnStmt(table()) + "; " + DropTableStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key"}));
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(AddColRenameColTest)) {
  CreateTable(table());
  string ddl_statements = AddColumnStmt(table()) + "; " + RenameColumnStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key"}));
}

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(DmlWithAddColTest)) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string& table = "dml_with_add_col_test";
  CreateTable(table);
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  // Conn1: Begin write to the table.
  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (1)"));

  // Conn2: Initiate rollback of the alter.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "10"));
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
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key"}));

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
TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(DmlWithDropTableTest)) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string& table = "dml_with_drop_test";
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
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "10"));
  ASSERT_OK(conn3.TestFailDdl(DropTableStmt(table)));

  // Conn1: This should succeed.
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (3)"));
  ASSERT_OK(conn1.Execute("COMMIT"));

  // Wait for rollback to complete.
  sleep(5);

  // Conn2 must also commit successfully.
  ASSERT_OK(conn2.Execute("INSERT INTO " + table + " VALUES (4)"));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

class PgDdlAtomicityNegativeTestBase : public PgDdlAtomicitySanityTest {
 protected:
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  virtual string CreateTableStmt(const string& tablename) {
    return PgDdlAtomicitySanityTest::CreateTableStmt(tablename);
  }

  virtual string CreateRollbackEnabledTableStmt(const string& tablename) = 0;

  virtual string CreateIndexStmt(const string& tablename, const string& indexname) {
    return Format("CREATE INDEX $0 ON $1(key)", indexname, tablename);
  }

  // Test function.
  void negativeTest();
  void negativeDropTableTxnTest();

  std::unique_ptr<PGConn> conn_;
  string kDatabaseName = "yugabyte";
};

void PgDdlAtomicityNegativeTestBase::negativeTest() {
  const auto create_table_idx = "create_table_idx"s;
  const auto tables_to_create = {kRenameTable, kRenameCol, kAddCol};

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn_->Execute(CreateTableStmt(table)));
  }

  ASSERT_OK(conn_->TestFailDdl(CreateTableStmt(kCreateTable)));
  ASSERT_OK(conn_->TestFailDdl(CreateIndexStmt(kRenameTable, create_table_idx)));
  ASSERT_OK(conn_->TestFailDdl(RenameTableStmt(kRenameTable)));
  ASSERT_OK(conn_->TestFailDdl(RenameColumnStmt(kRenameCol)));
  ASSERT_OK(conn_->TestFailDdl(AddColumnStmt(kAddCol)));

  // Create is rolled back using existing transaction GC infrastructure.
  VerifyTableNotExists(client.get(), kDatabaseName, kCreateTable, 30);
  VerifyTableNotExists(client.get(), kDatabaseName, create_table_idx, 30);

  // Verify that Alter table transactions are not rolled back because rollback is not enabled yet
  // for certain cases like colocation and tablegroups.
  ASSERT_OK(VerifySchema(client.get(), kDatabaseName, "foobar", {"key"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabaseName, kRenameCol, {"key2"}));
  ASSERT_OK(VerifySchema(client.get(), kDatabaseName, kAddCol, {"key", "value"}));
}

void PgDdlAtomicityNegativeTestBase::negativeDropTableTxnTest() {
  // Verify that dropping a table with rollback enabled and another table with rollback disabled
  // in the same transaction works as expected.
  TableName rollback_enabled_table = "rollback_disabled_table";
  TableName rollback_disabled_table = "rollback_enabled_table";

  ASSERT_OK(conn_->Execute(CreateTableStmt(rollback_disabled_table)));
  ASSERT_OK(conn_->Execute(CreateRollbackEnabledTableStmt(rollback_enabled_table)));

  // Wait for rollback state to clear from the created tables.
  sleep(1);

  ASSERT_OK(conn_->TestFailDdl(Format("DROP TABLE $0, $1",
                                      rollback_enabled_table, rollback_disabled_table)));
  // The YB-Master background task ensures that 'rollback_enabled_table' is not deleted. For
  // 'rollback_disabled_table', PG layer delays sending the delete request until the transaction
  // completes. Thus, verify that the tables do not get deleted even after some time.
  sleep(5);
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableExists(client.get(), kDatabaseName, rollback_disabled_table, 15);
  VerifyTableExists(client.get(), kDatabaseName, rollback_enabled_table, 15);

  // DDLs succeed after rollback.
  ASSERT_OK(conn_->Execute(RenameColumnStmt(rollback_disabled_table)));
  ASSERT_OK(conn_->Execute(AddColumnStmt(rollback_enabled_table)));
}

class PgDdlAtomicityNegativeTestColocated : public PgDdlAtomicityNegativeTestBase {
  void SetUp() override {
    LibPqTestBase::SetUp();

    kDatabaseName = "colocateddbtest";
    PGConn conn_init = ASSERT_RESULT(Connect());
    ASSERT_OK(conn_init.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  string CreateRollbackEnabledTableStmt(const string& tablename) override {
    return Format("CREATE TABLE $0 (a INT) WITH (colocated = false);", tablename);
  }
};

TEST_F(PgDdlAtomicityNegativeTestColocated, YB_DISABLE_TEST_IN_TSAN(ColocatedTest)) {
  negativeTest();
  negativeDropTableTxnTest();
}

class PgDdlAtomicityNegativeTestTablegroup : public PgDdlAtomicityNegativeTestBase {
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(Connect()));
    ASSERT_OK(conn_->ExecuteFormat("CREATE TABLEGROUP $0", kTablegroup));
  }

  string CreateTableStmt(const string& tablename) override {
    return (PgDdlAtomicitySanityTest::CreateTableStmt(tablename) + " TABLEGROUP " + kTablegroup);
  }

  string CreateRollbackEnabledTableStmt(const string& tablename) override {
    return PgDdlAtomicitySanityTest::CreateTableStmt(tablename);
  }

  const string kTablegroup = "test_tgroup";
};

TEST_F(PgDdlAtomicityNegativeTestTablegroup, YB_DISABLE_TEST_IN_TSAN(TablegroupTest)) {
  negativeTest();
  negativeDropTableTxnTest();
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
};

TEST_F(PgDdlAtomicitySnapshotTest, YB_DISABLE_TEST_IN_TSAN(SnapshotTest)) {
  // Create requisite tables.
  const auto tables_to_create = {kAddCol, kDropTable};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  const int snapshot_interval_secs = 10;
  // Create a snapshot schedule before running all the failed DDLs.
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(kDatabase,
                                   client::WaitSnapshot::kTrue,
                                   MonoDelta::FromSeconds(snapshot_interval_secs)));

  // Run all the failed DDLs.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "35"));
  ASSERT_OK(conn.TestFailDdl(CreateTableStmt(kCreateTable)));
  ASSERT_OK(conn.TestFailDdl(AddColumnStmt(kAddCol)));
  ASSERT_OK(conn.TestFailDdl(DropTableStmt(kDropTable)));

  // Wait 10s to ensure that a snapshot is taken right after these DDLs failed.
  sleep(snapshot_interval_secs);

  // Get the hybrid time before the rollback can happen.
  Timestamp current_time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime hybrid_time_before_rollback = HybridTime::FromMicros(current_time.ToInt64());

  // Verify that rollback for Alter and Create has indeed not happened yet.
  VerifyTableExists(client.get(), kDatabase, kCreateTable, 10);
  VerifyTableExists(client.get(), kDatabase, kDropTable, 10);
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value"}));

  // Verify that rollback indeed happened.
  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 60);

  // Verify all the other tables are unchanged by all of the DDLs.
  for (const auto& table : tables_to_create) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key"}));
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
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key"}));
  }
}

} // namespace pgwrapper
} // namespace yb
