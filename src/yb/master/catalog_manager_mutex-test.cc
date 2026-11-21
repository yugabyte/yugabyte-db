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

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "yb/master/catalog_manager_mutex.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/monotime.h"
#include "yb/util/shared_lock.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

DEFINE_test_flag(int32, catalog_mutex_test_waiters, 2000,
    "Number of threads that queue for the lock behind the writer in the convoy test.");

namespace yb::master {

namespace {

// The full sizes take minutes on small hosts and under sanitizers. Without YB_ALLOW_SLOW_TESTS the
// cases run smaller versions that still check the same invariants.
template <class T>
T FullOrReduced(T full, T reduced) {
  return AllowSlowTests() ? full : reduced;
}

double CpuSeconds(const CpuTimes& times) {
  return times.user_cpu_seconds() + times.system_cpu_seconds();
}

MonoTime Earliest(const std::vector<MonoTime>& times) {
  return *std::min_element(times.begin(), times.end());
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

class CatalogManagerMutexTest : public YBTest {};

// The CE-1824 convoy: a long shared holder (the backup repack), a queued writer (the bg task) and
// thousands of RPC threads queued behind the writer. Measures how long the holder's fixed amount of
// work takes and how much CPU the process burns while the waiters wait.
TEST_F(CatalogManagerMutexTest, Convoy) {
  CatalogManagerMutex mutex;
  const auto kWork = FullOrReduced<uint64_t>(2'000'000'000ULL, 200'000'000ULL);
  const int num_waiters = FullOrReduced(
      FLAGS_TEST_catalog_mutex_test_waiters, std::min(FLAGS_TEST_catalog_mutex_test_waiters, 200));

  std::atomic<uint64_t> sink{0};
  auto start = MonoTime::Now();
  sink += Spin(kWork);
  const auto alone = MonoTime::Now() - start;

  std::atomic<bool> holder_has_lock{false};
  CpuTimes hold;
  MonoTime holder_out;
  MonoTime writer_in;
  std::vector<MonoTime> waiter_in(num_waiters);

  TestThreadHolder threads;
  threads.AddThread([&] {
    SharedLock<CatalogManagerMutex> lock(mutex);
    holder_has_lock = true;
    // Settling time for the writer and the waiters to queue, not synchronization: the ordering
    // asserts below fail if they did not queue in time.
    SleepFor(MonoDelta::FromSeconds(2));
    Stopwatch sw(Stopwatch::ALL_THREADS);
    sw.start();
    sink += Spin(kWork);
    sw.stop();
    hold = sw.elapsed();
    holder_out = MonoTime::Now();
  });
  ASSERT_OK(WaitFor(
      [&] { return holder_has_lock.load(); }, MonoDelta::FromSeconds(30), "holder has the lock"));

  threads.AddThread([&] {
    std::lock_guard lock(mutex);
    writer_in = MonoTime::Now();
  });
  SleepFor(MonoDelta::FromMilliseconds(100));

  for (int i = 0; i < num_waiters; ++i) {
    threads.AddThread([&, i] {
      SharedLock<CatalogManagerMutex> lock(mutex);
      waiter_in[i] = MonoTime::Now();
    });
  }
  threads.JoinAll();

  std::cerr << "Convoy with " << num_waiters << " waiters: holder work took "
            << hold.wall_seconds() << " s (alone " << alone.ToSeconds() << " s, "
            << hold.wall_seconds() / alone.ToSeconds() << "x), process CPU during the hold "
            << CpuSeconds(hold) << " s = " << CpuSeconds(hold) / hold.wall_seconds() << " cores"
            << std::endl;

  ASSERT_TRUE(holder_out <= writer_in)
      << "Writer got the lock " << (holder_out - writer_in) << " before the shared holder let go";
  if (num_waiters > 0) {
    ASSERT_TRUE(writer_in <= Earliest(waiter_in))
        << "A waiter got the lock " << (writer_in - Earliest(waiter_in))
        << " before the queued writer";
  }
}

// Steady state: many threads take the lock shared for a moment while one writer takes it
// exclusively every millisecond, like CatalogManagerBgTasks and DDLs against RPC traffic.
TEST_F(CatalogManagerMutexTest, Throughput) {
  CatalogManagerMutex mutex;
  const int kReaders = std::max(4u, std::thread::hardware_concurrency());
  const auto kDuration = MonoDelta::FromSeconds(FullOrReduced(5, 1));

  std::atomic<uint64_t> shared_ops{0};
  std::atomic<uint64_t> sink{0};
  std::atomic<bool> writer_inside{false};
  std::atomic<uint64_t> overlaps{0};
  std::vector<double> writer_wait_us;

  Stopwatch sw(Stopwatch::ALL_THREADS);
  sw.start();
  TestThreadHolder threads;
  auto& stop = threads.stop_flag();
  for (int i = 0; i < kReaders; ++i) {
    threads.AddThread([&] {
      uint64_t ops = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        SharedLock<CatalogManagerMutex> lock(mutex);
        // Checked on entry and on exit, since the writer holds the lock 10x longer than a reader.
        if (writer_inside.load(std::memory_order_relaxed)) {
          ++overlaps;
        }
        sink += Spin(200);
        if (writer_inside.load(std::memory_order_relaxed)) {
          ++overlaps;
        }
        ++ops;
      }
      shared_ops += ops;
    });
  }
  threads.AddThread([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      auto t = MonoTime::Now();
      {
        std::lock_guard lock(mutex);
        writer_inside.store(true, std::memory_order_relaxed);
        writer_wait_us.push_back((MonoTime::Now() - t).ToMicroseconds());
        sink += Spin(2000);
        writer_inside.store(false, std::memory_order_relaxed);
      }
      SleepFor(MonoDelta::FromMilliseconds(1));
    }
  });
  SleepFor(kDuration);
  threads.Stop();
  auto cpu = CpuSeconds(sw.elapsed());

  std::sort(writer_wait_us.begin(), writer_wait_us.end());
  auto pct = [&](double p) {
    return writer_wait_us.empty() ? 0 : writer_wait_us[(writer_wait_us.size() - 1) * p];
  };
  std::cerr << "Throughput with " << kReaders << " readers: "
            << shared_ops / kDuration.ToSeconds() << " shared ops/s, "
            << writer_wait_us.size() << " exclusive acquisitions, writer wait p50 " << pct(0.5)
            << " us p99 " << pct(0.99) << " us max " << pct(1.0) << " us, CPU " << cpu << " s"
            << std::endl;

  ASSERT_EQ(overlaps.load(), 0) << "Readers saw the writer inside the lock";
  ASSERT_GT(shared_ops.load(), 0);
  ASSERT_FALSE(writer_wait_us.empty());
}

// Cost of releasing the lock to many waiters: N threads wait for a shared lock behind an exclusive
// holder, then the holder unlocks. Measures how long unlock() itself takes, how long until the
// first and the last waiter get the lock, and the process CPU used during the handoff.
TEST_F(CatalogManagerMutexTest, ReleaseToWaiters) {
  for (int waiters : FullOrReduced<std::vector<int>>({1, 10, 100, 1000, 4000}, {1, 10, 100})) {
    for (int rep = 0; rep < 3; ++rep) {
      CatalogManagerMutex mutex;
      mutex.lock();
      std::atomic<int> started{0};
      std::vector<MonoTime> acquired(waiters);
      TestThreadHolder threads;
      for (int i = 0; i < waiters; ++i) {
        threads.AddThread([&, i] {
          ++started;
          SharedLock<CatalogManagerMutex> lock(mutex);
          acquired[i] = MonoTime::Now();
        });
      }
      while (started < waiters) {
        std::this_thread::yield();
      }
      // Settling time for every waiter to block (or start polling), not synchronization.
      SleepFor(MonoDelta::FromMilliseconds(300 + waiters / 4));

      Stopwatch sw(Stopwatch::ALL_THREADS);
      sw.start();
      auto t0 = MonoTime::Now();
      mutex.unlock();
      auto unlock_us = (MonoTime::Now() - t0).ToMicroseconds();
      threads.JoinAll();
      auto cpu = CpuSeconds(sw.elapsed());
      std::sort(acquired.begin(), acquired.end());
      std::cerr << "Release to " << waiters << " waiters: unlock() " << unlock_us
                << " us, first waiter in "
                << (acquired.front() - t0).ToMicroseconds() << " us, median "
                << (acquired[waiters / 2] - t0).ToMicroseconds() << " us, last "
                << (acquired.back() - t0).ToMicroseconds() << " us, CPU " << cpu * 1000
                << " ms" << std::endl;

      ASSERT_TRUE(t0 <= acquired.front())
          << "A reader got the lock " << (t0 - acquired.front()) << " before the writer let go";
    }
  }
}

// Last reader handing the lock to a queued writer while many readers queue behind that writer
// (the repack finishing while the bg task and RPCs wait).
TEST_F(CatalogManagerMutexTest, ReaderHandoffToWriter) {
  for (int waiters : FullOrReduced<std::vector<int>>({0, 100, 1000, 4000}, {0, 100})) {
    for (int rep = 0; rep < 3; ++rep) {
      CatalogManagerMutex mutex;
      mutex.lock_shared();
      MonoTime writer_in;
      std::vector<MonoTime> reader_in(waiters);
      TestThreadHolder threads;
      threads.AddThread([&] {
        std::lock_guard lock(mutex);
        writer_in = MonoTime::Now();
      });
      // Settling time for the writer to queue, not synchronization: the ordering assert below
      // fails if it did not queue before the readers.
      SleepFor(MonoDelta::FromMilliseconds(100));
      for (int i = 0; i < waiters; ++i) {
        threads.AddThread([&, i] {
          SharedLock<CatalogManagerMutex> lock(mutex);
          reader_in[i] = MonoTime::Now();
        });
      }
      SleepFor(MonoDelta::FromMilliseconds(300 + waiters / 4));
      Stopwatch sw(Stopwatch::ALL_THREADS);
      sw.start();
      auto t0 = MonoTime::Now();
      mutex.unlock_shared();
      auto unlock_us = (MonoTime::Now() - t0).ToMicroseconds();
      threads.JoinAll();
      auto writer_us = (writer_in - t0).ToMicroseconds();
      auto all_us = (MonoTime::Now() - t0).ToMicroseconds();
      auto cpu = CpuSeconds(sw.elapsed());
      std::cerr << "Reader->writer handoff with " << waiters
                << " queued readers: unlock_shared() " << unlock_us
                << " us, writer in " << writer_us << " us, all done " << all_us << " us, CPU "
                << cpu * 1000 << " ms" << std::endl;

      ASSERT_TRUE(t0 <= writer_in)
          << "Writer got the lock " << (t0 - writer_in) << " before the last reader let go";
      if (waiters > 0) {
        ASSERT_TRUE(writer_in <= Earliest(reader_in))
            << "A reader got the lock " << (writer_in - Earliest(reader_in))
            << " before the queued writer";
      }
    }
  }
}

} // namespace yb::master
