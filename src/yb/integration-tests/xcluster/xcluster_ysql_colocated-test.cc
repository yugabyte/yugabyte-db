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
//

#include <string>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/colocated_util.h"
#include "yb/common/wire_protocol.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/backoff_waiter.h"

DECLARE_bool(xcluster_wait_on_ddl_alter);
DECLARE_bool(ysql_legacy_colocated_database_creation);
DECLARE_bool(ysql_enable_packed_row);

namespace yb {

class XClusterYsqlColocatedTest : public XClusterYsqlTestBase {
 public:
  Status SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets, uint32_t replication_factor,
      uint32_t num_masters = 1) {
    RETURN_NOT_OK(Initialize(replication_factor, num_masters));

    SCHECK_EQ(
        num_consumer_tablets.size(), num_producer_tablets.size(), IllegalState,
        Format(
            "Num consumer tables: $0 num producer tables: $1 must be equal.",
            num_consumer_tablets.size(), num_producer_tablets.size()));

    namespace_name = "colocated_db";
    RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) {
      return CreateDatabase(cluster, namespace_name, /* colocated */ true);
    }));

    RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
      const auto* num_tablets = &num_producer_tablets;
      if (cluster == &consumer_cluster_) {
        num_tablets = &num_consumer_tablets;
      }

      for (uint32_t i = 0; i < num_tablets->size(); i++) {
        auto table_name = VERIFY_RESULT(CreateYsqlTable(
            i, num_tablets->at(i), cluster, boost::none /* tablegroup */,
            /* colocated = */ true));
        std::shared_ptr<client::YBTable> table;
        RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
        cluster->tables_.push_back(table);
      }
      return Status::OK();
    }));

    return PostSetUp();
  }

  Status TestDatabaseReplication(bool compact = false, bool use_transaction = false) {
    SetAtomicFlag(true, &FLAGS_xcluster_wait_on_ddl_alter);
    constexpr auto kRecordBatch = 5;
    auto count = 0;
    constexpr int kNTabletsPerColocatedTable = 1;
    constexpr int kNTabletsPerTable = 3;
    std::vector<uint32_t> tables_vector = {kNTabletsPerColocatedTable, kNTabletsPerColocatedTable};
    // Create two colocated tables on each cluster.
    RETURN_NOT_OK(SetUpWithParams(tables_vector, tables_vector, 3, 1));

    // Also create an additional non-colocated table in each database.
    auto non_colocated_table = VERIFY_RESULT(CreateYsqlTable(
        &producer_cluster_, namespace_name, "" /* schema_name */, "test_table_2",
        boost::none /* tablegroup */, kNTabletsPerTable, false /* colocated */));
    std::shared_ptr<client::YBTable> non_colocated_producer_table;
    RETURN_NOT_OK(producer_client()->OpenTable(non_colocated_table, &non_colocated_producer_table));
    non_colocated_table = VERIFY_RESULT(CreateYsqlTable(
        &consumer_cluster_, namespace_name, "" /* schema_name */, "test_table_2",
        boost::none /* tablegroup */, kNTabletsPerTable, false /* colocated */));
    std::shared_ptr<client::YBTable> non_colocated_consumer_table;
    RETURN_NOT_OK(consumer_client()->OpenTable(non_colocated_table, &non_colocated_consumer_table));

    auto colocated_consumer_tables = consumer_tables_;
    producer_tables_.push_back(non_colocated_producer_table);
    consumer_tables_.push_back(non_colocated_consumer_table);

    BumpUpSchemaVersionsWithAlters(consumer_tables_);

    // 1. Write some data to all tables.
    for (const auto& producer_table : producer_tables_) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      RETURN_NOT_OK(
          InsertRowsInProducer(count, count + kRecordBatch, producer_table, use_transaction));
    }
    count += kRecordBatch;

    // 2. Setup replication for only the colocated tables.
    // Get the producer colocated parent table id.
    auto colocated_parent_table_id = VERIFY_RESULT(GetColocatedDatabaseParentTableId());

    RETURN_NOT_OK(SetupUniverseReplication({colocated_parent_table_id}));
    RETURN_NOT_OK(CorrectlyPollingAllTablets(kNTabletsPerColocatedTable));

    // 4. Check that colocated tables are being replicated.
    auto data_replicated_correctly = [&](int num_results, bool onlyColocated) -> Result<bool> {
      auto& tables = onlyColocated ? colocated_consumer_tables : consumer_tables_;
      for (const auto& consumer_table : tables) {
        LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
        RETURN_NOT_OK(WaitForRowCount(consumer_table->name(), num_results, &consumer_cluster_));
        RETURN_NOT_OK(ValidateRows(consumer_table->name(), num_results, &consumer_cluster_));
      }
      return true;
    };
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> { return data_replicated_correctly(count, true); },
        MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly Colocated only"));
    // Ensure that the non colocated table is not replicated.
    auto non_coloc_results =
        VERIFY_RESULT(ScanToStrings(non_colocated_consumer_table->name(), &consumer_cluster_));
    SCHECK_EQ(
        0, PQntuples(non_coloc_results.get()), IllegalState,
        "Non colocated table should not be replicated.");

    // 5. Add the regular table to replication.
    // Prepare and send AlterUniverseReplication command.
    RETURN_NOT_OK(AlterUniverseReplication(
        kReplicationGroupId, {non_colocated_producer_table}, true /*add_tables*/));

    // Wait until we have 2 tables (colocated tablet + regular table) logged.
    RETURN_NOT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          return VerifyUniverseReplication(&tmp_resp).ok() && tmp_resp.entry().tables_size() == 2;
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    RETURN_NOT_OK(CorrectlyPollingAllTablets(kNTabletsPerColocatedTable + kNTabletsPerTable));
    // Check that all data is replicated for the new table as well.
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> { return data_replicated_correctly(count, false); },
        MonoDelta::FromSeconds(20 * kTimeMultiplier), "IsDataReplicatedCorrectly all tables"));

    // 6. Add additional data to all tables
    for (const auto& producer_table : producer_tables_) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      RETURN_NOT_OK(
          InsertRowsInProducer(count, count + kRecordBatch, producer_table, use_transaction));
    }
    count += kRecordBatch;

    // 7. Verify all tables are properly replicated.
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> { return data_replicated_correctly(count, false); },
        MonoDelta::FromSeconds(20 * kTimeMultiplier),
        "IsDataReplicatedCorrectly all tables more rows"));

    // Test Add Colocated Table, which is an ALTER operation.
    std::shared_ptr<client::YBTable> new_colocated_producer_table, new_colocated_consumer_table;

    // Add a Colocated Table on the Producer for an existing Replication stream.
    uint32_t idx = static_cast<uint32_t>(tables_vector.size()) + 1;
    {
      const int co_id = (idx) * 111111;
      auto table = VERIFY_RESULT(CreateYsqlTable(
          &producer_cluster_, namespace_name, "", Format("test_table_$0", idx), boost::none,
          kNTabletsPerColocatedTable, true, co_id));
      RETURN_NOT_OK(producer_client()->OpenTable(table, &new_colocated_producer_table));
    }

    // 2. Write data so we have some entries on the new colocated table.
    RETURN_NOT_OK(
        InsertRowsInProducer(0, kRecordBatch, new_colocated_producer_table, use_transaction));

    {
      // Matching schema to consumer should succeed.
      const int co_id = (idx) * 111111;
      auto table = VERIFY_RESULT(CreateYsqlTable(
          &consumer_cluster_, namespace_name, "", Format("test_table_$0", idx), boost::none,
          kNTabletsPerColocatedTable, true, co_id));
      RETURN_NOT_OK(consumer_client()->OpenTable(table, &new_colocated_consumer_table));
    }

    // 5. Verify the new schema Producer entries are added to Consumer.
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          LOG(INFO) << "Checking records for table "
                    << new_colocated_consumer_table->name().ToString();
          RETURN_NOT_OK(WaitForRowCount(
              new_colocated_consumer_table->name(), kRecordBatch, &consumer_cluster_));
          RETURN_NOT_OK(ValidateRows(
              new_colocated_consumer_table->name(), kRecordBatch, &consumer_cluster_));
          return true;
        },
        MonoDelta::FromSeconds(20 * kTimeMultiplier),
        "IsDataReplicatedCorrectly new colocated table"));

    // 6. Shutdown the colocated tablet leader and verify that replication is still happening.
    {
      auto tablet_ids = ListTabletIdsForTable(consumer_cluster(), colocated_parent_table_id);
      auto old_ts = FindTabletLeader(consumer_cluster(), *tablet_ids.begin());
      old_ts->Shutdown();
      const auto deadline = CoarseMonoClock::Now() + 10s * kTimeMultiplier;
      RETURN_NOT_OK(WaitUntilTabletHasLeader(consumer_cluster(), *tablet_ids.begin(), deadline));
      RETURN_NOT_OK(old_ts->RestartStoppedServer());
      RETURN_NOT_OK(old_ts->WaitStarted());

      RETURN_NOT_OK(InsertRowsInProducer(
          kRecordBatch, 2 * kRecordBatch, new_colocated_producer_table,
          use_transaction));

      RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            LOG(INFO) << "Checking records for table "
                      << new_colocated_consumer_table->name().ToString();
            RETURN_NOT_OK(WaitForRowCount(
                new_colocated_consumer_table->name(), 2 * kRecordBatch, &consumer_cluster_));
            RETURN_NOT_OK(ValidateRows(
                new_colocated_consumer_table->name(), 2 * kRecordBatch, &consumer_cluster_));
            return true;
      },
          MonoDelta::FromSeconds(20 * kTimeMultiplier),
          "IsDataReplicatedCorrectly new colocated table"));
    }

    // 7. Drop the new table and ensure that data is getting replicated correctly for
    // the other tables
    RETURN_NOT_OK(
        DropYsqlTable(&producer_cluster_, namespace_name, "", Format("test_table_$0", idx)));
    LOG(INFO) << Format("Dropped test_table_$0 on Producer side", idx);

    // 8. Add additional data to the original tables.
    for (const auto& producer_table : producer_tables_) {
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      RETURN_NOT_OK(
          InsertRowsInProducer(count, count + kRecordBatch, producer_table, use_transaction));
    }
    count += kRecordBatch;

    // 9. Verify all tables are properly replicated.
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> { return data_replicated_correctly(count, false); },
        MonoDelta::FromSeconds(20 * kTimeMultiplier),
        "IsDataReplicatedCorrectly after new colocated table drop"));

    if (compact) {
      BumpUpSchemaVersionsWithAlters(consumer_tables_);

      RETURN_NOT_OK(consumer_cluster()->FlushTablets());
      RETURN_NOT_OK(consumer_cluster()->CompactTablets());

      RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> { return data_replicated_correctly(count, false); },
          MonoDelta::FromSeconds(20 * kTimeMultiplier),
          "IsDataReplicatedCorrectlyAfterCompaction"));
    }

    return Status::OK();
  }
};

