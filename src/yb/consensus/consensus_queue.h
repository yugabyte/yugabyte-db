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
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/tablespace_parser.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_types.h"
#include "yb/consensus/log_cache.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/ref_counted.h"

#include "yb/server/clock.h"

#include "yb/rpc/strand.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"

namespace yb {
template<class T>
class AtomicGauge;
class MemTracker;
class MetricEntity;
class ThreadPoolToken;

namespace consensus {

class PeerMessageQueueObserver;
struct MajorityReplicatedData;

// The id for the server-wide consensus queue MemTracker.
extern const char kConsensusQueueParentTrackerId[];

// Utility structure to track value sent to and received by follower.
template <class Value>
struct FollowerWatermark {
  const Value initial;

  // When value is sent to follower, its value is written to last_sent.
  Value last_sent;

  // After follower successfully process our request, we copy value from last_sent to last_received.
  Value last_received;

  explicit FollowerWatermark(const Value& initial_ = Value())
      : initial(initial_), last_sent(initial_), last_received(initial_) {}

  void Reset() {
    last_sent = initial;
    last_received = initial;
  }

  void OnReplyFromFollower() {
    last_received = last_sent;
  }

  std::string ToString() const {
    return Format("{ last_sent: $0 last_received: $1 }", last_sent, last_received);
  }
};


// Tracks the state of the peers and which transactions they have replicated.  Owns the LogCache
// which actually holds the replicate messages which are en route to the various peers.
//
// This also takes care of pushing requests to peers as new operations are added, and notifying
// RaftConsensus when the commit index advances.
//
// TODO Currently this class is able to track one outstanding operation per peer. If we want to have
// more than one outstanding RPC we need to modify it.
class PeerMessageQueue {
 public:
  struct TrackedPeer {
    explicit TrackedPeer(const std::string& uuid)
        : uuid(uuid),
          last_known_committed_idx(OpId::Min().index),
          last_successful_communication_time(MonoTime::Now()) {}

    explicit TrackedPeer(const RaftPeerPB& raft_peer_pb)
        : uuid(raft_peer_pb.permanent_uuid()),
          last_known_committed_idx(OpId::Min().index),
          last_successful_communication_time(MonoTime::Now()) {
      cloud_info = raft_peer_pb.cloud_info();
      last_known_private_addr = std::vector<HostPortPB>(
          raft_peer_pb.last_known_private_addr().begin(),
          raft_peer_pb.last_known_private_addr().end());

      last_known_broadcast_addr = std::vector<HostPortPB>(
          raft_peer_pb.last_known_broadcast_addr().begin(),
          raft_peer_pb.last_known_broadcast_addr().end());
    }

    // Check that the terms seen from a given peer only increase monotonically.
    void CheckMonotonicTerms(int64_t term) {
      DCHECK_GE(term, last_seen_term_);
      last_seen_term_ = term;
    }

    std::string ToString() const;

    void ResetLeaderLeases();

    void ResetLastRequest();

    // UUID of the peer.
    const std::string uuid;

    // Whether this is a newly tracked peer.
    bool is_new = true;

    // Next index to send to the peer.  This corresponds to "nextIndex" as specified in Raft.
    int64_t next_index = kInvalidOpIdIndex;

    // Number of ops starting from next_index_ to retransmit.
    int64_t last_num_messages_sent = -1;

    // Number of retransmissions from same next_index_.
    int64_t current_retransmissions = -1;

    // The last operation that we've sent to this peer and that it acked. Used for watermark
    // movement.
    OpId last_received = yb::OpId::Min();

    // The last committed index this peer knows about.
    int64_t last_known_committed_idx;

    // The ID of the operation last applied by this peer.
    OpId last_applied;

    // Whether the last exchange with this peer was successful.
    bool is_last_exchange_successful = false;

    // The time of the last communication with the peer.
    // Defaults to the time of construction, so does not necessarily mean that
    // successful communication ever took place.
    MonoTime last_successful_communication_time;

    // Leader lease expiration from this follower's point of view.
    FollowerWatermark<CoarseTimePoint> leader_lease_expiration;

