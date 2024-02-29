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

#include "yb/util/prometheus_metric_filter.h"

namespace yb {

PrometheusMetricFilterV1::PrometheusMetricFilterV1(const MetricPrometheusOptions& opts)
    : priority_regex_(opts.priority_regex_string) {}

AggregationLevels PrometheusMetricFilterV1::GetAggregationLevels(
    const std::string& metric_name, AggregationLevels default_levels) {
  auto& levels_from_regex = metric_filter_[metric_name];

  if (!levels_from_regex) {
    // No filter for stream and server level.
    levels_from_regex = kStreamLevel | kServerLevel;

    if (boost::regex_match(metric_name, priority_regex_)) {
      levels_from_regex |= kTableLevel;
    }
  }

  auto aggregation_level = levels_from_regex & default_levels;
  // If a metric will be exposed at the table level, it cannot be exposed at the server level.
  if (aggregation_level & kTableLevel) {
    aggregation_level &= ~kServerLevel;
  }

  return aggregation_level;
}

std::string PrometheusMetricFilterV1::Version() const {
  return kFilterVersionOne;
}


PrometheusMetricFilterV2::PrometheusMetricFilterV2(const MetricPrometheusOptions& opts)
    : table_allowlist_(opts.table_allowlist_string),
      table_blocklist_(opts.table_blocklist_string),
      server_allowlist_(opts.server_allowlist_string),
      server_blocklist_(opts.server_blocklist_string) {}

AggregationLevels PrometheusMetricFilterV2::GetAggregationLevels(
    const std::string& metric_name, AggregationLevels default_levels) {
  auto& levels_from_regex = metric_filter_[metric_name];

  if (!levels_from_regex) {
    // No filter for stream level.
    levels_from_regex = kStreamLevel;

    if (!boost::regex_match(metric_name, table_blocklist_) &&
        boost::regex_match(metric_name, table_allowlist_)) {
      levels_from_regex |= kTableLevel;
    }
    if (!boost::regex_match(metric_name, server_blocklist_) &&
        boost::regex_match(metric_name, server_allowlist_)) {
      levels_from_regex |= kServerLevel;
    }
  }

  return levels_from_regex & default_levels;
}

std::string PrometheusMetricFilterV2::Version() const {
  return kFilterVersionTwo;
}

std::unique_ptr<PrometheusMetricFilter> CreatePrometheusMetricFilter(
    const MetricPrometheusOptions& opts) {
  DCHECK(opts.version == kFilterVersionOne || opts.version == kFilterVersionTwo);
  if (opts.version == kFilterVersionTwo) {
    return std::make_unique<PrometheusMetricFilterV2>(opts);
  }
  return std::make_unique<PrometheusMetricFilterV1>(opts);
}

}  // namespace yb
