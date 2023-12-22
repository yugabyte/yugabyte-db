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
//--------------------------------------------------------------------------------------------------
// Utility functions and macros to manage process signals.

#pragma once

#include <signal.h>

#include <vector>
#include <type_traits>

#include <glog/logging.h>

#include "yb/util/result.h"

namespace yb {

//
// Generic functions
//

// On current thread, block the given signals and return an old signal mask.
// Signals will be accumulated by OS and delivered when unblocked.
// This is needed before starting the thread if we only want those signals to be handled
// at the main thread to avoid concurrency issues.
Result<sigset_t> ThreadSignalMaskBlock(const std::vector<int>& signals_to_block);

// On current thread, unblock the given signals and return an old signal mask.
// Signals that were accumulated by OS will be delivered before this function returns.
Result<sigset_t> ThreadSignalMaskUnblock(const std::vector<int>& signals_to_unblock);

// Restore previous signal mask on the current thread.
// Unblocking signals lets the blocked signals be delivered if they had been raised in the meantime.
Status ThreadSignalMaskRestore(sigset_t old_mask);

//
// Specific functions
//

extern const std::vector<int> kYsqlHandledSignals;

// Calls ThreadSignalMaskBlock to block signals with handlers installed by postgres layer.
Result<sigset_t> ThreadYsqlSignalMaskBlock();

// Applies ThreadYsqlSignalMaskBlock, executes a given code (which should return a Status or Result)
// and apples ThreadSignalMaskRestore, returning execution result.
// Will attempt to revert a mask even if execution fails.
// In case both execution and mask restoration fail, execution error status will be returned.
template<typename Functor>
typename std::invoke_result<Functor>::type WithMaskedYsqlSignals(Functor callback) {
  sigset_t old_mask = VERIFY_RESULT(ThreadYsqlSignalMaskBlock());
  auto&& callback_status = callback();
  Status restore_status = yb::ThreadSignalMaskRestore(old_mask);
  if (!restore_status.ok() && !callback_status.ok()) {
    LOG(WARNING) << "Failed to restore thread signal mask: " << restore_status;
    return std::move(callback_status);
  }
  RETURN_NOT_OK(restore_status);
  return std::move(callback_status);
}

} // namespace yb
