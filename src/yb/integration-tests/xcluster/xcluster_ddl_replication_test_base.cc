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

#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"

#include <rapidjson/error/en.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_test_utils.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

#include "yb/master/mini_master.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/xcluster_ddl_queue_handler.h"
#include "yb/util/backoff_waiter.h"

DECLARE_bool(enable_xcluster_api_v2);

DECLARE_bool(TEST_xcluster_ddl_queue_handler_log_queries);

using namespace std::chrono_literals;

namespace yb {

void XClusterDDLReplicationTestBase::SetUp() {
  XClusterYsqlTestBase::SetUp();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_xcluster_api_v2) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_log_queries) = true;
}

Status XClusterDDLReplicationTestBase::SetUpClusters(
    bool is_colocated, bool start_yb_controller_servers) {
  namespace_name = is_colocated ? "colocated_test_db" : "test_db";
  const SetupParams kDefaultParams{
      // By default start with no consumer or producer tables.
      .num_consumer_tablets = {},
      .num_producer_tablets = {},
      // We only create one pg proxy per cluster, so we need to ensure that the target ddl_queue
      // table leader is on that tserver (so that setting xcluster context works properly).
      .replication_factor = 1,
      .num_masters = 1,
      .ranged_partitioned = false,
      .is_colocated = is_colocated,
      .use_different_database_oids = true,
      .start_yb_controller_servers = start_yb_controller_servers,
  };
  RETURN_NOT_OK(XClusterYsqlTestBase::SetUpClusters(kDefaultParams));
  if (is_colocated) {
    RETURN_NOT_OK(CreateInitialColocatedTable());
  }
  return Status::OK();
}

Status XClusterDDLReplicationTestBase::CheckpointReplicationGroupOnNamespaces(
    const std::vector<NamespaceName>& namespace_names) {
  std::vector<NamespaceId> namespace_ids;
  for (const auto& namespace_name : namespace_names) {
    namespace_ids.push_back(
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*producer_client(), namespace_name)));
  }
  RETURN_NOT_OK(
      client::XClusterClient(*producer_client())
          .CreateOutboundReplicationGroup(kReplicationGroupId, namespace_ids, UseAutomaticMode()));

  for (const auto& namespace_id : namespace_ids) {
    auto bootstrap_required =
        VERIFY_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, namespace_id));
    LOG(INFO) << "bootstrap_required for namespace ID " << namespace_id << ": "
              << bootstrap_required;
  }
  return Status::OK();
}

Status XClusterDDLReplicationTestBase::BackupFromProducer(
    std::vector<NamespaceName> namespace_names) {
  if (namespace_names.empty()) {
    namespace_names = {namespace_name};
  }

  auto BackupDir = [&](NamespaceName namespace_name) {
    return GetTempDir(Format("backup_$0", namespace_name));
  };

  // Backup databases from producer.
  for (const auto& namespace_name : namespace_names) {
    RETURN_NOT_OK(RunBackupCommand(
        {"--backup_location", BackupDir(namespace_name), "--keyspace",
         Format("ysql.$0", namespace_name), "create"},
        &*producer_cluster_.mini_cluster_));
  }
  return Status::OK();
}

Status XClusterDDLReplicationTestBase::RestoreToConsumer(
    std::vector<NamespaceName> namespace_names) {
  if (namespace_names.empty()) {
    namespace_names = {namespace_name};
  }
  auto BackupDir = [&](NamespaceName namespace_name) {
    return GetTempDir(Format("backup_$0", namespace_name));
  };

  // Restore to new databases on the consumer.
  for (const auto& namespace_name : namespace_names) {
    (void)DropDatabase(consumer_cluster_, namespace_name);
    RETURN_NOT_OK(RunBackupCommand(
        {"--backup_location", BackupDir(namespace_name), "--keyspace",
         Format("ysql.$0", namespace_name), "restore"},
        &*consumer_cluster_.mini_cluster_));
  }
  return Status::OK();
}

Status XClusterDDLReplicationTestBase::RunBackupCommand(
    const std::vector<std::string>& args, MiniClusterBase* cluster) {
  if (UseYbController()) {
    return tools::RunYbControllerCommand(cluster, *tmp_dir_, args);
  }
  // We should have skipped this test but just in case fail here.
  ADD_FAILURE()
      << "This test does not work with yb_backup.py; did you forget to skip this in that case?";
  return STATUS(
      IllegalState,
      "XClusterDDLReplicationTestBase::RunBackupCommand does not work with yb_backup.py");
}

void XClusterDDLReplicationTestBase::InsertRowsIntoProducerTableAndVerifyConsumer(
    const client::YBTableName& producer_table_name, uint32_t start, uint32_t end,
    const xcluster::ReplicationGroupId replication_group) {
  std::shared_ptr<client::YBTable> producer_table =
      ASSERT_RESULT(GetProducerTable(producer_table_name));
  ASSERT_OK(InsertRowsInProducer(start, end, producer_table));

  // Once the safe time advances, the target should have the new table and its rows.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  std::shared_ptr<client::YBTable> consumer_table =
      ASSERT_RESULT(GetConsumerTable(producer_table_name));

  if (!consumer_table->colocated()) {
    // Verify that universe was setup on consumer.
    // Skip for colocated as the table is not tracked in master replication.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(replication_group, &resp));
    ASSERT_EQ(resp.entry().replication_group_id(), replication_group);
    ASSERT_TRUE(std::any_of(
        resp.entry().tables().begin(), resp.entry().tables().end(),
        [&](const std::string& table) { return table == producer_table_name.table_id(); }));
  }

  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
}

