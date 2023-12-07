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
#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include <boost/optional/optional_fwd.hpp>

#include "yb/common/entity_ids_types.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_types.pb.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/opid.h"
#include "yb/util/opid.pb.h"
#include "yb/util/physical_time.h"
#include "yb/util/status_callback.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

namespace server {
class Clock;
}

namespace tablet {
class TabletPeer;
}

namespace tserver {
class TabletServerErrorPB;
}

namespace consensus {

// After completing bootstrap, some of the results need to be plumbed through
// into the consensus implementation.
struct ConsensusBootstrapInfo {
  ConsensusBootstrapInfo();

  // The id of the last operation in the log
  OpIdPB last_id;

  // The id of the last committed operation in the log.
  OpIdPB last_committed_id;

  // REPLICATE messages which were in the log with no accompanying
  // COMMIT. These need to be passed along to consensus init in order
  // to potentially commit them.
  //
  // These are owned by the ConsensusBootstrapInfo instance.
  ReplicateMsgs orphaned_replicates;

 private:
  DISALLOW_COPY_AND_ASSIGN(ConsensusBootstrapInfo);
};

struct LeaderState;

// Mode is orthogonal to pre-elections, so any combination could be used.
YB_DEFINE_ENUM(ElectionMode,
    // A normal leader election. Peers will not vote for this node
    // if they believe that a leader is alive.
    (NORMAL_ELECTION)
    // In this mode, peers will vote for this candidate even if they
    // think a leader is alive. This can be used for a faster hand-off
    // between a leader and one of its replicas.
    (ELECT_EVEN_IF_LEADER_IS_ALIVE));

// Arguments for StartElection.
struct LeaderElectionData {
  ElectionMode mode = ElectionMode::NORMAL_ELECTION;

  // pending_commit - we should start election only after we have specified entry committed.
  const bool pending_commit = false;

  // must_be_committed_opid - only matters if pending_commit is true.
  //    If this is specified, we would wait until this entry is committed. If not specified
  //    (i.e. if this has the default OpId value) it is taken from the last call to StartElection
  //    with pending_commit = true.
  OpId must_be_committed_opid;

  // originator_uuid - if election is initiated by an old leader as part of a stepdown procedure,
  //    this would contain the uuid of the old leader.
  std::string originator_uuid = std::string();

  TEST_SuppressVoteRequest suppress_vote_request = TEST_SuppressVoteRequest::kFalse;

  bool initial_election = false;

  std::string ToString() const;
};

// The external interface for a consensus peer.
//
// Note: Even though Consensus points to Log, it needs to be destroyed
// after it. See Log class header comment for the reason why. On the other
// hand Consensus must be quiesced before closing the log, otherwise it
// will try to write to a destroyed/closed log.
//
// The order of these operations on shutdown must therefore be:
// 1 - quiesce Consensus
// 2 - close/destroy Log
// 3 - destroy Consensus
class Consensus {
 public:
  class ConsensusFaultHooks;
  Consensus() {}
  virtual ~Consensus() {}

  // Starts running the consensus algorithm.
  virtual Status Start(const ConsensusBootstrapInfo& info) = 0;

  // Returns true if consensus is running.
  virtual bool IsRunning() const = 0;

  // Emulates a leader election by simply making this peer leader.
  virtual Status EmulateElection() = 0;

  virtual Status StartElection(const LeaderElectionData& data) = 0;

  // We tried to step down, so you protege become leader.
  // But it failed to win election, so we should reset our withhold time and try to reelect ourself.
  // election_lost_by_uuid - uuid of protege that lost election.
  virtual Status ElectionLostByProtege(const std::string& election_lost_by_uuid) = 0;

  // Implement a LeaderStepDown() request.
  virtual Status StepDown(const LeaderStepDownRequestPB* req,
                                  LeaderStepDownResponsePB* resp);

  // Wait until the node has LEADER role.
  // Returns Status::TimedOut if the role is not LEADER within 'timeout'.
  virtual Status WaitUntilLeaderForTests(const MonoDelta& timeout) = 0;

