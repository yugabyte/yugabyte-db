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
// Tests for the yb-admin command-line tool.

#include <boost/assign/list_of.hpp>
#include <gtest/gtest.h>
#include "yb/util/logging.h"

#include "yb/consensus/consensus.pb.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/ts_itest-base.h"

#include "yb/master/master_client.pb.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/tools/admin-test-base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/subprocess.h"

using std::string;
using std::vector;

using boost::assign::list_of;
using strings::Split;
using strings::Substitute;
using yb::consensus::OpIdType;
using yb::itest::FindTabletFollowers;
using yb::itest::FindTabletLeader;
using yb::itest::TabletServerMap;
using yb::itest::TServerDetails;
using yb::itest::WaitUntilLeader;

constexpr int kTabletTimeout = 30;

namespace yb {
namespace tools {

static const char* const kTsCliToolName = "yb-ts-cli";

class YBTsCliTest : public ExternalMiniClusterITestBase {
 protected:
  // Figure out where the admin tool is.
  string GetTsCliToolPath() const;
};

string YBTsCliTest::GetTsCliToolPath() const {
  return GetToolPath(kTsCliToolName);
}

// Test setting vmodule
TEST_F(YBTsCliTest, TestVModuleUpdate) {
  std::vector<std::string> ts_flags = {
      "--vmodule=foo=0,bar=1",
  };
  std::vector<std::string> master_flags = {};
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  string exe_path = GetTsCliToolPath();
  vector<string> argv;
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(yb::ToString(cluster_->tablet_server(0)->bound_rpc_addr()));
  argv.push_back("set_flag");
  argv.push_back("vmodule");

  {
    // Should be able to update for specified modules.
    argv.push_back("foo=1,bar=2");
    ASSERT_OK(Subprocess::Call(argv));
    argv.pop_back();
  }

  {
    // Should be able to update for a subset of the modules specified.
    argv.push_back("foo=1");
    ASSERT_OK(Subprocess::Call(argv));
    argv.pop_back();
  }

  {
    // Test with an empty string.
    argv.push_back("");
    ASSERT_OK(Subprocess::Call(argv));
    argv.pop_back();
  }

  {
    // Should be able to update for any module unspecified at start-up.
    argv.push_back("foo=1,baz=2");
    ASSERT_OK(Subprocess::Call(argv));
    argv.pop_back();
  }

  {
    // Test with an empty string.
    argv.push_back("foo=,baz=2");
    ASSERT_NOK(Subprocess::Call(argv));
    argv.pop_back();
  }

  {
    // Test with an empty string.
    argv.push_back("foo=1,=2");
    ASSERT_NOK(Subprocess::Call(argv));
    argv.pop_back();
  }
}

// Test deleting a tablet.
TEST_F(YBTsCliTest, TestDeleteTablet) {
  MonoDelta timeout = MonoDelta::FromSeconds(kTabletTimeout);
  std::vector<std::string> ts_flags = {
    "--enable_leader_failure_detection=false"s,
  };
  std::vector<std::string> master_flags = {
    "--catalog_manager_wait_for_new_tablets_to_elect_leader=false"s,
    "--use_create_table_leader_hint=false"s,
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  TestWorkload workload(cluster_.get());
  workload.Setup(); // Easy way to create a new tablet.

  vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  for (const auto& entry : ts_map_) {
    TServerDetails* ts = entry.second.get();
    ASSERT_OK(itest::WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  }
  string tablet_id = tablets[0].tablet_status().tablet_id();

  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()].get(),
                                            tablet_id, timeout));
  }

  string exe_path = GetTsCliToolPath();
  vector<string> argv;
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(yb::ToString(cluster_->tablet_server(0)->bound_rpc_addr()));
  argv.push_back("delete_tablet");
  argv.push_back(tablet_id);
  argv.push_back("Deleting for yb-ts-cli-test");
  ASSERT_OK(Subprocess::Call(argv));

  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(0, tablet_id, tablet::TABLET_DATA_TOMBSTONED));
  TServerDetails* ts = ts_map_[cluster_->tablet_server(0)->uuid()].get();
  ASSERT_OK(itest::WaitUntilTabletInState(ts, tablet_id, tablet::SHUTDOWN, timeout));
}

