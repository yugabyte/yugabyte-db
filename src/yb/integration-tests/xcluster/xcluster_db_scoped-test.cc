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

#include <gmock/gmock.h>

#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/xcluster_util.h"
#include "yb/integration-tests/xcluster/xcluster_test_utils.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"

DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_string(certs_for_cdc_dir);
DECLARE_bool(TEST_force_automatic_ddl_replication_mode);
DECLARE_bool(disable_xcluster_db_scoped_new_table_processing);
DECLARE_bool(xcluster_skip_health_check_on_replication_setup);
DECLARE_bool(ysql_enable_auto_analyze);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDBScopedTest : public XClusterYsqlTestBase {
 public:
  XClusterDBScopedTest() = default;
  ~XClusterDBScopedTest() = default;

  void SetUp() override {
    XClusterYsqlTestBase::SetUp();
  }

  std::vector<TableId> ExtractTableIds(const master::GetUniverseReplicationResponsePB& resp) {
    std::vector<TableId> results;
    for (const auto& table_id : resp.entry().tables()) {
      results.push_back(table_id);
    }
    return results;
  }

  Result<master::GetXClusterStreamsResponsePB> GetXClusterStreams(
      const NamespaceId& namespace_id, const std::vector<TableName>& table_names,
      const std::vector<PgSchemaName>& pg_schema_names) {
    std::promise<Result<master::GetXClusterStreamsResponsePB>> promise;
    client::XClusterClient remote_client(*producer_client());
    auto outbound_table_info = remote_client.GetXClusterStreams(
        CoarseMonoClock::Now() + kTimeout, kReplicationGroupId, namespace_id, table_names,
        pg_schema_names, [&promise](Result<master::GetXClusterStreamsResponsePB> result) {
          promise.set_value(std::move(result));
        });
    return promise.get_future().get();
  }

  Result<master::GetXClusterStreamsResponsePB> GetAllXClusterStreams(
      const NamespaceId& namespace_id) {
    return GetXClusterStreams(namespace_id, /*table_names=*/{}, /*pg_schema_names=*/{});
  }

  void VerifyRangedPartitionsWithIndex(bool is_colocated = false) {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));

    auto execute_on_both = [&p_conn, &c_conn](const std::string& stmt) -> Status {
      RETURN_NOT_OK(p_conn.Execute(stmt));
      return c_conn.Execute(stmt);
    };

    int64_t row_count = 20;
    ASSERT_OK(execute_on_both(Format(
        "CREATE TABLE demo (k int primary key, v text, d timestamp default clock_timestamp()) $0",
        is_colocated ? "WITH(colocation_id = 44444)" : "")));

    // Insert half of the rows before index creation and the rest after index creation.
    ASSERT_OK(p_conn.ExecuteFormat(
        "INSERT INTO demo(k,v) SELECT x,x FROM generate_series(1, $0) x", row_count / 2));

    ASSERT_OK(execute_on_both(Format(
        "CREATE INDEX ON demo(mod(yb_hash_code(k), 5) ASC, d) $0",
        is_colocated ? "WITH(colocation_id = 44445)" : "SPLIT AT VALUES ((2), (4))")));

    ASSERT_OK(execute_on_both(Format(
        "CREATE INDEX ON demo(v DESC) $0",
        is_colocated ? "WITH(colocation_id = 44446)" : "SPLIT AT VALUES (('10'))")));

    ASSERT_OK(p_conn.ExecuteFormat(
        "INSERT INTO demo(k,v) SELECT x,x FROM generate_series($0, $1) x", row_count / 2 + 1,
        row_count));

    auto count_index_rows =
        [&row_count](pgwrapper::PGConn& conn, const std::string& index_scan_stmt) -> Status {
      bool is_index_scan = VERIFY_RESULT(conn.HasIndexScan(index_scan_stmt));
      SCHECK(is_index_scan, IllegalState, "Query does not generate index scan: ", index_scan_stmt);
      auto rows = VERIFY_RESULT(conn.FetchRow<int64_t>(index_scan_stmt));
      SCHECK_EQ(
          rows, row_count, IllegalState,
          Format("Invalid number of rows in index scan: $0", index_scan_stmt));
      return Status::OK();
    };

    auto validate_rows = [&row_count, &count_index_rows](pgwrapper::PGConn& conn) -> Status {
      auto rows = VERIFY_RESULT(conn.FetchRow<int64_t>("SELECT count(1) FROM demo"));
      SCHECK_EQ(rows, row_count, IllegalState, "Invalid number of rows in demo table");

      RETURN_NOT_OK(count_index_rows(
          conn, "SELECT count(1) FROM demo WHERE mod(yb_hash_code(k), 5) in (0,1,2,3,4)"));

      RETURN_NOT_OK(count_index_rows(conn, "SELECT count(d) FROM demo WHERE d IS NOT NULL"));

      RETURN_NOT_OK(count_index_rows(conn, "SELECT count(v) FROM demo WHERE v > '0'"));
      return Status::OK();
    };

    ASSERT_OK(validate_rows(p_conn));

    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    ASSERT_OK(validate_rows(c_conn));
  }
};

