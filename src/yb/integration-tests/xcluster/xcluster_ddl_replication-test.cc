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
//

#include "yb/cdc/xcluster_types.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/colocated_util.h"
#include "yb/common/common_types.pb.h"
#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_uint32(xcluster_consistent_wal_safe_time_frequency_ms);

DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_at_end);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_at_start);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_ddl);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDDLReplicationTest : public XClusterDDLReplicationTestBase {
 public:
  Status AddDatabaseToReplication(
      const NamespaceId& source_db_id, const NamespaceId& target_db_id) {
    auto source_xcluster_client = client::XClusterClient(*producer_client());
    RETURN_NOT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
        kReplicationGroupId, source_db_id));
    auto bootstrap_required =
        VERIFY_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, source_db_id));
    SCHECK(!bootstrap_required, IllegalState, "Bootstrap should not be required");

    return AddNamespaceToXClusterReplication(source_db_id, target_db_id);
  }
};

// In automatic mode, sequences_data should have been created on both universe.
TEST_F(XClusterDDLReplicationTest, CheckSequenceDataTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    auto table_info = VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())
                          ->catalog_manager_impl()
                          .GetTableInfo(kPgSequencesDataTableId);
    SCHECK_NOTNULL(table_info);
    return Status::OK();
  }));
}

TEST_F(XClusterDDLReplicationTest, BasicSetupAlterTeardown) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto source_xcluster_client = client::XClusterClient(*producer_client());
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();

  // Ensure that tables are properly created with only one tablet each.
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name));

  // Alter replication to add a new database.
  const auto namespace_name2 = namespace_name + "2";
  auto [source_db2_id, target_db2_id] =
      ASSERT_RESULT(CreateDatabaseOnBothClusters(namespace_name2));
  ASSERT_OK(AddDatabaseToReplication(source_db2_id, target_db2_id));
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name2));

  // Alter replication to remove the new database.
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_db2_id, target_master_address));
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name2));
  // First namespace should not be affected.
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name));

  // Add the second database to replication again to test dropping everything.
  ASSERT_OK(AddDatabaseToReplication(source_db2_id, target_db2_id));
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name2));

  // Drop replication.
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, target_master_address));

  // Extension should no longer exist on either side.
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name));
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name2));
}

TEST_F(XClusterDDLReplicationTest, TestExtensionDeletionWithMultipleReplicationGroups) {
  const xcluster::ReplicationGroupId kReplicationGroupId2("ReplicationGroup2");
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name, /*only_source=*/true));
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId2));
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name, /*only_source=*/true));

  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_address*/ {}));
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name, /*only_source=*/true));

  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId2, /*target_master_address*/ {}));
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name, /*only_source=*/true));
}

TEST_F(XClusterDDLReplicationTest, DisableSplitting) {
  // Ensure that splitting of xCluster DDL Replication tables is disabled on both sides.
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  for (auto* cluster : {&producer_cluster_, &consumer_cluster_}) {
    for (const auto& table : {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
      auto yb_table_name = ASSERT_RESULT(
          GetYsqlTable(cluster, namespace_name, xcluster::kDDLQueuePgSchemaName, table));

      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
      ASSERT_OK(cluster->client_->GetTabletsFromTableId(yb_table_name.table_id(), 1, &tablets));

      auto res = CallAdmin(cluster->mini_cluster_.get(), "split_tablet", tablets[0].tablet_id());
      ASSERT_NOK(res);
      ASSERT_TRUE(res.status().message().Contains(
          "Tablet splitting is not supported for xCluster DDL Replication tables"));
    }
  }
}

TEST_F(XClusterDDLReplicationTest, DDLReplicationTablesNotColocated) {
  // Ensure that xCluster DDL Replication system tables are not colocated.

  ASSERT_OK(SetUpClusters(/* is_colocated */ true));
  // Create a colocated table so that we can run xCluster setup.
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    RETURN_NOT_OK(CreateYsqlTable(
        /*idx=*/1, /*num_tablets=*/1, cluster, /*tablegroup_name=*/{}, /*colocated=*/true));
    return Status::OK();
  }));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  for (auto* cluster : {&producer_cluster_, &consumer_cluster_}) {
    for (const auto& table : {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
      auto yb_table_name = ASSERT_RESULT(
          GetYsqlTable(cluster, namespace_name, xcluster::kDDLQueuePgSchemaName, table));

      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
      ASSERT_OK(cluster->client_->GetTabletsFromTableId(yb_table_name.table_id(), 0, &tablets));

      ASSERT_EQ(tablets.size(), 1);
      ASSERT_FALSE(IsColocationParentTableId(tablets[0].table_id()));
    }
  }
}

TEST_F(XClusterDDLReplicationTest, Bootstrapping) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(SetUpClusters(/*is_colocated=*/false, /*start_yb_controller_servers=*/true));
  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace_name}));
  ASSERT_OK(BackupFromProducer());
  ASSERT_OK(RestoreToConsumer());
  ASSERT_OK(CreateReplicationFromCheckpoint());
}

// TODO(Julien): As part of #24888, undisable this or make this a test that this correctly fails
// with an error.
TEST_F(XClusterDDLReplicationTest, YB_DISABLE_TEST(BootstrappingWithNoTables)) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(SetUpClusters(/*is_colocated=*/false, /*start_yb_controller_servers=*/true));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace_name}));
  ASSERT_OK(BackupFromProducer());
  ASSERT_OK(RestoreToConsumer());
  ASSERT_OK(CreateReplicationFromCheckpoint());
}

