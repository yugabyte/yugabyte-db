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

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master_cluster_client.h"
#include "yb/master/master_error.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_load_balancing);
DECLARE_bool(persist_tserver_registry);
DECLARE_int32(replication_factor);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(tserver_unresponsive_timeout_ms);

namespace yb::master {

class MasterClusterTest : public YBTest {
 public:
  virtual void SetUp() override;

  virtual void TearDown() override;

  virtual MiniClusterOptions CreateMiniClusterOptions();

  Result<MasterClusterClient> CreateClusterClient();

  Result<tserver::TabletServerServiceProxy> CreateTabletServerServiceProxy(const std::string& uuid);

  Result<tserver::ListTabletsForTabletServerResponsePB> ListTabletsForTabletServer(
      const tserver::TabletServerServiceProxy& proxy);
  Result<tserver::ListTabletsForTabletServerResponsePB> ListTabletsForTabletServer(
      const std::string& uuid);

  Status DrainTabletServer(
      const std::string& uuid, const MasterClusterClient& client, MonoDelta timeout);

  Status ShutdownTabletServer(const std::string& uuid);

  Status WaitForMasterLeaderToMarkTabletServerDead(
      const std::string& uuid, const MasterClusterClient& client, MonoDelta timeout);

 protected:
  std::unique_ptr<MiniCluster> cluster_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

class RemoveTabletServerTest : public MasterClusterTest {
 public:
  virtual void SetUp() override;

  virtual MiniClusterOptions CreateMiniClusterOptions() override;

};

TEST_F(RemoveTabletServerTest, HappyPath) {
  auto cluster_client = ASSERT_RESULT(CreateClusterClient());
  auto tserver_resp = ASSERT_RESULT(cluster_client.ListTabletServers());
  ASSERT_GE(tserver_resp.servers().size(), 3);
  auto tserver_to_remove = tserver_resp.servers(0);
  auto& uuid_to_remove = tserver_to_remove.instance_id().permanent_uuid();
  ASSERT_OK(DrainTabletServer(uuid_to_remove, cluster_client, 60s));
  ASSERT_OK(ShutdownTabletServer(uuid_to_remove));

  // Reduce the timeout so we don't have to wait too long for the tserver to be marked unresponsive.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 5 * 1000;
  ASSERT_OK(WaitForMasterLeaderToMarkTabletServerDead(uuid_to_remove, cluster_client, 30s));
  ASSERT_OK(cluster_client.RemoveTabletServer(
      std::string(tserver_to_remove.instance_id().permanent_uuid())));
  // Verify the tablet server is removed by calling the list tablet servers rpc.
  auto find_tserver_result = ASSERT_RESULT(cluster_client.GetTabletServer(uuid_to_remove));
  EXPECT_EQ(find_tserver_result, std::nullopt);
}

TEST_F(RemoveTabletServerTest, NotBlacklisted) {
  auto cluster_client = ASSERT_RESULT(CreateClusterClient());
  auto tserver_resp = ASSERT_RESULT(cluster_client.ListTabletServers());
  ASSERT_GE(tserver_resp.servers().size(), 3);
  auto tserver_to_remove = tserver_resp.servers(0);
  auto& uuid_to_remove = tserver_to_remove.instance_id().permanent_uuid();
  ASSERT_OK(DrainTabletServer(uuid_to_remove, cluster_client, 60s));

  ASSERT_OK(ShutdownTabletServer(uuid_to_remove));
  // Reduce the timeout so we don't have to wait too long for the tserver to be marked unresponsive.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 5 * 1000;
  ASSERT_OK(WaitForMasterLeaderToMarkTabletServerDead(uuid_to_remove, cluster_client, 30s));

  // Disable the cluster balancer and unblacklist the node.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ASSERT_OK(cluster_client.ClearBlacklist());

  auto s = cluster_client.RemoveTabletServer(
      std::string(tserver_to_remove.instance_id().permanent_uuid()));
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToUserMessage(), "because it is not blacklisted");
}

TEST_F(RemoveTabletServerTest, StillAlive) {
  auto cluster_client = ASSERT_RESULT(CreateClusterClient());
  auto tserver_resp = ASSERT_RESULT(cluster_client.ListTabletServers());
  ASSERT_GE(tserver_resp.servers().size(), 3);
  auto tserver_to_remove = tserver_resp.servers(0);
  auto& uuid_to_remove = tserver_to_remove.instance_id().permanent_uuid();
  ASSERT_OK(DrainTabletServer(uuid_to_remove, cluster_client, 60s));

  auto s = cluster_client.RemoveTabletServer(
      std::string(tserver_to_remove.instance_id().permanent_uuid()));
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToUserMessage(), "because it is live");
}

