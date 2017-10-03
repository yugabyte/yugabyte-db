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
#ifndef KUDU_CONSENSUS_RAFT_CONSENSUS_UTIL_H_
#define KUDU_CONSENSUS_RAFT_CONSENSUS_UTIL_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "kudu/consensus/consensus.h"
#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/consensus/consensus_queue.h"
#include "kudu/consensus/log_util.h"
#include "kudu/gutil/port.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"

namespace kudu {

class HostPort;
class ReplicaState;
class ThreadPool;

namespace rpc {
class Messenger;
}

namespace consensus {

// Class that coordinates access to the replica state (independently of Role).
// This has a 1-1 relationship with RaftConsensus and is essentially responsible for
// keeping state and checking if state changes are viable.
//
// Note that, in the case of a LEADER role, there are two configuration states that
// that are tracked: a pending and a committed configuration. The "active" state is
// considered to be the pending configuration if it is non-null, otherwise the
// committed configuration is the active configuration.
//
// When a replica becomes a leader of a configuration, it sets the pending configuration to
// a new configuration declaring itself as leader and sets its "active" role to LEADER.
// It then starts up ConsensusPeers for each member of the pending configuration and
// tries to push a new configuration to the peers. Once that configuration is
// pushed to a majority of the cluster, it is considered committed and the
// replica flushes that configuration to disk as the committed configuration.
//
// Each time an operation is to be performed on the replica the appropriate LockFor*()
// method should be called. The LockFor*() methods check that the replica is in the
// appropriate state to perform the requested operation and returns the lock or return
// Status::IllegalState if that is not the case.
//
// All state reading/writing methods acquire the lock, unless suffixed by "Unlocked", in
// which case a lock should be obtained prior to calling them.
class ReplicaState {
 public:
  enum State {
    // State after the replica is built.
    kInitialized,

    // State signaling the replica accepts requests (from clients
    // if leader, from leader if follower)
    kRunning,

    // State signaling that the replica is shutting down and no longer accepting
    // new transactions or commits.
    kShuttingDown,

    // State signaling the replica is shut down and does not accept
    // any more requests.
    kShutDown
  };

  typedef unique_lock<simple_spinlock> UniqueLock;

  typedef std::map<int64_t, scoped_refptr<ConsensusRound> > IndexToRoundMap;

  typedef std::set<int64_t> OutstandingCommits;

  typedef IndexToRoundMap::value_type IndexToRoundEntry;

  ReplicaState(ConsensusOptions options, std::string peer_uuid,
               gscoped_ptr<ConsensusMetadata> cmeta,
               ReplicaTransactionFactory* txn_factory);

  Status StartUnlocked(const OpId& last_in_wal);

  // Locks a replica in preparation for StartUnlocked(). Makes
  // sure the replica is in kInitialized state.
  Status LockForStart(UniqueLock* lock) const WARN_UNUSED_RESULT;

  // Locks a replica down until the critical section of an append completes,
  // i.e. until the replicate message has been assigned an id and placed in
  // the log queue.
  // This also checks that the replica is in the appropriate
  // state (role) to replicate the provided operation, that the operation
  // contains a replicate message and is of the appropriate type, and returns
  // Status::IllegalState if that is not the case.
  Status LockForReplicate(UniqueLock* lock, const ReplicateMsg& msg) const WARN_UNUSED_RESULT;

  // Locks a replica down until the critical section of a commit completes.
  // This succeeds for all states since a replica which has initiated
  // a Prepare()/Replicate() must eventually commit even if it's state
  // has changed after the initial Append()/Update().
  Status LockForCommit(UniqueLock* lock) const WARN_UNUSED_RESULT;

  // Locks a replica down until an the critical section of an update completes.
  // Further updates from the same or some other leader will be blocked until
  // this completes. This also checks that the replica is in the appropriate
  // state (role) to be updated and returns Status::IllegalState if that
  // is not the case.
  Status LockForUpdate(UniqueLock* lock) const WARN_UNUSED_RESULT;

  // Changes the role to non-participant and returns a lock that can be
  // used to make sure no state updates come in until Shutdown() is
  // completed.
  Status LockForShutdown(UniqueLock* lock) WARN_UNUSED_RESULT;

  Status LockForConfigChange(UniqueLock* lock) const WARN_UNUSED_RESULT;

  // Obtains the lock for a state read, does not check state.
  Status LockForRead(UniqueLock* lock) const WARN_UNUSED_RESULT;

  // Obtains the lock so that we can advance the majority replicated
  // index and possibly the committed index.
  // Requires that this peer is leader.
  Status LockForMajorityReplicatedIndexUpdate(
      UniqueLock* lock) const WARN_UNUSED_RESULT;

  // Ensure the local peer is the active leader.
  // Returns OK if leader, IllegalState otherwise.
  Status CheckActiveLeaderUnlocked() const;

