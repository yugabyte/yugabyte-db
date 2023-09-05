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

#include <map>

#include "yb/util/metric_entity.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(ExportHelpAndType);

class PrometheusWriter {
 public:
  struct MetricHelpAndType {
    const char* type;
    const char* help;
  };

  explicit PrometheusWriter(
      std::stringstream* output,
      ExportHelpAndType export_help_and_type);

  virtual ~PrometheusWriter();

  // Write to the a single metric entry for non-table level metrics.
  template <typename T>
  Status WriteSingleEntryNonTable(
      const MetricEntity::AttributeMap& attr, const std::string& name, const T& value) {
    auto it = attr.find("table_id");
    if (it != attr.end()) {
      return STATUS(
          InvalidArgument, "Expect no table_id in attr argument when calling this function.");
    }

    RETURN_NOT_OK(FlushSingleEntry(attr, name, value));
    return Status::OK();
  }

  Status WriteSingleEntry(
      const MetricEntity::AttributeMap& attr,
      const std::string& name,
      int64_t value,
      AggregationFunction aggregation_function,
      const char* type = "unknown",
      const char* description = "unknown",
      const AggregationLevels aggregation_levels = kServerLevel | kTableLevel);

  Status FlushAggregatedValues();

 private:
  friend class MetricsTest;
  // FlushSingleEntry() was a function template with type of "value" as template
  // var T. To allow NMSWriter to override FlushSingleEntry(), the type of "value"
  // has been instantiated to int64_t.
  virtual Status FlushSingleEntry(
      const MetricEntity::AttributeMap& attr, const std::string& name, int64_t value);

  void FlushHelpAndType(
      const std::string& name, const char* type, const char* description);

  void InvalidAggregationFunction(AggregationFunction aggregation_function);

  void AddAggregatedEntry(const std::string& key,
                          const MetricEntity::AttributeMap& attr,
                          const std::string& name, int64_t value,
                          AggregationFunction aggregation_function,
                          const char* type, const char* description);

  // Map metric name to type and description.
  std::unordered_map<std::string, MetricHelpAndType> metric_help_and_type_;
  // Map entity id to values
  using EntityValues = std::unordered_map<std::string, int64_t>;
  // Map entity id to attributes
  using EntityAtrributes = std::unordered_map<std::string, MetricEntity::AttributeMap>;
  // Map from metric name to EntityValues
  std::unordered_map<std::string, EntityValues> aggregated_values_;
  EntityAtrributes aggregated_attributes_;

  // Output stream
  std::stringstream* output_;
  // Timestamp for all metrics belonging to this writer instance.
  int64_t timestamp_;

  ExportHelpAndType export_help_and_type_;
};

// Native Metrics Storage Writer - writes prometheus metrics into system table.
class NMSWriter : public PrometheusWriter {
 public:
  typedef std::unordered_map<std::string, int64_t> MetricsMap;
  typedef std::unordered_map<std::string, MetricsMap> EntityMetricsMap;

  explicit NMSWriter(EntityMetricsMap* table_metrics, MetricsMap* server_metrics);

 private:
  Status FlushSingleEntry(
      const MetricEntity::AttributeMap& attr, const std::string& name, int64_t value) override;

  // Output
  // Map from table_id to map of metric_name to value
  EntityMetricsMap& table_metrics_;
  // Map from metric_name to value
  MetricsMap& server_metrics_;
};

} // namespace yb
