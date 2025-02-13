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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#pragma once

#include <signal.h>
#include <spawn.h>

#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/enums.h"
#include "yb/util/status.h"

namespace yb {
namespace util {

// Handle and log the status code from waitpid().
// The status code is encoded. We need to call the proper macro
// to decode it to be readable.
void LogWaitCode(int ret_code, const std::string &process_name);

} // namespace util

YB_DEFINE_ENUM(StdFdType,
               ((kIn, STDIN_FILENO))
               ((kOut, STDOUT_FILENO))
               ((kErr, STDERR_FILENO)));

using StdFdTypes = EnumBitSet<StdFdType>;

YB_DEFINE_ENUM(SubprocessState, (kNotStarted)(kRunning)(kExited));
YB_DEFINE_ENUM(SubprocessStreamMode, (kDisabled)(kShared)(kPiped));

// Wrapper around a spawned subprocess.
//
// program will be treated as an absolute path unless it begins with a dot or a
// slash.
//
// This takes care of creating pipes to/from the subprocess and offers
// basic functionality to wait on it or send signals.
// By default, child process only has stdin captured and separate from the parent.
// The stdout/stderr streams are shared with the parent by default.
//
// The process may only be started and waited on/killed once.
//
// Optionally, user may change parent/child stream sharing. Also, a user may disable
// a subprocess stream. A user cannot do both.
//
// Note that, when the Subprocess object is destructed, the child process
// will be forcibly SIGKILLed to avoid orphaning processes.
class Subprocess {
 public:
  Subprocess(std::string program, std::vector<std::string> argv);
  ~Subprocess();

  // Disable subprocess stream output.  Must be called before subprocess starts.
  void DisableStderr() EXCLUDES(state_lock_);
  void DisableStdout() EXCLUDES(state_lock_);

  // Share a stream with parent. Must be called before subprocess starts.
  // Cannot set sharing at all if stream is disabled.
  void ShareParentStdin() EXCLUDES(state_lock_) {
    SetFdShared(STDIN_FILENO, SubprocessStreamMode::kShared);
  }
  void ShareParentStdout() EXCLUDES(state_lock_) {
    SetFdShared(STDOUT_FILENO, SubprocessStreamMode::kShared);
  }
  void ShareParentStderr() EXCLUDES(state_lock_) {
    SetFdShared(STDERR_FILENO, SubprocessStreamMode::kShared);
  }

  void PipeParentStdin() EXCLUDES(state_lock_) {
    SetFdShared(STDIN_FILENO, SubprocessStreamMode::kPiped);
  }
  void PipeParentStdout() EXCLUDES(state_lock_) {
    SetFdShared(STDOUT_FILENO, SubprocessStreamMode::kPiped);
  }
  void PipeParentStderr() EXCLUDES(state_lock_) {
    SetFdShared(STDERR_FILENO, SubprocessStreamMode::kPiped);
  }

  // Marks a non-standard file descriptor which should not be closed after
  // forking the child process.
  void InheritNonstandardFd(int fd);

  // Start the subprocess. Can only be called once.
  //
  // This returns a bad Status if the fork() fails. However,
  // note that if the executable path was incorrect such that
  // exec() fails, this will still return Status::OK. You must
  // use Wait() to check for failure.
  Status Start() EXCLUDES(state_lock_);

  // Similar to Call. Starts the sub process and waits for it to exit.
  // log_stdout_and_stderr: Captures and logs the stdout and stderr to glog.
  Status Run(bool log_stdout_and_stderr = true) EXCLUDES(state_lock_);

  // Wait for the subprocess to exit. The return value is the same as
  // that of the waitpid() syscall. Only call after starting.
  //
  // NOTE: unlike the standard wait(2) call, this may be called multiple
  // times. If the process has exited, it will repeatedly return the same
  // exit code.
  //
  // The integer pointed by ret (ret must be non-NULL) is set to the "status" value as described
  // by the waitpid documentation (https://linux.die.net/man/2/waitpid), paraphrasing below.
  //
  // If ret is not NULL, wait() and waitpid() store status information in the int to which it
  // points. This integer can be inspected with the following macros (which take the integer
  // itself as an argument, not a pointer to it, as is done in wait() and waitpid()!):
  // WCONTINUED, WCOREDUMP, WEXITSTATUS, WIFCONTINUED, WIFEXITED, WIFSIGNALED, WIFSTOPPED,
  // WNOHANG, WSTOPSIG, WTERMSIG, WUNTRACED.
  Status Wait(int* ret) EXCLUDES(state_lock_);

  Status Wait() EXCLUDES(state_lock_);

  // Like the above, but does not block. This returns Status::TimedOut
  // immediately if the child has not exited. Otherwise returns Status::OK
  // and sets *ret. Only call after starting.
  //
  // NOTE: unlike the standard wait(2) call, this may be called multiple
  // times. If the process has exited, it will repeatedly return the same
  // exit code.
  Status WaitNoBlock(int* ret) EXCLUDES(state_lock_);

  // Send a signal to the subprocess.
  // Note that this does not reap the process -- you must still Wait()
  // in order to reap it. Only call after starting.
  Status Kill(int signal) EXCLUDES(state_lock_);

  // Similar to Kill, but does not enforce that the process must be running.
  Status KillNoCheckIfRunning(int signal) EXCLUDES(state_lock_);

