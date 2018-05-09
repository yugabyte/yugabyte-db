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

#include "yb/consensus/leader_election.h"

#include <mutex>
#include <functional>

#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/port.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/common/wire_protocol.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"

using namespace std::literals;

namespace yb {
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
      return STATUS(InvalidArgument, msg);
    }

    // This was just a duplicate. Allow the caller to log it but don't change
    // the voting record.
    *is_duplicate = true;
    return Status::OK();
  }

  // Sanity check to ensure we did not exceed the allowed number of voters.
  if (PREDICT_FALSE(yes_votes_ + no_votes_ == num_voters_)) {
    // More unique voters than allowed!
    return STATUS(InvalidArgument, Substitute(
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
  return STATUS(IllegalState, "Vote not yet decided");
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

ElectionResult::ElectionResult(ConsensusTerm election_term_,
                               ElectionVote decision_,
                               MonoTime old_leader_lease_expiration_,
                               MicrosTime old_leader_ht_lease_expiration_)
  : election_term(election_term_),
    decision(decision_),
    has_higher_term(false),
    higher_term(kMinimumTerm),
    old_leader_lease_expiration(old_leader_lease_expiration_),
    old_leader_ht_lease_expiration(old_leader_ht_lease_expiration_) {
}

ElectionResult::ElectionResult(ConsensusTerm election_term,
                               ElectionVote decision,
                               ConsensusTerm higher_term,
                               const std::string& message)
  : election_term(election_term),
    decision(decision),
    has_higher_term(true),
    higher_term(higher_term),
    message(message),
    old_leader_ht_lease_expiration(HybridTime::kMin.GetPhysicalValueMicros()) {
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
                               std::unique_ptr<VoteCounter> vote_counter,
                               MonoDelta timeout,
                               TEST_SuppressVoteRequest suppress_vote_request,
                               ElectionDecisionCallback decision_callback)
    : request_(request),
      vote_counter_(std::move(vote_counter)),
      timeout_(timeout),
      suppress_vote_request_(suppress_vote_request),
      decision_callback_(std::move(decision_callback)) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (request.candidate_uuid() == peer.permanent_uuid()) continue;
    // Only peers with member_type == VOTER are allowed to vote.
    if (peer.member_type() != RaftPeerPB::VOTER) {
      LOG(INFO) << "Ignoring peer " << peer.permanent_uuid() << " vote because its member type is "
                << RaftPeerPB::MemberType_Name(peer.member_type());
      continue;
    }

    voting_follower_uuids_.push_back(peer.permanent_uuid());

    auto state = std::make_unique<VoterState>();
    state->proxy_future = proxy_factory->NewProxyFuture(peer);
    state->address = HostPortFromPB(peer.last_known_addr());
    CHECK(voter_state_.emplace(peer.permanent_uuid(), std::move(state)).second);
  }

  // Ensure that the candidate has already voted for itself.
  CHECK_EQ(1, vote_counter_->GetTotalVotesCounted()) << "Candidate must vote for itself first";

  // Ensure that existing votes + future votes add up to the expected total.
  CHECK_EQ(vote_counter_->GetTotalVotesCounted() + voting_follower_uuids_.size(),
           vote_counter_->GetTotalExpectedVotes())
      << "Expected different number of followers. Follower UUIDs: ["
      << yb::ToString(voting_follower_uuids_)
      << "]; RaftConfig: {" << config.ShortDebugString() << "}";
}

LeaderElection::~LeaderElection() {
  std::lock_guard<Lock> guard(lock_);
  DCHECK(has_responded_); // We must always call the callback exactly once.
  voter_state_.clear();
}

void LeaderElection::Run() {
  VLOG_WITH_PREFIX(1) << "Running leader election.";

  // Check if we have already won the election (relevant if this is a
  // single-node configuration, since we always pre-vote for ourselves).
  CheckForDecision();

  auto deadline = std::chrono::steady_clock::now() + timeout_.ToSteadyDuration();
  size_t voters_left = voting_follower_uuids_.size();
  while (voters_left > 0) {
    TrySendRequestToVoters(deadline, &voters_left);
  }
}

void LeaderElection::TrySendRequestToVoters(
    std::chrono::steady_clock::time_point deadline, size_t* voters_left) {
  const auto kStepTimeout = 100ms;

  auto step_deadline = std::min(deadline, std::chrono::steady_clock::now() + kStepTimeout);
  bool last_step = step_deadline == deadline;
  // The rest of the code below is for a typical multi-node configuration.
  for (const std::string& voter_uuid : voting_follower_uuids_) {
    VoterState* state = nullptr;
    {
      std::lock_guard<Lock> guard(lock_);
      if (result_) { // Already have result.
        *voters_left = 0;
        break;
      }
      auto it = voter_state_.find(voter_uuid);
      CHECK(it != voter_state_.end());
      state = it->second.get();
      // Safe to drop the lock because voter_state_ is not mutated outside of
      // the constructor / destructor. We do this to avoid deadlocks below.
    }

    // Already processed this voter.
    if (state->proxy || !state->proxy_future.valid()) {
      continue;
    }

    bool ready = state->proxy_future.wait_until(step_deadline) == std::future_status::ready;
    if (!ready && !last_step) {
      continue;
    }

    --*voters_left;
    auto proxy_result = ready
        ? state->proxy_future.get()
        : STATUS_FORMAT(TimedOut, "Timed out trying to resolve host: $0, timeout: $1",
                        state->address, timeout_);

    // If we failed to construct the proxy, just record a 'NO' vote with the status
    // that indicates why it failed.
    if (!proxy_result.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Was unable to construct an RPC proxy to peer "
                               << voter_uuid << ": " << proxy_result.status()
                               << ". Counting it as a 'NO' vote.";
      {
        std::lock_guard<Lock> guard(lock_);
        RecordVoteUnlocked(voter_uuid, VOTE_DENIED);
      }
      CheckForDecision();
      continue;
    } else {
      state->proxy = std::move(*proxy_result);
    }

    // Send the RPC request.
    LOG_WITH_PREFIX(INFO) << "Requesting vote from peer " << voter_uuid;
    state->rpc.set_timeout(timeout_);

    state->request = request_;
    state->request.set_dest_uuid(voter_uuid);

    LeaderElectionPtr retained_self = this;
    if (!suppress_vote_request_) {
      state->proxy->RequestConsensusVoteAsync(
          &state->request, &state->response, &state->rpc,
          std::bind(&LeaderElection::VoteResponseRpcCallback, this, voter_uuid, retained_self));
    } else {
      state->response.set_responder_uuid(voter_uuid);
      VoteResponseRpcCallback(voter_uuid, retained_self);
    }
  }
}

void LeaderElection::CheckForDecision() {
  bool to_respond = false;
  {
    std::lock_guard<Lock> guard(lock_);
    // Check if the vote has been newly decided.
    if (!result_ && vote_counter_->IsDecided()) {
      ElectionVote decision;
      CHECK_OK(vote_counter_->GetDecision(&decision));
      LOG_WITH_PREFIX(INFO) << "Election decided. Result: candidate "
                << ((decision == VOTE_GRANTED) ? "won." : "lost.");
      result_.emplace(election_term(),
                      decision,
                      old_leader_lease_expiration_,
                      old_leader_ht_lease_expiration_);
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
    decision_callback_(*result_);
  }
}

void LeaderElection::VoteResponseRpcCallback(const std::string& voter_uuid,
                                             const LeaderElectionPtr& self) {
  {
    std::lock_guard<Lock> guard(lock_);
    auto it = voter_state_.find(voter_uuid);
    CHECK(it != voter_state_.end());
    VoterState* state = it->second.get();

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
    result_.emplace(election_term(), VOTE_DENIED, state.response.responder_term(), msg);
  }
}

void LeaderElection::HandleVoteGrantedUnlocked(const string& voter_uuid, const VoterState& state) {
  DCHECK(lock_.is_locked());
  DCHECK_EQ(state.response.responder_term(), election_term());
  DCHECK(state.response.vote_granted());
  if (state.response.has_remaining_leader_lease_duration_ms()) {
    old_leader_lease_expiration_.MakeAtLeast(MonoTime::Now() +
        MonoDelta::FromMilliseconds(state.response.remaining_leader_lease_duration_ms()));
  }

  if (state.response.has_leader_ht_lease_expiration()) {
    old_leader_ht_lease_expiration_ = std::max(
        old_leader_ht_lease_expiration_,
        state.response.leader_ht_lease_expiration());
  }

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
} // namespace yb
