//
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
//
//

#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "yb/gutil/callback_forward.h"
#include "yb/gutil/map-util.h"

#include "yb/util/enums.h"
#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {

static const char* const kXClusterMetricEntityName = "xcluster";
static const char* const kCdcsdkMetricEntityName = "cdcsdk";
static const char* const kVectorIndexMetricEntityName = "vector_index";

static const char* const kServerLevelAggregationId = "server_level";

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

using AggregationLevels = unsigned int;
constexpr AggregationLevels kNoLevel = 0;
constexpr AggregationLevels kServerLevel = 1 << 0;
constexpr AggregationLevels kStreamLevel = 1 << 1;
constexpr AggregationLevels kTableLevel = 1 << 2;

using MetricAggregationMap = std::unordered_map<std::string, AggregationLevels>;

struct MetricOptions {
  // Determine whether system reset histogram or not
  // Default: false
  bool reset_histograms = true;

  // Include the metrics at a level and above.
  // Default: debug
  MetricLevel level = MetricLevel::kDebug;

  // Missing vector means select all metrics.
  std::optional<std::vector<std::string>> general_metrics_allowlist;
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

static const std::string kFilterVersionOne = "v1";
static const std::string kFilterVersionTwo = "v2";

struct MetricPrometheusOptions : public MetricOptions {
  // Include #TYPE and #HELP in Prometheus metrics output
  ExportHelpAndType export_help_and_type{ExportHelpAndType::kFalse};

  uint32_t max_metric_entries = UINT32_MAX;

  std::string version = kFilterVersionOne;

  // For filtering table level metrics when version is equal to kFilterVersionOne.
  std::string priority_regex_string = ".*";

  // The four regexs are for filtering table level and server level metrics
  // when version is equal to kFilterVersionTwo.
  std::string table_allowlist_string = ".*";
  std::string table_blocklist_string = "";

  std::string server_allowlist_string = ".*";
  std::string server_blocklist_string = "";
};

struct MetricHelpAndType {
  const char* help;
  const char* type;
};

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
  using MetricMap = std::map<const MetricPrototype*, scoped_refptr<Metric>>;
  using AttributeMap = std::unordered_map<std::string, std::string>;
  using NonPreAggregatedMetrics = std::vector<scoped_refptr<Metric>>;

  template<typename Metric, typename PrototypePtr, typename ...Args>
  scoped_refptr<Metric> FindOrCreateMetric(PrototypePtr proto, Args&&... args);

  // Return the metric instantiated from the given prototype, or NULL if none has been
  // instantiated. Primarily used by tests trying to read metric values.
  template<typename Metric>
  scoped_refptr<Metric> FindOrNull(const MetricPrototype& prototype) const;

  const std::string& id() const { return id_; }

  // See MetricRegistry::WriteAsJson()
  Status WriteAsJson(JsonWriter* writer,
                     const MetricJsonOptions& opts) const;

  Status WriteForPrometheus(PrometheusWriter* writer,
                            const MetricPrometheusOptions& opts);

  const MetricMap& TEST_UsageMetricsMap() const NO_THREAD_SAFETY_ANALYSIS {
    return metric_map_;
  }

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

  const MetricEntityPrototype& prototype() const { return *prototype_; }

  void RemoveFromMetricMap(const MetricPrototype* proto);

  bool TEST_ContainsMetricName(const std::string& metric_name) const;

  Result<std::string> TEST_GetAttributeFromMap(const std::string& key) const;

 private:
  friend class MetricRegistry;
  friend class RefCountedThreadSafe<MetricEntity>;

  MetricEntity(
      const MetricEntityPrototype* prototype,
      std::string id,
      AttributeMap attributes,
      MetricsAggregator* metrics_aggregator,
      std::shared_ptr<MemTracker> mem_tracker = nullptr);

  ~MetricEntity();

  // Update the prometheus_attributes_ based on current attributes_.
  // Return AggregationLevels that this entity should be aggregated at.
  AggregationLevels ReconstructPrometheusAttributes() EXCLUDES(lock_);

  AggregationLevels ReconstructPrometheusAttributesUnlocked() REQUIRES(lock_);

