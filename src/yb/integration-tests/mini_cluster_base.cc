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

#include "yb/client/stateful_services/stateful_service_client_base.h"
#include "yb/server/hybrid_clock.h"
#include "yb/rpc/secure.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/rpc/messenger.h"

DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_dir);

namespace yb {

namespace {
Result<std::unique_ptr<client::YBClient>> CreateSecureClientInternal(
    const std::string& name, const std::string& host,
    std::unique_ptr<rpc::SecureContext>* secure_context, client::YBClientBuilder* builder) {
  rpc::MessengerBuilder messenger_builder("test_client");
  *secure_context = VERIFY_RESULT(rpc::SetupSecureContext(
      FLAGS_certs_dir, name, rpc::SecureContextType::kInternal, &messenger_builder));
  auto messenger = VERIFY_RESULT(messenger_builder.Build());
  messenger->TEST_SetOutboundIpBase(VERIFY_RESULT(HostToAddress(host)));
  auto clock = make_scoped_refptr<server::HybridClock>();
  RETURN_NOT_OK(clock->Init());
  return builder->Build(std::move(messenger), clock);
}
}  // namespace

Result<std::unique_ptr<client::YBClient>> MiniClusterBase::CreateClient(rpc::Messenger* messenger) {
  client::YBClientBuilder builder;
  ConfigureClientBuilder(&builder);
  auto clock = make_scoped_refptr<server::HybridClock>();
  RETURN_NOT_OK(clock->Init());
  return builder.Build(messenger, clock);
}

// Created client will shutdown messenger on client shutdown.
Result<std::unique_ptr<client::YBClient>> MiniClusterBase::CreateClient(
    client::YBClientBuilder* builder) {
  if (builder == nullptr) {
    client::YBClientBuilder default_builder;
    return CreateClient(&default_builder);
  }

  if (FLAGS_use_node_to_node_encryption) {
    const auto host = "127.0.0.52";
    ConfigureClientBuilder(builder);
    return CreateSecureClientInternal(host, host, &secure_context_, builder);
  }

  ConfigureClientBuilder(builder);
  auto clock = make_scoped_refptr<server::HybridClock>();
  RETURN_NOT_OK(clock->Init());
  return builder->Build(nullptr, clock);
}

Result<std::unique_ptr<client::YBClient>> MiniClusterBase::CreateSecureClient(
    const std::string& name, const std::string& host,
    std::unique_ptr<rpc::SecureContext>* secure_context) {
  client::YBClientBuilder builder;
  ConfigureClientBuilder(&builder);
  return CreateSecureClientInternal(name, host, secure_context, &builder);
}

Result<HostPort> MiniClusterBase::GetLeaderMasterBoundRpcAddr() {
  return DoGetLeaderMasterBoundRpcAddr();
}

Status MiniClusterBase::InitStatefulServiceClient(client::StatefulServiceClientBase* client) {
  auto host_port = VERIFY_RESULT(GetLeaderMasterBoundRpcAddr());
  return client->TEST_Init("127.0.0.52", host_port.ToString());
}
}  // namespace yb