    // Leader hybrid time lease expiration from this follower's point of view.
    FollowerWatermark<MicrosTime> leader_ht_lease_expiration{
        HybridTime::kMin.GetPhysicalValueMicros()};

    // History cutoff from this follower's point of view.
    FollowerWatermark<HybridTime> history_cutoff{HybridTime::kMin};

    // Whether the follower was detected to need remote bootstrap.
    bool needs_remote_bootstrap = false;

    // Number of times this peer attempted bootstrap from a closest non-leader peer
    // that resulted in a failure.
    uint32_t failed_bootstrap_attempts_from_non_leader = 0;

    // Member type of this peer in the config.
    PeerMemberType member_type = PeerMemberType::UNKNOWN_MEMBER_TYPE;

    uint64_t num_sst_files = 0;

    std::optional<CloudInfoPB> cloud_info;

    std::vector<HostPortPB> last_known_private_addr;

    std::vector<HostPortPB> last_known_broadcast_addr;

   private:
    // The last term we saw from a given peer.
    // This is only used for sanity checking that a peer doesn't
    // go backwards in time.
    int64_t last_seen_term_ = 0;
  };

  PeerMessageQueue(const scoped_refptr<MetricEntity>& metric_entity,
                   const scoped_refptr<log::Log>& log,
                   const std::shared_ptr<MemTracker>& server_tracker,
                   const std::shared_ptr<MemTracker>& parent_tracker,
                   const RaftPeerPB& local_peer_pb,
                   const std::string& tablet_id,
                   const server::ClockPtr& clock,
                   ConsensusContext* context,
                   std::unique_ptr<rpc::Strand> observers_strand);

  // Initialize the queue.
  virtual void Init(const OpId& last_locally_replicated);

  // Changes the queue to leader mode, meaning it tracks majority replicated operations and notifies
  // observers when those change.
  //
  // 'committed_index' corresponds to the id of the last committed operation, i.e. operations with
  // ids <= 'committed_index' should be considered committed.
  //
  // 'current_term' corresponds to the leader's current term, this is different from
  // 'committed_index.term()' if the leader has not yet committed an operation in the current term.
  //
  // 'active_config' is the currently-active Raft config. This must always be a superset of the
  // tracked peers, and that is enforced with runtime CHECKs.
  virtual void SetLeaderMode(const OpId& committed_op_id,
                             int64_t current_term,
                             const OpId& last_applied_op_id,
                             const RaftConfigPB& active_config);

  // Changes the queue to non-leader mode. Currently tracked peers will still be tracked so that the
  // cache is only evicted when the peers no longer need the operations but the queue will no longer
  // advance the majority replicated index or notify observers of its advancement.
  virtual void SetNonLeaderMode();

  // Makes the queue track this peer.
  virtual void TrackPeer(const std::string& peer_uuid);

  virtual void TrackPeer(const RaftPeerPB& raft_peer_pb);

  // Makes the queue untrack this peer.
  virtual void UntrackPeer(const std::string& peer_uuid);

  // Appends a single message to be replicated to the peers.  Returns OK unless the message could
  // not be added to the queue for some reason (e.g. the queue reached max size).
  //
  // If it returns OK the queue takes ownership of 'msg'.
  //
  // This is thread-safe against all of the read methods, but not thread-safe with concurrent Append
  // calls.
  Status TEST_AppendOperation(const ReplicateMsgPtr& msg);

  // Appends a vector of messages to be replicated to the peers.  Returns OK unless the message
  // could not be added to the queue for some reason (e.g. the queue reached max size). Calls
  // 'log_append_callback' when the messages are durable in the local Log.
  //
  // If it returns OK the queue takes ownership of 'msgs'.
  //
  // This is thread-safe against all of the read methods, but not thread-safe with concurrent Append
  // calls.
  //
  // It is possible that this method will be invoked with empty list of messages, when
  // we update committed op id.
  virtual Status AppendOperations(
      const ReplicateMsgs& msgs, const yb::OpId& committed_op_id,
      RestartSafeCoarseTimePoint batch_mono_time);

