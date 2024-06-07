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

#include "yb/rpc/rpc_metrics.h"

#include "yb/util/metrics.h"

METRIC_DEFINE_gauge_int64(server, rpc_connections_alive,
                          "Number of alive RPC connections.",
                          yb::MetricUnit::kConnections,
                          "Number of alive RPC connections.");

METRIC_DEFINE_counter(server, rpc_connections_created,
                      "Number of created RPC connections.",
                      yb::MetricUnit::kConnections,
                      "Number of created RPC connections.");

METRIC_DEFINE_gauge_int64(server, rpc_inbound_calls_alive,
                          "Number of alive RPC inbound calls.",
                          yb::MetricUnit::kRequests,
                          "Number of alive RPC inbound calls.");

METRIC_DEFINE_counter(server, rpc_inbound_calls_created,
                      "Number of created RPC inbound calls.",
                      yb::MetricUnit::kRequests,
                      "Number of created RPC inbound calls.");

METRIC_DEFINE_gauge_int64(server, rpc_outbound_calls_alive,
                          "Number of alive RPC outbound calls.",
                          yb::MetricUnit::kRequests,
                          "Number of alive RPC outbound calls.");

METRIC_DEFINE_counter(server, rpc_outbound_calls_created,
                      "Number of created RPC outbound calls.",
                      yb::MetricUnit::kRequests,
                      "Number of created RPC outbound calls.");

METRIC_DEFINE_counter(server, rpc_outbound_calls_stuck,
                      "Number of stuck outbound RPC calls.",
                      yb::MetricUnit::kRequests,
                      "Number of events where we detected an unreasonably long running outbound "
                      "RPC call.");


METRIC_DEFINE_gauge_int64(server, rpc_busy_reactors, "The number of busy reactors.",
                          yb::MetricUnit::kUnits,
                          "The number of reactors doing some work at this time.");

namespace yb {
namespace rpc {

RpcMetrics::RpcMetrics(const scoped_refptr<MetricEntity>& metric_entity) {
  if (metric_entity) {
    connections_alive = METRIC_rpc_connections_alive.Instantiate(metric_entity, 0);
    connections_created = METRIC_rpc_connections_created.Instantiate(metric_entity);
    inbound_calls_alive = METRIC_rpc_inbound_calls_alive.Instantiate(metric_entity, 0);
    inbound_calls_created = METRIC_rpc_inbound_calls_created.Instantiate(metric_entity);
    outbound_calls_alive = METRIC_rpc_outbound_calls_alive.Instantiate(metric_entity, 0);
    outbound_calls_created = METRIC_rpc_outbound_calls_created.Instantiate(metric_entity);
    outbound_calls_stuck = METRIC_rpc_outbound_calls_stuck.Instantiate(metric_entity);
    busy_reactors = METRIC_rpc_busy_reactors.Instantiate(metric_entity, 0);
  }
}

} // namespace rpc
} // namespace yb
