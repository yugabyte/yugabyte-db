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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "yb/consensus/consensus-test-util.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/leader_election.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

namespace rpc {
class Messenger;
} // namespace rpc

namespace consensus {

using std::string;
using std::unordered_map;
using std::vector;
using strings::Substitute;

namespace {

const MonoDelta kLeaderElectionTimeout = MonoDelta::FromSeconds(10);

// Generate list of voter uuids.
static vector<string> GenVoterUUIDs(int num_voters) {
  vector<string> voter_uuids;
  for (int i = 0; i < num_voters; i++) {
    voter_uuids.push_back(Substitute("peer-$0", i));
  }
  return voter_uuids;
}

} // namespace

////////////////////////////////////////
// LeaderElectionTest
////////////////////////////////////////

typedef unordered_map<string, PeerProxy*> ProxyMap;

// A proxy factory that serves proxies from a map.
class FromMapPeerProxyFactory : public PeerProxyFactory {
 public:
  explicit FromMapPeerProxyFactory(const ProxyMap* proxy_map)
      : proxy_map_(proxy_map) {
  }

  ~FromMapPeerProxyFactory() {
    DeleteUnusedPeerProxies();
  }

  PeerProxyPtr NewProxy(const RaftPeerPB& peer_pb) override {
    PeerProxy* proxy_ptr = FindPtrOrNull(*proxy_map_, peer_pb.permanent_uuid());
    if (proxy_ptr == nullptr) {
      return nullptr;
    }
    used_peer_proxy_.insert(peer_pb.permanent_uuid());
    return PeerProxyPtr(proxy_ptr);
  }

  void DeleteUnusedPeerProxies() {
    for (auto item : *proxy_map_) {
      if (used_peer_proxy_.count(item.first) == 0) {
        delete item.second;
      }
    }
    used_peer_proxy_.clear();
  }

  Messenger* messenger() const override {
    return nullptr;
  }

 private:
  // FYI, the tests may add and remove nodes from this map while we hold a
  // reference to it.
  const ProxyMap* const proxy_map_;
  std::set<string> used_peer_proxy_;
};

class LeaderElectionTest : public YBTest {
 public:
  LeaderElectionTest()
    : tablet_id_("test-tablet"),
      proxy_factory_(new FromMapPeerProxyFactory(&proxies_)),
      latch_(1) {
    CHECK_OK(ThreadPoolBuilder("test-peer-pool").set_max_threads(5).Build(&pool_));
  }

  void ElectionCallback(const ElectionResult& result);

 protected:
  void InitUUIDs(int num_voters, int non_voters);
  void InitNoOpPeerProxies(int num_voters,
                           int num_pre_voters,
                           int num_pre_observers,
                           int num_observers);
  void InitDelayableMockedProxies(int num_voters,
                                  int num_pre_voters,
                                  int num_pre_observers,
                                  int num_observers,
                                  bool enable_delay);
  std::unique_ptr<VoteCounter> InitVoteCounter(int num_voters, int majority_size);

  // Voter 0 is the high-term voter.
  LeaderElectionPtr SetUpElectionWithHighTermVoter(ConsensusTerm election_term);

  // Predetermine the election results using the specified number of
  // grant / deny / error responses.
  // num_grant must be at least 1, for the candidate to vote for itself.
  // num_grant + num_deny + num_error must add up to an odd number.
  LeaderElectionPtr SetUpElectionWithGrantDenyErrorVotes(ConsensusTerm election_term,
                                                                     int num_grant,
                                                                     int num_deny,
                                                                     int num_error);

  const string tablet_id_;
  string candidate_uuid_;
  vector<string> voter_uuids_;

  RaftConfigPB config_;
  ProxyMap proxies_;
  std::unique_ptr<PeerProxyFactory> proxy_factory_;
  std::unique_ptr<ThreadPool> pool_;

