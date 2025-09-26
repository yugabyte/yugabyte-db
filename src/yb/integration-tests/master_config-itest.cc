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

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/gutil/algorithm.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using std::string;
using std::vector;
using std::min;
using strings::Substitute;
using yb::tserver::TabletServerErrorPB;

using namespace std::chrono_literals;

namespace yb {
namespace master {

// Test master addition and removal config changes via an external mini cluster
class MasterChangeConfigTest : public YBTest {
 public:
  MasterChangeConfigTest() {}

  ~MasterChangeConfigTest() {}

 protected:
  void SetUp() override {
    YBTest::SetUp();
    ExternalMiniClusterOptions opts;
    opts.master_rpc_ports = { 0, 0, 0 }; // external mini-cluster Start() gets the free ports.
    opts.num_masters = num_masters_ = opts.master_rpc_ports.size();
    opts.num_tablet_servers = 0;
    opts.timeout = MonoDelta::FromSeconds(30);
    // Master failovers should not be happening concurrently with us trying to load an initial sys
    // catalog snapshot. At least this is not supported as of 05/27/2019.
    opts.enable_ysql = false;
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForLeaderCommitTermAdvance());

    ASSERT_OK(CheckNumMastersWithCluster("Start"));
  }

  void TearDown() override {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }

    YBTest::TearDown();
  }

  Status CheckNumMastersWithCluster(string msg) {
    if (num_masters_ != cluster_->num_masters()) {
      return STATUS(IllegalState, Substitute(
          "$0 : expected to have $1 masters but our cluster has $2 masters.",
          msg, num_masters_, cluster_->num_masters()));
    }

    return Status::OK();
  }

  Status RestartCluster() {
    if (!cluster_) {
      return STATUS(IllegalState, "Cluster was not initialized, cannot restart.");
    }

    RETURN_NOT_OK(CheckNumMastersWithCluster("Pre Restart"));

    cluster_->Shutdown();
    RETURN_NOT_OK(cluster_->Restart());

    RETURN_NOT_OK(CheckNumMastersWithCluster("Post Restart"));

    RETURN_NOT_OK(cluster_->WaitForLeaderCommitTermAdvance());

    return Status::OK();
  }

  // Ensure that the leader's in-memory state has the expected number of peers.
  void VerifyLeaderMasterPeerCount();

  // Ensure that each non-leader's in-memory state has the expected number of peers.
  void VerifyNonLeaderMastersPeerCount();

  // Waits till the master leader is ready - as deemed by the catalog manager. If the leader never
  // loads the sys catalog, this api will timeout. If 'master' is not the leader it will surely
  // timeout. Return status of OK() implies leader is ready.
  Status WaitForMasterLeaderToBeReady(ExternalMaster* master, int timeout_sec);

  // API to capture the latest commit index on the master leader.
  void SetCurLogIndex();

  size_t num_masters_;
  int64_t cur_log_index_;
  std::unique_ptr<ExternalMiniCluster> cluster_;
};

void MasterChangeConfigTest::VerifyLeaderMasterPeerCount() {
  int num_peers = 0;
  ExternalMaster *leader_master = cluster_->GetLeaderMaster();
  LOG(INFO) << "Checking leader at port " << leader_master->bound_rpc_hostport().port();
  Status s = cluster_->GetNumMastersAsSeenBy(leader_master, &num_peers);
  ASSERT_OK_PREPEND(s, "Leader master number of peers lookup returned error");
  EXPECT_EQ(num_peers, num_masters_);
}

void MasterChangeConfigTest::VerifyNonLeaderMastersPeerCount() {
  int num_peers = 0;
  auto leader_index = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());

  for (size_t i = 0; i < num_masters_; i++) {
    if (i == leader_index) {
      continue;
    }

    ExternalMaster *non_leader_master = cluster_->master(i);

    LOG(INFO) << "Checking non_leader " << i << " at port "
              << non_leader_master->bound_rpc_hostport().port();
    num_peers = 0;
    Status s;
    ASSERT_OK_PREPEND(
        WaitFor(
            [&] {
              s = cluster_->GetNumMastersAsSeenBy(non_leader_master, &num_peers);
              return s.ok();
            },
            5s * kTimeMultiplier, "Waiting master is initialized"),
        Format("Non-leader master number of peers lookup returned error: $0", s));
    EXPECT_EQ(num_peers, num_masters_);
  }
}

