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

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "yb/gutil/casts.h"

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"

#include "yb/fs/fs_manager.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/status_log.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBTableCreator;
using yb::client::YBTableName;
using yb::itest::TabletServerMap;
using yb::rpc::RpcController;

DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(log_preallocate_segments);
DECLARE_bool(TEST_log_consider_all_ops_safe);
DECLARE_bool(TEST_enable_remote_bootstrap);
DECLARE_bool(use_preelection);
DECLARE_int32(leader_failure_exp_backoff_max_delta_ms);
DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_int32(TEST_slowdown_master_async_rpc_tasks_by_ms);
DECLARE_int32(unresponsive_ts_rpc_timeout_ms);

DEFINE_NON_RUNTIME_int32(num_test_tablets, 60, "Number of tablets for stress test");

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {

class MasterPartitionedTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MasterPartitionedTest() {}

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = kTimeMultiplier * 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_raft_heartbeat_interval_ms) = kTimeMultiplier * 200;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_unresponsive_ts_rpc_timeout_ms) = 10000;  // 10 sec.

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_failure_exp_backoff_max_delta_ms) = 5000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_master_async_rpc_tasks_by_ms) = 100;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_log_consider_all_ops_safe) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_test_tablets) = RegularBuildVsSanitizers(60, 10);

    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = num_tservers_;
    opts.num_masters = 3;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));
    client_ = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master(0)->bound_rpc_addr_str())
        .add_master_server_addr(cluster_->mini_master(1)->bound_rpc_addr_str())
        .add_master_server_addr(cluster_->mini_master(2)->bound_rpc_addr_str())
        .Build());
  }

  Status BreakMasterConnectivityTo(size_t from_idx, size_t to_idx) {
    master::MiniMaster* src_master = cluster_->mini_master(from_idx);
    IpAddress src = VERIFY_RESULT(HostToAddress(src_master->bound_rpc_addr().host()));
    // TEST_RpcAddress is 1-indexed; we expect from_idx/to_idx to be 0-indexed.
    auto dst_prv = CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kTrue)));
    auto dst_pub =
        CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kFalse)));
    LOG(INFO) << "Breaking connectivities from master " << from_idx << " to " << to_idx << " i.e. "
              << src << " to " << dst_prv << " and " << dst_pub;
    src_master->messenger().BreakConnectivityTo(dst_prv);
    src_master->messenger().BreakConnectivityTo(dst_pub);
    return Status::OK();
  }

  Status RestoreMasterConnectivityTo(size_t from_idx, size_t to_idx) {
    master::MiniMaster* src_master = cluster_->mini_master(from_idx);
    IpAddress src = VERIFY_RESULT(HostToAddress(src_master->bound_rpc_addr().host()));
    // TEST_RpcAddress is 1-indexed; we expect from_idx/to_idx to be 0-indexed.
    auto dst_prv = CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kTrue)));
    auto dst_pub =
        CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kFalse)));
    LOG(INFO) << "Restoring connectivities from master " << from_idx << " to " << to_idx << " i.e. "
              << src << " to " << dst_prv << " and " << dst_pub;
    src_master->messenger().RestoreConnectivityTo(dst_prv);
    src_master->messenger().RestoreConnectivityTo(dst_pub);
    return Status::OK();
  }

  void DoTearDown() override {
    client_.reset();
    SetAtomicFlag(0, &FLAGS_TEST_slowdown_master_async_rpc_tasks_by_ms);
    SleepFor(MonoDelta::FromMilliseconds(1000));
    cluster_->Shutdown();
  }

  void CreateTable(const YBTableName& table_name, int num_tablets);

 protected:
  std::unique_ptr<YBClient> client_;
  int32_t num_tservers_ = 5;
};

void MasterPartitionedTest::CreateTable(const YBTableName& table_name, int num_tablets) {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                table_name.namespace_type()));
  std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(3);
  ASSERT_OK(table_creator->table_name(table_name)
                .table_type(client::YBTableType::REDIS_TABLE_TYPE)
                .num_tablets(num_tablets)
                .wait(false)
                .Create());
}

OpId LastReceivedOpId(master::MiniMaster* master) {
  auto consensus = CHECK_RESULT(master->tablet_peer()->GetConsensus());
  return consensus->GetLastReceivedOpId();
}

