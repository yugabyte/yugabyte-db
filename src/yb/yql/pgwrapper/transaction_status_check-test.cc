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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/transaction_status_tablets.h"

#include "yb/common/transaction.h"

#include "yb/integration-tests/master_test_api.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_parser.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::chrono_literals;

DECLARE_int32(transaction_table_check_interval_sec);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(transaction_table_num_tablets_per_tserver);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(TEST_name_transaction_tables_with_tablespace_id);
DECLARE_int32(replication_factor);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(autoscale_transaction_tables);
DECLARE_bool(TEST_check_broadcast_address);
DECLARE_int32(ysql_tablespace_info_refresh_secs);

namespace yb {

class MasterTxnStatusCheck : public pgwrapper::PgMiniTestBase {

 public:
  size_t NumTabletServers() override {
    return 1;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    pgwrapper::PgMiniTestBase::SetUp();
    LOG(INFO) << "MasterTxnStatusCheck SetUp: "
        << "tservers: " << cluster_->num_tablet_servers()
        << ", flag:autoscale: " << FLAGS_autoscale_transaction_tables
        << ", flag:check_interval_sec: " << FLAGS_transaction_table_check_interval_sec
        << ", flag:num_tablets: " << FLAGS_transaction_table_num_tablets
        << ", flag:num_tablets_per_tserver: " << FLAGS_transaction_table_num_tablets_per_tserver
        << ", flag:broadcast_address: " << FLAGS_TEST_check_broadcast_address
        << ", flag:load_balancing: " << FLAGS_enable_load_balancing;
    ASSERT_TRUE(FLAGS_autoscale_transaction_tables)
        << "autoscale_transaction_tables must be enabled (default is true) for this test";
  }

 protected:
  // Helper to get tablespace OID from PostgreSQL.
  Result<uint32_t> GetTablespaceOid(const std::string& tablespace_name) {
    auto conn = VERIFY_RESULT(Connect());
    auto result = VERIFY_RESULT(conn.FetchFormat(
        "SELECT oid FROM pg_tablespace WHERE spcname = '$0'", tablespace_name));
    if (PQntuples(result.get()) == 0) {
      return STATUS_FORMAT(NotFound, "Tablespace $0 not found", tablespace_name);
    }
    return VERIFY_RESULT(pgwrapper::GetValue<pgwrapper::PGOid>(result.get(), 0, 0));
  }

  // Helper to create a tablespace and return its OID.
  Result<uint32_t> CreateTablespaceAndGetOid(
      const std::string& tablespace_name,
      const std::string& cloud = "test_cloud",
      const std::string& region = "test_region",
      const std::string& zone = "zone2",
      int num_replicas = 1) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(R"#(
        CREATE TABLESPACE $0 WITH (replica_placement='{
          "num_replicas": $1,
          "placement_blocks": [{
            "cloud": "$2",
            "region": "$3",
            "zone": "$4",
            "min_num_replicas": $1
          }]
        }')
      )#", tablespace_name, num_replicas, cloud, region, zone));
    return GetTablespaceOid(tablespace_name);
  }

  // Helper to get current transaction status table version.
  Result<uint64_t> GetCurrentTransactionTablesVersion() {
    auto rows = VERIFY_RESULT(MasterTestApi(cluster_.get()).DumpRows(
        master::SysRowEntryType::SYS_CONFIG, master::kTransactionTablesConfigType));
    for (const auto& [id, text] : rows) {
      if (id != master::kTransactionTablesConfigType) {
        continue;
      }
      auto pb = VERIFY_RESULT(
          master::DebugStringToCatalogEntityPB(master::SysRowEntryType::SYS_CONFIG, text));
      return static_cast<master::SysConfigEntryPB*>(pb.get())
          ->transaction_tables_config()
          .version();
    }
    return 0;
  }

  // Helper to verify number of tablets for global and local transaction status tables
  // using their table info.
  Status VerifyTransactionTableTablets(
      const std::string& txn_table_name,
      size_t expected_tablets) {
    auto table_id = VERIFY_RESULT(GetTableIDFromTableName(txn_table_name));
    const auto num_tablets =
        VERIFY_RESULT(MasterTestApi(cluster_.get()).GetTablets(table_id)).size();
    LOG(INFO) << "Txn table: " << txn_table_name << ", table ID: " << table_id
              << ", tablets: " << num_tablets;

    SCHECK_EQ(
        num_tablets, expected_tablets, IllegalState,
        Format(
            "Txn transaction table $0 tablet count mismatch. Expected: $1, Got: $2",
            txn_table_name, expected_tablets, num_tablets));
    return Status::OK();
  }