  CountDownLatch latch_;
  std::unique_ptr<ElectionResult> result_;
};

void LeaderElectionTest::ElectionCallback(const ElectionResult& result) {
  result_.reset(new ElectionResult(result));
  latch_.CountDown();
}

void LeaderElectionTest::InitUUIDs(int num_voters, int non_voters) {
  ASSERT_GT(num_voters, 0);
  voter_uuids_ = GenVoterUUIDs(num_voters + non_voters);
  candidate_uuid_ = voter_uuids_[0];
  voter_uuids_.erase(voter_uuids_.begin());
}

void LeaderElectionTest::InitNoOpPeerProxies(int num_voters,
                                             int num_pre_voters,
                                             int num_pre_observers,
                                             int num_observers) {
  ASSERT_GT(num_voters, 0);
  ASSERT_GE(num_pre_voters, 0);
  ASSERT_GE(num_pre_observers, 0);
  ASSERT_GE(num_observers, 0);
  config_.Clear();

  // Remove candidate_uuid_.
  num_voters--;
  ASSERT_EQ(voter_uuids_.size(), num_voters + num_pre_voters + num_pre_observers + num_observers);
  for (const string& uuid : voter_uuids_) {
    RaftPeerPB* peer_pb = config_.add_peers();
    PeerMemberType member_type;
    if (num_voters > 0) {
      member_type = PeerMemberType::VOTER;
      num_voters--;
    } else if (num_pre_voters > 0) {
      member_type = PeerMemberType::PRE_VOTER;
      num_pre_voters--;
    } else if (num_pre_observers > 0) {
      member_type = PeerMemberType::PRE_OBSERVER;
      num_pre_observers--;
    } else if (num_observers > 0) {
      member_type = PeerMemberType::OBSERVER;
      num_observers--;
    } else {
      member_type = PeerMemberType::UNKNOWN_MEMBER_TYPE;
      LOG(FATAL) << "Invalid member type";
    }
    peer_pb->set_member_type(member_type);
    peer_pb->set_permanent_uuid(uuid);
    PeerProxy* proxy = new NoOpTestPeerProxy(pool_.get(), *peer_pb);
    InsertOrDie(&proxies_, uuid, proxy);
  }
}

void LeaderElectionTest::InitDelayableMockedProxies(int num_voters,
                                                    int num_pre_voters,
                                                    int num_pre_observers,
                                                    int num_observers,
                                                    bool enable_delay) {
  ASSERT_GT(num_voters, 0);
  ASSERT_GE(num_pre_voters, 0);
  ASSERT_GE(num_pre_observers, 0);
  ASSERT_GE(num_observers, 0);
  config_.Clear();

  // Remove candidate_uuid_.
  num_voters--;
  ASSERT_EQ(voter_uuids_.size(), num_voters + num_pre_voters + num_pre_observers + num_observers);
  for (const string& uuid : voter_uuids_) {
    RaftPeerPB* peer_pb = config_.add_peers();
    PeerMemberType member_type;
    if (num_voters > 0) {
      member_type = PeerMemberType::VOTER;
      num_voters--;
    } else if (num_pre_voters > 0) {
      member_type = PeerMemberType::PRE_VOTER;
      num_pre_voters--;
    } else if (num_pre_observers >0) {
      member_type = PeerMemberType::PRE_OBSERVER;
      num_pre_observers--;
    } else if (num_observers > 0) {
      member_type = PeerMemberType::OBSERVER;
      num_observers--;
    } else {
      member_type = PeerMemberType::UNKNOWN_MEMBER_TYPE;
      LOG(FATAL) << "Invalid member type";
    }
    peer_pb->set_member_type(member_type);
    peer_pb->set_permanent_uuid(uuid);
    auto proxy = new DelayablePeerProxy<MockedPeerProxy>(pool_.get(),
                                                         new MockedPeerProxy(pool_.get()));
    if (enable_delay) {
      proxy->DelayResponse();
    }
    InsertOrDie(&proxies_, uuid, proxy);
  }
}

std::unique_ptr<VoteCounter> LeaderElectionTest::InitVoteCounter(
    int num_voters, int majority_size) {
  auto counter = std::make_unique<VoteCounter>(num_voters, majority_size);
  bool duplicate;
  CHECK_OK(counter->RegisterVote(candidate_uuid_, ElectionVote::kGranted, &duplicate));
  CHECK(!duplicate);
  return counter;
}

LeaderElectionPtr LeaderElectionTest::SetUpElectionWithHighTermVoter(ConsensusTerm election_term) {
  const int kNumVoters = 3;
  const int kMajoritySize = 2;

  InitUUIDs(kNumVoters, 0);
  InitDelayableMockedProxies(kNumVoters, 0, 0, 0, true);
  auto counter = InitVoteCounter(kNumVoters, kMajoritySize);

  VoteResponsePB response;
  response.set_responder_uuid(voter_uuids_[0]);
  response.set_responder_term(election_term + 1);
  response.set_vote_granted(false);
  response.mutable_consensus_error()->set_code(ConsensusErrorPB::INVALID_TERM);
  StatusToPB(STATUS(InvalidArgument, "Bad term"),
      response.mutable_consensus_error()->mutable_status());
  down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[0]])
      ->proxy()->set_vote_response(response);

  response.Clear();
  response.set_responder_uuid(voter_uuids_[1]);
  response.set_responder_term(election_term);
  response.set_vote_granted(true);
  down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[1]])
      ->proxy()->set_vote_response(response);

  VoteRequestPB request;
  request.set_candidate_uuid(candidate_uuid_);
  request.set_candidate_term(election_term);
  request.set_tablet_id(tablet_id_);

  return make_scoped_refptr<LeaderElection>(
      config_, proxy_factory_.get(), request, std::move(counter), kLeaderElectionTimeout,
      PreElection::kFalse, TEST_SuppressVoteRequest::kFalse,
      std::bind(&LeaderElectionTest::ElectionCallback, this, std::placeholders::_1));
}