// Test readiness check before and after a tablet server is done bootstrapping.
TEST_F(YBTsCliTest, TestTabletServerReadiness) {
  MonoDelta timeout = MonoDelta::FromSeconds(kTabletTimeout);
  ASSERT_NO_FATALS(StartCluster({ "--TEST_tablet_bootstrap_delay_ms=2000"s }));

  TestWorkload workload(cluster_.get());
  workload.Setup(); // Easy way to create a new tablet.

  vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  for (const auto& entry : ts_map_) {
    TServerDetails* ts = entry.second.get();
    ASSERT_OK(itest::WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  }
  string tablet_id = tablets[0].tablet_status().tablet_id();

  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()].get(),
                                            tablet_id, timeout));
  }

  ASSERT_NO_FATALS(cluster_->tablet_server(0)->Shutdown());
  ASSERT_OK(cluster_->tablet_server(0)->Restart());

  string exe_path = GetTsCliToolPath();
  vector<string> argv;
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(yb::ToString(cluster_->tablet_server(0)->bound_rpc_addr()));
  argv.push_back("is_server_ready");
  ASSERT_NOK(Subprocess::Call(argv));

  ASSERT_OK(WaitFor([&]() {
    return Subprocess::Call(argv).ok();
  }, MonoDelta::FromSeconds(10), "Wait for tablet bootstrap to finish"));
}

TEST_F(YBTsCliTest, TestManualRemoteBootstrap) {
  MonoDelta timeout = MonoDelta::FromSeconds(kTabletTimeout);
  ASSERT_NO_FATALS(StartCluster({}, {}, 3 /*num tservers*/, 1 /*num masters*/));

  TestWorkload workload(cluster_.get());
  workload.Setup();
  workload.Start();

  std::vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  for (const auto& entry : ts_map_) {
    TServerDetails* ts = entry.second.get();
    ASSERT_OK(itest::WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  }

  for (const auto& tablet : tablets) {
    const auto& tablet_id = tablet.tablet_status().tablet_id();
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()].get(),
                                              tablet_id, timeout));
    }
  }

  workload.WaitInserted(1000);
  workload.Stop();
  ASSERT_EQ(workload.rows_insert_failed(), 0);

  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_NO_FATALS(cluster_->tablet_server(i)->Shutdown());
  }

  auto* env = Env::Default();
  for (size_t i = 1; i < cluster_->num_tablet_servers(); ++i) {
    for (const auto& data_dir : cluster_->tablet_server(i)->GetDataDirs()) {
      for (const auto& tablet : tablets) {
        const auto& tablet_id = tablet.tablet_status().tablet_id();
        auto meta_dir = FsManager::GetRaftGroupMetadataDir(data_dir);
        auto metadata_path = JoinPathSegments(meta_dir, tablet_id);
        tablet::RaftGroupReplicaSuperBlockPB superblock;
        ASSERT_OK(pb_util::ReadPBContainerFromPath(env, metadata_path, &superblock));
        auto tablet_data_dir = superblock.kv_store().rocksdb_dir();
        const auto& rocksdb_files = ASSERT_RESULT(env->GetChildren(
            tablet_data_dir, ExcludeDots::kTrue));
        ASSERT_GT(rocksdb_files.size(), 0);

        for (const auto& file : rocksdb_files) {
          ASSERT_OK(env->DeleteFile(JoinPathSegments(tablet_data_dir, file)));
        }
        ASSERT_OK(env->DeleteFile(metadata_path));
      }
    }
  }

  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->tablet_server(i)->Restart());
  }

  std::string exe_path = GetTsCliToolPath();
  std::vector<std::string> argv;
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(yb::ToString(cluster_->tablet_server(1)->bound_rpc_addr()));
  argv.push_back("remote_bootstrap");
  argv.push_back(yb::ToString(cluster_->tablet_server(0)->bound_rpc_addr()));

  for (const auto& tablet : tablets) {
    const auto& tablet_id = tablet.tablet_status().tablet_id();
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(0)->uuid()].get(),
                                            tablet_id, timeout));
    argv.push_back(tablet_id);
    ASSERT_OK(Subprocess::Call(argv));
    argv.pop_back();

    for (size_t i = 1; i < cluster_->num_tablet_servers(); ++i) {
      ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()].get(),
                                              tablet_id, timeout));
    }
  }

  auto wait_until_rows = workload.rows_inserted() + 1000;
  workload.Start();
  workload.WaitInserted(wait_until_rows);
  workload.StopAndJoin();
  ASSERT_EQ(workload.rows_insert_failed(), 0);

  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_NO_FATALS(cluster_->tablet_server(i)->Shutdown());
  }

  for (size_t i = 1; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->tablet_server(i)->Restart());
    for (const auto& tablet : tablets) {
      const auto& tablet_id = tablet.tablet_status().tablet_id();
      ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()].get(),
                                              tablet_id, timeout));

      std::vector<std::string> delete_argv;
      delete_argv.push_back(exe_path);
      delete_argv.push_back("--server_address");
      delete_argv.push_back(yb::ToString(cluster_->tablet_server(i)->bound_rpc_addr()));
      delete_argv.push_back("delete_tablet");
      delete_argv.push_back(tablet_id);
      delete_argv.push_back("reason");

      ASSERT_OK(Subprocess::Call(delete_argv));
    }
  }
  ASSERT_OK(cluster_->tablet_server(0)->Restart());

  for (const auto& tablet : tablets) {
    const auto& tablet_id = tablet.tablet_status().tablet_id();
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(0)->uuid()].get(),
                                            tablet_id, timeout));
    argv.push_back(tablet_id);
    ASSERT_OK(Subprocess::Call(argv));
    argv.pop_back();
  }

  wait_until_rows = workload.rows_inserted() + 1000;
  workload.Start();
  workload.WaitInserted(wait_until_rows);
  workload.StopAndJoin();
  ASSERT_EQ(workload.rows_insert_failed(), 0);
}

