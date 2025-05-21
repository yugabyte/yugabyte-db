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
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_uint32(ysql_oid_cache_prefetch_size);
DECLARE_uint32(xcluster_consistent_wal_safe_time_frequency_ms);
DECLARE_int32(xcluster_ddl_queue_max_retries_per_ddl);
DECLARE_int32(ysql_sequence_cache_minval);

DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_at_end);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_at_start);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_ddl);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDDLReplicationTest : public XClusterDDLReplicationTestBase {
 public:
  Status SetUpClustersAndCheckpointReplicationGroup(
      bool is_colocated = false, bool start_yb_controller_servers = false) {
    RETURN_NOT_OK(SetUpClusters(is_colocated, start_yb_controller_servers));
    RETURN_NOT_OK(
        CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
    // Bootstrap here would have no effect because the database is empty so we skip it for the test.
    return Status::OK();
  }

  // Precondition: a bootstrap is not actually needed.
  // For example, the two databases might both be completely empty.
  // This is not the same as whether or not IsXClusterBootstrapRequired will return false.
  Status AddDatabaseToReplication(
      const NamespaceId& source_db_id, const NamespaceId& target_db_id) {
    auto source_xcluster_client = client::XClusterClient(*producer_client());
    RETURN_NOT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
        kReplicationGroupId, source_db_id));
    auto bootstrap_required =
        VERIFY_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, source_db_id));
    SCHECK(
        bootstrap_required, IllegalState, "Bootstrap should always be required for Automatic mode");

    return AddNamespaceToXClusterReplication(source_db_id, target_db_id);
  }
};

// In automatic mode, sequences_data should have been created on both universe.
TEST_F(XClusterDDLReplicationTest, CheckSequenceDataTable) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto source_xcluster_client = client::XClusterClient(*producer_client());
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();

  // Ensure that tables are properly created with only one tablet each.
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name));

  // Alter replication to add a new database.
  const auto namespace_name2 = namespace_name + "2";
  auto [source_db2_id, target_db2_id] =
      ASSERT_RESULT(CreateDatabaseOnBothClusters(namespace_name2));
  // AddDatabaseToReplication precondition met because databases are empty
  ASSERT_OK(AddDatabaseToReplication(source_db2_id, target_db2_id));
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name2));

  // Alter replication to remove the new database.
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_db2_id, target_master_address));
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name2));
  // First namespace should not be affected.
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name));

  // Add the second database to replication again to test dropping everything.
  // AddDatabaseToReplication precondition met because databases are empty
  ASSERT_OK(AddDatabaseToReplication(source_db2_id, target_db2_id));
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name2));

  // Drop replication.
  ASSERT_OK(DeleteOutboundReplicationGroup());

  // Extension should no longer exist on either side.
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name));
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name2));
}

TEST_F(XClusterDDLReplicationTest, YB_DISABLE_TEST_ON_MACOS(SurviveRestarts)) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  {
    TEST_SetThreadPrefixScoped prefix_se("NP");
    ASSERT_OK(producer_cluster_.mini_cluster_.get()->RestartSync());
  }
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  {
    TEST_SetThreadPrefixScoped prefix_se("NC");
    ASSERT_OK(consumer_cluster_.mini_cluster_.get()->RestartSync());
  }
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  {
    TEST_SetThreadPrefixScoped prefix_se("NNP");
    ASSERT_OK(producer_cluster_.mini_cluster_.get()->RestartSync());
  }
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
}

TEST_F(XClusterDDLReplicationTest, ExtensionRoleUpdating) {
  ASSERT_OK(SetUpClusters());
  auto& catalog_manager =
      ASSERT_RESULT(producer_cluster_.mini_cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  auto* xcluster_manager = catalog_manager.GetXClusterManagerImpl();
  const auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client(), namespace_name));
  auto* tserver = producer_cluster_.mini_cluster_->mini_tablet_server(0);
  auto& xcluster_context = tserver->server()->GetXClusterContext();
  auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));

  // We expect role NOT_AUTOMATIC_MODE here since no replication is set up yet.
  EXPECT_EQ(
      xcluster_context.GetXClusterRole(namespace_id),
      XClusterNamespaceInfoPB_XClusterRole_NOT_AUTOMATIC_MODE);

  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // The producer should have role AUTOMATIC_SOURCE after automatic mode replication is set up.
  // We should see this both at the TServer's xcluster_context and on the existing Postgres backend.
  {
    EXPECT_EQ(
        xcluster_context.GetXClusterRole(namespace_id),
        XClusterNamespaceInfoPB_XClusterRole_AUTOMATIC_SOURCE);
    std::string current_role = ASSERT_RESULT(
        conn.FetchRowAsString("SELECT yb_xcluster_ddl_replication.get_replication_role()"));
    EXPECT_EQ(current_role, "source");
  }

  // Manually change the role to AUTOMATIC_TARGET and verify the change is seen.
  ASSERT_OK(xcluster_manager->SetXClusterRole(
      catalog_manager.GetLeaderEpochInternal(), namespace_id,
      XClusterNamespaceInfoPB_XClusterRole_AUTOMATIC_TARGET));
  // TODO(mlillibridge): replace with a call to wait for heartbeats once that call is available.
  std::this_thread::sleep_for(30s);
  {
    EXPECT_EQ(
        xcluster_context.GetXClusterRole(namespace_id),
        XClusterNamespaceInfoPB_XClusterRole_AUTOMATIC_TARGET);
    std::string current_role = ASSERT_RESULT(
        conn.FetchRowAsString("SELECT yb_xcluster_ddl_replication.get_replication_role()"));
    EXPECT_EQ(current_role, "target");
  }

  ASSERT_OK(DeleteOutboundReplicationGroup());
  // TODO(mlillibridge): modify DeleteOutboundReplicationGroup() with a call to wait for heartbeats
  // once that call is available.
  std::this_thread::sleep_for(30s);

  // After replication is dropped, we should be back to role NOT_AUTOMATIC_MODE.
  EXPECT_EQ(
      xcluster_context.GetXClusterRole(namespace_id),
      XClusterNamespaceInfoPB_XClusterRole_NOT_AUTOMATIC_MODE);
}

