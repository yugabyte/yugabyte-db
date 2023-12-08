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

#include "yb/yql/pgwrapper/pg_test_utils.h"

#include "yb/tserver/tablet_server.h"

#include "yb/util/metrics.h"

namespace yb::pgwrapper {
namespace {

Result<size_t> GetMetric(const MetricWatcherDescriptor& desc) {
  auto item = desc.map.find(&desc.proto);
  RSTATUS_DCHECK(item != desc.map.end(), IllegalState, "Metric not found");
  const auto& metric = *item->second;
  switch(metric.prototype()->type()) {
    case MetricType::kHistogram: return down_cast<const Histogram&>(metric).TotalCount();
    case MetricType::kEventStats:
        return down_cast<const EventStats&>(metric).TotalCount();
    case MetricType::kCounter: return down_cast<const Counter&>(metric).value();

    case MetricType::kGauge: break;
    case MetricType::kLag: break;
  }
  RSTATUS_DCHECK(
      false, IllegalState, Format("Unsupported metric type $0", metric.prototype()->type()));
  return 0;
}

} // namespace

Status UpdateDelta(
  const MetricWatcherDescriptor* descrs, size_t count, const MetricWatcherFunctor& func) {
  const auto* end = descrs + count;
  for (const auto* d = descrs; d != end; ++d) {
    d->delta_receiver = VERIFY_RESULT(GetMetric(*d));
  }
  RETURN_NOT_OK(func());
  for (const auto* d = descrs; d != end; ++d) {
    d->delta_receiver = VERIFY_RESULT(GetMetric(*d)) - d->delta_receiver;
  }
  return Status::OK();
}

const MetricEntity::MetricMap& GetMetricMap(
    std::reference_wrapper<const server::RpcServerBase> server) {
  return server.get().metric_entity()->UnsafeMetricsMapForTests();
}

} // namespace yb::pgwrapper
