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

#pragma once

#include <mutex>
#include <condition_variable>
#include <ev++.h>

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {
class Thread;

// Monitors for termination signals. Termination can be called from either the Terminate method or
// from a SIGTERM signal.
// Only once instance of this class is allowed.
class TerminationMonitor {
 public:
  ~TerminationMonitor();

  static std::unique_ptr<TerminationMonitor> Create();

  // This is not async-signal-safe.
  void Terminate() EXCLUDES(mutex_);

  // Installs handler for SIGTERM signal and wait for termination to be called.
  void WaitForTermination() EXCLUDES(mutex_);

 private:
  TerminationMonitor() = default;

  void InstallSigtermHandler() EXCLUDES(mutex_);
  void SigtermAsyncHandler();

  std::mutex mutex_;
  bool stopped_ GUARDED_BY(mutex_) = false;
  std::condition_variable stop_signal_;

  ev::dynamic_loop async_sig_term_loop_;
  scoped_refptr<yb::Thread> thread_;

  DISALLOW_COPY_AND_ASSIGN(TerminationMonitor);
};
}  // namespace yb
