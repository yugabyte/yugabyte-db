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

#include "yb/util/metrics_aggregator.h"

#include "yb/util/debug.h"
#include "yb/util/metrics.h"
#include "yb/util/unique_lock.h"

namespace yb {

std::shared_ptr<AtomicInt<int64_t>>
PreAggregatedMetricInfo::CreateOrFindPreAggregatedValueHolder(const std::string& aggregation_id) {
  std::lock_guard l(aggregated_value_holders_mutex_);
  auto& value_holder_ptr = aggregated_value_holders_[aggregation_id];
  if (value_holder_ptr == nullptr) {
    value_holder_ptr = std::make_shared<AtomicInt<int64_t>>(0);
  }
  return value_holder_ptr;
}

std::unordered_map<std::string, int64_t>
PreAggregatedMetricInfo::GetPreAggregatedValues(
    bool need_server_level_values, bool need_table_or_stream_level_values) const {
  std::lock_guard l(aggregated_value_holders_mutex_);
  std::unordered_map<std::string, int64_t> aggregated_values;
  int64_t server_level_value = 0;
  for (const auto& [aggregation_id, value_holder] : aggregated_value_holders_) {
    if (value_holder.use_count() == 1) {
      // Metric is no longer used, will be removed in the next aggregator cleanup.
      continue;
    }

    const auto value = value_holder->Load();
    if (need_table_or_stream_level_values) {
      aggregated_values[aggregation_id] = value;
    }
    if (need_server_level_values) {
      server_level_value += value;
    }
  }

  if (need_server_level_values) {
    aggregated_values[kServerLevelAggregationId] = server_level_value;
  }

  return aggregated_values;
}

void PreAggregatedMetricInfo::CleanUpAggregatedValueHolders(
  std::unordered_set<std::string>& current_aggregation_ids) {
  std::lock_guard<std::mutex> l(aggregated_value_holders_mutex_);
  for (auto value_holder_it = aggregated_value_holders_.begin();
      value_holder_it != aggregated_value_holders_.end();) {
    if (value_holder_it->second.use_count() == 1) {
      value_holder_it = aggregated_value_holders_.erase(value_holder_it);
      continue;
    }
    current_aggregation_ids.insert(value_holder_it->first);
    ++value_holder_it;
  }
}

bool MetricsAggregator::IsPreAggregationSupported(
    const MetricPrototype* metric_prototype, AggregationLevels default_aggregation_levels) const {
  if (metric_prototype->aggregation_function() != AggregationFunction::kSum) {
    return false;
  }

  if (strcmp(metric_prototype->entity_type(), "tablet") == 0) {
    DCHECK(default_aggregation_levels & kTableLevel);
    return true;
  }

  if (strcmp(metric_prototype->entity_type(), kXClusterMetricEntityName) == 0 ||
      strcmp(metric_prototype->entity_type(), kCdcsdkMetricEntityName) == 0) {
    DCHECK(default_aggregation_levels & kStreamLevel);
    return true;
  }

  return false;
}

std::shared_ptr<AtomicInt<int64_t>>
MetricsAggregator::CreateOrFindPreAggregatedMetricValueHolder(
    const MetricEntity::AttributeMap& attributes,
    std::shared_ptr<MetricPrototype> metric_prototype_holder,
    const std::string& metric_name,
    AggregationLevels default_aggregation_levels,
    const std::string& metric_entity_type,
    const std::string& aggregation_id,
    const char* type,
    const char* description) {
  UniqueLock lock(mutex_);

  auto& attributes_ptr_by_aggregation_id =
      attributes_ptr_by_metric_entity_type_and_aggregation_id_[metric_entity_type];

  auto StoreAttributesForPreAggregatedMetric = [&](const std::string& attributes_aggregation_id) {
    auto& attributes_ptr = attributes_ptr_by_aggregation_id[attributes_aggregation_id];
    if (attributes_ptr != nullptr) {
      return;
    }

    attributes_ptr = std::make_shared<MetricEntity::AttributeMap>(attributes);
    if (attributes_aggregation_id == kServerLevelAggregationId) {
      ConvertToServerLevelAttributes(attributes_ptr.get());
    }
  };

  // Store Attributes if not already stored
  StoreAttributesForPreAggregatedMetric(aggregation_id);
  if (default_aggregation_levels & kServerLevel) {
    StoreAttributesForPreAggregatedMetric(kServerLevelAggregationId);
  }

  // Store metric metadata if this is the first time we see this metric
  auto& aggregated_metric_info = pre_aggregated_metric_info_by_metric_name_[metric_name];
  if (aggregated_metric_info == nullptr) {
    aggregated_metric_info = std::make_shared<PreAggregatedMetricInfo>(
        metric_entity_type, default_aggregation_levels, MetricHelpAndType{description, type},
        std::move(metric_prototype_holder));
  }
  lock.unlock();

  return aggregated_metric_info->CreateOrFindPreAggregatedValueHolder(aggregation_id);
}

Status MetricsAggregator::ReplaceAttributes(
    const std::string& metric_entity_type,
    const std::string& aggregation_id,
    const MetricEntity::AttributeMap& attributes) {
  std::lock_guard l(mutex_);
  return ReplaceAttributesUnlocked(metric_entity_type, aggregation_id, attributes);
}

Status MetricsAggregator::ReplaceAttributesUnlocked(
    const std::string& metric_entity_type,
    const std::string& aggregation_id,
    const MetricEntity::AttributeMap& attributes) {
  const std::string errmsg = Format(
      "No attributes stored for metric with entity type $0 and aggregation id $1",
      metric_entity_type, aggregation_id);

  auto attributes_ptr_by_aggregation_id_it =
      attributes_ptr_by_metric_entity_type_and_aggregation_id_.find(metric_entity_type);
  if (attributes_ptr_by_aggregation_id_it ==
      attributes_ptr_by_metric_entity_type_and_aggregation_id_.end()) {
    return STATUS_FORMAT(NotFound, "No attributes stored for metric with entity type $0",
        metric_entity_type);
  }
  auto& attributes_ptr = attributes_ptr_by_aggregation_id_it->second[aggregation_id];
  if (attributes_ptr == nullptr) {
    return STATUS_FORMAT(NotFound,
        "No attributes stored for metric with entity type $0 and aggregation id $1",
        metric_entity_type, aggregation_id);
  }

  attributes_ptr = std::make_shared<MetricEntity::AttributeMap>(attributes);
  if (aggregation_id != kServerLevelAggregationId) {
    // Also update server level attributes.
    return ReplaceAttributesUnlocked(metric_entity_type, kServerLevelAggregationId, attributes);
  }

  ConvertToServerLevelAttributes(attributes_ptr.get());
  return Status::OK();
}

void MetricsAggregator::CleanupRetiredMetricsAndCorrespondingAttributes() {
  std::lock_guard l(mutex_);
  // Remove unreferenced metric value holder.
  std::unordered_set<std::string> current_aggregation_ids{kServerLevelAggregationId};
  for (auto metric_info_it = pre_aggregated_metric_info_by_metric_name_.begin();
       metric_info_it != pre_aggregated_metric_info_by_metric_name_.end();) {
    metric_info_it->second->CleanUpAggregatedValueHolders(current_aggregation_ids);

    if (metric_info_it->second->num_aggregated_value_holders() == 0) {
      metric_info_it = pre_aggregated_metric_info_by_metric_name_.erase(metric_info_it);
      continue;
    }
    ++metric_info_it;
  }

  // Remove attributes that are no longer needed
  for (auto& [metric_entity_type, attributes_ptr_by_aggregation_ids] :
      attributes_ptr_by_metric_entity_type_and_aggregation_id_) {
    std::erase_if(attributes_ptr_by_aggregation_ids, [&current_aggregation_ids](const auto& pair) {
      return !current_aggregation_ids.contains(pair.first);
    });
  }
}

std::optional<int64_t> MetricsAggregator::TEST_GetMetricPreAggregatedValue(
    const std::string& metric_name, const std::string& aggregation_id) {
  SharedLock lock(mutex_);
  auto pre_aggregated_metric_info_it =
      pre_aggregated_metric_info_by_metric_name_.find(metric_name);
  if (pre_aggregated_metric_info_it == pre_aggregated_metric_info_by_metric_name_.end()) {
    return std::nullopt;
  }

  auto& pre_aggregated_metric_info = pre_aggregated_metric_info_it->second;
  if (aggregation_id == kServerLevelAggregationId) {
    auto result = pre_aggregated_metric_info->GetPreAggregatedValues(
        /*need_server_level_values=*/true, /*need_table_or_stream_level_values=*/false);
    if (result.count(kServerLevelAggregationId) > 0) {
      return result[kServerLevelAggregationId];
    }
    return std::nullopt;
  }

  std::lock_guard l(pre_aggregated_metric_info->aggregated_value_holders_mutex_);
  auto pre_aggregated_metric_value_holders = pre_aggregated_metric_info->aggregated_value_holders_;
  auto value_holder_it = pre_aggregated_metric_value_holders.find(aggregation_id);
  if (value_holder_it == pre_aggregated_metric_value_holders.end()) {
    return std::nullopt;
  }

  return value_holder_it->second->Load();
}

std::optional<MetricEntity::AttributeMap>
MetricsAggregator::TEST_GetAttributesForAggregationId(
    const std::string& metric_entity_type, const std::string& aggregation_id) {
  SharedLock lock(mutex_);
  auto attributes_ptr_by_aggregation_id_it =
      attributes_ptr_by_metric_entity_type_and_aggregation_id_.find(metric_entity_type);
  if (attributes_ptr_by_aggregation_id_it ==
      attributes_ptr_by_metric_entity_type_and_aggregation_id_.end()) {
    return std::nullopt;
  }

  auto& attributes_ptr_by_aggregation_id = attributes_ptr_by_aggregation_id_it->second;
  auto attributes_ptr_it = attributes_ptr_by_aggregation_id.find(aggregation_id);
  if (attributes_ptr_it == attributes_ptr_by_aggregation_id.end() ||
      attributes_ptr_it->second == nullptr) {
    return std::nullopt;
  }

  return *attributes_ptr_it->second;
}

} // namespace yb
