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

// Throughput of every CRC32C implementation that the CPU supports, with Adler32 for comparison.
//
// Build and run (use a release build for meaningful numbers):
//   ./yb_build.sh release --cxx-benchmark crc-benchmark
// Add --benchmark-args '--benchmark_filter=Crc32c' to run a subset.

#include <zlib.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "yb/util/benchmark_main.h"
#include "yb/util/crc32c_accelerated.h"

namespace yb {
namespace {

constexpr size_t kBufferSize = 256 * 1024;
constexpr int64_t kMaxLength = 128 * 1024;

const std::vector<uint8_t>& Data() {
  static const auto data = [] {
    std::vector<uint8_t> result(kBufferSize);
    std::mt19937 rng(42);
    for (auto& byte : result) {
      byte = static_cast<uint8_t>(rng());
    }
    return result;
  }();
  return data;
}

void Lengths(benchmark::Benchmark* benchmark) {
  benchmark->RangeMultiplier(4)->Range(64, kMaxLength);
}

// Each call checksums the next length bytes of the buffer, which fits in L2.
template <class F>
void Run(benchmark::State& state, const F& checksum) {
  const auto* data = Data().data();
  const auto length = static_cast<size_t>(state.range(0));
  size_t offset = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(checksum(data + offset, length));
    offset += length;
    if (offset + length > kBufferSize) {
      offset = 0;
    }
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

void BM_Adler32(benchmark::State& state) {
  Run(state, [](const uint8_t* data, size_t length) { return adler32_z(0, data, length); });
}
BENCHMARK(BM_Adler32)->Apply(Lengths);

} // namespace

// The implementations are known only at run time, so they are registered here.
bool BenchmarkInit(int argc, char** argv) {
  for (const auto& implementation : crc::Crc32cImplementations()) {
    benchmark::RegisterBenchmark(
        std::string("BM_Crc32c/") + implementation.name,
        [extend = implementation.extend](benchmark::State& state) {
          Run(state, [extend](const uint8_t* data, size_t length) {
            return extend(0, data, length);
          });
        })->Apply(Lengths);
  }
  return DefaultBenchmarkInit(argc, argv);
}

} // namespace yb
