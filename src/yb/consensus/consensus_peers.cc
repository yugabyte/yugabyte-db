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

#include "yb/consensus/consensus_peers.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/bind.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <boost/optional.hpp>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus_queue.h"
#include "yb/consensus/log.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/messenger.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_callback.h"
#include "yb/util/threadpool.h"

using namespace std::literals;

DEFINE_int32(consensus_rpc_timeout_ms, 3000,
             "Timeout used for all consensus internal RPC communications.");
TAG_FLAG(consensus_rpc_timeout_ms, advanced);

DECLARE_int32(raft_heartbeat_interval_ms);

DEFINE_test_flag(double, fault_crash_on_leader_request_fraction, 0.0,
                 "Fraction of the time when the leader will crash just before sending an "
                 "UpdateConsensus RPC.");

// Allow for disabling remote bootstrap in unit tests where we want to test
// certain scenarios without triggering bootstrap of a remote peer.
DEFINE_test_flag(bool, enable_remote_bootstrap, true,
                 "Whether remote bootstrap will be initiated by the leader when it "
                 "detects that a follower is out of date or does not have a tablet "
                 "replica.");

namespace yb {
namespace consensus {

using log::Log;
using log::LogEntryBatch;
using std::shared_ptr;
using rpc::Messenger;
using rpc::RpcController;
using strings::Substitute;

Result<PeerPtr> Peer::NewRemotePeer(const RaftPeerPB& peer_pb,
                                    const string& tablet_id,
                                    const string& leader_uuid,
                                    PeerMessageQueue* queue,
                                    ThreadPoolToken* raft_pool_token,
                                    PeerProxyPtr proxy,
                                    Consensus* consensus) {

  PeerPtr new_peer(new Peer(
      peer_pb, tablet_id, leader_uuid, std::move(proxy), queue, raft_pool_token, consensus));
  RETURN_NOT_OK(new_peer->Init());
  return Result<PeerPtr>(std::move(new_peer));
}

Peer::Peer(
    const RaftPeerPB& peer_pb, string tablet_id, string leader_uuid, PeerProxyPtr proxy,
    PeerMessageQueue* queue, ThreadPoolToken* raft_pool_token, Consensus* consensus)
    : tablet_id_(std::move(tablet_id)),
      leader_uuid_(std::move(leader_uuid)),
      peer_pb_(peer_pb),
      proxy_(std::move(proxy)),
      queue_(queue),
      sem_(1),
      heartbeater_(
          peer_pb.permanent_uuid(), MonoDelta::FromMilliseconds(FLAGS_raft_heartbeat_interval_ms),
          std::bind(&Peer::SignalRequest, this, RequestTriggerMode::kAlwaysSend)),
      raft_pool_token_(raft_pool_token),
      state_(kPeerCreated),
      consensus_(consensus) {}

void Peer::SetTermForTest(int term) {
  response_.set_responder_term(term);
}

Status Peer::Init() {
  std::lock_guard<simple_spinlock> lock(peer_lock_);
  queue_->TrackPeer(peer_pb_.permanent_uuid());
  RETURN_NOT_OK(heartbeater_.Start());
  state_ = kPeerStarted;
  return Status::OK();
}

Status Peer::SignalRequest(RequestTriggerMode trigger_mode) {
  // If the peer is currently sending, return Status::OK().
  // If there are new requests in the queue we'll get them on ProcessResponse().
  std::unique_lock<Semaphore> lock(sem_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return Status::OK();
  }

  {
    std::lock_guard<simple_spinlock> l(peer_lock_);

    if (PREDICT_FALSE(state_ == kPeerClosed)) {
      return STATUS(IllegalState, "Peer was closed.");
    }

    // For the first request sent by the peer, we send it even if the queue is empty, which it will
    // always appear to be for the first request, since this is the negotiation round.
    if (PREDICT_FALSE(state_ == kPeerStarted)) {
      trigger_mode = RequestTriggerMode::kAlwaysSend;
      state_ = kPeerRunning;
    }
    DCHECK_EQ(state_, kPeerRunning);

    // If our last request generated an error, and this is not a normal heartbeat request (i.e.
    // we're not forcing a request even if the queue is empty, unlike we do during heartbeats),
    // then don't send the "per-RPC" request. Instead, we'll wait for the heartbeat.
    //
    // TODO: we could consider looking at the number of consecutive failed attempts, and instead of
    // ignoring the signal, ask the heartbeater to "expedite" the next heartbeat in order to achieve
    // something like exponential backoff after an error. As it is implemented today, any transient
    // error will result in a latency blip as long as the heartbeat period.
    if (failed_attempts_ > 0 && trigger_mode == RequestTriggerMode::kNonEmptyOnly) {
      return Status::OK();
    }
  }

  auto status = raft_pool_token_->SubmitClosure(
      Bind(&Peer::SendNextRequest, Unretained(this), trigger_mode));
  if (status.ok()) {
    lock.release();
  }
  return status;
}

void Peer::SendNextRequest(RequestTriggerMode trigger_mode) {
  DCHECK_EQ(sem_.GetValue(), 0) << "Cannot send request";

  std::unique_lock<Semaphore> lock(sem_, std::adopt_lock);

  // The peer has no pending request nor is sending: send the request.
  bool needs_remote_bootstrap = false;
  bool last_exchange_successful = false;
  RaftPeerPB::MemberType member_type = RaftPeerPB::UNKNOWN_MEMBER_TYPE;
  int64_t commit_index_before = request_.has_committed_index() ?
      request_.committed_index().index() : kMinimumOpIdIndex;
  Status s = queue_->RequestForPeer(peer_pb_.permanent_uuid(), &request_,
      &replicate_msg_refs_, &needs_remote_bootstrap, &member_type, &last_exchange_successful);
  int64_t commit_index_after = request_.has_committed_index() ?
      request_.committed_index().index() : kMinimumOpIdIndex;

  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << "Could not obtain request from queue for peer: "
        << peer_pb_.permanent_uuid() << ". Status: " << s.ToString();
    return;
  }

