// Copyright (c) YugaByte, Inc.
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

#include <vector>

#include "yb/util/file_system.h"
#include "yb/util/malloc.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

namespace yb {

class InMemoryFileState {
 public:
  explicit InMemoryFileState(std::string filename) : filename_(std::move(filename)), size_(0) {}

  ~InMemoryFileState() {
    for (uint8_t* block : blocks_) {
      delete[] block;
    }
  }

  InMemoryFileState(const InMemoryFileState&) = delete;
  void operator=(const InMemoryFileState&) = delete;

  uint64_t Size() const { return size_; }

  Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const;

  Status PreAllocate(uint64_t size);

  Status Append(const Slice& data);

  Status AppendRaw(const uint8_t *src, size_t src_len);

  const std::string& filename() const { return filename_; }

  size_t memory_footprint() const;

 private:
  static constexpr const size_t kBlockSize = 8_KB;

  const std::string filename_;

  // The following fields are not protected by any mutex. They are only mutable
  // while the file is being written, and concurrent access is not allowed
  // to writable files.
  std::vector<uint8_t*> blocks_;
  uint64_t size_;
};

class InMemorySequentialFile : public SequentialFile {
 public:
  explicit InMemorySequentialFile(std::shared_ptr<InMemoryFileState> file)
    : file_(std::move(file)), pos_(0) {}

  ~InMemorySequentialFile() {}

  Status Read(size_t n, Slice* result, uint8_t* scratch) override;

  Status Skip(uint64_t n) override;

  const std::string& filename() const override {
    return file_->filename();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
  size_t pos_;
};

class InMemoryRandomAccessFile : public RandomAccessFile {
 public:
  explicit InMemoryRandomAccessFile(std::shared_ptr<InMemoryFileState> file)
    : file_(std::move(file)) {}

  ~InMemoryRandomAccessFile() {}

  Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override;

  Result<uint64_t> Size() const override;

  Result<uint64_t> INode() const override;

  const std::string& filename() const override {
    return file_->filename();
  }

  size_t memory_footprint() const override {
    // The FileState is actually shared between multiple files, but the double
    // counting doesn't matter much since MemEnv is only used in tests.
    return malloc_usable_size(this) + file_->memory_footprint();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
};

} // namespace yb
