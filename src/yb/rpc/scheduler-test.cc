//
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
//
//

#include <future>
#include <thread>

#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/test_macros.h"
#include "yb/util/tostring.h"

namespace yb {
namespace rpc {

using namespace std::placeholders;
using namespace std::literals;

class SchedulerTest : public RpcTestBase {
 public:
  void SetUp() override {
    pool_.emplace("test", 1);
    scheduler_.emplace(&pool_->io_service());
  }

  void TearDown() override {
    scheduler_->Shutdown();
    pool_->Shutdown();
    pool_->Join();
  }

 protected:
  boost::optional<IoThreadPool> pool_;
  boost::optional<Scheduler> scheduler_;
};

const int kCycles = 1000;

auto SetPromiseValueToStatusFunctor(std::promise<Status>* promise) {
  return [promise](Status status) { promise->set_value(std::move(status)); };
}

TEST_F(SchedulerTest, TestFunctionIsCalled) {
  for (int i = 0; i != kCycles; ++i) {
    std::promise<Status> promise;
    auto future = promise.get_future();
    scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), 0s);
    ASSERT_OK(future.get());
  }
}

TEST_F(SchedulerTest, TestFunctionIsCalledAtTheRightTime) {
  using yb::ToString;

  for (int i = 0; i != 10; ++i) {
    auto before = std::chrono::steady_clock::now();
    auto delay = 50ms;
    std::promise<Status> promise;
    auto future = promise.get_future();
    scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), delay);
    ASSERT_OK(future.get());
    auto after = std::chrono::steady_clock::now();
    auto delta = after - before;
#if defined(OS_MACOSX)
    auto upper = delay + 200ms;
#else
    auto upper = delay + 50ms;
#endif
    CHECK(delta >= delay) << "Delta: " << ToString(delta) << ", lower bound: " << ToString(delay);
    CHECK(delta < upper) << "Delta: " << ToString(delta) << ", upper bound: " << ToString(upper);
  }
}

TEST_F(SchedulerTest, TestFunctionIsCalledIfReactorShutdown) {
  std::promise<Status> promise;
  auto future = promise.get_future();
  scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), 60s);
  scheduler_->Shutdown();
  ASSERT_TRUE(future.get().IsAborted());
}

TEST_F(SchedulerTest, Abort) {
  for (int i = 0; i != kCycles; ++i) {
    std::promise<Status> promise;
    auto future = promise.get_future();
    auto task_id = scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), 1s);
    scheduler_->Abort(task_id);
    ASSERT_EQ(std::future_status::ready, future.wait_for(100ms));
    auto status = future.get();
    ASSERT_TRUE(status.IsAborted());
  }
}

TEST_F(SchedulerTest, Shutdown) {
  const size_t kThreads = 8;
  std::vector<std::thread> threads;
  std::atomic<size_t> scheduled(0);
  std::atomic<size_t> executed(0);
  std::atomic<bool> failed(false);
  while (threads.size() != kThreads) {
    threads.emplace_back([this, &scheduled, &executed, &failed] {
      while (!failed.load(std::memory_order_acquire)) {
        ++scheduled;
        scheduler_->Schedule([&failed, &executed](const Status& status) {
          ++executed;
          if (!status.ok()) {
            failed.store(true, std::memory_order_release);
          }
        }, 0ms);
      }
    });
  }
  scheduler_->Shutdown();
  for (auto& thread : threads) {
    thread.join();
  }
  ASSERT_GT(scheduled.load(std::memory_order_acquire), 0);
  for (int i = 0; i != 20; ++i) {
    if (scheduled.load(std::memory_order_acquire) == executed.load(std::memory_order_acquire)) {
      break;
    }
    std::this_thread::sleep_for(200ms);
  }
  ASSERT_EQ(scheduled.load(std::memory_order_acquire), executed.load(std::memory_order_acquire));
}

} // namespace rpc
} // namespace yb
