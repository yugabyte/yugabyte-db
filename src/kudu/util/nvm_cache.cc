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
//
// Some portions copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// ------------------------------------------------------------
// This file implements a cache based on the NVML library (http://pmem.io),
// specifically its "libvmem" component. This library makes it easy to program
// against persistent memory hardware by exposing an API which parallels
// malloc/free, but allocates from persistent memory instead of DRAM.
//
// We use this API to implement a cache which treats persistent memory or
// non-volatile memory as if it were a larger cheaper bank of volatile memory. We
// currently make no use of its persistence properties.
//
// Currently, we only store key/value in NVM. All other data structures such as the
// ShardedLRUCache instances, hash table, etc are in DRAM. The assumption is that
// the ratio of data stored vs overhead is quite high.

#include "kudu/util/nvm_cache.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <libvmem.h>
#include <memory>
#include <stdlib.h>
#include <string>
#include <vector>

#include "kudu/gutil/atomic_refcount.h"
#include "kudu/gutil/hash/city.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/atomic.h"
#include "kudu/util/cache.h"
#include "kudu/util/cache_metrics.h"
#include "kudu/util/flags.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/locks.h"
#include "kudu/util/metrics.h"

DEFINE_string(nvm_cache_path, "/vmem",
              "The path at which the NVM cache will try to allocate its memory. "
              "This can be a tmpfs or ramfs for testing purposes.");
TAG_FLAG(nvm_cache_path, experimental);

DEFINE_int32(nvm_cache_allocation_retry_count, 10,
             "The number of times that the NVM cache will retry attempts to allocate "
             "memory for new entries. In between attempts, a cache entry will be "
             "evicted.");
TAG_FLAG(nvm_cache_allocation_retry_count, advanced);
TAG_FLAG(nvm_cache_allocation_retry_count, experimental);

DEFINE_bool(nvm_cache_simulate_allocation_failure, false,
            "If true, the NVM cache will inject failures in calls to vmem_malloc "
            "for testing.");
TAG_FLAG(nvm_cache_simulate_allocation_failure, unsafe);


namespace kudu {

class MetricEntity;

namespace {

using std::shared_ptr;
using std::vector;

typedef simple_spinlock MutexType;

// LRU cache implementation

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.
struct LRUHandle {
  void* value;
  CacheDeleter* deleter;
  LRUHandle* next_hash;
  LRUHandle* next;
  LRUHandle* prev;
  size_t charge;      // TODO(opt): Only allow uint32_t?
  size_t key_length;
  Atomic32 refs;
  uint32_t hash;      // Hash of key(); used for fast sharding and comparisons
  uint8_t key_data[1];   // Beginning of key

  Slice key() const {
    // For cheaper lookups, we allow a temporary Handle object
    // to store a pointer to a key in "value".
    if (next == this) {
      return *(reinterpret_cast<Slice*>(value));
    } else {
      return Slice(key_data, key_length);
    }
  }
};

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
class HandleTable {
 public:
  HandleTable() : length_(0), elems_(0), list_(NULL) { Resize(); }
  ~HandleTable() { delete[] list_; }

  LRUHandle* Lookup(const Slice& key, uint32_t hash) {
    return *FindPointer(key, hash);
  }

  LRUHandle* Insert(LRUHandle* h) {
    LRUHandle** ptr = FindPointer(h->key(), h->hash);
    LRUHandle* old = *ptr;
    h->next_hash = (old == NULL ? NULL : old->next_hash);
    *ptr = h;
    if (old == NULL) {
      ++elems_;
      if (elems_ > length_) {
        // Since each cache entry is fairly large, we aim for a small
        // average linked list length (<= 1).
        Resize();
      }
    }
    return old;
  }

  LRUHandle* Remove(const Slice& key, uint32_t hash) {
    LRUHandle** ptr = FindPointer(key, hash);
    LRUHandle* result = *ptr;
    if (result != NULL) {
      *ptr = result->next_hash;
      --elems_;
    }
    return result;
  }

 private:
  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  uint32_t elems_;
  LRUHandle** list_;

  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  LRUHandle** FindPointer(const Slice& key, uint32_t hash) {
    LRUHandle** ptr = &list_[hash & (length_ - 1)];
    while (*ptr != NULL &&
           ((*ptr)->hash != hash || key != (*ptr)->key())) {
      ptr = &(*ptr)->next_hash;
    }
    return ptr;
  }

