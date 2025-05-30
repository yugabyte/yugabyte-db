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

#include "yb/hnsw/types.h"

#include "yb/util/env.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/mem_tracker.h"

namespace yb::hnsw {

class FileBlockCacheBuilder {
 public:
  void Add(DataBlock&& block) {
    blocks_.push_back(std::move(block));
  }

  DataBlock MakeFooter(const Header& header) const;

  std::vector<DataBlock>& blocks() {
    return blocks_;
  }

 private:
  std::vector<DataBlock> blocks_;
};

class BlockCacheShard;
struct CachedBlock;

struct BlockCacheMetrics {
  explicit BlockCacheMetrics(const MetricEntityPtr& entity);

  CounterPtr hit;
  CounterPtr query;
  CounterPtr read;
  CounterPtr add;
  CounterPtr evict;
  CounterPtr remove;
};

class FileBlockCache {
 public:
  FileBlockCache(
      BlockCache& block_cache, std::unique_ptr<RandomAccessFile> file,
      FileBlockCacheBuilder* builder = nullptr);
  ~FileBlockCache();

  Result<Header> Load();

  size_t size() const {
    return size_;
  }

  Result<const std::byte*> Take(size_t index);
  void Release(size_t index);

 private:
  void AllocateBlocks(size_t size);

  BlockCache& block_cache_;
  std::unique_ptr<RandomAccessFile> file_;
  std::unique_ptr<CachedBlock[]> blocks_;
  size_t size_ = 0;
};

class BlockCache {
 public:
  BlockCache(
      Env& env, const MemTrackerPtr& mem_tracker, const MetricEntityPtr& metric_entity,
      size_t capacity, size_t num_shard_bits);
  ~BlockCache();

  BlockCacheShard& NextShard();

  Env& env() const {
    return env_;
  }

  const MemTrackerPtr& mem_tracker() const {
    return mem_tracker_;
  }

  BlockCacheMetrics& metrics() const {
    return *metrics_;
  }

 private:
  Env& env_;
  const MemTrackerPtr mem_tracker_;
  std::unique_ptr<BlockCacheMetrics> metrics_;
  const size_t shards_mask_;
  std::atomic<size_t> next_shard_ = 0;
  std::unique_ptr<BlockCacheShard[]> shards_;
};

Status WriteFooter();

} // namespace yb::hnsw
