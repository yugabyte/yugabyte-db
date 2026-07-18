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

#include "yb/util/signal_util.h"

#include "yb/util/errno.h"

namespace yb {

namespace {

Result<sigset_t> ThreadSignalMask(int how, const std::vector<int>& signals_to_change) {
  // Note that in case of an error sigaddset sets errno and returns -1
  // while pthread_sigmask returns errno directly.

  sigset_t mask;
  sigemptyset(&mask);
  for (int sig : signals_to_change) {
    int err = sigaddset(&mask, sig);
    if (err != 0) {
      return STATUS(InternalError, "sigaddset failed", Errno(errno));
    }
  }

  sigset_t old_mask;
  int err = pthread_sigmask(how, &mask, &old_mask);
  return (
      err == 0 ? Result<sigset_t>(old_mask)
               : STATUS(InternalError, "SIG_BLOCK failed", Errno(err)));
}

}  // namespace

Result<sigset_t> ThreadSignalMaskBlock(const std::vector<int>& signals_to_block) {
  return ThreadSignalMask(SIG_BLOCK, signals_to_block);
}

Result<sigset_t> ThreadSignalMaskUnblock(const std::vector<int>& signals_to_unblock) {
  return ThreadSignalMask(SIG_UNBLOCK, signals_to_unblock);
}

Status ThreadSignalMaskRestore(sigset_t old_mask) {
  int err = pthread_sigmask(SIG_SETMASK, &old_mask, NULL /* oldset */);
  return (err == 0
      ? Status::OK()
      : STATUS(InternalError, "SIG_SETMASK failed", Errno(err)));
}

const std::vector<int> kYsqlHandledSignals{
    // Following handlers are installed in StartBackgroundWorker:
    SIGINT, // StatementCancelHandler
    SIGUSR1, // procsignal_sigusr1_handler
    SIGFPE, // FloatExceptionHandler
    SIGTERM, // bgworker_die
    SIGQUIT, // bgworker_quickdie
    SIGALRM // handle_sig_alarm
};

Result<sigset_t> ThreadYsqlSignalMaskBlock() {
  return ThreadSignalMaskBlock(kYsqlHandledSignals);
}

Status InstallSignalHandler(int signum, void (*handler)(int)) {
  struct sigaction sig_action{};
  sig_action.sa_handler = handler;
  return STATUS_FROM_ERRNO_IF_NONZERO_RV(
      Format("InstallSignalHandler failed for signal $0 ($1)", signum, strsignal(signum)),
      sigaction(signum, &sig_action, nullptr));
}

} // namespace yb
