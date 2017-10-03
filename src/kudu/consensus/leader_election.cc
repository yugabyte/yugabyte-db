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

#include "kudu/consensus/leader_election.h"

#include <boost/bind.hpp>

#include "kudu/consensus/consensus_peers.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/gutil/bind.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/port.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/util/logging.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/status.h"

namespace kudu {
namespace consensus {

using std::string;
using strings::Substitute;

///////////////////////////////////////////////////
// VoteCounter
///////////////////////////////////////////////////

VoteCounter::VoteCounter(int num_voters, int majority_size)
  : num_voters_(num_voters),
    majority_size_(majority_size),
    yes_votes_(0),
    no_votes_(0) {
  CHECK_LE(majority_size, num_voters);
  CHECK_GT(num_voters_, 0);
  CHECK_GT(majority_size_, 0);
}

Status VoteCounter::RegisterVote(const std::string& voter_uuid, ElectionVote vote,
                                 bool* is_duplicate) {
  // Handle repeated votes.
  if (PREDICT_FALSE(ContainsKey(votes_, voter_uuid))) {
    // Detect changed votes.
    ElectionVote prior_vote = votes_[voter_uuid];
    if (PREDICT_FALSE(prior_vote != vote)) {
      string msg = Substitute("Peer $0 voted a different way twice in the same election. "
                              "First vote: $1, second vote: $2.",
                              voter_uuid, prior_vote, vote);
      return Status::InvalidArgument(msg);
    }

    // This was just a duplicate. Allow the caller to log it but don't change
    // the voting record.
    *is_duplicate = true;
    return Status::OK();
  }

  // Sanity check to ensure we did not exceed the allowed number of voters.
  if (PREDICT_FALSE(yes_votes_ + no_votes_ == num_voters_)) {
    // More unique voters than allowed!
    return Status::InvalidArgument(Substitute(
        "Vote from peer $0 would cause the number of votes to exceed the expected number of "
        "voters, which is $1. Votes already received from the following peers: {$2}",
        voter_uuid,
        num_voters_,
        JoinKeysIterator(votes_.begin(), votes_.end(), ", ")));
  }

  // This is a valid vote, so store it.
  InsertOrDie(&votes_, voter_uuid, vote);
  switch (vote) {
    case VOTE_GRANTED:
      ++yes_votes_;
      break;
    case VOTE_DENIED:
      ++no_votes_;
      break;
  }
  *is_duplicate = false;
  return Status::OK();
}

bool VoteCounter::IsDecided() const {
  return yes_votes_ >= majority_size_ ||
         no_votes_ > num_voters_ - majority_size_;
}

Status VoteCounter::GetDecision(ElectionVote* decision) const {
  if (yes_votes_ >= majority_size_) {
    *decision = VOTE_GRANTED;
    return Status::OK();
  }
  if (no_votes_ > num_voters_ - majority_size_) {
    *decision = VOTE_DENIED;
    return Status::OK();
  }
  return Status::IllegalState("Vote not yet decided");
}

int VoteCounter::GetTotalVotesCounted() const {
  return yes_votes_ + no_votes_;
}

bool VoteCounter::AreAllVotesIn() const {
  return GetTotalVotesCounted() == num_voters_;
}

///////////////////////////////////////////////////
// ElectionResult
///////////////////////////////////////////////////

ElectionResult::ElectionResult(ConsensusTerm election_term, ElectionVote decision)
  : election_term(election_term),
    decision(decision),
    has_higher_term(false),
    higher_term(kMinimumTerm) {
}

ElectionResult::ElectionResult(ConsensusTerm election_term, ElectionVote decision,
                               ConsensusTerm higher_term, const std::string& message)
  : election_term(election_term),
    decision(decision),
    has_higher_term(true),
    higher_term(higher_term),
    message(message) {
  CHECK_EQ(VOTE_DENIED, decision);
  CHECK_GT(higher_term, election_term);
  DCHECK(!message.empty());
}

///////////////////////////////////////////////////
// LeaderElection
///////////////////////////////////////////////////

LeaderElection::LeaderElection(const RaftConfigPB& config,
                               PeerProxyFactory* proxy_factory,
                               const VoteRequestPB& request,
                               gscoped_ptr<VoteCounter> vote_counter,
                               MonoDelta timeout,
                               ElectionDecisionCallback decision_callback)
    : has_responded_(false),
      request_(request),
      vote_counter_(vote_counter.Pass()),
      timeout_(std::move(timeout)),
      decision_callback_(std::move(decision_callback)) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (request.candidate_uuid() == peer.permanent_uuid()) continue;
    follower_uuids_.push_back(peer.permanent_uuid());

