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

#include "yb/server/rpc_server.h"

#include <list>
#include <string>
#include <vector>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/gutil/casts.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/service_pool.h"

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/metric_entity.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"

using yb::rpc::Messenger;
using std::string;

DEFINE_UNKNOWN_string(rpc_bind_addresses, "0.0.0.0",
              "Comma-separated list of addresses to bind to for RPC connections. "
              "Currently, ephemeral ports (i.e. port 0) are not allowed.");
TAG_FLAG(rpc_bind_addresses, stable);

DEFINE_UNKNOWN_bool(rpc_server_allow_ephemeral_ports, false,
            "Allow binding to ephemeral ports. This can cause problems, so currently "
            "only allowed in tests.");
TAG_FLAG(rpc_server_allow_ephemeral_ports, unsafe);

DECLARE_int32(rpc_default_keepalive_time_ms);

namespace yb {
namespace server {

RpcServerOptions::RpcServerOptions()
  : rpc_bind_addresses(FLAGS_rpc_bind_addresses),
    connection_keepalive_time_ms(FLAGS_rpc_default_keepalive_time_ms) {
}

RpcServer::RpcServer(const std::string& name, RpcServerOptions opts,
                     rpc::ConnectionContextFactoryPtr connection_context_factory)
    : name_(name),
      server_state_(UNINITIALIZED),
      options_(std::move(opts)),
      connection_context_factory_(std::move(connection_context_factory)) {
        LOG(INFO) << "yb::server::RpcServer created at " << this;
      }

RpcServer::~RpcServer() {
  Shutdown();
}

string RpcServer::ToString() const {
  // TODO: include port numbers, etc.
  return "RpcServer";
}

Status RpcServer::Init(Messenger* messenger) {
  CHECK_EQ(server_state_, UNINITIALIZED);
  messenger_ = messenger;

  RETURN_NOT_OK(HostPort::ParseStrings(options_.rpc_bind_addresses,
                                       options_.default_port,
                                       &rpc_host_port_));

  RETURN_NOT_OK(ParseAddressList(options_.rpc_bind_addresses,
                                 options_.default_port,
                                 &rpc_bind_addresses_));
  for (const auto& addr : rpc_bind_addresses_) {
    if (IsPrivilegedPort(addr.port())) {
      LOG(WARNING) << "May be unable to bind to privileged port for address " << addr;
    }

    // Currently, we can't support binding to ephemeral ports outside of
    // unit tests, because consensus caches RPC ports of other servers
    // across restarts. See KUDU-334.
    if (addr.port() == 0 && !FLAGS_rpc_server_allow_ephemeral_ports) {
      LOG(FATAL) << "Binding to ephemeral ports not supported (RPC address "
                 << "configured to " << addr << ")";
    }
  }

  server_state_ = INITIALIZED;
  return Status::OK();
}

Status RpcServer::RegisterService(size_t queue_limit,
                                  rpc::ServiceIfPtr service,
                                  rpc::ServicePriority priority) {
  CHECK(server_state_ == INITIALIZED ||
        server_state_ == BOUND) << "bad state: " << server_state_;
  const scoped_refptr<MetricEntity>& metric_entity = messenger_->metric_entity();
  string service_name = service->service_name();

  rpc::ThreadPool& thread_pool = messenger_->ThreadPool(priority);

  scoped_refptr<rpc::ServicePool> service_pool(new rpc::ServicePool(
      queue_limit, &thread_pool, &messenger_->scheduler(), std::move(service), metric_entity));
  RETURN_NOT_OK(messenger_->RegisterService(service_name, service_pool));
  return Status::OK();
}

Status RpcServer::Bind() {
  CHECK_EQ(server_state_, INITIALIZED);

  rpc_bound_addresses_.resize(rpc_bind_addresses_.size());
  for (size_t i = 0; i != rpc_bind_addresses_.size(); ++i) {
    RETURN_NOT_OK(messenger_->ListenAddress(
        connection_context_factory_, rpc_bind_addresses_[i], &rpc_bound_addresses_[i]));
  }

  server_state_ = BOUND;
  return Status::OK();
}

Status RpcServer::Start() {
  if (server_state_ == INITIALIZED) {
    RETURN_NOT_OK(Bind());
  }
  CHECK_EQ(server_state_, BOUND);
  server_state_ = STARTED;

  RETURN_NOT_OK(messenger_->StartAcceptor());
  string bound_addrs_str;
  for (const auto& bind_addr : rpc_bound_addresses_) {
    if (!bound_addrs_str.empty()) bound_addrs_str += ", ";
    bound_addrs_str += yb::ToString(bind_addr);
  }
  LOG(INFO) << "RPC server started. Bound to: " << bound_addrs_str;

  return Status::OK();
}

void RpcServer::Shutdown() {
  if (messenger_) {
    messenger_->ShutdownThreadPools();
    messenger_->ShutdownAcceptor();
    messenger_->UnregisterAllServices();
  }
}

const rpc::ServicePool* RpcServer::TEST_service_pool(const string& service_name) const {
  return down_cast<rpc::ServicePool*>(messenger_->TEST_rpc_service(service_name).get());
}

} // namespace server
} // namespace yb
