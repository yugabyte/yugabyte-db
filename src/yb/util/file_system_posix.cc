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
#include "yb/util/malloc.h"
#include "yb/util/result.h"
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

namespace yb {

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