TEST_F(XClusterDDLReplicationTest, TestExtensionDeletionWithMultipleReplicationGroups) {
  const xcluster::ReplicationGroupId kReplicationGroupId2("ReplicationGroup2");
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name, /*only_source=*/true));
  ASSERT_OK(
      CheckpointReplicationGroup(kReplicationGroupId2, /*require_no_bootstrap_needed=*/false));
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  // Ensure that xCluster DDL Replication system tables are not colocated.

  ASSERT_OK(SetUpClusters(/*is_colocated=*/true, /*start_yb_controller_servers=*/true));
  // Create a colocated table so that we can run xCluster setup.
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    RETURN_NOT_OK(CreateYsqlTable(
        /*idx=*/1, /*num_tablets=*/1, cluster, /*tablegroup_name=*/{}, /*colocated=*/true));
    return Status::OK();
  }));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace_name}));
  ASSERT_OK(BackupFromProducer());
  ASSERT_OK(RestoreToConsumer());
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

TEST_F(XClusterDDLReplicationTest, BootstrappingEmptyTable) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(SetUpClusters(/*is_colocated=*/false, /*start_yb_controller_servers=*/true));
  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));

  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace_name}));
  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client(), namespace_name));
  auto bootstrap_required =
      ASSERT_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, namespace_id));
  EXPECT_EQ(bootstrap_required, true);
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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

TEST_F(XClusterDDLReplicationTest, CreateTableInExistingConnection) {
  ASSERT_OK(SetUpClusters());
  {
    auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));

    ASSERT_OK(
        CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
    // Bootstrap here would have no effect because the database is empty so we skip it for the test.
    ASSERT_OK(CreateReplicationFromCheckpoint());

    // Here we create a table using a connection open before replication got set up.
    ASSERT_OK(conn.Execute("CREATE TABLE test_table_1 (key int PRIMARY KEY);"));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  }

  {
    auto conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    std::string row_count =
        ASSERT_RESULT(conn.FetchRowAsString("SELECT count(*) FROM test_table_1;"));
    // Check that the CREATE TABLE DDL got replicated.
    ASSERT_EQ(row_count, "0");
  }
}

TEST_F(XClusterDDLReplicationTest, CreateTableWithEnum) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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

TEST_F(XClusterDDLReplicationTest, NonconcurrentBackfills) {
  // Test commands that trigger nonconcurrent backfills.
  // Want to ensure that we don't trigger the backfill on the target, otherwise we may see duplicate
  // rows.
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const std::string kBaseTableName = "base_table";
  const std::string kColumn2Name = "a";
  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));

  // Create a base table.
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE TABLE $0($1 int PRIMARY KEY, $2 int);", kBaseTableName, kKeyColumnName,
      kColumn2Name));

  // Insert some rows.
  ASSERT_OK(p_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(1, 100) as i;", kBaseTableName));
  const auto producer_base_table_name = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kBaseTableName));
  {
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    auto producer_table = ASSERT_RESULT(GetProducerTable(producer_base_table_name));
    auto consumer_table = ASSERT_RESULT(GetConsumerTable(producer_base_table_name));
    ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
  }

  // Create index nonconcurrently.
  const auto kNonconcurrentIndex = "nonconcurrent_index";
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE INDEX NONCONCURRENTLY $0 ON $1($2 ASC)", kNonconcurrentIndex, kBaseTableName,
      kColumn2Name));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  // Verify index is replicated on consumer and has proper count of rows.
  const auto kCol2CountStmt = Format(
      "/*+ IndexScan($0) */ SELECT COUNT(*) FROM $1 WHERE $2 >= 0", kNonconcurrentIndex,
      kBaseTableName, kColumn2Name);
  ASSERT_EQ(ASSERT_RESULT(c_conn.FetchRow<int64_t>(kCol2CountStmt)), 100);

  // Ensure that we can also create a unique index nonconcurrently.
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE UNIQUE INDEX NONCONCURRENTLY ON $0($1)", kBaseTableName, kKeyColumnName));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Test adding a unique constraint, this will also trigger a nonconcurrent backfill.
  const auto kUniqueConstraintName = "unique_constraint";
  ASSERT_OK(p_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD CONSTRAINT $1 UNIQUE($2);", kBaseTableName, kUniqueConstraintName,
      kKeyColumnName));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  // Verify unique constraint is replicated on consumer.
  const auto kUniqueCountStmt = Format(
      "/*+ IndexScan($0) */ SELECT COUNT(*) FROM $1 WHERE $2 >= 0", kUniqueConstraintName,
      kBaseTableName, kKeyColumnName);
  ASSERT_EQ(ASSERT_RESULT(c_conn.FetchRow<int64_t>(kUniqueCountStmt)), 100);
}

