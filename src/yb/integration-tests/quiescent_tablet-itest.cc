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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"

#include "yb/consensus/log.h"

#include "yb/fs/fs_manager.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/sync_point.h"

DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_quiescent_tablets);
DECLARE_bool(remote_bootstrap_from_leader_only);
DECLARE_bool(TEST_assert_remote_bootstrap_happens_from_same_zone);
DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(TEST_simulate_abrupt_server_restart);
DECLARE_int32(quiescent_tablet_idle_sec);
DECLARE_int32(replication_factor);

namespace yb::integration_tests {

using namespace std::literals;

class QuiescentTabletTest : public YBTableTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_quiescent_tablets) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_quiescent_tablet_idle_sec) = 1;
    YBTableTestBase::SetUp();
  }

  void TearDown() override {
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
    YBTableTestBase::TearDown();
  }

  bool enable_ysql() override {
    return false;
  }

  int num_tablets() override {
    return 2;
  }

  void WriteRows(int begin, int end) {
    for (auto i = begin; i != end; ++i) {
      PutKeyValue(Format("key_$0", i), Format("value_$0", i));
    }
  }

  void CheckRows(size_t num_rows) {
    auto kvs = GetScanResults(client::TableRange(table_));
    ASSERT_EQ(kvs.size(), num_rows);
  }

  std::vector<tablet::TabletPeerPtr> TablePeers() {
    return ListTableActiveTabletPeers(mini_cluster(), table_->id());
  }

  Status WaitAllQuiescent(std::optional<size_t> expected_num_tablets = std::nullopt) {
    const auto num_peers = expected_num_tablets.value_or(num_tablets()) * num_tablet_servers();
    return WaitFor([this, num_peers]() -> Result<bool> {
      auto peers = TablePeers();
      if (peers.size() != num_peers) {
        return false;
      }
      for (const auto& peer : peers) {
        if (!peer->IsQuiescent()) {
          return false;
        }
      }
      return true;
    }, 30s * kTimeMultiplier, "All tablets quiescent");
  }

  // Checks that the WAL dir of each table peer has a marker and no segments. Returns the markers.
  Result<std::unordered_map<std::string, OpId>> CheckWalDirs() {
    std::unordered_map<std::string, OpId> result;
    for (const auto& peer : TablePeers()) {
      auto* log = peer->log();
      auto* env = peer->tablet_metadata()->fs_manager()->env();
      auto children = VERIFY_RESULT(env->GetChildren(log->wal_dir(), ExcludeDots::kTrue));
      for (const auto& child : children) {
        SCHECK(
            !log::IsLogFileName(child), IllegalState,
            Format("$0: segment $1 in quiescent log", peer->LogPrefix(), child));
      }
      SCHECK(
          env->FileExists(log::Log::QuiescentMarkerPath(log->wal_dir())), IllegalState,
          Format("$0: no quiescent marker", peer->LogPrefix()));
      result[peer->permanent_uuid() + peer->tablet_id()] = log->quiescent_marker_op_id();
    }
    return result;
  }

  void CountRemoteBootstraps() {
    SyncPoint::GetInstance()->SetCallBack(
        "TSTabletManager::StartRemoteBootstrap", [this](void*) { ++num_remote_bootstraps_; });
    SyncPoint::GetInstance()->EnableProcessing();
  }

  void TestRestart();
  void TestRemoteBootstrap(const TabletId& tablet_id);

  std::atomic<int> num_remote_bootstraps_{0};
};

void QuiescentTabletTest::TestRestart() {
  CountRemoteBootstraps();

  // A tablet without data and a marker must not be taken for a new tablet.
  ASSERT_OK(WaitAllQuiescent());
  ASSERT_RESULT(CheckWalDirs());
  ASSERT_NO_FATALS(RestartCluster());
  ASSERT_NO_FATALS(CheckRows(0));

  ASSERT_NO_FATALS(WriteRows(0, 100));
  ASSERT_OK(WaitAllQuiescent());
  auto markers = ASSERT_RESULT(CheckWalDirs());

  ASSERT_NO_FATALS(RestartCluster());
  ASSERT_NO_FATALS(CheckRows(100));
  ASSERT_NO_FATALS(WriteRows(100, 200));
  ASSERT_NO_FATALS(CheckRows(200));

  ASSERT_OK(WaitAllQuiescent());
  auto new_markers = ASSERT_RESULT(CheckWalDirs());
  for (const auto& [key, op_id] : markers) {
    ASSERT_GT(new_markers[key], op_id) << key;
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
  ASSERT_NO_FATALS(RestartCluster());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = false;
  ASSERT_NO_FATALS(CheckRows(200));
  ASSERT_NO_FATALS(WriteRows(200, 300));
  ASSERT_NO_FATALS(CheckRows(300));

  ASSERT_EQ(num_remote_bootstraps_.load(), 0);
}

TEST_F(QuiescentTabletTest, Restart) {
  TestRestart();
}

class QuiescentTabletRf1Test : public QuiescentTabletTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
    QuiescentTabletTest::SetUp();
  }

  size_t num_tablet_servers() override {
    return 1;
  }
};

TEST_F(QuiescentTabletRf1Test, Restart) {
  TestRestart();
}

