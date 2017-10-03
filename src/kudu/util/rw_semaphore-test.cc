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

#include <gtest/gtest.h>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <vector>

#include "kudu/util/monotime.h"
#include "kudu/util/rw_semaphore.h"

using boost::thread;
using std::vector;

namespace kudu {
struct SharedState {
  SharedState() : done(false), int_var(0) {}

  bool done;
  int64_t int_var;
  rw_semaphore sem;
};

// Thread which increases the value in the shared state under the write lock.
void Writer(SharedState* state) {
  int i = 0;
  while (true) {
    boost::lock_guard<rw_semaphore> l(state->sem);
    state->int_var += (i++);
    if (state->done) {
      break;
    }
  }
}

// Thread which verifies that the value in the shared state only increases.
void Reader(SharedState* state) {
  int prev_val = 0;
  while (true) {
    boost::shared_lock<rw_semaphore> l(state->sem);
    // The int var should only be seen to increase.
    CHECK_GE(state->int_var, prev_val);
    prev_val = state->int_var;
    if (state->done) {
      break;
    }
  }
}

// Test which verifies basic functionality of the semaphore.
// When run under TSAN this also verifies the barriers.
TEST(RWSemaphoreTest, TestBasicOperation) {
  SharedState s;
  vector<thread*> threads;
  // Start 5 readers and writers.
  for (int i = 0; i < 5; i++) {
    threads.push_back(new thread(Reader, &s));
    threads.push_back(new thread(Writer, &s));
  }

  // Let them contend for a short amount of time.
  SleepFor(MonoDelta::FromMilliseconds(50));

  // Signal them to stop.
  {
    boost::lock_guard<rw_semaphore> l(s.sem);
    s.done = true;
  }

  for (thread* t : threads) {
    t->join();
    delete t;
  }
}

} // namespace kudu
