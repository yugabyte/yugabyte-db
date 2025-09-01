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

#include "yb/client/table_info.h"

#include "yb/client/client-test-util.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/util/async_util.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_ysql_yb_enable_ddl_savepoint_support);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(yb_enable_read_committed_isolation);

namespace yb::pgwrapper {

const std::string kDatabase = "yugabyte";
const auto kTableName = "test";
const auto kTableName2 = "test2";

class PgDdlTransactionTest : public LibPqTestBase {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    opts->extra_master_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=true");
    opts->extra_master_flags.push_back("--yb_enable_read_committed_isolation=true");
    opts->extra_master_flags.push_back(
        "--allowed_preview_flags_csv=ysql_yb_ddl_transaction_block_enabled");
    opts->extra_tserver_flags.push_back("--ysql_pg_conf_csv=log_statement=all");
    opts->extra_tserver_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=true");
    opts->extra_tserver_flags.push_back("--yb_enable_read_committed_isolation=true");
    opts->extra_tserver_flags.push_back(
        "--allowed_preview_flags_csv=ysql_yb_ddl_transaction_block_enabled");
  }

  // ysql_yb_disable_ddl_transaction_block_for_read_committed is a non-runtime flag for now, so we
  // need to restart the cluster.
  void RestartClusterSetDisableTxnBlockForReadCommitted(bool value) {
    LOG(INFO) << "Restart the cluster and turn " << (value ? "on" : "off")
              << " --ysql_yb_disable_ddl_transaction_block_for_read_committed";
    cluster_->Shutdown();
    const std::string flag_value = Format(
        "--ysql_yb_disable_ddl_transaction_block_for_read_committed=$0",
        value ? "true" : "false");
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->tablet_server(i)->mutable_flags()->push_back(flag_value);
    }
    ASSERT_OK(cluster_->Restart());
  }
};

TEST_F(PgDdlTransactionTest, TestTableCreateDropSameTransaction) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "report_ysql_ddl_txn_status_to_master", "false"));

  // Table shouldn't exist in DocDB after the transaction commmit.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_create_drop_commit(id int)"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_create_drop_commit"));
  ASSERT_OK(conn.Execute("COMMIT"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_create_drop_commit", 10);

  // Table shouldn't exist in DocDB after the transaction rollback.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_create_drop_rollback(id int)"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_create_drop_rollback"));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_create_drop_rollback", 10);
}

TEST_F(PgDdlTransactionTest, TestTableCreateDropSameTransactionAnotherTableUsedForDisambiguation) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "report_ysql_ddl_txn_status_to_master", "false"));

  // The state of the table 'foo' should be used to determine that the transaction was a success.
  // As a result, the table 'foo' should exist in DocDB.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_create_drop_commit(id int)"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_create_drop_commit"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo(id int)"));
  ASSERT_OK(conn.Execute("COMMIT"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_create_drop_commit", 10);
  ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", "foo"));
  VerifyTableExists(client.get(), kDatabase, "foo", 10);

  // The state of the table 'bar' should be used to determine that the transaction was an abort.
  // As a result, the table 'bar' should not exist in DocDB.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_create_drop_rollback(id int)"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_create_drop_rollback"));
  ASSERT_OK(conn.Execute("CREATE TABLE bar(id int)"));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_create_drop_rollback", 10);
  VerifyTableNotExists(client.get(), kDatabase, "bar", 10);
}

TEST_F(PgDdlTransactionTest, TestTableDropCommit) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "report_ysql_ddl_txn_status_to_master", "false"));

  ASSERT_OK(conn.Execute("CREATE TABLE txn_drop_existing(id int)"));
  ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", "txn_drop_existing"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_drop_existing"));
  ASSERT_OK(conn.Execute("COMMIT"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_drop_existing", 10);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/27359.
TEST_F(PgDdlTransactionTest, TestRewriteAndDropMaterializedViewInTxn) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "report_ysql_ddl_txn_status_to_master", "false"));

  ASSERT_OK(conn.Execute("CREATE TABLE transactions (id SERIAL PRIMARY KEY, amount INT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo (id SERIAL PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO transactions (amount) VALUES (10), (20), (30)"));
  ASSERT_OK(conn.Execute(
      "CREATE MATERIALIZED VIEW sales_summary AS "
      "SELECT COUNT(*) AS transaction_count, SUM(amount) AS total_amount FROM transactions"));
  auto row = ASSERT_RESULT((conn.FetchRow<int64_t, int64_t>("SELECT * FROM sales_summary")));
  std::tuple<int64_t, int64_t> expected_row = {3, 60}; // 3 rows, total amount 10+20+30
  ASSERT_EQ(row, expected_row);

  ASSERT_OK(conn.Execute("INSERT INTO transactions (amount) VALUES (40)"));

  // Rewrite and drop the materialized view in a transaction. We are using the table 'foo' to
  // detect whether the transaction was determined to be a success or an abort.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("REFRESH MATERIALIZED VIEW sales_summary"));
  ASSERT_OK(conn.Execute("DROP MATERIALIZED VIEW sales_summary"));
  ASSERT_OK(conn.Execute("ALTER TABLE foo ADD COLUMN new_col INT"));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // The transaction must have been deemed as an abort. This can be detected by the absence of the
  // column 'new_col' in the table 'foo'.
  auto foo_table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabase, "foo"));
  std::shared_ptr<client::YBTableInfo> foo_table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  ASSERT_OK(client->GetTableSchemaById(foo_table_id, foo_table_info, sync.AsStatusCallback()));
  ASSERT_OK(sync.Wait());
  const auto& columns = foo_table_info->schema.columns();
  ASSERT_EQ(columns.size(), 1);
  ASSERT_EQ(columns[0].name(), "id");

  // View should still exist and show the old data as the refresh was rolled back.
  row = ASSERT_RESULT((conn.FetchRow<int64_t, int64_t>("SELECT * FROM sales_summary")));
  expected_row = {3, 60};
  ASSERT_EQ(row, expected_row);
}

