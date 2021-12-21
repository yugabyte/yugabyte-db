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
//

#include "yb/encryption/encryption_test_util.h"

#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace encryption {

constexpr uint32_t kEncryptionTestNumIterations = 10;

void DoTest(std::function<void(uint32_t, uint32_t)> file_op, int32_t size) {
  std::vector<int32_t> indices = RandomUniformVector(0, size - 1, kEncryptionTestNumIterations);
  std::sort(indices.begin(), indices.end());
  int last_idx = 0;
  for (auto i : indices) {
    if (last_idx == i) {
      continue;
    }
    ASSERT_NO_FATALS(file_op(last_idx, i));
    last_idx = i;
  }
  ASSERT_NO_FATALS(file_op(last_idx, size));
}

} // namespace encryption
} // namespace yb
