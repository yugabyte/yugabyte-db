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
      return Status::IllegalState(Substitute(
          "$0 : expected to have $1 masters but our cluster has $2 masters.",
          msg, num_masters_, cluster_->num_masters()));
    }

    return Status::OK();
  }

  Status RestartCluster() {
    if (!cluster_) {
      return Status::IllegalState("Cluster was not initialized, cannot restart.");
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

TEST_F(MasterChangeConfigTest, TestAddMaster) {
  // NOTE: Not using smart pointer as ExternalMaster is derived from a RefCounted base class.
  ExternalMaster* new_master = nullptr;
  Status s = cluster_->StartNewMaster(&new_master);
  ASSERT_OK_PREPEND(s, "Unable to start new master : ");

  if (!new_master) {
    FAIL() << "No new master returned.";
  }

  s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
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
  // NOTE: Not using smart pointer as ExternalMaster is derived from a RefCounted base class.
  ExternalMaster* new_master = nullptr;
  Status s = cluster_->StartNewMaster(&new_master);
  ASSERT_OK_PREPEND(s, "Unable to start new master : ");
  if (!new_master) {
    FAIL() << "No new master returned.";
  }
  cluster_->SetFlag(new_master, "inject_latency_during_remote_bootstrap_secs", "8");

  s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
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
  Status s = cluster_->StartNewMaster(&new_master);
  ASSERT_OK_PREPEND(s, "Unable to start new master");

  if (!new_master) {
    FAIL() << "No new master returned.";
  }

  s = cluster_->ChangeConfig(new_master, consensus::ADD_SERVER);
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

} // namespace master
} // namespace yb
