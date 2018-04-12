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
#ifndef YB_UTIL_ASYNC_UTIL_H
#define YB_UTIL_ASYNC_UTIL_H

#include <condition_variable>
#include <future>
#include <mutex>

#include <boost/function.hpp>

#include "yb/gutil/bind.h"
#include "yb/gutil/macros.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"

namespace yb {

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

  void StatusCB(const Status& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!assigned_) {
      assigned_ = true;
      status_ = status;
      cond_.notify_all();
    } else {
      LOG(DFATAL) << "Status already assigned, existing: " << status_ << ", new: " << status;
    }
  }

  StatusCallback AsStatusCallback() {
    DCHECK(!assigned_);

    // Synchronizers are often declared on the stack, so it doesn't make
    // sense for a callback to take a reference to its synchronizer.
    //
    // Note: this means the returned callback _must_ go out of scope before
    // its synchronizer.
    return Bind(&Synchronizer::StatusCB, Unretained(this));
  }

  StdStatusCallback AsStdStatusCallback() {
    DCHECK(!assigned_);

    // Synchronizers are often declared on the stack, so it doesn't make
    // sense for a callback to take a reference to its synchronizer.
    //
    // Note: this means the returned callback _must_ go out of scope before
    // its synchronizer.
    return std::bind(&Synchronizer::StatusCB, this, std::placeholders::_1);
  }

  boost::function<void(const Status&)> AsStatusFunctor() {
    return std::bind(&Synchronizer::StatusCB, this, std::placeholders::_1);
  }

  CHECKED_STATUS Wait() {
    return WaitUntil(std::chrono::steady_clock::time_point::max());
  }

  CHECKED_STATUS WaitFor(const MonoDelta& delta) {
    return WaitUntil(std::chrono::steady_clock::now() + delta.ToSteadyDuration());
  }

  CHECKED_STATUS WaitUntil(const std::chrono::steady_clock::time_point& time) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto predicate = [this] { return assigned_; };
    if (time == std::chrono::steady_clock::time_point::max()) {
      cond_.wait(lock, predicate);
    } else if (!cond_.wait_until(lock, time, predicate)) {
      return STATUS(TimedOut, "Timed out while waiting for the callback to be called.");
    }

    return status_;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    assigned_ = false;
    status_ = Status::OK();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_;
  bool assigned_ = false;
  Status status_;
};

// Functor is any functor that accepts callback as only argument.
template <class Result, class Functor>
std::future<Result> MakeFuture(const Functor& functor) {
  auto promise = std::make_shared<std::promise<Result>>();
  functor([promise](Result result) {
    promise->set_value(std::move(result));
  });
  return promise->get_future();
}

} // namespace yb
#endif /* YB_UTIL_ASYNC_UTIL_H */