  // Returns true if the process is running.
  bool IsRunning() const EXCLUDES(state_lock_);

  // Helper method that creates a Subprocess, issues a Start() then a Wait().
  // Expects a blank-separated list of arguments, with the first being the
  // full path to the executable.
  // The returned Status will only be OK if all steps were successful and
  // the return code was 0.
  static Status Call(const std::string& arg_str);

  // Same as above, but accepts a vector that includes the path to the
  // executable as argv[0] and the arguments to the program in argv[1..n].
  static Status Call(const std::vector<std::string>& argv, bool log_stdout_and_stderr = false);

  // Same as above, but collects the output from the child process stdout into
  // the output parameter.
  static Status Call(
      const std::vector<std::string>& argv, std::string* output, std::string* error = nullptr);

  // Return the pipe fd to the child's standard stream.
  // Stream should not be disabled or shared.
  int to_child_stdin_fd() const EXCLUDES(state_lock_) { return CheckAndOffer(STDIN_FILENO); }
  int from_child_stdout_fd() const EXCLUDES(state_lock_) { return CheckAndOffer(STDOUT_FILENO); }
  int from_child_stderr_fd() const EXCLUDES(state_lock_) { return CheckAndOffer(STDERR_FILENO); }

  // Release control of the file descriptor for the child's stream, only if piped.
  // Writes to this FD show up on stdin in the subprocess
  int ReleaseChildStdinFd() EXCLUDES(state_lock_) { return ReleaseChildFd(STDIN_FILENO); }
  // Reads from this FD come from stdout of the subprocess
  int ReleaseChildStdoutFd() EXCLUDES(state_lock_) { return ReleaseChildFd(STDOUT_FILENO); }
  // Reads from this FD come from stderr of the subprocess
  int ReleaseChildStderrFd() EXCLUDES(state_lock_) { return ReleaseChildFd(STDERR_FILENO); }

  pid_t pid() const;

  void SetEnv(const std::string& key, const std::string& value);

  // Issues Start() then Wait() and collects the desired streams from the child process
  // into the output parameter(s).
  Status Call(std::string* output, std::string* error = nullptr);

  // Writes pid to cgroup specified by path
  void AddPIDToCGroup(const std::string& path, pid_t pid);

 private:

  struct ChildPipes {
    int child_stdin[2] = {-1, -1};
    int child_stdout[2] = {-1, -1};
    int child_stderr[2] = {-1, -1};
  };

  Status StartWithForkExec() REQUIRES(state_lock_);
  Status StartWithPosixSpawn() REQUIRES(state_lock_);

  void SetFdShared(int stdfd, SubprocessStreamMode mode) EXCLUDES(state_lock_);
  int CheckAndOffer(int stdfd) const EXCLUDES(state_lock_);
  int ReleaseChildFd(int stdfd) EXCLUDES(state_lock_);
  Status DoWait(int* ret, int options) EXCLUDES(state_lock_);
  SubprocessState state() const EXCLUDES(state_lock_);
  Status KillInternal(int signal, bool must_be_running) EXCLUDES(state_lock_);

  // Combine the existing environment with the overrides from env_, and return it as a vector
  // of name=value strings and a pointer array terminated with a null, referring to the vector,
  // suitable for use with standard C library functions.
  std::pair<std::vector<std::string>, std::vector<char*>> GetCombinedEnv();

  Result<std::vector<char*>> GetArgvPtrs() REQUIRES(state_lock_);
  Result<ChildPipes> CreateChildPipes() REQUIRES(state_lock_);

  Status ConfigureFileActionsForPosixSpawn(
      posix_spawn_file_actions_t* file_actions, const ChildPipes& child_pipes)
      REQUIRES(state_lock_);

  Status ConfigureOutputStreamActionForPosixSpawn(
      posix_spawn_file_actions_t* file_actions, int out_stream_fd, int child_write_fd)
      REQUIRES(state_lock_);

  // Finds all open file descriptors other than stdin/stdout/stderr and not included in
  // ns_fds_inherited_, and registers them in file_actions to be closed during posix_spawn.
  // Returns the list of file descriptors to be closed.
  Result<std::vector<int>> CloseFileDescriptorsForPosixSpawn(
      posix_spawn_file_actions_t* file_actions) REQUIRES(state_lock_);

  void FinalizeParentSideOfPipes(const ChildPipes& child_pipes) REQUIRES(state_lock_);

  void ConfigureOutputStreamAfterFork(int out_stream_fd, int child_write_fd) REQUIRES(state_lock_);

  // ----------------------------------------------------------------------------------------------
  // Fields
  // ----------------------------------------------------------------------------------------------

  std::string program_;
  std::vector<std::string> argv_;

  mutable std::mutex state_lock_;
  SubprocessState state_;
  pid_t child_pid_ GUARDED_BY(state_lock_);
  SubprocessStreamMode fd_state_[3] GUARDED_BY(state_lock_);
  int child_fds_[3] GUARDED_BY(state_lock_);

  // The cached exit result code if Wait() has been called.
  // Only valid if state_ == kExited.
  int cached_rc_ GUARDED_BY(state_lock_);

  std::map<std::string, std::string> env_;

  // List of non-standard file descriptors which should be inherited by the
  // child process.

  std::unordered_set<int> ns_fds_inherited_;

  DISALLOW_COPY_AND_ASSIGN(Subprocess);
};

} // namespace yb
