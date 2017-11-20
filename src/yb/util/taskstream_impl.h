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
//

#ifndef YB_UTIL_TASKSTREAM_IMPL_H
#define YB_UTIL_TASKSTREAM_IMPL_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/lockfree/queue.hpp>

#include <gflags/gflags.h>

#include "yb/util/logging.h"
#include "yb/util/taskstream.h"
#include "yb/util/threadpool.h"

// We have to make the queue length really long.
// Note that the lock-free queue seems to be preallocating memory proportional to the queue size
// (about 64 bytes per entry for 8-byte pointer keys)
DEFINE_int32(taskstream_queue_max_size, 100000,
             "Maximum number of operations waiting in the per-tablet prepare queue.");

using std::vector;

namespace yb {
class ThreadPool;
class ThreadPoolToken;

// ------------------------------------------------------------------------------------------------
// TaskStreamImpl

template <typename T>
class TaskStreamImpl {
 public:
  explicit TaskStreamImpl(std::function<void(T*)> process_item, ThreadPool* thread_pool);
  ~TaskStreamImpl();
  CHECKED_STATUS Start();
  void Stop();

  CHECKED_STATUS Submit(T* task);

 private:

  // We set this to true to tell the Run function to return. No new tasks will be accepted, but
  // existing tasks will still be processed.
  std::atomic<bool> stop_requested_{false};

  // If true, a task is running for this tablet already.
  // If false, no taska are running for this tablet,
  // and we can submit a task to the thread pool token.
  std::atomic<int> running_{0};

  // This is set to true immediately before the thread exits.
  std::atomic<bool> stopped_{false};

  boost::lockfree::queue<T*> queue_;

  // This mutex/condition combination is used in Stop() in case multiple threads are calling that
  // function concurrently. One of them will ask the taskstream thread to stop and wait for it, and
  // then will notify other threads that have called Stop().
  std::mutex stop_mtx_;
  std::condition_variable stop_cond_;

  std::unique_ptr<ThreadPoolToken> taskstream_pool_token_;
  std::function<void(T*)> process_item_;

  void Run();
  void ProcessItem(T* item);

};

template <typename T>
TaskStreamImpl<T>::TaskStreamImpl(std::function<void(T*)> process_item, ThreadPool* thread_pool)
    : queue_(FLAGS_taskstream_queue_max_size),
      taskstream_pool_token_(thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL)),
      process_item_(process_item) {
}

template <typename T>
TaskStreamImpl<T>::~TaskStreamImpl() {
  Stop();
}

template <typename T> Status TaskStreamImpl<T>::Start() {
  return Status::OK();
}

template <typename T>
void TaskStreamImpl<T>::Stop() {
  if (stopped_.load(std::memory_order_acquire)) {
    return;
  }
  stop_requested_ = true;
  {
    std::unique_lock<std::mutex> stop_lock(stop_mtx_);
    stop_cond_.wait(stop_lock, [this] {
      return (!running_.load(std::memory_order_acquire) && queue_.empty());
    });
  }
  stopped_.store(true, std::memory_order_release);
}

template <typename T> Status TaskStreamImpl<T>::Submit(T *task) {
  if (stop_requested_.load(std::memory_order_acquire)) {
    return STATUS(IllegalState, "Tablet is shutting down");
  }
  if (!queue_.bounded_push(task)) {
    return STATUS_FORMAT(ServiceUnavailable,
                         "TaskStream queue is full (max capacity $0)",
                         FLAGS_taskstream_queue_max_size);
  }

  int expected = 0;
  if (!running_.compare_exchange_strong(expected, 1, std::memory_order_release)) {
    // running_ was not 0, so we are not creating a task to process operations.
    return Status::OK();
  }
  // We flipped running_ from 0 to 1. The previously running thread could go back to doing another
  // iteration, but in that case since we are submitting to a token of a thread pool, only one
  // such thread will be running, the other will be in the queue.
  return taskstream_pool_token_->SubmitFunc(std::bind(&TaskStreamImpl::Run, this));
}

template <typename T> void TaskStreamImpl<T>::Run() {
  VLOG(1) << "Starting taskstream task:" << this;
  for (;;) {
    T *item = nullptr;
    while (queue_.pop(item)) {
      ProcessItem(item);
    }
    if (queue_.empty()) {
      // Not processing and queue empty, return from task.
      ProcessItem(nullptr);
      std::unique_lock<std::mutex> stop_lock(stop_mtx_);
      running_--;
      if (!queue_.empty()) {
        // Got more operations, stay in the loop.
        running_++;
        continue;
      }
      if (stop_requested_.load(std::memory_order_acquire)) {
        VLOG(1) << "TaskStream task's Run() function is returning because stop is requested.";
        stop_cond_.notify_all();
        return;
      }
      VLOG(1) << "Returning from TaskStream task after inactivity:" << this;
      return;
    }
  }
}

template <typename T> void TaskStreamImpl<T>::ProcessItem(T* item) {
  process_item_(item);
}

// ------------------------------------------------------------------------------------------------
// TaskStream

template <typename T>
TaskStream<T>::TaskStream(std::function<void(T *)> process_item, ThreadPool* thread_pool)
    : impl_(std::make_unique<TaskStreamImpl<T>>(process_item, thread_pool)) {
}

template <typename T>
TaskStream<T>::~TaskStream() {
}

template <typename T>
Status TaskStream<T>::Start() {
  VLOG(1) << "Starting the TaskStream";
  return impl_->Start();
}

template <typename T> void TaskStream<T>::Stop() {
  VLOG(1) << "Stopping the TaskStream";
  impl_->Stop();
  VLOG(1) << "The TaskStream has stopped";
}

template <typename T> Status TaskStream<T>::Submit(T* item) {
  return impl_->Submit(item);
}


}  // namespace yb
#endif  // YB_UTIL_TASKSTREAM_IMPL_H
