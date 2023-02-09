// Copyright (c) Yugabyte, Inc.
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

#include "yb/common/termination_monitor.h"
#include <signal.h>
#include <sys/time.h>

#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/thread.h"

DECLARE_bool(TEST_running_test);

namespace yb {

namespace {
ev::async& GetSigTermEvAsync() {
  static ev::async async_sig_term_;
  return async_sig_term_;
}

void SigtermHandler(int signum) {
  // This is thread and signal safe
  GetSigTermEvAsync().send();
}

}  // namespace

TerminationMonitor::~TerminationMonitor() {
  if (thread_) {
    CHECK_OK(InstallSignalHandler(SIGTERM, SIG_DFL));
    GetSigTermEvAsync().send();
    CHECK_OK(ThreadJoiner(thread_.get()).Join());
  }
}

std::unique_ptr<TerminationMonitor> TerminationMonitor::Create() {
  static std::atomic_bool instantiated = false;
  CHECK(!instantiated.exchange(true)) << "Only one instance of TerminationMonitor is allowed";

  auto monitor = std::unique_ptr<TerminationMonitor>(new TerminationMonitor());
  // Up to this point SIGTERM would have caused an immediate process termination. We are now ready
  // to gracefully handle SIGTERM so install our handler.
  monitor->InstallSigtermHandler();
  return monitor;
}

void TerminationMonitor::Terminate() {
  {
    std::lock_guard lock(mutex_);
    stopped_ = true;
  }
  stop_signal_.notify_all();
}

void TerminationMonitor::WaitForTermination() {
  std::unique_lock lock(mutex_);
  stop_signal_.wait(lock, [this]() REQUIRES(mutex_) { return stopped_; });
}

void TerminationMonitor::InstallSigtermHandler() {
  // TODO(Hari) #15061 Limiting this to tests to catch TSAN and ASAN issues before we enable it in
  // production.
  if (!FLAGS_TEST_running_test) {
    return;
  }

  std::lock_guard lock(mutex_);
  if (thread_) {
    return;
  }

  auto& async_sig_term = GetSigTermEvAsync();
  async_sig_term.set(async_sig_term_loop_);
  async_sig_term.set<TerminationMonitor, &TerminationMonitor::SigtermAsyncHandler>(this);
  async_sig_term.start();
  CHECK_OK(yb::Thread::Create(
      "termination_monitor", "sigterm_loop", [this]() { async_sig_term_loop_.run(); }, &thread_));

  CHECK_OK(InstallSignalHandler(SIGTERM, SigtermHandler));
}

void TerminationMonitor::SigtermAsyncHandler() {
  GetSigTermEvAsync().stop();
  Terminate();
  async_sig_term_loop_.break_loop(ev::how_t::ALL);
}

}  // namespace yb
