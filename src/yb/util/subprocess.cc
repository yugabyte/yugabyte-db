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

#include "yb/util/subprocess.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/once.h"
#include "yb/gutil/port.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/split.h"

#include "yb/util/errno.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::shared_ptr;
using std::string;
using std::vector;
using std::mutex;
using std::unique_lock;
using strings::Split;
using strings::Substitute;

extern char** environ;

namespace yb {

namespace {

static const char* kProcSelfFd =
#if defined(__APPLE__)
  "/dev/fd";
#else
  "/proc/self/fd";
#endif // defined(__APPLE__)

#if defined(__linux__)
#define READDIR readdir64
#define DIRENT dirent64
#else
#define READDIR readdir
#define DIRENT dirent
#endif

void DisableSigPipe() {
  struct sigaction act;

  act.sa_handler = SIG_IGN;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  PCHECK(sigaction(SIGPIPE, &act, nullptr) == 0);
}

void EnsureSigPipeDisabled() {
  static GoogleOnceType once = GOOGLE_ONCE_INIT;
  GoogleOnceInit(&once, &DisableSigPipe);
}

// Since opendir() calls malloc(), this must be called before fork().
// This function is not async-signal-safe.
Result<DIR*> OpenProcFdDir() {
  DIR* dir = opendir(kProcSelfFd);
  if (PREDICT_FALSE(dir == nullptr)) {
    return STATUS(IOError, Substitute("opendir(\"$0\") failed", kProcSelfFd), Errno(errno));
  }
  return dir;
}

// Close the directory stream opened by OpenProcFdDir().
// This function is not async-signal-safe.
void CloseProcFdDir(DIR* dir) {
  if (PREDICT_FALSE(closedir(dir) == -1)) {
    LOG(WARNING)
        << "Unable to close fd dir: "
        << STATUS(IOError, Substitute("closedir(\"$0\") failed", kProcSelfFd), Errno(errno));
  }
}

} // anonymous namespace

Subprocess::Subprocess(string program, vector<string> argv)
    : program_(std::move(program)),
      argv_(std::move(argv)),
      state_(SubprocessState::kNotStarted),
      child_pid_(0),
      fd_state_(),
      child_fds_() {
  fd_state_[STDIN_FILENO] = SubprocessStreamMode::kPiped;
  fd_state_[STDOUT_FILENO] = SubprocessStreamMode::kShared;
  fd_state_[STDERR_FILENO] = SubprocessStreamMode::kShared;
  child_fds_[STDIN_FILENO] = -1;
  child_fds_[STDOUT_FILENO] = -1;
  child_fds_[STDERR_FILENO] = -1;
}

Subprocess::~Subprocess() {
  if (state() == SubprocessState::kRunning) {
    LOG(WARNING) << "Child process " << child_pid_
                 << "(" << JoinStrings(argv_, " ") << ") "
                 << " was orphaned. Sending SIGKILL...";
    WARN_NOT_OK(Kill(SIGKILL), "Failed to send SIGKILL");
    int junk = 0;
    WARN_NOT_OK(Wait(&junk), "Failed to Wait()");
  }

  for (int i = 0; i < 3; ++i) {
    if (fd_state_[i] == SubprocessStreamMode::kPiped && child_fds_[i] >= 0) {
      close(child_fds_[i]);
    }
  }
}

void Subprocess::SetEnv(const std::string& key, const std::string& value) {
  CHECK_EQ(state_, SubprocessState::kNotStarted);
  env_[key] = value;
}

void Subprocess::AddPIDToCGroup(const string& path, pid_t pid) {
#if defined(__APPLE__)
  LOG(WARNING) << "Writing to cgroup.procs is not supported";
#else
  const char* filename = path.c_str();
  FILE *fptr = fopen(const_cast<char *>(filename), "w");
  if (fptr == NULL) {
    LOG(WARNING) << "Couldn't open " << path;
  } else {
    int ret = fprintf(fptr, "%d\n", pid);
    if (ret < 0) {
      LOG(WARNING) << "Cannot write to " << path  << ". Return = " << ret;
    }
    fclose(fptr);
  }
#endif
}

void Subprocess::SetFdShared(int stdfd, SubprocessStreamMode mode) {
  CHECK_NE(mode, SubprocessStreamMode::kDisabled);
  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(state_, SubprocessState::kNotStarted);
  CHECK_NE(fd_state_[stdfd], SubprocessStreamMode::kDisabled);
  fd_state_[stdfd] = mode;
}

void Subprocess::InheritNonstandardFd(int fd) {
  ns_fds_inherited_.insert(fd);
}

void Subprocess::DisableStderr() {
  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(state_, SubprocessState::kNotStarted);
  fd_state_[STDERR_FILENO] = SubprocessStreamMode::kDisabled;
}

void Subprocess::DisableStdout() {
  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(state_, SubprocessState::kNotStarted);
  fd_state_[STDOUT_FILENO] = SubprocessStreamMode::kDisabled;
}

#if defined(__APPLE__)
static int pipe2(int pipefd[2], int flags) {
  DCHECK_EQ(O_CLOEXEC, flags);

  int new_fds[2] = { 0, 0 };
  if (pipe(new_fds) == -1) {
    return -1;
  }
  if (fcntl(new_fds[0], F_SETFD, O_CLOEXEC) == -1) {
    close(new_fds[0]);
    close(new_fds[1]);
    return -1;
  }
  if (fcntl(new_fds[1], F_SETFD, O_CLOEXEC) == -1) {
    close(new_fds[0]);
    close(new_fds[1]);
    return -1;
  }
  pipefd[0] = new_fds[0];
  pipefd[1] = new_fds[1];
  return 0;
}
#endif

Status Subprocess::Start() {
  std::lock_guard<std::mutex> l(state_lock_);
  SCHECK_EQ(state_, SubprocessState::kNotStarted, IllegalState,
            "Incorrect state when starting the process");
  EnsureSigPipeDisabled();
#if defined(__APPLE__)
  // Closing file descriptors with posix_spawn has some issues on macOS so we still use fork/exec
  // there.
  Status s = StartWithForkExec();
#else
  Status s = StartWithPosixSpawn();
#endif
  if (s.ok()) {
    state_ = SubprocessState::kRunning;
  }
  return s;
}

namespace {

static void RedirectToDevNull(int fd) {
  // We must not close stderr or stdout, because then when a new file descriptor
  // gets opened, it might get that fd number.  (We always allocate the lowest
  // available file descriptor number.)  Instead, we reopen that fd as
  // /dev/null.
  int dev_null = open("/dev/null", O_WRONLY);
  if (dev_null < 0) {
    PLOG(WARNING) << "failed to open /dev/null";
  } else {
    PCHECK(dup2(dev_null, fd));
  }
}

// Close all open file descriptors other than stdin, stderr, stdout.
// Expects a directory stream created by OpenProdFdDir() as a parameter.
// This function is called after fork() and must not call malloc().
// The rule of thumb is to only call async-signal-safe functions in such cases
// if at all possible.
void CloseNonStandardFDs(DIR* fd_dir, const std::unordered_set<int>& excluding) {
  // This is implemented by iterating over the open file descriptors
  // rather than using sysconf(SC_OPEN_MAX) -- the latter is error prone
  // since it may not represent the highest open fd if the fd soft limit
  // has changed since the process started. This should also be faster
  // since iterating over all possible fds is likely to cause 64k+ syscalls
  // in typical configurations.
  //
  // Note also that this doesn't use any of the Env utility functions, to
  // make it as lean and mean as possible -- this runs in the subprocess
  // after a fork, so there's some possibility that various global locks
  // inside malloc() might be held, so allocating memory is a no-no.
  PCHECK(fd_dir != nullptr);
  int dir_fd = dirfd(fd_dir);

  struct DIRENT* ent;
  // readdir64() is not reentrant (it uses a static buffer) and it also
  // locks fd_dir->lock, so it must not be called in a multi-threaded
  // environment and is certainly not async-signal-safe.
  // However, it appears to be safe to call right after fork(), since only one
  // thread exists in the child process at that time. It also does not call
  // malloc() or free(). We could use readdir64_r() instead, but all that
  // buys us is reentrancy, and not async-signal-safety, due to the use of
  // dir->lock, so seems not worth the added complexity in lifecycle & plumbing.
  while ((ent = READDIR(fd_dir)) != nullptr) {
    int32_t fd;
    if (!safe_strto32(ent->d_name, &fd)) continue;
    if (!(fd == STDIN_FILENO  ||
          fd == STDOUT_FILENO ||
          fd == STDERR_FILENO ||
          fd == dir_fd ||
          excluding.count(fd))) {
      close(fd);
    }
  }
}

} // anonymous namespace

Status Subprocess::StartWithForkExec() {
  auto argv_ptrs = VERIFY_RESULT(GetArgvPtrs());
  auto child_pipes = VERIFY_RESULT(CreateChildPipes());

  DIR* fd_dir = VERIFY_RESULT(OpenProcFdDir());
  SCHECK_NOTNULL(fd_dir);
  auto scope_exit_fd_dir = ScopeExit([fd_dir]() {
    CloseProcFdDir(fd_dir);
  });

  int ret = fork();
  if (ret == -1) {
    return STATUS(RuntimeError, "Unable to fork", Errno(errno));
  }
  if (ret == 0) { // We are the child
    // Send the child a SIGTERM when the parent dies. This is done as early
    // as possible in the child's life to prevent any orphaning whatsoever
    // (e.g. from KUDU-402).

    // stdin
    if (fd_state_[STDIN_FILENO] == SubprocessStreamMode::kPiped) {
      PCHECK(dup2(child_pipes.child_stdin[0], STDIN_FILENO) == STDIN_FILENO);
    }
    ConfigureOutputStreamAfterFork(STDOUT_FILENO, child_pipes.child_stdout[1]);
    ConfigureOutputStreamAfterFork(STDERR_FILENO, child_pipes.child_stderr[1]);
    CloseNonStandardFDs(fd_dir, ns_fds_inherited_);

    // setenv does allocate memory, which has led to the child process getting stuck on a lock
    // when attempting to allocate memory, because that lock is held by the parent process.
    // If we don't find a way to switch to posix_spawn on macOS, we will need to ensure setenv
    // does not attempt to allocate memory here.
    for (const auto& env_kv : env_) {
      setenv(env_kv.first.c_str(), env_kv.second.c_str(), /* replace */ true);
    }

    execvp(program_.c_str(), &argv_ptrs[0]);
    PLOG(WARNING) << "Couldn't exec " << program_;
    _exit(errno);
  } else {
    // We are the parent
    child_pid_ = ret;
    FinalizeParentSideOfPipes(child_pipes);
  }

  return Status::OK();
}

void Subprocess::ConfigureOutputStreamAfterFork(int out_stream_fd, int child_write_fd) {
  // out_stream_id is STDOUT_FILENO or STDERR_FILENO.
  // Not doing sanity checks because we are in the forked process and can't allocate memory.l
  switch (fd_state_[out_stream_fd]) {
    case SubprocessStreamMode::kPiped: {
      PCHECK(dup2(child_write_fd, out_stream_fd) == out_stream_fd);
      break;
    }
    case SubprocessStreamMode::kDisabled: {
      RedirectToDevNull(out_stream_fd);
      break;
    }
    default: break;
  }
}

Status Subprocess::StartWithPosixSpawn() {

  auto argv_ptrs = VERIFY_RESULT(GetArgvPtrs());

  auto child_pipes = VERIFY_RESULT(CreateChildPipes());

  posix_spawn_file_actions_t file_actions;

  // posix_spawn_file_actions_... functions do not necessarily set errno, but they return an errno.
  RETURN_ON_ERRNO_RV_FN_CALL(posix_spawn_file_actions_init, &file_actions);

  auto scope_exit_destroy_file_actions = ScopeExit([&file_actions] {
    WARN_NOT_OK(
        STATUS_FROM_ERRNO_RV_FN_CALL(posix_spawn_file_actions_destroy, &file_actions),
        "posix_spawn_file_actions_destroy failed");
  });

  RETURN_NOT_OK(ConfigureFileActionsForPosixSpawn(&file_actions, child_pipes));
  std::vector<int> fds_to_be_closed = VERIFY_RESULT(
      CloseFileDescriptorsForPosixSpawn(&file_actions));

  auto combined_env = GetCombinedEnv();

  posix_spawnattr_t attrs;
  posix_spawnattr_init(&attrs);
  auto scope_exit_spawnattr = ScopeExit([&attrs] {
    WARN_NOT_OK(
        STATUS_FROM_ERRNO_RV_FN_CALL(posix_spawnattr_destroy, &attrs),
        "posix_spawnattr_destroy failed");
  });

  child_pid_ = 0;

  auto posix_spawn_status = STATUS_FROM_ERRNO_RV_FN_CALL(
      posix_spawnp,
      &child_pid_,
      program_.c_str(),
      &file_actions,
      &attrs,
      argv_ptrs.data(),
      combined_env.second.data());
  RETURN_NOT_OK_PREPEND(
      posix_spawn_status,
      Format("Tried to close file descriptors as part of posix_spawn: $0", fds_to_be_closed));

  FinalizeParentSideOfPipes(child_pipes);

  return Status::OK();
}

Status Subprocess::Wait(int* ret) {
  return DoWait(ret, 0);
}

Result<int> Subprocess::Wait() {
  int ret = 0;
  RETURN_NOT_OK(Wait(&ret));
  return ret;
}

Status Subprocess::DoWait(int* ret, int options) {
  if (!ret) {
    return STATUS(InvalidArgument, "ret is NULL");
  }
  *ret = 0;

  pid_t child_pid = 0;
  {
    unique_lock<mutex> l(state_lock_);
    if (state_ == SubprocessState::kExited) {
      *ret = cached_rc_;
      return Status::OK();
    }
    if (state_ != SubprocessState::kRunning) {
      return STATUS(IllegalState, "DoWait called on a process that is not running");
    }
    child_pid = child_pid_;
  }

  CHECK_NE(child_pid, 0);

  int waitpid_ret_val = waitpid(child_pid, ret, options);
  if (waitpid_ret_val == -1) {
    return STATUS(RuntimeError, "Unable to wait on child", Errno(errno));
  }
  if ((options & WNOHANG) && waitpid_ret_val == 0) {
    return STATUS(TimedOut, "");
  }

  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(waitpid_ret_val, child_pid_);
  child_pid_ = 0;
  cached_rc_ = *ret;
  state_ = SubprocessState::kExited;
  return Status::OK();
}

Status Subprocess::Kill(int signal) {
  return KillInternal(signal, /* must_be_running = */ true);
}

Status Subprocess::KillNoCheckIfRunning(int signal) {
  return KillInternal(signal, /* must_be_running = */ false);
}

Status Subprocess::KillInternal(int signal, bool must_be_running) {
  unique_lock<mutex> l(state_lock_);

  if (must_be_running) {
    CHECK_EQ(state_, SubprocessState::kRunning);
  } else if (state_ == SubprocessState::kNotStarted) {
    return STATUS(IllegalState, "Child process has not been started, cannot send signal");
  } else if (state_ == SubprocessState::kExited) {
    return STATUS(IllegalState, "Child process has exited, cannot send signal");
  }
  CHECK_NE(child_pid_, 0);

  if (kill(child_pid_, signal) != 0) {
    return STATUS(RuntimeError, "Unable to kill", Errno(errno));
  }
  return Status::OK();
}

std::pair<std::vector<std::string>, std::vector<char*>> Subprocess::GetCombinedEnv() {
  std::map<std::string, std::string> complete_env;
  for (char **env_var = environ; *env_var != nullptr; env_var++) {
    const char* equal_char = strchr(*env_var, '=');
    if (equal_char) {
      size_t name_length = equal_char - *env_var;
      complete_env[std::string(*env_var, name_length)] = equal_char + 1;
    }
  }
  for (const auto& env_kv : env_) {
    complete_env[env_kv.first] = env_kv.second;
  }

  std::vector<std::string> k_equals_v_strings;
  k_equals_v_strings.reserve(complete_env.size());

  std::vector<char*> envp;
  envp.reserve(complete_env.size() + 1);

  for (const auto& kv : complete_env) {
    k_equals_v_strings.push_back(yb::Format("$0=$1", kv.first, kv.second));
    envp.push_back(const_cast<char*>(k_equals_v_strings.back().c_str()));
  }
  envp.push_back(nullptr);

  return {std::move(k_equals_v_strings), std::move(envp)};
}

Result<std::vector<char*>> Subprocess::GetArgvPtrs() {
  if (argv_.size() < 1) {
    return STATUS(InvalidArgument, "argv must have at least one elem");
  }

  vector<char*> argv_ptrs;
  argv_ptrs.reserve(argv_.size() + 1);
  for (const string& arg : argv_) {
    argv_ptrs.push_back(const_cast<char*>(arg.c_str()));
  }
  argv_ptrs.push_back(nullptr);
  return argv_ptrs;
}

bool Subprocess::IsRunning() const {
  unique_lock<mutex> l(state_lock_);
  if (state_ == SubprocessState::kRunning) {
    CHECK_NE(child_pid_, 0);
    return kill(child_pid_, 0) == 0;
  }
  return false;
}

Status Subprocess::Call(const string& arg_str) {
  VLOG(2) << "Invoking command: " << arg_str;
  vector<string> argv = Split(arg_str, " ");
  return Call(argv);
}

Status Subprocess::Call(const vector<string>& argv) {
  Subprocess proc(argv[0], argv);
  RETURN_NOT_OK(proc.Start());
  int retcode;
  RETURN_NOT_OK(proc.Wait(&retcode));

  if (retcode == 0) {
    return Status::OK();
  }
  return STATUS(RuntimeError, Substitute(
      "Subprocess '$0' terminated with non-zero exit status $1",
      argv[0],
      retcode));
}

Status Subprocess::Call(const vector<string>& argv, string* output, StdFdTypes read_fds) {
  Subprocess p(argv[0], argv);
  return p.Call(output, read_fds);
}

Status Subprocess::Call(string* output, StdFdTypes read_fds) {
  if (read_fds.Test(StdFdType::kIn)) {
    return STATUS(InvalidArgument, "Cannot read from child stdin");
  }
  for (const auto fd_type : read_fds) {
    SetFdShared(to_underlying(fd_type), SubprocessStreamMode::kPiped);
  }

  RETURN_NOT_OK_PREPEND(Start(), "Unable to fork " + argv_[0]);
  const int err = close(ReleaseChildStdinFd());
  if (PREDICT_FALSE(err != 0)) {
    return STATUS(IOError, "Unable to close child process stdin", Errno(errno));
  }

  output->clear();
  char buf[1024];
  boost::container::small_vector<int, 2> fds;
  for (const auto fd_type : read_fds) {
    fds.push_back(CheckAndOffer(to_underlying(fd_type)));
  }

  while (!fds.empty()) {
    auto it = fds.end();
    while (it != fds.begin()) {
      auto fd = *--it;
      ssize_t n = read(fd, buf, arraysize(buf));
      if (n < 0) {
        if (errno == EINTR) continue;
        return STATUS(IOError, "IO error reading from " + argv_[0], Errno(errno));
      }
      if (n == 0) {
        fds.erase(it);
        continue;
      }
      output->append(buf, n);
    }
  }

  int retcode = 0;
  RETURN_NOT_OK_PREPEND(Wait(&retcode), "Unable to wait() for " + argv_[0]);

  if (PREDICT_FALSE(retcode != 0)) {
    return STATUS(RuntimeError, Substitute(
        "Subprocess '$0' terminated with non-zero exit status $1",
        argv_[0],
        retcode));
  }
  return Status::OK();
}

int Subprocess::CheckAndOffer(int stdfd) const {
  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(state_, SubprocessState::kRunning);
  CHECK_EQ(fd_state_[stdfd], SubprocessStreamMode::kPiped);
  return child_fds_[stdfd];
}

int Subprocess::ReleaseChildFd(int stdfd) {
  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(state_, SubprocessState::kRunning);
  CHECK_GE(child_fds_[stdfd], 0);
  CHECK_EQ(fd_state_[stdfd], SubprocessStreamMode::kPiped);
  int ret = child_fds_[stdfd];
  child_fds_[stdfd] = -1;
  return ret;
}

pid_t Subprocess::pid() const {
  unique_lock<mutex> l(state_lock_);
  CHECK_EQ(state_, SubprocessState::kRunning);
  return child_pid_;
}

SubprocessState Subprocess::state() const {
  unique_lock<mutex> l(state_lock_);
  return state_;
}

Result<Subprocess::ChildPipes> Subprocess::CreateChildPipes() {
  ChildPipes pipes;
  // Pipe from caller process to child's stdin
  // [0] = stdin for child, [1] = how parent writes to it
  if (fd_state_[STDIN_FILENO] == SubprocessStreamMode::kPiped) {
    RETURN_NOT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(
        "pipe2 failed for stdin", pipe2(pipes.child_stdin, O_CLOEXEC)));
  }

