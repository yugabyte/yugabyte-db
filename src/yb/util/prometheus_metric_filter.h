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

#include "yb/util/metric_entity.h"

namespace yb {

class PrometheusMetricFilter {
 public:
  virtual AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_levels) = 0;

  virtual std::string Version() const = 0;

  virtual ~PrometheusMetricFilter() = default;

  MetricAggregationMap* TEST_GetAggregationMap() {
    return &metric_filter_;
  }

 protected:
  MetricAggregationMap metric_filter_;
};


class PrometheusMetricFilterV1 : public PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilterV1(const MetricPrometheusOptions& opts);

  AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_levels) override;

  std::string Version() const override;

 private:
  const boost::regex priority_regex_;
};


class PrometheusMetricFilterV2 : public PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilterV2(const MetricPrometheusOptions& opts);

  AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_levels) override;

  std::string Version() const override;

 private:
  const boost::regex table_allowlist_;
  const boost::regex table_blocklist_;
  const boost::regex server_allowlist_;
  const boost::regex server_blocklist_;
};

std::unique_ptr<PrometheusMetricFilter> CreatePrometheusMetricFilter(
    const MetricPrometheusOptions& opts);

} // namespace yb
