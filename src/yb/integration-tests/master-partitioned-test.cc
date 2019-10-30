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
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table_creator.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/fs/fs_manager.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master-test-util.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/shared_lock.h"

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBColumnSchema;
using yb::client::YBSchema;
using yb::client::YBSchemaBuilder;
using yb::client::YBTableCreator;
using yb::client::YBTableName;
using yb::itest::CreateTabletServerMap;
using yb::itest::TabletServerMap;
using yb::master::MasterServiceProxy;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;

DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(log_preallocate_segments);
DECLARE_bool(log_consider_all_ops_safe);
DECLARE_bool(enable_remote_bootstrap);
DECLARE_int32(leader_failure_exp_backoff_max_delta_ms);
DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_int32(slowdown_master_async_rpc_tasks_by_ms);
DECLARE_int32(unresponsive_ts_rpc_timeout_ms);
DECLARE_string(vmodule);

DEFINE_int32(num_test_tablets, 60, "Number of tablets for stress test");

using std::string;
using std::vector;
using std::thread;
using std::unique_ptr;
using strings::Substitute;

namespace yb {

class MasterPartitionedTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MasterPartitionedTest() {}

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    FLAGS_heartbeat_interval_ms = 10;
    FLAGS_raft_heartbeat_interval_ms = 200;
    FLAGS_unresponsive_ts_rpc_timeout_ms = 10000;  // 10 sec.

    FLAGS_leader_failure_exp_backoff_max_delta_ms = 5000;
    FLAGS_slowdown_master_async_rpc_tasks_by_ms = 100;
    FLAGS_vmodule = "catalog_manager=2,async_rpc_tasks=2";

    FLAGS_log_consider_all_ops_safe = true;

    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = num_tservers_;
    opts.num_masters = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));
    client_ = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master(0)->bound_rpc_addr_str())
        .add_master_server_addr(cluster_->mini_master(1)->bound_rpc_addr_str())
        .add_master_server_addr(cluster_->mini_master(2)->bound_rpc_addr_str())
        .Build());
  }

  Status BreakMasterConnectivityTo(int from_idx, int to_idx) {
    master::MiniMaster* src_master = cluster_->mini_master(from_idx);
    IpAddress src = VERIFY_RESULT(HostToAddress(src_master->bound_rpc_addr().host()));
    // TEST_RpcAddress is 1-indexed; we expect from_idx/to_idx to be 0-indexed.
    auto dst_prv = CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kTrue)));
    auto dst_pub =
        CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kFalse)));
    LOG(INFO) << "Breaking connectivities from master " << from_idx << " to " << to_idx << " i.e. "
              << src << " to " << dst_prv << " and " << dst_pub;
    src_master->master()->messenger()->BreakConnectivityTo(dst_prv);
    src_master->master()->messenger()->BreakConnectivityTo(dst_pub);
    return Status::OK();
  }

  Status RestoreMasterConnectivityTo(int from_idx, int to_idx) {
    master::MiniMaster* src_master = cluster_->mini_master(from_idx);
    IpAddress src = VERIFY_RESULT(HostToAddress(src_master->bound_rpc_addr().host()));
    // TEST_RpcAddress is 1-indexed; we expect from_idx/to_idx to be 0-indexed.
    auto dst_prv = CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kTrue)));
    auto dst_pub =
        CHECK_RESULT(HostToAddress(TEST_RpcAddress(to_idx + 1, server::Private::kFalse)));
    LOG(INFO) << "Restoring connectivities from master " << from_idx << " to " << to_idx << " i.e. "
              << src << " to " << dst_prv << " and " << dst_pub;
    src_master->master()->messenger()->RestoreConnectivityTo(dst_prv);
    src_master->master()->messenger()->RestoreConnectivityTo(dst_pub);
    return Status::OK();
  }

  void DoTearDown() override {
    client_.reset();
    FLAGS_slowdown_master_async_rpc_tasks_by_ms = 0;
    SleepFor(MonoDelta::FromMilliseconds(1000));
    cluster_->Shutdown();
  }

  void CreateTable(const YBTableName& table_name, int num_tablets);

  void CheckLeaderMasterIsResponsive(int master_idx);

 protected:
  std::unique_ptr<YBClient> client_;
  int32_t num_tservers_ = 5;
};

