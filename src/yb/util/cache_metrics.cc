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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/cache_metrics.h"

#include "yb/util/metrics.h"

METRIC_DEFINE_gauge_uint64(server, block_cache_usage, "Block Cache Memory Usage",
                           yb::MetricUnit::kBytes,
                           "Memory consumed by the block cache");
METRIC_DEFINE_gauge_uint64(server, block_cache_single_touch_usage,
                           "Single Touch Block Cache Memory Usage",
                           yb::MetricUnit::kBytes,
                           "Memory consumed by the single touch block cache");
METRIC_DEFINE_gauge_uint64(server, block_cache_multi_touch_usage,
                           "Multi Cache Block Cache Memory Usage",
                           yb::MetricUnit::kBytes,
                           "Memory consumed by the multi cache block cache");
namespace yb {

#define GINIT(member, x) member(METRIC_##x.Instantiate(entity, 0))
CacheMetrics::CacheMetrics(const scoped_refptr<MetricEntity>& entity)
  : GINIT(cache_usage, block_cache_usage),
    GINIT(single_touch_cache_usage, block_cache_single_touch_usage),
    GINIT(multi_touch_cache_usage, block_cache_multi_touch_usage) {
}
#undef GINIT

} // namespace yb
