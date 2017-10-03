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

#include "kudu/rpc/reactor.h"

#include "kudu/rpc/rpc-test-base.h"
#include "kudu/util/countdown_latch.h"

using std::shared_ptr;

namespace kudu {
namespace rpc {

class ReactorTest : public RpcTestBase {
 public:
  ReactorTest()
    : messenger_(CreateMessenger("my_messenger", 4)),
      latch_(1) {
  }

  void ScheduledTask(const Status& status, const Status& expected_status) {
    CHECK_EQ(expected_status.CodeAsString(), status.CodeAsString());
    latch_.CountDown();
  }

  void ScheduledTaskCheckThread(const Status& status, const Thread* thread) {
    CHECK_OK(status);
    CHECK_EQ(thread, Thread::current_thread());
    latch_.CountDown();
  }

  void ScheduledTaskScheduleAgain(const Status& status) {
    messenger_->ScheduleOnReactor(
        boost::bind(&ReactorTest::ScheduledTaskCheckThread, this, _1,
                    Thread::current_thread()),
        MonoDelta::FromMilliseconds(0));
    latch_.CountDown();
  }

 protected:
  const shared_ptr<Messenger> messenger_;
  CountDownLatch latch_;
};

TEST_F(ReactorTest, TestFunctionIsCalled) {
  messenger_->ScheduleOnReactor(
      boost::bind(&ReactorTest::ScheduledTask, this, _1, Status::OK()),
      MonoDelta::FromSeconds(0));
  latch_.Wait();
}

TEST_F(ReactorTest, TestFunctionIsCalledAtTheRightTime) {
  MonoTime before = MonoTime::Now(MonoTime::FINE);
  messenger_->ScheduleOnReactor(
      boost::bind(&ReactorTest::ScheduledTask, this, _1, Status::OK()),
      MonoDelta::FromMilliseconds(100));
  latch_.Wait();
  MonoTime after = MonoTime::Now(MonoTime::FINE);
  MonoDelta delta = after.GetDeltaSince(before);
  CHECK_GE(delta.ToMilliseconds(), 100);
}

TEST_F(ReactorTest, TestFunctionIsCalledIfReactorShutdown) {
  messenger_->ScheduleOnReactor(
      boost::bind(&ReactorTest::ScheduledTask, this, _1,
                  Status::Aborted("doesn't matter")),
      MonoDelta::FromSeconds(60));
  messenger_->Shutdown();
  latch_.Wait();
}

TEST_F(ReactorTest, TestReschedulesOnSameReactorThread) {
  // Our scheduled task will schedule yet another task.
  latch_.Reset(2);

  messenger_->ScheduleOnReactor(
      boost::bind(&ReactorTest::ScheduledTaskScheduleAgain, this, _1),
      MonoDelta::FromSeconds(0));
  latch_.Wait();
  latch_.Wait();
}

} // namespace rpc
} // namespace kudu