Status MasterChangeConfigTest::WaitForMasterLeaderToBeReady(
    ExternalMaster* master,
    int timeout_sec) {
  MonoTime now = MonoTime::Now();
  MonoTime deadline = now;
  deadline.AddDelta(MonoDelta::FromSeconds(timeout_sec));
  Status s;

  for (int i = 1; now.ComesBefore(deadline); ++i) {
    s = cluster_->GetIsMasterLeaderServiceReady(master);
    if (!s.ok()) {
      // Spew out error info only if it is something other than not-the-leader.
      if (s.ToString().find("NOT_THE_LEADER") == std::string::npos) {
        LOG(WARNING) << "Hit error '" << s.ToString() << "', in iter " << i;
      }
    } else {
      LOG(INFO) << "Got leader ready in iter " << i;
      return Status::OK();
    }
    SleepFor(MonoDelta::FromMilliseconds(min(i, 10)));
    now = MonoTime::Now();
  }

  return STATUS(TimedOut, Substitute("Timed out as master leader $0 term not ready.",
                                     master->bound_rpc_hostport().ToString()));
}

void MasterChangeConfigTest::SetCurLogIndex() {
  OpIdPB op_id;
  ASSERT_OK(cluster_->GetLastOpIdForLeader(&op_id));
  cur_log_index_ = op_id.index();
  LOG(INFO) << "cur_log_index_ " << cur_log_index_;
}

TEST_F(MasterChangeConfigTest, TestAddMaster) {
  // NOTE: Not using smart pointer as ExternalMaster is derived from a RefCounted base class.
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());

  SetCurLogIndex();

  Status s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error : ");

  // Adding a server will generate two ChangeConfig calls. One to add a server as a learner, and one
  // to promote this server to a voter once bootstrapping is finished.
  cur_log_index_ += 2;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));
  ++num_masters_;

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestSlowRemoteBootstrapDoesNotCrashMaster) {
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());
  ASSERT_OK(
      cluster_->SetFlag(new_master.get(), "TEST_inject_latency_during_remote_bootstrap_secs", "8"));

  SetCurLogIndex();

  Status s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error : ");

  // Adding a server will generate two ChangeConfig calls. One to add a server as a learner, and one
  // to promote this server to a voter once bootstrapping is finished.
  cur_log_index_ += 2;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));
  ++num_masters_;

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestRemoveMaster) {
  auto non_leader_index = ASSERT_RESULT(cluster_->GetFirstNonLeaderMasterIndex());
  ExternalMaster* remove_master = cluster_->master(non_leader_index);

  LOG(INFO) << "Going to remove master at port " << remove_master->bound_rpc_hostport().port();

  SetCurLogIndex();

  auto s = cluster_->ChangeConfig(remove_master, consensus::REMOVE_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error");

  // REMOVE_SERVER causes the op index to increase by one.
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(++cur_log_index_));

  --num_masters_;

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestRemoveDeadMaster) {
  auto non_leader_index = ASSERT_RESULT(cluster_->GetFirstNonLeaderMasterIndex());
  ExternalMaster* remove_master = cluster_->master(non_leader_index);
  remove_master->Shutdown();
  LOG(INFO) << "Stopped and removing master at " << remove_master->bound_rpc_hostport().port();

  SetCurLogIndex();

  auto s = cluster_->ChangeConfig(remove_master, consensus::REMOVE_SERVER,
                                  consensus::PeerMemberType::PRE_VOTER, true /* use_hostport */);
  ASSERT_OK_PREPEND(s, "Change Config returned error");

  // REMOVE_SERVER causes the op index to increase by one.
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(++cur_log_index_));

  --num_masters_;

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestRestartAfterConfigChange) {
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());

  SetCurLogIndex();

  Status s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error");

  ++num_masters_;

  // Adding a server will generate two ChangeConfig calls. One to add a server as a learner, and one
  // to promote this server to a voter once bootstrapping is finished.
  cur_log_index_ += 2;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();

  // Give time for cmeta to get flushed on all peers - TODO(Bharat) ENG-104
  SleepFor(MonoDelta::FromSeconds(5));

  s = RestartCluster();
  ASSERT_OK_PREPEND(s, "Restart Cluster failed");

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestNewLeaderWithPendingConfigLoadsSysCatalog) {
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());

  LOG(INFO) << "New master " << new_master->bound_rpc_hostport().ToString();

  SetCurLogIndex();

  // This will disable new elections on the old masters.
  vector<ExternalMaster*> masters = cluster_->master_daemons();
  for (auto master : masters) {
    ASSERT_OK(cluster_->SetFlag(master, "TEST_do_not_start_election_test_only", "true"));
    // Do not let the followers commit change role - to keep their opid same as the new master,
    // and hence will vote for it.
    ASSERT_OK(cluster_->SetFlag(master, "inject_delay_commit_pre_voter_to_voter_secs", "5"));
  }

  // Wait for 5 seconds on new master to commit voter mode transition. Note that this should be
  // less than the timeout sent to WaitForMasterLeaderToBeReady() below. We want the pending
  // config to be preset when the new master is deemed as leader to start the sys catalog load, but
  // would need to get that pending config committed for load to progress.
  ASSERT_OK(
      cluster_->SetFlag(new_master.get(), "inject_delay_commit_pre_voter_to_voter_secs", "5"));
  // And don't let it start an election too soon.
  ASSERT_OK(cluster_->SetFlag(new_master.get(), "TEST_do_not_start_election_test_only", "true"));

  Status s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error");

  // Wait for addition of the new master as a PRE_VOTER to commit on all peers. The CHANGE_ROLE
  // part is not committed on all the followers, as that might block the new master from becoming
  // the leader as others would have a opid higher than the new master and will not vote for it.
  // The new master will become FOLLOWER and can start an election once it has a pending change
  // that makes it a VOTER.
  cur_log_index_ += 1;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));
  TabletServerErrorPB::Code dummy_err = TabletServerErrorPB::UNKNOWN_ERROR;
  // Leader step down.
  s = cluster_->StepDownMasterLeader(&dummy_err);

  // Now the new master should start the election process.
  ASSERT_OK(cluster_->SetFlag(new_master.get(), "TEST_do_not_start_election_test_only", "false"));

  // Leader stepdown might not succeed as PRE_VOTER could still be uncommitted. Let it go through
  // as new master should get the other votes anyway once it starts the election.
  if (!s.IsIllegalState()) {
    ASSERT_OK_PREPEND(s,  "Leader step down failed.");
  } else {
    LOG(INFO) << "Triggering election as step down failed.";
    ASSERT_OK_PREPEND(cluster_->StartElection(new_master.get()), "Start Election failed");
    SleepFor(MonoDelta::FromSeconds(2));
  }

  // Ensure that the new leader is the new master we spun up above.
  ExternalMaster* new_leader = cluster_->GetLeaderMaster();
  LOG(INFO) << "New leader " << new_leader->bound_rpc_hostport().ToString();
  ASSERT_EQ(new_master->bound_rpc_addr().port(), new_leader->bound_rpc_addr().port());

  // This check ensures that the sys catalog is loaded into new leader even when it has a
  // pending config change.
  ASSERT_OK(WaitForMasterLeaderToBeReady(new_master.get(), 8 /* timeout_sec */));
}

