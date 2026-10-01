//
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
//

#include "yb/client/async_rpc.h"
#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_rpc.h"
#include "yb/client/yb_op.h"

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_client.pb.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/sidecars.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tserver/tserver_service.messages.h"

#include "yb/util/memory/arena.h"
#include "yb/util/test_util.h"
#include "yb/util/trace.h"

using namespace std::literals;

namespace yb {
namespace client {
namespace internal {

const TabletId kTestTablet = "kTestTablet";

class TabletRpcTest : public YBTest {
 public:
  TabletRpcTest() {
    cloud_info_.set_placement_cloud("cloud1");
    cloud_info_.set_placement_region("datacenter1");
    cloud_info_.set_placement_zone("rack1");
  }

  void FillTsInfo(
      const std::string& uuid, const std::string& host, const std::string& addr,
      master::TSInfoPB* ts_info) {
    ts_info->set_permanent_uuid(uuid);
    ts_info->mutable_cloud_info()->CopyFrom(cloud_info_);
    ts_info->set_placement_uuid("");
    auto* rpc_addr = ts_info->add_private_rpc_addresses();
    rpc_addr->set_host(addr);
    rpc_addr->set_port(9100);
  }

  Status InitReadRpcTest(bool known_leader = true) {
    client_ = VERIFY_RESULT(YBClientBuilder()
        .add_master_server_addr("192.0.2.1:7100")
        .set_skip_master_leader_resolution(true)
        .set_cloud_info_pb(cloud_info_)
        .set_tserver_uuid("leader")
        .Build());
    auto clock = make_scoped_refptr(new server::HybridClock());
    RETURN_NOT_OK(clock->Init());
    arena_ = SharedThreadSafeArena();
    session_ = std::make_shared<YBSession>(client_.get(), 60s, clock, arena_);
    session_->SetReadPoint(snapshot_);
    batcher_ = std::make_shared<Batcher>(
        client_.get(), session_, nullptr, session_->read_point(), false, OpId::kUnknownTerm,
        arena_);
    batcher_->SetDeadline(CoarseMonoClock::Now() + 60s);

    YBTableInfo info;
    info.table_id = "auth-catalog";
    info.table_name = YBTableName(YQL_DATABASE_PGSQL, "yugabyte", "pg_authid");
    info.table_type = YBTableType::PGSQL_TABLE_TYPE;
    info.colocated = false;
    info.schema = YBSchema(Schema());
    table_ = std::make_shared<YBTable>(
        info, std::make_shared<VersionedTablePartitionList>(
                  VersionedTablePartitionList{.keys = {""}, .version = 0}));

    master::TabletLocationsPB locations;
    for (const auto& uuid : {"leader", "follower-1", "follower-2"}) {
      auto* replica = locations.add_replicas();
      FillTsInfo(uuid, uuid, "192.0.2.1", replica->mutable_ts_info());
      replica->set_role(
          known_leader && uuid == std::string("leader") ? PeerRole::LEADER : PeerRole::FOLLOWER);
      replica->set_member_type(consensus::PeerMemberType::VOTER);
      ts_map_.emplace(uuid, std::make_unique<RemoteTabletServer>(replica->ts_info()));
    }
    remote_tablet_ = new RemoteTablet(
        kTestTablet, dockv::Partition(), 0, 0, "", RemoteTablet::kUnknownOpIdIndex);
    remote_tablet_->Refresh(ts_map_, locations.replicas());
    return Status::OK();
  }

  YBPgsqlReadOpPtr AddReadOp(bool marked = false) {
    auto op = YBPgsqlReadOp::NewSelect(table_, arena_, &sidecars_);
    op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
    if (marked) {
      op->set_ysql_auth_catalog_read(true);
    }
    ops_.emplace_back(op, ops_.size());
    return op;
  }

  AsyncRpcData MakeRpcData() {
    return {
        .batcher = batcher_,
        .tablet = remote_tablet_.get(),
        .need_consistent_read = false,
        .arena = arena_,
        .ops = InFlightOps(ops_.begin(), ops_.end())};
  }

  std::unique_ptr<ReadRpc> MakeReadRpc(
      YBConsistencyLevel consistency = YBConsistencyLevel::CONSISTENT_PREFIX) {
    return std::make_unique<ReadRpc>(MakeRpcData(), consistency);
  }

  const Status& InitializationStatus(const ReadRpc& rpc) {
    return rpc.initialization_status_;
  }

  const tserver::LWReadRequestPB& Request(const ReadRpc& rpc) {
    return rpc.req_;
  }

  TabletInvoker& Invoker(ReadRpc& rpc) {
    return rpc.tablet_invoker_;
  }

