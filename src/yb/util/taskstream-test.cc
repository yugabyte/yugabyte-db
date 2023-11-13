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

#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/atomicops.h"

#include "yb/util/blocking_queue.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/taskstream.h"
#include "yb/util/test_macros.h"
#include "yb/util/threadpool.h"

using std::atomic;
using std::shared_ptr;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;

using strings::Substitute;
DECLARE_bool(enable_tracing);

using std::shared_ptr;

namespace yb {

namespace {

static Status BuildMinMaxTestPool(
    int min_threads, int max_threads, std::unique_ptr<ThreadPool> *pool) {
  return ThreadPoolBuilder("test").set_min_threads(min_threads)
      .set_max_threads(max_threads)
      .Build(pool);
}

} // anonymous namespace

class TestTaskStream : public ::testing::Test {
 public:
  TestTaskStream() {
    FLAGS_enable_tracing = true;
  }
 protected:
  const int32_t kTaskstreamQueueMaxSize = 100000;
  const MonoDelta kTaskstreamQueueMaxWait = MonoDelta::FromMilliseconds(1000);
};

static void SimpleTaskStreamMethod(int* value, std::atomic<int32_t>* counter) {
  if (value == nullptr) {
    return;
  }
  int n = *value;
  while (n--) {
    (*counter)++;
  }
}

TEST_F(TestTaskStream, TestSimpleTaskStream) {
  using namespace std::placeholders;
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(1, 1, &thread_pool));

  std::atomic<int32_t> counter(0);
  std::function<void (int*)> f1 = std::bind(&SimpleTaskStreamMethod, _1, &counter);

  TaskStream<int> taskStream(f1, thread_pool.get(), kTaskstreamQueueMaxSize,
                             kTaskstreamQueueMaxWait);
  ASSERT_OK(taskStream.Start());
  int a[4] = {10, 9, 8, 7};
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(taskStream.Submit(&a[i]));
  }
  thread_pool->Wait();
  ASSERT_EQ(34, counter.load(std::memory_order_acquire));
  taskStream.Stop();
  thread_pool->Shutdown();
}

TEST_F(TestTaskStream, TestTwoTaskStreams) {
  using namespace std::placeholders;
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(1, 1, &thread_pool));

  std::atomic<int32_t> counter0(0);
  std::atomic<int32_t> counter1(0);
  std::function<void (int*)> f0 = std::bind(&SimpleTaskStreamMethod, _1, &counter0);
  std::function<void (int*)> f1 = std::bind(&SimpleTaskStreamMethod, _1, &counter1);

  TaskStream<int> taskStream0(f0, thread_pool.get(), kTaskstreamQueueMaxSize,
                              kTaskstreamQueueMaxWait);
  TaskStream<int> taskStream1(f1, thread_pool.get(), kTaskstreamQueueMaxSize,
                              kTaskstreamQueueMaxWait);
  ASSERT_OK(taskStream0.Start());
  ASSERT_OK(taskStream1.Start());
  int a[4] = {10, 9, 8, 7};
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(taskStream0.Submit(&a[i]));
  }
  int b[5] = {1, 2, 3, 4, 5};
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(taskStream1.Submit(&b[i]));
  }
  thread_pool->Wait();
  ASSERT_EQ(34, counter0.load(std::memory_order_acquire));
  ASSERT_EQ(15, counter1.load(std::memory_order_acquire));
  taskStream0.Stop();
  taskStream1.Stop();
  thread_pool->Shutdown();
}
} // namespace yb
