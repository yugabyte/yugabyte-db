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

#include "yb/util/file_system_posix.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __linux__
#include <linux/fs.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#endif // __linux__

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/util/coding-inl.h"
#include "yb/util/coding.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/errno.h"
#include "yb/util/logging.h"
#include "yb/util/malloc.h"
#include "yb/util/result.h"
#include "yb/util/stack_trace_tracker.h"
#include "yb/util/stats/iostats_context_imp.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
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
DECLARE_bool(writable_file_use_fsync);
DECLARE_bool(TEST_simulate_fs_without_fallocate);
DECLARE_int64(TEST_simulate_free_space_bytes);
DECLARE_bool(TEST_skip_file_close);

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

#if defined(__APPLE__)
// Simulates Linux's fallocate(mode=0) file preallocation on OS X. Only mode=0 is supported.
int fallocate(int fd, int mode, off_t offset, off_t len) {
  CHECK_EQ(mode, 0);
  off_t size = offset + len;

  struct stat stat;
  int ret = fstat(fd, &stat);
  if (ret < 0) {
    return ret;
  }

  if (stat.st_blocks * 512 < size) {
    auto store = fstore_t{
        .fst_flags = F_ALLOCATECONTIG,
        .fst_posmode = F_PEOFPOSMODE,
        .fst_offset = 0,
        .fst_length = size,
        .fst_bytesalloc = 0};
    if (fcntl(fd, F_PREALLOCATE, &store) < 0) {
      store.fst_flags = F_ALLOCATEALL;
      ret = fcntl(fd, F_PREALLOCATE, &store);
      if (ret < 0) {
        return ret;
      }
    }
  }

  if (stat.st_size < size) {
    return ftruncate(fd, size);
  }
  return 0;
}
#endif

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
  InlineEncodeFixed32(rid, static_cast<uint32_t>(buf.st_mtime));
  rid += sizeof(uint32_t);
  DCHECK_GE(rid, id);
  return rid - id;
}
#endif // __linux__

#if defined(__APPLE__)
// On macOS, use raw fd I/O to avoid the ~32K stdio FILE* stream limit (SHRT_MAX in Apple's libc).
PosixSequentialFile::PosixSequentialFile(const std::string& fname, int fd,
                                         const FileSystemOptions& options)
    : filename_(fname),
      fd_(fd),
      use_os_buffer_(options.use_os_buffer) {}

PosixSequentialFile::~PosixSequentialFile() { close(fd_); }

Status PosixSequentialFile::Read(size_t n, Slice* result, uint8_t* scratch) {
  ThreadRestrictions::AssertIOAllowed();
  Status s;
  size_t total_read = 0;
  while (total_read < n) {
    ssize_t r = read(fd_, scratch + total_read, n - total_read);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      s = STATUS_IO_ERROR(filename_, errno);
      break;
    }
    if (r == 0) {
      // EOF
      break;
    }
    TrackStackTrace(StackTraceTrackingGroup::kReadIO, r);
    total_read += r;
  }
  *result = Slice(scratch, total_read);
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
  if (lseek(fd_, static_cast<off_t>(n), SEEK_CUR) == static_cast<off_t>(-1)) {
    return STATUS_IO_ERROR(filename_, errno);
  }
  return Status::OK();
}
#else
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
  TrackStackTrace(StackTraceTrackingGroup::kReadIO, r);
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
#endif // defined(__APPLE__)

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
    TrackStackTrace(StackTraceTrackingGroup::kReadIO, r);
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

void PosixRandomAccessFile::Readahead(size_t offset, size_t length) {
#ifdef __linux__
  auto ret = readahead(fd_, implicit_cast<off64_t>(offset), length);
  if (ret == 0) {
    return;
  }
  YB_LOG_EVERY_N_SECS(ERROR, 60) << "Readahead error for " << filename_ << " at " << offset
                                 << ", length=" << length << ": " << ErrnoToString(errno);
#endif
}

Status DoSync(int fd, const std::string& filename) {
  ThreadRestrictions::AssertIOAllowed();
  if (FLAGS_never_fsync) {
    return Status::OK();
  }
  if (FLAGS_writable_file_use_fsync) {
    if (fsync(fd) < 0) {
      return STATUS_IO_ERROR(filename, errno);
    }
  } else {
    if (fdatasync(fd) < 0) {
      return STATUS_IO_ERROR(filename, errno);
    }
  }
  return Status::OK();
}

Result<uint64_t> GetLogicalFileSize(int fd, const std::string& filename) {
  ThreadRestrictions::AssertIOAllowed();
  struct stat st;
  if (fstat(fd, &st) == -1) {
    return STATUS_IO_ERROR(filename, errno);
  }
  return st.st_size;
}

