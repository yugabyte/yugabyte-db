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

#include "yb/util/file_system_posix.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __linux__
#include <linux/fs.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#endif // __linux__

#include "yb/util/coding.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/errno.h"
#include "yb/util/flag_tags.h"
#include "yb/util/malloc.h"
#include "yb/util/result.h"
#include "yb/util/stats/iostats_context_imp.h"
#include "yb/util/test_kill.h"
#include "yb/util/thread_restrictions.h"


// For platforms without fdatasync (like OS X)
#ifndef fdatasync
#define fdatasync fsync
#endif

// For platforms without unlocked_stdio (like OS X)
#ifndef fread_unlocked
#define fread_unlocked fread
#endif

// For non linux platform, the following macros are used only as place
// holder.
#if !(defined __linux__) && !(defined CYGWIN)
#define POSIX_FADV_NORMAL 0     /* [MC1] no further special treatment */
#define POSIX_FADV_RANDOM 1     /* [MC1] expect random page refs */
#define POSIX_FADV_SEQUENTIAL 2 /* [MC1] expect sequential page refs */
#define POSIX_FADV_WILLNEED 3   /* [MC1] will need these pages */
#define POSIX_FADV_DONTNEED 4   /* [MC1] dont need these pages */
#endif

#ifdef __linux__
#ifndef FALLOC_FL_KEEP_SIZE
#include <linux/falloc.h>
#endif
#endif // __linux__

DEFINE_RUNTIME_uint64(rocksdb_check_sst_file_tail_for_zeros, 0,
    "Size of just written SST data file tail to be checked for being zeros. Check is not performed "
    "if flag value is zero.");
TAG_FLAG(rocksdb_check_sst_file_tail_for_zeros, advanced);

DECLARE_bool(never_fsync);

namespace {

// A wrapper for fadvise, if the platform doesn't support fadvise, it will simply return
// Status::NotSupport.
int Fadvise(int fd, off_t offset, size_t len, int advice) {
#ifdef __linux__
  return posix_fadvise(fd, offset, len, advice);
#else
  return 0;  // simply do nothing.
#endif
}

#define STATUS_IO_ERROR(context, err_number) \
    STATUS_FROM_ERRNO_SPECIAL_EIO_HANDLING(context, err_number)

} // namespace

namespace yb {

#if defined(__linux__)
size_t GetUniqueIdFromFile(int fd, uint8_t* id) {
  struct stat buf;
  int result = fstat(fd, &buf);
  if (result == -1) {
    return 0;
  }

  int version = 0;
  result = ioctl(fd, FS_IOC_GETVERSION, &version);
  if (result == -1) {
    return 0;
  }

  uint8_t* rid = id;
  rid = EncodeVarint64(rid, buf.st_dev);
  rid = EncodeVarint64(rid, buf.st_ino);
  rid = EncodeVarint64(rid, version);
  DCHECK_GE(rid, id);
  return rid - id;
}
#endif // __linux__

PosixSequentialFile::PosixSequentialFile(const std::string& fname, FILE* f,
                                         const FileSystemOptions& options)
    : filename_(fname),
      file_(f),
      fd_(fileno(f)),
      use_os_buffer_(options.use_os_buffer) {}

PosixSequentialFile::~PosixSequentialFile() { fclose(file_); }

Status PosixSequentialFile::Read(size_t n, Slice* result, uint8_t* scratch) {
  ThreadRestrictions::AssertIOAllowed();
  Status s;
  size_t r = 0;
  do {
    r = fread_unlocked(scratch, 1, n, file_);
  } while (r == 0 && ferror(file_) && errno == EINTR);
  *result = Slice(scratch, r);
  if (r < n) {
    if (feof(file_)) {
      // We leave status as ok if we hit the end of the file
      // We also clear the error so that the reads can continue
      // if a new data is written to the file
      clearerr(file_);
    } else {
      // A partial read with an error: return a non-ok status
      s = STATUS_IO_ERROR(filename_, errno);
    }
  }
  if (!use_os_buffer_) {
    // We need to fadvise away the entire range of pages because we do not want readahead pages to
    // be cached.
    Fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);  // free OS pages
  }
  return s;
}

Status PosixSequentialFile::Skip(uint64_t n) {
  TRACE_EVENT1("io", "PosixSequentialFile::Skip", "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  if (fseek(file_, static_cast<long>(n), SEEK_CUR)) { // NOLINT
    return STATUS_IO_ERROR(filename_, errno);
  }
  return Status::OK();
}

Status PosixSequentialFile::InvalidateCache(size_t offset, size_t length) {
#ifndef __linux__
  return Status::OK();
#else
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0) {
    return Status::OK();
  }
  return STATUS_IO_ERROR(filename_, errno);