  // Ensure that the given metric prototype is allowed to be instantiated
  // within this entity. This entity's type must match the expected entity
  // type defined within the metric prototype.
  void CheckInstantiation(const MetricPrototype* proto) const;

  template<typename Pointer>
  void AddConsumption(const Pointer& value) REQUIRES(lock_);

  MetricMap GetFilteredMetricMap(
      const std::optional<std::vector<std::string>>& match_params_optional) const REQUIRES(lock_);

  std::string LogPrefix() const;

  void RebuildNonPreAggregatedMetricsIfNeeded() REQUIRES(lock_);

  const MetricEntityPrototype* const prototype_;
  const std::string id_;

  mutable simple_spinlock lock_;

  // Map from metric prototype to Metric object. Metrics in this map can either be pre-aggregated
  // metrics managed by aggregator_, or non-pre-aggregated metrics whose prototypes are stored in
  // non_pre_aggregated_metrics_.
  // Invariant: need_rebuild_non_pre_aggregated_metrics_ must be set to true whenever
  // a non-pre-aggregated metric is added to or removed from metric_map_.
  MetricMap metric_map_ GUARDED_BY(lock_);

  bool need_rebuild_non_pre_aggregated_metrics_ GUARDED_BY(lock_) = true;

  // Vector holding metric prototypes from metric_map_ that are not pre-aggregated.
  // These metrics can either be scrape time aggregated metric or require no aggregation.
  // Invariant: Valid only when need_rebuild_non_pre_aggregated_metrics_ is false.
  std::shared_ptr<const NonPreAggregatedMetrics> non_pre_aggregated_metrics_
      GUARDED_BY(lock_);

  // The key/value attributes.
  AttributeMap attributes_ GUARDED_BY(lock_);

  // prometheus_attributes_ holds both Prometheus-specific, and general attributes
  // from attributes_. It's used for Prometheus metric scraping.
  AttributeMap prometheus_attributes_ GUARDED_BY(lock_);

  // Empty value means metrics under this entity should not be pre-aggregated.
  std::string aggregation_id_for_pre_aggregation_ GUARDED_BY(lock_);

  MetricsAggregator* metrics_aggregator_;

  std::weak_ptr<MemTracker> mem_tracker_ GUARDED_BY(lock_);

  const AggregationLevels default_aggregation_levels_;

  // The set of metrics which should never be retired.
  std::vector<scoped_refptr<Metric>> never_retire_metrics_ GUARDED_BY(lock_);
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

    if (!aggregation_id_for_pre_aggregation_.empty()) {
      WARN_NOT_OK(m->SetUpPreAggregationForPrometheus(
          metrics_aggregator_, prometheus_attributes_, default_aggregation_levels_,
          aggregation_id_for_pre_aggregation_),
          Format("Failed to setup pre-aggregation for metric: $0", m->prototype()->name()));
    }

    if (!m->IsPreAggregated()) {
      // non_pre_aggregated_metrics_ will be rebuilt during the next Prometheus scrape.
      need_rebuild_non_pre_aggregated_metrics_ = true;
    }

    InsertOrDie(&metric_map_, m->prototype(), m);
    AddConsumption(*m);
  }
  return m;
}

template<typename Metric>
scoped_refptr<Metric> MetricEntity::FindOrNull(const MetricPrototype& prototype) const {
  std::lock_guard l(lock_);
  return down_cast<Metric*>(FindPtrOrNull(metric_map_, &prototype).get());
}

void WriteRegistryAsJson(JsonWriter* writer);

void ConvertToServerLevelAttributes(MetricEntity::AttributeMap* non_server_level_attributes);

// Asserts that the actual attributes match the expected attributes.
// Note that if aggregation_id is at the server level then ignores expected attributes that
// are not at the server level attributes by calling ConvertToServerLevelAttributes.
void AssertAttributesMatchExpectation(
    const std::string& metric_name,
    const std::string& aggregation_id,
    const MetricEntity::AttributeMap& actual_attributes,
    MetricEntity::AttributeMap expected_attributes);

} // namespace yb
