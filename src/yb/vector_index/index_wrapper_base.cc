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

#include "yb/vector_index/index_wrapper_base.h"

#include "yb/rocksdb/cache.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"

DEFINE_RUNTIME_bool(vector_index_enable_block_cache_reservation, true,
    "Whether a usearch/hnswlib vector index chunk reserves space in the block cache for its "
    "in-memory footprint when it is created. Reserving lowers the cache's effective capacity so it "
    "evicts other blocks instead of letting total memory grow. Disable to restore the prior "
    "behavior where the index footprint is purely additive on top of the block cache.");

namespace yb::vector_index {

Result<BlockCacheReservation> BlockCacheReservation::Create(
    rocksdb::Cache* cache, size_t bytes, rocksdb::Cache::ReservationMode reservation_mode) {
  if (cache == nullptr || bytes == 0 || !FLAGS_vector_index_enable_block_cache_reservation) {
    return BlockCacheReservation{};
  }
  // TODO(vector_index): it may make sense to add a metric for capacity violations here.
  const bool within_capacity = VERIFY_RESULT(cache->ConsumeSpace(bytes, reservation_mode));
  if (!within_capacity) {
    YB_LOG_EVERY_N_SECS(WARNING, 60)
        << "Block cache reservation of " << bytes << " bytes exceeds cache capacity";
  }
  return BlockCacheReservation{ cache, bytes };
}

BlockCacheReservation::BlockCacheReservation(BlockCacheReservation&& rhs) noexcept
    : cache_(rhs.cache_), bytes_(rhs.bytes_) {
  rhs.cache_ = nullptr;
  rhs.bytes_ = 0;
}

BlockCacheReservation& BlockCacheReservation::operator=(BlockCacheReservation&& rhs) noexcept {
  if (this != &rhs) {
    Reset();
    cache_ = rhs.cache_;
    bytes_ = rhs.bytes_;
    rhs.cache_ = nullptr;
    rhs.bytes_ = 0;
  }
  return *this;
}

BlockCacheReservation::~BlockCacheReservation() {
  Reset();
}

void BlockCacheReservation::Reset() {
  if (cache_ == nullptr) {
    return;
  }
  cache_->ReleaseSpace(bytes_);
  cache_ = nullptr;
  bytes_ = 0;
}

}  // namespace yb::vector_index
