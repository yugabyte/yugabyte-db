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

#include <stdint.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/version.hpp>
#include "yb/util/flags.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/integral_types.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/status_fwd.h"
#include "yb/util/atomic.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/locks.h"
#include "yb/util/memory/arena.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/semaphore.h"
#include "yb/util/shared_lock.h"
#include "yb/util/trace.h"

namespace yb {
class HostPort;
class ThreadPoolToken;

namespace rpc {
class Messenger;
class PeriodicTimer;
}

namespace consensus {

// A peer in consensus (local or remote).
//
// Leaders use peers to update the local Log and remote replicas.
//
// Peers are owned by the consensus implementation and do not keep state aside from whether there
// are requests pending or if requests are being processed.
//
// There are two external actions that trigger a state change:
//
// SignalRequest(): Called by the consensus implementation, notifies that the queue contains
// messages to be processed. This function takes a parameter allowing to send requests only if
// the queue is not empty, or to force-send a request even if it is empty.
//
// ProcessResponse() Called a response from a peer is received.
//
// The following state diagrams describe what happens when a state changing method is called.
//
//                        +
//                        |
//       SignalRequest()  |
//                        |
//                        |
//                        v
//              +------------------+
//       +------+    processing ?  +-----+
//       |      +------------------+     |
//       |                               |
//       | Yes                           | No
//       |                               |
//       v                               v
//     return                      ProcessNextRequest()
//                                 processing = true
//                                 - get reqs. from queue
//                                 - update peer async
//                                 return
//
//                         +
//                         |
//      ProcessResponse()  |
//      processing = false |
//                         v
//               +------------------+
//        +------+   more pending?  +-----+
//        |      +------------------+     |
//        |                               |
//        | Yes                           | No
//        |                               |
//        v                               v
//  SignalRequest()                    return
//
class Peer;
typedef std::shared_ptr<Peer> PeerPtr;

class Peer : public std::enable_shared_from_this<Peer> {
 public:
  Peer(const RaftPeerPB& peer, std::string tablet_id, std::string leader_uuid,
       PeerProxyPtr proxy, PeerMessageQueue* queue, MultiRaftHeartbeatBatcherPtr multi_raft_batcher,
       ThreadPoolToken* raft_pool_token, Consensus* consensus, rpc::Messenger* messenger);

  // Initializes a peer and get its status.
  Status Init();

  // Signals that this peer has a new request to replicate/store.
  Status SignalRequest(RequestTriggerMode trigger_mode);

  const RaftPeerPB& peer_pb() const { return peer_pb_; }

  // Returns the PeerProxy if this is a remote peer or NULL if it
  // isn't. Used for tests to fiddle with the proxy and emulate remote
  // behavior.
  PeerProxy* GetPeerProxyForTests();

  // Stop sending requests and periodic heartbeats.
  //
  // This does not block waiting on any current outstanding requests to finish.
  // However, when they do finish, the results will be disregarded, so this
  // is safe to call at any point.
  //
  // This method must be called before the Peer's associated ThreadPoolToken
  // is destructed. Once this method returns, it is safe to destruct
  // the ThreadPoolToken.
  void Close();

  void TEST_SetTerm(int term, ThreadSafeArena* arena);

  ~Peer();

  // Creates a new remote peer and makes the queue track it.'
  //
  // Requests to this peer (which may end up doing IO to read non-cached log entries) are assembled
  // on 'raft_pool_token'.  Response handling may also involve IO related to log-entry lookups
  // and is also done on 'thread_pool'.
  template <class... Args>
  static Result<PeerPtr> NewRemotePeer(Args&&... args) {
    auto new_peer = std::make_shared<Peer>(std::forward<Args>(args)...);
    RETURN_NOT_OK(new_peer->Init());
    return Result<PeerPtr>(std::move(new_peer));
  }

  uint64_t failed_attempts() {
    std::lock_guard<simple_spinlock> l(peer_lock_);
    return failed_attempts_;
  }

  void DumpToHtml(std::ostream& out) const;

 private:
  void SendNextRequest(RequestTriggerMode trigger_mode);

  // Signals that a response was received from the peer. This method does response handling that
  // requires IO or may block.
  void ProcessResponse(TracePtr trace);

  // Signals that a heartbeat response was received from the peer.
  void ProcessHeartbeatResponse(const Status& status);

  // Returns true if there are more pending ops to process, false otherwise.
  bool ProcessResponseWithStatus(const Status& status,
                                 LWConsensusResponsePB* response);