  Status SwapResponses(ReadRpc* rpc) {
    return rpc->SwapResponses({});
  }

  const RemoteTabletServer* Select(ReadRpc* rpc, bool leader_only = false) {
    auto& invoker = Invoker(*rpc);
    if (leader_only) {
      invoker.SelectTabletServer();
    } else {
      invoker.SelectTabletServerWithConsistentPrefix();
    }
    return invoker.current_ts_;
  }

  MonoDelta PrepareRead(ReadRpc* rpc) {
    return rpc->PrepareReadController()->timeout();
  }

  MonoDelta FollowerTimeout() {
    return MonoDelta::FromSeconds(2);
  }

  void CheckSnapshot(const ReadRpc& rpc) {
    tserver::ReadRequestPB wire_request;
    ASSERT_TRUE(wire_request.ParseFromString(Request(rpc).SerializeAsString()));
    ASSERT_TRUE(wire_request.ysql_auth_catalog_read());
    ASSERT_TRUE(wire_request.has_read_time());
    ASSERT_EQ(ReadHybridTime::FromPB(wire_request.read_time()), snapshot_);
  }

 protected:
  const ReadHybridTime snapshot_ = ReadHybridTime::FromMicros(1000000);
  CloudInfoPB cloud_info_;
  std::unique_ptr<YBClient> client_;
  ThreadSafeArenaPtr arena_;
  YBSessionPtr session_;
  BatcherPtr batcher_;
  YBTablePtr table_;
  TabletServerMap ts_map_;
  RemoteTabletPtr remote_tablet_;
  rpc::Sidecars sidecars_;
  std::vector<InFlightOp> ops_;
};

TEST_F(TabletRpcTest, TabletInvokerSelectTabletServerRace) {

  master::TabletLocationsPB tablet_locations;
  tablet_locations.set_tablet_id(kTestTablet);
  tablet_locations.set_stale(false);

  master::TabletLocationsPB_ReplicaPB replica1;
  FillTsInfo("n1-uuid", "n1", "127.0.0.1", replica1.mutable_ts_info());

  master::TabletLocationsPB_ReplicaPB replica2;
  FillTsInfo("n2-uuid", "n2", "127.0.0.2", replica2.mutable_ts_info());

  TabletServerMap ts_map;

  for (auto* replica : {&replica1, &replica2}) {
    replica->set_role(PeerRole::FOLLOWER);
    replica->set_member_type(consensus::PeerMemberType::VOTER);

    const auto& uuid = replica->ts_info().permanent_uuid();
    ts_map.emplace(uuid, std::make_unique<RemoteTabletServer>(uuid, nullptr, nullptr));
  }

  dockv::Partition partition;
  dockv::Partition::FromPB(tablet_locations.partition(), &partition);
  internal::RemoteTabletPtr remote_tablet = new internal::RemoteTablet(
      tablet_locations.tablet_id(), partition, /* partition_list_version = */ 0,
      /* split_depth = */ 0, /* split_parent_id = */ "", RemoteTablet::kUnknownOpIdIndex);

  std::atomic<bool> stop_requested{false};
  std::thread replicas_refresher(
      [&stop_requested, &remote_tablet, &ts_map, &tablet_locations, &replica1, &replica2]{
    bool two_replicas = false;
    while (!stop_requested) {
      tablet_locations.clear_replicas();
      if (two_replicas) {
        tablet_locations.add_replicas()->CopyFrom(replica1);
      }
      tablet_locations.add_replicas()->CopyFrom(replica2);
      remote_tablet->Refresh(ts_map, tablet_locations.replicas());
      two_replicas = !two_replicas;
    }
  });

  scoped_refptr<Trace> trace(new Trace());

  for (int iter = 0; iter < 200; ++iter) {
    internal::TabletInvoker invoker(false /* local_tserver_only */,
                                    false /* consistent_prefix */,
                                    nullptr /* client */,
                                    nullptr /* command */,
                                    nullptr /* rpc */,
                                    remote_tablet.get(),
                                    /* table =*/ nullptr,
                                    nullptr /* retrier */,
                                    trace.get());
    invoker.SelectTabletServer();
  }

  stop_requested = true;
  replicas_refresher.join();
}

TEST_F(TabletRpcTest, AuthReadsPreferFollowersAndPreserveSnapshotOnRetry) {
  ASSERT_OK(InitReadRpcTest());
  AddReadOp(true);
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_TRUE(rpc->PreferFollower());
  const auto serialized_time = Request(*rpc).read_time().SerializeAsString();

  std::unordered_set<std::string> selected_followers;
  for (size_t i = 0; i != 100; ++i) {
    const auto* selected = Select(rpc.get());
    ASSERT_NE(selected, nullptr);
    ASSERT_NE(selected->permanent_uuid(), "leader");
    ASSERT_FALSE(Invoker(*rpc).is_leader_selection());
    selected_followers.insert(selected->permanent_uuid());
    ASSERT_EQ(PrepareRead(rpc.get()), FollowerTimeout());
    ASSERT_EQ(Request(*rpc).consistency_level(), YBConsistencyLevel::CONSISTENT_PREFIX);
  }
  ASSERT_EQ(selected_followers.size(), 2);
  CheckSnapshot(*rpc);

  // Retrying must use the frozen envelope, not a new clock or session read point.
  session_->SetReadPoint(ReadHybridTime::SingleTime(snapshot_.read.Incremented()));
  ASSERT_EQ(Select(rpc.get(), true)->permanent_uuid(), "leader");
  ASSERT_GT(PrepareRead(rpc.get()), FollowerTimeout());
  ASSERT_EQ(Request(*rpc).consistency_level(), YBConsistencyLevel::STRONG);
  ASSERT_EQ(Request(*rpc).read_time().SerializeAsString(), serialized_time);
  CheckSnapshot(*rpc);
}

TEST_F(TabletRpcTest, AuthReadsUseStrongWhenNoFollowersAreHealthy) {
  ASSERT_OK(InitReadRpcTest());
  AddReadOp(true);
  for (const auto& uuid : {"follower-1", "follower-2"}) {
    ASSERT_TRUE(remote_tablet_->MarkReplicaFailed(
        ts_map_.at(uuid).get(), STATUS(NetworkError, "Test unavailable follower")));
  }
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_EQ(Select(rpc.get())->permanent_uuid(), "leader");
  ASSERT_TRUE(Invoker(*rpc).is_leader_selection());
  ASSERT_EQ(rpc->num_attempts(), 1);
  ASSERT_GT(PrepareRead(rpc.get()), FollowerTimeout());
  ASSERT_EQ(Request(*rpc).consistency_level(), YBConsistencyLevel::STRONG);
  CheckSnapshot(*rpc);
}

TEST_F(TabletRpcTest, AuthReadsUseStrongWhenDiscoveringLeader) {
  ASSERT_OK(InitReadRpcTest(false));
  AddReadOp(true);
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_NE(Select(rpc.get()), nullptr);
  ASSERT_TRUE(Invoker(*rpc).is_leader_selection());
  PrepareRead(rpc.get());
  ASSERT_EQ(Request(*rpc).consistency_level(), YBConsistencyLevel::STRONG);
  CheckSnapshot(*rpc);
}

TEST_F(TabletRpcTest, OrdinaryReadRoutingIsUnchanged) {
  ASSERT_OK(InitReadRpcTest());
  auto op = AddReadOp();
  ASSERT_FALSE(op->ysql_auth_catalog_read());
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_FALSE(rpc->PreferFollower());
  ASSERT_FALSE(Request(*rpc).has_ysql_auth_catalog_read());
  ASSERT_FALSE(Request(*rpc).has_read_time());
  ASSERT_EQ(Select(rpc.get())->permanent_uuid(), "leader");
  ASSERT_GT(PrepareRead(rpc.get()), FollowerTimeout());
  ASSERT_EQ(Request(*rpc).consistency_level(), YBConsistencyLevel::CONSISTENT_PREFIX);

  auto leader_rpc = MakeReadRpc(YBConsistencyLevel::STRONG);
  ASSERT_OK(InitializationStatus(*leader_rpc));
  ASSERT_EQ(Select(leader_rpc.get(), true)->permanent_uuid(), "leader");
  PrepareRead(leader_rpc.get());
  ASSERT_FALSE(Request(*leader_rpc).has_ysql_auth_catalog_read());
  ASSERT_EQ(Request(*leader_rpc).consistency_level(), YBConsistencyLevel::STRONG);
}

TEST_F(TabletRpcTest, AuthReadsRejectMixedBatches) {
  ASSERT_OK(InitReadRpcTest());
  auto first = AddReadOp();
  auto second = AddReadOp();
  for (bool mark_first : {false, true}) {
    first->set_ysql_auth_catalog_read(mark_first);
    second->set_ysql_auth_catalog_read(!mark_first);
    auto rpc = MakeReadRpc();
    ASSERT_TRUE(InitializationStatus(*rpc).IsInvalidArgument());
    ASSERT_STR_CONTAINS(InitializationStatus(*rpc).ToString(), "Cannot mix");
  }
}

TEST_F(TabletRpcTest, AuthReadsRequireSingleExplicitSnapshot) {
  ASSERT_OK(InitReadRpcTest());
  AddReadOp(true);
  for (auto time :
       {HybridTime::kInvalid, HybridTime::kMin, HybridTime::kInitial, HybridTime::kMax}) {
    session_->SetReadPoint(ReadHybridTime::SingleTime(time));
    auto rpc = MakeReadRpc();
    ASSERT_TRUE(InitializationStatus(*rpc).IsInvalidArgument());
  }

  auto uncertain_time = snapshot_;
  uncertain_time.global_limit = uncertain_time.read.Incremented();
  session_->SetReadPoint(uncertain_time);
  ASSERT_TRUE(InitializationStatus(*MakeReadRpc()).IsInvalidArgument());

  session_->SetReadPoint(snapshot_);
  ASSERT_TRUE(InitializationStatus(*MakeReadRpc(YBConsistencyLevel::STRONG)).IsInvalidArgument());
  auto data = MakeRpcData();
  data.read_at_in_txn_limit = true;
  ReadRpc rpc(data, YBConsistencyLevel::CONSISTENT_PREFIX);
  ASSERT_TRUE(InitializationStatus(rpc).IsInvalidArgument());
}

TEST_F(TabletRpcTest, AuthFollowerTimeoutReservesFallbackBudget) {
  ASSERT_OK(InitReadRpcTest());
  AddReadOp(true);
  batcher_->SetDeadline(CoarseMonoClock::Now() + 500ms);
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_NE(Select(rpc.get())->permanent_uuid(), "leader");
  ASSERT_LE(PrepareRead(rpc.get()), MonoDelta::FromMilliseconds(250));
  CheckSnapshot(*rpc);
}

TEST_F(TabletRpcTest, AuthReadsExcludeDiscoveredLeader) {
  ASSERT_OK(InitReadRpcTest());
  AddReadOp(true);
  ASSERT_TRUE(remote_tablet_->MarkTServerAsLeader(ts_map_.at("follower-1").get()));
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_EQ(Select(rpc.get())->permanent_uuid(), "leader");
  ASSERT_FALSE(Invoker(*rpc).is_leader_selection());
  PrepareRead(rpc.get());
  ASSERT_EQ(Request(*rpc).consistency_level(), YBConsistencyLevel::CONSISTENT_PREFIX);
  CheckSnapshot(*rpc);
}

TEST_F(TabletRpcTest, AuthReadsFailOnSnapshotTooOldAfterPartialResponse) {
  ASSERT_OK(InitReadRpcTest());
  auto op = AddReadOp(true);
  auto rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*rpc));
  ASSERT_NE(Select(rpc.get())->permanent_uuid(), "leader");
  PrepareRead(rpc.get());
  const auto serialized_time = Request(*rpc).read_time().SerializeAsString();

