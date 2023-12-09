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

using std::string;
using std::vector;

namespace yb {
namespace pgwrapper {

const auto kCreateTable = "create_test"s;
const auto kRenameTable = "rename_table_test"s;
const auto kRenameCol = "rename_col_test"s;
const auto kRenamedTable = "foobar"s;
const auto kRenamedIndex = "foobar_idx"s;
const auto kAddCol = "add_col_test"s;
const auto kDropTable = "drop_test"s;
const auto kDropCol = "drop_col_test"s;

const auto kIndexedTable = "indexed_table_test"s;
const auto kCreateIndex = "create_index_test"s;
const auto kRenameIndex = "rename_index_test"s;
const auto kDropIndex = "drop_index_test"s;

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
    SleepFor(2s);
  }

  virtual string CreateTableStmt(const string& tablename) {
    return "CREATE TABLE " + tablename + " (key INT PRIMARY KEY, value text)";
  }

  string RenameTableStmt(const string& tablename) {
    return RenameTableStmt(tablename, kRenamedTable);
  }

  string RenameTableStmt(const string& tablename, const string& table_new_name) {
    return Format("ALTER TABLE $0 RENAME TO $1", tablename, table_new_name);
    }

  string AddColumnStmt(const string& tablename, const string& col_name_to_add = "value2",
      const string& default_val = "") {
    return default_val.empty() ?
        Format("ALTER TABLE $0 ADD COLUMN $1 TEXT", tablename, col_name_to_add) :
        Format("ALTER TABLE $0 ADD COLUMN $1 TEXT DEFAULT '$2'",
            tablename, col_name_to_add, default_val);
  }

  string DropColumnStmt(const string& tablename) {
    return DropColumnStmt(tablename, "value");
  }

  string DropColumnStmt(const string& tablename, const string& col) {
    return Format("ALTER TABLE $0 DROP COLUMN $1", tablename, col);
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

  string CreateIndexStmt(const string& idxname, const string& tablename) {
    return CreateIndexStmt(idxname, tablename, "value");
  }

  string CreateIndexStmt(const string& idxname, const string& table, const string& col) {
    return Format("CREATE INDEX $0 ON $1($2)", idxname, table, col);
  }

  string RenameIndexStmt(const string& idxname, const string& idx_new_name) {
    return Format("ALTER INDEX $0 RENAME TO $1", idxname, idx_new_name);
  }

  string DropIndexStmt(const string& idxname) {
    return Format("DROP INDEX $0", idxname);
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
      SleepFor(1s);
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
      SleepFor(5s);
    }
  }

  Status VerifySchema(client::YBClient* client,
                    const string& database_name,
                    const string& table_name,
                    const vector<string>& expected_column_names,
                    const std::set<string>& cols_marked_for_deletion = {}) {
    return LoggedWaitFor([&] {
      return CheckIfSchemaMatches(client, database_name, table_name, expected_column_names,
                                  cols_marked_for_deletion);
    }, MonoDelta::FromSeconds(60), "Wait for schema to match");
  }

  Result<bool> CheckIfSchemaMatches(client::YBClient* client,
                                    const string& database_name,
                                    const string& table_name,
                                    const vector<string>& expected_column_names,
                                    const std::set<string>& cols_marked_for_deletion) {
    std::string table_id = VERIFY_RESULT(GetTableIdByTableName(client, database_name, table_name));

    std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
    Synchronizer sync;
    RETURN_NOT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    RETURN_NOT_OK(sync.Wait());

    const auto& columns = table_info->schema.columns();
    if (expected_column_names.size() != columns.size()) {
      LOG(INFO) << "Expected " << expected_column_names.size() << " columns for " << table_name
                << " but found " << columns.size() << " columns";
      return false;
    }

    size_t num_cols_marked_for_deletion = 0;
    for (size_t i = 0; i < expected_column_names.size(); ++i) {
      if (columns[i].name().compare(expected_column_names[i]) != 0) {
        LOG(INFO) << "Expected column " << expected_column_names[i] << " but found "
                  << columns[i].name();
        return false;
      }
      if (columns[i].marked_for_deletion()) {
        ++num_cols_marked_for_deletion;
        if (cols_marked_for_deletion.find(columns[i].name()) == cols_marked_for_deletion.end()) {
          LOG(INFO) << "Column " << columns[i].name() << " is marked for deletion";
          return false;
        }
      }
    }
    if (cols_marked_for_deletion.size() != num_cols_marked_for_deletion) {
      LOG(INFO) << Format("Expected $0 columns marked for deletion for table $1 but found $2 "
                          "columns. Table schema: $3", cols_marked_for_deletion.size(),
                          table_name, num_cols_marked_for_deletion, table_info->schema.ToString());
      return false;
    }
    return true;
  }