TEST_F(XClusterDDLReplicationTest, CreateTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Create a simple table.
  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  InsertRowsIntoProducerTableAndVerifyConsumer(producer_table_name);

  // Create a table in a new schema.
  const std::string kNewSchemaName = "new_schema";
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    // TODO(jhe) can remove this once create schema is replicated.
    RETURN_NOT_OK(conn.Execute("SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", kNewSchemaName));
    return Status::OK();
  }));
  {
    auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0.$1($2 int)", kNewSchemaName, producer_table_name.table_name(),
        kKeyColumnName));
  }
  auto producer_table_name_new_schema = ASSERT_RESULT(GetYsqlTable(
      &producer_cluster_, producer_table_name.namespace_name(), kNewSchemaName,
      producer_table_name.table_name()));
  InsertRowsIntoProducerTableAndVerifyConsumer(producer_table_name_new_schema);

  // Create a table under a new user.
  const std::string kNewUserName = "new_user";
  const std::string producer_table_name_new_user_str = producer_table_name.table_name() + "newuser";
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE USER $0 WITH PASSWORD '123'", kNewUserName));
    RETURN_NOT_OK(conn.Execute("SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
    RETURN_NOT_OK(conn.ExecuteFormat("GRANT CREATE ON SCHEMA public TO $0", kNewUserName));
    return Status::OK();
  }));
  {
    auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(conn.ExecuteFormat("SET ROLE $0", kNewUserName));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0($1 int)", producer_table_name_new_user_str, kKeyColumnName));
    // Also try connecting directly as the user.
    conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name, kNewUserName));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0($1 int)", producer_table_name_new_user_str + "2", kKeyColumnName));
    // Ensure that we are still connected as new_user (ie no elevated permissions).
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRowAsString("SELECT current_user")), kNewUserName);
  }
  auto producer_table_name_new_user = ASSERT_RESULT(GetYsqlTable(
      &producer_cluster_, producer_table_name.namespace_name(), producer_table_name.pgschema_name(),
      producer_table_name_new_user_str));
  InsertRowsIntoProducerTableAndVerifyConsumer(producer_table_name_new_user);
}

TEST_F(XClusterDDLReplicationTest, CreateTableWithEnum) {
  ASSERT_OK(SetUpClusters());
  {
    // Perturb OIDs on consumer side to make sure we don't accidentally preserve OIDs.
    auto conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(
        conn.Execute("CREATE TYPE gratuitous_enum AS ENUM ('red', 'orange', 'yellow', 'green', "
                     "'blue', 'purple');"));
    ASSERT_OK(conn.Execute("DROP TYPE gratuitous_enum;"));
  }

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  std::string expected;
  {
    auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(conn.Execute("CREATE TYPE color AS ENUM ('red', 'blue', 'green');"));
    ASSERT_OK(conn.Execute("CREATE TABLE t (paint_color color, amount INT);"));
    ASSERT_OK(
        conn.Execute("INSERT INTO t (paint_color, amount) VALUES "
                     "('red', 10), "
                     "('blue', 20), "
                     "('green', 30), "
                     "('red', 15), "
                     "('blue', 25);"));
    // PGConn can't handle enum values so have Postgres convert them to TEXT names.
    expected = ASSERT_RESULT(conn.FetchAllAsString("SELECT paint_color::TEXT, amount FROM t;"));
    LOG(INFO) << "expected table contents are: " << expected;
  }

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow({namespace_name}));
  {
    auto conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    auto actual = ASSERT_RESULT(conn.FetchAllAsString("SELECT paint_color::TEXT, amount FROM t;"));
    ASSERT_EQ(expected, actual);
  }

}

TEST_F(XClusterDDLReplicationTest, BlockMultistatementQuery) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Have to do this through ysqlsh -c since that sends the whole
  // query string as a single command.
  auto call_multistatement_query = [&](const std::string& query) {
    std::vector<std::string> args;
    args.push_back(GetPgToolPath("ysqlsh"));
    args.push_back("--host");
    args.push_back(producer_cluster_.pg_host_port_.host());
    args.push_back("--port");
    args.push_back(AsString(producer_cluster_.pg_host_port_.port()));
    args.push_back("-d");
    args.push_back(namespace_name);
    args.push_back("-c");
    args.push_back(query);

    auto s = CallAdminVec(args);
    LOG(INFO) << "Command output: " << s;
    ASSERT_NOK(s);
    ASSERT_TRUE(
        s.status().message().Contains("only a single DDL command is allowed in the query string"));
  };

  call_multistatement_query(
      "CREATE TABLE multistatement(i int PRIMARY KEY);"
      "INSERT INTO multistatement VALUES (1);");
  call_multistatement_query(
      "SELECT 1;"
      "CREATE TABLE multistatement(i int PRIMARY KEY);");
  call_multistatement_query(
      "CREATE TABLE multistatement1(i int PRIMARY KEY);"
      "CREATE TABLE multistatement2(i int PRIMARY KEY);");
  call_multistatement_query(
      "CREATE TABLE multistatement(i int);"
      "CREATE UNIQUE INDEX ON multistatement(i);");
}

