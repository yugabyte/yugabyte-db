//
// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

class MemTracker;

namespace rpc {

class ProxyContext {
 public:
  virtual scoped_refptr<MetricEntity> metric_entity() const = 0;

  // Queue a call for transmission. This will pick the appropriate reactor, and enqueue a task on
  // that reactor to assign and send the call.
  virtual void QueueOutboundCall(OutboundCallPtr call) = 0;

  virtual void Handle(InboundCallPtr call, Queue queue) = 0;

  virtual const Protocol* DefaultProtocol() = 0;

  virtual ThreadPool& CallbackThreadPool(ServicePriority priority = ServicePriority::kNormal) = 0;

  virtual IoService& io_service() = 0;

  virtual DnsResolver& resolver() = 0;

  virtual const std::shared_ptr<RpcMetrics>& rpc_metrics() = 0;

  virtual const std::shared_ptr<MemTracker>& parent_mem_tracker() = 0;

  // Number of connections to create per destination address.
  virtual int num_connections_to_server() const = 0;

  virtual ~ProxyContext() {}
};

} // namespace rpc
} // namespace yb
