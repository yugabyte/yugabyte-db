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

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"

DEFINE_RUNTIME_uint32(client_request_id_block_size, 256,
    "Number of consecutive retryable request ids a client thread reserves at a time. Larger "
    "values reduce contention on the shared request id registry, but increase how far the "
    "min_running_request_id advertised to servers may lag behind, delaying server-side cleanup "
    "of retryable request state.");

DEFINE_RUNTIME_uint32(client_request_id_block_idle_sec, 30,
    "Seal a thread's reserved block of retryable request ids after it has not been allocated "
    "from for this long, so that an idle thread does not hold back min_running_request_id.");

namespace yb {
namespace client {
namespace internal {

namespace {

// Entries cached per thread; above this, entries of destroyed allocators are purged.
constexpr size_t kThreadLocalPurgeThreshold = 32;

std::atomic<uint64_t> instance_id_seq{0};

} // namespace

// A contiguous range of ids [floor, floor + size) owned by a single allocating thread.
// next_offset_ is bumped only by the owner thread; finished_ is bumped by whichever threads
// complete the requests. Once sealed, no new ids are handed out; the block is retired when
// every allocated id has finished.
class RequestIdBlock {
 public:
  RequestIdBlock(
      std::weak_ptr<RequestIdAllocatorImpl> impl, RetryableRequestId floor, uint32_t size,
      CoarseTimePoint now)
      : impl_(std::move(impl)), floor_(floor), size_(size),
        last_alloc_nanos_(now.time_since_epoch().count()) {}

  RetryableRequestId floor() const { return floor_; }

  // Returns an allocated id, or nullopt if the block is exhausted or sealed.
  // Must be called by the owner thread only.
  std::optional<RetryableRequestId> TryAllocate() {
    // The fetch_add and the sealed_ load below are both sequentially consistent, pairing with
    // the sequentially consistent store in Seal(): for any concurrent seal, either we observe
    // sealed_ == true here and discard the slot, or the sealer's subsequent reads observe our
    // increment of next_offset_ and the block stays active until the matching MarkFinished().
    auto offset = next_offset_.fetch_add(1);
    if (offset >= size_) {
      // Exhausted. This overshoot is not compensated; Quiescent() clamps next_offset_ to size_.
      // Only the owner bumps next_offset_, and the owner drops the block after this, so the
      // clamp discards at most this one overshoot.
      Seal();
      return std::nullopt;
    }
    if (sealed_.load()) {
      // Sealed concurrently by the idle sweep. The slot at `offset` was still counted by the
      // sealer, so return it as immediately finished.
      MarkFinished();
      return std::nullopt;
    }
    last_alloc_nanos_.store(
        CoarseMonoClock::Now().time_since_epoch().count(), std::memory_order_relaxed);
    return floor_ + offset;
  }

  void Seal() {
    sealed_.store(true);
  }

  bool sealed() const {
    return sealed_.load();
  }

  void MarkFinished() {
    finished_.fetch_add(1);
  }

  // Whether the block can be retired: sealed, and every allocated id has finished.
  // Every increment of next_offset_ (clamped to size_) is eventually matched by exactly one
  // increment of finished_ - either the real completion of the request, or the discard
  // compensation in TryAllocate - so equality means no id from this block is still running.
  bool Quiescent() const {
    if (!sealed_.load()) {
      return false;
    }
    auto allocated = std::min<uint64_t>(next_offset_.load(), size_);
    return finished_.load() == allocated;
  }

  CoarseTimePoint last_alloc() const {
    return CoarseTimePoint(CoarseMonoClock::Duration(
        last_alloc_nanos_.load(std::memory_order_relaxed)));
  }

  const std::weak_ptr<RequestIdAllocatorImpl>& impl() const { return impl_; }

 private:
  const std::weak_ptr<RequestIdAllocatorImpl> impl_;
  const RetryableRequestId floor_;
  const uint32_t size_;
  std::atomic<uint64_t> next_offset_{0};
  std::atomic<uint64_t> finished_{0};
  std::atomic<bool> sealed_{false};
  std::atomic<int64_t> last_alloc_nanos_;
};

class RequestIdAllocatorImpl {
 public:
  RequestIdBlockPtr AllocateBlock(const std::shared_ptr<RequestIdAllocatorImpl>& self) {
    auto size = std::max<uint32_t>(1, FLAGS_client_request_id_block_size);
    std::lock_guard lock(registry_lock_);
    auto now = CoarseMonoClock::Now();
    if (now >= last_sweep_ + kSweepInterval) {
      last_sweep_ = now;
      SweepUnlocked(now);
    }
    auto floor = id_seq_.fetch_add(size);
    auto block = std::make_shared<RequestIdBlock>(self, floor, size, now);
    active_blocks_.emplace(floor, block);
    return block;
  }

