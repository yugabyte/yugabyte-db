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

#pragma once

#include "yb/common/common_fwd.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_types.pb.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace consensus {

class ConsensusContext {
 public:
  // Factory for replica transactions.
  // An implementation of this factory must be registered prior to consensus
  // start, and is used to create transactions when the consensus implementation receives
  // messages from the leader.
  //
  // Replica transactions execute the following way:
  //
  // - When a ReplicateMsg is first received from the leader, the Consensus
  //   instance creates the ConsensusRound and calls StartReplicaOperation().
  //   This will trigger the Prepare(). At the same time replica consensus
  //   instance immediately stores the ReplicateMsg in the Log. Once the replicate
  //   message is stored in stable storage an ACK is sent to the leader (i.e. the
  //   replica Consensus instance does not wait for Prepare() to finish).
  virtual Status StartReplicaOperation(
      const ConsensusRoundPtr& context, HybridTime propagated_safe_time) = 0;

  virtual void SetPropagatedSafeTime(HybridTime ht) = 0;

  // Set the propagated safe time of the mvcc.
  virtual void SetMvccPropagatedSafeTime(HybridTime ht) = 0;

  virtual bool ShouldApplyWrite() = 0;

  // Returns true if writes are hard-stopped on any backing store (regular_db or intents_db).
  // Unlike ShouldApplyWrite(), this only checks for hard stops (not delays) and is safe
  // to call outside ReplicaState's lock. Used by RaftConsensus::Update() to reject
  // UpdateConsensus RPCs before acquiring update_mutex_, preventing RPC thread pile-up
  // when a DB is stalled. See #30728.
  virtual bool AreWritesStopped() = 0;

  // Performs steps to prepare request for peer.
  // For instance it could enqueue some operations to the Raft.
  //
  // Returns the current safe time, so we can send it from leaders to followers.
  virtual Result<HybridTime> PreparePeerRequest() = 0;

  // This is called every time majority-replicated watermarks (OpId / leader leases) change. This is
  // used for updating the "propagated safe time" value in MvccManager and unblocking readers
  // waiting for it to advance, and notifying async write operations of completion.
  virtual Status MajorityReplicated(const OpId& committed_op_id) = 0;

  // This is called when a LEADER transitions to a follower or learner.
  virtual void BecomeReplica() = 0;

  // This is called every time the Raft config was changed and replicated.
  // This is used to notify the higher layer about the config change. Currently it's
  // needed to update the internal flag in the MvccManager to return a correct safe
  // time value for a read/write operation in case of RF==1 mode.
  virtual void ChangeConfigReplicated(const RaftConfigPB& config) = 0;

  // See DB::GetCurrentVersionNumSSTFiles
  virtual uint64_t NumSSTFiles() = 0;

  // Register listener that will be invoked when number of SST files changed.
  // Listener could be set only once and then reset.
  virtual void ListenNumSSTFilesChanged(std::function<void()> listener) = 0;

  // Checks whether operation with provided op id and type could be added to the log.
  virtual Status CheckOperationAllowed(
      const OpId& op_id, consensus::OperationType op_type) = 0;

  // Checks whether the leader may replicate replicate_msg, which already has its op id and hybrid
  // time.  Called only on the leader, under the Raft lock, before the op is added as pending.  Not
  // called for a client's resend of a write that has already been replicated: Raft answers that
  // resend AlreadyPresent before reaching this check, so the client learns its write succeeded
  // even if this check would now reject it.
  //
  // A non-OK status rejects the op: Raft rolls back its op id and fails its round with that
  // status.  Unlike CheckOperationAllowed, which runs before the hybrid time is assigned, this
  // check can judge an op by the time it would commit at.
  //
  // Every op type reaches this check, including those Raft and the tablet rely on for their own
  // operation, e.g., the NO_OP a new leader appends, CHANGE_CONFIG_OP and SPLIT_OP.  Rejecting one
  // of those can stall the tablet or break its invariants, so reject only op types for which that
  // has been shown to be safe.
  //
  // Runs for every op the leader appends while holding the Raft lock, so it must be cheap and must
  // not block.
  //
  // May modify an op it allows, e.g., to drop fields only the check needs: the op has not yet
  // reached the log cache, the WAL, or any follower, all of which see the modified op.
  virtual Status CheckReplicateAllowed(LWReplicateMsg& replicate_msg) = 0;

  // Whether this replica must give up leadership and not seek it.  While this holds, this replica
  // starts no elections, including pre-elections; the server steps down its leaders on its own.
  //
  // Called on every election start, under the Raft lock, so it must be cheap: it may take only
  // briefly held locks, and none that is held while taking the Raft lock.
  virtual bool ShouldRelinquishLeadership() = 0;

  virtual ~ConsensusContext() = default;
};

} // namespace consensus
} // namespace yb