void MasterPartitionedTest::CheckLeaderMasterIsResponsive(int master_idx) {
  master::MiniMaster* master = cluster_->mini_master(master_idx);
  auto role = master->master()->catalog_manager()->Role();
  if (role != consensus::RaftPeerPB::LEADER) {
    LOG(ERROR) << "Master " << master_idx << " is not the leader. It is " << yb::ToString(role);
    return;
  }
  // cluster_->leader_mini_master() will retry and wait until the the leader
  // is ready to serve.
  master::MiniMaster* leader_master = cluster_->leader_mini_master();
  if (!leader_master) {
    // We may be in an election storm. So if we are at least making progress wrt the
    // error messages that we get (which contains the term/ready_term) we will not
    // consider it as a failure.
    Status leader_status_before, leader_status_after;
    {
      master::CatalogManager::ScopedLeaderSharedLock l(master->master()->catalog_manager());
      if (!l.catalog_status().ok()) {
        LOG(INFO) << "Catalog status is not ok. " << l.catalog_status();
        return;
      }
      leader_status_before = l.leader_status();
    }
    if (leader_status_before.ok()) {
      return;
    }
    SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_leader_failure_exp_backoff_max_delta_ms));
    {
      master::CatalogManager::ScopedLeaderSharedLock l(master->master()->catalog_manager());
      if (!l.catalog_status().ok()) {
        LOG(INFO) << "Catalog status is not ok. " << l.catalog_status();
        return;
      }
      leader_status_after = l.leader_status();
    }
    if (leader_status_after.ok()) {
      return;
    }

    LOG(INFO) << "Master leader is not ready. Looking for some progress "
              << " in " << yb::ToString(2 * FLAGS_leader_failure_exp_backoff_max_delta_ms) << " ms"
              << "\nleader status before sleep " << leader_status_before
              << "\nleader status  after sleep " << leader_status_after;
    CHECK(leader_status_before.ToString(false) != leader_status_after.ToString(false))
        << "Master leader is not ready. And not making progress even after "
        << yb::ToString(2 * FLAGS_leader_failure_exp_backoff_max_delta_ms) << " ms"
        << "\n leader status before sleep " << leader_status_before
        << "\n leader status  after sleep " << leader_status_after;
  }
}

void MasterPartitionedTest::CreateTable(const YBTableName& table_name, int num_tablets) {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                YQLDatabase::YQL_DATABASE_REDIS));
  gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(3);
  ASSERT_OK(table_creator->table_name(table_name)
                .table_type(client::YBTableType::REDIS_TABLE_TYPE)
                .num_tablets(num_tablets)
                .wait(false)
                .Create());
}

TEST_F(MasterPartitionedTest, CauseMasterLeaderStepdownWithTasksInProgress) {
  DontVerifyClusterBeforeNextTearDown();

  // Break connectivity so that :
  //   master 0 can make outgoing RPCs to 1 and 2.
  //   but 1 and 2 cannot do Outgoing rpcs.
  // This should result in master 0 becoming the leader.
  BreakMasterConnectivityTo(1, 0);
  BreakMasterConnectivityTo(1, 2);
  BreakMasterConnectivityTo(2, 1);
  BreakMasterConnectivityTo(2, 0);

  // Allow some time for master 0 to become the leader
  SleepFor(MonoDelta::FromMilliseconds(4000));
  ASSERT_OK(cluster_->WaitForTabletServerCount(num_tservers_));

  YBTableName table_name("my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateTable(table_name, FLAGS_num_test_tablets));
  LOG(INFO) << "Created table successfully!";

  for (int i = 0; i < 10; i++) {
    LOG(INFO) << "iteration " << i;
    // master-0 cannot send updates to master 2. This will cause master-2
    // to increase its term. And cause the leader (master-0) to step down
    // and re-elect himself
    BreakMasterConnectivityTo(0, 2);

    SleepFor(MonoDelta::FromMilliseconds(4000));
    CheckLeaderMasterIsResponsive(0);

    RestoreMasterConnectivityTo(0, 2);
    // Give some time for the master to realize the higher term from master-2.
    SleepFor(MonoDelta::FromMilliseconds(1000));
    CheckLeaderMasterIsResponsive(0);
  }
}

}  // namespace yb