TEST_F(XClusterDBScopedTest, TestCreateWithCheckpoint) {
  SetupParams param;
  param.num_producer_tablets = {};
  param.num_consumer_tablets = {};
  if (UseAutomaticMode()) {
    // Make sure we can connect sequence streams across different OIDs.
    param.use_different_database_oids = true;
    namespace_name = "db_with_different_oids";
  }
  ASSERT_OK(SetUpClusters(param));

  ASSERT_NOK_STR_CONTAINS(
      CheckpointReplicationGroup(),
      "Database should have at least one table in order to be part of xCluster replication");

  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/0, /*num_tablets=*/3, &producer_cluster_));
  ASSERT_OK(producer_client()->OpenTable(producer_table_name, &producer_table_));

  ASSERT_OK(CheckpointReplicationGroup());

  ASSERT_OK(InsertRowsInProducer(0, 50));

  ASSERT_NOK(CreateReplicationFromCheckpoint("bad-master-addr"));
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  ASSERT_NOK_STR_CONTAINS(CreateReplicationFromCheckpoint(), "Could not find matching table");
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  auto consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/0, /*num_tablets=*/3, &consumer_cluster_));
  ASSERT_OK(consumer_client()->OpenTable(consumer_table_name, &consumer_table_));

  auto consumer_extra_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));

  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(),
      "has additional tables that were not added to xCluster DB Scoped replication group");
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  ASSERT_OK(DropYsqlTable(
      &consumer_cluster_, consumer_extra_table_name.namespace_name(),
      consumer_extra_table_name.pgschema_name(), consumer_extra_table_name.table_name()));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  // Verify the groups shows up in GetUniverseReplications and GetUniverseReplicationInfo client
  // APIs.
  auto target_xcluster_client = client::XClusterClient(*consumer_client());
  auto replication_groups = ASSERT_RESULT(target_xcluster_client.GetUniverseReplications(""));
  ASSERT_EQ(replication_groups.size(), 1);
  ASSERT_EQ(replication_groups.front(), kReplicationGroupId);
  replication_groups = ASSERT_RESULT(
      target_xcluster_client.GetUniverseReplications(consumer_table_->name().namespace_id()));
  ASSERT_EQ(replication_groups.size(), 1);
  ASSERT_EQ(replication_groups.front(), kReplicationGroupId);
  auto replication_info =
      ASSERT_RESULT(target_xcluster_client.GetUniverseReplicationInfo(kReplicationGroupId));
  ASSERT_EQ(replication_info.replication_type, XClusterReplicationType::XCLUSTER_YSQL_DB_SCOPED);
  ASSERT_EQ(replication_info.db_scope_namespace_id_map.size(), 1);
  const auto& source_namespace_id = producer_table_->name().namespace_id();
  const auto& target_namespace_id = consumer_table_->name().namespace_id();
  EXPECT_THAT(
      replication_info.db_scope_namespace_id_map,
      testing::Contains(testing::Key(target_namespace_id)));
  ASSERT_EQ(replication_info.db_scope_namespace_id_map[target_namespace_id], source_namespace_id);
  ASSERT_EQ(replication_info.table_infos.size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : replication_info.table_infos) {
    if (table_info.source_table_id == producer_table_->id() &&
        table_info.target_table_id == consumer_table_->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find normal table in replication_info.table_infos";

  if (UseAutomaticMode()) {
    // In automatic mode, sequences_data should have been created on the target universe.
    ASSERT_TRUE(ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                    ->catalog_manager_impl()
                    .GetTableInfo(kPgSequencesDataTableId));
  }

  ASSERT_OK(InsertRowsInProducer(50, 100));

  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterDBScopedTest, CreateTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Creating a new table on target first should fail.
  ASSERT_NOK_STR_CONTAINS(
      CreateYsqlTable(
          /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_),
      "Table public.test_table_1 not found");

  auto new_producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> new_producer_table;
  ASSERT_OK(producer_client()->OpenTable(new_producer_table_name, &new_producer_table));

  ASSERT_OK(InsertRowsInProducer(0, 50, new_producer_table));

  auto new_consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> new_consumer_table;
  ASSERT_OK(consumer_client()->OpenTable(new_consumer_table_name, &new_consumer_table));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 2 + OverheadStreamsCount());

  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the other table remains unchanged.
  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));
}

