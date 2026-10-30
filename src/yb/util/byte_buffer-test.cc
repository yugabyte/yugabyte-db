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
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/byte_buffer.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracked_byte_buffer.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/random_util.h"
#include "yb/util/tcmalloc_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

namespace yb {

using Buffer = ByteBuffer<8>;
using MemTrackedBuffer = MemTrackedByteBuffer<8>;

TEST(ByteBufferTest, Consumption) {
  // Ensure for non-mem-tracked buffer we don't waste memory on consumption field.
  ASSERT_LT(sizeof(Buffer), sizeof(MemTrackedBuffer));

  auto t = MemTracker::CreateTracker("t");
  auto allocated_bytes_prev = GetTCMallocCurrentAllocatedBytes();
  auto consumption_prev = t->consumption();

  auto get_allocated_bytes_delta = [&allocated_bytes_prev] {
    if (IsSanitizer()) {
      // Can't detect properly under sanitizers, so just make test always pass but still run it
      // in order to potentially catch sanitizer issues.
      return int64_t(0);
    }
    const auto old_allocated_bytes_prev = allocated_bytes_prev;
    allocated_bytes_prev = GetTCMallocCurrentAllocatedBytes();
    return allocated_bytes_prev - old_allocated_bytes_prev;
  };
  auto get_consumption_delta = [&consumption_prev, t] {
    if (IsSanitizer()) {
      return int64_t(0);
    }
    const auto old_consumption_prev = consumption_prev;
    consumption_prev = t->consumption();
    return consumption_prev - old_consumption_prev;
  };

  MemTrackedBuffer buffer(t);
  if (!IsSanitizer()) {
    ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());
  }