TEST_F(XClusterDDLReplicationTest, NonconcurrentBackfillsWithPartitions) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const auto kPartitionedTableName = "partitioned_table";
  const auto kPartitionedIndexName = "partitioned_index";
  const std::string kColumn2Name = "a";
  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));

  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE TABLE $0 ($1 int PRIMARY KEY, $2 int) PARTITION BY RANGE ($1);",
      kPartitionedTableName, kKeyColumnName, kColumn2Name));
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE TABLE $0_p1 PARTITION OF $0 FOR VALUES FROM (0) TO (100);", kPartitionedTableName));
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE TABLE $0_p2 PARTITION OF $0 FOR VALUES FROM (100) TO (200);", kPartitionedTableName));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  // Insert some rows.
  ASSERT_OK(p_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2 FROM generate_series(51, 150) as i;", kPartitionedTableName));
  // Create partitioned index on the parent, this will cause nonconcurrent index creates on the
  // partitions. Make the table ranged so we can force an index scan later.
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE INDEX $0 ON $1($2 ASC);", kPartitionedIndexName, kPartitionedTableName,
      kColumn2Name));
  // Verify indexes on target.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  const auto kPartitionedIndexCountStmt = Format(
      "/*+ IndexScan($0) */ SELECT COUNT(*) FROM $1 WHERE $2 >= 0", kPartitionedIndexName,
      kPartitionedTableName, kColumn2Name);
  ASSERT_EQ(ASSERT_RESULT(c_conn.FetchRow<int64_t>(kPartitionedIndexCountStmt)), 100);

  // Also verify that we can create a unique index on the partitioned table.
  ASSERT_OK(p_conn.ExecuteFormat(
      "CREATE UNIQUE INDEX ON $0($1, $2);", kPartitionedTableName, kKeyColumnName, kColumn2Name));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
}

TEST_F(XClusterDDLReplicationTest, ExactlyOnceReplication) {
  // Test that DDLs are only replicated exactly once.
  const int kNumTablets = 3;

  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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

TEST_F(XClusterDDLReplicationTest, PauseTargetOnRepeatedFailures) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(p_conn.Execute("CREATE TABLE test_table_1 (key int PRIMARY KEY);"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Cause the target to fail the DDLs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = true;

  const auto alter_query = "ALTER TABLE test_table_1 RENAME TO test_table_2;";
  ASSERT_OK(p_conn.Execute(alter_query));

  // Replication should not continue. Wait till we see replication errors.
  ASSERT_OK(
      StringWaiterLogSink("DDL replication is paused due to repeated failures").WaitFor(kTimeout));

  // Stop failing DDLs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = false;

  // Replication should not resume until we recreate the ddl_queue poller.
  ASSERT_NOK(WaitForSafeTimeToAdvanceToNow());

  // Resume replication by pausing and unpausing, this will recreate the pollers.
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, false /* is_enabled */));
  SleepFor(MonoDelta::FromSeconds(1));
  ASSERT_OK(ToggleUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, true /* is_enabled */));

  // Replication should resume and rows should replicate.
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
  // Decrease number of iterations for slower build types.
  const int kNumIterations = (IsSanitizer() || kIsMac) ? 3 : 10;
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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

  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  const auto kNumTables = 3;

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
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