LeaderElectionPtr LeaderElectionTest::SetUpElectionWithGrantDenyErrorVotes(
    ConsensusTerm election_term, int num_grant, int num_deny, int num_error) {
  const int kNumVoters = num_grant + num_deny + num_error;
  CHECK_GE(num_grant, 1);       // Gotta vote for yourself.
  CHECK_EQ(1, kNumVoters % 2);  // RaftConfig size must be odd.
  const int kMajoritySize = (kNumVoters / 2) + 1;

  InitUUIDs(kNumVoters, 0);
  InitDelayableMockedProxies(kNumVoters, 0, 0, 0, false); // Don't delay the vote responses.
  auto counter = InitVoteCounter(kNumVoters, kMajoritySize);
  int num_grant_followers = num_grant - 1;

  // Set up mocked responses based on the params specified in the method arguments.
  size_t voter_index = 0;
  while (voter_index < voter_uuids_.size()) {
    VoteResponsePB response;
    if (num_grant_followers > 0) {
      response.set_responder_uuid(voter_uuids_[voter_index]);
      response.set_responder_term(election_term);
      response.set_vote_granted(true);
      --num_grant_followers;
    } else if (num_deny > 0) {
      response.set_responder_uuid(voter_uuids_[voter_index]);
      response.set_responder_term(election_term);
      response.set_vote_granted(false);
      response.mutable_consensus_error()->set_code(ConsensusErrorPB::LAST_OPID_TOO_OLD);
      StatusToPB(STATUS(InvalidArgument, "Last OpId"),
          response.mutable_consensus_error()->mutable_status());
      --num_deny;
    } else if (num_error > 0) {
      response.mutable_error()->set_code(tserver::TabletServerErrorPB::TABLET_NOT_FOUND);
      StatusToPB(STATUS(NotFound, "Unknown Tablet"),
          response.mutable_error()->mutable_status());
      --num_error;
    } else {
      LOG(FATAL) << "Unexpected fallthrough";
    }

    down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[voter_index]])
        ->proxy()->set_vote_response(response);
    ++voter_index;
  }

  VoteRequestPB request;
  request.set_candidate_uuid(candidate_uuid_);
  request.set_candidate_term(election_term);
  request.set_tablet_id(tablet_id_);

  return make_scoped_refptr<LeaderElection>(
      config_, proxy_factory_.get(), request, std::move(counter), kLeaderElectionTimeout,
      PreElection::kFalse, TEST_SuppressVoteRequest::kFalse,
      std::bind(&LeaderElectionTest::ElectionCallback, this, std::placeholders::_1));
}

