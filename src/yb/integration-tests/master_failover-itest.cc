// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client-internal.h"
#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/monitored_task.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(ycql_num_tablets);
DECLARE_int32(ysql_num_tablets);

namespace yb {

// Note: this test needs to be in the client namespace in order for
// YBClient::Data class methods to be visible via FRIEND_TEST macro.
namespace client {

const int kNumTabletServerReplicas = 3;
const int kHeartbeatIntervalMs = 500;

using std::shared_ptr;
using std::string;
using std::vector;
using client::YBTableName;

class MasterFailoverTest : public YBTest {
 public:
  enum CreateTableMode {
    kWaitForCreate = 0,
    kNoWaitForCreate = 1
  };

  MasterFailoverTest() {
    opts_.master_rpc_ports = { 0, 0, 0 };
    opts_.num_masters = num_masters_ = opts_.master_rpc_ports.size();
    opts_.num_tablet_servers = kNumTabletServerReplicas;

    // Reduce various timeouts below as to make the detection of
    // leader master failures (specifically, failures as result of
    // long pauses) more rapid.

    // Set the TS->master heartbeat timeout to 1 second (down from 15 seconds).
    opts_.extra_tserver_flags.push_back("--heartbeat_rpc_timeout_ms=1000");
    // Allow one TS heartbeat failure before retrying with back-off (down from 3).
    opts_.extra_tserver_flags.push_back("--heartbeat_max_failures_before_backoff=1");
    // Wait for 500 ms after 'max_consecutive_failed_heartbeats'
    // before trying again (down from 1 second).
    string heartbeat_interval_flag =
                "--heartbeat_interval_ms="+std::to_string(kHeartbeatIntervalMs);
    opts_.extra_tserver_flags.push_back(heartbeat_interval_flag);
  }

  void SetUp() override {
    YBTest::SetUp();
    ASSERT_NO_FATALS(RestartCluster());
  }

  void TearDown() override {
    client_.reset();
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  void RestartCluster() {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }
    opts_.timeout = MonoDelta::FromSeconds(NonTsanVsTsan(20, 60));
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  Status CreateTable(const YBTableName& table_name, CreateTableMode mode) {
    RETURN_NOT_OK_PREPEND(
        client_->CreateNamespaceIfNotExists(table_name.namespace_name()),
        "Unable to create namespace " + table_name.namespace_name());
    client::YBSchema client_schema(client::YBSchemaFromSchema(yb::GetSimpleTestSchema()));
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    return table_creator->table_name(table_name)
        .table_type(YBTableType::YQL_TABLE_TYPE)
        .schema(&client_schema)
        .hash_schema(YBHashSchema::kMultiColumnHash)
        .timeout(MonoDelta::FromSeconds(90))
        .wait(mode == kWaitForCreate)
        .Create();
  }

  Status CreateIndex(
      const YBTableName& indexed_table_name, const YBTableName& index_name, CreateTableMode mode) {
    RETURN_NOT_OK_PREPEND(
      client_->CreateNamespaceIfNotExists(index_name.namespace_name()),
      "Unable to create namespace " + index_name.namespace_name());
    client::YBSchema client_schema(client::YBSchemaFromSchema(yb::GetSimpleTestSchema()));
    client::TableHandle table;
    RETURN_NOT_OK_PREPEND(
        table.Open(indexed_table_name, client_.get()),
        "Unable to open table " + indexed_table_name.ToString());

    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    return table_creator->table_name(index_name)
        .table_type(YBTableType::YQL_TABLE_TYPE)
        .indexed_table_id(table->id())
        .schema(&client_schema)
        .hash_schema(YBHashSchema::kMultiColumnHash)
        .timeout(MonoDelta::FromSeconds(90))
        .wait(mode == kWaitForCreate)
        // In the new style create index request, the CQL proxy populates the
        // index info instead of the master. However, in these tests we bypass
        // the proxy and go directly to the master. We need to use the old
        // style create request to have the master generate the appropriate
        // index info.
        .TEST_use_old_style_create_request()
        .Create();
  }

  Status RenameTable(const YBTableName& table_name_orig, const YBTableName& table_name_new) {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(table_name_orig));
    return table_alterer
      ->RenameTo(table_name_new)
      ->timeout(MonoDelta::FromSeconds(90))
      ->wait(true)
      ->Alter();
  }

