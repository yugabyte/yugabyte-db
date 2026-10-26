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

// The C++ process host of master-tserver-sim: an RPC server with no storage, listening on one
// address per simulated tserver, whose services Rust handles
// (src/rust/master/tools/tserver-sim-main).

#pragma once

#include <memory>

#include "rust/cxx.h"

#include "yb/rust_bridge/rpc_bridge.h"

#include "yb/server/server_base.h"

namespace yb::master_host {

class TserverSimHost : public server::RpcServerBase, public rust_bridge::RpcServerHandle {
 public:
  explicit TserverSimHost(const server::ServerBaseOptions& opts);
  ~TserverSimHost();

  // Bridge API (master/tools/tserver-sim-main/src/ffi.rs). Failures throw std::runtime_error.

  // Creates the messenger and binds the RPC addresses.
  void InitHost();
  rust_bridge::RpcServerHandle& ServerHandle() { return *this; }
  // Starts serving. Rust registers its services before.
  void StartHost();
  void ShutdownHost();

  // RpcServerHandle.
  Status RegisterRustService(
      size_t queue_limit, rpc::ServiceIfPtr service, rpc::ServicePriority priority) override;
  rpc::ProxyCache& GetProxyCache() override { return proxy_cache(); }
  const scoped_refptr<MetricEntity>& GetMetricEntity() const override { return metric_entity(); }

 private:
  bool shut_down_ = false;
};

// `rpc_bind_addresses` lists one host:port per simulated tserver.
std::unique_ptr<TserverSimHost> CreateTserverSimHost(rust::Str rpc_bind_addresses);

}  // namespace yb::master_host
