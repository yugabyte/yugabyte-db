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

#include <unordered_map>

#include "yb/gutil/ref_counted.h"

namespace yb {

class AtomicMillisLag;
class EventStats;
class EventStatsPrototype;
class Counter;
class CounterPrototype;
class Gauge;
class Histogram;
class HistogramPrototype;
class HistogramSnapshotPB;
class HdrHistogram;
class Metric;
class MetricEntity;
class MetricEntityPrototype;
class MetricPrototype;
class MetricRegistry;
class MillisLag;
class MillisLagPrototype;
class NMSWriter;
class MetricsAggregator;
class PrometheusWriter;
class StatsOnlyHistogram;

struct MetricJsonOptions;
struct MetricPrometheusOptions;

using CounterPtr = scoped_refptr<Counter>;
using EventStatsPtr = scoped_refptr<EventStats>;
using MetricEntityPtr = scoped_refptr<MetricEntity>;
using MetricAttributeMap = std::unordered_map<std::string, std::string>;

template<typename T>
class AtomicGauge;
template<typename T>
class FunctionGauge;
template<typename T>
class GaugePrototype;

} // namespace yb