  void Resize() {
    uint32_t new_length = 16;
    while (new_length < elems_ * 1.5) {
      new_length *= 2;
    }
    LRUHandle** new_list = new LRUHandle*[new_length];
    memset(new_list, 0, sizeof(new_list[0]) * new_length);
    uint32_t count = 0;
    for (uint32_t i = 0; i < length_; i++) {
      LRUHandle* h = list_[i];
      while (h != NULL) {
        LRUHandle* next = h->next_hash;
        uint32_t hash = h->hash;
        LRUHandle** ptr = &new_list[hash & (new_length - 1)];
        h->next_hash = *ptr;
        *ptr = h;
        h = next;
        count++;
      }
    }
    DCHECK_EQ(elems_, count);
    delete[] list_;
    list_ = new_list;
    length_ = new_length;
  }
};

// A single shard of sharded cache.
class NvmLRUCache {
 public:
  explicit NvmLRUCache(VMEM *vmp);
  ~NvmLRUCache();

  // Separate from constructor so caller can easily make an array of LRUCache
  void SetCapacity(size_t capacity) { capacity_ = capacity; }

  void SetMetrics(CacheMetrics* metrics) { metrics_ = metrics; }

  // Like Cache methods, but with an extra "hash" parameter.
  Cache::Handle* Insert(const Slice& key, uint32_t hash,
                        void* value, size_t charge,
                        CacheDeleter* deleter);
  Cache::Handle* Lookup(const Slice& key, uint32_t hash, bool caching);
  void Release(Cache::Handle* handle);
  void Erase(const Slice& key, uint32_t hash);
  void* AllocateAndRetry(size_t size);

 private:
  void NvmLRU_Remove(LRUHandle* e);
  void NvmLRU_Append(LRUHandle* e);
  // Just reduce the reference count by 1.
  // Return true if last reference
  bool Unref(LRUHandle* e);
  void FreeEntry(LRUHandle* e);

  // Evict the LRU item in the cache, adding it to the linked list
  // pointed to by 'to_remove_head'.
  void EvictOldestUnlocked(LRUHandle** to_remove_head);

  // Free all of the entries in the linked list that has to_free_head
  // as its head.
  void FreeLRUEntries(LRUHandle* to_free_head);

  // Wrapper around vmem_malloc which injects failures based on a flag.
  void* VmemMalloc(size_t size);

  // Initialized before use.
  size_t capacity_;

  // mutex_ protects the following state.
  MutexType mutex_;
  size_t usage_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  LRUHandle lru_;

  HandleTable table_;

  VMEM* vmp_;