TEST_F(PgDdlTransactionTest, TestReadCommittedTxnDdlDisabled) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL READ COMMITTED"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo (id SERIAL PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("ALTER TABLE foo ADD COLUMN new_col INT"));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  auto res = GetTableIdByTableName(client.get(), "yugabyte", "foo");
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsNotFound());

  // Disable transactional DDL for READ COMMITTED isolation level. This will make DDLs use
  // autonomous transactions, which are not rolled back by the enclosing transaction block.
  RestartClusterSetDisableTxnBlockForReadCommitted(true /* value */);
  conn = ASSERT_RESULT(Connect());
  client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL READ COMMITTED"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo (id SERIAL PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("ALTER TABLE foo ADD COLUMN new_col INT"));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // The table 'foo' should exist in DocDB after the transaction rollback, as the DDLs were executed
  // in autonomous transactions.
  // The column 'new_col' should also exist in the table 'foo'.
  ASSERT_OK(GetTableIdByTableName(client.get(), "yugabyte", "foo"));
  ASSERT_OK(conn.Execute("INSERT INTO foo (id, new_col) VALUES (1, 42)"));
}

YB_STRONGLY_TYPED_BOOL(TestCommit);

class PgDdlSavepointMiniClusterTest : public PgMiniTestBase,
                                      public ::testing::WithParamInterface<TestCommit> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_yb_enable_ddl_savepoint_support) = true;

    // Disable READ_COMMITTED isolation because it creates additional savepoints (sub_transactions)
    // for a statement that requires special handling when asserting the size of
    // ysql_ddl_txn_verifier_state.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = false;
    pgwrapper::PgMiniTestBase::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(bool, PgDdlSavepointMiniClusterTest,
    ::testing::Values(TestCommit::kFalse, TestCommit::kTrue));

TEST_P(PgDdlSavepointMiniClusterTest, TestTransactionStateMultipleSubTransactions) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName));
  auto table_info = catalog_manager.GetTableInfo(table_id);
  ASSERT_EQ(table_info->LockForRead()->ysql_ddl_txn_verifier_state().size(), 1);

  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN b TEXT", kTableName));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c TEXT", kTableName));
  ASSERT_EQ(table_info->LockForRead()->ysql_ddl_txn_verifier_state().size(), 2);

  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kTableName));

  ASSERT_OK(conn.Execute("SAVEPOINT c"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName2));
  auto new_table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName2));
  auto new_table_info = catalog_manager.GetTableInfo(new_table_id);

  // 'table' must have 3 ddl verification states due to 2 savepoints
  auto table_verifier_states = table_info->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 3);
  ASSERT_TRUE(table_verifier_states[0].contains_create_table_op());
  ASSERT_TRUE(table_verifier_states[1].contains_alter_table_op());
  ASSERT_TRUE(table_verifier_states[2].contains_drop_table_op());
  ASSERT_TRUE(table_info->LockForRead()->is_being_created_by_ysql_ddl_txn());
  ASSERT_TRUE(table_info->LockForRead()->is_being_altered_by_ysql_ddl_txn());
  ASSERT_TRUE(table_info->LockForRead()->is_being_deleted_by_ysql_ddl_txn());

  // 'new_table' must have only 1 ddl verification state
  auto new_table_verifier_states = new_table_info->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(new_table_verifier_states.size(), 1);
  ASSERT_TRUE(new_table_verifier_states[0].contains_create_table_op());
  ASSERT_TRUE(new_table_info->LockForRead()->is_being_created_by_ysql_ddl_txn());
  ASSERT_FALSE(new_table_info->LockForRead()->is_being_altered_by_ysql_ddl_txn());
  ASSERT_FALSE(new_table_info->LockForRead()->is_being_deleted_by_ysql_ddl_txn());

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
    ASSERT_FALSE(table_info->LockForRead()->has_ysql_ddl_txn_verifier_state());
    ASSERT_FALSE(new_table_info->LockForRead()->has_ysql_ddl_txn_verifier_state());
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
}

// Test multiple sub-transactions with DDLs that roll forward such as Drop Column and Drop Table.
TEST_P(PgDdlSavepointMiniClusterTest, TestTransactionStateRollForwards) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (a int, b text, c text, d text, e text, f text)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName2));
  ASSERT_OK(conn.Execute("BEGIN"));

  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN b", kTableName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kTableName2));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN c", kTableName));

  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName));
  auto table = catalog_mgr.GetTableInfo(table_id);
  auto table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 2);
  ASSERT_TRUE(table_verifier_states[0].contains_alter_table_op());
  ASSERT_TRUE(table_verifier_states[1].contains_alter_table_op());

  auto table_id_2 = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName2));
  auto table_2 = catalog_mgr.GetTableInfo(table_id_2);
  auto table_2_verifier_states = table_2->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_2_verifier_states.size(), 1);
  ASSERT_TRUE(table_2_verifier_states[0].contains_drop_table_op());

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
  ASSERT_FALSE(table_2->LockForRead()->has_ysql_ddl_txn_verifier_state());
}

} // namespace yb::pgwrapper
