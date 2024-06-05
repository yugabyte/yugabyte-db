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

#pragma once

#include <memory>
#include <atomic>

#include "yb/util/status.h"
#include "yb/util/source_location.h"
#include "yb/gutil/macros.h"

namespace yb {
namespace rpc {

class Reactor;

// ------------------------------------------------------------------------------------------------
// A task which can be enqueued to run on the reactor thread.

class ReactorTask : public std::enable_shared_from_this<ReactorTask> {
 public:
  // source_location - location of code that initiated this task.
  explicit ReactorTask(const SourceLocation& source_location);

  // Run the task. 'reactor' is guaranteed to be the current thread.
  virtual void Run(Reactor *reactor) = 0;

  // Abort the task, in the case that the reactor shut down before the task could be processed. This
  // may or may not run on the reactor thread itself.  If this is run not on the reactor thread,
  // then reactor thread should have already been shut down. It is guaranteed that Abort() will be
  // called at most once.
  //
  // The Reactor guarantees that the Reactor lock is free when this method is called.
  void Abort(const Status& abort_status);

  virtual std::string ToString() const;

  virtual ~ReactorTask() = default;

 protected:
  const SourceLocation source_location_;

 private:
  // To be overridden by subclasses.
  virtual void DoAbort(const Status &abort_status) {}

  // Used to prevent Abort() from being called twice from multiple threads.
  std::atomic<bool> abort_called_{false};

  DISALLOW_COPY_AND_ASSIGN(ReactorTask);
};

// ------------------------------------------------------------------------------------------------
// A task that runs the given user functor on success. Abort is ignored.

template <class F>
class FunctorReactorTask : public ReactorTask {
 public:
  explicit FunctorReactorTask(const F& f, const SourceLocation& source_location)
      : ReactorTask(source_location), f_(f) {}

  void Run(Reactor* reactor) override  {
    f_(reactor);
  }

 private:
  F f_;
};

// ------------------------------------------------------------------------------------------------

using ReactorTaskPtr = std::shared_ptr<ReactorTask>;
using ReactorTasks = std::vector<ReactorTaskPtr>;

// ------------------------------------------------------------------------------------------------
// A task that runs the given user functor on success or abort.

template <class F>
class FunctorReactorTaskWithAbort : public ReactorTask {
 public:
  FunctorReactorTaskWithAbort(const F& f, const SourceLocation& source_location)
      : ReactorTask(source_location), f_(f) {}

  void Run(Reactor* reactor) override  {
    f_(reactor, Status::OK());
  }

 private:
  void DoAbort(const Status &abort_status) override {
    f_(nullptr, abort_status);
  }

  F f_;
};

template <class F>
ReactorTaskPtr MakeFunctorReactorTaskWithAbort(const F& f, const SourceLocation& source_location) {
  return std::make_shared<FunctorReactorTaskWithAbort<F>>(f, source_location);
}

// ------------------------------------------------------------------------------------------------
// A task that runs the user functor if the given weak pointer is still valid by the time the
// reactor runs the task.

template <class F, class Object>
class FunctorReactorTaskWithWeakPtr : public ReactorTask {
 public:
  FunctorReactorTaskWithWeakPtr(const F& f, const std::weak_ptr<Object>& ptr,
                                const SourceLocation& source_location)
      : ReactorTask(source_location), f_(f), ptr_(ptr) {}

  void Run(Reactor* reactor) override  {
    auto shared_ptr = ptr_.lock();
    if (shared_ptr) {
      f_(reactor);
    }
  }

 private:
  F f_;
  std::weak_ptr<Object> ptr_;
};

// ------------------------------------------------------------------------------------------------
// Factory functions for for creating reactor tasks.
// ------------------------------------------------------------------------------------------------

template <class F>
ReactorTaskPtr MakeFunctorReactorTask(const F& f, const SourceLocation& source_location) {
  return std::make_shared<FunctorReactorTask<F>>(f, source_location);
}

template <class F, class Object>
ReactorTaskPtr MakeFunctorReactorTask(
    const F& f, const std::weak_ptr<Object>& ptr,
    const SourceLocation& source_location) {
  return std::make_shared<FunctorReactorTaskWithWeakPtr<F, Object>>(f, ptr, source_location);
}

template <class F, class Object>
ReactorTaskPtr MakeFunctorReactorTask(
    const F& f, const std::shared_ptr<Object>& ptr, const SourceLocation& source_location) {
  return std::make_shared<FunctorReactorTaskWithWeakPtr<F, Object>>(f, ptr, source_location);
}

}  // namespace rpc
}  // namespace yb
