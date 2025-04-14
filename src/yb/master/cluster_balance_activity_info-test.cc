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

#include "yb/master/cluster_balance_activity_info.h"

#include "yb/util/test_util.h"

namespace yb::master {

TEST(TestClusterBalancerActivityInfo, WarningAggregation) {
  ClusterBalancerActivityInfo activity_info;
  ASSERT_TRUE(activity_info.IsIdle());
  activity_info.CountWarning(ClusterBalancerWarningType::kTableAddReplicas, "Example message 1");
  activity_info.CountWarning(ClusterBalancerWarningType::kTableAddReplicas, "Example message 2");
  auto warnings_summary = activity_info.GetWarningsSummary();
  ASSERT_EQ(warnings_summary.size(), 1);
  ASSERT_EQ(warnings_summary[0].count, 2);
  ASSERT_EQ(warnings_summary[0].example_message, "Example message 1");
  ASSERT_FALSE(activity_info.IsIdle());
}

TEST(TestClusterBalancerActivityInfo, TaskAggregation) {
  class DummyAddServerTask : public RetryingRpcTask {
   public:
    explicit DummyAddServerTask(server::MonitoredTaskState state, Status status = Status::OK()):
        RetryingRpcTask(nullptr, nullptr, nullptr) {
      state_ = state;
      final_status_ = status;
    }
    std::string type_name() const override { return "DummyAddServerTask"; }
    std::string description() const override { return "DummyAddServerTask description"; }
    void SetState(server::MonitoredTaskState state) { state_ = state; }

   private:
    // Dummy implementation.
    Status status_;
    server::MonitoredTaskType type() const override {
      return server::MonitoredTaskType::kAddServer;
    }
    bool SendRequest(int attempt) override { return true; }
    void HandleResponse(int attempt) override {}
    Status ResetProxies() override { return Status::OK(); }
    void DoRpcCallback() override {}
  };

  ClusterBalancerActivityInfo activity_info;
  ASSERT_TRUE(activity_info.IsIdle());

  // The tasks in the same state should be aggregated.
  auto task1 = std::make_shared<DummyAddServerTask>(server::MonitoredTaskState::kComplete);
  auto task2 = std::make_shared<DummyAddServerTask>(server::MonitoredTaskState::kComplete);
  auto task3 = std::make_shared<DummyAddServerTask>(
      server::MonitoredTaskState::kFailed, STATUS(RuntimeError, "Task failed"));
  activity_info.AddTask(task1);
  activity_info.AddTask(task2);
  activity_info.AddTask(task3);

  auto tasks_summary = activity_info.GetTasksSummary();
  ASSERT_EQ(tasks_summary.size(), 2);
  auto completed_tasks = tasks_summary.at(
      {server::MonitoredTaskType::kAddServer, server::MonitoredTaskState::kComplete});
  ASSERT_EQ(completed_tasks.count, 2);
  ASSERT_EQ(completed_tasks.example_status, "OK");
  ASSERT_EQ(completed_tasks.example_description, "DummyAddServerTask description");

  auto failed_tasks = tasks_summary.at(
      {server::MonitoredTaskType::kAddServer, server::MonitoredTaskState::kFailed});
  ASSERT_EQ(failed_tasks.count, 1);
  ASSERT_STR_CONTAINS(failed_tasks.example_status, "Task failed");
  ASSERT_EQ(failed_tasks.example_description, "DummyAddServerTask description");
}

}  // namespace yb::master