TEST_F(XClusterDDLReplicationTest, CreateColocatedIndexes) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto consumer_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));

  ASSERT_OK(producer_conn.ExecuteFormat(
      "CREATE TABLE $0 (key int PRIMARY KEY, a int, b text)", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2, i::text FROM generate_series(1, 100) as i;", kNewTableName));

  // Pause DDL replication to test that we handle the index data correctly.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = true;
  // Create index on column a and insert some more rows.
  ASSERT_OK(producer_conn.ExecuteFormat("CREATE INDEX ON $0(a DESC)", kNewTableName));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT i, i%2, i::text FROM generate_series(101, 200) as i;", kNewTableName));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = false;

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  auto producer_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  auto consumer_table = ASSERT_RESULT(GetConsumerTable(ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name*/ "", kNewTableName))));
  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));

  // Ensure that the index is correctly replicated.
  const auto kCol2CountStmt = Format("SELECT COUNT(*) FROM $0 WHERE a >= 0", kNewTableName);
  ASSERT_TRUE(ASSERT_RESULT(producer_conn.HasIndexScan(kCol2CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(consumer_conn.HasIndexScan(kCol2CountStmt)));

  // Test unique index on column b.
  ASSERT_OK(producer_conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0(b)", kNewTableName));
  // Test inserting duplicate value.
  ASSERT_NOK(producer_conn.ExecuteFormat("INSERT INTO $0 VALUES(-1, -1, '1');", kNewTableName));

  // Verify uniqueness constraint on consumer.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  // Bypass writes being blocked on target clusters.
  ASSERT_OK(consumer_conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = true"));
  ASSERT_NOK(consumer_conn.ExecuteFormat("INSERT INTO $0 VALUES(-1, -1, '1');", kNewTableName));
  ASSERT_OK(consumer_conn.ExecuteFormat("INSERT INTO $0 VALUES(-1, -1, '-1');", kNewTableName));
  ASSERT_OK(consumer_conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = false"));
}

TEST_F(XClusterDDLReplicationTest, CreateColocatedTableWithPause) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Fail the DDL apply, but let replication to the colocated tablet keep running.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;
  const auto kNewTableName = "new_colocated_table";

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(producer_conn.ExecuteFormat("CREATE TABLE $0 (key int primary key)", kNewTableName));
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  const auto kColocationId = 123456;

  // Pause any processing of DDLs so we can accumulate some pending schemas.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;

  // First fail creating a colocated table on the source.
  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
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
  // - 2 for the drop column (second part has the same schema, so doesn't get added)
  // After the DDLs run on the target:
  // - 3 for the create table
  // - 4 for the drop column
  // - 5 for the second part of the drop column
  EXPECT_EQ(consumer_table->schema().version(), 5);
}

TEST_F(XClusterDDLReplicationTest, CreateColocatedTableWithTargetFailures) {
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";

  // Allow DDLs through but fail them at the end of execution.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) = true;
  // Bump up the number of retries to ensure that we don't hit the limit.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_ddl_queue_max_retries_per_ddl) = 1000;

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(producer_conn.ExecuteFormat("CREATE TABLE $0 (key int primary key)", kNewTableName));
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
  // Also create an index as part of adding a unique index.
  ASSERT_OK(producer_conn.ExecuteFormat("ALTER TABLE $0 add column b int unique", kNewTableName2));
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto kNewTableName = "new_colocated_table";
  const auto kNumTables = 3;

  // Pause any processing of DDLs so we can accumulate some pending schemas.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup(/*is_colocated=*/true));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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

class XClusterDDLReplicationSwitchoverTest : public XClusterDDLReplicationTest {
 public:
  bool SetReplicationDirection(ReplicationDirection direction) override {
    if (XClusterDDLReplicationTest::SetReplicationDirection(direction)) {
      std::swap(cluster_A_, cluster_B_);
      return true;
    }
    return false;
  }

  Cluster* cluster_A_ = &producer_cluster_;
  Cluster* cluster_B_ = &consumer_cluster_;
  const xcluster::ReplicationGroupId kBackwardsReplicationGroupId =
      xcluster::ReplicationGroupId("backwards_replication");
};

TEST_F(XClusterDDLReplicationSwitchoverTest, SwitchoverWithWorkload) {
  // Set up replication from A to B.
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(ValidateReplicationRole(*cluster_A_, "source"));
  ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));

  // Create a table and write some rows, ensure that replication is setup correctly.
  uint32_t num_rows_written = 0;
  const auto kNumRecordsPerBatch = 10;
  auto table_name = ASSERT_RESULT(CreateYsqlTable(/*idx=*/1, /*num_tablets=*/3, cluster_A_));
  InsertRowsIntoProducerTableAndVerifyConsumer(
      table_name, num_rows_written, num_rows_written + kNumRecordsPerBatch);
  num_rows_written += kNumRecordsPerBatch;

  LOG(INFO) << "===== Beginning switchover: checkpoint B";
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(CheckpointReplicationGroup(
      kBackwardsReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  // B should still have the target role.
  ASSERT_OK(ValidateReplicationRole(*cluster_A_, "source"));
  ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));

  LOG(INFO) << "===== Switchover: test writes after checkpoint";
  SetReplicationDirection(ReplicationDirection::AToB);
  // Write to A should succeed and be replicated.
  InsertRowsIntoProducerTableAndVerifyConsumer(
      table_name, num_rows_written, num_rows_written + kNumRecordsPerBatch);
  num_rows_written += kNumRecordsPerBatch;
  // B should still disallow writes as it is still a target.
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(table_name, num_rows_written, num_rows_written + 1, cluster_B_),
      "Data modification is forbidden");

  LOG(INFO) << "===== Switchover: set up replication from B to A";
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(CreateReplicationFromCheckpoint(
      cluster_A_->mini_cluster_->GetMasterAddresses(), kBackwardsReplicationGroupId));
  // Both sides should now be targets and disallow writes.
  ASSERT_OK(ValidateReplicationRole(*cluster_A_, "target"));
  ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(table_name, num_rows_written, num_rows_written + 1, cluster_A_),
      "Data modification is forbidden");
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(table_name, num_rows_written, num_rows_written + 1, cluster_B_),
      "Data modification is forbidden");

  LOG(INFO) << "===== Continuing switchover: drop replication from A to B";
  SetReplicationDirection(ReplicationDirection::AToB);
  ASSERT_OK(DeleteOutboundReplicationGroup());

  LOG(INFO) << "===== Finishing switchover: wait for B to no longer be in readonly mode";
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
      ASSERT_RESULT(GetNamespaceId(producer_client())), /*is_read_only=*/false, cluster_B_));

  LOG(INFO) << "===== Switchover done";
  // Roles should match the new switchover state.
  ASSERT_OK(ValidateReplicationRole(*cluster_A_, "target"));
  ASSERT_OK(ValidateReplicationRole(*cluster_B_, "source"));

  // Ensure writes from B->A are now allowed and are replicated.
  SetReplicationDirection(ReplicationDirection::BToA);
  auto cluster_b_table = ASSERT_RESULT(GetProducerTable(ASSERT_RESULT(GetYsqlTable(
      cluster_B_, table_name.namespace_name(), table_name.pgschema_name(),
      table_name.table_name()))));
  InsertRowsIntoProducerTableAndVerifyConsumer(
      cluster_b_table->name(), num_rows_written, num_rows_written + kNumRecordsPerBatch,
      kBackwardsReplicationGroupId);
  // Writes on A should be blocked.
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(table_name, num_rows_written, num_rows_written + 1, cluster_A_),
      "Data modification is forbidden");
}

