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

#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "yb/master/catalog_manager_mutex.h"
#include "yb/util/flags.h"
#include "yb/util/monotime.h"
#include "yb/util/shared_lock.h"
#include "yb/util/test_util.h"

DECLARE_string(catalog_manager_mutex_type);

DEFINE_test_flag(int32, catalog_mutex_test_waiters, 2000,
    "Number of threads that queue for the lock behind the writer in the convoy test.");

namespace yb::master {

namespace {

double ProcessCpuSeconds() {
  rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
         (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

// CPU-bound work whose duration depends on how much CPU the calling thread gets.
uint64_t Spin(uint64_t iterations) {
  uint64_t x = 1;
  for (uint64_t i = 0; i < iterations; ++i) {
    x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    // Keeps the compiler from collapsing the loop into a closed form.
    asm volatile("" : "+r"(x));
  }
  return x;
}

} // namespace

class CatalogManagerMutexTest : public YBTest, public ::testing::WithParamInterface<std::string> {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_mutex_type) = GetParam();
  }
};

INSTANTIATE_TEST_SUITE_P(, CatalogManagerMutexTest,
    ::testing::Values("spinlock", "rwmutex", "absl"),
    [](const auto& info) { return info.param; });

// The CE-1824 convoy: a long shared holder (the backup repack), a queued writer (the bg task) and
// thousands of RPC threads queued behind the writer. Measures how long the holder's fixed amount of
// work takes and how much CPU the process burns while the waiters wait.
TEST_P(CatalogManagerMutexTest, Convoy) {
  CatalogManagerMutex mutex;

  constexpr uint64_t kWork = 2'000'000'000ULL;
  std::atomic<uint64_t> sink{0};
  auto start = MonoTime::Now();
  sink += Spin(kWork);
  const auto alone = MonoTime::Now() - start;

  std::atomic<bool> holder_has_lock{false};
  MonoDelta held;
  double cpu_during_hold = 0;

  std::thread holder([&] {
    SharedLock<CatalogManagerMutex> lock(mutex);
    holder_has_lock = true;
    // Let the writer and the waiters queue up before timing the work.
    SleepFor(MonoDelta::FromSeconds(2));
    auto cpu_start = ProcessCpuSeconds();
    auto hold_start = MonoTime::Now();
    sink += Spin(kWork);
    held = MonoTime::Now() - hold_start;
    cpu_during_hold = ProcessCpuSeconds() - cpu_start;
  });
  while (!holder_has_lock) {
    std::this_thread::yield();
  }

  std::thread writer([&] {
    std::lock_guard lock(mutex);
  });
  SleepFor(MonoDelta::FromMilliseconds(100));

  std::vector<std::thread> waiters;
  waiters.reserve(FLAGS_TEST_catalog_mutex_test_waiters);
  for (int i = 0; i < FLAGS_TEST_catalog_mutex_test_waiters; ++i) {
    waiters.emplace_back([&] {
      SharedLock<CatalogManagerMutex> lock(mutex);
    });
  }

  holder.join();
  writer.join();
  for (auto& t : waiters) {
    t.join();
  }

  std::cerr << GetParam() << " convoy with "
            << FLAGS_TEST_catalog_mutex_test_waiters << " waiters: holder work took "
            << held.ToSeconds() << " s (alone " << alone.ToSeconds() << " s, "
            << held.ToSeconds() / alone.ToSeconds() << "x), process CPU during the hold "
            << cpu_during_hold << " s = " << cpu_during_hold / held.ToSeconds() << " cores"
            << std::endl;
}

// Steady state: many threads take the lock shared for a moment while one writer takes it
// exclusively every millisecond, like CatalogManagerBgTasks and DDLs against RPC traffic.
TEST_P(CatalogManagerMutexTest, Throughput) {
  CatalogManagerMutex mutex;
  const int kReaders = std::max(4u, std::thread::hardware_concurrency());
  const auto kDuration = MonoDelta::FromSeconds(5);

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> shared_ops{0};
  std::atomic<uint64_t> sink{0};
  std::vector<double> writer_wait_us;

  auto cpu_start = ProcessCpuSeconds();
  std::vector<std::thread> readers;
  for (int i = 0; i < kReaders; ++i) {
    readers.emplace_back([&] {
      uint64_t ops = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        SharedLock<CatalogManagerMutex> lock(mutex);
        sink += Spin(200);
        ++ops;
      }
      shared_ops += ops;
    });
  }
  std::thread writer([&] {
    while (!stop) {
      auto t = MonoTime::Now();
      {
        std::lock_guard lock(mutex);
        writer_wait_us.push_back((MonoTime::Now() - t).ToMicroseconds());
        sink += Spin(2000);
      }
      SleepFor(MonoDelta::FromMilliseconds(1));
    }
  });
  SleepFor(kDuration);
  stop = true;
  writer.join();
  for (auto& t : readers) {
    t.join();
  }
  auto cpu = ProcessCpuSeconds() - cpu_start;

