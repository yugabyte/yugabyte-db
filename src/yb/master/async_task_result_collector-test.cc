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

#include "yb/master/async_rpc_tasks_base.h"
#include "yb/master/async_task_result_collector.h"

namespace yb {
namespace master {

using namespace std::chrono_literals;

TEST(AsyncTaskResultCollectorTest, Basic) {
  // Create two tasks and track their responses.
  auto collector = TaskResultCollector<int>::Create(2 /* num_tasks */);
  collector->TrackResponse("uuid1", 1 /* value */);
  collector->TrackResponse("uuid2", 2 /* value */);
  auto responses = collector->TakeResponses(1s /* timeout */);
  ASSERT_EQ(responses.size(), 2);
  ASSERT_EQ(responses["uuid1"], 1);
  ASSERT_EQ(responses["uuid2"], 2);
}

TEST(AsyncTaskResultCollectorTest, Timeout) {
  // If only one task finishes, we should still get that response.
  auto collector = TaskResultCollector<int>::Create(2 /* num_tasks */);
  collector->TrackResponse("uuid1", 1 /* value */);
  auto responses = collector->TakeResponses(1s /* timeout */);
  ASSERT_EQ(responses.size(), 1);
  ASSERT_EQ(responses["uuid1"], 1);

  // It's ok for the other task to finish after the timeout.
  collector->TrackResponse("uuid2", 2 /* value */);
}

} // namespace master
} // namespace yb