  // Completes the Shutdown() of this replica. No more operations, local
  // or otherwise can happen after this point.
  // Called after the quiescing phase (started with LockForShutdown())
  // finishes.
  Status ShutdownUnlocked() WARN_UNUSED_RESULT;

  // Return current consensus state summary.
  ConsensusStatePB ConsensusStateUnlocked(ConsensusConfigType type) const {
    return cmeta_->ToConsensusStatePB(type);
  }

  // Returns the currently active Raft role.
  RaftPeerPB::Role GetActiveRoleUnlocked() const;

  // Returns true if there is a configuration change currently in-flight but not yet
  // committed.
  bool IsConfigChangePendingUnlocked() const;

  // Inverse of IsConfigChangePendingUnlocked(): returns OK if there is
  // currently *no* configuration change pending, and IllegalState is there *is* a
  // configuration change pending.
  Status CheckNoConfigChangePendingUnlocked() const;

  // Returns true if an operation is in this replica's log, namely:
  // - If the op's index is lower than or equal to our committed index
  // - If the op id matches an inflight op.
  // If an operation with the same index is in our log but the terms
  // are different 'term_mismatch' is set to true, it is false otherwise.
  bool IsOpCommittedOrPending(const OpId& op_id, bool* term_mismatch);

  // Sets the given configuration as pending commit. Does not persist into the peers
  // metadata. In order to be persisted, SetCommittedConfigUnlocked() must be called.
  Status SetPendingConfigUnlocked(const RaftConfigPB& new_config) WARN_UNUSED_RESULT;

  // Clear (cancel) the pending configuration.
  void ClearPendingConfigUnlocked();

  // Return the pending configuration, or crash if one is not set.
  const RaftConfigPB& GetPendingConfigUnlocked() const;

  // Changes the committed config for this replica. Checks that there is a
  // pending configuration and that it is equal to this one. Persists changes to disk.
  // Resets the pending configuration to null.
  Status SetCommittedConfigUnlocked(const RaftConfigPB& new_config);

  // Return the persisted configuration.
  const RaftConfigPB& GetCommittedConfigUnlocked() const;

  // Return the "active" configuration - if there is a pending configuration return it;
  // otherwise return the committed configuration.
  const RaftConfigPB& GetActiveConfigUnlocked() const;

  // Checks if the term change is legal. If so, sets 'current_term'
  // to 'new_term' and sets 'has voted' to no for the current term.
  Status SetCurrentTermUnlocked(int64_t new_term) WARN_UNUSED_RESULT;

  // Returns the term set in the last config change round.
  const int64_t GetCurrentTermUnlocked() const;

  // Accessors for the leader of the current term.
  void SetLeaderUuidUnlocked(const std::string& uuid);
  const std::string& GetLeaderUuidUnlocked() const;
  bool HasLeaderUnlocked() const { return !GetLeaderUuidUnlocked().empty(); }
  void ClearLeaderUnlocked() { SetLeaderUuidUnlocked(""); }

  // Return whether this peer has voted in the current term.
  const bool HasVotedCurrentTermUnlocked() const;

  // Record replica's vote for the current term, then flush the consensus
  // metadata to disk.
  Status SetVotedForCurrentTermUnlocked(const std::string& uuid) WARN_UNUSED_RESULT;

  // Return replica's vote for the current term.
  // The vote must be set; use HasVotedCurrentTermUnlocked() to check.
  const std::string& GetVotedForCurrentTermUnlocked() const;

  ReplicaTransactionFactory* GetReplicaTransactionFactoryUnlocked() const;

  // Returns the uuid of the peer to which this replica state belongs.
  // Safe to call with or without locks held.
  const std::string& GetPeerUuid() const;

  const ConsensusOptions& GetOptions() const;

  // Returns the operations that are not consensus committed.
  void GetUncommittedPendingOperationsUnlocked(std::vector<scoped_refptr<ConsensusRound> >* ops);

  // Aborts pending operations after, but not including 'index'. The OpId with 'index'
  // will become our new last received id. If there are pending operations with indexes
  // higher than 'index' those operations are aborted.
  Status AbortOpsAfterUnlocked(int64_t index);

  // Returns the the ConsensusRound with the provided index, if there is any, or NULL
  // if there isn't.
  scoped_refptr<ConsensusRound> GetPendingOpByIndexOrNullUnlocked(int64_t index);

  // Add 'round' to the set of rounds waiting to be committed.
  Status AddPendingOperation(const scoped_refptr<ConsensusRound>& round);

  // Marks ReplicaTransactions up to 'id' as majority replicated, meaning the
  // transaction may Apply() (immediately if Prepare() has completed or when Prepare()
  // completes, if not).
  //
  // If this advanced the committed index, sets *committed_index_changed to true.
  Status UpdateMajorityReplicatedUnlocked(const OpId& majority_replicated,
                                          OpId* committed_index,
                                          bool* committed_index_changed);

