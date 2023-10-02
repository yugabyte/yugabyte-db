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

#include <functional>
#include <map>
#include <unordered_map>

#include "yb/gutil/callback_forward.h"
#include "yb/gutil/map-util.h"

#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {

class JsonWriter;

// Severity level used with metrics.
// Levels:
//   - Debug: Metrics that are diagnostically helpful but generally not monitored
//            during normal operation.
//   - Info: Generally useful metrics that operators always want to have available
//           but may not be monitored under normal circumstances.
//   - Warn: Metrics which can often indicate operational oddities, which may need
//           more investigation.
//
// The levels are ordered and lower levels include the levels above them:
//    Debug < Info < Warn
enum class MetricLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2
};

enum class AggregationMetricLevel {
  kServer,
  kTable,
  kStream
};

struct MetricOptions {
  // Determine whether system reset histogram or not
  // Default: false
  bool reset_histograms = true;

  // Include the metrics at a level and above.
  // Default: debug
  MetricLevel level = MetricLevel::kDebug;
};

struct MetricJsonOptions : public MetricOptions {
  // Include the raw histogram values and counts in the JSON output.
  // This allows consumers to do cross-server aggregation or window
  // data over time.
  // Default: false
  bool include_raw_histograms = false;

  // Include the metrics "schema" information (i.e description, label,
  // unit, etc).
  // Default: false
  bool include_schema_info = false;
};

YB_STRONGLY_TYPED_BOOL(ExportHelpAndType);

struct MetricPrometheusOptions : public MetricOptions {
  // Number of tables to include metrics for.
  uint32_t max_tables_metrics_breakdowns;

  // Include #TYPE and #HELP in Prometheus metrics output
  ExportHelpAndType export_help_and_type{ExportHelpAndType::kFalse};
};

struct MetricEntityOptions {
  std::vector<std::string> metrics;
  std::vector<std::string> exclude_metrics;

  // Regex for metrics that should always be included for all tables.
  std::string priority_regex;
};

using MeticEntitiesOptions = std::map<AggregationMetricLevel, MetricEntityOptions>;

class MetricEntityPrototype {
 public:
  explicit MetricEntityPrototype(const char* name);
  ~MetricEntityPrototype();

  const char* name() const { return name_; }

  // Find or create an entity with the given ID within the provided 'registry'.
  scoped_refptr<MetricEntity> Instantiate(MetricRegistry* registry, const std::string& id) const;

  // If the entity already exists, then 'initial_attrs' will replace all existing
  // attributes.
  scoped_refptr<MetricEntity> Instantiate(
      MetricRegistry* registry,
      const std::string& id,
      const std::unordered_map<std::string, std::string>& initial_attrs,
      std::shared_ptr<MemTracker> mem_tracker = nullptr) const;

 private:
  const char* const name_;

  DISALLOW_COPY_AND_ASSIGN(MetricEntityPrototype);
};

enum AggregationFunction {
  kSum,
  kMax
};

class MetricEntity : public RefCountedThreadSafe<MetricEntity> {
 public:
  typedef std::unordered_map<const MetricPrototype*, scoped_refptr<Metric> > MetricMap;
  typedef std::unordered_map<std::string, std::string> AttributeMap;
  typedef std::function<void (JsonWriter* writer, const MetricJsonOptions& opts)>
    ExternalJsonMetricsCb;
  typedef std::function<void (PrometheusWriter* writer, const MetricPrometheusOptions& opts)>
    ExternalPrometheusMetricsCb;

  template<typename Metric, typename PrototypePtr, typename ...Args>
  scoped_refptr<Metric> FindOrCreateMetric(PrototypePtr proto, Args&&... args);

  // Return the metric instantiated from the given prototype, or NULL if none has been
  // instantiated. Primarily used by tests trying to read metric values.
  scoped_refptr<Metric> FindOrNull(const MetricPrototype& prototype) const;

  const std::string& id() const { return id_; }

  // See MetricRegistry::WriteAsJson()
  Status WriteAsJson(JsonWriter* writer,
                     const MetricEntityOptions& entity_options,
                     const MetricJsonOptions& opts) const;