  // Assembles a request for a peer, adding entries past 'op_id' up to
  // 'consensus_max_batch_size_bytes'.
  //
  // Returns OK if the request was assembled, or STATUS(NotFound, "") if the peer with 'uuid' was
  // not tracked, or if the queue is not in leader mode.
  //
  // Returns STATUS(Incomplete, "") if we try to read an operation index from the log that has not
  // been written.
  //
  // WARNING: In order to avoid copying the same messages to every peer, entries are added to
  // 'request' via AddAllocated() methods.  The owner of 'request' is expected not to delete the
  // request prior to removing the entries through ExtractSubRange() or any other method that does
  // not delete the entries. The simplest way is to pass the same instance of ConsensusRequestPB to
  // RequestForPeer(): the buffer will replace the old entries with new ones without de-allocating
  // the old ones if they are still required.
  virtual Status RequestForPeer(
      const std::string& uuid,
      LWConsensusRequestPB* request,
      LWReplicateMsgsHolder* msgs_holder,
      bool* needs_remote_bootstrap,
      PeerMemberType* member_type = nullptr,
      bool* last_exchange_successful = nullptr);

  // Fill in a StartRemoteBootstrapRequest for the specified peer.  If that peer should not remotely
  // bootstrap, returns a non-OK status.  On success, also internally resets
  // peer->needs_remote_bootstrap to false.
  Status GetRemoteBootstrapRequestForPeer(
      const std::string& uuid,
      StartRemoteBootstrapRequestPB* req);

  // Scans the peers_map_ and returns a pointer to the closest peer for the passed remote peer.
  // Reutrns NULL if all tracked peers in the peers_map_ don't have CloudInfoPB specified.
  const TrackedPeer* FindClosestPeerForBootstrap(const TrackedPeer* remote_tracked_peer)
      REQUIRES(queue_lock_);

  // Increment failed_bootstrap_attempts_from_non_leader for the given peer. The method should only
  // be called when an attempt to bootstrap from a non-leader peer was made, and it resulted in an
  // error, where (error != ALREADY_IN_PROGRESS && error != TABLET_SPLIT_PARENT_STILL_LIVE) holds
  // true.
  // We do so because a remote bootstrap could only have been tried from the new peer when the above
  // expression holds true.
  void IncrementFailedBootstrapAttemptsFromNonLeader(const std::string& peer_uuid);

  // Update the last successful communication timestamp for the given peer to the current time. This
  // should be called when a non-network related error is received from the peer, indicating that it
  // is alive, even if it may not be fully up and running or able to accept updates.
  void NotifyPeerIsResponsiveDespiteError(const std::string& peer_uuid);

  // Updates the request queue with the latest response of a peer, returns whether this peer has
  // more requests pending.
  virtual bool ResponseFromPeer(const std::string& peer_uuid,
                                const LWConsensusResponsePB& response);

  void RequestWasNotSent(const std::string& peer_uuid);

  // Closes the queue, peers are still allowed to call UntrackPeer() and ResponseFromPeer() but no
  // additional peers can be tracked or messages queued.
  virtual void Close();

  // Returns the last message replicated by all peers, for tests.
  OpId TEST_GetAllReplicatedIndex() const;

  OpId TEST_GetCommittedIndex() const;

  OpId GetAllAppliedOpId() const;

  // Returns the current majority replicated OpId, for tests.
  OpId TEST_GetMajorityReplicatedOpId() const;

  OpId TEST_GetLastAppended() const;

  OpId TEST_GetLastAppliedOpId() const;

  // Returns true if specified peer accepted our lease request.
  bool PeerAcceptedOurLease(const std::string& uuid) const;

  // Returns a copy of the TrackedPeer with 'uuid' or crashes if the peer is not being tracked.
  TrackedPeer GetTrackedPeerForTests(std::string uuid);

  std::string ToString() const;

  void DumpToHtml(std::ostream& out) const;

  void RegisterObserver(PeerMessageQueueObserver* observer);

  Status UnRegisterObserver(PeerMessageQueueObserver* observer);

  bool CanPeerBecomeLeader(const std::string& peer_uuid) const;

  OpId PeerLastReceivedOpId(const TabletServerId& uuid) const;

  std::string GetUpToDatePeer() const;

