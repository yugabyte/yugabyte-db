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

  string db() {
    return "yugabyte";
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

  void VerifySchema(client::YBClient* client,
                    const string& database_name,
                    const string& table_name,
                    const vector<string>& expected_column_names) {
    ASSERT_TRUE(ASSERT_RESULT(
      CheckIfSchemaMatches(client, database_name, table_name, expected_column_names)));
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
  ASSERT_NOK(conn.TestFailDdl("CREATE DATABASE " + test_name));

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
  ASSERT_NOK(conn.TestFailDdl("CREATE DATABASE " + test_name));

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

  ASSERT_NOK(conn.TestFailDdl("CREATE INDEX " + test_name_idx + " ON " + test_name + "(key)"));

  // Wait for DocDB index creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  VerifyTableExists(client.get(), db(), test_name_idx, 10);

  // DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the index.
  VerifyTableNotExists(client.get(), db(), test_name_idx, 40);
}

// Class for sanity test.
class PgDdlAtomicitySanityTest : public PgDdlAtomicityTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_ddl_rollback_enabled=true");
    options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
  }
};

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(AlterDropTableRollback)) {
  TableName rename_table_test = "rename_table_test";
  TableName rename_col_test = "rename_col_test";
  TableName add_col_test = "add_col_test";
  TableName drop_table_test = "drop_table_test";

  vector<TableName> tables = {
    rename_table_test, rename_col_test, add_col_test, drop_table_test};
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  auto conn = ASSERT_RESULT(Connect());
  for (size_t ii = 0; ii < tables.size(); ++ii) {
    ASSERT_OK(conn.Execute(CreateTableStmt(tables[ii])));
  }

  // Wait for the transaction state left by create table statement to clear.
  sleep(5);

  // Deliberately cause failure of the following Alter Table statements.
  ASSERT_NOK(conn.TestFailDdl(RenameTableStmt(rename_table_test)));
  ASSERT_NOK(conn.TestFailDdl(RenameColumnStmt(rename_col_test)));
  ASSERT_NOK(conn.TestFailDdl(AddColumnStmt(add_col_test)));
  ASSERT_NOK(conn.TestFailDdl(DropTableStmt(drop_table_test)));

  // Wait for rollback.
  sleep(5);
  for (size_t ii = 0; ii < tables.size(); ++ii) {
    VerifySchema(client.get(), db(), tables[ii], {"key"});
  }

  // Verify that DDL succeeds after rollback is complete.
  // TODO: Need to start a different connection here for rename to work as expected until #14395
  // is fixed.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(RenameTableStmt(rename_table_test)));
  ASSERT_OK(conn.Execute(RenameColumnStmt(rename_col_test)));
  ASSERT_OK(conn.Execute(AddColumnStmt(add_col_test)));
  ASSERT_OK(conn.Execute(DropTableStmt(drop_table_test)));
}

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(PrimaryKeyRollback)) {
  TableName add_pk_table = "add_pk_table";
  TableName drop_pk_table = "drop_pk_table";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id int)", add_pk_table));
  ASSERT_OK(conn.Execute(CreateTableStmt(drop_pk_table)));

  // Wait for transaction verification on the tables to complete.
  sleep(1);

  // Delay rollback.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "10000"));
  // Fail Alter operation that adds/drops primary key.
  ASSERT_NOK(conn.TestFailDdl(Format("ALTER TABLE $0 ADD PRIMARY KEY(id)", add_pk_table)));
  ASSERT_NOK(conn.TestFailDdl(Format("ALTER TABLE $0 DROP CONSTRAINT $0_pkey;", drop_pk_table)));

  // Verify presence of temp table created for adding a primary key.
  VerifyTableExists(client.get(), db(), add_pk_table + "_temp_old", 10);
  VerifyTableExists(client.get(), db(), drop_pk_table + "_temp_old", 10);

  // Verify that the background task detected the failure of the DDL operation and removed the
  // temp table.
  VerifyTableNotExists(client.get(), db(), add_pk_table + "_temp_old", 40);
  VerifyTableNotExists(client.get(), db(), drop_pk_table + "_temp_old", 40);

  // Verify that PK constraint is not present on the table.
  ASSERT_OK(conn.Execute("INSERT INTO " + add_pk_table + " VALUES (1), (1)"));

  // Verify that PK constraint is still present on the table.
  ASSERT_NOK(conn.Execute("INSERT INTO " + drop_pk_table + " VALUES (1), (1)"));
}

TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST_IN_TSAN(DdlRollbackMasterRestart)) {
  TableName create_table_test = "create_test";
  TableName rename_table_test = "rename_table_test";
  TableName rename_col_test = "rename_col_test";
  TableName add_col_test = "add_col_test";
  TableName drop_table_test = "drop_test";

  vector<TableName> tables_to_create = {
    rename_table_test, rename_col_test, add_col_test, drop_table_test};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (size_t ii = 0; ii < tables_to_create.size(); ++ii) {
    ASSERT_OK(conn.Execute(CreateTableStmt(tables_to_create[ii])));
  }

  // Set ysql_transaction_bg_task_wait_ms so high that table rollback is nearly
  // disabled.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "100000"));
  ASSERT_NOK(conn.TestFailDdl(CreateTableStmt(create_table_test)));
  ASSERT_NOK(conn.TestFailDdl(RenameTableStmt(rename_table_test)));
  ASSERT_NOK(conn.TestFailDdl(RenameColumnStmt(rename_col_test)));
  ASSERT_NOK(conn.TestFailDdl(AddColumnStmt(add_col_test)));
  ASSERT_NOK(conn.TestFailDdl(DropTableStmt(drop_table_test)));

  // Verify that table was created on DocDB.
  VerifyTableExists(client.get(), db(), create_table_test, 10);
  VerifyTableExists(client.get(), db(), drop_table_test, 10);

  // Verify that the tables were altered on DocDB.
  VerifySchema(client.get(), db(), "foobar", {"key"});
  VerifySchema(client.get(), db(), rename_col_test, {"key2"});
  VerifySchema(client.get(), db(), add_col_test, {"key", "value"});

  // Restart the master before the BG task can kick in and GC the failed transaction.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "0"));
  RestartMaster();

  // Verify that rollback reflected on all the tables after restart.
  client = ASSERT_RESULT(cluster_->CreateClient()); // Reinit the YBClient after restart.

  VerifyTableNotExists(client.get(), db(), create_table_test, 20);
  VerifyTableExists(client.get(), db(), rename_table_test, 20);
  // Verify all the other tables are unchanged by all of the DDLs.
  for (const string& table : tables_to_create) {
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      return CheckIfSchemaMatches(client.get(), db(), table, {"key"});
    }, MonoDelta::FromSeconds(30), Format("Wait for rollback for table $0", table)));
  }
}

