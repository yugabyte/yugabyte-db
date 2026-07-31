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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <stddef.h>
#include <stdint.h>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <string>
#include <vector>

#include "yb/rpc/rpc_fwd.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/tostring.h"
#include "yb/gutil/macros.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"

namespace yb {
namespace rpc {
class Messenger;
class ServicePool;
}  // namespace rpc

namespace server {

struct RpcServerOptions {
  RpcServerOptions();

  std::string rpc_bind_addresses;
  uint16_t default_port = 0;
  int32_t connection_keepalive_time_ms;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(rpc_bind_addresses, default_port, connection_keepalive_time_ms);
  }
};

class RpcServer {
 public:
  RpcServer(const std::string& name, RpcServerOptions opts,
            rpc::ConnectionContextFactoryPtr connection_context_factory);
  ~RpcServer();

  Status Init(rpc::Messenger* messenger);
  // Services need to be registered after Init'ing, but before Start'ing.
  // The service's ownership will be given to a ServicePool.
  Status RegisterService(
      size_t queue_limit, rpc::ServiceIfPtr service,
      rpc::ServicePriority priority = rpc::ServicePriority::kNormal);
  Status Bind();
  Status Start();
  void Shutdown();

  const std::vector<Endpoint>& GetBoundAddresses() const {
    return rpc_bound_addresses_;
  }

  const std::vector<HostPort>& GetRpcHostPort() const {
    return rpc_host_port_;
  }

  std::string ToString() const;

  const rpc::ServicePool* TEST_service_pool(const std::string& service_name) const;

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

  std::string name_;

  ServerState server_state_;

  const RpcServerOptions options_;

  rpc::Messenger* messenger_ = nullptr;

  // Parsed addresses to bind RPC to. Set by Init().
  std::vector<Endpoint> rpc_bind_addresses_;
  std::vector<Endpoint> rpc_bound_addresses_;

  // This saves the rpc host port flag's ip and port information (and no dns name lookup is done).
  std::vector<HostPort> rpc_host_port_;

  rpc::ConnectionContextFactoryPtr connection_context_factory_;

  DISALLOW_COPY_AND_ASSIGN(RpcServer);
};

} // namespace server
} // namespace yb