  struct Metrics {
    // Keeps track of the number of ops. that are completed by a majority but still need
    // to be replicated to a minority (IsDone() is true, IsAllDone() is false).
    scoped_refptr<AtomicGauge<int64_t> > num_majority_done_ops;
    // Keeps track of the number of ops. that are still in progress (IsDone() returns false).
    scoped_refptr<AtomicGauge<int64_t> > num_in_progress_ops;

    explicit Metrics(const scoped_refptr<MetricEntity>& metric_entity);
  };

  virtual ~PeerMessageQueue();

  void NotifyObserversOfFailedFollower(const std::string& uuid,
                                       const std::string& reason);

  void SetContext(ConsensusContext* context) {
    context_ = context;
  }

  const CloudInfoPB& local_cloud_info() const {
    return local_peer_pb_.cloud_info();
  }

  // Read replicated log records starting from the OpId immediately after last_op_id.
  Result<ReadOpsResult> ReadReplicatedMessagesForCDC(
      const yb::OpId& last_op_id, int64_t* last_replicated_opid_index = nullptr,
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      const bool fetch_single_entry = false);

  Result<ReadOpsResult> ReadReplicatedMessagesForConsistentCDC(
      OpId last_op_id, uint64_t stream_safe_time, CoarseTimePoint deadline,
      bool fetch_single_entry = false, int64_t* last_replicated_opid_index = nullptr);

  Result<ReadOpsResult> ReadReplicatedMessagesInSegmentForCDC(
      const OpId& from_op_id, CoarseTimePoint deadline, bool fetch_single_entry = false,
      int64_t* last_committed_index = nullptr,
      HybridTime* consistent_stream_safe_time_footer = nullptr,
      bool* read_entire_wal = nullptr);

  void UpdateCDCConsumerOpId(const yb::OpId& op_id);

  // Get the maximum op ID that can be evicted for CDC consumer from log cache.
  yb::OpId GetCDCConsumerOpIdToEvict();

  size_t LogCacheSize();
  size_t EvictLogCache(size_t bytes_to_evict);

  // Start memory tracking of following operations in case they are still present in our caches.
  void TrackOperationsMemory(const OpIds& op_ids);

  const server::ClockPtr& clock() const {
    return clock_;
  }

  Result<OpId> TEST_GetLastOpIdWithType(int64_t max_allowed_index, OperationType op_type);

  std::vector<FollowerCommunicationTime> GetFollowerCommunicationTimes() const;

 private:
  FRIEND_TEST(ConsensusQueueTest, TestQueueAdvancesCommittedIndex);
  FRIEND_TEST(ConsensusQueueTest, TestReadReplicatedMessagesForCDC);

  // Mode specifies how the queue currently behaves:
  //
  // LEADER - Means the queue tracks remote peers and replicates whatever messages are appended.
  //          Observers are notified of changes.
  //
  // NON_LEADER - Means the queue only tracks the local peer (remote peers are ignored).  Observers
  //              are not notified of changes.
  enum class Mode {
    LEADER,
    NON_LEADER
  };

  static const char* ModeToStr(Mode mode);
  friend std::ostream& operator <<(std::ostream& out, Mode mode);

  enum class State {
    kQueueConstructed,
    kQueueOpen,
    kQueueClosed
  };

  static const char* StateToStr(State state);
  friend std::ostream& operator <<(std::ostream& out, State mode);

  static constexpr ssize_t kUninitializedMajoritySize = -1;

  struct QueueState {

    // The last operation that has been replicated to all currently tracked peers.
    OpId all_replicated_op_id = OpId::Min();

    // The last operation that has been replicated to all currently non-lagging tracked peers.
    OpId all_nonlagging_replicated_op_id = OpId::Min();

    // The last operation that has been applied by all currently tracked peers.
    OpId all_applied_op_id = OpId::Min();

    // The index of the last operation replicated to a majority.  This is usually the same as
    // 'committed_op_id' but might not be if the terms changed.
    OpId majority_replicated_op_id = OpId::Min();

    // The index of the last operation to be considered committed.
    OpId committed_op_id = OpId::Min();

    // The ID of the last applied operation.
    OpId last_applied_op_id = OpId::Min();

    // The opid of the last operation appended to the queue.
    OpId last_appended = OpId::Min();