  Status SetupTables(PGConn *conn,
                     const vector<TableName>& tables,
                     const vector<TableName>& indexes);
  Status RunAllDdlsWithErrorInjection(PGConn* conn);
  Status RunAllDdls(PGConn* conn);
  Status VerifyAllFailingDdlsNotRolledBack(client::YBClient* client,
                                           const string& database);
  Status VerifyAllFailingDdlsRolledBack(client::YBClient* client,
                                        const string& database);
  Status VerifyAllSuccessfulDdls(client::YBClient* client);
};

Status PgDdlAtomicityTest::SetupTables(PGConn *conn,
                     const vector<TableName>& tables,
                     const vector<TableName>& indexes) {
  for (const auto& table : tables) {
    RETURN_NOT_OK(conn->Execute(CreateTableStmt(table)));
  }
  for (const auto& index : indexes) {
    RETURN_NOT_OK(conn->Execute(CreateIndexStmt(index, kIndexedTable)));
  }
  return Status::OK();
}

Status PgDdlAtomicityTest::RunAllDdlsWithErrorInjection(PGConn* conn) {
  // Create Table.
  RETURN_NOT_OK(conn->TestFailDdl(CreateTableStmt(kCreateTable)));

  // Alter Table.
  RETURN_NOT_OK(conn->TestFailDdl(RenameTableStmt(kRenameTable)));
  RETURN_NOT_OK(conn->TestFailDdl(RenameColumnStmt(kRenameCol)));
  RETURN_NOT_OK(conn->TestFailDdl(AddColumnStmt(kAddCol)));
  RETURN_NOT_OK(conn->TestFailDdl(DropColumnStmt(kDropCol)));

  // Drop Table.
  RETURN_NOT_OK(conn->TestFailDdl(DropTableStmt(kDropTable)));

  // Index.
  RETURN_NOT_OK(conn->TestFailDdl(CreateIndexStmt(kCreateIndex, kIndexedTable)));
  RETURN_NOT_OK(conn->TestFailDdl(RenameIndexStmt(kRenameIndex, kRenamedIndex)));
  RETURN_NOT_OK(conn->TestFailDdl(DropIndexStmt(kDropIndex)));

  return Status::OK();
}

Status PgDdlAtomicityTest::VerifyAllFailingDdlsNotRolledBack(client::YBClient* client,
                                                             const string& database) {
  VerifyTableExists(client, database, kCreateTable, 10);
  VerifyTableExists(client, database, kCreateIndex, 10);
  VerifyTableExists(client, database, kRenamedIndex, 10);
  // DROP does not reflect until the transaction commits, so the tables still exist even though
  // no rollback actually happened.
  VerifyTableExists(client, database, kDropTable, 10);
  VerifyTableExists(client, database, kDropIndex, 10);

  // Verify that the tables were altered on DocDB.
  RETURN_NOT_OK(VerifySchema(client, database, kRenamedTable, {"key", "value"}));
  RETURN_NOT_OK(VerifySchema(client, database, kRenameCol, {"key2", "value"}));
  RETURN_NOT_OK(VerifySchema(client, database, kAddCol, {"key", "value", "value2"}));
  RETURN_NOT_OK(VerifySchema(client, database, kDropCol, {"key", "value"}, {"value"}));

  return Status::OK();
}

