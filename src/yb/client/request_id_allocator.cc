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

#include <algorithm>
#include <mutex>
#include <set>
#include <vector>

#include "yb/client/atomic_request_id_allocator.h"

#include "yb/util/cgroups.h"
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/thread.h"

DEFINE_NON_RUNTIME_string(client_request_id_allocator, "sharded-queue",
    "Allocator of the retryable request ids of a client. spinlock: the set of the running ids "
    "under a spinlock that the others replace. queue: lock free, with the finished ids folded "
    "from a queue. sharded-queue: a queue per shard, each with its own client id, see "
    "client_request_id_shards.");
DEFINE_validator(client_request_id_allocator,
    FLAG_IN_SET_VALIDATOR("spinlock", "queue", "sharded-queue"));

DEFINE_RUNTIME_uint32(client_request_id_shards, 0,
    "Number of shards that the retryable request ids of a client are split into. Each shard has "
    "its own client id and its own min_running_request_id, so more shards mean less contention "
    "between the threads and a smaller blast radius of a request that stays unfinished, but also "
    "more per client state on the server. Zero picks an eighth of the CPUs, at least 1 and at "
    "most 64, which keeps the threads that share a shard well below the number where it "
    "degrades.");

namespace yb::client::internal {

namespace {

// A shard reaches full throughput while up to 16 threads finish requests through it, and degrades
// beyond that, so an eighth of the CPUs leaves margin for the clients whose threads outnumber
// them. The cap bounds the per client state that the server keeps for the shards.
size_t NumRequestIdShards() {
  if (FLAGS_client_request_id_shards) {
    return FLAGS_client_request_id_shards;
  }
  return std::clamp<size_t>(NumEffectiveCPUs() / 8, 1, 64);
}

// The allocator that the others replace: a set of the running ids under a spinlock.
class SpinlockRequestIdAllocator : public RequestIdAllocator {
 public:
  const ClientId& client_id() const override {
    return client_id_;
  }

  RequestIdAllocation Next() override {
    std::lock_guard lock(mutex_);
    auto id = next_id_++;
    running_.insert(id);
    return RequestIdAllocation {
      .id = id,
      .min_running = *running_.begin(),
      .client_id = &client_id_,
      .allocator = this,
    };
  }

  void Finish(RetryableRequestId id) override {
    std::lock_guard lock(mutex_);
    if (!running_.erase(id)) {
      LOG(DFATAL) << "Finished an unknown request: " << id;
    }
  }

 private:
  const ClientId client_id_ = ClientId::GenerateRandom();
  simple_spinlock mutex_;
  RetryableRequestId next_id_ GUARDED_BY(mutex_) = 0;
  std::set<RetryableRequestId> running_ GUARDED_BY(mutex_);
};

// A queue per shard, each with its own client id. The shards share no state: the ids of a shard
// are dense and independent, and a request that stays unfinished holds back the min_running of
// its own shard only, instead of the one that the whole client reports.
//
// Each thread walks the shards round-robin from a thread local counter, so the pick is not a
// contended atomic. A thread bound to a shard would be cheaper, but a shard whose threads stopped
// writing to a tablet would never send it a newer min_running, and the server would keep that
// shard's state until it expires. Round-robin reaches every shard within a few writes to the
// tablet, so the state of every client id is trimmed at the usual rate.
//
// The price is paid by the server, which keeps the deduplication state per client id, see
// consensus/retryable_requests.cc. It tracks the same number of requests either way, but the per
// client part of that state is multiplied by the number of shards, per tablet that the client
// writes to. So the shard count is a trade between that and the contention between the threads,
// and it does not have to match the number of threads.
class ShardedRequestIdAllocator : public RequestIdAllocator {
 public:
  ShardedRequestIdAllocator() {
    auto num_shards = NumRequestIdShards();
    shards_.reserve(num_shards);
    for (size_t i = 0; i != num_shards; ++i) {
      shards_.push_back(std::make_unique<AtomicRequestIdAllocator>());
    }
  }

  const ClientId& client_id() const override {
    return shards_.front()->client_id();
  }

  // The request is finished through the shard of the allocation.
  RequestIdAllocation Next() override {
    // Seeded by the thread id, so that threads in lockstep start on different shards.
    thread_local size_t next_shard = Thread::CurrentThreadId() % shards_.size();
    if (next_shard >= shards_.size()) {
      next_shard = 0;
    }
    return shards_[next_shard++]->Next();
  }

  void Finish(RetryableRequestId id) override {
    LOG(DFATAL) << "The shard of the allocation finishes it: " << id;
  }

 private:
  std::vector<std::unique_ptr<AtomicRequestIdAllocator>> shards_;
};

} // namespace

std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator() {
  return CreateRequestIdAllocator(FLAGS_client_request_id_allocator);
}

std::unique_ptr<RequestIdAllocator> CreateRequestIdAllocator(const std::string& name) {
  if (name == "spinlock") {
    return std::make_unique<SpinlockRequestIdAllocator>();
  }
  if (name == "queue") {
    return std::make_unique<AtomicRequestIdAllocator>();
  }
  if (name == "sharded-queue") {
    return std::make_unique<ShardedRequestIdAllocator>();
  }
  LOG(FATAL) << "Unknown request id allocator: " << name;
}

} // namespace yb::client::internal
