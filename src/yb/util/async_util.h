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
// Utility functions which are handy when doing async/callback-based programming.
#pragma once

#include <pthread.h>

#include <future>

#include <boost/function.hpp>

#include "yb/gutil/macros.h"

#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"

#include "yb/util/concepts.h"

namespace yb {

typedef boost::function<void(const Status&)> StatusFunctor;

// Simple class which can be used to make async methods synchronous.
// For example:
//   Synchronizer s;
//   SomeAsyncMethod(s.callback());
//   CHECK_OK(s.Wait());
class Synchronizer {
 public:
  Synchronizer(const Synchronizer&) = delete;
  void operator=(const Synchronizer&) = delete;

  Synchronizer() {}
  ~Synchronizer();

  void StatusCB(const Status& status);

  // Use this for synchronizers declared on the stack. The callback does not take a reference to
  // its synchronizer, so the returned callback _must_ go out of scope before its synchronizer.
  StatusCallback AsStatusCallback();

  // Same semantics as AsStatusCallback.
  StdStatusCallback AsStdStatusCallback();

  // This version of AsStatusCallback is for cases when the callback can outlive the synchronizer.
  // The callback holds a weak pointer to the synchronizer.
  static StatusCallback AsStatusCallback(const std::shared_ptr<Synchronizer>& synchronizer);
  static StdStatusCallback AsStdStatusCallback(const std::shared_ptr<Synchronizer>& synchronizer);

  StatusFunctor AsStatusFunctor() {
    return std::bind(&Synchronizer::StatusCB, this, std::placeholders::_1);
  }

  Status Wait() {
    return WaitUntil(std::chrono::steady_clock::time_point::max());
  }

  Status WaitFor(const MonoDelta& delta) {
    return WaitUntil(std::chrono::steady_clock::now() + delta.ToSteadyDuration());
  }

  Status WaitUntil(const std::chrono::steady_clock::time_point& time);

  void Reset();

 private:

  // Invoked in the destructor and in Reset() to make sure Wait() was invoked if it had to be.
  void EnsureWaitDone();

  std::mutex mutex_;
  std::condition_variable cond_;
  bool assigned_ = false;

  // If we've created a callback and given it out to an asynchronous operation, we must call Wait()
  // on the synchronizer before destroying it. Not doing any locking around this variable because
  // Wait() is supposed to be called on the same thread as AsStatusCallback(), or with adequate
  // synchronization after that. Most frequently Wait() is called right after creating the
  // synchronizer.
  bool must_wait_ = false;

  Status status_;
};

// Functor is any functor that accepts callback as only argument.
template <class Result, class Functor>
std::future<Result> MakeFuture(const Functor& functor) {
  auto promise = std::make_shared<std::promise<Result>>();
  auto future = promise->get_future();
  functor([promise](Result result) {
    promise->set_value(std::move(result));
  });
  return future;
}

template <class T>
concept FutureType = AnyOfTemplateTypes<T, std::future, std::shared_future>;

template <FutureType F>
bool IsReady(const F& f) {
  return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

template <FutureType F, class Clock, class Duration>
Status Wait(const F& f, const std::chrono::time_point<Clock, Duration>& deadline) {
  return f.wait_until(deadline) == std::future_status::ready
      ? Status::OK() : STATUS(TimedOut, "Timeout waiting on future");
}

} // namespace yb
