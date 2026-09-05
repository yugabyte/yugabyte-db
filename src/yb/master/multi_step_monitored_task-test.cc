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

#include <atomic>
#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/master/catalog_entity_base.h"
#include "yb/master/multi_step_monitored_task.h"
#include "yb/master/tasks_tracker.h"
#include "yb/master/ysql_ddl_verification_task.h"
#include "yb/rpc/messenger.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/dist_trace.h"
#include "yb/util/dist_trace_test_util.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/threadpool.h"

using namespace std::placeholders;

DECLARE_bool(TEST_enable_sync_points);

namespace yb::master {

class CatalogEntityWithTasksMock : public CatalogEntityWithTasks {
 public:
  CatalogEntityWithTasksMock() : CatalogEntityWithTasks(/*tasks_tracker=*/nullptr) {}
};

const auto kTaskType = server::MonitoredTaskType::kAddServer;
const auto kTaskDelay = MonoDelta::FromSeconds(2);
LeaderEpoch leader_epoch = LeaderEpoch(1, 1);

class TestCatalogEntityTask : public MultiStepCatalogEntityTask {
 public:
  TestCatalogEntityTask(
      CatalogEntityWithTasks& catalog_entity, ThreadPool& async_task_pool,
      rpc::Messenger& messenger, const LeaderEpoch& epoch)
      : MultiStepCatalogEntityTask(
            std::bind(&TestCatalogEntityTask::ValidateEpoch, this, _1), async_task_pool, messenger,
            catalog_entity, epoch) {
    completion_callback_ = [this](const Status& status) {
      completion_sync_.AsStatusFunctor()(status);
    };
  }

  ~TestCatalogEntityTask() = default;

  Status ValidateEpoch(const LeaderEpoch& epoch) {
    SCHECK_EQ(epoch, leader_epoch, IllegalState, "Epoch does not match");
    return Status::OK();
  }

  server::MonitoredTaskType type() const override { return kTaskType; }

  std::string type_name() const override { return "Test task"; }

  std::string description() const override { return "Test task"; }

  Status FirstStep() override {
    current_step_ = 1;
    ScheduleNextStep(std::bind(&TestCatalogEntityTask::SecondStep, this), "second step");
    return Status::OK();
  }

  Status SecondStep() {
    current_step_ = 2;
    TEST_SYNC_POINT("SecondStep");
    TEST_SYNC_POINT("SecondStepBeforeDelay");
    ScheduleNextStepWithDelay(
        std::bind(&TestCatalogEntityTask::ThirdStep, this), "third step", kTaskDelay * 2);
    return Status::OK();
  }

  Status ThirdStep() {
    current_step_ = 3;
    Complete();
    return Status::OK();
  }

