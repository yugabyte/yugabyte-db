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

#include <chrono>
#include "yb/client/stateful_services/test_echo_service_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-internal.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/master/master_defaults.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/service_util.h"

DECLARE_int32(follower_unavailable_considered_failed_sec);
DECLARE_bool(TEST_echo_service_enabled);
DECLARE_string(vmodule);

namespace yb {

using namespace std::chrono_literals;
const MonoDelta kTimeout = 20s * kTimeMultiplier;
const int kNumMasterServers = 3;
const int kNumTServers = 3;
const client::YBTableName service_table_name(
    YQL_DATABASE_CQL, master::kSystemNamespaceName,
    StatefulServiceKind_Name(StatefulServiceKind::TEST_ECHO) + "_table");

class StatefulServiceTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    FLAGS_TEST_echo_service_enabled = true;
    ASSERT_OK(SET_FLAG(vmodule, "stateful_service*=4"));
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = kNumTServers;
    opts.num_masters = kNumMasterServers;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));

    ASSERT_OK(CreateClient());
    ASSERT_OK(client_->WaitForCreateTableToFinish(service_table_name));
    std::vector<TabletId> tablet_ids;
    ASSERT_OK(client_->GetTablets(service_table_name, 0 /* max_tablets */, &tablet_ids, NULL));
    ASSERT_EQ(tablet_ids.size(), 1);
    tablet_id_.swap(tablet_ids[0]);
    ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(kTimeout));
  }

  Status VerifyEchoServiceHostedOnAllPeers() {
    for (auto& tserver : cluster_->mini_tablet_servers()) {
      auto initial_peer_tablet =
          VERIFY_RESULT(LookupTabletPeer(tserver->server()->tablet_peer_lookup(), tablet_id_));
      auto hosted_service = initial_peer_tablet.tablet->metadata()->GetHostedServiceList();
      SCHECK_EQ(
          hosted_service.size(), 1, IllegalState,
          Format("Expected 1 hosted service: Received: $0", ToString(hosted_service)));
      SCHECK_EQ(
          *hosted_service.begin(), StatefulServiceKind::TEST_ECHO, IllegalState,
          "Expected TEST_ECHO service");
    }

    return Status::OK();
  }

  TabletId tablet_id_;
};

TEST_F(StatefulServiceTest, TestRemoteBootstrap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_follower_unavailable_considered_failed_sec) =
      5 * kTimeMultiplier;

  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  master::MasterClusterProxy master_proxy(&client_->proxy_cache(), leader_master->bound_rpc_addr());
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, &client_->proxy_cache()));

  // Pick a random tserver and shut it down for for 2x the time it takes for a follower to be
  // considered failed. This will cause it to get remote bootstrapped.
  auto t_server = cluster_->mini_tablet_server(0);
  t_server->Shutdown();

  SleepFor(FLAGS_follower_unavailable_considered_failed_sec * 2s);

  // Wait till the peer is removed from quorum.
  itest::TServerDetails* leader_ts = nullptr;
  ASSERT_OK(FindTabletLeader(ts_map, tablet_id_, kTimeout, &leader_ts));
  ASSERT_OK(itest::WaitUntilCommittedConfigNumVotersIs(
      kNumTServers - 1, leader_ts, tablet_id_, kTimeout));

  // Restart the server and wait for it bootstrap.
  ASSERT_OK(t_server->Start());
  ASSERT_OK(
      itest::WaitUntilCommittedConfigNumVotersIs(kNumTServers, leader_ts, tablet_id_, kTimeout));

  // Wait for new bootstrapped replica to catch up.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto op_ids = VERIFY_RESULT(itest::GetLastOpIdForEachReplica(
            tablet_id_, TServerDetailsVector(ts_map), consensus::OpIdType::COMMITTED_OPID,
            kTimeout));
        SCHECK_EQ(op_ids.size(), 3, IllegalState, "Expected 3 replicas");

        return op_ids[0] == op_ids[1] && op_ids[1] == op_ids[2];
      },
      kTimeout, "Waiting for all replicas to have the same committed op id"));

  ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(kTimeout));

  // Failover to the rebootstrapped server.
  ASSERT_OK(FindTabletLeader(ts_map, tablet_id_, kTimeout, &leader_ts));
  auto* new_leader = ts_map[t_server->server()->permanent_uuid()].get();
  if (leader_ts != new_leader) {
    ASSERT_OK(itest::LeaderStepDown(leader_ts, tablet_id_, new_leader, kTimeout));
  }
  ASSERT_OK(itest::WaitUntilLeader(new_leader, tablet_id_, kTimeout));

  ASSERT_OK(VerifyEchoServiceHostedOnAllPeers());
}

TEST_F(StatefulServiceTest, TestGetStatefulServiceLocation) {
  // Verify the Hosted service is set on all the replicas.
  ASSERT_OK(VerifyEchoServiceHostedOnAllPeers());

  // Verify GetStatefulServiceLocation returns the correct location.
  auto initial_leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
  auto location =
      ASSERT_RESULT(client_->GetStatefulServiceLocation(StatefulServiceKind::TEST_ECHO));
  ASSERT_EQ(location.permanent_uuid(), initial_leader->server()->permanent_uuid());

  initial_leader->Shutdown();

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
        return leader != nullptr;
      },
      kTimeout, "Wait for new leader"));

  ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(kTimeout));

  // Verify GetStatefulServiceLocation returns the correct location again.
  auto final_leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
  ASSERT_NE(final_leader, initial_leader);

  location = ASSERT_RESULT(client_->GetStatefulServiceLocation(StatefulServiceKind::TEST_ECHO));
  ASSERT_EQ(location.permanent_uuid(), final_leader->server()->permanent_uuid());

  ASSERT_OK(initial_leader->Start());
}

TEST_F(StatefulServiceTest, TestEchoService) {
  auto service_client =
      ASSERT_RESULT(cluster_->CreateStatefulServiceClient<client::TestEchoServiceClient>());

  stateful_service::GetEchoRequestPB req;
  stateful_service::GetEchoResponsePB resp;
  req.set_message("Hello World!");

  ASSERT_OK(service_client->GetEcho(CoarseMonoClock::Now() + kTimeout, req, &resp));

  ASSERT_EQ(resp.message(), "Hello World! World! World!");
  auto initial_node_id = resp.node_id();

  // Make sure the tablet leader is the one serving the request.
  auto initial_leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
  auto initial_leader_uuid = initial_leader->server()->permanent_uuid();
  ASSERT_EQ(resp.node_id(), initial_leader_uuid);

  initial_leader->Shutdown();

  resp.Clear();
  req.set_message("Hungry shark doo");
  ASSERT_OK(service_client->GetEcho(CoarseMonoClock::Now() + kTimeout, req, &resp));

  ASSERT_EQ(resp.message(), "Hungry shark doo doo doo");
  ASSERT_NE(resp.node_id(), initial_leader_uuid);

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
        return leader != nullptr;
      },
      kTimeout, "Wait for new leader"));

  ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(kTimeout));

  auto final_leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
  auto final_leader_uuid = final_leader->server()->permanent_uuid();
  ASSERT_NE(final_leader_uuid, initial_leader_uuid);

  resp.Clear();
  req.set_message("Anybody there?");
  ASSERT_OK(service_client->GetEcho(CoarseMonoClock::Now() + kTimeout, req, &resp));

  // Make sure the new tablet leader is the one serving the request.
  ASSERT_EQ(resp.message(), "Anybody there? there? there?");
  ASSERT_EQ(resp.node_id(), final_leader_uuid);

  ASSERT_OK(initial_leader->Start());
}
}  // namespace yb
