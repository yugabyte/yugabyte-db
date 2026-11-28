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

// Lock/unlock throughput and handoff latency of YB's mutex types as the number of contending
// threads grows. rw_spinlock (and RWCLock) follow --use_absl_rw_spinlock, so run once with
// --use_absl_rw_spinlock=false (the original polling implementation) and once with true.
// Most cases hold the lock for a single increment, which stresses lock handoff rather than modeling
// any real workload. BM_Handoff adds hold and think times and reports how long acquisitions wait.
//
// Build and run (use a release build for meaningful numbers; add --full-lto to measure the
// statically linked, LTO-optimized variant, as release servers are linked):
//   ./yb_build.sh release --cxx-benchmark mutex-benchmark
// Add --benchmark-args '--benchmark_filter=AbslMutex' to run a subset.
//
// ThreadRange() runs the loop body on 1, 2, 4, ... 64 threads at once. "Time" is the average time
// per iteration as seen by one thread; items_per_second is the total across all threads.
//
// See https://github.com/google/benchmark/blob/main/docs/user_guide.md for the available
// --benchmark_* flags.

#include <absl/synchronization/mutex.h>
#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "yb/util/locks.h"
#include "yb/util/rwc_lock.h"

namespace yb {
namespace {

void BM_StdMutex(benchmark::State& state) {
  static std::mutex mutex;
  static int64_t counter = 0;
  for (auto _ : state) {
    std::lock_guard lock(mutex);
    benchmark::DoNotOptimize(++counter);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StdMutex)->ThreadRange(1, 64)->UseRealTime();

void BM_AbslMutex(benchmark::State& state) {
  static absl::Mutex mutex;
  static int64_t counter = 0;
  for (auto _ : state) {
    absl::MutexLock lock(&mutex);
    benchmark::DoNotOptimize(++counter);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AbslMutex)->ThreadRange(1, 64)->UseRealTime();

void BM_SimpleSpinlock(benchmark::State& state) {
  static simple_spinlock mutex;
  static int64_t counter = 0;
  for (auto _ : state) {
    std::lock_guard lock(mutex);
    benchmark::DoNotOptimize(++counter);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SimpleSpinlock)->ThreadRange(1, 64)->UseRealTime();

// Every iteration takes the lock exclusively.
template <class Mutex>
void BM_Exclusive(benchmark::State& state) {
  static Mutex mutex;
  static int64_t counter = 0;
  for (auto _ : state) {
    std::lock_guard lock(mutex);
    benchmark::DoNotOptimize(++counter);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Exclusive<rw_spinlock>)->ThreadRange(1, 64)->UseRealTime();
BENCHMARK(BM_Exclusive<RWCLock>)->ThreadRange(1, 64)->UseRealTime();

// Read-mostly: one iteration in 16 takes the lock exclusively, the rest take it shared.
template <class Mutex>
void BM_ReadMostly(benchmark::State& state) {
  static Mutex mutex;
  static int64_t counter = 0;
  size_t i = 0;
  for (auto _ : state) {
    if (++i % 16 == 0) {
      std::lock_guard lock(mutex);
      benchmark::DoNotOptimize(++counter);
    } else {
      std::shared_lock lock(mutex);
      benchmark::DoNotOptimize(counter);
    }
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ReadMostly<rw_spinlock>)->ThreadRange(1, 64)->UseRealTime();
BENCHMARK(BM_ReadMostly<std::shared_mutex>)->ThreadRange(1, 64)->UseRealTime();

// Copy-on-write pattern: one iteration in 16 takes write mode and commits, the rest read.
void BM_RWCLockReadMostlyCommit(benchmark::State& state) NO_THREAD_SAFETY_ANALYSIS {
  static RWCLock lock;
  static int64_t counter = 0;
  size_t i = 0;
  for (auto _ : state) {
    if (++i % 16 == 0) {
      lock.WriteLock();
      lock.UpgradeToCommitLock();
      benchmark::DoNotOptimize(++counter);
      lock.CommitUnlock();
    } else {
      lock.ReadLock();
      benchmark::DoNotOptimize(counter);
      lock.ReadUnlock();
    }
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RWCLockReadMostlyCommit)->ThreadRange(1, 64)->UseRealTime();

// Busy-waits, so hold and think time use CPU the way real work does.
void BusyFor(std::chrono::nanoseconds duration) {
  const auto end = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < end) {
  }
}

// Models real use: each iteration takes the lock, holds it for range(0) ns, then works without it
// for range(1) ns. With kReadMostly, 15 of 16 iterations take the lock shared for 1us and the 16th
// takes it exclusively for range(0) ns, like a writer doing slow work under a lock that readers
// need.
//
// Throughput alone favors an unfair lock that lets the releasing thread re-acquire while waiters
// sleep, so this reports how long each acquisition waited (p50, p99, max; averaged over threads)
// and process CPU time (cpu_time column).
template <class Mutex, bool kReadMostly>
void BM_Handoff(benchmark::State& state) {
  static Mutex mutex;
  const std::chrono::nanoseconds hold(state.range(0));
  const std::chrono::nanoseconds think(state.range(1));
  constexpr std::chrono::nanoseconds kSharedHold = std::chrono::microseconds(1);
  std::vector<int64_t> waits_ns;
  waits_ns.reserve(1 << 16);
  size_t i = 0;
  for (auto _ : state) {
    const auto start = std::chrono::steady_clock::now();
    auto record_wait = [&waits_ns, start] {
      waits_ns.push_back((std::chrono::steady_clock::now() - start).count());
    };
    bool exclusive = true;
    if constexpr (kReadMostly) {
      exclusive = ++i % 16 == 0;
      if (!exclusive) {
        std::shared_lock lock(mutex);
        record_wait();
        BusyFor(kSharedHold);
      }
    }
    if (exclusive) {
      std::lock_guard lock(mutex);
      record_wait();
      BusyFor(hold);
    }
    BusyFor(think);
  }
  std::sort(waits_ns.begin(), waits_ns.end());
  auto percentile_us = [&waits_ns](double p) {
    if (waits_ns.empty()) {
      return 0.0;
    }
    return waits_ns[std::min(waits_ns.size() - 1, static_cast<size_t>(p * waits_ns.size()))] / 1e3;
  };
  for (auto [name, p] :
       {std::pair{"wait_p50_us", 0.5}, {"wait_p99_us", 0.99}, {"wait_max_us", 1.0}}) {
    state.counters[name] = benchmark::Counter(percentile_us(p), benchmark::Counter::kAvgThreads);
  }
  state.SetItemsProcessed(state.iterations());
}

// {hold ns, think ns}: a short critical section, and slow work (e.g. I/O) under the lock.
#define HANDOFF_ARGS \
    Args({1000, 10000})->Args({100000, 100000})->Threads(8)->Threads(32)->UseRealTime() \
        ->MeasureProcessCPUTime()
BENCHMARK(BM_Handoff<rw_spinlock, false>)->HANDOFF_ARGS;
BENCHMARK(BM_Handoff<std::mutex, false>)->HANDOFF_ARGS;
BENCHMARK(BM_Handoff<simple_spinlock, false>)->HANDOFF_ARGS;
BENCHMARK(BM_Handoff<rw_spinlock, true>)->HANDOFF_ARGS;
BENCHMARK(BM_Handoff<std::shared_mutex, true>)->HANDOFF_ARGS;

} // namespace
} // namespace yb