TEST_F(MasterChangeConfigTest, TestChangeAllMasters) {
  ExternalMasterPtr new_masters[3] = {nullptr, nullptr, nullptr};
  ExternalMasterPtr remove_master = nullptr;

  // Create all new masters before to avoid rpc port reuse.
  for (int idx = 0; idx <= 2; idx++) {
    new_masters[idx] = ASSERT_RESULT(cluster_->StartShellMaster());
  }

  SetCurLogIndex();

  for (int idx = 0; idx <= 2; idx++) {
    LOG(INFO) << "LOOP " << idx << " start.";
    LOG(INFO) << "ADD " << new_masters[idx]->bound_rpc_hostport().ToString();
    ASSERT_OK_PREPEND(cluster_->ChangeConfig(new_masters[idx], consensus::ADD_SERVER),
                      "Add Change Config returned error");
    ++num_masters_;
    remove_master = cluster_->master(0);
    LOG(INFO) << "REMOVE " << remove_master->bound_rpc_hostport().ToString();
    ASSERT_OK_PREPEND(cluster_->ChangeConfig(remove_master, consensus::REMOVE_SERVER),
                      "Remove Change Config returned error");
    --num_masters_;
    LOG(INFO) << "LOOP " << idx << " end.";
  }

  // Followers might not be up to speed as we did not wait, so just check leader.
  VerifyLeaderMasterPeerCount();
}

TEST_F(MasterChangeConfigTest, TestAddPreObserverMaster) {
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());

  SetCurLogIndex();
  ASSERT_OK_PREPEND(cluster_->ChangeConfig(new_master, consensus::ADD_SERVER,
                                           consensus::PeerMemberType::PRE_OBSERVER),
                    "Add Change Config returned error");
  ++num_masters_;

  // Followers might not be up to speed as we did not wait, so just check leader.
  VerifyLeaderMasterPeerCount();
}

