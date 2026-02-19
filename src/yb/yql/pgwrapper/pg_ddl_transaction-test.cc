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

#include "yb/client/table_info.h"

#include "yb/client/client-test-util.h"

#include "yb/common/ql_type.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/sync_point.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_string(allowed_preview_flags_csv);
DECLARE_bool(ysql_yb_enable_ddl_savepoint_support);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(yb_enable_read_committed_isolation);

namespace yb::pgwrapper {

const std::string kDatabase = "yugabyte";
const auto kTableName = "test";
const auto kTableName2 = "test2";

YB_STRONGLY_TYPED_BOOL(MarkedForDeletion);
YB_STRONGLY_TYPED_BOOL(TestCommit);

enum class DdlOp : uint8_t {
  kNone = 0,
  kCreate = 1 << 0,
  kAlter = 1 << 1,
  kDrop = 1 << 2,
};

inline DdlOp operator|(DdlOp a, DdlOp b) {
  return static_cast<DdlOp>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline DdlOp operator&(DdlOp a, DdlOp b) {
  return static_cast<DdlOp>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline void VerifyDdlOps(const yb::master::YsqlDdlTxnVerifierStatePB& state, DdlOp expected_ops) {
  if ((expected_ops & DdlOp::kCreate) == DdlOp::kCreate) {
    ASSERT_TRUE(state.contains_create_table_op());
  } else {
    ASSERT_FALSE(state.contains_create_table_op());
  }

  if ((expected_ops & DdlOp::kAlter) == DdlOp::kAlter) {
    ASSERT_TRUE(state.contains_alter_table_op());
  } else {
    ASSERT_FALSE(state.contains_alter_table_op());
  }

  if ((expected_ops & DdlOp::kDrop) == DdlOp::kDrop) {
    ASSERT_TRUE(state.contains_drop_table_op());
  } else {
    ASSERT_FALSE(state.contains_drop_table_op());
  }
}

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

class PgDdlSavepointMiniClusterTest : public PgMiniTestBase,
                                      public ::testing::WithParamInterface<TestCommit> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allowed_preview_flags_csv) =
        "ysql_yb_enable_ddl_savepoint_support";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_ddl_savepoint_support) = true;

