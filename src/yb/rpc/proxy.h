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

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <boost/asio/ip/tcp.hpp>
#include <boost/lockfree/queue.hpp>

#include "yb/gutil/atomicops.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/growable_buffer.h"
#include "yb/rpc/proxy_base.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/concurrent_pod.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/net_util.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace yb {

class MemTracker;

namespace rpc {

YB_DEFINE_ENUM(ResolveState, (kIdle)(kResolving)(kNotifying)(kFinished));

// Interface to send calls to a remote or local service.
//
// Proxy objects do not map one-to-one with TCP connections.  The underlying TCP
// connection is not established until the first call, and may be torn down and
// re-established as necessary by the messenger. Additionally, the messenger is
// likely to multiplex many Proxy objects on the same connection. Or, split the
// requests sent over a single proxy across different connections to the server.
//
// When remote endpoint is blank (i.e. HostPort()), the proxy will attempt to
// call the service locally in the messenger instead.
//
// Proxy objects are thread-safe after initialization only.
// Setters on the Proxy are not thread-safe, and calling a setter after any RPC
// request has started will cause a fatal error.
//
// After initialization, multiple threads may make calls using the same proxy object.
class Proxy {
 public:
  Proxy(ProxyContext* context,
        const HostPort& remote,
        const Protocol* protocol = nullptr,
        const MonoDelta& resolve_cache_timeout = MonoDelta());
  ~Proxy();

  Proxy(const Proxy&) = delete;
  void operator=(const Proxy&) = delete;

  // Call a remote method asynchronously.
  //
  // Typically, users will not call this directly, but rather through
  // a generated Proxy subclass.
  //
  // method: the method name to invoke on the remote server.
  //
  // req:  the request protobuf. This will be serialized immediately,
  //       so the caller may free or otherwise mutate 'req' safely.
  //
  // resp: the response protobuf. This protobuf will be mutated upon
  //       completion of the call. The RPC system does not take ownership
  //       of this storage.
  //
  // NOTE: 'req' and 'resp' should be the appropriate protocol buffer implementation
  // class corresponding to the parameter and result types of the service method
  // defined in the service's '.proto' file.
  //
  // controller: the RpcController to associate with this call. Each call
  //             must use a unique controller object. Does not take ownership.
  //
  // callback: the callback to invoke upon call completion. This callback may be invoked before
  // AsyncRequest() itself returns, or any time thereafter. It may be invoked either on the
  // caller's thread or asynchronously. RpcController::set_invoke_callback_mode could be used to
  // specify on which thread to invoke callback in case of asynchronous invocation.
  void AsyncRequest(const RemoteMethod* method,
                    std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                    const google::protobuf::Message& req,
                    google::protobuf::Message* resp,
                    RpcController* controller,
                    ResponseCallback callback);

  void AsyncRequest(const RemoteMethod* method,
                    std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                    const LightweightMessage& req,
                    LightweightMessage* resp,
                    RpcController* controller,
                    ResponseCallback callback);

  // The same as AsyncRequest(), except that the call blocks until the call
  // finishes. If the call fails, returns a non-OK result.
  Status SyncRequest(const RemoteMethod* method,
                     std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                     const google::protobuf::Message& req,
                     google::protobuf::Message* resp,
                     RpcController* controller);

  Status SyncRequest(const RemoteMethod* method,
                     std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                     const LightweightMessage& request,
                     LightweightMessage* resp,
                     RpcController* controller);

  // Is the service local?
  bool IsServiceLocal() const { return call_local_service_; }

  scoped_refptr<MetricEntity> metric_entity() const;

 private:
  void Resolve();
  void HandleResolve(const Result<IpAddress>& result);
  void ResolveDone(const Result<IpAddress>& result);
  void NotifyAllFailed(const Status& status);
  void QueueCall(RpcController* controller, const Endpoint& endpoint);
  ThreadPool *GetCallbackThreadPool(
      bool force_run_callback_on_reactor, InvokeCallbackMode invoke_callback_mode);

  // Implements logic for AsyncRequest function, but allows to force to run callback on
  // reactor thread. This is an optimisation used by SyncRequest function.
  void DoAsyncRequest(const RemoteMethod* method,
                      std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                      AnyMessageConstPtr req,
                      AnyMessagePtr resp,
                      RpcController* controller,
                      ResponseCallback callback,
                      bool force_run_callback_on_reactor);

  Status DoSyncRequest(const RemoteMethod* method,
                       std::shared_ptr<const OutboundMethodMetrics> method_metrics,
                       AnyMessageConstPtr req,
                       AnyMessagePtr resp,
                       RpcController* controller);

  static void NotifyFailed(RpcController* controller, const Status& status);

  void AsyncLocalCall(
      const RemoteMethod* method, AnyMessageConstPtr req, AnyMessagePtr resp,
      RpcController* controller, ResponseCallback callback, bool force_run_callback_on_reactor);

  void AsyncRemoteCall(
      const RemoteMethod* method, std::shared_ptr<const OutboundMethodMetrics> method_metrics,
      AnyMessageConstPtr req, AnyMessagePtr resp, RpcController* controller,
      ResponseCallback callback, bool force_run_callback_on_reactor);

  bool PrepareCall(AnyMessageConstPtr req, RpcController* controller);

  ProxyContext* context_;
  HostPort remote_;
  const Protocol* const protocol_;
  mutable std::atomic<size_t> num_calls_{0};
  std::shared_ptr<OutboundCallMetrics> outbound_call_metrics_;
  const bool call_local_service_;

  std::atomic<ResolveState> resolve_state_{ResolveState::kIdle};
  boost::lockfree::queue<RpcController*> resolve_waiters_;
  ConcurrentPod<Endpoint> resolved_ep_;

  scoped_refptr<EventStats> latency_stats_;

  // Number of outbound connections to create per each destination server address.
  int num_connections_to_server_;

  std::shared_ptr<MemTracker> mem_tracker_;
};

class ProxyCache {
 public:
  explicit ProxyCache(ProxyContext* context)
      : context_(context) {}

  ProxyPtr GetProxy(
      const HostPort& remote, const Protocol* protocol, const MonoDelta& resolve_cache_timeout);

  ProxyMetricsPtr GetMetrics(const std::string& service_name, ProxyMetricsFactory factory);

 private:
  typedef std::pair<HostPort, const Protocol*> ProxyKey;

  struct ProxyKeyHash {
    size_t operator()(const ProxyKey& key) const {
      size_t result = 0;
      boost::hash_combine(result, HostPortHash()(key.first));
      boost::hash_combine(result, key.second);
      return result;
    }
  };

  ProxyContext* context_;

  std::mutex proxy_mutex_;
  std::unordered_map<ProxyKey, ProxyPtr, ProxyKeyHash> proxies_ GUARDED_BY(proxy_mutex_);

  std::mutex metrics_mutex_;
  std::unordered_map<std::string , ProxyMetricsPtr> metrics_ GUARDED_BY(metrics_mutex_);
};

}  // namespace rpc
}  // namespace yb