  auto* page = rpc->resp().add_pgsql_batch();
  page->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  page->mutable_paging_state()->dup_next_row_key("next-row");
  page->mutable_paging_state()->set_total_num_rows_read(1);
  snapshot_.ToPB(page->mutable_paging_state()->mutable_read_time());
  ASSERT_OK(SwapResponses(rpc.get()));
  ASSERT_EQ(op->response().status(), PgsqlResponsePB::PGSQL_STATUS_OK);
  ASSERT_TRUE(op->response().has_paging_state());
  *op->mutable_request()->mutable_paging_state() = op->response().paging_state();

  auto next_rpc = MakeReadRpc();
  ASSERT_OK(InitializationStatus(*next_rpc));
  ASSERT_NE(Select(next_rpc.get())->permanent_uuid(), "leader");
  PrepareRead(next_rpc.get());
  auto* error = next_rpc->resp().mutable_error();
  error->set_code(tserver::TabletServerErrorPB::UNKNOWN_ERROR);
  StatusToPB(
      STATUS(SnapshotTooOld, "Test expired authentication catalog snapshot"),
      error->mutable_status());

  Status status;
  ASSERT_TRUE(Invoker(*next_rpc).Done(&status));
  ASSERT_TRUE(status.IsSnapshotTooOld());
  ASSERT_EQ(op->response().status(), PgsqlResponsePB::PGSQL_STATUS_RUNTIME_ERROR);
  ASSERT_EQ(op->response().error_status().size(), 1);
  ASSERT_TRUE(StatusFromPB(op->response().error_status().front()).IsSnapshotTooOld());
  ASSERT_EQ(next_rpc->num_attempts(), 1);
  ASSERT_FALSE(session_->read_point()->IsRestartRequired());
  ASSERT_EQ(session_->read_point()->GetReadTime(kTestTablet), snapshot_);
  ASSERT_EQ(Request(*next_rpc).read_time().SerializeAsString(), serialized_time);
  CheckSnapshot(*next_rpc);
}

} // namespace internal
} // namespace client
} // namespace yb