PosixWritableFile::PosixWritableFile(const std::string& fname, int fd,
                                     const FileSystemOptions& options,
                                     uint64_t initial_size, bool sync_on_close)
    : filename_(fname),
      fd_(fd),
      sync_on_close_(sync_on_close),
      filesize_(initial_size),
      pre_allocated_size_(0),
      pending_sync_(false) {
#ifdef ROCKSDB_FALLOCATE_PRESENT
  allow_fallocate_ = options.allow_fallocate;
  fallocate_with_keep_size_ = options.fallocate_with_keep_size;
#endif
}

PosixWritableFile::~PosixWritableFile() {
  if (fd_ >= 0) {
    WARN_NOT_OK(PosixWritableFile::Close(), "Failed to close " + filename_);
  }
}

Status PosixWritableFile::Append(const Slice& data) {
  return AppendSlices(&data, 1);
}

Status PosixWritableFile::AppendSlices(const Slice* slices, size_t num) {
  ThreadRestrictions::AssertIOAllowed();
  static const size_t kIovMaxElements = IOV_MAX;

  Status s;
  for (size_t i = 0; i < num && s.ok(); i += kIovMaxElements) {
    size_t n = std::min(num - i, kIovMaxElements);
    s = DoWritev(slices + i, n);
  }

  pending_sync_ = true;
  return s;
}

Status PosixWritableFile::PreAllocate(uint64_t size) {
  TRACE_EVENT1("io", "PosixWritableFile::PreAllocate", "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  if (PREDICT_FALSE(FLAGS_TEST_simulate_fs_without_fallocate)) {
    YB_LOG_FIRST_N(WARNING, 1) << "Simulating a filesystem without fallocate() support";
    return Status::OK();
  }
  if (PREDICT_FALSE(FLAGS_TEST_simulate_free_space_bytes >= 0 &&
                    static_cast<uint64_t>(FLAGS_TEST_simulate_free_space_bytes) < size)) {
    return STATUS_IO_ERROR(filename_, ENOSPC);
  }
  const uint64_t offset = std::max(filesize_, pre_allocated_size_);
  if (fallocate(fd_, 0, offset, size) < 0) {
    if (errno == EOPNOTSUPP) {
      YB_LOG_FIRST_N(WARNING, 1) << "The filesystem does not support fallocate().";
    } else if (errno == ENOSYS) {
      YB_LOG_FIRST_N(WARNING, 1) << "The kernel does not implement fallocate().";
    } else {
      return STATUS_IO_ERROR(filename_, errno);
    }
    return Status::OK();
  }
  pre_allocated_size_ = offset + size;
  return Status::OK();
}

Status PosixWritableFile::Truncate(uint64_t size) {
  return Status::OK();
}

Status PosixWritableFile::Close() {
  TRACE_EVENT1("io", "PosixWritableFile::Close", "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  if (FLAGS_TEST_skip_file_close) {
    return Status::OK();
  }
  Status s;

  // If we've allocated more space than we used, truncate to the actual size of the file.
  off_t size_on_disk = lseek(fd_, 0, SEEK_END);
  if (size_on_disk < 0) {
    return STATUS_IO_ERROR(filename_, errno);
  }
  if (filesize_ < static_cast<uint64_t>(size_on_disk)) {
    if (ftruncate(fd_, filesize_) < 0) {
      s = STATUS_IO_ERROR(filename_, errno);
      pending_sync_ = true;
    }
  }

  // Punch the unused tail of any block-level preallocation done via PrepareWrite/Allocate.
#ifdef ROCKSDB_FALLOCATE_PRESENT
  size_t block_size;
  size_t last_allocated_block;
  GetPreallocationStatus(&block_size, &last_allocated_block);
  if (last_allocated_block > 0 && allow_fallocate_ &&
      block_size * last_allocated_block > filesize_) {
    if (FLAGS_rocksdb_check_sst_file_tail_for_zeros > 0) {
      LOG(INFO) << filename_ << " block_size: " << block_size
                << " last_allocated_block: " << last_allocated_block
                << " filesize_: " << filesize_;
    }
    IOSTATS_TIMER_GUARD(allocate_nanos);
    if (fallocate(
            fd_, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE, filesize_,
            block_size * last_allocated_block - filesize_) != 0) {
      LOG(WARNING) << STATUS_IO_ERROR(filename_, errno) << " block_size: " << block_size
                   << " last_allocated_block: " << last_allocated_block
                   << " filesize_: " << filesize_;
    }
  }
#endif

  if (sync_on_close_) {
    Status sync_status = Sync();
    if (!sync_status.ok()) {
      LOG(WARNING) << "Unable to Sync " << filename_ << ": " << sync_status;
      if (s.ok()) {
        s = sync_status;
      }
    }
  }

  if (close(fd_) < 0) {
    if (s.ok()) {
      s = STATUS_IO_ERROR(filename_, errno);
    }
  }
  fd_ = -1;
  return s;
}

