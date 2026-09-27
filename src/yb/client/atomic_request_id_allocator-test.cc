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
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <unordered_set>
#include <vector>

#include "yb/client/atomic_request_id_allocator.h"
#include "yb/client/bitmap_request_id_allocator.h"
#include "yb/client/counter_request_id_allocator.h"
#include "yb/client/block_request_id_allocator.h"
#include "yb/client/request_id_allocator.h"
#include "yb/client/retryable_request_tracker.h"
#include "yb/client/sharded_request_id_allocator.h"

#include "yb/gutil/strings/split.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_uint32(client_request_id_num_words);
DECLARE_uint32(client_request_id_shards);

DEFINE_NON_RUNTIME_string(request_id_benchmark_threads, "1,4,8,16,32,64",
    "Comma separated list of thread counts to run the request id allocator benchmark with.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_requests_per_thread, 100000,
    "Number of requests allocated and finished by each benchmark thread.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_outstanding, 8,
    "Number of requests that each benchmark thread keeps allocated but not finished.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_work_cache_lines, 0,
    "Number of cache lines that a benchmark thread dirties per request, so that the request "
    "evicts the state that the allocator keeps hot, like a real one does.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_work_buffer_kb, 4096,
    "Size of the per thread buffer that request_id_benchmark_work_cache_lines walks.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_stall_threads, 0,
    "Number of benchmark threads that stall while holding their requests, like a request that "
    "waits for an RPC or rides out a leader election.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_stall_ms, 0,
    "How long a stalling thread holds its requests.");

DEFINE_NON_RUNTIME_int32(request_id_benchmark_stall_period, 50000,
    "Requests between the stalls of a stalling thread.");

DEFINE_NON_RUNTIME_string(request_id_benchmark_impls, "",
    "Comma separated implementations that the benchmark runs, empty for all of them: id-blocks, "
    "queue, bitmap, sharded-queue, sharded-bitmap, sharded-counters, striped, spinlock.");

DEFINE_NON_RUNTIME_bool(request_id_benchmark_reverse, false,
    "Whether the benchmark runs the implementations in the reverse order, to tell their own "
    "difference from the effect of the position in the run.");

DEFINE_NON_RUNTIME_int32(request_id_invariant_threads, 8,
    "Number of threads of the min_running invariant tests.");

DEFINE_NON_RUNTIME_int32(request_id_invariant_requests_per_thread, 20000,
    "Number of requests that each thread of the min_running invariant tests allocates.");

DEFINE_NON_RUNTIME_bool(request_id_benchmark_cross_thread_finish, false,
    "Whether a request is finished by the next benchmark thread instead of the one that allocated "
    "it, the way the reactor thread that completes the last RPC of a batcher finishes the "
    "requests of the thread that allocated them. The ids that wait for the other thread are not "
    "counted in the exact distance.");

DEFINE_NON_RUNTIME_int64(request_id_benchmark_work_ns, 0,
    "Time that a benchmark thread spends on each request outside of the allocator, simulating "
    "the work that a real write request does. Zero measures the allocator alone.");

