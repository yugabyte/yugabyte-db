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

#include <list>
#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "kudu/gutil/casts.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/acceptor_pool.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/service_if.h"
#include "kudu/rpc/service_pool.h"
#include "kudu/server/rpc_server.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/status.h"

using kudu::rpc::AcceptorPool;
using kudu::rpc::Messenger;
using kudu::rpc::ServiceIf;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;

DEFINE_string(rpc_bind_addresses, "0.0.0.0",
              "Comma-separated list of addresses to bind to for RPC connections. "
              "Currently, ephemeral ports (i.e. port 0) are not allowed.");
TAG_FLAG(rpc_bind_addresses, stable);

DEFINE_int32(rpc_num_acceptors_per_address, 1,
             "Number of RPC acceptor threads for each bound address");
TAG_FLAG(rpc_num_acceptors_per_address, advanced);

DEFINE_int32(rpc_num_service_threads, 10,
             "Number of RPC worker threads to run");
TAG_FLAG(rpc_num_service_threads, advanced);

DEFINE_int32(rpc_service_queue_length, 50,
             "Default length of queue for incoming RPC requests");
TAG_FLAG(rpc_service_queue_length, advanced);

DEFINE_bool(rpc_server_allow_ephemeral_ports, false,
            "Allow binding to ephemeral ports. This can cause problems, so currently "
            "only allowed in tests.");
TAG_FLAG(rpc_server_allow_ephemeral_ports, unsafe);

namespace kudu {

RpcServerOptions::RpcServerOptions()
  : rpc_bind_addresses(FLAGS_rpc_bind_addresses),
    num_acceptors_per_address(FLAGS_rpc_num_acceptors_per_address),
    num_service_threads(FLAGS_rpc_num_service_threads),
    default_port(0),
    service_queue_length(FLAGS_rpc_service_queue_length) {
}

RpcServer::RpcServer(RpcServerOptions opts)
    : server_state_(UNINITIALIZED), options_(std::move(opts)) {}

RpcServer::~RpcServer() {
  Shutdown();
}

string RpcServer::ToString() const {
  // TODO: include port numbers, etc.
  return "RpcServer";
}

Status RpcServer::Init(const shared_ptr<Messenger>& messenger) {
  CHECK_EQ(server_state_, UNINITIALIZED);
  messenger_ = messenger;

  RETURN_NOT_OK(ParseAddressList(options_.rpc_bind_addresses,
                                 options_.default_port,
                                 &rpc_bind_addresses_));
  for (const Sockaddr& addr : rpc_bind_addresses_) {
    if (IsPrivilegedPort(addr.port())) {
      LOG(WARNING) << "May be unable to bind to privileged port for address "
                   << addr.ToString();
    }

    // Currently, we can't support binding to ephemeral ports outside of
    // unit tests, because consensus caches RPC ports of other servers
    // across restarts. See KUDU-334.
    if (addr.port() == 0 && !FLAGS_rpc_server_allow_ephemeral_ports) {
      LOG(FATAL) << "Binding to ephemeral ports not supported (RPC address "
                 << "configured to " << addr.ToString() << ")";
    }
  }

  server_state_ = INITIALIZED;
  return Status::OK();
}

Status RpcServer::RegisterService(gscoped_ptr<rpc::ServiceIf> service) {
  CHECK(server_state_ == INITIALIZED ||
        server_state_ == BOUND) << "bad state: " << server_state_;
  const scoped_refptr<MetricEntity>& metric_entity = messenger_->metric_entity();
  string service_name = service->service_name();
  scoped_refptr<rpc::ServicePool> service_pool =
    new rpc::ServicePool(service.Pass(), metric_entity, options_.service_queue_length);
  RETURN_NOT_OK(service_pool->Init(options_.num_service_threads));
  RETURN_NOT_OK(messenger_->RegisterService(service_name, service_pool));
  return Status::OK();
}

Status RpcServer::Bind() {
  CHECK_EQ(server_state_, INITIALIZED);

  // Create the Acceptor pools (one per bind address)
  vector<shared_ptr<AcceptorPool> > new_acceptor_pools;
  // Create the AcceptorPool for each bind address.
  for (const Sockaddr& bind_addr : rpc_bind_addresses_) {
    shared_ptr<rpc::AcceptorPool> pool;
    RETURN_NOT_OK(messenger_->AddAcceptorPool(
                    bind_addr,
                    &pool));
    new_acceptor_pools.push_back(pool);
  }
  acceptor_pools_.swap(new_acceptor_pools);

  server_state_ = BOUND;
  return Status::OK();
}

Status RpcServer::Start() {
  if (server_state_ == INITIALIZED) {
    RETURN_NOT_OK(Bind());
  }
  CHECK_EQ(server_state_, BOUND);
  server_state_ = STARTED;

  for (const shared_ptr<AcceptorPool>& pool : acceptor_pools_) {
    RETURN_NOT_OK(pool->Start(options_.num_acceptors_per_address));
  }

  vector<Sockaddr> bound_addrs;
  RETURN_NOT_OK(GetBoundAddresses(&bound_addrs));
  string bound_addrs_str;
  for (const Sockaddr& bind_addr : bound_addrs) {
    if (!bound_addrs_str.empty()) bound_addrs_str += ", ";
    bound_addrs_str += bind_addr.ToString();
  }
  LOG(INFO) << "RPC server started. Bound to: " << bound_addrs_str;

  return Status::OK();
}

void RpcServer::Shutdown() {
  for (const shared_ptr<AcceptorPool>& pool : acceptor_pools_) {
    pool->Shutdown();
  }
  acceptor_pools_.clear();

  if (messenger_) {
    WARN_NOT_OK(messenger_->UnregisterAllServices(), "Unable to unregister our services");
  }
}

Status RpcServer::GetBoundAddresses(vector<Sockaddr>* addresses) const {
  CHECK(server_state_ == BOUND ||
        server_state_ == STARTED) << "bad state: " << server_state_;
  for (const shared_ptr<AcceptorPool>& pool : acceptor_pools_) {
    Sockaddr bound_addr;
    RETURN_NOT_OK_PREPEND(pool->GetBoundAddress(&bound_addr),
                          "Unable to get bound address from AcceptorPool");
    addresses->push_back(bound_addr);
  }
  return Status::OK();
}

const rpc::ServicePool* RpcServer::service_pool(const string& service_name) const {
  return down_cast<rpc::ServicePool*>(messenger_->rpc_service(service_name).get());
}

} // namespace kudu
