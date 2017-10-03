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
#ifndef KUDU_CFILE_BLOCK_CACHE_H
#define KUDU_CFILE_BLOCK_CACHE_H

#include <algorithm>
#include <glog/logging.h>

#include "kudu/fs/block_id.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/singleton.h"
#include "kudu/util/cache.h"

DECLARE_string(block_cache_type);

namespace kudu {

class MetricRegistry;

namespace cfile {

class BlockCacheHandle;

// Wrapper around kudu::Cache specifically for caching blocks of CFiles.
// Provides a singleton and LRU cache for CFile blocks.
class BlockCache {
 public:
  // BlockId refers to the unique identifier for a Kudu block, that is, for an
  // entire CFile. This is different than the block cache's notion of a block,
  // which is just a portion of a CFile.
  typedef BlockId FileId;

  static BlockCache *GetSingleton() {
    return Singleton<BlockCache>::get();
  }

  explicit BlockCache(size_t capacity);

  // Lookup the given block in the cache.
  //
  // If the entry is found, then sets *handle to refer to the entry.
  // This object's destructor will release the cache entry so it may be freed again.
  // Alternatively,  handle->Release() may be used to explicitly release it.
  //
  // Returns true to indicate that the entry was found, false otherwise.
  bool Lookup(FileId file_id, uint64_t offset,
              Cache::CacheBehavior behavior, BlockCacheHandle *handle);

  // Insert the given block into the cache.
  //
  // The data pointed to by Slice should have been allocated using Allocate().
  // After insertion, the block cache owns this pointer and will free it upon
  // eviction.
  //
  // The inserted entry is returned in *inserted.
  bool Insert(FileId file_id, uint64_t offset, const Slice &block_data,
              BlockCacheHandle *inserted);

  // Pass a metric entity to the cache to start recording metrics.
  // This should be called before the block cache starts serving blocks.
  // Not calling StartInstrumentation will simply result in no block cache-related metrics.
  // Calling StartInstrumentation multiple times will reset the metrics each time.
  void StartInstrumentation(const scoped_refptr<MetricEntity>& metric_entity);

  // Allocate a chunk of memory to hold a value in this cache.
  //
  // Some cache implementations may allocate the buffer outside of the normal
  // heap area.
  //
  // NOTE: The returned pointer may either be passed to Insert(), MoveToHeap(), or
  // Free(). It must NOT be freed using free() or delete[].
  uint8_t* Allocate(size_t size);

  // Move a pointer previously allocated using Allocate() onto the normal heap.
  // This is a no-op for a DRAM-based cache, but in other cases may relocate the
  // data.
  uint8_t* MoveToHeap(uint8_t* p, size_t size);

  // Free a pointer previously allocated using Allocate().
  void Free(uint8_t *p);

 private:
  friend class Singleton<BlockCache>;
  BlockCache();

  DISALLOW_COPY_AND_ASSIGN(BlockCache);

  // Deleter must be defined before cache_ so that cache_ destructs first.
  // (the Cache needs to use the Deleter during destruction)
  gscoped_ptr<CacheDeleter> deleter_;
  gscoped_ptr<Cache> cache_;
};

// Scoped reference to a block from the block cache.
class BlockCacheHandle {
 public:
  BlockCacheHandle() :
    handle_(NULL)
  {}

  ~BlockCacheHandle() {
    if (handle_ != NULL) {
      Release();
    }
  }

  void Release() {
    CHECK_NOTNULL(cache_)->Release(CHECK_NOTNULL(handle_));
    handle_ = NULL;
  }

  // Swap this handle with another handle.
  // This can be useful to transfer ownership of a handle by swapping
  // with an empty BlockCacheHandle.
  void swap(BlockCacheHandle *dst) {
    std::swap(this->cache_, dst->cache_);
    std::swap(this->handle_, dst->handle_);
  }

  // Return the data in the cached block.
  //
  // NOTE: this slice is only valid until the block cache handle is
  // destructed or explicitly Released().
  const Slice &data() const {
    const Slice *slice = reinterpret_cast<const Slice *>(cache_->Value(handle_));
    return *slice;
  }

  bool valid() const {
    return handle_ != NULL;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(BlockCacheHandle);
  friend class BlockCache;

  void SetHandle(Cache *cache, Cache::Handle *handle) {
    if (handle_ != NULL) Release();

    cache_ = cache;
    handle_ = handle;
  }

  Cache::Handle *handle_;
  Cache *cache_;
};


} // namespace cfile
} // namespace kudu

#endif