TEST_F(XClusterDBScopedTest, DropTableOnProducerThenConsumer) {
  // Drop bg task timer to speed up test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  // Setup replication with two tables
  SetupParams params;
  params.num_consumer_tablets = params.num_producer_tablets = {3, 3};
  ASSERT_OK(SetUpClusters(params));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Perform the drop on producer cluster.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Perform the drop on consumer cluster. This will also delete the replication stream.
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));

  ASSERT_OK(WaitForTableToFullyDelete(producer_cluster_, producer_table_->name(), kTimeout));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));

  auto result = GetXClusterStreams(
      namespace_id, {producer_table_->name().table_name()},
      {producer_table_->name().pgschema_name()});
  ASSERT_NOK(result) << result->DebugString();
  ASSERT_STR_CONTAINS(result.status().ToString(), "test_table_0 not found in namespace");

  auto get_streams_resp = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(get_streams_resp.table_infos_size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : get_streams_resp.table_infos()) {
    if (table_info.table_id() == producer_tables_[1]->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find producer table in get_streams_resp.table_infos";
}

// Test dropping all tables and then creating new tables.
TEST_F(XClusterDBScopedTest, DropAllTables) {
  // Drop bg task timer to speed up test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  // Setup replication with one table
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Drop the table.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));

  ASSERT_OK(WaitForTableToFullyDelete(producer_cluster_, producer_table_->name(), kTimeout));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  auto outbound_streams = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(outbound_streams.table_infos_size(), 0 + OverheadStreamsCount());

  auto resp = ASSERT_RESULT(GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_EQ(resp.entry().tables_size(), 0 + OverheadStreamsCount());

  // Add a new table.
  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table2));

  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));
}

