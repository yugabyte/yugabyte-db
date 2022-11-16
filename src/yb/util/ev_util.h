//
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
//

#pragma once

#include <ev++.h>

#include "yb/util/atomic.h"
#include "yb/util/monotime.h"

namespace yb {

// We need to do all operations with ev::timer from the same thread, but for example RPC connection
// could be held by shared_ptr inside RPC calls even after reactor is shutdown. Connection
// destructor could be invoked from another thread after shutdown.
// EvTimerHolder provides a shutdown function to destroy embedded timer, so it won't be destroyed
// on holder destruction.
// This class is not thread-safe, but allowed to be destructed from any thread given that it was
// previously shutdown from owning thread.
class EvTimerHolder {
 public:
  EvTimerHolder() {}

  ~EvTimerHolder() {
    auto* timer = timer_.get();
    LOG_IF(DFATAL, timer) << "Timer " << timer << " should be already shutdown " << this;
  }

  // Should be called before first usage of timer.
  void Init(const ev::loop_ref& loop);

  // Should be invoked from the timer loop thread before holder destruction.
  // Safe to call multiple times.
  void Shutdown();

  bool IsInitialized() {
    return timer_.get();
  }

  // Starts timer after `left` time period.
  void Start(CoarseMonoClock::Duration left) {
    GetInitialized()->start(MonoDelta(left).ToSeconds(), 0 /* repeat */);
  }

  // Sets method of the `object` as a callback for timer.
  template<class T, void (T::*Method)(ev::timer &w, int)> // NOLINT
  void SetCallback(T* object) {
    GetInitialized()->set<T, Method>(object);
  }

  ev::timer& operator*() {
    return *GetInitialized();
  }

  ev::timer* operator->() {
    return GetInitialized();
  }

 private:
  ev::timer* GetInitialized() {
    LOG_IF(DFATAL, !IsInitialized()) << "Timer should be previously initialized";
    return timer_.get();
  }

  AtomicUniquePtr<ev::timer> timer_;
};

} // namespace yb