  // Test that we can get the table location information from the
  // master and then open scanners on the tablet server. This involves
  // sending RPCs to both the master and the tablet servers and
  // requires that the table and tablet exist both on the masters and
  // the tablet servers.
  Status OpenTableAndScanner(const YBTableName& table_name) {
    client::TableHandle table;
    RETURN_NOT_OK_PREPEND(table.Open(table_name, client_.get()),
                          "Unable to open table " + table_name.ToString());
    client::TableRange range(table);
    auto it = range.begin();
    if (it != range.end()) {
      ++it;
    }

    return Status::OK();
  }

 protected:
  size_t num_masters_;
  ExternalMiniClusterOptions opts_;
  std::unique_ptr<ExternalMiniCluster> cluster_;
  std::unique_ptr<YBClient> client_;
};

class MasterFailoverTestIndexCreation : public MasterFailoverTest,
                                        public ::testing::WithParamInterface<int> {
 public:
  MasterFailoverTestIndexCreation() {
    opts_.extra_tserver_flags.push_back("--allow_index_table_read_write=true");
    opts_.extra_tserver_flags.push_back(
        "--index_backfill_upperbound_for_user_enforced_txn_duration_ms=100");
    opts_.extra_master_flags.push_back("--TEST_slowdown_backfill_alter_table_rpcs_ms=50");
    opts_.extra_master_flags.push_back("--disable_index_backfill=false");
    // Sometimes during master failover we have the create index kick in before the tservers have
    // checked in. By default we wait for enough TSs -- else we fail the create table/idx request.
    // We don't have to wait for that in the tests here.
    opts_.extra_master_flags.push_back("--catalog_manager_check_ts_count_for_create_table=false");
  }

  // Master has to do 5 RPCs to TServers to create+backfill an index.
  // 4 corresponding to set each of the 4 IndexPermissions, and 1 for GetSafeTime.
  // We want to simulate a failure before and after each RPC, so total 10 stages.
  static constexpr int kNumMaxStages = 10;
};

INSTANTIATE_TEST_CASE_P(
    MasterFailoverTestIndexCreation, MasterFailoverTestIndexCreation,
    ::testing::Range(1, MasterFailoverTestIndexCreation::kNumMaxStages));
// Test that synchronous CreateTable (issue CreateTable call and then
// wait until the table has been created) works even when the original
// leader master has been paused.
//
// Temporarily disabled since multi-master isn't supported yet.
// This test fails as of KUDU-1138, since the tablet servers haven't
// registered with the follower master, and thus it's likely to deny
// the CreateTable request thinking there are no TS available.
TEST_F(MasterFailoverTest, DISABLED_TestCreateTableSync) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  YBTableName table_name(YQL_DATABASE_CQL, "testCreateTableSync");
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));
  ASSERT_OK(OpenTableAndScanner(table_name));
}

// Test that we can issue a CreateTable call, pause the leader master
// immediately after, then verify that the table has been created on
// the newly elected leader master.
//
// TODO enable this test once flakiness issues are worked out and
// eliminated on test machines.
TEST_F(MasterFailoverTest, DISABLED_TestPauseAfterCreateTableIssued) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  YBTableName table_name(YQL_DATABASE_CQL, "testPauseAfterCreateTableIssued");
  LOG(INFO) << "Issuing CreateTable for " << table_name.ToString();
  ASSERT_OK(CreateTable(table_name, kNoWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  auto deadline = CoarseMonoClock::Now() + 90s;
  ASSERT_OK(client_->data_->WaitForCreateTableToFinish(client_.get(), table_name, "" /* table_id */,
                                                       deadline));

  ASSERT_OK(OpenTableAndScanner(table_name));
}

// Test that we can create a namespace, trigger a leader master failover, then verify that the
// new namespace is usable.
TEST_F(MasterFailoverTest, TestFailoverAfterNamespaceCreated) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  // Create namespace.
  constexpr auto kNamespaceFailoverName = "testNamespaceFailover";
  LOG(INFO) << "Issuing CreateNamespace for " << kNamespaceFailoverName;
  ASSERT_OK(client_->CreateNamespace(kNamespaceFailoverName, YQLDatabase::YQL_DATABASE_PGSQL));

  auto deadline = CoarseMonoClock::Now() + 90s;
  ASSERT_OK(client_->data_->WaitForCreateNamespaceToFinish(
      client_.get(), kNamespaceFailoverName, YQLDatabase::YQL_DATABASE_PGSQL, "" /* namespace_id */,
      deadline));

  // Failover the leader.
  LOG(INFO) << "Failing over master leader.";
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());

  // Create a table in new namespace to make sure it is usable.
  YBTableName table_name(
      YQL_DATABASE_PGSQL, kNamespaceFailoverName, "testNamespaceFailover" /* table_name */);
  LOG(INFO) << "Issuing CreateTable for " << table_name.ToString();
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));
}

