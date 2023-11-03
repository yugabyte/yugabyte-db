//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#include <unistd.h>
#include "yb/rocksdb/env.h"

// For non linux platform, the following macros are used only as place
// holder.
#if !(defined __linux__) && !(defined CYGWIN)
#define POSIX_FADV_NORMAL 0     /* [MC1] no further special treatment */
#define POSIX_FADV_RANDOM 1     /* [MC1] expect random page refs */
#define POSIX_FADV_SEQUENTIAL 2 /* [MC1] expect sequential page refs */
#define POSIX_FADV_WILLNEED 3   /* [MC1] will need these pages */
#define POSIX_FADV_DONTNEED 4   /* [MC1] dont need these pages */
#endif

namespace rocksdb {

#define STATUS_IO_ERROR(context, err_number) STATUS(IOError, (context), strerror(err_number))

class PosixMmapReadableFile : public RandomAccessFile {
 private:
  int fd_;
  std::string filename_;
  void* mmapped_region_;
  size_t length_;

 public:
  PosixMmapReadableFile(const int fd, const std::string& fname, void* base,
                        size_t length, const EnvOptions& options);
  ~PosixMmapReadableFile();
  Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override;
  Status InvalidateCache(size_t offset, size_t length) override;
  yb::Result<uint64_t> Size() const override;
  yb::Result<uint64_t> INode() const override;
  // Doesn't include memory usage by mmap.
  size_t memory_footprint() const override;
  const std::string& filename() const override { return filename_; }
};

class PosixMmapFile : public WritableFile {
 private:
  std::string filename_;
  int fd_;
  size_t page_size_;
  size_t map_size_;       // How much extra memory to map at a time
  char* base_;            // The mapped region
  char* limit_;           // Limit of the mapped region
  char* dst_;             // Where to write next  (in range [base_,limit_])
  char* last_sync_;       // Where have we synced up to
  uint64_t file_offset_;  // Offset of base_ in file
#ifdef ROCKSDB_FALLOCATE_PRESENT
  bool allow_fallocate_;  // If false, fallocate calls are bypassed
  bool fallocate_with_keep_size_;
#endif

  // Roundup x to a multiple of y
  static size_t Roundup(size_t x, size_t y) { return ((x + y - 1) / y) * y; }

  size_t TruncateToPageBoundary(size_t s) {
    s -= (s & (page_size_ - 1));
    assert((s % page_size_) == 0);
    return s;
  }

  Status MapNewRegion();
  Status UnmapCurrentRegion();
  Status Msync();

  const std::string& filename() const override { return filename_; }

 public:
  PosixMmapFile(const std::string& fname, int fd, size_t page_size,
                const EnvOptions& options);
  ~PosixMmapFile();

  // Means Close() will properly take care of truncate
  // and it does not need any additional information
  Status Truncate(uint64_t size) override;
  Status Close() override;
  Status Append(const Slice& data) override;
  Status Flush() override;
  Status Sync() override;
  Status Fsync() override;
  uint64_t GetFileSize() override;
  Status InvalidateCache(size_t offset, size_t length) override;
#ifdef ROCKSDB_FALLOCATE_PRESENT
  Status Allocate(uint64_t offset, uint64_t len) override;
#endif
};

class PosixDirectory : public Directory {
 public:
  explicit PosixDirectory(int fd) : fd_(fd) {}
  ~PosixDirectory();
  virtual Status Fsync() override;

 private:
  int fd_;
};

}  // namespace rocksdb
