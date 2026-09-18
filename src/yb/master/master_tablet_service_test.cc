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
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_tablet_service.h"
#include "yb/master/mini_master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tserver.messages.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/memory/arena.h"
#include "yb/util/test_util.h"

namespace yb::master {

class MasterTabletServiceTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  virtual void SetUp() override;

  virtual MiniClusterOptions GetMiniClusterOptions();

 protected:
  Result<std::shared_ptr<tablet::AbstractTablet>> LookupReadTablet(
      MiniMaster* mini_master, TabletIdView tablet_id, YBConsistencyLevel consistency_level,
      tablet::TabletPeerPtr peer = nullptr) {
    auto* master = mini_master->master();
    MasterTabletServiceImpl service(master->tablet_server(), master);
    tserver::ReadTabletProvider& provider = service;
    ThreadSafeArena arena;
    tserver::ReadResponseMsg resp(&arena);
    return provider.GetTabletForRead(
        tablet_id, std::move(peer), consistency_level, tserver::AllowSplitTablet::kFalse, &resp);
  }
};

class MasterTabletServiceMultiMasterTest : public MasterTabletServiceTest {
 public:
  MiniClusterOptions GetMiniClusterOptions() override {
    auto options = MasterTabletServiceTest::GetMiniClusterOptions();
    options.num_masters = 3;
    return options;
  }

 protected:
  Result<MiniMaster*> RestartFollower() {
    auto* leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      auto* follower = cluster_->mini_master(i);
      if (follower != leader) {
        RETURN_NOT_OK(follower->Restart());
        return follower;
      }
    }
    return STATUS(NotFound, "No master follower");
  }
};

TEST_F(MasterTabletServiceTest, ListMasterServers) {
  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<tserver::TabletServerServiceProxy>());
  tserver::ListMasterServersRequestPB req;
  tserver::ListMasterServersResponsePB resp;
  rpc::RpcController rpc;
  auto status = proxy.ListMasterServers(req, &resp, &rpc);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Not implemented");
}

TEST_F(MasterTabletServiceTest, SysCatalogTabletLookup) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto peer = leader->tablet_peer();
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(
                leader, kSysCatalogTabletId, YBConsistencyLevel::STRONG)), tablet);
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(
                leader, kSysCatalogTabletId, YBConsistencyLevel::STRONG, peer)), tablet);
}

TEST_F(MasterTabletServiceTest, VirtualTabletLookup) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto& catalog_manager = leader->catalog_manager_impl();
  SCOPED_LEADER_SHARED_LOCK(lock, &catalog_manager);
  ASSERT_OK(lock.first_failed_status());
  auto table = catalog_manager.GetTableInfoFromNamespaceNameAndTableName(
      YQL_DATABASE_CQL, "system", "peers");
  ASSERT_TRUE(table);
  auto tablets = ASSERT_RESULT(table->GetTablets());
  ASSERT_EQ(tablets.size(), 1);
  const auto& tablet_id = tablets.front()->tablet_id();
  ASSERT_NOK(catalog_manager.GetServingTablet(tablet_id));
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(leader, tablet_id, YBConsistencyLevel::STRONG)),
            ASSERT_RESULT(catalog_manager.GetSystemTablet(tablet_id)));
}

TEST_F(MasterTabletServiceMultiMasterTest, FollowerSysCatalogTabletLookup) {
  auto* follower = ASSERT_RESULT(RestartFollower());
  ASSERT_LT(follower->catalog_manager().leader_ready_term(), 0);
  auto peer = follower->tablet_peer();
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_RESULT(tablet->SafeTime(
      tablet::RequireLease::kFalse, leader->Now(),
      CoarseMonoClock::Now() + MonoDelta::FromSeconds(30)));
  ASSERT_EQ(ASSERT_RESULT(LookupReadTablet(
                follower, kSysCatalogTabletId, YBConsistencyLevel::CONSISTENT_PREFIX)), tablet);
  ASSERT_EQ(
      ASSERT_RESULT(LookupReadTablet(
          follower, kSysCatalogTabletId, YBConsistencyLevel::CONSISTENT_PREFIX, peer)), tablet);
  auto strong_read = LookupReadTablet(follower, kSysCatalogTabletId, YBConsistencyLevel::STRONG);
  ASSERT_NOK(strong_read);
  ASSERT_TRUE(tserver::IsErrorCodeNotTheLeader(strong_read.status()));
}

TEST_F(MasterTabletServiceMultiMasterTest, FollowerReadRpcRemainsLeaderOnly) {
  auto* follower = ASSERT_RESULT(RestartFollower());
  tserver::TabletServerServiceProxy proxy(&cluster_->proxy_cache(), follower->bound_rpc_addr());
  for (auto consistency : {YBConsistencyLevel::STRONG, YBConsistencyLevel::CONSISTENT_PREFIX}) {
    tserver::ReadRequestPB req;
    req.set_tablet_id(kSysCatalogTabletId);
    req.set_consistency_level(consistency);
    tserver::ReadResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(10));
    ASSERT_OK(proxy.Read(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(resp.error().code(), tserver::TabletServerErrorPB::NOT_THE_LEADER);
  }
}

void MasterTabletServiceTest::SetUp() {
  YBMiniClusterTestBase::SetUp();
  cluster_ = std::make_unique<MiniCluster>(GetMiniClusterOptions());
  ASSERT_OK(cluster_->Start());
}

MiniClusterOptions MasterTabletServiceTest::GetMiniClusterOptions() { return MiniClusterOptions(); }

}  // namespace yb::master