  if (PREDICT_FALSE(needs_remote_bootstrap)) {
    Status s = SendRemoteBootstrapRequest();
    if (!s.ok()) {
      LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Unable to generate remote bootstrap request for peer: "
                                        << s.ToString();
    } else {
      lock.release();
    }
    return;
  }

  // If the peer doesn't need remote bootstrap, but it is a PRE_VOTER or PRE_OBSERVER in the config,
  // we need to promote it.
  if (last_exchange_successful &&
      (member_type == RaftPeerPB::PRE_VOTER || member_type == RaftPeerPB::PRE_OBSERVER)) {
    if (PREDICT_TRUE(consensus_)) {
      auto uuid = peer_pb_.permanent_uuid();
      lock.unlock();
      consensus::ChangeConfigRequestPB req;
      consensus::ChangeConfigResponsePB resp;

      req.set_tablet_id(tablet_id_);
      req.set_type(consensus::CHANGE_ROLE);
      RaftPeerPB *peer = req.mutable_server();
      peer->set_permanent_uuid(peer_pb_.permanent_uuid());

      boost::optional<tserver::TabletServerErrorPB::Code> error_code;

      // If another ChangeConfig is being processed, our request will be rejected.
      LOG(INFO) << "Sending ChangeConfig request";
      auto status = consensus_->ChangeConfig(req, &DoNothingStatusCB, &error_code);
      if (PREDICT_FALSE(!status.ok())) {
        LOG(WARNING) << "Unable to change role for peer " << uuid << ": " << status.ToString(false);
        // Since we released the semaphore, we need to call SignalRequest again to send a message
        status = SignalRequest(RequestTriggerMode::kAlwaysSend);
        if (PREDICT_FALSE(!status.ok())) {
          LOG(WARNING) << "Unexpected error when trying to send request: "
                       << status.ToString(false);
        }
      }
      return;
    }
  }

  request_.set_tablet_id(tablet_id_);
  request_.set_caller_uuid(leader_uuid_);
  request_.set_dest_uuid(peer_pb_.permanent_uuid());

  const bool req_has_ops = (request_.ops_size() > 0) || (commit_index_after > commit_index_before);

  // If the queue is empty, check if we were told to send a status-only message (which is what
  // happens during heartbeats). If not, just return.
  if (PREDICT_FALSE(!req_has_ops && trigger_mode == RequestTriggerMode::kNonEmptyOnly)) {
    return;
  }

