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

#ifndef KUDU_CONSENSUS_LEADER_ELECTION_H
#define KUDU_CONSENSUS_LEADER_ELECTION_H

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "kudu/consensus/consensus.h"
#include "kudu/consensus/consensus.pb.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/util/locks.h"

namespace kudu {
class Status;

namespace metadata {
class RaftPeerPB;
}

namespace rpc {
class Messenger;
class RpcController;
}

namespace consensus {
class PeerProxy;
class PeerProxyFactory;

// The vote a peer has given.
enum ElectionVote {
  VOTE_DENIED = 0,
  VOTE_GRANTED = 1,
};

// Simple class to count votes (in-memory, not persisted to disk).
// This class is not thread safe and requires external synchronization.
class VoteCounter {
 public:
  // Create new VoteCounter with the given majority size.
  VoteCounter(int num_voters, int majority_size);

  // Register a peer's vote.
  //
  // If the voter already has a vote recorded, but it has a different value than
  // the vote specified, returns Status::IllegalArgument.
  //
  // If the same vote is duplicated, 'is_duplicate' is set to true.
  // Otherwise, it is set to false.
  // If an OK status is not returned, the value in 'is_duplicate' is undefined.
  Status RegisterVote(const std::string& voter_uuid, ElectionVote vote, bool* is_duplicate);

  // Return whether the vote is decided yet.
  bool IsDecided() const;

  // Return decision iff IsDecided() returns true.
  // If vote is not yet decided, returns Status::IllegalState().
  Status GetDecision(ElectionVote* decision) const;

  // Return the total of "Yes" and "No" votes.
  int GetTotalVotesCounted() const;

  // Return total number of expected votes.
  int GetTotalExpectedVotes() const { return num_voters_; }

  // Return true iff GetTotalVotesCounted() == num_voters_;
  bool AreAllVotesIn() const;

 private:
  friend class VoteCounterTest;

  typedef std::map<std::string, ElectionVote> VoteMap;

  const int num_voters_;
  const int majority_size_;
  VoteMap votes_; // Voting record.
  int yes_votes_; // Accumulated yes votes, for quick counting.
  int no_votes_;  // Accumulated no votes.

  DISALLOW_COPY_AND_ASSIGN(VoteCounter);
};

// The result of a leader election.
struct ElectionResult {
 public:
  ElectionResult(ConsensusTerm election_term, ElectionVote decision);
  ElectionResult(ConsensusTerm election_term, ElectionVote decision,
                 ConsensusTerm higher_term, const std::string& message);

  // Term the election was run for.
  const ConsensusTerm election_term;

  // The overall election GRANTED/DENIED decision of the configuration.
  const ElectionVote decision;

  // At least one voter had a higher term than the candidate.
  const bool has_higher_term;
  const ConsensusTerm higher_term;

  // Human-readable explanation of the vote result, if any.
  const std::string message;
};

// Driver class to run a leader election.
//
// The caller must pass a callback to the driver, which will be called exactly
// once when a Yes/No decision has been made, except in case of Shutdown()
// on the Messenger or test ThreadPool, in which case no guarantee of a
// callback is provided. In that case, we should not care about the election
// result, because the server is ostensibly shutting down.
//
// For a "Yes" decision, a majority of voters must grant their vote.
//
// A "No" decision may be caused by either one of the following:
// - One of the peers replies with a higher term before a decision is made.
// - A majority of the peers votes "No".
//
// Any votes that come in after a decision has been made and the callback has
// been invoked are logged but ignored. Note that this somewhat strays from the
// letter of the Raft paper, in that replies that come after a "Yes" decision
// do not immediately cause the candidate/leader to step down, but this keeps
// our implementation and API simple, and the newly-minted leader will soon
// discover that it must step down when it attempts to replicate its first
// message to the peers.
//
// This class is thread-safe.
class LeaderElection : public RefCountedThreadSafe<LeaderElection> {
 public:
  typedef Callback<void(const ElectionResult&)> ElectionDecisionCallback;
  typedef std::unordered_map<std::string, PeerProxy*> ProxyMap;

  // Set up a new leader election driver.
  //
  // The 'vote_counter' must be initialized with the candidate's own yes vote.
  LeaderElection(const RaftConfigPB& config, PeerProxyFactory* proxy_factory,
                 const VoteRequestPB& request,
                 gscoped_ptr<VoteCounter> vote_counter, MonoDelta timeout,
                 ElectionDecisionCallback decision_callback);

  // Run the election: send the vote request to followers.
  void Run();

 private:
  friend class RefCountedThreadSafe<LeaderElection>;

  struct VoterState {
    gscoped_ptr<PeerProxy> proxy;

    // If constructing the proxy failed (e.g. due to a DNS resolution issue)
    // then 'proxy' will be NULL, and 'proxy_status' will contain the error.
    Status proxy_status;

    rpc::RpcController rpc;
    VoteRequestPB request;
    VoteResponsePB response;
  };

  typedef std::unordered_map<std::string, VoterState*> VoterStateMap;
  typedef simple_spinlock Lock;

  // This class is refcounted.
  ~LeaderElection();

  // Check to see if a decision has been made. If so, invoke decision callback.
  // Calls the callback outside of holding a lock.
  void CheckForDecision();

  // Callback called when the RPC responds.
  void VoteResponseRpcCallback(const std::string& voter_uuid);

  // Record vote from specified peer.
  void RecordVoteUnlocked(const std::string& voter_uuid, ElectionVote vote);

  // Handle a peer that reponded with a term greater than the election term.
  void HandleHigherTermUnlocked(const std::string& voter_uuid, const VoterState& state);

  // Log and record a granted vote.
  void HandleVoteGrantedUnlocked(const std::string& voter_uuid, const VoterState& state);

  // Log the reason for a denied vote and record it.
  void HandleVoteDeniedUnlocked(const std::string& voter_uuid, const VoterState& state);

  // Returns a string to be prefixed to all log entries.
  // This method accesses const members and is thread safe.
  std::string LogPrefix() const;

  // Helper to reference the term we are running the election for.
  ConsensusTerm election_term() const { return request_.candidate_term(); }

  // All non-const fields are protected by 'lock_'.
  Lock lock_;

  // The result returned by the ElectionDecisionCallback.
  // NULL if not yet known.
  gscoped_ptr<ElectionResult> result_;

  // Whether we have responded via the callback yet.
  bool has_responded_;

  // Election request to send to voters.
  const VoteRequestPB request_;

  // Object to count the votes.
  const gscoped_ptr<VoteCounter> vote_counter_;

  // Timeout for sending RPCs.
  const MonoDelta timeout_;

  // Callback invoked to notify the caller of an election decision.
  const ElectionDecisionCallback decision_callback_;

  // List of all potential followers to request votes from.
  // The candidate's own UUID must not be included.
  std::vector<std::string> follower_uuids_;

  // Map of UUID -> VoterState.
  VoterStateMap voter_state_;
};

} // namespace consensus
} // namespace kudu

#endif /* KUDU_CONSENSUS_LEADER_ELECTION_H */
