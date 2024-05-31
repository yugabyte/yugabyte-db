// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "yb/util/background_task.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::chrono_literals;

namespace yb {

class BackgroundTaskTest : public YBTest {
 public:
  BackgroundTaskTest() : YBTest(), run_count_(0) {}

  std::unique_ptr<BackgroundTask> GetTask(std::chrono::milliseconds timeout = 0s) {
    auto task = std::make_unique<BackgroundTask>([this]() {
      run_count_.fetch_add(1);
    }, "test", "test", timeout);
    EXPECT_OK(task->Init());
    return task;
  }

 protected:
  std::atomic<int> run_count_;
};

TEST_F(BackgroundTaskTest, RunsTaskOnWake) {
  constexpr int kNumTaskRuns = 1000;
  auto bg_task = GetTask();

  for (int i = 1; i < kNumTaskRuns; ++i) {
    EXPECT_OK(bg_task->Wake());
    EXPECT_OK(WaitFor([this, i]() {
      return run_count_ == i;
    }, 1s * kTimeMultiplier, Format("Wait for i-th ($0) task run.", i)));
  }

  bg_task->Shutdown();
}

TEST_F(BackgroundTaskTest, RunsTaskOnInterval) {
  constexpr int kNumTaskRuns = 1000;

  auto interval = 10ms * kTimeMultiplier;
  auto bg_task = GetTask(interval);

  for (int i = 1; i < kNumTaskRuns; ++i) {
    std::this_thread::sleep_for(interval);
    EXPECT_OK(WaitFor([this, i]() {
      return run_count_ >= i;
    }, interval / 10, Format("Wait for i-th ($0) task run.", i)));
  }

  bg_task->Shutdown();
}

class BackgroundTaskShutdownTest :
    public BackgroundTaskTest, public ::testing::WithParamInterface<std::chrono::milliseconds> {};

TEST_P(BackgroundTaskShutdownTest, InitAndShutdown) {
  auto interval = GetParam() * kTimeMultiplier;
  auto bg_task = GetTask(interval);
  std::this_thread::sleep_for(1s * kTimeMultiplier);
  bg_task->Shutdown();
}

INSTANTIATE_TEST_CASE_P(
    InitAndShutdown, BackgroundTaskShutdownTest, ::testing::Values(0s, 20ms, 5s));

}  // namespace yb