TEST_F(XClusterDDLReplicationTest, CreateIndex) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const std::string kBaseTableName = "base_table";
  const std::string kColumn2Name = "a";
  const std::string kColumn3Name = "b";
  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));

  // Create a base table.
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE TABLE $0($1 int PRIMARY KEY, $2 int, $3 text)", kBaseTableName, kKeyColumnName,
      kColumn2Name, kColumn3Name));
  const auto producer_base_table_name = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kBaseTableName));

  // Insert some rows.
  ASSERT_OK(p_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2, i::text FROM generate_series(1, 100) as i;", kBaseTableName));
  {
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    auto producer_table = ASSERT_RESULT(GetProducerTable(producer_base_table_name));
    auto consumer_table = ASSERT_RESULT(GetConsumerTable(producer_base_table_name));
    ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
  }

  // Create index on column 2.
  ASSERT_OK(p_conn.ExecuteFormat("CREATE INDEX ON $0($1 ASC)", kBaseTableName, kColumn2Name));
  const auto kCol2CountStmt =
      Format("SELECT COUNT(*) FROM $0 WHERE $1 >= 0", kBaseTableName, kColumn2Name);
  ASSERT_TRUE(ASSERT_RESULT(p_conn.HasIndexScan(kCol2CountStmt)));

  // Verify index is replicated on consumer.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  {
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(&resp));
    EXPECT_EQ(resp.entry().tables_size(), 4);  // ddl_queue + base_table + index + sequences_data
  }
  ASSERT_TRUE(ASSERT_RESULT(c_conn.HasIndexScan(kCol2CountStmt)));

  // Create unique index on column 3.
  ASSERT_OK(p_conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0($1)", kBaseTableName, kColumn3Name));
  // Test inserting duplicate value.
  ASSERT_NOK(p_conn.ExecuteFormat("INSERT INTO $0 VALUES(101, 101, '1');", kBaseTableName));
  ASSERT_OK(p_conn.ExecuteFormat("INSERT INTO $0 VALUES(0, 0, '0');", kBaseTableName));

  // Verify uniqueness constraint on consumer.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  // Bypass writes being blocked on target clusters.
  ASSERT_OK(c_conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = true"));
  ASSERT_NOK(c_conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 1, '0');", kBaseTableName));
  ASSERT_NOK(c_conn.ExecuteFormat("INSERT INTO $0 VALUES(101, 101, '1');", kBaseTableName));
  ASSERT_OK(c_conn.ExecuteFormat("INSERT INTO $0 VALUES(101, 101, '101');", kBaseTableName));
  ASSERT_OK(c_conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = false"));
}

TEST_F(XClusterDDLReplicationTest, ExactlyOnceReplication) {
  // Test that DDLs are only replicated exactly once.
  const int kNumTablets = 3;

  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Fail next DDL query and continue to process it.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_end) = true;

  // Pause replication so we can accumulate a few DDLs.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, false /* is_enabled */));

  const int kNumTables = 3;
  std::vector<client::YBTableName> producer_table_names;
  for (int i = 0; i < kNumTables; ++i) {
    producer_table_names.push_back(ASSERT_RESULT(CreateYsqlTable(
        /*idx=*/i, kNumTablets, &producer_cluster_)));
    // Wait for apply safe time to increase.
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_xcluster_consistent_wal_safe_time_frequency_ms));
  }

  // Resume replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, true /* is_enabled */));

  // Safe time should not advance.
  ASSERT_NOK(WaitForSafeTimeToAdvanceToNow());

  // Allow processing to continue.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_end) = false;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  for (int i = 0; i < kNumTables; ++i) {
    InsertRowsIntoProducerTableAndVerifyConsumer(producer_table_names[i]);
  }
}

TEST_F(XClusterDDLReplicationTest, DDLsWithinTransaction) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  // Run a bunch of DDLs within a transaction, each of which depend on previous ones so the exact
  // ordering matters. If the target tries to run in any other order it will get stuck.
  ASSERT_OK(p_conn.Execute("BEGIN"));
  ASSERT_OK(p_conn.Execute("CREATE TABLE test_table_1 (key int PRIMARY KEY);"));
  ASSERT_OK(p_conn.Execute("ALTER TABLE test_table_1 RENAME TO test_table_2;"));
  ASSERT_OK(p_conn.Execute("ALTER TABLE test_table_2 ADD COLUMN a int;"));
  ASSERT_OK(p_conn.Execute("ALTER TABLE test_table_2 RENAME COLUMN a TO b;"));
  ASSERT_OK(p_conn.Execute("ALTER TABLE test_table_2 DROP COLUMN b;"));
  ASSERT_OK(p_conn.Execute("COMMIT"));

  ASSERT_OK(PrintDDLQueue(producer_cluster_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", "test_table_2"))));

  InsertRowsIntoProducerTableAndVerifyConsumer(producer_table->name());
}

TEST_F(XClusterDDLReplicationTest, DuplicateTableNames) {
  // Test that when there are multiple tables with the same name, we are able to correctly link the
  // target tables to the correct source tables.

  const int kNumTablets = 3;
  const int kNumRowsTable1 = 10;
  const int kNumRowsTable2 = 3 * kNumRowsTable1;
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Pause replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, false /* is_enabled */));

  // Create a table on the producer.
  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, kNumTablets, &producer_cluster_));
  // Insert some rows into the first table.
  auto producer_table = ASSERT_RESULT(GetProducerTable(producer_table_name));
  ASSERT_OK(InsertRowsInProducer(0, kNumRowsTable1, producer_table));

  // Drop the table, it should move to HIDDEN state.
  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(producer_conn.ExecuteFormat("DROP TABLE $0", producer_table_name.table_name()));

  // Create a new table with the same name.
  auto producer_table_name2 = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, kNumTablets, &producer_cluster_));
  // Insert a different number of rows into the second table.
  auto producer_table2 = ASSERT_RESULT(GetProducerTable(producer_table_name2));
  ASSERT_OK(InsertRowsInProducer(100, 100 + kNumRowsTable2, producer_table2));

  // Resume replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, true /* is_enabled */));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify that we only see the second table, and that it has the right number of rows.
  ASSERT_OK(WaitForRowCount(producer_table_name2, kNumRowsTable2, &consumer_cluster_));

  // Ensure that we can write more rows to this table still.
  InsertRowsIntoProducerTableAndVerifyConsumer(producer_table_name2);
}

