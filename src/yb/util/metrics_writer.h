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

#ifndef YB_UTIL_METRICS_WRITER_H
#define YB_UTIL_METRICS_WRITER_H

#include <map>

#include "yb/util/metric_entity.h"

namespace yb {

class PrometheusWriter {
 public:
  explicit PrometheusWriter(std::stringstream* output);

  virtual ~PrometheusWriter();

  template<typename T>
  CHECKED_STATUS WriteSingleEntry(
      const MetricEntity::AttributeMap& attr, const std::string& name, const T& value,
      AggregationFunction aggregation_function) {
    auto it = attr.find("table_id");
    if (it != attr.end()) {
      // For tablet level metrics, we roll up on the table level.
      if (per_table_attributes_.find(it->second) == per_table_attributes_.end()) {
        // If it's the first time we see this table, create the aggregate structures.
        per_table_attributes_[it->second] = attr;
        per_table_values_[it->second][name] = value;
      } else {
        switch (aggregation_function) {
          case kSum:
            per_table_values_[it->second][name] += value;
            break;
          case kMax:
            // If we have a new max, also update the metadata so that it matches correctly.
            if (static_cast<double>(value) > per_table_values_[it->second][name]) {
              per_table_attributes_[it->second] = attr;
              per_table_values_[it->second][name] = value;
            }
            break;
          default:
            InvalidAggregationFunction(aggregation_function);
            break;
        }
      }
    } else {
      // For non-tablet level metrics, export them directly.
      RETURN_NOT_OK(FlushSingleEntry(attr, name, value));
    }
    return Status::OK();
  }

  CHECKED_STATUS FlushAggregatedValues(const uint32_t& max_tables_metrics_breakdowns,
                                       std::string priority_regex);

 private:
  friend class MetricsTest;
  // FlushSingleEntry() was a function template with type of "value" as template
  // var T. To allow NMSWriter to override FlushSingleEntry(), the type of "value"
  // has been instantiated to int64_t.
  virtual CHECKED_STATUS FlushSingleEntry(const MetricEntity::AttributeMap& attr,
                                          const std::string& name, const int64_t& value);

  void InvalidAggregationFunction(AggregationFunction aggregation_function);

  // Map from table_id to attributes
  std::map<std::string, MetricEntity::AttributeMap> per_table_attributes_;
  // Map from table_id to map of metric_name to value
  std::map<std::string, std::map<std::string, double>> per_table_values_;
  // Output stream
  std::stringstream* output_;
  // Timestamp for all metrics belonging to this writer instance.
  int64_t timestamp_;
};

// Native Metrics Storage Writer - writes prometheus metrics into system table.
class NMSWriter : public PrometheusWriter {
 public:
  typedef std::unordered_map<std::string, int64_t> MetricsMap;
  typedef std::unordered_map<std::string, MetricsMap> EntityMetricsMap;

  explicit NMSWriter(EntityMetricsMap* table_metrics, MetricsMap* server_metrics);

 private:
  CHECKED_STATUS FlushSingleEntry(
      const MetricEntity::AttributeMap& attr, const std::string& name,
      const int64_t& value) override;

  // Output
  // Map from table_id to map of metric_name to value
  EntityMetricsMap* table_metrics_;
  // Map from metric_name to value
  MetricsMap* server_metrics_;
};

} // namespace yb

#endif // YB_UTIL_METRICS_WRITER_H
