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


#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/mini_cluster_utils.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_loading_state.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/path_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_util.h"

DECLARE_string(initial_sys_catalog_snapshot_path);
DECLARE_bool(master_join_existing_universe);
DECLARE_bool(quick_leader_election_on_create);

namespace yb::master {

class CatalogManagerWithMiniClusterTest : public integration_tests::YBTableTestBase {
 public:
  size_t num_masters() override {
    return 1;
  }

  size_t num_tablet_servers() override {
    return 1;
  }
};

TEST_F(
    CatalogManagerWithMiniClusterTest, PreventRestoringInitialSnapshotWhenJoinExistingUniverseSet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_sys_catalog_snapshot_path) =
      JoinPathSegments(mini_cluster()->GetMasterFsRoot(0), "snapshot_path");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_join_existing_universe) = true;
  ASSERT_OK(Env::Default()->CreateDir(FLAGS_initial_sys_catalog_snapshot_path));
  SysCatalogLoadingState state;
  auto status = ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster())
                    ->catalog_manager_impl()
                    .VisitSysCatalog(&state);
  ASSERT_TRUE(status.IsIllegalState())
      << "Visiting sys catalog should fail with IllegalState status, instead status was: "
      << status.ToString();
  ASSERT_STR_CONTAINS(
      status.ToString(),
      "Master is joining an existing universe but wants to restore initial sys catalog snapshot");
}

class CatalogManagerFirstElectionTest : public CatalogManagerWithMiniClusterTest {
 public:
  void BeforeStartCluster() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_quick_leader_election_on_create) = true;
    // Hold back the sys catalog's TABLET_PEER_STARTED notification until its first election is won.
    auto* sync_point = SyncPoint::GetInstance();
    sync_point->SetCallBack("TabletPeer::Start:BeforeStartedNotification", [](void* arg) {
      auto* peer = static_cast<tablet::TabletPeer*>(arg);
      if (peer->tablet_id() != kSysCatalogTabletId) {
        return;
      }
      auto consensus = CHECK_RESULT(peer->GetRaftConsensus());
      CHECK_OK(WaitFor(
          [&consensus] { return consensus->role() == PeerRole::LEADER; }, 30s * kTimeMultiplier,
          "Sys catalog leader"));
    });
    sync_point->EnableProcessing();
  }

  void TearDown() override {
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
    CatalogManagerWithMiniClusterTest::TearDown();
  }

 protected:
  StringWaiterLogSink load_log_{"Loading table and tablet metadata into memory for term"};
};

TEST_F(CatalogManagerFirstElectionTest, LoadSysCatalogOncePerTerm) {
  // A second load would be queued behind the first one, which is done once the cluster is up.
  SleepFor(2s * kTimeMultiplier);
  ASSERT_EQ(load_log_.GetEventCount(), 1);
}

}  // namespace yb::master