TEST_F(MasterPartitionedTest, CauseMasterLeaderStepdownWithTasksInProgress) {
  const auto kTimeout = 60s;

  DontVerifyClusterBeforeNextTearDown();

  auto master_0_is_leader = [this]() {
    auto l = cluster_->GetLeaderMiniMaster();
    return l.ok() && (*l)->permanent_uuid() == cluster_->mini_master(0)->permanent_uuid();
  };

  // Break connectivity so that :
  //   master 0 can make outgoing RPCs to 1 and 2.
  //   but 1 and 2 cannot do Outgoing rpcs.
  // This should result in master 0 becoming the leader.
  //   Network topology:  1 <-- 0 --> 2
  std::vector<std::pair<int, int>> break_connectivity = {{1, 0}, {1, 2}, {2, 1}, {2, 0}};
  bool connectivity_broken = false;

  ASSERT_OK(WaitFor(
      [this, &master_0_is_leader, &break_connectivity, &connectivity_broken]() -> Result<bool> {
    auto leader_mini_master = cluster_->GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      return false;
    }
    if (LastReceivedOpId(*leader_mini_master) != LastReceivedOpId(cluster_->mini_master(0))) {
      if (connectivity_broken) {
        for (const auto& p : break_connectivity) {
          RETURN_NOT_OK(RestoreMasterConnectivityTo(p.first, p.second));
        }
        connectivity_broken = false;
      }
      return false;
    }

    if (!connectivity_broken) {
      for (const auto& p : break_connectivity) {
        RETURN_NOT_OK(BreakMasterConnectivityTo(p.first, p.second));
      }
      connectivity_broken = true;
    }

    LOG(INFO) << "Master 0: " << cluster_->mini_master(0)->permanent_uuid();

    if (master_0_is_leader()) {
      return true;
    }

    return false;
  }, kTimeout, "Master 0 is leader"));

  ASSERT_OK(WaitFor(
      [this]() { return cluster_->WaitForTabletServerCount(num_tservers_).ok(); },
      kTimeout,
      "Wait for master 0 to hear from all tservers"));

  YBTableName table_name(YQL_DATABASE_REDIS, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateTable(table_name, FLAGS_num_test_tablets));
  LOG(INFO) << "Created table successfully!";

  constexpr int kNumLoops = 3;
  for (int i = 0; i < kNumLoops; i++) {
    LOG(INFO) << "Iteration " << i;

    // This test was added during Jepsen/CQL testing before preelections
    // were implemented. Enabling preelections will prevent us from getting
    // into the case that we want to test -- where the master leader has to
    // step down because it sees that another master has moved on to a higher
    // term.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_preelection) = false;

    consensus::ConsensusStatePB cpb;
    ASSERT_OK(cluster_->mini_master(0)->catalog_manager().GetCurrentConfig(&cpb));
    const auto initial_term = cpb.current_term();

    // master-0 cannot send updates to master 2. This will cause master-2
    // to increase its term. And cause the leader (master-0) to step down
    // and re-elect himself
    ASSERT_OK(BreakMasterConnectivityTo(0, 2));
    ASSERT_OK(WaitFor(
        [this, initial_term]() {
          consensus::ConsensusStatePB cpb;
          return cluster_->mini_master(2)->catalog_manager().GetCurrentConfig(&cpb).ok() &&
                 cpb.current_term() > initial_term;
        },
        kTimeout,
        "Wait for master 2 to do elections and increase the term"));

    ASSERT_OK(RestoreMasterConnectivityTo(0, 2));

    ASSERT_OK(cluster_->mini_master(2)->catalog_manager().GetCurrentConfig(&cpb));
    const auto new_term = cpb.current_term();
    ASSERT_OK(WaitFor(
        [this, new_term]() {
          consensus::ConsensusStatePB cpb;
          return cluster_->mini_master(0)->catalog_manager().GetCurrentConfig(&cpb).ok() &&
                 cpb.current_term() > new_term;
        },
        kTimeout,
        "Wait for master 0 to update its term"));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_preelection) = true;

    ASSERT_OK(WaitFor(
        master_0_is_leader, kTimeout, "Wait for master 0 to become the leader again"));
  }
}

