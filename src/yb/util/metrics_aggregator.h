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

#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include "yb/util/atomic.h"
#include "yb/util/metric_entity.h"
#include "yb/util/shared_lock.h"

namespace yb {

class MetricPrototype;

// This class stores information about a pre-aggregated metric,
// including its aggregated value holders.
class PreAggregatedMetricInfo {
 public:
  PreAggregatedMetricInfo(
      std::string metric_entity_type,
      AggregationLevels default_aggregation_levels,
      MetricHelpAndType help_and_type,
      std::shared_ptr<MetricPrototype> metric_prototype_holder)
    : metric_entity_type_(std::move(metric_entity_type)),
      default_aggregation_levels_(default_aggregation_levels),
      help_and_type_(help_and_type),
      metric_prototype_holder_(std::move(metric_prototype_holder)) {}

  std::shared_ptr<AtomicInt<int64_t>>
  CreateOrFindPreAggregatedValueHolder(const std::string& aggregation_id)
      EXCLUDES(aggregated_value_holders_mutex_);

  // Remove unused aggregated value holders and insert the used ones into the
  // out parameter current_aggregation_ids.
  void CleanUpAggregatedValueHolders(std::unordered_set<std::string>& current_aggregation_ids)
      EXCLUDES(aggregated_value_holders_mutex_);

  std::unordered_map<std::string, int64_t> GetPreAggregatedValues(
      bool need_server_level_values,
      bool need_table_or_stream_level_values) const EXCLUDES(aggregated_value_holders_mutex_);

  size_t num_aggregated_value_holders() const EXCLUDES(aggregated_value_holders_mutex_) {
    std::lock_guard l(aggregated_value_holders_mutex_);
    return aggregated_value_holders_.size();
  }

  const std::string metric_entity_type_;
  const AggregationLevels default_aggregation_levels_;
  const MetricHelpAndType help_and_type_;
  const std::shared_ptr<MetricPrototype> metric_prototype_holder_;

  // Mutex to protect aggregated_values
  mutable std::mutex aggregated_value_holders_mutex_;

  // Pre-aggregated values for different aggregation_ids (table id or stream id)
  std::unordered_map<std::string, std::shared_ptr<AtomicInt<int64_t>>> aggregated_value_holders_
      GUARDED_BY(aggregated_value_holders_mutex_);
};

// Pre-aggregate metrics by storing pre-aggregated metric information and attributes.
class MetricsAggregator {
 public:
  using AggregationIdToAttributeMapPtr =
      std::unordered_map<std::string, std::shared_ptr<MetricEntity::AttributeMap>>;
  using AttributesMapPtrByMetricEntityTypeAndAggregationId =
      std::unordered_map<std::string, AggregationIdToAttributeMapPtr>;
  using PreAggregatedMetricInfoByMetricName =
      std::unordered_map<std::string, std::shared_ptr<PreAggregatedMetricInfo>>;

  bool IsPreAggregationSupported(
      const MetricPrototype* metric_prototype,
      AggregationLevels default_levels) const EXCLUDES(mutex_);

  // Store its metadata and attributes if this is the first time encountering this metric.
  // Next, find or create the pre-aggregated value holder for the metric, then return it.
  std::shared_ptr<AtomicInt<int64_t>> CreateOrFindPreAggregatedMetricValueHolder(
      const MetricEntity::AttributeMap& attributes,
      std::shared_ptr<MetricPrototype> metric_prototype_holder,
      const std::string& metric_name,
      AggregationLevels default_aggregation_levels,
      const std::string& metric_entity_type,
      const std::string& aggregation_id,
      const char* type,
      const char* description) EXCLUDES(mutex_);

  bool IsPreAggregatedMetric(const std::string& metric_name) const EXCLUDES(mutex_) {
    SharedLock lock(mutex_);
    return pre_aggregated_metric_info_by_metric_name_.count(metric_name) > 0;
  }

  PreAggregatedMetricInfoByMetricName
  pre_aggregated_metric_info_by_metric_name() const EXCLUDES(mutex_) {
    SharedLock lock(mutex_);
    return pre_aggregated_metric_info_by_metric_name_;
  }

  AttributesMapPtrByMetricEntityTypeAndAggregationId
  attributes_ptr_by_metric_entity_type_and_aggregation_id() const EXCLUDES(mutex_) {
    SharedLock lock(mutex_);
    return attributes_ptr_by_metric_entity_type_and_aggregation_id_;
  }

  Status ReplaceAttributes(
      const std::string& metric_entity_type,
      const std::string& aggregation_id,
      const MetricEntity::AttributeMap& attributes) EXCLUDES(mutex_);

  void CleanupRetiredMetricsAndCorrespondingAttributes() EXCLUDES(mutex_);

  std::optional<MetricEntity::AttributeMap>TEST_GetAttributesForAggregationId (
      const std::string& metric_entity_type, const std::string& aggregation_id) EXCLUDES(mutex_);

  std::optional<int64_t> TEST_GetMetricPreAggregatedValue(
      const std::string& metric_name, const std::string& aggregation_id) EXCLUDES(mutex_);

 private:

  Status ReplaceAttributesUnlocked(
      const std::string& metric_entity_type,
      const std::string& aggregation_id,
      const MetricEntity::AttributeMap& attributes) REQUIRES(mutex_);

  mutable std::shared_mutex mutex_;

  AttributesMapPtrByMetricEntityTypeAndAggregationId
      attributes_ptr_by_metric_entity_type_and_aggregation_id_ GUARDED_BY(mutex_);

  PreAggregatedMetricInfoByMetricName pre_aggregated_metric_info_by_metric_name_
      GUARDED_BY(mutex_);
};

} // namespace yb
