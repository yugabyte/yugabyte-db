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

#include "yb/rpc/proxy.h"

#include <inttypes.h>
#include <stdint.h>

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <glog/logging.h>

#include "yb/rpc/local_call.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/response_callback.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/status.h"
#include "yb/util/user.h"

DEFINE_int32(num_connections_to_server, 4, "Number of underlying connections to each server");

using google::protobuf::Message;
using std::string;
using std::shared_ptr;

namespace yb {
namespace rpc {

Proxy::Proxy(const std::shared_ptr<Messenger>& messenger,
             const Endpoint& remote, string service_name)
    : service_name_(std::move(service_name)),
      messenger_(messenger),
      outbound_call_metrics_(messenger && messenger->metric_entity() ?
          std::make_shared<OutboundCallMetrics>(messenger_->metric_entity()) : nullptr),
      call_local_service_(remote == Endpoint()) {
  CHECK(messenger != nullptr);
  DCHECK(!service_name_.empty()) << "Proxy service name must not be blank";

  // By default, we set the real user to the currently logged-in user.
  // Effective user and password remain blank.
  string real_user;
  Status s = GetLoggedInUser(&real_user);
  if (!s.ok()) {
    LOG(WARNING) << "Proxy for " << service_name_ << ": Unable to get logged-in user name: "
                 << s.ToString() << " before connecting to remote: " << remote;
  }

  conn_id_.set_remote(remote);
  conn_id_.mutable_user_credentials()->set_real_user(real_user);
  is_started_.store(false, std::memory_order_release);
  num_calls_.store(0, std::memory_order_release);
}

Proxy::~Proxy() {
}

void Proxy::AsyncRequest(const string& method,
                         const google::protobuf::Message& req,
                         google::protobuf::Message* resp,
                         RpcController* controller,
                         ResponseCallback callback) const {
  CHECK(controller->call_.get() == nullptr) << "Controller should be reset";
  is_started_.store(true, std::memory_order_release);
  uint8_t idx = num_calls_.fetch_add(1) % FLAGS_num_connections_to_server;
  auto indexed_conn_id = conn_id_;
  indexed_conn_id.set_idx(idx);

  controller->call_ =
      call_local_service_ ?
      std::make_shared<LocalOutboundCall>(indexed_conn_id,
                                          RemoteMethod(service_name_, method),
                                          outbound_call_metrics_,
                                          resp,
                                          controller,
                                          std::move(callback)) :
      std::make_shared<OutboundCall>(indexed_conn_id,
                                     RemoteMethod(service_name_, method),
                                     outbound_call_metrics_,
                                     resp,
                                     controller,
                                     std::move(callback));
  auto call = controller->call_.get();
  Status s = call->SetRequestParam(req);
  if (PREDICT_FALSE(!s.ok())) {
    // Failed to serialize request: likely the request is missing a required
    // field.
    call->SetFailed(s); // calls callback internally
    return;
  }

  if (call_local_service_) {
    // For local call, the response message buffer is reused when an RPC call is retried. So clear
    // the buffer before calling the RPC method.
    resp->Clear();
    call->SetQueued();
    call->SetSent();
    // If currrent thread is RPC worker thread, it is ok to call the handler in the current thread.
    // Otherwise, enqueue the call to be handled by the service's handler thread.
    const shared_ptr<LocalYBInboundCall>& local_call =
        static_cast<LocalOutboundCall*>(call)->CreateLocalInboundCall();
    if (ThreadPool::IsCurrentThreadRpcWorker()) {
      messenger_->Handle(local_call);
    } else {
      messenger_->QueueInboundCall(local_call);
    }
  } else {
    // If this fails to queue, the callback will get called immediately
    // and the controller will be in an ERROR state.
    messenger_->QueueOutboundCall(controller->call_);
  }
}


Status Proxy::SyncRequest(const string& method,
                          const google::protobuf::Message& req,
                          google::protobuf::Message* resp,
                          RpcController* controller) const {
  CountDownLatch latch(1);
  AsyncRequest(method, req, DCHECK_NOTNULL(resp), controller, [&latch]() { latch.CountDown(); });

  latch.Wait();
  return controller->status();
}

void Proxy::set_user_credentials(const UserCredentials& user_credentials) {
  CHECK(!is_started_.load(std::memory_order_acquire))
      << "It is illegal to call set_user_credentials() after request processing has started";
  conn_id_.set_user_credentials(user_credentials);
}

}  // namespace rpc
}  // namespace yb
