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
#pragma once

#include "yb/gutil/ref_counted.h"

namespace yb {

class Counter;
template <class T>
class AtomicGauge;
class Histogram;
class MetricEntity;

namespace xcluster {

// Container for all metrics pertaining to the entire cluster.
class XClusterConsumerClusterMetrics {
 public:
  explicit XClusterConsumerClusterMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  // Safe time lag is current time - min (current) safe time.
  scoped_refptr<AtomicGauge<uint64_t>> consumer_safe_time_lag;
  // Skew is max safe time - min (current) safe time.
  scoped_refptr<AtomicGauge<uint64_t>> consumer_safe_time_skew;

 private:
  scoped_refptr<MetricEntity> entity_;
};

}  // namespace xcluster
}  // namespace yb