  // Fetch the desired remote bootstrap request from the queue and send it to the peer. The callback
  // goes to ProcessRemoteBootstrapResponse().
  //
  // Returns a bad Status if remote bootstrap is disabled, or if the request cannot be generated for
  // some reason.
  Status SendRemoteBootstrapRequest();

  // Handle RPC callback from initiating remote bootstrap.
  void ProcessRemoteBootstrapResponse();

  // Signals there was an error sending the request to the peer.
  void ProcessResponseError(const Status& status);

  // Returns true if the peer is closed and the calling function should return.
  std::unique_lock<simple_spinlock> StartProcessingUnlocked();

  template <class LockType>
  std::unique_lock<AtomicTryMutex> LockPerformingUpdate(LockType type) {
    return std::unique_lock<AtomicTryMutex>(performing_update_mutex_, type);
  }

  template <class LockType>
  std::unique_lock<AtomicTryMutex> LockPerformingHeartbeat(LockType type) {
    return std::unique_lock<AtomicTryMutex>(performing_heartbeat_mutex_, type);
  }

  std::string LogPrefix() const;

  const std::string& tablet_id() const { return tablet_id_; }

  const std::string tablet_id_;
  const std::string leader_uuid_;

  const RaftPeerPB peer_pb_;

  PeerProxyPtr proxy_;

  PeerMessageQueue* queue_;
  uint64_t failed_attempts_ = 0;

  // The latest consensus update request and response stored in arena_.
  ThreadSafeArena arena_;
  LWConsensusRequestPB* update_request_ = nullptr;
  LWConsensusResponsePB* update_response_ = nullptr;

  // Latest heartbeat request and response
  ConsensusRequestPB heartbeat_request_;
  ConsensusResponsePB heartbeat_response_;

  // Each time a heartbeat request is sent this value is incremented.
  int64_t cur_heartbeat_id_ = 0;
  // Indiciates the last valid heartbeat id that was sent.
  // Each time an operation (non-heartbeat) is sent this value is updated.
  // Upon receving the response for an outstanding heartbeat if
  // cur_heartbeat_id_ < minimum_viable_heartbeat_ then the heartbeat is invalid
  // since a more recent op was sent so we won't process it's response.
  int64_t minimum_viable_heartbeat_ = 0;

  // The latest remote bootstrap request and response.
  StartRemoteBootstrapRequestPB rb_request_;
  StartRemoteBootstrapResponsePB rb_response_;

  rpc::RpcController controller_;
  std::atomic<CoarseTimePoint> last_rpc_start_time_{CoarseTimePoint::min()};

  // Held if there is an outstanding request.  This is used in order to ensure that we only have a
  // single request outstanding at a time, and to wait for the outstanding requests at Close().
  AtomicTryMutex performing_update_mutex_;

  // Held if there is an outstanding heartbeat request.
  // This is used in order to ensure that we only have a
  // single heartbeat request outstanding at a time.
  AtomicTryMutex performing_heartbeat_mutex_;

  // Heartbeater for remote peer implementations.  This will send status only requests to the remote
  // peers whenever we go more than 'FLAGS_raft_heartbeat_interval_ms' without sending actual data.
  std::shared_ptr<rpc::PeriodicTimer> heartbeater_;

  // Batcher that currently batches heartbeat requests that are sent by each consensus peer
  // on a per tserver level
  MultiRaftHeartbeatBatcherPtr multi_raft_batcher_;

  // Thread pool used to construct requests to this peer.
  ThreadPoolToken* raft_pool_token_;

  enum State {
    kPeerCreated,
    kPeerStarted,
    kPeerRunning,
    kPeerClosed
  };

  // Lock that protects Peer state changes, initialization, etc.  Must not try to acquire sem_ while
  // holding peer_lock_.
  mutable simple_spinlock peer_lock_;
  State state_ = kPeerCreated;
  Consensus* consensus_ = nullptr;
  rpc::Messenger* messenger_ = nullptr;
  std::atomic<int> using_thread_pool_{0};
};

// A proxy to another peer. Usually a thin wrapper around an rpc proxy but can be replaced for
// tests.
class PeerProxy {
 public:

  // Sends a request, asynchronously, to a remote peer.
  virtual void UpdateAsync(const LWConsensusRequestPB* request,
                           RequestTriggerMode trigger_mode,
                           LWConsensusResponsePB* response,
                           rpc::RpcController* controller,
                           const rpc::ResponseCallback& callback) = 0;