TEST_F(YBTsCliTest, TestRefreshFlags) {
  std::string gflag = "client_read_write_timeout_ms";
  std::string old_value = "120000";
  std::string new_value = "150000";

  // Write a flagfile.
  std::string flag_filename = JoinPathSegments(GetTestDataDirectory(), "flagfile.test");
  std::unique_ptr<WritableFile> flag_file;
  ASSERT_OK(Env::Default()->NewWritableFile(flag_filename, &flag_file));
  ASSERT_OK(flag_file->Append("--" + gflag + "=" + old_value));
  ASSERT_OK(flag_file->Close());

  // Start the cluster;
  vector<string> extra_flags = {"--flagfile=" + flag_filename};

  ASSERT_NO_FATALS(StartCluster(extra_flags, extra_flags));

  // Verify that the cluster is started with the custom GFlag value in the config.
  auto master_flag = ASSERT_RESULT(cluster_->master(0)->GetFlag(gflag));
  ASSERT_EQ(master_flag, old_value);
  auto ts_flag = ASSERT_RESULT(cluster_->tablet_server(0)->GetFlag(gflag));
  ASSERT_EQ(ts_flag, old_value);

  // Change the flagfile to have a different value for the GFlag.
  ASSERT_OK(Env::Default()->NewWritableFile(flag_filename, &flag_file));
  ASSERT_OK(flag_file->Append("--" + gflag + "=" + new_value));
  ASSERT_OK(flag_file->Close());

  // Send RefreshFlags RPC to the Master process
  string exe_path = GetTsCliToolPath();
  vector<string> argv;
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(yb::ToString(cluster_->master(0)->bound_rpc_addr()));
  argv.push_back("refresh_flags");
  ASSERT_OK(Subprocess::Call(argv));

  // Wait for the master process to have the updated GFlag value.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->master(0)->GetFlag(gflag)) == new_value;
  }, MonoDelta::FromSeconds(60), "Verify updated GFlag"));

  // The TServer should still have the old value because we didn't send it the RPC.
  ts_flag = ASSERT_RESULT(cluster_->tablet_server(0)->GetFlag(gflag));
  ASSERT_EQ(ts_flag, old_value);

  // Now, send a RefreshFlags RPC to the TServer process
  argv.clear();
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(yb::ToString(cluster_->tablet_server(0)->bound_rpc_addr()));
  argv.push_back("refresh_flags");
  ASSERT_OK(Subprocess::Call(argv));

  // Wait for the TS process to have the updated GFlag value.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->tablet_server(0)->GetFlag(gflag)) == new_value;
  }, MonoDelta::FromSeconds(60), "Verify updated GFlag"));
}

class YBTsCliUnsafeChangeTest : public AdminTestBase {
 public:
  // Figure out where the admin tool is.
  string GetTsCliToolPath() const;
};

string YBTsCliUnsafeChangeTest::GetTsCliToolPath() const {
  return GetToolPath(kTsCliToolName);
}