    // The queue's owner current_term.  Set by the last appended operation.  If the queue owner's
    // term is less than the term observed from another peer the queue owner must step down.
    // TODO: it is likely to be cleaner to get this from the ConsensusMetadata rather than by
    // snooping on what operations are appended to the queue.
    int64_t current_term = OpId::Min().term;

    // The size of the majority for the queue.
    ssize_t majority_size_ = kUninitializedMajoritySize;

    State state = State::kQueueConstructed;

    // The current mode of the queue.
    Mode mode = Mode::NON_LEADER;

    // The currently-active raft config. Only set if in LEADER mode.
    std::unique_ptr<RaftConfigPB> active_config;

    std::string ToString() const;
  };

  // Returns true iff given 'desired_op' is found in the local WAL.
  // If the op is not found, returns false.
  // If the log cache returns some error other than NotFound, crashes with a fatal error.
  bool IsOpInLog(const yb::OpId& desired_op) const;

  void NotifyObserversOfMajorityReplOpChange(const MajorityReplicatedData& data);

  void NotifyObserversOfMajorityReplOpChangeTask(const MajorityReplicatedData& data);

  void NotifyObserversOfTermChange(int64_t term);

  void NotifyObserversOfFailedFollower(const std::string& uuid,
                                       int64_t term,
                                       const std::string& reason);

  template <class Func>
  void NotifyObservers(const char* title, Func&& func);

  typedef std::unordered_map<std::string, TrackedPeer*> PeersMap;

  std::string ToStringUnlocked() const;

  std::string LogPrefix() const;

  std::string LogPrefixUnlocked() const REQUIRES(queue_lock_);

  // Updates the metrics based on index math.
  void UpdateMetrics() REQUIRES(queue_lock_);

  void ClearUnlocked() REQUIRES(queue_lock_);

  // Returns the last operation in the message queue, or 'preceding_first_op_in_queue_' if the queue
  // is empty.
  const OpIdPB& GetLastOp() const;

  // Does the setup work required after adding a new tracked peer.
  TrackedPeer* SetupNewTrackedPeerUnlocked(
      std::unique_ptr<PeerMessageQueue::TrackedPeer> tracked_peer) REQUIRES(queue_lock_);

  TrackedPeer* TrackPeerUnlocked(const std::string& uuid) REQUIRES(queue_lock_);

  TrackedPeer* TrackPeerUnlocked(const RaftPeerPB& raft_peer_pb) REQUIRES(queue_lock_);

  // Checks that if the queue is in LEADER mode then all registered peers are in the active config.
  // Crashes with a FATAL log message if this invariant does not hold. If the queue is in NON_LEADER
  // mode, does nothing.
  void CheckPeersInActiveConfigIfLeaderUnlocked() const REQUIRES(queue_lock_);

  // Callback when a REPLICATE message has finished appending to the local log.
  void LocalPeerAppendFinished(const OpId& id, const Status& status);

  void NumSSTFilesChanged();

  // Updates op id replicated on each node.
  void UpdateAllReplicatedOpId(OpId* result) REQUIRES(queue_lock_);

  // Updates op ID applied on each node.
  void UpdateAllAppliedOpId(OpId* result) REQUIRES(queue_lock_);

  // Updates op id replicated on each non-lagging node.
  void UpdateAllNonLaggingReplicatedOpId(int32_t threshold) REQUIRES(queue_lock_);

  // Policy is responsible for tuning of watermark calculation.
  // I.e. simple leader lease or hybrid time leader lease etc.
  // It should provide result type and a function for extracting a value from a peer.
  template <class Policy>
  typename Policy::result_type GetWatermark() REQUIRES(queue_lock_);

  CoarseTimePoint LeaderLeaseExpirationWatermark() REQUIRES(queue_lock_);
  MicrosTime HybridTimeLeaseExpirationWatermark() REQUIRES(queue_lock_);
  OpId OpIdWatermark() REQUIRES(queue_lock_);
  uint64_t NumSSTFilesWatermark() REQUIRES(queue_lock_);

