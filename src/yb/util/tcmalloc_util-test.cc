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

#include <gtest/gtest.h>

#include "yb/util/tcmalloc_util.h"
#include "yb/util/test_util.h"

namespace yb {

class TCMallocUtilTest : public YBTest {};

TEST_F(TCMallocUtilTest, KernelUnsafeForPerCpuCaches) {
  for (const auto* release : {
           "6.19.0", "6.19.0-rc1", "6.19.12-200.fc43.x86_64", "7.0.0-14-generic", "7.0.13",
           "7.0.13-arch1-1"}) {
    EXPECT_TRUE(IsKernelUnsafeForTCMallocPerCpuCaches(release)) << release;
  }
  for (const auto* release : {
           "3.10.0-1160.el7.x86_64", "4.18.0-553.el8_10.x86_64", "5.14.0-503.el9.x86_64",
           "5.15.210-1.el8.elrepo.x86_64", "6.12.0-55.el10.x86_64", "6.18.9", "7.0.14",
           "7.0.14-generic", "7.1.0-rc3", "8.0.0", "", "garbage", "7"}) {
    EXPECT_FALSE(IsKernelUnsafeForTCMallocPerCpuCaches(release)) << release;
  }
}

}  // namespace yb
