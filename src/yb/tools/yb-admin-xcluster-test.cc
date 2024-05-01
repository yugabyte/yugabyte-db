// Copyright (c) YugaByte, Inc.
//
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

#include "yb/tools/yb-admin-test-base.h"
#include "yb/util/flags.h"

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"

#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/rpc/secure_stream.h"

#include "yb/tools/admin-test-base.h"
#include "yb/tools/yb-admin_util.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/client/table_alterer.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/date_time.h"
#include "yb/util/env_util.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/tsan_util.h"

DECLARE_uint64(TEST_yb_inbound_big_calls_parse_delay_ms);
DECLARE_bool(TEST_allow_ycql_transactional_xcluster);
DECLARE_bool(check_bootstrap_required);
DECLARE_int64(rpc_throttle_threshold_bytes);

namespace yb {
namespace tools {

using namespace std::literals;

using std::string;

using client::Transactional;
using client::YBTableName;
using master::ListSnapshotsResponsePB;
using master::MasterBackupProxy;
using master::MasterReplicationProxy;
using master::SysCDCStreamEntryPB;
using rpc::RpcController;

namespace {

const string kFakeUuid = "11111111111111111111111111111111";
const string kBootstrapArg = "bootstrap";

Result<string> GetRecentStreamId(MiniCluster* cluster, TabletId target_table_id = "") {
  // Return the first stream with tablet_id matching target_table_id using ListCDCStreams.
  // If target_table_id is not specified, return the first stream.
  const int kStreamUuidLength = 32;
  string output = VERIFY_RESULT(RunAdminToolCommand(cluster, "list_cdc_streams"));
  const string find_stream_id = "stream_id: \"";
  const string find_table_id = "table_id: \"";
  string target_stream_id;

  string::size_type stream_id_pos = output.find(find_stream_id);
  string::size_type table_id_pos = output.find(find_table_id);
  while (stream_id_pos != string::npos && table_id_pos != string::npos) {
    string stream_id = output.substr(stream_id_pos + find_stream_id.size(), kStreamUuidLength);
    string table_id = output.substr(table_id_pos + find_table_id.size(), kStreamUuidLength);
    if (target_table_id.empty() || table_id == target_table_id) {
      target_stream_id = stream_id;
      break;
    }
    stream_id_pos = output.find(find_stream_id, stream_id_pos + kStreamUuidLength);
    table_id_pos = output.find(find_table_id, table_id_pos + kStreamUuidLength);
  }
  return target_stream_id;
}
}  // namespace

// Configures two clusters with clients for the producer and consumer side of xcluster replication.
class XClusterAdminCliTest : public AdminCliTestBase {
 public:
  virtual int num_tablet_servers() { return 3; }

  void SetUp() override {
    // Setup the default cluster as the consumer cluster.
    {
      TEST_SetThreadPrefixScoped prefix_se("C");
      AdminCliTestBase::SetUp();
    }

    // Only create a table on the consumer, producer table may differ in tests.
    CreateTable(Transactional::kTrue);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_check_bootstrap_required) = false;

    // Create the producer cluster.
    opts.num_tablet_servers = num_tablet_servers();
    opts.cluster_id = kProducerClusterId;
    producer_cluster_ = std::make_unique<MiniCluster>(opts);
    {
      TEST_SetThreadPrefixScoped prefix_se("P");
      ASSERT_OK(producer_cluster_->StartSync());
    }

    ASSERT_OK(producer_cluster_->WaitForTabletServerCount(num_tablet_servers()));
    producer_cluster_client_ = ASSERT_RESULT(producer_cluster_->CreateClient());
  }

  void DoTearDown() override {
    if (producer_cluster_) {
      TEST_SetThreadPrefixScoped prefix_se("P");
      producer_cluster_->Shutdown();
    }

    TEST_SetThreadPrefixScoped prefix_se("C");
    AdminCliTestBase::DoTearDown();
  }

  template <class... Args>
  Result<std::string> RunAdminToolCommandOnProducer(Args&&... args) {
    return tools::RunAdminToolCommand(producer_cluster_.get(), std::forward<Args>(args)...);
  }

