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
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_bg_tasks.h"
#include "yb/master/mini_master.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/logging_test_util.h"
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
        << ", bgtask run count: " << master::TEST_transaction_status_check_run_count()
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
  std::string mismatch_logline_ = "insufficient tablets detected";

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
    auto* leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    return leader_master->catalog_manager_impl().GetTransactionTablesVersion();
  }

  // Helper to verify number of tablets for global and local transaction status tables
  // using their table info.
  Status VerifyTransactionTableTablets(
      const std::string& local_txnstatus_name,
      size_t expected_global_tablets,
      size_t expected_local_tablets) {
    auto cm = VERIFY_RESULT(catalog_manager());

    // Get global transactions table info.
    auto table_id = VERIFY_RESULT(GetTableIDFromTableName("transactions"));
    auto global_table_info = VERIFY_RESULT(cm->FindTableById(table_id));
    LOG(INFO) << "Global table info: " << global_table_info->name()
              << ", table ID: " << global_table_info->id()
              << ", tablets: " << global_table_info->TabletCount();

    // Get local transactions table info.
    table_id = VERIFY_RESULT(GetTableIDFromTableName(local_txnstatus_name));
    auto local_table_info = VERIFY_RESULT(cm->FindTableById(table_id));
    LOG(INFO) << "Local Table info: " << local_table_info->name()
              << ", table ID: " << local_table_info->id()
              << ", tablets: " << local_table_info->TabletCount();

    // Verify tablet counts.
    SCHECK_EQ(global_table_info->TabletCount(), expected_global_tablets, IllegalState,
              Format("Global transaction table tablet count mismatch. Expected: $0, Got: $1",
                     expected_global_tablets, global_table_info->TabletCount()));
    SCHECK_EQ(local_table_info->TabletCount(), expected_local_tablets, IllegalState,
              Format("Local transaction table tablet count mismatch. Expected: $0, Got: $1",
                     expected_local_tablets, local_table_info->TabletCount()));

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
    return WaitFor([&]() -> Result<bool> {
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

  // Helper to sleep and return background task run count.
  int32_t SleepAndGetBackgroundTaskRunCount() {
    SleepFor(MonoDelta::FromSeconds(FLAGS_transaction_table_check_interval_sec * 3 + 2));
    return master::TEST_transaction_status_check_run_count();
  }

  std::string RebootTriggerLogline() {
    return Format("Number of live tservers changed from 0 to $0",
      cluster_->num_tablet_servers());
  }

  Status WaitUntilBgTaskRunCountExceeds(int32_t prev_run_count) {
    return WaitFor([&]() -> Result<bool> {
      return master::TEST_transaction_status_check_run_count() > prev_run_count;
    }, MonoDelta::FromSeconds(60), "Waiting for background task to trigger");
  }

  Status WaitForGlobalTxnStatusTableCreation(MonoDelta timeout = MonoDelta::FromSeconds(30)) {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    return WaitFor([&]() -> Result<bool> {
      auto result = client->GetTransactionStatusTablets(CloudInfoPB());
      if (!result.ok()) {
        return false;
      }
      return !result->global_tablets.empty();
    }, timeout, "Waiting for global transaction status table to be created");
  }

  // Helper to wait reliably for the background task to run once after the initial SetUp.
  // Only used by reboot testcases.
  void WaitForBgTaskRunAfterInitialSetUp() {
    ASSERT_OK(WaitForGlobalTxnStatusTableCreation());
    // This testcase sets up only one tserver. There may be a race between the first tserver
    // registration and the first background task run. If the background task runs before the
    // tserver registration, waiting for that event will timeout since the check interval is 900s.
    // So reduce the check interval to 30s before waiting.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_check_interval_sec) = 30;
    ASSERT_OK(WaitUntilBgTaskRunCountExceeds(0));
  }
};

// Test that the transaction status check runs once after every boot.
// It should have nothing to do when tserver/config did not change.
TEST_F(MasterTxnStatusCheck, RebootNoChange) {
  WaitForBgTaskRunAfterInitialSetUp();
  // Create a log waiter to wait for the message that shows the check has triggered on boot.
  StringWaiterLogSink log_sink(RebootTriggerLogline());
  // Create a log waiter to wait for the mismatch message.
  PatternWaiterLogSink<std::string> log_sink2(mismatch_logline_);

  auto run_count_before = master::TEST_transaction_status_check_run_count();

  // Restart the cluster.
  ASSERT_OK(cluster_->RestartSync());

  // Background task should trigger on every boot.
  // But no action is taken since tserver/config did not change.
  ASSERT_OK(WaitUntilBgTaskRunCountExceeds(run_count_before));
  // Trigger message should have been logged.
  ASSERT_EQ(log_sink.GetEventCount(), 1);
  // No mismatch message should have been logged.
  ASSERT_EQ(log_sink2.GetEventCount(), 0);
}

// Test that the transaction status check does not take any action on scaling down.
// It should trigger but do nothing.
TEST_F(MasterTxnStatusCheck, NoActionOnScaleDown) {
  WaitForBgTaskRunAfterInitialSetUp();
  // Create a log waiter to wait for the trigger message on reboot.
  StringWaiterLogSink log_sink_scale_down(RebootTriggerLogline());
  // Create a log waiter to wait for the mismatch message.
  PatternWaiterLogSink<std::string> log_sink2(mismatch_logline_);
  // Simulates scale down by rebooting with flag changes.
  // Alternative: reduce the tserver count.
  // Decrease the number of tablets flag to a smaller number.
  int32_t original_transaction_table_num_tablets = FLAGS_transaction_table_num_tablets;
  ASSERT_GT(original_transaction_table_num_tablets, 1); // setup assumption
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) =
      original_transaction_table_num_tablets - 1;

  auto run_count_before = master::TEST_transaction_status_check_run_count();
  // Restart the cluster.
  ASSERT_OK(cluster_->RestartSync());
  // Background task should trigger on reboot.
  // But no action is taken since the number of tablets is more than the expected number.
  ASSERT_OK(WaitUntilBgTaskRunCountExceeds(run_count_before));
  ASSERT_EQ(log_sink_scale_down.GetEventCount(), 1);
  ASSERT_EQ(log_sink2.GetEventCount(), 0);
}

// Test that the background task does not trigger if the elapsed time since
// the last check (on boot) is less than the check interval.
TEST_F(MasterTxnStatusCheck, CheckInterval) {
  WaitForBgTaskRunAfterInitialSetUp();
  // Needed to scale up the number of tablets with tserver change.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets_per_tserver) = 2;
  // Decrease the check interval to a smaller value to speed up the test.
  const auto kCheckInterval = FLAGS_transaction_table_check_interval_sec;
  ASSERT_EQ(kCheckInterval, 30); // setup assumption

  auto run_count_before = master::TEST_transaction_status_check_run_count();
  // Add a new tserver.
  auto ts_opts = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(2));

  // Verify that the background task does not trigger if the elapsed time since
  // the last check (on boot) is less than the check interval.
  SleepFor(MonoDelta::FromSeconds(kCheckInterval / 2));
  ASSERT_EQ(master::TEST_transaction_status_check_run_count(), run_count_before);

  // It will eventually trigger after the check interval.
  ASSERT_OK(WaitUntilBgTaskRunCountExceeds(run_count_before));

  // Verify that the flag validator rejects zero and negative values.
  std::string old_value, output_msg;
  ASSERT_NE(
      flags_internal::SetFlagResult::SUCCESS,
      flags_internal::SetFlag(
          "transaction_table_check_interval_sec", "0",
          flags_internal::SetFlagForce::kFalse, &old_value, &output_msg));
  ASSERT_NE(
      flags_internal::SetFlagResult::SUCCESS,
      flags_internal::SetFlag(
          "transaction_table_check_interval_sec", "-1",
          flags_internal::SetFlagForce::kFalse, &old_value, &output_msg));
}

