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
// Some portions Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <glog/logging.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <memory>

#include <vector>
#include "kudu/util/cache.h"
#include "kudu/util/coding.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_util.h"

#if defined(__linux__)
DECLARE_string(nvm_cache_path);
#endif // defined(__linux__)

namespace kudu {

// Conversions between numeric keys/values and the types expected by Cache.
static std::string EncodeKey(int k) {
  faststring result;
  PutFixed32(&result, k);
  return result.ToString();
}
static int DecodeKey(const Slice& k) {
  assert(k.size() == 4);
  return DecodeFixed32(k.data());
}
static void* EncodeValue(uintptr_t v) { return reinterpret_cast<void*>(v); }
static int DecodeValue(void* v) { return reinterpret_cast<uintptr_t>(v); }

class CacheTest : public KuduTest,
                  public ::testing::WithParamInterface<CacheType>,
                  public CacheDeleter {
 public:

  // Implementation of the CacheDeleter interface
  virtual void Delete(const Slice& key, void* v) OVERRIDE {
    deleted_keys_.push_back(DecodeKey(key));
    deleted_values_.push_back(DecodeValue(v));
  }
  std::vector<int> deleted_keys_;
  std::vector<int> deleted_values_;
  std::shared_ptr<MemTracker> mem_tracker_;
  gscoped_ptr<Cache> cache_;
  MetricRegistry metric_registry_;

  static const int kCacheSize = 14*1024*1024;

  virtual void SetUp() OVERRIDE {

#if defined(__linux__)
    if (google::GetCommandLineFlagInfoOrDie("nvm_cache_path").is_default) {
      FLAGS_nvm_cache_path = GetTestPath("nvm-cache");
      ASSERT_OK(Env::Default()->CreateDir(FLAGS_nvm_cache_path));
    }
#endif // defined(__linux__)

    cache_.reset(NewLRUCache(GetParam(), kCacheSize, "cache_test"));

    MemTracker::FindTracker("cache_test-sharded_lru_cache", &mem_tracker_);
    // Since nvm cache does not have memtracker due to the use of
    // tcmalloc for this we only check for it in the DRAM case.
    if (GetParam() == DRAM_CACHE) {
      ASSERT_TRUE(mem_tracker_.get());
    }

    scoped_refptr<MetricEntity> entity = METRIC_ENTITY_server.Instantiate(
        &metric_registry_, "test");
    cache_->SetMetrics(entity);
  }

  int Lookup(int key) {
    Cache::Handle* handle = cache_->Lookup(EncodeKey(key), Cache::EXPECT_IN_CACHE);
    const int r = (handle == nullptr) ? -1 : DecodeValue(cache_->Value(handle));
    if (handle != nullptr) {
      cache_->Release(handle);
    }
    return r;
  }

  void Insert(int key, int value, int charge = 1) {
    cache_->Release(cache_->Insert(EncodeKey(key), EncodeValue(value), charge,
                                   this));
  }

  void Erase(int key) {
    cache_->Erase(EncodeKey(key));
  }
};

#if defined(__linux__)
INSTANTIATE_TEST_CASE_P(CacheTypes, CacheTest, ::testing::Values(DRAM_CACHE, NVM_CACHE));
#else
INSTANTIATE_TEST_CASE_P(CacheTypes, CacheTest, ::testing::Values(DRAM_CACHE));
#endif // defined(__linux__)

TEST_P(CacheTest, TrackMemory) {
  if (mem_tracker_) {
    Insert(100, 100, 1);
    ASSERT_EQ(1, mem_tracker_->consumption());
    Erase(100);
    ASSERT_EQ(0, mem_tracker_->consumption());
    ASSERT_EQ(1, mem_tracker_->peak_consumption());
  }
}

TEST_P(CacheTest, HitAndMiss) {
  ASSERT_EQ(-1, Lookup(100));

  Insert(100, 101);
  ASSERT_EQ(101, Lookup(100));
  ASSERT_EQ(-1,  Lookup(200));
  ASSERT_EQ(-1,  Lookup(300));

  Insert(200, 201);
  ASSERT_EQ(101, Lookup(100));
  ASSERT_EQ(201, Lookup(200));
  ASSERT_EQ(-1,  Lookup(300));

  Insert(100, 102);
  ASSERT_EQ(102, Lookup(100));
  ASSERT_EQ(201, Lookup(200));
  ASSERT_EQ(-1,  Lookup(300));

  ASSERT_EQ(1, deleted_keys_.size());
  ASSERT_EQ(100, deleted_keys_[0]);
  ASSERT_EQ(101, deleted_values_[0]);
}

TEST_P(CacheTest, Erase) {
  Erase(200);
  ASSERT_EQ(0, deleted_keys_.size());

  Insert(100, 101);
  Insert(200, 201);
  Erase(100);
  ASSERT_EQ(-1,  Lookup(100));
  ASSERT_EQ(201, Lookup(200));
  ASSERT_EQ(1, deleted_keys_.size());
  ASSERT_EQ(100, deleted_keys_[0]);
  ASSERT_EQ(101, deleted_values_[0]);

  Erase(100);
  ASSERT_EQ(-1,  Lookup(100));
  ASSERT_EQ(201, Lookup(200));
  ASSERT_EQ(1, deleted_keys_.size());
}

TEST_P(CacheTest, EntriesArePinned) {
  Insert(100, 101);
  Cache::Handle* h1 = cache_->Lookup(EncodeKey(100), Cache::EXPECT_IN_CACHE);
  ASSERT_EQ(101, DecodeValue(cache_->Value(h1)));

  Insert(100, 102);
  Cache::Handle* h2 = cache_->Lookup(EncodeKey(100), Cache::EXPECT_IN_CACHE);
  ASSERT_EQ(102, DecodeValue(cache_->Value(h2)));
  ASSERT_EQ(0, deleted_keys_.size());

  cache_->Release(h1);
  ASSERT_EQ(1, deleted_keys_.size());
  ASSERT_EQ(100, deleted_keys_[0]);
  ASSERT_EQ(101, deleted_values_[0]);

  Erase(100);
  ASSERT_EQ(-1, Lookup(100));
  ASSERT_EQ(1, deleted_keys_.size());

  cache_->Release(h2);
  ASSERT_EQ(2, deleted_keys_.size());
  ASSERT_EQ(100, deleted_keys_[1]);
  ASSERT_EQ(102, deleted_values_[1]);
}

TEST_P(CacheTest, EvictionPolicy) {
  Insert(100, 101);
  Insert(200, 201);

  const int kNumElems = 1000;
  const int kSizePerElem = kCacheSize / kNumElems;

  // Frequently used entry must be kept around
  for (int i = 0; i < kNumElems + 100; i++) {
    Insert(1000+i, 2000+i, kSizePerElem);
    ASSERT_EQ(2000+i, Lookup(1000+i));
    ASSERT_EQ(101, Lookup(100));
  }
  ASSERT_EQ(101, Lookup(100));
  ASSERT_EQ(-1, Lookup(200));
}

TEST_P(CacheTest, HeavyEntries) {
  // Add a bunch of light and heavy entries and then count the combined
  // size of items still in the cache, which must be approximately the
  // same as the total capacity.
  const int kLight = kCacheSize/1000;
  const int kHeavy = kCacheSize/100;
  int added = 0;
  int index = 0;
  while (added < 2*kCacheSize) {
    const int weight = (index & 1) ? kLight : kHeavy;
    Insert(index, 1000+index, weight);
    added += weight;
    index++;
  }

  int cached_weight = 0;
  for (int i = 0; i < index; i++) {
    const int weight = (i & 1 ? kLight : kHeavy);
    int r = Lookup(i);
    if (r >= 0) {
      cached_weight += weight;
      ASSERT_EQ(1000+i, r);
    }
  }
  ASSERT_LE(cached_weight, kCacheSize + kCacheSize/10);
}

TEST_P(CacheTest, NewId) {
  uint64_t a = cache_->NewId();
  uint64_t b = cache_->NewId();
  ASSERT_NE(a, b);
}

}  // namespace kudu
