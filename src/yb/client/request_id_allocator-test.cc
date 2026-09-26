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
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

#include "yb/client/request_id_allocator.h"

#include "yb/util/flags.h"
#include "yb/util/test_util.h"

DECLARE_uint32(client_request_id_block_size);
DECLARE_uint32(client_request_id_block_idle_sec);

namespace yb {
namespace client {
namespace internal {

class RequestIdAllocatorTest : public YBTest {
};

TEST_F(RequestIdAllocatorTest, Basic) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_size) = 8;
  RequestIdAllocator allocator;

  std::unordered_set<RetryableRequestId> ids;
  std::vector<RequestIdAllocation> allocations;
  RetryableRequestId prev_min = 0;
  for (int i = 0; i != 100; ++i) {
    auto allocation = allocator.Next();
    ASSERT_TRUE(ids.insert(allocation.id).second) << "Duplicate id: " << allocation.id;
    ASSERT_LE(allocation.min_running, allocation.id);
    ASSERT_GE(allocation.min_running, prev_min) << "min_running went backwards";
    prev_min = allocation.min_running;
    allocations.push_back(std::move(allocation));
  }

  for (const auto& allocation : allocations) {
    RequestIdAllocator::Finished(allocation.block);
  }
  allocations.clear();

  // After all requests finished and idle blocks sealed, min advances past every issued id.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_idle_sec) = 0;
  SleepFor(MonoDelta::FromMilliseconds(50));
  allocator.TEST_Sweep();
  ASSERT_EQ(allocator.TEST_num_active_blocks(), 0);
  for (auto id : ids) {
    ASSERT_GT(allocator.TEST_min_running(), id);
  }
}

TEST_F(RequestIdAllocatorTest, RetireOnBlockExhaustion) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_size) = 4;
  RequestIdAllocator allocator;

  // Consume and finish two full blocks; exhausted blocks retire without the idle sweep.
  for (int i = 0; i != 9; ++i) {
    auto allocation = allocator.Next();
    RequestIdAllocator::Finished(allocation.block);
  }
  // Only the current (third) block may still be active.
  ASSERT_LE(allocator.TEST_num_active_blocks(), 1);
  ASSERT_GE(allocator.TEST_min_running(), 8);
}

TEST_F(RequestIdAllocatorTest, IdleBlockDoesNotPinMinForever) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_size) = 1024;
  RequestIdAllocator allocator;

  // A thread allocates once from a big block and goes idle.
  auto idle_allocation = allocator.Next();
  RequestIdAllocator::Finished(idle_allocation.block);
  ASSERT_EQ(allocator.TEST_min_running(), 0);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_idle_sec) = 0;
  SleepFor(MonoDelta::FromMilliseconds(50));
  allocator.TEST_Sweep();

  // The idle block was sealed and retired; min moved past its floor.
  ASSERT_EQ(allocator.TEST_num_active_blocks(), 0);
  ASSERT_GT(allocator.TEST_min_running(), idle_allocation.id);
}

TEST_F(RequestIdAllocatorTest, SealedBlockStillTracksRunningRequests) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_size) = 1024;
  RequestIdAllocator allocator;

  auto running = allocator.Next();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_idle_sec) = 0;
  SleepFor(MonoDelta::FromMilliseconds(50));
  allocator.TEST_Sweep();

  // The block is sealed but the request is still running, so min must not pass its id.
  ASSERT_EQ(allocator.TEST_num_active_blocks(), 1);
  ASSERT_LE(allocator.TEST_min_running(), running.id);

  RequestIdAllocator::Finished(running.block);
  allocator.TEST_Sweep();
  ASSERT_EQ(allocator.TEST_num_active_blocks(), 0);
  ASSERT_GT(allocator.TEST_min_running(), running.id);
}

TEST_F(RequestIdAllocatorTest, ConcurrentHammer) {
  constexpr int kThreads = 16;
  constexpr int kIterations = 5000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_size) = 16;
  RequestIdAllocator allocator;

  // Max min_running ever advertised. The core safety invariant is that an id returned by
  // Next() is never below a min_running advertised before that Next() call started.
  std::atomic<RetryableRequestId> max_advertised_min{0};

  std::mutex all_ids_mutex;
  std::unordered_set<RetryableRequestId> all_ids;

  std::vector<std::thread> threads;
  for (int t = 0; t != kThreads; ++t) {
    threads.emplace_back([&allocator, &max_advertised_min, &all_ids_mutex, &all_ids, t] {
      std::mt19937 rng(t);
      std::vector<RequestIdAllocation> outstanding;
      std::vector<RetryableRequestId> my_ids;
      for (int i = 0; i != kIterations; ++i) {
        auto min_before = max_advertised_min.load();
        auto allocation = allocator.Next();
        ASSERT_GE(allocation.id, min_before)
            << "Issued an id below a previously advertised min_running";
        ASSERT_LE(allocation.min_running, allocation.id);
        my_ids.push_back(allocation.id);

        auto current = max_advertised_min.load();
        while (current < allocation.min_running &&
               !max_advertised_min.compare_exchange_weak(current, allocation.min_running)) {}

        outstanding.push_back(std::move(allocation));
        // Finish a random outstanding request about half the time.
        if (!outstanding.empty() && rng() % 2 == 0) {
          size_t idx = rng() % outstanding.size();
          RequestIdAllocator::Finished(outstanding[idx].block);
          outstanding[idx] = std::move(outstanding.back());
          outstanding.pop_back();
        }
      }
      for (const auto& allocation : outstanding) {
        RequestIdAllocator::Finished(allocation.block);
      }
      std::lock_guard lock(all_ids_mutex);
      for (auto id : my_ids) {
        ASSERT_TRUE(all_ids.insert(id).second) << "Duplicate id: " << id;
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  ASSERT_EQ(all_ids.size(), kThreads * kIterations);

  // All requests finished; after the idle sweep the min passes everything issued.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_request_id_block_idle_sec) = 0;
  SleepFor(MonoDelta::FromMilliseconds(50));
  allocator.TEST_Sweep();
  ASSERT_EQ(allocator.TEST_num_active_blocks(), 0);
}

} // namespace internal
} // namespace client
} // namespace yb
