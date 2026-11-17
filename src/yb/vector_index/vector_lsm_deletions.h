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

#include <array>
#include <atomic>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include "yb/common/hybrid_time.h"

#include "yb/gutil/port.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"

#include "yb/vector_index/vector_index_fwd.h"

namespace yb::vector_index {

// A vector deleted at hybrid_time: it is not visible at hybrid_time and later.
struct VectorLSMDeletion {
  VectorId vector_id;
  HybridTime hybrid_time;

  std::string ToString() const;
};

using VectorLSMDeletionList = std::vector<VectorLSMDeletion>;

// The order of VectorId, without the separate equality test its operator<=> runs on x86.
inline bool VectorIdBytesLess(const VectorId& lhs, const VectorId& rhs) {
  return std::memcmp(lhs.data(), rhs.data(), kUuidSize) < 0;
}

// Vector deletions kept in memory. The vector id picks one of kNumShards shards, each a flat hash
// map behind its own lock, so concurrent lookups and inserts rarely contend.
class VectorLSMDeletionMap {
 public:
  static constexpr size_t kNumShards = 256;

  // mem_tracker, when set, tracks the memory of the map.
  explicit VectorLSMDeletionMap(MemTrackerPtr mem_tracker);

  VectorLSMDeletionMap(const VectorLSMDeletionMap&) = delete;
  void operator=(const VectorLSMDeletionMap&) = delete;

  // A vector is deleted only once, so a deletion of a vector already in the map is ignored.
  void Insert(std::span<const VectorLSMDeletion> deletions);

  // Returns the hybrid time vector_id was deleted at, invalid when it was not deleted.
  HybridTime Find(const VectorId& vector_id) const;

  // Returns the deletions sorted by VectorIdBytesLess. Must not run concurrently with Insert.
  VectorLSMDeletionList List() const;

  size_t size() const {
    return size_.load(std::memory_order_acquire);
  }

  // Returns the earliest deletion time, invalid when empty.
  HybridTime MinHybridTime() const {
    return HybridTime(min_hybrid_time_.load(std::memory_order_acquire));
  }

 private:
  struct VectorIdHash {
    size_t operator()(const VectorId& vector_id) const;
  };

  // Counts the bytes of a shard under its lock, so the tracker is updated after it is released.
  using Allocator = STLCountingAllocator<std::pair<const VectorId, HybridTime>>;
  using Map = boost::unordered_flat_map<
      VectorId, HybridTime, VectorIdHash, std::equal_to<VectorId>, Allocator>;

  struct alignas(CACHELINE_SIZE) Shard {
    Shard() : deletions(Allocator(&allocated_bytes)) {}

    // Returns false when the vector of deletion is in the shard already. Adds the bytes the shard
    // allocated for it to allocated.
    bool Insert(const VectorLSMDeletion& deletion, int64_t& allocated);

    // See VectorLSMDeletionMap::Find.
    HybridTime Find(const VectorId& vector_id) const;

    mutable rw_spinlock mutex;
    int64_t allocated_bytes GUARDED_BY(mutex) = 0;
    Map deletions GUARDED_BY(mutex);
  };

  const Shard& ShardFor(const VectorId& vector_id) const;
  Shard& ShardFor(const VectorId& vector_id);

  std::array<Shard, kNumShards> shards_;
  std::atomic<size_t> size_{0};
  std::atomic<HybridTimeRepr> min_hybrid_time_{HybridTime::kInvalid.ToUint64()};
  std::atomic<int64_t> allocated_bytes_{0};
  // The memory of the map and the shards. The map never shrinks.
  MonotonicThreadSafeScopedTrackedConsumption consumption_;
};

}  // namespace yb::vector_index