  Status WaitForSetupUniverseReplicationCleanUp(string producer_uuid) {
    auto proxy = std::make_shared<master::MasterReplicationProxy>(
        &client_->proxy_cache(), VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

    master::GetUniverseReplicationRequestPB req;
    master::GetUniverseReplicationResponsePB resp;
    return WaitFor(
        [proxy, &req, &resp, producer_uuid]() -> Result<bool> {
          req.set_replication_group_id(producer_uuid);
          RpcController rpc;
          Status s = proxy->GetUniverseReplication(req, &resp, &rpc);

          return resp.has_error();
        },
        20s, "Waiting for universe to delete");
  }

 protected:
  Status CheckTableIsBeingReplicated(
      const std::vector<TableId>& tables,
      SysCDCStreamEntryPB::State target_state = SysCDCStreamEntryPB::ACTIVE) {
    string output = VERIFY_RESULT(RunAdminToolCommandOnProducer("list_cdc_streams"));
    string state_search_str =
        Format("value: \"$0\"", SysCDCStreamEntryPB::State_Name(target_state));

    for (const auto& table_id : tables) {
      // Ensure a stream object with table_id exists.
      size_t table_id_pos = output.find(table_id);
      if (table_id_pos == string::npos) {
        return STATUS_FORMAT(NotFound, "Table id '$0' not found in output: $1", table_id, output);
      }

      // Ensure that the strem object has the expected state value.
      size_t state_pos = output.find(state_search_str, table_id_pos);
      if (state_pos == string::npos) {
        return STATUS_FORMAT(
            NotFound, "Table id '$0' has the incorrect state value in output: $1", table_id,
            output);
      }

      // Ensure that the state value we captured earlier did not belong
      // to different stream object.
      size_t next_stream_obj_pos = output.find("streams {", table_id_pos);
      if (next_stream_obj_pos != string::npos && next_stream_obj_pos <= state_pos) {
        return STATUS_FORMAT(
            NotFound, "Table id '$0' has no state value in output: $1", table_id, output);
      }
    }
    return Status::OK();
  }

  Result<MasterBackupProxy*> ProducerBackupServiceProxy() {
    if (!producer_backup_service_proxy_) {
      producer_backup_service_proxy_.reset(new MasterBackupProxy(
          &producer_cluster_client_->proxy_cache(),
          VERIFY_RESULT(producer_cluster_->GetLeaderMasterBoundRpcAddr())));
    }
    return producer_backup_service_proxy_.get();
  }

  std::pair<client::TableHandle, client::TableHandle> CreateAdditionalTableOnBothClusters() {
    const YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table2");
    client::TableHandle consumer_table2;
    client::TableHandle producer_table2;
    client::kv_table_test::CreateTable(
        Transactional::kTrue, NumTablets(), client_.get(), &consumer_table2, kTableName2);
    client::kv_table_test::CreateTable(
        Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table2,
        kTableName2);
    return std::make_pair(consumer_table2, producer_table2);
  }

  const string kProducerClusterId = "producer";
  std::unique_ptr<client::YBClient> producer_cluster_client_;
  std::unique_ptr<MiniCluster> producer_cluster_;
  MiniClusterOptions opts;

 private:
  std::unique_ptr<MasterBackupProxy> producer_backup_service_proxy_;
};

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplication) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id()));

  // Check that the stream was properly created for this table.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationChecksForColumnIdMismatch) {
  client::TableHandle producer_table;
  client::TableHandle consumer_table;
  const YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "column_id_mismatch_test_table");

  client::kv_table_test::CreateTable(
      Transactional::kTrue,
      NumTablets(),
      producer_cluster_client_.get(),
      &producer_table,
      table_name);
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), client_.get(), &consumer_table, table_name);

  // Drop a column from the consumer table.
  {
    auto table_alterer = client_.get()->NewTableAlterer(table_name);
    ASSERT_OK(table_alterer->DropColumn(kValueColumn)->Alter());
  }

  // Add the same column back into the producer table. This results in a schema mismatch
  // between the producer and consumer versions of the table.
  {
    auto table_alterer = client_.get()->NewTableAlterer(table_name);
    table_alterer->AddColumn(kValueColumn)->Type(DataType::INT32);
    ASSERT_OK(table_alterer->timeout(MonoDelta::FromSeconds(60 * kTimeMultiplier))->Alter());
  }

  // Try setting up replication, this should fail due to the schema mismatch.
  ASSERT_NOK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),

      producer_table->id()));

  // Make a snapshot of the producer table.
  auto timestamp = DateTime::TimestampToString(DateTime::TimestampNow());
  auto producer_backup_proxy = ASSERT_RESULT(ProducerBackupServiceProxy());
  ASSERT_OK(RunAdminToolCommandOnProducer(
      "create_snapshot", producer_table.name().namespace_name(),
      producer_table.name().table_name()));

  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(producer_backup_proxy, 1, 0));
  ASSERT_RESULT(WaitForAllSnapshots(producer_backup_proxy));

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_producer_snapshot.dat");
  ASSERT_OK(RunAdminToolCommandOnProducer("export_snapshot", snapshot_id, snapshot_file));

  // Delete consumer table, then import snapshot of producer table into the existing
  // consumer table. This should fix the schema mismatch issue.
  ASSERT_OK(client_->DeleteTable(table_name, /* wait */ true));
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));

  // Try running SetupUniverseReplication again, this time it should succeed.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id()));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationFailsWithInvalidSchema) {
  client::TableHandle producer_cluster_table;

  // Create a table with a different schema on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kFalse,  // Results in different schema!
      NumTablets(),
      producer_cluster_client_.get(),
      &producer_cluster_table);

  // Try to setup universe replication, should return with a useful error.
  string error_msg;
  // First provide a non-existant table id.
  // ASSERT_NOK since this should fail.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id() + "-BAD"));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Now try with the correct table id.
  // Note that SetupUniverseReplication should call DeleteUniverseReplication to
  // clean up the environment on failure, so we don't need to explicitly call
  // DeleteUniverseReplication here.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id()));

  // Verify that error message has relevant information.
  ASSERT_TRUE(error_msg.find("Source and target schemas don't match") != string::npos);
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationFailsWithInvalidBootstrapId) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  // Try to setup universe replication with a fake bootstrap id, should return with a useful error.
  string error_msg;
  // ASSERT_NOK since this should fail.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id(),
      kFakeUuid));

  // Verify that error message has relevant information.
  ASSERT_TRUE(
      error_msg.find("Could not find CDC stream: stream_id: \"" + kFakeUuid + "\"") !=
      string::npos);
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationCleanupOnFailure) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  string error_msg;
  // Try to setup universe replication with a fake bootstrap id, should result in failure.
  // ASSERT_NOK since this should fail. We should be able to make consecutive calls to
  // SetupUniverseReplication without having to call DeleteUniverseReplication first.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id(),
      kFakeUuid));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Try to setup universe replication with fake producer master address.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      "fake-producer-address",
      producer_cluster_table->id()));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Try to setup universe replication with fake producer table id.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      "fake-producer-table-id"));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Test when producer and local table have different schema.
  client::TableHandle producer_cluster_table2;
  client::TableHandle consumer_table2;
  const YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "different_schema_test_table");

  client::kv_table_test::CreateTable(
      Transactional::kFalse,  // Results in different schema!
      NumTablets(),
      producer_cluster_client_.get(),
      &producer_cluster_table2,
      kTableName2);
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), client_.get(), &consumer_table2, kTableName2);

  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table2->id()));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Verify that the environment is cleaned up correctly after failure.
  // A valid call to SetupUniverseReplication after the failure should succeed
  // without us having to first call DeleteUniverseReplication.
  ASSERT_OK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id()));
  // Verify table is being replicated.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Try calling SetupUniverseReplication again. This should fail as the producer
  // is already present. However, in this case, DeleteUniverseReplication should
  // not be called since the error was due to failing a sanity check.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id()));
  // Verify the universe replication has not been deleted is still there.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete universe.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestSetupNamespaceReplicationWithBootstrap) {
  client::TableHandle producer_cluster_table;
  const client::YBTableName kTestTableName(YQL_DATABASE_CQL, "my_keyspace", "test_table");

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table,
      kTestTableName);

  const auto& producer_namespace = "ycql.my_keyspace";
  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand(
      "setup_namespace_universe_replication", kProducerClusterId,
      producer_cluster_->GetMasterAddresses(), producer_namespace, kBootstrapArg));

  // Check that the stream was properly created for this table.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestSetupNamespaceReplicationWithBootstrapFailTransactionalCQL) {
  client::TableHandle producer_cluster_table;
  const client::YBTableName kTestTableName(YQL_DATABASE_CQL, "my_keyspace", "test_table");

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table,
      kTestTableName);

  const auto& producer_namespace = "ycql.my_keyspace";
  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", kProducerClusterId,
      producer_cluster_->GetMasterAddresses(), producer_namespace, kBootstrapArg, "transactional"));
}