// TODO (deepthi) : This test is flaky because of #14995. Re-enable it back after #14995 is fixed.
TEST_F(PgDdlAtomicitySanityTest, YB_DISABLE_TEST(FailureRecoveryTest)) {
  TableName rename_table_test = "rename_table_test";
  TableName rename_col_test = "rename_col_test";
  TableName add_col_test = "add_col_test";
  TableName drop_table_test = "drop_test";

  vector<TableName> tables_to_create = {
    rename_table_test, rename_col_test, add_col_test, drop_table_test};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (size_t ii = 0; ii < tables_to_create.size(); ++ii) {
    ASSERT_OK(conn.Execute(CreateTableStmt(tables_to_create[ii])));
  }

  // Set ysql_transaction_bg_task_wait_ms so high that table rollback is nearly
  // disabled.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "100000"));
  ASSERT_NOK(conn.TestFailDdl(RenameTableStmt(rename_table_test)));
  ASSERT_NOK(conn.TestFailDdl(RenameColumnStmt(rename_col_test)));
  ASSERT_NOK(conn.TestFailDdl(AddColumnStmt(add_col_test)));
  ASSERT_NOK(conn.TestFailDdl(DropTableStmt(drop_table_test)));

  // Verify that table was created on DocDB.
  VerifyTableExists(client.get(), db(), drop_table_test, 10);

  // Verify that the tables were altered on DocDB.
  VerifySchema(client.get(), db(), "foobar", {"key"});
  VerifySchema(client.get(), db(), rename_col_test, {"key2"});
  VerifySchema(client.get(), db(), add_col_test, {"key", "value"});

  // Disable DDL rollback.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_ddl_rollback_enabled", "false"));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_ddl_rollback_enabled", "false"));

  // Verify that best effort rollback works when ysql_ddl_rollback is disabled.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", add_col_test));
  // The following DDL fails because the table already contains data, so it is not possible to
  // add a new column with a not null constraint.
  ASSERT_NOK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN value2 TEXT NOT NULL"));
  // Verify that the above DDL was rolled back by YSQL.
  VerifySchema(client.get(), db(), add_col_test, {"key", "value"});

  // Restart the cluster with DDL rollback disabled.
  SetFlagOnAllProcessesWithRollingRestart("--ysql_ddl_rollback_enabled=false");
  // Verify that rollback did not occur even after the restart.
  client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableExists(client.get(), db(), drop_table_test, 10);
  VerifySchema(client.get(), db(), "foobar", {"key"});
  VerifySchema(client.get(), db(), rename_col_test, {"key2"});
  VerifySchema(client.get(), db(), add_col_test, {"key", "value"});

  // Verify that it is still possible to run DDL on an affected table.
  // Tables having unverified transaction state on them can still be altered if the DDL rollback
  // is not enabled.
  // TODO: Need to start a different connection here for rename to work as expected until #14395
  // is fixed.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(RenameTableStmt(rename_table_test, "foobar2")));
  ASSERT_OK(conn.Execute(AddColumnStmt(add_col_test, "value2")));

  // Re-enable DDL rollback properly with restart.
  SetFlagOnAllProcessesWithRollingRestart("--ysql_ddl_rollback_enabled=true");

  client = ASSERT_RESULT(cluster_->CreateClient());
  conn = ASSERT_RESULT(Connect());
  // The tables with the transaction state on them must have had their state cleared upon restart
  // since the flag is now enabled again.
  ASSERT_OK(conn.Execute(RenameColumnStmt(rename_col_test)));
  ASSERT_OK(conn.Execute(DropTableStmt(drop_table_test)));

  // However add_col_test will be corrupted because we never performed transaction rollback on it.
  // It should ideally have only one added column "value2", but we never rolled back the addition of
  // "value".
  VerifySchema(client.get(), db(), add_col_test, {"key", "value", "value2"});

  // Add a column to add_col_test which is now corrupted.
  ASSERT_OK(conn.Execute(AddColumnStmt(add_col_test, "value3")));
  // Wait for transaction verification to run.
  sleep(2);
  // However since the PG schema and DocDB schema have become out-of-sync, the above DDL cannot
  // be verified. All future DDL operations will now fail.
  ASSERT_NOK(conn.Execute(RenameTableStmt(add_col_test, "foobar3")));
  ASSERT_NOK(conn.Execute(RenameColumnStmt(add_col_test)));
  ASSERT_NOK(conn.Execute(DropTableStmt(add_col_test)));
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
    return s.ToString().find("Table is undergoing DDL transaction verification") != string::npos;
  }

  void testConcurrentDDL(const string& stmt1, const string& stmt2) {
    // Test that until transaction verification is complete, another DDL operating on
    // the same object cannot go through.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute(stmt1));
    // The first DDL was successful, but the second should fail because rollback has
    // been delayed using 'ysql_transaction_bg_task_wait_ms'.
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_TRUE(testFailedDueToTxnVerification(&conn2, stmt2));
  }

  void testConcurrentFailedDDL(const string& stmt1, const string& stmt2) {
    // Same as 'testConcurrentDDL' but here the first DDL statement is a failure. However
    // other DDLs still cannot happen unless rollback is complete.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_NOK(conn.TestFailDdl(stmt1));
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_TRUE(testFailedDueToTxnVerification(&conn2, stmt2));
  }
};

