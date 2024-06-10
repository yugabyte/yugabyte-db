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

#include "yb/util/async_util.h"

#include "yb/gutil/bind.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/logging.h" // Required in NDEBUG mode
#include "yb/util/status_log.h"

namespace yb {

namespace {

void CallStatusCBMaybe(std::weak_ptr<Synchronizer> weak_sync, const Status& status) {
  auto sync = weak_sync.lock();
  if (sync) {
    sync->StatusCB(status);
  }
}

} // anonymous namespace

Synchronizer::~Synchronizer() {
  EnsureWaitDone();
}

void Synchronizer::StatusCB(const Status& status) {
  std::lock_guard lock(mutex_);
  if (!assigned_) {
    assigned_ = true;
    status_ = status;
    YB_PROFILE(cond_.notify_all());
  } else {
    LOG(DFATAL) << "Status already assigned, existing: " << status_ << ", new: " << status;
  }
}

StatusCallback Synchronizer::AsStatusCallback() {
  DCHECK(!assigned_);

  // Cannot destroy the synchronizer without calling Wait().
  must_wait_ = true;
  return Bind(&Synchronizer::StatusCB, Unretained(this));
}

StdStatusCallback Synchronizer::AsStdStatusCallback() {
  DCHECK(!assigned_);

  // Cannot destroy the synchronizer without calling Wait().
  must_wait_ = true;
  return std::bind(&Synchronizer::StatusCB, this, std::placeholders::_1);
}

StatusCallback Synchronizer::AsStatusCallback(const std::shared_ptr<Synchronizer>& synchronizer) {
  DCHECK(!synchronizer->assigned_);
  // No need to set must_wait_ here -- the callback knows whether Synchronizer still exists.
  std::weak_ptr<Synchronizer> weak_sync(synchronizer);
  return Bind(CallStatusCBMaybe, weak_sync);
}

Status Synchronizer::WaitUntil(const std::chrono::steady_clock::time_point& time) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto predicate = [this] { return assigned_; };
  if (time == std::chrono::steady_clock::time_point::max()) {
    cond_.wait(lock, predicate);
  } else if (!cond_.wait_until(lock, time, predicate)) {
    return STATUS(TimedOut, "Timed out while waiting for the callback to be called.");
  }

  // The callback that keep a pointer to this potentially stack-allocated synchronizer has been
  // called, assuming there was only one such callback. OK for the synchronizer to go out of
  // scope.
  must_wait_ = false;

  return status_;
}

void Synchronizer::Reset() {
  std::lock_guard lock(mutex_);
  EnsureWaitDone();
  assigned_ = false;
  status_ = Status::OK();
  must_wait_ = false;
}

void Synchronizer::EnsureWaitDone() {
  if (must_wait_) {
    static const char* kErrorMsg =
        "Synchronizer went out of scope, Wait() has returned success, callbacks may "
        "access invalid memory!";

#ifndef NDEBUG
    LOG(FATAL) << kErrorMsg;
#else
    const int kWaitSec = 10;
    YB_LOG_EVERY_N_SECS(ERROR, 1) << kErrorMsg << " Waiting up to " << kWaitSec << " seconds";
    CHECK_OK(WaitFor(MonoDelta::FromSeconds(kWaitSec)));
#endif
  }
}

}  // namespace yb
