// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/consensus/consensus_meta.h"

#include <vector>

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"
#include "yb/fs/fs_manager.h"
#include "yb/util/net/net_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

#define ASSERT_VALUES_EQUAL(cmeta, opid_index, uuid, term) \
  ASSERT_NO_FATALS(AssertValuesEqual(cmeta, opid_index, uuid, term))

namespace yb {
namespace consensus {

using std::string;
using std::vector;

const char* kTabletId = "test-consensus-metadata";
const int64_t kInitialTerm = 3;

class ConsensusMetadataTest : public YBTest {
 public:
  ConsensusMetadataTest()
    : fs_manager_(env_.get(), GetTestPath("fs_root"), "tserver_test") {
  }

  void SetUp() override {
    YBTest::SetUp();
    ASSERT_OK(fs_manager_.CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_.CheckAndOpenFileSystemRoots());
    fs_manager_.SetTabletPathByDataPath(kTabletId, fs_manager_.GetDataRootDirs()[0]);

    // Initialize test configuration.
    config_.set_opid_index(kInvalidOpIdIndex);
    RaftPeerPB* peer = config_.add_peers();
    peer->set_permanent_uuid(fs_manager_.uuid());
    peer->set_member_type(PeerMemberType::VOTER);
    HostPortToPB(HostPort("fake-host", 0), peer->mutable_last_known_private_addr()->Add());
  }

 protected:
  // Assert that the given cmeta has a single configuration with the given metadata values.
  void AssertValuesEqual(const ConsensusMetadata& cmeta,
                         int64_t opid_index, const string& permanent_uuid, int64_t term);