  // Called by a Leader to replicate an entry to the state machine.
  //
  // From the leader instance perspective execution proceeds as follows:
  //
  //           Leader                               RaftConfig
  //             +                                     +
  //     1) Req->| Replicate()                         |
  //             |                                     |
  //     2)      +-------------replicate-------------->|
  //             |<---------------ACK------------------+
  //             |                                     |
  //     3)      +--+                                  |
  //           <----+ round.NotifyReplicationFinished()|
  //             |                                     |
  //     3a)     |  +------ update commitIndex ------->|
  //             |                                     |
  //
  // 1) Caller calls Replicate(), method returns immediately to the caller and
  //    runs asynchronously.
  //
  // 2) Leader replicates the entry to the peers using the consensus
  //    algorithm, proceeds as soon as a majority of voters acknowledges the
  //    entry.
  //
  // 3) Leader defers to the caller by calling ConsensusRound::NotifyReplicationFinished,
  //    which calls the ConsensusReplicatedCallback.
  //
  // 3a) The leader asynchronously notifies other peers of the new
  //     commit index, which tells them to apply the operation.
  //
  // This method can only be called on the leader, i.e. role() == LEADER

  virtual Status TEST_Replicate(const ConsensusRoundPtr& round) = 0;

  // A batch version of Replicate, which is what we try to use as much as possible for performance.
  virtual Status ReplicateBatch(const ConsensusRounds& rounds) = 0;

  // Messages sent from LEADER to FOLLOWERS and LEARNERS to update their
  // state machines. This is equivalent to "AppendEntries()" in Raft
  // terminology.
  //
  // ConsensusRequestPB contains a sequence of 0 or more operations to apply
  // on the replica. If there are 0 operations the request is considered
  // 'status-only' i.e. the leader is communicating with the follower only
  // in order to pass back and forth information on watermarks (eg committed
  // operation ID, replicated op id, etc).
  //
  // If the sequence contains 1 or more operations they will be replicated
  // in the same order as the leader, and submitted for asynchronous Prepare
  // in the same order.
  //
  // The leader also provides information on the index of the latest
  // operation considered committed by consensus. The replica uses this
  // information to update the state of any pending (previously replicated/prepared)
  // transactions.
  //
  // Returns Status::OK if the response has been filled (regardless of accepting
  // or rejecting the specific request). Returns non-OK Status if a specific
  // error response could not be formed, which will result in the service
  // returning an UNKNOWN_ERROR RPC error code to the caller and including the
  // stringified Status message.
  virtual Status Update(
      const std::shared_ptr<LWConsensusRequestPB>& request,
      LWConsensusResponsePB* response, CoarseTimePoint deadline) = 0;

  // Messages sent from CANDIDATEs to voting peers to request their vote
  // in leader election.
  virtual Status RequestVote(const VoteRequestPB* request,
                                     VoteResponsePB* response) = 0;

  // Implement a ChangeConfig() request.
  virtual Status ChangeConfig(const ChangeConfigRequestPB& req,
                                      const StdStatusCallback& client_cb,
                                      boost::optional<tserver::TabletServerErrorPB::Code>* error);

  virtual Status UnsafeChangeConfig(
      const UnsafeChangeConfigRequestPB& req,
      boost::optional<tserver::TabletServerErrorPB::Code>* error_code) = 0;

  // Returns the current Raft role of this instance.
  virtual PeerRole role() const = 0;

  // Returns the leader status (see LeaderStatus type description for details).
  // If leader is ready, then also returns term, otherwise OpId::kUnknownTerm is returned.
  //
  // allow_stale could be used to avoid refreshing cache, when we are OK to read slightly outdated
  // value.
  virtual LeaderState GetLeaderState(bool allow_stale = false) const = 0;

  LeaderStatus GetLeaderStatus(bool allow_stale = false) const;
  int64_t LeaderTerm() const;

  // Returns the uuid of this peer.
  virtual const std::string& peer_uuid() const = 0;

  // Returns the id of the tablet whose updates this consensus instance helps coordinate.
  virtual const TabletId& tablet_id() const = 0;

  virtual const TabletId& split_parent_tablet_id() const = 0;

  // Returns a copy of the committed state of the Consensus system. Also allows returning the
  // leader lease status captured under the same lock.
  virtual ConsensusStatePB ConsensusState(
      ConsensusConfigType type,
      LeaderLeaseStatus* leader_lease_status = nullptr, 
      ConsensusWatermarksPB* opid_list = nullptr) const = 0;

  // Returns a copy of the committed state of the Consensus system, assuming caller holds the needed
  // locks.
  virtual ConsensusStatePB ConsensusStateUnlocked(
      ConsensusConfigType type,
      LeaderLeaseStatus* leader_lease_status = nullptr, 
      ConsensusWatermarksPB* opid_list = nullptr) const = 0;

  // Returns a copy of the current committed Raft configuration.
  virtual RaftConfigPB CommittedConfig() const = 0;