  // If we're actually sending ops there's no need to heartbeat for a while, reset the heartbeater.
  if (req_has_ops) {
    heartbeater_.Reset();
  }

  MAYBE_FAULT(FLAGS_fault_crash_on_leader_request_fraction);
  controller_.Reset();

  lock.release();
  proxy_->UpdateAsync(&request_, trigger_mode, &response_, &controller_,
                      std::bind(&Peer::ProcessResponse, this));
}

void Peer::ProcessResponse() {
  // Note: This method runs on the reactor thread.

  DCHECK_EQ(sem_.GetValue(), 0) << "Got a response when nothing was pending";

  std::unique_lock<Semaphore> lock(sem_, std::adopt_lock);

  if (!controller_.status().ok()) {
    if (controller_.status().IsRemoteError()) {
      // Most controller errors are caused by network issues or corner cases like shutdown and
      // failure to serialize a protobuf. Therefore, we generally consider these errors to indicate
      // an unreachable peer.  However, a RemoteError wraps some other error propagated from the
      // remote peer, so we know the remote is alive. Therefore, we will let the queue know that the
      // remote is responsive.
      queue_->NotifyPeerIsResponsiveDespiteError(peer_pb_.permanent_uuid());
    }
    ProcessResponseError(controller_.status());
    return;
  }

  // We should try to evict a follower which returns a WRONG UUID error.
  if (response_.has_error() &&
      response_.error().code() == tserver::TabletServerErrorPB::WRONG_SERVER_UUID) {
    queue_->NotifyObserversOfFailedFollower(
        peer_pb_.permanent_uuid(),
        Substitute("Leader communication with peer $0 received error $1, will try to "
                   "evict peer", peer_pb_.permanent_uuid(),
                   response_.error().ShortDebugString()));
    ProcessResponseError(StatusFromPB(response_.error().status()));
    return;
  }

  // Pass through errors we can respond to, like not found, since in that case
  // we will need to remotely bootstrap. TODO: Handle DELETED response once implemented.
  if ((response_.has_error() &&
      response_.error().code() != tserver::TabletServerErrorPB::TABLET_NOT_FOUND) ||
      (response_.status().has_error() &&
          response_.status().error().code() == consensus::ConsensusErrorPB::CANNOT_PREPARE)) {
    // Again, let the queue know that the remote is still responsive, since we will not be sending
    // this error response through to the queue.
    queue_->NotifyPeerIsResponsiveDespiteError(peer_pb_.permanent_uuid());
    ProcessResponseError(StatusFromPB(response_.error().status()));
    return;
  }

  // The queue's handling of the peer response may generate IO (reads against the WAL) and
  // SendNextRequest() may do the same thing. So we run the rest of the response handling logic on
  // our thread pool and not on the reactor thread.
  Status s = raft_pool_token_->SubmitClosure(Bind(&Peer::DoProcessResponse, Unretained(this)));
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Unable to process peer response: " << s.ToString()
        << ": " << response_.ShortDebugString();
  } else {
    lock.release();
  }
}

void Peer::DoProcessResponse() {
  DCHECK_EQ(0, sem_.GetValue());
  std::unique_lock<Semaphore> lock(sem_, std::adopt_lock);

  failed_attempts_ = 0;
  bool more_pending = false;
  queue_->ResponseFromPeer(peer_pb_.permanent_uuid(), response_, &more_pending);

  if (more_pending && state_.load(std::memory_order_acquire) != kPeerClosed) {
    lock.release();
    SendNextRequest(RequestTriggerMode::kAlwaysSend);
  }
}

Status Peer::SendRemoteBootstrapRequest() {
  if (!FLAGS_enable_remote_bootstrap) {
    failed_attempts_++;
    return STATUS(NotSupported, "remote bootstrap is disabled");
  }

  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Sending request to remotely bootstrap";
  RETURN_NOT_OK(queue_->GetRemoteBootstrapRequestForPeer(peer_pb_.permanent_uuid(), &rb_request_));
  controller_.Reset();
  proxy_->StartRemoteBootstrap(
      &rb_request_, &rb_response_, &controller_,
      std::bind(&Peer::ProcessRemoteBootstrapResponse, this));
  return Status::OK();
}