Status RunUnsafeChangeConfig(
    YBTsCliUnsafeChangeTest* test,
    const string& tablet_id,
    const string& dst_host,
    const vector<string>& peer_uuid_list) {
  LOG(INFO) << " Called RunUnsafeChangeConfig with " << tablet_id << " " << dst_host << " and "
            << yb::ToString(peer_uuid_list);
  std::vector<string> params;
  params.push_back(test->GetTsCliToolPath());
  params.push_back("--server_address");
  params.push_back(dst_host);
  params.push_back("unsafe_config_change");
  params.push_back(tablet_id);
  for (const auto& peer : peer_uuid_list) {
    params.push_back(peer);
  }
  VERIFY_RESULT(test->CallAdminVec(params));
  return Status::OK();
}

// Test unsafe config change when there is one follower survivor in the cluster.
// 1. Instantiate external mini cluster with 1 tablet having 3 replicas and 5 TS.
// 2. Shut down leader and follower1.
// 3. Trigger unsafe config change on follower2 having follower2 in the config.
// 4. Wait until the new config is populated on follower2(new leader) and master.
// 5. Bring up leader and follower1 and verify replicas are deleted.
// 6. Verify that new config doesn't contain old leader and follower1.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigOnSingleFollower) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 5;
  FLAGS_num_replicas = 3;
  // tserver_unresponsive_timeout_ms is useful so that master considers
  // the live tservers for tablet re-replication.
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  LOG(INFO) << "Finding tablet leader and waiting for things to start...";
  string tablet_id = tablet_replicas_.begin()->first;

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(cluster_.get(),
                                                   3, tablet_id_, kTimeout,
                                                   itest::WAIT_FOR_LEADER,
                                                   &has_leader, &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  // Shut down master so it doesn't interfere while we shut down the leader and
  // one of the other followers.
  cluster_->master()->Shutdown();
  cluster_->tablet_server_by_uuid(leader_ts->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(followers[1]->uuid())->Shutdown();

  LOG(INFO) << "Forcing unsafe config change on remaining follower " << followers[0]->uuid();
  const string& follower0_addr =
      cluster_->tablet_server_by_uuid(followers[0]->uuid())->bound_rpc_addr().ToString();
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, follower0_addr, {followers[0]->uuid()}));
  ASSERT_OK(WaitUntilLeader(followers[0], tablet_id_, kTimeout));
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(1, followers[0], tablet_id_, kTimeout));

  LOG(INFO) << "Restarting master...";

  // Restart master so it can re-replicate the tablet to remaining tablet servers.
  ASSERT_OK(cluster_->master()->Restart());

  // Wait for master to re-replicate.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(3, followers[0], tablet_id_, kTimeout));
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  OpId opid;
  ASSERT_OK(WaitForOpFromCurrentTerm(
      followers[0], tablet_id_, OpIdType::COMMITTED_OPID, kTimeout, &opid));

  active_tablet_servers.clear();
  std::unordered_set<string> replica_uuids;
  for (const auto& loc : tablet_locations.replicas()) {
    const string& uuid = loc.ts_info().permanent_uuid();
    InsertOrDie(&active_tablet_servers, uuid, tablet_servers_[uuid].get());
  }
  ASSERT_OK(WaitForServersToAgree(kTimeout, active_tablet_servers, tablet_id_, opid.index));

  // Verify that two new servers are part of new config and old
  // servers are gone.
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[1]->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), leader_ts->uuid());
  }

  // Also verify that when we bring back followers[1] and leader,
  // we should see the tablet in TOMBSTONED state on these servers.
  ASSERT_OK(cluster_->tablet_server_by_uuid(leader_ts->uuid())->Restart());
  ASSERT_OK(cluster_->tablet_server_by_uuid(followers[1]->uuid())->Restart());
  ASSERT_OK(itest::WaitUntilTabletInState(leader_ts, tablet_id, tablet::SHUTDOWN, kTimeout));
  ASSERT_OK(itest::WaitUntilTabletInState(followers[1], tablet_id, tablet::SHUTDOWN, kTimeout));
}