// Orchestrate a master failover at various points of a backfill,
// ensure that the backfill eventually completes.
TEST_P(MasterFailoverTestIndexCreation, TestPauseAfterCreateIndexIssued) {
  const int kPauseAfterStage = GetParam();
  YBTableName table_name(YQL_DATABASE_CQL, "test", "testPauseAfterCreateTableIssued");
  LOG(INFO) << "Issuing CreateTable for " << table_name.ToString();
  FLAGS_ycql_num_tablets = 5;
  FLAGS_ysql_num_tablets = 5;
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));
  LOG(INFO) << "CreateTable done for " << table_name.ToString();

  MonoDelta total_time_taken_for_one_iteration;
  // In the first run, we estimate the total time taken for one create index to complete.
  // The second run will pause the master at the desired point during create index.
  for (int i = 0; i < 2; i++) {
    auto start = ToSteady(CoarseMonoClock::Now());
    auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
    ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

    OpIdPB op_id;
    ASSERT_OK(cluster_->GetLastOpIdForLeader(&op_id));
    ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(static_cast<int>(op_id.index())));

    YBTableName index_table_name(
        YQL_DATABASE_CQL, "test", "testPauseAfterCreateTableIssuedIdx" + yb::ToString(i));
    LOG(INFO) << "Issuing CreateIndex for " << index_table_name.ToString();
    ASSERT_OK(CreateIndex(table_name, index_table_name, kNoWaitForCreate));

    if (i != 0) {
      // In the first run, we estimate how long it takes for an uninterrupted
      // backfill process to complete, then the remaining iterations kill the
      // master leader at various points to cause the  master failover during
      // the various stages of index backfill.
      MonoDelta sleep_time = total_time_taken_for_one_iteration * kPauseAfterStage / kNumMaxStages;
      LOG(INFO) << "Sleeping for " << sleep_time << ", before master pause";
      SleepFor(sleep_time);

      LOG(INFO) << "Pausing leader master 0-based: " << leader_idx << " i.e. m-"
                << 1 + leader_idx;
      ASSERT_OK(cluster_->master(leader_idx)->Pause());
    }

    IndexInfoPB index_info_pb;
    TableId index_table_id;
    const auto deadline = CoarseMonoClock::Now() + 900s;
    do {
      ASSERT_OK(client_->data_->WaitForCreateTableToFinish(
          client_.get(), index_table_name, "" /* table_id */, deadline));
      ASSERT_OK(client_->data_->WaitForCreateTableToFinish(
          client_.get(), table_name, "" /* table_id */, deadline));

      Result<YBTableInfo> table_info = client_->GetYBTableInfo(table_name);
      ASSERT_TRUE(table_info);
      Result<YBTableInfo> index_table_info = client_->GetYBTableInfo(index_table_name);
      ASSERT_TRUE(index_table_info);

      index_table_id = index_table_info->table_id;
      index_info_pb.Clear();
      table_info->index_map[index_table_id].ToPB(&index_info_pb);
      YB_LOG_EVERY_N_SECS(INFO, 1) << "The index info for "
                                   << index_table_name.ToString() << " is "
                                   << yb::ToString(index_info_pb);

      ASSERT_TRUE(index_info_pb.has_index_permissions());
    } while (index_info_pb.index_permissions() <
                 IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE &&
             CoarseMonoClock::Now() < deadline);

    EXPECT_EQ(index_info_pb.index_permissions(),
              IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

    LOG(INFO) << "All good for iteration " << i;
    ASSERT_OK(client_->DeleteIndexTable(index_table_name, nullptr, /* wait */ true));
    ASSERT_OK(client_->data_->WaitForDeleteTableToFinish(
        client_.get(), index_table_id, deadline));

    // For the first round we just simply calculate the time it takes
    if (i == 0) {
      total_time_taken_for_one_iteration = ToSteady(CoarseMonoClock::Now()) - start;
    }
  }
}

