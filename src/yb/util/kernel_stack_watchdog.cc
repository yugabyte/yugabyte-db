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

#include "yb/util/kernel_stack_watchdog.h"

#include <string>

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "yb/util/debug-util.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/thread.h"
#include "yb/util/status.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

DEFINE_int32(hung_task_check_interval_ms, 200,
             "Number of milliseconds in between checks for hung threads");
TAG_FLAG(hung_task_check_interval_ms, hidden);

using strings::Substitute;

namespace yb {

DEFINE_STATIC_THREAD_LOCAL(KernelStackWatchdog::TLS,
                           KernelStackWatchdog, tls_);

KernelStackWatchdog::KernelStackWatchdog()
  : finish_(1) {
  CHECK_OK(Thread::Create(
      "kernel-watchdog", "kernel-watcher", std::bind(&KernelStackWatchdog::RunThread, this),
      &thread_));
}

KernelStackWatchdog::~KernelStackWatchdog() {
  finish_.CountDown();
  CHECK_OK(ThreadJoiner(thread_.get()).Join());
}

void KernelStackWatchdog::TEST_SaveLogs() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!log_collector_) {
    log_collector_ = std::make_unique<std::vector<std::string>>();
  }
}

vector<string> KernelStackWatchdog::TEST_LoggedMessages() const {
  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(log_collector_) << "Must call TEST_SaveLogs() first";
  return *log_collector_;
}

void KernelStackWatchdog::Register(TLS* tls) {
  auto tid = Thread::CurrentThreadIdForStack();
  std::lock_guard<std::mutex> lock(mutex_);
  InsertOrDie(&tls_by_tid_, tid, tls);
}

void KernelStackWatchdog::Unregister(TLS* tls) {
  auto tid = Thread::CurrentThreadIdForStack();
  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(tls_by_tid_.erase(tid));
}

Status GetKernelStack(ThreadIdForStack p, string* ret) {
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

    std::vector<ThreadIdForStack> to_copy;
    std::vector<std::pair<ThreadIdForStack, TLS::Data>> to_process;
    std::vector<std::string>* log_collector = nullptr;
    {
      to_copy.clear();
      to_process.resize(1);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& map_entry : tls_by_tid_) {
          if (map_entry.second->data_.TryCopySnapshot(&to_process.back().second)) {
            to_process.back().first = map_entry.first;
            to_process.emplace_back();
          } else {
            to_copy.push_back(map_entry.first);
          }
        }
        log_collector = log_collector_.get();
      }

      while (to_process.size() > 1 || !to_copy.empty()) {
        to_process.pop_back();
        MicrosecondsInt64 now = GetMonoTimeMicros();
        for (const auto& p : to_process) {
          const auto& tls_copy = p.second;
          for (int i = 0; i < tls_copy.depth_; i++) {
            const TLS::Frame* frame = &tls_copy.frames_[i];

            int paused_us = now - frame->start_time_;
            if (paused_us > frame->threshold_us_) {
              string kernel_stack;
              Status s = GetKernelStack(p.first, &kernel_stack);
              if (!s.ok()) {
                // Can't read the kernel stack of the pid -- it's possible that the thread exited
                // while we were iterating, so just ignore it.
                kernel_stack = "(could not read kernel stack)";
              }

              const auto user_stack = DumpThreadStack(p.first);
              LOG_STRING(WARNING, log_collector)
                << "Thread " << p.first << " stuck at " << frame->status_
                << " for " << paused_us / 1000 << "ms" << ":\n"
                << "Kernel stack:\n" << kernel_stack << "\n"
                << "User stack:\n" << user_stack;
            }
          }
        }
        to_process.resize(1);
        if (!to_copy.empty()) {
          base::subtle::PauseCPU();
          std::lock_guard<std::mutex> lock(mutex_);
          auto w = to_copy.begin();
          for (auto tid : to_copy) {
            auto it = tls_by_tid_.find(tid);
            if (it == tls_by_tid_.end()) {
              continue;
            }
            if (it->second->data_.TryCopySnapshot(&to_process.back().second)) {
              to_process.back().first = tid;
              to_process.emplace_back();
            } else {
              *w = tid;
              ++w;
            }
          }
          to_copy.erase(w, to_copy.end());
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

bool KernelStackWatchdog::TLS::Data::TryCopySnapshot(Data* copy) const {
  Atomic32 v_0 = base::subtle::Acquire_Load(&seq_lock_);
  // If the value is odd, then the thread is in the middle of modifying
  // its TLS, and we have to spin.
  if (v_0 & 1) {
    return false;
  }
  ANNOTATE_IGNORE_READS_BEGIN();
  memcpy(copy, this, sizeof(*copy));
  ANNOTATE_IGNORE_READS_END();
  Atomic32 v_1 = base::subtle::Release_Load(&seq_lock_);

  // If the value hasn't changed since we started the copy, then
  // we know that the copy was a consistent snapshot.
  return v_1 == v_0;
}

} // namespace yb
