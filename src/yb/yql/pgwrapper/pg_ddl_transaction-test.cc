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
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/client/client-test-util.h"

#include "yb/util/async_util.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pgwrapper {

const std::string kDatabase = "yugabyte";

class PgDdlTransactionTest : public LibPqTestBase {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    opts->extra_master_flags.push_back("--TEST_ysql_yb_ddl_transaction_block_enabled=true");
    opts->extra_tserver_flags.push_back("--ysql_pg_conf_csv=log_statement=all");
    opts->extra_tserver_flags.push_back("--TEST_ysql_yb_ddl_transaction_block_enabled=true");
  }
};

TEST_F(PgDdlTransactionTest, TestTableCreateDropSameTransaction) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "report_ysql_ddl_txn_status_to_master", "false"));

  // Table shouldn't exist in DocDB after the transaction commmit.
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_create_drop_commit(id int)"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_create_drop_commit"));
  ASSERT_OK(conn.Execute("COMMIT"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_create_drop_commit", 10);

  // Table shouldn't exist in DocDB after the transaction rollback.
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
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
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn.Execute("CREATE TABLE txn_create_drop_commit(id int)"));
  ASSERT_OK(conn.Execute("DROP TABLE txn_create_drop_commit"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo(id int)"));
  ASSERT_OK(conn.Execute("COMMIT"));
  VerifyTableNotExists(client.get(), kDatabase, "txn_create_drop_commit", 10);
  ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", "foo"));
  VerifyTableExists(client.get(), kDatabase, "foo", 10);

  // The state of the table 'bar' should be used to determine that the transaction was an abort.
  // As a result, the table 'bar' should not exist in DocDB.
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
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

  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
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
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
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

} // namespace yb::pgwrapper
