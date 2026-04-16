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

#include "yb/util/metrics_aggregator.h"
#include "yb/util/prometheus_metric_filter.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(ExportHelpAndType);

class PrometheusWriter {
 public:
  PrometheusWriter(
      std::stringstream* output,
      const MetricPrometheusOptions& opts);

  virtual ~PrometheusWriter();

  // Write to the a single scrape-time metric entry for non-table level metrics.
  template <typename T>
  Status WriteSingleEntryNonTable(
      const MetricEntity::AttributeMap& attributes, const std::string& name, const T& value,
      const char* type = "unknown", const char* help = "unknown") {
    auto it = attributes.find("table_id");
    if (it != attributes.end()) {
      return STATUS(
          InvalidArgument, "Expect no table_id in attr argument when calling this function.");
    }
    FlushHelpAndTypeIfRequested(name, help, type);
    RETURN_NOT_OK(FlushSingleEntry(attributes, name, value));
    return Status::OK();
  }

  // Write or aggregate a single scrape-time metric.
  Status WriteSingleEntry(
      const MetricEntity::AttributeMap& attributes,
      const std::string& name,
      int64_t value,
      AggregationFunction aggregation_function,
      AggregationLevels default_aggregation_levels,
      const std::string& metric_entity_type,
      const char* type = "unknown",
      const char* description = "unknown",
      std::shared_ptr<MetricPrototype> metric_prototype_holder = nullptr);

  // This function performs the following steps:
  // 1. Flush pre-aggregated metrics from metrics_aggregator to the output stream.
  // 2. Flush scrape-time aggregated metrics to the output stream.
  // 3. Finally, flush a fake metric that indicates the number of entries cut off,
  //    and this metric is exposed regardless of the value of remaining_allowed_entries_.
  Status Finish(const MetricsAggregator& metrics_aggregator);

  PrometheusMetricFilter* TEST_GetPrometheusMetricFilter() const {
    return prometheus_metric_filter_.get();
  }

  uint32_t TEST_GetNumberOfEntriesCutOff() const {
    return num_of_entries_cut_off_;
  }

  std::optional<MetricEntity::AttributeMap> TEST_GetAttributesForAggregationId(
      std::string metric_entity_type, std::string aggregation_id);

  std::optional<int64_t> TEST_GetScrapeTimeAggregatedValue(
      std::string metric_name, std::string aggregation_id);

 private:
  class ScrapeTimeAggregatedMetricInfo {
   public:
    ScrapeTimeAggregatedMetricInfo(
        std::string metric_entity_type,
        MetricHelpAndType help_and_type,
        std::shared_ptr<MetricPrototype> metric_prototype_holder)
        : metric_entity_type_(std::move(metric_entity_type)),
          help_and_type_(help_and_type),
          metric_prototype_holder_(std::move(metric_prototype_holder)) {}

    const std::string metric_entity_type_;
    const MetricHelpAndType help_and_type_;
    // Aggregated values for different aggregation_ids(table id or stream id)
    std::unordered_map<std::string, int64_t> scrape_time_aggregated_values_;
    const std::shared_ptr<MetricPrototype> metric_prototype_holder_;
  };

  friend class MetricsTest;

  // Flush pre-aggregated values to the output stream.
  Status FlushPreAggregatedValues(const MetricsAggregator& metrics_aggregator);

  // Flush scrape-time aggregated values to the output stream.
  Status FlushScrapeTimeAggregatedValues();

  // FlushSingleEntry() was a function template with type of "value" as template
  // var T. To allow NMSWriter to override FlushSingleEntry(), the type of "value"
  // has been instantiated to int64_t.
  virtual Status FlushSingleEntry(
      const MetricEntity::AttributeMap& attributes, const std::string& name, int64_t value);

  void FlushHelpAndTypeIfRequested(
      const std::string& name, const char* description, const char* type);

  void InvalidAggregationFunction(AggregationFunction aggregation_function);

  void AddScrapeTimeAggregatedEntry(const std::string& key,
                          const char* type, const char* description,
                          const MetricEntity::AttributeMap& attributes,
                          const std::string& name, int64_t value,
                          AggregationFunction aggregation_function,
                          const std::string& metric_entity_type,
                          std::shared_ptr<MetricPrototype> metric_prototype_holder);

  using AggregationIdToAttributeMap = std::unordered_map<std::string, MetricEntity::AttributeMap>;

  using AttributesByMetricEntityTypeAndAggregationId =
      std::unordered_map<std::string, AggregationIdToAttributeMap>;

  AttributesByMetricEntityTypeAndAggregationId
      attributes_by_metric_entity_type_and_aggregation_id_;

  std::unordered_map<std::string, ScrapeTimeAggregatedMetricInfo>
      scrape_time_aggregated_metric_info_by_metric_name_;

  // Output stream
  std::stringstream* output_;
  // Timestamp for all metrics belonging to this writer instance.
  int64_t timestamp_;

  ExportHelpAndType export_help_and_type_ = ExportHelpAndType::kFalse;

  std::unique_ptr<PrometheusMetricFilter> prometheus_metric_filter_;

  uint32_t remaining_allowed_entries_;

  uint32_t num_of_entries_cut_off_ = 0;
};

// Native Metrics Storage Writer - writes prometheus metrics into system table.
class NMSWriter : public PrometheusWriter {
 public:
  typedef std::unordered_map<std::string, int64_t> MetricsMap;
  typedef std::unordered_map<std::string, MetricsMap> EntityMetricsMap;

  explicit NMSWriter(
      EntityMetricsMap* table_metrics,
      MetricsMap* server_metrics,
      const MetricPrometheusOptions& opts);

 private:
  Status FlushSingleEntry(
      const MetricEntity::AttributeMap& attributes,
      const std::string& name,
      int64_t value) override;
  // Output Map from table_id to map of metric_name to value
  EntityMetricsMap& table_metrics_;
  // Map from metric_name to value
  MetricsMap& server_metrics_;
};

} // namespace yb
