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

#include "yb/client/request_id_allocator.h"

#include <mutex>
#include <set>
#include <span>
#include <vector>

#include "yb/client/atomic_request_id_allocator.h"
#include "yb/client/bitmap_request_id_allocator.h"
#include "yb/client/block_request_id_allocator.h"
#include "yb/client/counter_request_id_allocator.h"
#include "yb/client/retryable_request_tracker.h"
#include "yb/client/sharded_request_id_allocator.h"

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"

DEFINE_NON_RUNTIME_string(client_request_id_allocator, "sharded-queue",
    "Allocator of the retryable request ids of a client. spinlock: a set of the running ids under "
    "a spinlock. id-blocks: per thread blocks of ids, https://github.com/yugabyte/yugabyte-db/"
    "pull/33246. striped: a set per stripe, each under its own lock, https://github.com/yugabyte/"
    "yugabyte-db/pull/33244. queue, bitmap: lock free, with the finished ids folded from a queue "
    "or a bitmap. sharded-queue, sharded-bitmap, sharded-counters: a lock free allocator per "
    "shard, each with its own client id, see client_request_id_shards.");
DEFINE_validator(client_request_id_allocator,
    FLAG_IN_SET_VALIDATOR("spinlock", "id-blocks", "striped", "queue", "bitmap", "sharded-queue",
                          "sharded-bitmap", "sharded-counters"));

namespace yb::client::internal {

namespace {

// The allocator that all of them replace: a set of the running ids under a spinlock.
class SpinlockRequestIdAllocator : public RequestIdAllocator {
 public:
  explicit SpinlockRequestIdAllocator(const ClientId& client_id) : client_id_(client_id) {}

  RequestIdAllocation Next() override {
    std::lock_guard lock(mutex_);
    auto id = next_id_++;
    running_.insert(id);
    return RequestIdAllocation {
      .id = id,
      .min_running = *running_.begin(),
      .client_id = &client_id_,
      .allocator = this,
      .state = nullptr,
    };
  }

  void Finish(const RequestIdAllocation& allocation) override {
    std::lock_guard lock(mutex_);
    if (!running_.erase(allocation.id)) {
      LOG(DFATAL) << "Finished an unknown request: " << allocation.id;
    }
  }

 private:
  const ClientId client_id_;
  simple_spinlock mutex_;
  RetryableRequestId next_id_ GUARDED_BY(mutex_) = 0;
  std::set<RetryableRequestId> running_ GUARDED_BY(mutex_);
};

class BlockRequestIdAllocatorAdapter : public RequestIdAllocator {
 public:
  explicit BlockRequestIdAllocatorAdapter(const ClientId& client_id) : client_id_(client_id) {}

  RequestIdAllocation Next() override {
    auto allocation = allocator_.Next();
    return RequestIdAllocation {
      .id = allocation.id,
      .min_running = allocation.min_running,
      .client_id = &client_id_,
      .allocator = this,
      .state = std::move(allocation.block),
    };
  }

  void Finish(const RequestIdAllocation& allocation) override {
    BlockRequestIdAllocator::Finished(std::static_pointer_cast<RequestIdBlock>(allocation.state));
  }

 private:
  const ClientId client_id_;
  BlockRequestIdAllocator allocator_;
};

class StripedRequestIdAllocator : public RequestIdAllocator {
 public:
  explicit StripedRequestIdAllocator(const ClientId& client_id) : client_id_(client_id) {}

  RequestIdAllocation Next() override {
    // The registration is move only and holds a list iterator, so it lives on the heap.
    auto registration = std::make_shared<Registration>(tracker_.Register());
    return RequestIdAllocation {
      .id = registration->request_id(),
      .min_running = registration->min_running_request_id(),
      .client_id = &client_id_,
      .allocator = this,
      .state = std::move(registration),
    };
  }

  void Finish(const RequestIdAllocation& allocation) override {
    auto* registration = static_cast<Registration*>(allocation.state.get());
    tracker_.Unregister(std::span(&registration, 1));
  }

 private:
  using Registration = RetryableRequestTracker::Registration;

  const ClientId client_id_;
  RetryableRequestTracker tracker_;
};

// An allocator with a single id space: the client id is the one given, or a generated one for a
// shard.
template <class Impl>
class SingleSpaceRequestIdAllocator : public RequestIdAllocator {
 public:
  SingleSpaceRequestIdAllocator() : client_id_(ClientId::GenerateRandom()) {}
  explicit SingleSpaceRequestIdAllocator(const ClientId& client_id) : client_id_(client_id) {}

  RequestIdAllocation Next() override {
    auto allocation = impl_.Next();
    return RequestIdAllocation {
      .id = allocation.id,
      .min_running = allocation.min_running,
      .client_id = &client_id_,
      .allocator = this,
      .state = nullptr,
    };
  }

  void Finish(const RequestIdAllocation& allocation) override {
    impl_.Finish(allocation.id);
  }

 private:
  const ClientId client_id_;
  Impl impl_;
};

// A shard per group of threads, each with its own client id, see ShardedRequestIdAllocator.
template <class Impl>
class ShardedRequestIdAllocatorAdapter : public RequestIdAllocator {
 public:
  ShardedRequestIdAllocatorAdapter() {
    auto num_shards = NumRequestIdShards();
    shards_.reserve(num_shards);
    for (size_t i = 0; i != num_shards; ++i) {
      shards_.push_back(std::make_unique<SingleSpaceRequestIdAllocator<Impl>>());
    }
  }

  RequestIdAllocation Next() override {
    return shards_[RequestIdThreadIndex() % shards_.size()]->Next();
  }

  void Finish(const RequestIdAllocation& allocation) override {
    LOG(DFATAL) << "The shard of the allocation finishes it";
  }

 private:
  std::vector<std::unique_ptr<SingleSpaceRequestIdAllocator<Impl>>> shards_;
};

} // namespace

std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator(const ClientId& client_id) {
  return CreateRequestIdAllocator(FLAGS_client_request_id_allocator, client_id);
}

std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator(
    const std::string& name, const ClientId& client_id) {
  if (name == "spinlock") {
    return std::make_unique<SpinlockRequestIdAllocator>(client_id);
  }
  if (name == "id-blocks") {
    return std::make_unique<BlockRequestIdAllocatorAdapter>(client_id);
  }
  if (name == "striped") {
    return std::make_unique<StripedRequestIdAllocator>(client_id);
  }
  if (name == "queue") {
    return std::make_unique<SingleSpaceRequestIdAllocator<AtomicRequestIdAllocator>>(client_id);
  }
  if (name == "bitmap") {
    return std::make_unique<SingleSpaceRequestIdAllocator<BitmapRequestIdAllocator>>(client_id);
  }
  if (name == "sharded-queue") {
    return std::make_unique<ShardedRequestIdAllocatorAdapter<AtomicRequestIdAllocator>>();
  }
  if (name == "sharded-bitmap") {
    return std::make_unique<ShardedRequestIdAllocatorAdapter<BitmapRequestIdAllocator>>();
  }
  if (name == "sharded-counters") {
    return std::make_unique<ShardedRequestIdAllocatorAdapter<CounterRequestIdAllocator>>();
  }
  LOG(FATAL) << "Unknown request id allocator: " << name;
}

} // namespace yb::client::internal