  uint32 current_step_ = 0;
  Synchronizer completion_sync_;
};

TEST(CatalogEntityTaskTest, TestMultipleSteps) {
  google::SetVLOGLevel("multi_step*", 4);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"SecondStep", "ReachedSecondStep"}, {"ResumeSecondStep", "SecondStepBeforeDelay"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::unique_ptr<ThreadPool> thread_pool;
  ThreadPoolBuilder thread_pool_builder("Test");
  ASSERT_OK(thread_pool_builder.Build(&thread_pool));

  rpc::MessengerBuilder messenger_builder("Test");
  auto messenger = ASSERT_RESULT(messenger_builder.Build());

  CatalogEntityWithTasksMock catalog_entity;

  auto task = std::make_shared<TestCatalogEntityTask>(
      catalog_entity, *thread_pool.get(), *messenger.get(), leader_epoch);
  task->Start();
  ASSERT_TRUE(catalog_entity.HasTasks());
  ASSERT_EQ(catalog_entity.NumTasks(), 1);
  ASSERT_EQ(catalog_entity.HasTasks(kTaskType), 1);

  TEST_SYNC_POINT("ReachedSecondStep");
  ASSERT_EQ(task->current_step_, 2);

  auto start_time = CoarseMonoClock::now();
  TEST_SYNC_POINT("ResumeSecondStep");

  catalog_entity.WaitTasksCompletion();
  ASSERT_OK(task->completion_sync_.Wait());
  ASSERT_EQ(task->current_step_, 3);
  ASSERT_GE(MonoDelta(CoarseMonoClock::now() - start_time), kTaskDelay);

  ASSERT_FALSE(catalog_entity.HasTasks());
  ASSERT_EQ(catalog_entity.NumTasks(), 0);
  ASSERT_EQ(catalog_entity.HasTasks(kTaskType), 0);

  messenger->Shutdown();
  thread_pool->Shutdown();
}

TEST(CatalogEntityTaskTest, TestEpochChanges) {
  google::SetVLOGLevel("multi_step*", 4);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  yb::SyncPoint::GetInstance()->LoadDependency({
      {"SecondStep", "ReachedSecondStep"},
      {"ResumeSecondStep", "SecondStepBeforeDelay"},
  });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::unique_ptr<ThreadPool> thread_pool;
  ThreadPoolBuilder thread_pool_builder("Test");
  ASSERT_OK(thread_pool_builder.Build(&thread_pool));

  rpc::MessengerBuilder messenger_builder("Test");
  auto messenger = ASSERT_RESULT(messenger_builder.Build());
  auto se = ScopeExit([messenger = messenger.get(), thread_pool = thread_pool.get()] {
    messenger->Shutdown();
    thread_pool->Shutdown();
  });

  CatalogEntityWithTasksMock catalog_entity;

  auto task = std::make_shared<TestCatalogEntityTask>(
      catalog_entity, *thread_pool.get(), *messenger.get(), leader_epoch);
  task->Start();

  TEST_SYNC_POINT("ReachedSecondStep");
  leader_epoch.leader_term++;

  TEST_SYNC_POINT("ResumeSecondStep");

  catalog_entity.WaitTasksCompletion();
  ASSERT_NOK(task->completion_sync_.Wait());
  ASSERT_EQ(task->current_step_, 2);

  ASSERT_FALSE(catalog_entity.HasTasks());
  ASSERT_EQ(catalog_entity.NumTasks(), 0);
  ASSERT_EQ(catalog_entity.HasTasks(kTaskType), 0);
}

// Mirrors the shape of NamespaceVerificationTask and TableSchemaVerificationTask.
class PollTransactionStatusTestTask : public MultiStepCatalogEntityTask,
                                      public PollTransactionStatusBase {
 public:
  PollTransactionStatusTestTask(
      CatalogEntityWithTasks& catalog_entity, ThreadPool& async_task_pool,
      rpc::Messenger& messenger, const LeaderEpoch& epoch,
      std::shared_future<client::YBClient*> client_future)
      : MultiStepCatalogEntityTask(
            [](const LeaderEpoch&) { return Status::OK(); }, async_task_pool, messenger,
            catalog_entity, epoch),
        PollTransactionStatusBase(TransactionMetadata(), std::move(client_future), "test task") {}

  ~PollTransactionStatusTestTask() = default;

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::TableSchemaVerification;
  }

  std::string type_name() const override { return "Poll transaction status test task"; }

  std::string description() const override { return "Poll transaction status test task"; }

  Status poll_step_status_;

 private:
  Status FirstStep() override {
    ScheduleNextStep(std::bind(&PollTransactionStatusTestTask::PollStep, this), "poll step");
    return Status::OK();
  }

  // Runs while holding step_execution_mutex_, which is the mutex AbortAndReturnPrevState waits on.
  Status PollStep() {
    TEST_SYNC_POINT("PollTransactionStatusTestTask::PollStepEntered");
    TEST_SYNC_POINT("PollTransactionStatusTestTask::ResumePollStep");
    poll_step_status_ = VerifyTransaction();
    return poll_step_status_;
  }

  // AbortAndReturnPrevState invokes it.
  void PerformAbort() override {
    MultiStepCatalogEntityTask::PerformAbort();
    Shutdown();
    TEST_SYNC_POINT("PollTransactionStatusTestTask::PerformAbortDone");
  }