TEST_F(XClusterDDLReplicationSwitchoverTest, SwitchoverWithPendingDDL) {
  // Set up replication from A to B.
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(ValidateReplicationRole(*cluster_A_, "source"));
  ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));

  {
    auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE my_table (f INT)"));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    // Pause replication and perform additional DDLs to add some pending DDLs to the queue.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE my_table RENAME TO my_table2"));
    // TODO(#26028): Also handle create/drop table here, those need additional handling of the
    // outbound replication group state.
  }

  {
    LOG(INFO) << "===== Beginning switchover: checkpoint B";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_OK(CheckpointReplicationGroup(
        kBackwardsReplicationGroupId, /*require_no_bootstrap_needed=*/false));
    // B should still have the target role.
    ASSERT_OK(ValidateReplicationRole(*cluster_A_, "source"));
    ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));

    // This should fail since the schemas are not in sync.
    LOG(INFO) << "===== Switchover: fail to create replication from B to A";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_NOK_STR_CONTAINS(
        CreateReplicationFromCheckpoint(
            cluster_A_->mini_cluster_->GetMasterAddresses(), kBackwardsReplicationGroupId),
        "Could not find matching table");
    // Note that A will get marked as a target at this point.
    // TODO(#26160): reset A back to a source on replication failure.
    // TODO(mlillibridge): Add a call to wait for heartbeats to create replication from checkpoint
    // code once that call is available.
    std::this_thread::sleep_for(30s);
    ASSERT_OK(ValidateReplicationRole(*cluster_A_, "target"));
    ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));

    LOG(INFO) << "===== Switchover: unpause and wait for replication to catch up";
    SetReplicationDirection(ReplicationDirection::AToB);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = false;
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    LOG(INFO) << "===== Switchover: set up replication from B to A";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_OK(CreateReplicationFromCheckpoint(
        cluster_A_->mini_cluster_->GetMasterAddresses(), kBackwardsReplicationGroupId));
    // Both sides should be targets.
    ASSERT_OK(ValidateReplicationRole(*cluster_A_, "target"));
    ASSERT_OK(ValidateReplicationRole(*cluster_B_, "target"));

    LOG(INFO) << "===== Continuing switchover: drop replication from A to B";
    SetReplicationDirection(ReplicationDirection::AToB);
    ASSERT_OK(DeleteOutboundReplicationGroup());

    LOG(INFO) << "===== Finishing switchover: wait for B to no longer be in readonly mode";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
        ASSERT_RESULT(GetNamespaceId(producer_client())), /*is_read_only=*/false, cluster_B_));

    LOG(INFO) << "===== Switchover done";
    // Roles should match the new switchover state.
    ASSERT_OK(ValidateReplicationRole(*cluster_A_, "target"));
    ASSERT_OK(ValidateReplicationRole(*cluster_B_, "source"));
  }

  // Create a new table on B and ensure that it is replicated to A.
  {
    SetReplicationDirection(ReplicationDirection::BToA);
    auto conn = ASSERT_RESULT(cluster_B_->ConnectToDB(namespace_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE my_table3 (f INT)"));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  }

  // Verify that both sides have the same tables.
  const auto fetch_tables_query =
      "SELECT relname FROM pg_class WHERE relname LIKE '%my_table%' ORDER BY relname ASC;";
  std::string a_result, b_result;
  {
    auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    a_result = ASSERT_RESULT(conn.FetchAllAsString(fetch_tables_query, ", ", "\n"));
    LOG(INFO) << "tables on A:\n" << a_result;
  }
  {
    auto conn = ASSERT_RESULT(cluster_B_->ConnectToDB(namespace_name));
    b_result = ASSERT_RESULT(conn.FetchAllAsString(fetch_tables_query, ", ", "\n"));
    LOG(INFO) << "tables on B:\n" << b_result;
  }
  ASSERT_EQ(a_result, b_result);
}

TEST_F(XClusterDDLReplicationSwitchoverTest, SwitchoverWithPendingSequenceBump) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_sequence_cache_minval) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;

  const int kInitialSequenceValue = 7777700;

  // Set up replication from A to B.
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Create a sequence on A, let its creation replicate then bump it
  // 10 times but do not let the bumps replicate via pausing
  // replication.
  {
    auto conn_A = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    ASSERT_OK(
        conn_A.ExecuteFormat("CREATE SEQUENCE my_sequence START WITH $0;", kInitialSequenceValue));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    ASSERT_OK(ToggleUniverseReplication(
        consumer_cluster(), consumer_client(), kReplicationGroupId, /*is_enabled=*/false));

    // Consume 10 sequence values, bumping last_value to kInitialSequenceValue+9 on producer.
    for (int i = 0; i < 10; i++) {
      ASSERT_OK(conn_A.FetchRowAsString("SELECT pg_catalog.nextval('my_sequence');"));
    }
  }

  // Switch the replication direction unpausing the replication and
  // letting the bumps through in the middle.
  {
    LOG(INFO) << "===== Beginning switchover: checkpoint B";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_OK(CheckpointReplicationGroup(
        kBackwardsReplicationGroupId, /*require_no_bootstrap_needed=*/false));

    LOG(INFO) << "===== Switchover: set up replication from B to A";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_OK(CreateReplicationFromCheckpoint(
        cluster_A_->mini_cluster_->GetMasterAddresses(), kBackwardsReplicationGroupId));

    LOG(INFO) << "===== Resuming replication from A to B";
    SetReplicationDirection(ReplicationDirection::AToB);
    ASSERT_OK(ToggleUniverseReplication(
        consumer_cluster(), consumer_client(), kReplicationGroupId, /*is_enabled=*/true));

    LOG(INFO) << "===== Continuing switchover: drop replication from A to B";
    SetReplicationDirection(ReplicationDirection::AToB);
    ASSERT_OK(DeleteOutboundReplicationGroup());

    LOG(INFO) << "===== Finishing switchover: wait for B to no longer be in readonly mode";
    SetReplicationDirection(ReplicationDirection::BToA);
    ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
        ASSERT_RESULT(GetNamespaceId(producer_client())), /*is_read_only=*/false, cluster_B_));

    LOG(INFO) << "===== Switchover done";
  }

  // Finally verify that the bumps were not lost.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  {
    auto conn_A = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    auto final = ASSERT_RESULT(conn_A.FetchRow<int64_t>("SELECT last_value FROM my_sequence;"));
    EXPECT_EQ(final, kInitialSequenceValue + 9);
  }
  {
    auto conn_B = ASSERT_RESULT(cluster_B_->ConnectToDB(namespace_name));
    auto final = ASSERT_RESULT(conn_B.FetchRow<int64_t>("SELECT last_value FROM my_sequence;"));
    EXPECT_EQ(final, kInitialSequenceValue + 9);
  }
}