  std::sort(writer_wait_us.begin(), writer_wait_us.end());
  auto pct = [&](double p) {
    return writer_wait_us.empty() ? 0 : writer_wait_us[(writer_wait_us.size() - 1) * p];
  };
  std::cerr << GetParam() << " throughput with " << kReaders
            << " readers: " << shared_ops / kDuration.ToSeconds() << " shared ops/s, "
            << writer_wait_us.size() << " exclusive acquisitions, writer wait p50 " << pct(0.5)
            << " us p99 " << pct(0.99) << " us max " << pct(1.0) << " us, CPU " << cpu << " s"
            << std::endl;
}

// Cost of releasing the lock to many waiters: N threads wait for a shared lock behind an exclusive
// holder, then the holder unlocks. Measures how long unlock() itself takes, how long until the
// first and the last waiter get the lock, and the process CPU used during the handoff.
TEST_P(CatalogManagerMutexTest, ReleaseToWaiters) {
  for (int waiters : {1, 10, 100, 1000, 4000}) {
    for (int rep = 0; rep < 3; ++rep) {
      CatalogManagerMutex mutex;
      mutex.lock();
      std::atomic<int> started{0};
      std::vector<MonoTime> acquired(waiters);
      std::vector<std::thread> threads;
      threads.reserve(waiters);
      for (int i = 0; i < waiters; ++i) {
        threads.emplace_back([&, i] {
          ++started;
          SharedLock<CatalogManagerMutex> lock(mutex);
          acquired[i] = MonoTime::Now();
        });
      }
      while (started < waiters) {
        std::this_thread::yield();
      }
      // Give every waiter time to block (or start polling).
      SleepFor(MonoDelta::FromMilliseconds(300 + waiters / 4));

      auto cpu_start = ProcessCpuSeconds();
      auto t0 = MonoTime::Now();
      mutex.unlock();
      auto unlock_us = (MonoTime::Now() - t0).ToMicroseconds();
      for (auto& t : threads) {
        t.join();
      }
      auto cpu = ProcessCpuSeconds() - cpu_start;
      std::sort(acquired.begin(), acquired.end());
      std::cerr << GetParam() << " release to " << waiters
                << " waiters: unlock() " << unlock_us << " us, first waiter in "
                << (acquired.front() - t0).ToMicroseconds() << " us, median "
                << (acquired[waiters / 2] - t0).ToMicroseconds() << " us, last "
                << (acquired.back() - t0).ToMicroseconds() << " us, CPU " << cpu * 1000
                << " ms" << std::endl;
    }
  }
}

// Last reader handing the lock to a queued writer while many readers queue behind that writer
// (the repack finishing while the bg task and RPCs wait).
TEST_P(CatalogManagerMutexTest, ReaderHandoffToWriter) {
  for (int waiters : {0, 100, 1000, 4000}) {
    for (int rep = 0; rep < 3; ++rep) {
      CatalogManagerMutex mutex;
      mutex.lock_shared();
      MonoTime writer_in;
      std::thread writer([&] {
        std::lock_guard lock(mutex);
        writer_in = MonoTime::Now();
      });
      SleepFor(MonoDelta::FromMilliseconds(100));
      std::vector<std::thread> threads;
      for (int i = 0; i < waiters; ++i) {
        threads.emplace_back([&] { SharedLock<CatalogManagerMutex> lock(mutex); });
      }
      SleepFor(MonoDelta::FromMilliseconds(300 + waiters / 4));
      auto cpu_start = ProcessCpuSeconds();
      auto t0 = MonoTime::Now();
      mutex.unlock_shared();
      auto unlock_us = (MonoTime::Now() - t0).ToMicroseconds();
      writer.join();
      auto writer_us = (writer_in - t0).ToMicroseconds();
      for (auto& t : threads) {
        t.join();
      }
      auto all_us = (MonoTime::Now() - t0).ToMicroseconds();
      auto cpu = ProcessCpuSeconds() - cpu_start;
      std::cerr << GetParam() << " reader->writer handoff with "
                << waiters << " queued readers: unlock_shared() " << unlock_us
                << " us, writer in " << writer_us << " us, all done " << all_us << " us, CPU "
                << cpu * 1000 << " ms" << std::endl;
    }
  }
}

} // namespace yb::master