  // Sends a RequestConsensusVote to a remote peer.
  virtual void RequestConsensusVoteAsync(const VoteRequestPB* request,
                                         VoteResponsePB* response,
                                         rpc::RpcController* controller,
                                         const rpc::ResponseCallback& callback) = 0;

  // Instructs a peer to begin a remote bootstrap session.
  virtual void StartRemoteBootstrap(const StartRemoteBootstrapRequestPB* request,
                                    StartRemoteBootstrapResponsePB* response,
                                    rpc::RpcController* controller,
                                    const rpc::ResponseCallback& callback) {
    LOG(DFATAL) << "Not implemented";
  }

  // Sends a RunLeaderElection request to a peer.
  virtual void RunLeaderElectionAsync(const RunLeaderElectionRequestPB* request,
                                      RunLeaderElectionResponsePB* response,
                                      rpc::RpcController* controller,
                                      const rpc::ResponseCallback& callback) {
    LOG(DFATAL) << "Not implemented";
  }

  virtual void LeaderElectionLostAsync(const LeaderElectionLostRequestPB* request,
                                       LeaderElectionLostResponsePB* response,
                                       rpc::RpcController* controller,
                                       const rpc::ResponseCallback& callback) {
    LOG(DFATAL) << "Not implemented";
  }

  virtual ~PeerProxy() {}
};

typedef std::unique_ptr<PeerProxy> PeerProxyPtr;

// A peer proxy factory. Usually just obtains peers through the rpc implementation but can be
// replaced for tests.
class PeerProxyFactory {
 public:
  virtual PeerProxyPtr NewProxy(const RaftPeerPB& peer_pb) = 0;

  virtual ~PeerProxyFactory() {}

  virtual rpc::Messenger* messenger() const {
    return nullptr;
  }
};

// PeerProxy implementation that does RPC calls
class RpcPeerProxy : public PeerProxy {
 public:
  RpcPeerProxy(HostPort hostport, ConsensusServiceProxyPtr consensus_proxy);

  virtual void UpdateAsync(const LWConsensusRequestPB* request,
                           RequestTriggerMode trigger_mode,
                           LWConsensusResponsePB* response,
                           rpc::RpcController* controller,
                           const rpc::ResponseCallback& callback) override;

  virtual void RequestConsensusVoteAsync(const VoteRequestPB* request,
                                         VoteResponsePB* response,
                                         rpc::RpcController* controller,
                                         const rpc::ResponseCallback& callback) override;

  virtual void StartRemoteBootstrap(const StartRemoteBootstrapRequestPB* request,
                                    StartRemoteBootstrapResponsePB* response,
                                    rpc::RpcController* controller,
                                    const rpc::ResponseCallback& callback) override;

  virtual void RunLeaderElectionAsync(const RunLeaderElectionRequestPB* request,
                                      RunLeaderElectionResponsePB* response,
                                      rpc::RpcController* controller,
                                      const rpc::ResponseCallback& callback) override;

  virtual void LeaderElectionLostAsync(const LeaderElectionLostRequestPB* request,
                                       LeaderElectionLostResponsePB* response,
                                       rpc::RpcController* controller,
                                       const rpc::ResponseCallback& callback) override;

  virtual ~RpcPeerProxy();

 private:
  HostPort hostport_;
  ConsensusServiceProxyPtr consensus_proxy_;
};

// PeerProxyFactory implementation that generates RPCPeerProxies
class RpcPeerProxyFactory : public PeerProxyFactory {
 public:
  RpcPeerProxyFactory(rpc::Messenger* messenger, rpc::ProxyCache* proxy_cache, CloudInfoPB from);

  PeerProxyPtr NewProxy(const RaftPeerPB& peer_pb) override;

  virtual ~RpcPeerProxyFactory();

  rpc::Messenger* messenger() const override;

 private:
  rpc::Messenger* messenger_ = nullptr;
  rpc::ProxyCache* const proxy_cache_;
  const CloudInfoPB from_;
};

// Query the consensus service at last known host/port that is specified in 'remote_peer' and set
// the 'permanent_uuid' field based on the response.
Status SetPermanentUuidForRemotePeer(
    rpc::ProxyCache* proxy_cache,
    std::chrono::steady_clock::duration timeout,
    const std::vector<HostPort>& endpoints,
    RaftPeerPB* remote_peer);

}  // namespace consensus
}  // namespace yb