TEST_F(XClusterDDLReplicationSwitchoverTest, SwitchoverBumpsAboveUsedOids) {
  // To understand this test, it helps to picture the result of A->B replication before we do a
  // switchover.  The following is an example of the OID spaces of A and B for one database after A
  // has allocated three OIDs we don't care about preserving (the Ns) and one OID we do care about
  // preserving (P).  The [OID ptr]'s indicate where the next OID would be allocated in each space
  // modulo we skip OIDs already in use in that space on that universe.
  //
  // In particular, the next normal space OID that will be allocated on B is the one marked (*),
  // which conflicts with an OID already in use in cluster A.  While not a problem while B is a
  // target (targets only allocate in the secondary space), this will be a problem if we switch the
  // replication direction so B is now the source.
  //
  // Accordingly, xCluster is designed to bump up B's normal space [OID ptr] to after A's normal
  // space [OID ptr] as part of doing switchover; this test attempts to verify that that
  // successfully avoids the OID conflict problem described above.
  //
  //           A:                  B:
  //  Normal:
  //           N                [OID ptr] (*)
  //           N
  //           P                   P
  //           N
  //         [OID ptr]
  //
  //  Secondary:
  //         [OID ptr]             N
  //                               N
  //                               N
  //                            [OID ptr]

  // Limit how many OIDs we cache at a time so cache flushing doesn't consume too many OIDs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_oid_cache_prefetch_size) = 1;

  // Set up replication from A to B.
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Log information about OID reservations; search logs for (case insensitive) "reserve".
  google::SetVLOGLevel("catalog_manager*", 2);
  google::SetVLOGLevel("pg_client_service*", 2);

  // Use up a lot of pg_class OIDs in the normal OIDs space on A; we are using views here because
  // those are faster to create than tables when using xCluster.  These will be allocated on B in
  // the secondary space because it is a target.  We create several times more than the number of
  // OIDs cached to ensure cache flushing alone doesn't pass this test.
  uint32_t highest_normal_oid_used = 0;
  {
    auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE my_table (f INT)"));
    for (uint32_t i = 0; i < FLAGS_ysql_oid_cache_prefetch_size * 10; i++) {
      ASSERT_OK(conn.ExecuteFormat("CREATE VIEW my_view_$0 AS SELECT f FROM my_table", i));
    }
    // As a simplification here, we are ignoring OIDs of kinds other than pg_class.
    highest_normal_oid_used = ASSERT_RESULT(
        conn.FetchRow<pgwrapper::PGOid>("SELECT oid FROM pg_class ORDER BY oid DESC LIMIT 1"));
    LOG(INFO) << "Highest normal OID in use in universe A: " << highest_normal_oid_used;
  }
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Perform switchover from A->B to B->A.
  LOG(INFO) << "===== Beginning switchover: checkpoint B";
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(CheckpointReplicationGroup(
      kBackwardsReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  LOG(INFO) << "===== Switchover: set up replication from B to A";
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(CreateReplicationFromCheckpoint(
      cluster_A_->mini_cluster_->GetMasterAddresses(), kBackwardsReplicationGroupId));
  LOG(INFO) << "===== Continuing switchover: drop replication from A to B";
  SetReplicationDirection(ReplicationDirection::AToB);
  ASSERT_OK(DeleteOutboundReplicationGroup());
  LOG(INFO) << "===== Finishing switchover: wait for B to no longer be in readonly mode";
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
      ASSERT_RESULT(GetNamespaceId(producer_client())), /*is_read_only=*/false, cluster_B_));
  LOG(INFO) << "===== Switchover done";

  // Attempt to allocate pg_class OIDs on B in the normal space that we will want to preserve and
  // thus use the same OIDs on A.
  //
  // This will fail if the normal space OID counter on B was not bumped above the highest OID
  // already used in the normal space on A.  (Otherwise, the OID that B picks may already be in use
  // on A.)
  //
  // Note that the OID cache flush from switching OID allocation spaces itself will bump the OID
  // counter by the size of the OID cache; we require more bumping than that to make sure this test
  // is not passed just by the flushing.
  {
    auto conn = ASSERT_RESULT(cluster_B_->ConnectToDB(namespace_name));
    for (int i = 0; i < 20; i++) {
      ASSERT_OK(conn.ExecuteFormat("CREATE SEQUENCE my_sequence_$0", i));
    }

    uint32_t my_sequence_0_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        "SELECT oid FROM pg_class WHERE pg_class.relname = 'my_sequence_0'"));
    LOG(INFO) << "my_sequence_0's OID: " << my_sequence_0_oid;
    ASSERT_GT(my_sequence_0_oid, highest_normal_oid_used);
  }

  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
}