// Test the scenario where we create a table, pause the leader master,
// and then issue the DeleteTable call: DeleteTable should go to the newly
// elected leader master and succeed.
TEST_F(MasterFailoverTest, TestDeleteTableSync) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  YBTableName table_name(YQL_DATABASE_CQL, "test", "testDeleteTableSync");
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  ASSERT_OK(client_->DeleteTable(table_name));
  shared_ptr<YBTable> table;
  Status s = client_->OpenTable(table_name, &table);
  ASSERT_TRUE(s.IsNotFound());
}

// Test the scenario where we create a table, pause the leader master,
// and then issue the AlterTable call renaming a table: AlterTable
// should go to the newly elected leader master and succeed, renaming
// the table.
//
// TODO: Add an equivalent async test. Add a test for adding and/or
// renaming a column in a table.
TEST_F(MasterFailoverTest, TestRenameTableSync) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "This test can only be run in slow mode.";
    return;
  }

  auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  YBTableName table_name_orig(YQL_DATABASE_CQL, "test", "testAlterTableSync");
  ASSERT_OK(CreateTable(table_name_orig, kWaitForCreate));

  LOG(INFO) << "Pausing leader master";
  ASSERT_OK(cluster_->master(leader_idx)->Pause());
  ScopedResumeExternalDaemon resume_daemon(cluster_->master(leader_idx));

  YBTableName table_name_new(YQL_DATABASE_CQL, "test", "testAlterTableSyncRenamed");
  ASSERT_OK(RenameTable(table_name_orig, table_name_new));
  shared_ptr<YBTable> table;
  ASSERT_OK(client_->OpenTable(table_name_new, &table));

  Status s = client_->OpenTable(table_name_orig, &table);
  ASSERT_TRUE(s.IsNotFound());
}

TEST_F(MasterFailoverTest, TestFailoverAfterTsFailure) {
  for (auto master : cluster_->master_daemons()) {
    ASSERT_OK(cluster_->SetFlag(master, "enable_register_ts_from_raft", "true"));
  }
  YBTableName table_name(YQL_DATABASE_CQL, "test", "testFailoverAfterTsFailure");
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));

  cluster_->tablet_server(0)->Shutdown();

  // Roll over to a new master.
  ASSERT_OK(cluster_->ChangeConfig(cluster_->GetLeaderMaster(), consensus::REMOVE_SERVER));

  // Count all servers equal to 3.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    int tserver_count;
    RETURN_NOT_OK(client_->TabletServerCount(&tserver_count, false /* primary_only */));
    return tserver_count == 3;
  }, MonoDelta::FromSeconds(30), "Wait for tablet server count"));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    int tserver_count;
    RETURN_NOT_OK(client_->TabletServerCount(&tserver_count, true /* primary_only */));
    return tserver_count == 2;
  }, MonoDelta::FromSeconds(30), "Wait for tablet server count"));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(table_name, 0, &tablets, /* partition_list_version =*/ nullptr));

  // Assert master sees that all tablets have 3 replicas.
  for (const auto& loc : tablets) {
    ASSERT_EQ(loc.replicas_size(), 3);
  }

  // Make sure we can issue a delete table that doesn't crash with the fake ts. Then, make sure
  // when we restart the server, we properly re-register and have no crashes.
  ASSERT_OK(client_->DeleteTable(table_name, false /* wait */));
  ASSERT_OK(cluster_->tablet_server(0)->Start());

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    int tserver_count;
    RETURN_NOT_OK(client_->TabletServerCount(&tserver_count, true /* primary_only */));
    bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
    // We have registered the new tserver and the LB is idle.
    return tserver_count == 3 && is_idle;
  }, MonoDelta::FromSeconds(30), "Wait for LB idle"));

  cluster_->AssertNoCrashes();
}