Status PgDdlAtomicityTest::VerifyAllFailingDdlsRolledBack(client::YBClient* client,
                                                          const string& database) {
  VerifyTableNotExists(client, database, kCreateTable, 20);
  VerifyTableNotExists(client, database, kCreateIndex, 20);
  VerifyTableExists(client, database, kDropTable, 20);
  VerifyTableExists(client, database, kRenameTable, 20);
  VerifyTableExists(client, database, kRenameIndex, 20);
  VerifyTableExists(client, database, kDropIndex, 20);

  static const auto tables = {kRenameCol, kAddCol, kDropCol};
  for (const string& table : tables) {
    RETURN_NOT_OK(VerifySchema(client, database, table, {"key", "value"}));
  }
  return Status::OK();
}

Status PgDdlAtomicityTest::RunAllDdls(PGConn* conn) {
  // Create Table.
  RETURN_NOT_OK(conn->Execute(CreateTableStmt(kCreateTable)));

  // Alter Table.
  RETURN_NOT_OK(conn->Execute(RenameTableStmt(kRenameTable)));
  RETURN_NOT_OK(conn->Execute(RenameColumnStmt(kRenameCol)));
  RETURN_NOT_OK(conn->Execute(AddColumnStmt(kAddCol)));
  RETURN_NOT_OK(conn->Execute(DropColumnStmt(kDropCol)));

  // Drop Table.
  RETURN_NOT_OK(conn->Execute(DropTableStmt(kDropTable)));

  // Create Index.
  RETURN_NOT_OK(conn->Execute(CreateIndexStmt(kCreateIndex, kIndexedTable)));

  // Alter Index.
  RETURN_NOT_OK(conn->Execute(RenameIndexStmt(kRenameIndex, kRenamedIndex)));

  // Drop Index.
  RETURN_NOT_OK(conn->Execute(DropIndexStmt(kDropIndex)));
  return Status::OK();
}

Status PgDdlAtomicityTest::VerifyAllSuccessfulDdls(client::YBClient* client) {
  VerifyTableExists(client, kDatabase, kCreateTable, 10);
  VerifyTableExists(client, kDatabase, kCreateIndex, 10);
  VerifyTableExists(client, kDatabase, kRenamedIndex, 10);

  VerifyTableNotExists(client, kDatabase, kDropTable, 10);
  VerifyTableNotExists(client, kDatabase, kDropIndex, 10);

  // Verify that the tables were altered on DocDB.
  RETURN_NOT_OK(VerifySchema(client, kDatabase, kRenamedTable, {"key", "value"}));
  RETURN_NOT_OK(VerifySchema(client, kDatabase, kRenameCol, {"key2", "value"}));
  RETURN_NOT_OK(VerifySchema(client, kDatabase, kAddCol, {"key", "value", "value2"}));
  RETURN_NOT_OK(VerifySchema(client, kDatabase, kDropCol, {"key"}));

  return Status::OK();
}

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
    options->extra_tserver_flags.push_back(
      "--allowed_preview_flags_csv=ysql_ddl_rollback_enabled");
    options->extra_tserver_flags.push_back("--ysql_ddl_rollback_enabled=true");
    options->extra_tserver_flags.push_back("--report_ysql_ddl_txn_status_to_master=true");
  }
};

TEST_F(PgDdlAtomicitySanityTest, BasicTest) {
  const auto tables = {kRenameTable, kRenameCol, kAddCol, kDropCol, kDropTable, kIndexedTable};
  const auto indexes = {kRenameIndex, kDropIndex};

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(SetupTables(&conn, tables, indexes));

  ASSERT_OK(RunAllDdlsWithErrorInjection(&conn));

  // Wait for rollback to happen.
  SleepFor(5s);

  // Verify that all the above operations are rolled back.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifyAllFailingDdlsRolledBack(client.get(), kDatabase));
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

TEST_F(PgDdlAtomicitySanityTest, PrimaryKeyRollback) {
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
  VerifyTableExists(client.get(), kDatabase, add_pk_table + "_temp_old", 10);
  VerifyTableExists(client.get(), kDatabase, drop_pk_table + "_temp_old", 10);

  // Verify that the background task detected the failure of the DDL operation and removed the
  // temp table.
  VerifyTableNotExists(client.get(), kDatabase, add_pk_table + "_temp_old", 40);
  VerifyTableNotExists(client.get(), kDatabase, drop_pk_table + "_temp_old", 40);

  // Verify that PK constraint is not present on the table.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1), (1)", add_pk_table));

  // Verify that PK constraint is still present on the table.
  ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1), (1)", drop_pk_table));
}

