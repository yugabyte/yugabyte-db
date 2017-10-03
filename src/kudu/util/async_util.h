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
// Utility functions which are handy when doing async/callback-based programming.
#ifndef KUDU_UTIL_ASYNC_UTIL_H
#define KUDU_UTIL_ASYNC_UTIL_H

#include "kudu/gutil/bind.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/status.h"
#include "kudu/util/status_callback.h"

namespace kudu {

// Simple class which can be used to make async methods synchronous.
// For example:
//   Synchronizer s;
//   SomeAsyncMethod(s.callback());
//   CHECK_OK(s.Wait());
class Synchronizer {
 public:
  Synchronizer()
    : l(1) {
  }
  void StatusCB(const Status& status) {
    s = status;
    l.CountDown();
  }
  StatusCallback AsStatusCallback() {
    // Synchronizers are often declared on the stack, so it doesn't make
    // sense for a callback to take a reference to its synchronizer.
    //
    // Note: this means the returned callback _must_ go out of scope before
    // its synchronizer.
    return Bind(&Synchronizer::StatusCB, Unretained(this));
  }
  Status Wait() {
    l.Wait();
    return s;
  }
  Status WaitFor(const MonoDelta& delta) {
    if (PREDICT_FALSE(!l.WaitFor(delta))) {
      return Status::TimedOut("Timed out while waiting for the callback to be called.");
    }
    return s;
  }
  void Reset() {
    l.Reset(1);
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(Synchronizer);
  Status s;
  CountDownLatch l;
};

} // namespace kudu
#endif /* KUDU_UTIL_ASYNC_UTIL_H */