TEST_F(MasterFailoverTest, TestLoadMoveCompletion) {
  // Original cluster is RF3 so add a TS.
  LOG(INFO) << "Adding a T-Server.";
  ASSERT_OK(cluster_->AddTabletServer());

  // Create a table to introduce some workload.
  YBTableName table_name(YQL_DATABASE_CQL, "test", "testLoadMoveCompletion");
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));

  // Give some time for the cluster balancer to balance tablets.
  std::function<Result<bool> ()> is_idle = [&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  };
  ASSERT_OK(WaitFor(is_idle,
                MonoDelta::FromSeconds(60),
                "Load Balancer Idle check failed"));

  // Disable TS heartbeats.
  LOG(INFO) << "Disabled Heartbeats";
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_tserver_disable_heartbeat", "true"));

  // Blacklist a TS.
  ExternalMaster *leader = cluster_->GetLeaderMaster();
  ExternalTabletServer *ts = cluster_->tablet_server(3);
  ASSERT_OK(cluster_->AddTServerToBlacklist(leader, ts));
  LOG(INFO) << "Blacklisted tserver#3";

  // Get the initial load.
  auto idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  auto proxy = cluster_->GetMasterProxy<master::MasterClusterProxy>(idx);

  rpc::RpcController rpc;
  master::GetLoadMovePercentRequestPB req;
  master::GetLoadMovePercentResponsePB resp;
  ASSERT_OK(proxy.GetLoadMoveCompletion(req, &resp, &rpc));

  auto initial_total_load = resp.total();

  // Failover the leader.
  LOG(INFO) << "Failing over master leader.";
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());

  // Get the final load and validate.
  req.Clear();
  resp.Clear();
  rpc.Reset();

  idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  proxy = cluster_->GetMasterProxy<master::MasterClusterProxy>(idx);
  ASSERT_OK(proxy.GetLoadMoveCompletion(req, &resp, &rpc));
  LOG(INFO) << "Initial loads. Before master leader failover: " <<  initial_total_load
            << " v/s after master leader failover: " << resp.total();

  EXPECT_EQ(resp.total(), initial_total_load)
      << "Expected the initial blacklisted load to be propagated to new leader master.";

  // The progress should be reported as 0 until tservers heartbeat
  // their tablet reports.
  EXPECT_EQ(resp.percent(), 0) << "Expected the initial progress"
                                  " to be zero.";

  // Now enable heartbeats.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_tserver_disable_heartbeat", "false"));
  ASSERT_OK(cluster_->SetFlagOnMasters("blacklist_progress_initial_delay_secs",
                                        std::to_string((kHeartbeatIntervalMs * 20)/1000)));
  LOG(INFO) << "Enabled heartbeats";

  ASSERT_OK(LoggedWaitFor(
    [&]() -> Result<bool> {
      req.Clear();
      resp.Clear();
      rpc.Reset();
      RETURN_NOT_OK(proxy.GetLoadMoveCompletion(req, &resp, &rpc));
      return resp.percent() >= 100;
    },
    MonoDelta::FromSeconds(300),
    "Waiting for blacklist load transfer to complete"
  ));
}

class MasterFailoverTestWithPlacement : public MasterFailoverTest {
 public:
  virtual void SetUp() override {
    opts_.extra_tserver_flags.push_back("--placement_cloud=c");
    opts_.extra_tserver_flags.push_back("--placement_region=r");
    opts_.extra_tserver_flags.push_back("--placement_zone=z${index}");
    opts_.extra_tserver_flags.push_back("--placement_uuid=" + kLivePlacementUuid);
    opts_.extra_master_flags.push_back("--enable_register_ts_from_raft=true");
    MasterFailoverTest::SetUp();
    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(30));
    ASSERT_OK(yb_admin_client_->Init());
    ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, kLivePlacementUuid));
  }

  virtual void TearDown() override {
    yb_admin_client_.reset();
    MasterFailoverTest::TearDown();
  }

  void AssertTserverHasPlacementUuid(
      const string& ts_uuid, const string& placement_uuid,
      const std::vector<YBTabletServer>& tablet_servers) {
    auto it = std::find_if(tablet_servers.begin(), tablet_servers.end(), [&](const auto& ts) {
        return ts.uuid == ts_uuid;
    });
    ASSERT_TRUE(it != tablet_servers.end());
    ASSERT_EQ(it->placement_uuid, placement_uuid);
  }

 protected:
  const string kReadReplicaPlacementUuid = "read_replica";
  const string kLivePlacementUuid = "live";
  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;
};

