//
// Copyright (c) YugaByte, Inc.
//

#include <future>
#include <thread>

#include <boost/optional/optional.hpp>

#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/tostring.h"

namespace yb {
namespace rpc {

using std::shared_ptr;
using namespace std::placeholders;
using namespace std::literals;

class SchedulerTest : public RpcTestBase {
 public:
  void SetUp() override {
    pool_.emplace(1);
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
    scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), 0s);
    ASSERT_OK(promise.get_future().get());
  }
}

TEST_F(SchedulerTest, TestFunctionIsCalledAtTheRightTime) {
  using yb::ToString;

  for (int i = 0; i != 10; ++i) {
    auto before = std::chrono::steady_clock::now();
    auto delay = 50ms;
    std::promise<Status> promise;
    scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), delay);
    ASSERT_OK(promise.get_future().get());
    auto after = std::chrono::steady_clock::now();
    auto delta = after - before;
    auto upper = delay + 10ms;
    CHECK(delta >= delay) << "Delta: " << ToString(delta) << ", lower bound: " << ToString(delay);
    CHECK(delta < upper) << "Delta: " << ToString(delta) << ", upper bound: " << ToString(upper);
  }
}

TEST_F(SchedulerTest, TestFunctionIsCalledIfReactorShutdown) {
  std::promise<Status> promise;
  scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), 60s);
  scheduler_->Shutdown();
  auto status = promise.get_future().get();
  ASSERT_TRUE(status.IsAborted());
}

TEST_F(SchedulerTest, Abort) {
  for (int i = 0; i != kCycles; ++i) {
    std::promise<Status> promise;
    auto task_id = scheduler_->Schedule(SetPromiseValueToStatusFunctor(&promise), 1s);
    scheduler_->Abort(task_id);
    auto future = promise.get_future();
    ASSERT_EQ(std::future_status::ready, future.wait_for(10ms));
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
  std::this_thread::sleep_for(200ms);
  ASSERT_GT(scheduled.load(std::memory_order_acquire), 0);
  ASSERT_EQ(scheduled.load(std::memory_order_acquire), executed.load(std::memory_order_acquire));
}

} // namespace rpc
} // namespace yb
