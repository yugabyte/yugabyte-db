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

// Lock/unlock throughput of std::mutex and absl::Mutex as the number of contending threads grows.
// The critical section is a single increment, so this stresses lock handoff rather than modeling
// any real workload.
//
// Build and run (use a release build for meaningful numbers):
//   ./yb_build.sh release --target mutex-benchmark
//   build/latest/benchmarks-util/mutex-benchmark --benchmark_filter=AbslMutex
//
// ThreadRange() runs the loop body on 1, 2, 4, ... 64 threads at once. "Time" is the average time
// per iteration as seen by one thread; items_per_second is the total across all threads.
//
// See https://github.com/google/benchmark/blob/main/docs/user_guide.md for the available
// --benchmark_* flags.

#include <absl/synchronization/mutex.h>
#include <benchmark/benchmark.h>

#include <mutex>

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

} // namespace
} // namespace yb
