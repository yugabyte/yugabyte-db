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
#ifndef KUDU_RPC_SERVER_H
#define KUDU_RPC_SERVER_H

#include <memory>
#include <string>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/rpc/service_pool.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/status.h"

namespace kudu {

namespace rpc {
class AcceptorPool;
class Messenger;
class ServiceIf;
} // namespace rpc

struct RpcServerOptions {
  RpcServerOptions();

  std::string rpc_bind_addresses;
  uint32_t num_acceptors_per_address;
  uint32_t num_service_threads;
  uint16_t default_port;
  size_t service_queue_length;
};

class RpcServer {
 public:
  explicit RpcServer(RpcServerOptions opts);
  ~RpcServer();

  Status Init(const std::shared_ptr<rpc::Messenger>& messenger);
  // Services need to be registered after Init'ing, but before Start'ing.
  // The service's ownership will be given to a ServicePool.
  Status RegisterService(gscoped_ptr<rpc::ServiceIf> service);
  Status Bind();
  Status Start();
  void Shutdown();

  std::string ToString() const;

  // Return the addresses that this server has successfully
  // bound to. Requires that the server has been Start()ed.
  Status GetBoundAddresses(std::vector<Sockaddr>* addresses) const WARN_UNUSED_RESULT;

  const rpc::ServicePool* service_pool(const std::string& service_name) const;

 private:
  enum ServerState {
    // Default state when the rpc server is constructed.
    UNINITIALIZED,
    // State after Init() was called.
    INITIALIZED,
    // State after Bind().
    BOUND,
    // State after Start() was called.
    STARTED
  };
  ServerState server_state_;

  const RpcServerOptions options_;
  std::shared_ptr<rpc::Messenger> messenger_;

  // Parsed addresses to bind RPC to. Set by Init()
  std::vector<Sockaddr> rpc_bind_addresses_;

  std::vector<std::shared_ptr<rpc::AcceptorPool> > acceptor_pools_;

  DISALLOW_COPY_AND_ASSIGN(RpcServer);
};

} // namespace kudu

#endif
