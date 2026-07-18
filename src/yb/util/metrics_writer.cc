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
#include "yb/util/metrics_writer.h"

#include "yb/util/metrics.h"

#include "yb/util/debug.h"
#include "yb/util/enums.h"

DECLARE_string(metric_node_name);

namespace yb {

static const char* const kNumberOfEntriesCutOffMetricName = "num_of_entries_cut_off";

PrometheusWriter::PrometheusWriter(std::stringstream* output,
                                   const MetricPrometheusOptions& opts)
    : output_(output),
      timestamp_(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count()),
      export_help_and_type_(opts.export_help_and_type),
      prometheus_metric_filter_(CreatePrometheusMetricFilter(opts)),
      remaining_allowed_entries_(opts.max_metric_entries) {}

PrometheusWriter::~PrometheusWriter() {}

Status PrometheusWriter::FlushScrapeTimeAggregatedValues() {
  for (const auto& [metric_name, metric_info] :
      scrape_time_aggregated_metric_info_by_metric_name_) {
    if (metric_info.metric_prototype_holder_.use_count() == 1) {
      // This metric has been removed, so we don't need to output it.
      continue;
    }
    auto& aggregated_values = metric_info.scrape_time_aggregated_values_;
    if (remaining_allowed_entries_ < aggregated_values.size()) {
      num_of_entries_cut_off_ += aggregated_values.size();
      continue;
    }
    remaining_allowed_entries_ -= aggregated_values.size();

    const auto& metric_entity_type = metric_info.metric_entity_type_;
    auto attributes_by_aggregation_id_it =
        attributes_by_metric_entity_type_and_aggregation_id_.find(metric_entity_type);
    if (attributes_by_aggregation_id_it ==
        attributes_by_metric_entity_type_and_aggregation_id_.end()) {
      return STATUS(NotFound,
          Format("No attributes stored for metric with entity type $0", metric_entity_type));
    }
    const auto& attributes_by_aggregation_id = attributes_by_aggregation_id_it->second;

    const auto& metric_help = metric_info.help_and_type_.help;
    const auto& metric_type = metric_info.help_and_type_.type;
    for (const auto& [aggregation_id, value] : aggregated_values) {
      auto attributes_it = attributes_by_aggregation_id.find(aggregation_id);
      if (attributes_it == attributes_by_aggregation_id.end()) {
        return STATUS(NotFound,
            Format("No attributes stored for metric with entity type $0 and aggregation id $1",
                metric_entity_type, aggregation_id));
      }
      FlushHelpAndTypeIfRequested(metric_name, metric_help, metric_type);
      RETURN_NOT_OK(FlushSingleEntry(attributes_it->second, metric_name, value));
    }
  }
  return Status::OK();
}

Status PrometheusWriter::Finish(const MetricsAggregator& metrics_aggregator) {
  RETURN_NOT_OK(FlushPreAggregatedValues(metrics_aggregator));
  RETURN_NOT_OK(FlushScrapeTimeAggregatedValues());
  FlushHelpAndTypeIfRequested(
      kNumberOfEntriesCutOffMetricName,
      "Number of metric entries truncated due to exceeding the maximum metric entry limit",
      "counter");
  MetricEntity::AttributeMap attributes;
  attributes["exported_instance"] = FLAGS_metric_node_name;
  return FlushSingleEntry(attributes, kNumberOfEntriesCutOffMetricName,
      num_of_entries_cut_off_);
}


void PrometheusWriter::FlushHelpAndTypeIfRequested(
    const std::string& name, const char* description, const char* type) {
  if (export_help_and_type_) {
    *output_ << "# HELP " << name << " " << description << std::endl;
    *output_ << "# TYPE " << name << " " << type << std::endl;
  }
}

Status PrometheusWriter::FlushSingleEntry(
    const MetricEntity::AttributeMap& attributes,
    const std::string& name, const int64_t value) {
  *output_ << name;
  size_t total_elements = attributes.size();
  if (total_elements > 0) {
    *output_ << "{";
    for (const auto& entry : attributes) {
      *output_ << entry.first << "=\"" << entry.second << "\"";
      if (--total_elements > 0) {
        *output_ << ",";
      }
    }
    *output_ << "}";
  }
  *output_ << " " << value;
  *output_ << " " << timestamp_;
  *output_ << std::endl;
  return Status::OK();
}

void PrometheusWriter::InvalidAggregationFunction(AggregationFunction aggregation_function) {
  FATAL_INVALID_ENUM_VALUE(AggregationFunction, aggregation_function);
}

void PrometheusWriter::AddScrapeTimeAggregatedEntry(
    const std::string& aggregation_id, const char* type, const char* description,
    const MetricEntity::AttributeMap& attributes, const std::string& metric_name, int64_t value,
    AggregationFunction aggregation_function, const std::string& metric_entity_type,
    std::shared_ptr<MetricPrototype> metric_prototype_holder) {
  // Store attributes.
  auto [cached_attributes_it, inserted] =
      attributes_by_metric_entity_type_and_aggregation_id_[metric_entity_type]
          .try_emplace(aggregation_id, attributes);
  if (inserted && aggregation_id == kServerLevelAggregationId) {
    ConvertToServerLevelAttributes(&cached_attributes_it->second);
  }
  if (!inserted && kIsDebug) {
    // Ensure attributes with the same aggregation_id and metric_entity are identical.
    AssertAttributesMatchExpectation(
        metric_name, aggregation_id,
        /*actual_attributes=*/cached_attributes_it->second,
        /*expected_attributes=*/attributes);
  }

  // Store metric metadata if this is the first time we see this metric.
  auto metric_info_it =
      scrape_time_aggregated_metric_info_by_metric_name_.find(metric_name);
  if (metric_info_it == scrape_time_aggregated_metric_info_by_metric_name_.end()) {
    auto [it, _] = scrape_time_aggregated_metric_info_by_metric_name_.emplace(metric_name,
        ScrapeTimeAggregatedMetricInfo(metric_entity_type, MetricHelpAndType{description, type},
                                       std::move(metric_prototype_holder)));
    metric_info_it = it;
  }

  // Aggregate values.
  auto& aggregated_values = metric_info_it->second.scrape_time_aggregated_values_;
  auto stored_value_it = aggregated_values.find(aggregation_id);
  if (stored_value_it == aggregated_values.end()) {
    aggregated_values[aggregation_id] = value;
    return;
  }

  auto& stored_value = stored_value_it->second;
  switch (aggregation_function) {
    case kSum:
      stored_value += value;
      break;
    case kMax:
      if (value > stored_value) {
        stored_value = value;
      }
      break;
    default:
      InvalidAggregationFunction(aggregation_function);
      break;
  }
}

Status PrometheusWriter::WriteSingleEntry(
    const MetricEntity::AttributeMap& attributes,
    const std::string& name,
    int64_t value,
    AggregationFunction aggregation_function,
    AggregationLevels default_aggregation_levels,
    const std::string& metric_entity_type,
    const char* type,
    const char* description,
    std::shared_ptr<MetricPrototype> metric_prototype_holder) {
  AggregationLevels aggregation_levels =
      prometheus_metric_filter_->GetAggregationLevels(name, default_aggregation_levels);

  if (aggregation_levels & kStreamLevel) {
    DCHECK(metric_entity_type == kXClusterMetricEntityName ||
           metric_entity_type == kCdcsdkMetricEntityName);
    auto stream_id_it = attributes.find("stream_id");
    DCHECK(stream_id_it != attributes.end());
    AddScrapeTimeAggregatedEntry(
        stream_id_it->second, type, description, attributes, name, value,
        aggregation_function, metric_entity_type, metric_prototype_holder);
    // Metrics from xcluster or cdcsdk entity should only be exposed on stream level.
    DCHECK(aggregation_levels == kStreamLevel);
  }

  auto table_id_it = attributes.find("table_id");

  if (aggregation_levels & kTableLevel) {
    DCHECK(table_id_it != attributes.end());
    AddScrapeTimeAggregatedEntry(
        table_id_it->second, type, description, attributes, name, value,
        aggregation_function, metric_entity_type, metric_prototype_holder);
  }

  if (aggregation_levels & kServerLevel) {
    if (table_id_it == attributes.end()) {
      if (remaining_allowed_entries_ == 0) {
        num_of_entries_cut_off_++;
        return Status::OK();
      }
      // Metric doesn't have table id, so no need to aggregate.
      remaining_allowed_entries_--;
      FlushHelpAndTypeIfRequested(name, description, type);
      return FlushSingleEntry(attributes, name, value);
    }
    AddScrapeTimeAggregatedEntry(
        kServerLevelAggregationId, type, description, attributes,
        name, value, aggregation_function, metric_entity_type, metric_prototype_holder);
  }

  return Status::OK();
}

NMSWriter::NMSWriter(EntityMetricsMap* table_metrics,
                     MetricsMap* server_metrics,
                     const MetricPrometheusOptions& opts)
    : PrometheusWriter(nullptr, opts), table_metrics_(*table_metrics),
    server_metrics_(*server_metrics) {}

Status NMSWriter::FlushSingleEntry(
    const MetricEntity::AttributeMap& attributes, const std::string& name, const int64_t value) {
  auto it = attributes.find("table_id");
  if (it != attributes.end()) {
    table_metrics_[it->second][name] = value;
    return Status::OK();
  }
  it = attributes.find("metric_type");
  if (it == attributes.end() || it->second != "server") {
    return Status::OK();
  }
  server_metrics_[name] = value;
  return Status::OK();
}

Status PrometheusWriter::FlushPreAggregatedValues(
    const MetricsAggregator& metrics_aggregator) {
  const auto& attributes_ptr_by_metric_entity_type_and_aggregation_id =
      metrics_aggregator.attributes_ptr_by_metric_entity_type_and_aggregation_id();
  const auto& pre_aggregated_metric_info_by_metric_name =
      metrics_aggregator.pre_aggregated_metric_info_by_metric_name();

  for (const auto& [name, metric_info_ptr] : pre_aggregated_metric_info_by_metric_name) {
    const auto& metric_info = *metric_info_ptr;

    // Get aggregation levels for the metric.
    const auto aggregation_levels = prometheus_metric_filter_->GetAggregationLevels(
        name, metric_info.default_aggregation_levels_);
    if (aggregation_levels == kNoLevel) {
      continue;
    }
    bool need_server_level_values = aggregation_levels & kServerLevel;
    bool need_table_or_stream_level_values = aggregation_levels & kTableLevel ||
                                             aggregation_levels & kStreamLevel;

    // Make sure we have enough space to write all the entries.
    auto num_output_entries = (need_table_or_stream_level_values)
                              ? metric_info.num_aggregated_value_holders()
                              : 0;
    num_output_entries += (need_server_level_values) ? 1 : 0;
    if (remaining_allowed_entries_ < num_output_entries) {
      num_of_entries_cut_off_ += num_output_entries;
      continue;
    }
    remaining_allowed_entries_ -= num_output_entries;

    // Retrieve attributes_ptr_by_aggregation_id map.
    auto attributes_ptr_by_aggregation_id_it =
        attributes_ptr_by_metric_entity_type_and_aggregation_id.find(
            metric_info.metric_entity_type_);
    if (attributes_ptr_by_aggregation_id_it ==
        attributes_ptr_by_metric_entity_type_and_aggregation_id.end()) {
      return STATUS(NotFound, Format("No attributes stored for metric with entity type $0",
          metric_info.metric_entity_type_));
    }
    const auto& attributes_ptr_by_aggregation_id = attributes_ptr_by_aggregation_id_it->second;

    // Begin flushing the metric.
    const auto& metric_help = metric_info.help_and_type_.help;
    const auto& metric_type = metric_info.help_and_type_.type;
    const auto pre_aggregated_values = metric_info.GetPreAggregatedValues(
        need_server_level_values, need_table_or_stream_level_values);
    for (const auto& [aggregation_id, value] : pre_aggregated_values) {
      auto attributes_ptr_it = attributes_ptr_by_aggregation_id.find(aggregation_id);
      if (attributes_ptr_it == attributes_ptr_by_aggregation_id.end()) {
        RSTATUS_DCHECK(aggregation_id != kServerLevelAggregationId, NotFound, Format(
            "No server-level attributes stored for metric with entity type $0, aggregation id $1",
            metric_info.metric_entity_type_, aggregation_id));
        // This can happen when a new tablet is added during the flush.
        continue;
      }

      FlushHelpAndTypeIfRequested(name, metric_help, metric_type);
      RETURN_NOT_OK(FlushSingleEntry(*attributes_ptr_it->second, name, value));
    }
  }

  return Status::OK();
}

std::optional<MetricEntity::AttributeMap> PrometheusWriter::TEST_GetAttributesForAggregationId(
    std::string metric_entity_type, std::string aggregation_id) {
  auto attributes_by_aggregation_id_it =
      attributes_by_metric_entity_type_and_aggregation_id_.find(metric_entity_type);
  if (attributes_by_aggregation_id_it ==
      attributes_by_metric_entity_type_and_aggregation_id_.end()) {
    return std::nullopt;
  }

  const auto& attributes_by_aggregation_id = attributes_by_aggregation_id_it->second;
  auto attributes_it = attributes_by_aggregation_id.find(aggregation_id);
  if (attributes_it == attributes_by_aggregation_id.end()) {
    return std::nullopt;
  }

  return attributes_it->second;
}

std::optional<int64_t> PrometheusWriter::TEST_GetScrapeTimeAggregatedValue(
    std::string metric_name, std::string aggregation_id) {
  auto metric_info_it = scrape_time_aggregated_metric_info_by_metric_name_.find(metric_name);
  if (metric_info_it == scrape_time_aggregated_metric_info_by_metric_name_.end()) {
    return std::nullopt;
  }

  const auto& aggregated_values = metric_info_it->second.scrape_time_aggregated_values_;
  auto value_it = aggregated_values.find(aggregation_id);
  if (value_it == aggregated_values.end()) {
    return std::nullopt;
  }

  return value_it->second;
}

} // namespace yb
