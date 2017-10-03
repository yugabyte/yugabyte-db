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
#ifndef KUDO_QUORUM_CONSENSUS_H_
#define KUDO_QUORUM_CONSENSUS_H_

#include <boost/optional/optional_fwd.hpp>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/ref_counted_replicate.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/status.h"
#include "kudu/util/status_callback.h"

namespace kudu {

namespace log {
class Log;
}

namespace master {
class SysCatalogTable;
}

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

class ConsensusCommitContinuation;
class ConsensusRound;
class ReplicaTransactionFactory;

typedef int64_t ConsensusTerm;

typedef StatusCallback ConsensusReplicatedCallback;

struct ConsensusOptions {
  std::string tablet_id;
};

// After completing bootstrap, some of the results need to be plumbed through
// into the consensus implementation.
struct ConsensusBootstrapInfo {
  ConsensusBootstrapInfo();
  ~ConsensusBootstrapInfo();

  // The id of the last operation in the log
  OpId last_id;

  // The id of the last committed operation in the log.
  OpId last_committed_id;

  // REPLICATE messages which were in the log with no accompanying
  // COMMIT. These need to be passed along to consensus init in order
  // to potentially commit them.
  //
  // These are owned by the ConsensusBootstrapInfo instance.
  std::vector<ReplicateMsg*> orphaned_replicates;

 private:
  DISALLOW_COPY_AND_ASSIGN(ConsensusBootstrapInfo);
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
class Consensus : public RefCountedThreadSafe<Consensus> {
 public:
  class ConsensusFaultHooks;

  Consensus() {}

  // Starts running the consensus algorithm.
  virtual Status Start(const ConsensusBootstrapInfo& info) = 0;

  // Returns true if consensus is running.
  virtual bool IsRunning() const = 0;

  // Emulates a leader election by simply making this peer leader.
  virtual Status EmulateElection() = 0;

  // Triggers a leader election.
  enum ElectionMode {
    // A normal leader election. Peers will not vote for this node
    // if they believe that a leader is alive.
    NORMAL_ELECTION,

    // In this mode, peers will vote for this candidate even if they
    // think a leader is alive. This can be used for a faster hand-off
    // between a leader and one of its replicas.
    ELECT_EVEN_IF_LEADER_IS_ALIVE
  };
  virtual Status StartElection(ElectionMode mode) = 0;

  // Implement a LeaderStepDown() request.
  virtual Status StepDown(LeaderStepDownResponsePB* resp) {
    return Status::NotSupported("Not implemented.");
  }

  // Creates a new ConsensusRound, the entity that owns all the data
  // structures required for a consensus round, such as the ReplicateMsg
  // (and later on the CommitMsg). ConsensusRound will also point to and
  // increase the reference count for the provided callbacks.
  scoped_refptr<ConsensusRound> NewRound(
      gscoped_ptr<ReplicateMsg> replicate_msg,
      const ConsensusReplicatedCallback& replicated_cb);

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
  virtual Status Replicate(const scoped_refptr<ConsensusRound>& round) = 0;

  // Ensures that the consensus implementation is currently acting as LEADER,
  // and thus is allowed to submit operations to be prepared before they are
  // replicated. To avoid a time-of-check-to-time-of-use (TOCTOU) race, the
  // implementation also stores the current term inside the round's "bound_term"
  // member. When we eventually are about to replicate the transaction, we verify
  // that the term has not changed in the meantime.
  virtual Status CheckLeadershipAndBindTerm(const scoped_refptr<ConsensusRound>& round) {
    return Status::OK();
  }

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
  virtual Status Update(const ConsensusRequestPB* request,
                        ConsensusResponsePB* response) = 0;

  // Messages sent from CANDIDATEs to voting peers to request their vote
  // in leader election.
  virtual Status RequestVote(const VoteRequestPB* request,
                             VoteResponsePB* response) = 0;

  // Implement a ChangeConfig() request.
  virtual Status ChangeConfig(const ChangeConfigRequestPB& req,
                              const StatusCallback& client_cb,
                              boost::optional<tserver::TabletServerErrorPB::Code>* error) {
    return Status::NotSupported("Not implemented.");
  }

  // Returns the current Raft role of this instance.
  virtual RaftPeerPB::Role role() const = 0;

  // Returns the uuid of this peer.
  virtual std::string peer_uuid() const = 0;

  // Returns the id of the tablet whose updates this consensus instance helps coordinate.
  virtual std::string tablet_id() const = 0;

  // Returns a copy of the committed state of the Consensus system.
  virtual ConsensusStatePB ConsensusState(ConsensusConfigType type) const = 0;

  // Returns a copy of the current committed Raft configuration.
  virtual RaftConfigPB CommittedConfig() const = 0;

  virtual void DumpStatusHtml(std::ostream& out) const = 0;

  void SetFaultHooks(const std::shared_ptr<ConsensusFaultHooks>& hooks);

  const std::shared_ptr<ConsensusFaultHooks>& GetFaultHooks() const;

  // Stops running the consensus algorithm.
  virtual void Shutdown() = 0;

  // Returns the last OpId (either received or committed, depending on the
  // 'type' argument) that the Consensus implementation knows about.
  // Primarily used for testing purposes.
  virtual Status GetLastOpId(OpIdType type, OpId* id) {
    return Status::NotFound("Not implemented.");
  }

 protected:
  friend class RefCountedThreadSafe<Consensus>;
  friend class tablet::TabletPeer;
  friend class master::SysCatalogTable;

