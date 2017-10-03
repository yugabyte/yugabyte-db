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

#ifndef KUDU_CONSENSUS_CONSENSUS_QUEUE_H_
#define KUDU_CONSENSUS_CONSENSUS_QUEUE_H_

#include <iosfwd>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/log_cache.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/ref_counted_replicate.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"

namespace kudu {
template<class T>
class AtomicGauge;
class MemTracker;
class MetricEntity;
class ThreadPool;

namespace log {
class Log;
class AsyncLogReader;
}

namespace consensus {
class PeerMessageQueueObserver;

// The id for the server-wide consensus queue MemTracker.
extern const char kConsensusQueueParentTrackerId[];

// Tracks the state of the peers and which transactions they have replicated.
// Owns the LogCache which actually holds the replicate messages which are
// en route to the various peers.
//
// This also takes care of pushing requests to peers as new operations are
// added, and notifying RaftConsensus when the commit index advances.
//
// This class is used only on the LEADER side.
//
// TODO Right now this class is able to track one outstanding operation
// per peer. If we want to have more than one outstanding RPC we need to
// modify it.
class PeerMessageQueue {
 public:
  struct TrackedPeer {
    explicit TrackedPeer(std::string uuid)
        : uuid(std::move(uuid)),
          is_new(true),
          next_index(kInvalidOpIdIndex),
          last_received(MinimumOpId()),
          last_known_committed_idx(MinimumOpId().index()),
          is_last_exchange_successful(false),
          last_successful_communication_time(MonoTime::Now(MonoTime::FINE)),
          needs_remote_bootstrap(false),
          last_seen_term_(0) {}

    // Check that the terms seen from a given peer only increase
    // monotonically.
    void CheckMonotonicTerms(int64_t term) {
      DCHECK_GE(term, last_seen_term_);
      last_seen_term_ = term;
    }

    std::string ToString() const;

    // UUID of the peer.
    const std::string uuid;

    // Whether this is a newly tracked peer.
    bool is_new;

    // Next index to send to the peer.
    // This corresponds to "nextIndex" as specified in Raft.
    int64_t next_index;

    // The last operation that we've sent to this peer and that
    // it acked. Used for watermark movement.
    OpId last_received;

    // The last committed index this peer knows about.
    int64_t last_known_committed_idx;

    // Whether the last exchange with this peer was successful.
    bool is_last_exchange_successful;

    // The time of the last communication with the peer.
    // Defaults to the time of construction, so does not necessarily mean that
    // successful communication ever took place.
    MonoTime last_successful_communication_time;

    // Whether the follower was detected to need remote bootstrap.
    bool needs_remote_bootstrap;

   private:
    // The last term we saw from a given peer.
    // This is only used for sanity checking that a peer doesn't
    // go backwards in time.
    int64_t last_seen_term_;
  };

  PeerMessageQueue(const scoped_refptr<MetricEntity>& metric_entity,
                   const scoped_refptr<log::Log>& log,
                   const RaftPeerPB& local_peer_pb,
                   const std::string& tablet_id);

  // Initialize the queue.
  virtual void Init(const OpId& last_locally_replicated);

  // Changes the queue to leader mode, meaning it tracks majority replicated
  // operations and notifies observers when those change.
  // 'committed_index' corresponds to the id of the last committed operation,
  // i.e. operations with ids <= 'committed_index' should be considered committed.
  // 'current_term' corresponds to the leader's current term, this is different
  // from 'committed_index.term()' if the leader has not yet committed an
  // operation in the current term.
  // 'active_config' is the currently-active Raft config. This must always be
  // a superset of the tracked peers, and that is enforced with runtime CHECKs.
  virtual void SetLeaderMode(const OpId& committed_index,
                             int64_t current_term,
                             const RaftConfigPB& active_config);

  // Changes the queue to non-leader mode. Currently tracked peers will still
  // be tracked so that the cache is only evicted when the peers no longer need
  // the operations but the queue will no longer advance the majority replicated
  // index or notify observers of its advancement.
  virtual void SetNonLeaderMode();

  // Makes the queue track this peer.
  virtual void TrackPeer(const std::string& peer_uuid);

  // Makes the queue untrack this peer.
  virtual void UntrackPeer(const std::string& peer_uuid);

  // Appends a single message to be replicated to the peers.
  // Returns OK unless the message could not be added to the queue for some
  // reason (e.g. the queue reached max size).
  // If it returns OK the queue takes ownership of 'msg'.
  //
  // This is thread-safe against all of the read methods, but not thread-safe
  // with concurrent Append calls.
  virtual Status AppendOperation(const ReplicateRefPtr& msg);