TEST_F(MasterChangeConfigTest, TestWaitForChangeRoleCompletion) {
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());
  ExternalMaster* leader = cluster_->GetLeaderMaster();

  // Ensure leader does not change.
  for (int idx = 0; idx <= 2; idx++) {
    ExternalMaster* master = cluster_->master(idx);
    if (master->bound_rpc_hostport().port() != leader->bound_rpc_hostport().port()) {
      ASSERT_OK(cluster_->SetFlag(master, "TEST_do_not_start_election_test_only", "false"));
    }
  }

  ASSERT_OK(cluster_->SetFlag(leader,
            "TEST_inject_delay_leader_change_role_append_secs", "8"));
  SetCurLogIndex();
  ASSERT_OK_PREPEND(cluster_->ChangeConfig(new_master, consensus::ADD_SERVER),
                    "Add Change Config returned error");

  // Wait a bit for PRE_VOTER to be committed. This should be less than the value of 8 seconds
  // set in the injected delay above.
  SleepFor(MonoDelta::FromSeconds(1));

  LOG(INFO) << "Remove Leader " << leader->bound_rpc_hostport().ToString();
  ASSERT_OK_PREPEND(cluster_->ChangeConfig(leader, consensus::REMOVE_SERVER),
                    "Remove Change Config returned error");

  VerifyLeaderMasterPeerCount();
}

TEST_F(MasterChangeConfigTest, TestLeaderSteppedDownNotElected) {
  SetCurLogIndex();
  ExternalMaster* old_leader = cluster_->GetLeaderMaster();
  // Give the other peers few iterations to converge.
  ASSERT_OK(cluster_->SetFlag(old_leader, "leader_failure_max_missed_heartbeat_periods", "24"));
  LOG(INFO) << "Current leader bound to " << old_leader->bound_rpc_hostport().ToString();
  TabletServerErrorPB::Code dummy_err = TabletServerErrorPB::UNKNOWN_ERROR;
  ASSERT_OK_PREPEND(cluster_->StepDownMasterLeader(&dummy_err),
                    "Leader step down failed.");
  // Ensure that the new leader is not the old leader.
  ExternalMaster* new_leader = cluster_->GetLeaderMaster();
  LOG(INFO) << "New leader bound to " << new_leader->bound_rpc_hostport().ToString();
  ASSERT_NE(old_leader->bound_rpc_addr().port(), new_leader->bound_rpc_addr().port());
}

TEST_F(MasterChangeConfigTest, TestMulitpleLeaderRestarts) {
  ExternalMaster* first_leader = cluster_->GetLeaderMaster();
  first_leader->Shutdown();
  // Ensure that the new leader is not the old leader.
  ExternalMaster* second_leader = cluster_->GetLeaderMaster();
  ASSERT_NE(second_leader->bound_rpc_addr().port(), first_leader->bound_rpc_addr().port());
  // Revive the first leader.
  ASSERT_OK(first_leader->Restart());
  ExternalMaster* check_leader = cluster_->GetLeaderMaster();
  // Leader should not be first leader.
  ASSERT_NE(check_leader->bound_rpc_addr().port(), first_leader->bound_rpc_addr().port());
  second_leader->Shutdown();
  check_leader = cluster_->GetLeaderMaster();
  // Leader should not be second one, it can be any one of the other masters.
  ASSERT_NE(second_leader->bound_rpc_addr().port(), check_leader->bound_rpc_addr().port());
}

TEST_F(MasterChangeConfigTest, TestPingShellMaster) {
  // Create a shell master as `peers` is empty (for master_addresses).
  auto new_shell_master = ASSERT_RESULT(cluster_->StartMaster(/*peer_addrs=*/""));
  // Add the new shell master to the quorum and ensure it is still running and pingable.
  SetCurLogIndex();
  Status s = cluster_->ChangeConfig(new_shell_master, consensus::ADD_SERVER);
  LOG(INFO) << "Started shell " << new_shell_master->bound_rpc_hostport().ToString();
  ASSERT_OK_PREPEND(s, "Change Config returned error : ");
  ++num_masters_;
  ASSERT_OK(cluster_->PingMaster(new_shell_master.get()));
}