  Status WriteForPrometheus(PrometheusWriter* writer,
                            const MetricEntityOptions& entity_options,
                            const MetricPrometheusOptions& opts) const;

  const MetricMap& UnsafeMetricsMapForTests() const { return metric_map_; }

  // Mark that the given metric should never be retired until the metric
  // registry itself destructs. This is useful for system metrics such as
  // tcmalloc, etc, which should live as long as the process itself.
  void NeverRetire(const scoped_refptr<Metric>& metric);

  // Scan the metrics map for metrics needing retirement, removing them as necessary.
  //
  // Metrics are retired when they are no longer referenced outside of the metrics system
  // itself. Additionally, we only retire a metric that has been in this state for
  // at least FLAGS_metrics_retirement_age_ms milliseconds.
  void RetireOldMetrics();

  // Replaces all attributes for this entity.
  // Any attributes currently set, but not in 'attrs', are removed.
  void SetAttributes(const AttributeMap& attrs);

  // Set a particular attribute. Replaces any current value.
  void SetAttribute(const std::string& key, const std::string& val);

  size_t num_metrics() const {
    std::lock_guard l(lock_);
    return metric_map_.size();
  }

  void AddExternalJsonMetricsCb(const ExternalJsonMetricsCb &external_metrics_cb) {
    std::lock_guard l(lock_);
    external_json_metrics_cbs_.push_back(external_metrics_cb);
  }

  void AddExternalPrometheusMetricsCb(const ExternalPrometheusMetricsCb&external_metrics_cb) {
    std::lock_guard l(lock_);
    external_prometheus_metrics_cbs_.push_back(external_metrics_cb);
  }

  const MetricEntityPrototype& prototype() const { return *prototype_; }

  void Remove(const MetricPrototype* proto);

 private:
  friend class MetricRegistry;
  friend class RefCountedThreadSafe<MetricEntity>;

  MetricEntity(const MetricEntityPrototype* prototype, std::string id, AttributeMap attributes,
               std::shared_ptr<MemTracker> mem_tracker = nullptr);
  ~MetricEntity();

  // Ensure that the given metric prototype is allowed to be instantiated
  // within this entity. This entity's type must match the expected entity
  // type defined within the metric prototype.
  void CheckInstantiation(const MetricPrototype* proto) const;

  template<typename Pointer>
  void AddConsumption(const Pointer& value) REQUIRES(lock_);

  const MetricEntityPrototype* const prototype_;
  const std::string id_;

  mutable simple_spinlock lock_;

  // Map from metric name to Metric object. Protected by lock_.
  MetricMap metric_map_;

  // The key/value attributes. Protected by lock_
  AttributeMap attributes_;

  std::weak_ptr<MemTracker> mem_tracker_ GUARDED_BY(lock_);

  // The set of metrics which should never be retired. Protected by lock_.
  std::vector<scoped_refptr<Metric> > never_retire_metrics_;

  // Callbacks fired each time WriteAsJson is called.
  std::vector<ExternalJsonMetricsCb> external_json_metrics_cbs_;

  // Callbacks fired each time WriteForPrometheus is called.
  std::vector<ExternalPrometheusMetricsCb> external_prometheus_metrics_cbs_;
};

template<typename T>
void MetricEntity::AddConsumption(const T& value) {
  if (auto mem_tracker = mem_tracker_.lock()) {
    mem_tracker->Consume(DynamicMemoryUsageOrSizeOf(value));
  }
}

template<typename Metric, typename PrototypePtr, typename ...Args>
scoped_refptr<Metric> MetricEntity::FindOrCreateMetric(PrototypePtr proto, Args&&... args) {
  CheckInstantiation(std::to_address(proto));
  std::lock_guard l(lock_);
  auto m = down_cast<Metric*>(FindPtrOrNull(metric_map_, std::to_address(proto)).get());
  if (!m) {
    m = new Metric(std::move(proto), std::forward<Args>(args)...);
    InsertOrDie(&metric_map_, m->prototype(), m);
    AddConsumption(*m);
  }
  return m;
}

void WriteRegistryAsJson(JsonWriter* writer);

} // namespace yb
