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
#include "kudu/server/tcmalloc_metrics.h"

#include <boost/bind.hpp>
#include <glog/logging.h>
#include <gperftools/malloc_extension.h>

#include "kudu/util/metrics.h"

#ifndef TCMALLOC_ENABLED
#define TCM_ASAN_MSG " (Disabled - no tcmalloc in this build)"
#else
#define TCM_ASAN_MSG
#endif

// As of this writing, we expose all of the un-deprecated tcmalloc status metrics listed at:
// http://gperftools.googlecode.com/svn/trunk/doc/tcmalloc.html

METRIC_DEFINE_gauge_uint64(server, generic_current_allocated_bytes,
    "Heap Memory Usage", kudu::MetricUnit::kBytes,
    "Number of bytes used by the application. This will not typically match the memory "
    "use reported by the OS, because it does not include TCMalloc overhead or memory "
    "fragmentation." TCM_ASAN_MSG);

METRIC_DEFINE_gauge_uint64(server, generic_heap_size,
    "Reserved Heap Memory", kudu::MetricUnit::kBytes,
    "Bytes of system memory reserved by TCMalloc." TCM_ASAN_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_pageheap_free_bytes,
    "Free Heap Memory", kudu::MetricUnit::kBytes,
    "Number of bytes in free, mapped pages in page heap. These bytes can be used to "
    "fulfill allocation requests. They always count towards virtual memory usage, and "
    "unless the underlying memory is swapped out by the OS, they also count towards "
    "physical memory usage." TCM_ASAN_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_pageheap_unmapped_bytes,
    "Unmapped Heap Memory", kudu::MetricUnit::kBytes,
    "Number of bytes in free, unmapped pages in page heap. These are bytes that have "
    "been released back to the OS, possibly by one of the MallocExtension \"Release\" "
    "calls. They can be used to fulfill allocation requests, but typically incur a page "
    "fault. They always count towards virtual memory usage, and depending on the OS, "
    "typically do not count towards physical memory usage." TCM_ASAN_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_max_total_thread_cache_bytes,
    "Thread Cache Memory Limit", kudu::MetricUnit::kBytes,
    "A limit to how much memory TCMalloc dedicates for small objects. Higher numbers "
    "trade off more memory use for -- in some situations -- improved efficiency." TCM_ASAN_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_current_total_thread_cache_bytes,
    "Thread Cache Memory Usage", kudu::MetricUnit::kBytes,
    "A measure of some of the memory TCMalloc is using (for small objects)." TCM_ASAN_MSG);

#undef TCM_ASAN_MSG

namespace kudu {
namespace tcmalloc {

static uint64_t GetTCMallocPropValue(const char* prop) {
  size_t value = 0;
#ifdef TCMALLOC_ENABLED
  if (!MallocExtension::instance()->GetNumericProperty(prop, &value)) {
    LOG(DFATAL) << "Failed to get value of numeric tcmalloc property: " << prop;
  }
#endif
  return value;
}

void RegisterMetrics(const scoped_refptr<MetricEntity>& entity) {
  entity->NeverRetire(
      METRIC_generic_current_allocated_bytes.InstantiateFunctionGauge(
          entity, Bind(GetTCMallocPropValue, Unretained("generic.current_allocated_bytes"))));
  entity->NeverRetire(
      METRIC_generic_heap_size.InstantiateFunctionGauge(
          entity, Bind(GetTCMallocPropValue, Unretained("generic.heap_size"))));
  entity->NeverRetire(
      METRIC_tcmalloc_pageheap_free_bytes.InstantiateFunctionGauge(
          entity, Bind(GetTCMallocPropValue, Unretained("tcmalloc.pageheap_free_bytes"))));
  entity->NeverRetire(
      METRIC_tcmalloc_pageheap_unmapped_bytes.InstantiateFunctionGauge(
          entity, Bind(GetTCMallocPropValue, Unretained("tcmalloc.pageheap_unmapped_bytes"))));
  entity->NeverRetire(
      METRIC_tcmalloc_max_total_thread_cache_bytes.InstantiateFunctionGauge(
          entity, Bind(GetTCMallocPropValue, Unretained("tcmalloc.max_total_thread_cache_bytes"))));
  entity->NeverRetire(
      METRIC_tcmalloc_current_total_thread_cache_bytes.InstantiateFunctionGauge(
          entity, Bind(GetTCMallocPropValue,
                       Unretained("tcmalloc.current_total_thread_cache_bytes"))));
}

} // namespace tcmalloc
} // namespace kudu
