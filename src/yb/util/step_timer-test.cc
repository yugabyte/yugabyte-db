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


#include "yb/util/step_timer.h"

#include <regex>
#include <thread>

#include <gtest/gtest.h>

#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

TEST(StepTimerTest, NoSteps) {
  StepTimer timer;
  ASSERT_TRUE(std::regex_match(timer.ToString(), std::regex(R"(took \d+\.\d{3}s)")))
      << timer.ToString();
}

TEST(StepTimerTest, StepsInOrder) {
  StepTimer timer;
  timer.StartStep("first");
  std::this_thread::sleep_for(100ms);
  timer.StartStep("second");
  const auto str = timer.ToString();
  std::smatch match;
  ASSERT_TRUE(std::regex_match(
      str, match, std::regex(R"(took (\d+\.\d{3})s: first=(\d+\.\d{3})s, second=\d+\.\d{3}s)")))
      << str;
  ASSERT_GE(std::stod(match[2]), 0.1) << str;
  ASSERT_GE(std::stod(match[1]), std::stod(match[2])) << str;
  ASSERT_GE(timer.Elapsed(), MonoDelta::FromMilliseconds(100));
}

// The last step lasts until the timer is logged, so a step the operation failed in keeps its time.
TEST(StepTimerTest, LastStepRunsUntilLogged) {
  StepTimer timer;
  timer.StartStep("failed");
  std::this_thread::sleep_for(100ms);
  const auto str = timer.ToString();
  std::smatch match;
  ASSERT_TRUE(std::regex_match(str, match, std::regex(R"(took \d+\.\d{3}s: failed=(\d+\.\d{3})s)")))
      << str;
  ASSERT_GE(std::stod(match[1]), 0.1) << str;
}

} // namespace yb
