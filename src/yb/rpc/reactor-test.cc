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

#include <thread>

#include "yb/rpc/rpc-test-base.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/logging.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"

using namespace std::literals;
using namespace std::placeholders;

DECLARE_int32(TEST_rpc_reactor_index_for_init_failure_simulation);
DECLARE_int32(rpc_reactor_task_timeout_ms);

namespace yb::rpc {

MessengerOptions MakeMessengerOptions() {
  auto result = kDefaultClientMessengerOptions;
  result.n_reactors = 4;
  return result;
}

class ReactorTest : public RpcTestBase {
 public:
  ReactorTest() {
    FLAGS_rpc_reactor_task_timeout_ms = 500;

    messenger_.reset(CreateMessenger("my_messenger", MakeMessengerOptions()).release());
  }

  void ScheduledTask(const Status& status, const Status& expected_status) {
    ASSERT_EQ(expected_status.CodeAsString(), status.CodeAsString());
    latch_.CountDown();
  }

  void ScheduledTaskCheckThread(const Status& status, const Thread* thread) {
    ASSERT_OK(status);
    ASSERT_EQ(thread, Thread::current_thread());
    latch_.CountDown();
  }

  void ScheduledTaskScheduleAgain(const Status& status) {
    auto expected_task_id = messenger_->TEST_next_task_id();
    auto task_id = ASSERT_RESULT(messenger_->ScheduleOnReactor(
        std::bind(&ReactorTest::ScheduledTaskCheckThread, this, _1, Thread::current_thread()),
        0s, SOURCE_LOCATION()));
    ASSERT_EQ(expected_task_id, task_id);
    latch_.CountDown();
  }

 protected:
  AutoShutdownMessengerHolder messenger_;
  CountDownLatch latch_{1};
};

TEST_F_EX(ReactorTest, MessengerInitFailure, YBTest) {
  MessengerBuilder builder("test-msgr");
  // Test Reactor::Init failure in very first Messenger's reactor
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_rpc_reactor_index_for_init_failure_simulation) = 0;
  ASSERT_NOK(builder.Build());
  // Test Reactor::Init failure in second Messenger's reactor
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_rpc_reactor_index_for_init_failure_simulation) = 1;
  ASSERT_NOK(builder.Build());
}

TEST_F(ReactorTest, TestFunctionIsCalled) {
  auto expected_task_id = messenger_->TEST_next_task_id();
  auto task_id = ASSERT_RESULT(messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTask, this, _1, Status::OK()), 0s,
      SOURCE_LOCATION()));
  ASSERT_EQ(expected_task_id, task_id);
  latch_.Wait();
}

TEST_F(ReactorTest, TestFunctionIsCalledAtTheRightTime) {
  MonoTime before = MonoTime::Now();
  auto expected_task_id = messenger_->TEST_next_task_id();
  auto task_id = ASSERT_RESULT(messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTask, this, _1, Status::OK()),
      100ms, SOURCE_LOCATION()));
  ASSERT_EQ(expected_task_id, task_id);
  latch_.Wait();
  MonoTime after = MonoTime::Now();
  MonoDelta delta = after.GetDeltaSince(before);
  CHECK_GE(delta.ToMilliseconds(), 100);
}

TEST_F(ReactorTest, TestFunctionIsCalledIfReactorShutdown) {
  auto expected_task_id = messenger_->TEST_next_task_id();
  auto task_id = ASSERT_RESULT(messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTask, this, _1, STATUS(Aborted, "doesn't matter")),
      60s, SOURCE_LOCATION()));
  ASSERT_EQ(expected_task_id, task_id);
  messenger_->Shutdown();
  latch_.Wait();
}

TEST_F(ReactorTest, TestReschedulesOnSameReactorThread) {
  // Our scheduled task will schedule yet another task.
  latch_.Reset(2);

  auto expected_task_id = messenger_->TEST_next_task_id();
  auto task_id = ASSERT_RESULT(messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTaskScheduleAgain, this, _1), 0s,
      SOURCE_LOCATION()));
  ASSERT_EQ(expected_task_id, task_id);
  latch_.Wait();
  latch_.Wait();
}

namespace {

class MonitorTestLogger : public google::base::Logger {
 public:
  MonitorTestLogger() : impl_(google::base::GetLogger(google::GLOG_WARNING)) {
  }

  size_t num_writes() const {
    return num_writes_.load();
  }
 private:
  void Write(bool force_flush, time_t timestamp, const char* message, int message_len) override {
    ++num_writes_;
    impl_->Write(force_flush, timestamp, message, message_len);
  }

  void Flush() override {
    impl_->Flush();
  }

  uint32 LogSize() override {
    return impl_->LogSize();
  }
 private:
  std::atomic<size_t> num_writes_{0};
  google::base::Logger* impl_;
};

} // namespace

TEST_F(ReactorTest, Monitor) {
  latch_.Reset(2);
  auto* logger = new MonitorTestLogger;
  google::base::SetLogger(google::GLOG_WARNING, logger);
  ASSERT_RESULT(messenger_->ScheduleOnReactor([this](const Status& status) {
    CHECK_OK(status);
    latch_.CountDown();
  }, 0s, SOURCE_LOCATION()));
  ASSERT_RESULT(messenger_->ScheduleOnReactor([this, logger](const Status& status) {
    CHECK_OK(status);
    while (!logger->num_writes()) {
      std::this_thread::sleep_for(100ms);
    }
    latch_.CountDown();
  }, 0s, SOURCE_LOCATION()));
  latch_.Wait();
  ASSERT_EQ(logger->num_writes(), 1);
}

} // namespace yb::rpc
