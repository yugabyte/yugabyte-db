//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/util/thread_posix.h"

#include <pthread.h>
#include <unistd.h>

#include <atomic>
#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "yb/util/format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

namespace rocksdb {

void ThreadPool::PthreadCall(const char* label, int result) {
  if (result != 0) {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    abort();
  }
}

ThreadPool::ThreadPool() {
  PthreadCall("mutex_init", pthread_mutex_init(&mu_, nullptr));
  PthreadCall("cvar_init", pthread_cond_init(&bgsignal_, nullptr));
}

ThreadPool::~ThreadPool() {
  DCHECK(bgthreads_.empty());
}

void ThreadPool::JoinAllThreads() {
  PthreadCall("lock", pthread_mutex_lock(&mu_));
  DCHECK(!exit_all_threads_);
  exit_all_threads_ = true;
  PthreadCall("signalall", pthread_cond_broadcast(&bgsignal_));
  PthreadCall("unlock", pthread_mutex_unlock(&mu_));
  for (const auto& thread : bgthreads_) {
    thread->Join();
  }
  bgthreads_.clear();
}

void ThreadPool::LowerIOPriority() {
#ifdef __linux__
  PthreadCall("lock", pthread_mutex_lock(&mu_));
  low_io_priority_ = true;
  PthreadCall("unlock", pthread_mutex_unlock(&mu_));
#endif
}

void ThreadPool::BGThread(size_t thread_id) {
  bool low_io_priority = false;
  while (true) {
    // Wait until there is an item that is ready to run
    PthreadCall("lock", pthread_mutex_lock(&mu_));
    // Stop waiting if the thread needs to do work or needs to terminate.
    while (!exit_all_threads_ && !IsLastExcessiveThread(thread_id) &&
           (queue_.empty() || IsExcessiveThread(thread_id))) {
      PthreadCall("wait", pthread_cond_wait(&bgsignal_, &mu_));
    }
    if (exit_all_threads_) {  // mechanism to let BG threads exit safely
      PthreadCall("unlock", pthread_mutex_unlock(&mu_));
      break;
    }
    if (IsLastExcessiveThread(thread_id)) {
      // Current thread is the last generated one and is excessive.
      // We always terminate excessive thread in the reverse order of
      // generation time.
      bgthreads_.pop_back();
      if (HasExcessiveThread()) {
        // There is still at least more excessive thread to terminate.
        WakeUpAllThreads();
      }
      PthreadCall("unlock", pthread_mutex_unlock(&mu_));
      break;
    }
    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();
    queue_len_.store(static_cast<unsigned int>(queue_.size()),
                     std::memory_order_relaxed);

    bool decrease_io_priority = (low_io_priority != low_io_priority_);
    PthreadCall("unlock", pthread_mutex_unlock(&mu_));

#ifdef __linux__
    if (decrease_io_priority) {
#define IOPRIO_CLASS_SHIFT (13)
#define IOPRIO_PRIO_VALUE(class, data) (((class) << IOPRIO_CLASS_SHIFT) | data)
      // Put schedule into IOPRIO_CLASS_IDLE class (lowest)
      // These system calls only have an effect when used in conjunction
      // with an I/O scheduler that supports I/O priorities. As at
      // kernel 2.6.17 the only such scheduler is the Completely
      // Fair Queuing (CFQ) I/O scheduler.
      // To change scheduler:
      //  echo cfq > /sys/block/<device_name>/queue/schedule
      // Tunables to consider:
      //  /sys/block/<device_name>/queue/slice_idle
      //  /sys/block/<device_name>/queue/slice_sync
      syscall(SYS_ioprio_set, 1,  // IOPRIO_WHO_PROCESS
              0,                  // current thread
              IOPRIO_PRIO_VALUE(3, 0));
      low_io_priority = true;
    }
#else
    (void)decrease_io_priority;  // avoid 'unused variable' error
#endif
    (*function)(arg);
  }
}

void ThreadPool::WakeUpAllThreads() {
  PthreadCall("signalall", pthread_cond_broadcast(&bgsignal_));
}

void ThreadPool::SetBackgroundThreadsInternal(int num, bool allow_reduce) {
  PthreadCall("lock", pthread_mutex_lock(&mu_));
  if (exit_all_threads_) {
    PthreadCall("unlock", pthread_mutex_unlock(&mu_));
    return;
  }
  if (num > total_threads_limit_ ||
      (num < total_threads_limit_ && allow_reduce)) {
    total_threads_limit_ = std::max(1, num);
    WakeUpAllThreads();
    StartBGThreads();
  }
  PthreadCall("unlock", pthread_mutex_unlock(&mu_));
}

void ThreadPool::IncBackgroundThreadsIfNeeded(int num) {
  SetBackgroundThreadsInternal(num, false);
}

void ThreadPool::SetBackgroundThreads(int num) {
  SetBackgroundThreadsInternal(num, true);
}

void ThreadPool::StartBGThreads() {
  // Start background thread if necessary
  std::string category_name = yb::Format(
      "rocksdb:$0", GetThreadPriority() == Env::Priority::HIGH ? "high" : "low");
  while (static_cast<int>(bgthreads_.size()) < total_threads_limit_) {
    size_t tid = bgthreads_.size();
    std::string thread_name = yb::Format("$0:$1", category_name, tid);
    yb::ThreadPtr thread;
    CHECK_OK(yb::Thread::Create(
        category_name, std::move(thread_name),
        [this, tid]() { this->BGThread(tid); }, &thread));

    bgthreads_.push_back(thread);
  }
}

void ThreadPool::Schedule(void (*function)(void* arg1), void* arg, void* tag,
                          void (*unschedFunction)(void* arg)) {
  PthreadCall("lock", pthread_mutex_lock(&mu_));

  if (exit_all_threads_) {
    PthreadCall("unlock", pthread_mutex_unlock(&mu_));
    return;
  }

  StartBGThreads();

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;
  queue_.back().tag = tag;
  queue_.back().unschedFunction = unschedFunction;
  queue_len_.store(static_cast<unsigned int>(queue_.size()),
                   std::memory_order_relaxed);

  if (!HasExcessiveThread()) {
    // Wake up at least one waiting thread.
    PthreadCall("signal", pthread_cond_signal(&bgsignal_));
  } else {
    // Need to wake up all threads to make sure the one woken
    // up is not the one to terminate.
    WakeUpAllThreads();
  }

  PthreadCall("unlock", pthread_mutex_unlock(&mu_));
}

int ThreadPool::UnSchedule(void* arg) {
  int count = 0;
  PthreadCall("lock", pthread_mutex_lock(&mu_));

  // Remove from priority queue
  BGQueue::iterator it = queue_.begin();
  while (it != queue_.end()) {
    if (arg == (*it).tag) {
      void (*unschedFunction)(void*) = (*it).unschedFunction;
      void* arg1 = (*it).arg;
      if (unschedFunction != nullptr) {
        (*unschedFunction)(arg1);
      }
      it = queue_.erase(it);
      count++;
    } else {
      it++;
    }
  }
  queue_len_.store(static_cast<unsigned int>(queue_.size()),
                   std::memory_order_relaxed);
  PthreadCall("unlock", pthread_mutex_unlock(&mu_));
  return count;
}

}  // namespace rocksdb
