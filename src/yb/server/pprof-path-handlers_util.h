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

#if YB_TCMALLOC_ENABLED
#include <gperftools/malloc_extension.h>
#endif

#include <cstdint>
#include <string>
#include <utility>

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"

namespace yb {

struct SampleInfo {
  int64_t bytes;
  int64_t count;
};

using SampleStack = std::string;
typedef std::pair<SampleStack, SampleInfo> Sample;

YB_DEFINE_ENUM(SampleOrder, (kCount)(kBytes));

#if YB_TCMALLOC_ENABLED

std::vector<Sample> GetAggregateAndSortHeapSnapshot(SampleOrder order);

#endif // YB_TCMALLOC_ENABLED

void GenerateTable(std::stringstream* output, const std::vector<Sample>& samples,
    const std::string& title, size_t max_call_stacks);

} // namespace yb