TEST_F(XClusterYsqlColocatedTest, ReplicationWithPackedColumnsAndSchemaVersionMismatch) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1, 1));

  TestReplicationWithSchemaChanges(
      ASSERT_RESULT(GetColocatedDatabaseParentTableId()), false /* boostrap */);
}

TEST_F(XClusterYsqlColocatedTest, ReplicationWithPackedColumnsAndBootstrap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1, 1));

  TestReplicationWithSchemaChanges(
      ASSERT_RESULT(GetColocatedDatabaseParentTableId()), true /* boostrap */);
}

TEST_F(XClusterYsqlColocatedTest, DatabaseReplication) { ASSERT_OK(TestDatabaseReplication()); }

TEST_F(XClusterYsqlColocatedTest, LegacyColocatedDatabaseReplication) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_legacy_colocated_database_creation) = true;
  ASSERT_NOK_STR_CONTAINS(
      TestDatabaseReplication(),
      "Pre GA colocated databases are not supported with xCluster replication");
}

TEST_F(XClusterYsqlColocatedTest, DatabaseReplicationWithPacked) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ASSERT_OK(TestDatabaseReplication());
}

TEST_F(XClusterYsqlColocatedTest, DatabaseReplicationWithPackedAndCompact) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ASSERT_OK(TestDatabaseReplication(/* compact = */ true));
}

