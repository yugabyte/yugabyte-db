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
#include <deque>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/client/atomic_request_id_allocator.h"
#include "yb/client/request_id_allocator.h"

#include "yb/util/flags.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

DEFINE_NON_RUNTIME_int32(request_id_invariant_threads, 8,
    "Number of threads that the min_running invariant test runs with.");
DEFINE_NON_RUNTIME_int32(request_id_invariant_requests_per_thread, 20000,
    "Number of requests that each thread of the min_running invariant test allocates.");

namespace yb::client::internal {

namespace {

constexpr int kNumRequests = 128;

} // namespace

class AtomicRequestIdAllocatorTest : public YBTest {
};

TEST_F(AtomicRequestIdAllocatorTest, Sequential) {
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
  AtomicRequestIdAllocator allocator;
  std::vector<RetryableRequestId> ids;
  for (int i = 0; i != kNumRequests; ++i) {
    ids.push_back(allocator.Next().id);
  }

  // min_running does not move until the first id finishes, and then jumps over all of them.
  for (size_t i = ids.size(); i > 1;) {
    allocator.Finish(ids[--i]);
    ASSERT_EQ(allocator.TEST_min_running(), 0);
  }
  allocator.Finish(ids.front());
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

// A request that is still running should hold min_running back, even when newer requests finish.
TEST_F(AtomicRequestIdAllocatorTest, RunningRequestHoldsMin) {
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

// min_running is the exact minimum of the running ids.
TEST_F(AtomicRequestIdAllocatorTest, ExactMinRunning) {
  constexpr int kRunningId = 5;
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
  // min_running stops at the running id.
  ASSERT_EQ(allocator.TEST_min_running(), kRunningId);

  allocator.Finish(ids[kRunningId]);
  ASSERT_EQ(allocator.TEST_min_running(), kNumRequests);
}

TEST_F(AtomicRequestIdAllocatorTest, Concurrent) {
  constexpr int kNumThreads = 8;
  constexpr int kNumRequestsPerThread = 10000;
  constexpr int kTotalRequests = kNumThreads * kNumRequestsPerThread;

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

// The invariant the server relies on: min_running never exceeds the id of a request that is
// still running, otherwise a retry of that request is rejected as expired. Next() runs under the
// mutex, so that it is atomic with the update of the running set, while Finish() does not.
// Removing an id from the set before finishing it only makes the check weaker, never wrong.
// The set is per client id, since the sharded allocator has an id space per shard.
void CheckMinRunningInvariant(const std::string& name) {
  const auto num_threads = FLAGS_request_id_invariant_threads;
  const auto requests_per_thread = FLAGS_request_id_invariant_requests_per_thread;
  constexpr size_t kOutstanding = 8;

  auto allocator = CreateRequestIdAllocator(name);
  std::mutex mutex;
  std::unordered_map<const ClientId*, std::set<RetryableRequestId>> running;

  TestThreadHolder threads;
  for (int i = 0; i != num_threads; ++i) {
    threads.AddThreadFunctor([&allocator, &mutex, &running, requests_per_thread] {
      std::deque<RequestIdAllocation> outstanding;
      auto finish = [&mutex, &running](const RequestIdAllocation& allocation) {
        {
          std::lock_guard lock(mutex);
          running[allocation.client_id].erase(allocation.id);
        }
        allocation.allocator->Finish(allocation.id);
      };
      for (int j = 0; j != requests_per_thread; ++j) {
        {
          std::lock_guard lock(mutex);
          auto allocation = allocator->Next();
          auto& client_running = running[allocation.client_id];
          client_running.insert(allocation.id);
          ASSERT_LE(allocation.min_running, *client_running.begin())
              << "min_running is above a running id of client " << *allocation.client_id;
          outstanding.push_back(allocation);
        }
        if (outstanding.size() > kOutstanding) {
          finish(outstanding.front());
          outstanding.pop_front();
        }
      }
      for (const auto& allocation : outstanding) {
        finish(allocation);
      }
    });
  }
  threads.JoinAll();
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantSpinlock) {
  CheckMinRunningInvariant("spinlock");
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantQueue) {
  CheckMinRunningInvariant("queue");
}

TEST_F(AtomicRequestIdAllocatorTest, MinRunningInvariantSharded) {
  CheckMinRunningInvariant("sharded-queue");
}

} // namespace yb::client::internal
