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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <semaphore.h>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include "yb/util/atomic.h"
#endif  // define(__APPLE__)

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#include "yb/util/monotime.h"

namespace yb {

// Wrapper for POSIX semaphores.
class Semaphore {
 public:
  // Initialize the semaphore with the specified capacity.
  explicit Semaphore(int capacity);
  ~Semaphore();

  // Acquire the semaphore.
  void Acquire();

  // Acquire the semaphore within the given timeout. Returns true if successful.
  bool TimedAcquire(const MonoDelta& timeout);

  // Try to acquire the semaphore immediately. Returns false if unsuccessful.
  bool TryAcquire();

  // Release the semaphore.
  void Release();

  // Get the current value of the semaphore.
  int GetValue();

  // Boost-compatible wrappers.
  void lock() { Acquire(); }
  void unlock() { Release(); }
  bool try_lock() { return TryAcquire(); }

 private:
#if !defined(__APPLE__)
  // Log a fatal error message. Separated out to keep the main functions
  // as small as possible in terms of code size.
  void Fatal(const char* action) ATTRIBUTE_NORETURN;
#endif  // !define(__APPLE__)

#if defined(__APPLE__)
  std::atomic<int32_t> count_;
  std::condition_variable cv_;
  // Mutex to use with the condition variable cv_.
  std::mutex mutex_;
#else
  sem_t sem_;
#endif  // define(__APPLE__)
  DISALLOW_COPY_AND_ASSIGN(Semaphore);
};

} // namespace yb