    // Disable READ_COMMITTED isolation because it creates additional savepoints (sub_transactions)
    // for a statement that requires special handling when asserting the size of
    // ysql_ddl_txn_verifier_state.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = false;
    pgwrapper::PgMiniTestBase::SetUp();
  }

  struct PersistedColumnInformation {
    std::string name;
    PersistentDataType persistent_data_type;
    MarkedForDeletion marked_for_deletion;
  };

  void ValidateSchemaColumns(
      const SchemaPB& schema,
      const std::vector<PersistedColumnInformation>& expected_column_informations) {
    std::unordered_map<std::string, const ColumnSchemaPB*> schema_columns;
    for (const auto& column : schema.columns()) {
      schema_columns[column.name()] = &column;
    }

    EXPECT_EQ(schema_columns.size(), expected_column_informations.size());
    for (const auto& [column_name, expected_column_type, expected_marked_for_deletion] :
         expected_column_informations) {
      SCOPED_TRACE(Format("column_name: $0", column_name));
      auto column_info = *ASSERT_NOTNULL(FindOrNull(schema_columns, column_name));
      EXPECT_EQ(column_info->type().main(), expected_column_type);
      EXPECT_EQ(
          MarkedForDeletion(column_info->marked_for_deletion()), expected_marked_for_deletion);
    }
  }

  struct ColumnInformation {
    std::string name;
    DataType data_type;
  };

  void ValidateTabletSchema(
      const TableId table_id, const std::vector<ColumnInformation>& expected_column_informations) {
    auto tablet_peers = ListTableTabletPeers(cluster_.get(), table_id, ListPeersFilter::kLeaders);

    for (const auto& peer : tablet_peers) {
      auto tablet = peer->shared_tablet_maybe_null();
      if (!tablet) {
        continue;
      }

      auto table_schema = ASSERT_RESULT(tablet->metadata()->GetTableInfo(table_id))->schema();
      std::unordered_map<std::string, const ColumnSchema*> schema_columns;
      for (const auto& column : table_schema.columns()) {
        schema_columns[column.name()] = &column;
      }

      EXPECT_EQ(schema_columns.size(), expected_column_informations.size());
      for (const auto& [column_name, expected_column_type] : expected_column_informations) {
        SCOPED_TRACE(Format("column_name: $0", column_name));
        auto column_info = *ASSERT_NOTNULL(FindOrNull(schema_columns, column_name));
        EXPECT_EQ(column_info->type()->main(), expected_column_type);
      }
    }
  }

  Status WaitForTableDeletionToFinish(client::YBClient* client, const std::string& table_id) {
    return LoggedWaitFor([&]() -> Result<bool> {
      bool table_found = false;
      const auto tables = VERIFY_RESULT(client->ListTables());
      for (const auto& t : tables) {
        if (t.namespace_name() == "yugabyte" && t.table_id() == table_id) {
          table_found = true;
          break;
        }
      }
      return !table_found;
    }, MonoDelta::FromSeconds(60), "Wait for Table deletion to finish for table " + table_id);
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

  // 'table' must have 3 ddl verification states due to the 2 savepoints 'a' and 'b'. Savepoint 'c'
  // is not relevant here since it is after the DROP TABLE. The CREATE TABLE post that creates a new
  // DocDB table with a different table_id.
  auto table_verifier_states = table_info->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 3);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  VerifyDdlOps(table_verifier_states[1], DdlOp::kAlter);
  VerifyDdlOps(table_verifier_states[2], DdlOp::kDrop);
  ASSERT_TRUE(table_info->LockForRead()->is_being_created_by_ysql_ddl_txn());
  ASSERT_TRUE(table_info->LockForRead()->is_being_altered_by_ysql_ddl_txn());
  ASSERT_TRUE(table_info->LockForRead()->is_being_deleted_by_ysql_ddl_txn());
  ValidateSchemaColumns(
      table_verifier_states[1].previous_schema(),
      {{.name = "k",
        .persistent_data_type = PersistentDataType::INT32,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "v",
        .persistent_data_type = PersistentDataType::INT32,
        .marked_for_deletion = MarkedForDeletion::kFalse}});

  // 'new_table' must have only 1 ddl verification state
  auto new_table_verifier_states = new_table_info->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(new_table_verifier_states.size(), 1);
  VerifyDdlOps(new_table_verifier_states[0], DdlOp::kCreate);
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
      "CREATE TABLE $0 (a int primary key, b text, c text, d text, e text, f text)", kTableName));
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
  VerifyDdlOps(table_verifier_states[0], DdlOp::kAlter);
  // All columns from 'a' to 'f' exist including 'b'.
  ValidateSchemaColumns(
      table_verifier_states[0].previous_schema(),
      {{.name = "a",
        .persistent_data_type = PersistentDataType::INT32,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "b",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "c",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "d",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "e",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "f",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse}});
  VerifyDdlOps(table_verifier_states[1], DdlOp::kAlter);
  // 'b' must be marked for deletion due to the DROP COLUMN operation.
  ValidateSchemaColumns(
      table_verifier_states[1].previous_schema(),
      {{.name = "a",
        .persistent_data_type = PersistentDataType::INT32,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "b",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kTrue},
       {.name = "c",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "d",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "e",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "f",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse}});

  auto table_id_2 = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName2));
  auto table_2 = catalog_mgr.GetTableInfo(table_id_2);
  auto table_2_verifier_states = table_2->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_2_verifier_states.size(), 1);
  VerifyDdlOps(table_2_verifier_states[0], DdlOp::kDrop);

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
  ASSERT_FALSE(table_2->LockForRead()->has_ysql_ddl_txn_verifier_state());
}

