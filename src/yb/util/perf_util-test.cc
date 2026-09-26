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

#include <optional>

#include <gtest/gtest.h>

#include "yb/util/perf_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

class PerfProfilerTest : public YBTest {};

TEST_F(PerfProfilerTest, ConcurrentStartRejected) {
  const auto storage_dir = GetTestDataDirectory();
  std::optional<PerfProfiler> first;
  first.emplace();
  auto s = first->Start(99, storage_dir);
  if (s.IsNotFound()) {
    GTEST_SKIP() << "perf is not installed: " << s;
  }
  ASSERT_OK(s);

  PerfProfiler second;
  s = second.Start(99, storage_dir);
  ASSERT_TRUE(s.IsIllegalState()) << s;

  first.reset();
  ASSERT_OK(second.Start(99, storage_dir));
}

} // namespace yb
