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
  explicit PrometheusMetricFilter(const std::string& priority_regex_string);

  AggregationLevels GetAggregationLevels(const std::string& metric_name);

  MetricAggregationMap* TEST_GetAggregationMap() {
    return &metric_filter_;
  }

 private:
  bool MatchMetricAgainstRegex(const std::string& metric_name,
                               const boost::regex& regex) const;

  boost::regex priority_regex_;

  MetricAggregationMap metric_filter_;
};

} // namespace yb
