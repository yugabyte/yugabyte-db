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
// This module is internal to the client and not a public API.
#pragma once

#include <stdint.h>
#include <string.h>

#include <cstdarg>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/optional/optional_fwd.hpp>
#include <boost/version.hpp>
#include "yb/util/flags.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc.h"

#include "yb/server/server_base_options.h"

#include "yb/util/locks.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"

namespace yb {

class ServerEntryPB;
class HostPort;

namespace master {

// In parallel, send requests to the specified Master servers until a
// response comes back from the leader of the Master consensus configuration.
//
// If queries have been made to all of the specified servers, but no
// leader has been found, we re-try again (with an increasing delay,
// see: RpcRetrier in yb/rpc/rpc.{cc,h}) until a specified deadline
// passes or we find a leader.
//
// The RPCs are sent in parallel in order to avoid prolonged delays on
// the client-side that would happen with a serial approach when one
// of the Master servers is slow or stopped (that is, when we have to
// wait for an RPC request to server N to timeout before we can make
// an RPC request to server N+1). This allows for true fault tolerance
// for the YB client.
//
// The class is reference counted to avoid a "use-after-free"
// scenario, when responses to the RPC return to the caller _after_ a
// leader has already been found.
class GetLeaderMasterRpc : public rpc::Rpc {
 public:
  typedef Callback<void(const Status&, const HostPort&)> LeaderCallback;
  // The host and port of the leader master server is stored in
  // 'leader_master', which must remain valid for the lifetime of this
  // object.
  //
  // Calls 'user_cb' when the leader is found, or if no leader can be
  // found until 'deadline' passes.
  GetLeaderMasterRpc(LeaderCallback user_cb,
                     const server::MasterAddresses& addrs,
                     CoarseTimePoint deadline,
                     rpc::Messenger* messenger,
                     rpc::ProxyCache* proxy_cache,
                     rpc::Rpcs* rpcs,
                     bool should_timeout_to_follower_ = false,
                     bool wait_for_leader_election = true);

  ~GetLeaderMasterRpc();

  void SendRpc() override EXCLUDES(lock_);

  std::string ToString() const override;

 private:
  void Finished(const Status& status) override EXCLUDES(lock_);

  // Invoked when a response comes back from a Master with address
  // 'node_addr'.
  //
  // Invokes Finished if the response indicates that the specified
  // master is a leader, or if responses have been received from all
  // of the Masters.
  void GetMasterRegistrationRpcCbForNode(
      size_t idx, const Status& status, const std::shared_ptr<rpc::RpcCommand>& self,
      rpc::Rpcs::Handle handle) EXCLUDES(lock_);

  LeaderCallback user_cb_;
  std::vector<HostPort> addrs_;

  HostPort leader_master_;

  // The received responses.
  //
  // See also: GetMasterRegistrationRpc above.
  std::vector<ServerEntryPB> responses_;

  // Number of pending responses.
  size_t pending_responses_ = 0;

  // If true, then we've already executed the user callback and the
  // RPC can be deallocated.
  bool completed_ = false;

  // Protects 'pending_responses_' and 'completed_'.
  mutable simple_spinlock lock_;

  rpc::Rpcs& rpcs_;

  // The time of creation of the rpc, used for deadline tracking.
  MonoTime start_time_ = MonoTime::Now();

  // The number of master iterations the rpc has completed.
  int num_iters_ = 0;

  // Should the rpc timeout and pick a random follower instead of waiting for leader.
  bool should_timeout_to_follower_;

  bool wait_for_leader_election_;
};

} // namespace master
} // namespace yb
