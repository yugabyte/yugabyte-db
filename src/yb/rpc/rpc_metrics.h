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

#ifndef YB_RPC_RPC_METRICS_H
#define YB_RPC_RPC_METRICS_H

#include "yb/util/metrics_fwd.h"

namespace yb {
namespace rpc {

struct RpcMetrics {
  explicit RpcMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  scoped_refptr<AtomicGauge<int64_t>> connections_alive;
  scoped_refptr<Counter> connections_created;
  scoped_refptr<AtomicGauge<int64_t>> inbound_calls_alive;
  scoped_refptr<Counter> inbound_calls_created;
  scoped_refptr<AtomicGauge<int64_t>> outbound_calls_alive;
  scoped_refptr<Counter> outbound_calls_created;
  scoped_refptr<Counter> outbound_calls_stuck;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_METRICS_H
