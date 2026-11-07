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

// MiniCluster with the Rust master embedded in the test process (D12): what lets in-process tests
// run against Rust masters with YB_RUST_MASTER.

#include <memory>

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/mini_master.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

class RustMasterMiniClusterTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
#ifndef YB_WITH_RUST_MASTER
    GTEST_SKIP() << "This build has no Rust master (--skip-rust)";
#endif
    MiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 3;
    opts.rust_masters = master::RustMasters::kAll;
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
  }

  void TearDown() override {
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  std::unique_ptr<MiniCluster> cluster_;
};

// Three Rust masters in one process elect a leader that every tserver registers with; the
// leadership moves on a step-down, and a restarted master rejoins.
TEST_F(RustMasterMiniClusterTest, LeadsFailsOverAndRestarts) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_TRUE(leader->is_rust());
  ASSERT_EQ(leader->master(), nullptr);
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  const auto old_leader_uuid = leader->permanent_uuid();

  ASSERT_OK(cluster_->StepDownMasterLeader());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto new_leader = cluster_->GetLeaderMiniMaster();
        return new_leader.ok() && (*new_leader)->permanent_uuid() != old_leader_uuid;
      },
      60s, "a new Rust leader"));
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));

  // A follower restarts on its own data and catches up.
  auto* new_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    auto* master = cluster_->mini_master(i);
    if (master != new_leader) {
      ASSERT_OK(master->Restart());
      break;
    }
  }
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())
                ->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
}

}  // namespace yb
