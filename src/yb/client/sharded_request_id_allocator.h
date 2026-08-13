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

#pragma once

#include <memory>
#include <vector>

#include "yb/client/atomic_request_id_allocator.h"

#include "yb/common/retryable_request.h"

namespace yb::client::internal {

/// Allocated id, with the shard that owns it. The shard finishes the request and provides the
/// client id to send with it, since the server deduplicates by client id and request id together.
/// It outlives the request, so the caller keeps the pointer until the request is finished.
template <class Allocator>
struct ShardedRequestIdAllocation {
  Allocator* shard;
  RetryableRequestId id;
  RetryableRequestId min_running;
};

/// Number of shards, from FLAGS_client_request_id_shards or from the number of CPUs.
size_t NumRequestIdShards();

/// Index of the calling thread, so that a thread always uses the same shard.
size_t RequestIdThreadIndex();

/// A pool of allocators, one per shard, each with its own client id. A thread always uses the same
/// shard, so the shards share no state: the ids of a shard are dense and independent, and a
/// request that stays unfinished holds back the min_running of its own shard only, instead of the
/// one that the whole client reports.
///
/// The price is paid by the server, which keeps the deduplication state per client id, see
/// consensus/retryable_requests.cc. It tracks the same number of requests either way, but the per
/// client part of that state is multiplied by the number of shards, per tablet that the client
/// writes to. So the shard count is a trade between that and the contention between the threads,
/// and it does not have to match the number of threads.
template <class Allocator>
class ShardedRequestIdAllocator {
 public:
  using Allocation = ShardedRequestIdAllocation<Allocator>;

  ShardedRequestIdAllocator() {
    auto num_shards = NumRequestIdShards();
    shards_.reserve(num_shards);
    for (size_t i = 0; i != num_shards; ++i) {
      shards_.push_back(std::make_unique<Allocator>());
    }
  }

  ShardedRequestIdAllocator(const ShardedRequestIdAllocator&) = delete;
  void operator=(const ShardedRequestIdAllocator&) = delete;

  /// Allocates an id from the shard of the calling thread. The request is finished through the
  /// shard of the allocation.
  Allocation Next() {
    auto* shard = shards_[RequestIdThreadIndex() % shards_.size()].get();
    auto allocation = shard->Next();
    return Allocation {
      .shard = shard,
      .id = allocation.id,
      .min_running = allocation.min_running,
    };
  }

  size_t num_shards() const {
    return shards_.size();
  }

 private:
  // Pointers, so that the shards keep their addresses: the allocations point at them.
  std::vector<std::unique_ptr<Allocator>> shards_;
};

using ShardedAtomicRequestIdAllocator = ShardedRequestIdAllocator<AtomicRequestIdAllocator>;
using ShardedAtomicRequestIdAllocation = ShardedRequestIdAllocation<AtomicRequestIdAllocator>;

} // namespace yb::client::internal