namespace yb::client::internal {

namespace {

// Ids per word of the finished bitmap, i.e. what min_running moves by.
constexpr int kBitsPerWord = 64;

} // namespace

class AtomicRequestIdAllocatorTest : public YBTest {
};

TEST_F(AtomicRequestIdAllocatorTest, Sequential) {
  constexpr int kNumRequests = 2 * kBitsPerWord;

  AtomicRequestIdAllocator allocator;
  for (int i = 0; i != kNumRequests; ++i) {
    auto allocation = allocator.Next();
    ASSERT_EQ(allocation.id, i);
    ASSERT_LE(allocation.min_running, allocation.id);
    allocator.Finish(allocation.id);
    // min_running is exact, so it follows every id finished in order.
    ASSERT_EQ(allocator.TEST_min_running(), i + 1);
  }
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

TEST_F(AtomicRequestIdAllocatorTest, ReverseOrder) {
  constexpr int kNumRequests = 2 * kBitsPerWord;

  AtomicRequestIdAllocator allocator;
  std::vector<RetryableRequestId> ids;
  for (int i = 0; i != kNumRequests; ++i) {
    ids.push_back(allocator.Next().id);
  }

  // The first window stays incomplete until its first id finishes, so min_running does not move
  // and then jumps over both windows.
  for (size_t i = ids.size(); i > 1;) {
    allocator.Finish(ids[--i]);
    ASSERT_EQ(allocator.TEST_min_running(), 0);
  }
  allocator.Finish(ids.front());
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

// A request that is still running should hold min_running back, even when newer requests finish.
TEST_F(AtomicRequestIdAllocatorTest, RunningRequestHoldsMin) {
  constexpr int kNumRequests = 2 * kBitsPerWord;

  AtomicRequestIdAllocator allocator;
  auto running = allocator.Next();
  for (int i = 1; i != kNumRequests; ++i) {
    auto allocation = allocator.Next();
    ASSERT_LE(allocation.min_running, running.id);
    allocator.Finish(allocation.id);
  }
  ASSERT_EQ(allocator.TEST_min_running(), running.id);

  allocator.Finish(running.id);
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

// The bits that do not fit into the ring are taken into account when min_running reaches them.
TEST_F(AtomicRequestIdAllocatorTest, Overflow) {
  constexpr int kNumWords = 64;
  constexpr int kNumRequests = 32 * kNumWords * kBitsPerWord;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_num_words) = kNumWords;
  BitmapRequestIdAllocator allocator;

  auto running = allocator.Next();
  for (int i = 1; i != kNumRequests; ++i) {
    allocator.Finish(allocator.Next().id);
  }
  ASSERT_GT(allocator.TEST_num_overflows(), 0);
  ASSERT_EQ(allocator.TEST_min_running(), running.id);

  allocator.Finish(running.id);
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

// min_running is the exact minimum of the running ids, not the start of the word that holds it.
TEST_F(AtomicRequestIdAllocatorTest, ExactMinRunning) {
  constexpr int kRunningId = 5;
  constexpr int kNumRequests = 2 * kBitsPerWord;

  AtomicRequestIdAllocator allocator;
  std::vector<RetryableRequestId> ids;
  for (int i = 0; i != kNumRequests; ++i) {
    ids.push_back(allocator.Next().id);
  }

  for (int i = 0; i != kNumRequests; ++i) {
    if (i != kRunningId) {
      allocator.Finish(ids[i]);
    }
  }
  // min_running stops at the running id in the middle of the first word.
  ASSERT_EQ(allocator.TEST_min_running(), kRunningId);

  allocator.Finish(ids[kRunningId]);
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

TEST_F(AtomicRequestIdAllocatorTest, Concurrent) {
  constexpr int kNumThreads = 8;
  constexpr int kNumRequestsPerThread = 10000;
  constexpr int kTotalRequests = kNumThreads * kNumRequestsPerThread;
  static_assert(kTotalRequests % kBitsPerWord == 0);

  AtomicRequestIdAllocator allocator;

  // An id must never be below a min_running advertised before its Next() call started, the
  // server would have rejected it as expired.
  std::atomic<RetryableRequestId> max_advertised_min{0};

  std::mutex all_ids_mutex;
  std::unordered_set<RetryableRequestId> all_ids;

  TestThreadHolder threads;
  for (int i = 0; i != kNumThreads; ++i) {
    threads.AddThreadFunctor([&allocator, &max_advertised_min, &all_ids_mutex, &all_ids, i] {
      std::mt19937_64 rng(i);
      std::vector<RetryableRequestId> ids;
      std::vector<RetryableRequestId> running;
      for (int j = 0; j != kNumRequestsPerThread; ++j) {
        auto min_before = max_advertised_min.load(std::memory_order_acquire);
        auto allocation = allocator.Next();
        ASSERT_GE(allocation.id, min_before);
        ASSERT_LE(allocation.min_running, allocation.id);

        auto advertised = max_advertised_min.load(std::memory_order_acquire);
        while (advertised < allocation.min_running &&
               !max_advertised_min.compare_exchange_weak(advertised, allocation.min_running)) {}

        ids.push_back(allocation.id);
        running.push_back(allocation.id);
        // Finish a random running request about half of the time, so that ids finish out of order.
        if (rng() % 2 == 0) {
          auto index = rng() % running.size();
          allocator.Finish(running[index]);
          running[index] = running.back();
          running.pop_back();
        }
      }
      for (auto id : running) {
        allocator.Finish(id);
      }
      std::lock_guard lock(all_ids_mutex);
      for (auto id : ids) {
        ASSERT_TRUE(all_ids.insert(id).second) << "Duplicate id: " << id;
      }
    });
  }
  threads.JoinAll();

  ASSERT_EQ(all_ids.size(), kTotalRequests);
  // Nothing is left unfolded, even though the threads stopped finishing requests.
  ASSERT_EQ(allocator.TEST_min_running(), kTotalRequests);
}

// ------------------------------------------------------------------------------------------------
// Performance comparison of the request id allocator implementations.

namespace {

// One per cache line, so that publishing it does not add false sharing to what is measured.
struct alignas(64) PaddedId {
  std::atomic<RetryableRequestId> value{std::numeric_limits<RetryableRequestId>::max()};
};

// Dirties the given number of cache lines of the buffer, so that the request evicts the state
// that the allocator keeps hot.
void TouchCacheLines(std::vector<uint64_t>* buffer, size_t num_lines, uint64_t* rng_state) {
  constexpr size_t kWordsPerLine = 64 / sizeof(uint64_t);
  auto num_lines_in_buffer = buffer->size() / kWordsPerLine;
  for (size_t i = 0; i != num_lines; ++i) {
    *rng_state = *rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    ++(*buffer)[(*rng_state % num_lines_in_buffer) * kWordsPerLine];
  }
}

// Simulates the work that a real request does between its allocation and its completion.
void SimulateWork(int64_t work_ns) {
  if (work_ns <= 0) {
    return;
  }
  auto deadline = MonoTime::Now() + MonoDelta::FromNanoseconds(work_ns);
  while (MonoTime::Now() < deadline) {}
}

// Hands finished requests from the thread that allocated them to the one that finishes them.
// Single producer, single consumer.
template <class Handle>
class HandoffRing {
 public:
  explicit HandoffRing(size_t capacity) : slots_(capacity) {}

  // Waits for a free slot, since the consumer only stops draining after Close().
  void Push(Handle&& handle) {
    auto tail = tail_.load(std::memory_order_relaxed);
    while (tail - head_.load(std::memory_order_acquire) == slots_.size()) {
      std::this_thread::yield();
    }
    slots_[tail % slots_.size()].emplace(std::move(handle));
    tail_.store(tail + 1, std::memory_order_release);
  }

  std::optional<Handle> Pop() {
    auto head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    auto& slot = slots_[head % slots_.size()];
    std::optional<Handle> result(std::move(*slot));
    slot.reset();
    head_.store(head + 1, std::memory_order_release);
    return result;
  }

  void Close() {
    closed_.store(true, std::memory_order_release);
  }

  bool closed() const {
    return closed_.load(std::memory_order_acquire);
  }

 private:
  std::vector<std::optional<Handle>> slots_;
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};
  std::atomic<bool> closed_{false};
};

template <class HandleType>
struct BenchmarkAllocation {
  HandleType handle;

  RetryableRequestId id;

  // Distance between the allocated id and the min_running_id advertised with it, i.e. how much
  // retryable request state the server has to keep because of this allocation.
  RetryableRequestId gap;

  // Ids and min_running of different shards are unrelated, since a shard has its own client id.
  const void* shard = nullptr;
};

// Adapts the block based allocator of https://github.com/yugabyte/yugabyte-db/pull/33246.
class BlockAllocatorAdapter {
 public:
  using Handle = RequestIdBlockPtr;
  using Allocation = BenchmarkAllocation<Handle>;

  Allocation Next() {
    auto allocation = allocator_.Next();
    return {
      .handle = std::move(allocation.block),
      .id = allocation.id,
      .gap = allocation.id - allocation.min_running,
    };
  }

  void Finish(const Handle& block) { BlockRequestIdAllocator::Finished(block); }

  void Drain() {}

  void LogStats() {}

 private:
  BlockRequestIdAllocator allocator_;
};

// The per window counters, kept for comparison with the bitmap.
class CounterAllocatorAdapter {
 public:
  using Handle = RetryableRequestId;
  using Allocation = BenchmarkAllocation<Handle>;

  Allocation Next() {
    auto allocation = allocator_.Next();
    return {
      .handle = allocation.id,
      .id = allocation.id,
      .gap = allocation.id - allocation.min_running,
    };
  }

  void Finish(Handle id) { allocator_.Finish(id); }

  void Drain() {}

  void LogStats() {
    LOG(INFO) << "  window counter overflows: " << allocator_.TEST_num_overflows();
  }

 private:
  CounterRequestIdAllocator allocator_;
};

class AtomicAllocatorAdapter {
 public:
  using Handle = RetryableRequestId;
  using Allocation = BenchmarkAllocation<Handle>;

  Allocation Next() {
    auto allocation = allocator_.Next();
    return {
      .handle = allocation.id,
      .id = allocation.id,
      .gap = allocation.id - allocation.min_running,
    };
  }

  void Finish(Handle id) { allocator_.Finish(id); }

  void Drain() {}

  void LogStats() {}

 private:
  AtomicRequestIdAllocator allocator_;
};

// The bitmap of the finished ids, instead of the queue.
class BitmapAllocatorAdapter {
 public:
  using Handle = RetryableRequestId;
  using Allocation = BenchmarkAllocation<Handle>;

  Allocation Next() {
    auto allocation = allocator_.Next();
    return {
      .handle = allocation.id,
      .id = allocation.id,
      .gap = allocation.id - allocation.min_running,
    };
  }

  void Finish(Handle id) { allocator_.Finish(id); }

  void Drain() {}

  void LogStats() {
    LOG(INFO) << "  bitmap overflows: " << allocator_.TEST_num_overflows();
  }

 private:
  BitmapRequestIdAllocator allocator_;
};

// A shard per group of threads, each shard with its own client id.
template <class Allocator>
class ShardedAllocatorAdapter {
 public:
  // The shard finishes the request, and a real caller also sends its client id with it.
  struct Handle {
    Allocator* shard;
    RetryableRequestId id;
  };
  using Allocation = BenchmarkAllocation<Handle>;

  Allocation Next() {
    auto allocation = allocator_.Next();
    return {
      .handle = Handle { .shard = allocation.shard, .id = allocation.id },
      .id = allocation.id,
      .gap = allocation.id - allocation.min_running,
      .shard = allocation.shard,
    };
  }

  void Finish(const Handle& handle) { handle.shard->Finish(handle.id); }

  void Drain() {}

  void LogStats() {
    LOG(INFO) << "  shards: " << allocator_.num_shards();
  }

 private:
  ShardedRequestIdAllocator<Allocator> allocator_;
};

// The striped tracker of https://github.com/yugabyte/yugabyte-db/pull/33244: a lock per stripe,
// one id space, and the min over the stripes as min_running.
class StripedTrackerAdapter {
 public:
  using Handle = RetryableRequestTracker::Registration;
  using Allocation = BenchmarkAllocation<Handle>;

  Allocation Next() {
    auto registration = tracker_.Register();
    auto id = registration.request_id();
    auto min_running = registration.min_running_request_id();
    return {
      .handle = std::move(registration),
      .id = id,
      .gap = id - min_running,
    };
  }

  void Finish(Handle& registration) {
    auto* ptr = &registration;
    tracker_.Unregister(std::span(&ptr, 1));
  }

  void Drain() {}

  void LogStats() {
    LOG(INFO) << "  stripes: " << tracker_.TEST_StripeCount();
  }

 private:
  RetryableRequestTracker tracker_;
};

// Any allocator through the production interface, for the ones that only exist behind it.
class InterfaceAdapter {
 public:
  using Handle = RequestIdAllocation;
  using Allocation = BenchmarkAllocation<Handle>;

  explicit InterfaceAdapter(const std::string& name)
      : allocator_(CreateRequestIdAllocator(name, ClientId::GenerateRandom())) {}

  Allocation Next() {
    auto allocation = allocator_->Next();
    auto id = allocation.id;
    auto gap = id - allocation.min_running;
    // The ids of different client ids are unrelated.
    const void* shard = allocation.client_id;
    return {
      .handle = std::move(allocation),
      .id = id,
      .gap = gap,
      .shard = shard,
    };
  }

  void Finish(const Handle& allocation) { allocation.allocator->Finish(allocation); }

  void Drain() {}

  void LogStats() {}

 private:
  std::unique_ptr<RequestIdAllocator> allocator_;
};

class SpinlockAdapter : public InterfaceAdapter {
 public:
  SpinlockAdapter() : InterfaceAdapter("spinlock") {}
};

} // namespace

// The invariant the server relies on: min_running never exceeds the id of a request that is
// still running, otherwise a retry of that request is rejected as expired. Next() runs under the
// mutex, so that it is atomic with the update of the running set, while Finish() does not.
// Removing an id from the set before finishing it only makes the check weaker, never wrong.
template <class Allocator>
void CheckMinRunningInvariant() {
  const auto num_threads = FLAGS_request_id_invariant_threads;
  const auto requests_per_thread = FLAGS_request_id_invariant_requests_per_thread;
  constexpr size_t kOutstanding = 8;

  Allocator allocator;
  std::mutex mutex;
  // Per shard, since the ids of different shards are independent.
  std::unordered_map<const void*, std::set<RetryableRequestId>> running;

  TestThreadHolder threads;
  for (int i = 0; i != num_threads; ++i) {
    threads.AddThreadFunctor([&allocator, &mutex, &running, requests_per_thread] {
      std::deque<std::pair<typename Allocator::Handle, std::pair<const void*, RetryableRequestId>>>
          outstanding;
      for (int j = 0; j != requests_per_thread; ++j) {
        {
          std::lock_guard lock(mutex);
          auto allocation = allocator.Next();
          auto& shard_running = running[allocation.shard];
          shard_running.insert(allocation.id);
          ASSERT_LE(allocation.id - allocation.gap, *shard_running.begin())
              << "min_running is above a running id of shard " << allocation.shard;
          outstanding.emplace_back(
              std::move(allocation.handle), std::make_pair(allocation.shard, allocation.id));
        }
        if (outstanding.size() > kOutstanding) {
          auto entry = std::move(outstanding.front());
          outstanding.pop_front();
          {
            std::lock_guard lock(mutex);
            running[entry.second.first].erase(entry.second.second);
          }
          allocator.Finish(entry.first);
        }
      }
      for (auto& entry : outstanding) {
        {
          std::lock_guard lock(mutex);
          running[entry.second.first].erase(entry.second.second);
        }
        allocator.Finish(entry.first);
      }
    });
  }
  threads.JoinAll();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantBlocks) {
  CheckMinRunningInvariant<BlockAllocatorAdapter>();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantCounters) {
  CheckMinRunningInvariant<CounterAllocatorAdapter>();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantQueue) {
  CheckMinRunningInvariant<AtomicAllocatorAdapter>();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantBitmap) {
  CheckMinRunningInvariant<BitmapAllocatorAdapter>();
}

// Each shard has its own client id, so the invariant is per shard: the ids of a shard are only
// compared to the min_running of that shard.
TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantSharded) {
  CheckMinRunningInvariant<ShardedAllocatorAdapter<AtomicRequestIdAllocator>>();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantShardedBitmap) {
  CheckMinRunningInvariant<ShardedAllocatorAdapter<BitmapRequestIdAllocator>>();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantStriped) {
  CheckMinRunningInvariant<StripedTrackerAdapter>();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantSpinlock) {
  CheckMinRunningInvariant<SpinlockAdapter>();
}

class RequestIdAllocatorBenchmark : public YBTest {
 protected:
  using Impls = std::vector<std::pair<std::string, std::function<void(size_t)>>>;

  template <class Adapter>
  void Add(const std::string& name, Impls* impls) {
    impls->emplace_back(name, [this, name](size_t num_threads) {
      Adapter adapter;
      Run(name, num_threads, &adapter);
    });
  }

  Result<std::vector<size_t>> ThreadCounts() {
    std::vector<size_t> result;
    for (const auto& part : SplitStringUsing(FLAGS_request_id_benchmark_threads, ",")) {
      result.push_back(VERIFY_RESULT(CheckedStoull(part)));
    }
    SCHECK(!result.empty(), InvalidArgument, "No thread counts specified");
    return result;
  }

  template <class Allocator>
  void Run(const std::string& name, size_t num_threads, Allocator* allocator) {
    const auto requests_per_thread = FLAGS_request_id_benchmark_requests_per_thread;
    const size_t outstanding_per_thread = FLAGS_request_id_benchmark_outstanding;
    const auto cross_thread_finish = FLAGS_request_id_benchmark_cross_thread_finish;
    const auto work_ns = FLAGS_request_id_benchmark_work_ns;
    const size_t work_cache_lines = FLAGS_request_id_benchmark_work_cache_lines;
    const size_t buffer_words = FLAGS_request_id_benchmark_work_buffer_kb * 1024 / sizeof(uint64_t);
    const size_t stall_threads = FLAGS_request_id_benchmark_stall_threads;
    const auto stall_ms = FLAGS_request_id_benchmark_stall_ms;
    const auto stall_period = FLAGS_request_id_benchmark_stall_period;

    std::atomic<bool> start{false};
    std::atomic<size_t> ready{0};
    std::atomic<int64_t> total_gap{0};
    std::atomic<RetryableRequestId> max_gap{0};
    // Oldest unfinished id of each thread, to compare the advertised min_running to the exact
    // one.
    std::vector<PaddedId> oldest_ids(num_threads);
    std::atomic<int64_t> total_exact_gap{0};
    std::atomic<int64_t> num_exact_samples{0};
    std::atomic<double> min_thread_seconds{std::numeric_limits<double>::max()};
    std::atomic<double> max_thread_seconds{0};
    // Thread i hands its finished requests to thread i + 1, which finishes them.
    std::vector<std::unique_ptr<HandoffRing<typename Allocator::Handle>>> handoffs;
    if (cross_thread_finish) {
      for (size_t i = 0; i != num_threads; ++i) {
        handoffs.push_back(std::make_unique<HandoffRing<typename Allocator::Handle>>(
            std::max<size_t>(16, 4 * outstanding_per_thread)));
      }
    }

    TestThreadHolder threads;
    for (size_t i = 0; i != num_threads; ++i) {
      threads.AddThreadFunctor(
          [allocator, &start, &ready, &total_gap, &max_gap, &oldest_ids, &total_exact_gap,
           &num_exact_samples, &min_thread_seconds, &max_thread_seconds, &handoffs,
           requests_per_thread, outstanding_per_thread, cross_thread_finish, work_ns,
           work_cache_lines, buffer_words, stall_threads, stall_ms, stall_period, i,
           num_threads] {
        auto* outbox = cross_thread_finish ? handoffs[i].get() : nullptr;
        auto* inbox = cross_thread_finish ? handoffs[(i + num_threads - 1) % num_threads].get()
                                          : nullptr;
        auto finish = [allocator, outbox, inbox](typename Allocator::Handle& handle) {
          if (!outbox) {
            allocator->Finish(handle);
            return;
          }
          outbox->Push(std::move(handle));
          while (auto handed = inbox->Pop()) {
            allocator->Finish(*handed);
          }
        };
        std::vector<uint64_t> buffer(work_cache_lines ? buffer_words : 0);
        uint64_t rng_state = i + 1;
        std::vector<RetryableRequestId> ids(outstanding_per_thread);
        std::vector<typename Allocator::Handle> outstanding;
        outstanding.reserve(outstanding_per_thread);
        for (size_t j = 0; j != outstanding_per_thread; ++j) {
          auto allocation = allocator->Next();
          ids[j] = allocation.id;
          outstanding.push_back(std::move(allocation.handle));
        }
        oldest_ids[i].value.store(ids[0], std::memory_order_relaxed);
        ready.fetch_add(1);
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        int64_t gap_sum = 0;
        RetryableRequestId gap_max = 0;
        int64_t exact_gap_sum = 0;
        int64_t num_samples = 0;
        auto thread_started_at = MonoTime::Now();
        // A fixed number of requests in flight, finished in the allocation order.
        for (int j = 0; j != requests_per_thread; ++j) {
          auto index = j % outstanding_per_thread;
          finish(outstanding[index]);
          auto allocation = allocator->Next();
          outstanding[index] = std::move(allocation.handle);
          ids[index] = allocation.id;
          oldest_ids[i].value.store(ids[(index + 1) % outstanding_per_thread],
                                    std::memory_order_relaxed);
          gap_sum += allocation.gap;
          gap_max = std::max(gap_max, allocation.gap);
          // Sampled by one thread, it reads the state of all the others.
          if (i == 0 && index == 0 && j % 1024 == 0) {
            auto exact_min = allocation.id;
            for (size_t k = 0; k != num_threads; ++k) {
              exact_min = std::min(exact_min, oldest_ids[k].value.load(std::memory_order_relaxed));
            }
            exact_gap_sum += allocation.id - exact_min;
            ++num_samples;
          }
          if (work_cache_lines) {
            TouchCacheLines(&buffer, work_cache_lines, &rng_state);
          }
          SimulateWork(work_ns);
          // Holds the outstanding requests while stalling, which is what pins min_running.
          if (stall_ms && static_cast<size_t>(i) < stall_threads &&
              (j + 1) % stall_period == 0) {
            SleepFor(MonoDelta::FromMilliseconds(stall_ms));
          }
        }
        auto thread_seconds = (MonoTime::Now() - thread_started_at).ToSeconds();
        for (auto& handle : outstanding) {
          finish(handle);
        }
        if (outbox) {
          outbox->Close();
          // The previous thread could still be handing over, so drain until it is done. What
          // it pushed before closing is visible once closed() is.
          while (!inbox->closed()) {
            while (auto handed = inbox->Pop()) {
              allocator->Finish(*handed);
            }
            std::this_thread::yield();
          }
          while (auto handed = inbox->Pop()) {
            allocator->Finish(*handed);
          }
        }
        oldest_ids[i].value.store(
            std::numeric_limits<RetryableRequestId>::max(), std::memory_order_relaxed);
        total_gap.fetch_add(gap_sum);
        total_exact_gap.fetch_add(exact_gap_sum);
        num_exact_samples.fetch_add(num_samples);
        auto current_max = max_gap.load();
        while (current_max < gap_max && !max_gap.compare_exchange_weak(current_max, gap_max)) {}
        auto current_min_seconds = min_thread_seconds.load();
        while (current_min_seconds > thread_seconds &&
               !min_thread_seconds.compare_exchange_weak(current_min_seconds, thread_seconds)) {}
        auto current_max_seconds = max_thread_seconds.load();
        while (current_max_seconds < thread_seconds &&
               !max_thread_seconds.compare_exchange_weak(current_max_seconds, thread_seconds)) {}
      });
    }

    while (ready.load() != num_threads) {
      std::this_thread::yield();
    }
    auto started_at = MonoTime::Now();
    start.store(true, std::memory_order_release);
    threads.JoinAll();
    auto requests_done_at = MonoTime::Now();
    allocator->Drain();
    auto drained_at = MonoTime::Now();

    auto num_requests = num_threads * requests_per_thread;
    auto elapsed = (requests_done_at - started_at).ToSeconds();
    // Per request time of one thread. What is left after the simulated work is the allocator,
    // waiting included.
    auto ns_per_request = elapsed * 1e9 * num_threads / num_requests;
    auto num_samples = std::max<int64_t>(1, num_exact_samples.load());
    LOG(INFO) << Format(
        "$0, $1 thread(s): $2 requests/sec, $3 ns/request in allocator, "
        "id - min_running avg $4 max $5, exact avg $6, thread time $7..$8 sec, drain $9 sec",
        name, num_threads, static_cast<int64_t>(num_requests / elapsed),
        static_cast<int64_t>(ns_per_request - work_ns),
        total_gap.load() / num_requests, max_gap.load(), total_exact_gap.load() / num_samples,
        min_thread_seconds.load(), max_thread_seconds.load(),
        (drained_at - requests_done_at).ToSeconds());
    allocator->LogStats();
  }
};

TEST_F(RequestIdAllocatorBenchmark, Compare) {
  LOG(INFO) << "Hardware concurrency: " << std::thread::hardware_concurrency();

  std::vector<std::pair<std::string, std::function<void(size_t)>>> impls;
  Add<BlockAllocatorAdapter>("id-blocks", &impls);
  Add<AtomicAllocatorAdapter>("queue", &impls);
  Add<BitmapAllocatorAdapter>("bitmap", &impls);
  Add<ShardedAllocatorAdapter<AtomicRequestIdAllocator>>("sharded-queue", &impls);
  Add<ShardedAllocatorAdapter<BitmapRequestIdAllocator>>("sharded-bitmap", &impls);
  Add<ShardedAllocatorAdapter<CounterRequestIdAllocator>>("sharded-counters", &impls);
  Add<StripedTrackerAdapter>("striped", &impls);
  Add<SpinlockAdapter>("spinlock", &impls);

  std::unordered_set<std::string> enabled;
  for (const auto& name : SplitStringUsing(FLAGS_request_id_benchmark_impls, ",")) {
    enabled.insert(name);
  }
  if (!enabled.empty()) {
    std::erase_if(impls, [&enabled](const auto& impl) { return !enabled.contains(impl.first); });
    ASSERT_EQ(impls.size(), enabled.size()) << "Unknown implementation requested";
  }
  if (FLAGS_request_id_benchmark_reverse) {
    std::reverse(impls.begin(), impls.end());
  }

  for (auto num_threads : ASSERT_RESULT(ThreadCounts())) {
    for (const auto& impl : impls) {
      impl.second(num_threads);
    }
  }
}

} // namespace yb::client::internal
