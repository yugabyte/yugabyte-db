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

#include "yb/rpc/proxy.h"

#include <inttypes.h>
#include <stdint.h>

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include <glog/logging.h>

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

using google::protobuf::Message;
using std::string;
using std::shared_ptr;

namespace yb {
namespace rpc {

Proxy::Proxy(const std::shared_ptr<Messenger>& messenger,
             const Sockaddr& remote, string service_name)
    : service_name_(std::move(service_name)),
      messenger_(messenger) {
  CHECK(messenger != nullptr);
  DCHECK(!service_name_.empty()) << "Proxy service name must not be blank";

  // By default, we set the real user to the currently logged-in user.
  // Effective user and password remain blank.
  string real_user;
  Status s = GetLoggedInUser(&real_user);
  if (!s.ok()) {
    LOG(WARNING) << "Proxy for " << service_name_ << ": Unable to get logged-in user name: "
        << s.ToString() << " before connecting to remote: " << remote.ToString();
  }

  conn_id_.set_remote(remote);
  conn_id_.mutable_user_credentials()->set_real_user(real_user);
  is_started_.store(false, std::memory_order_release);
}

Proxy::~Proxy() {
}

void Proxy::AsyncRequest(const string& method,
                         const google::protobuf::Message& req,
                         google::protobuf::Message* response,
                         RpcController* controller,
                         const ResponseCallback& callback) const {
  CHECK(controller->call_.get() == nullptr) << "Controller should be reset";
  is_started_.store(true, std::memory_order_release);
  RemoteMethod remote_method(service_name_, method);
  OutboundCall* call = new OutboundCall(conn_id_, remote_method, response, controller, callback);
  controller->call_.reset(call);
  Status s = call->SetRequestParam(req);
  if (PREDICT_FALSE(!s.ok())) {
    // Failed to serialize request: likely the request is missing a required
    // field.
    call->SetFailed(s); // calls callback internally
    return;
  }

  // If this fails to queue, the callback will get called immediately
  // and the controller will be in an ERROR state.
  messenger_->QueueOutboundCall(controller->call_);
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

} // namespace rpc
} // namespace yb