TEST_F(XClusterAdminCliTest, TestSetupNamespaceReplicationWithBootstrapFailInvalidArgumentOrder) {
  client::TableHandle producer_cluster_table;
  const client::YBTableName kTestTableName(YQL_DATABASE_CQL, "my_keyspace", "test_table");

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table,
      kTestTableName);

  const auto& producer_namespace = "ycql.my_keyspace";

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", producer_cluster_->GetMasterAddresses(),
      kProducerClusterId, producer_namespace, kBootstrapArg));

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", producer_cluster_->GetMasterAddresses(),
      producer_namespace, kProducerClusterId, kBootstrapArg));

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", producer_namespace,
      producer_cluster_->GetMasterAddresses(), kProducerClusterId, kBootstrapArg));

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", producer_namespace, kProducerClusterId,
      producer_cluster_->GetMasterAddresses(), kBootstrapArg));
}

TEST_F(XClusterAdminCliTest, TestSetupNamespaceReplicationWithBootstrapFailInvalidNamespace) {
  client::TableHandle producer_cluster_table;
  const client::YBTableName kTestTableName(YQL_DATABASE_CQL, "my_keyspace", "test_table");

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table,
      kTestTableName);


  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", kProducerClusterId,
      producer_cluster_->GetMasterAddresses(), "my_keyspace.ycql", kBootstrapArg));

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", kProducerClusterId,
      producer_cluster_->GetMasterAddresses(), "my_keyspace.ysql", kBootstrapArg));
}

