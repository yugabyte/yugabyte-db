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
#include "kudu/consensus/consensus_queue.h"

#include <algorithm>
#include <boost/thread/locks.hpp>
#include <gflags/gflags.h>
#include <iostream>
#include <string>
#include <utility>

#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/log_reader.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/consensus/raft_consensus.h"
#include "kudu/gutil/dynamic_annotations.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/human_readable.h"
#include "kudu/util/fault_injection.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/locks.h"
#include "kudu/util/logging.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/url-coding.h"

DEFINE_int32(consensus_max_batch_size_bytes, 1024 * 1024,
             "The maximum per-tablet RPC batch size when updating peers.");
TAG_FLAG(consensus_max_batch_size_bytes, advanced);

DEFINE_int32(follower_unavailable_considered_failed_sec, 300,
             "Seconds that a leader is unable to successfully heartbeat to a "
             "follower after which the follower is considered to be failed and "
             "evicted from the config.");
TAG_FLAG(follower_unavailable_considered_failed_sec, advanced);

DEFINE_int32(consensus_inject_latency_ms_in_notifications, 0,
             "Injects a random sleep between 0 and this many milliseconds into "
             "asynchronous notifications from the consensus queue back to the "
             "consensus implementation.");
TAG_FLAG(consensus_inject_latency_ms_in_notifications, hidden);
TAG_FLAG(consensus_inject_latency_ms_in_notifications, unsafe);

