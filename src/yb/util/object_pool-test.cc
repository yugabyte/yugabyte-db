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

#ifdef __linux__
#include <sched.h>
#endif

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/sysinfo.h"

#include "yb/util/errno.h"
#include "yb/util/object_pool.h"

namespace yb {

// Simple class which maintains a count of how many objects
// are currently alive.
class MyClass {
 public:
  MyClass() {
    instance_count_++;
  }

  ~MyClass() {
    instance_count_--;
  }

  static int instance_count() {
    return instance_count_;
  }

  static void ResetCount() {
    instance_count_ = 0;
  }

 private:
  static int instance_count_;
};
int MyClass::instance_count_ = 0;

TEST(TestObjectPool, TestPooling) {
  MyClass::ResetCount();
  {
    ObjectPool<MyClass> pool;
    ASSERT_EQ(0, MyClass::instance_count());
    MyClass *a = pool.Construct();
    ASSERT_EQ(1, MyClass::instance_count());
    MyClass *b = pool.Construct();
    ASSERT_EQ(2, MyClass::instance_count());
    ASSERT_TRUE(a != b);
    pool.Destroy(b);
    ASSERT_EQ(1, MyClass::instance_count());
    MyClass *c = pool.Construct();
    ASSERT_EQ(2, MyClass::instance_count());
    ASSERT_TRUE(c == b) << "should reuse instance";
    pool.Destroy(c);

    ASSERT_EQ(1, MyClass::instance_count());
  }

  ASSERT_EQ(0, MyClass::instance_count())
    << "destructing pool should have cleared instances";
}

TEST(TestObjectPool, TestScopedPtr) {
  MyClass::ResetCount();
  ASSERT_EQ(0, MyClass::instance_count());
  ObjectPool<MyClass> pool;
  {
    ObjectPool<MyClass>::scoped_ptr sptr(
      pool.make_scoped_ptr(pool.Construct()));
    ASSERT_EQ(1, MyClass::instance_count());
  }
  ASSERT_EQ(0, MyClass::instance_count());
}

#ifdef __linux__

namespace {

// Restores the affinity mask of the calling thread on destruction, so pinning does not leak into
// the rest of the test binary.
struct ScopedThreadAffinity {
  cpu_set_t original;

  ScopedThreadAffinity() { PCHECK(sched_getaffinity(0, sizeof(original), &original) == 0); }
  ~ScopedThreadAffinity() { PCHECK(sched_setaffinity(0, sizeof(original), &original) == 0); }
};

} // namespace

// ThreadSafeObjectPool picks a pool using sched_getcpu(), so every CPU the process may be scheduled
// on has to map to a pool. Pins the calling thread to each allowed CPU in turn and takes and
// releases an object from there.
TEST(TestThreadSafeObjectPool, TakeAndReleaseOnEveryCpu) {
  MyClass::ResetCount();
  {
    ScopedThreadAffinity affinity;
    ThreadSafeObjectPool<MyClass> pool;

    int cpus_covered = 0;
    for (int cpu = 0; cpu != CPU_SETSIZE; ++cpu) {
      if (!CPU_ISSET(cpu, &affinity.original)) {
        continue;
      }

      cpu_set_t single;
      CPU_ZERO(&single);
      CPU_SET(cpu, &single);
      ASSERT_EQ(sched_setaffinity(0, sizeof(single), &single), 0) << ErrnoToString(errno);
      ASSERT_EQ(sched_getcpu(), cpu) << "a single-CPU mask must leave the thread nowhere else";

      auto* object = pool.Take();
      ASSERT_NE(object, nullptr);
      pool.Release(object);
      ++cpus_covered;
    }

    LOG(INFO) << "Exercised the pool on " << cpus_covered << " CPUs, max CPU index "
              << base::MaxCPUIndex() << ", online CPUs " << base::RawNumCPUs();
    ASSERT_GT(cpus_covered, 0);
  }

  ASSERT_EQ(0, MyClass::instance_count()) << "pool destruction must free every pooled object";
}

#endif // __linux__

} // namespace yb