TEST_F(XClusterDDLReplicationTest, RepeatedCreateAndDropTable) {
  // Test when a table is created and dropped multiple times.
  const int kNumIterations = 10;
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Pause replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, false /* is_enabled */));

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  for (int i = 0; i < kNumIterations; i++) {
    ASSERT_OK(producer_conn.Execute("DROP TABLE IF EXISTS live_die_repeat"));
    ASSERT_OK(producer_conn.Execute("CREATE TABLE live_die_repeat(a int)"));
    ASSERT_OK(producer_conn.ExecuteFormat("INSERT INTO live_die_repeat VALUES($0)", i + 1));
  }

  // Resume replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, true /* is_enabled */));
  propagation_timeout_ = propagation_timeout_ * 2;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Ensure table has the correct row at the end.
  auto consumer_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_EQ(
      ASSERT_RESULT(consumer_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM live_die_repeat")), 1);
  ASSERT_EQ(
      ASSERT_RESULT(consumer_conn.FetchRow<int32>("SELECT * FROM live_die_repeat")),
      kNumIterations);
}

TEST_F(XClusterDDLReplicationTest, AddRenamedTable) {
  // Test that when a table is renamed, the new table is correctly linked to the source table.
  const std::string kTableNewName = "renamed_table";

  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Pause replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, false /* is_enabled */));

  // Create a table on the producer.
  auto producer_table_name_original = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  // Insert some rows into the table.
  auto producer_table = ASSERT_RESULT(GetProducerTable(producer_table_name_original));
  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table));

  // Rename the table.
  // TODO(#23951) remove manual flag once we support ALTERs.
  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 RENAME TO $1", producer_table_name_original.table_name(), kTableNewName));

  // Insert some more rows into the table.
  auto producer_table_name_renamed = producer_table_name_original;
  producer_table_name_renamed.set_table_name(kTableNewName);
  producer_table = ASSERT_RESULT(GetProducerTable(producer_table_name_renamed));
  ASSERT_OK(InsertRowsInProducer(10, 30, producer_table));

  // Resume replication.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, true /* is_enabled */));

  // TODO(#23951) need to wait for create and manually run the DDL on the target side for now.
  ASSERT_OK(StringWaiterLogSink("Successfully processed entry").WaitFor(kTimeout));
  auto consumer_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 RENAME TO $1", producer_table_name_original.table_name(), kTableNewName));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify row counts.
  auto consumer_table = ASSERT_RESULT(GetConsumerTable(producer_table_name_renamed));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
}

