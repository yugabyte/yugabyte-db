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
// Tests for the kudu-admin command-line tool.

#include <gtest/gtest.h>

#include "kudu/client/client.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/test_workload.h"
#include "kudu/integration-tests/ts_itest-base.h"
#include "kudu/util/subprocess.h"
#include "kudu/util/test_util.h"

namespace kudu {
namespace tools {

using client::KuduClient;
using client::KuduClientBuilder;
using client::sp::shared_ptr;
using itest::TabletServerMap;
using itest::TServerDetails;
using strings::Substitute;

static const char* const kAdminToolName = "kudu-admin";

class AdminCliTest : public tserver::TabletServerIntegrationTestBase {
 protected:
  // Figure out where the admin tool is.
  string GetAdminToolPath() const;
};

string AdminCliTest::GetAdminToolPath() const {
  string exe;
  CHECK_OK(Env::Default()->GetExecutablePath(&exe));
  string binroot = DirName(exe);
  string tool_path = JoinPathSegments(binroot, kAdminToolName);
  CHECK(Env::Default()->FileExists(tool_path)) << "kudu-admin tool not found at " << tool_path;
  return tool_path;
}

// Test kudu-admin config change while running a workload.
// 1. Instantiate external mini cluster with 3 TS.
// 2. Create table with 2 replicas.
// 3. Invoke kudu-admin CLI to invoke a config change.
// 4. Wait until the new server bootstraps.
// 5. Profit!
TEST_F(AdminCliTest, TestChangeConfig) {
  FLAGS_num_tablet_servers = 3;
  FLAGS_num_replicas = 2;

  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers;
  AppendValuesFromMap(tablet_servers_, &tservers);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  TabletServerMap active_tablet_servers;
  TabletServerMap::const_iterator iter = tablet_replicas_.find(tablet_id_);
  TServerDetails* leader = iter->second;
  TServerDetails* follower = (++iter)->second;
  InsertOrDie(&active_tablet_servers, leader->uuid(), leader);
  InsertOrDie(&active_tablet_servers, follower->uuid(), follower);

  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  // Elect the leader (still only a consensus config size of 2).
  ASSERT_OK(StartElection(leader, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(30), active_tablet_servers,
                                  tablet_id_, 1));

  TestWorkload workload(cluster_.get());
  workload.set_table_name(kTableId);
  workload.set_timeout_allowed(true);
  workload.set_write_timeout_millis(10000);
  workload.set_num_replicas(FLAGS_num_replicas);
  workload.set_num_write_threads(1);
  workload.set_write_batch_size(1);
  workload.Setup();
  workload.Start();

  // Wait until the Master knows about the leader tserver.
  TServerDetails* master_observed_leader;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &master_observed_leader));
  ASSERT_EQ(leader->uuid(), master_observed_leader->uuid());

  LOG(INFO) << "Adding tserver with uuid " << new_node->uuid() << " as VOTER...";
  string exe_path = GetAdminToolPath();
  string arg_str = Substitute("$0 -master_addresses $1 change_config $2 ADD_SERVER $3 VOTER",
                              exe_path,
                              cluster_->master()->bound_rpc_addr().ToString(),
                              tablet_id_, new_node->uuid());
  ASSERT_OK(Subprocess::Call(arg_str));

  InsertOrDie(&active_tablet_servers, new_node->uuid(), new_node);
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                leader, tablet_id_,
                                                MonoDelta::FromSeconds(10)));

  workload.StopAndJoin();
  int num_batches = workload.batches_completed();

  LOG(INFO) << "Waiting for replicas to agree...";
  // Wait for all servers to replicate everything up through the last write op.
  // Since we don't batch, there should be at least # rows inserted log entries,
  // plus the initial leader's no-op, plus 1 for
  // the added replica for a total == #rows + 2.
  int min_log_index = num_batches + 2;
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(30),
                                  active_tablet_servers, tablet_id_,
                                  min_log_index));

  int rows_inserted = workload.rows_inserted();
  LOG(INFO) << "Number of rows inserted: " << rows_inserted;

  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
  NO_FATALS(v.CheckRowCount(kTableId, ClusterVerifier::AT_LEAST, rows_inserted));

  // Now remove the server once again.
  LOG(INFO) << "Removing tserver with uuid " << new_node->uuid() << " from the config...";
  arg_str = Substitute("$0 -master_addresses $1 change_config $2 REMOVE_SERVER $3",
                       exe_path,
                       cluster_->master()->bound_rpc_addr().ToString(),
                       tablet_id_, new_node->uuid());

  ASSERT_OK(Subprocess::Call(arg_str));

  ASSERT_EQ(1, active_tablet_servers.erase(new_node->uuid()));
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                leader, tablet_id_,
                                                MonoDelta::FromSeconds(10)));
}

TEST_F(AdminCliTest, TestDeleteTable) {
  FLAGS_num_tablet_servers = 1;
  FLAGS_num_replicas = 1;

  vector<string> ts_flags, master_flags;
  BuildAndStart(ts_flags, master_flags);
  string master_address = cluster_->master()->bound_rpc_addr().ToString();

  shared_ptr<KuduClient> client;
  CHECK_OK(KuduClientBuilder()
        .add_master_server_addr(master_address)
        .Build(&client));

  // Default table that gets created;
  string table_name = "TestTable";

  string exe_path = GetAdminToolPath();
  string arg_str = Substitute("$0 -master_addresses $1 delete_table $2",
                              exe_path,
                              master_address,
                              table_name);

  ASSERT_OK(Subprocess::Call(arg_str));

  vector<string> tables;
  ASSERT_OK(client->ListTables(&tables));
  ASSERT_TRUE(tables.empty());
}

} // namespace tools
} // namespace kudu