  void TaskCompleted(const Status& status) override {
    MultiStepCatalogEntityTask::TaskCompleted(status);
    Shutdown();
  }

  // Only reachable from the RPC callback, which never fires in this test.
  void TransactionPending() override {}
  void FinishPollTransaction(bool aborted) override {}
};

// Aborting a task while one of its steps is polling the transaction status must not strand the
// task's Synchronizer signal.
TEST(CatalogEntityTaskTest, TestAbortWithInFlightTransactionPoll) {
  google::SetVLOGLevel("multi_step*", 4);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"PollTransactionStatusTestTask::PollStepEntered", "Test::ReachedPollStep"},
       {"PollTransactionStatusTestTask::PerformAbortDone",
        "PollTransactionStatusTestTask::ResumePollStep"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::shared_ptr<ThreadPool> thread_pool;
  {
    std::unique_ptr<ThreadPool> thread_pool_holder;
    ThreadPoolBuilder thread_pool_builder("Test");
    ASSERT_OK(thread_pool_builder.Build(&thread_pool_holder));
    thread_pool = std::move(thread_pool_holder);
  }

  rpc::MessengerBuilder messenger_builder("Test");
  std::shared_ptr<rpc::Messenger> messenger(ASSERT_RESULT(messenger_builder.Build()).release());

  auto catalog_entity = std::make_shared<CatalogEntityWithTasksMock>();

  auto client = std::make_shared<client::MockYBClient>();
  std::promise<client::YBClient*> client_promise;
  client_promise.set_value(client.get());

  auto task = std::make_shared<PollTransactionStatusTestTask>(
      *catalog_entity, *thread_pool, *messenger, LeaderEpoch(1, 1),
      client_promise.get_future().share());
  task->Start();

  // Wait until the poll step is running and holding step_execution_mutex_.
  TEST_SYNC_POINT("Test::ReachedPollStep");

  // At this point, the poll thread is parked on the sync point, ResumePollStep.
  // We are going to start an abort thread, which will call AbortAndReturnPrevState.
  // That will call PerformAbort and release the poll step.

  auto abort_returned = std::make_shared<std::atomic<bool>>(false);
  std::thread abort_thread([task, abort_returned, catalog_entity]() {
    task->AbortAndReturnPrevState(STATUS(Aborted, "Test abort"));
    abort_returned->store(true);
  });

  auto wait_status = WaitFor(
      [abort_returned]() -> Result<bool> { return abort_returned->load(); },
      MonoDelta::FromSeconds(30), "abort to complete");

  if (!wait_status.ok()) {
    // Record the failure first, so the diagnostic lands even if the cleanup below blocks.
    ADD_FAILURE() << wait_status.CloneAndPrepend(
        "Abort deadlocked on the transaction status poll Synchronizer");
    abort_thread.detach();
    yb::SyncPoint::GetInstance()->DisableProcessing();
    messenger->Shutdown();
    thread_pool->Shutdown();
    return;
  }

  abort_thread.join();

  // Three exits in VerifyTransaction return IllegalState. Match the message so this fails if the
  // step took any exit other than the shutdown check, the only one that strands the signal.
  ASSERT_TRUE(task->poll_step_status_.IsIllegalState()) << task->poll_step_status_;
  ASSERT_STR_CONTAINS(task->poll_step_status_.ToString(), "Task has been shut down");
  ASSERT_FALSE(catalog_entity->HasTasks());

  yb::SyncPoint::GetInstance()->DisableProcessing();
  messenger->Shutdown();
  thread_pool->Shutdown();
}

// Two-step task recording the trace context active in each step. Steps run on the async pool.
class TraceObservingCatalogEntityTask : public MultiStepCatalogEntityTask {
 public:
  TraceObservingCatalogEntityTask(
      CatalogEntityWithTasks& catalog_entity, ThreadPool& async_task_pool,
      rpc::Messenger& messenger)
      : MultiStepCatalogEntityTask(
            [](const LeaderEpoch&) { return Status::OK(); }, async_task_pool, messenger,
            catalog_entity, leader_epoch) {
    completion_callback_ = [this](const Status& status) {
      completion_sync_.AsStatusFunctor()(status);
    };
  }

