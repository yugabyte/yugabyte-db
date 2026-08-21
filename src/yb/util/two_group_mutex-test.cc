// Copyright (c) YugabyteDB, Inc.
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
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/two_group_mutex.h"

namespace yb {

// Verifies that all readers can hold the lock at the same time -- i.e. readers never block readers.
// If the lock serialized readers, `inside` would never reach kReaders and the threads would spin
// until the deadline, leaving `all_in` false.
TEST(TwoGroupMutexTest, ReadersDoNotBlockReaders) {
  constexpr int kReaders = 8;
  TwoGroupMutex mutex;
  std::atomic<int> inside{0};
  std::atomic<bool> all_in{false};

  std::vector<std::thread> threads;
  for (int i = 0; i != kReaders; ++i) {
    threads.emplace_back([&] {
      TwoGroupMutex::ReadLock lock(mutex);
      if (++inside == kReaders) {
        all_in = true;
      }
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      while (!all_in.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
      }
      --inside;
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  ASSERT_TRUE(all_in.load());
}

// Symmetric to the above: writers never block writers.
TEST(TwoGroupMutexTest, WritersDoNotBlockWriters) {
  constexpr int kWriters = 8;
  TwoGroupMutex mutex;
  std::atomic<int> inside{0};
  std::atomic<bool> all_in{false};

  std::vector<std::thread> threads;
  for (int i = 0; i != kWriters; ++i) {
    threads.emplace_back([&] {
      TwoGroupMutex::WriteLock lock(mutex);
      if (++inside == kWriters) {
        all_in = true;
      }
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      while (!all_in.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
      }
      --inside;
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  ASSERT_TRUE(all_in.load());
}

// Under mixed load, a read phase and a write phase never overlap, and both groups keep making
// progress (no deadlock, no starvation). Also catches torn state under TSAN.
TEST(TwoGroupMutexTest, ReadersAndWritersAreExclusive) {
  constexpr int kReaders = 4;
  constexpr int kWriters = 4;
  TwoGroupMutex mutex;
  std::atomic<int> active_readers{0};
  std::atomic<int> active_writers{0};
  std::atomic<bool> violation{false};
  std::atomic<int64_t> read_ops{0};
  std::atomic<int64_t> write_ops{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> threads;
  for (int i = 0; i != kReaders; ++i) {
    threads.emplace_back([&] {
      while (!stop.load()) {
        TwoGroupMutex::ReadLock lock(mutex);
        ++active_readers;
        // No writer may be in its critical section while we are in ours.
        if (active_writers.load() != 0) {
          violation = true;
        }
        std::this_thread::yield();
        if (active_writers.load() != 0) {
          violation = true;
        }
        --active_readers;
        ++read_ops;
      }
    });
  }
  for (int i = 0; i != kWriters; ++i) {
    threads.emplace_back([&] {
      while (!stop.load()) {
        TwoGroupMutex::WriteLock lock(mutex);
        ++active_writers;
        if (active_readers.load() != 0) {
          violation = true;
        }
        std::this_thread::yield();
        if (active_readers.load() != 0) {
          violation = true;
        }
        --active_writers;
        ++write_ops;
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));
  stop = true;
  for (auto& t : threads) {
    t.join();
  }

  ASSERT_FALSE(violation.load());
  // Both groups advanced, so neither deadlocked nor starved.
  ASSERT_GT(read_ops.load(), 0);
  ASSERT_GT(write_ops.load(), 0);
}

}  // namespace yb
