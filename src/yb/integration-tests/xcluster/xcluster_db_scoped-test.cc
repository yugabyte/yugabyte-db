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

#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

DECLARE_bool(enable_xcluster_api_v2);
DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_string(certs_for_cdc_dir);
DECLARE_bool(disable_xcluster_db_scoped_new_table_processing);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDBScopedTest : public XClusterYsqlTestBase {
 public:
  XClusterDBScopedTest() = default;
  ~XClusterDBScopedTest() = default;

  virtual void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_xcluster_api_v2) = true;
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
};

TEST_F(XClusterDBScopedTest, TestCreateWithCheckpoint) {
  SetupParams param;
  param.num_producer_tablets = {};
  param.num_consumer_tablets = {};
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

  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(), "Could not find matching table for yugabyte.test_table_0");

  auto consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/0, /*num_tablets=*/3, &consumer_cluster_));
  ASSERT_OK(producer_client()->OpenTable(consumer_table_name, &consumer_table_));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

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
  ASSERT_EQ(resp.entry().tables_size(), 2);

  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the other table remains unchanged.
  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));
}

TEST_F(XClusterDBScopedTest, DropTableOnProducerThenConsumer) {
  // Setup replication with two tables
  SetupParams params;
  params.num_consumer_tablets = params.num_producer_tablets = {3, 3};
  ASSERT_OK(SetUpClusters(params));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Perform the drop on producer cluster.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Perform the drop on consumer cluster.
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));

  ASSERT_OK(WaitForTableToFullyDelete(producer_cluster_, producer_table_->name(), kTimeout));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));

  auto result = GetXClusterStreams(
      namespace_id, {producer_table_->name().table_name()},
      {producer_table_->name().pgschema_name()});
  ASSERT_NOK(result) << result->DebugString();
  ASSERT_STR_CONTAINS(result.status().ToString(), "test_table_0 not found in namespace");

  auto get_streams_resp = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(get_streams_resp.table_infos_size(), 1);
  ASSERT_EQ(get_streams_resp.table_infos(0).table_id(), producer_tables_[1]->id());
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
  ASSERT_EQ(outbound_streams.table_infos_size(), 0);

  auto resp = ASSERT_RESULT(GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_EQ(resp.entry().tables_size(), 0);

  // Add a new table.
  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table2));

  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(producer_client()->OpenTable(consumer_table2_name, &consumer_table2));

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
      /*tablegroup_name=*/boost::none, /*colocated=*/true));
  std::shared_ptr<client::YBTable> producer_colocated_table;
  ASSERT_OK(producer_client()->OpenTable(producer_colocated_table_name, &producer_colocated_table));

  ASSERT_OK(CheckpointReplicationGroup());

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(InsertRowsInProducer(0, 50, producer_colocated_table));

  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(),
      "Could not find matching table for colocated_db.test_table_1");

  auto consumer_colocated_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_,
      /*tablegroup_name=*/boost::none, /*colocated=*/true));
  std::shared_ptr<client::YBTable> consumer_colocated_table;
  ASSERT_OK(consumer_client()->OpenTable(consumer_colocated_table_name, &consumer_colocated_table));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table_name, consumer_colocated_table_name));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table, consumer_colocated_table));

  // Make sure we only colocated parent table and one non-colocated table
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 2);

  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table2));

  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(producer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));

  auto producer_colocated_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/3, /*num_tablets=*/3, &producer_cluster_,
      /*tablegroup_name=*/boost::none, /*colocated=*/true));
  std::shared_ptr<client::YBTable> producer_colocated_table2;
  ASSERT_OK(
      producer_client()->OpenTable(producer_colocated_table2_name, &producer_colocated_table2));
  ASSERT_OK(InsertRowsInProducer(0, 50, producer_colocated_table2));

  auto consumer_colocated_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/3, /*num_tablets=*/3, &consumer_cluster_,
      /*tablegroup_name=*/boost::none, /*colocated=*/true));
  std::shared_ptr<client::YBTable> consumer_colocated_table2;
  ASSERT_OK(
      consumer_client()->OpenTable(consumer_colocated_table2_name, &consumer_colocated_table2));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table2, consumer_colocated_table2));

  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_colocated_table));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_colocated_table));

  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 3);

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
  ASSERT_EQ(resp.entry().tables_size(), 2);
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
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  auto get_streams_resp = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(get_streams_resp.table_infos_size(), 1);
  ASSERT_EQ(get_streams_resp.table_infos(0).table_id(), producer_table_->id());

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

}  // namespace yb