TEST_F(MasterChangeConfigTest, TestConcurrentAddMastersFails) {
  auto initial_masters = cluster_->master_daemons();
  ExternalMasterPtr second_new_master = nullptr;
  SetCurLogIndex();

  auto first_new_master = ASSERT_RESULT(cluster_->StartShellMaster());
  // Delay rbs so that the master leader gets to process ADD_SERVER request for the second new
  // master while the first new master is in PRE_VOTER state.
  ASSERT_OK(cluster_->SetFlag(
      first_new_master.get(), "TEST_inject_latency_during_remote_bootstrap_secs", "15"));
  ASSERT_OK_PREPEND(cluster_->ChangeConfig(first_new_master, consensus::ADD_SERVER),
                    "Change Config(ADD_SERVER) returned error: ");
  // Just ADD_SERVER gets processed, so increment the log index only by 1.
  cur_log_index_ += 1;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_, initial_masters, 5s));
  ++num_masters_;
  // Try adding the second new master. This should block until the above added master gets promoted
  // from PRE_VOTER to VOTER.
  auto status_future = std::async(std::launch::async, [&]() {
    second_new_master = VERIFY_RESULT(cluster_->StartShellMaster());
    return cluster_->ChangeConfig(second_new_master, consensus::ADD_SERVER);
  });
  ASSERT_TRUE(status_future.wait_for(10s) == std::future_status::timeout)
      << "Change Config(ADD_SERVER) should have been blocked for at least 10 secs.";

  // Wait for the first new master to raise a CHANGE_ROLE request.
  cur_log_index_ += 1;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));
  // Now the ADD_SERVER for the second master should go through.
  ASSERT_OK_PREPEND(status_future.get(), "Change Config(ADD_SERVER) returned error: ");
  // Wait for the log index to be incremented by 2, one for ADD_SERVER and other for CHANGE_ROLE.
  cur_log_index_ += 2;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));
  ++num_masters_;
  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestBlockRemoveServerWhenConfigHasTransitioningServer) {
  auto current_masters = cluster_->master_daemons();
  const size_t num_add_masters = 2;
  for (size_t i = 0 ; i < num_add_masters ; i++) {
    auto new_master = ASSERT_RESULT(cluster_->StartShellMaster());
    // Make this peer stay in the PRE_VOTER phase for a while. Setting the delay to 15 secs as the
    // timeout for WaitForMastersToCommitUpTo is set to 30 secs. So when we finally wait below for
    // this peer to become a VOTER (at max for 30 secs), the call wouldn't fail.
    ASSERT_OK(cluster_->SetFlag(
        new_master.get(), "TEST_inject_latency_during_remote_bootstrap_secs", "15"));

    SetCurLogIndex();

    Status s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
    ASSERT_OK_PREPEND(s, "Change Config(ADD_SERVER) returned error: ");
    // Adding a server will generate two ChangeConfig calls. One to add a server as a learner, and
    // one to promote this server to a voter once bootstrapping is finished. Since we don't want to
    // wait until the new peer becomes a VOTER, just wait for the log index to be incremented by 1.
    cur_log_index_ += 1;
    ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_, current_masters));
    ++num_masters_;

    // If we try to remove the leader master, it will initiate a leader stepdown and then remove it.
    // But since there is a peer in transition, leader stepdown will not go through. So try removing
    // a follower instead.
    if (cluster_->GetLeaderMaster()->uuid() == current_masters.back()->uuid()) {
      std::swap(current_masters.back(), current_masters.front());
    }
    // Even the follower removal shouldn't go through until the above added server is in transition.
    ExternalMaster* follower_to_remove = current_masters.back();
    auto status_future = std::async(std::launch::async, [&, follower_to_remove]() {
      return cluster_->ChangeConfig(follower_to_remove, consensus::REMOVE_SERVER);
    });
    ASSERT_TRUE(status_future.wait_for(10s) == std::future_status::timeout)
        << "Change Config(REMOVE_SERVER) should have been blocked for at least 10 secs.";
    current_masters.pop_back();
    // Wait for the above added server to transition from PRE_VOTER to VOTER.
    cur_log_index_ += 1;
    ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_, current_masters));
    current_masters.push_back(new_master.get());
    // Now the REMOVE_SERVER request should go through.
    ASSERT_OK_PREPEND(status_future.get(), "Change Config(REMOVE_SERVER) returned error: ");
    cur_log_index_ += 1;
    ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_, current_masters));
    --num_masters_;
  }

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

} // namespace master
} // namespace yb
