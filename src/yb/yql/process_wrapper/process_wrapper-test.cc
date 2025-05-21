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

#include <chrono>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

#include "yb/yql/process_wrapper/process_wrapper.h"

using namespace std::literals;

namespace yb {

class TestSleepProcessWrapper : public ProcessWrapper {
 public:
  explicit TestSleepProcessWrapper(int sleep) : sleep_(sleep) {}

  Status PreflightCheck() override {
    return Status::OK();
  }

  Status ReloadConfig() override {
    return Status::OK();
  }

  Status UpdateAndReloadConfig() override {
    return Status::OK();
  }

  Status Start() override {
    std::vector<std::string> argv{"/usr/bin/sleep", AsString(sleep_)};
    proc_.emplace(argv[0], argv);
    RETURN_NOT_OK(proc_->Start());
    LOG(INFO) << "Started pid: " << proc_->pid();
    return Status::OK();
  }

 private:
  int sleep_;
};

class TestSleepProcessSupervisor : public ProcessSupervisor {
 public:
  explicit TestSleepProcessSupervisor(int sleep): sleep_(sleep) {}
  ~TestSleepProcessSupervisor() {
    LOG(INFO) << "~TestSleepProcessSupervisor";
  }

  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override {
    return std::make_shared<TestSleepProcessWrapper>(sleep_);
  }

  std::string GetProcessName() override {
    return "test sleep process";
  }
 private:
  int sleep_;
};

// Test is disabled because it will cause test server instability.
TEST(ProcessWrapperTest, YB_DISABLE_TEST(HitThreadsLimit)) {
  std::vector<std::unique_ptr<TestSleepProcessSupervisor>> supervisors;

  // This process is supposed to restart in a loop to reproduce crash after hitting limit on
  // number of threads.
  TestSleepProcessSupervisor supervisor(/* sleep = */ 1);
  ASSERT_OK(supervisor.Start());

  std::atomic<bool> stop{false};
  std::vector<scoped_refptr<Thread>> threads;

  auto deadline = CoarseMonoClock::now() + 120s;
  bool hit_threads_limit = false;
  while (CoarseMonoClock::now() < deadline) {
    scoped_refptr<Thread> thread;
    auto status = Thread::Create(
        "termination_monitor", "sigterm_loop",
        [&stop]() {
          while (!stop.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(50ms);
          }
        },
        &thread);
    if (!status.ok()) {
      LOG(INFO) << "Failed to start thread: " << status;
      if (!hit_threads_limit) {
        // Give process time to attempt restarts and reproduce the bug.
        deadline = CoarseMonoClock::now() + 3s;
        hit_threads_limit = true;
      }
      continue;
    }
    threads.push_back(thread);
    YB_LOG_EVERY_N_SECS(INFO, 5) << "Threads running: " << threads.size();
  }

  stop.store(true);
  for (auto& thread : threads) {
    thread->Join();
  }

  supervisor.Stop();
}

class TestCatProcessWrapper : public ProcessWrapper {
 public:
  Status PreflightCheck() override;
  Status ReloadConfig() override;
  Status UpdateAndReloadConfig() override;
  Status Start() override;
};


class TestCatProcessSupervisor : public ProcessSupervisor {
 public:
  TestCatProcessSupervisor() {}
  ~TestCatProcessSupervisor();
  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override;
  uint64_t cnt();

 protected:
  std::string GetProcessName() override;

 private:
  uint64_t cnt_{0};
};

Status RestartAndWaitForRespawn(TestCatProcessSupervisor& supervisor, MonoDelta timeout);

std::unique_ptr<TestCatProcessSupervisor> MakeTestSupervisor();

TEST(TestProcessSupervisor, StopUnStartedSupervisor) {
  auto supervisor = MakeTestSupervisor();
  supervisor->Stop();
}

TEST(TestProcessSupervisor, StartPausedAndStop) {
  auto supervisor = MakeTestSupervisor();
  ASSERT_OK(supervisor->InitPaused());
  supervisor->Stop();
}

TEST(TestProcessSupervisor, StartPausedAndRestart) {
  auto supervisor = MakeTestSupervisor();
  ASSERT_OK(supervisor->InitPaused());
  ASSERT_OK(RestartAndWaitForRespawn(*supervisor, MonoDelta::FromSeconds(10)));
  // todo(zdrudi): There might be a race here. Use sleeps to avoid for now.
  std::this_thread::sleep_for(1s);
  ASSERT_OK(supervisor->Pause());
  supervisor->Stop();
}

TEST(TestProcessSupervisor, StartAndRestart) {
  auto supervisor = MakeTestSupervisor();
  ASSERT_OK(supervisor->Start());
  // todo(zdrudi): There might be a race here. Use sleeps to avoid for now.
  std::this_thread::sleep_for(1s);
  ASSERT_OK(RestartAndWaitForRespawn(*supervisor, MonoDelta::FromSeconds(10)));
  std::this_thread::sleep_for(1s);
  ASSERT_OK(supervisor->Pause());
  supervisor->Stop();
}

TEST(TestProcessSupervisor, RestartOnUnStarted) {
  auto supervisor = MakeTestSupervisor();
  auto s = supervisor->Restart();
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsIllegalState());
}

Status TestCatProcessWrapper::PreflightCheck() { return Status::OK(); }

Status TestCatProcessWrapper::ReloadConfig() { return Status::OK(); }

Status TestCatProcessWrapper::UpdateAndReloadConfig() { return Status::OK(); }

Status TestCatProcessWrapper::Start() {
  std::vector<std::string> argv{"cat"};
  proc_.emplace("/bin/cat", argv);
  return proc_->Start();
}

TestCatProcessSupervisor::~TestCatProcessSupervisor() {
  Stop();
}

std::shared_ptr<ProcessWrapper> TestCatProcessSupervisor::CreateProcessWrapper() {
  cnt_++;
  LOG(INFO) << "Creating process wrapper, count is: " << cnt_;
  return std::make_shared<TestCatProcessWrapper>();
}

uint64_t TestCatProcessSupervisor::cnt() {
  std::lock_guard lock(mtx_);
  return cnt_;
}

std::string TestCatProcessSupervisor::GetProcessName() { return "test_process"; }

Status RestartAndWaitForRespawn(TestCatProcessSupervisor& supervisor, MonoDelta timeout) {
  auto current_count = supervisor.cnt();
  RETURN_NOT_OK(supervisor.Restart());
  return WaitFor(
      [&supervisor, current_count]() -> bool { return supervisor.cnt() > current_count; },
      MonoDelta::FromSeconds(10), "Waiting for supervisor thread to restart process");
}

std::unique_ptr<TestCatProcessSupervisor> MakeTestSupervisor() {
  return std::make_unique<TestCatProcessSupervisor>();
}

}  // namespace yb