namespace kudu {
namespace consensus {

using log::AsyncLogReader;
using log::Log;
using rpc::Messenger;
using strings::Substitute;

METRIC_DEFINE_gauge_int64(tablet, majority_done_ops, "Leader Operations Acked by Majority",
                          MetricUnit::kOperations,
                          "Number of operations in the leader queue ack'd by a majority but "
                          "not all peers.");
METRIC_DEFINE_gauge_int64(tablet, in_progress_ops, "Leader Operations in Progress",
                          MetricUnit::kOperations,
                          "Number of operations in the leader queue ack'd by a minority of "
                          "peers.");

std::string PeerMessageQueue::TrackedPeer::ToString() const {
  return Substitute("Peer: $0, Is new: $1, Last received: $2, Next index: $3, "
                    "Last known committed idx: $4, Last exchange result: $5, "
                    "Needs remote bootstrap: $6",
                    uuid, is_new, OpIdToString(last_received), next_index,
                    last_known_committed_idx,
                    is_last_exchange_successful ? "SUCCESS" : "ERROR",
                    needs_remote_bootstrap);
}

#define INSTANTIATE_METRIC(x) \
  x.Instantiate(metric_entity, 0)
PeerMessageQueue::Metrics::Metrics(const scoped_refptr<MetricEntity>& metric_entity)
  : num_majority_done_ops(INSTANTIATE_METRIC(METRIC_majority_done_ops)),
    num_in_progress_ops(INSTANTIATE_METRIC(METRIC_in_progress_ops)) {
}
#undef INSTANTIATE_METRIC

PeerMessageQueue::PeerMessageQueue(const scoped_refptr<MetricEntity>& metric_entity,
                                   const scoped_refptr<log::Log>& log,
                                   const RaftPeerPB& local_peer_pb,
                                   const string& tablet_id)
    : local_peer_pb_(local_peer_pb),
      tablet_id_(tablet_id),
      log_cache_(metric_entity, log, local_peer_pb.permanent_uuid(), tablet_id),
      metrics_(metric_entity) {
  DCHECK(local_peer_pb_.has_permanent_uuid());
  DCHECK(local_peer_pb_.has_last_known_addr());
  queue_state_.current_term = MinimumOpId().term();
  queue_state_.committed_index = MinimumOpId();
  queue_state_.all_replicated_opid = MinimumOpId();
  queue_state_.majority_replicated_opid = MinimumOpId();
  queue_state_.state = kQueueConstructed;
  queue_state_.mode = NON_LEADER;
  queue_state_.majority_size_ = -1;
  CHECK_OK(ThreadPoolBuilder("queue-observers-pool").set_max_threads(1).Build(&observers_pool_));
}

void PeerMessageQueue::Init(const OpId& last_locally_replicated) {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  CHECK_EQ(queue_state_.state, kQueueConstructed);
  log_cache_.Init(last_locally_replicated);
  queue_state_.last_appended = last_locally_replicated;
  queue_state_.state = kQueueOpen;
  TrackPeerUnlocked(local_peer_pb_.permanent_uuid());
}

void PeerMessageQueue::SetLeaderMode(const OpId& committed_index,
                                     int64_t current_term,
                                     const RaftConfigPB& active_config) {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  CHECK(committed_index.IsInitialized());
  queue_state_.current_term = current_term;
  queue_state_.committed_index = committed_index;
  queue_state_.majority_replicated_opid = committed_index;
  queue_state_.active_config.reset(new RaftConfigPB(active_config));
  CHECK(IsRaftConfigVoter(local_peer_pb_.permanent_uuid(), *queue_state_.active_config))
      << local_peer_pb_.ShortDebugString() << " not a voter in config: "
      << queue_state_.active_config->ShortDebugString();
  queue_state_.majority_size_ = MajoritySize(CountVoters(*queue_state_.active_config));
  queue_state_.mode = LEADER;

  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Queue going to LEADER mode. State: "
      << queue_state_.ToString();
  CheckPeersInActiveConfigIfLeaderUnlocked();

  // Reset last communication time with all peers to reset the clock on the
  // failure timeout.
  MonoTime now(MonoTime::Now(MonoTime::FINE));
  for (const PeersMap::value_type& entry : peers_map_) {
    entry.second->last_successful_communication_time = now;
  }
}

void PeerMessageQueue::SetNonLeaderMode() {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  queue_state_.active_config.reset();
  queue_state_.mode = NON_LEADER;
  queue_state_.majority_size_ = -1;
  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Queue going to NON_LEADER mode. State: "
      << queue_state_.ToString();
}

void PeerMessageQueue::TrackPeer(const string& uuid) {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  TrackPeerUnlocked(uuid);
}

void PeerMessageQueue::TrackPeerUnlocked(const string& uuid) {
  CHECK(!uuid.empty()) << "Got request to track peer with empty UUID";
  DCHECK_EQ(queue_state_.state, kQueueOpen);

  TrackedPeer* tracked_peer = new TrackedPeer(uuid);
  // We don't know the last operation received by the peer so, following the
  // Raft protocol, we set next_index to one past the end of our own log. This
  // way, if calling this method is the result of a successful leader election
  // and the logs between the new leader and remote peer match, the
  // peer->next_index will point to the index of the soon-to-be-written NO_OP
  // entry that is used to assert leadership. If we guessed wrong, and the peer
  // does not have a log that matches ours, the normal queue negotiation
  // process will eventually find the right point to resume from.
  tracked_peer->next_index = queue_state_.last_appended.index() + 1;
  InsertOrDie(&peers_map_, uuid, tracked_peer);

  CheckPeersInActiveConfigIfLeaderUnlocked();

  // We don't know how far back this peer is, so set the all replicated watermark to
  // MinimumOpId. We'll advance it when we know how far along the peer is.
  queue_state_.all_replicated_opid = MinimumOpId();
}

void PeerMessageQueue::UntrackPeer(const string& uuid) {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  TrackedPeer* peer = EraseKeyReturnValuePtr(&peers_map_, uuid);
  if (peer != nullptr) {
    delete peer;
  }
}

void PeerMessageQueue::CheckPeersInActiveConfigIfLeaderUnlocked() const {
  if (queue_state_.mode != LEADER) return;
  unordered_set<string> config_peer_uuids;
  for (const RaftPeerPB& peer_pb : queue_state_.active_config->peers()) {
    InsertOrDie(&config_peer_uuids, peer_pb.permanent_uuid());
  }
  for (const PeersMap::value_type& entry : peers_map_) {
    if (!ContainsKey(config_peer_uuids, entry.first)) {
      LOG_WITH_PREFIX_UNLOCKED(FATAL) << Substitute("Peer $0 is not in the active config. "
                                                    "Queue state: $1",
                                                    entry.first,
                                                    queue_state_.ToString());
    }
  }
}

void PeerMessageQueue::LocalPeerAppendFinished(const OpId& id,
                                               const StatusCallback& callback,
                                               const Status& status) {
  CHECK_OK(status);

  // Fake an RPC response from the local peer.
  // TODO: we should probably refactor the ResponseFromPeer function
  // so that we don't need to construct this fake response, but this
  // seems to work for now.
  ConsensusResponsePB fake_response;
  fake_response.set_responder_uuid(local_peer_pb_.permanent_uuid());
  *fake_response.mutable_status()->mutable_last_received() = id;
  *fake_response.mutable_status()->mutable_last_received_current_leader() = id;
  {
    boost::unique_lock<simple_spinlock> lock(queue_lock_);
    fake_response.mutable_status()->set_last_committed_idx(queue_state_.committed_index.index());
  }
  bool junk;
  ResponseFromPeer(local_peer_pb_.permanent_uuid(), fake_response, &junk);

  callback.Run(status);
}

Status PeerMessageQueue::AppendOperation(const ReplicateRefPtr& msg) {
  return AppendOperations({ msg }, Bind(DoNothingStatusCB));
}

Status PeerMessageQueue::AppendOperations(const vector<ReplicateRefPtr>& msgs,
                                          const StatusCallback& log_append_callback) {

  DFAKE_SCOPED_LOCK(append_fake_lock_);
  boost::unique_lock<simple_spinlock> lock(queue_lock_);

  OpId last_id = msgs.back()->get()->id();

  if (last_id.term() > queue_state_.current_term) {
    queue_state_.current_term = last_id.term();
  }

  // Unlock ourselves during Append to prevent a deadlock: it's possible that
  // the log buffer is full, in which case AppendOperations would block. However,
  // for the log buffer to empty, it may need to call LocalPeerAppendFinished()
  // which also needs queue_lock_.
  lock.unlock();
  RETURN_NOT_OK(log_cache_.AppendOperations(msgs,
                                            Bind(&PeerMessageQueue::LocalPeerAppendFinished,
                                                 Unretained(this),
                                                 last_id,
                                                 log_append_callback)));
  lock.lock();
  queue_state_.last_appended = last_id;
  UpdateMetrics();

  return Status::OK();
}

Status PeerMessageQueue::RequestForPeer(const string& uuid,
                                        ConsensusRequestPB* request,
                                        vector<ReplicateRefPtr>* msg_refs,
                                        bool* needs_remote_bootstrap) {
  TrackedPeer* peer = nullptr;
  OpId preceding_id;
  {
    lock_guard<simple_spinlock> lock(&queue_lock_);
    DCHECK_EQ(queue_state_.state, kQueueOpen);
    DCHECK_NE(uuid, local_peer_pb_.permanent_uuid());

    peer = FindPtrOrNull(peers_map_, uuid);
    if (PREDICT_FALSE(peer == nullptr || queue_state_.mode == NON_LEADER)) {
      return Status::NotFound("Peer not tracked or queue not in leader mode.");
    }

    // Clear the requests without deleting the entries, as they may be in use by other peers.
    request->mutable_ops()->ExtractSubrange(0, request->ops_size(), nullptr);

    // This is initialized to the queue's last appended op but gets set to the id of the
    // log entry preceding the first one in 'messages' if messages are found for the peer.
    preceding_id = queue_state_.last_appended;
    request->mutable_committed_index()->CopyFrom(queue_state_.committed_index);
    request->set_caller_term(queue_state_.current_term);
  }

  MonoDelta unreachable_time =
      MonoTime::Now(MonoTime::FINE).GetDeltaSince(peer->last_successful_communication_time);
  if (unreachable_time.ToSeconds() > FLAGS_follower_unavailable_considered_failed_sec) {
    if (CountVoters(*queue_state_.active_config) > 2) {
      // We never drop from 2 to 1 automatically, at least for now. We may want
      // to revisit this later, we're just being cautious with this.
      string msg = Substitute("Leader has been unable to successfully communicate "
                              "with Peer $0 for more than $1 seconds ($2)",
                              uuid,
                              FLAGS_follower_unavailable_considered_failed_sec,
                              unreachable_time.ToString());
      NotifyObserversOfFailedFollower(uuid, queue_state_.current_term, msg);
    }
  }

  if (PREDICT_FALSE(peer->needs_remote_bootstrap)) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << "Peer needs remote bootstrap: " << peer->ToString();
    *needs_remote_bootstrap = true;
    return Status::OK();
  }
  *needs_remote_bootstrap = false;

