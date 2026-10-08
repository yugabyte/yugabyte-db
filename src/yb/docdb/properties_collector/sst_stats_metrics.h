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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "yb/docdb/docdb_fwd.h"

#include "yb/gutil/ref_counted.h"
#include "yb/util/metrics_fwd.h"

namespace yb::docdb {

// The docdb_sst_* tablet gauges over one tablet's SstStatsAggregate. What is exported and why:
// properties_collector/README.md, "Prometheus gauges".
//
// The gauges are pulled on scrape. This object holds the aggregator alive and detaches the
// callbacks when destroyed, so a scrape concurrent with tablet shutdown cannot read freed state.
class SstStatsMetrics {
 public:
  SstStatsMetrics(
      const MetricEntityPtr& entity, std::shared_ptr<const SstStatsAggregator> aggregator);
  ~SstStatsMetrics();

 private:
  struct MetricInfo;
  struct MetricInfos;

  uint64_t CalculateMetric(const MetricInfo& metric) const;

  const MetricEntityPtr entity_;
  const std::shared_ptr<const SstStatsAggregator> aggregator_;
  // The gauges this instance put in the entity, parallel to MetricInfos::kMetricInfos. The entity
  // keys gauges by prototype and outlives any one instance, so a second instance on the same
  // entity replaces these; the destructor compares before removing so it cannot strip the
  // replacements.
  std::vector<scoped_refptr<FunctionGauge<uint64_t>>> gauges_;

  // Declared last so it is destroyed first, freezing the gauges while aggregator_ is still alive.
  std::shared_ptr<void> metric_detacher_;
};

}  // namespace yb::docdb
