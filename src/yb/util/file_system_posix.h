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

#include <atomic>

#include "yb/util/drive_io_stats_fwd.h"
#include "yb/util/file_system.h"

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

  // Two writable-file classes still exist here (see the unify_env TODO above), so the per-drive
  // accounting below is a second copy of what yb::PosixWritableFile in env_posix.cc carries: that
  // one instruments the Raft WAL, this one the RocksDB SSTs. Keep the two in sync.

  // Per-drive IO counters for the drive this file lives on, resolved once at construction by
  // path prefix, or null when the file is under no registered drive root. Owned by the process-
  // global DriveIoStatsRegistry, so this pointer stays valid for the life of the file.
  yb::DriveIoStats* const drive_stats_;

  // Bytes appended since the last sync of this file, used to walk the drive's approximate
  // unsynced-bytes gauge back down. Atomic because Sync() is documented thread-safe with respect
  // to Append() (see IsSyncThreadSafe()).
  //
  // Deliberately approximate, and the approximation is what keeps it cheap. Sync() zeroes this
  // and then calls fdatasync, so an Append() landing in between is counted as still unsynced even
  // though that fdatasync almost certainly pushed it out. Making the number exact would mean
  // holding a lock across the append and the sync together, i.e. serializing the two operations
  // that IsSyncThreadSafe() exists to let run concurrently. An upper bound is all the gauge
  // claims to be (see the drive_bytes_unsynced description).
  //
  // Accessed with memory_order_relaxed, like the drive counters it feeds. It publishes no other
  // memory, and every update is a read-modify-write on this one variable, so concurrent updates
  // still compose correctly without any barrier.
  std::atomic<uint64_t> unsynced_bytes_{0};

  // Hands whatever this file still holds unsynced back to the drive gauge without counting a
  // sync. Closing without syncing is the normal case, so without this the gauge only climbs.
  void ReleaseUnsyncedBytes();
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
