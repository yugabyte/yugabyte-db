// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gflags/gflags.h>

#include "kudu/cfile/block_cache.h"
#include "kudu/gutil/port.h"
#include "kudu/util/cache.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"
#include "kudu/util/slice.h"
#include "kudu/util/string_case.h"

DEFINE_int64(block_cache_capacity_mb, 512, "block cache capacity in MB");
TAG_FLAG(block_cache_capacity_mb, stable);

DEFINE_string(block_cache_type, "DRAM",
              "Which type of block cache to use for caching data. "
              "Valid choices are 'DRAM' or 'NVM'. DRAM, the default, "
              "caches data in regular memory. 'NVM' caches data "
              "in a memory-mapped file using the NVML library.");
TAG_FLAG(block_cache_type, experimental);

namespace kudu {

class MetricEntity;

namespace cfile {

struct CacheKey {
  CacheKey(BlockCache::FileId file_id, uint64_t offset) :
    file_id_(file_id.id()),
    offset_(offset)
  {}

  const Slice slice() const {
    return Slice(reinterpret_cast<const uint8_t *>(this), sizeof(*this));
  }

  uint64_t file_id_;
  uint64_t offset_;
} PACKED;

namespace {
class Deleter : public CacheDeleter {
 public:
  explicit Deleter(Cache* cache) : cache_(cache) {
  }
  virtual void Delete(const Slice& slice, void* value) OVERRIDE {
    Slice *value_slice = reinterpret_cast<Slice *>(value);

    // The actual data was allocated from the cache's memory
    // (i.e. it may be in nvm)
    cache_->Free(value_slice->mutable_data());
    delete value_slice;
  }
 private:
  Cache* cache_;
  DISALLOW_COPY_AND_ASSIGN(Deleter);
};

Cache* CreateCache(int64_t capacity) {
  CacheType t;
  ToUpperCase(FLAGS_block_cache_type, &FLAGS_block_cache_type);
  if (FLAGS_block_cache_type == "NVM") {
    t = NVM_CACHE;
  } else if (FLAGS_block_cache_type == "DRAM") {
    t = DRAM_CACHE;
  } else {
    LOG(FATAL) << "Unknown block cache type: '" << FLAGS_block_cache_type
               << "' (expected 'DRAM' or 'NVM')";
  }
  return NewLRUCache(t, capacity, "block_cache");
}

} // anonymous namespace

BlockCache::BlockCache()
  : cache_(CreateCache(FLAGS_block_cache_capacity_mb * 1024 * 1024)) {
  deleter_.reset(new Deleter(cache_.get()));
}

BlockCache::BlockCache(size_t capacity)
  : cache_(CreateCache(capacity)) {
  deleter_.reset(new Deleter(cache_.get()));
}

uint8_t* BlockCache::Allocate(size_t size) {
  return cache_->Allocate(size);
}

void BlockCache::Free(uint8_t* p) {
  cache_->Free(p);
}

uint8_t* BlockCache::MoveToHeap(uint8_t* p, size_t size) {
  return cache_->MoveToHeap(p, size);
}

bool BlockCache::Lookup(FileId file_id, uint64_t offset, Cache::CacheBehavior behavior,
                        BlockCacheHandle *handle) {
  CacheKey key(file_id, offset);
  Cache::Handle *h = cache_->Lookup(key.slice(), behavior);
  if (h != nullptr) {
    handle->SetHandle(cache_.get(), h);
  }
  return h != nullptr;
}

bool BlockCache::Insert(FileId file_id, uint64_t offset, const Slice &block_data,
                        BlockCacheHandle *inserted) {
  CacheKey key(file_id, offset);
  // Allocate a copy of the value Slice (not the referred-to-data!)
  // for insertion in the cache.
  gscoped_ptr<Slice> value(new Slice(block_data));
  Cache::Handle *h = cache_->Insert(key.slice(), value.get(), value->size(),
                                    deleter_.get());
  if (h != nullptr) {
    inserted->SetHandle(cache_.get(), h);
    ignore_result(value.release());
  }
  return h != nullptr;
}

void BlockCache::StartInstrumentation(const scoped_refptr<MetricEntity>& metric_entity) {
  cache_->SetMetrics(metric_entity);
}

} // namespace cfile
} // namespace kudu
