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

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

#include <glog/logging.h>

#include "yb/rpc/local_call.h"
#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/proxy_context.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_header.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

DEFINE_int32(num_connections_to_server, 8,
             "Number of underlying connections to each server");

DEFINE_int32(proxy_resolve_cache_ms, 5000,
             "Time in milliseconds to cache resolution result in Proxy");

using namespace std::literals;

using google::protobuf::Message;
using std::string;
using std::shared_ptr;

namespace yb {
namespace rpc {

Proxy::Proxy(ProxyContext* context,
             const HostPort& remote,
             const Protocol* protocol,
             const MonoDelta& resolve_cache_timeout)
    : context_(context),
      remote_(remote),
      protocol_(protocol ? protocol : context_->DefaultProtocol()),
      outbound_call_metrics_(context_->metric_entity() ?
          std::make_shared<OutboundCallMetrics>(context_->metric_entity()) : nullptr),
      call_local_service_(remote == HostPort()),
      resolve_waiters_(30),
      resolved_ep_(std::chrono::milliseconds(
          resolve_cache_timeout.Initialized() ? resolve_cache_timeout.ToMilliseconds()
                                              : FLAGS_proxy_resolve_cache_ms)),
      latency_hist_(ScopedDnsTracker::active_metric()),
      // Use the context->num_connections_to_server() here as opposed to directly reading the
      // FLAGS_num_connections_to_server, because the flag value could have changed since then.
      num_connections_to_server_(context_->num_connections_to_server()) {
  VLOG(1) << "Create proxy to " << remote << " with num_connections_to_server="
          << num_connections_to_server_;
  if (context_->parent_mem_tracker()) {
    mem_tracker_ = MemTracker::FindOrCreateTracker(
        "Queueing", context_->parent_mem_tracker());
  }
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
                         std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                         const google::protobuf::Message& req,
                         google::protobuf::Message* resp,
                         RpcController* controller,
                         ResponseCallback callback) {
  DoAsyncRequest(
      method, std::move(method_metrics), AnyMessageConstPtr(&req), AnyMessagePtr(resp), controller,
      std::move(callback), false /* force_run_callback_on_reactor */);
}

void Proxy::AsyncRequest(const RemoteMethod* method,
                         std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                         const LightweightMessage& req,
                         LightweightMessage* resp,
                         RpcController* controller,
                         ResponseCallback callback) {
  DoAsyncRequest(
      method, std::move(method_metrics), AnyMessageConstPtr(&req), AnyMessagePtr(resp), controller,
      std::move(callback), false /* force_run_callback_on_reactor */);
}

ThreadPool* Proxy::GetCallbackThreadPool(
    bool force_run_callback_on_reactor, InvokeCallbackMode invoke_callback_mode) {
  if (force_run_callback_on_reactor) {
    return nullptr;
  }
  switch (invoke_callback_mode) {
    case InvokeCallbackMode::kReactorThread:
      return nullptr;
      break;
    case InvokeCallbackMode::kThreadPoolNormal:
      return &context_->CallbackThreadPool(ServicePriority::kNormal);
    case InvokeCallbackMode::kThreadPoolHigh:
      return &context_->CallbackThreadPool(ServicePriority::kHigh);
  }
  FATAL_INVALID_ENUM_VALUE(InvokeCallbackMode, invoke_callback_mode);
}

bool Proxy::PrepareCall(AnyMessageConstPtr req, RpcController* controller) {
  auto call = controller->call_.get();
  Status s = call->SetRequestParam(req, mem_tracker_);
  if (PREDICT_FALSE(!s.ok())) {
    // Failed to serialize request: likely the request is missing a required
    // field.
    NotifyFailed(controller, s);
    return false;
  }

  // Sanity check to make sure timeout is setup and has some sensible value (to prevent infinity).
  if (controller->timeout().Initialized() && controller->timeout() > 7200s) {
    LOG(DFATAL) << "Too big timeout specified: " << controller->timeout();
  }

  // Propagate the test only flag to OutboundCall.
  if (controller->TEST_disable_outbound_call_response_processing) {
    call->TEST_ignore_response();
  }

  return true;
}

void Proxy::AsyncLocalCall(
    const RemoteMethod* method, AnyMessageConstPtr req, AnyMessagePtr resp,
    RpcController* controller, ResponseCallback callback,
    const bool force_run_callback_on_reactor) {
  controller->call_ = std::make_shared<LocalOutboundCall>(
      *method, outbound_call_metrics_, resp, controller, context_->rpc_metrics(),
      std::move(callback),
      GetCallbackThreadPool(force_run_callback_on_reactor, controller->invoke_callback_mode()));
  if (!PrepareCall(req, controller)) {
    return;
  }
  // For local call, the response message buffer is reused when an RPC call is retried. So clear
  // the buffer before calling the RPC method.
  if (resp.is_lightweight()) {
    resp.lightweight()->Clear();
  } else {
    resp.protobuf()->Clear();
  }
  auto call = controller->call_.get();
  call->SetQueued();
  auto ignored [[maybe_unused]] = call->SetSent();  // NOLINT
  // If currrent thread is RPC worker thread, it is ok to call the handler in the current thread.
  // Otherwise, enqueue the call to be handled by the service's handler thread.
  const shared_ptr<LocalYBInboundCall>& local_call =
      static_cast<LocalOutboundCall*>(call)->CreateLocalInboundCall();
  Queue queue(!controller->allow_local_calls_in_curr_thread() ||
              !ThreadPool::IsCurrentThreadRpcWorker());
  context_->Handle(local_call, queue);
}

void Proxy::AsyncRemoteCall(
    const RemoteMethod* method, std::shared_ptr<const OutboundMethodMetrics> method_metrics,
    AnyMessageConstPtr req, AnyMessagePtr resp, RpcController* controller,
    ResponseCallback callback, const bool force_run_callback_on_reactor) {
  // Do not use make_shared to allow for long-lived weak OutboundCall pointers without wasting
  // memory.
  controller->call_ = std::shared_ptr<OutboundCall>(new OutboundCall(
      *method, outbound_call_metrics_, std::move(method_metrics), resp, controller,
      context_->rpc_metrics(), std::move(callback),
      GetCallbackThreadPool(force_run_callback_on_reactor, controller->invoke_callback_mode())));
  if (!PrepareCall(req, controller)) {
    return;
  }
  auto ep = resolved_ep_.Load();
  if (ep.address().is_unspecified()) {
    CHECK(resolve_waiters_.push(controller));
    Resolve();
  } else {
    QueueCall(controller, ep);
  }
}

void Proxy::DoAsyncRequest(const RemoteMethod* method,
                           std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                           AnyMessageConstPtr req,
                           AnyMessagePtr resp,
                           RpcController* controller,
                           ResponseCallback callback,
                           const bool force_run_callback_on_reactor) {
  CHECK(controller->call_.get() == nullptr) << "Controller should be reset";

  if (call_local_service_) {
    AsyncLocalCall(
        method, req, resp, controller, std::move(callback), force_run_callback_on_reactor);
  } else {
    AsyncRemoteCall(
        method, std::move(method_metrics), req, resp, controller, std::move(callback),
        force_run_callback_on_reactor);
  }
}

void Proxy::Resolve() {
  auto expected = ResolveState::kIdle;
  if (!resolve_state_.compare_exchange_strong(
      expected, ResolveState::kResolving, std::memory_order_acq_rel)) {
    return;
  }

  auto endpoint = resolved_ep_.Load();
  if (!endpoint.address().is_unspecified()) {
    expected = ResolveState::kResolving;
    while (resolve_state_.compare_exchange_strong(
        expected, ResolveState::kNotifying, std::memory_order_acq_rel)) {
      RpcController* controller = nullptr;
      while (resolve_waiters_.pop(controller)) {
        QueueCall(controller, endpoint);
      }
      resolve_state_.store(ResolveState::kIdle, std::memory_order_release);
      if (resolve_waiters_.empty()) {
        break;
      }
      expected = ResolveState::kIdle;
    }
    return;
  }

  const std::string kService = "";

  auto address = TryFastResolve(remote_.host());
  if (address) {
    HandleResolve(*address);
    return;
  }

  auto latency_metric = std::make_shared<ScopedLatencyMetric>(latency_hist_, Auto::kFalse);

  context_->resolver().AsyncResolve(
      remote_.host(), [this, latency_metric = std::move(latency_metric)](
          const Result<IpAddress>& result) {
    latency_metric->Finish();
    HandleResolve(result);
  });
}

void Proxy::NotifyAllFailed(const Status& status) {
  RpcController* controller = nullptr;
  while (resolve_waiters_.pop(controller)) {
    NotifyFailed(controller, status);
  }
}

void Proxy::HandleResolve(const Result<IpAddress>& result) {
  auto expected = ResolveState::kResolving;
  if (resolve_state_.compare_exchange_strong(
      expected, ResolveState::kNotifying, std::memory_order_acq_rel)) {
    ResolveDone(result);
    resolve_state_.store(ResolveState::kIdle, std::memory_order_release);
    if (!resolve_waiters_.empty()) {
      Resolve();
    }
  }
}

void Proxy::ResolveDone(const Result<IpAddress>& result) {
  if (!result.ok()) {
    LOG(WARNING) << "Resolve " << remote_.host() << " failed: " << result.status();
    NotifyAllFailed(result.status());
    return;
  }

  Endpoint endpoint(*result, remote_.port());
  resolved_ep_.Store(endpoint);

  RpcController* controller = nullptr;
  while (resolve_waiters_.pop(controller)) {
    QueueCall(controller, endpoint);
  }
}

void Proxy::QueueCall(RpcController* controller, const Endpoint& endpoint) {
  uint8_t idx = num_calls_.fetch_add(1) % num_connections_to_server_;
  ConnectionId conn_id(endpoint, idx, protocol_);
  controller->call_->SetConnectionId(conn_id, &remote_.host());
  context_->QueueOutboundCall(controller->call_);
}

void Proxy::NotifyFailed(RpcController* controller, const Status& status) {
  // We should retain reference to call, so it would not be destroyed during SetFailed.
  auto call = controller->call_;
  call->SetFailed(status);
}

Status Proxy::DoSyncRequest(const RemoteMethod* method,
                            std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                            AnyMessageConstPtr request,
                            AnyMessagePtr resp,
                            RpcController* controller) {
  CountDownLatch latch(1);
  // We want to execute this fast callback in reactor thread to avoid overhead on putting in
  // separate pool.
  DoAsyncRequest(
      method, std::move(method_metrics), request, DCHECK_NOTNULL(resp), controller,
      latch.CountDownCallback(), true /* force_run_callback_on_reactor */);
  latch.Wait();
  return controller->status();
}

Status Proxy::SyncRequest(const RemoteMethod* method,
                          std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                          const google::protobuf::Message& req,
                          google::protobuf::Message* resp,
                          RpcController* controller) {
  return DoSyncRequest(
      method, std::move(method_metrics), AnyMessageConstPtr(&req), AnyMessagePtr(resp), controller);
}

Status Proxy::SyncRequest(const RemoteMethod* method,
                          std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                          const LightweightMessage& req,
                          LightweightMessage* resp,
                          RpcController* controller) {
  return DoSyncRequest(
      method, std::move(method_metrics), AnyMessageConstPtr(&req), AnyMessagePtr(resp), controller);
}

scoped_refptr<MetricEntity> Proxy::metric_entity() const {
  return context_->metric_entity();
}

ProxyPtr ProxyCache::GetProxy(
    const HostPort& remote, const Protocol* protocol, const MonoDelta& resolve_cache_timeout) {
  ProxyKey key(remote, protocol);
  std::lock_guard<std::mutex> lock(proxy_mutex_);
  auto it = proxies_.find(key);
  if (it == proxies_.end()) {
    it = proxies_.emplace(
        key, std::make_unique<Proxy>(context_, remote, protocol, resolve_cache_timeout)).first;
  }
  return it->second;
}

ProxyMetricsPtr ProxyCache::GetMetrics(
    const std::string& service_name, ProxyMetricsFactory factory) {
  std::lock_guard<std::mutex> lock(metrics_mutex_);
  auto it = metrics_.find(service_name);
  if (it != metrics_.end()) {
    return it->second;
  }

  auto entity = context_->metric_entity();
  auto metrics = entity ? factory(entity) : nullptr;
  metrics_.emplace(service_name, metrics);
  return metrics;
}

ProxyBase::ProxyBase(const std::string& service_name, ProxyMetricsFactory metrics_factory,
                     ProxyCache* cache, const HostPort& remote,
                     const Protocol* protocol,
                     const MonoDelta& resolve_cache_timeout)
    : proxy_(cache->GetProxy(remote, protocol, resolve_cache_timeout)),
      metrics_(cache->GetMetrics(service_name, metrics_factory)) {
}

}  // namespace rpc
}  // namespace yb