TEST_F(XClusterDBScopedTest, ColocatedDB) {
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;

  // Create clusters with colocated database, and 1 non-colocated table.
  ASSERT_OK(SetUpClusters(param));

  ASSERT_NOK_STR_CONTAINS(
      CheckpointReplicationGroup(),
      "Colocated database should have at least one colocated table in order to be part of "
      "xCluster replication");

  auto producer_colocated_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> producer_colocated_table;
  ASSERT_OK(producer_client()->OpenTable(producer_colocated_table_name, &producer_colocated_table));

  ASSERT_OK(CheckpointReplicationGroup());

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(InsertRowsInProducer(0, 50, producer_colocated_table));

  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(),
      "Could not find matching table for colocated_db.test_table_1");
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  auto consumer_colocated_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> consumer_colocated_table;
  ASSERT_OK(consumer_client()->OpenTable(consumer_colocated_table_name, &consumer_colocated_table));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table_name, consumer_colocated_table_name));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table, consumer_colocated_table));

  // Make sure we only colocated parent table and one non-colocated table
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 2 + OverheadStreamsCount());

  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table2));

  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));

  auto producer_colocated_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/3, /*num_tablets=*/3, &producer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> producer_colocated_table2;
  ASSERT_OK(
      producer_client()->OpenTable(producer_colocated_table2_name, &producer_colocated_table2));
  ASSERT_OK(InsertRowsInProducer(0, 50, producer_colocated_table2));

  auto consumer_colocated_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/3, /*num_tablets=*/3, &consumer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> consumer_colocated_table2;
  ASSERT_OK(
      consumer_client()->OpenTable(consumer_colocated_table2_name, &consumer_colocated_table2));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table2, consumer_colocated_table2));

  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_colocated_table));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_colocated_table));

  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 3 + OverheadStreamsCount());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(10, 20, producer_table_));
  ASSERT_OK(InsertRowsInProducer(50, 100, producer_table2));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the other table remains unchanged.
  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table2, consumer_colocated_table2));

  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table2));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table2));

  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 2 + OverheadStreamsCount());
}

// When disable_xcluster_db_scoped_new_table_processing is set make sure we do not checkpoint new
// tables or add them to replication.
TEST_F(XClusterDBScopedTest, DisableAutoTableProcessing) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_xcluster_db_scoped_new_table_processing) = true;

  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Creating a new table on target first should succeed.
  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  auto get_streams_resp = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(get_streams_resp.table_infos_size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : get_streams_resp.table_infos()) {
    if (table_info.table_id() == producer_table_->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find producer table in get_streams_resp.table_infos";

  ASSERT_OK(InsertRowsInProducer(0, 100, producer_table2));
  ASSERT_NOK(VerifyWrittenRecords(producer_table2, consumer_table2));

  // Reenable the flag and make sure new table is added to replication.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_xcluster_db_scoped_new_table_processing) = false;

  auto producer_table3_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table3;
  ASSERT_OK(producer_client()->OpenTable(producer_table3_name, &producer_table3));
  ASSERT_OK(InsertRowsInProducer(0, 100, producer_table3));

  auto consumer_table3_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table3;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table3_name, &consumer_table3));

  ASSERT_OK(VerifyWrittenRecords(producer_table3, consumer_table3));
}

class XClusterDBScopedTestWithTwoDBs : public XClusterDBScopedTest {
 public:
  Status SetUpClusters(SetupParams params = {}) {
    RETURN_NOT_OK(XClusterYsqlTestBase::SetUpClusters(params));

    RETURN_NOT_OK(RunOnBothClusters([this](Cluster* cluster) -> Status {
      RETURN_NOT_OK(CreateDatabase(cluster, namespace_name2_));
      auto table_name = VERIFY_RESULT(CreateYsqlTable(
          cluster, namespace_name2_, "" /* schema_name */, namespace2_table_name_,
          /*tablegroup_name=*/std::nullopt, /*num_tablets=*/3));

      std::shared_ptr<client::YBTable> table;
      RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
      cluster->tables_.emplace_back(std::move(table));

      return Status::OK();
    }));

    source_namespace2_table_ = producer_tables_.back();
    target_namespace2_table_ = consumer_tables_.back();
    source_namespace2_id_ =
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*producer_client(), namespace_name2_));
    target_namespace2_id_ =
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*consumer_client(), namespace_name2_));

    return Status::OK();
  }

  void TestAddRemoveNamespace();

  const NamespaceName namespace_name2_ = "db2";
  const TableName namespace2_table_name_ = "test_table";
  NamespaceId source_namespace2_id_, target_namespace2_id_;
  std::shared_ptr<client::YBTable> source_namespace2_table_, target_namespace2_table_;
};