// Test unsafe config change when there is one leader survivor in the cluster.
// 1. Instantiate external mini cluster with 1 tablet having 3 replicas and 5 TS.
// 2. Shut down both followers.
// 3. Trigger unsafe config change on leader having leader in the config.
// 4. Wait until the new config is populated on leader and master.
// 5. Verify that new config does not contain old followers.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigOnSingleLeader) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 5;
  FLAGS_num_replicas = 3;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  // Shut down servers follower1 and follower2,
  // so that we can force new config on remaining leader.
  cluster_->tablet_server_by_uuid(followers[0]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(followers[1]->uuid())->Shutdown();
  // Restart master to cleanup cache of dead servers from its list of candidate
  // servers to trigger placement of new replicas on healthy servers.
  cluster_->master()->Shutdown();
  ASSERT_OK(cluster_->master()->Restart());

  LOG(INFO) << "Forcing unsafe config change on tserver " << leader_ts->uuid();
  const string& leader_addr = Substitute(
      "$0:$1",
      leader_ts->registration->common().private_rpc_addresses(0).host(),
      leader_ts->registration->common().private_rpc_addresses(0).port());
  const vector<string> peer_uuid_list = {leader_ts->uuid()};
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, leader_addr, peer_uuid_list));

  // Check that new config is populated to a new follower.
  TServerDetails* new_follower = nullptr;
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  for (const auto& ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_follower = ts;
      break;
    }
  }
  ASSERT_TRUE(new_follower != nullptr);

  // Master may try to add the servers which are down until tserver_unresponsive_timeout_ms,
  // so it is safer to wait until consensus metadata has 3 voters on new_follower.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(3, new_follower, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  LOG(INFO) << "Waiting for Master to see new config...";
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[0]->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[1]->uuid());
  }
}

// Test unsafe config change when the unsafe config contains 2 nodes.
// 1. Instantiate external minicluster with 1 tablet having 3 replicas and 5 TS.
// 2. Shut down leader.
// 3. Trigger unsafe config change on follower1 having follower1 and follower2 in the config.
// 4. Wait until the new config is populated on new_leader and master.
// 5. Verify that new config does not contain old leader.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigForConfigWithTwoNodes) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 4;
  FLAGS_num_replicas = 3;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  // Shut down leader and prepare 2-node config.
  cluster_->tablet_server_by_uuid(leader_ts->uuid())->Shutdown();
  // Restart master to cleanup cache of dead servers from its list of candidate
  // servers to trigger placement of new replicas on healthy servers.
  cluster_->master()->Shutdown();
  ASSERT_OK(cluster_->master()->Restart());

  LOG(INFO) << "Forcing unsafe config change on tserver " << followers[1]->uuid();
  const string& follower1_addr = Substitute(
      "$0:$1",
      followers[1]->registration->common().private_rpc_addresses(0).host(),
      followers[1]->registration->common().private_rpc_addresses(0).port());
  const vector<string> peer_uuid_list = {
      followers[0]->uuid(),
      followers[1]->uuid(),
  };
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, follower1_addr, peer_uuid_list));

  // Find a remaining node which will be picked for re-replication.
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Master may try to add the servers which are down until tserver_unresponsive_timeout_ms,
  // so it is safer to wait until consensus metadata has 3 voters on follower1.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(3, new_node, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  LOG(INFO) << "Waiting for Master to see new config...";
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), leader_ts->uuid());
  }
}

// Test unsafe config change on a 5-replica tablet when the unsafe config contains 2 nodes.
// 1. Instantiate external minicluster with 1 tablet having 5 replicas and 8 TS.
// 2. Shut down leader and 2 followers.
// 3. Trigger unsafe config change on a surviving follower with those
//    2 surviving followers in the new config.
// 4. Wait until the new config is populated on new_leader and master.
// 5. Verify that new config does not contain old leader and old followers.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigWithFiveReplicaConfig) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 8;
  FLAGS_num_replicas = 5;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  vector<ExternalTabletServer*> external_tservers;
  vector<TServerDetails*> tservers = TServerDetailsVector(tablet_servers_);
  for (TServerDetails* ts : tservers) {
    external_tservers.push_back(cluster_->tablet_server_by_uuid(ts->uuid()));
  }

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 5, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  ASSERT_OK(itest::WaitUntilCommittedOpIdIndexIs(1, leader_ts, tablet_id_, kTimeout));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  ASSERT_EQ(followers.size(), 4);
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  cluster_->tablet_server_by_uuid(followers[2]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(followers[3]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(leader_ts->uuid())->Shutdown();
  // Restart master to cleanup cache of dead servers from its list of candidate
  // servers to trigger placement of new replicas on healthy servers.
  cluster_->master()->Shutdown();
  ASSERT_OK(cluster_->master()->Restart());

  LOG(INFO) << "Forcing unsafe config change on tserver " << followers[1]->uuid();
  const string& follower1_addr = Substitute(
      "$0:$1",
      followers[1]->registration->common().private_rpc_addresses(0).host(),
      followers[1]->registration->common().private_rpc_addresses(0).port());
  const vector<string> peer_uuid_list = {
      followers[0]->uuid(),
      followers[1]->uuid(),
  };
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, follower1_addr, peer_uuid_list));

  // Find a remaining node which will be picked for re-replication.
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Master may try to add the servers which are down until tserver_unresponsive_timeout_ms,
  // so it is safer to wait until consensus metadata has 5 voters back on new_node.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(5, new_node, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  LOG(INFO) << "Waiting for Master to see new config...";
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 5, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), leader_ts->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[2]->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[3]->uuid());
  }
}