Status PosixWritableFile::Flush(FlushMode mode) {
  TRACE_EVENT1("io", "PosixWritableFile::Flush", "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  if (FLAGS_never_fsync) {
    return Status::OK();
  }
#if defined(__linux__)
  int flags = SYNC_FILE_RANGE_WRITE;
  if (mode == FLUSH_SYNC) {
    flags |= SYNC_FILE_RANGE_WAIT_AFTER;
  }
  if (sync_file_range(fd_, 0, 0, flags) < 0) {
    return STATUS_IO_ERROR(filename_, errno);
  }
#else
  if (fsync(fd_) < 0) {
    return STATUS_IO_ERROR(filename_, errno);
  }
#endif
  return Status::OK();
}

Status PosixWritableFile::Sync() {
  TRACE_EVENT1("io", "PosixWritableFile::Sync", "path", filename_);
  ThreadRestrictions::AssertIOAllowed();
  LOG_SLOW_EXECUTION(WARNING, 1000, strings::Substitute("sync call for $0", filename_)) {
    if (pending_sync_) {
      pending_sync_ = false;
      RETURN_NOT_OK(DoSync(fd_, filename_));
    }
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
  pending_sync_ = false;
  return Status::OK();
}

bool PosixWritableFile::IsSyncThreadSafe() const { return true; }

Result<uint64_t> PosixWritableFile::SizeOnDisk() const {
  TRACE_EVENT1("io", "PosixWritableFile::SizeOnDisk", "path", filename_);
  return GetLogicalFileSize(fd_, filename_);
}

Status PosixWritableFile::InvalidateCache(size_t offset, size_t length) {
#ifndef __linux__
  return Status::OK();
#else
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0) {
    return Status::OK();
  }
  return STATUS_IO_ERROR(filename_, errno);
#endif
}

Status PosixWritableFile::DoWritev(const Slice* slices, size_t n) {
  ThreadRestrictions::AssertIOAllowed();
  DCHECK_LE(n, IOV_MAX);

  struct iovec iov[n];
  ssize_t nbytes = 0;

  for (size_t i = 0; i < n; ++i) {
    const Slice& data = slices[i];
    iov[i].iov_base = const_cast<uint8_t*>(data.data());
    iov[i].iov_len = data.size();
    nbytes += data.size();
  }

  struct iovec* remaining_iov = iov;
  int remaining_count = static_cast<int>(n);
  ssize_t total_written = 0;

  while (remaining_count > 0) {
    ssize_t written = writev(fd_, remaining_iov, remaining_count);
    if (PREDICT_FALSE(written == -1)) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return STATUS_IO_ERROR(filename_, errno);
    }

    TrackStackTrace(StackTraceTrackingGroup::kWriteIO, written);
    UnwrittenRemaining(&remaining_iov, written, &remaining_count);
    total_written += written;
  }

  filesize_ += total_written;

  if (PREDICT_FALSE(total_written != nbytes)) {
    return STATUS_FORMAT(
        IOError, "writev error: expected to write $0 bytes, wrote $1 bytes instead",
        nbytes, total_written);
  }
  return Status::OK();
}

void PosixWritableFile::UnwrittenRemaining(
    struct iovec** remaining_iov, ssize_t written, int* remaining_count) {
  size_t bytes_to_consume = written;
  do {
    if (bytes_to_consume >= (*remaining_iov)->iov_len) {
      bytes_to_consume -= (*remaining_iov)->iov_len;
      (*remaining_iov)++;
      (*remaining_count)--;
    } else {
      (*remaining_iov)->iov_len -= bytes_to_consume;
      (*remaining_iov)->iov_base =
          static_cast<uint8_t*>((*remaining_iov)->iov_base) + bytes_to_consume;
      bytes_to_consume = 0;
    }
  } while (bytes_to_consume > 0);
}

#ifdef ROCKSDB_FALLOCATE_PRESENT
Status PosixWritableFile::Allocate(uint64_t offset, uint64_t len) {
  assert(std::cmp_less_equal(offset, std::numeric_limits<off_t>::max()));
  assert(std::cmp_less_equal(len, std::numeric_limits<off_t>::max()));
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
  assert(std::cmp_less_equal(offset, std::numeric_limits<off_t>::max()));
  assert(std::cmp_less_equal(nbytes, std::numeric_limits<off_t>::max()));
  if (sync_file_range(fd_, static_cast<off_t>(offset),
      static_cast<off_t>(nbytes), SYNC_FILE_RANGE_WRITE) == 0) {
    return Status::OK();
  } else {
    return STATUS_IO_ERROR(filename_, errno);
  }
}

size_t PosixWritableFile::GetUniqueId(char* id) const {
  return GetUniqueIdFromFile(fd_, pointer_cast<uint8_t*>(id));
}
#endif

} // namespace yb
