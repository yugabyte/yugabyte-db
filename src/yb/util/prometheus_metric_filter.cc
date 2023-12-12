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

PrometheusMetricFilter::PrometheusMetricFilter(const std::string& priority_regex_string) {
  priority_regex_ = priority_regex_string;
}

bool PrometheusMetricFilter::MatchMetricAgainstRegex(const std::string& metric_name,
                                                     const boost::regex& regex) const {
  return boost::regex_match(metric_name, regex);
}

AggregationLevels PrometheusMetricFilter::GetAggregationLevels(const std::string& metric_name) {
  auto& aggregation_levels = metric_filter_[metric_name];
  if (aggregation_levels) {
    return aggregation_levels;
  }
  // Need to do regex matching to figure out and store the result in metric_filter.
  // No filter for stream and server metrics currently.
  AggregationLevels filter_result = kStreamLevel | kServerLevel;
  // Check table level.
  if (MatchMetricAgainstRegex(metric_name, priority_regex_)) {
    filter_result |= kTableLevel;
  }
  aggregation_levels = filter_result;
  return filter_result;
}

}  // namespace yb