  // If we've never communicated with the peer, we don't know what messages to
  // send, so we'll send a status-only request. Otherwise, we grab requests
  // from the log starting at the last_received point.
  if (!peer->is_new) {

    // The batch of messages to send to the peer.
    vector<ReplicateRefPtr> messages;
    int max_batch_size = FLAGS_consensus_max_batch_size_bytes - request->ByteSize();

    // We try to get the follower's next_index from our log.
    Status s = log_cache_.ReadOps(peer->next_index - 1,
                                  max_batch_size,
                                  &messages,
                                  &preceding_id);
    if (PREDICT_FALSE(!s.ok())) {
      // It's normal to have a NotFound() here if a follower falls behind where
      // the leader has GCed its logs.
      if (PREDICT_TRUE(s.IsNotFound())) {
        string msg = Substitute("The logs necessary to catch up peer $0 have been "
                                "garbage collected. The follower will never be able "
                                "to catch up ($1)", uuid, s.ToString());
        NotifyObserversOfFailedFollower(uuid, queue_state_.current_term, msg);
        return s;
      // IsIncomplete() means that we tried to read beyond the head of the log
      // (in the future). See KUDU-1078.
      } else if (s.IsIncomplete()) {
        LOG_WITH_PREFIX_UNLOCKED(ERROR) << "Error trying to read ahead of the log "
                                        << "while preparing peer request: "
                                        << s.ToString() << ". Destination peer: "
                                        << peer->ToString();
        return s;
      } else {
        LOG_WITH_PREFIX_UNLOCKED(FATAL) << "Error reading the log while preparing peer request: "
                                        << s.ToString() << ". Destination peer: "
                                        << peer->ToString();
      }
    }

    // We use AddAllocated rather than copy, because we pin the log cache at the
    // "all replicated" point. At some point we may want to allow partially loading
    // (and not pinning) earlier messages. At that point we'll need to do something
    // smarter here, like copy or ref-count.
    for (const ReplicateRefPtr& msg : messages) {
      request->mutable_ops()->AddAllocated(msg->get());
    }
    msg_refs->swap(messages);
    DCHECK_LE(request->ByteSize(), FLAGS_consensus_max_batch_size_bytes);
  }