TEST_F(MasterPartitionedTest, VerifyOldLeaderStepsDown) {
  // Partition away the old master leader from the cluster.
  auto old_leader_idx = cluster_->LeaderMasterIdx();
  LOG(INFO) << "Old leader master: " << old_leader_idx;

  ssize_t new_cohort_peer1 = -1, new_cohort_peer2 = -1;
  for (size_t i = 0; i < cluster_->num_masters(); i++) {
    if (implicit_cast<ssize_t>(i) == old_leader_idx) {
      continue;
    }
    if (new_cohort_peer1 == -1) {
      new_cohort_peer1 = i;
    } else if (new_cohort_peer2 == -1) {
      new_cohort_peer2 = i;
    }
    LOG(INFO) << "Breaking connectivity between " << i << " and " << old_leader_idx;
    ASSERT_OK(BreakMasterConnectivityTo(old_leader_idx, i));
    ASSERT_OK(BreakMasterConnectivityTo(i, old_leader_idx));
  }

  LOG(INFO) << "Introduced a network split. Cohort#1 masters: " << old_leader_idx
            << ", cohort#2 masters: " << new_cohort_peer1 << ", " << new_cohort_peer2;

  // Wait for a master leader in the new cohort.
  ASSERT_OK(WaitFor(
    [&]() -> Result<bool> {
      // Get the config of the old leader.
      consensus::ConsensusStatePB cbp, cbp1, cbp2;
      RETURN_NOT_OK(
          cluster_->mini_master(old_leader_idx)->catalog_manager().GetCurrentConfig(&cbp));

      // Get the config of the new cluster.
      RETURN_NOT_OK(
          cluster_->mini_master(new_cohort_peer1)->catalog_manager().GetCurrentConfig(&cbp1));

      RETURN_NOT_OK(
          cluster_->mini_master(new_cohort_peer2)->catalog_manager().GetCurrentConfig(&cbp2));

      // Term number of the new cohort's config should increase.
      // Leader should not be the same as the old leader.
      return cbp1.current_term() == cbp2.current_term() &&
             cbp1.current_term() > cbp.current_term() &&
             cbp1.has_leader_uuid() && cbp1.leader_uuid() != cbp.leader_uuid() &&
             cbp2.has_leader_uuid() && cbp2.leader_uuid() == cbp1.leader_uuid();
    },
    100s,
    "Waiting for a leader on the new cohort."
  ));

  // Get the index of the new leader.
  string uuid1 = cluster_->mini_master(new_cohort_peer1)->master()->fs_manager()->uuid();
  string uuid2 = cluster_->mini_master(new_cohort_peer2)->master()->fs_manager()->uuid();

  consensus::ConsensusStatePB cbp1;
  ASSERT_OK(cluster_->mini_master(new_cohort_peer1)
                    ->master()
                    ->catalog_manager()
                    ->GetCurrentConfig(&cbp1));

  ssize_t new_leader_idx = -1;
  if (cbp1.leader_uuid() == uuid1) {
    new_leader_idx = new_cohort_peer1;
  } else if (cbp1.leader_uuid() == uuid2) {
    new_leader_idx = new_cohort_peer2;
  }
  LOG(INFO) << "Leader of the new cohort " << new_leader_idx;

  // Wait for the leader lease to expire on the new master.
  ASSERT_OK(cluster_->mini_master(new_leader_idx)->catalog_manager().WaitUntilCaughtUpAsLeader(
      MonoDelta::FromSeconds(100)));

  // Now perform an RPC that involves a SHARED_LEADER_LOCK and confirm that it fails.
  yb::master::Master* m = cluster_->mini_master(old_leader_idx)->master();
  master::MasterClusterProxy proxy(&m->proxy_cache(), m->rpc_server()->GetRpcHostPort()[0]);

  RpcController controller;
  controller.Reset();
  master::ListTabletServersRequestPB req;
  master::ListTabletServersResponsePB resp;

  ASSERT_OK(proxy.ListTabletServers(req, &resp, &controller));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(resp.error().code(), master::MasterErrorPB::NOT_THE_LEADER);
  ASSERT_EQ(resp.error().status().code(), AppStatusPB::LEADER_HAS_NO_LEASE);

  // Restore connectivity.
  ASSERT_OK(RestoreMasterConnectivityTo(old_leader_idx, new_cohort_peer1));
  ASSERT_OK(RestoreMasterConnectivityTo(old_leader_idx, new_cohort_peer2));
  ASSERT_OK(RestoreMasterConnectivityTo(new_cohort_peer1, old_leader_idx));
  ASSERT_OK(RestoreMasterConnectivityTo(new_cohort_peer2, old_leader_idx));
}

}  // namespace yb
