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
#include <mutex>
#include <unordered_set>

#include "yb/util/logging.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stl_util.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/locks.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/threadlocal.h"

using std::unordered_set;
using std::vector;
using strings::Substitute;

namespace yb {
namespace threadlocal {

class ThreadLocalTest : public YBTest {};

const int kTargetCounterVal = 1000000;

class Counter;
typedef unordered_set<Counter*> CounterPtrSet;
typedef Mutex RegistryLockType;
typedef simple_spinlock CounterLockType;

// Registry to provide reader access to the thread-local Counters.
// The methods are only thread-safe if the calling thread holds the lock.
class CounterRegistry {
 public:
  CounterRegistry() {
  }

  RegistryLockType* get_lock() const {
    return &lock_;
  }

  bool RegisterUnlocked(Counter* counter) {
    LOG(INFO) << "Called RegisterUnlocked()";
    return InsertIfNotPresent(&counters_, counter);
  }

  bool UnregisterUnlocked(Counter* counter) {
    LOG(INFO) << "Called UnregisterUnlocked()";
    return counters_.erase(counter) > 0;
  }

  CounterPtrSet* GetCountersUnlocked() {
    return &counters_;
  }

 private:
  mutable RegistryLockType lock_;
  CounterPtrSet counters_;
  DISALLOW_COPY_AND_ASSIGN(CounterRegistry);
};

// A simple Counter class that registers itself with a CounterRegistry.
class Counter {
 public:
  Counter(CounterRegistry* registry, int val)
    : tid_(Env::Default()->gettid()),
      registry_(CHECK_NOTNULL(registry)),
      val_(val) {
    LOG(INFO) << "Counter::~Counter(): tid = " << tid_ << ", addr = " << this << ", val = " << val_;
    std::lock_guard reg_lock(*registry_->get_lock());
    CHECK(registry_->RegisterUnlocked(this));
  }

  ~Counter() {
    LOG(INFO) << "Counter::~Counter(): tid = " << tid_ << ", addr = " << this << ", val = " << val_;
    std::lock_guard reg_lock(*registry_->get_lock());
    std::lock_guard self_lock(lock_);
    LOG(INFO) << tid_ << ": deleting self from registry...";
    CHECK(registry_->UnregisterUnlocked(this));
  }

  uint64_t tid() {
    return tid_;
  }

  CounterLockType* get_lock() const {
    return &lock_;
  }

  void IncrementUnlocked() {
    val_++;
  }

  int GetValueUnlocked() {
    return val_;
  }

 private:
  // We expect that most of the time this lock will be uncontended.
  mutable CounterLockType lock_;

  // TID of thread that constructed this object.
  const uint64_t tid_;

  // Register / unregister ourselves with this on construction / destruction.
  CounterRegistry* const registry_;

  // Current value of the counter.
  int val_;