TEST_F(PgDdlAtomicityConcurrentDdlTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentDdl)) {
  const string kCreateAndAlter = "create_and_alter_test";
  const string kCreateAndDrop = "create_and_drop_test";
  const string kDropAndAlter = "drop_and_alter_test";
  const string kAlterAndAlter = "alter_and_alter_test";
  const string kAlterAndDrop = "alter_and_drop_test";

  const vector<string> tables_to_create = {kDropAndAlter, kAlterAndAlter, kAlterAndDrop};

  auto conn = ASSERT_RESULT(Connect());
  for (const auto& table : tables_to_create) {
    ASSERT_OK(conn.Execute(CreateTableStmt(table)));
  }

  // Wait for YsqlDdlTxnVerifierState to be cleaned up from the tables.
  sleep(5);

  // Test that we can't run a second DDL on a table until the YsqlTxnVerifierState on the first
  // table is cleared.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "60000"));
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
  VerifyTableNotExists(client.get(), db(), table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(CreateDropTest)) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          DropTableStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), db(), table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(CreateAlterTest)) {
  string ddl_statements = CreateTableStmt(table()) + "; " +
                          AddColumnStmt(table()) +  "; " +
                          RenameColumnStmt(table()) + "; " +
                          RenameTableStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);
  // Table should not exist.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifyTableNotExists(client.get(), db(), table(), 10);
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(AlterDropTest)) {
  CreateTable(table());
  string ddl_statements = RenameColumnStmt(table()) + "; " + DropTableStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifySchema(client.get(), db(), table(), {"key"});
}