  // Reads operations from the log cache in the range (after_index, to_index].
  //
  // If 'to_index' is 0, then all operations after 'after_index' will be included.
  Result<ReadOpsResult> ReadFromLogCache(
      int64_t after_index,
      int64_t to_index,
      size_t max_batch_size,
      const std::string& peer_uuid,
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      const bool fetch_single_entry = false);

  Result<ReadOpsResult> ReadFromLogCacheForCDC(
      int64_t last_op_id_index,
      int64_t to_index,
      CoarseTimePoint deadline = CoarseTimePoint::max(),
      bool fetch_single_entry = false);

  std::pair<int64_t, int64_t> GetCommittedAndMajorityReplicatedIndex();

  int64_t GetStartOpIdIndex(int64_t start_index);

  void TEST_WaitForNotificationToFinish();

  std::vector<PeerMessageQueueObserver*> observers_;

  // The pool token which executes observer notifications.
  std::unique_ptr<rpc::Strand> notifications_strand_;

  // PB containing identifying information about the local peer.
  const RaftPeerPB local_peer_pb_;
  const yb::PeerId local_peer_uuid_;

  const TabletId tablet_id_;

  QueueState queue_state_ GUARDED_BY(queue_lock_);

  // The currently tracked peers.
  PeersMap peers_map_;
  TrackedPeer* local_peer_ = nullptr;

  using LockType = simple_spinlock;
  using LockGuard = std::lock_guard<LockType>;
  mutable LockType queue_lock_; // TODO: rename

  // We assume that we never have multiple threads racing to append to the queue.  This fake mutex
  // adds some extra assurance that this implementation property doesn't change.
  DFAKE_MUTEX(append_fake_lock_);

  LogCache log_cache_;

  std::shared_ptr<MemTracker> operations_mem_tracker_;

  Metrics metrics_;

  server::ClockPtr clock_;

  ConsensusContext* context_ = nullptr;
  bool installed_num_sst_files_changed_listener_ = false;

  // Used to protect cdc_consumer_op_id_ and cdc_consumer_op_id_last_updated_.
  mutable rw_spinlock cdc_consumer_lock_;
  yb::OpId cdc_consumer_op_id_ = yb::OpId::Max();
  CoarseTimePoint cdc_consumer_op_id_last_updated_ = ToCoarse(MonoTime::kMin);
};

inline std::ostream& operator <<(std::ostream& out, PeerMessageQueue::Mode mode) {
  return out << PeerMessageQueue::ModeToStr(mode);
}

inline std::ostream& operator <<(std::ostream& out, PeerMessageQueue::State state) {
  return out << PeerMessageQueue::StateToStr(state);
}

struct MajorityReplicatedData {
  OpId op_id;
  CoarseTimePoint leader_lease_expiration;
  MicrosTime ht_lease_expiration;
  uint64_t num_sst_files;

  // Update was caused by the following peer, that received all operations.
  TabletServerId peer_got_all_ops;

  std::string ToString() const;
};

// The interface between RaftConsensus and the PeerMessageQueue.
class PeerMessageQueueObserver {
 public:
  // Called by the queue each time the response for a peer is handled with the resulting majority
  // replicated index.  The consensus implementation decides the commit index based on that and
  // triggers the apply for pending transactions.
  //
  // 'committed_index' is set to the id of the last operation considered committed by consensus.
  // `last_applied_op_id` is set the ID of the last operation applied by consensus.
  //
  // The implementation is idempotent, i.e. independently of the ordering of calls to this method
  // only non-triggered applys will be started.
  virtual void UpdateMajorityReplicated(
      const MajorityReplicatedData& data, OpId* committed_index, OpId* last_applied_op_id) = 0;

  // Notify the Consensus implementation that a follower replied with a term higher than that
  // established in the queue.
  virtual void NotifyTermChange(int64_t term) = 0;

  // Notify Consensus that a peer is unable to catch up due to falling behind the leader's log GC
  // threshold.
  virtual void NotifyFailedFollower(const std::string& peer_uuid,
                                    int64_t term,
                                    const std::string& reason) = 0;

  virtual void MajorityReplicatedNumSSTFilesChanged(uint64_t majority_replicated_num_sst_files) = 0;

  virtual ~PeerMessageQueueObserver() {}
};

}  // namespace consensus
}  // namespace yb
