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

#include "yb/util/crash_point.h"
#include "yb/util/shared_mem.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

class CrashPointTest : public YBTest { };

TEST_F(CrashPointTest, YB_DEBUG_ONLY_TEST(TestMatchingCrashPoint)) {
  ASSERT_OK(ForkAndRunToCrashPoint([] {
    TEST_CRASH_POINT("TestCrashPoint");
  }, "TestCrashPoint"));
}

TEST_F(CrashPointTest, YB_DEBUG_ONLY_TEST(TestNonMatchingCrashPoint)) {
  auto clean_child_exit = ASSERT_RESULT(SharedMemoryObject<bool>::Create(false));

  ASSERT_OK(ForkAndRunToCompletion([&clean_child_exit] {
    SetTestCrashPoint("TestCrashPoint:1");
    TEST_CRASH_POINT("TestCrashPoint:2");

    *clean_child_exit = true;
  }));

  ASSERT_TRUE(*clean_child_exit);
}

TEST_F(CrashPointTest, YB_DEBUG_ONLY_TEST(TestClearedCrashPoint)) {
  auto clean_child_exit = ASSERT_RESULT(SharedMemoryObject<bool>::Create(false));

  ASSERT_OK(ForkAndRunToCompletion([&clean_child_exit] {
    SetTestCrashPoint("TestCrashPoint");
    ClearTestCrashPoint();
    TEST_CRASH_POINT("TestCrashPoint");

    *clean_child_exit = true;
  }));

  ASSERT_TRUE(*clean_child_exit);
}

} // namespace yb