TEST_F(XClusterDDLReplicationTest, CreateColocatedTables) {
  ASSERT_OK(SetUpClusters(/*is_colocated=*/true));
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  const auto kNumTables = 3;

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(
      producer_conn.Execute("SET yb_xcluster_ddl_replication.TEST_allow_colocated_objects=1"));
  for (int i = 0; i < kNumTables; i++) {
    const auto table_name = kNewTableName + std::to_string(i);
    ASSERT_OK(producer_conn.ExecuteFormat("CREATE TABLE $0 (key int)", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i FROM generate_series(1, 100) as i", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 rename column key to a", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i FROM generate_series(101, 200) as i", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 rename column a to key", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i FROM generate_series(201, 300) as i", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column a int", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, i*2 FROM generate_series(301, 400) as i", table_name));
  }

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify row counts.
  for (int i = 0; i < kNumTables; i++) {
    const auto table_name = kNewTableName + std::to_string(i);
    auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", table_name))));
    auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", table_name))));
    ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
  }
}

TEST_F(XClusterDDLReplicationTest, CreateColocatedTableWithPause) {
  ASSERT_OK(SetUpClusters(/*is_colocated=*/true));
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Fail the DDL apply, but let replication to the colocated tablet keep running.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;
  const auto kNewTableName = "new_colocated_table";

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(
      producer_conn.Execute("SET yb_xcluster_ddl_replication.TEST_allow_colocated_objects=1"));
  ASSERT_OK(producer_conn.ExecuteFormat("CREATE TABLE $0 (key int)", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i FROM generate_series(1, 100) as i", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column a int", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column b int", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i*2, i*3 FROM generate_series(101, 200) as i", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 drop column a", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 rename column b to a", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i*3 FROM generate_series(201, 300) as i", kNewTableName));

  // Ensure that the colocated table is able to be fully replicated.
  auto colocated_parent_table_id = ASSERT_RESULT(GetColocatedDatabaseParentTableId());
  ASSERT_OK(WaitForReplicationDrain(
      /*expected_num_nondrained=*/0, kRpcTimeout, /*target_time=*/std::nullopt,
      {colocated_parent_table_id}));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = false;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify row counts.
  auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));

  // Verify that xcluster table info is cleaned up.
  auto target_table_info = ASSERT_RESULT(consumer_cluster_.mini_cluster_->GetLeaderMiniMaster())
                               ->catalog_manager_impl()
                               .GetTableInfo(consumer_table->id());
  LOG(INFO) << "Table info: " << target_table_info->LockForRead()->pb.ShortDebugString();
  EXPECT_FALSE(target_table_info->LockForRead()->pb.has_xcluster_table_info());
  // Replication info should no longer have any historical schemas for this colocation id.
  auto replication_info =
      ASSERT_RESULT(GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  LOG(INFO) << "Replication info: " << replication_info.ShortDebugString();
  auto target_namespace_info =
      replication_info.entry().db_scoped_info().target_namespace_infos().at(
          consumer_table->name().namespace_id());
  EXPECT_EQ(target_namespace_info.colocated_historical_schema_packings_size(), 0);
}

TEST_F(XClusterDDLReplicationTest, CreateColocatedTableWithSourceFailures) {
  ASSERT_OK(SetUpClusters(/*is_colocated=*/true));
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  const auto kColocationId = 123456;

  // Pause any processing of DDLs so we can accumulate some pending schemas.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;

  // First fail creating a colocated table on the source.
  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(
      producer_conn.Execute("SET yb_xcluster_ddl_replication.TEST_allow_colocated_objects=1"));
  ASSERT_OK(producer_conn.Execute("SET yb_test_fail_next_ddl=1"));
  ASSERT_NOK(producer_conn.ExecuteFormat(
      "CREATE TABLE $0 (a text) WITH (colocation_id=$1)", kNewTableName, kColocationId));

  // Now create the table successfully using the same colocation id but different schemas.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "CREATE TABLE $0 (key int, b text) WITH (colocation_id=$1)", kNewTableName, kColocationId));
  // Perform some additional DDLs.
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN b", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i FROM generate_series(1, 100) as i", kNewTableName));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = false;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify row counts.
  auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));

  // Target schema version will be 5 in the end.
  // Pending schemas:
  // - 0 for the failed create table
  // - 1 for the successful create table
  // - 2 for the drop column
  // After the DDLs run on the target:
  // - 4 for the create table
  // - 5 for the drop column
  EXPECT_EQ(consumer_table->schema().version(), 5);
}

TEST_F(XClusterDDLReplicationTest, CreateColocatedTableWithTargetFailures) {
  ASSERT_OK(SetUpClusters(/*is_colocated=*/true));
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";

  // Allow DDLs through but fail them at the end of execution.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = true;

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(
      producer_conn.Execute("SET yb_xcluster_ddl_replication.TEST_allow_colocated_objects=1"));
  ASSERT_OK(producer_conn.ExecuteFormat("CREATE TABLE $0 (key int)", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i FROM generate_series(1, 100) as i", kNewTableName));

  SleepFor(MonoDelta::FromSeconds(2));  // Sleep for a bit to allow DDLs to continue to fail.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = false;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));

  // Test another alter + inserts.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = true;

  const auto kNewTableName2 = "renamed_colocated_table";
  ASSERT_OK(
      producer_conn.ExecuteFormat("ALTER TABLE $0 RENAME TO $1", kNewTableName, kNewTableName2));
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column b int", kNewTableName2));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i*2 FROM generate_series(101, 200) as i", kNewTableName2));

  SleepFor(MonoDelta::FromSeconds(2));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = false;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName2))));
  consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName2))));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
}

// Test is disabled until #25926 is fixed.
TEST_F(XClusterDDLReplicationTest, YB_DISABLE_TEST(ColocatedHistoricalSchemasWithCompactions)) {
  ASSERT_OK(SetUpClusters(/*is_colocated=*/true));
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  const auto kNumTables = 3;

  // Pause any processing of DDLs so we can accumulate some pending schemas.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(
      producer_conn.Execute("SET yb_xcluster_ddl_replication.TEST_allow_colocated_objects=1"));
  for (int i = 0; i < kNumTables; i++) {
    const auto table_name = kNewTableName + std::to_string(i);
    ASSERT_OK(producer_conn.ExecuteFormat("CREATE TABLE $0 (key int)", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i FROM generate_series(1, 100) as i", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column a int", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column b int", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, i*2, i*3 FROM generate_series(101, 200) as i", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 drop column a", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 rename column b to a", table_name));
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, i*3 FROM generate_series(201, 300) as i", table_name));
  }

  // Flush and compact all target tablets. Ensure that we don't lose any historical schemas.
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = false;
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify row counts.
  for (int i = 0; i < kNumTables; i++) {
    auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(GetYsqlTable(
        &producer_cluster_, namespace_name, /*schema_name*/ "",
        kNewTableName + std::to_string(i)))));
    auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(GetYsqlTable(
        &producer_cluster_, namespace_name, /*schema_name*/ "",
        kNewTableName + std::to_string(i)))));
    ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
  }
}

