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

#include "yb/util/prometheus_metric_filter.h"

#include <boost/regex.hpp>

namespace yb {

namespace {

class PrometheusMetricFilterV1 : public PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilterV1(const MetricPrometheusOptions& opts);

  AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_aggregation_levels) override;

  std::string Version() const override;

 private:
  bool ShouldCollectMetric(const std::string& metric_name) const;

  const boost::regex priority_regex_;

  const std::optional<std::vector<std::string>> general_metrics_allowlist_;
};

PrometheusMetricFilterV1::PrometheusMetricFilterV1(const MetricPrometheusOptions& opts)
    : priority_regex_(opts.priority_regex_string),
      general_metrics_allowlist_(opts.general_metrics_allowlist) {}

bool PrometheusMetricFilterV1::ShouldCollectMetric(const std::string& metric_name) const {
  if (!general_metrics_allowlist_) {
    // Metric is scraped if the general_metrics_allowlist_ is not provided.
    return true;
  }

  for (const auto& required_metric_substring : *general_metrics_allowlist_) {
    if (metric_name.find(required_metric_substring) != std::string::npos) {
      // Required metric substring is found in the metric name.
      return true;
    }
  }

  return false;
}

AggregationLevels PrometheusMetricFilterV1::GetAggregationLevels(
    const std::string& metric_name, AggregationLevels default_aggregation_levels) {
  auto [levels_from_regex_it, inserted] = metric_filter_.try_emplace(metric_name, kNoLevel);
  auto& levels_from_regex = levels_from_regex_it->second;

  if (inserted && ShouldCollectMetric(metric_name)) {
    // No filter for stream and server level.
    levels_from_regex = kStreamLevel | kServerLevel;
    if (boost::regex_match(metric_name, priority_regex_)) {
      levels_from_regex |= kTableLevel;
    }
  }

  auto aggregation_level = levels_from_regex & default_aggregation_levels;
  // If a metric will be exposed at the table level, it cannot be exposed at the server level.
  if (aggregation_level & kTableLevel) {
    aggregation_level &= ~kServerLevel;
  }

  return aggregation_level;
}

std::string PrometheusMetricFilterV1::Version() const {
  return kFilterVersionOne;
}

class PrometheusMetricFilterV2 : public PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilterV2(const MetricPrometheusOptions& opts);

  AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_aggregation_levels) override;

  std::string Version() const override;

 private:
  const boost::regex table_allowlist_;
  const boost::regex table_blocklist_;
  const boost::regex server_allowlist_;
  const boost::regex server_blocklist_;
};

PrometheusMetricFilterV2::PrometheusMetricFilterV2(const MetricPrometheusOptions& opts)
    : table_allowlist_(opts.table_allowlist_string),
      table_blocklist_(opts.table_blocklist_string),
      server_allowlist_(opts.server_allowlist_string),
      server_blocklist_(opts.server_blocklist_string) {}

AggregationLevels PrometheusMetricFilterV2::GetAggregationLevels(
    const std::string& metric_name, AggregationLevels default_aggregation_levels) {
  auto [levels_from_regex_it, inserted] = metric_filter_.try_emplace(metric_name, kNoLevel);
  auto& levels_from_regex = levels_from_regex_it->second;

  if (inserted) {
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

  return levels_from_regex & default_aggregation_levels;
}

std::string PrometheusMetricFilterV2::Version() const {
  return kFilterVersionTwo;
}

} // namespace

std::unique_ptr<PrometheusMetricFilter> CreatePrometheusMetricFilter(
    const MetricPrometheusOptions& opts) {
  DCHECK(opts.version == kFilterVersionOne || opts.version == kFilterVersionTwo);
  if (opts.version == kFilterVersionTwo) {
    return std::make_unique<PrometheusMetricFilterV2>(opts);
  }
  return std::make_unique<PrometheusMetricFilterV1>(opts);
}

}  // namespace yb