  // Pipe from child's stdout back to caller process
  // [0] = how parent reads from child's stdout, [1] = how child writes to it
  if (fd_state_[STDOUT_FILENO] == SubprocessStreamMode::kPiped) {
    RETURN_NOT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(
        "pipe2 failed for stdout", pipe2(pipes.child_stdout, O_CLOEXEC)));
  }
  // Pipe from child's stderr back to caller process
  // [0] = how parent reads from child's stderr, [1] = how child writes to it
  if (fd_state_[STDERR_FILENO] == SubprocessStreamMode::kPiped) {
    RETURN_NOT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(
        "pipe2 failed for stderr", pipe2(pipes.child_stderr, O_CLOEXEC)));
  }

  return pipes;
}

Status Subprocess::ConfigureFileActionsForPosixSpawn(
    posix_spawn_file_actions_t* file_actions,
    const Subprocess::ChildPipes& child_pipes) {
  // stdin
  if (fd_state_[STDIN_FILENO] == SubprocessStreamMode::kPiped) {
    RETURN_ON_ERRNO_RV_FN_CALL(
        posix_spawn_file_actions_adddup2, file_actions, child_pipes.child_stdin[0], STDIN_FILENO);
  }

  RETURN_NOT_OK(ConfigureOutputStreamActionForPosixSpawn(
      file_actions, STDOUT_FILENO, child_pipes.child_stdout[1]));
  RETURN_NOT_OK(ConfigureOutputStreamActionForPosixSpawn(
      file_actions, STDERR_FILENO, child_pipes.child_stderr[1]));

  return Status::OK();
}

