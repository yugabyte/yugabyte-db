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


#pragma once

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/statistics.h"

namespace rocksdb {
class InstrumentedCondVar;

// A wrapper class for port::Mutex that provides additional layer
// for collecting stats and instrumentation.
class InstrumentedMutex {
 public:
  explicit InstrumentedMutex(bool adaptive = false)
      : mutex_(adaptive), stats_(nullptr), env_(nullptr),
        stats_code_(0) {}

  InstrumentedMutex(
      Statistics* stats, Env* env,
      int stats_code, bool adaptive = false)
      : mutex_(adaptive), stats_(stats), env_(env),
        stats_code_(stats_code) {}

  void Lock();

  void Unlock() {
    mutex_.Unlock();
  }

  void AssertHeld() {
    mutex_.AssertHeld();
  }

  // For compatibility with std::lock_guard.
  void lock() { Lock(); }
  void unlock() { Unlock(); }

 private:
  void LockInternal();
  friend class InstrumentedCondVar;
  port::Mutex mutex_;
  Statistics* stats_;
  Env* env_;
  int stats_code_;
};

// A wrapper class for port::Mutex that provides additional layer
// for collecting stats and instrumentation.
class InstrumentedMutexLock {
 public:
  explicit InstrumentedMutexLock(InstrumentedMutex* mutex) : mutex_(mutex) {
    mutex_->Lock();
  }

  ~InstrumentedMutexLock() {
    mutex_->Unlock();
  }

 private:
  InstrumentedMutex* const mutex_;
  InstrumentedMutexLock(const InstrumentedMutexLock&) = delete;
  void operator=(const InstrumentedMutexLock&) = delete;
};

class InstrumentedCondVar {
 public:
  explicit InstrumentedCondVar(InstrumentedMutex* instrumented_mutex)
      : cond_(&(instrumented_mutex->mutex_)),
        stats_(instrumented_mutex->stats_),
        env_(instrumented_mutex->env_),
        stats_code_(instrumented_mutex->stats_code_) {}

  void Wait();

  bool TimedWait(uint64_t abs_time_us);

  void Signal() {
    cond_.Signal();
  }

  void SignalAll() {
    cond_.SignalAll();
  }

 private:
  void WaitInternal();
  bool TimedWaitInternal(uint64_t abs_time_us);
  port::CondVar cond_;
  Statistics* stats_;
  Env* env_;
  int stats_code_;
};

}  // namespace rocksdb
