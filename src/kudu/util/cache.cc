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

#include <glog/logging.h>
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
#include "kudu/util/locks.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"

#if !defined(__APPLE__)
#include "kudu/util/nvm_cache.h"
#endif

namespace kudu {

class MetricEntity;

Cache::~Cache() {
}

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
  HandleTable() : length_(0), elems_(0), list_(nullptr) { Resize(); }
  ~HandleTable() { delete[] list_; }

  LRUHandle* Lookup(const Slice& key, uint32_t hash) {
    return *FindPointer(key, hash);
  }

  LRUHandle* Insert(LRUHandle* h) {
    LRUHandle** ptr = FindPointer(h->key(), h->hash);
    LRUHandle* old = *ptr;
    h->next_hash = (old == nullptr ? nullptr : old->next_hash);
    *ptr = h;
    if (old == nullptr) {
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
    if (result != nullptr) {
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
    while (*ptr != nullptr &&
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
    auto new_list = new LRUHandle*[new_length];
    memset(new_list, 0, sizeof(new_list[0]) * new_length);
    uint32_t count = 0;
    for (uint32_t i = 0; i < length_; i++) {
      LRUHandle* h = list_[i];
      while (h != nullptr) {
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
class LRUCache {
 public:
  explicit LRUCache(MemTracker* tracker);
  ~LRUCache();

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

 private:
  void LRU_Remove(LRUHandle* e);
  void LRU_Append(LRUHandle* e);
  // Just reduce the reference count by 1.
  // Return true if last reference
  bool Unref(LRUHandle* e);
  // Call deleter and free
  void FreeEntry(LRUHandle* e);

  // Initialized before use.
  size_t capacity_;

  // mutex_ protects the following state.
  MutexType mutex_;
  size_t usage_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  LRUHandle lru_;

  HandleTable table_;

  MemTracker* mem_tracker_;

  CacheMetrics* metrics_;
};

LRUCache::LRUCache(MemTracker* tracker)
 : usage_(0),
   mem_tracker_(tracker),
   metrics_(nullptr) {
  // Make empty circular linked list
  lru_.next = &lru_;
  lru_.prev = &lru_;
}

LRUCache::~LRUCache() {
  for (LRUHandle* e = lru_.next; e != &lru_; ) {
    LRUHandle* next = e->next;
    DCHECK_EQ(e->refs, 1);  // Error if caller has an unreleased handle
    if (Unref(e)) {
      FreeEntry(e);
    }
    e = next;
  }
}

bool LRUCache::Unref(LRUHandle* e) {
  DCHECK_GT(ANNOTATE_UNPROTECTED_READ(e->refs), 0);
  return !base::RefCountDec(&e->refs);
}

void LRUCache::FreeEntry(LRUHandle* e) {
  DCHECK_EQ(ANNOTATE_UNPROTECTED_READ(e->refs), 0);
  e->deleter->Delete(e->key(), e->value);
  mem_tracker_->Release(e->charge);
  if (PREDICT_TRUE(metrics_)) {
    metrics_->cache_usage->DecrementBy(e->charge);
    metrics_->evictions->Increment();
  }
  free(e);
}

void LRUCache::LRU_Remove(LRUHandle* e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
  usage_ -= e->charge;
}

void LRUCache::LRU_Append(LRUHandle* e) {
  // Make "e" newest entry by inserting just before lru_
  e->next = &lru_;
  e->prev = lru_.prev;
  e->prev->next = e;
  e->next->prev = e;
  usage_ += e->charge;
}

Cache::Handle* LRUCache::Lookup(const Slice& key, uint32_t hash, bool caching) {
  LRUHandle* e;
  {
    lock_guard<MutexType> l(&mutex_);
    e = table_.Lookup(key, hash);
    if (e != nullptr) {
      base::RefCountInc(&e->refs);
      LRU_Remove(e);
      LRU_Append(e);
    }
  }

  // Do the metrics outside of the lock.
  if (metrics_) {
    metrics_->lookups->Increment();
    bool was_hit = (e != nullptr);
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

void LRUCache::Release(Cache::Handle* handle) {
  LRUHandle* e = reinterpret_cast<LRUHandle*>(handle);
  bool last_reference = Unref(e);
  if (last_reference) {
    FreeEntry(e);
  }
}

Cache::Handle* LRUCache::Insert(
    const Slice& key, uint32_t hash, void* value, size_t charge,
    CacheDeleter *deleter) {

  LRUHandle* e = reinterpret_cast<LRUHandle*>(
      malloc(sizeof(LRUHandle)-1 + key.size()));
  LRUHandle* to_remove_head = nullptr;

  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size();
  e->hash = hash;
  e->refs = 2;  // One from LRUCache, one for the returned handle
  memcpy(e->key_data, key.data(), key.size());
  mem_tracker_->Consume(charge);
  if (PREDICT_TRUE(metrics_)) {
    metrics_->cache_usage->IncrementBy(charge);
    metrics_->inserts->Increment();
  }

  {
    lock_guard<MutexType> l(&mutex_);

    LRU_Append(e);

    LRUHandle* old = table_.Insert(e);
    if (old != nullptr) {
      LRU_Remove(old);
      if (Unref(old)) {
        old->next = to_remove_head;
        to_remove_head = old;
      }
    }

    while (usage_ > capacity_ && lru_.next != &lru_) {
      LRUHandle* old = lru_.next;
      LRU_Remove(old);
      table_.Remove(old->key(), old->hash);
      if (Unref(old)) {
        old->next = to_remove_head;
        to_remove_head = old;
      }
    }
  }

  // we free the entries here outside of mutex for
  // performance reasons
  while (to_remove_head != nullptr) {
    LRUHandle* next = to_remove_head->next;
    FreeEntry(to_remove_head);
    to_remove_head = next;
  }

  return reinterpret_cast<Cache::Handle*>(e);
}

void LRUCache::Erase(const Slice& key, uint32_t hash) {
  LRUHandle* e;
  bool last_reference = false;
  {
    lock_guard<MutexType> l(&mutex_);
    e = table_.Remove(key, hash);
    if (e != nullptr) {
      LRU_Remove(e);
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
  shared_ptr<MemTracker> mem_tracker_;
  gscoped_ptr<CacheMetrics> metrics_;
  vector<LRUCache*> shards_;
  MutexType id_mutex_;
  uint64_t last_id_;

  static inline uint32_t HashSlice(const Slice& s) {
    return util_hash::CityHash64(
      reinterpret_cast<const char *>(s.data()), s.size());
  }

  static uint32_t Shard(uint32_t hash) {
    return hash >> (32 - kNumShardBits);
  }

 public:
  explicit ShardedLRUCache(size_t capacity, const string& id)
      : last_id_(0) {
    // A cache is often a singleton, so:
    // 1. We reuse its MemTracker if one already exists, and
    // 2. It is directly parented to the root MemTracker.
    mem_tracker_ = MemTracker::FindOrCreateTracker(
        -1, strings::Substitute("$0-sharded_lru_cache", id));

    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++) {
      gscoped_ptr<LRUCache> shard(new LRUCache(mem_tracker_.get()));
      shard->SetCapacity(per_shard);
      shards_.push_back(shard.release());
    }
  }

  virtual ~ShardedLRUCache() {
    STLDeleteElements(&shards_);
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
    for (LRUCache* cache : shards_) {
      cache->SetMetrics(metrics_.get());
    }
  }

  virtual uint8_t* Allocate(int bytes) OVERRIDE {
    DCHECK_GE(bytes, 0);
    return new uint8_t[bytes];
  }

  virtual void Free(uint8_t* ptr) OVERRIDE {
    delete[] ptr;
  }

  virtual uint8_t* MoveToHeap(uint8_t* ptr, int size) OVERRIDE {
    // Our allocated pointers are always on the heap.
    return ptr;
  }

};

}  // end anonymous namespace

Cache* NewLRUCache(CacheType type, size_t capacity, const string& id) {
  switch (type) {
    case DRAM_CACHE:
      return new ShardedLRUCache(capacity, id);
#if !defined(__APPLE__)
    case NVM_CACHE:
      return NewLRUNvmCache(capacity, id);
#endif
    default:
      LOG(FATAL) << "Unsupported LRU cache type: " << type;
  }
}

}  // namespace kudu
