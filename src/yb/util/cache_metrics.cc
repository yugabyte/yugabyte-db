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

#include "yb/util/cache_metrics.h"

#include "yb/util/metrics.h"

METRIC_DEFINE_counter(server, block_cache_inserts,
                      "Block Cache Inserts", yb::MetricUnit::kBlocks,
                      "Number of blocks inserted in the cache");
METRIC_DEFINE_counter(server, block_cache_lookups,
                      "Block Cache Lookups", yb::MetricUnit::kBlocks,
                      "Number of blocks looked up from the cache");
METRIC_DEFINE_counter(server, block_cache_evictions,
                      "Block Cache Evictions", yb::MetricUnit::kBlocks,
                      "Number of blocks evicted from the cache");
METRIC_DEFINE_counter(server, block_cache_misses,
                      "Block Cache Misses", yb::MetricUnit::kBlocks,
                      "Number of lookups that didn't yield a block");
METRIC_DEFINE_counter(server, block_cache_misses_caching,
                      "Block Cache Misses (Caching)", yb::MetricUnit::kBlocks,
                      "Number of lookups that were expecting a block that didn't yield one."
                      "Use this number instead of cache_misses when trying to determine how "
                      "efficient the cache is");
METRIC_DEFINE_counter(server, block_cache_hits,
                      "Block Cache Hits", yb::MetricUnit::kBlocks,
                      "Number of lookups that found a block");
METRIC_DEFINE_counter(server, block_cache_hits_caching,
                      "Block Cache Hits (Caching)", yb::MetricUnit::kBlocks,
                      "Number of lookups that were expecting a block that found one."
                      "Use this number instead of cache_hits when trying to determine how "
                      "efficient the cache is");

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

#define MINIT(member, x) member(METRIC_##x.Instantiate(entity))
#define GINIT(member, x) member(METRIC_##x.Instantiate(entity, 0))
CacheMetrics::CacheMetrics(const scoped_refptr<MetricEntity>& entity)
  : MINIT(inserts, block_cache_inserts),
    MINIT(lookups, block_cache_lookups),
    MINIT(evictions, block_cache_evictions),
    MINIT(cache_hits, block_cache_hits),
    MINIT(cache_hits_caching, block_cache_hits_caching),
    MINIT(cache_misses, block_cache_misses),
    MINIT(cache_misses_caching, block_cache_misses_caching),
    GINIT(cache_usage, block_cache_usage),
    GINIT(single_touch_cache_usage, block_cache_single_touch_usage),
    GINIT(multi_touch_cache_usage, block_cache_multi_touch_usage) {
}
#undef MINIT
#undef GINIT

} // namespace yb
