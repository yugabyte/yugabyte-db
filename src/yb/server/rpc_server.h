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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_SERVER_RPC_SERVER_H
#define YB_SERVER_RPC_SERVER_H

#include <memory>
#include <string>
#include <vector>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/service_pool.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/enums.h"

namespace yb {

struct RpcServerOptions {
  RpcServerOptions();

  std::string rpc_bind_addresses;
  uint16_t default_port;
  size_t queue_limit;
  size_t workers_limit;
  int32_t connection_keepalive_time_ms;
};

YB_DEFINE_ENUM(ServicePriority, (kNormal)(kHigh));

class RpcServer {
 public:
  explicit RpcServer(const std::string& name, RpcServerOptions opts);
  ~RpcServer();

  CHECKED_STATUS Init(const std::shared_ptr<rpc::Messenger>& messenger);
  // Services need to be registered after Init'ing, but before Start'ing.
  // The service's ownership will be given to a ServicePool.
  CHECKED_STATUS RegisterService(
      size_t queue_limit, std::unique_ptr<rpc::ServiceIf> service,
      ServicePriority priority = ServicePriority::kNormal);
  CHECKED_STATUS Bind();
  CHECKED_STATUS Start();
  void Shutdown();

  const std::vector<Endpoint>& GetBoundAddresses() const {
    return rpc_bound_addresses_;
  }

  const std::vector<HostPort>& GetRpcHostPort() const {
    return rpc_host_port_;
  }

  std::string ToString() const;

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

  std::unique_ptr<rpc::ThreadPool> CreateThreadPool(string name_prefix, ServicePriority priority);

  string name_;

  ServerState server_state_;

  const RpcServerOptions options_;
  std::unique_ptr<rpc::ThreadPool> normal_thread_pool_;

  // This could be used for high-priority services such as Consensus.
  std::unique_ptr<rpc::ThreadPool> high_priority_thread_pool_;

  std::shared_ptr<rpc::Messenger> messenger_;

  // Parsed addresses to bind RPC to. Set by Init().
  std::vector<Endpoint> rpc_bind_addresses_;
  std::vector<Endpoint> rpc_bound_addresses_;

  // This saves the rpc host port flag's ip and port information (and no dns name lookup is done).
  std::vector<HostPort> rpc_host_port_;

  DISALLOW_COPY_AND_ASSIGN(RpcServer);
};

} // namespace yb

#endif // YB_SERVER_RPC_SERVER_H
