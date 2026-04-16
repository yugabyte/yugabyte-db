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

#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"

namespace yb::master {

// Wrapper class around RPCs to the MasterCluster service. All RPC wrapper methods check the error
// field in response objects. For now only used in tests.
class MasterClusterClient {
 public:
  explicit MasterClusterClient(MasterClusterProxy&& proxy) noexcept;

  Status BlacklistHost(HostPortPB&& hp) const;
  Status UnBlacklistHost(const HostPortPB& hp) const;
  Status ClearBlacklist() const;

  Result<std::optional<ListTabletServersResponsePB::Entry>> GetTabletServer(
      const std::string& uuid) const;

  Result<ListTabletServersResponsePB> ListTabletServers() const;

  Result<ListLiveTabletServersResponsePB> ListLiveTabletServers() const;

  Result<SysClusterConfigEntryPB> GetMasterClusterConfig() const;

  Status ChangeMasterClusterConfig(SysClusterConfigEntryPB&& cluster_config) const;

  Status RemoveTabletServer(std::string&& uuid) const;

 private:
  MasterClusterProxy proxy_;
};
}  // namespace yb::master
