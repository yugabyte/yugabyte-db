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

#include "yb/util/test_util.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/process_wrapper/process_wrapper.h"

using namespace std::literals;

namespace yb {

class ProcessWrapperTest : public YBTest {
};

class TestProcessWrapper : public ProcessWrapper {
 public:
  explicit TestProcessWrapper(int sleep) : sleep_(sleep) {}

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

class TestProcessSupervisor : public ProcessSupervisor {
 public:
  explicit TestProcessSupervisor(int sleep): sleep_(sleep) {}
  ~TestProcessSupervisor() {
    LOG(INFO) << "~TestProcessSupervisor";
  }

  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override {
    return std::make_shared<TestProcessWrapper>(sleep_);
  }

  std::string GetProcessName() override {
    return "test process";
  }
 private:
  int sleep_;
};

// Test is disabled because it will cause test server instability.
TEST_F(ProcessWrapperTest, YB_DISABLE_TEST(HitThreadsLimit)) {
  std::vector<std::unique_ptr<TestProcessSupervisor>> supervisors;

  // This process is supposed to restart in a loop to reproduce crash after hitting limit on
  // number of threads.
  TestProcessSupervisor supervisor(/* sleep = */ 1);
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

}  // namespace yb
