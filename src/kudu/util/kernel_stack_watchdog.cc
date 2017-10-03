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

#include "kudu/util/kernel_stack_watchdog.h"

#include <boost/bind.hpp>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <string>

#include "kudu/util/debug-util.h"
#include "kudu/util/env.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/thread.h"
#include "kudu/util/status.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/substitute.h"

DEFINE_int32(hung_task_check_interval_ms, 200,
             "Number of milliseconds in between checks for hung threads");
TAG_FLAG(hung_task_check_interval_ms, hidden);

using strings::Substitute;

namespace kudu {

DEFINE_STATIC_THREAD_LOCAL(KernelStackWatchdog::TLS,
                           KernelStackWatchdog, tls_);

KernelStackWatchdog::KernelStackWatchdog()
  : log_collector_(nullptr),
    finish_(1) {
  CHECK_OK(Thread::Create("kernel-watchdog", "kernel-watcher",
                          boost::bind(&KernelStackWatchdog::RunThread, this),
                          &thread_));
}

KernelStackWatchdog::~KernelStackWatchdog() {
  finish_.CountDown();
  CHECK_OK(ThreadJoiner(thread_.get()).Join());
}

void KernelStackWatchdog::SaveLogsForTests(bool save_logs) {
  MutexLock l(lock_);
  if (save_logs) {
    log_collector_.reset(new vector<string>());
  } else {
    log_collector_.reset();
  }
}

vector<string> KernelStackWatchdog::LoggedMessagesForTests() const {
  MutexLock l(lock_);
  CHECK(log_collector_) << "Must call SaveLogsForTests(true) first";
  return *log_collector_;
}

void KernelStackWatchdog::Register(TLS* tls) {
  int64_t tid = Thread::CurrentThreadId();
  MutexLock l(lock_);
  InsertOrDie(&tls_by_tid_, tid, tls);
}

void KernelStackWatchdog::Unregister(TLS* tls) {
  int64_t tid = Thread::CurrentThreadId();
  MutexLock l(lock_);
  CHECK(tls_by_tid_.erase(tid));
}

Status GetKernelStack(pid_t p, string* ret) {
  faststring buf;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), Substitute("/proc/$0/stack", p), &buf));
  *ret = buf.ToString();
  return Status::OK();
}

void KernelStackWatchdog::RunThread() {
  while (true) {
    MonoDelta delta = MonoDelta::FromMilliseconds(FLAGS_hung_task_check_interval_ms);
    if (finish_.WaitFor(delta)) {
      // Watchdog exiting.
      break;
    }

    {
      MutexLock l(lock_);
      MicrosecondsInt64 now = GetMonoTimeMicros();

      for (const TLSMap::value_type& map_entry : tls_by_tid_) {
        pid_t p = map_entry.first;
        const TLS::Data* tls = &map_entry.second->data_;

        TLS::Data tls_copy;
        tls->SnapshotCopy(&tls_copy);

        for (int i = 0; i < tls_copy.depth_; i++) {
          TLS::Frame* frame = &tls_copy.frames_[i];

          int paused_ms = (now - frame->start_time_) / 1000;
          if (paused_ms > frame->threshold_ms_) {
            string kernel_stack;
            Status s = GetKernelStack(p, &kernel_stack);
            if (!s.ok()) {
              // Can't read the kernel stack of the pid -- it's possible that the thread exited
              // while we were iterating, so just ignore it.
              kernel_stack = "(could not read kernel stack)";
            }

            string user_stack = DumpThreadStack(p);
            LOG_STRING(WARNING, log_collector_.get())
              << "Thread " << p << " stuck at " << frame->status_
              << " for " << paused_ms << "ms" << ":\n"
              << "Kernel stack:\n" << kernel_stack << "\n"
              << "User stack:\n" << user_stack;
          }
        }
      }
    }
  }
}

KernelStackWatchdog::TLS* KernelStackWatchdog::GetTLS() {
  INIT_STATIC_THREAD_LOCAL(KernelStackWatchdog::TLS, tls_);
  return tls_;
}

KernelStackWatchdog::TLS::TLS() {
  memset(&data_, 0, sizeof(data_));
  KernelStackWatchdog::GetInstance()->Register(this);
}

KernelStackWatchdog::TLS::~TLS() {
  KernelStackWatchdog::GetInstance()->Unregister(this);
}

// Optimistic concurrency control approach to snapshot the value of another
// thread's TLS, even though that thread might be changing it.
//
// Called by the watchdog thread to see if a target thread is currently in the
// middle of a watched section.
void KernelStackWatchdog::TLS::Data::SnapshotCopy(Data* copy) const {
  while (true) {
    Atomic32 v_0 = base::subtle::Acquire_Load(&seq_lock_);
    if (v_0 & 1) {
      // If the value is odd, then the thread is in the middle of modifying
      // its TLS, and we have to spin.
      base::subtle::PauseCPU();
      continue;
    }
    ANNOTATE_IGNORE_READS_BEGIN();
    memcpy(copy, this, sizeof(*copy));
    ANNOTATE_IGNORE_READS_END();
    Atomic32 v_1 = base::subtle::Release_Load(&seq_lock_);

    // If the value hasn't changed since we started the copy, then
    // we know that the copy was a consistent snapshot.
    if (v_1 == v_0) break;
  }
}

} // namespace kudu
