// Copyright (c) YugabyteDB, Inc.
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

// Metrics for Rust (common/yb-cpp-host/src/ffi/metrics.rs), registered in the host's metric
// registry so /metrics and /prometheus-metrics export them with the C++ metrics of the same
// entity.

#pragma once

#include <cstdint>
#include <memory>

#include "rust/cxx.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/metrics_fwd.h"

namespace yb::rust_bridge {

class RpcServerHandle;

class RustCounter {
 public:
  explicit RustCounter(scoped_refptr<Counter> counter);
  ~RustCounter();
  void IncrementBy(uint64_t amount) const;

 private:
  scoped_refptr<Counter> counter_;
};

class RustGauge {
 public:
  explicit RustGauge(scoped_refptr<AtomicGauge<int64_t>> gauge);
  ~RustGauge();
  void Set(int64_t value) const;

 private:
  scoped_refptr<AtomicGauge<int64_t>> gauge_;
};

class RustHistogram {
 public:
  explicit RustHistogram(scoped_refptr<Histogram> histogram);
  ~RustHistogram();
  void Record(uint64_t value) const;

 private:
  scoped_refptr<Histogram> histogram_;
};

// Creates metrics in one metric entity. Prototypes are created on first use of a name and kept
// for the life of the process, as statically defined ones are; a name is created once per entity.
class RustMetricRegistry {
 public:
  explicit RustMetricRegistry(scoped_refptr<MetricEntity> entity);
  ~RustMetricRegistry();

  // `unit` is a name from yb_host_api::metrics::MetricUnit::as_str. Throws on an unknown unit, or
  // when `name` is already defined as another kind of metric.
  std::unique_ptr<RustCounter> AddCounter(
      rust::Str name, rust::Str label, rust::Str description, rust::Str unit) const;
  std::unique_ptr<RustGauge> AddGauge(
      rust::Str name, rust::Str label, rust::Str description, rust::Str unit) const;
  std::unique_ptr<RustHistogram> AddHistogram(
      rust::Str name, rust::Str label, rust::Str description, rust::Str unit,
      uint64_t max_value) const;

 private:
  scoped_refptr<MetricEntity> entity_;
};

// The registry of the host's server metric entity.
std::unique_ptr<RustMetricRegistry> MakeMetricRegistry(const RpcServerHandle& server);

}  // namespace yb::rust_bridge