  // Helper to verify the tablets available for a placement using the client API.
  Status VerifyTransactionStatusTabletsFromClient(
      const CloudInfoPB& cloud_info,
      size_t expected_global_tablets,
      size_t expected_local_tablets,
      const std::string& log_prefix) {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    auto txn_tablets = VERIFY_RESULT(client->GetTransactionStatusTablets(cloud_info));
    size_t global_tablet_count = txn_tablets.global_tablets.size();
    size_t local_tablet_count = txn_tablets.region_local_tablets.size();

    LOG(INFO) << "" << log_prefix << " Global tablet count: " << global_tablet_count
              << ", " << log_prefix << " Local tablet count: " << local_tablet_count;

    SCHECK_EQ(global_tablet_count, expected_global_tablets, IllegalState,
              Format("Global transaction tablet count mismatch. Expected: $0, Got: $1",
                     expected_global_tablets, global_tablet_count));
    SCHECK_EQ(local_tablet_count, expected_local_tablets, IllegalState,
              Format("Local transaction tablet count mismatch. Expected: $0, Got: $1",
                     expected_local_tablets, local_tablet_count));

    return Status::OK();
  }

  // Helper to wait for transaction status tablets to reach expected counts.
  Status WaitForTransactionStatusTablets(
      const CloudInfoPB& cloud_info,
      size_t expected_global_tablets,
      size_t expected_local_tablets,
      MonoDelta timeout = MonoDelta::FromSeconds(30)) {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    return WaitFor([&] -> Result<bool> {
      auto result = client->GetTransactionStatusTablets(cloud_info);
      if (!result.ok()) {
        return false;
      }
      auto txn_tablets = *result;
      LOG(INFO) << "global tablet count: " << txn_tablets.global_tablets.size()
                << ", local tablet count: " << txn_tablets.region_local_tablets.size();
      return txn_tablets.global_tablets.size() == expected_global_tablets &&
             txn_tablets.region_local_tablets.size() == expected_local_tablets;
    }, timeout, "Waiting for tablet server to finish creating tablets");
  }

  Status WaitForGlobalTxnStatusTableCreation(MonoDelta timeout = MonoDelta::FromSeconds(30)) {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    return WaitFor([&] -> Result<bool> {
      auto result = client->GetTransactionStatusTablets(CloudInfoPB());
      if (!result.ok()) {
        return false;
      }
      return !result->global_tablets.empty();
    }, timeout, "Waiting for global transaction status table to be created");
  }

};