TEST_F(XClusterAdminCliTest, TestSetupNamespaceReplicationWithBootstrapFailInvalidNumArgs) {
  client::TableHandle producer_cluster_table;
  const client::YBTableName kTestTableName(YQL_DATABASE_CQL, "my_keyspace", "test_table");

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table,
      kTestTableName);

  ASSERT_NOK(RunAdminToolCommand("setup_namespace_universe_replication", kProducerClusterId));

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", kProducerClusterId,
      producer_cluster_->GetMasterAddresses()));

  ASSERT_NOK(RunAdminToolCommand(
      "setup_namespace_universe_replication", kProducerClusterId,
      producer_cluster_->GetMasterAddresses(), "my_keyspace.ysql", kBootstrapArg, kBootstrapArg,
      kBootstrapArg));
}

TEST_F(XClusterAdminCliTest, TestListCdcStreamsWithBootstrappedStreams) {
  const int kStreamUuidLength = 32;
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  string output = ASSERT_RESULT(RunAdminToolCommandOnProducer("list_cdc_streams"));
  // First check that the table and bootstrap status are not present.
  ASSERT_EQ(output.find(producer_cluster_table->id()), string::npos);
  ASSERT_EQ(
      output.find(SysCDCStreamEntryPB::State_Name(SysCDCStreamEntryPB::INITIATED)), string::npos);

  // Bootstrap the producer.
  output = ASSERT_RESULT(
      RunAdminToolCommandOnProducer("bootstrap_cdc_producer", producer_cluster_table->id()));
  // Get the bootstrap id (output format is "table id: 123, CDC bootstrap id: 123\n").
  string bootstrap_id = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);

  // Check list_cdc_streams again for the table and the status INITIATED.
  ASSERT_OK(
      CheckTableIsBeingReplicated({producer_cluster_table->id()}, SysCDCStreamEntryPB::INITIATED));

  // Setup universe replication using the bootstrap_id
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id(),
      bootstrap_id));

  // Check list_cdc_streams again for the table and the status ACTIVE.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Try restarting the producer to ensure that the status persists.
  ASSERT_OK(producer_cluster_->RestartSync());
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestRenameUniverseReplication) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id()));

  // Check that the stream was properly created for this table.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Now rename the replication group and then try to perform operations on it.
  std::string new_replication_id = "new_replication_id";
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication", kProducerClusterId, "rename_id", new_replication_id));

  // Assert that using old universe id fails.
  ASSERT_NOK(RunAdminToolCommand("set_universe_replication_enabled", kProducerClusterId, 0));
  // But using correct name should succeed.
  ASSERT_OK(RunAdminToolCommand("set_universe_replication_enabled", new_replication_id, 0));

  // Also create a second stream so we can verify name collisions.
  std::string collision_id = "collision_id";
  // Need to create new tables so that we don't hit "N:1 replication topology not supported" errors.
  auto [consumer_table2, producer_table2] = CreateAdditionalTableOnBothClusters();
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      collision_id,
      producer_cluster_->GetMasterAddresses(),
      producer_table2->id()));
  ASSERT_NOK(RunAdminToolCommand(
      "alter_universe_replication", new_replication_id, "rename_id", collision_id));

  // Using correct name should still succeed.
  ASSERT_OK(RunAdminToolCommand("set_universe_replication_enabled", new_replication_id, 1));

  // Also test that we can rename again.
  std::string new_replication_id2 = "new_replication_id2";
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication", new_replication_id, "rename_id", new_replication_id2));

  // Assert that using old universe ids fails.
  ASSERT_NOK(RunAdminToolCommand("set_universe_replication_enabled", kProducerClusterId, 1));
  ASSERT_NOK(RunAdminToolCommand("set_universe_replication_enabled", new_replication_id, 1));
  // But using new correct name should succeed.
  ASSERT_OK(RunAdminToolCommand("set_universe_replication_enabled", new_replication_id2, 1));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", new_replication_id2));
  // Also delete second one too.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", collision_id));
}

