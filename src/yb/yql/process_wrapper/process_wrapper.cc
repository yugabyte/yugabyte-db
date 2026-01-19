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

#include "yb/yql/process_wrapper/process_wrapper.h"

#include "yb/util/env.h"
#include "yb/util/scope_exit.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

namespace yb {

// ------------------------------------------------------------------------------------------------
// ProcessWrapper: managing one instance of a child process
// ------------------------------------------------------------------------------------------------
Status ProcessWrapper::CheckExecutableValid(const std::string& executable_path) {
  if (VERIFY_RESULT(Env::Default()->IsExecutableFile(executable_path))) {
    return Status::OK();
  }
  return STATUS_FORMAT(NotFound, "Not an executable file: $0", executable_path);
}

Result<int> ProcessWrapper::Wait() {
  if (!proc_) {
    return STATUS(IllegalState, "Child process has not been started, cannot wait for it to exit");
  }
  int retcode;
  RETURN_NOT_OK(proc_->Wait(&retcode));
  return retcode;
}

void ProcessWrapper::Kill() { Kill(SIGINT); }

void ProcessWrapper::Kill(int signal) {
  if (proc_) {
    WARN_NOT_OK(proc_->KillNoCheckIfRunning(signal), "Kill process failed");
  }
}

void ProcessWrapper::Shutdown() { Kill(); }

// ------------------------------------------------------------------------------------------------
// ProcessWrapper: managing one instance of a child process
// ------------------------------------------------------------------------------------------------
YbSubProcessState ProcessSupervisor::GetState() {
  std::lock_guard lock(mtx_);
  return state_;
}

Status ProcessSupervisor::ExpectStateUnlocked(YbSubProcessState expected_state) {
  SCHECK_EQ(state_, expected_state, IllegalState, "Process is in unexpected state");
  return Status::OK();
}

void ProcessSupervisor::RunThread() {
  auto se = ScopeExit([this] {
    thread_finished_latch_.CountDown();
  });
  std::string process_name = GetProcessName();
  while (true) {
    if (process_wrapper_) {
      Result<int> wait_result = process_wrapper_->Wait();
      if (wait_result.ok()) {
        int ret_code = *wait_result;
        if (ret_code == 0) {
          LOG(INFO) << process_name << " exited normally";
        } else {
          util::LogWaitCode(ret_code, process_name);
        }
      } else {
        LOG(WARNING) << "Failed when waiting for process to exit: " << wait_result.status();

        // Don't continue waiting in the loop if it is IllegalState as this means the process is not
        // currently running at all, perhaps due to failure to start. In this case, the
        // process_wrapper is not initilized. So there isn't a process to wait.
        if (!wait_result.status().IsIllegalState()) {
          LOG(INFO) << "Wait a bit next process_wrapper wait-check for " << process_name;
          SleepFor(MonoDelta::FromSeconds(1));
          continue;
        }
      }
    }

    {
      UniqueLock lock(mtx_);
      if (state_ == YbSubProcessState::kPaused) {
        LOG(INFO) << "Supervisor thread for " << process_name << " waiting on paused state.";
        cond_.wait(GetLockForCondition(lock), [this]() REQUIRES(mtx_) {
          return this->state_ != YbSubProcessState::kPaused;
        });
      }
      process_wrapper_.reset();
      if (state_ == YbSubProcessState::kStopping) {
        break;
      }
      LOG(INFO) << "Restarting " << process_name << " process";
      Status start_status = StartProcessUnlocked();
      if (!start_status.ok()) {
        LOG(WARNING) << "Failed trying to start " << process_name
                     << " process: " << start_status << ", waiting a bit";
        SleepFor(MonoDelta::FromSeconds(1));
      }
    }
  }
  LOG(INFO) << "Supervisor thread for " << process_name << " exiting";
}

Status ProcessSupervisor::StartProcessUnlocked() {
  if (process_wrapper_) {
    RSTATUS_DCHECK(!process_wrapper_, IllegalState, "Expecting 'process_wrapper_' to not be set");
  }
  auto process_wrapper = CreateProcessWrapper();
  RETURN_NOT_OK(process_wrapper->Start());

  process_wrapper_.swap(process_wrapper);
  return Status::OK();
}

Status ProcessSupervisor::Start() {
  return Init(YbSubProcessState::kRunning);
}

Status ProcessSupervisor::InitPaused() {
  return Init(YbSubProcessState::kPaused);
}

Status ProcessSupervisor::InitializeProcessWrapperUnlocked() {
  if (process_wrapper_) {
    RSTATUS_DCHECK(!process_wrapper_, IllegalState, "Expecting 'process_wrapper_' to not be set");
  }
  process_wrapper_ = CreateProcessWrapper();
  return Status::OK();
}

Status ProcessSupervisor::Restart() {
  return StopProcessAndChangeState(YbSubProcessState::kRunning);
}

Status ProcessSupervisor::Pause() {
  return StopProcessAndChangeState(YbSubProcessState::kPaused);
}

void ProcessSupervisor::Stop() {
  LOG(INFO) << "Stopping " << GetProcessName();
  {
    std::lock_guard lock(mtx_);
    state_ = YbSubProcessState::kStopping;
    PrepareForStop();
    if (process_wrapper_) {
      process_wrapper_->Shutdown();
    }
    if (!supervisor_thread_) {
      return;
    }
  }
  cond_.notify_one();
  auto start = CoarseMonoClock::now();
  for (;;) {
    if (thread_finished_latch_.WaitFor(10s)) {
      break;
    }
    const auto passed = MonoDelta(CoarseMonoClock::now() - start);
    if (passed >= 1min) {
      LOG(DFATAL) << GetProcessName() << " did not gracefully exit after " << passed
                  << ". Force killing it with SIGKILL";
      std::lock_guard lock(mtx_);
      if (process_wrapper_) {
        process_wrapper_->Kill(SIGKILL);
      }
      break;
    } else {
      LOG(WARNING) << GetProcessName() << " did not gracefully exist after " << passed;

    }
  }
  supervisor_thread_->Join();
}

Status ProcessSupervisor::StopProcessAndChangeState(YbSubProcessState new_state) {
  {
    std::lock_guard lock(mtx_);
    if (state_ != YbSubProcessState::kPaused && state_ != YbSubProcessState::kRunning) {
      return STATUS_FORMAT(
          IllegalState, "State must be either paused or running, state is $0", state_);
    }
    if (process_wrapper_) {
      process_wrapper_->Shutdown();
    }
    state_ = new_state;
  }
  cond_.notify_one();
  return Status::OK();
}

Status ProcessSupervisor::Init(YbSubProcessState target_state) {
  if (target_state != YbSubProcessState::kPaused && target_state != YbSubProcessState::kRunning) {
    return STATUS_FORMAT(
        InvalidArgument, "First state after kNotStarted must be kRunning or kPaused, not $0",
        target_state);
  }
  std::lock_guard lock(mtx_);
  std::string process_name = GetProcessName();
  RETURN_NOT_OK(ExpectStateUnlocked(YbSubProcessState::kNotStarted));
  RETURN_NOT_OK(PrepareForStart());
  if (target_state == YbSubProcessState::kRunning) {
    LOG(INFO) << "Starting " << process_name << " process";
    RETURN_NOT_OK(StartProcessUnlocked());
  }

  state_ = target_state;
  std::string thread_name = process_name + " supervisor";
  auto status = Thread::Create(
      thread_name, thread_name, &ProcessSupervisor::RunThread, this, &supervisor_thread_);
  if (!status.ok()) {
    supervisor_thread_.reset();
    state_ = YbSubProcessState::kNotStarted;
    return status;
  }

  return Status::OK();
}

}  // namespace yb
