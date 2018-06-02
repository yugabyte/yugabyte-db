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

#include <cinttypes>
#include <cstdint>
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

#include "yb/util/backoff_waiter.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/status.h"
#include "yb/util/user.h"

DEFINE_int32(num_connections_to_server, 8, "Number of underlying connections to each server");
DEFINE_int32(proxy_resolve_cache_ms, 5000,
             "Time in milliseconds to cache resolution result in Proxy");

using namespace std::literals;

using google::protobuf::Message;
using std::string;
using std::shared_ptr;

namespace yb {
namespace rpc {

Proxy::Proxy(std::shared_ptr<ProxyContext> context, const HostPort& remote)
    : context_(std::move(context)),
      remote_(remote),
      outbound_call_metrics_(context_->metric_entity() ?
          std::make_shared<OutboundCallMetrics>(context_->metric_entity()) : nullptr),
      call_local_service_(remote == HostPort()),
      resolve_waiters_(30),
      resolved_ep_(std::chrono::milliseconds(FLAGS_proxy_resolve_cache_ms)),
      latency_hist_(ScopedDnsTracker::active_metric()) {
  VLOG(1) << "Create proxy to " << remote;
}

Proxy::~Proxy() {
  const auto kTimeout = 5s;
  const auto kMaxWaitTime = 100ms;
  BackoffWaiter waiter(std::chrono::steady_clock::now() + kTimeout, kMaxWaitTime);
  for (;;) {
    auto expected = ResolveState::kIdle;
    if (resolve_state_.compare_exchange_weak(
        expected, ResolveState::kFinished, std::memory_order_acq_rel)) {
      break;
    }
    if (!waiter.Wait()) {
      LOG(DFATAL) << "Timeout to wait resolve to complete";
      break;
    }
  }
}

void Proxy::AsyncRequest(const RemoteMethod* method,
                         const google::protobuf::Message& req,
                         google::protobuf::Message* resp,
                         RpcController* controller,
                         ResponseCallback callback) {
  CHECK(controller->call_.get() == nullptr) << "Controller should be reset";
  is_started_.store(true, std::memory_order_release);

  controller->call_ =
      call_local_service_ ?
      std::make_shared<LocalOutboundCall>(method,
                                          outbound_call_metrics_,
                                          resp,
                                          controller,
                                          std::move(callback)) :
      std::make_shared<OutboundCall>(method,
                                     outbound_call_metrics_,
                                     resp,
                                     controller,
                                     std::move(callback));
  auto call = controller->call_.get();
  Status s = call->SetRequestParam(req);
  if (PREDICT_FALSE(!s.ok())) {
    // Failed to serialize request: likely the request is missing a required
    // field.
    NotifyFailed(controller, s);
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
    if (controller->allow_local_calls_in_curr_thread() && ThreadPool::IsCurrentThreadRpcWorker()) {
      context_->Handle(local_call);
    } else {
      context_->QueueInboundCall(local_call);
    }
  } else {
    auto ep = resolved_ep_.Load();
    if (ep.address().is_unspecified()) {
      CHECK(resolve_waiters_.push(controller));
      Resolve();
    } else {
      QueueCall(controller, ep);
    }
  }
}

void Proxy::Resolve() {
  auto expected = ResolveState::kIdle;
  if (!resolve_state_.compare_exchange_strong(
      expected, ResolveState::kResolving, std::memory_order_acq_rel)) {
    return;
  }

  const std::string kService = "";

  boost::system::error_code ec;
  auto address = IpAddress::from_string(remote_.host(), ec);
  if (!ec) {
    Endpoint ep(address, remote_.port());
    HandleResolve(ec, Resolver::results_type::create(ep, remote_.host(), kService));
    return;
  }

  auto resolver = std::make_shared<Resolver>(context_->io_service());
  ScopedLatencyMetric latency_metric(latency_hist_, Auto::kFalse);

  resolver->async_resolve(
      Resolver::query(remote_.host(), kService),
      [this, resolver, latency_metric = std::move(latency_metric)](
          const boost::system::error_code& error,
          const Resolver::results_type& entries) mutable {
    latency_metric.Finish();
    HandleResolve(error, entries);
  });

  if (context_->io_service().stopped()) {
    auto expected = ResolveState::kResolving;
    if (resolve_state_.compare_exchange_strong(
        expected, ResolveState::kIdle, std::memory_order_acq_rel)) {
      NotifyAllFailed(STATUS(Aborted, "Messenger already stopped"));
    }
  }
}

void Proxy::NotifyAllFailed(const Status& status) {
  RpcController* controller = nullptr;
  while (resolve_waiters_.pop(controller)) {
    NotifyFailed(controller, status);
  }
}

void Proxy::HandleResolve(
    const boost::system::error_code& error, const Resolver::results_type& entries) {
  auto expected = ResolveState::kResolving;
  if (resolve_state_.compare_exchange_strong(
      expected, ResolveState::kNotifying, std::memory_order_acq_rel)) {
    ResolveDone(error, entries);
    resolve_state_.store(ResolveState::kIdle, std::memory_order_release);
    if (!resolve_waiters_.empty()) {
      Resolve();
    }
  }
}

void Proxy::ResolveDone(
    const boost::system::error_code& error, const Resolver::results_type& entries) {
  if (error) {
    NotifyAllFailed(STATUS_FORMAT(
        NetworkError, "Resolve failed $0: $1", remote_.host(), error.message()));
    return;
  }
  std::vector<Endpoint> endpoints, endpoints_v6;
  for (const auto& entry : entries) {
    auto& dest = entry.endpoint().address().is_v4() ? endpoints : endpoints_v6;
    dest.emplace_back(entry.endpoint().address(), remote_.port());
  }
  endpoints.insert(endpoints.end(), endpoints_v6.begin(), endpoints_v6.end());
  if (endpoints.empty()) {
    NotifyAllFailed(STATUS_FORMAT(NetworkError, "No endpoints resolved for: $0", remote_.host()));
    return;
  }
  if (endpoints.size() > 1) {
    LOG(WARNING) << "Peer address '" << remote_.ToString() << "' "
                 << "resolves to " << yb::ToString(endpoints) << " different addresses. Using "
                 << endpoints.front();
  }

  RpcController* controller = nullptr;
  resolved_ep_.Store(endpoints.front());
  while (resolve_waiters_.pop(controller)) {
    QueueCall(controller, endpoints.front());
  }
}

void Proxy::QueueCall(RpcController* controller, const Endpoint& endpoint) {
  uint8_t idx = num_calls_.fetch_add(1) % FLAGS_num_connections_to_server;
  ConnectionId conn_id(endpoint, idx);
  controller->call_->SetConnectionId(conn_id);
  context_->QueueOutboundCall(controller->call_);
}

void Proxy::NotifyFailed(RpcController* controller, const Status& status) {
  // We should retain reference to call, so it would not be destroyed during SetFailed.
  auto call = controller->call_;
  call->SetFailed(status);
}

Status Proxy::SyncRequest(const RemoteMethod* method,
                          const google::protobuf::Message& req,
                          google::protobuf::Message* resp,
                          RpcController* controller) {
  CountDownLatch latch(1);
  AsyncRequest(method, req, DCHECK_NOTNULL(resp), controller, [&latch]() { latch.CountDown(); });

  latch.Wait();
  return controller->status();
}

std::shared_ptr<Proxy> ProxyCache::Get(const HostPort& remote) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = proxies_.find(remote);
  if (it == proxies_.end()) {
    it = proxies_.emplace(remote, std::make_unique<Proxy>(context_, remote)).first;
  }
  return it->second;
}

}  // namespace rpc
}  // namespace yb
