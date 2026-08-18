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

#include <thread>

#include <gtest/gtest.h>

#include "yb/master/async_rpc_tasks_base.h"

#include "yb/util/dist_trace.h"
#include "yb/util/dist_trace_test_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"


namespace yb::master {

// Runs RetryingRpcTask::Run() with a null master, recording the trace context active in
// SendRequest(). Reports success so Run() does not fall through to UnregisterAsyncTask().
class TraceObservingRpcTask : public RetryingRpcTask {
 public:
  TraceObservingRpcTask() : RetryingRpcTask(nullptr, nullptr, nullptr) {}

  std::string type_name() const override { return "TraceObservingRpcTask"; }
  std::string description() const override { return "TraceObservingRpcTask"; }

  const std::optional<dist_trace::trace::SpanContext>& observed() const { return observed_; }

  // Moves the task to a terminal state, which its destructor requires.
  void MarkComplete() { state_ = server::MonitoredTaskState::kComplete; }

 private:
  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddServer;
  }

  bool SendRequest(int attempt) override {
    observed_ = dist_trace::GetActiveSpanContext();
    return true;
  }

  void HandleResponse(int attempt) override {}
  Status ResetProxies() override { return Status::OK(); }
  void DoRpcCallback() override {}

  std::optional<dist_trace::trace::SpanContext> observed_;
};

class RetryingRpcTaskTraceTest : public YBTest {
 private:
  dist_trace::ScopedTestDistTrace dist_trace_;
};

// Runs the task on a thread other than the one that created it, as the master does.
Status RunOnAnotherThread(const std::shared_ptr<TraceObservingRpcTask>& task) {
  Status status;
  std::thread thread([&task, &status] { status = task->Run(); });
  thread.join();
  return status;
}

// Construct under an active trace context: Run() sends the request under that context.
TEST_F(RetryingRpcTaskTraceTest, TraceContextCarriedToRun) {
  const auto expected = dist_trace::MakeTestSpanContext(0x55);

  std::shared_ptr<TraceObservingRpcTask> task;
  {
    auto scope = dist_trace::ActivateParentScope(expected);
    ASSERT_TRUE(scope != nullptr);
    task = std::make_shared<TraceObservingRpcTask>();
  }

  ASSERT_OK(RunOnAnotherThread(task));

  ASSERT_TRUE(task->observed().has_value());
  ASSERT_EQ(task->observed()->trace_id(), expected.trace_id());
  ASSERT_EQ(task->observed()->span_id(), expected.span_id());
  task->MarkComplete();
}

// Construct with no active trace context: Run() sends the request with none.
TEST_F(RetryingRpcTaskTraceTest, NoTraceContextCarriedWhenNoneActive) {
  auto task = std::make_shared<TraceObservingRpcTask>();

  ASSERT_OK(RunOnAnotherThread(task));

  ASSERT_FALSE(task->observed().has_value());
  task->MarkComplete();
}

// Two tasks constructed under different trace contexts: each sends under its own.
TEST_F(RetryingRpcTaskTraceTest, TraceContextIsPerTask) {
  const auto first_context = dist_trace::MakeTestSpanContext(0x66);
  const auto second_context = dist_trace::MakeTestSpanContext(0x77);

  std::shared_ptr<TraceObservingRpcTask> first_task;
  std::shared_ptr<TraceObservingRpcTask> second_task;
  {
    auto scope = dist_trace::ActivateParentScope(first_context);
    first_task = std::make_shared<TraceObservingRpcTask>();
  }
  {
    auto scope = dist_trace::ActivateParentScope(second_context);
    second_task = std::make_shared<TraceObservingRpcTask>();
  }

  ASSERT_OK(RunOnAnotherThread(first_task));
  ASSERT_OK(RunOnAnotherThread(second_task));

  ASSERT_EQ(first_task->observed()->trace_id(), first_context.trace_id());
  ASSERT_EQ(second_task->observed()->trace_id(), second_context.trace_id());
  first_task->MarkComplete();
  second_task->MarkComplete();
}

}  // namespace yb::master
