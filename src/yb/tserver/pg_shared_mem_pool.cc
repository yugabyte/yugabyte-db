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

#include "yb/tserver/pg_shared_mem_pool.h"

#include <boost/interprocess/mapped_region.hpp>
#include <boost/intrusive/list.hpp>

#include "yb/gutil/bits.h"

#include "yb/rpc/poller.h"

#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/flags.h"
#include "yb/util/shared_mem.h"
#include "yb/util/size_literals.h"

using namespace std::literals;
using namespace yb::size_literals;

DEFINE_RUNTIME_uint64(max_big_shared_memory_segment_size, 1_MB,
    "Max size of big shared memory segment in bytes.");

DEFINE_RUNTIME_uint64(big_shared_memory_segment_expiration_time_ms, 5000,
    "Time to release unused allocated big memory segment.");

DEFINE_NON_RUNTIME_uint64(big_shared_memory_allocated_limit, 128_MB,
    "The limit for all allocated big shared memory segments in bytes.");

namespace yb::tserver {

namespace {

class AllocatedSegment : public boost::intrusive::list_base_hook<> {
 public:
  explicit AllocatedSegment(uint64_t id)
      : id_(id) {
  }

  AllocatedSegment(AllocatedSegment&& rhs) = default;

  Status Init(const std::string& instance_id, size_t size) {
    shared_memory_object_ = VERIFY_RESULT(InterprocessSharedMemoryObject::Create(
        MakeSharedMemoryBigSegmentName(instance_id, id_), size));
    mapped_region_ = VERIFY_RESULT(shared_memory_object_.Map());
    return Status::OK();
  }

  ~AllocatedSegment() {
    shared_memory_object_.DestroyAndRemove();
  }

  uint64_t id() const {
    return id_;
  }

  std::byte* address() const {
    return static_cast<std::byte*>(mapped_region_.get_address());
  }

  size_t size() const {
    return mapped_region_.get_size();
  }

  void Freed() {
    last_access_ = CoarseMonoClock::Now();
  }

  CoarseTimePoint last_access() const {
    return last_access_;
  }

 private:
  const uint64_t id_;
  InterprocessSharedMemoryObject shared_memory_object_;
  InterprocessMappedRegion mapped_region_;
  CoarseTimePoint last_access_;
};

} // namespace

void SharedMemorySegmentHandle::Reset() {
  if (!holder_) {
    return;
  }
  holder_->Freed(id_);
  holder_ = nullptr;
}

const std::string PgSharedMemoryPool::kAllocatedMemTrackerId = "Allocated Big Shared Memory";
const std::string PgSharedMemoryPool::kAvailableMemTrackerId = "Available Big Shared Memory";

class PgSharedMemoryPool::Impl : public SharedMemorySegmentHolder {
 public:
  // Allocate at least 64kb.
  static constexpr size_t kIndexBase = 16;

  Impl(const MemTrackerPtr& parent_mem_tracker, const std::string& instance_id)
      : instance_id_(instance_id),
        allocated_mem_tracker_(
            MemTracker::FindOrCreateTracker(FLAGS_big_shared_memory_allocated_limit,
            kAllocatedMemTrackerId, parent_mem_tracker)),
        available_mem_tracker_(
            MemTracker::FindOrCreateTracker(kAvailableMemTrackerId, allocated_mem_tracker_,
            AddToParent::kFalse)),
        poller_("PgSharedMemoryPool: ", std::bind(&Impl::Cleanup, this)) {}

  SharedMemorySegmentHandle Obtain(size_t size) {
    if (size > FLAGS_max_big_shared_memory_segment_size) {
      return {};
    }
    auto index = std::max<size_t>(kIndexBase, Bits::Log2Ceiling64(size)) - kIndexBase;
    {
      std::lock_guard lock(mutex_);
      if (available_segments_.size() > index && !available_segments_[index].empty()) {
        SharedMemorySegmentHandle result(*this, available_segments_[index].back());
        available_mem_tracker_->Release(result.size());
        available_segments_[index].pop_back();
        return result;
      }
    }
    auto segment_size = 1ULL << (index + kIndexBase);
    if (!allocated_mem_tracker_->TryConsume(segment_size)) {
      return {};
    }
    auto id = ++id_serial_no_;
    AllocatedSegment segment(id);
    auto status = segment.Init(instance_id_, segment_size);
    if (!status.ok()) {
      LOG(WARNING) << status;
      return {};
    }
    SharedMemorySegmentHandle result(*this, segment);
    {
      std::lock_guard lock(mutex_);
      allocated_segments_.emplace(id, std::move(segment));
    }
    return result;
  }

  void Cleanup() {
    auto expiration_bound =
        CoarseMonoClock::Now() - FLAGS_big_shared_memory_segment_expiration_time_ms * 1ms;
    std::lock_guard lock(mutex_);
    for (auto& list : available_segments_) {
      while (!list.empty() && list.front().last_access() < expiration_bound) {
        auto& segment = list.front();
        available_mem_tracker_->Release(segment.size());
        list.pop_front();
        allocated_mem_tracker_->Release(segment.size());
        allocated_segments_.erase(segment.id());
      }
    }
  }

  void Freed(uint64_t id) override {
    std::lock_guard lock(mutex_);
    auto it = allocated_segments_.find(id);
    if (it == allocated_segments_.end()) {
      LOG(DFATAL) << "Freed unknown segment: " << id;
      return;
    }
    size_t index = Bits::Log2FloorNonZero64(it->second.size()) - kIndexBase;
    available_segments_.resize(std::max(available_segments_.size(), index + 1));
    it->second.Freed();
    available_segments_[index].push_back(it->second);
    available_mem_tracker_->Consume(it->second.size());
  }

  void Start(rpc::Scheduler& scheduler) {
    poller_.Start(&scheduler, FLAGS_big_shared_memory_segment_expiration_time_ms * 1ms);
  }

 private:
  const std::string instance_id_;
  std::atomic<size_t> id_serial_no_ = 0;
  std::mutex mutex_;
  std::unordered_map<uint64_t, AllocatedSegment> allocated_segments_ GUARDED_BY(mutex_);
  std::vector<boost::intrusive::list<AllocatedSegment>> available_segments_ GUARDED_BY(mutex_);
  MemTrackerPtr allocated_mem_tracker_;
  MemTrackerPtr available_mem_tracker_;
  rpc::Poller poller_;
};

PgSharedMemoryPool::PgSharedMemoryPool(
    const MemTrackerPtr& parent_mem_tracker, const std::string& instance_id)
    : impl_(new Impl(parent_mem_tracker, instance_id)) {}

PgSharedMemoryPool::~PgSharedMemoryPool() = default;

SharedMemorySegmentHandle PgSharedMemoryPool::Obtain(size_t size) {
  return impl_->Obtain(size);
}

void PgSharedMemoryPool::Start(rpc::Scheduler& scheduler) {
  impl_->Start(scheduler);
}

void PgSharedMemoryPool::Freed(uint64_t id) {
  impl_->Freed(id);
}

}  // namespace yb::tserver