// Testing adding and removing namespaces to replication.
void XClusterDBScopedTestWithTwoDBs::TestAddRemoveNamespace() {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  // Bootstrap here would have no effect because the database is empty so we skip it even if
  // CheckpointReplicationGroup said it was required.
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto source_xcluster_client = client::XClusterClient(*producer_client());

  // Add the namespace to the source replication group.
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));

  auto bootstrap_required =
      ASSERT_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_EQ(bootstrap_required, UseAutomaticMode());
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.

  // Validate streams on source.
  auto streams = ASSERT_RESULT(GetAllXClusterStreams(source_namespace2_id_));
  ASSERT_EQ(streams.table_infos_size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : streams.table_infos()) {
    if (table_info.table_name() == namespace2_table_name_ &&
        table_info.table_id() == source_namespace2_table_->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find source_namespace2_table in streams.table_infos";

  // Add the namespace to the target.
  ASSERT_OK(AddNamespaceToXClusterReplication(source_namespace2_id_, target_namespace2_id_));

  // Validate streams on target.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  EXPECT_EQ(resp.entry().tables_size(), 2 + 2 * OverheadStreamsCount());

  auto replication_info = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                              ->catalog_manager_impl()
                              .GetUniverseReplication(kReplicationGroupId);
  ASSERT_TRUE(replication_info);
  ASSERT_TRUE(replication_info->IsDbScoped());
  ASSERT_EQ(replication_info->LockForRead()->pb.db_scoped_info().namespace_infos_size(), 2);

  ASSERT_OK(InsertRowsInProducer(0, 100, source_namespace2_table_));
  ASSERT_OK(VerifyWrittenRecords(source_namespace2_table_, target_namespace2_table_));

  // Remove the namespace from both sides.
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_, target_master_address));

  // Check the target side.
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  // Only the first table should be left.
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  replication_info = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                         ->catalog_manager_impl()
                         .GetUniverseReplication(kReplicationGroupId);
  ASSERT_TRUE(replication_info);
  ASSERT_EQ(replication_info->LockForRead()->pb.db_scoped_info().namespace_infos_size(), 1);

  // Check the source side.
  auto streams_result = GetAllXClusterStreams(source_namespace2_id_);
  ASSERT_NOK_STR_CONTAINS(streams_result, "Not found");

  // Checkpoint the namespace again and make sure it now requires bootstrap.
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));

  bootstrap_required =
      ASSERT_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_TRUE(bootstrap_required) << "Bootstrap should be required";
}

TEST_F(XClusterDBScopedTestWithTwoDBs, AddRemoveNamespace) {
  ASSERT_NO_FATALS(TestAddRemoveNamespace());
}

class XClusterDBScopedTestWithTwoDBsAutomaticDDLMode : public XClusterDBScopedTestWithTwoDBs {
 public:
  bool UseAutomaticMode() override { return true; }
};

TEST_F(XClusterDBScopedTestWithTwoDBsAutomaticDDLMode, AddRemoveNamespace) {
  ASSERT_NO_FATALS(TestAddRemoveNamespace());
}

// Remove a namespaces from replication when the target side is down.
TEST_F_EX(XClusterDBScopedTest, RemoveNamespaceWhenTargetIsDown, XClusterDBScopedTestWithTwoDBs) {
  // Setup replication with both databases.
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(AddNamespaceToXClusterReplication(source_namespace2_id_, target_namespace2_id_));

  ASSERT_OK(InsertRowsInProducer(0, 100, source_namespace2_table_));
  ASSERT_OK(VerifyWrittenRecords(source_namespace2_table_, target_namespace2_table_));

  // Take down the target.
  consumer_cluster()->StopSync();

  // Remove the namespace from source side.
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_, /*target_master_addresses=*/""));

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace2_id_), "Not found");

  // Bring the target back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    ASSERT_OK(consumer_cluster()->StartSync());
  }

  // It should still have both namespaces.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  EXPECT_EQ(resp.entry().tables_size(), 2 + 2 * OverheadStreamsCount());

  auto target_xcluster_client = client::XClusterClient(*consumer_client());

  // Make sure universe uuid is checked.
  ASSERT_NOK_STR_CONTAINS(
      target_xcluster_client.RemoveNamespaceFromUniverseReplication(
          kReplicationGroupId, source_namespace2_id_, UniverseUuid::GenerateRandom()),
      "Invalid Universe UUID");

  ASSERT_OK(target_xcluster_client.RemoveNamespaceFromUniverseReplication(
      kReplicationGroupId, source_namespace2_id_, UniverseUuid::Nil()));
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));
}