Status XClusterDDLReplicationTestBase::WaitForSafeTimeToAdvanceToNowWithoutDDLQueue() {
  HybridTime now = VERIFY_RESULT(producer_cluster()->GetLeaderMiniMaster())->Now();
  for (auto ts : producer_cluster()->mini_tablet_servers()) {
    now.MakeAtLeast(ts->Now());
  }
  auto namespace_id = VERIFY_RESULT(GetNamespaceId(consumer_client()));

  return WaitFor(
      [&]() -> Result<bool> {
        auto safe_time_result = consumer_client()->GetXClusterSafeTimeForNamespace(
            namespace_id, master::XClusterSafeTimeFilter::DDL_QUEUE);
        if (!safe_time_result) {
          CHECK(safe_time_result.status().IsTryAgain());

          return false;
        }
        auto safe_time = safe_time_result.get();
        return safe_time && safe_time.is_valid() && safe_time > now;
      },
      propagation_timeout_, Format("Wait for safe_time to move above $0", now.ToDebugString()));
}

Status XClusterDDLReplicationTestBase::PrintDDLQueue(Cluster& cluster) {
  const int kMaxJsonStrLen = 500;
  auto conn = VERIFY_RESULT(cluster.ConnectToDB(namespace_name));
  const auto rows = VERIFY_RESULT((conn.FetchRows<int64_t, int64_t, std::string>(Format(
      "SELECT $0, $1, $2 FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY $0 ASC",
      xcluster::kDDLQueueDDLEndTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      xcluster::kDDLQueueYbDataColumn))));

  std::stringstream ss;
  ss << "DDL Queue Table:" << std::endl;
  for (const auto& [ddl_end_time, query_id, raw_json_data] : rows) {
    // Serialized JSON string has an extra character at the front.
    ss << ddl_end_time << "\t" << query_id << "\t" << raw_json_data.substr(1, kMaxJsonStrLen)
       << std::endl;
  }
  LOG(INFO) << ss.str();

  return Status::OK();
}

Result<xcluster::SafeTimeBatch>
XClusterDDLReplicationTestBase::FetchSafeTimeBatchFromReplicatedDdls() {
  auto conn = VERIFY_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  RETURN_NOT_OK(tserver::XClusterDDLQueueHandler::RunDdlQueueHandlerPrepareQueries(&conn));
  return tserver::XClusterDDLQueueHandler::FetchSafeTimeBatchFromReplicatedDdls(&conn);
}

Status XClusterDDLReplicationTestBase::StepDownDdlQueueTablet(Cluster& cluster) {
  auto ddl_queue_table = VERIFY_RESULT(GetYsqlTable(
      &cluster, namespace_name, xcluster::kDDLQueuePgSchemaName, xcluster::kDDLQueueTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  RETURN_NOT_OK(cluster.client_->GetTabletsFromTableId(ddl_queue_table.table_id(), 1, &tablets));
  DCHECK_EQ(tablets.size(), 1);

  const auto leader_peer =
      VERIFY_RESULT(GetLeaderPeerForTablet(cluster.mini_cluster_.get(), tablets[0].tablet_id()));
  return StepDown(leader_peer, /*new_leader_uuid=*/"", ForceStepDown::kTrue);
}

Status XClusterDDLReplicationTestBase::CreateInitialColocatedTable() {
  // Create a simple table on each side with the same colocation id.
  return RunOnBothClusters([&](Cluster* cluster) -> Status {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0($1 int PRIMARY KEY) WITH (colocation_id = 999999)",
        kInitialColocatedTableName, kKeyColumnName));
    return Status::OK();
  });
}

Result<std::string> XClusterDDLReplicationTestBase::GetReplicationRole(
    Cluster& cluster, const NamespaceName& database) {
  const auto& db_name = database.empty() ? namespace_name : database;
  auto conn = VERIFY_RESULT(cluster.ConnectToDB(db_name));
  return conn.FetchRowAsString("SELECT yb_xcluster_ddl_replication.get_replication_role();");
}

Status XClusterDDLReplicationTestBase::ValidateReplicationRole(
    Cluster& cluster, const std::string& expected_role, const NamespaceName& database) {
  auto actual_role = VERIFY_RESULT(GetReplicationRole(cluster, database));
  SCHECK_EQ(
      actual_role, expected_role, IllegalState,
      Format("Expected replication role $0, got $1", expected_role, actual_role));
  return Status::OK();
}

bool XClusterDDLReplicationTestBase::SetReplicationDirection(
    ReplicationDirection replication_direction) {
  if (replication_direction_ == replication_direction) {
    return false;
  }
  replication_direction_ = replication_direction;
  std::swap(consumer_cluster_, producer_cluster_);
  std::swap(consumer_table_, producer_table_);
  LOG(INFO) << "Switched replication direction to "
            << (replication_direction_ == ReplicationDirection::AToB ? "A -> B" : "B -> A");
  return true;
}

}  // namespace yb
