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
// This module is internal to the client and not a public API.
#ifndef KUDU_MASTER_MASTER_RPC_H
#define KUDU_MASTER_MASTER_RPC_H

#include <vector>
#include <string>

#include "kudu/gutil/ref_counted.h"
#include "kudu/master/master.pb.h"
#include "kudu/rpc/rpc.h"
#include "kudu/util/locks.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"


namespace kudu {

class ServerEntryPB;
class HostPort;

namespace master {

// An RPC for getting a Master server's registration.
class GetMasterRegistrationRpc : public rpc::Rpc {
 public:

  // Create a wrapper object for a retriable GetMasterRegistration RPC
  // to 'addr'. The result is stored in 'out', which must be a valid
  // pointer for the lifetime of this object.
  //
  // Invokes 'user_cb' upon failure or success of the RPC call.
  GetMasterRegistrationRpc(StatusCallback user_cb, Sockaddr addr,
                           const MonoTime& deadline,
                           const std::shared_ptr<rpc::Messenger>& messenger,
                           ServerEntryPB* out);

  ~GetMasterRegistrationRpc();

  virtual void SendRpc() OVERRIDE;

  virtual std::string ToString() const OVERRIDE;

 private:
  virtual void SendRpcCb(const Status& status) OVERRIDE;

  StatusCallback user_cb_;
  Sockaddr addr_;

  ServerEntryPB* out_;

  GetMasterRegistrationResponsePB resp_;
};

// In parallel, send requests to the specified Master servers until a
// response comes back from the leader of the Master consensus configuration.
//
// If queries have been made to all of the specified servers, but no
// leader has been found, we re-try again (with an increasing delay,
// see: RpcRetrier in kudu/rpc/rpc.{cc,h}) until a specified deadline
// passes or we find a leader.
//
// The RPCs are sent in parallel in order to avoid prolonged delays on
// the client-side that would happen with a serial approach when one
// of the Master servers is slow or stopped (that is, when we have to
// wait for an RPC request to server N to timeout before we can make
// an RPC request to server N+1). This allows for true fault tolerance
// for the Kudu client.
//
// The class is reference counted to avoid a "use-after-free"
// scenario, when responses to the RPC return to the caller _after_ a
// leader has already been found.
class GetLeaderMasterRpc : public rpc::Rpc,
                           public RefCountedThreadSafe<GetLeaderMasterRpc> {
 public:
  typedef Callback<void(const Status&, const HostPort&)> LeaderCallback;
  // The host and port of the leader master server is stored in
  // 'leader_master', which must remain valid for the lifetime of this
  // object.
  //
  // Calls 'user_cb' when the leader is found, or if no leader can be
  // found until 'deadline' passes.
  GetLeaderMasterRpc(LeaderCallback user_cb, std::vector<Sockaddr> addrs,
                     const MonoTime& deadline,
                     const std::shared_ptr<rpc::Messenger>& messenger);

  virtual void SendRpc() OVERRIDE;

  virtual std::string ToString() const OVERRIDE;
 private:
  friend class RefCountedThreadSafe<GetLeaderMasterRpc>;
  ~GetLeaderMasterRpc();

  virtual void SendRpcCb(const Status& status) OVERRIDE;

  // Invoked when a response comes back from a Master with address
  // 'node_addr'.
  //
  // Invokes SendRpcCb if the response indicates that the specified
  // master is a leader, or if responses have been received from all
  // of the Masters.
  void GetMasterRegistrationRpcCbForNode(const Sockaddr& node_addr,
                                         const ServerEntryPB& resp,
                                         const Status& status);

  LeaderCallback user_cb_;
  std::vector<Sockaddr> addrs_;

  HostPort leader_master_;

  // The received responses.
  //
  // See also: GetMasterRegistrationRpc above.
  std::vector<ServerEntryPB> responses_;

  // Number of pending responses.
  int pending_responses_;

  // If true, then we've already executed the user callback and the
  // RPC can be deallocated.
  bool completed_;

  // Protects 'pending_responses_' and 'completed_'.
  mutable simple_spinlock lock_;
};

} // namespace master
} // namespace kudu

#endif /* KUDU_MASTER_MASTER_RPC_H */