TEST_F(XClusterDDLReplicationTest, AlterExistingColocatedTable) {
  // Test alters on a table that is already part of replication.
  ASSERT_OK(SetUpClusters(/*is_colocated=*/true));
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(
      producer_conn.Execute("SET yb_xcluster_ddl_replication.TEST_allow_colocated_objects=1"));
  ASSERT_OK(
      producer_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN j int", kInitialColocatedTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i FROM generate_series(1, 100) as i", kInitialColocatedTableName));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify row counts.
  auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(GetYsqlTable(
      &producer_cluster_, namespace_name, /*schema_name*/ "", kInitialColocatedTableName))));
  auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(GetYsqlTable(
      &producer_cluster_, namespace_name, /*schema_name*/ "", kInitialColocatedTableName))));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
}

TEST_F(XClusterDDLReplicationTest, ExtraOidAllocationsOnTarget) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());
  google::SetVLOGLevel("catalog_manager*", 1);
  google::SetVLOGLevel("pg_client_service*", 1);

  {
    /*
     * Do extra OID allocations on the target that do not happen on the source.
     *
     * Most obviously, a manual DDL will do this but we are using that here as a stand-in for any
     * DDL weirdness that causes an extra OID allocation when a replicated DDL is run on the target
     * vs when it is run on the source.
     */
    auto conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(conn.Execute("SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
    for (int i = 0; i < 100; i++) {
      ASSERT_OK(conn.ExecuteFormat("CREATE TYPE my_manual_enum_$0 AS ENUM ('label')", i));
    }
  }

  {
    // See if the allocations a replicated DDL does collide with the extra allocations above.
    auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    for (int i = 0; i < 100; i++) {
      ASSERT_OK(conn.ExecuteFormat("CREATE TYPE my_enum_$0 AS ENUM ('label')", i));
    }
  }

  // Wait to see if applying the DDL on the target runs into problems.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
}

class XClusterDDLReplicationAddDropColumnTest : public XClusterDDLReplicationTest {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    XClusterDDLReplicationTest::SetUp();
    ASSERT_OK(SetUpClusters());
    ASSERT_OK(CheckpointReplicationGroup());
    ASSERT_OK(CreateReplicationFromCheckpoint());
    producer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
    consumer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
  }

  Status PerformStep(size_t step) {
    // Step 0 is initial table creation.
    if (step == 0) {
      RETURN_NOT_OK(producer_conn_->ExecuteFormat(
          "CREATE TABLE IF NOT EXISTS $0($1 int)", kTableName, kColumnNames[0]));
      return producer_conn_->ExecuteFormat(
          "INSERT INTO $0($1) values ($2)", kTableName, kColumnNames[0], 0);
    }
    // Odd steps are add column, even steps are drop column.
    if (step % 2) {
      LOG(INFO) << "STARTING STEP " << step << ": ADD COLUMN " << kColumnNames[step / 2 + 1];
      RETURN_NOT_OK(producer_conn_->ExecuteFormat(
          "ALTER TABLE $0 ADD COLUMN $1 int", kTableName, kColumnNames[step / 2 + 1]));
      RETURN_NOT_OK(producer_conn_->ExecuteFormat(
          "INSERT INTO $0($1,$2) values ($3,$3)", kTableName, kColumnNames[0],
          kColumnNames[step / 2 + 1], step));
    } else {
      LOG(INFO) << "STARTING STEP " << step << ": DROP COLUMN " << kColumnNames[step / 2];
      RETURN_NOT_OK(producer_conn_->ExecuteFormat(
          "ALTER TABLE $0 DROP COLUMN $1", kTableName, kColumnNames[step / 2]));
      RETURN_NOT_OK(producer_conn_->ExecuteFormat(
          "INSERT INTO $0($1) values ($2)", kTableName, kColumnNames[0], step));
    }
    return Status::OK();
  }

  Status VerifyTargetData(bool is_paused) {
    if (!is_paused) {
      // Tables should have the same schema and data.
      for (const auto& query :
           {Format(kDataQueryStr, kTableName, kColumnNames[0]),
            Format(kSchemaQueryStr, kTableName)}) {
        auto producer_output = VERIFY_RESULT(producer_conn_->FetchAllAsString(query));
        auto consumer_output = VERIFY_RESULT(consumer_conn_->FetchAllAsString(query));
        SCHECK_EQ(producer_output, consumer_output, IllegalState, "Data mismatch");
      }
      return Status::OK();
    }

    // Capture the expected output at the pause step.
    if (paused_expected_data_output_.empty()) {
      paused_expected_data_output_ = VERIFY_RESULT(
          producer_conn_->FetchAllAsString(Format(kDataQueryStr, kTableName, kColumnNames[0])));
      paused_expected_schema_output_ =
          VERIFY_RESULT(producer_conn_->FetchAllAsString(Format(kSchemaQueryStr, kTableName)));
      LOG(INFO) << "Paused expected data output: " << paused_expected_data_output_;
      LOG(INFO) << "Paused expected schema output: " << paused_expected_schema_output_;
    }

    // Consumer should be stuck on paused step.
    auto consumer_output = VERIFY_RESULT(
        consumer_conn_->FetchAllAsString(Format(kDataQueryStr, kTableName, kColumnNames[0])));
    SCHECK_EQ(paused_expected_data_output_, consumer_output, IllegalState, "Data mismatch");
    auto consumer_schema_output =
        VERIFY_RESULT(consumer_conn_->FetchAllAsString(Format(kSchemaQueryStr, kTableName)));
    SCHECK_EQ(
        paused_expected_schema_output_, consumer_schema_output, IllegalState, "Schema mismatch");

    return Status::OK();
  }

  Status WaitForDDLReplication(bool is_paused) {
    if (!is_paused) {
      return WaitForSafeTimeToAdvanceToNow();
    }
    // Other pollers asides from ddl_queue should still be able to advance.
    return WaitForSafeTimeToAdvanceToNowWithoutDDLQueue();
  }

  Status RunTest(size_t step_to_pause_on) {
    bool is_paused = false;
    for (size_t step = 0; step <= kNumSteps; ++step) {
      RETURN_NOT_OK(PerformStep(step));
      RETURN_NOT_OK(WaitForDDLReplication(is_paused));

      if (step == step_to_pause_on) {
        is_paused = true;
        LOG(INFO) << "STARTING STEP " << step_to_pause_on << ": PAUSING";
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = is_paused;
      }

      RETURN_NOT_OK(VerifyTargetData(is_paused));
    }

    // Unpause and verify that the consumer catches up.
    LOG(INFO) << "STARTING STEP " << kNumSteps + 1 << ": UNPAUSE";
    is_paused = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = is_paused;
    RETURN_NOT_OK(WaitForDDLReplication(is_paused));
    RETURN_NOT_OK(VerifyTargetData(is_paused));

    // Reset state.
    LOG(INFO) << "STARTING STEP " << kNumSteps + 2 << ": RESET";
    RETURN_NOT_OK(producer_conn_->ExecuteFormat("DELETE FROM $0", kTableName));
    RETURN_NOT_OK(WaitForDDLReplication(is_paused));
    RETURN_NOT_OK(VerifyTargetData(is_paused));
    paused_expected_data_output_.clear();
    paused_expected_schema_output_.clear();

    return Status::OK();
  }

 protected:
  const std::vector<std::string> kColumnNames = {"a", "b", "c"};
  const size_t kNumSteps = (kColumnNames.size() - 1) * 2;
  const std::string kTableName = "add_drop_column_table";
  const std::string kDataQueryStr = "SELECT * FROM $0 ORDER BY $1";
  const std::string kSchemaQueryStr =
      "SELECT column_name, data_type FROM information_schema.columns WHERE table_name = '$0' ORDER "
      "BY column_name";

  std::unique_ptr<pgwrapper::PGConn> producer_conn_;
  std::unique_ptr<pgwrapper::PGConn> consumer_conn_;

  std::string paused_expected_data_output_;
  std::string paused_expected_schema_output_;
};

