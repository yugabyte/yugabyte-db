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

#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/poller.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/test_macros.h"

using namespace std::literals;

namespace yb::rpc {

class PollerTest : public RpcTestBase {
 public:
  void SetUp() override {
    // A single io thread so the test can hold the scheduler's queue back.
    pool_.emplace("test", 1);
    scheduler_.emplace(&pool_->io_service());
  }

  void TearDown() override {
    scheduler_->Shutdown();
    pool_->Shutdown();
    pool_->Join();
  }

 protected:
  std::optional<IoThreadPool> pool_;
  std::optional<Scheduler> scheduler_;
};

TEST_F(PollerTest, StartPauseResume) {
  std::atomic<int> calls{0};
  Poller poller("PollerTest: ", [&calls] { ++calls; });
  poller.Start(&*scheduler_, 10ms);
  ASSERT_OK(WaitFor([&calls] { return calls.load() >= 3; }, 5s, "poller ticks"));

  poller.Pause();
  // Pause leaves the pending tick scheduled; it fires once as a no-op. Let it drain, then check
  // that ticks have stopped.
  std::this_thread::sleep_for(200ms);
  auto calls_at_pause = calls.load();
  std::this_thread::sleep_for(200ms);
  ASSERT_EQ(calls.load(), calls_at_pause);

  poller.Resume();
  ASSERT_OK(WaitFor(
      [&calls, calls_at_pause] { return calls.load() > calls_at_pause; }, 5s, "poller resumed"));

  poller.Shutdown();
}

// Resume() must not lose the wake-up when it runs while the task scheduled before a Pause() is
// still pending (GH #35051). Resume() sees poll_task_id_ set and skips Schedule(), so that task
// must keep polling alive instead of stopping.
TEST_F(PollerTest, ResumeWhileTaskPending) {
  std::atomic<int> calls{0};
  Poller poller("PollerTest: ", [&calls] { ++calls; });
  poller.Pause();
  poller.Start(&*scheduler_, 100ms);

  // Hold the only io thread so every scheduler handler queues behind this one and the task
  // scheduled by Resume() cannot run until the latch is released.
  CountDownLatch io_blocked(1), release_io(1);
  pool_->io_service().post([&io_blocked, &release_io] {
    io_blocked.CountDown();
    release_io.Wait();
  });
  io_blocked.Wait();

  poller.Resume();  // Schedules task T, poll_task_id_ = T.
  poller.Pause();   // T stays pending.
  poller.Resume();  // paused_ = false. Sees poll_task_id_ set and does not schedule.

  release_io.CountDown();

  // A correct poller keeps ticking after that sequence.
  ASSERT_OK(WaitFor([&calls] { return calls.load() >= 1; }, 3s, "poller ticks after resume"));

  poller.Shutdown();
}

}  // namespace yb::rpc