// Test unsafe config change when there is a pending config on a surviving leader.
// 1. Instantiate external minicluster with 1 tablet having 3 replicas and 5 TS.
// 2. Shut down both the followers.
// 3. Trigger a regular config change on the leader which remains pending on leader.
// 4. Trigger unsafe config change on the surviving leader.
// 5. Wait until the new config is populated on leader and master.
// 6. Verify that new config does not contain old followers and a standby node
//    has populated the new config.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigLeaderWithPendingConfig) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 5;
  FLAGS_num_replicas = 3;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  ASSERT_EQ(followers.size(), 2);
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  // Shut down servers follower1 and follower2,
  // so that leader can't replicate future config change ops.
  cluster_->tablet_server_by_uuid(followers[0]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(followers[1]->uuid())->Shutdown();

  // Now try to replicate a ChangeConfig operation. This should get stuck and time out
  // because the server can't replicate any operations.
  Status s =
      RemoveServer(leader_ts, tablet_id_, followers[1], -1, MonoDelta::FromSeconds(2), nullptr);
  ASSERT_TRUE(s.IsTimedOut());

  LOG(INFO) << "Change Config Op timed out, Sending a Replace config "
            << "command when change config op is pending on the leader.";
  const string& leader_addr = Substitute(
      "$0:$1",
      leader_ts->registration->common().private_rpc_addresses(0).host(),
      leader_ts->registration->common().private_rpc_addresses(0).port());
  const vector<string> peer_uuid_list = {leader_ts->uuid()};
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, leader_addr, peer_uuid_list));

  // Restart master to cleanup cache of dead servers from its list of candidate
  // servers to trigger placement of new replicas on healthy servers.
  cluster_->master()->Shutdown();
  ASSERT_OK(cluster_->master()->Restart());

  // Find a remaining node which will be picked for re-replication.
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Master may try to add the servers which are down until tserver_unresponsive_timeout_ms,
  // so it is safer to wait until consensus metadata has 3 voters on new_node.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(3, new_node, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  LOG(INFO) << "Waiting for Master to see new config...";
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[0]->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[1]->uuid());
  }
}

// Test unsafe config change when there is a pending config on a surviving follower.
// 1. Instantiate external minicluster with 1 tablet having 3 replicas and 5 TS.
// 2. Shut down both the followers.
// 3. Trigger a regular config change on the leader which remains pending on leader.
// 4. Trigger a leader_step_down command such that leader is forced to become follower.
// 5. Trigger unsafe config change on the follower.
// 6. Wait until the new config is populated on leader and master.
// 7. Verify that new config does not contain old followers and a standby node
//    has populated the new config.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigFollowerWithPendingConfig) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 5;
  FLAGS_num_replicas = 3;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  // Shut down servers follower1 and follower2,
  // so that leader can't replicate future config change ops.
  cluster_->tablet_server_by_uuid(followers[0]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(followers[1]->uuid())->Shutdown();
  // Restart master to cleanup cache of dead servers from its
  // list of candidate servers to place the new replicas.
  cluster_->master()->Shutdown();
  ASSERT_OK(cluster_->master()->Restart());

  // Now try to replicate a ChangeConfig operation. This should get stuck and time out
  // because the server can't replicate any operations.
  Status s =
      RemoveServer(leader_ts, tablet_id_, followers[1], -1, MonoDelta::FromSeconds(2), nullptr);
  ASSERT_TRUE(s.IsTimedOut());

  // Force leader to step down, best effort command since the leadership
  // could change anytime during cluster lifetime.
  //  string stderr;
  ASSERT_OK(CallAdmin("leader_stepdown", tablet_id_));
  //  s = Subprocess::Call({GetybCtlAbsolutePath(), "tablet", "leader_step_down",
  //                        cluster_->master()->bound_rpc_addr().ToString(),
  //                        tablet_id_},
  //                       "", nullptr, &stderr);
  //  bool not_currently_leader = stderr.find(
  //      Status::IllegalState("").CodeAsString()) != string::npos;
  //  ASSERT_TRUE(s.ok() || not_currently_leader);

  LOG(INFO) << "Change Config Op timed out, Sending a Replace config "
            << "command when change config op is pending on the leader.";
  const string& leader_addr = Substitute(
      "$0:$1",
      leader_ts->registration->common().private_rpc_addresses(0).host(),
      leader_ts->registration->common().private_rpc_addresses(0).port());
  const vector<string> peer_uuid_list = {leader_ts->uuid()};
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, leader_addr, peer_uuid_list));

  // Find a remaining node which will be picked for re-replication.
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Master may try to add the servers which are down until tserver_unresponsive_timeout_ms,
  // so it is safer to wait until consensus metadata has 3 voters on new_node.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(3, new_node, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  LOG(INFO) << "Waiting for Master to see new config...";
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[1]->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[0]->uuid());
  }
}