  // Appends a vector of messages to be replicated to the peers.
  // Returns OK unless the message could not be added to the queue for some
  // reason (e.g. the queue reached max size), calls 'log_append_callback' when
  // the messages are durable in the local Log.
  // If it returns OK the queue takes ownership of 'msgs'.
  //
  // This is thread-safe against all of the read methods, but not thread-safe
  // with concurrent Append calls.
  virtual Status AppendOperations(const std::vector<ReplicateRefPtr>& msgs,
                                  const StatusCallback& log_append_callback);

  // Assembles a request for a peer, adding entries past 'op_id' up to
  // 'consensus_max_batch_size_bytes'.
  // Returns OK if the request was assembled, or Status::NotFound() if the
  // peer with 'uuid' was not tracked, of if the queue is not in leader mode.
  // Returns Status::Incomplete if we try to read an operation index from the
  // log that has not been written.
  //
  // WARNING: In order to avoid copying the same messages to every peer,
  // entries are added to 'request' via AddAllocated() methods.
  // The owner of 'request' is expected not to delete the request prior
  // to removing the entries through ExtractSubRange() or any other method
  // that does not delete the entries. The simplest way is to pass the same
  // instance of ConsensusRequestPB to RequestForPeer(): the buffer will
  // replace the old entries with new ones without de-allocating the old
  // ones if they are still required.
  virtual Status RequestForPeer(const std::string& uuid,
                                ConsensusRequestPB* request,
                                std::vector<ReplicateRefPtr>* msg_refs,
                                bool* needs_remote_bootstrap);

  // Fill in a StartRemoteBootstrapRequest for the specified peer.
  // If that peer should not remotely bootstrap, returns a non-OK status.
  // On success, also internally resets peer->needs_remote_bootstrap to false.
  virtual Status GetRemoteBootstrapRequestForPeer(const std::string& uuid,
                                                  StartRemoteBootstrapRequestPB* req);

  // Update the last successful communication timestamp for the given peer
  // to the current time. This should be called when a non-network related
  // error is received from the peer, indicating that it is alive, even if it
  // may not be fully up and running or able to accept updates.
  void NotifyPeerIsResponsiveDespiteError(const std::string& peer_uuid);

  // Updates the request queue with the latest response of a peer, returns
  // whether this peer has more requests pending.
  virtual void ResponseFromPeer(const std::string& peer_uuid,
                                const ConsensusResponsePB& response,
                                bool* more_pending);

  // Closes the queue, peers are still allowed to call UntrackPeer() and
  // ResponseFromPeer() but no additional peers can be tracked or messages
  // queued.
  virtual void Close();

  virtual int64_t GetQueuedOperationsSizeBytesForTests() const;

  // Returns the last message replicated by all peers, for tests.
  virtual OpId GetAllReplicatedIndexForTests() const;


  virtual OpId GetCommittedIndexForTests() const;

  // Returns the current majority replicated OpId, for tests.
  virtual OpId GetMajorityReplicatedOpIdForTests() const;

  // Returns a copy of the TrackedPeer with 'uuid' or crashes if the peer is
  // not being tracked.
  virtual TrackedPeer GetTrackedPeerForTests(std::string uuid);

  virtual std::string ToString() const;

  // Dumps the contents of the queue to the provided string vector.
  virtual void DumpToStrings(std::vector<std::string>* lines) const;

  virtual void DumpToHtml(std::ostream& out) const;

  virtual void RegisterObserver(PeerMessageQueueObserver* observer);

  virtual Status UnRegisterObserver(PeerMessageQueueObserver* observer);

  struct Metrics {
    // Keeps track of the number of ops. that are completed by a majority but still need
    // to be replicated to a minority (IsDone() is true, IsAllDone() is false).
    scoped_refptr<AtomicGauge<int64_t> > num_majority_done_ops;
    // Keeps track of the number of ops. that are still in progress (IsDone() returns false).
    scoped_refptr<AtomicGauge<int64_t> > num_in_progress_ops;

    explicit Metrics(const scoped_refptr<MetricEntity>& metric_entity);
  };

  virtual ~PeerMessageQueue();

 private:
  FRIEND_TEST(ConsensusQueueTest, TestQueueAdvancesCommittedIndex);

  // Mode specifies how the queue currently behaves:
  // LEADER - Means the queue tracks remote peers and replicates whatever messages
  //          are appended. Observers are notified of changes.
  // NON_LEADER - Means the queue only tracks the local peer (remote peers are ignored).
  //              Observers are not notified of changes.
  enum Mode {
    LEADER,
    NON_LEADER
  };

  enum State {
    kQueueConstructed,
    kQueueOpen,
    kQueueClosed
  };

  struct QueueState {

    // The first operation that has been replicated to all currently
    // tracked peers.
    OpId all_replicated_opid;

    // The index of the last operation replicated to a majority.
    // This is usually the same as 'committed_index' but might not
    // be if the terms changed.
    OpId majority_replicated_opid;

