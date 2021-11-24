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

#include <regex>

#include "yb/util/enums.h"

namespace yb {

PrometheusWriter::PrometheusWriter(std::stringstream* output)
    : output_(output),
      timestamp_(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count()) {}

PrometheusWriter::~PrometheusWriter() {}

Status PrometheusWriter::FlushAggregatedValues(const uint32_t& max_tables_metrics_breakdowns,
                                               std::string priority_regex) {
  uint32_t counter = 0;
  const auto& p_regex = std::regex(priority_regex);
  for (const auto& entry : per_table_values_) {
    const auto& attrs = per_table_attributes_[entry.first];
    for (const auto& metric_entry : entry.second) {
      if (counter < max_tables_metrics_breakdowns ||
          std::regex_match(metric_entry.first, p_regex)) {
        RETURN_NOT_OK(FlushSingleEntry(attrs, metric_entry.first, metric_entry.second));
      }
    }
    counter += 1;
  }
  return Status::OK();
}

Status PrometheusWriter::FlushSingleEntry(const MetricEntity::AttributeMap& attr,
    const std::string& name, const int64_t& value) {
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

NMSWriter::NMSWriter(EntityMetricsMap* table_metrics, MetricsMap* server_metrics)
    : PrometheusWriter(nullptr), table_metrics_(table_metrics),
      server_metrics_(server_metrics) {}

Status NMSWriter::FlushSingleEntry(
    const MetricEntity::AttributeMap& attr, const std::string& name,
    const int64_t& value) {
  auto it = attr.find("metric_type");
  if (it == attr.end()) {
    // ignore.
  } else if (it->second == "server") {
    (*server_metrics_)[name] = (int64_t)value;
  } else if (it->second == "tablet") {
    auto it2 = attr.find("table_id");
    if (it2 == attr.end()) {
      // ignore.
    } else {
      (*table_metrics_)[it2->second][name] = (int64_t)value;
    }
  }
  return Status::OK();
}

} // namespace yb
