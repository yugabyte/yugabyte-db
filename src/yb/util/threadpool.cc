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

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>

#include "yb/util/flags.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/errno.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/strand.h"
#include "yb/util/thread.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"

DEFINE_RUNTIME_bool(threadpool_use_current_trace_for_tasks, false,
    "If true, the thread pool will use the current trace for tasks submitted to it.");

namespace yb {

using strings::Substitute;
using std::unique_ptr;
using std::deque;


////////////////////////////////////////////////////////
// ThreadPoolBuilder
///////////////////////////////////////////////////////

ThreadPoolBuilder::ThreadPoolBuilder(std::string name)
    : options_(ThreadPoolOptions {
        .name = std::move(name),
        .max_workers = make_unsigned(base::NumCPUs()),
        .idle_timeout = MonoDelta::FromMilliseconds(500),
      }) {}

Status ThreadPool::SubmitClosure(const Closure& task) {
  return DoSubmit([task] { task.Run(); });
}

Status ThreadPool::Submit(const std::shared_ptr<Runnable>& r) {
  return DoSubmit([r]() { r->Run(); });
}

Status ThreadPool::SubmitFunc(const std::function<void()>& func) {
  return DoSubmit(func);
}

template <class F>
Status ThreadPool::DoSubmit(const F& f) {
  bool enqueued;
  if (FLAGS_threadpool_use_current_trace_for_tasks) {
    TracePtr trace(Trace::CurrentTrace());
    enqueued = impl_.EnqueueFunctor([f, trace]() {
      ADOPT_TRACE(trace.get());
      f();
    });
  } else {
    enqueued = impl_.EnqueueFunctor(f);
  }

  return PREDICT_TRUE(enqueued)
      ? Status::OK() : STATUS(ServiceUnavailable, "The pool has been shut down.");
}

template <class Impl>
class ThreadPoolTokenImpl : public ThreadPoolToken {
 public:
  explicit ThreadPoolTokenImpl(YBThreadPool* thread_pool) : impl_(thread_pool) {}

  ~ThreadPoolTokenImpl() {
    impl_.Shutdown();
  }

  Status SubmitFunc(std::function<void()> f) override {
    if (impl_.EnqueueFunctor(std::move(f))) {
      return Status::OK();
    }
    return STATUS(ServiceUnavailable, "Thread pool token was shut down.", "", Errno(ESHUTDOWN));
  }

  void Shutdown() override {
    impl_.Shutdown();
  }

 private:
  Impl impl_;
};

std::unique_ptr<ThreadPoolToken> ThreadPool::NewToken(ExecutionMode mode) {
  switch (mode) {
    case ExecutionMode::SERIAL:
      return std::make_unique<ThreadPoolTokenImpl<Strand>>(&impl_);
    case ExecutionMode::CONCURRENT:
      return std::make_unique<ThreadPoolTokenImpl<ThreadSubPool>>(&impl_);
  }
  FATAL_INVALID_ENUM_VALUE(ExecutionMode, mode);
}

Status ThreadPoolToken::SubmitClosure(const Closure& task) {
  return SubmitFunc(std::bind(&Closure::Run, task));
}

Status ThreadPoolToken::Submit(const std::shared_ptr<Runnable>& runnable) {
  return SubmitFunc([runnable] { runnable->Run(); });
}

Status TaskRunner::Init(int concurrency) {
  ThreadPoolBuilder builder("Task Runner");
  if (concurrency > 0) {
    builder.set_max_threads(concurrency);
  }
  return builder.Build(&thread_pool_);
}

Status TaskRunner::Wait(StopWaitIfFailed stop_wait_if_failed) {
  UniqueLock lock(mutex_);
  WaitOnConditionVariable(&cond_, &lock, [this, &stop_wait_if_failed] {
    return (running_tasks_ == 0 || (stop_wait_if_failed && failed_.load()));
  });
  return first_failure_;
}

void TaskRunner::CompleteTask(const Status& status) {
  bool is_first_failure = false;
  if (!status.ok()) {
    bool expected = false;
    if (failed_.compare_exchange_strong(expected, true)) {
      is_first_failure = true;
      std::lock_guard lock(mutex_);
      first_failure_ = status;
    } else {
      LOG(WARNING) << status.message() << std::endl;
    }
  }
  if (--running_tasks_ == 0 || is_first_failure) {
    std::lock_guard lock(mutex_);
    YB_PROFILE(cond_.notify_one());
  }
}

} // namespace yb