TEST_F(RemoveTabletServerTest, StillHostingTablets) {
  auto cluster_client = ASSERT_RESULT(CreateClusterClient());
  auto tserver_resp = ASSERT_RESULT(cluster_client.ListTabletServers());
  // Pick a tserver that is hosting tablets. This should work because the global transaction table
  // is created automatically and its tablets are hosted by tservers.
  auto tserver_it = std::find_if(
      tserver_resp.servers().begin(), tserver_resp.servers().end(), [this](const auto& server) {
        auto& uuid = server.instance_id().permanent_uuid();
        auto result = ListTabletsForTabletServer(uuid);
        if (!result.ok()) {
          return false;
        }
        for (const auto& entry : result->entries()) {
          if (entry.state() == tablet::RaftGroupStatePB::RUNNING) {
            return true;
          }
        }
        return false;
      });
  ASSERT_NE(tserver_it, tserver_resp.servers().end())
      << "Couldn't find a tserver hosting live tablets";
  // We're going to blacklist the tserver to avoid triggering the "not blacklisted" validation
  // logic. So disable the cluster balancer.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  auto tserver_to_remove = *tserver_it;
  auto& uuid_to_remove = tserver_to_remove.instance_id().permanent_uuid();

  ASSERT_OK(ShutdownTabletServer(uuid_to_remove));
  // Reduce the timeout so we don't have to wait too long for the tserver to be marked unresponsive.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 5 * 1000;
  ASSERT_OK(WaitForMasterLeaderToMarkTabletServerDead(uuid_to_remove, cluster_client, 30s));
  ASSERT_OK(cluster_client.BlacklistHost(
      HostPortPB(tserver_to_remove.registration().common().broadcast_addresses(0))));
  auto s = cluster_client.RemoveTabletServer(
      std::string(tserver_to_remove.instance_id().permanent_uuid()));
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToUserMessage(), "because it is hosting tablet");
}

TEST_F(RemoveTabletServerTest, RemoveMissingTabletServer) {
  auto cluster_client = ASSERT_RESULT(CreateClusterClient());
  auto result = cluster_client.RemoveTabletServer("foobarbaz");
  ASSERT_NOK(result);
  ASSERT_EQ(master::MasterError(result), master::MasterErrorPB::TABLET_SERVER_NOT_FOUND);
}

void MasterClusterTest::SetUp() {
  YBTest::SetUp();
  cluster_ = std::make_unique<MiniCluster>(CreateMiniClusterOptions());
  ASSERT_OK(cluster_->Start());
  messenger_ = ASSERT_RESULT(
      rpc::MessengerBuilder("master-cluster-test-messenger").set_num_reactors(1).Build());
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
}

void MasterClusterTest::TearDown() {
  messenger_->Shutdown();
  cluster_->Shutdown();
  cluster_ = nullptr;
  YBTest::TearDown();
}

MiniClusterOptions MasterClusterTest::CreateMiniClusterOptions() {
  return MiniClusterOptions();
}

Result<MasterClusterClient> MasterClusterTest::CreateClusterClient() {
  return MasterClusterClient(MasterClusterProxy(
      proxy_cache_.get(), VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->bound_rpc_addr()));
}