// Test unsafe config change when there are back to back pending configs on leader logs.
// 1. Instantiate external minicluster with 1 tablet having 3 replicas and 5 TS.
// 2. Shut down both the followers.
// 3. Trigger a regular config change on the leader which remains pending on leader.
// 4. Set a fault crash flag to trigger upon next commit of config change.
// 5. Trigger unsafe config change on the surviving leader which should trigger
//    the fault while the old config change is being committed.
// 6. Shutdown and restart the leader and verify that tablet bootstrapped on leader.
// 7. Verify that a new node has populated the new config with 3 voters.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigWithPendingConfigsOnWAL) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 5;
  FLAGS_num_replicas = 3;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  ASSERT_OK(FindTabletFollowers(active_tablet_servers, tablet_id_, kTimeout, &followers));
  // Wait for initial NO_OP to be committed by the leader.
  ASSERT_OK(WaitForOpFromCurrentTerm(leader_ts, tablet_id_, OpIdType::COMMITTED_OPID, kTimeout));

  // Shut down servers follower1 and follower2,
  // so that leader can't replicate future config change ops.
  cluster_->tablet_server_by_uuid(followers[0]->uuid())->Shutdown();
  cluster_->tablet_server_by_uuid(followers[1]->uuid())->Shutdown();

  // Now try to replicate a ChangeConfig operation. This should get stuck and time out
  // because the server can't replicate any operations.
  Status s =
      RemoveServer(leader_ts, tablet_id_, followers[1], -1, MonoDelta::FromSeconds(2), nullptr);
  ASSERT_TRUE(s.IsTimedOut());

  LOG(INFO) << "Change Config Op timed out, Sending a Replace config "
            << "command when change config op is pending on the leader.";
  const string& leader_addr = Substitute(
      "$0:$1",
      leader_ts->registration->common().private_rpc_addresses(0).host(),
      leader_ts->registration->common().private_rpc_addresses(0).port());
  const vector<string> peer_uuid_list = {leader_ts->uuid()};
  ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, leader_addr, peer_uuid_list));

  // Inject the crash via TEST_fault_crash_before_cmeta_flush flag.
  // Tablet will find 2 pending configs back to back during bootstrap,
  // one from ChangeConfig (RemoveServer) and another from UnsafeChangeConfig.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server_by_uuid(leader_ts->uuid()),
      "TEST_fault_crash_before_cmeta_flush", "1.0"));

  // Find a remaining node which will be picked for re-replication.
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);
  // Restart master to cleanup cache of dead servers from its list of candidate
  // servers to trigger placement of new replicas on healthy servers.
  cluster_->master()->Shutdown();
  ASSERT_OK(cluster_->master()->Restart());

  auto ts_process = cluster_->tablet_server_by_uuid(leader_ts->uuid());
  ASSERT_OK(cluster_->WaitForTSToCrash(ts_process, kTimeout));
  // ASSERT_OK(cluster_->tablet_server_by_uuid(leader_ts->uuid())->WaitForInjectedCrash(kTimeout));

  cluster_->tablet_server_by_uuid(leader_ts->uuid())->Shutdown();
  ASSERT_OK(cluster_->tablet_server_by_uuid(leader_ts->uuid())->Restart());
  vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB> tablets_ignored;
  ASSERT_OK(WaitForNumTabletsOnTS(leader_ts, 1, kTimeout, &tablets_ignored));
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(3, new_node, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 3, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[0]->uuid());
    ASSERT_NE(replica.ts_info().permanent_uuid(), followers[1]->uuid());
  }
}

