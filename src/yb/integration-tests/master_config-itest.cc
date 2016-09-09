// Copyright (c) Yugabyte, Inc.

#include <gtest/gtest.h>

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/gutil/strings/join.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/master/master.h"
#include "yb/master/master-test-util.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/master.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"

using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::consensus::ConsensusServiceProxy;
using yb::consensus::RaftPeerPB;
using yb::consensus::ChangeConfigRequestPB;
using yb::consensus::ChangeConfigResponsePB;
using yb::master::ListMastersRequestPB;
using yb::master::ListMastersResponsePB;

namespace yb {
namespace master {

// Test master addition and removal config changes via an external mini cluster
class MasterChangeConfigTest : public YBTest {
 public:
  MasterChangeConfigTest() {}

  ~MasterChangeConfigTest() {}

 protected:
  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();
    ExternalMiniClusterOptions opts;
    opts.master_rpc_ports = { 0, 0, 0 }; // external mini-cluster Start() gets the free ports.
    opts.num_masters = num_masters_ = static_cast<int>(opts.master_rpc_ports.size());
    opts.num_tablet_servers = 0;
    opts.timeout_ = MonoDelta::FromSeconds(30);
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(cluster_->WaitForLeaderCommitTermAdvance());

    ASSERT_OK(CheckNumMastersWithCluster("Start"));

    MessengerBuilder builder("config_change");
    Status s = builder.Build(&messenger_);
    if (!s.ok()) {
      LOG(FATAL) << "Unable to build messenger : " << s.ToString();
    }

    OpId op_id;
    ASSERT_OK(cluster_->GetLastOpIdForLeader(&op_id));
    cur_log_index_ = op_id.index();
    LOG(INFO) << "cur_log_index_ " << cur_log_index_;
  }

  virtual void TearDown() OVERRIDE {
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

  int num_masters_;
  int cur_log_index_;
  std::unique_ptr<ExternalMiniCluster> cluster_;
  std::shared_ptr<rpc::Messenger> messenger_;
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
  int leader_index = -1;
  ASSERT_OK_PREPEND(cluster_->GetLeaderMasterIndex(&leader_index), "Leader index get failed.");

  if (leader_index == -1) {
    FAIL() << "Leader index not found.";
  }

  for (int i = 0; i < num_masters_; i++) {
    if (i == leader_index) {
      continue;
    }

    ExternalMaster *non_leader_master = cluster_->master(i);

    LOG(INFO) << "Checking non_leader " << i << " at port "
              << non_leader_master->bound_rpc_hostport().port();
    num_peers = 0;
    Status s = cluster_->GetNumMastersAsSeenBy(non_leader_master, &num_peers);
    ASSERT_OK_PREPEND(s, "Non-leader master number of peers lookup returned error");
    EXPECT_EQ(num_peers, num_masters_);
  }
}

Status MasterChangeConfigTest::WaitForMasterLeaderToBeReady(
    ExternalMaster* master,
    int timeout_sec) {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
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
    now = MonoTime::Now(MonoTime::FINE);
  }

  return STATUS(TimedOut, Substitute("Timed out as master leader $0 term not ready.",
                                     master->bound_rpc_hostport().ToString()));
}

TEST_F(MasterChangeConfigTest, TestAddMaster) {
  // NOTE: Not using smart pointer as ExternalMaster is derived from a RefCounted base class.
  ExternalMaster* new_master = nullptr;
  cluster_->StartNewMaster(&new_master);

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
  ExternalMaster* new_master = nullptr;
  cluster_->StartNewMaster(&new_master);
  cluster_->SetFlag(new_master, "inject_latency_during_remote_bootstrap_secs", "8");

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
  int non_leader_index = -1;
  Status s = cluster_->GetFirstNonLeaderMasterIndex(&non_leader_index);
  ASSERT_OK_PREPEND(s, "Non-leader master lookup returned error");
  if (non_leader_index == -1) {
    FAIL() << "Failed to get a non-leader master index.";
  }
  ExternalMaster* remove_master = cluster_->master(non_leader_index);

  LOG(INFO) << "Going to remove master at port " << remove_master->bound_rpc_hostport().port();

  s = cluster_->ChangeConfig(remove_master, consensus::REMOVE_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error");

  // REMOVE_SERVER causes the op index to increase by one.
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(++cur_log_index_));

  --num_masters_;

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestRestartAfterConfigChange) {
  ExternalMaster* new_master = nullptr;
  cluster_->StartNewMaster(&new_master);

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
  SleepFor(MonoDelta::FromSeconds(2));

  s = RestartCluster();
  ASSERT_OK_PREPEND(s, "Restart Cluster failed");

  VerifyLeaderMasterPeerCount();
  VerifyNonLeaderMastersPeerCount();
}

TEST_F(MasterChangeConfigTest, TestNewLeaderWithPendingConfigLoadsSysCatalog) {
  ExternalMaster* new_master = nullptr;
  cluster_->StartNewMaster(&new_master);

  LOG(INFO) << "New master " << new_master->bound_rpc_hostport().ToString();

  // This will disable new elections on the old masters.
  vector<ExternalDaemon*> masters = cluster_->master_daemons();
  for (auto master : masters) {
    ASSERT_OK(cluster_->SetFlag(master, "do_not_start_election_test_only", "true"));
    // Do not let the followers commit change role - to keep their opid same as the new master,
    // and hence will vote for it.
    ASSERT_OK(cluster_->SetFlag(master, "inject_delay_commit_non_voter_to_voter_secs", "5"));
  }

  // Wait for 5 seconds on new master to commit voter mode transition. Note that this should be
  // less than the timeout sent to WaitForMasterLeaderToBeReady() below. We want the pending
  // config to be preset when the new master is deemed as leader to start the sys catalog load, but
  // would need to get that pending config committed for load to progress.
  ASSERT_OK(cluster_->SetFlag(new_master, "inject_delay_commit_non_voter_to_voter_secs", "5"));
  // And don't let it start an election too soon.
  ASSERT_OK(cluster_->SetFlag(new_master, "do_not_start_election_test_only", "true"));

  Status s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
  ASSERT_OK_PREPEND(s, "Change Config returned error");

  // Wait for addition of the new master as a NON_VOTER to commit on all peers. The CHANGE_ROLE
  // part is not committed on all the followers, as that might block the new master from becoming
  // the leader as others would have a opid higher than the new master and will not vote for it.
  // The new master will become FOLLOWER and can start an election once it has a pending change
  // that makes it a VOTER.
  cur_log_index_ += 1;
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(cur_log_index_));

  // Leader step down.
  ASSERT_OK_PREPEND(cluster_->StepDownMasterLeader(), "Leader step down failed.");

  // Now the new master should start the election process.
  ASSERT_OK(cluster_->SetFlag(new_master, "do_not_start_election_test_only", "false"));

  // Ensure that the new leader is the new master we spun up above.
  ExternalMaster* new_leader = cluster_->GetLeaderMaster();
  LOG(INFO) << "New leader " << new_leader->bound_rpc_hostport().ToString();
  ASSERT_EQ(new_master->bound_rpc_addr().port(), new_leader->bound_rpc_addr().port());

  // This check ensures that the sys catalog is loaded into new leader even when it has a
  // pending config change.
  ASSERT_OK(WaitForMasterLeaderToBeReady(new_master, 8 /* timeout_sec */));
}

} // namespace master
} // namespace yb
