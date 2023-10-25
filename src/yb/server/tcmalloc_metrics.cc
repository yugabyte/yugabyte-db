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
#include "yb/server/tcmalloc_metrics.h"

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/util/logging.h"

#include "yb/gutil/bind.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/tcmalloc_util.h"
#include "yb/util/logging.h"

#if YB_TCMALLOC_ENABLED
#define TCMALLOC_DISABLED_MSG
#else
#define TCMALLOC_DISABLED_MSG " (Disabled - no tcmalloc in this build)"
#endif

// As of this writing, we expose all of the un-deprecated tcmalloc status metrics listed at:
// http://gperftools.googlecode.com/svn/trunk/doc/tcmalloc.html

METRIC_DEFINE_gauge_uint64(server, generic_current_allocated_bytes,
    "Heap Memory Usage", yb::MetricUnit::kBytes,
    "Number of bytes used by the application. This will not typically match the memory "
    "use reported by the OS, because it does not include TCMalloc overhead or memory "
    "fragmentation." TCMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, generic_heap_size,
    "Reserved Heap Memory", yb::MetricUnit::kBytes,
    "Bytes of system memory reserved by TCMalloc." TCMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_pageheap_free_bytes,
    "Free Heap Memory", yb::MetricUnit::kBytes,
    "Number of bytes in free, mapped pages in page heap. These bytes can be used to "
    "fulfill allocation requests. They always count towards virtual memory usage, and "
    "unless the underlying memory is swapped out by the OS, they also count towards "
    "physical memory usage." TCMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_pageheap_unmapped_bytes,
    "Unmapped Heap Memory", yb::MetricUnit::kBytes,
    "Number of bytes in free, unmapped pages in page heap. These are bytes that have "
    "been released back to the OS, possibly by one of the MallocExtension \"Release\" "
    "calls. They can be used to fulfill allocation requests, but typically incur a page "
    "fault. They always count towards virtual memory usage, and depending on the OS, "
    "typically do not count towards physical memory usage." TCMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_max_total_thread_cache_bytes,
    "Thread Cache Memory Limit", yb::MetricUnit::kBytes,
    "A limit to how much memory TCMalloc dedicates for small objects. Higher numbers "
    "trade off more memory use for -- in some situations -- improved efficiency."
    TCMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, tcmalloc_current_total_thread_cache_bytes,
    "Thread Cache Memory Usage", yb::MetricUnit::kBytes,
    "A measure of some of the memory TCMalloc is using (for small objects)."
    TCMALLOC_DISABLED_MSG);

#undef TCMALLOC_DISABLED_MSG

namespace yb {
namespace tcmalloc {

static uint64_t GetTCMallocPropValue(const char* prop) {
  size_t value = 0;
#if YB_TCMALLOC_ENABLED
  value = ::yb::GetTCMallocProperty(prop);
  if (value < 0) {
    YB_LOG_EVERY_N_SECS(DFATAL, 1) << "Negative value returned for tcmalloc property " << prop
                                   << ": " << value;
    value = 0;
  }
#endif

  return value;
}

#define REGISTER_TCMALLOC_METRIC(name1, name2) \
  entity->NeverRetire( \
      BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(METRIC_, name1), _), name2).\
         InstantiateFunctionGauge(entity, \
         Bind(GetTCMallocPropValue, Unretained( \
              BOOST_PP_STRINGIZE(name1) "." BOOST_PP_STRINGIZE(name2)))));

void RegisterMetrics(const scoped_refptr<MetricEntity>& entity) {
  REGISTER_TCMALLOC_METRIC(generic, current_allocated_bytes);
  REGISTER_TCMALLOC_METRIC(generic, heap_size);
  REGISTER_TCMALLOC_METRIC(tcmalloc, pageheap_free_bytes);
  REGISTER_TCMALLOC_METRIC(tcmalloc, pageheap_unmapped_bytes);
  REGISTER_TCMALLOC_METRIC(tcmalloc, max_total_thread_cache_bytes);
  REGISTER_TCMALLOC_METRIC(tcmalloc, current_total_thread_cache_bytes);
}

#undef REGISTER_TCMALLOC_METRIC

} // namespace tcmalloc
} // namespace yb