  // Removes the block from the registry if it is quiescent, and advances the cached min.
  void RetireIfQuiescent(RequestIdBlock* block) {
    if (!block->Quiescent()) {
      return;
    }
    std::lock_guard lock(registry_lock_);
    auto it = active_blocks_.find(block->floor());
    if (it == active_blocks_.end() || it->second.get() != block) {
      return;
    }
    active_blocks_.erase(it);
    RecomputeMinUnlocked();
  }

  RetryableRequestId min_running() const {
    return cached_min_.load(std::memory_order_acquire);
  }

  size_t num_active_blocks() const {
    std::lock_guard lock(registry_lock_);
    return active_blocks_.size();
  }

  void Sweep() {
    std::lock_guard lock(registry_lock_);
    SweepUnlocked(CoarseMonoClock::Now());
  }

 private:
  static constexpr auto kSweepInterval = std::chrono::seconds(1);

  // Seals blocks that have not allocated for FLAGS_client_request_id_block_idle_sec, retires
  // quiescent blocks, and advances the cached min.
  void SweepUnlocked(CoarseTimePoint now) REQUIRES(registry_lock_) {
    const auto idle_cutoff =
        now - std::chrono::seconds(FLAGS_client_request_id_block_idle_sec);
    for (auto it = active_blocks_.begin(); it != active_blocks_.end();) {
      auto& block = *it->second;
      if (!block.sealed() && block.last_alloc() < idle_cutoff) {
        block.Seal();
      }
      if (block.Quiescent()) {
        it = active_blocks_.erase(it);
      } else {
        ++it;
      }
    }
    RecomputeMinUnlocked();
  }

  void RecomputeMinUnlocked() REQUIRES(registry_lock_) {
    // With no active blocks there are no running requests and any future id is >= id_seq_.
    // Otherwise no running or future id is below the smallest active floor. Both are computed
    // under registry_lock_, which also serializes id_seq_ advancement, and the cached min only
    // ratchets forward.
    auto new_min = active_blocks_.empty() ? id_seq_.load() : active_blocks_.begin()->first;
    if (new_min > cached_min_.load(std::memory_order_acquire)) {
      cached_min_.store(new_min, std::memory_order_release);
    }
  }

  std::atomic<RetryableRequestId> id_seq_{0};
  std::atomic<RetryableRequestId> cached_min_{0};

  mutable simple_spinlock registry_lock_;
  // Active blocks by floor. Touched once per block lifecycle, not per request.
  std::map<RetryableRequestId, RequestIdBlockPtr> active_blocks_ GUARDED_BY(registry_lock_);
  CoarseTimePoint last_sweep_ GUARDED_BY(registry_lock_) = CoarseTimePoint::min();
};

namespace {

// Per-thread current block, per allocator instance. Keyed by a never-reused instance id so a
// destroyed allocator's entry cannot be confused with a new allocator at the same address.
RequestIdBlockPtr& ThreadLocalSlot(uint64_t instance_id) {
  thread_local std::unordered_map<uint64_t, RequestIdBlockPtr> slots;
  if (slots.size() >= kThreadLocalPurgeThreshold) {
    for (auto it = slots.begin(); it != slots.end();) {
      if (it->first != instance_id && (!it->second || it->second->impl().expired())) {
        it = slots.erase(it);
      } else {
        ++it;
      }
    }
  }
  return slots[instance_id];
}

} // namespace

RequestIdAllocator::RequestIdAllocator()
    : instance_id_(instance_id_seq.fetch_add(1)),
      impl_(std::make_shared<RequestIdAllocatorImpl>()) {}

RequestIdAllocator::~RequestIdAllocator() = default;

RequestIdAllocation RequestIdAllocator::Next() {
  auto& slot = ThreadLocalSlot(instance_id_);
  for (;;) {
    if (slot) {
      auto id = slot->TryAllocate();
      if (id) {
        // min_running is loaded while our unsealed block is registered, so it cannot exceed
        // our block's floor and therefore cannot exceed *id.
        return RequestIdAllocation{*id, impl_->min_running(), slot};
      }
      impl_->RetireIfQuiescent(slot.get());
      slot.reset();
    }
    slot = impl_->AllocateBlock(impl_);
  }
}

void RequestIdAllocator::Finished(const RequestIdBlockPtr& block) {
  block->MarkFinished();
  auto impl = block->impl().lock();
  if (impl) {
    impl->RetireIfQuiescent(block.get());
  }
}

RetryableRequestId RequestIdAllocator::TEST_min_running() const {
  return impl_->min_running();
}

size_t RequestIdAllocator::TEST_num_active_blocks() const {
  return impl_->num_active_blocks();
}

void RequestIdAllocator::TEST_Sweep() {
  impl_->Sweep();
}

} // namespace internal
} // namespace client
} // namespace yb