TEST_F_EX(MasterFailoverTest, TestFailoverWithReadReplicas, MasterFailoverTestWithPlacement) {
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z0:1", 1, kReadReplicaPlacementUuid));

  // Add a new read replica tserver to the cluster with a matching cloud info to a live placement,
  // to test that we distinguish not just by cloud info but also by peer role.
  std::vector<std::string> extra_opts;
  extra_opts.push_back("--placement_cloud=c");
  extra_opts.push_back("--placement_region=r");
  extra_opts.push_back("--placement_zone=z0");
  extra_opts.push_back("--placement_uuid=" + kReadReplicaPlacementUuid);
  ASSERT_OK(cluster_->AddTabletServer(true, extra_opts));

  YBTableName table_name(YQL_DATABASE_CQL, "test", "testFailoverWithReadReplicas");
  ASSERT_OK(CreateTable(table_name, kWaitForCreate));

  // Shutdown the live ts in c.r.z0
  auto live_ts_uuid = cluster_->tablet_server(0)->instance_id().permanent_uuid();
  cluster_->tablet_server(0)->Shutdown();

  // Shutdown the rr ts in c.r.z0
  auto rr_ts_uuid = cluster_->tablet_server(3)->instance_id().permanent_uuid();
  cluster_->tablet_server(3)->Shutdown();

  // Roll over to a new master.
  ASSERT_OK(cluster_->ChangeConfig(cluster_->GetLeaderMaster(), consensus::REMOVE_SERVER));

  // Count all servers equal to 4.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    int tserver_count;
    RETURN_NOT_OK(client_->TabletServerCount(&tserver_count, false /* primary_only */));
    return tserver_count == 4;
  }, MonoDelta::FromSeconds(30), "Wait for tablet server count"));

  const auto tablet_servers = ASSERT_RESULT(client_->ListTabletServers());

  ASSERT_NO_FATALS(AssertTserverHasPlacementUuid(live_ts_uuid, kLivePlacementUuid, tablet_servers));
  ASSERT_NO_FATALS(AssertTserverHasPlacementUuid(
      rr_ts_uuid, kReadReplicaPlacementUuid, tablet_servers));
  cluster_->AssertNoCrashes();
}

class MasterFailoverMiniClusterTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MasterFailoverMiniClusterTest() {
    opts_.num_masters = num_masters_ = 3;
    opts_.num_tablet_servers = kNumTabletServerReplicas;
  }

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    cluster_.reset(new MiniCluster(opts_));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(cluster_->WaitForTabletServerCount(kNumTabletServerReplicas));
  }

  void DoTearDown() override {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }
    YBMiniClusterTestBase::DoTearDown();
  }

 private:
  int num_masters_;
  MiniClusterOptions opts_;
};