TEST_F(MasterTxnStatusCheck, AutoScale) {
  // Decrease the transaction status check interval to 2 seconds
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_check_interval_sec) = 2;
  // Reset transaction_table_num_tablets to test auto scaling (up) with tserver changes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 0;
  // Set transaction_table_num_tablets_per_tserver to a fixed value for deterministic test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets_per_tserver) = 2;
  // Enable auto_create_local_transaction_tables.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  // Enable name_transaction_tables_with_tablespace_id for deterministic test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_name_transaction_tables_with_tablespace_id) = true;

  // Test assumptions.
  ASSERT_EQ(cluster_->num_tablet_servers(), 1);

  // Wait for the global transaction status table to be created.
  ASSERT_OK(WaitForGlobalTxnStatusTableCreation());

  // Note placement info from the initial tserver (default placement).
  auto* initial_tserver = cluster_->mini_tablet_server(0);
  std::string initial_cloud = initial_tserver->options()->placement_cloud();
  std::string initial_region = initial_tserver->options()->placement_region();
  std::string initial_zone = initial_tserver->options()->placement_zone();

  CloudInfoPB initial_cloud_info;
  initial_cloud_info.set_placement_cloud(initial_cloud);
  initial_cloud_info.set_placement_region(initial_region);
  initial_cloud_info.set_placement_zone(initial_zone);

  LOG(INFO) << Format(
      "Initial cloud: $0, Initial region: $1, Initial zone: $2, Global transactions Table ID: $3",
      initial_cloud, initial_region, initial_zone,
      GetTableIDFromTableName(kGlobalTransactionsTableName));

  // (1) Create a local transaction status table.

  // A new placement (zone2).
  CloudInfoPB zone2_cloud_info;
  zone2_cloud_info.set_placement_cloud("test_cloud");
  zone2_cloud_info.set_placement_region("test_region");
  zone2_cloud_info.set_placement_zone("zone2");

  // 1.1 Create a tablespace for the new placement (zone2) using SQL.
  std::string tablespace_name = "tablespace_zone2";
  auto tablespace_oid = ASSERT_RESULT(CreateTablespaceAndGetOid(tablespace_name));

  // 1.2 Add a new tablet server in the new placement (zone2).
  // Tserver must already be started before we create the
  // local transaction status table (else creation will fail).
  auto ts_opts_new = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts_new.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts_new, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(2));

  auto initial_version = ASSERT_RESULT(GetCurrentTransactionTablesVersion());
  // 1.3 Create a test table in the tablespace, this will trigger the creation
  // of the local transaction status table transactions_<tablespace_oid>.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_table_in_zone2 (key INT PRIMARY KEY) TABLESPACE $0",
      tablespace_name));

  // Local transaction status table creation would bump the transaction table version.
  auto current_version = ASSERT_RESULT(GetCurrentTransactionTablesVersion());
  ASSERT_GE(current_version, initial_version+1);

  std::string local_txnstatus_name  = "transactions_" + std::to_string(tablespace_oid);
  // Find the name of the transaction status table that was created.
  LOG(INFO) << "Local transaction status table created "
            << local_txnstatus_name << "[ "
            << GetTableIDFromTableName(local_txnstatus_name) << "]";

  // Verify the tablets are as expected. This table info based check
  // is guaranteed to work since the background task has already added
  // the tablets by now. But the tablets may not be physically created
  // yet.
  ASSERT_OK(WaitFor(
      [&] -> Result<bool> {
        return VerifyTransactionTableTablets(
                   kGlobalTransactionsTableName,
                   cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok() &&
               VerifyTransactionTableTablets(
                   local_txnstatus_name, FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok();
      },
      MonoDelta::FromSeconds(30),
      "Waiting for background task to add a tablet to the global txn table"));

  // Wait until the tablet server finishes creating the tablets.
  // GetTransactionStatusTablets will return service not available
  // until the tablets are created.
  // RESOLVE: Technically this is not the background task's work,
  // but nice to test. This is done asynchronously after tablet
  // is added. It could take indeterminate time to finish.
  // This sometimes runs into issues if there's a tablet
  // movement. So turned off the load balancer.
  // Need to ensure this check will not be flaky.
  ASSERT_OK(WaitForTransactionStatusTablets(
      zone2_cloud_info,
      2 * FLAGS_transaction_table_num_tablets_per_tserver,
      FLAGS_transaction_table_num_tablets_per_tserver));

  // Verify the number of tablets available for global and local transaction
  // status tables for all the 3 placements is as expected. This is the API
  // used by transaction coordinator.
  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      initial_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0,
      "Original"));

  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone2_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      FLAGS_transaction_table_num_tablets_per_tserver,
      "Zone2"));

  CloudInfoPB zone3_cloud_info;
  zone3_cloud_info.set_placement_cloud("test_cloud");
  zone3_cloud_info.set_placement_region("test_region");
  zone3_cloud_info.set_placement_zone("zone3");
  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone3_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0,
      "Zone3"));

  // (2) Add another tablet server, but to a different placement (zone3).
  auto ts_opts_new3 = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts_new3.SetPlacement("test_cloud", "test_region", "zone3");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts_new3, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));

  ASSERT_OK(WaitFor(
      [&] -> Result<bool> {
        return VerifyTransactionTableTablets(
                   kGlobalTransactionsTableName,
                   cluster_->num_tablet_servers() *
                       FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok() &&
               VerifyTransactionTableTablets(
                   local_txnstatus_name, FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok();
      },
      MonoDelta::FromSeconds(30),
      "Waiting for background task to add a tablet to the global txn table"));

  // Wait until the tablet server finishes creating the tablets.
  // We don't expect any transaction tablets in zone3 because we never created a tablespace or a
  // table in that zone, local transaction tables are created lazily when needed.
  ASSERT_OK(WaitForTransactionStatusTablets(
      zone3_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0));

  // Verify the number of tablets for all the 3 placements.
  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      initial_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0,
      "Original"));

  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone2_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      FLAGS_transaction_table_num_tablets_per_tserver,
      "Zone2"));

  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone3_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver, 0,
      "Zone3"));

  // Wait until the tablet server finishes creating the tablets.
  ASSERT_OK(WaitForTransactionStatusTablets(
      zone3_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0));

  // Restart the master leader to confirm changes are persisted to the sys catalog.
  LOG(INFO) << "Stepping down leader master";
  ASSERT_OK(MasterTestApi(cluster_.get()).StepDownLeader());

  // (3) Add another tablet server, but to the same placement (zone2).
  auto ts_opts_new4 = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts_new4.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts_new4, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(4));

  // Verify background task ran once more.
  ASSERT_OK(WaitFor(
      [&] -> Result<bool> {
        return VerifyTransactionTableTablets(
                   kGlobalTransactionsTableName,
                   cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok() &&
               VerifyTransactionTableTablets(
                   local_txnstatus_name, 2 * FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok();
      },
      MonoDelta::FromSeconds(30),
      "Waiting for background task to add a tablet to the global txn table"));

  // Wait until the tablet server finishes creating the tablets.
  ASSERT_OK(WaitForTransactionStatusTablets(
      zone2_cloud_info,
      4 * FLAGS_transaction_table_num_tablets_per_tserver,
      2 * FLAGS_transaction_table_num_tablets_per_tserver));

  // Verify the number of tablets for all the 3 placements.
  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      initial_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0,
      "Original"));

  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone2_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      2 * FLAGS_transaction_table_num_tablets_per_tserver,
      "Zone2"));

  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone3_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0,
      "Zone3"));

}

