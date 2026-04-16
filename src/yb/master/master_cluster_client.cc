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

#include "yb/master/master_cluster_client.h"

#include <algorithm>

#include "yb/common/wire_protocol.h"

namespace yb::master {

MasterClusterClient::MasterClusterClient(MasterClusterProxy&& proxy) noexcept
    : proxy_(std::move(proxy)) {}

Status MasterClusterClient::BlacklistHost(HostPortPB&& hp) const {
  auto config = VERIFY_RESULT(GetMasterClusterConfig());
  *config.mutable_server_blacklist()->add_hosts() = std::move(hp);
  return ChangeMasterClusterConfig(std::move(config));
}

Status MasterClusterClient::UnBlacklistHost(const HostPortPB& hp) const {
  auto config = VERIFY_RESULT(GetMasterClusterConfig());
  auto* hosts = config.mutable_server_blacklist()->mutable_hosts();
  auto new_end =
      std::remove_if(hosts->begin(), hosts->end(), [&hp](const auto& current_hp) -> bool {
        return current_hp.host() == hp.host() && current_hp.port() == hp.port();
      });
  (void)new_end;
  return ChangeMasterClusterConfig(std::move(config));
}

Status MasterClusterClient::ClearBlacklist() const {
  auto config = VERIFY_RESULT(GetMasterClusterConfig());
  config.mutable_server_blacklist()->clear_hosts();
  return ChangeMasterClusterConfig(std::move(config));
}

Result<std::optional<ListTabletServersResponsePB::Entry>> MasterClusterClient::GetTabletServer(
    const std::string& uuid) const {
  auto resp = VERIFY_RESULT(ListTabletServers());
  auto tserver_it = std::find_if(
      resp.servers().begin(), resp.servers().end(),
      [&uuid](const auto& server) { return server.instance_id().permanent_uuid() == uuid; });
  if (tserver_it == resp.servers().end()) {
    return std::nullopt;
  }
  return std::move(*tserver_it);
}

Result<ListTabletServersResponsePB> MasterClusterClient::ListTabletServers() const {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.ListTabletServers(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp;
}

Result<ListLiveTabletServersResponsePB> MasterClusterClient::ListLiveTabletServers() const {
  ListLiveTabletServersRequestPB req;
  ListLiveTabletServersResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.ListLiveTabletServers(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp;
}

Result<SysClusterConfigEntryPB> MasterClusterClient::GetMasterClusterConfig() const {
  GetMasterClusterConfigRequestPB req;
  GetMasterClusterConfigResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.GetMasterClusterConfig(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  if (!resp.has_cluster_config()) {
    return STATUS(IllegalState, "GetMasterClusterConfig response is missing cluster config");
  }
  return std::move(*resp.mutable_cluster_config());
}

Status MasterClusterClient::ChangeMasterClusterConfig(
    SysClusterConfigEntryPB&& cluster_config) const {
  ChangeMasterClusterConfigRequestPB req;
  *req.mutable_cluster_config() = std::move(cluster_config);
  ChangeMasterClusterConfigResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.ChangeMasterClusterConfig(req, &resp, &rpc));
  return ResponseStatus(resp);
}

Status MasterClusterClient::RemoveTabletServer(std::string&& uuid) const {
  RemoveTabletServerRequestPB req;
  RemoveTabletServerResponsePB resp;
  rpc::RpcController rpc;
  *req.mutable_permanent_uuid() = std::move(uuid);
  RETURN_NOT_OK(proxy_.RemoveTabletServer(req, &resp, &rpc));
  return ResponseStatus(resp);
}

}  // namespace yb::master
