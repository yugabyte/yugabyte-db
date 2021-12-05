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

#include "yb/integration-tests/mini_cluster_base.h"

#include "yb/client/client.h"

#include "yb/util/net/net_util.h"
#include "yb/util/result.h"

namespace yb {

Result<std::unique_ptr<client::YBClient>> MiniClusterBase::CreateClient(rpc::Messenger* messenger) {
  client::YBClientBuilder builder;
  ConfigureClientBuilder(&builder);
  return builder.Build(messenger);
}

// Created client will shutdown messenger on client shutdown.
Result<std::unique_ptr<client::YBClient>> MiniClusterBase::CreateClient(
    client::YBClientBuilder* builder) {
  if (builder == nullptr) {
    client::YBClientBuilder default_builder;
    return CreateClient(&default_builder);
  }
  ConfigureClientBuilder(builder);
  return builder->Build();
}

// Created client gets messenger ownership and will shutdown messenger on client shutdown.
Result<std::unique_ptr<client::YBClient>> MiniClusterBase::CreateClient(
    std::unique_ptr<rpc::Messenger>&& messenger) {
  client::YBClientBuilder builder;
  ConfigureClientBuilder(&builder);
  return builder.Build(std::move(messenger));
}

Result<HostPort> MiniClusterBase::GetLeaderMasterBoundRpcAddr() {
  return DoGetLeaderMasterBoundRpcAddr();
}

}  // namespace yb