class XClusterAlterUniverseAdminCliTest : public XClusterAdminCliTest {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();

    // Use more masters so we can test set_master_addresses
    opts.num_masters = 3;

    XClusterAdminCliTest::SetUp();
  }
};

TEST_F(XClusterAlterUniverseAdminCliTest, TestAlterUniverseReplication) {
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Create an additional table to test with as well.
  auto [consumer_table2, producer_table2] = CreateAdditionalTableOnBothClusters();

  // Setup replication with both tables, this should only return once complete.
  // Only use the leader master address initially.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      ASSERT_RESULT(producer_cluster_->GetLeaderMiniMaster())->bound_rpc_addr_str(),
      producer_table->id() + "," + producer_table2->id()));

  // Test set_master_addresses, use all the master addresses now.
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication",
      kProducerClusterId,
      "set_master_addresses",
      producer_cluster_->GetMasterAddresses()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id(), producer_table2->id()}));

  // Test removing a table.
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication", kProducerClusterId, "remove_table", producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table2->id()}));
  ASSERT_NOK(CheckTableIsBeingReplicated({producer_table->id()}));

  // Test adding a table.
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication", kProducerClusterId, "add_table", producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id(), producer_table2->id()}));

  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAlterUniverseAdminCliTest, TestAlterUniverseReplicationWithBootstrapId) {
  const int kStreamUuidLength = 32;
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Create an additional table to test with as well.
  auto [consumer_table2, producer_table2] = CreateAdditionalTableOnBothClusters();

  // Get bootstrap ids for both producer tables and get bootstrap ids.
  string output =
      ASSERT_RESULT(RunAdminToolCommandOnProducer("bootstrap_cdc_producer", producer_table->id()));
  string bootstrap_id1 = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);
  ASSERT_OK(CheckTableIsBeingReplicated(
      {producer_table->id()}, master::SysCDCStreamEntryPB_State_INITIATED));

  output =
      ASSERT_RESULT(RunAdminToolCommandOnProducer("bootstrap_cdc_producer", producer_table2->id()));
  string bootstrap_id2 = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);
  ASSERT_OK(CheckTableIsBeingReplicated(
      {producer_table2->id()}, master::SysCDCStreamEntryPB_State_INITIATED));

  // Setup replication with first table, this should only return once complete.
  // Only use the leader master address initially.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      ASSERT_RESULT(producer_cluster_->GetLeaderMiniMaster())->bound_rpc_addr_str(),
      producer_table->id(),
      bootstrap_id1));

  // Test adding the second table with bootstrap id
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication",
      kProducerClusterId,
      "add_table",
      producer_table2->id(),
      bootstrap_id2));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id(), producer_table2->id()}));

  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