  buffer.append(std::string(777, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  buffer.append(std::string(555, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  auto buffer2 = std::move(buffer);
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  buffer2.assign(std::string(1234, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  MemTrackedBuffer buffer3(t, std::string(9999, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  auto buffer4 = buffer3;
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  MemTrackedBuffer buffer5(t);
  buffer5 = std::string(5555, 'x');
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  auto buffer6(buffer5);
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  std::string str6(10000, 'x');
  // Update allocated bytes to account for str6 memory usage which is not tracked by mem tracker.
  get_allocated_bytes_delta();

  buffer6 = Slice(str6);
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());
}

TEST(ByteBufferTest, Assign) {
  std::string str = "source_string"s;
  Buffer buffer(str);
  Buffer copy(buffer);
  ASSERT_EQ(buffer, copy);
  copy = buffer;
  Buffer moved(std::move(buffer));
  ASSERT_TRUE(buffer.empty()); // NOLINT(bugprone-use-after-move)
  ASSERT_EQ(moved, copy);
  moved = std::move(copy);
  ASSERT_TRUE(copy.empty()); // NOLINT(bugprone-use-after-move)
  buffer.Assign(str);
  ASSERT_EQ(buffer, moved);
}

TEST(ByteBufferTest, Append) {
  constexpr int kStringLen = 128;
  constexpr int kIterations = 1000;
  std::string str = RandomHumanReadableString(kStringLen);
  for (int i = kIterations; i-- > 0;) {
    Buffer buffer;
    while (buffer.size() < str.size()) {
      size_t len = RandomUniformInt<size_t>(0, str.size() - buffer.size());
      buffer.append(Slice(str.c_str() + buffer.size(), len));
      ASSERT_EQ(buffer.AsSlice(), Slice(str).Prefix(buffer.size()));
      if (len == 0) {
        buffer.PushBack(str[buffer.size()]);
        ASSERT_EQ(buffer.AsSlice(), Slice(str).Prefix(buffer.size()));
      }
    }
  }
}

template <class Map>
void TestMap() {
  std::vector<std::pair<std::string, int>> entries = {
      { "one"s, 1 },
      { "two"s, 2 },
      { "three"s, 3 },
  };

  Map m;
  for (const auto& p : entries) {
    ASSERT_TRUE(m.emplace(Buffer(p.first), p.second).second);
  }

  for (const auto& p : entries) {
    ASSERT_EQ(m[Buffer(p.first)], p.second);
  }
}

TEST(ByteBufferTest, Map) {
  TestMap<std::map<Buffer, int>>();
  TestMap<std::unordered_map<Buffer, int, ByteBufferHash>>();
}

// Capacity must keep growing to the smallest power of two multiple of the current capacity that
// fits the request, both from the inline buffer and from an already allocated one.
TEST(ByteBufferTest, CapacityGrowth) {
  constexpr std::pair<size_t, size_t> kReserveFromInline[] = {
      {9, 16}, {16, 16}, {17, 32}, {100, 128}, {128, 128}, {129, 256}, {1000, 1024}};
  for (const auto& [requested, expected] : kReserveFromInline) {
    Buffer buffer;
    buffer.Reserve(requested);
    ASSERT_EQ(buffer.capacity(), expected) << "requested: " << requested;
  }

  constexpr std::pair<size_t, size_t> kReserveSequence[] = {
      {17, 32}, {33, 64}, {64, 64}, {200, 256}, {5000, 8192}};
  Buffer buffer;
  for (const auto& [requested, expected] : kReserveSequence) {
    buffer.Reserve(requested);
    ASSERT_EQ(buffer.capacity(), expected) << "requested: " << requested;
  }
}

// When the rounded capacity does not fit in size_t, the requested capacity is taken as is. Such a
// request is too large to allocate and must fail explicitly instead of looping forever.
TEST(ByteBufferTest, CapacityTooLarge) {
  if (IsSanitizer()) {
    GTEST_SKIP() << "Sanitizers abort on allocation size too big";
  }
  for (auto requested : {(size_t{1} << 63) + 1, std::numeric_limits<size_t>::max()}) {
    ASSERT_DEATH(Buffer().Reserve(requested), "Failed to allocate ByteBuffer");
  }
}

namespace {

// Keeps the buffer, and so its allocation, from being optimized away.
void DoNotOptimize(const void* ptr) {
  asm volatile("" : : "r"(ptr) : "memory");
}

constexpr size_t kNumSizes = 1 << 16;
constexpr size_t kSourceSize = 1 << 17;

std::vector<size_t> GenerateSizes(size_t min, size_t max, bool log_uniform = false) {
  std::mt19937_64 rng(42);
  std::vector<size_t> result(kNumSizes);
  if (log_uniform) {
    std::uniform_real_distribution<double> dist(std::log2(min), std::log2(max));
    for (auto& size : result) {
      size = static_cast<size_t>(std::exp2(dist(rng)));
    }
  } else {
    std::uniform_int_distribution<size_t> dist(min, max);
    for (auto& size : result) {
      size = dist(rng);
    }
  }
  return result;
}

// Runs body 7 times and logs the median, min and max time per operation.
template <class Body>
void RunGrowthBenchmark(const char* name, size_t num_ops, const Body& body) {
  std::vector<double> ns_per_op;
  for (int run = 0; run < 7; ++run) {
    const auto start = std::chrono::steady_clock::now();
    body(num_ops);
    const std::chrono::duration<double, std::nano> elapsed =
        std::chrono::steady_clock::now() - start;
    ns_per_op.push_back(elapsed.count() / num_ops);
  }
  std::sort(ns_per_op.begin(), ns_per_op.end());
  LOG(INFO) << name << ": median " << std::fixed << std::setprecision(2)
            << ns_per_op[ns_per_op.size() / 2] << " ns/op (min " << ns_per_op.front()
            << ", max " << ns_per_op.back() << ")";
}

} // namespace

// Benchmark for EnsureCapacity. Run a release build pinned to one core, e.g.:
// taskset -c 3 byte_buffer-test --gtest_also_run_disabled_tests
//     --gtest_filter=ByteBufferTest.DISABLED_GrowthPerf
TEST(ByteBufferTest, DISABLED_GrowthPerf) {
#ifdef NDEBUG
  constexpr size_t kScale = 1;
#else
  constexpr size_t kScale = 100;
#endif
  std::string source(kSourceSize, 0);
  std::mt19937_64 rng(7);
  for (auto& c : source) {
    c = static_cast<char>('a' + rng() % 26);
  }
  const char* src = source.data();

  // Reused buffer whose appends always fit: the fast path only, like DocDBTest.KeyBuffer.
  {
    const auto sizes = GenerateSizes(0, 21);
    ByteBuffer<64> buffer;
    RunGrowthBenchmark("fits_reused", 20000000 / kScale, [&](size_t num_ops) {
      for (size_t i = 0; i < num_ops; ++i) {
        buffer.clear();
        for (size_t j = 0; j < 3; ++j) {
          buffer.Append(src + j * 128, sizes[(i * 3 + j) % kNumSizes]);
        }
        DoNotOptimize(buffer.cdata());
      }
    });
  }

  // New buffer per operation with three appends of 0..128 bytes: 0-3 growths, like a key built
  // per row.
  {
    const auto sizes = GenerateSizes(0, 128);
    RunGrowthBenchmark("fresh_key", 5000000 / kScale, [&](size_t num_ops) {
      for (size_t i = 0; i < num_ops; ++i) {
        ByteBuffer<64> buffer;
        for (size_t j = 0; j < 3; ++j) {
          buffer.Append(src + j * 256, sizes[(i * 3 + j) % kNumSizes]);
        }
        DoNotOptimize(buffer.cdata());
      }
    });
  }

  // New buffer per operation with one Reserve of 65 bytes..64KB: a single growth by 1-10
  // doublings.
  {
    const auto sizes = GenerateSizes(65, 65536, /* log_uniform= */ true);
    RunGrowthBenchmark("fresh_reserve", 5000000 / kScale, [&](size_t num_ops) {
      for (size_t i = 0; i < num_ops; ++i) {
        ByteBuffer<64> buffer;
        buffer.Reserve(sizes[i % kNumSizes]);
        DoNotOptimize(buffer.cdata());
      }
    });
  }

  // New small buffer grown byte by byte to 1KB: 7 growths by a single doubling.
  {
    RunGrowthBenchmark("pushback_1k", 500000 / kScale, [&](size_t num_ops) {
      for (size_t i = 0; i < num_ops; ++i) {
        ByteBuffer<8> buffer;
        for (size_t j = 0; j < 1024; ++j) {
          buffer.PushBack(src[(i + j) % kSourceSize]);
        }
        DoNotOptimize(buffer.cdata());
      }
    });
  }
}

} // namespace yb
