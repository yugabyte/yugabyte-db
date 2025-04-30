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

class FileBlockCache {
 public:
  explicit FileBlockCache(
      std::unique_ptr<RandomAccessFile> file, FileBlockCacheBuilder* builder = nullptr);
  ~FileBlockCache();

  Result<Header> Load();

  const std::byte* Data(size_t index) {
    return blocks_[index].content.data();
  }

 private:
  std::unique_ptr<RandomAccessFile> file_;
  struct BlockInfo {
    size_t end;
    DataBlock content;
  };
  std::vector<BlockInfo> blocks_;
};

class BlockCache {
 public:
  explicit BlockCache(Env& env) : env_(env) {}

  void Register(FileBlockCachePtr&& file_block_cache);

  Env& env() const {
    return env_;
  }

 private:
  Env& env_;
  std::mutex mutex_;
  std::vector<FileBlockCachePtr> files_ GUARDED_BY(mutex_);
};

Status WriteFooter();

} // namespace yb::hnsw
