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

#include <sys/stat.h>

#include "yb/util/debug/trace_event.h"
#include "yb/util/errno.h"
#include "yb/util/malloc.h"
#include "yb/util/thread_restrictions.h"

DECLARE_bool(suicide_on_eio);

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
#if !(defined OS_LINUX) && !(defined CYGWIN)
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
#ifdef OS_LINUX
  return posix_fadvise(fd, offset, len, advice);
#else
  return 0;  // simply do nothing.
#endif
}

static Status IOError(const std::string& context, int err_number, const char* file, int line) {
  switch (err_number) {
    case ENOENT:
      return Status(Status::kNotFound, file, line, context, ErrnoToString(err_number), err_number);
    case EEXIST:
      return Status(Status::kAlreadyPresent, file, line, context, ErrnoToString(err_number),
                    err_number);
    case EOPNOTSUPP:
      return Status(Status::kNotSupported, file, line, context, ErrnoToString(err_number),
                    err_number);
    case EIO:
      if (FLAGS_suicide_on_eio) {
        // TODO: This is very, very coarse-grained. A more comprehensive
        // approach is described in KUDU-616.
        LOG(FATAL) << "Fatal I/O error, context: " << context;
      }
  }
  return Status(Status::kIOError, file, line, context, ErrnoToString(err_number), err_number);
}

#define STATUS_IO_ERROR(context, err_number) IOError(context, err_number, __FILE__, __LINE__)

} // namespace

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
#ifndef OS_LINUX
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