TEST_F(XClusterYsqlColocatedTest, PackedDatabaseReplicationWithTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ASSERT_OK(TestDatabaseReplication(/* compact= */ true, /* use_transaction = */ true));
}

TEST_F(XClusterYsqlColocatedTest, TestTablesReplicationWithLargeTableCount) {
  constexpr int kNTabletsPerColocatedTable = 1;
  std::vector<uint32_t> tables_vector;
  for (int i = 0; i < 30; i++) {
    tables_vector.push_back(kNTabletsPerColocatedTable);
  }

  // Create colocated tables on each cluster
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 3, 1));

  ASSERT_OK(InsertRowsInProducer(0, 50));

  // 2. Setup replication for only the colocated tables.
  // Get the producer colocated parent table id.
  auto colocated_parent_table_id = ASSERT_RESULT(GetColocatedDatabaseParentTableId());

  ASSERT_OK(SetupUniverseReplication({colocated_parent_table_id}));

  ASSERT_OK(CorrectlyPollingAllTablets(kNTabletsPerColocatedTable));
  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterYsqlColocatedTest, DifferentColocationIds) {
  ASSERT_OK(SetUpWithParams({}, {}, 3, 1));

  // Create two tables with different colocation ids.
  auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto table_info = ASSERT_RESULT(CreateYsqlTable(
      &producer_cluster_, namespace_name, "" /* schema_name */, "test_table_0",
      boost::none /* tablegroup */, 1 /* num_tablets */, true /* colocated */,
      123456 /* colocation_id */));
  ASSERT_RESULT(CreateYsqlTable(
      &consumer_cluster_, namespace_name, "" /* schema_name */, "test_table_0",
      boost::none /* tablegroup */, 1 /* num_tablets */, true /* colocated */,
      123457 /* colocation_id */));
  std::shared_ptr<client::YBTable> producer_table;
  ASSERT_OK(producer_client()->OpenTable(table_info, &producer_table));

  // Try to setup replication, should fail on schema validation due to different colocation ids.
  ASSERT_NOK(SetupUniverseReplication({producer_table}));
}

