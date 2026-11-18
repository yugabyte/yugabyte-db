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

#include "yb/vector_index/vector_lsm_deletions.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <mutex>

#include "yb/gutil/endian.h"

#include "yb/util/atomic.h"
#include "yb/util/shared_lock.h"
#include "yb/util/tostring.h"

namespace yb::vector_index {

namespace {

// The order of VectorId, without the separate equality test its operator<=> runs on x86.
bool VectorIdBytesLess(const VectorId& lhs, const VectorId& rhs) {
  return std::memcmp(lhs.data(), rhs.data(), kUuidSize) < 0;
}

} // namespace

std::string VectorLSMDeletion::ToString() const {
  return YB_STRUCT_TO_STRING(vector_id, hybrid_time);
}

size_t VectorLSMDeletionMap::VectorIdHash::operator()(const VectorId& vector_id) const {
  // The table mixes the hash, so folding the id is enough.
  return LittleEndian::Load64(vector_id.data()) ^ LittleEndian::Load64(vector_id.data() + 8);
}

VectorLSMDeletionMap::VectorLSMDeletionMap(MemTrackerPtr mem_tracker) {
  if (mem_tracker) {
    consumption_.Init(std::move(mem_tracker), sizeof(*this));
  }
}

auto& VectorLSMDeletionMap::ShardFor(this auto& self, const VectorId& vector_id) {
  // Multiplicative hashing spreads ids that are not random too.
  constexpr uint64_t kMultiplier = 0x9E3779B97F4A7C15ULL;
  constexpr int kShardBits = std::countr_zero(kNumShards);
  return self.shards_[(VectorIdHash()(vector_id) * kMultiplier) >> (64 - kShardBits)];
}

bool VectorLSMDeletionMap::Shard::Insert(const VectorLSMDeletion& deletion, int64_t& allocated) {
  std::lock_guard lock(mutex);
  const auto old_allocated_bytes = allocated_bytes;
  // A rehash runs under the lock, but tables double, so it is rare and copies one shard.
  if (!deletions.emplace(deletion.vector_id, deletion.hybrid_time).second) {
    return false;
  }
  allocated += allocated_bytes - old_allocated_bytes;
  return true;
}

HybridTime VectorLSMDeletionMap::Shard::Find(const VectorId& vector_id) const {
  SharedLock lock(mutex);
  auto it = deletions.find(vector_id);
  return it != deletions.end() ? it->second : HybridTime::kInvalid;
}

void VectorLSMDeletionMap::Insert(std::span<const VectorLSMDeletion> deletions) {
  size_t num_added = 0;
  int64_t allocated = 0;
  auto min_hybrid_time = HybridTime::kInvalid;
  for (const auto& deletion : deletions) {
    DCHECK(deletion.hybrid_time.is_valid()) << deletion.ToString();
    if (ShardFor(deletion.vector_id).Insert(deletion, allocated)) {
      ++num_added;
      min_hybrid_time.MakeAtMost(deletion.hybrid_time);
    }
  }
  if (!num_added) {
    return;
  }
  size_.fetch_add(num_added, std::memory_order_acq_rel);
  UpdateAtomicMin(&min_hybrid_time_, min_hybrid_time);
  if (allocated) {
    const auto total = allocated_bytes_.fetch_add(allocated, std::memory_order_acq_rel) + allocated;
    consumption_.UpdateMonotonic(sizeof(*this) + total);
  }
}

HybridTime VectorLSMDeletionMap::Find(const VectorId& vector_id) const {
  return ShardFor(vector_id).Find(vector_id);
}

VectorLSMDeletionList VectorLSMDeletionMap::SortedList() const {
  const auto size = this->size();
  VectorLSMDeletionList result;
  result.reserve(size);
  for (const auto& shard : shards_) {
    SharedLock lock(shard.mutex);
    for (const auto& [vector_id, hybrid_time] : shard.deletions) {
      result.push_back({.vector_id = vector_id, .hybrid_time = hybrid_time});
    }
  }
  // Inserts while listing would leave deletions out of the list.
  DCHECK_EQ(result.size(), size);
  DCHECK_EQ(this->size(), size);
  std::ranges::sort(result, VectorIdBytesLess, &VectorLSMDeletion::vector_id);
  return result;
}

}  // namespace yb::vector_index