  DCHECK(preceding_id.IsInitialized());
  request->mutable_preceding_id()->CopyFrom(preceding_id);

  if (PREDICT_FALSE(VLOG_IS_ON(2))) {
    if (request->ops_size() > 0) {
      VLOG_WITH_PREFIX_UNLOCKED(2) << "Sending request with operations to Peer: " << uuid
          << ". Size: " << request->ops_size()
          << ". From: " << request->ops(0).id().ShortDebugString() << ". To: "
          << request->ops(request->ops_size() - 1).id().ShortDebugString();
    } else {
      VLOG_WITH_PREFIX_UNLOCKED(2) << "Sending status only request to Peer: " << uuid
          << ": " << request->DebugString();
    }
  }

  return Status::OK();
}

Status PeerMessageQueue::GetRemoteBootstrapRequestForPeer(const string& uuid,
                                                          StartRemoteBootstrapRequestPB* req) {
  TrackedPeer* peer = nullptr;
  {
    lock_guard<simple_spinlock> lock(&queue_lock_);
    DCHECK_EQ(queue_state_.state, kQueueOpen);
    DCHECK_NE(uuid, local_peer_pb_.permanent_uuid());
    peer = FindPtrOrNull(peers_map_, uuid);
    if (PREDICT_FALSE(peer == nullptr || queue_state_.mode == NON_LEADER)) {
      return Status::NotFound("Peer not tracked or queue not in leader mode.");
    }
  }

  if (PREDICT_FALSE(!peer->needs_remote_bootstrap)) {
    return Status::IllegalState("Peer does not need to remotely bootstrap", uuid);
  }
  req->Clear();
  req->set_dest_uuid(uuid);
  req->set_tablet_id(tablet_id_);
  req->set_bootstrap_peer_uuid(local_peer_pb_.permanent_uuid());
  *req->mutable_bootstrap_peer_addr() = local_peer_pb_.last_known_addr();
  req->set_caller_term(queue_state_.current_term);
  peer->needs_remote_bootstrap = false; // Now reset the flag.
  return Status::OK();
}

void PeerMessageQueue::AdvanceQueueWatermark(const char* type,
                                             OpId* watermark,
                                             const OpId& replicated_before,
                                             const OpId& replicated_after,
                                             int num_peers_required,
                                             const TrackedPeer* peer) {

  if (VLOG_IS_ON(2)) {
    VLOG_WITH_PREFIX_UNLOCKED(2) << "Updating " << type << " watermark: "
        << "Peer (" << peer->ToString() << ") changed from "
        << replicated_before << " to " << replicated_after << ". "
        << "Current value: " << watermark->ShortDebugString();
  }

  // Go through the peer's watermarks, we want the highest watermark that
  // 'num_peers_required' of peers has replicated. To find this we do the
  // following:
  // - Store all the peer's 'last_received' in a vector
  // - Sort the vector
  // - Find the vector.size() - 'num_peers_required' position, this
  //   will be the new 'watermark'.
  vector<const OpId*> watermarks;
  for (const PeersMap::value_type& peer : peers_map_) {
    if (peer.second->is_last_exchange_successful) {
      watermarks.push_back(&peer.second->last_received);
    }
  }

  // If we haven't enough peers to calculate the watermark return.
  if (watermarks.size() < num_peers_required) {
    return;
  }

  std::sort(watermarks.begin(), watermarks.end(), OpIdIndexLessThanPtrFunctor());

  OpId new_watermark = *watermarks[watermarks.size() - num_peers_required];
  OpId old_watermark = *watermark;
  watermark->CopyFrom(new_watermark);

  VLOG_WITH_PREFIX_UNLOCKED(1) << "Updated " << type << " watermark "
      << "from " << old_watermark << " to " << new_watermark;
  if (VLOG_IS_ON(3)) {
    VLOG_WITH_PREFIX_UNLOCKED(3) << "Peers: ";
    for (const PeersMap::value_type& peer : peers_map_) {
      VLOG_WITH_PREFIX_UNLOCKED(3) << "Peer: " << peer.second->ToString();
    }
    VLOG_WITH_PREFIX_UNLOCKED(3) << "Sorted watermarks:";
    for (const OpId* watermark : watermarks) {
      VLOG_WITH_PREFIX_UNLOCKED(3) << "Watermark: " << watermark->ShortDebugString();
    }
  }
}

void PeerMessageQueue::NotifyPeerIsResponsiveDespiteError(const std::string& peer_uuid) {
  lock_guard<simple_spinlock> l(&queue_lock_);
  TrackedPeer* peer = FindPtrOrNull(peers_map_, peer_uuid);
  if (!peer) return;
  peer->last_successful_communication_time = MonoTime::Now(MonoTime::FINE);
}

void PeerMessageQueue::ResponseFromPeer(const std::string& peer_uuid,
                                        const ConsensusResponsePB& response,
                                        bool* more_pending) {
  DCHECK(response.IsInitialized()) << "Error: Uninitialized: "
      << response.InitializationErrorString() << ". Response: " << response.ShortDebugString();

  OpId updated_majority_replicated_opid;
  Mode mode_copy;
  {
    unique_lock<simple_spinlock> scoped_lock(&queue_lock_);
    DCHECK_NE(kQueueConstructed, queue_state_.state);

    TrackedPeer* peer = FindPtrOrNull(peers_map_, peer_uuid);
    if (PREDICT_FALSE(queue_state_.state != kQueueOpen || peer == nullptr)) {
      LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Queue is closed or peer was untracked, disregarding "
          "peer response. Response: " << response.ShortDebugString();
      *more_pending = false;
      return;
    }

    // Remotely bootstrap the peer if the tablet is not found or deleted.
    if (response.has_error()) {
      // We only let special types of errors through to this point from the peer.
      CHECK_EQ(tserver::TabletServerErrorPB::TABLET_NOT_FOUND, response.error().code())
          << response.ShortDebugString();

      peer->needs_remote_bootstrap = true;
      LOG_WITH_PREFIX_UNLOCKED(INFO) << "Marked peer as needing remote bootstrap: "
                                     << peer->ToString();
      *more_pending = true;
      return;
    }

    // Sanity checks.
    // Some of these can be eventually removed, but they are handy for now.
    DCHECK(response.status().IsInitialized()) << "Error: Uninitialized: "
        << response.InitializationErrorString() << ". Response: " << response.ShortDebugString();
    // TODO: Include uuid in error messages as well.
    DCHECK(response.has_responder_uuid() && !response.responder_uuid().empty())
        << "Got response from peer with empty UUID";

    // Application level errors should be handled elsewhere
    DCHECK(!response.has_error());
    // Responses should always have a status.
    DCHECK(response.has_status());
    // The status must always have a last received op id and a last committed index.
    DCHECK(response.status().has_last_received());
    DCHECK(response.status().has_last_received_current_leader());
    DCHECK(response.status().has_last_committed_idx());

    const ConsensusStatusPB& status = response.status();

    // Take a snapshot of the current peer status.
    TrackedPeer previous = *peer;

    // Update the peer status based on the response.
    peer->is_new = false;
    peer->last_known_committed_idx = status.last_committed_idx();
    peer->last_successful_communication_time = MonoTime::Now(MonoTime::FINE);

    // If the reported last-received op for the replica is in our local log,
    // then resume sending entries from that point onward. Otherwise, resume
    // after the last op they received from us. If we've never successfully
    // sent them anything, start after the last-committed op in their log, which
    // is guaranteed by the Raft protocol to be a valid op.

    bool peer_has_prefix_of_log = IsOpInLog(status.last_received());
    if (peer_has_prefix_of_log) {
      // If the latest thing in their log is in our log, we are in sync.
      peer->last_received = status.last_received();
      peer->next_index = peer->last_received.index() + 1;

    } else if (!OpIdEquals(status.last_received_current_leader(), MinimumOpId())) {
      // Their log may have diverged from ours, however we are in the process
      // of replicating our ops to them, so continue doing so. Eventually, we
      // will cause the divergent entry in their log to be overwritten.
      peer->last_received = status.last_received_current_leader();
      peer->next_index = peer->last_received.index() + 1;

    } else {
      // The peer is divergent and they have not (successfully) received
      // anything from us yet. Start sending from their last committed index.
      // This logic differs from the Raft spec slightly because instead of
      // stepping back one-by-one from the end until we no longer have an LMP
      // error, we jump back to the last committed op indicated by the peer with
      // the hope that doing so will result in a faster catch-up process.
      DCHECK_GE(peer->last_known_committed_idx, 0);
      peer->next_index = peer->last_known_committed_idx + 1;
    }

    if (PREDICT_FALSE(status.has_error())) {
      peer->is_last_exchange_successful = false;
      switch (status.error().code()) {
        case ConsensusErrorPB::PRECEDING_ENTRY_DIDNT_MATCH: {
          DCHECK(status.has_last_received());
          if (previous.is_new) {
            // That's currently how we can detect that we able to connect to a peer.
            LOG_WITH_PREFIX_UNLOCKED(INFO) << "Connected to new peer: " << peer->ToString();
          } else {
            LOG_WITH_PREFIX_UNLOCKED(INFO) << "Got LMP mismatch error from peer: "
                                           << peer->ToString();
          }
          *more_pending = true;
          return;
        }
        case ConsensusErrorPB::INVALID_TERM: {
          CHECK(response.has_responder_term());
          LOG_WITH_PREFIX_UNLOCKED(INFO) << "Peer responded invalid term: " << peer->ToString();
          NotifyObserversOfTermChange(response.responder_term());
          *more_pending = false;
          return;
        }
        default: {
          LOG_WITH_PREFIX_UNLOCKED(FATAL) << "Unexpected consensus error. Code: "
              << ConsensusErrorPB::Code_Name(status.error().code()) << ". Response: "
              << response.ShortDebugString();
        }
      }
    }

    peer->is_last_exchange_successful = true;

    if (response.has_responder_term()) {
      // The peer must have responded with a term that is greater than or equal to
      // the last known term for that peer.
      peer->CheckMonotonicTerms(response.responder_term());

      // If the responder didn't send an error back that must mean that it has
      // a term that is the same or lower than ours.
      CHECK_LE(response.responder_term(), queue_state_.current_term);
    }

    if (PREDICT_FALSE(VLOG_IS_ON(2))) {
      VLOG_WITH_PREFIX_UNLOCKED(2) << "Received Response from Peer (" << peer->ToString() << "). "
          << "Response: " << response.ShortDebugString();
    }

    // If our log has the next request for the peer or if the peer's committed index is
    // lower than our own, set 'more_pending' to true.
    *more_pending = log_cache_.HasOpBeenWritten(peer->next_index) ||
        (peer->last_known_committed_idx < queue_state_.committed_index.index());

    mode_copy = queue_state_.mode;
    if (mode_copy == LEADER) {
      // Advance the majority replicated index.
      AdvanceQueueWatermark("majority_replicated",
                            &queue_state_.majority_replicated_opid,
                            previous.last_received,
                            peer->last_received,
                            queue_state_.majority_size_,
                            peer);

      updated_majority_replicated_opid = queue_state_.majority_replicated_opid;
    }

    // Advance the all replicated index.
    AdvanceQueueWatermark("all_replicated",
                          &queue_state_.all_replicated_opid,
                          previous.last_received,
                          peer->last_received,
                          peers_map_.size(),
                          peer);

    log_cache_.EvictThroughOp(queue_state_.all_replicated_opid.index());

    UpdateMetrics();
  }

  if (mode_copy == LEADER) {
    NotifyObserversOfMajorityReplOpChange(updated_majority_replicated_opid);
  }
}

PeerMessageQueue::TrackedPeer PeerMessageQueue::GetTrackedPeerForTests(string uuid) {
  unique_lock<simple_spinlock> scoped_lock(&queue_lock_);
  TrackedPeer* tracked = FindOrDie(peers_map_, uuid);
  return *tracked;
}

OpId PeerMessageQueue::GetAllReplicatedIndexForTests() const {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  return queue_state_.all_replicated_opid;
}

OpId PeerMessageQueue::GetCommittedIndexForTests() const {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  return queue_state_.committed_index;
}

OpId PeerMessageQueue::GetMajorityReplicatedOpIdForTests() const {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  return queue_state_.majority_replicated_opid;
}


void PeerMessageQueue::UpdateMetrics() {
  // Since operations have consecutive indices we can update the metrics based
  // on simple index math.
  metrics_.num_majority_done_ops->set_value(
      queue_state_.committed_index.index() -
      queue_state_.all_replicated_opid.index());
  metrics_.num_in_progress_ops->set_value(
    queue_state_.last_appended.index() -
    queue_state_.committed_index.index());
}

void PeerMessageQueue::DumpToStrings(vector<string>* lines) const {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  DumpToStringsUnlocked(lines);
}

void PeerMessageQueue::DumpToStringsUnlocked(vector<string>* lines) const {
  lines->push_back("Watermarks:");
  for (const PeersMap::value_type& entry : peers_map_) {
    lines->push_back(
        Substitute("Peer: $0 Watermark: $1", entry.first, entry.second->ToString()));
  }

  log_cache_.DumpToStrings(lines);
}

void PeerMessageQueue::DumpToHtml(std::ostream& out) const {
  using std::endl;

  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  out << "<h3>Watermarks</h3>" << endl;
  out << "<table>" << endl;;
  out << "  <tr><th>Peer</th><th>Watermark</th></tr>" << endl;
  for (const PeersMap::value_type& entry : peers_map_) {
    out << Substitute("  <tr><td>$0</td><td>$1</td></tr>",
                      EscapeForHtmlToString(entry.first),
                      EscapeForHtmlToString(entry.second->ToString())) << endl;
  }
  out << "</table>" << endl;

  log_cache_.DumpToHtml(out);
}

void PeerMessageQueue::ClearUnlocked() {
  STLDeleteValues(&peers_map_);
  queue_state_.state = kQueueClosed;
}

void PeerMessageQueue::Close() {
  observers_pool_->Shutdown();
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  ClearUnlocked();
}

int64_t PeerMessageQueue::GetQueuedOperationsSizeBytesForTests() const {
  return log_cache_.BytesUsed();
}

string PeerMessageQueue::ToString() const {
  // Even though metrics are thread-safe obtain the lock so that we get
  // a "consistent" snapshot of the metrics.
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  return ToStringUnlocked();
}

string PeerMessageQueue::ToStringUnlocked() const {
  return Substitute("Consensus queue metrics:"
                    "Only Majority Done Ops: $0, In Progress Ops: $1, Cache: $2",
                    metrics_.num_majority_done_ops->value(), metrics_.num_in_progress_ops->value(),
                    log_cache_.StatsString());
}

void PeerMessageQueue::RegisterObserver(PeerMessageQueueObserver* observer) {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  auto iter = std::find(observers_.begin(), observers_.end(), observer);
  if (iter == observers_.end()) {
    observers_.push_back(observer);
  }
}

Status PeerMessageQueue::UnRegisterObserver(PeerMessageQueueObserver* observer) {
  boost::lock_guard<simple_spinlock> lock(queue_lock_);
  auto iter = std::find(observers_.begin(), observers_.end(), observer);
  if (iter == observers_.end()) {
    return Status::NotFound("Can't find observer.");
  }
  observers_.erase(iter);
  return Status::OK();
}

bool PeerMessageQueue::IsOpInLog(const OpId& desired_op) const {
  OpId log_op;
  Status s = log_cache_.LookupOpId(desired_op.index(), &log_op);
  if (PREDICT_TRUE(s.ok())) {
    return OpIdEquals(desired_op, log_op);
  }
  if (PREDICT_TRUE(s.IsNotFound() || s.IsIncomplete())) {
    return false;
  }
  LOG_WITH_PREFIX_UNLOCKED(FATAL) << "Error while reading the log: " << s.ToString();
  return false; // Unreachable; here to squelch GCC warning.
}

void PeerMessageQueue::NotifyObserversOfMajorityReplOpChange(
    const OpId new_majority_replicated_op) {
  WARN_NOT_OK(observers_pool_->SubmitClosure(
      Bind(&PeerMessageQueue::NotifyObserversOfMajorityReplOpChangeTask,
           Unretained(this), new_majority_replicated_op)),
              LogPrefixUnlocked() + "Unable to notify RaftConsensus of "
                                    "majority replicated op change.");
}

void PeerMessageQueue::NotifyObserversOfTermChange(int64_t term) {
  WARN_NOT_OK(observers_pool_->SubmitClosure(
      Bind(&PeerMessageQueue::NotifyObserversOfTermChangeTask,
           Unretained(this), term)),
              LogPrefixUnlocked() + "Unable to notify RaftConsensus of term change.");
}

void PeerMessageQueue::NotifyObserversOfMajorityReplOpChangeTask(
    const OpId new_majority_replicated_op) {
  std::vector<PeerMessageQueueObserver*> copy;
  {
    boost::lock_guard<simple_spinlock> lock(queue_lock_);
    copy = observers_;
  }

  // TODO move commit index advancement here so that the queue is not dependent on
  // consensus at all, but that requires a bit more work.
  OpId new_committed_index;
  for (PeerMessageQueueObserver* observer : copy) {
    observer->UpdateMajorityReplicated(new_majority_replicated_op, &new_committed_index);
  }

  {
    boost::lock_guard<simple_spinlock> lock(queue_lock_);
    if (new_committed_index.IsInitialized() &&
        new_committed_index.index() > queue_state_.committed_index.index()) {
      queue_state_.committed_index.CopyFrom(new_committed_index);
    }
  }
}

void PeerMessageQueue::NotifyObserversOfTermChangeTask(int64_t term) {
  MAYBE_INJECT_RANDOM_LATENCY(FLAGS_consensus_inject_latency_ms_in_notifications);
  std::vector<PeerMessageQueueObserver*> copy;
  {
    boost::lock_guard<simple_spinlock> lock(queue_lock_);
    copy = observers_;
  }
  OpId new_committed_index;
  for (PeerMessageQueueObserver* observer : copy) {
    observer->NotifyTermChange(term);
  }
}

void PeerMessageQueue::NotifyObserversOfFailedFollower(const string& uuid,
                                                       int64_t term,
                                                       const string& reason) {
  WARN_NOT_OK(observers_pool_->SubmitClosure(
      Bind(&PeerMessageQueue::NotifyObserversOfFailedFollowerTask,
           Unretained(this), uuid, term, reason)),
              LogPrefixUnlocked() + "Unable to notify RaftConsensus of abandoned follower.");
}

void PeerMessageQueue::NotifyObserversOfFailedFollowerTask(const string& uuid,
                                                           int64_t term,
                                                           const string& reason) {
  MAYBE_INJECT_RANDOM_LATENCY(FLAGS_consensus_inject_latency_ms_in_notifications);
  std::vector<PeerMessageQueueObserver*> observers_copy;
  {
    boost::lock_guard<simple_spinlock> lock(queue_lock_);
    observers_copy = observers_;
  }
  OpId new_committed_index;
  for (PeerMessageQueueObserver* observer : observers_copy) {
    observer->NotifyFailedFollower(uuid, term, reason);
  }
}

PeerMessageQueue::~PeerMessageQueue() {
  Close();
}

string PeerMessageQueue::LogPrefixUnlocked() const {
  // TODO: we should probably use an atomic here. We'll just annotate
  // away the TSAN error for now, since the worst case is a slightly out-of-date
  // log message, and not very likely.
  Mode mode = ANNOTATE_UNPROTECTED_READ(queue_state_.mode);
  return Substitute("T $0 P $1 [$2]: ",
                    tablet_id_,
                    local_peer_pb_.permanent_uuid(),
                    mode == LEADER ? "LEADER" : "NON_LEADER");
}

string PeerMessageQueue::QueueState::ToString() const {
  return Substitute("All replicated op: $0, Majority replicated op: $1, "
      "Committed index: $2, Last appended: $3, Current term: $4, Majority size: $5, "
      "State: $6, Mode: $7$8",
      OpIdToString(all_replicated_opid), OpIdToString(majority_replicated_opid),
      OpIdToString(committed_index), OpIdToString(last_appended), current_term,
      majority_size_, state, (mode == LEADER ? "LEADER" : "NON_LEADER"),
      active_config ? ", active raft config: " + active_config->ShortDebugString() : "");
}

}  // namespace consensus
}  // namespace kudu