// Test that the transaction status check runs once after every boot.
// It detects and takes action if the relevant flags change.
TEST_F(MasterTxnStatusCheck, RebootFlagChange) {
  WaitForBgTaskRunAfterInitialSetUp();
  // Create a log waiter to wait for the trigger message on reboot.
  StringWaiterLogSink log_sink_scale_down(RebootTriggerLogline());
  // Create a log waiter to wait for the mismatch message.
  PatternWaiterLogSink<std::string> log_sink2(mismatch_logline_);

  // Increase the number of tablets flag to a bigger number.
  int32_t original_transaction_table_num_tablets = FLAGS_transaction_table_num_tablets;
  ASSERT_GE(original_transaction_table_num_tablets, 1); // setup assumption
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) =
      original_transaction_table_num_tablets + 1;

  auto run_count_before = master::TEST_transaction_status_check_run_count();

  // Restart the cluster.
  ASSERT_OK(cluster_->RestartSync());
  // Background task should trigger on boot and take action.
  ASSERT_OK(WaitUntilBgTaskRunCountExceeds(run_count_before));
  ASSERT_EQ(log_sink_scale_down.GetEventCount(), 1);
  ASSERT_EQ(log_sink2.GetEventCount(), 1);
}

// Test that the background task does not run when autoscale_transaction_tables is disabled.
// Adding a tserver and restarting should not trigger any scaling, and tablet counts should
// remain unchanged.
TEST_F(MasterTxnStatusCheck, Disabled) {
  WaitForBgTaskRunAfterInitialSetUp();

  // Turn off the feature flag.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_autoscale_transaction_tables) = false;

  // But setup the other flags for auto scaling with tserver change.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets_per_tserver) = 2;
  // Decrease the check interval to a smaller value.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_check_interval_sec) = 2;

  auto run_count_before = master::TEST_transaction_status_check_run_count();

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto txn_tablets = ASSERT_RESULT(client->GetTransactionStatusTablets(CloudInfoPB()));
  auto initial_global_tablets = txn_tablets.global_tablets.size();
  ASSERT_GT(initial_global_tablets, 0);

  // Add a new tserver.
  auto ts_opts = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(2));

  ASSERT_OK(cluster_->RestartSync());

  ASSERT_EQ(SleepAndGetBackgroundTaskRunCount(), run_count_before);
  txn_tablets = ASSERT_RESULT(client->GetTransactionStatusTablets(CloudInfoPB()));
  ASSERT_EQ(txn_tablets.global_tablets.size(), initial_global_tablets);
}

