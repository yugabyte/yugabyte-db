// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

#include "yb/common/common_net.pb.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_base_options.h"

namespace yb {
class Cgroup;
} // namespace yb

namespace yb::tserver {

class MasterLeaderFinder {
 public:
  // A master leader and the placement it registered. The two travel together because every
  // proxy to the leader is scoped by that placement, the way DesiredHostPort and
  // ProxyContext::ProtocolFor both take the destination's.
  struct MasterLeader {
    HostPort hostport;
    ServerRegistrationPB registration;
  };

  MasterLeaderFinder(
      rpc::Messenger* messenger, rpc::ProxyCache& proxy_cache,
      server::MasterAddressesPtr master_addresses, CloudInfoPB connect_from);
  Result<MasterLeader> UpdateMasterLeader(MonoDelta timeout) EXCLUDES(master_meta_mtx_);
  rpc::ProxyCache& get_proxy_cache();
  server::MasterAddressesPtr get_master_addresses() const EXCLUDES(master_meta_mtx_);
  HostPort get_master_leader_hostport() const EXCLUDES(master_meta_mtx_);
  void set_master_addresses(server::MasterAddressesPtr master_addresses) EXCLUDES(master_meta_mtx_);
  void Shutdown();

  template <class P>
  Result<P> CreateProxy(MonoDelta timeout) {
    auto leader = VERIFY_RESULT(UpdateMasterLeader(timeout));
    return P(&get_proxy_cache(), leader.hostport, &ProtocolFor(leader));
  }

 private:
  server::MasterAddressesPtr get_master_addresses_unlocked() const REQUIRES(master_meta_mtx_);
  Result<MasterLeader> FindMasterLeader(MonoDelta timeout) REQUIRES(master_meta_mtx_);

  // The transport for a connection to this leader, from this server's own placement.
  const rpc::Protocol& ProtocolFor(const MasterLeader& leader) const;

  rpc::Rpcs rpcs_;
  rpc::Messenger* messenger_;
  rpc::ProxyCache& proxy_cache_;
  const CloudInfoPB connect_from_;
  MasterLeader master_leader_ GUARDED_BY(master_meta_mtx_);
  mutable std::mutex master_meta_mtx_;
  server::MasterAddressesPtr master_addresses_ GUARDED_BY(master_meta_mtx_);
};

class MasterLeaderPollerInterface {
 public:
  virtual ~MasterLeaderPollerInterface() = default;
  virtual Status Poll() = 0;
  virtual MonoDelta IntervalToNextPoll(int32_t consecutive_failures) = 0;
  virtual void Init() = 0;
  virtual void ResetProxy() = 0;
  virtual std::string name() = 0;
  virtual const std::string& LogPrefix() const = 0;
};

class MasterLeaderPollScheduler {
 public:
  MasterLeaderPollScheduler(MasterLeaderFinder& connector, MasterLeaderPollerInterface& poller);
  ~MasterLeaderPollScheduler();

  Status Start(Cgroup* cgroup = nullptr);
  void Shutdown();
  void TriggerASAP();
  void UpdateMasterAddresses(server::MasterAddressesPtr master_addresses);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