    gscoped_ptr<VoterState> state(new VoterState());
    state->proxy_status = proxy_factory->NewProxy(peer, &state->proxy);
    InsertOrDie(&voter_state_, peer.permanent_uuid(), state.release());
  }

  // Ensure that the candidate has already voted for itself.
  CHECK_EQ(1, vote_counter_->GetTotalVotesCounted()) << "Candidate must vote for itself first";

  // Ensure that existing votes + future votes add up to the expected total.
  CHECK_EQ(vote_counter_->GetTotalVotesCounted() + follower_uuids_.size(),
           vote_counter_->GetTotalExpectedVotes())
      << "Expected different number of followers. Follower UUIDs: ["
      << JoinStringsIterator(follower_uuids_.begin(), follower_uuids_.end(), ", ")
      << "]; RaftConfig: {" << config.ShortDebugString() << "}";
}

LeaderElection::~LeaderElection() {
  lock_guard<Lock> guard(&lock_);
  DCHECK(has_responded_); // We must always call the callback exactly once.
  STLDeleteValues(&voter_state_);
}

void LeaderElection::Run() {
  VLOG_WITH_PREFIX(1) << "Running leader election.";

  // Check if we have already won the election (relevant if this is a
  // single-node configuration, since we always pre-vote for ourselves).
  CheckForDecision();

  // The rest of the code below is for a typical multi-node configuration.
  for (const std::string& voter_uuid : follower_uuids_) {
    VoterState* state = nullptr;
    {
      lock_guard<Lock> guard(&lock_);
      state = FindOrDie(voter_state_, voter_uuid);
      // Safe to drop the lock because voter_state_ is not mutated outside of
      // the constructor / destructor. We do this to avoid deadlocks below.
    }

    // If we failed to construct the proxy, just record a 'NO' vote with the status
    // that indicates why it failed.
    if (!state->proxy_status.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Was unable to construct an RPC proxy to peer "
                               << voter_uuid << ": " << state->proxy_status.ToString()
                               << ". Counting it as a 'NO' vote.";
      {
        lock_guard<Lock> guard(&lock_);
        RecordVoteUnlocked(voter_uuid, VOTE_DENIED);
      }
      CheckForDecision();
      continue;
    }

    // Send the RPC request.
    LOG_WITH_PREFIX(INFO) << "Requesting vote from peer " << voter_uuid;
    state->rpc.set_timeout(timeout_);

    state->request = request_;
    state->request.set_dest_uuid(voter_uuid);

    state->proxy->RequestConsensusVoteAsync(
        &state->request,
        &state->response,
        &state->rpc,
        // We use gutil Bind() for the refcounting and boost::bind to adapt the
        // gutil Callback to a thunk.
        boost::bind(&Closure::Run,
                    Bind(&LeaderElection::VoteResponseRpcCallback, this, voter_uuid)));
  }
}

void LeaderElection::CheckForDecision() {
  bool to_respond = false;
  {
    lock_guard<Lock> guard(&lock_);
    // Check if the vote has been newly decided.
    if (!result_ && vote_counter_->IsDecided()) {
      ElectionVote decision;
      CHECK_OK(vote_counter_->GetDecision(&decision));
      LOG_WITH_PREFIX(INFO) << "Election decided. Result: candidate "
                << ((decision == VOTE_GRANTED) ? "won." : "lost.");
      result_.reset(new ElectionResult(election_term(), decision));
    }
    // Check whether to respond. This can happen as a result of either getting
    // a majority vote or of something invalidating the election, like
    // observing a higher term.
    if (result_ && !has_responded_) {
      has_responded_ = true;
      to_respond = true;
    }
  }

  // Respond outside of the lock.
  if (to_respond) {
    // This is thread-safe since result_ is write-once.
    decision_callback_.Run(*result_);
  }
}