#endif
}

PosixRandomAccessFile::PosixRandomAccessFile(const std::string& fname, int fd,
                                             const FileSystemOptions& options)
    : filename_(fname), fd_(fd), use_os_buffer_(options.use_os_buffer) {
  assert(!options.use_mmap_reads || sizeof(void*) < 8);
}

PosixRandomAccessFile::~PosixRandomAccessFile() { close(fd_); }

Status PosixRandomAccessFile::Read(uint64_t offset, size_t n, Slice* result,
                                   uint8_t* scratch) const {
  ThreadRestrictions::AssertIOAllowed();
  Status s;
  ssize_t r = -1;
  size_t left = n;
  uint8_t* ptr = scratch;
  while (left > 0) {
    r = pread(fd_, ptr, left, static_cast<off_t>(offset));

    if (r <= 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    ptr += r;
    offset += r;
    left -= r;
  }

  *result = Slice(scratch, (r < 0) ? 0 : n - left);
  if (r < 0) {
    // An error: return a non-ok status
    s = STATUS_IO_ERROR(filename_, errno);
  }
  if (!use_os_buffer_) {
    // we need to fadvise away the entire range of pages because
    // we do not want readahead pages to be cached.
    Fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);  // free OS pages
  }
  return s;
}

Result<uint64_t> PosixRandomAccessFile::Size() const {
  TRACE_EVENT1("io", __PRETTY_FUNCTION__, "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  struct stat st;
  if (fstat(fd_, &st) == -1) {
    return STATUS_IO_ERROR(filename_, errno);
  }
  return st.st_size;
}

Result<uint64_t> PosixRandomAccessFile::INode() const {
  TRACE_EVENT1("io", __PRETTY_FUNCTION__, "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  struct stat st;
  if (fstat(fd_, &st) == -1) {
    return STATUS_IO_ERROR(filename_, errno);
  }
  return st.st_ino;
}

size_t PosixRandomAccessFile::memory_footprint() const {
  return malloc_usable_size(this) + filename_.capacity();
}

#ifdef __linux__
size_t PosixRandomAccessFile::GetUniqueId(char* id) const {
  return GetUniqueIdFromFile(fd_, pointer_cast<uint8_t*>(id));
}
#endif

void PosixRandomAccessFile::Hint(AccessPattern pattern) {
  switch (pattern) {
    case NORMAL:
      Fadvise(fd_, 0, 0, POSIX_FADV_NORMAL);
      break;
    case RANDOM:
      Fadvise(fd_, 0, 0, POSIX_FADV_RANDOM);
      break;
    case SEQUENTIAL:
      Fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
      break;
    case WILLNEED:
      Fadvise(fd_, 0, 0, POSIX_FADV_WILLNEED);
      break;
    case DONTNEED:
      Fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
      break;
    default:
      assert(false);
      break;
  }
}

Status PosixRandomAccessFile::InvalidateCache(size_t offset, size_t length) {
#ifndef __linux__
  return Status::OK();
#else
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0) {
    return Status::OK();
  }
  return STATUS_IO_ERROR(filename_, errno);
#endif
}

} // namespace yb

