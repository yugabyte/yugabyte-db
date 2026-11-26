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
#include <memory>
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
#include "yb/util/result.h"

#include "yb/vector_index/vector_index_fwd.h"

namespace yb::vector_index {

// A vector deleted at hybrid_time: it is not visible at hybrid_time and later.
struct VectorLSMDeletion {
  VectorId vector_id;
  HybridTime hybrid_time;

  std::string ToString() const;
};

using VectorLSMDeletionList = std::vector<VectorLSMDeletion>;

// The deletions kept with a vector index chunk, see VectorIndexWriterIf::InsertDeletions.
class VectorDeletionsReaderIf {
 public:
  virtual ~VectorDeletionsReaderIf() = default;

  virtual size_t NumDeletions() const {
    return 0;
  }

  // Returns the hybrid time vector_id was deleted at, invalid when it was not deleted.
  virtual Result<HybridTime> FindDeletion(const VectorId& vector_id) const {
    return HybridTime::kInvalid;
  }
};

using VectorDeletionsReaderPtr = std::shared_ptr<const VectorDeletionsReaderIf>;

// Vector deletions kept in memory. The vector id picks one of kNumShards shards, each a flat hash
// map behind its own lock, so concurrent lookups and inserts rarely contend.
class VectorLSMDeletionMap {
 public:
  static constexpr size_t kNumShards = 256;

  // The average memory of a deletion: an entry and its metadata byte, in tables that grow when 7/8
  // full, so are 7/16 to 7/8 full, about 5/8 on average.
  static constexpr size_t kAverageEntryBytes =
      (sizeof(VectorId) + sizeof(HybridTime) + 1) * 8 / 5;

  // mem_tracker, when set, tracks the memory of the map.
  explicit VectorLSMDeletionMap(MemTrackerPtr mem_tracker);

  VectorLSMDeletionMap(const VectorLSMDeletionMap&) = delete;
  void operator=(const VectorLSMDeletionMap&) = delete;

  // A vector is deleted only once, so a deletion of a vector already in the map is ignored.
  void Insert(std::span<const VectorLSMDeletion> deletions);

  // Returns the hybrid time vector_id was deleted at, invalid when it was not deleted.
  HybridTime Find(const VectorId& vector_id) const;

  // Returns the deletions sorted by vector id. Must not run concurrently with Insert.
  VectorLSMDeletionList SortedList() const;

  size_t size() const {
    return size_.load(std::memory_order_acquire);
  }

  // Returns the earliest deletion time, invalid when empty.
  HybridTime MinHybridTime() const {
    return min_hybrid_time_.load(std::memory_order_acquire);
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

  auto& ShardFor(this auto& self, const VectorId& vector_id);

  std::array<Shard, kNumShards> shards_;
  std::atomic<size_t> size_{0};
  std::atomic<HybridTime> min_hybrid_time_{HybridTime::kInvalid};
  std::atomic<int64_t> allocated_bytes_{0};
  // The memory of the map and the shards. The map never shrinks.
  MonotonicThreadSafeScopedTrackedConsumption consumption_;
};

// Finds the deletions of vectors in a set of chunks.
class VectorLSMDeletionsLookup {
 public:
  void Reserve(size_t num_chunks) {
    chunks_.reserve(num_chunks);
  }

  void Add(VectorDeletionsReaderPtr chunk) {
    chunks_.push_back(std::move(chunk));
  }

  // Returns the hybrid time vector_id was deleted at, invalid when it was not deleted. A vector is
  // deleted only once, so at most one chunk has its deletion.
  Result<HybridTime> Find(const VectorId& vector_id) const;

 private:
  std::vector<VectorDeletionsReaderPtr> chunks_;
};

}  // namespace yb::vector_index