  DISALLOW_COPY_AND_ASSIGN(Counter);
};

// Create a new THREAD_LOCAL Counter and loop an increment operation on it.
static void RegisterCounterAndLoopIncr(CounterRegistry* registry,
                                       CountDownLatch* counters_ready,
                                       CountDownLatch* reader_ready,
                                       CountDownLatch* counters_done,
                                       CountDownLatch* reader_done) {
  BLOCK_STATIC_THREAD_LOCAL(Counter, counter, registry, 0);
  // Inform the reader that we are alive.
  counters_ready->CountDown();
  // Let the reader initialize before we start counting.
  reader_ready->Wait();
  // Now rock & roll on the counting loop.
  for (int i = 0; i < kTargetCounterVal; i++) {
    std::lock_guard l(*counter->get_lock());
    counter->IncrementUnlocked();
  }
  // Let the reader know we're ready for him to verify our counts.
  counters_done->CountDown();
  // Wait until the reader is done before we exit the thread, which will call
  // delete on the Counter.
  reader_done->Wait();
}

// Iterate over the registered counters and their values.
static uint64_t Iterate(CounterRegistry* registry, int expected_counters) {
  uint64_t sum = 0;
  int seen_counters = 0;
  std::lock_guard l(*registry->get_lock());
  for (Counter* counter : *registry->GetCountersUnlocked()) {
    uint64_t value;
    {
      std::lock_guard l(*counter->get_lock());
      value = counter->GetValueUnlocked();
    }
    LOG(INFO) << "tid " << counter->tid() << " (counter " << counter << "): " << value;
    sum += value;
    seen_counters++;
  }
  CHECK_EQ(expected_counters, seen_counters);
  return sum;
}

static void TestThreadLocalCounters(CounterRegistry* registry, const int num_threads) {
  LOG(INFO) << "Starting threads...";
  vector<scoped_refptr<yb::Thread> > threads;

  CountDownLatch counters_ready(num_threads);
  CountDownLatch reader_ready(1);
  CountDownLatch counters_done(num_threads);
  CountDownLatch reader_done(1);
  for (int i = 0; i < num_threads; i++) {
    scoped_refptr<yb::Thread> new_thread;
    CHECK_OK(yb::Thread::Create("test", strings::Substitute("t$0", i),
        &RegisterCounterAndLoopIncr, registry, &counters_ready, &reader_ready,
        &counters_done, &reader_done, &new_thread));
    threads.push_back(new_thread);
  }

  // Wait for all threads to start and register their Counters.
  counters_ready.Wait();
  CHECK_EQ(0, Iterate(registry, num_threads));
  LOG(INFO) << "--";

  // Let the counters start spinning.
  reader_ready.CountDown();

  // Try to catch them in the act, just for kicks.
  for (int i = 0; i < 2; i++) {
    Iterate(registry, num_threads);
    LOG(INFO) << "--";
    SleepFor(MonoDelta::FromMicroseconds(1));
  }

  // Wait until they're done and assure they sum up properly.
  counters_done.Wait();
  LOG(INFO) << "Checking Counter sums...";
  CHECK_EQ(kTargetCounterVal * num_threads, Iterate(registry, num_threads));
  LOG(INFO) << "Counter sums add up!";
  reader_done.CountDown();

  LOG(INFO) << "Joining & deleting threads...";
  for (scoped_refptr<yb::Thread> thread : threads) {
    CHECK_OK(ThreadJoiner(thread.get()).Join());
  }
  LOG(INFO) << "Done.";
}

TEST_F(ThreadLocalTest, TestConcurrentCounters) {
  // Run this multiple times to ensure we don't leave remnants behind in the
  // CounterRegistry.
  CounterRegistry registry;
  for (int i = 0; i < 3; i++) {
    TestThreadLocalCounters(&registry, 8);
  }
}

// Test class that stores a string in a static thread local member.
// This class cannot be instantiated. The methods are all static.
class ThreadLocalString {
 public:
  static void set(std::string value);
  static const std::string& get();
 private:
  ThreadLocalString() {
  }
  DECLARE_STATIC_THREAD_LOCAL(std::string, value_);
  DISALLOW_COPY_AND_ASSIGN(ThreadLocalString);
};

DEFINE_STATIC_THREAD_LOCAL(std::string, ThreadLocalString, value_);

void ThreadLocalString::set(std::string value) {
  INIT_STATIC_THREAD_LOCAL(std::string, value_);
  *value_ = value;
}

const std::string& ThreadLocalString::get() {
  INIT_STATIC_THREAD_LOCAL(std::string, value_);
  return *value_;
}

static void RunAndAssign(CountDownLatch* writers_ready,
                         CountDownLatch *readers_ready,
                         CountDownLatch *all_done,
                         CountDownLatch *threads_exiting,
                         const std::string& in,
                         std::string* out) {
  writers_ready->Wait();
  // Ensure it starts off as an empty string.
  CHECK_EQ("", ThreadLocalString::get());
  ThreadLocalString::set(in);

  readers_ready->Wait();
  out->assign(ThreadLocalString::get());
  all_done->Wait();
  threads_exiting->CountDown();
}

TEST_F(ThreadLocalTest, TestTLSMember) {
  const int num_threads = 8;

  vector<CountDownLatch*> writers_ready;
  vector<CountDownLatch*> readers_ready;
  vector<std::string*> out_strings;
  vector<scoped_refptr<yb::Thread> > threads;

  ElementDeleter writers_deleter(&writers_ready);
  ElementDeleter readers_deleter(&readers_ready);
  ElementDeleter out_strings_deleter(&out_strings);

  CountDownLatch all_done(1);
  CountDownLatch threads_exiting(num_threads);

  LOG(INFO) << "Starting threads...";
  for (int i = 0; i < num_threads; i++) {
    writers_ready.push_back(new CountDownLatch(1));
    readers_ready.push_back(new CountDownLatch(1));
    out_strings.push_back(new std::string());
    scoped_refptr<yb::Thread> new_thread;
    CHECK_OK(yb::Thread::Create("test", strings::Substitute("t$0", i),
        &RunAndAssign, writers_ready[i], readers_ready[i],
        &all_done, &threads_exiting, Substitute("$0", i), out_strings[i], &new_thread));
    threads.push_back(new_thread);
  }

  // Unlatch the threads in order.
  LOG(INFO) << "Writing to thread locals...";
  for (int i = 0; i < num_threads; i++) {
    writers_ready[i]->CountDown();
  }
  LOG(INFO) << "Reading from thread locals...";
  for (int i = 0; i < num_threads; i++) {
    readers_ready[i]->CountDown();
  }
  all_done.CountDown();
  // threads_exiting acts as a memory barrier.
  threads_exiting.Wait();
  for (int i = 0; i < num_threads; i++) {
    ASSERT_EQ(Substitute("$0", i), *out_strings[i]);
    LOG(INFO) << "Read " << *out_strings[i];
  }

  LOG(INFO) << "Joining & deleting threads...";
  for (scoped_refptr<yb::Thread> thread : threads) {
    CHECK_OK(ThreadJoiner(thread.get()).Join());
  }
}

} // namespace threadlocal
} // namespace yb