void Peer::ProcessRemoteBootstrapResponse() {
  std::unique_lock<Semaphore> lock(sem_, std::adopt_lock);

  // We treat remote bootstrap as fire-and-forget.
  if (rb_response_.has_error()) {
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Unable to begin remote bootstrap on peer: "
                                      << rb_response_.ShortDebugString();
  }
}

void Peer::ProcessResponseError(const Status& status) {
  DCHECK_EQ(0, sem_.GetValue());
  failed_attempts_++;
  YB_LOG_WITH_PREFIX_UNLOCKED_EVERY_N_SECS(WARNING, 5) << "Couldn't send request. "
      << " Status: " << status.ToString() << ". Retrying in the next heartbeat period."
      << " Already tried " << failed_attempts_ << " times. State: " << state_;
}

string Peer::LogPrefixUnlocked() const {
  return Format("T $0 P $1 -> Peer $2 ($3, $4): ",
                tablet_id_, leader_uuid_, peer_pb_.permanent_uuid(),
                peer_pb_.last_known_private_addr(), peer_pb_.last_known_broadcast_addr());
}

void Peer::Close() {
  WARN_NOT_OK(heartbeater_.Stop(), "Could not stop heartbeater");

  // If the peer is already closed return.
  {
    std::lock_guard<simple_spinlock> lock(peer_lock_);
    if (state_ == kPeerClosed) return;
    DCHECK(state_ == kPeerRunning || state_ == kPeerStarted) << "Unexpected state: " << state_;
    state_ = kPeerClosed;
  }
  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Closing peer: " << peer_pb_.permanent_uuid();

  // Acquire the semaphore to wait for any concurrent request to finish.  They will see the state_
  // == kPeerClosed and not start any new requests, but we can't currently cancel the already-sent
  // ones. (see KUDU-699)
  std::lock_guard<Semaphore> l(sem_);
  queue_->UntrackPeer(peer_pb_.permanent_uuid());
  // We don't own the ops (the queue does).
  request_.mutable_ops()->ExtractSubrange(0, request_.ops_size(), /* elements */ nullptr);
  replicate_msg_refs_.clear();
}

Peer::~Peer() {
  Close();
}

RpcPeerProxy::RpcPeerProxy(HostPort hostport, ConsensusServiceProxyPtr consensus_proxy)
    : hostport_(std::move(hostport)), consensus_proxy_(std::move(consensus_proxy)) {
}

void RpcPeerProxy::UpdateAsync(const ConsensusRequestPB* request,
                               RequestTriggerMode trigger_mode,
                               ConsensusResponsePB* response,
                               rpc::RpcController* controller,
                               const rpc::ResponseCallback& callback) {
  controller->set_timeout(MonoDelta::FromMilliseconds(FLAGS_consensus_rpc_timeout_ms));
  consensus_proxy_->UpdateConsensusAsync(*request, response, controller, callback);
}

void RpcPeerProxy::RequestConsensusVoteAsync(const VoteRequestPB* request,
                                             VoteResponsePB* response,
                                             rpc::RpcController* controller,
                                             const rpc::ResponseCallback& callback) {
  consensus_proxy_->RequestConsensusVoteAsync(*request, response, controller, callback);
}

void RpcPeerProxy::RunLeaderElectionAsync(const RunLeaderElectionRequestPB* request,
                                          RunLeaderElectionResponsePB* response,
                                          rpc::RpcController* controller,
                                          const rpc::ResponseCallback& callback) {
  controller->set_timeout(MonoDelta::FromMilliseconds(FLAGS_consensus_rpc_timeout_ms));
  consensus_proxy_->RunLeaderElectionAsync(*request, response, controller, callback);
}

void RpcPeerProxy::LeaderElectionLostAsync(const LeaderElectionLostRequestPB* request,
                                           LeaderElectionLostResponsePB* response,
                                           rpc::RpcController* controller,
                                           const rpc::ResponseCallback& callback) {
  consensus_proxy_->LeaderElectionLostAsync(*request, response, controller, callback);
}