// All peers respond "yes", no failures.
TEST_F(LeaderElectionTest, TestPerfectElection) {
  // Try configuration sizes of 1, 3, 5.
  const vector<int> pre_voters_config_sizes = { 0, 1, 3, 5 };
  const vector<int> pre_observers_config_sizes = { 0, 1, 3, 5 };
  const vector<int> observers_config_sizes = { 0, 1, 3, 5 };
  const vector<int> voters_config_sizes = { 1, 3, 5 };

  for (int num_pre_voters : pre_voters_config_sizes) {
    for (int num_pre_observers : pre_observers_config_sizes) {
      for (int num_observers : observers_config_sizes) {
        for (int num_voters : voters_config_sizes) {
          LOG(INFO) << "Testing election with config { voters: " << num_voters
                    << ", pre_voters: " << num_pre_voters
                    << ", pre_observers: " << num_pre_observers
                    << ", observers: " << num_observers << " }";
          int majority_size = (num_voters / 2) + 1;
          ConsensusTerm election_term = 10 + num_voters; // Just to be able to differentiate.

          InitUUIDs(num_voters, num_pre_voters + num_pre_observers + num_observers);
          InitNoOpPeerProxies(num_voters, num_pre_voters, num_pre_observers, num_observers);
          auto counter = InitVoteCounter(num_voters, majority_size);

          VoteRequestPB request;
          LOG(INFO) << "candidate_uuid_: " << candidate_uuid_;
          request.set_candidate_uuid(candidate_uuid_);
          request.set_candidate_term(election_term);
          request.set_tablet_id(tablet_id_);

          auto election = make_scoped_refptr<LeaderElection>(
              config_, proxy_factory_.get(), request, std::move(counter), kLeaderElectionTimeout,
              PreElection::kFalse, TEST_SuppressVoteRequest::kFalse,
              std::bind(&LeaderElectionTest::ElectionCallback, this, std::placeholders::_1));
          election->Run();
          latch_.Wait();

          ASSERT_EQ(election_term, result_->election_term);
          ASSERT_EQ(ElectionVote::kGranted, result_->decision);

          pool_->Wait();
          FromMapPeerProxyFactory *from_map_proxy_factory =
              down_cast<FromMapPeerProxyFactory*>(proxy_factory_.get());
          from_map_proxy_factory->DeleteUnusedPeerProxies();
          proxies_.clear(); // We don't delete them; The election VoterState object
          // ends up owning them.
          latch_.Reset(1);
        }
      }
    }
  }
}

// Test leader election when we encounter a peer with a higher term before we
// have arrived at a majority decision.
TEST_F(LeaderElectionTest, TestHigherTermBeforeDecision) {
  const ConsensusTerm kElectionTerm = 2;
  auto election = SetUpElectionWithHighTermVoter(kElectionTerm);
  election->Run();

  // This guy has a higher term.
  down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[0]])
      ->Respond(TestPeerProxy::Method::kRequestVote);
  latch_.Wait();

  ASSERT_EQ(kElectionTerm, result_->election_term);
  ASSERT_EQ(ElectionVote::kDenied, result_->decision);
  ASSERT_TRUE(result_->higher_term);
  ASSERT_EQ(kElectionTerm + 1, *result_->higher_term);
  LOG(INFO) << "Election lost. Reason: " << result_->message;

  // This guy will vote "yes".
  down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[1]])
      ->Respond(TestPeerProxy::Method::kRequestVote);

  pool_->Wait(); // Wait for the election callbacks to finish before we destroy proxies.
}

// Test leader election when we encounter a peer with a higher term after we
// have arrived at a majority decision of "yes".
TEST_F(LeaderElectionTest, TestHigherTermAfterDecision) {
  const ConsensusTerm kElectionTerm = 2;
  auto election = SetUpElectionWithHighTermVoter(kElectionTerm);
  election->Run();

  // This guy will vote "yes".
  down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[1]])
      ->Respond(TestPeerProxy::Method::kRequestVote);
  latch_.Wait();

  ASSERT_EQ(kElectionTerm, result_->election_term);
  ASSERT_EQ(ElectionVote::kGranted, result_->decision);
  ASSERT_FALSE(result_->higher_term);
  ASSERT_TRUE(result_->message.empty());
  LOG(INFO) << "Election won.";

  // This guy has a higher term.
  down_cast<DelayablePeerProxy<MockedPeerProxy>*>(proxies_[voter_uuids_[0]])
      ->Respond(TestPeerProxy::Method::kRequestVote);

  pool_->Wait(); // Wait for the election callbacks to finish before we destroy proxies.
}