// Remove a namespaces from replication when the source side is down.
TEST_F_EX(XClusterDBScopedTest, RemoveNamespaceWhenSourceIsDown, XClusterDBScopedTestWithTwoDBs) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(AddNamespaceToXClusterReplication(source_namespace2_id_, target_namespace2_id_));

  ASSERT_OK(InsertRowsInProducer(0, 100, source_namespace2_table_));
  ASSERT_OK(VerifyWrittenRecords(source_namespace2_table_, target_namespace2_table_));

  // Take down the source.
  producer_cluster()->StopSync();

  // Remove replication from target and verify.
  auto target_xcluster_client = client::XClusterClient(*consumer_client());
  ASSERT_OK(target_xcluster_client.RemoveNamespaceFromUniverseReplication(
      kReplicationGroupId, source_namespace2_id_, UniverseUuid::Nil()));

  // The replication group is unhealthy since source is down.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = true;

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  // Bring the source back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    ASSERT_OK(producer_cluster()->StartSync());
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = false;

  // Source should still have the namespace and stream.
  auto streams = ASSERT_RESULT(GetAllXClusterStreams(source_namespace2_id_));
  ASSERT_EQ(streams.table_infos_size(), 1 + OverheadStreamsCount());

  // Remove the namespace from source side.
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_, /*target_master_addresses=*/""));

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace2_id_), "Not found");
}

// Delete replication from both sides using one command.
TEST_F(XClusterDBScopedTest, Delete) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Delete from both sides.
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, target_master_address));

  auto source_namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));

  // Running the same command again should fail.
  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");

  auto replication_info = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                              ->catalog_manager_impl()
                              .GetUniverseReplication(kReplicationGroupId);
  ASSERT_FALSE(replication_info);
}

// Delete replication when the target side is down.
TEST_F(XClusterDBScopedTest, DeleteWhenTargetIsDown) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Take down the target.
  consumer_cluster()->StopSync();

  // Delete only from source.
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/""));

  auto source_namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");

  // Bring the target back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    ASSERT_OK(consumer_cluster()->StartSync());
  }

  // Target should still have the replication group.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  EXPECT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());

  auto target_xcluster_client = client::XClusterClient(*consumer_client());

  // Make sure the universe uuid is checked.
  ASSERT_NOK_STR_CONTAINS(
      target_xcluster_client.DeleteUniverseReplication(
          kReplicationGroupId, /*ignore_errors=*/true, UniverseUuid::GenerateRandom()),
      "Invalid Universe UUID");

  // Delete from the target.
  ASSERT_OK(target_xcluster_client.DeleteUniverseReplication(
      kReplicationGroupId, /*ignore_errors=*/true, /*target_universe_uuid=*/UniverseUuid::Nil()));

  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");
}

// Delete replication when the source side is down.
TEST_F(XClusterDBScopedTest, DeleteWhenSourceIsDown) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Take down the source.
  producer_cluster()->StopSync();

  // Delete from the target.
  auto target_xcluster_client = client::XClusterClient(*consumer_client());
  ASSERT_OK(target_xcluster_client.DeleteUniverseReplication(
      kReplicationGroupId, /*ignore_errors=*/true, /*target_universe_uuid=*/UniverseUuid::Nil()));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");

  // Bring the source back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    ASSERT_OK(producer_cluster()->StartSync());
  }

  auto source_namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  // Source should still have the replication group and streams.
  auto streams = ASSERT_RESULT(GetAllXClusterStreams(source_namespace_id));
  ASSERT_EQ(streams.table_infos_size(), 1 + OverheadStreamsCount());

  auto source_xcluster_client = client::XClusterClient(*producer_client());

  // Delete from the source.
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/""));

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");
}