  // Advances the committed index.
  // This is a no-op if the committed index has not changed.
  // Returns in '*committed_index_changed' whether the operation actually advanced
  // the index.
  Status AdvanceCommittedIndexUnlocked(const OpId& committed_index,
                                       bool* committed_index_changed);

  // Returns the watermark below which all operations are known to
  // be committed according to consensus.
  //
  // This must be called under a lock.
  const OpId& GetCommittedOpIdUnlocked() const;

  // Returns OK iff an op from the current term has been committed.
  Status CheckHasCommittedOpInCurrentTermUnlocked() const;

  // Updates the last received operation.
  // This must be called under a lock.
  void UpdateLastReceivedOpIdUnlocked(const OpId& op_id);

  // Returns the last received op id. This must be called under the lock.
  const OpId& GetLastReceivedOpIdUnlocked() const;

  // Returns the id of the last op received from the current leader.
  const OpId& GetLastReceivedOpIdCurLeaderUnlocked() const;

  // Returns the id of the latest pending transaction (i.e. the one with the
  // latest index). This must be called under the lock.
  OpId GetLastPendingTransactionOpIdUnlocked() const;

  // Updates the last committed operation including removing it from the pending commits.
  //
  // 'commit_op_id' refers to the OpId of the actual commit operation, whereas
  // 'committed_op_id' refers to the OpId of the original REPLICATE message which was
  // committed.
  //
  // This must be called under a lock.
  void UpdateReplicaCommittedOpIdUnlocked(const OpId& committed_op_id);

  // Waits for already triggered Apply()s to commit.
  Status WaitForOustandingApplies();

  // Used by replicas to cancel pending transactions. Pending transaction are those
  // that have completed prepare/replicate but are waiting on the LEADER's commit
  // to complete. This does not cancel transactions being applied.
  Status CancelPendingTransactions();

  void NewIdUnlocked(OpId* id);

  // Used when, for some reason, an operation that failed before it could be considered
  // a part of the state machine. Basically restores the id gen to the state it was before
  // generating 'id'.
  void CancelPendingOperation(const OpId& id);

  // Returns the number of transactions that are currently in the pending state
  // i.e. transactions for which Prepare() is done or under way.
  int GetNumPendingTxnsUnlocked() const;

  std::string ToString() const;
  std::string ToStringUnlocked() const;

  // A common prefix that should be in any log messages emitted,
  // identifying the tablet and peer.
  std::string LogPrefix();
  std::string LogPrefixUnlocked() const;

  // A variant of LogPrefix which does not take the lock. This is a slightly
  // less thorough prefix which only includes immutable (and thus thread-safe)
  // information, but does not require the lock.
  std::string LogPrefixThreadSafe() const;

  // Checks that 'current' correctly follows 'previous'. Specifically it checks
  // that the term is the same or higher and that the index is sequential.
  static Status CheckOpInSequence(const OpId& previous, const OpId& current);

  // Return the current state of this object.
  // The update_lock_ must be held.
  ReplicaState::State state() const;

 private:
  const ConsensusOptions options_;

  // The UUID of the local peer.
  const std::string peer_uuid_;

  mutable simple_spinlock update_lock_;

  // Consensus metadata persistence object.
  gscoped_ptr<ConsensusMetadata> cmeta_;

  // Used by the LEADER. This is the index of the next operation generated
  // by this LEADER.
  int64_t next_index_;

  // Index=>Round map that manages pending ops, i.e. operations for which we've
  // received a replicate message from the leader but have yet to be committed.
  // The key is the index of the replicate operation.
  IndexToRoundMap pending_txns_;

  // When we receive a message from a remote peer telling us to start a transaction, we use
  // this factory to start it.
  ReplicaTransactionFactory* txn_factory_;

  // The id of the last received operation, which corresponds to the last entry
  // written to the local log. Operations whose id is lower than or equal to
  // this id do not need to be resent by the leader. This is not guaranteed to
  // be monotonically increasing due to the possibility for log truncation and
  // aborted operations when a leader change occurs.
  OpId last_received_op_id_;

  // Same as last_received_op_id_ but only includes operations sent by the
  // current leader. The "term" in this op may not actually match the current
  // term, since leaders may replicate ops from prior terms.
  //
  // As an implementation detail, this field is reset to MinumumOpId() every
  // time there is a term advancement on the local node, to simplify the logic
  // involved in resetting this every time a new node becomes leader.
  OpId last_received_op_id_current_leader_;

  // The id of the Apply that was last triggered when the last message from the leader
  // was received. Initialized to MinimumOpId().
  OpId last_committed_index_;

  State state_;
};

}  // namespace consensus
}  // namespace kudu

#endif /* KUDU_CONSENSUS_RAFT_CONSENSUS_UTIL_H_ */
