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

#pragma once

#include <stdint.h>
#include <memory>
#include <vector>

#include "yb/util/tcmalloc_profile.h"
#include "yb/util/test_util.h"

namespace yb {

#if YB_TCMALLOC_ENABLED

class SamplingProfilerTest : public YBTest {
 public:
  void SetUp() override;

  void SetProfileSamplingRate(int64_t sample_freq_bytes);

  std::unique_ptr<char[]> TestAllocArrayOfSize(int64_t alloc_size);

  std::vector<Sample> GetTestAllocs(const std::vector<Sample>& samples);

 private:
  std::unique_ptr<char[]> InternalAllocArrayOfSize(int64_t alloc_size);
};

#endif // YB_TCMALLOC_ENABLED

} // namespace yb