// Remote bootstrap from a quiescent source carries the marker.
void QuiescentTabletTest::TestRemoteBootstrap(const TabletId& tablet_id) {
  ASSERT_NO_FATALS(WriteRows(0, 100));
  ASSERT_OK(WaitAllQuiescent());

  auto leader = ASSERT_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
  tserver::MiniTabletServer* follower_ts = nullptr;
  for (size_t i = 0; i != mini_cluster()->num_tablet_servers(); ++i) {
    auto* ts = mini_cluster()->mini_tablet_server(i);
    if (ts->server()->permanent_uuid() != leader->permanent_uuid()) {
      follower_ts = ts;
      break;
    }
  }
  ASSERT_NE(follower_ts, nullptr);

  std::optional<tserver::TabletServerErrorPB::Code> error_code;
  ASSERT_OK(follower_ts->server()->tablet_manager()->DeleteTablet(
      tablet_id, tablet::TABLET_DATA_TOMBSTONED, tablet::ShouldAbortActiveTransactions::kFalse,
      /* cas_config_opid_index_less_or_equal= */ std::nullopt, /* hide_only= */ false,
      /* keep_data= */ false, &error_code));

  // The tombstoned replica is remote bootstrapped and becomes quiescent again.
  ASSERT_OK(WaitAllQuiescent());
  ASSERT_RESULT(CheckWalDirs());
  auto peer = ASSERT_RESULT(follower_ts->server()->tablet_manager()->GetTablet(tablet_id));
  ASSERT_EQ(
      ASSERT_RESULT(peer->shared_tablet())->TEST_DocDBDumpStr(),
      ASSERT_RESULT(leader->shared_tablet())->TEST_DocDBDumpStr());

  ASSERT_NO_FATALS(RestartCluster());
  ASSERT_NO_FATALS(CheckRows(100));
  ASSERT_NO_FATALS(WriteRows(100, 200));
  ASSERT_NO_FATALS(CheckRows(200));
}

TEST_F(QuiescentTabletTest, RemoteBootstrap) {
  TestRemoteBootstrap(TablePeers().front()->tablet_id());
}

// The first tablet server is alone in its region, and the other two share a zone.
class QuiescentTabletFollowerSourceTest : public QuiescentTabletTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_remote_bootstrap_from_leader_only) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_assert_remote_bootstrap_happens_from_same_zone) = true;
    // The check breaks the addresses that the default placement does not use.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    QuiescentTabletTest::SetUp();
  }

  void BeforeCreateTable() override {
    for (size_t i = 0; i != num_tablet_servers(); ++i) {
      auto* ts = mini_cluster()->mini_tablet_server(i);
      const auto location = i == 0 ? 0 : 1;
      ts->options()->SetPlacement(
          "cloud", Format("region$0", location), Format("zone$0", location));
      ASSERT_OK(ts->Restart());
    }
    // Replicas of the table get the placement that the master has when it creates the table.
    ASSERT_OK(WaitFor([this]() -> Result<bool> {
      auto* master = VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster())->master();
      for (const auto& desc : master->ts_manager()->GetAllDescriptors()) {
        if (desc->GetCloudInfo().placement_zone() == "zone") {
          return false;
        }
      }
      return true;
    }, 30s * kTimeMultiplier, "New placement registered"));
  }
};

// A follower in the destination's zone is the source. It anchors the quiescent leader's log at L.
TEST_F(QuiescentTabletFollowerSourceTest, RemoteBootstrap) {
  const auto tablet_id = TablePeers().front()->tablet_id();
  const auto leader_uuid = mini_cluster()->mini_tablet_server(0)->server()->permanent_uuid();
  ASSERT_OK(TransferLeadership(mini_cluster(), tablet_id, leader_uuid));
  TestRemoteBootstrap(tablet_id);
}

// Split children start from the marker of a quiescent parent.
TEST_F(QuiescentTabletTest, Split) {
  ASSERT_NO_FATALS(WriteRows(0, 100));
  ASSERT_OK(WaitAllQuiescent());

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      mini_cluster(), TablePeers().front()->tablet_id()));
  ASSERT_OK(WaitAllQuiescent(num_tablets() + 1));
  ASSERT_RESULT(CheckWalDirs());

  ASSERT_NO_FATALS(RestartCluster());
  ASSERT_NO_FATALS(CheckRows(100));
  ASSERT_NO_FATALS(WriteRows(100, 200));
  ASSERT_NO_FATALS(CheckRows(200));
}

// A table that asks to keep its WAL for a time stays out of the mode.
TEST_F(QuiescentTabletTest, TableWalRetention) {
  ASSERT_OK(client_->NewTableAlterer(table_name())->SetWalRetentionSecs(3600)->Alter());
  ASSERT_NO_FATALS(WriteRows(0, 10));
  std::this_thread::sleep_for(FLAGS_quiescent_tablet_idle_sec * 3s);
  for (const auto& peer : TablePeers()) {
    ASSERT_FALSE(peer->IsQuiescent());
  }
}

TEST_F(QuiescentTabletTest, Disabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_quiescent_tablets) = false;
  ASSERT_NO_FATALS(WriteRows(0, 10));
  std::this_thread::sleep_for(FLAGS_quiescent_tablet_idle_sec * 3s);
  for (const auto& peer : TablePeers()) {
    ASSERT_FALSE(peer->IsQuiescent());
    auto* env = peer->tablet_metadata()->fs_manager()->env();
    ASSERT_FALSE(env->FileExists(log::Log::QuiescentMarkerPath(peer->log()->wal_dir())));
  }
}

} // namespace yb::integration_tests
