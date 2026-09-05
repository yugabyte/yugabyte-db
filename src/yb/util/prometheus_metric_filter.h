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

#include "yb/util/metric_entity.h"

namespace yb {

class PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilter(const MetricPrometheusOptions& opts);

  virtual AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_aggregation_levels) = 0;

  // Returns whether an entry with the given attributes passes the active-table list, if one was
  // supplied. Entries without a table id, and entries of stream-level entities, always pass.
  bool ShouldExportTableMetrics(
      const MetricEntity::AttributeMap& attributes, const std::string& metric_entity_type) const;

  virtual std::string Version() const = 0;

  virtual ~PrometheusMetricFilter() = default;

  MetricAggregationMap* TEST_GetAggregationMap() {
    return &metric_filter_;
  }

 protected:
  MetricAggregationMap metric_filter_;

 private:
  const std::shared_ptr<const std::unordered_set<std::string>> active_table_ids_;
};

std::unique_ptr<PrometheusMetricFilter> CreatePrometheusMetricFilter(
    const MetricPrometheusOptions& opts);

} // namespace yb