void RpcPeerProxy::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB* request,
                                        StartRemoteBootstrapResponsePB* response,
                                        rpc::RpcController* controller,
                                        const rpc::ResponseCallback& callback) {
  consensus_proxy_->StartRemoteBootstrapAsync(*request, response, controller, callback);
}

RpcPeerProxy::~RpcPeerProxy() {}

RpcPeerProxyFactory::RpcPeerProxyFactory(
    shared_ptr<Messenger> messenger, rpc::ProxyCache* proxy_cache, CloudInfoPB from)
    : messenger_(std::move(messenger)), proxy_cache_(proxy_cache), from_(std::move(from)) {}

PeerProxyPtr RpcPeerProxyFactory::NewProxy(const RaftPeerPB& peer_pb) {
  auto hostport = HostPortFromPB(DesiredHostPort(peer_pb, from_));
  auto proxy = std::make_unique<ConsensusServiceProxy>(proxy_cache_, hostport);
  return std::make_unique<RpcPeerProxy>(std::move(hostport), std::move(proxy));
}

RpcPeerProxyFactory::~RpcPeerProxyFactory() {}

std::shared_ptr<rpc::Messenger> RpcPeerProxyFactory::messenger() const { return messenger_; }

struct GetNodeInstanceRequest {
  GetNodeInstanceRequestPB req;
  GetNodeInstanceResponsePB resp;
  rpc::RpcController controller;
  ConsensusServiceProxy proxy;

  GetNodeInstanceRequest(rpc::ProxyCache* proxy_cache, const HostPort& hostport)
      : proxy(proxy_cache, hostport) {}
};

Status SetPermanentUuidForRemotePeer(
    rpc::ProxyCache* proxy_cache,
    std::chrono::steady_clock::duration timeout,
    const std::vector<HostPort>& endpoints,
    RaftPeerPB* remote_peer) {

  DCHECK(!remote_peer->has_permanent_uuid());
  auto deadline = std::chrono::steady_clock::now() + timeout;

  std::vector<GetNodeInstanceRequest> requests;
  requests.reserve(endpoints.size());
  for (const auto& hp : endpoints) {
    requests.emplace_back(proxy_cache, hp);
  }

  CountDownLatch latch(requests.size());
  const auto kMaxWait = 10s;
  BackoffWaiter waiter(deadline, kMaxWait);
  for (;;) {
    latch.Reset(requests.size());
    std::atomic<GetNodeInstanceRequest*> last_reply{nullptr};
    for (auto& request : requests) {
      request.controller.Reset();
      request.controller.set_timeout(kMaxWait);
      VLOG(2) << "Getting uuid from remote peer. Request: " << request.req.ShortDebugString();

      request.proxy.GetNodeInstanceAsync(
          request.req, &request.resp, &request.controller,
          [&latch, &request, &last_reply] {
        if (!request.controller.status().IsTimedOut()) {
          last_reply.store(&request, std::memory_order_release);
        }
        latch.CountDown();
      });
    }

    latch.Wait();

    for (auto& request : requests) {
      auto status = request.controller.status();
      if (status.ok()) {
        remote_peer->set_permanent_uuid(request.resp.node_instance().permanent_uuid());
        remote_peer->set_member_type(RaftPeerPB::VOTER);
        if (request.resp.has_registration()) {
          CopyRegistration(request.resp.registration(), remote_peer);
        } else {
          // Required for backward compatibility.
          HostPortsToPBs(endpoints, remote_peer->mutable_last_known_private_addr());
        }
        return Status::OK();
      }
    }

    auto* last_reply_value = last_reply.load(std::memory_order_acquire);
    if (last_reply_value == nullptr) {
      last_reply_value = &requests.front();
    }

    LOG(WARNING) << "Error getting permanent uuid from config peer " << yb::ToString(endpoints)
                 << ": " << last_reply_value->controller.status();

    if (!waiter.Wait()) {
      return STATUS_FORMAT(
          TimedOut, "Getting permanent uuid from $0 timed out after $1: $2",
          endpoints, timeout, last_reply_value->controller.status());
    }

    LOG(INFO) << "Retrying to get permanent uuid for remote peer: "
              << yb::ToString(endpoints) << " attempt: " << waiter.attempt();
  }
}

}  // namespace consensus
}  // namespace yb