  CacheMetrics* metrics_;
};

NvmLRUCache::NvmLRUCache(VMEM* vmp)
  : usage_(0),
  vmp_(vmp),
  metrics_(NULL) {
  // Make empty circular linked list
  lru_.next = &lru_;
  lru_.prev = &lru_;
}

NvmLRUCache::~NvmLRUCache() {
  for (LRUHandle* e = lru_.next; e != &lru_; ) {
    LRUHandle* next = e->next;
    DCHECK_EQ(e->refs, 1);  // Error if caller has an unreleased handle
    if (Unref(e)) {
      FreeEntry(e);
    }
    e = next;
  }
}

void* NvmLRUCache::VmemMalloc(size_t size) {
  if (PREDICT_FALSE(FLAGS_nvm_cache_simulate_allocation_failure)) {
    return NULL;
  }
  return vmem_malloc(vmp_, size);
}

bool NvmLRUCache::Unref(LRUHandle* e) {
  DCHECK_GT(ANNOTATE_UNPROTECTED_READ(e->refs), 0);
  return !base::RefCountDec(&e->refs);
}

void NvmLRUCache::FreeEntry(LRUHandle* e) {
  DCHECK_EQ(ANNOTATE_UNPROTECTED_READ(e->refs), 0);
  e->deleter->Delete(e->key(), e->value);
  if (PREDICT_TRUE(metrics_)) {
    metrics_->cache_usage->DecrementBy(e->charge);
    metrics_->evictions->Increment();
  }
  vmem_free(vmp_, e);
}

// Allocate nvm memory. Try until successful or FLAGS_nvm_cache_allocation_retry_count
// has been exceeded.
void *NvmLRUCache::AllocateAndRetry(size_t size) {
  void *tmp;
  // There may be times that an allocation fails. With NVM we have
  // a fixed size to allocate from. If we cannot allocate the size
  // that was asked for, we will remove entries from the cache and
  // retry up to the configured number of retries. If this fails, we
  // return NULL, which will cause the caller to not insert anything
  // into the cache.
  LRUHandle *to_remove_head = NULL;
  tmp = VmemMalloc(size);

  if (tmp == NULL) {
    unique_lock<MutexType> l(&mutex_);

    int retries_remaining = FLAGS_nvm_cache_allocation_retry_count;
    while (tmp == NULL && retries_remaining-- > 0 && lru_.next != &lru_) {
      EvictOldestUnlocked(&to_remove_head);

      // Unlock while allocating memory.
      l.unlock();
      tmp = VmemMalloc(size);
      l.lock();
    }
  }

  // we free the entries here outside of mutex for
  // performance reasons
  FreeLRUEntries(to_remove_head);
  return tmp;
}

void NvmLRUCache::NvmLRU_Remove(LRUHandle* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
  usage_ -= e->charge;
}

void NvmLRUCache::NvmLRU_Append(LRUHandle* e) {
  // Make "e" newest entry by inserting just before lru_
  e->next = &lru_;
  e->prev = lru_.prev;
  e->prev->next = e;
  e->next->prev = e;
  usage_ += e->charge;
}

Cache::Handle* NvmLRUCache::Lookup(const Slice& key, uint32_t hash, bool caching) {
 LRUHandle* e;
  {
    lock_guard<MutexType> l(&mutex_);
    e = table_.Lookup(key, hash);
    if (e != NULL) {
      // If an entry exists, remove the old entry from the cache
      // and re-add to the end of the linked list.
      base::RefCountInc(&e->refs);
      NvmLRU_Remove(e);
      NvmLRU_Append(e);
    }
  }

  // Do the metrics outside of the lock.
  if (metrics_) {
    metrics_->lookups->Increment();
    bool was_hit = (e != NULL);
    if (was_hit) {
      if (caching) {
        metrics_->cache_hits_caching->Increment();
      } else {
        metrics_->cache_hits->Increment();
      }
    } else {
      if (caching) {
        metrics_->cache_misses_caching->Increment();
      } else {
        metrics_->cache_misses->Increment();
      }
    }
  }

  return reinterpret_cast<Cache::Handle*>(e);
}

void NvmLRUCache::Release(Cache::Handle* handle) {
  LRUHandle* e = reinterpret_cast<LRUHandle*>(handle);
  bool last_reference = Unref(e);
  if (last_reference) {
    FreeEntry(e);
  }
}

void NvmLRUCache::EvictOldestUnlocked(LRUHandle** to_remove_head) {
  LRUHandle* old = lru_.next;
  NvmLRU_Remove(old);
  table_.Remove(old->key(), old->hash);
  if (Unref(old)) {
    old->next = *to_remove_head;
    *to_remove_head = old;
  }
}

void NvmLRUCache::FreeLRUEntries(LRUHandle* to_free_head) {
  while (to_free_head != NULL) {
    LRUHandle* next = to_free_head->next;
    FreeEntry(to_free_head);
    to_free_head = next;
  }
}

Cache::Handle* NvmLRUCache::Insert(const Slice& key, uint32_t hash,
                                   void* value, size_t charge,
                                   CacheDeleter* deleter) {
  // Account for nvm key memory.
  LRUHandle* e = reinterpret_cast<LRUHandle*>(
      AllocateAndRetry(sizeof(LRUHandle) - 1 /* sizeof(LRUHandle::key_data) */ + key.size()));
  LRUHandle* to_remove_head = NULL;

  if (!e) {
    return NULL;
  }

  e->value = value;
  memcpy(e->key_data, key.data(), key.size());

  // Modify the charge to the nvm cache to account for all allocations
  // done from the nvm address space. In this case we allocated the value
  // slice object, the key slice and the key_data structure from nvm.
  e->charge = charge + key.size();
  e->hash = hash;
  e->refs = 2;  // One from LRUCache, one for the returned handle
  e->key_length = key.size();
  e->deleter = deleter;
  if (PREDICT_TRUE(metrics_)) {
    metrics_->cache_usage->IncrementBy(e->charge);
    metrics_->inserts->Increment();
  }

  {
    lock_guard<MutexType> l(&mutex_);

    NvmLRU_Append(e);

    LRUHandle* old = table_.Insert(e);
    if (old != NULL) {
      NvmLRU_Remove(old);
      if (Unref(old)) {
        old->next = to_remove_head;
        to_remove_head = old;
      }
    }

    while (usage_ > capacity_ && lru_.next != &lru_) {
      EvictOldestUnlocked(&to_remove_head);
    }
  }

  // we free the entries here outside of mutex for
  // performance reasons
  FreeLRUEntries(to_remove_head);

  return reinterpret_cast<Cache::Handle*>(e);
}

void NvmLRUCache::Erase(const Slice& key, uint32_t hash) {
  LRUHandle* e;
  bool last_reference = false;
  {
    lock_guard<MutexType> l(&mutex_);
    e = table_.Remove(key, hash);
    if (e != NULL) {
      NvmLRU_Remove(e);
      last_reference = Unref(e);
    }
  }
  // mutex not held here
  // last_reference will only be true if e != NULL
  if (last_reference) {
    FreeEntry(e);
  }
}
static const int kNumShardBits = 4;
static const int kNumShards = 1 << kNumShardBits;

class ShardedLRUCache : public Cache {
 private:
  gscoped_ptr<CacheMetrics> metrics_;
  vector<NvmLRUCache*> shards_;
  MutexType id_mutex_;
  uint64_t last_id_;
  VMEM* vmp_;

