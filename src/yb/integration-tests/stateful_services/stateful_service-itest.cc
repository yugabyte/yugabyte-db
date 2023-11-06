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

#include "yb/client/client-internal.h"
#include "yb/client/session.h"
#include "yb/client/stateful_services/test_echo_service_client.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/service_util.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/monotime.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_int32(follower_unavailable_considered_failed_sec);
DECLARE_bool(TEST_echo_service_enabled);
DECLARE_string(vmodule);
DECLARE_bool(TEST_combine_batcher_errors);

namespace yb {

using namespace std::chrono_literals;
const MonoDelta kTimeout = 20s * kTimeMultiplier;
const int kNumMasterServers = 3;
const int kNumTServers = 3;
const client::YBTableName service_table_name =
    stateful_service::GetStatefulServiceTableName(StatefulServiceKind::TEST_ECHO);
const Status kUninitializedStatus = STATUS(IllegalState, "Uninitialized");

class StatefulServiceTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_echo_service_enabled) = true;
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

struct TableRow {
  std::string node_uuid;
  std::string message;
};

namespace {
Status ValidateRowsFromServiceTable(
    const client::TableHandle& table, int expected_row_count, const std::string& message,
    const std::string& node_uuid) {
  std::vector<TableRow> table_rows;

  Status table_scan_status;
  client::TableIteratorOptions options;
  options.error_handler = [&table_scan_status](const Status& status) {
    table_scan_status = status;
  };

  for (const auto& row : client::TableRange(table, options)) {
    TableRow table_row;
    table_row.node_uuid = row.column(master::kTestEchoNodeIdIdx).string_value();
    table_row.message = row.column(master::kTestEchoMessageIdx).string_value();
    table_rows.emplace_back(std::move(table_row));
  }
  RETURN_NOT_OK(table_scan_status);

  SCHECK_EQ(table_rows.size(), expected_row_count, IllegalState, "Row count mismatch");
  auto it = std::find_if(table_rows.begin(), table_rows.end(), [&message](const TableRow& row) {
    return row.message == message;
  });
  SCHECK(it != table_rows.end(), IllegalState, Format("Row for message '$0' not found", message));
  SCHECK_EQ(
      it->node_uuid, node_uuid, IllegalState,
      Format("Node UUID for message '$0' is incorrect", message));

  return Status::OK();
}

}  // namespace

TEST_F(StatefulServiceTest, TestEchoService) {
  auto service_client =
      ASSERT_RESULT(cluster_->CreateStatefulServiceClient<client::TestEchoServiceClient>());

  auto service_table = std::make_unique<client::TableHandle>();
  ASSERT_OK(service_table->Open(service_table_name, client_.get()));
  auto session = client_->NewSession(kTimeout);

  stateful_service::GetEchoRequestPB echo_req;
  stateful_service::GetEchoCountRequestPB count_req;
  auto message = "Hello World!";
  echo_req.set_message(message);

  auto echo_resp = ASSERT_RESULT(service_client->GetEcho(echo_req, kTimeout));

  ASSERT_EQ(echo_resp.message(), "Hello World! World! World!");
  auto initial_node_id = echo_resp.node_id();

  // Make sure the tablet leader is the one serving the request.
  auto initial_leader = GetLeaderForTablet(cluster_.get(), tablet_id_);
  auto initial_leader_uuid = initial_leader->server()->permanent_uuid();
  ASSERT_EQ(echo_resp.node_id(), initial_leader_uuid);
  ASSERT_OK(ValidateRowsFromServiceTable(*service_table, 1, message, initial_leader_uuid));

  auto count_resp = ASSERT_RESULT(service_client->GetEchoCount(count_req, kTimeout));
  ASSERT_EQ(count_resp.count(), 1);

  initial_leader->Shutdown();

  message = "Hungry shark doo";
  echo_req.set_message(message);
  echo_resp = ASSERT_RESULT(service_client->GetEcho(echo_req, CoarseMonoClock::Now() + kTimeout));

  ASSERT_EQ(echo_resp.message(), "Hungry shark doo doo doo");
  ASSERT_NE(echo_resp.node_id(), initial_leader_uuid);

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

  // We cannot test ValidateRowsFromServiceTable as we dont know which leader which processed the
  // request. Load balancer could have moved the leader before we can find it.
  count_resp = ASSERT_RESULT(service_client->GetEchoCount(count_req, kTimeout));
  ASSERT_EQ(count_resp.count(), 2);

  message = "Anybody there?";
  echo_req.set_message(message);
  echo_resp = ASSERT_RESULT(service_client->GetEcho(echo_req, kTimeout));

  // Make sure the new tablet leader is the one serving the request.
  ASSERT_EQ(echo_resp.message(), "Anybody there? there? there?");
  ASSERT_EQ(echo_resp.node_id(), final_leader_uuid);
  ASSERT_OK(ValidateRowsFromServiceTable(*service_table, 3, message, final_leader_uuid));

  count_resp = ASSERT_RESULT(service_client->GetEchoCount(count_req, kTimeout));
  ASSERT_EQ(count_resp.count(), 3);

  ASSERT_OK(initial_leader->Start());
}