Status Subprocess::ConfigureOutputStreamActionForPosixSpawn(
    posix_spawn_file_actions_t* file_actions, int out_stream_fd, int pipe_input_fd) {
  SCHECK_FORMAT(
      out_stream_fd == STDOUT_FILENO || out_stream_fd == STDERR_FILENO,
      IllegalState,
      "out_stream_fd must be one of STDOUT_FILENO ($0) or STDERR_FILENO ($1), but is $2",
      STDOUT_FILENO, STDERR_FILENO, out_stream_fd);

  switch (fd_state_[out_stream_fd]) {
    case SubprocessStreamMode::kPiped: {
      RETURN_ON_ERRNO_RV_FN_CALL(
          posix_spawn_file_actions_adddup2, file_actions, pipe_input_fd, out_stream_fd);
      break;
    }
    case SubprocessStreamMode::kDisabled: {
      RETURN_ON_ERRNO_RV_FN_CALL(
          posix_spawn_file_actions_addopen, file_actions, out_stream_fd, "/dev/null", O_WRONLY,
          0 /* mode */);
      break;
    }
    default:
      break;
  }
  return Status::OK();
}

Result<std::vector<int>> Subprocess::CloseFileDescriptorsForPosixSpawn(
    posix_spawn_file_actions_t* file_actions) {
  DIR* fd_dir = VERIFY_RESULT(OpenProcFdDir());
  SCHECK_NOTNULL(fd_dir);
  auto scope_exit_fd_dir = ScopeExit([fd_dir]() {
    CloseProcFdDir(fd_dir);
  });

  int dir_fd = dirfd(fd_dir);
  struct DIRENT* ent;
  std::vector<int> fds_to_close;
  while ((ent = READDIR(fd_dir)) != nullptr) {
    int32_t fd;
    if (!safe_strto32(ent->d_name, &fd)) continue;
    if (fd == STDIN_FILENO  ||
        fd == STDOUT_FILENO ||
        fd == STDERR_FILENO ||
        fd == dir_fd ||
        ns_fds_inherited_.count(fd)) {
      continue;
    }

    int fcntl_result = fcntl(fd, F_GETFD);
    if (fcntl_result != 0) {
      int fcntl_errno = errno;
      if (fcntl_result == FD_CLOEXEC) {
        VLOG(1) << "fcntl(fd=" << fd << ", F_GETFD) is FD_CLOEXEC (" << FD_CLOEXEC << ") "
                << ", skipping posix_spawn_file_actions_addclose";
        continue;
      }
      if (fcntl_result != 0) {
        if (fcntl_result == -1 && fcntl_errno == EBADF) {
          // This is an expected combination, only print a VLOG.
          VLOG(1) << "fcntl(fd=" << fd << ", F_GETFD) is -1 and errno is EBADF ("
                  << EBADF << "): skipping posix_spawn_file_actions_addclose (already closed?)";
          continue;
        }
        LOG(WARNING) << "fcntl(fd=" << fd << ", F_GETFD) is " << fcntl_result << ", errno="
                     << fcntl_errno << " (" << strerror(fcntl_errno) << "): an unexpected "
                     << "combination. Skipping posix_spawn_file_actions_addclose.";
        continue;
      }
    }
    VLOG(1) << "Calling posix_spawn_file_actions_addclose for fd=" << fd;
    RETURN_ON_ERRNO_RV_FN_CALL(posix_spawn_file_actions_addclose, file_actions, fd);
    fds_to_close.push_back(fd);
  }
  return fds_to_close;
}

void Subprocess::FinalizeParentSideOfPipes(const Subprocess::ChildPipes& child_pipes) {
  // Close child's side of the pipes.
  if (fd_state_[STDIN_FILENO] == SubprocessStreamMode::kPiped) {
    close(child_pipes.child_stdin[0]);
  }
  if (fd_state_[STDOUT_FILENO] == SubprocessStreamMode::kPiped) {
    close(child_pipes.child_stdout[1]);
  }
  if (fd_state_[STDERR_FILENO] == SubprocessStreamMode::kPiped) {
    close(child_pipes.child_stderr[1]);
  }

  // Keep parent's side of the pipes.
  child_fds_[STDIN_FILENO] = child_pipes.child_stdin[1];
  child_fds_[STDOUT_FILENO] = child_pipes.child_stdout[0];
  child_fds_[STDERR_FILENO] = child_pipes.child_stderr[0];
}

Status Subprocess::WaitNoBlock(int* ret) {
  return DoWait(ret, WNOHANG);
}

} // namespace yb