TEST_P(PgDdlSavepointMiniClusterTest, TestRollbackToSavepoints) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (a int primary key, b text, c text, d text, e text, f text)", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN g TEXT", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN b", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT c"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kTableName));

  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT c"));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName));
  // Column 'b' won't exist as it has been dropped before the savepoint 'c'.
  ValidateTabletSchema(
      table_id, {{.name = "a", .data_type = DataType::INT32},
                 {.name = "c", .data_type = DataType::STRING},
                 {.name = "d", .data_type = DataType::STRING},
                 {.name = "e", .data_type = DataType::STRING},
                 {.name = "f", .data_type = DataType::STRING},
                 {.name = "g", .data_type = DataType::STRING}});

  auto table = catalog_mgr.GetTableInfo(table_id);
  auto table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 3);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  VerifyDdlOps(table_verifier_states[1], DdlOp::kAlter);
  // Column 'g' won't exist in the previous_schema since it was created as part of savepoint 'b'.
  ValidateSchemaColumns(
      table_verifier_states[1].previous_schema(),
      {{.name = "a",
        .persistent_data_type = PersistentDataType::INT32,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "b",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "c",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "d",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "e",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "f",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse}});
  VerifyDdlOps(table_verifier_states[2], DdlOp::kAlter);
  // Column 'g' exists now in previous_schema.
  ValidateSchemaColumns(
      table_verifier_states[2].previous_schema(),
      {{.name = "a",
        .persistent_data_type = PersistentDataType::INT32,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "b",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "c",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "d",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "e",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "f",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse},
       {.name = "g",
        .persistent_data_type = PersistentDataType::STRING,
        .marked_for_deletion = MarkedForDeletion::kFalse}});

  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT b"));
  // Drop of column 'b' must be rolled back now.
  ValidateTabletSchema(
      table_id, {{.name = "a", .data_type = DataType::INT32},
                 {.name = "b", .data_type = DataType::STRING},
                 {.name = "c", .data_type = DataType::STRING},
                 {.name = "d", .data_type = DataType::STRING},
                 {.name = "e", .data_type = DataType::STRING},
                 {.name = "f", .data_type = DataType::STRING},
                 {.name = "g", .data_type = DataType::STRING}});
  table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 2);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  VerifyDdlOps(table_verifier_states[1], DdlOp::kAlter);

  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT a"));
  // Column 'g' doesn't exist anymore.
  ValidateTabletSchema(
      table_id, {{.name = "a", .data_type = DataType::INT32},
                 {.name = "b", .data_type = DataType::STRING},
                 {.name = "c", .data_type = DataType::STRING},
                 {.name = "d", .data_type = DataType::STRING},
                 {.name = "e", .data_type = DataType::STRING},
                 {.name = "f", .data_type = DataType::STRING}});
  table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 1);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
}

// Tests that rollback to savepoint which involves DROP and CREATE TABLE with same name works.
TEST_P(PgDdlSavepointMiniClusterTest, TestRollbackToSavepointWithDropCreateTableSameName) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));

  ASSERT_OK(conn.ExecuteFormat(
    "CREATE TABLE $0 (a int primary key, b text, c text, d text, e text, f text)", kTableName));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName));
  auto table = catalog_mgr.GetTableInfo(table_id);

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a int, b text, c text, d text)", kTableName));

  auto table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 1);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kDrop);

  // Get the id of the table created with the same name but after the drop statement.
  // The GetTableIdByTableName function cannot be used here because it returns the id of the first
  // table with the given name. In this case, there are two. So we need to choose the one different
  // from the first table.
  std::string table_id_after_drop;
  {
    const auto tables = ASSERT_RESULT(client->ListTables());
    for (const auto& t : tables) {
      if (t.namespace_name() == "yugabyte" && t.table_name() == kTableName &&
          t.table_id() != table_id) {
        table_id_after_drop = t.table_id();
      }
    }
  }
  ASSERT_FALSE(table_id_after_drop.empty());
  auto table_after_drop = catalog_mgr.GetTableInfo(table_id_after_drop);
  ASSERT_EQ(table_after_drop->LockForRead()->ysql_ddl_txn_verifier_state().size(), 1);
  VerifyDdlOps(table_after_drop->LockForRead()->ysql_ddl_txn_verifier_state()[0], DdlOp::kCreate);

  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT a"));
  ValidateTabletSchema(
      table_id, {{.name = "a", .data_type = DataType::INT32},
                 {.name = "b", .data_type = DataType::STRING},
                 {.name = "c", .data_type = DataType::STRING},
                 {.name = "d", .data_type = DataType::STRING},
                 {.name = "e", .data_type = DataType::STRING},
                 {.name = "f", .data_type = DataType::STRING}});
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
  ASSERT_OK(WaitForTableDeletionToFinish(client.get(), table_id_after_drop));
  ASSERT_FALSE(table_after_drop->LockForRead()->has_ysql_ddl_txn_verifier_state());

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
  ASSERT_FALSE(table_after_drop->LockForRead()->has_ysql_ddl_txn_verifier_state());
}