TEST_F(StatefulServiceTest, TestLeadershipChange) {
  // If the tablet leader changes in the middle of a RPC, but after the write then the RPC should
  // still fail. The StatefulServiceClient should retry the RPC on the new leader such that the
  // client is unaware of the leader change.
  // Note: This will lead to double write in our TestEchoService. If this behavior is undesirable
  // then the RPC should be made idempotent using things like primary keys, or persisting request
  // ids to check for duplicates. This is the standard behavior when writing to any database. We
  // should always be prepared to get a failure during the commit due to process crashes or network
  // failures. We don't know if the commit succeeded or not, so we either need to read and check the
  // committed data, or make the operation idempotent.

  // This is needed for the Batcher to propagate back the errors to the caller.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_combine_batcher_errors) = true;

  auto service_client =
      ASSERT_RESULT(cluster_->CreateStatefulServiceClient<client::TestEchoServiceClient>());
  auto service_table = std::make_unique<client::TableHandle>();
  ASSERT_OK(service_table->Open(service_table_name, client_.get()));

  stateful_service::GetEchoCountRequestPB count_req;
  stateful_service::GetEchoCountResponsePB count_resp;
  count_resp = ASSERT_RESULT(service_client->GetEchoCount(count_req, kTimeout));
  ASSERT_EQ(count_resp.count(), 0);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"StatefulRpcServiceBase::HandleRpcRequestWithTermCheck::AfterMethodImpl1",
        "StatefulServiceTest::TestLeadershipChange::BeforeLeaderChange"},
       {"StatefulServiceTest::TestLeadershipChange::AfterLeaderChange",
        "StatefulRpcServiceBase::HandleRpcRequestWithTermCheck::AfterMethodImpl2"}});

  uint64 attempts = 0;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "StatefulServiceClientBase::InvokeRpcSync",
      [&attempts](void* data) { attempts += *(reinterpret_cast<decltype(attempts)*>(data)); });

  yb::SyncPoint::GetInstance()->EnableProcessing();

  stateful_service::GetEchoRequestPB echo_req;

  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id_));

  const auto message = "Hello World!";
  echo_req.set_message(message);
  auto test_thread_holder = TestThreadHolder();
  Result<stateful_service::GetEchoResponsePB> result = kUninitializedStatus;
  test_thread_holder.AddThread([&service_client, &echo_req, &result]() {
    result = service_client->GetEcho(echo_req, kTimeout);
  });

  TEST_SYNC_POINT("StatefulServiceTest::TestLeadershipChange::BeforeLeaderChange");
  ASSERT_OK(StepDown(leader_peer, std::string() /* new_leader_uuid */, ForceStepDown::kTrue));
  TEST_SYNC_POINT("StatefulServiceTest::TestLeadershipChange::AfterLeaderChange");

  test_thread_holder.JoinAll();
  yb::SyncPoint::GetInstance()->DisableProcessing();
  ASSERT_OK(result);
  ASSERT_EQ(result->message(), "Hello World! World! World!");

  // We should fail at least once.
  ASSERT_GT(attempts, 1);

  // We should have logged to the table twice.
  count_resp = ASSERT_RESULT(service_client->GetEchoCount(count_req, kTimeout));
  ASSERT_EQ(count_resp.count(), 2);
}

TEST_F(StatefulServiceTest, TestWriteDuringLeadershipChange) {
  // If the tablet leader changes during a write then the write and the inflight RPC should fail.
  // The StatefulServiceClient should retry the RPC on the new leader such that the client is
  // unaware of the leader change.

  // This is needed for the Batcher to propagate back the errors to the caller.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_combine_batcher_errors) = true;

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TestEchoService::RecordRequestInTable::BeforeApply1",
        "StatefulServiceTest::TestWriteDuringLeadershipChange::BeforeLeaderChange"},
       {"StatefulServiceTest::TestWriteDuringLeadershipChange::AfterLeaderChange",
        "TestEchoService::RecordRequestInTable::BeforeApply2"}});

  uint32 count_ok = 0, count_term_err = 0, count_err = 0;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "TestEchoService::GetEchoImpl::RecordRequestInTable",
      [&count_ok, &count_term_err, &count_err](void* data) {
        Status* status = reinterpret_cast<Status*>(data);
        if (status->ok()) {
          ++count_ok;
        } else if (status->message().Contains("Leader term changed")) {
          ++count_term_err;
        } else {
          LOG(ERROR) << "Unexpected error: " << status->ToString();
          ++count_err;
        }
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto service_client =
      ASSERT_RESULT(cluster_->CreateStatefulServiceClient<client::TestEchoServiceClient>());
  auto service_table = std::make_unique<client::TableHandle>();
  ASSERT_OK(service_table->Open(service_table_name, client_.get()));

  stateful_service::GetEchoRequestPB echo_req;

  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id_));

  echo_req.set_message("Hello World!");
  auto test_thread_holder = TestThreadHolder();
  Result<stateful_service::GetEchoResponsePB> result = kUninitializedStatus;
  test_thread_holder.AddThread([&service_client, &echo_req, &result]() {
    result = service_client->GetEcho(echo_req, kTimeout);
  });

  TEST_SYNC_POINT("StatefulServiceTest::TestWriteDuringLeadershipChange::BeforeLeaderChange");
  ASSERT_OK(StepDown(leader_peer, std::string() /* new_leader_uuid */, ForceStepDown::kTrue));
  TEST_SYNC_POINT("StatefulServiceTest::TestWriteDuringLeadershipChange::AfterLeaderChange");

  test_thread_holder.JoinAll();
  yb::SyncPoint::GetInstance()->DisableProcessing();
  ASSERT_OK(result);
  ASSERT_EQ(result->message(), "Hello World! World! World!");

  // We should fail due to the term change exactly once and succeed exactly once.
  ASSERT_EQ(count_ok, 1);
  ASSERT_EQ(count_err, 0);
  ASSERT_EQ(count_term_err, 1);
}

}  // namespace yb
