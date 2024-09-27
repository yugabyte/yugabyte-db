// Copyright (c) YugaByte, Inc.
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

#include "yb/common/wire_protocol.h"

namespace yb::master {

MasterClusterClient::MasterClusterClient(MasterClusterProxy&& proxy) noexcept
    : proxy_(std::move(proxy)) {}

Result<ListTabletServersResponsePB> MasterClusterClient::ListTabletServers() {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.ListTabletServers(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp;
}

Result<ListLiveTabletServersResponsePB> MasterClusterClient::ListLiveTabletServers() {
  ListLiveTabletServersRequestPB req;
  ListLiveTabletServersResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.ListLiveTabletServers(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp;
}

Result<GetMasterClusterConfigResponsePB> MasterClusterClient::GetMasterClusterConfig() {
  GetMasterClusterConfigRequestPB req;
  GetMasterClusterConfigResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.GetMasterClusterConfig(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp;
}

Result<ChangeMasterClusterConfigResponsePB> MasterClusterClient::ChangeMasterClusterConfig(
    const SysClusterConfigEntryPB& cluster_config) {
  ChangeMasterClusterConfigRequestPB req;
  *req.mutable_cluster_config() = cluster_config;
  ChangeMasterClusterConfigResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.ChangeMasterClusterConfig(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp;
}
}  // namespace yb::master