TEST_F(PgDdlAtomicitySanityTest, DdlRollbackMasterRestart) {
  const auto tables = {kRenameTable, kRenameCol, kAddCol, kDropCol, kDropTable, kIndexedTable};
  const auto indexes = {kRenameIndex, kDropIndex};

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(SetupTables(&conn, tables, indexes));

  // Set TEST_delay_ysql_ddl_rollback_secs so high that table rollback is nearly
  // disabled.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "100"));
  ASSERT_OK(RunAllDdlsWithErrorInjection(&conn));

  // Verify that for now all the incomplete DDLs have taken effect.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifyAllFailingDdlsNotRolledBack(client.get(), kDatabase));

  // Restart the master before the BG task can kick in and GC the failed transaction.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "0"));
  RestartMaster();

  // Verify that rollback reflected on all the tables after restart.
  client = ASSERT_RESULT(cluster_->CreateClient()); // Reinit the YBClient after restart.

  ASSERT_OK(VerifyAllFailingDdlsRolledBack(client.get(), kDatabase));
}

TEST_F(PgDdlAtomicitySanityTest, TestYsqlTxnStatusReporting) {
  const auto tables = {kRenameTable, kRenameCol, kAddCol, kDropCol, kDropTable, kIndexedTable};
  const auto indexes = {kRenameIndex, kDropIndex};
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(SetupTables(&conn, tables, indexes));

  // Disable YB-Master's background task.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_disable_ysql_ddl_txn_verification", "true"));
  // Run some failing Ddl transactions
  ASSERT_OK(RunAllDdlsWithErrorInjection(&conn));

  // The rollback should have succeeded anyway because YSQL would have reported the
  // status.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifyAllFailingDdlsRolledBack(client.get(), kDatabase));

  // Now test successful DDLs.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(RunAllDdls(&conn));

  // Verify that these DDLs were not rolled back.
  ASSERT_OK(VerifyAllSuccessfulDdls(client.get()));
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
  expected_cols.emplace_back("value");
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
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key", "value"}));
}

TEST_F(PgDdlAtomicityTxnTest, AddColRenameColTest) {
  CreateTable(table());
  string ddl_statements = AddColumnStmt(table()) + "; " + RenameColumnStmt(table()) + "; ";
  RunFailingDdlTransaction(ddl_statements);

  // Table should exist with old schema intact.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key", "value"}));
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
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table(), {"key", "value"}));
  PGResultPtr res = ASSERT_RESULT(conn.FetchFormat("SELECT value FROM $0", table()));
  ASSERT_EQ(PQntuples(res.get()), 5);
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
  ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key", "value"}));

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
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "10"));
  ASSERT_OK(conn3.TestFailDdl(DropTableStmt(table)));

  // Conn1: This should succeed.
  ASSERT_OK(conn1.Execute("INSERT INTO " + table + " VALUES (3)"));
  ASSERT_OK(conn1.Execute("COMMIT"));

  // Wait for rollback to complete.
  SleepFor(5s);

  // Conn2 must also commit successfully.
  ASSERT_OK(conn2.Execute("INSERT INTO " + table + " VALUES (4)"));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

