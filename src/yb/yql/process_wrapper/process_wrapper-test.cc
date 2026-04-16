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
#include <latch>
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

class FailStartProcessWrapper : public ProcessWrapper {
 public:
  explicit FailStartProcessWrapper(bool succeed_on_start);

  Status PreflightCheck() override;
  Status ReloadConfig() override;

  Status UpdateAndReloadConfig() override;

  Status Start() override;

  Status WaitForStart();

 private:
  bool succeed_on_start_;
  Status start_status_;
  std::latch started_;
};

class FailStartProcessSupervisor : public ProcessSupervisor {
 public:
  FailStartProcessSupervisor();

  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override;

  std::string GetProcessName() override;

  std::shared_ptr<FailStartProcessWrapper> WaitForProcessSpawn(uint64_t count);

  void SetSucceedProcessStart(bool succeed_process_start);

 private:
  uint64_t start_count_{0};
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic_bool succeed_process_start_;
  std::shared_ptr<FailStartProcessWrapper> last_spawned_proc_;
};

TEST(ProcessWrapperTest, ProcessWrapperStartFails) {
  FailStartProcessSupervisor supervisor;
  ASSERT_OK(supervisor.Start());
  auto first_proc = supervisor.WaitForProcessSpawn(1);
  ASSERT_OK(first_proc->WaitForStart());
  // Now fail the next process start.
  supervisor.SetSucceedProcessStart(false);
  ASSERT_OK(supervisor.Restart());
  // Give 2 go rounds to ensure the process runner thread has dealt with the Start() failure.
  auto last_failed_proc = supervisor.WaitForProcessSpawn(3);
  ASSERT_NOK(last_failed_proc->WaitForStart());
  supervisor.SetSucceedProcessStart(true);
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

FailStartProcessWrapper::FailStartProcessWrapper(bool succeed_on_start)
    : succeed_on_start_(succeed_on_start), started_(1) {}

Status FailStartProcessWrapper::PreflightCheck() { return Status::OK(); }

Status FailStartProcessWrapper::ReloadConfig() { return Status::OK(); }

Status FailStartProcessWrapper::UpdateAndReloadConfig() { return Status::OK(); }

Status FailStartProcessWrapper::Start() {
  if (!succeed_on_start_) {
    start_status_ = STATUS(IllegalState, "Failed to spawn sleep process due to count hit");
    started_.count_down();
    return start_status_;
  }
  std::vector<std::string> argv{"cat"};
  proc_.emplace("/bin/cat", argv);
  start_status_ = proc_->Start();
  started_.count_down();
  return start_status_;
}

Status FailStartProcessWrapper::WaitForStart() {
  started_.wait();
  return start_status_;
}

FailStartProcessSupervisor::FailStartProcessSupervisor() : succeed_process_start_(true) {}

std::shared_ptr<ProcessWrapper> FailStartProcessSupervisor::CreateProcessWrapper() {
  auto proc = std::make_shared<FailStartProcessWrapper>(succeed_process_start_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    start_count_++;
    last_spawned_proc_ = proc;
  }
  cv_.notify_all();
  return proc;
}

std::string FailStartProcessSupervisor::GetProcessName() { return "test_process"; }

std::shared_ptr<FailStartProcessWrapper> FailStartProcessSupervisor::WaitForProcessSpawn(
    uint64_t count) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this, &count] { return start_count_ >= count; });
  return last_spawned_proc_;
}

void FailStartProcessSupervisor::SetSucceedProcessStart(bool succeed_process_start) {
  succeed_process_start_ = succeed_process_start;
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