  static inline uint32_t HashSlice(const Slice& s) {
    return util_hash::CityHash64(
      reinterpret_cast<const char *>(s.data()), s.size());
  }

  static uint32_t Shard(uint32_t hash) {
    return hash >> (32 - kNumShardBits);
  }

 public:
  explicit ShardedLRUCache(size_t capacity, const string& id, VMEM* vmp)
        : last_id_(0),
          vmp_(vmp) {

    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
      gscoped_ptr<NvmLRUCache> shard(new NvmLRUCache(vmp_));
      shard->SetCapacity(per_shard);
      shards_.push_back(shard.release());
    }
  }

  virtual ~ShardedLRUCache() {
    STLDeleteElements(&shards_);
    // Per the note at the top of this file, our cache is entirely volatile.
    // Hence, when the cache is destructed, we delete the underlying
    // VMEM pool.
    vmem_delete(vmp_);
  }

  virtual Handle* Insert(const Slice& key, void* value, size_t charge,
                         CacheDeleter* deleter) OVERRIDE {
    const uint32_t hash = HashSlice(key);
    return shards_[Shard(hash)]->Insert(key, hash, value, charge, deleter);
  }
  virtual Handle* Lookup(const Slice& key, CacheBehavior caching) OVERRIDE {
    const uint32_t hash = HashSlice(key);
    return shards_[Shard(hash)]->Lookup(key, hash, caching == EXPECT_IN_CACHE);
  }
  virtual void Release(Handle* handle) OVERRIDE {
    LRUHandle* h = reinterpret_cast<LRUHandle*>(handle);
    shards_[Shard(h->hash)]->Release(handle);
  }
  virtual void Erase(const Slice& key) OVERRIDE {
    const uint32_t hash = HashSlice(key);
    shards_[Shard(hash)]->Erase(key, hash);
  }
  virtual void* Value(Handle* handle) OVERRIDE {
    return reinterpret_cast<LRUHandle*>(handle)->value;
  }
  virtual uint64_t NewId() OVERRIDE {
    lock_guard<MutexType> l(&id_mutex_);
    return ++(last_id_);
  }
  virtual void SetMetrics(const scoped_refptr<MetricEntity>& entity) OVERRIDE {
    metrics_.reset(new CacheMetrics(entity));
    for (NvmLRUCache* cache : shards_) {
      cache->SetMetrics(metrics_.get());
    }
  }
  virtual uint8_t* Allocate(int size) OVERRIDE {
    // Try allocating from each of the shards -- if vmem is tight,
    // this can cause eviction, so we might have better luck in different
    // shards.
    for (NvmLRUCache* cache : shards_) {
      uint8_t* ptr = reinterpret_cast<uint8_t*>(cache->AllocateAndRetry(size));
      if (ptr) return ptr;
    }
    // TODO: increment a metric here on allocation failure.
    return NULL;
  }
  virtual void Free(uint8_t *ptr) OVERRIDE {
    vmem_free(vmp_, ptr);
  }
  virtual uint8_t* MoveToHeap(uint8_t* ptr, int size) OVERRIDE {
    uint8_t* ret = new uint8_t[size];
    memcpy(ret, ptr, size);
    vmem_free(vmp_, ptr);
    return ret;
  }

};

} // end anonymous namespace

Cache* NewLRUNvmCache(size_t capacity, const std::string& id) {
  // vmem_create() will fail if the capacity is too small, but with
  // an inscrutable error. So, we'll check ourselves.
  CHECK_GE(capacity, VMEM_MIN_POOL)
    << "configured capacity " << capacity << " bytes is less than "
    << "the minimum capacity for an NVM cache: " << VMEM_MIN_POOL;

  VMEM* vmp = vmem_create(FLAGS_nvm_cache_path.c_str(), capacity);
  // If we cannot create the cache pool we should not retry.
  PLOG_IF(FATAL, vmp == NULL) << "Could not initialize NVM cache library in path "
                              << FLAGS_nvm_cache_path.c_str();

  return new ShardedLRUCache(capacity, id, vmp);
}

}  // namespace kudu
