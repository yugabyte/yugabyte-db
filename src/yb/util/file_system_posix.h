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

#include "yb/util/file_system.h"

struct iovec;

namespace yb {

#if defined(__linux__)
size_t GetUniqueIdFromFile(int fd, uint8_t* id);
#endif // __linux__

class PosixSequentialFile : public SequentialFile {
 public:
#if defined(__APPLE__)
  // On macOS, use raw fd to avoid the ~32K stdio FILE* stream limit (SHRT_MAX in Apple's libc).
  PosixSequentialFile(const std::string& fname, int fd, const FileSystemOptions& options);
#else
  PosixSequentialFile(const std::string& fname, FILE* f, const FileSystemOptions& options);
#endif
  virtual ~PosixSequentialFile();

  Status Read(size_t n, Slice* result, uint8_t* scratch) override;
  Status Skip(uint64_t n) override;
  Status InvalidateCache(size_t offset, size_t length) override;

  const std::string& filename() const override { return filename_; }

 private:
  std::string filename_;
#if !defined(__APPLE__)
  FILE* file_;
#endif
  int fd_;
  bool use_os_buffer_;
};

// Identifies a locked file on the POSIX filesystem.
class PosixFileLock : public FileLock {
 public:
  int fd_;
  std::string filename;
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

// fdatasync (or fsync, when FLAGS_writable_file_use_fsync is set) the given fd.
Status DoSync(int fd, const std::string& filename);

// Get logical on-disk file size using file descriptor.
Result<uint64_t> GetLogicalFileSize(int fd, const std::string& filename);

class PosixWritableFile : public WritableFile {
 public:
  PosixWritableFile(const std::string& fname, int fd, const FileSystemOptions& options,
                    uint64_t initial_size = 0, bool sync_on_close = false);

  // Convenience constructor for callers that don't carry FileSystemOptions.
  PosixWritableFile(const std::string& fname, int fd, uint64_t initial_size, bool sync_on_close)
      : PosixWritableFile(fname, fd, FileSystemOptions::kDefault, initial_size, sync_on_close) {}

  ~PosixWritableFile();

  Status Append(const Slice& data) override;
  Status AppendSlices(const Slice* slices, size_t num) override;
  Status PreAllocate(uint64_t size) override;
  Status Truncate(uint64_t size) override;
  Status Close() override;
  Status Flush(FlushMode mode) override;
  Status Sync() override;
  Status Fsync() override;
  bool IsSyncThreadSafe() const override;
  uint64_t Size() const override { return filesize_; }
  Result<uint64_t> SizeOnDisk() const override;
  Status InvalidateCache(size_t offset, size_t length) override;
#ifdef ROCKSDB_FALLOCATE_PRESENT
  size_t GetUniqueId(char* id) const override;
#endif
  const std::string& filename() const override { return filename_; }

 protected:
  Status DoWritev(const Slice* slices, size_t n);
  static void UnwrittenRemaining(
      struct iovec** remaining_iov, ssize_t written, int* remaining_count);

#ifdef ROCKSDB_FALLOCATE_PRESENT
  Status Allocate(uint64_t offset, uint64_t len) override;
  Status RangeSync(uint64_t offset, uint64_t nbytes) override;
#endif

  const std::string filename_;
  int fd_;
  bool sync_on_close_;
  uint64_t filesize_;
  uint64_t pre_allocated_size_;
  std::atomic<bool> pending_sync_;
#ifdef ROCKSDB_FALLOCATE_PRESENT
  bool allow_fallocate_;
  bool fallocate_with_keep_size_;
#endif
};

} // namespace yb