  // This class is refcounted.
  virtual ~Consensus() {}

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

// Factory for replica transactions.
// An implementation of this factory must be registered prior to consensus
// start, and is used to create transactions when the consensus implementation receives
// messages from the leader.
//
// Replica transactions execute the following way:
//
// - When a ReplicateMsg is first received from the leader, the Consensus
//   instance creates the ConsensusRound and calls StartReplicaTransaction().
//   This will trigger the Prepare(). At the same time replica consensus
//   instance immediately stores the ReplicateMsg in the Log. Once the replicate
//   message is stored in stable storage an ACK is sent to the leader (i.e. the
//   replica Consensus instance does not wait for Prepare() to finish).
//
// - When the CommitMsg for a replicate is first received from the leader
//   the replica waits for the corresponding Prepare() to finish (if it has
//   not completed yet) and then proceeds to trigger the Apply().
//
// - Once Apply() completes the ReplicaTransactionFactory is responsible for logging
//   a CommitMsg to the log to ensure that the operation can be properly restored
//   on a restart.
class ReplicaTransactionFactory {
 public:
  virtual Status StartReplicaTransaction(const scoped_refptr<ConsensusRound>& context) = 0;

  virtual ~ReplicaTransactionFactory() {}
};

// Context for a consensus round on the LEADER side, typically created as an
// out-parameter of Consensus::Append.
// This class is ref-counted because we want to ensure it stays alive for the
// duration of the Transaction when it is associated with a Transaction, while
// we also want to ensure it has a proper lifecycle when a ConsensusRound is
// pushed that is not associated with a Tablet transaction.
class ConsensusRound : public RefCountedThreadSafe<ConsensusRound> {

 public:
  // Ctor used for leader transactions. Leader transactions can and must specify the
  // callbacks prior to initiating the consensus round.
  ConsensusRound(Consensus* consensus, gscoped_ptr<ReplicateMsg> replicate_msg,
                 ConsensusReplicatedCallback replicated_cb);

  // Ctor used for follower/learner transactions. These transactions do not use the
  // replicate callback and the commit callback is set later, after the transaction
  // is actually started.
  ConsensusRound(Consensus* consensus,
                 const ReplicateRefPtr& replicate_msg);

  ReplicateMsg* replicate_msg() {
    return replicate_msg_->get();
  }

  const ReplicateRefPtr& replicate_scoped_refptr() {
    return replicate_msg_;
  }

  // Returns the id of the (replicate) operation this context
  // refers to. This is only set _after_ Consensus::Replicate(context).
  OpId id() const {
    return replicate_msg_->get()->id();
  }

  // Register a callback that is called by Consensus to notify that the round
  // is considered either replicated, if 'status' is OK(), or that it has
  // permanently failed to replicate if 'status' is anything else. If 'status'
  // is OK() then the operation can be applied to the state machine, otherwise
  // the operation should be aborted.
  void SetConsensusReplicatedCallback(const ConsensusReplicatedCallback& replicated_cb) {
    replicated_cb_ = replicated_cb;
  }

  // If a continuation was set, notifies it that the round has been replicated.
  void NotifyReplicationFinished(const Status& status);

  // Binds this round such that it may not be eventually executed in any term
  // other than 'term'.
  // See CheckBoundTerm().
  void BindToTerm(int64_t term) {
    DCHECK_EQ(bound_term_, -1);
    bound_term_ = term;
  }

  // Check for a rare race in which an operation is submitted to the LEADER in some term,
  // then before the operation is prepared, the replica loses its leadership, receives
  // more operations as a FOLLOWER, and then regains its leadership. We detect this case
  // by setting the ConsensusRound's "bound term" when it is first submitted to the
  // PREPARE queue, and validate that the term is still the same when we have finished
  // preparing it. See KUDU-597 for details.
  //
  // If this round has not been bound to any term, this is a no-op.
  Status CheckBoundTerm(int64_t current_term) const;

 private:
  friend class RaftConsensusQuorumTest;
  friend class RefCountedThreadSafe<ConsensusRound>;

  ~ConsensusRound() {}

  Consensus* consensus_;
  // This round's replicate message.
  ReplicateRefPtr replicate_msg_;

  // The continuation that will be called once the transaction is
  // deemed committed/aborted by consensus.
  ConsensusReplicatedCallback replicated_cb_;

  // The leader term that this round was submitted in. CheckBoundTerm()
  // ensures that, when it is eventually replicated, the term has not
  // changed in the meantime.
  //
  // Set to -1 if no term has been bound.
  int64_t bound_term_;
};

class Consensus::ConsensusFaultHooks {
 public:
  virtual Status PreStart() { return Status::OK(); }
  virtual Status PostStart() { return Status::OK(); }
  virtual Status PreConfigChange() { return Status::OK(); }
  virtual Status PostConfigChange() { return Status::OK(); }
  virtual Status PreReplicate() { return Status::OK(); }
  virtual Status PostReplicate() { return Status::OK(); }
  virtual Status PreUpdate() { return Status::OK(); }
  virtual Status PostUpdate() { return Status::OK(); }
  virtual Status PreShutdown() { return Status::OK(); }
  virtual Status PostShutdown() { return Status::OK(); }
  virtual ~ConsensusFaultHooks() {}
};

} // namespace consensus
} // namespace kudu

#endif /* CONSENSUS_H_ */
