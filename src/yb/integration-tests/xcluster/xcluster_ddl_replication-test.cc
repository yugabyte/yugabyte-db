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
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/util/backoff_waiter.h"

DECLARE_uint32(xcluster_consistent_wal_safe_time_frequency_ms);

DECLARE_bool(enable_xcluster_api_v2);
DECLARE_bool(TEST_xcluster_enable_ddl_replication);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_at_end);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_log_queries);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDDLReplicationTest : public XClusterYsqlTestBase {
 public:
  struct SetupParams {
    // By default start with no consumer or producer tables.
    std::vector<uint32_t> num_consumer_tablets = {};
    std::vector<uint32_t> num_producer_tablets = {};
    uint32_t replication_factor = 3;
    uint32_t num_masters = 1;
    bool ranged_partitioned = false;
  };

  XClusterDDLReplicationTest() = default;
  ~XClusterDDLReplicationTest() = default;

  virtual void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_xcluster_api_v2) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_enable_ddl_replication) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_log_queries) = true;
  }

  Status SetUpClusters() {
    static const SetupParams kDefaultParams;
    return SetUpClusters(kDefaultParams);
  }

  Status SetUpClusters(const SetupParams& params) {
    return XClusterYsqlTestBase::SetUpWithParams(
        params.num_consumer_tablets, params.num_producer_tablets, params.replication_factor,
        params.num_masters, params.ranged_partitioned);
  }

  Status EnableDDLReplicationExtension() {
    // TODO(#19184): This will be done as part of creating the replication groups.
    auto p_conn = VERIFY_RESULT(producer_cluster_.Connect());
    RETURN_NOT_OK(p_conn.ExecuteFormat("CREATE EXTENSION $0", xcluster::kDDLQueuePgSchemaName));
    RETURN_NOT_OK(p_conn.ExecuteFormat(
        "ALTER DATABASE $0 SET $1.replication_role = SOURCE", namespace_name,
        xcluster::kDDLQueuePgSchemaName));
    auto c_conn = VERIFY_RESULT(consumer_cluster_.Connect());
    RETURN_NOT_OK(c_conn.ExecuteFormat("CREATE EXTENSION $0", xcluster::kDDLQueuePgSchemaName));
    return c_conn.ExecuteFormat(
        "ALTER DATABASE $0 SET $1.replication_role = TARGET", namespace_name,
        xcluster::kDDLQueuePgSchemaName);
  }

  void InsertRowsIntoProducerTableAndVerifyConsumer(
      const client::YBTableName& producer_table_name) {
    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(producer_table_name, &producer_table));
    ASSERT_OK(InsertRowsInProducer(0, 50, producer_table));

    // Once the safe time advances, the target should have the new table and its rows.
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    auto consumer_table_name = ASSERT_RESULT(GetYsqlTable(
        &consumer_cluster_, producer_table_name.namespace_name(),
        producer_table_name.pgschema_name(), producer_table_name.table_name()));
    std::shared_ptr<client::YBTable> consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(consumer_table_name, &consumer_table));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(&resp));
    ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
    ASSERT_TRUE(std::any_of(
        resp.entry().tables().begin(), resp.entry().tables().end(),
        [&](const std::string& table) { return table == producer_table_name.table_id(); }));

    ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
  }
};

TEST_F(XClusterDDLReplicationTest, CreateTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(EnableDDLReplicationExtension());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Create a simple table.
  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  InsertRowsIntoProducerTableAndVerifyConsumer(producer_table_name);

  // Create a table in a new schema.
  const std::string kNewSchemaName = "new_schema";
  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    auto conn = VERIFY_RESULT(cluster->Connect());
    // TODO(jhe) can remove this once create schema is replicated.
    RETURN_NOT_OK(conn.Execute("SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", kNewSchemaName));
    return Status::OK();
  }));
  {
    auto conn = ASSERT_RESULT(producer_cluster_.Connect());
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
    auto conn = VERIFY_RESULT(cluster->Connect());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE USER $0 WITH PASSWORD '123'", kNewUserName));
    RETURN_NOT_OK(conn.Execute("SET yb_xcluster_ddl_replication.enable_manual_ddl_replication=1"));
    RETURN_NOT_OK(conn.ExecuteFormat("GRANT CREATE ON SCHEMA public TO $0", kNewUserName));
    return Status::OK();
  }));
  {
    auto conn = ASSERT_RESULT(producer_cluster_.Connect());
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

TEST_F(XClusterDDLReplicationTest, ExactlyOnceReplication) {
  // Test that DDLs are only replicated exactly once.
  const int kNumTablets = 3;

  ASSERT_OK(SetUpClusters());
  ASSERT_OK(EnableDDLReplicationExtension());
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

}  // namespace yb