// Do a TRUNCATE, which creates an AsyncTruncate task (a shared_ptr).  When it finishes, there
// should still be a ref to it from tasks_tracker_, which tracks recent tasks to show in master UI.
// When master loses leadership, recent job/tasks trackers should be cleared, so make sure that that
// last ref is removed.
//
// Run yb_build.sh with `--test-args --vmodule=master_failover-itest=1,catalog_manager=4` for
// helpful logs.
TEST_F_EX(MasterFailoverTest, DereferenceTasks, MasterFailoverMiniClusterTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const auto& kKeyspaceName = "mykeyspace";
  const auto* master_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  TestThreadHolder thread_holder;
  auto kSpinWaitTime = 10ms;
  auto kWaitForMaxDelay = 100ms;

  LOG(INFO) << "Ensure there are exactly zero truncate tasks";
  {
    const auto tasks = master_leader->catalog_manager().GetRecentTasks();
    for (const auto& task : tasks) {
      ASSERT_NE(task->type(), server::MonitoredTaskType::kTruncateTablet) << task;
    }
  }

  LOG(INFO) << "Create keyspace";
  ASSERT_OK(client->CreateNamespaceIfNotExists(kKeyspaceName));

  LOG(INFO) << "Create table";
  const YBTableName table_name(YQL_DATABASE_CQL, kKeyspaceName, "dereference_tasks_table");
  YBSchema schema;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(INT32)->NotNull()->PrimaryKey();
  ASSERT_OK(b.Build(&schema));
  auto table_creator = client->NewTableCreator();
  ASSERT_OK(table_creator->table_name(table_name)
                         .schema(&schema)
                         .hash_schema(YBHashSchema::kMultiColumnHash)
                         .num_tablets(1)
                         .Create());

  thread_holder.AddThreadFunctor(
      [&stop = thread_holder.stop_flag(), &table_name, &client] {
        LOG(INFO) << "Begin truncate table thread";
        auto table_id = ASSERT_RESULT(GetTableId(client.get(), table_name));
        LOG(INFO) << "Truncate table: start";
        ASSERT_OK(client->TruncateTable(table_id));
        LOG(INFO) << "Truncate table: done";
        // Spin until end of test to prevent this thread from setting stop flag.
        ASSERT_OK(Wait(
            [&stop]() -> Result<bool> {
              return stop.load(std::memory_order_acquire);
            },
            MonoTime::kMax,
            "wait for end of test"));
      });

  // The one truncate on the one-tablet table should produce one truncate task.
  LOG(INFO) << "Wait for and ensure there is exactly one truncate task";
  // Use a weak_ptr because we want to passively observe the mini master's shared_ptr, not
  // influencing the refcount.
  std::weak_ptr<server::MonitoredTask> truncate_task;
  ASSERT_OK(LoggedWaitFor(
      [&master_leader, &truncate_task]() -> Result<bool> {
        const auto tasks = master_leader->catalog_manager().GetRecentTasks();

        // Look for truncate tasks.  There should only be one truncate task.  We try to enforce this
        // by looking at every task returned in the GetRecentTasks call.  It could be the case that
        // a second truncate task appears later, but that is less easy to check for and less likely
        // to happen, so don't bother.
        for (const auto& task : tasks) {
          if (task->type() == server::MonitoredTaskType::kTruncateTablet) {
            // Check whether truncate_task is already loaded.  All truncate tasks should have
            // refcount at least one because tasks tracker holds a reference even after the task is
            // complete.  The task could lose all refcounts when master leadership changes, but that
            // should be very unlikely without an explicit stepdown request.
            if (truncate_task.use_count() != 0) {
              return STATUS(RuntimeError, "Did not expect to find a second truncate task");
            }
            truncate_task = task;
          }
        }

        // Check whether truncate_task is loaded.  When loaded, the count could vary between 1-4
        // because of refs from places like
        // - recent tasks tracker
        // - lambda function to schedule task (SubmitFunc from CatalogManager::ScheduleTask)
        // - async task callback (BindRpcCallback from AsyncTruncate::SendRequest)
        // - local variables
        return truncate_task.use_count() != 0;
      },
      RegularBuildVsSanitizers(3s, 5s),
      "wait for truncate task to show up in catalog manager tasks",
      MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
      kDefaultWaitDelayMultiplier,
      kWaitForMaxDelay));

  if (VLOG_IS_ON(1)) {
    thread_holder.AddThreadFunctor(
        [&stop = thread_holder.stop_flag(), &truncate_task, kSpinWaitTime] {
          while (!stop.load(std::memory_order_acquire)) {
            VLOG(1) << "Observing use count: " << truncate_task.use_count();
            SleepFor(kSpinWaitTime);
          }
        });
  }

  LOG(INFO) << "Wait for truncate to complete";
  ASSERT_OK(LoggedWaitFor(
      [&truncate_task]() -> Result<bool> {
        return server::MonitoredTask::IsStateTerminal(truncate_task.lock()->state());
      },
      RegularBuildVsSanitizers(10s, 20s),
      "wait for truncate task terminal state",
      MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
      kDefaultWaitDelayMultiplier,
      kWaitForMaxDelay));
  ASSERT_EQ(truncate_task.lock()->state(), server::MonitoredTaskState::kComplete)
      << truncate_task.lock()->state();

  // AsyncTruncate::HandleResponse does TransitionToCompleteState.  We are at that point right now,
  // according to the above assert.  The callback holding reference to the AsyncTruncate task could
  // still be active (it is the caller of HandleResponse).  Wait a bit longer for the callback to
  // complete and eventually be destroyed (see OutboundCall::InvokeCallbackSync).
  LOG(INFO) << "Wait for truncate task callback to be cleaned up";
  ASSERT_OK(LoggedWaitFor(
      [&truncate_task]() -> Result<bool> {
        const auto use_count = truncate_task.use_count();

        // Expect one ref from CatalogManager tasks_tracker_.
        if (use_count == 1) {
          return true;
        } else if (use_count > 1) {
          return false;
        }
        return STATUS(
            RuntimeError,
            "Truncate task totally dereferenced before invoking master leader failover:"
            " did leader failover happen circumstantially?");
      },
      RegularBuildVsSanitizers(1s, 2s),
      "wait for truncate task to have exactly one ref",
      MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
      kDefaultWaitDelayMultiplier,
      kWaitForMaxDelay));

  LOG(INFO) << "Check references on truncate task shared ptr (before leader failover)";
  // Verify the one ref is in tasks_tracker_.
  bool found = false;
  auto tasks = master_leader->catalog_manager().GetRecentTasks();
  for (const auto& task : tasks) {
    if (task->type() == server::MonitoredTaskType::kTruncateTablet &&
        task == truncate_task.lock()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "The last ref to the truncate task was not found in tasks_tracker_";
  // Clear tasks local var because it holds a ref to the truncate task.
  tasks.clear();
  // Double-check it's still one ref.
  ASSERT_EQ(truncate_task.use_count(), 1);

  LOG(INFO) << "Cause master leader failover";
  ASSERT_OK(StepDown(
      master_leader->tablet_peer(), std::string() /* new_leader_uuid */, ForceStepDown::kTrue));

  LOG(INFO) << "Ensure leadership is lost";
  ASSERT_OK(LoggedWaitFor(
      [&master_leader]() -> Result<bool> {
        const auto s = master_leader->catalog_manager().CheckIsLeaderAndReady();
        if (s.ok()) {
          // Still the leader.
          return false;
        } else if (!s.IsIllegalState()) {
          // Unexpected status code.
          return s;
        } else if (s.message().ToBuffer().find("Not the leader") == std::string::npos) {
          // Unexpected status message.
          return s;
        }
        return true;
      },
      RegularBuildVsSanitizers(2s, 5s),
      "lose leadership",
      MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
      kDefaultWaitDelayMultiplier,
      kWaitForMaxDelay));

  // Add some extra margin for the bg task to do its work.
  const auto wait_time = (
      MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms) +
      RegularBuildVsSanitizers(1s, 2s));
  LOG(INFO) << "Wait " << wait_time << " for catalog manager bg task to happen";
  ASSERT_OK(LoggedWaitFor(
      [&truncate_task]() -> Result<bool> {
        const auto use_count = truncate_task.use_count();

        // Expect all refs on the task to be released after losing leadership.  Specifically, expect
        // the catalog manager bg task to abort and reset tasks, especially tasks_tracker_, which
        // should be the last ref.
        if (use_count == 0) {
          return true;
        } else if (use_count == 1) {
          return false;
        }
        return STATUS(
            RuntimeError,
            "Truncate task gained ref(s) after it was down to one ref in tasks_tracker_");
      },
      wait_time,
      "wait for truncate task to have no refs",
      MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
      kDefaultWaitDelayMultiplier,
      kWaitForMaxDelay));

  // By default, DoBeforeTearDown checks cluster consistency using ysck.  Since we recently stepped
  // down master leader, the new master will be registering tservers.  ysck will try to get master
  // and tservers.  It internally calls MasterClusterServiceImpl::ListTabletServers to get live
  // tservers.  It is possible for there to be no live tservers because the new master leader is
  // initializing.  In that case, ysck is intelligent to realize this and will sleep and try again
  // later.  However, if there is at least one live tserver and ysck successfully connects to them,
  // it considers it a success, even if it did not get all tservers.  Later, when checksumming, it
  // will CHECK fail because a table has a tablet on a tserver it did not find in the initial
  // collection.
  //
  // Avoid this by waiting for all tservers to register before ending the test.
  LOG(INFO) << "Wait for all tservers to be re-registered to new master leader";
  ASSERT_OK(cluster_->WaitForAllTabletServers());
}

}  // namespace client
}  // namespace yb