TEST_F(PgDdlAtomicityTxnTest, YB_DISABLE_TEST_IN_TSAN(AddColRenameColTest)) {
  CreateTable(table());
  string ddl_statements = AddColumnStmt(table()) + "; " + RenameColumnStmt(table()) + "; ";
  runFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  VerifySchema(client.get(), db(), table(), {"key"});
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
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "10000"));
  ASSERT_NOK(conn2.TestFailDdl(AddColumnStmt(table)));

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
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    return CheckIfSchemaMatches(client.get(), db(), table, {"key"});
  }, MonoDelta::FromSeconds(30), "Wait for Add Column to be rolled back."));

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
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "5000"));
  ASSERT_NOK(conn3.TestFailDdl(DropTableStmt(table)));

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
  TableName create_table_test = "create_test";
  TableName create_table_idx = "create_test_idx";
  TableName rename_table_test = "rename_table_test";
  TableName rename_col_test = "rename_col_test";
  TableName add_col_test = "add_col_test";

  vector<TableName> tables_to_create = {
    rename_table_test, rename_col_test, add_col_test};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  for (size_t ii = 0; ii < tables_to_create.size(); ++ii) {
    ASSERT_OK(conn_->Execute(CreateTableStmt(tables_to_create[ii])));
  }

  ASSERT_NOK(conn_->TestFailDdl(CreateTableStmt(create_table_test)));
  ASSERT_NOK(conn_->TestFailDdl(CreateIndexStmt(rename_table_test, create_table_idx)));
  ASSERT_NOK(conn_->TestFailDdl(RenameTableStmt(rename_table_test)));
  ASSERT_NOK(conn_->TestFailDdl(RenameColumnStmt(rename_col_test)));
  ASSERT_NOK(conn_->TestFailDdl(AddColumnStmt(add_col_test)));

  // Wait for rollback to complete.
  sleep(5);

  // Create is rolled back using existing transaction GC infrastructure.
  VerifyTableNotExists(client.get(), kDatabaseName, create_table_test, 15);
  VerifyTableNotExists(client.get(), kDatabaseName, create_table_idx, 15);

  // Verify that Alter table transactions are not rolled back because rollback is not enabled yet
  // for certain cases like colocation and tablegroups.
  VerifySchema(client.get(), kDatabaseName, "foobar", {"key"});
  VerifySchema(client.get(), kDatabaseName, rename_col_test, {"key2"});
  VerifySchema(client.get(), kDatabaseName, add_col_test, {"key", "value"});
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

  ASSERT_NOK(conn_->TestFailDdl(Format("DROP TABLE $0, $1",
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
  TableName create_table_test = "create_test";
  TableName add_col_test = "add_col_test";
  TableName drop_table_test = "drop_test";

  vector<TableName> tables_to_create = {add_col_test, drop_table_test};

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  for (size_t ii = 0; ii < tables_to_create.size(); ++ii) {
    ASSERT_OK(conn.Execute(CreateTableStmt(tables_to_create[ii])));
  }

  const int snapshot_interval_secs = 10;
  // Create a snapshot schedule before running all the failed DDLs.
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(db(),
                                   client::WaitSnapshot::kTrue,
                                   MonoDelta::FromSeconds(snapshot_interval_secs)));

  // Run all the failed DDLs.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "35000"));
  ASSERT_NOK(conn.TestFailDdl(CreateTableStmt(create_table_test)));
  ASSERT_NOK(conn.TestFailDdl(AddColumnStmt(add_col_test)));
  ASSERT_NOK(conn.TestFailDdl(DropTableStmt(drop_table_test)));

  // Wait 10s to ensure that a snapshot is taken right after these DDLs failed.
  sleep(snapshot_interval_secs);

  // Get the hybrid time before the rollback can happen.
  Timestamp current_time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime hybrid_time_before_rollback = HybridTime::FromMicros(current_time.ToInt64());

  // Verify that rollback for Alter and Create has indeed not happened yet.
  VerifyTableExists(client.get(), db(), create_table_test, 10);
  VerifyTableExists(client.get(), db(), drop_table_test, 10);
  VerifySchema(client.get(), db(), add_col_test, {"key", "value"});

  // Verify that rollback happens eventually.
  VerifyTableNotExists(client.get(), db(), create_table_test, 60);
  for (const string& table : tables_to_create) {
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      return CheckIfSchemaMatches(client.get(), db(), table, {"key"});
    }, MonoDelta::FromSeconds(60), "Wait for rollback to complete"));
  }

  /*
   TODO (deepthi): Uncomment the following code after #14679 is fixed.
  // Run different failing DDL operations on the tables.
  ASSERT_NOK(conn.TestFailDdl(RenameTableStmt(add_col_test)));
  ASSERT_NOK(conn.TestFailDdl(RenameColumnStmt(drop_table_test)));
  */

  // Restore to before rollback.
  LOG(INFO) << "Start restoration to timestamp " << hybrid_time_before_rollback;
  auto snapshot_id =
    ASSERT_RESULT(snapshot_util_->PickSuitableSnapshot(schedule_id, hybrid_time_before_rollback));
  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, hybrid_time_before_rollback));

  LOG(INFO) << "Restoration complete";

  // We restored to a point before the first rollback occurred. That DDL transaction should have
  // been re-detected to be a failure and rolled back again.
  VerifyTableNotExists(client.get(), db(), create_table_test, 60);
  for (const string& table : tables_to_create) {
    ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
      return CheckIfSchemaMatches(client.get(), db(), table, {"key"});
    }, MonoDelta::FromSeconds(60), "Wait for rollback to complete"));
  }
}

} // namespace pgwrapper
} // namespace yb
