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

#include "yb/client/atomic_request_id_allocator.h"

#include <atomic>
#include <functional>
#include <queue>
#include <vector>

#include "yb/util/flags.h"
#include "yb/util/lockfree.h"
#include "yb/util/logging.h"

DEFINE_RUNTIME_uint64(client_request_id_fold_batch, 128,
    "Max number of finished request ids that a thread folds into min_running_request_id in one "
    "Finish call, so that it does not stay the folder of its shard while its own requests wait. "
    "The ids left over are folded by the Finish call of a request that is still running. Set it "
    "high enough and a thread always folds everything it finds.");

namespace yb::client::internal {

// The atomics use the default, sequentially consistent memory order: weakening them was measured
// to make no difference on x86_64.
class AtomicRequestIdAllocatorImpl {
 public:
  ~AtomicRequestIdAllocatorImpl() {
    DCHECK(!running_.load());
    finished_queue_.Drain();
  }

  AtomicRequestIdAllocation Next() {
    auto id = next_id_.fetch_add(1);
    // min_running only moves past finished ids, so it stays below the id allocated above.
    return AtomicRequestIdAllocation { .id = id, .min_running = min_running_id_.load() };
  }

  void Finish(RetryableRequestId id) {
    // tcmalloc keeps the freed entries in a per thread cache, which beats recycling them through
    // a stack shared by all the threads.
    auto* entry = new FinishedRequest;
    entry->id = id;
    finished_queue_.Push(entry);
    finished_count_.fetch_add(1);

    auto expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
      // Another thread is folding the queued ids, it will also fold the entry pushed above.
      return;
    }
    Process();
  }

  RetryableRequestId min_running() const {
    return min_running_id_.load();
  }

 private:
  struct FinishedRequest : public MPSCQueueEntry<FinishedRequest> {
    RetryableRequestId id;
  };

  void Process() {
    const auto fold_batch = FLAGS_client_request_id_fold_batch;
    for (;;) {
      uint64_t folded = 0;
      auto limited = false;
      while (auto* entry = finished_queue_.Pop()) {
        ProcessFinished(entry->id);
        delete entry;
        if (++folded >= fold_batch) {
          // Enough for one Finish call, so that this thread gets back to its own requests.
          limited = true;
          break;
        }
      }
      // As in PreparerImpl::Run, the check below must not be reordered before this store, or an
      // entry pushed by a Finish() that saw running_ as true is left behind. It also has to come
      // after the store: the requests that it finds running could be the ones whose Finish calls
      // have just failed to take the role.
      if (PREDICT_TRUE(running_.exchange(false))) {
        // Whether this thread has to come back for the ids left in the queue. After the batch
        // limit it leaves them to the Finish call of a request that is still running, and there is
        // none when every allocated id has finished. Without the limit the queue was drained, so
        // only what was pushed since matters, which is what Empty() sees.
        if (limited ? next_id_.load() == finished_count_.load() : !finished_queue_.Empty()) {
          auto expected = false;
          if (running_.compare_exchange_strong(expected, true)) {
            continue;
          }
          // Somebody else took the role and folds the rest.
        }
      } else {
        LOG(DFATAL) << "running_ is false while a thread is folding finished request ids";
      }
      return;
    }
  }

  void ProcessFinished(RetryableRequestId id) {
    auto min_running = min_running_id_.load();
    if (id != min_running) {
      if (PREDICT_FALSE(id < min_running)) {
        LOG(DFATAL) << "Finished request id " << id << " is below min running id " << min_running;
        return;
      }
      // Finished out of order, so min_running stays below it until the ids before it finish.
      processed_queue_.push(id);
      return;
    }
    ++min_running;
    while (!processed_queue_.empty() && processed_queue_.top() <= min_running) {
      // A second copy of a finished id. Left in the queue, it would never match min_running
      // again and would stall it for good.
      if (PREDICT_FALSE(processed_queue_.top() < min_running)) {
        LOG(DFATAL) << "Request id " << processed_queue_.top() << " finished twice";
      } else {
        ++min_running;
      }
      processed_queue_.pop();
    }
    min_running_id_.store(min_running);
  }

  std::atomic<RetryableRequestId> next_id_{0};
  std::atomic<RetryableRequestId> min_running_id_{0};

  // Finished requests, to tell whether any request is still running.
  std::atomic<RetryableRequestId> finished_count_{0};

  MPSCQueue<FinishedRequest> finished_queue_;

  // Whether a thread is folding the queued ids.
  std::atomic<bool> running_{false};

  // Finished ids above min_running_id_, touched by the folding thread only, which running_
  // serializes.
  std::priority_queue<
      RetryableRequestId, std::vector<RetryableRequestId>, std::greater<RetryableRequestId>>
          processed_queue_;
};

AtomicRequestIdAllocator::AtomicRequestIdAllocator()
    : impl_(std::make_unique<AtomicRequestIdAllocatorImpl>()) {
}

AtomicRequestIdAllocator::~AtomicRequestIdAllocator() = default;

AtomicRequestIdAllocation AtomicRequestIdAllocator::Next() {
  return impl_->Next();
}

void AtomicRequestIdAllocator::Finish(RetryableRequestId id) {
  impl_->Finish(id);
}

RetryableRequestId AtomicRequestIdAllocator::TEST_min_running() const {
  return impl_->min_running();
}

} // namespace yb::client::internal