  FsManager fs_manager_;
  RaftConfigPB config_;
};

void ConsensusMetadataTest::AssertValuesEqual(const ConsensusMetadata& cmeta,
                                              int64_t opid_index,
                                              const string& permanent_uuid,
                                              int64_t term) {
  // Sanity checks.
  ASSERT_EQ(1, cmeta.committed_config().peers_size());

  // Value checks.
  ASSERT_EQ(opid_index, cmeta.committed_config().opid_index());
  ASSERT_EQ(permanent_uuid, cmeta.committed_config().peers().begin()->permanent_uuid());
  ASSERT_EQ(term, cmeta.current_term());
}

// Test the basic "happy case" of creating and then loading a file.
TEST_F(ConsensusMetadataTest, TestCreateLoad) {
  // Create the file.
  {
    std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(ConsensusMetadata::Create(
        &fs_manager_, kTabletId, fs_manager_.uuid(), config_, kInitialTerm));
    ASSERT_VALUES_EQUAL(*cmeta, kInvalidOpIdIndex, fs_manager_.uuid(), kInitialTerm);
  }

  // Load the file.
  std::unique_ptr<ConsensusMetadata> cmeta;
  ASSERT_OK(ConsensusMetadata::Load(&fs_manager_, kTabletId, fs_manager_.uuid(), &cmeta));
  ASSERT_VALUES_EQUAL(*cmeta, kInvalidOpIdIndex, fs_manager_.uuid(), kInitialTerm);
}

// Ensure that we get an error when loading a file that doesn't exist.
TEST_F(ConsensusMetadataTest, TestFailedLoad) {
  std::unique_ptr<ConsensusMetadata> cmeta;
  Status s = ConsensusMetadata::Load(&fs_manager_, kTabletId, fs_manager_.uuid(), &cmeta);
  ASSERT_TRUE(s.IsNotFound()) << "Unexpected status: " << s.ToString();
  LOG(INFO) << "Expected failure: " << s.ToString();
}

// Check that changes are not written to disk until Flush() is called.
TEST_F(ConsensusMetadataTest, TestFlush) {
  const int64_t kNewTerm = 4;
  std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(ConsensusMetadata::Create(
      &fs_manager_, kTabletId, fs_manager_.uuid(), config_, kInitialTerm));
  cmeta->set_current_term(kNewTerm);

  // We are sort of "breaking the rules" by having multiple ConsensusMetadata
  // objects in flight that point to the same file, but for a test this is fine
  // since it's read-only.
  {
    std::unique_ptr<ConsensusMetadata> cmeta_read;
    ASSERT_OK(ConsensusMetadata::Load(&fs_manager_, kTabletId, fs_manager_.uuid(), &cmeta_read));
    ASSERT_VALUES_EQUAL(*cmeta_read, kInvalidOpIdIndex, fs_manager_.uuid(), kInitialTerm);
  }

  ASSERT_OK(cmeta->Flush());

  {
    std::unique_ptr<ConsensusMetadata> cmeta_read;
    ASSERT_OK(ConsensusMetadata::Load(&fs_manager_, kTabletId, fs_manager_.uuid(), &cmeta_read));
    ASSERT_VALUES_EQUAL(*cmeta_read, kInvalidOpIdIndex, fs_manager_.uuid(), kNewTerm);
  }
}

// Builds a distributed configuration of voters with the given uuids.
RaftConfigPB BuildConfig(const vector<string>& uuids) {
  RaftConfigPB config;
  for (const string& uuid : uuids) {
    RaftPeerPB* peer = config.add_peers();
    peer->set_permanent_uuid(uuid);
    peer->set_member_type(PeerMemberType::VOTER);
    HostPortToPB(HostPort("255.255.255.255", 0), peer->mutable_last_known_private_addr()->Add());
  }
  return config;
}

// Test ConsensusMetadata active role calculation.
TEST_F(ConsensusMetadataTest, TestActiveRole) {
  vector<string> uuids = { "a", "b", "c", "d" };
  string peer_uuid = "e";
  RaftConfigPB config1 = BuildConfig(uuids); // We aren't a member of this config...
  const auto op_id = OpId(1, 1);
  config1.set_opid_index(op_id.index);

  std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(
      ConsensusMetadata::Create(&fs_manager_, kTabletId, peer_uuid, config1, kInitialTerm));

  // Not a participant.
  ASSERT_EQ(PeerRole::NON_PARTICIPANT, cmeta->active_role());

  // Follower.
  uuids.push_back(peer_uuid);
  RaftConfigPB config2 = BuildConfig(uuids); // But we are a member of this one.
  config2.set_opid_index(op_id.index);
  cmeta->set_committed_config(config2);
  ASSERT_EQ(PeerRole::FOLLOWER, cmeta->active_role());

  // Pending should mask committed.
  cmeta->set_pending_config(config1, op_id);
  ASSERT_EQ(PeerRole::NON_PARTICIPANT, cmeta->active_role());
  cmeta->clear_pending_config();
  ASSERT_EQ(PeerRole::FOLLOWER, cmeta->active_role());

  // Leader.
  cmeta->set_leader_uuid(peer_uuid);
  ASSERT_EQ(PeerRole::LEADER, cmeta->active_role());

  // Again, pending should mask committed.
  cmeta->set_pending_config(config1, op_id);
  ASSERT_EQ(PeerRole::NON_PARTICIPANT, cmeta->active_role());
  cmeta->set_pending_config(config2, op_id); // pending == committed.
  ASSERT_EQ(PeerRole::LEADER, cmeta->active_role());
  cmeta->set_committed_config(config1); // committed now excludes this node, but is masked...
  ASSERT_EQ(PeerRole::LEADER, cmeta->active_role());

  // ... until we clear pending, then we find committed now excludes us.
  cmeta->clear_pending_config();
  ASSERT_EQ(PeerRole::NON_PARTICIPANT, cmeta->active_role());
}

// Ensure that invocations of ToConsensusStatePB() return the expected state
// in the returned object.
TEST_F(ConsensusMetadataTest, TestToConsensusStatePB) {
  vector<string> uuids = { "a", "b", "c", "d" };
  string peer_uuid = "e";

  RaftConfigPB committed_config = BuildConfig(uuids); // We aren't a member of this config...
  committed_config.set_opid_index(1);
  std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(ConsensusMetadata::Create(
      &fs_manager_, kTabletId, peer_uuid, committed_config, kInitialTerm));

  uuids.push_back(peer_uuid);
  RaftConfigPB pending_config = BuildConfig(uuids);

  // Set the pending configuration to be one containing the current leader (who is not
  // in the committed configuration). Ensure that the leader shows up when we ask for
  // the active consensus state.
  const auto op_id = OpId(1, 1);
  cmeta->set_pending_config(pending_config, op_id);
  cmeta->set_leader_uuid(peer_uuid);
  ConsensusStatePB active_cstate = cmeta->ToConsensusStatePB(CONSENSUS_CONFIG_ACTIVE);
  ASSERT_TRUE(active_cstate.has_leader_uuid());
  ASSERT_OK(VerifyConsensusState(active_cstate, UNCOMMITTED_QUORUM));

  // Without changing anything, ask for the committed consensus state.
  // Since the current leader is not a voter in the committed configuration, the
  // returned consensus state should not list a leader.
  ConsensusStatePB committed_cstate = cmeta->ToConsensusStatePB(CONSENSUS_CONFIG_COMMITTED);
  ASSERT_FALSE(committed_cstate.has_leader_uuid());
  ASSERT_OK(VerifyConsensusState(committed_cstate, COMMITTED_QUORUM));

  // Set a new leader to be a member of the committed configuration. Now the committed
  // consensus state should list a leader.
  cmeta->set_leader_uuid("a");
  ConsensusStatePB new_committed_cstate = cmeta->ToConsensusStatePB(CONSENSUS_CONFIG_COMMITTED);
  ASSERT_TRUE(new_committed_cstate.has_leader_uuid());
  ASSERT_OK(VerifyConsensusState(new_committed_cstate, COMMITTED_QUORUM));
}

namespace {

// Helper for TestMergeCommittedConsensusStatePB.
void AssertConsensusMergeExpected(const ConsensusMetadata& cmeta,
                                  const ConsensusStatePB& cstate,
                                  int64_t expected_term,
                                  const string& expected_voted_for) {
  // See header docs for ConsensusMetadata::MergeCommittedConsensusStatePB() for
  // a "spec" of these assertions.
  ASSERT_TRUE(!cmeta.has_pending_config());
  ASSERT_EQ(cmeta.committed_config().ShortDebugString(), cstate.config().ShortDebugString());
  ASSERT_EQ("", cmeta.leader_uuid());
  ASSERT_EQ(expected_term, cmeta.current_term());
  if (expected_voted_for.empty()) {
    ASSERT_FALSE(cmeta.has_voted_for());
  } else {
    ASSERT_EQ(expected_voted_for, cmeta.voted_for());
  }
}

} // namespace

// Ensure that MergeCommittedConsensusStatePB() works as advertised.
TEST_F(ConsensusMetadataTest, TestMergeCommittedConsensusStatePB) {
  vector<string> uuids = { "a", "b", "c", "d" };

  RaftConfigPB committed_config = BuildConfig(uuids); // We aren't a member of this config...
  committed_config.set_opid_index(1);
  std::unique_ptr<ConsensusMetadata> cmeta =
      ASSERT_RESULT(ConsensusMetadata::Create(&fs_manager_, kTabletId, "e", committed_config, 1));

  uuids.push_back("e");
  RaftConfigPB pending_config = BuildConfig(uuids);
  auto op_id = OpId(1, 2);
  cmeta->set_pending_config(pending_config, op_id);
  cmeta->set_leader_uuid("e");
  cmeta->set_voted_for("e");

  // Keep the term and votes because the merged term is lower.
  ConsensusStatePB remote_state;
  remote_state.set_current_term(0);
  *remote_state.mutable_config() = BuildConfig({ "x", "y", "z" });
  cmeta->MergeCommittedConsensusStatePB(remote_state);
  ASSERT_NO_FATALS(AssertConsensusMergeExpected(*cmeta, remote_state, 1, "e"));

  // Same as above because the merged term is the same as the cmeta term.
  remote_state.set_current_term(1);
  *remote_state.mutable_config() = BuildConfig({ "f", "g", "h" });
  cmeta->MergeCommittedConsensusStatePB(remote_state);
  ASSERT_NO_FATALS(AssertConsensusMergeExpected(*cmeta, remote_state, 1, "e"));

  // Higher term, so wipe out the prior state.
  remote_state.set_current_term(2);
  *remote_state.mutable_config() = BuildConfig({ "i", "j", "k" });
  op_id = OpId(2, 3);
  cmeta->set_pending_config(pending_config, op_id);
  cmeta->MergeCommittedConsensusStatePB(remote_state);
  ASSERT_NO_FATALS(AssertConsensusMergeExpected(*cmeta, remote_state, 2, ""));
}

} // namespace consensus
} // namespace yb
