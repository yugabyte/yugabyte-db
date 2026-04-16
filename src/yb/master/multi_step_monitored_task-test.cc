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

#include <gtest/gtest.h>

#include "yb/master/catalog_entity_base.h"
#include "yb/master/multi_step_monitored_task.h"
#include "yb/master/tasks_tracker.h"
#include "yb/rpc/messenger.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
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

}  // namespace yb::master
