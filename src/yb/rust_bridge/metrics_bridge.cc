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

#include "yb/rust_bridge/metrics_bridge.h"

#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "yb/rust_bridge/rpc_bridge.h"

#include "yb/util/metrics.h"

namespace yb::rust_bridge {

namespace {

MetricUnit::Type ParseUnit(rust::Str unit) {
  const std::string name(unit);
  if (name == "microseconds") return MetricUnit::kMicroseconds;
  if (name == "operations") return MetricUnit::kOperations;
  if (name == "entries") return MetricUnit::kEntries;
  if (name == "requests") return MetricUnit::kRequests;
  if (name == "bytes") return MetricUnit::kBytes;
  if (name == "units") return MetricUnit::kUnits;
  throw std::invalid_argument("unknown metric unit: " + name);
}

// Prototypes hold `const char*` to their strings and must outlive every metric instantiated from
// them, so both live for the life of the process, like the static METRIC_DEFINE_* prototypes.
class Prototypes {
 public:
  template <class Prototype, class... Extra>
  const Prototype& Get(
      const MetricEntity& entity, rust::Str name, rust::Str label, rust::Str description,
      rust::Str unit, Extra... extra) {
    std::lock_guard lock(mutex_);
    std::string key(name);
    auto it = prototypes_.find(key);
    if (it == prototypes_.end()) {
      MetricPrototype::CtorArgs args(
          entity.prototype().name(), Persist(name), Persist(label), ParseUnit(unit),
          Persist(description), MetricLevel::kInfo);
      it = prototypes_.emplace(key, std::make_unique<Prototype>(args, extra...)).first;
    }
    auto* prototype = dynamic_cast<const Prototype*>(it->second.get());
    if (prototype == nullptr) {
      throw std::invalid_argument("metric " + key + " is already defined as another kind");
    }
    return *prototype;
  }

 private:
  static const char* Persist(rust::Str value) {
    return strdup(std::string(value).c_str());
  }

  std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<MetricPrototype>> prototypes_;
};

Prototypes& GetPrototypes() {
  static auto* prototypes = new Prototypes();
  return *prototypes;
}

// Significant digits of Rust histograms, as most latency histograms in the server use.
constexpr int kHistogramSignificantDigits = 2;

}  // namespace

RustCounter::RustCounter(scoped_refptr<Counter> counter) : counter_(std::move(counter)) {}
RustCounter::~RustCounter() = default;

void RustCounter::IncrementBy(uint64_t amount) const {
  counter_->IncrementBy(static_cast<int64_t>(amount));
}

RustGauge::RustGauge(scoped_refptr<AtomicGauge<int64_t>> gauge) : gauge_(std::move(gauge)) {}
RustGauge::~RustGauge() = default;

void RustGauge::Set(int64_t value) const {
  gauge_->set_value(value);
}

RustHistogram::RustHistogram(scoped_refptr<Histogram> histogram)
    : histogram_(std::move(histogram)) {}
RustHistogram::~RustHistogram() = default;

void RustHistogram::Record(uint64_t value) const {
  histogram_->Increment(static_cast<int64_t>(value));
}

RustMetricRegistry::RustMetricRegistry(scoped_refptr<MetricEntity> entity)
    : entity_(std::move(entity)) {}
RustMetricRegistry::~RustMetricRegistry() = default;

std::unique_ptr<RustCounter> RustMetricRegistry::AddCounter(
    rust::Str name, rust::Str label, rust::Str description, rust::Str unit) const {
  const auto& prototype =
      GetPrototypes().Get<CounterPrototype>(*entity_, name, label, description, unit);
  return std::make_unique<RustCounter>(prototype.Instantiate(entity_));
}

std::unique_ptr<RustGauge> RustMetricRegistry::AddGauge(
    rust::Str name, rust::Str label, rust::Str description, rust::Str unit) const {
  const auto& prototype =
      GetPrototypes().Get<GaugePrototype<int64_t>>(*entity_, name, label, description, unit);
  return std::make_unique<RustGauge>(prototype.Instantiate(entity_, 0));
}

std::unique_ptr<RustHistogram> RustMetricRegistry::AddHistogram(
    rust::Str name, rust::Str label, rust::Str description, rust::Str unit,
    uint64_t max_value) const {
  const auto& prototype = GetPrototypes().Get<HistogramPrototype>(
      *entity_, name, label, description, unit, max_value, kHistogramSignificantDigits);
  return std::make_unique<RustHistogram>(prototype.Instantiate(entity_));
}

std::unique_ptr<RustMetricRegistry> MakeMetricRegistry(const RpcServerHandle& server) {
  return std::make_unique<RustMetricRegistry>(server.GetMetricEntity());
}

}  // namespace yb::rust_bridge
