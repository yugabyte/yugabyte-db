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

#include "yb/util/file_system.h"

namespace yb {

#if defined(__linux__)
size_t GetUniqueIdFromFile(int fd, uint8_t* id);
#endif // __linux__

class PosixSequentialFile : public SequentialFile {
 public:
  PosixSequentialFile(const std::string& fname, FILE* f, const FileSystemOptions& options);
  virtual ~PosixSequentialFile();

  Status Read(size_t n, Slice* result, uint8_t* scratch) override;
  Status Skip(uint64_t n) override;
  Status InvalidateCache(size_t offset, size_t length) override;

  const std::string& filename() const override { return filename_; }

 private:
  std::string filename_;
  FILE* file_;
  int fd_;
  bool use_os_buffer_;
};

// pread() based random-access file.
class PosixRandomAccessFile : public RandomAccessFile {
 public:
  PosixRandomAccessFile(const std::string& fname, int fd,
                        const FileSystemOptions& options);
  virtual ~PosixRandomAccessFile();

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      uint8_t* scratch) const override;

  Result<uint64_t> Size() const override;

  Result<uint64_t> INode() const override;

  const std::string& filename() const override { return filename_; }

  size_t memory_footprint() const override;

#ifdef __linux__
  virtual size_t GetUniqueId(char* id) const override;
#endif
  void Hint(AccessPattern pattern) override;
  Status InvalidateCache(size_t offset, size_t length) override;

  void Readahead(size_t offset, size_t length) override;

 private:
  std::string filename_;
  int fd_;
  bool use_os_buffer_;
};

} // namespace yb

namespace rocksdb {

// TODO(unify_env): remove `using` statement once filesystem classes are fully merged into yb
// namespace:
using yb::FileSystemOptions;

class PosixWritableFile : public WritableFile {
 private:
  const std::string filename_;
  int fd_;
  uint64_t filesize_;
#ifdef ROCKSDB_FALLOCATE_PRESENT
  bool allow_fallocate_;
  bool fallocate_with_keep_size_;
#endif

 public:
  PosixWritableFile(const std::string& fname, int fd,
                    const FileSystemOptions& options);
  ~PosixWritableFile();

  // Means Close() will properly take care of truncate
  // and it does not need any additional information
  Status Truncate(uint64_t size) override;
  Status Close() override;
  Status Append(const Slice& data) override;
  Status Flush() override;
  Status Sync() override;
  Status Fsync() override;
  uint64_t GetFileSize() override;
  bool IsSyncThreadSafe() const override;
  Status InvalidateCache(size_t offset, size_t length) override;
#ifdef ROCKSDB_FALLOCATE_PRESENT
  Status Allocate(uint64_t offset, uint64_t len) override;
  Status RangeSync(uint64_t offset, uint64_t nbytes) override;
  size_t GetUniqueId(char* id) const override;
#endif
  const std::string& filename() const override { return filename_; }
};

} // namespace rocksdb
