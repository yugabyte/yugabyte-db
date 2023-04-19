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
#pragma once

#include "yb/gutil/macros.h"
#include "yb/util/countdown_latch.h"

namespace yb {

// A promise boxes a value which is to be provided at some time in the future.
// A single producer calls Set(...), and any number of consumers can call Get()
// to retrieve the produced value.
//
// In Guava terms, this is a SettableFuture<T>.
template<typename T>
class Promise {
 public:
  Promise() : latch_(1) {}
  ~Promise() {}

  // Reset the promise to be used again.
  // For this to be safe, there must be some kind of external synchronization
  // ensuring that no threads are still accessing the value from the previous
  // incarnation of the promise.
  void Reset() {
    latch_.Reset(1);
    val_ = T();
  }

  // Block until a value is available, and return a reference to it.
  const T& Get() const {
    latch_.Wait();
    return val_;
  }

  // Wait for the promised value to become available with the given timeout.
  //
  // Returns NULL if the timeout elapses before a value is available.
  // Otherwise returns a pointer to the value. This pointer's lifetime is
  // tied to the lifetime of the Promise object.
  const T* WaitFor(const MonoDelta& delta) const {
    if (latch_.WaitFor(delta)) {
      return &val_;
    } else {
      return NULL;
    }
  }

  // Set the value of this promise.
  // This may be called at most once.
  void Set(const T& val) {
    DCHECK_EQ(latch_.count(), 1) << "Already set!";
    val_ = val;
    latch_.CountDown();
  }

 private:
  CountDownLatch latch_;
  T val_;
  DISALLOW_COPY_AND_ASSIGN(Promise);
};

} // namespace yb