// delete_cdc_stream tests
TEST_F(XClusterAdminCliTest, TestDeleteCDCStreamWithConsumerSetup) {
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id()}));

  string stream_id = ASSERT_RESULT(GetRecentStreamId(producer_cluster_.get()));

  // Should fail as it should meet the conditions to be stopped.
  ASSERT_NOK(RunAdminToolCommandOnProducer("delete_cdc_stream", stream_id));
  // Should pass as we force it.
  ASSERT_OK(RunAdminToolCommandOnProducer("delete_cdc_stream", stream_id, "force_delete"));
  // Delete universe should NOT fail due to a deleted stream
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestDeleteCDCStreamWithAlterUniverse) {
  client::TableHandle producer_table;
  constexpr int kNumTables = 3;
  const client::YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "test_table2");

  // Create identical table on producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Create some additional tables. The reason is that the number of tables removed using
  // alter_universe_replication remove_table must be less than the total number of tables.
  string producer_table_ids_str = producer_table->id();
  std::vector<TableId> producer_table_ids = {producer_table->id()};
  for (int i = 1; i < kNumTables; i++) {
    client::TableHandle tmp_producer_table, tmp_consumer_table;
    const client::YBTableName kTestTableName(
        YQL_DATABASE_CQL, "my_keyspace", Format("test_table_$0", i));
    client::kv_table_test::CreateTable(
        Transactional::kTrue, NumTablets(), client_.get(), &tmp_consumer_table, kTestTableName);
    client::kv_table_test::CreateTable(
        Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &tmp_producer_table,
        kTestTableName);
    producer_table_ids_str += Format(",$0", tmp_producer_table->id());
    producer_table_ids.push_back(tmp_producer_table->id());
  }

  // Setup universe replication.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table_ids_str));
  ASSERT_OK(CheckTableIsBeingReplicated(producer_table_ids));

  // Obtain the stream ID for the first table.
  string stream_id =
      ASSERT_RESULT(GetRecentStreamId(producer_cluster_.get(), producer_table->id()));
  ASSERT_FALSE(stream_id.empty());

  // Mark one stream as deleted.
  ASSERT_OK(RunAdminToolCommandOnProducer("delete_cdc_stream", stream_id, "force_delete"));

  // Remove table should succeed.
  ASSERT_OK(RunAdminToolCommand(
      "alter_universe_replication", kProducerClusterId, "remove_table", producer_table->id()));
}

TEST_F(XClusterAdminCliTest, TestWaitForReplicationDrain) {
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Setup universe replication.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id()}));
  string stream_id = ASSERT_RESULT(GetRecentStreamId(producer_cluster_.get()));

  // API should succeed with correctly formatted arguments.
  ASSERT_OK(RunAdminToolCommandOnProducer("wait_for_replication_drain", stream_id));
  ASSERT_OK(RunAdminToolCommandOnProducer(
      "wait_for_replication_drain", stream_id, GetCurrentTimeMicros()));
  ASSERT_OK(RunAdminToolCommandOnProducer("wait_for_replication_drain", stream_id, "minus", "3s"));

  // API should fail with an invalid stream ID.
  ASSERT_NOK(RunAdminToolCommandOnProducer("wait_for_replication_drain", "abc"));

  // API should fail with an invalid target_time format.
  ASSERT_NOK(RunAdminToolCommandOnProducer("wait_for_replication_drain", stream_id, 123));
  ASSERT_NOK(
      RunAdminToolCommandOnProducer("wait_for_replication_drain", stream_id, "minus", "hello"));
}

TEST_F(XClusterAdminCliTest, TestDeleteCDCStreamWithBootstrap) {
  const int kStreamUuidLength = 32;
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  string output =
      ASSERT_RESULT(RunAdminToolCommandOnProducer("bootstrap_cdc_producer", producer_table->id()));
  // Get the bootstrap id (output format is "table id: 123, CDC bootstrap id: 123\n").
  string bootstrap_id = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id(),
      bootstrap_id));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id()}));

  // Should fail as it should meet the conditions to be stopped.
  ASSERT_NOK(RunAdminToolCommandOnProducer("delete_cdc_stream", bootstrap_id));
  // Delete should work fine from deleting from universe.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestFailedSetupUniverseWithDeletion) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  string error_msg;
  // Setup universe replication, this should only return once complete.
  // First provide a non-existant table id.
  // ASSERT_NOK since this should fail.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id() + "-BAD"));

  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Universe should be deleted by BG cleanup
  ASSERT_NOK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));

  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_cluster_table->id()));
  std::this_thread::sleep_for(5s);
}

TEST_F(XClusterAdminCliTest, SetupTransactionalReplicationFailure) {
  client::TableHandle producer_table;
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);
  ASSERT_NOK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id(),
      "random_flag"));
}