using XClusterDDLReplicationSetupTest = XClusterDDLReplicationSwitchoverTest;

TEST_F(XClusterDDLReplicationSetupTest, ReplicationSetUpBumpsOidCounter) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  // The resulting statement will consume 100 pg_enum OIDs.
  auto CreateGiantEnumStatement = [](std::string name) {
    std::string result = Format("CREATE TYPE $0 AS ENUM ('l0'", name);
    for (int i = 1; i < 100; i++) {
      result += Format(", 'l$0'", i);
    }
    return result + ");";
  };

  google::SetVLOGLevel("catalog_manager*", 1);
  google::SetVLOGLevel("pg_client_service*", 1);
  google::SetVLOGLevel("xcluster_source_manager", 1);
  // Cache only 30 OIDs at a time; see below for why this value was chosen.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_oid_cache_prefetch_size) = 30;

  ASSERT_OK(SetUpClusters(/*is_colocated=*/false, /*start_yb_controller_servers=*/true));

  // Create a giant enum on cluster A.
  {
    auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    ASSERT_OK(conn.Execute(CreateGiantEnumStatement("first_enum")));
    // Backup requires at least one table so create one.
    ASSERT_OK(conn.Execute("CREATE TABLE my_table (x INT);"));
  }

  // Reset OID counters on A by backing up then restoring the database on cluster A.
  ASSERT_OK(BackupFromProducer());
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(RestoreToConsumer());
  SetReplicationDirection(ReplicationDirection::AToB);

  // Allocate a few OIDs on A to force caching of normal space OIDs.
  {
    auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    ASSERT_OK(conn.Execute("CREATE TABLE exercise_cache (x INT);"));
  }

  // Set up xCluster replication with a nonempty database.
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace_name}));
  ASSERT_OK(BackupFromProducer());
  ASSERT_OK(RestoreToConsumer());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // At this point, in the absence of OID cache invalidation, we would still have OIDs cached on A
  // from before replication was set up.  (Replication setup does create some objects, consuming
  // OIDs, but not enough to exhaust the cache size of 30 we have set.)
  //
  // Importantly, we do not have enough OIDs cached to handle an entire giant enum.

  {
    // Drop via manual DDL replication the enum on only cluster A.
    auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
    ASSERT_OK(conn.Execute(R"(
                     SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1;
                     DROP TYPE first_enum;
                     SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=0;)"));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    // Now create a new giant enum via normal DDL replication.  If we did not bump up the normal
    // space OID counter and invalidate on A then this will cause a OID collision on cluster B
    // because the new enum on A will use OIDs freed up by dropping the previous enum but those OIDs
    // are still in use on B because the previous enum still exists there.
    ASSERT_OK(conn.Execute(CreateGiantEnumStatement("second_enum")));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  }
}