TEST_P(PgDdlSavepointMiniClusterTest, TestRollbackToSavepointWithNestedSavepoint) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a INT, b INT)", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c TEXT", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN d TEXT", kTableName));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN b", kTableName));

  // Rollback to savepoint a -> should remove column 'c', 'd', and bring back column 'b'.
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT a"));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName));
  ValidateTabletSchema(
      table_id, {{"ybrowid", DataType::BINARY},
                 {"a", DataType::INT32},
                 {"b", DataType::INT32}});
  auto table = catalog_mgr.GetTableInfo(table_id);
  auto table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 1);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  auto table_schema = ASSERT_RESULT(table->GetSchema());
  ASSERT_EQ(table_schema.num_columns(), 3); // Only column 'ybrowid', 'a' and 'b'.
  ASSERT_EQ(table_schema.columns()[0].name(), "ybrowid");
  ASSERT_EQ(table_schema.columns()[1].name(), "a");
  ASSERT_EQ(table_schema.columns()[2].name(), "b");

  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN e TEXT", kTableName));
  table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 2);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  VerifyDdlOps(table_verifier_states[1], DdlOp::kAlter);

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
}

TEST_P(PgDdlSavepointMiniClusterTest, TestRollbackToSavepointWithReleaseSavepoint) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a INT, b INT)", kTableName));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), "yugabyte", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kTableName));
  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a INT, b INT)", kTableName));
  std::string table_id_after_drop;
  {
    const auto tables = ASSERT_RESULT(client->ListTables());
    for (const auto& t : tables) {
      if (t.namespace_name() == "yugabyte" && t.table_name() == kTableName &&
          t.table_id() != table_id) {
        table_id_after_drop = t.table_id();
      }
    }
  }
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN b", kTableName));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT b"));
  ASSERT_OK(conn.Execute("SAVEPOINT c"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c INT", kTableName));

  // Rollback to savepoint a -> should bring bring the earlier table with column 'a' and 'b'.
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT a"));
  ValidateTabletSchema(
      table_id, {{"ybrowid", DataType::BINARY},
                 {"a", DataType::INT32},
                 {"b", DataType::INT32}});
  auto table = catalog_mgr.GetTableInfo(table_id);
  auto table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 1);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  auto table_schema = ASSERT_RESULT(table->GetSchema());
  ASSERT_EQ(table_schema.num_columns(), 3); // Only column 'ybrowid', 'a' and 'b'.
  ASSERT_EQ(table_schema.columns()[0].name(), "ybrowid");
  ASSERT_EQ(table_schema.columns()[1].name(), "a");
  ASSERT_EQ(table_schema.columns()[2].name(), "b");

  ASSERT_FALSE(table_id_after_drop.empty());
  ASSERT_OK(WaitForTableDeletionToFinish(client.get(), table_id_after_drop));
  auto table_after_drop = catalog_mgr.GetTableInfo(table_id_after_drop);
  ASSERT_FALSE(table_after_drop->LockForRead()->has_ysql_ddl_txn_verifier_state());

  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN e TEXT", kTableName));
  table_verifier_states = table->LockForRead()->ysql_ddl_txn_verifier_state();
  ASSERT_EQ(table_verifier_states.size(), 2);
  VerifyDdlOps(table_verifier_states[0], DdlOp::kCreate);
  VerifyDdlOps(table_verifier_states[1], DdlOp::kAlter);

  if (GetParam()) {
    ASSERT_OK(conn.Execute("COMMIT"));
  } else {
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }
  ASSERT_FALSE(table->LockForRead()->has_ysql_ddl_txn_verifier_state());
}

TEST_P(PgDdlSavepointMiniClusterTest, TestRollbackToSavepointSlowTableDeletion) {
  // Skip in case of commit as the test case is redundant.
  if (GetParam()) {
    return;
  }

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTableName));

  SyncPoint::GetInstance()->LoadDependency({
    {
      "PgDdlSavepointMiniClusterTest::TestRollbackToSavepointSlowTableDeletion:WaitForDeleteTable",
      "CatalogManager::CheckTableDeleted:BeforeRemoveDdlTransactionState"
    }
  });
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a INT, b INT)", kTableName2));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a INT, b INT)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx ON $0 (a)", kTableName));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT b"));
  // The deletion of the table due to the above rollback to savepoint b will be delayed until the
  // below sync point is hit. So it simulates the case when another rollback to savepoint arrives
  // while the table deletion has started but not completed.
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT a"));
  TEST_SYNC_POINT(
      "PgDdlSavepointMiniClusterTest::TestRollbackToSavepointSlowTableDeletion:WaitForDeleteTable");
  ASSERT_OK(conn.Execute("ROLLBACK"));
}

} // namespace yb::pgwrapper
