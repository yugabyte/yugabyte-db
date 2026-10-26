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

#include "yb/master_host/tserver_sim_host.h"

#include <stdexcept>

#include "yb/rpc/yb_rpc.h"

#include "yb/server/clock.h"
#include "yb/server/server_base_options.h"


namespace yb::master_host {

namespace {

void ThrowIfNotOk(const Status& status) {
  if (!status.ok()) {
    throw std::runtime_error(status.ToString());
  }
}

class TserverSimOptions : public server::ServerBaseOptions {
 public:
  TserverSimOptions() : ServerBaseOptions(/* default_port= */ 0) {}
};

}  // namespace

TserverSimHost::TserverSimHost(const server::ServerBaseOptions& opts)
    : RpcServerBase(
          "TserverSim", opts, "yb.tserver_sim", server::CreateMemTrackerForServer()) {
  // No inbound memory limit: the default of inbound_rpc_memory_limit, defined in the tserver.
  SetConnectionContextFactory(rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(
      /* memory_limit= */ 0, mem_tracker()));
}

TserverSimHost::~TserverSimHost() {
  ShutdownHost();
}

void TserverSimHost::InitHost() {
  ThrowIfNotOk(Init());
}

void TserverSimHost::StartHost() {
  ThrowIfNotOk(StartRpcServer());
}

void TserverSimHost::ShutdownHost() {
  if (!shut_down_ && initialized_) {
    shut_down_ = true;
    Shutdown();
  }
}

Status TserverSimHost::RegisterRustService(
    size_t queue_limit, rpc::ServiceIfPtr service, rpc::ServicePriority priority) {
  return RegisterService(queue_limit, std::move(service), priority);
}

std::unique_ptr<TserverSimHost> CreateTserverSimHost(rust::Str rpc_bind_addresses) {
  TserverSimOptions opts;
  opts.rpc_opts.rpc_bind_addresses = std::string(rpc_bind_addresses);
  return std::make_unique<TserverSimHost>(opts);
}

}  // namespace yb::master_host
