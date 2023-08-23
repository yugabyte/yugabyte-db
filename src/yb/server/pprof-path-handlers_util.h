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

#pragma once

#if YB_GOOGLE_TCMALLOC
#include <tcmalloc/malloc_extension.h>
#endif // YB_GOOGLE_TCMALLOC

#include <cstdint>
#include <string>
#include <utility>

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"

DECLARE_bool(enable_process_lifetime_heap_profiling);

namespace yb {

#if YB_GOOGLE_TCMALLOC

struct SampleInfo {
  int64_t bytes;
  int64_t count;
};

typedef std::pair<std::string, SampleInfo> Sample;

tcmalloc::Profile GetAllocationProfile(int seconds, int64_t sample_freq_bytes);

// If peak_heap is set, gets the snapshot of the heap at peak memory usage.
tcmalloc::Profile GetHeapSnapshot(bool peak_heap);

std::vector<Sample> AggregateAndSortProfile(const tcmalloc::Profile& profile, bool only_growth);

void GenerateTable(std::stringstream* output, const std::vector<Sample>& samples,
    const std::string& title, size_t max_call_stacks);

#endif // YB_GOOGLE_TCMALLOC

} // namespace yb