TEST_F(XClusterYsqlColocatedTest, IsBootstrapRequired) {
  // Make sure IsBootstrapRequired returns false when tables are empty and true when even one table
  // in a colocated DB has data.
  constexpr int kNTablets = 1;
  std::vector<uint32_t> tables_vector = {kNTablets, kNTablets};
  // Create two colocated tables on each cluster.
  ASSERT_OK(SetUpWithParams(
      tables_vector, tables_vector, /* replication_factor */ 3, /* num_masters */ 1));
  auto colocated_parent_table_id = ASSERT_RESULT(GetColocatedDatabaseParentTableId());

  // Empty DB should not require bootstrap.
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({colocated_parent_table_id})));

  // Adding table to replication should not make it require bootstrap.
  ASSERT_OK(SetupUniverseReplication(producer_tables_));
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({colocated_parent_table_id})));

  // Inserting data into one table should make it require bootstrap.
  ASSERT_OK(InsertRowsInProducer(0, 1, producer_tables_[0]));
  ASSERT_TRUE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({colocated_parent_table_id})));

  // Delete all rows in the first table and inserting data into another table should still make it
  // require bootstrap.
  ASSERT_OK(DeleteRowsInProducer(0, 1, producer_tables_[0]));
  ASSERT_OK(InsertRowsInProducer(0, 1, producer_tables_[1]));
  ASSERT_TRUE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({colocated_parent_table_id})));

  // Delete all rows in the second table should make it not require bootstrap.
  ASSERT_OK(DeleteRowsInProducer(0, 1, producer_tables_[1]));
  ASSERT_FALSE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({colocated_parent_table_id})));
}

}  // namespace yb
