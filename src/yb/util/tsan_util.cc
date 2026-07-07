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

#include "yb/util/tsan_util.h"

#include <thread>

namespace yb {

size_t SanitizerCappedConcurrency() {
  const size_t num_cpus = std::thread::hardware_concurrency();
  const size_t half = num_cpus / 2;
  const size_t capped = half < 4 ? half : 4;  // min(num_cpus / 2, 4)
  return RegularBuildVsSanitizers<size_t>(num_cpus, capped < 1 ? 1 : capped);  // max(1, capped)
}

}  // namespace yb