TEST_F(XClusterAdminCliTest, SetupTransactionalReplicationWithYCQLTable) {
  client::TableHandle producer_table;
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  string error_msg;
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(
      &error_msg,
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id(),
      "transactional"));

  ASSERT_TRUE(
      error_msg.find("Transactional replication is not supported for non-YSQL tables") !=
      string::npos);
}

TEST_F(XClusterAdminCliTest, AllowAddTransactionTablet) {
  // We normally disable setting up transactional replication for CQL tables because the
  // implementation isn't quite complete yet.  It's fine to use it in tests, however.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_allow_ycql_transactional_xcluster) = true;

  // Create an identical table on the producer.
  client::TableHandle producer_table;
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);
  auto global_txn_table =
      YBTableName(YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  auto producer_global_txn_table_id =
      ASSERT_RESULT(client::GetTableId(producer_cluster_client_.get(), global_txn_table));
  auto consumer_global_txn_table_id =
      ASSERT_RESULT(client::GetTableId(client_.get(), global_txn_table));

  // Should be fine to add without any replication.
  ASSERT_OK(RunAdminToolCommandOnProducer("add_transaction_tablet", producer_global_txn_table_id));
  ASSERT_OK(RunAdminToolCommand("add_transaction_tablet", consumer_global_txn_table_id));

  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      producer_cluster_->GetMasterAddresses(),
      producer_table->id(),
      "transactional"));

  // Should be fine to add with transactional replication.
  ASSERT_OK(RunAdminToolCommandOnProducer("add_transaction_tablet", producer_global_txn_table_id));
  ASSERT_OK(RunAdminToolCommand("add_transaction_tablet", consumer_global_txn_table_id));
}

class XClusterAdminCliTest_Large : public XClusterAdminCliTest {
 public:
  void SetUp() override {
    // Skip this test in TSAN since the test will time out waiting
    // for table creation to finish.
    YB_SKIP_TEST_IN_TSAN();

    XClusterAdminCliTest::SetUp();
  }
  int num_tablet_servers() override { return 5; }
};

TEST_F(XClusterAdminCliTest_Large, TestBootstrapProducerPerformance) {
  const int table_count = 10;
  const int tablet_count = 5;
  const int expected_runtime_seconds = 15 * kTimeMultiplier;
  const std::string keyspace = "my_keyspace";
  std::vector<client::TableHandle> tables;

  for (int i = 0; i < table_count; i++) {
    client::TableHandle th;
    tables.push_back(th);

    // Build the table.
    client::YBSchemaBuilder builder;
    builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn(kValueColumn)->Type(DataType::INT32);

    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);

    const YBTableName table_name(
        YQL_DATABASE_CQL, keyspace, Format("bootstrap_producer_performance_test_table_$0", i));
    ASSERT_OK(producer_cluster_client_.get()->CreateNamespaceIfNotExists(
        table_name.namespace_name(), table_name.namespace_type()));

    ASSERT_OK(
        tables.at(i).Create(table_name, tablet_count, producer_cluster_client_.get(), &builder));
  }

  std::string table_ids = tables.at(0)->id();
  for (size_t i = 1; i < tables.size(); ++i) {
    table_ids += "," + tables.at(i)->id();
  }

  // Wait for load balancer to be idle until we call bootstrap_cdc_producer.
  // This prevents TABLET_DATA_TOMBSTONED errors when we make rpc calls.
  // Todo: need to improve bootstrap behaviour with load balancer.
  ASSERT_OK(WaitFor(
      [this, table_ids]() -> Result<bool> {
        return producer_cluster_client_->IsLoadBalancerIdle();
      },
      MonoDelta::FromSeconds(120 * kTimeMultiplier),
      "Waiting for load balancer to be idle"));

  // Add delays to all rpc calls to simulate live environment.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms) = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_throttle_threshold_bytes) = 0;

  // Check that bootstrap_cdc_producer returns within time limit.
  ASSERT_OK(WaitFor(
      [this, table_ids]() -> Result<bool> {
        auto res = RunAdminToolCommandOnProducer("bootstrap_cdc_producer", table_ids);
        return res.ok();
      },
      MonoDelta::FromSeconds(expected_runtime_seconds),
      "Waiting for bootstrap_cdc_producer to complete"));
}

}  // namespace tools
}  // namespace yb
