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

#include <algorithm>

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/test_async_rpc_manager.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/status_callback.h"
#include "yb/util/test_macros.h"
#include "yb/util/unique_lock.h"

using namespace std::chrono_literals;

DECLARE_int32(retrying_ts_rpc_max_delay_ms);
DECLARE_int32(retrying_rpc_max_jitter_ms);

namespace yb {

class MasterTasksTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MasterTasksTest() {}

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.num_masters = 3;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));
  }
};

// Test that retrying master and tserver rpc tasks retry properly and that the delay before retrying
// is capped by FLAGS_retrying_ts_rpc_max_delay_ms + up to 50ms random jitter per retry.
TEST_F(MasterTasksTest, RetryingMasterRpcTaskMaxDelay) {
  constexpr auto kNumRetries = 10;

  auto* leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  std::vector<consensus::RaftPeerPB> master_peers;
  ASSERT_OK(leader_master->master()->ListRaftConfigMasters(&master_peers));

  // Send the RPC to a non-leader master.
  auto non_leader_master = std::find_if(
      master_peers.begin(), master_peers.end(),
      [&](auto& peer) { return peer.permanent_uuid() != leader_master->permanent_uuid(); });

  std::promise<Status> promise;
  std::future<Status> future = promise.get_future();
  ASSERT_OK(leader_master->master()->test_async_rpc_manager()->SendMasterTestRetryRequest(
    *non_leader_master, kNumRetries, [&promise](const Status& s) {
      LOG(INFO) << "Done: " << s;
      promise.set_value(s);
  }));

  LOG(INFO) << "Task scheduled";

  auto status = future.wait_for(
      (FLAGS_retrying_ts_rpc_max_delay_ms + FLAGS_retrying_rpc_max_jitter_ms) *
      kNumRetries * RegularBuildVsSanitizers(1.1, 1.2) * 1ms);
  ASSERT_EQ(status, std::future_status::ready);
  ASSERT_OK(future.get());
}

TEST_F(MasterTasksTest, RetryingTSRpcTaskMaxDelay) {
  constexpr auto kNumRetries = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_retrying_ts_rpc_max_delay_ms) = 100;

  auto* ts = cluster_->mini_tablet_server(0);

  auto* leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());

  std::promise<Status> promise;
  std::future<Status> future = promise.get_future();
  ASSERT_OK(leader_master->master()->test_async_rpc_manager()->SendTsTestRetryRequest(
    ts->server()->permanent_uuid(), kNumRetries, [&promise](const Status& s) {
      LOG(INFO) << "Done: " << s;
      promise.set_value(s);
  }));

  LOG(INFO) << "Task scheduled";

  auto status = future.wait_for(
      (FLAGS_retrying_ts_rpc_max_delay_ms + FLAGS_retrying_rpc_max_jitter_ms) *
      kNumRetries * RegularBuildVsSanitizers(1.1, 1.2) * 1ms);
  ASSERT_EQ(status, std::future_status::ready);
  ASSERT_OK(future.get());
}

}  // namespace yb
