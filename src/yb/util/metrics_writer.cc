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

#include "yb/util/metrics_writer.h"

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

Status PrometheusWriter::FlushAggregatedValues() {
  for (const auto& [metric_name, entity] : aggregated_values_) {
    if (remaining_allowed_entries_ < entity.size()) {
      num_of_entries_cut_off_ += entity.size();
      continue;
    }
    remaining_allowed_entries_ -= entity.size();
    auto it = metric_help_and_type_.find(metric_name);
    for (const auto& [aggregation_id, value] : entity) {
      if (it != metric_help_and_type_.end()) {
        FlushHelpAndType(metric_name, it->second.type, it->second.help);
      }
      RETURN_NOT_OK(FlushSingleEntry(
          aggregated_id_to_attributes_[aggregation_id], metric_name, value));
    }
  }
  return Status::OK();
}

Status PrometheusWriter::FlushNumberOfEntriesCutOff() {
  // We expose this metric regardless of remaining_allowed_entries_.
  if (export_help_and_type_) {
    FlushHelpAndType(kNumberOfEntriesCutOffMetricName, "counter",
        "Number of metric entries truncated due to exceeding the maximum metric entry limit");
  }
  MetricEntity::AttributeMap attr;
  attr["exported_instance"] = FLAGS_metric_node_name;
  RETURN_NOT_OK(FlushSingleEntry(attr, kNumberOfEntriesCutOffMetricName,
      num_of_entries_cut_off_));
  return Status::OK();
}


void PrometheusWriter::FlushHelpAndType(
    const std::string& name, const char* type, const char* description) {
  *output_ << "# HELP " << name << " " << description << std::endl;
  *output_ << "# TYPE " << name << " " << type << std::endl;
}

Status PrometheusWriter::FlushSingleEntry(
    const MetricEntity::AttributeMap& attr,
    const std::string& name, const int64_t value) {
  *output_ << name;
  size_t total_elements = attr.size();
  if (total_elements > 0) {
    *output_ << "{";
    for (const auto& entry : attr) {
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

void PrometheusWriter::AddAggregatedEntry(
    const std::string& aggregation_id, const MetricEntity::AttributeMap& attr,
    const std::string& metric_name, int64_t value, AggregationFunction aggregation_function,
    const char* type, const char* description) {
  // For #TYPE and #HELP.
  if (export_help_and_type_) {
    metric_help_and_type_.try_emplace(metric_name, MetricHelpAndType{type, description});
  }
  aggregated_id_to_attributes_.try_emplace(aggregation_id, attr);
  auto& stored_value = aggregated_values_[metric_name][aggregation_id];
  switch (aggregation_function) {
    case kSum:
      stored_value += value;
      break;
    case kMax:
      // If we have a new max, also update the metadata so that it matches correctly.
      if (value > stored_value) {
        aggregated_id_to_attributes_[aggregation_id] = attr;
        stored_value = value;
      }
      break;
    default:
      InvalidAggregationFunction(aggregation_function);
      break;
  }
}

Status PrometheusWriter::WriteSingleEntry(
    const MetricEntity::AttributeMap& attr,
    const std::string& name,
    int64_t value,
    AggregationFunction aggregation_function,
    AggregationLevels default_levels,
    const char* type,
    const char* description) {
  AggregationLevels aggregation_levels =
      prometheus_metric_filter_->GetAggregationLevels(name, default_levels);

  auto metric_type_it = attr.find("metric_type");
  DCHECK(metric_type_it != attr.end());
  auto metric_type = metric_type_it->second;

  if (aggregation_levels & kStreamLevel) {
    DCHECK(metric_type == kXClusterMetricEntityName || metric_type == kCdcsdkMetricEntityName);
    auto stream_id_it = attr.find("stream_id");
    DCHECK(stream_id_it != attr.end());
    AddAggregatedEntry(stream_id_it->second, attr, name, value, aggregation_function,
        type, description);
    // Metrics from xcluster or cdcsdk entity should only be exposed on stream level.
    DCHECK(aggregation_levels == kStreamLevel);
  }

  auto table_id_it = attr.find("table_id");

  if (aggregation_levels & kTableLevel) {
    DCHECK(table_id_it != attr.end());
    AddAggregatedEntry(table_id_it->second, attr, name, value, aggregation_function,
        type, description);
  }

  if (aggregation_levels & kServerLevel) {
    if (table_id_it == attr.end()) {
      if (remaining_allowed_entries_ == 0) {
        num_of_entries_cut_off_++;
        return Status::OK();
      }
      // Metric doesn't have table id, so no need to aggregate.
      remaining_allowed_entries_--;
      if (export_help_and_type_) {
        FlushHelpAndType(name, type, description);
      }
      return FlushSingleEntry(attr, name, value);
    }
    MetricEntity::AttributeMap new_attr = attr;
    new_attr.erase("table_id");
    new_attr.erase("table_name");
    new_attr.erase("table_type");
    new_attr.erase("namespace_name");
    AddAggregatedEntry(metric_type, new_attr, name, value, aggregation_function,
        type, description);
  }

  return Status::OK();
}

NMSWriter::NMSWriter(EntityMetricsMap* table_metrics,
                     MetricsMap* server_metrics,
                     const MetricPrometheusOptions& opts)
    : PrometheusWriter(nullptr, opts), table_metrics_(*table_metrics),
    server_metrics_(*server_metrics) {}

Status NMSWriter::FlushSingleEntry(
    const MetricEntity::AttributeMap& attr, const std::string& name, const int64_t value) {
  auto it = attr.find("table_id");
  if (it != attr.end()) {
    table_metrics_[it->second][name] = value;
    return Status::OK();
  }
  it = attr.find("metric_type");
  if (it == attr.end() || it->second != "server") {
    return Status::OK();
  }
  server_metrics_[name] = value;
  return Status::OK();
}

} // namespace yb