  server::MonitoredTaskType type() const override { return kTaskType; }

  std::string type_name() const override { return "Trace observing task"; }

  std::string description() const override { return "Trace observing task"; }

  Status FirstStep() override {
    first_step_context_ = dist_trace::GetActiveSpanContext();
    ScheduleNextStep(std::bind(&TraceObservingCatalogEntityTask::SecondStep, this), "second step");
    return Status::OK();
  }

  Status SecondStep() {
    second_step_context_ = dist_trace::GetActiveSpanContext();
    Complete();
    return Status::OK();
  }

  std::optional<dist_trace::trace::SpanContext> first_step_context_;
  std::optional<dist_trace::trace::SpanContext> second_step_context_;
  Synchronizer completion_sync_;
};

class CatalogEntityTaskTraceTest : public YBTest {
 private:
  dist_trace::ScopedTestDistTrace dist_trace_;
};

// Construct a task under an active trace context, then Start() it after the context is gone: the
// async pool runs both steps under that context.
TEST_F(CatalogEntityTaskTraceTest, TraceContextCarriedToSteps) {
  std::unique_ptr<ThreadPool> thread_pool;
  ThreadPoolBuilder thread_pool_builder("Test");
  ASSERT_OK(thread_pool_builder.Build(&thread_pool));

  rpc::MessengerBuilder messenger_builder("Test");
  auto messenger = ASSERT_RESULT(messenger_builder.Build());
  auto se = ScopeExit([messenger = messenger.get(), thread_pool = thread_pool.get()] {
    messenger->Shutdown();
    thread_pool->Shutdown();
  });

  CatalogEntityWithTasksMock catalog_entity;

  const auto expected = dist_trace::MakeTestSpanContext(0x5a);
  std::shared_ptr<TraceObservingCatalogEntityTask> task;
  {
    dist_trace::ScopedAdoptSpan scope(expected);
    ASSERT_TRUE(dist_trace::HasActiveContext());
    task = std::make_shared<TraceObservingCatalogEntityTask>(
        catalog_entity, *thread_pool.get(), *messenger.get());
  }
  task->Start();

  catalog_entity.WaitTasksCompletion();
  ASSERT_OK(task->completion_sync_.Wait());

  ASSERT_TRUE(task->first_step_context_.has_value());
  ASSERT_EQ(task->first_step_context_->trace_id(), expected.trace_id());
  ASSERT_EQ(task->first_step_context_->span_id(), expected.span_id());
  ASSERT_TRUE(task->second_step_context_.has_value());
  ASSERT_EQ(task->second_step_context_->trace_id(), expected.trace_id());
  ASSERT_EQ(task->second_step_context_->span_id(), expected.span_id());
}

// Construct a task with no active trace context: the async pool runs both steps with none.
TEST_F(CatalogEntityTaskTraceTest, NoTraceContextCarriedWhenNoneActive) {
  std::unique_ptr<ThreadPool> thread_pool;
  ThreadPoolBuilder thread_pool_builder("Test");
  ASSERT_OK(thread_pool_builder.Build(&thread_pool));

  rpc::MessengerBuilder messenger_builder("Test");
  auto messenger = ASSERT_RESULT(messenger_builder.Build());
  auto se = ScopeExit([messenger = messenger.get(), thread_pool = thread_pool.get()] {
    messenger->Shutdown();
    thread_pool->Shutdown();
  });

  CatalogEntityWithTasksMock catalog_entity;

  auto task = std::make_shared<TraceObservingCatalogEntityTask>(
      catalog_entity, *thread_pool.get(), *messenger.get());
  task->Start();

  catalog_entity.WaitTasksCompletion();
  ASSERT_OK(task->completion_sync_.Wait());

  ASSERT_FALSE(task->first_step_context_.has_value());
  ASSERT_FALSE(task->second_step_context_.has_value());
}

}  // namespace yb::master