TEST_F(MasterTxnStatusCheck, AutoScaleRetriesWhenTablespaceInfoUnavailable) {
  // Scale-up check fires every 2 seconds.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_check_interval_sec) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets_per_tserver) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_name_transaction_tables_with_tablespace_id) = true;

  ASSERT_EQ(cluster_->num_tablet_servers(), 1);
  ASSERT_OK(WaitForGlobalTxnStatusTableCreation());

  // -- Setup: create tablespace, add a tserver for it, and create a table to trigger
  // local transaction table creation.
  CloudInfoPB zone2_cloud_info;
  zone2_cloud_info.set_placement_cloud("test_cloud");
  zone2_cloud_info.set_placement_region("test_region");
  zone2_cloud_info.set_placement_zone("zone2");

  std::string tablespace_name = "tablespace_zone2";
  auto tablespace_oid = ASSERT_RESULT(CreateTablespaceAndGetOid(tablespace_name));

  auto ts_opts1 = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts1.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts1, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(2));

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_table (key INT PRIMARY KEY) TABLESPACE $0", tablespace_name));

  std::string local_txnstatus_name = "transactions_" + std::to_string(tablespace_oid);
  LOG(INFO) << "Local transaction status table: " << local_txnstatus_name;

  ASSERT_OK(WaitFor(
      [&] -> Result<bool> {
        return VerifyTransactionTableTablets(
                   local_txnstatus_name, FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok() &&
               VerifyTransactionTableTablets(
                   kGlobalTransactionsTableName,
                   2 * FLAGS_transaction_table_num_tablets_per_tserver)
                   .ok();
      },
      MonoDelta::FromSeconds(30), "Waiting for a local transaction table"));

  // Disable the ysql tablespace refresh task on the master leader, restart the master leader, and
  // add another tablet server. Because tablespace information is not available, the auto scale up
  // logic should not be able to add another tablet to the local txn table. But it should succeed in
  // adding another tablet to the global txn table.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_tablespace_info_refresh_secs) = 0;
  LOG(INFO) << "Stepping down leader master. Ysql tablespace refresh task should not run.";
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Restart());

  auto ts_opts2 = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts2.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts2, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));

  // Scaling up the global table should have worked.
  ASSERT_OK(WaitFor(
      [&] -> Result<bool> {
        return VerifyTransactionTableTablets(
                   kGlobalTransactionsTableName,
                   3 * FLAGS_transaction_table_num_tablets_per_tserver)
            .ok();
      },
      MonoDelta::FromSeconds(30), "Waiting for global transaction tablet"));

  // But scaling up the local table should not have worked.
  ASSERT_OK(VerifyTransactionTableTablets(
      local_txnstatus_name, FLAGS_transaction_table_num_tablets_per_tserver));

  // Re-enable the background ysql table space refresher task so the auto scale up logic can get the
  // replication info for the local txn table and scale it up.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_tablespace_info_refresh_secs) = 5;
  ASSERT_OK(WaitFor(
      [&] -> Result<bool> {
        return VerifyTransactionTableTablets(
                   local_txnstatus_name, 2 * FLAGS_transaction_table_num_tablets_per_tserver)
            .ok();
      },
      MonoDelta::FromSeconds(30), "Waiting for transaction tablets in the local table"));
}

}  // namespace yb
