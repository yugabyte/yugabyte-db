// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A Cache is an interface that maps keys to values.  It has internal
// synchronization and may be safely accessed concurrently from
// multiple threads.  It may automatically evict entries to make room
// for new entries.  Values have a specified charge against the cache
// capacity.  For example, a cache where the values are variable
// length strings, may use the length of the string as the charge for
// the string.
//
// This is taken from LevelDB and evolved to fit the kudu codebase.
//
// TODO: this is pretty lock-heavy. Would be good to sub out something
// a little more concurrent.

#ifndef KUDU_UTIL_CACHE_H_
#define KUDU_UTIL_CACHE_H_

#include <stdint.h>
#include <string>

#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/slice.h"

namespace kudu {

class Cache;
struct CacheMetrics;
class MetricEntity;

enum CacheType {
  DRAM_CACHE,
  NVM_CACHE
};

// Create a new cache with a fixed size capacity.  This implementation
// of Cache uses a least-recently-used eviction policy.
Cache* NewLRUCache(CacheType type, size_t capacity, const std::string& id);

// Callback interface for deleting a value stored in the cache.
// This is called when an inserted entry is no longer needed.
class CacheDeleter {
 public:
  // Delete the given 'value'.
  // The key is only passed for convenenience -- the cache itself is
  // responsible for managing the key's memory.
  virtual void Delete(const Slice& key, void* value) = 0;
  virtual ~CacheDeleter() {}
};

class Cache {
 public:
  Cache() { }

  // Destroys all existing entries by calling the "deleter"
  // function that was passed to the constructor.
  virtual ~Cache();

  // Opaque handle to an entry stored in the cache.
  struct Handle { };

  // Insert a mapping from key->value into the cache and assign it
  // the specified charge against the total cache capacity.
  //
  // Returns a handle that corresponds to the mapping.  The caller
  // must call this->Release(handle) when the returned mapping is no
  // longer needed.
  //
  // Note that the 'key' Slice is copied into the internal storage of
  // the cache. The caller may free or mutate the key data freely
  // after this method returns.
  //
  // When the inserted entry is no longer needed, the cache object, key and
  // value will be passed to "deleter". The deleter callback must remain
  // valid until it is called.
  virtual Handle* Insert(const Slice& key, void* value, size_t charge,
                         CacheDeleter* deleter) = 0;

  // Passing EXPECT_IN_CACHE will increment the hit/miss metrics that track the number of times
  // blocks were requested that the users were hoping to get the block from the cache, along with
  // with the basic metrics.
  // Passing NO_EXPECT_IN_CACHE will only increment the basic metrics.
  // This helps in determining if we are effectively caching the blocks that matter the most.
  enum CacheBehavior {
    EXPECT_IN_CACHE,
    NO_EXPECT_IN_CACHE
  };

  // If the cache has no mapping for "key", returns NULL.
  //
  // Else return a handle that corresponds to the mapping.  The caller
  // must call this->Release(handle) when the returned mapping is no
  // longer needed.
  virtual Handle* Lookup(const Slice& key, CacheBehavior caching) = 0;

  // Release a mapping returned by a previous Lookup().
  // REQUIRES: handle must not have been released yet.
  // REQUIRES: handle must have been returned by a method on *this.
  virtual void Release(Handle* handle) = 0;

  // Return the value encapsulated in a handle returned by a
  // successful Lookup().
  // REQUIRES: handle must not have been released yet.
  // REQUIRES: handle must have been returned by a method on *this.
  virtual void* Value(Handle* handle) = 0;

  // If the cache contains entry for key, erase it.  Note that the
  // underlying entry will be kept around until all existing handles
  // to it have been released.
  virtual void Erase(const Slice& key) = 0;

  // Return a new numeric id.  May be used by multiple clients who are
  // sharing the same cache to partition the key space.  Typically the
  // client will allocate a new id at startup and prepend the id to
  // its cache keys.
  virtual uint64_t NewId() = 0;

  // Pass a metric entity in order to start recoding metrics.
  virtual void SetMetrics(const scoped_refptr<MetricEntity>& metric_entity) = 0;

  // Allocate 'bytes' bytes from the cache's memory pool.
  //
  // It is possible that this will return NULL if the cache is above its capacity
  // and eviction fails to free up enough space for the requested allocation.
  //
  // NOTE: the returned memory is not automatically freed by the cache: the
  // caller must either free it using Free(), or MoveToHeap() followed by
  // delete[].
  virtual uint8_t* Allocate(int bytes) = 0;

  // Free 'ptr', which must have been previously allocated using 'Allocate'.
  virtual void Free(uint8_t* ptr) = 0;

  // Moves 'ptr' to the normal C++ heap, if it is not already there.
  // 'ptr' must have previously been allocated using Allocate(bytes).
  // If 'ptr' is already on the C++ heap, then returns the same value.
  //
  // The returned value should be freed by the caller using the 'delete[]'
  // operator.
  virtual uint8_t* MoveToHeap(uint8_t* ptr, int bytes) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Cache);

  void LRU_Remove(Handle* e);
  void LRU_Append(Handle* e);
  void Unref(Handle* e);

  struct Rep;
  Rep* rep_;
};

}  // namespace kudu

#endif
