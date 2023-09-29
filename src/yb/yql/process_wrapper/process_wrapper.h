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

#include <boost/optional.hpp>

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

  // Waits for the running process to complete. Returns the exit code or an error.
  // Non-zero exit codes are considered non-error cases for the purpose of this function.
  Result<int> Wait();

 protected:
  static Status CheckExecutableValid(const std::string& executable_path);
  boost::optional<Subprocess> proc_;
};

YB_DEFINE_ENUM(YbSubProcessState, (kNotStarted)(kRunning)(kStopping)(kStopped));

// ProcessSupervisor deals with the lifecycle of process (like, Start() and Stop() methods).
// ProcessSupervisor starts a separate thread to keep a process running in the background,
// monitor it and restart it in case it crashes.
class ProcessSupervisor {
 public:
  virtual ~ProcessSupervisor() {}
  void Stop();
  Status Start();

  // Returns the current state of the process.
  YbSubProcessState GetState();

 protected:
  virtual std::shared_ptr<ProcessWrapper> CreateProcessWrapper() = 0;
  std::mutex mtx_;
  std::shared_ptr<ProcessWrapper> process_wrapper_ = NULL;
  virtual void PrepareForStop() {}
  virtual Status PrepareForStart() { return Status::OK(); }
  virtual std::string GetProcessName() = 0;

 private:
  // Compares the expected and current state.
  // Caller function need to lock the mutex before calling the function.
  Status ExpectStateUnlocked(YbSubProcessState state) REQUIRES(mtx_);

  // Start a process.
  // Caller function need to lock the mutex before calling the function.
  Status StartProcessUnlocked() REQUIRES(mtx_);

  // Current state of the process.
  YbSubProcessState state_ GUARDED_BY(mtx_) = YbSubProcessState::kNotStarted;

  scoped_refptr<Thread> supervisor_thread_;
  void RunThread();
};

struct ProcessWrapperCommonConfig {
  std::string certs_dir;
  std::string certs_for_client_dir;
  std::string cert_base_name;
  bool enable_tls = false;
};

}  // namespace yb