TEST_F(XClusterDDLReplicationAddDropColumnTest, AddDropColumns) {
  // Repeatedly add/drop columns while inserting data. Run the test multiple times with a pause
  // after each add/drop. Ensure that the consumer is able to still read older data if paused even
  // as other streams are making progress.
  for (size_t i = 0; i < kNumSteps; ++i) {
    LOG(INFO) << "Running test with pause on step " << i;
    ASSERT_OK(RunTest(i));
    LOG(INFO) << "Finished running test with pause on step " << i;
  }
}

// Make sure we can create Colocated db and table on both clusters that is not affected by an the
// replication of a different database.
TEST_F(XClusterDDLReplicationTest, CreateNonXClusterColocatedDb) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const auto kColocatedDB = "colocated_db";
  const auto kCreateTableStmt = "CREATE TABLE tbl1(a int)";
  const auto kInsertStmt = "INSERT INTO tbl1 VALUES (1)";

  ASSERT_OK(CreateDatabase(&consumer_cluster_, kColocatedDB, /*colocated=*/true));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(kColocatedDB));
  ASSERT_OK(c_conn.Execute(kCreateTableStmt));
  ASSERT_OK(c_conn.Execute(kInsertStmt));

  ASSERT_OK(CreateDatabase(&producer_cluster_, kColocatedDB, /*colocated=*/true));
  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(kColocatedDB));
  ASSERT_OK(p_conn.Execute(kCreateTableStmt));
  ASSERT_OK(p_conn.Execute(kInsertStmt));
}

class XClusterDDLReplicationTableRewriteTest : public XClusterDDLReplicationTest {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    XClusterDDLReplicationTest::SetUp();
    ASSERT_OK(SetUpClusters());
    ASSERT_OK(CheckpointReplicationGroup());
    ASSERT_OK(CreateReplicationFromCheckpoint());

    producer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
    consumer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));

    // Create a base table and insert some rows.
    ASSERT_OK(producer_conn_->ExecuteFormat(
        "CREATE TABLE $0($1 int, $2 int)", kBaseTableName_, kKeyColumnName, kColumn2Name_));
    producer_base_table_name_ = ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kBaseTableName_));

    // Create index on the second column.
    ASSERT_OK(producer_conn_->ExecuteFormat("CREATE INDEX idx ON $0($1 ASC)",
        kBaseTableName_, kColumn2Name_));

    // Check number of tables in the universe replication.
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(&resp));
    EXPECT_EQ(resp.entry().tables_size(), 4);  // ddl_queue + base_table + index + sequences_data

    // Check the second column is indexed.
    VerifyIndex(kColumn2Name_, /* expected_indexed */ true);
  }

  void VerifyIndex(const std::string& column_name, bool expected_indexed) {
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    const auto stmt =
        Format("SELECT COUNT(*) FROM $0 WHERE $1 = 1", kBaseTableName_, column_name);
    ASSERT_EQ(expected_indexed, ASSERT_RESULT(producer_conn_->HasIndexScan(stmt)));
    ASSERT_EQ(expected_indexed, ASSERT_RESULT(consumer_conn_->HasIndexScan(stmt)));
  }

  void VerifyTableRewrite() {
    // Verify table rewrite change the table id.
    auto producer_base_table_name_after_rewrite = ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, "", kBaseTableName_));
    ASSERT_NE(producer_base_table_name_.table_id(),
        producer_base_table_name_after_rewrite.table_id());
    // Verify data has been replicated.
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    auto producer_table = ASSERT_RESULT(GetProducerTable(producer_base_table_name_after_rewrite));
    auto consumer_table = ASSERT_RESULT(GetConsumerTable(producer_base_table_name_after_rewrite));
    ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
  }

  const std::string kBaseTableName_ = "base_table";
  const std::string kColumn2Name_ = "b";
  std::unique_ptr<pgwrapper::PGConn> producer_conn_;
  std::unique_ptr<pgwrapper::PGConn> consumer_conn_;
  client::YBTableName producer_base_table_name_;
};

