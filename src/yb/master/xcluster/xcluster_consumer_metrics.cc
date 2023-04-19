// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/master/xcluster/xcluster_consumer_metrics.h"

#include "yb/util/metrics.h"
#include "yb/util/trace.h"

// Cluster wide metrics
METRIC_DEFINE_gauge_uint64(
    cluster, consumer_safe_time_lag, "XCluster consumer lag between current time and safe time",
    yb::MetricUnit::kMilliseconds,
    "Lag in ms of xCluster consumer's safe time compared to the current time. Can indicate if "
    "there are namespace wide replication issues.");

METRIC_DEFINE_gauge_uint64(
    cluster, consumer_safe_time_skew, "XCluster consumer safe time skew",
    yb::MetricUnit::kMilliseconds,
    "Difference in ms between the max xCluster safe time and min safe time within a namespace. Can "
    "indicate if there is an issue with a single replication stream.");

namespace yb {
namespace xcluster {

#define MINIT(x) x(METRIC_##x.Instantiate(entity))
#define GINIT(x) x(METRIC_##x.Instantiate(entity, 0))

XClusterConsumerClusterMetrics::XClusterConsumerClusterMetrics(
    const scoped_refptr<MetricEntity>& entity)
    : GINIT(consumer_safe_time_lag),
      GINIT(consumer_safe_time_skew),
      entity_(entity) {}

#undef MINIT
#undef GINIT

}  // namespace xcluster
}  // namespace yb