// Test that we are able to rollback DROP COLUMN on a column with a missing
// default value.
TEST_F(PgDdlAtomicitySanityTest, DropColumnWithMissingDefaultTest) {
  const string& table = "drop_column_missing_default_test";
  CreateTable(table);
  auto conn = ASSERT_RESULT(Connect());
  // Write some rows to the table.
  ASSERT_OK(conn.Execute("INSERT INTO " + table + "(key) VALUES (generate_series(1, 3))"));
  // Add column with missing default value.
  ASSERT_OK(conn.Execute(Format(AddColumnStmt(table, "value2", "default"))));
  // Insert rows with the new column set to null.
  ASSERT_OK(conn.Execute(
      "INSERT INTO " + table + "(key, value2) VALUES (generate_series(4, 6), null)"));
  // Insert rows with the new column set to a non-default value.
  ASSERT_OK(conn.Execute(
      "INSERT INTO " + table + "(key, value2) VALUES (generate_series(7, 9), 'not default')"));
  auto res = ASSERT_RESULT(conn.FetchFormat("SELECT key, value2 FROM $0 ORDER BY key", table));
  // Verify data.
  auto check_result = [&res] {
    for (int i = 1; i <= 9; ++i) {
      const auto value1 = ASSERT_RESULT(GetInt32(res.get(), i - 1, 0));
      ASSERT_EQ(value1, i);
      const auto value2 = ASSERT_RESULT(GetString(res.get(), i - 1, 1));
      if (i <= 3) {
        ASSERT_EQ(value2, "default");
      } else if (i <= 6) {
        ASSERT_EQ(value2, "");
      } else {
        ASSERT_EQ(value2, "not default");
      }
    }
  };
  check_result();
  // Fail drop column.
  ASSERT_OK(conn.TestFailDdl(DropColumnStmt(table)));
  // Insert more rows.
  ASSERT_OK(conn.Execute(
      "INSERT INTO " + table + "(key) VALUES (generate_series(10, 12))"));
  res = ASSERT_RESULT(conn.FetchFormat("SELECT key, value2 FROM $0 ORDER BY key", table));
  check_result();
}

class PgDdlAtomicityColocatedTestBase : public PgDdlAtomicitySanityTest {
 protected:
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  // Test function.
  void sanityTest();

  std::unique_ptr<PGConn> conn_;
  string kDatabaseName = "yugabyte";
};

void PgDdlAtomicityColocatedTestBase::sanityTest() {
  const auto tables = {kRenameTable, kRenameCol, kAddCol, kDropCol, kDropTable, kIndexedTable};
  const auto indexes = {kRenameIndex, kDropIndex};
  ASSERT_OK(SetupTables(conn_.get(), tables, indexes));

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Insert some data into kDropTable.
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1), (2), (3)", kDropTable));
  ASSERT_OK(RunAllDdlsWithErrorInjection(conn_.get()));

  // Wait for rollback to happen.
  SleepFor(5s);

  // Verify that all the above operations are rolled back.
  ASSERT_OK(VerifyAllFailingDdlsRolledBack(client.get(), kDatabaseName));

  // Verify that the data is still present in the dropped table.
  auto curr_rows = ASSERT_RESULT(
    conn_->FetchValue<int64_t>(Format("SELECT COUNT(*) FROM $0", kDropTable)));
  ASSERT_EQ(curr_rows, 3);
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

