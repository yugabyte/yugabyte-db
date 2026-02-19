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

#pragma once

#include "yb/util/status.h"
#include "yb/util/subprocess.h"
#include "yb/util/thread.h"

namespace yb {

// ProcessWrapper is just a wrapper class for handling the details regarding running a
// process (like, the Kill method used and command used for running the process).
// It is used to invoke a child process once and is not thread-safe.
class ProcessWrapper {
 public:
  virtual ~ProcessWrapper() {}

  // Checks, before executing the command to start the process.
  virtual Status PreflightCheck() = 0;

  virtual Status ReloadConfig() = 0;
  virtual Status UpdateAndReloadConfig() = 0;
  virtual Status Start() = 0;
  void Kill();
  void Kill(int signal);
  virtual void Shutdown();

  // Waits for the running process to complete. Returns the exit code or an error.
  // Non-zero exit codes are considered non-error cases for the purpose of this function.
  Result<int> Wait();

 protected:
  static Status CheckExecutableValid(const std::string& executable_path);
  std::optional<Subprocess> proc_;
};

// ProcessSupervisor deals with the lifecycle of process (like, Start() and Stop() methods).
// ProcessSupervisor starts a separate thread to keep a process running in the background,
// monitor it and restart it in case it crashes.

// These states apply to the supervisor itself, not the supervised process. They control the
// behaviour of the monitor thread.
YB_DEFINE_ENUM(YbSubProcessState, (kNotStarted)(kRunning)(kStopping)(kPaused));

// kNotStarted - the ProcessSupervisor is in this state after construction until Start is called.
// The monitor thread and the supervised process do not yet exist.
//   possible state transitions:
//       kRunning
//       kStopping
// kRunning - when Start is called, the ProcessSupervisor transitions to kRunning. It spawns the
// process and creates a monitor thread. The monitor thread will respawn the supervised process if
// it dies.
//   possible state transitions:
//       kPaused
//       kStopping

// kPaused - the supervised process is killed and the monitor thread will not respawn it.
//   possible state transitions:
//       kRunning
//       kStopping

// kStopping - the supervised process is sent signals to stop and the monitor thread should stop.
//   possible state transitions:
//       (none)
class ProcessSupervisor {
 public:
  virtual ~ProcessSupervisor() {}
  virtual void Stop();
  Status Start();
  Status InitPaused();

  // Returns the current state of the process.
  YbSubProcessState GetState();
  Status Restart();
  Status Pause();

 protected:
  virtual std::shared_ptr<ProcessWrapper> CreateProcessWrapper() = 0;
  std::mutex mtx_;
  std::shared_ptr<ProcessWrapper> process_wrapper_ = nullptr;
  virtual void PrepareForStop() {}
  virtual Status PrepareForStart() { return Status::OK(); }
  virtual std::string GetProcessName() = 0;

 private:
  Status Init(YbSubProcessState target_state);
  // Compares the expected and current state.
  // Caller function need to lock the mutex before calling the function.
  Status ExpectStateUnlocked(YbSubProcessState state) REQUIRES(mtx_);

  // Start a process.
  // Caller function needs to lock the mutex before calling the function.
  Status StartProcessUnlocked() REQUIRES(mtx_);
  Status InitializeProcessWrapperUnlocked() REQUIRES(mtx_);

  Status StopProcessAndChangeState(YbSubProcessState new_state) EXCLUDES(mtx_);

  // Current state of the process.
  YbSubProcessState state_ GUARDED_BY(mtx_) = YbSubProcessState::kNotStarted;

  scoped_refptr<Thread> supervisor_thread_;

  CountDownLatch thread_finished_latch_{1};
  std::condition_variable cond_;
  void RunThread();
};

}  // namespace yb