// Out-of-date OpId "vote denied" case.
TEST_F(LeaderElectionTest, TestWithDenyVotes) {
  const ConsensusTerm kElectionTerm = 2;
  const int kNumGrant = 2;
  const int kNumDeny = 3;
  const int kNumError = 0;
  auto election = SetUpElectionWithGrantDenyErrorVotes(
      kElectionTerm, kNumGrant, kNumDeny, kNumError);
  LOG(INFO) << "Running";
  election->Run();

  latch_.Wait();
  ASSERT_EQ(kElectionTerm, result_->election_term);
  ASSERT_EQ(ElectionVote::kDenied, result_->decision);
  ASSERT_FALSE(result_->higher_term);
  ASSERT_TRUE(result_->message.empty());
  LOG(INFO) << "Election denied.";

  pool_->Wait(); // Wait for the election callbacks to finish before we destroy proxies.
}

// Count errors as denied votes.
TEST_F(LeaderElectionTest, TestWithErrorVotes) {
  const ConsensusTerm kElectionTerm = 2;
  const int kNumGrant = 1;
  const int kNumDeny = 0;
  const int kNumError = 4;
  auto election = SetUpElectionWithGrantDenyErrorVotes(
      kElectionTerm, kNumGrant, kNumDeny, kNumError);
  election->Run();

  latch_.Wait();
  ASSERT_EQ(kElectionTerm, result_->election_term);
  ASSERT_EQ(ElectionVote::kDenied, result_->decision);
  ASSERT_FALSE(result_->higher_term);
  ASSERT_TRUE(result_->message.empty());
  LOG(INFO) << "Election denied.";

  pool_->Wait(); // Wait for the election callbacks to finish before we destroy proxies.
}

////////////////////////////////////////
// VoteCounterTest
////////////////////////////////////////

class VoteCounterTest : public YBTest {
 protected:
  static void AssertUndecided(const VoteCounter& counter);
  static void AssertVoteCount(const VoteCounter& counter, int yes_votes, int no_votes);
};

void VoteCounterTest::AssertUndecided(const VoteCounter& counter) {
  ElectionVote decision = counter.GetDecision();
  ASSERT_EQ(decision, ElectionVote::kUnknown);
}

void VoteCounterTest::AssertVoteCount(const VoteCounter& counter, int yes_votes, int no_votes) {
  ASSERT_EQ(yes_votes, counter.yes_votes_);
  ASSERT_EQ(no_votes, counter.no_votes_);
  ASSERT_EQ(yes_votes + no_votes, counter.GetTotalVotesCounted());
}