// Validate that we can only have one inbound replication group per database.
TEST_F(XClusterDBScopedTest, MultipleInboundReplications) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto group2 = xcluster::ReplicationGroupId("group2");

  ASSERT_OK(CheckpointReplicationGroup(group2));
  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(/*target_master_addresses=*/"", group2),
      "already included in replication group");
}

TEST_F_EX(XClusterDBScopedTest, TestYbAdmin, XClusterDBScopedTestWithTwoDBsAutomaticDDLMode) {
  ASSERT_OK(SetUpClusters());

  // Create replication with 1 db.
  auto result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "create_xcluster_checkpoint", kReplicationGroupId, namespace_name,
      "automatic_ddl_mode"));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");

  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "is_xcluster_bootstrap_required", kReplicationGroupId, namespace_name));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.

  const auto target_master_address = consumer_cluster()->GetMasterAddresses();
  ASSERT_OK(CallAdmin(
      producer_cluster(), "setup_xcluster_replication", kReplicationGroupId,
      target_master_address));

  // The extension should exist on both sides with all the tables.
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name));

  result =
      ASSERT_RESULT(CallAdmin(producer_cluster(), "list_xcluster_outbound_replication_groups"));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  const auto source_namespace_id = producer_table_->name().namespace_id();
  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "list_xcluster_outbound_replication_groups", source_namespace_id));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "get_xcluster_outbound_replication_group_info",
      kReplicationGroupId.ToString()));
  ASSERT_STR_CONTAINS(result, source_namespace_id);
  ASSERT_STR_CONTAINS(result, producer_table_->id());
  ASSERT_STR_NOT_CONTAINS(result, source_namespace2_id_);
  ASSERT_STR_NOT_CONTAINS(result, source_namespace2_table_->id());

  // Test target side commands.
  const auto target_namespace_id = consumer_table_->name().namespace_id();
  result = ASSERT_RESULT(CallAdmin(consumer_cluster(), "list_universe_replications", "na"));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(
      CallAdmin(consumer_cluster(), "list_universe_replications", target_namespace2_id_));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(
      CallAdmin(consumer_cluster(), "list_universe_replications", target_namespace_id));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "get_universe_replication_info", kReplicationGroupId.ToString()));
  ASSERT_STR_CONTAINS(result, xcluster::ShortReplicationType(XCLUSTER_YSQL_DB_SCOPED));
  ASSERT_STR_CONTAINS(result, namespace_name);
  ASSERT_STR_CONTAINS(result, target_namespace_id);
  ASSERT_STR_CONTAINS(result, source_namespace_id);
  ASSERT_STR_NOT_CONTAINS(result, target_namespace2_id_);

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Add second db to replication.
  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_checkpoint", kReplicationGroupId,
      namespace_name2_));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.

  ASSERT_OK(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_replication", kReplicationGroupId,
      namespace_name2_, target_master_address));

  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "get_xcluster_outbound_replication_group_info",
      kReplicationGroupId.ToString()));
  ASSERT_STR_CONTAINS(result, namespace_name);
  ASSERT_STR_CONTAINS(result, producer_table_->id());
  ASSERT_STR_CONTAINS(result, namespace_name2_);
  ASSERT_STR_CONTAINS(result, source_namespace2_table_->id());

  // Remove database from both sides with one command.
  ASSERT_OK(CallAdmin(
      producer_cluster(), "remove_namespace_from_xcluster_replication", kReplicationGroupId,
      namespace_name2_, target_master_address));

  // Remove database from replication from each cluster individually.
  ASSERT_OK(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_checkpoint", kReplicationGroupId,
      namespace_name2_));
  ASSERT_OK(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_replication", kReplicationGroupId,
      namespace_name2_, target_master_address));
  ASSERT_OK(CallAdmin(
      consumer_cluster(), "alter_universe_replication", kReplicationGroupId, "remove_namespace",
      namespace_name2_));
  ASSERT_OK(CallAdmin(
      producer_cluster(), "remove_namespace_from_xcluster_replication", kReplicationGroupId,
      namespace_name2_));

  // Drop replication on both sides.
  ASSERT_OK(CallAdmin(
      producer_cluster(), "drop_xcluster_replication", kReplicationGroupId, target_master_address));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name));

  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "create_xcluster_checkpoint", kReplicationGroupId, namespace_name,
      "automatic_ddl_mode"));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");
}

