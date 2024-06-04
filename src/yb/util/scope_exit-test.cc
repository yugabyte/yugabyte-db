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

#include "yb/util/scope_exit.h"
#include "yb/util/test_util.h"

namespace yb::util {

TEST(ScopeExitTest, TestSuccess) {
  bool se_ran = false;

  {
    auto se = ScopeExit([&se_ran] { se_ran = true; });
    ASSERT_FALSE(se_ran);
  }

  ASSERT_TRUE(se_ran);

  se_ran = false;
  const auto lambda = [&se_ran] { se_ran = true; };
  {
    auto se = ScopeExit(lambda);
    ASSERT_FALSE(se_ran);
  }

  ASSERT_TRUE(se_ran);
}

TEST(ScopeExitTest, TestMove) {
  std::atomic<int32> se_run_count = 0;

  {
    auto se1 = ScopeExit([&se_run_count] { se_run_count++; });
    ASSERT_EQ(se_run_count, 0);
    auto se2(std::move(se1));
    ASSERT_EQ(se_run_count, 0);
  }

  ASSERT_EQ(se_run_count, 1);
}

TEST(ScopeExitTest, TestCancel) {
  bool se_ran = false;

  {
    auto se = ScopeExit([&se_ran] { se_ran = true; });
    ASSERT_FALSE(se_ran);
    se.Cancel();
  }

  ASSERT_FALSE(se_ran);

  // Move before cancel.
  {
    auto se1 = ScopeExit([&se_ran] { se_ran = true; });
    ASSERT_FALSE(se_ran);
    auto se2(std::move(se1));
    ASSERT_FALSE(se_ran);
    se2.Cancel();
  }

  ASSERT_FALSE(se_ran);

  // Cancel before move.
  {
    auto se1 = ScopeExit([&se_ran] { se_ran = true; });
    ASSERT_FALSE(se_ran);
    se1.Cancel();
    ASSERT_FALSE(se_ran);
    auto se2(std::move(se1));
  }

  ASSERT_FALSE(se_ran);

  // Ensure cancel releases resources.
  std::shared_ptr<int> count = std::make_shared<int>(0);
  ASSERT_EQ(count.use_count(), 1);
  {
    auto se1 = ScopeExit([count] {});
    ASSERT_EQ(count.use_count(), 2);
    se1.Cancel();
    ASSERT_EQ(count.use_count(), 1);
    auto se2(std::move(se1));
    ASSERT_EQ(count.use_count(), 1);
    se2.Cancel();
  }
  ASSERT_EQ(count.use_count(), 1);
}

}  // namespace yb::util