  virtual void DumpStatusHtml(std::ostream& out) const = 0;

  void SetFaultHooks(const std::shared_ptr<ConsensusFaultHooks>& hooks);

  const std::shared_ptr<ConsensusFaultHooks>& GetFaultHooks() const;

  // Stops running the consensus algorithm.
  virtual void Shutdown() = 0;

  // Returns the last OpId (either received or committed, depending on the 'type' argument) that the
  // Consensus implementation knows about.  Primarily used for testing purposes.
  Result<yb::OpId> GetLastOpId(OpIdType type);

  virtual yb::OpId GetLastReceivedOpId() = 0;

  virtual yb::OpId GetLastCommittedOpId() = 0;

  virtual yb::OpId GetLastAppliedOpId() = 0;

  // Assuming we are the leader, wait until we have a valid leader lease (i.e. the old leader's
  // lease has expired, and we have replicated a new lease that has not expired yet).
  virtual Status WaitForLeaderLeaseImprecise(CoarseTimePoint deadline) = 0;

  // Check that this Consensus is a leader and has lease, returns Status::OK in this case.
  // Otherwise error status is returned.
  virtual Status CheckIsActiveLeaderAndHasLease() const = 0;

  // Returns majority replicated ht lease, so we know that after leader change
  // operations would not be added with hybrid time below this lease.
  //
  // `min_allowed` - result should be greater or equal to `min_allowed`, otherwise
  // it tries to wait until ht lease reaches this value or `deadline` happens.
  //
  // Returns 0 if timeout happened.
  virtual Result<MicrosTime> MajorityReplicatedHtLeaseExpiration(
      MicrosTime min_allowed, CoarseTimePoint deadline) const = 0;

  // Read majority replicated messages for CDC producer.
  virtual Result<ReadOpsResult> ReadReplicatedMessagesForCDC(
      const yb::OpId& from, int64_t* repl_index, const CoarseTimePoint deadline,
      const bool fetch_single_entry = false) = 0;

  virtual void UpdateCDCConsumerOpId(const yb::OpId& op_id) = 0;

 protected:
  friend class RefCountedThreadSafe<Consensus>;
  friend class tablet::TabletPeer;

  // Fault hooks for tests. In production code this will always be null.
  std::shared_ptr<ConsensusFaultHooks> fault_hooks_;

  enum HookPoint {
    PRE_START,
    POST_START,
    PRE_CONFIG_CHANGE,
    POST_CONFIG_CHANGE,
    PRE_REPLICATE,
    POST_REPLICATE,
    PRE_COMMIT,
    POST_COMMIT,
    PRE_UPDATE,
    POST_UPDATE,
    PRE_SHUTDOWN,
    POST_SHUTDOWN
  };

  Status ExecuteHook(HookPoint point);

  enum State {
    kNotInitialized,
    kInitializing,
    kConfiguring,
    kRunning,
  };

 private:
  DISALLOW_COPY_AND_ASSIGN(Consensus);
};

YB_DEFINE_ENUM(StateChangeReason,
    (INVALID_REASON)
    (TABLET_PEER_STARTED)
    (CONSENSUS_STARTED)
    (NEW_LEADER_ELECTED)
    (FOLLOWER_NO_OP_COMPLETE)
    (LEADER_CONFIG_CHANGE_COMPLETE)
    (FOLLOWER_CONFIG_CHANGE_COMPLETE));

class Consensus::ConsensusFaultHooks {
 public:
  virtual Status PreStart();
  virtual Status PostStart();
  virtual Status PreConfigChange();
  virtual Status PostConfigChange();
  virtual Status PreReplicate();
  virtual Status PostReplicate();
  virtual Status PreUpdate();
  virtual Status PostUpdate();
  virtual Status PreShutdown();
  virtual Status PostShutdown();
  virtual ~ConsensusFaultHooks() {}
};

class SafeOpIdWaiter {
 public:
  virtual yb::OpId WaitForSafeOpIdToApply(const yb::OpId& op_id) = 0;

 protected:
  ~SafeOpIdWaiter() {}
};

struct LeaderState {
  LeaderStatus status;
  int64_t term;
  MonoDelta remaining_old_leader_lease;

  LeaderState& MakeNotReadyLeader(LeaderStatus status);

  bool ok() const {
    return status == LeaderStatus::LEADER_AND_READY;
  }

  Status CreateStatus() const;
};

Status MoveStatus(LeaderState&& state);

} // namespace consensus
} // namespace yb
