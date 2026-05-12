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

#include "yb/client/client.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/write_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/cast.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_bool(enable_load_balancing);
DECLARE_bool(TEST_allow_stop_writes);

namespace yb {

class WriteStallConsensusTest : public integration_tests::YBTableTestBase {
 protected:
  void BeforeStartCluster() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_allow_stop_writes) = true;
  }

  bool use_external_mini_cluster() override { return false; }

  size_t num_tablet_servers() override { return 3; }

  int num_tablets() override { return 1; }

  Result<std::string> GetOnlyTabletId() {
    std::vector<std::string> ranges;
    std::vector<TabletId> tablet_ids;
    RETURN_NOT_OK(
        client_->GetTablets(table_.name(), /* max_tablets = */ 0, &tablet_ids, &ranges));
    SCHECK_EQ(tablet_ids.size(), 1U, IllegalState, "Expected exactly one tablet");
    return tablet_ids[0];
  }
};

// Verifies that a write stop on a tablet's RocksDB does not prevent leader elections.
// The early rejection check in RaftConsensus::Update() prevents RPC thread pile-up by
// rejecting UpdateConsensus RPCs with ops before acquiring update_mutex_. Meanwhile
// ShouldApplyWrite() acts as a secondary guard at apply time. See #30728.
TEST_F(WriteStallConsensusTest, LeaderElectionSucceedsDuringWriteStop) {
  auto tablet_id = ASSERT_RESULT(GetOnlyTabletId());

  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
  auto tablet = ASSERT_RESULT(leader_peer->shared_tablet());
  auto* db = tablet->regular_db();
  ASSERT_NE(db, nullptr);

  auto* db_impl = down_cast<rocksdb::DBImpl*>(db);
  auto& write_controller = db_impl->TEST_write_controler();

  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());

  // Tablet::AreWritesStopped() should detect the stop (used by the early check
  // in RaftConsensus::Update()).
  ASSERT_TRUE(tablet->AreWritesStopped());

  // Tablet::ShouldApplyWrite() should also detect the stop (secondary guard in
  // ApplyPendingOperationsUnlocked()).
  ASSERT_FALSE(tablet->ShouldApplyWrite());

  LOG(INFO) << "Write stop injected on leader tablet " << tablet_id
            << ", attempting leader step-down...";

  // Step down the leader. With the early rejection check, UpdateConsensus RPCs carrying
  // ops are rejected before update_mutex_, freeing RPC threads. VoteRequest RPCs are
  // unaffected, so leader election proceeds normally.
  ASSERT_OK(StepDown(leader_peer, /* new_leader_uuid= */ std::string(), ForceStepDown::kTrue));

  ASSERT_OK(WaitUntilTabletHasLeader(
      mini_cluster(), tablet_id,
      CoarseMonoClock::Now() + 30s,
      RequireLeaderIsReady::kTrue));

  LOG(INFO) << "Leader election succeeded while write stop was active.";

  auto new_leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
  LOG(INFO) << "New leader: " << new_leader_peer->permanent_uuid()
            << " (was: " << leader_peer->permanent_uuid() << ")";

  stop_token.reset();
  ASSERT_FALSE(write_controller.IsStopped());
}

// Verifies that writes to other replicas remain possible when one replica is stalled.
// After leader step-down, the new leader (on a non-stalled tserver) accepts writes.
TEST_F(WriteStallConsensusTest, OtherTabletsRemainWritableDuringWriteStop) {
  auto tablet_id = ASSERT_RESULT(GetOnlyTabletId());

  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
  auto tablet = ASSERT_RESULT(leader_peer->shared_tablet());
  auto* db_impl = down_cast<rocksdb::DBImpl*>(tablet->regular_db());
  auto& write_controller = db_impl->TEST_write_controler();

  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());
  ASSERT_TRUE(tablet->AreWritesStopped());

  LOG(INFO) << "Write stop injected. Verifying writes still work via non-stalled replicas...";

  ASSERT_OK(StepDown(leader_peer, /* new_leader_uuid= */ std::string(), ForceStepDown::kTrue));
  ASSERT_OK(WaitUntilTabletHasLeader(
      mini_cluster(), tablet_id,
      CoarseMonoClock::Now() + 30s,
      RequireLeaderIsReady::kTrue));

  PutKeyValue(/* key= */ "test_key_during_stall", /* value= */ "test_value");

  auto result = GetScanResults(client::TableRange(table_));
  bool found = false;
  for (const auto& kv : result) {
    if (kv.first == "test_key_during_stall") {
      ASSERT_EQ(kv.second, "test_value");
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found) << "Write during stall was not found in scan results";

  stop_token.reset();
}

}  // namespace yb