Result<tserver::TabletServerServiceProxy> MasterClusterTest::CreateTabletServerServiceProxy(
    const std::string& uuid) {
  auto mini_ts = cluster_->find_tablet_server(uuid);
  if (mini_ts == nullptr) {
    return STATUS_FORMAT(NotFound, "Couldn't find tserver with uuid $0", uuid);
  }
  return tserver::TabletServerServiceProxy(
      proxy_cache_.get(), HostPort::FromBoundEndpoint(mini_ts->bound_rpc_addr()));
}

Result<tserver::ListTabletsForTabletServerResponsePB> MasterClusterTest::ListTabletsForTabletServer(
    const tserver::TabletServerServiceProxy& proxy) {
  tserver::ListTabletsForTabletServerRequestPB req;
  tserver::ListTabletsForTabletServerResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy.ListTabletsForTabletServer(req, &resp, &rpc));
  return resp;
}

Result<tserver::ListTabletsForTabletServerResponsePB> MasterClusterTest::ListTabletsForTabletServer(
    const std::string& uuid) {
  auto proxy = VERIFY_RESULT(CreateTabletServerServiceProxy(uuid));
  return ListTabletsForTabletServer(proxy);
}

Status MasterClusterTest::DrainTabletServer(
    const std::string& uuid, const MasterClusterClient& client, MonoDelta timeout) {
  auto tservers_resp = VERIFY_RESULT(client.ListTabletServers());
  auto tserver_it = std::find_if(
      tservers_resp.servers().begin(), tservers_resp.servers().end(),
      [&uuid](const auto& server) { return server.instance_id().permanent_uuid() == uuid; });
  if (tserver_it == tservers_resp.servers().end()) {
    return STATUS_FORMAT(
        NotFound, "Couldn't find tserver with uuid $0 in ListTabletServers response", uuid);
  }
  auto& hp = tserver_it->registration().common().broadcast_addresses(0);
  RETURN_NOT_OK(client.BlacklistHost(HostPortPB(hp)));

  // Now wait for the cluster balancer to move all tablets off the tserver.
  auto ts_proxy = VERIFY_RESULT(CreateTabletServerServiceProxy(uuid));
  std::string message;
  return WaitFor(
      [this, &ts_proxy, &message, &uuid]() -> Result<bool> {
        auto resp = VERIFY_RESULT(ListTabletsForTabletServer(ts_proxy));
        for (const auto& entry : resp.entries()) {
          if (entry.state() != tablet::RaftGroupStatePB::SHUTDOWN) {
            message = Format(
                "ts $0 is still hosting a tablet peer, example: $1", uuid, entry.DebugString());
            return false;
          }
        }
        return true;
      },
      timeout, message);
}

Status MasterClusterTest::ShutdownTabletServer(const std::string& uuid) {
  auto mini_ts = cluster_->find_tablet_server(uuid);
  if (!mini_ts) {
    return STATUS_FORMAT(NotFound, "Couldn't find tserver $0 in minicluster", uuid);
  }
  mini_ts->Shutdown();
  return Status::OK();
}

Status MasterClusterTest::WaitForMasterLeaderToMarkTabletServerDead(
    const std::string& uuid, const MasterClusterClient& client, MonoDelta timeout) {
  return WaitFor(
      [&client, &uuid]() -> Result<bool> {
        auto tserver = VERIFY_RESULT(client.GetTabletServer(uuid));
        return tserver && !tserver->alive();
      },
      timeout, "Tserver not present or still alive");
}

void RemoveTabletServerTest::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_persist_tserver_registry) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 1;
  MasterClusterTest::SetUp();
}

MiniClusterOptions RemoveTabletServerTest::CreateMiniClusterOptions() {
  auto opts = MiniClusterOptions();
  opts.num_tablet_servers = 4;
  return opts;
}

}  // namespace yb::master