// Test auto scaling of transaction status tables at runtime.
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

  // Background task runs once each time tserver joins.
  ASSERT_EQ(SleepAndGetBackgroundTaskRunCount(), 1) << "init";
  // Note placement info from the initial tserver (default placement).
  auto* initial_tserver = cluster_->mini_tablet_server(0);
  std::string initial_cloud = initial_tserver->options()->placement_cloud();
  std::string initial_region = initial_tserver->options()->placement_region();
  std::string initial_zone = initial_tserver->options()->placement_zone();

  CloudInfoPB initial_cloud_info;
  initial_cloud_info.set_placement_cloud(initial_cloud);
  initial_cloud_info.set_placement_region(initial_region);
  initial_cloud_info.set_placement_zone(initial_zone);

  LOG(INFO) << "Initial cloud: " << initial_cloud
            << ", Initial region: " << initial_region
            << ", Initial zone: " << initial_zone
            << ", Global transactions Table ID: " << GetTableIDFromTableName("transactions");

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

  // Background task runs once more when tserver (zone2) joins.
  ASSERT_EQ(SleepAndGetBackgroundTaskRunCount(), 2) << "after tserver1 (zone2) joins";

  // Verify the tablets are as expected. This table info based check
  // is guaranteed to work since the background task has already added
  // the tablets by now. But the tablets may not be physically created
  // yet.
  ASSERT_OK(VerifyTransactionTableTablets(
      local_txnstatus_name,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      1 * FLAGS_transaction_table_num_tablets_per_tserver));

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
      1 * FLAGS_transaction_table_num_tablets_per_tserver));

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
      1 * FLAGS_transaction_table_num_tablets_per_tserver,
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

  // Verify background task ran once more.
  ASSERT_EQ(SleepAndGetBackgroundTaskRunCount(), 3) << "after tserver3 (zone3) joins";

  ASSERT_OK(VerifyTransactionTableTablets(
      local_txnstatus_name,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      1 * FLAGS_transaction_table_num_tablets_per_tserver));

  // Wait until the tablet server finishes creating the tablets.
  ASSERT_OK(WaitForTransactionStatusTablets(
      zone3_cloud_info,
      3 * FLAGS_transaction_table_num_tablets_per_tserver,
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
      1 * FLAGS_transaction_table_num_tablets_per_tserver,
      "Zone2"));

  ASSERT_OK(VerifyTransactionStatusTabletsFromClient(
      zone3_cloud_info,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      0,
      "Zone3"));

  // (3) Add another tablet server, but to the same placement (zone2).
  auto ts_opts_new4 = ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  ts_opts_new4.SetPlacement("test_cloud", "test_region", "zone2");
  ASSERT_OK(cluster_->AddTabletServer(ts_opts_new4, true));
  ASSERT_OK(cluster_->WaitForTabletServerCount(4));

  // Verify background task ran once more.
  ASSERT_EQ(SleepAndGetBackgroundTaskRunCount(), 4) << "after tserver4 (zone2) joins";

  ASSERT_OK(VerifyTransactionTableTablets(
      local_txnstatus_name,
      cluster_->num_tablet_servers() * FLAGS_transaction_table_num_tablets_per_tserver,
      2 * FLAGS_transaction_table_num_tablets_per_tserver));

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

}  // namespace yb
