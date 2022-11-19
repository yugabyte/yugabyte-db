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

#pragma once

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"

namespace yb {
namespace rpc {

struct OutboundMethodMetrics {
  scoped_refptr<Counter> request_bytes;
  scoped_refptr<Counter> response_bytes;
};

struct ProxyMetrics {};
using ProxyMetricsPtr = std::shared_ptr<ProxyMetrics>;

using ProxyMetricsFactory = ProxyMetricsPtr(*)(const scoped_refptr<MetricEntity>& entity);

template <size_t size>
struct ProxyMetricsImpl : public ProxyMetrics {
  std::array<OutboundMethodMetrics, size> value;
};

class ProxyBase {
 public:
  ProxyBase(const std::string& service_name, ProxyMetricsFactory metrics_factory,
            ProxyCache* cache, const HostPort& remote,
            const Protocol* protocol = nullptr,
            const MonoDelta& resolve_cache_timeout = MonoDelta());

  template <size_t size>
  std::shared_ptr<const OutboundMethodMetrics> metrics(size_t index) const {
    if (!metrics_) {
      return nullptr;
    }
    auto* metrics_impl = static_cast<ProxyMetricsImpl<size>*>(metrics_.get());
    return std::shared_ptr<const OutboundMethodMetrics>(metrics_, &metrics_impl->value[index]);
  }

  Proxy& proxy() const { return *proxy_; }

 private:
  ProxyPtr proxy_;
  ProxyMetricsPtr metrics_;
};

}  // namespace rpc
}  // namespace yb
