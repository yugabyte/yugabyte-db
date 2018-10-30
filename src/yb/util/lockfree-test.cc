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

#include <thread>

#include "yb/gutil/strings/substitute.h"

#include "yb/util/test_util.h"
#include "yb/util/lockfree.h"

namespace yb {

struct TestEntry : public MPSCQueueEntry<TestEntry> {
  size_t thread_index;
  size_t index;
};

TEST(LockfreeTest, MPSCQueueSimple) {
  const size_t kTotalEntries = 10;
  std::vector<TestEntry> entries(kTotalEntries);
  for (int i = 0; i != entries.size(); ++i) {
    entries[i].index = i;
  }
  MPSCQueue<TestEntry> queue;

  // Push pop 1 entry
  queue.Push(&entries[0]);
  ASSERT_EQ(&entries[0], queue.Pop());

  // Push pop multiple entries
  for (auto& entry : entries) {
    queue.Push(&entry);
  }

  for (auto& entry : entries) {
    ASSERT_EQ(&entry, queue.Pop());
  }

  // Mixed push and pop
  queue.Push(&entries[0]);
  queue.Push(&entries[1]);
  ASSERT_EQ(&entries[0], queue.Pop());
  queue.Push(&entries[2]);
  queue.Push(&entries[3]);
  ASSERT_EQ(&entries[1], queue.Pop());
  ASSERT_EQ(&entries[2], queue.Pop());
  queue.Push(&entries[4]);
  ASSERT_EQ(&entries[3], queue.Pop());
  ASSERT_EQ(&entries[4], queue.Pop());
  queue.Push(&entries[5]);
  queue.Push(&entries[6]);
  queue.Push(&entries[7]);
  ASSERT_EQ(&entries[5], queue.Pop());
  ASSERT_EQ(&entries[6], queue.Pop());
  ASSERT_EQ(&entries[7], queue.Pop());
  ASSERT_EQ(nullptr, queue.Pop());
  queue.Push(&entries[8]);
  queue.Push(&entries[9]);
  ASSERT_EQ(&entries[8], queue.Pop());
  ASSERT_EQ(&entries[9], queue.Pop());
  ASSERT_EQ(nullptr, queue.Pop());
}

TEST(LockfreeTest, MPSCQueueConcurrent) {
  constexpr size_t kThreads = 10;
  constexpr size_t kEntriesPerThread = 200000;

  std::vector<TestEntry> entries(kThreads * kEntriesPerThread);
  MPSCQueue<TestEntry> queue;

  auto start_time = MonoTime::Now();
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    size_t thread_index = threads.size();
    threads.emplace_back([&queue, thread_index, &entries] {
      size_t base = thread_index * kEntriesPerThread;
      for (size_t i = 0; i != kEntriesPerThread; ++i) {
        auto& entry = entries[base + i];
        entry.thread_index = thread_index;
        entry.index = i;
        queue.Push(&entry);
      }
    });
  }

  size_t threads_left = kThreads;
  std::vector<size_t> next_index(kThreads);
  while (threads_left > 0) {
    auto* entry = queue.Pop();
    if (!entry) {
      continue;
    }

    ASSERT_EQ(entry->index, next_index[entry->thread_index]);
    if (++next_index[entry->thread_index] == kEntriesPerThread) {
      --threads_left;
    }
  }

  auto passed = MonoTime::Now() - start_time;

  LOG(INFO) << "Passed: " << passed;

  for (auto i : next_index) {
    ASSERT_EQ(i, kEntriesPerThread);
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace yb