// Test basic vote counting functionality with an early majority.
TEST_F(VoteCounterTest, TestVoteCounter_EarlyDecision) {
  const int kNumVoters = 3;
  const int kMajoritySize = 2;
  vector<string> voter_uuids = GenVoterUUIDs(kNumVoters);

  // "Yes" decision.
  {
    // Start off undecided.
    VoteCounter counter(kNumVoters, kMajoritySize);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // First yes vote.
    bool duplicate;
    ASSERT_OK(counter.RegisterVote(voter_uuids[0], ElectionVote::kGranted, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 1, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // Second yes vote wins it in a configuration of 3.
    ASSERT_OK(counter.RegisterVote(voter_uuids[1], ElectionVote::kGranted, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_EQ(counter.GetDecision(), ElectionVote::kGranted);
    ASSERT_NO_FATALS(AssertVoteCount(counter, 2, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());
  }

  // "No" decision.
  {
    // Start off undecided.
    VoteCounter counter(kNumVoters, kMajoritySize);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // First no vote.
    bool duplicate;
    ASSERT_OK(counter.RegisterVote(voter_uuids[0], ElectionVote::kDenied, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 1));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // Second no vote loses it in a configuration of 3.
    ASSERT_OK(counter.RegisterVote(voter_uuids[1], ElectionVote::kDenied, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_EQ(counter.GetDecision(), ElectionVote::kDenied);
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 2));
    ASSERT_FALSE(counter.AreAllVotesIn());
  }
}

// Test basic vote counting functionality with the last vote being the deciding vote.
TEST_F(VoteCounterTest, TestVoteCounter_LateDecision) {
  const int kNumVoters = 5;
  const int kMajoritySize = 3;
  vector<string> voter_uuids = GenVoterUUIDs(kNumVoters);

  // Start off undecided.
  VoteCounter counter(kNumVoters, kMajoritySize);
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 0));
  ASSERT_FALSE(counter.AreAllVotesIn());

  // Add single yes vote, still undecided.
  bool duplicate;
  ASSERT_OK(counter.RegisterVote(voter_uuids[0], ElectionVote::kGranted, &duplicate));
  ASSERT_FALSE(duplicate);
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 1, 0));
  ASSERT_FALSE(counter.AreAllVotesIn());

  // Attempt duplicate vote.
  ASSERT_OK(counter.RegisterVote(voter_uuids[0], ElectionVote::kGranted, &duplicate));
  ASSERT_TRUE(duplicate);
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 1, 0));
  ASSERT_FALSE(counter.AreAllVotesIn());

  // Attempt to change vote.
  Status s = counter.RegisterVote(voter_uuids[0], ElectionVote::kDenied, &duplicate);
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.ToString(), "voted a different way twice");
  LOG(INFO) << "Expected vote-changed error: " << s.ToString();
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 1, 0));
  ASSERT_FALSE(counter.AreAllVotesIn());

  // Add more votes...
  ASSERT_OK(counter.RegisterVote(voter_uuids[1], ElectionVote::kDenied, &duplicate));
  ASSERT_FALSE(duplicate);
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 1, 1));
  ASSERT_FALSE(counter.AreAllVotesIn());

  ASSERT_OK(counter.RegisterVote(voter_uuids[2], ElectionVote::kGranted, &duplicate));
  ASSERT_FALSE(duplicate);
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 2, 1));
  ASSERT_FALSE(counter.AreAllVotesIn());

  ASSERT_OK(counter.RegisterVote(voter_uuids[3], ElectionVote::kDenied, &duplicate));
  ASSERT_FALSE(duplicate);
  ASSERT_NO_FATALS(AssertUndecided(counter));
  ASSERT_NO_FATALS(AssertVoteCount(counter, 2, 2));
  ASSERT_FALSE(counter.AreAllVotesIn());

  // Win the election.
  ASSERT_OK(counter.RegisterVote(voter_uuids[4], ElectionVote::kGranted, &duplicate));
  ASSERT_FALSE(duplicate);
  ASSERT_EQ(counter.GetDecision(), ElectionVote::kGranted);
  ASSERT_NO_FATALS(AssertVoteCount(counter, 3, 2));
  ASSERT_TRUE(counter.AreAllVotesIn());

  // Attempt to vote with > the whole configuration.
  s = counter.RegisterVote("some-random-node", ElectionVote::kGranted, &duplicate);
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.ToString(), "cause the number of votes to exceed the expected number");
  LOG(INFO) << "Expected voters-exceeded error: " << s.ToString();
  ASSERT_NE(counter.GetDecision(), ElectionVote::kUnknown);
  ASSERT_NO_FATALS(AssertVoteCount(counter, 3, 2));
  ASSERT_TRUE(counter.AreAllVotesIn());
}

// Test vote counting with an even number of voters.
TEST_F(VoteCounterTest, TestVoteCounter_EvenVoters) {
  const int kNumVoters = 2;
  const int kMajoritySize = 2;
  vector<string> voter_uuids = GenVoterUUIDs(kNumVoters);

  // "Yes" decision.
  {
    VoteCounter counter(kNumVoters, kMajoritySize);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // Initial yes vote.
    bool duplicate;
    ASSERT_OK(counter.RegisterVote(voter_uuids[0], ElectionVote::kGranted, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 1, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // Second yes vote wins it.
    ASSERT_OK(counter.RegisterVote(voter_uuids[1], ElectionVote::kGranted, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_EQ(counter.GetDecision(), ElectionVote::kGranted);
    ASSERT_NO_FATALS(AssertVoteCount(counter, 2, 0));
    ASSERT_TRUE(counter.AreAllVotesIn());
  }

  // "No" decision.
  {
    VoteCounter counter(kNumVoters, kMajoritySize);
    ASSERT_NO_FATALS(AssertUndecided(counter));
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 0));
    ASSERT_FALSE(counter.AreAllVotesIn());

    // The first "no" vote guarantees a failed election when num voters == 2.
    bool duplicate;
    ASSERT_OK(counter.RegisterVote(voter_uuids[0], ElectionVote::kDenied, &duplicate));
    ASSERT_FALSE(duplicate);
    ASSERT_EQ(counter.GetDecision(), ElectionVote::kDenied);
    ASSERT_NO_FATALS(AssertVoteCount(counter, 0, 1));
    ASSERT_FALSE(counter.AreAllVotesIn());
  }
}

}  // namespace consensus
}  // namespace yb