TEST_F(XClusterDDLReplicationTableRewriteTest, AddAndDropPrimaryKeyTest) {
  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(1, 100) as i;", kBaseTableName_));

  // Execute ADD PRIMARY KEY table rewrite.
  ASSERT_OK(producer_conn_->ExecuteFormat("ALTER TABLE $0 ADD PRIMARY KEY ($1 ASC);",
      kBaseTableName_, kKeyColumnName));
  VerifyIndex(kKeyColumnName, /* expected_indexed */ true);
  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(101, 200) as i;",
      kBaseTableName_));
  VerifyTableRewrite();

  // Execute DROP PRIMARY KEY table rewrite.
  ASSERT_OK(producer_conn_->ExecuteFormat("ALTER TABLE $0 DROP CONSTRAINT $1;",
      kBaseTableName_, kBaseTableName_ + "_pkey"));
  VerifyIndex(kKeyColumnName, /* expected_indexed */ false);
  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(1, 100) as i;",
      kBaseTableName_));
  VerifyTableRewrite();

  // Verify column 2 is still indexed after reindex from table rewrite.
  VerifyIndex(kColumn2Name_, /* expected_indexed */ true);
}

TEST_F(XClusterDDLReplicationTableRewriteTest, AddColumnPrimaryKeyTest) {
  const std::string kColumn3Name = "c";

  // Execute ADD COLUMN .. PRIMARY KEY table rewrite.
  ASSERT_OK(producer_conn_->ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 int PRIMARY KEY;",
      kBaseTableName_, kColumn3Name));

  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2, i FROM generate_series(1, 100) as i;", kBaseTableName_));

  VerifyTableRewrite();

  // Verify new column is indexed.
  VerifyIndex(kColumn3Name, /* expected_indexed */ true);
  // Verify the second column is still indexed after reindex from table rewrite.
  VerifyIndex(kColumn2Name_, /* expected_indexed */ true);
}

TEST_F(XClusterDDLReplicationTableRewriteTest, AddColumnDefaultVolatile) {
  const std::string kColumn3Name = "created_at";

  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(1, 100) as i;", kBaseTableName_));

  // Execute ADD COLUMN ... DEFAULT (volatile) table rewrite.
  ASSERT_OK(producer_conn_->ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 TIMESTAMP DEFAULT clock_timestamp() NOT NULL;",
      kBaseTableName_, kColumn3Name));

  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(101, 200) as i;",
      kBaseTableName_));

  // Make sure there isn't any NULL in the new column.
  auto producer_base_table_name_after_rewrite = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, "", kBaseTableName_));
  auto producer_scan_results =
      ASSERT_RESULT(ScanToStrings(producer_base_table_name_after_rewrite, &producer_cluster_));
  for (int row = 0; row < PQntuples(producer_scan_results.get()); ++row) {
    auto prod_val = EXPECT_RESULT(pgwrapper::ToString(producer_scan_results.get(), row, 2));
    ASSERT_NE(prod_val, "NULL");
  }

  VerifyTableRewrite();

  // Verify column 2 is still indexed after reindex from table rewrite.
  VerifyIndex(kColumn2Name_, /* expected_indexed */ true);
}

TEST_F(XClusterDDLReplicationTableRewriteTest, AlterTypeIsBlocked) {
  ASSERT_OK(producer_conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(1, 100) as i;", kBaseTableName_));

  // Execute ALTER COLUMN ... TYPE table rewrite.
  auto status = producer_conn_->ExecuteFormat(
      "ALTER TABLE $0 ALTER COLUMN $1 TYPE float USING(random());",
      kBaseTableName_, kColumn2Name_);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Table Rewrite ALTER COLUMN TYPE is not supported");

  // Ensure the table rewrite is not processed by verifying that
  // the table ID and column type remain unchanged.
  auto producer_base_table_name_after_rewrite_failed = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, "", kBaseTableName_));
  ASSERT_EQ(producer_base_table_name_.table_id(),
      producer_base_table_name_after_rewrite_failed.table_id());
  auto column2_type = ASSERT_RESULT(producer_conn_->FetchAllAsString(
      Format("SELECT data_type FROM information_schema.columns WHERE table_name = '$0' "
             "AND column_name = '$1';", kBaseTableName_, kColumn2Name_)));
  ASSERT_EQ(column2_type, "integer");

  // Verify column 2 is still indexed.
  VerifyIndex(kColumn2Name_, /* expected_indexed */ true);
}

}  // namespace yb