class XClusterDDLReplicationAddDropColumnTest : public XClusterDDLReplicationTest {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    XClusterDDLReplicationTest::SetUp();
    ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
    ASSERT_OK(CreateReplicationFromCheckpoint());
    producer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
    consumer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));

    auto consumer_namespace_id = ASSERT_RESULT(GetNamespaceId(consumer_client()));
    consumer_database_oid_ = ASSERT_RESULT(GetPgsqlDatabaseOid(consumer_namespace_id));
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

  Result<uint64_t> GetConsumerCatalogVersion() {
    return consumer_conn_->FetchRow<pgwrapper::PGUint64>(Format(
        "SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = $0",
        consumer_database_oid_));
  }

  Status WaitForCatalogVersionToPropagate() {
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto current_version = VERIFY_RESULT(GetConsumerCatalogVersion());
          if (current_version < last_consumer_catalog_version_) {
            return false;
          }
          last_consumer_catalog_version_ = current_version;
          return true;
        },
        kRpcTimeout * 1s, "Wait for new catalog version to propagate"));
    return Status::OK();
  }

  Status VerifyTargetData(bool is_paused) {
    RETURN_NOT_OK(WaitForCatalogVersionToPropagate());
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
    last_consumer_catalog_version_ = VERIFY_RESULT(GetConsumerCatalogVersion());

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

  uint32_t consumer_database_oid_;
  uint64_t last_consumer_catalog_version_ = 0;
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
  ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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
    ASSERT_OK(SetUpClustersAndCheckpointReplicationGroup());
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

TEST_F(XClusterDDLReplicationTest, BackupRestorePreservesEnumSortValue) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(SetUpClusters(/*is_colocated=*/false, /*start_yb_controller_servers=*/true));
  {
    auto conn = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));

    ASSERT_OK(conn->ExecuteFormat(R"(
        CREATE TYPE planets AS ENUM ( 'A', 'D' );
        ALTER TYPE planets ADD VALUE 'B' BEFORE 'D';
        ALTER TYPE planets ADD VALUE 'C' BEFORE 'D';
    )"));
    ASSERT_OK(conn->ExecuteFormat(R"(
        CREATE TABLE enum_table (c planets, PRIMARY KEY (c ASC));
        INSERT INTO enum_table (c) VALUES('D');
        INSERT INTO enum_table (c) VALUES('A');
    )"));
    // If we keep adding new nables before 'Z', we will run into enum label renumber
    // that is not yet supported in Yugabyte.
    ASSERT_OK(conn->ExecuteFormat(R"(
        CREATE TYPE overflow AS ENUM ( 'A', 'Z' );
        ALTER TYPE overflow ADD VALUE 'B' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'C' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'D' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'E' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'F' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'G' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'H' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'I' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'J' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'K' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'L' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'M' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'N' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'O' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'P' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'Q' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'R' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'S' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'T' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'U' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'V' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'W' BEFORE 'Z';
        ALTER TYPE overflow ADD VALUE 'X' BEFORE 'Z';
    )"));
    ASSERT_NOK_STR_CONTAINS(conn->ExecuteFormat(R"(
        ALTER TYPE overflow ADD VALUE 'Y' BEFORE 'Z';
    )"), "renumber enum labels is not yet supported");
    ASSERT_OK(conn->ExecuteFormat(R"(
        CREATE TYPE underflow AS ENUM ( 'A', 'Z' );
        ALTER TYPE underflow ADD VALUE 'Y' BEFORE 'Z';
        ALTER TYPE underflow ADD VALUE 'X' BEFORE 'Y';
        ALTER TYPE underflow ADD VALUE 'W' BEFORE 'X';
        ALTER TYPE underflow ADD VALUE 'V' BEFORE 'W';
        ALTER TYPE underflow ADD VALUE 'U' BEFORE 'V';
        ALTER TYPE underflow ADD VALUE 'T' BEFORE 'U';
        ALTER TYPE underflow ADD VALUE 'S' BEFORE 'T';
        ALTER TYPE underflow ADD VALUE 'R' BEFORE 'S';
        ALTER TYPE underflow ADD VALUE 'Q' BEFORE 'R';
        ALTER TYPE underflow ADD VALUE 'P' BEFORE 'Q';
        ALTER TYPE underflow ADD VALUE 'O' BEFORE 'P';
        ALTER TYPE underflow ADD VALUE 'N' BEFORE 'O';
        ALTER TYPE underflow ADD VALUE 'M' BEFORE 'N';
        ALTER TYPE underflow ADD VALUE 'L' BEFORE 'M';
        ALTER TYPE underflow ADD VALUE 'K' BEFORE 'L';
        ALTER TYPE underflow ADD VALUE 'J' BEFORE 'K';
        ALTER TYPE underflow ADD VALUE 'I' BEFORE 'J';
        ALTER TYPE underflow ADD VALUE 'H' BEFORE 'I';
        ALTER TYPE underflow ADD VALUE 'G' BEFORE 'H';
        ALTER TYPE underflow ADD VALUE 'F' BEFORE 'G';
        ALTER TYPE underflow ADD VALUE 'E' BEFORE 'F';
        ALTER TYPE underflow ADD VALUE 'D' BEFORE 'E';
        ALTER TYPE underflow ADD VALUE 'C' BEFORE 'D';
    )"));
    ASSERT_NOK_STR_CONTAINS(conn->ExecuteFormat(R"(
        ALTER TYPE underflow ADD VALUE 'B' BEFORE 'C';
    )"), "renumber enum labels is not yet supported");
  }

  auto GetEnumInfoAndOrderedRows =
      [&](Cluster& cluster) -> Result<std::pair<std::string, std::string>> {
    auto conn = VERIFY_RESULT(cluster.ConnectToDB(namespace_name));
    auto enum_info = Format("enum information:\n$0",
        VERIFY_RESULT(conn.FetchAllAsString(
        "SELECT typname, enumlabel, pg_enum.oid, enumsortorder FROM pg_enum "
        "JOIN pg_type ON pg_enum.enumtypid = pg_type.oid ORDER BY typname, enumlabel ASC;",
        ", ", "\n")));
    LOG(INFO) << enum_info;
    auto enum_table_c =
        VERIFY_RESULT(conn.FetchAllAsString(
        // WARNING: you need the enum_table.c here to avoid it referring to the result of c::text.
        "SELECT c::text FROM enum_table ORDER BY enum_table.c ASC;", ", ", "\n"));
    return std::make_pair(enum_info, enum_table_c);
  };

  std::string expected_enum_info;
  {
    auto [enum_info, rows] = ASSERT_RESULT(GetEnumInfoAndOrderedRows(producer_cluster_));
    expected_enum_info = enum_info;
    LOG(INFO) << "before we backup: " << rows;
  }

  // Backup then restore our database; in theory this should not affect anything.
  ASSERT_OK(BackupFromProducer());
  SetReplicationDirection(ReplicationDirection::BToA);
  ASSERT_OK(RestoreToConsumer());
  SetReplicationDirection(ReplicationDirection::AToB);

  {
    auto conn = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
    ASSERT_OK(conn->ExecuteFormat(R"(
        INSERT INTO enum_table (c) VALUES('C');
    )"));
  }

  // At this point, we have inserted D then A before the backup&restore then inserted C afterwards.
  {
    auto [enum_info, rows] = ASSERT_RESULT(GetEnumInfoAndOrderedRows(producer_cluster_));
    ASSERT_EQ(rows, "A\nC\nD");
    // Also check after restore the exact enum info (including enumsortorder) do not change.
    ASSERT_EQ(enum_info, expected_enum_info);
  }
}

}  // namespace yb