// Make sure we can setup replication with hidden tables.
TEST_F(XClusterDBScopedTest, CreateReplicationWithHiddenTables) {
  ASSERT_OK(SetUpClusters());
  // Setup PITR schedule so that dropped tables are hidden.
  ASSERT_OK(EnablePITROnClusters());

  // Create and drop a table to create a hidden table.
  auto table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/1, &producer_cluster_));
  std::shared_ptr<client::YBTable> new_table;
  ASSERT_OK(producer_client()->OpenTable(table_name, &new_table));
  const auto hidden_table_id = new_table->id();

  auto& catalog_mgr = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->catalog_manager();
  auto table = catalog_mgr.GetTableInfo(hidden_table_id);
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->LockForRead()->visible_to_client());

  ASSERT_OK(DropYsqlTable(
      &producer_cluster_, table_name.namespace_name(), table_name.pgschema_name(),
      table_name.table_name()));
  ASSERT_NOK(producer_client()->OpenTable(table_name, &new_table));

  ASSERT_FALSE(table->LockForRead()->visible_to_client());

  // Setup replication and make sure it is healthy.
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));
  ASSERT_THAT(ExtractTableIds(resp), testing::Not(testing::Contains(hidden_table_id)));

  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the hidden table is still there.
  ASSERT_TRUE(table->LockForRead()->is_hidden_but_not_deleting());
}

// Create and drop tables in a loop with PITR which will keep the dropped tables in hidden state.
TEST_F(XClusterDBScopedTest, CreateDropTablesWithPITR) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(EnablePITROnClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  for (int i = 0; i < 5; i++) {
    auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
        /*idx=*/1, /*num_tablets=*/1, &producer_cluster_));
    std::shared_ptr<client::YBTable> new_producer_table;
    ASSERT_OK(producer_client()->OpenTable(producer_table_name, &new_producer_table));

    auto consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
        /*idx=*/1, /*num_tablets=*/1, &consumer_cluster_));
    std::shared_ptr<client::YBTable> new_consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(consumer_table_name, &new_consumer_table));

    ASSERT_OK(InsertRowsInProducer(0, 10, new_producer_table));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));

    ASSERT_OK(DropYsqlTable(producer_cluster_, *new_producer_table.get()));
    ASSERT_OK(DropYsqlTable(consumer_cluster_, *new_consumer_table.get()));
  }

  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterDBScopedTest, RangedPartitionsWithIndex) {
  // Disable auto analyze becauses the query plan changes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_NO_FATALS(VerifyRangedPartitionsWithIndex(/*is_colocated=*/false));
}

TEST_F(XClusterDBScopedTest, ColocatedRangedPartitionsWithIndex) {
  // Disable auto analyze becauses the query plan changes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;

  // Create clusters with colocated database, and 1 non-colocated table.
  ASSERT_OK(SetUpClusters(param));

  ASSERT_OK(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/1, &producer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  ASSERT_OK(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/1, &consumer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_NO_FATALS(VerifyRangedPartitionsWithIndex(/*is_colocated=*/true));
}
}  // namespace yb
