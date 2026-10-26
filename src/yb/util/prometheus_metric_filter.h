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

#include "yb/util/enums.h"
#include "yb/util/metric_entity.h"

namespace yb {

class PrometheusMetricFilter {
 public:
  virtual AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_aggregation_levels) = 0;

  // Returns whether a table-level entry of metric_name with the given attributes passes the
  // active-table list, if one was supplied. Only filter versions that also export a server-level
  // copy of table-level metrics apply the list, so by default every entry passes.
  virtual bool ShouldExportTableMetrics(
      const std::string& metric_name, const MetricEntity::AttributeMap& attributes,
      const std::string& metric_entity_type) {
    return true;
  }

  virtual std::string Version() const = 0;

  virtual ~PrometheusMetricFilter() = default;

  MetricAggregationMap* TEST_GetAggregationMap() {
    return &metric_filter_;
  }

 protected:
  MetricAggregationMap metric_filter_;
};

// Why a table keeps all of its table-level metrics whatever the active-table list says.
YB_DEFINE_ENUM(ActiveTableListExemption,
    // Not exempt: the active-table list decides.
    (kNone)
    // Only YSQL tables are filtered.
    (kNotYsqlTable)
    // A YSQL table created by YugabyteDB itself.
    (kInternalTable));

// Returns the exemption that applies to a table with the given metric attributes.
ActiveTableListExemption GetActiveTableListExemption(const MetricEntity::AttributeMap& attributes);

std::unique_ptr<PrometheusMetricFilter> CreatePrometheusMetricFilter(
    const MetricPrometheusOptions& opts);

} // namespace yb