void LeaderElection::VoteResponseRpcCallback(const std::string& voter_uuid) {
  {
    lock_guard<Lock> guard(&lock_);
    VoterState* state = FindOrDie(voter_state_, voter_uuid);

    // Check for RPC errors.
    if (!state->rpc.status().ok()) {
      LOG_WITH_PREFIX(WARNING) << "RPC error from VoteRequest() call to peer " << voter_uuid
                  << ": " << state->rpc.status().ToString();
      RecordVoteUnlocked(voter_uuid, VOTE_DENIED);

    // Check for tablet errors.
    } else if (state->response.has_error()) {
      LOG_WITH_PREFIX(WARNING) << "Tablet error from VoteRequest() call to peer "
                   << voter_uuid << ": "
                   << StatusFromPB(state->response.error().status()).ToString();
      RecordVoteUnlocked(voter_uuid, VOTE_DENIED);

    // If the peer changed their IP address, we shouldn't count this vote since
    // our knowledge of the configuration is in an inconsistent state.
    } else if (PREDICT_FALSE(voter_uuid != state->response.responder_uuid())) {
      LOG_WITH_PREFIX(DFATAL) << "Received vote response from peer we thought had UUID "
                  << voter_uuid << ", but its actual UUID is " << state->response.responder_uuid();
      RecordVoteUnlocked(voter_uuid, VOTE_DENIED);

    // Count the granted votes.
    } else if (state->response.vote_granted()) {
      HandleVoteGrantedUnlocked(voter_uuid, *state);

    // Anything else is a denied vote.
    } else {
      HandleVoteDeniedUnlocked(voter_uuid, *state);
    }
  }

  // Check for a decision outside the lock.
  CheckForDecision();
}

void LeaderElection::RecordVoteUnlocked(const std::string& voter_uuid, ElectionVote vote) {
  DCHECK(lock_.is_locked());

  // Record the vote.
  bool duplicate;
  Status s = vote_counter_->RegisterVote(voter_uuid, vote, &duplicate);
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Error registering vote for peer " << voter_uuid
                 << ": " << s.ToString();
    return;
  }
  if (duplicate) {
    // Note: This is DFATAL because at the time of writing we do not support
    // retrying vote requests, so this should be impossible. It may be valid to
    // receive duplicate votes in the future if we implement retry.
    LOG_WITH_PREFIX(DFATAL) << "Duplicate vote received from peer " << voter_uuid;
  }
}

void LeaderElection::HandleHigherTermUnlocked(const string& voter_uuid, const VoterState& state) {
  DCHECK(lock_.is_locked());
  DCHECK_GT(state.response.responder_term(), election_term());

  string msg = Substitute("Vote denied by peer $0 with higher term. Message: $1",
                          state.response.responder_uuid(),
                          StatusFromPB(state.response.consensus_error().status()).ToString());
  LOG_WITH_PREFIX(WARNING) << msg;

  if (!result_) {
    LOG_WITH_PREFIX(INFO) << "Cancelling election due to peer responding with higher term";
    result_.reset(new ElectionResult(election_term(), VOTE_DENIED,
                                     state.response.responder_term(), msg));
  }
}

void LeaderElection::HandleVoteGrantedUnlocked(const string& voter_uuid, const VoterState& state) {
  DCHECK(lock_.is_locked());
  DCHECK_EQ(state.response.responder_term(), election_term());
  DCHECK(state.response.vote_granted());

  LOG_WITH_PREFIX(INFO) << "Vote granted by peer " << voter_uuid;
  RecordVoteUnlocked(voter_uuid, VOTE_GRANTED);
}

void LeaderElection::HandleVoteDeniedUnlocked(const string& voter_uuid, const VoterState& state) {
  DCHECK(lock_.is_locked());
  DCHECK(!state.response.vote_granted());

  // If one of the voters responds with a greater term than our own, and we
  // have not yet triggered the decision callback, it cancels the election.
  if (state.response.responder_term() > election_term()) {
    return HandleHigherTermUnlocked(voter_uuid, state);
  }

  LOG_WITH_PREFIX(INFO) << "Vote denied by peer " << voter_uuid << ". Message: "
            << StatusFromPB(state.response.consensus_error().status()).ToString();
  RecordVoteUnlocked(voter_uuid, VOTE_DENIED);
}

std::string LeaderElection::LogPrefix() const {
  return Substitute("T $0 P $1 [CANDIDATE]: Term $2 election: ",
                    request_.tablet_id(),
                    request_.candidate_uuid(),
                    request_.candidate_term());
}

} // namespace consensus
} // namespace kudu