    // The index of the last operation to be considered committed.
    OpId committed_index;

    // The opid of the last operation appended to the queue.
    OpId last_appended;

    // The queue's owner current_term.
    // Set by the last appended operation.
    // If the queue owner's term is less than the term observed
    // from another peer the queue owner must step down.
    // TODO: it is likely to be cleaner to get this from the ConsensusMetadata
    // rather than by snooping on what operations are appended to the queue.
    int64_t current_term;

    // The size of the majority for the queue.
    int majority_size_;

    State state;

    // The current mode of the queue.
    Mode mode;

    // The currently-active raft config. Only set if in LEADER mode.
    gscoped_ptr<RaftConfigPB> active_config;

    std::string ToString() const;
  };

  // Returns true iff given 'desired_op' is found in the local WAL.
  // If the op is not found, returns false.
  // If the log cache returns some error other than NotFound, crashes with a
  // fatal error.
  bool IsOpInLog(const OpId& desired_op) const;

  void NotifyObserversOfMajorityReplOpChange(const OpId new_majority_replicated_op);
  void NotifyObserversOfMajorityReplOpChangeTask(const OpId new_majority_replicated_op);

  void NotifyObserversOfTermChange(int64_t term);
  void NotifyObserversOfTermChangeTask(int64_t term);

  void NotifyObserversOfFailedFollower(const std::string& uuid,
                                       int64_t term,
                                       const std::string& reason);
  void NotifyObserversOfFailedFollowerTask(const std::string& uuid,
                                           int64_t term,
                                           const std::string& reason);

  typedef std::unordered_map<std::string, TrackedPeer*> PeersMap;

  std::string ToStringUnlocked() const;

  std::string LogPrefixUnlocked() const;

  void DumpToStringsUnlocked(std::vector<std::string>* lines) const;

  // Updates the metrics based on index math.
  void UpdateMetrics();

  void ClearUnlocked();

  // Returns the last operation in the message queue, or
  // 'preceding_first_op_in_queue_' if the queue is empty.
  const OpId& GetLastOp() const;

  void TrackPeerUnlocked(const std::string& uuid);

  // Checks that if the queue is in LEADER mode then all registered peers are
  // in the active config. Crashes with a FATAL log message if this invariant
  // does not hold. If the queue is in NON_LEADER mode, does nothing.
  void CheckPeersInActiveConfigIfLeaderUnlocked() const;

  // Callback when a REPLICATE message has finished appending to the local log.
  void LocalPeerAppendFinished(const OpId& id,
                               const StatusCallback& callback,
                               const Status& status);

  // Advances 'watermark' to the smallest op that 'num_peers_required' have.
  void AdvanceQueueWatermark(const char* type,
                             OpId* watermark,
                             const OpId& replicated_before,
                             const OpId& replicated_after,
                             int num_peers_required,
                             const TrackedPeer* who_caused);

  std::vector<PeerMessageQueueObserver*> observers_;

  // The pool which executes observer notifications.
  // TODO consider reusing a another pool.
  gscoped_ptr<ThreadPool> observers_pool_;

  // PB containing identifying information about the local peer.
  const RaftPeerPB local_peer_pb_;

  // The id of the tablet.
  const std::string tablet_id_;

  QueueState queue_state_;

  // The currently tracked peers.
  PeersMap peers_map_;
  mutable simple_spinlock queue_lock_; // TODO: rename

  // We assume that we never have multiple threads racing to append to the queue.
  // This fake mutex adds some extra assurance that this implementation property
  // doesn't change.
  DFAKE_MUTEX(append_fake_lock_);

  LogCache log_cache_;

  Metrics metrics_;
};

// The interface between RaftConsensus and the PeerMessageQueue.
class PeerMessageQueueObserver {
 public:
  // Called by the queue each time the response for a peer is handled with
  // the resulting majority replicated index.
  // The consensus implementation decides the commit index based on that
  // and triggers the apply for pending transactions.
  // 'committed_index' is set to the id of the last operation considered
  // committed by consensus.
  // The implementation is idempotent, i.e. independently of the ordering of
  // calls to this method only non-triggered applys will be started.
  virtual void UpdateMajorityReplicated(const OpId& majority_replicated,
                                        OpId* committed_index) = 0;

  // Notify the Consensus implementation that a follower replied with a term
  // higher than that established in the queue.
  virtual void NotifyTermChange(int64_t term) = 0;

  // Notify Consensus that a peer is unable to catch up due to falling behind
  // the leader's log GC threshold.
  virtual void NotifyFailedFollower(const std::string& peer_uuid,
                                    int64_t term,
                                    const std::string& reason) = 0;

  virtual ~PeerMessageQueueObserver() {}
};

}  // namespace consensus
}  // namespace kudu

#endif /* KUDU_CONSENSUS_CONSENSUS_QUEUE_H_ */