class PgDdlAtomiPgDdlAtomicityTablegroupTest : public PgDdlAtomicityColocatedTestBase {
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(Connect()));
    ASSERT_OK(conn_->ExecuteFormat("CREATE TABLEGROUP $0", kTablegroup));
  }

  string CreateTableStmt(const string& tablename) override {
    return (PgDdlAtomicitySanityTest::CreateTableStmt(tablename) + " TABLEGROUP " + kTablegroup);
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

TEST_F(PgDdlAtomiPgDdlAtomicityTablegroupTest, TablegroupTableTest) {
  sanityTest();
}

TEST_F(PgDdlAtomiPgDdlAtomicityTablegroupTest, TablegroupTest) {
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

TEST_F(PgDdlAtomicitySnapshotTest, SnapshotTest) {
  // Create requisite tables.
  const auto tables_to_create = {kAddCol, kDropTable, kDropCol};

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
  ASSERT_OK(conn.TestFailDdl(DropColumnStmt(kDropCol)));

  // Wait 10s to ensure that a snapshot is taken right after these DDLs failed.
  SleepFor(snapshot_interval_secs * 1s);

  // Get the hybrid time before the rollback can happen.
  Timestamp current_time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime hybrid_time_before_rollback = HybridTime::FromMicros(current_time.ToInt64());

  // Verify that rollback for Alter and Create has indeed not happened yet.
  VerifyTableExists(client.get(), kDatabase, kCreateTable, 10);
  VerifyTableExists(client.get(), kDatabase, kDropTable, 10);
  ASSERT_OK(VerifySchema(client.get(), kDatabase, kAddCol, {"key", "value", "value2"}));

  // Verify that rollback indeed happened.
  VerifyTableNotExists(client.get(), kDatabase, kCreateTable, 60);

  // Verify all the other tables are unchanged by all of the DDLs.
  for (const auto& table : tables_to_create) {
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key", "value"}));
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
    ASSERT_OK(VerifySchema(client.get(), kDatabase, table, {"key", "value"}));
  }
}

TEST_F(PgDdlAtomicitySanityTest, MatviewTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t(id int)"));

  // Test matview creation failure.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(conn.TestFailDdl(Format("CREATE MATERIALIZED VIEW mv AS SELECT * FROM t")));

  // Verify matview creation is rolled back.
  VerifyTableNotExists(client.get(), kDatabase, "mv", 10);

  // Insert some values into the table.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE MATERIALIZED VIEW mv AS SELECT * FROM t"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1), (2)"));

  // Test failed refresh matview.
  ASSERT_OK(conn.TestFailDdl(Format("REFRESH MATERIALIZED VIEW mv")));
  // Wait for rollback to complete.
  SleepFor(2s);
  auto curr_rows = ASSERT_RESULT(conn.FetchValue<int64_t>("SELECT COUNT(*) FROM mv"));
  ASSERT_EQ(curr_rows, 0);
}

class PgLibPqMatviewFailure: public PgDdlAtomicitySanityTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
      "--allowed_preview_flags_csv=ysql_ddl_rollback_enabled");
    options->extra_tserver_flags.push_back(
        Format("--TEST_yb_test_fail_matview_refresh_after_creation=true"));
  }
 protected:
  void RefreshMatviewRollback();
};


void PgLibPqMatviewFailure::RefreshMatviewRollback() {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t(id int)"));
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Now test failure of Matview refresh.
  ASSERT_OK(conn.ExecuteFormat("CREATE MATERIALIZED VIEW mv AS SELECT * FROM t"));
  // Wait for DDL verification to complete for the above statement.
  SleepFor(4s);
  auto matview_oid = ASSERT_RESULT(conn.FetchValue<PGOid>(
      "SELECT oid FROM pg_class WHERE relname = 'mv'"));
  auto pg_temp_table_name = "pg_temp_" + std::to_string(matview_oid);
  // The following statement fails because we set 'yb_test_fail_matview_refresh_after_creation'.
  ASSERT_NOK(conn.Execute("REFRESH MATERIALIZED VIEW mv"));

  // Wait for DocDB Table (materialized view) creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  VerifyTableExists(client.get(), kDatabase, pg_temp_table_name, 20);

  // This temp table is later cleaned up once the transaction failure is detected.
  VerifyTableNotExists(client.get(), kDatabase, pg_temp_table_name, 20);
}

// Test that an orphaned table left after a failed refresh on a materialized view is cleaned up
// by transaction GC.
// TODO(deepthi): Remove this test after 'ysql_ddl_rollback_enabled' is set to true by default.
// TODO(deepthi): Re-enable both these tests in TSAN after #16055 is fixed.
TEST_F_EX(PgDdlAtomicitySanityTest,
          YB_DISABLE_TEST_IN_TSAN(TestMatviewRollbackWithTxnGC),
          PgLibPqMatviewFailure) {
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "10000"));
  // Disable the current version of DDL rollback so that we can test the transaction GC framework.
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_ddl_rollback_enabled", "false"));
  RefreshMatviewRollback();
}

// Test that the DocDB tables created upon Matview creation and refresh are rolled-back
// by the DDL Atomicity framework.
TEST_F_EX(PgDdlAtomicitySanityTest,
          YB_DISABLE_TEST_IN_TSAN(TestMatviewRollbackWithDdlAtomicity),
          PgLibPqMatviewFailure) {
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_ysql_ddl_rollback_secs", "10"));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_ddl_rollback_enabled", "true"));
  RefreshMatviewRollback();
}

} // namespace pgwrapper
} // namespace yb
