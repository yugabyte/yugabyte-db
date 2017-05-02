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

#ifndef YB_CONSENSUS_RAFT_CONSENSUS_H_
#define YB_CONSENSUS_RAFT_CONSENSUS_H_

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/optional/optional_fwd.hpp>
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus_queue.h"
#include "yb/util/atomic.h"
#include "yb/util/failure_detector.h"

namespace yb {

typedef std::lock_guard<simple_spinlock> Lock;
typedef gscoped_ptr<Lock> ScopedLock;

class Counter;
class FailureDetector;
class HostPort;
class ThreadPool;

namespace server {
class Clock;
}

namespace rpc {
class Messenger;
}

namespace consensus {
class ConsensusMetadata;
class Peer;
class PeerProxyFactory;
class PeerManager;
class ReplicaState;
struct ElectionResult;

class RaftConsensus : public Consensus,
                      public PeerMessageQueueObserver {
 public:
  class ConsensusFaultHooks;

  static scoped_refptr<RaftConsensus> Create(
    const ConsensusOptions& options,
    gscoped_ptr<ConsensusMetadata> cmeta,
    const RaftPeerPB& local_peer_pb,
    const scoped_refptr<MetricEntity>& metric_entity,
    const scoped_refptr<server::Clock>& clock,
    ReplicaTransactionFactory* txn_factory,
    const std::shared_ptr<rpc::Messenger>& messenger,
    const scoped_refptr<log::Log>& log,
    const std::shared_ptr<MemTracker>& parent_mem_tracker,
    const Callback<void(std::shared_ptr<StateChangeContext> context)> mark_dirty_clbk,
    TableType table_type);

  RaftConsensus(const ConsensusOptions& options,
    gscoped_ptr<ConsensusMetadata> cmeta,
    gscoped_ptr<PeerProxyFactory> peer_proxy_factory,
    gscoped_ptr<PeerMessageQueue> queue,
    gscoped_ptr<PeerManager> peer_manager,
    gscoped_ptr<ThreadPool> thread_pool,
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::string& peer_uuid,
    const scoped_refptr<server::Clock>& clock,
    ReplicaTransactionFactory* txn_factory,
    const scoped_refptr<log::Log>& log,
    std::shared_ptr<MemTracker> parent_mem_tracker,
    Callback<void(std::shared_ptr<StateChangeContext> context)> mark_dirty_clbk,
    TableType table_type);

  virtual ~RaftConsensus();

  virtual CHECKED_STATUS Start(const ConsensusBootstrapInfo& info) override;

  virtual bool IsRunning() const override;

  // Emulates an election by increasing the term number and asserting leadership
  // in the configuration by sending a NO_OP to other peers.
  // This is NOT safe to use in a distributed configuration with failure detection
  // enabled, as it could result in a split-brain scenario.
  virtual CHECKED_STATUS EmulateElection() override;

  virtual CHECKED_STATUS StartElection(
      ElectionMode mode,
      const bool pending_commit = false,
      const OpId& must_be_committed_opid = OpId::default_instance(),
      const std::string& originator_uuid = std::string()) override;

  virtual CHECKED_STATUS ElectionLostByProtege(const std::string& election_lost_by_uuid) override;

  virtual CHECKED_STATUS WaitUntilLeaderForTests(const MonoDelta& timeout) override;

  virtual CHECKED_STATUS StepDown(const LeaderStepDownRequestPB* req,
                          LeaderStepDownResponsePB* resp) override;

  // Call StartElection(), log a warning if the call fails (usually due to
  // being shut down).
  void ReportFailureDetected(const std::string& name, const Status& msg);

  virtual CHECKED_STATUS Replicate(const scoped_refptr<ConsensusRound>& round) override;

  virtual CHECKED_STATUS CheckLeadershipAndBindTerm(const scoped_refptr<ConsensusRound>& round)
      override;

  virtual CHECKED_STATUS Update(
      ConsensusRequestPB* request,
      ConsensusResponsePB* response) override;

  virtual CHECKED_STATUS RequestVote(const VoteRequestPB* request,
                             VoteResponsePB* response) override;

  virtual CHECKED_STATUS ChangeConfig(const ChangeConfigRequestPB& req,
                              const StatusCallback& client_cb,
                              boost::optional<tserver::TabletServerErrorPB::Code>* error_code)
                              override;

  RaftPeerPB::Role GetRoleUnlocked() const;

  virtual RaftPeerPB::Role role() const override;

  LeaderStatus leader_status() const override;

  virtual std::string peer_uuid() const override;

  virtual std::string tablet_id() const override;

  virtual ConsensusStatePB ConsensusState(ConsensusConfigType type) const override;

  virtual ConsensusStatePB ConsensusStateUnlocked(ConsensusConfigType type) const override;

  virtual RaftConfigPB CommittedConfig() const override;

  virtual void DumpStatusHtml(std::ostream& out) const override;

  virtual void Shutdown() override;

  // Return the active (as opposed to committed) role.
  RaftPeerPB::Role GetActiveRole() const;

  // Returns the replica state for tests. This should never be used outside of
  // tests, in particular calling the LockFor* methods on the returned object
  // can cause consensus to deadlock.
  ReplicaState* GetReplicaStateForTests();

  // Updates the committed_index and triggers the Apply()s for whatever
  // transactions were pending.
  // This is idempotent.
  void UpdateMajorityReplicated(const OpId& majority_replicated,
                                OpId* committed_index) override;

  virtual void NotifyTermChange(int64_t term) override;

  virtual void NotifyFailedFollower(const std::string& uuid,
                                    int64_t term,
                                    const std::string& reason) override;

  virtual CHECKED_STATUS GetLastOpId(OpIdType type, OpId* id) override;

 protected:
  // Trigger that a non-Transaction ConsensusRound has finished replication.
  // If the replication was successful, an status will be OK. Otherwise, it
  // may be Aborted or some other error status.
  // If 'status' is OK, write a Commit message to the local WAL based on the
  // type of message it is.
  // The 'client_cb' will be invoked at the end of this execution.
  virtual void NonTxRoundReplicationFinished(ConsensusRound* round,
                                             const StatusCallback& client_cb,
                                             const Status& status);

  // As a leader, append a new ConsensusRond to the queue.
  // Only virtual and protected for mocking purposes.
  virtual CHECKED_STATUS AppendNewRoundToQueueUnlocked(const scoped_refptr<ConsensusRound>& round);

  // As a follower, start a consensus round not associated with a Transaction.
  // Only virtual and protected for mocking purposes.
  virtual CHECKED_STATUS StartConsensusOnlyRoundUnlocked(const ReplicateRefPtr& msg);

 private:
  friend class ReplicaState;
  friend class RaftConsensusQuorumTest;

  // Control whether printing of log messages should be done for a particular
  // function call.
  enum AllowLogging {
    DO_NOT_LOG = 0,
    ALLOW_LOGGING = 1,
  };

  // Helper struct that contains the messages from the leader that we need to
  // append to our log, after they've been deduplicated.
  struct LeaderRequest {
    std::string leader_uuid;
    const OpId* preceding_opid;
    std::vector<ReplicateRefPtr> messages;
    // The positional index of the first message selected to be appended, in the
    // original leader's request message sequence.
    int64_t first_message_idx;

    std::string OpsRangeString() const;
  };

  std::string LogPrefixUnlocked();

  std::string LogPrefix();

  // Return true if the peer could become a leader during RAFT consensus start.
  bool ShouldBecomeLeaderOnStart();

  // Set the leader UUID of the configuration and mark the tablet config dirty for
  // reporting to the master.
  void SetLeaderUuidUnlocked(const std::string& uuid);

  // Replicate (as leader) a pre-validated config change. This includes
  // updating the peers and setting the new_configuration as pending.
  CHECKED_STATUS ReplicateConfigChangeUnlocked(const ReplicateRefPtr& replicate_ref,
                                       const RaftConfigPB& new_config,
                                       ChangeConfigType type,
                                       const StatusCallback& client_cb);

  // Update the peers and queue to be consistent with a new active configuration.
  // Should only be called by the leader.
  CHECKED_STATUS RefreshConsensusQueueAndPeersUnlocked();

  // Makes the peer become leader.
  // Returns OK once the change config transaction that has this peer as leader
  // has been enqueued, the transaction will complete asynchronously.
  //
  // The ReplicaState must be locked for configuration change before calling.
  CHECKED_STATUS BecomeLeaderUnlocked();

  // Makes the peer become a replica, i.e. a FOLLOWER or a LEARNER.
  //
  // The ReplicaState must be locked for configuration change before calling.
  CHECKED_STATUS BecomeReplicaUnlocked();

  // Updates the state in a replica by storing the received operations in the log
  // and triggering the required transactions. This method won't return until all
  // operations have been stored in the log and all Prepares() have been completed,
  // and a replica cannot accept any more Update() requests until this is done.
  CHECKED_STATUS UpdateReplica(
      ConsensusRequestPB* request,
      ConsensusResponsePB* response);

  // Deduplicates an RPC request making sure that we get only messages that we
  // haven't appended to our log yet.
  // On return 'deduplicated_req' is instantiated with only the new messages
  // and the correct preceding id.
  void DeduplicateLeaderRequestUnlocked(ConsensusRequestPB* rpc_req,
                                        LeaderRequest* deduplicated_req);

  // Handles a request from a leader, refusing the request if the term is lower than
  // ours or stepping down if it's higher.
  CHECKED_STATUS HandleLeaderRequestTermUnlocked(const ConsensusRequestPB* request,
                                         ConsensusResponsePB* response);

  // Checks that the preceding op in 'req' is locally committed or pending and sets an
  // appropriate error message in 'response' if not.
  // If there is term mismatch between the preceding op id in 'req' and the local log's
  // pending operations, we proactively abort those pending operations after and including
  // the preceding op in 'req' to avoid a pointless cache miss in the leader's log cache.
  CHECKED_STATUS EnforceLogMatchingPropertyMatchesUnlocked(const LeaderRequest& req,
                                                   ConsensusResponsePB* response);

  // Checks that deduplicated messages in an UpdateConsensus request are in the right order.
  CHECKED_STATUS CheckLeaderRequestOpIdSequence(
      const LeaderRequest& deduped_req,
      ConsensusRequestPB* request);

  // Check a request received from a leader, making sure:
  // - The request is in the right term
  // - The log matching property holds
  // - Messages are de-duplicated so that we only process previously unprocessed requests.
  // - We abort transactions if the leader sends transactions that have the same index as
  //   transactions currently on the pendings set, but different terms.
  // If this returns ok and the response has no errors, 'deduped_req' is set with only
  // the messages to add to our state machine.
  CHECKED_STATUS CheckLeaderRequestUnlocked(
      ConsensusRequestPB* request,
      ConsensusResponsePB* response,
      LeaderRequest* deduped_req);

  // Returns the most recent OpId written to the Log.
  OpId GetLatestOpIdFromLog();

  // Begin a replica transaction. If the type of message in 'msg' is not a type
  // that uses transactions, delegates to StartConsensusOnlyRoundUnlocked().
  CHECKED_STATUS StartReplicaTransactionUnlocked(const ReplicateRefPtr& msg);

  // Return header string for RequestVote log messages. The ReplicaState lock must be held.
  std::string GetRequestVoteLogPrefixUnlocked() const;

  // Fills the response with the current status, if an update was successful.
  void FillConsensusResponseOKUnlocked(ConsensusResponsePB* response);

  // Fills the response with an error code and error message.
  void FillConsensusResponseError(ConsensusResponsePB* response,
                                  ConsensusErrorPB::Code error_code,
                                  const Status& status);

  // Fill VoteResponsePB with the following information:
  // - Update responder_term to current local term.
  // - Set vote_granted to true.
  void FillVoteResponseVoteGranted(VoteResponsePB* response);

  // Fill VoteResponsePB with the following information:
  // - Update responder_term to current local term.
  // - Set vote_granted to false.
  // - Set consensus_error.code to the given code.
  void FillVoteResponseVoteDenied(ConsensusErrorPB::Code error_code, VoteResponsePB* response);

  // Respond to VoteRequest that the candidate has an old term.
  CHECKED_STATUS RequestVoteRespondInvalidTerm(const VoteRequestPB* request,
                                               VoteResponsePB* response);

  // Respond to VoteRequest that we already granted our vote to the candidate.
  CHECKED_STATUS RequestVoteRespondVoteAlreadyGranted(const VoteRequestPB* request,
                                              VoteResponsePB* response);

  // Respond to VoteRequest that we already granted our vote to someone else.
  CHECKED_STATUS RequestVoteRespondAlreadyVotedForOther(const VoteRequestPB* request,
                                                VoteResponsePB* response);

  // Respond to VoteRequest that the candidate's last-logged OpId is too old.
  CHECKED_STATUS RequestVoteRespondLastOpIdTooOld(const OpId& local_last_opid,
                                          const VoteRequestPB* request,
                                          VoteResponsePB* response);

  // Respond to VoteRequest that the vote was not granted because we believe
  // the leader to be alive.
  CHECKED_STATUS RequestVoteRespondLeaderIsAlive(const VoteRequestPB* request,
                                         VoteResponsePB* response);

  // Respond to VoteRequest that the replica is already in the middle of servicing
  // another vote request or an update from a valid leader.
  CHECKED_STATUS RequestVoteRespondIsBusy(const VoteRequestPB* request,
                                  VoteResponsePB* response);

  // Respond to VoteRequest that the vote is granted for candidate.
  CHECKED_STATUS RequestVoteRespondVoteGranted(const VoteRequestPB* request,
                                       VoteResponsePB* response);

  void UpdateMajorityReplicatedUnlocked(const OpId& majority_replicated,
                                        OpId* committed_index);

  // Callback for leader election driver. ElectionCallback is run on the
  // reactor thread, so it simply defers its work to DoElectionCallback.
  void ElectionCallback(const std::string& originator_uuid, const ElectionResult& result);
  void DoElectionCallback(const std::string& originator_uuid, const ElectionResult& result);
  void NotifyOriginatorAboutLostElection(const std::string& originator_uuid);

  // Helper struct that tracks the RunLeaderElection as part of leadership transferral.
  struct RunLeaderElectionState {
    gscoped_ptr<PeerProxy> proxy;
    RunLeaderElectionRequestPB req;
    RunLeaderElectionResponsePB resp;
    rpc::RpcController rpc;
  };

  // Callback for RunLeaderElection async request.
  void RunLeaderElectionResponseRpcCallback(std::shared_ptr<RunLeaderElectionState> election_state);

  // Start tracking the leader for failures. This typically occurs at startup
  // and when the local peer steps down as leader.
  // If the failure detector is already registered, has no effect.
  CHECKED_STATUS EnsureFailureDetectorEnabledUnlocked();

  // Untrack the current leader from failure detector.
  // This typically happens when the local peer becomes leader.
  // If the failure detector is already unregistered, has no effect.
  CHECKED_STATUS EnsureFailureDetectorDisabledUnlocked();

  // Set the failure detector to an "expired" state, so that the next time
  // the failure monitor runs it triggers an election.
  // This is primarily intended to be used at startup time.
  CHECKED_STATUS ExpireFailureDetectorUnlocked();

  // "Reset" the failure detector to indicate leader activity.
  // The failure detector must currently be enabled.
  // When this is called a failure is guaranteed not to be detected
  // before 'FLAGS_leader_failure_max_missed_heartbeat_periods' *
  // 'FLAGS_raft_heartbeat_interval_ms' has elapsed.
  CHECKED_STATUS SnoozeFailureDetectorUnlocked();

  // Like the above but adds 'additional_delta' to the default timeout
  // period. If allow_logging is set to ALLOW_LOGGING, then this method
  // will print a log message when called.
  CHECKED_STATUS SnoozeFailureDetectorUnlocked(const MonoDelta& additional_delta,
                                       AllowLogging allow_logging);

  // Return the minimum election timeout. Due to backoff and random
  // jitter, election timeouts may be longer than this.
  MonoDelta MinimumElectionTimeout() const;

  // Calculates an additional snooze delta for leader election.
  // The additional delta increases exponentially with the difference
  // between the current term and the term of the last committed
  // operation.
  // The maximum delta is capped by 'FLAGS_leader_failure_exp_backoff_max_delta_ms'.
  MonoDelta LeaderElectionExpBackoffDeltaUnlocked();

  // Checks if the leader is ready to process a change config request (one requirement for this is
  // for it to have at least one committed op in the current term). Also checks that there are no
  // voters in transition in the active config state. CHECKED_STATUS OK() implies leader is ready.
  // server_uuid is the uuid of the server that we are trying to remove, add, or change its
  // role.
  CHECKED_STATUS IsLeaderReadyForChangeConfigUnlocked(ChangeConfigType type,
                                              const std::string& server_uuid);

  // Increment the term to the next term, resetting the current leader, etc.
  CHECKED_STATUS IncrementTermUnlocked();

  // Handle when the term has advanced beyond the current term.
  CHECKED_STATUS HandleTermAdvanceUnlocked(ConsensusTerm new_term);

  // Notify the tablet peer that the consensus configuration
  // has changed, thus reporting it back to the master. This is performed inline.
  void MarkDirty(std::shared_ptr<StateChangeContext> context);

  // Calls MarkDirty() if 'status' == OK. Then, always calls 'client_cb' with
  // 'status' as its argument.
  void MarkDirtyOnSuccess(std::shared_ptr<StateChangeContext> context,
                          const StatusCallback& client_cb,
                          const Status& status);

  // Attempt to remove the follower with the specified 'uuid' from the config,
  // if the 'committed_config' is still the committed config and if the current
  // node is the leader.
  //
  // Since this is inherently an asynchronous operation run on a thread pool,
  // it may fail due to the configuration changing, the local node losing
  // leadership, or the tablet shutting down.
  // Logs a warning on failure.
  void TryRemoveFollowerTask(const std::string& uuid,
                             const RaftConfigPB& committed_config,
                             const std::string& reason);

  // Helper API to check if the pending/committed configuration has a PRE_VOTER. Non-null return
  // string implies there are servers in transit.
  string ServersInTransitionMessage();

  // Prevent starting new election for some time, after we stepped down.
  // protege_uuid - in case of step down we remember our protege.
  // After that we use its UUID to check whether node that lost election is our active protege.
  // There could be case that we already initiated another stepdown, and after that we received
  // delayed packet from old protege.
  // So this field allows us to filter out this situation.
  // Also we could introduce serial number of stepdown and filter using it.
  // That woule be more robust, since it handles also situation when we tried to stepdown
  // to the same node twice, and first retry was delayed, but second procedure is on the way.
  void WithholdElectionAfterStepDown(const std::string& protege_uuid);

  // Threadpool for constructing requests to peers, handling RPC callbacks,
  // etc.
  gscoped_ptr<ThreadPool> thread_pool_;

  scoped_refptr<log::Log> log_;
  scoped_refptr<server::Clock> clock_;
  gscoped_ptr<PeerProxyFactory> peer_proxy_factory_;

  gscoped_ptr<PeerManager> peer_manager_;

  // The queue of messages that must be sent to peers.
  gscoped_ptr<PeerMessageQueue> queue_;

  gscoped_ptr<ReplicaState> state_;

  Random rng_;

  // TODO: Plumb this from RpcAndWebServerBase.
  RandomizedFailureMonitor failure_monitor_;

  scoped_refptr<FailureDetector> failure_detector_;

  // If any RequestVote() RPC arrives before this hybrid time,
  // the request will be ignored. This prevents abandoned or partitioned
  // nodes from disturbing the healthy leader.
  MonoTime withhold_votes_until_;

  // Tracks if the peer was a leader and had stepped down. Used to make it not stand for election
  // again too soon. Once it waits for the extended delay before starting an election, it will
  // go back to the same delay as before.
  bool just_stepped_down_ = false;

  // This leader is ready to serve only if NoOp was successfully committed
  // after the new leader successful election.
  bool leader_no_op_committed_ = false;

  // UUID of new desired leader
  std::string protege_leader_uuid_;

  // This is the time for which election should not start on this peer.
  MonoTime withhold_election_start_until_;

  const Callback<void(std::shared_ptr<StateChangeContext> context)> mark_dirty_clbk_;

  // TODO hack to serialize updates due to repeated/out-of-order messages
  // should probably be refactored out.
  //
  // Lock ordering note: If both this lock and the ReplicaState lock are to be
  // taken, this lock must be taken first.
  mutable simple_spinlock update_lock_;

  AtomicBool shutdown_;

  scoped_refptr<Counter> follower_memory_pressure_rejections_;
  scoped_refptr<AtomicGauge<int64_t> > term_metric_;

  std::shared_ptr<MemTracker> parent_mem_tracker_;

  TableType table_type_;
  DISALLOW_COPY_AND_ASSIGN(RaftConsensus);
};

}  // namespace consensus
}  // namespace yb

#endif /* YB_CONSENSUS_RAFT_CONSENSUS_H_ */