// Test unsafe config change on a 5-replica tablet when the mulitple pending configs
// on the surviving node.
// 1. Instantiate external minicluster with 1 tablet having 5 replicas and 9 TS.
// 2. Shut down all the followers.
// 3. Trigger unsafe config changes on the surviving leader with those
//    dead followers in the new config.
// 4. Wait until the new config is populated on the master and the new leader.
// 5. Verify that new config does not contain old followers.
TEST_F(YBTsCliUnsafeChangeTest, TestUnsafeChangeConfigWithMultiplePendingConfigs) {
  MonoDelta kTimeout = MonoDelta::FromSeconds(30);
  FLAGS_num_tablet_servers = 9;
  FLAGS_num_replicas = 5;
  // Retire the dead servers early with these settings.
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers = TServerDetailsVector(tablet_servers_);
  vector<ExternalTabletServer*> external_tservers;
  for (TServerDetails* ts : tservers) {
    external_tservers.push_back(cluster_->tablet_server_by_uuid(ts->uuid()));
  }

  // Determine the list of tablet servers currently in the config.
  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.equal_range(tablet_id_);
  for (auto it = iter.first; it != iter.second; ++it) {
    InsertOrDie(&active_tablet_servers, it->second->uuid(), it->second);
  }

  // Get a baseline config reported to the master.
  LOG(INFO) << "Waiting for Master to see the current replicas...";
  master::TabletLocationsPB tablet_locations;
  bool has_leader;
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 5, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  ASSERT_TRUE(has_leader) << yb::ToString(tablet_locations);

  TServerDetails* leader_ts;
  ASSERT_OK(FindTabletLeader(active_tablet_servers, tablet_id_, kTimeout, &leader_ts));
  vector<TServerDetails*> followers;
  for (const auto& elem : active_tablet_servers) {
    if (elem.first == leader_ts->uuid()) {
      continue;
    }
    followers.push_back(elem.second);
    cluster_->tablet_server_by_uuid(elem.first)->Shutdown();
  }
  ASSERT_EQ(4, followers.size());

  // Shutdown master to cleanup cache of dead servers from its list of candidate
  // servers to trigger placement of new replicas on healthy servers when we restart later.
  cluster_->master()->Shutdown();

  const string& leader_addr = Substitute(
      "$0:$1",
      leader_ts->registration->common().private_rpc_addresses(0).host(),
      leader_ts->registration->common().private_rpc_addresses(0).port());

  // This should keep the multiple pending configs on the node since we are
  // adding all the dead followers to the new config, and then eventually we write
  // just one surviving node to the config.
  // New config write sequences are: {ABCDE}, {ABCD}, {ABC}, {AB}, {A},
  // A being the leader node where config is written and rest of the nodes are
  // dead followers.
  for (int num_replicas = static_cast<int>(followers.size()); num_replicas >= 0; num_replicas--) {
    vector<string> peer_uuid_list;
    peer_uuid_list.push_back(leader_ts->uuid());
    for (int i = 0; i < num_replicas; i++) {
      peer_uuid_list.push_back(followers[i]->uuid());
    }
    ASSERT_OK(RunUnsafeChangeConfig(this, tablet_id_, leader_addr, peer_uuid_list));
  }

  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(1, leader_ts, tablet_id_, kTimeout));
  ASSERT_OK(cluster_->master()->Restart());

  // Find a remaining node which will be picked for re-replication.
  vector<TServerDetails*> all_tservers = TServerDetailsVector(tablet_servers_);
  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : all_tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Master may try to add the servers which are down until tserver_unresponsive_timeout_ms,
  // so it is safer to wait until consensus metadata has 5 voters on new_node.
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(5, new_node, tablet_id_, kTimeout));

  // Wait for the master to be notified of the config change.
  LOG(INFO) << "Waiting for Master to see new config...";
  ASSERT_OK(itest::WaitForReplicasReportedToMaster(
      cluster_.get(), 5, tablet_id_, kTimeout, itest::WAIT_FOR_LEADER, &has_leader,
      &tablet_locations));
  LOG(INFO) << "Tablet locations:\n" << yb::ToString(tablet_locations);
  for (const master::TabletLocationsPB_ReplicaPB& replica : tablet_locations.replicas()) {
    // Verify that old followers aren't part of new config.
    for (const auto& old_follower : followers) {
      ASSERT_NE(replica.ts_info().permanent_uuid(), old_follower->uuid());
    }
  }
}

} // namespace tools
} // namespace yb