namespace rocksdb {

PosixWritableFile::PosixWritableFile(const std::string& fname, int fd,
                                     const FileSystemOptions& options)
    : filename_(fname), fd_(fd), filesize_(0) {
#ifdef ROCKSDB_FALLOCATE_PRESENT
  allow_fallocate_ = options.allow_fallocate;
  fallocate_with_keep_size_ = options.fallocate_with_keep_size;
#endif
  assert(!options.use_mmap_writes);
}

PosixWritableFile::~PosixWritableFile() {
  if (fd_ >= 0) {
    WARN_NOT_OK(PosixWritableFile::Close(), "Failed to close posix writable file");
  }
}

Status PosixWritableFile::Append(const Slice& data) {
  const char* src = data.cdata();
  size_t left = data.size();
  while (left != 0) {
    ssize_t done = write(fd_, src, left);
    if (done < 0) {
      if (errno == EINTR) {
        continue;
      }
      return STATUS_IO_ERROR(filename_, errno);
    }
    left -= done;
    src += done;
  }
  filesize_ += data.size();
  return Status::OK();
}

Status PosixWritableFile::Truncate(uint64_t size) {
  return Status::OK();
}

Status PosixWritableFile::Close() {
  Status s;

  size_t block_size;
  size_t last_allocated_block;
  GetPreallocationStatus(&block_size, &last_allocated_block);
  if (last_allocated_block > 0) {
    // trim the extra space preallocated at the end of the file
    // NOTE(ljin): we probably don't want to surface failure as an IOError,
    // but it will be nice to log these errors.
    if (FLAGS_rocksdb_check_sst_file_tail_for_zeros > 0) {
      LOG(INFO) << filename_ << " block_size: " << block_size
                << " last_allocated_block: " << last_allocated_block
#ifdef ROCKSDB_FALLOCATE_PRESENT
                << " allow_fallocate_: " << allow_fallocate_
#endif  // ROCKSDB_FALLOCATE_PRESENT
                << " filesize_: " << filesize_;
    }
    if (ftruncate(fd_, filesize_) != 0) {
      LOG(ERROR) << STATUS_IO_ERROR(filename_, errno) << " filesize_: " << filesize_;
    }
#ifdef ROCKSDB_FALLOCATE_PRESENT
    // in some file systems, ftruncate only trims trailing space if the
    // new file size is smaller than the current size. Calling fallocate
    // with FALLOC_FL_PUNCH_HOLE flag to explicitly release these unused
    // blocks. FALLOC_FL_PUNCH_HOLE is supported on at least the following
    // filesystems:
    //   XFS (since Linux 2.6.38)
    //   ext4 (since Linux 3.0)
    //   Btrfs (since Linux 3.7)
    //   tmpfs (since Linux 3.5)
    // We ignore error since failure of this operation does not affect
    // correctness.
    IOSTATS_TIMER_GUARD(allocate_nanos);
    if (allow_fallocate_) {
      if (fallocate(
              fd_, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, filesize_,
              block_size * last_allocated_block - filesize_) != 0) {
        LOG(ERROR) << STATUS_IO_ERROR(filename_, errno) << " block_size: " << block_size
                   << " last_allocated_block: " << last_allocated_block
                   << " filesize_: " << filesize_;
      }
    }
#endif
  }

  if (close(fd_) < 0) {
    s = STATUS_IO_ERROR(filename_, errno);
  }
  fd_ = -1;
  return s;
}

// write out the cached data to the OS cache
Status PosixWritableFile::Flush() { return Status::OK(); }

Status PosixWritableFile::Sync() {
  if (fdatasync(fd_) < 0) {
    return STATUS_IO_ERROR(filename_, errno);
  }
  return Status::OK();
}

Status PosixWritableFile::Fsync() {
  if (FLAGS_never_fsync) {
    return Status::OK();
  }
  if (fsync(fd_) < 0) {
    return STATUS_IO_ERROR(filename_, errno);
  }
  return Status::OK();
}

bool PosixWritableFile::IsSyncThreadSafe() const { return true; }

uint64_t PosixWritableFile::GetFileSize() { return filesize_; }

Status PosixWritableFile::InvalidateCache(size_t offset, size_t length) {
#ifndef __linux__
  return Status::OK();
#else
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0) {
    return Status::OK();
  }
  return STATUS_IO_ERROR(filename_, errno);
#endif
}

#ifdef ROCKSDB_FALLOCATE_PRESENT
Status PosixWritableFile::Allocate(uint64_t offset, uint64_t len) {
  assert(yb::std_util::cmp_less_equal(offset, std::numeric_limits<off_t>::max()));
  assert(yb::std_util::cmp_less_equal(len, std::numeric_limits<off_t>::max()));
  TEST_KILL_RANDOM("PosixWritableFile::Allocate:0", test_kill_odds);
  IOSTATS_TIMER_GUARD(allocate_nanos);
  int alloc_status = 0;
  if (allow_fallocate_) {
    alloc_status = fallocate(
        fd_, fallocate_with_keep_size_ ? FALLOC_FL_KEEP_SIZE : 0,
        static_cast<off_t>(offset), static_cast<off_t>(len));
  }
  if (alloc_status == 0) {
    return Status::OK();
  } else {
    return STATUS_IO_ERROR(filename_, errno);
  }
}

Status PosixWritableFile::RangeSync(uint64_t offset, uint64_t nbytes) {
  assert(yb::std_util::cmp_less_equal(offset, std::numeric_limits<off_t>::max()));
  assert(yb::std_util::cmp_less_equal(nbytes, std::numeric_limits<off_t>::max()));
  if (sync_file_range(fd_, static_cast<off_t>(offset),
      static_cast<off_t>(nbytes), SYNC_FILE_RANGE_WRITE) == 0) {
    return Status::OK();
  } else {
    return STATUS_IO_ERROR(filename_, errno);
  }
}

size_t PosixWritableFile::GetUniqueId(char* id) const {
  return yb::GetUniqueIdFromFile(fd_, pointer_cast<uint8_t*>(id));
}
#endif

} // namespace rocksdb
