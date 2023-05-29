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


#include "yb/client/universe_key_client.h"

#include "yb/client/client.h"

#include "yb/encryption/encryption.pb.h"

#include "yb/fs/fs_manager.h"

#include "yb/master/master_encryption.proxy.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"

using namespace std::chrono_literals;

DEFINE_RUNTIME_int32(
    universe_key_client_max_delay_ms, 2000,
    "Maximum Time in microseconds that an instance of Backoff_waiter waits before retrying to "
    "get the Universe key registry.");

DECLARE_bool(TEST_running_test);

namespace yb {
namespace client {

void UniverseKeyClient::GetUniverseKeyRegistryAsync() {
  for (const auto& host_port : hps_) {
    auto backoff_waiter = CoarseBackoffWaiter(
        CoarseTimePoint::max(), std::chrono::milliseconds(FLAGS_universe_key_client_max_delay_ms));
    SendAsyncRequest(host_port, backoff_waiter);
  }
}

void UniverseKeyClient::SendAsyncRequest(HostPort host_port, CoarseBackoffWaiter backoff_waiter) {
  master::GetUniverseKeyRegistryRequestPB req;
  auto resp = std::make_shared<master::GetUniverseKeyRegistryResponsePB>();
  auto rpc = std::make_shared<rpc::RpcController>();
  rpc->set_timeout(10s);

  master::MasterEncryptionProxy peer_proxy(proxy_cache_, host_port);
  peer_proxy.GetUniverseKeyRegistryAsync(
      req, resp.get(), rpc.get(),
      std::bind(
          &UniverseKeyClient::ProcessGetUniverseKeyRegistryResponse, this, resp, rpc, host_port,
          backoff_waiter));
}

void UniverseKeyClient::ProcessGetUniverseKeyRegistryResponse(
      std::shared_ptr<master::GetUniverseKeyRegistryResponsePB> resp,
      std::shared_ptr<rpc::RpcController> rpc,
      HostPort host_port,
      CoarseBackoffWaiter backoff_waiter) {
  if (!rpc->status().ok() || resp->has_error()) {
    YB_LOG_EVERY_N(WARNING, 100) << Format(
        "Rpc status: $0, resp: $1", rpc->status(), resp->ShortDebugString());

    // Always retry the request on failure.
    backoff_waiter.Wait();
    SendAsyncRequest(host_port, backoff_waiter);
    return;
  }
  LOG(INFO) << "Received universe keys from master: " << host_port.ToString();
  callback_(resp->universe_keys());
}

Result<encryption::UniverseKeyRegistryPB>UniverseKeyClient::GetFullUniverseKeyRegistry(
    const std::string& local_hosts,
    const std::string& master_addresses,
    const FsManager& fs_manager) {
  rpc::MessengerBuilder messenger_builder("universe_key_client");
  auto secure_context = VERIFY_RESULT(
        server::SetupInternalSecureContext(local_hosts, fs_manager, &messenger_builder));
  auto messenger = VERIFY_RESULT(messenger_builder.Build());
  auto se = ScopeExit([&] {
    if (messenger) {
      messenger->Shutdown();
    }
  });
  if (PREDICT_FALSE(FLAGS_TEST_running_test)) {
    std::vector<HostPort> host_ports;
    RETURN_NOT_OK(HostPort::ParseStrings(local_hosts, 0 /* default_port */, &host_ports));
    messenger->TEST_SetOutboundIpBase(VERIFY_RESULT(HostToAddress(host_ports[0].host())));
  }
  auto client = VERIFY_RESULT(yb::client::YBClientBuilder()
                              .add_master_server_addr(master_addresses)
                              .default_admin_operation_timeout(MonoDelta::FromSeconds(30))
                              .Build(messenger.get()));



  auto universe_key_registry = client->GetFullUniverseKeyRegistry();
  LOG(INFO) << "GetFullUniverseKeyRegistry returned successefully";
  return universe_key_registry;
}

} // namespace client
} // namespace yb
