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

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/unique_lock.h"

using namespace std::chrono_literals;

DECLARE_int32(retrying_ts_rpc_max_delay_ms);

namespace yb {

class MasterTasksTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MasterTasksTest() {}

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));
  }
};

// Make sure delay before retrying RetryingTSRpcTask is capped by
// FLAGS_retrying_ts_rpc_max_delay_ms + up to 50ms random jitter.
TEST_F(MasterTasksTest, RetryingTSRpcTaskMaxDelay) {
  constexpr auto kNumRetries = 10;
  constexpr auto kMaxJitterMs = 50;

  FLAGS_retrying_ts_rpc_max_delay_ms = 100;

  auto* ts = cluster_->mini_tablet_server(0);

  struct SharedData {
    CountDownLatch done = CountDownLatch(1);
    std::shared_mutex mutex;
    Status status GUARDED_BY(mutex);
  };

  auto shared_data = std::make_shared<SharedData>();

  ASSERT_OK(cluster_->mini_master()->catalog_manager_impl().TEST_SendTestRetryRequest(
      ts->server()->permanent_uuid(), kNumRetries, [shared_data](const Status& s) {
        LOG(INFO) << "Done: " << s;
        {
          UniqueLock<std::shared_mutex> lock(shared_data->mutex);
          shared_data->status = s;
        }
        shared_data->done.CountDown();
      }));

  LOG(INFO) << "Task scheduled";

  ASSERT_TRUE(shared_data->done.WaitFor(MonoDelta::FromMilliseconds(
      (FLAGS_retrying_ts_rpc_max_delay_ms + kMaxJitterMs) * kNumRetries *
      RegularBuildVsSanitizers(1.1, 1.2))));

  {
    SharedLock<std::shared_mutex> lock(shared_data->mutex);
    ASSERT_OK(shared_data->status);
  }
}

}  // namespace yb
