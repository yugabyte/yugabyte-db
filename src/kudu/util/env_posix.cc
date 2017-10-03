// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <glog/logging.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/bind.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/env.h"
#include "kudu/util/errno.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/logging.h"
#include "kudu/util/malloc.h"
#include "kudu/util/monotime.h"
#include "kudu/util/path_util.h"
#include "kudu/util/slice.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/thread_restrictions.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#else
#include <linux/falloc.h>
#include <sys/sysinfo.h>
#endif  // defined(__APPLE__)

// Copied from falloc.h. Useful for older kernels that lack support for
// hole punching; fallocate(2) will return EOPNOTSUPP.
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE 0x01 /* default is extend size */
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE  0x02 /* de-allocates range */
#endif

// For platforms without fdatasync (like OS X)
#ifndef fdatasync
#define fdatasync fsync
#endif

// For platforms without unlocked_stdio (like OS X)
#ifndef fread_unlocked
#define fread_unlocked fread
#endif

// See KUDU-588 for details.
DEFINE_bool(writable_file_use_fsync, false,
            "Use fsync(2) instead of fdatasync(2) for synchronizing dirty "
            "data to disk.");
TAG_FLAG(writable_file_use_fsync, advanced);

DEFINE_bool(suicide_on_eio, true,
            "Kill the process if an I/O operation results in EIO");
TAG_FLAG(suicide_on_eio, advanced);

DEFINE_bool(never_fsync, false,
            "Never fsync() anything to disk. This is used by certain test cases to "
            "speed up runtime. This is very unsafe to use in production.");
TAG_FLAG(never_fsync, advanced);
TAG_FLAG(never_fsync, unsafe);

using base::subtle::Atomic64;
using base::subtle::Barrier_AtomicIncrement;
using std::vector;
using strings::Substitute;

static __thread uint64_t thread_local_id;
static Atomic64 cur_thread_local_id_;

namespace kudu {

namespace {

#if defined(__APPLE__)
// Simulates Linux's fallocate file preallocation API on OS X.
int fallocate(int fd, int mode, off_t offset, off_t len) {
  CHECK(mode == 0);
  off_t size = offset + len;

  struct stat stat;
  int ret = fstat(fd, &stat);
  if (ret < 0) {
    return ret;
  }

  if (stat.st_blocks * 512 < size) {
    // The offset field seems to have no effect; the file is always allocated
    // with space from 0 to the size. This is probably because OS X does not
    // support sparse files.
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, size};
    if (fcntl(fd, F_PREALLOCATE, &store) < 0) {
      LOG(INFO) << "Unable to allocate contiguous disk space, attempting non-contiguous allocation";
      store.fst_flags = F_ALLOCATEALL;
      ret = fcntl(fd, F_PREALLOCATE, &store);
      if (ret < 0) {
        return ret;
      }
    }
  }

  if (stat.st_size < size) {
    // fcntl does not change the file size, so set it if necessary.
    return ftruncate(fd, size);
  }
  return 0;
}
#endif

// Close file descriptor when object goes out of scope.
class ScopedFdCloser {
 public:
  explicit ScopedFdCloser(int fd)
    : fd_(fd) {
  }

  ~ScopedFdCloser() {
    ThreadRestrictions::AssertIOAllowed();
    int err = ::close(fd_);
    if (PREDICT_FALSE(err != 0)) {
      PLOG(WARNING) << "Failed to close fd " << fd_;
    }
  }

 private:
  int fd_;
};

static Status IOError(const std::string& context, int err_number) {
  switch (err_number) {
    case ENOENT:
      return Status::NotFound(context, ErrnoToString(err_number), err_number);
    case EEXIST:
      return Status::AlreadyPresent(context, ErrnoToString(err_number), err_number);
    case EOPNOTSUPP:
      return Status::NotSupported(context, ErrnoToString(err_number), err_number);
    case EIO:
      if (FLAGS_suicide_on_eio) {
        // TODO: This is very, very coarse-grained. A more comprehensive
        // approach is described in KUDU-616.
        LOG(FATAL) << "Fatal I/O error, context: " << context;
      }
  }
  return Status::IOError(context, ErrnoToString(err_number), err_number);
}

static Status DoSync(int fd, const string& filename) {
  ThreadRestrictions::AssertIOAllowed();
  if (FLAGS_never_fsync) return Status::OK();
  if (FLAGS_writable_file_use_fsync) {
    if (fsync(fd) < 0) {
      return IOError(filename, errno);
    }
  } else {
    if (fdatasync(fd) < 0) {
      return IOError(filename, errno);
    }
  }
  return Status::OK();
}

static Status DoOpen(const string& filename, Env::CreateMode mode, int* fd) {
  ThreadRestrictions::AssertIOAllowed();
  int flags = O_RDWR;
  switch (mode) {
    case Env::CREATE_IF_NON_EXISTING_TRUNCATE:
      flags |= O_CREAT | O_TRUNC;
      break;
    case Env::CREATE_NON_EXISTING:
      flags |= O_CREAT | O_EXCL;
      break;
    case Env::OPEN_EXISTING:
      break;
    default:
      return Status::NotSupported(Substitute("Unknown create mode $0", mode));
  }
  const int f = open(filename.c_str(), flags, 0644);
  if (f < 0) {
    return IOError(filename, errno);
  }
  *fd = f;
  return Status::OK();
}

class PosixSequentialFile: public SequentialFile {
 private:
  std::string filename_;
  FILE* file_;

 public:
  PosixSequentialFile(std::string fname, FILE* f)
      : filename_(std::move(fname)), file_(f) {}
  virtual ~PosixSequentialFile() { fclose(file_); }

  virtual Status Read(size_t n, Slice* result, uint8_t* scratch) OVERRIDE {
    ThreadRestrictions::AssertIOAllowed();
    Status s;
    size_t r = fread_unlocked(scratch, 1, n, file_);
    *result = Slice(scratch, r);
    if (r < n) {
      if (feof(file_)) {
        // We leave status as ok if we hit the end of the file
      } else {
        // A partial read with an error: return a non-ok status.
        s = IOError(filename_, errno);
      }
    }
    return s;
  }

  virtual Status Skip(uint64_t n) OVERRIDE {
    TRACE_EVENT1("io", "PosixSequentialFile::Skip", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    if (fseek(file_, n, SEEK_CUR)) {
      return IOError(filename_, errno);
    }
    return Status::OK();
  }

  virtual const string& filename() const OVERRIDE { return filename_; }
};

// pread() based random-access
class PosixRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  int fd_;

 public:
  PosixRandomAccessFile(std::string fname, int fd)
      : filename_(std::move(fname)), fd_(fd) {}
  virtual ~PosixRandomAccessFile() { close(fd_); }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      uint8_t *scratch) const OVERRIDE {
    ThreadRestrictions::AssertIOAllowed();
    Status s;
    ssize_t r = pread(fd_, scratch, n, static_cast<off_t>(offset));
    *result = Slice(scratch, (r < 0) ? 0 : r);
    if (r < 0) {
      // An error: return a non-ok status.
      s = IOError(filename_, errno);
    }
    return s;
  }

  virtual Status Size(uint64_t *size) const OVERRIDE {
    TRACE_EVENT1("io", "PosixRandomAccessFile::Size", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    struct stat st;
    if (fstat(fd_, &st) == -1) {
      return IOError(filename_, errno);
    }
    *size = st.st_size;
    return Status::OK();
  }

  virtual const string& filename() const OVERRIDE { return filename_; }

  virtual size_t memory_footprint() const OVERRIDE {
    return kudu_malloc_usable_size(this) + filename_.capacity();
  }
};

// Use non-memory mapped POSIX files to write data to a file.
//
// TODO (perf) investigate zeroing a pre-allocated allocated area in
// order to further improve Sync() performance.
class PosixWritableFile : public WritableFile {
 public:
  PosixWritableFile(std::string fname, int fd, uint64_t file_size,
                    bool sync_on_close)
      : filename_(std::move(fname)),
        fd_(fd),
        sync_on_close_(sync_on_close),
        filesize_(file_size),
        pre_allocated_size_(0),
        pending_sync_(false) {}

  ~PosixWritableFile() {
    if (fd_ >= 0) {
      WARN_NOT_OK(Close(), "Failed to close " + filename_);
    }
  }

  virtual Status Append(const Slice& data) OVERRIDE {
    vector<Slice> data_vector;
    data_vector.push_back(data);
    return AppendVector(data_vector);
  }

  virtual Status AppendVector(const vector<Slice>& data_vector) OVERRIDE {
    ThreadRestrictions::AssertIOAllowed();
    static const size_t kIovMaxElements = IOV_MAX;

    Status s;
    for (size_t i = 0; i < data_vector.size() && s.ok(); i += kIovMaxElements) {
      size_t n = std::min(data_vector.size() - i, kIovMaxElements);
      s = DoWritev(data_vector, i, n);
    }

    pending_sync_ = true;
    return s;
  }

  virtual Status PreAllocate(uint64_t size) OVERRIDE {
    TRACE_EVENT1("io", "PosixWritableFile::PreAllocate", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    uint64_t offset = std::max(filesize_, pre_allocated_size_);
    if (fallocate(fd_, 0, offset, size) < 0) {
      if (errno == EOPNOTSUPP) {
        KLOG_FIRST_N(WARNING, 1) << "The filesystem does not support fallocate().";
      } else if (errno == ENOSYS) {
        KLOG_FIRST_N(WARNING, 1) << "The kernel does not implement fallocate().";
      } else {
        return IOError(filename_, errno);
      }
    }
    pre_allocated_size_ = offset + size;
    return Status::OK();
  }

  virtual Status Close() OVERRIDE {
    TRACE_EVENT1("io", "PosixWritableFile::Close", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    Status s;

    // If we've allocated more space than we used, truncate to the
    // actual size of the file and perform Sync().
    if (filesize_ < pre_allocated_size_) {
      if (ftruncate(fd_, filesize_) < 0) {
        s = IOError(filename_, errno);
        pending_sync_ = true;
      }
    }

    if (sync_on_close_) {
      Status sync_status = Sync();
      if (!sync_status.ok()) {
        LOG(ERROR) << "Unable to Sync " << filename_ << ": " << sync_status.ToString();
        if (s.ok()) {
          s = sync_status;
        }
      }
    }

    if (close(fd_) < 0) {
      if (s.ok()) {
        s = IOError(filename_, errno);
      }
    }

    fd_ = -1;
    return s;
  }

  virtual Status Flush(FlushMode mode) OVERRIDE {
    TRACE_EVENT1("io", "PosixWritableFile::Flush", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
#if defined(__linux__)
    int flags = SYNC_FILE_RANGE_WRITE;
    if (mode == FLUSH_SYNC) {
      flags |= SYNC_FILE_RANGE_WAIT_AFTER;
    }
    if (sync_file_range(fd_, 0, 0, flags) < 0) {
      return IOError(filename_, errno);
    }
#else
    if (fsync(fd_) < 0) {
      return IOError(filename_, errno);
    }
#endif
    return Status::OK();
  }

  virtual Status Sync() OVERRIDE {
    TRACE_EVENT1("io", "PosixWritableFile::Sync", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    LOG_SLOW_EXECUTION(WARNING, 1000, Substitute("sync call for $0", filename_)) {
      if (pending_sync_) {
        pending_sync_ = false;
        RETURN_NOT_OK(DoSync(fd_, filename_));
      }
    }
    return Status::OK();
  }

  virtual uint64_t Size() const OVERRIDE {
    return filesize_;
  }

  virtual const string& filename() const OVERRIDE { return filename_; }

 private:

  Status DoWritev(const vector<Slice>& data_vector,
                  size_t offset, size_t n) {
    ThreadRestrictions::AssertIOAllowed();
#if defined(__linux__)
    DCHECK_LE(n, IOV_MAX);

    struct iovec iov[n];
    size_t j = 0;
    size_t nbytes = 0;

    for (size_t i = offset; i < offset + n; i++) {
      const Slice& data = data_vector[i];
      iov[j].iov_base = const_cast<uint8_t*>(data.data());
      iov[j].iov_len = data.size();
      nbytes += data.size();
      ++j;
    }

    ssize_t written = pwritev(fd_, iov, n, filesize_);

    if (PREDICT_FALSE(written == -1)) {
      int err = errno;
      return IOError(filename_, err);
    }

    filesize_ += written;

    if (PREDICT_FALSE(written != nbytes)) {
      return Status::IOError(
          Substitute("pwritev error: expected to write $0 bytes, wrote $1 bytes instead",
                     nbytes, written));
    }
#else
    for (size_t i = offset; i < offset + n; i++) {
      const Slice& data = data_vector[i];
      ssize_t written = pwrite(fd_, data.data(), data.size(), filesize_);
      if (PREDICT_FALSE(written == -1)) {
        int err = errno;
        return IOError("pwrite error", err);
      }

      filesize_ += written;

      if (PREDICT_FALSE(written != data.size())) {
        return Status::IOError(
            Substitute("pwrite error: expected to write $0 bytes, wrote $1 bytes instead",
                       data.size(), written));
      }
    }
#endif

    return Status::OK();
  }

  const std::string filename_;
  int fd_;
  bool sync_on_close_;
  uint64_t filesize_;
  uint64_t pre_allocated_size_;

  bool pending_sync_;
};

class PosixRWFile : public RWFile {
// is not employed.
 public:
  PosixRWFile(string fname, int fd, bool sync_on_close)
      : filename_(std::move(fname)),
        fd_(fd),
        sync_on_close_(sync_on_close),
        pending_sync_(false) {}

  ~PosixRWFile() {
    if (fd_ >= 0) {
      WARN_NOT_OK(Close(), "Failed to close " + filename_);
    }
  }

  virtual Status Read(uint64_t offset, size_t length,
                      Slice* result, uint8_t* scratch) const OVERRIDE {
    ThreadRestrictions::AssertIOAllowed();
    int rem = length;
    uint8_t* dst = scratch;
    while (rem > 0) {
      ssize_t r = pread(fd_, dst, rem, offset);
      if (r < 0) {
        // An error: return a non-ok status.
        return IOError(filename_, errno);
      }
      Slice this_result(dst, r);
      DCHECK_LE(this_result.size(), rem);
      if (this_result.size() == 0) {
        // EOF
        return Status::IOError(Substitute("EOF trying to read $0 bytes at offset $1",
                                          length, offset));
      }
      dst += this_result.size();
      rem -= this_result.size();
      offset += this_result.size();
    }
    DCHECK_EQ(0, rem);
    *result = Slice(scratch, length);
    return Status::OK();
  }

  virtual Status Write(uint64_t offset, const Slice& data) OVERRIDE {
    ThreadRestrictions::AssertIOAllowed();
    ssize_t written = pwrite(fd_, data.data(), data.size(), offset);

    if (PREDICT_FALSE(written == -1)) {
      int err = errno;
      return IOError(filename_, err);
    }

    if (PREDICT_FALSE(written != data.size())) {
      return Status::IOError(
          Substitute("pwrite error: expected to write $0 bytes, wrote $1 bytes instead",
                     data.size(), written));
    }

    pending_sync_ = true;
    return Status::OK();
  }

  virtual Status PreAllocate(uint64_t offset, size_t length) OVERRIDE {
    TRACE_EVENT1("io", "PosixRWFile::PreAllocate", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    if (fallocate(fd_, 0, offset, length) < 0) {
      if (errno == EOPNOTSUPP) {
        KLOG_FIRST_N(WARNING, 1) << "The filesystem does not support fallocate().";
      } else if (errno == ENOSYS) {
        KLOG_FIRST_N(WARNING, 1) << "The kernel does not implement fallocate().";
      } else {
        return IOError(filename_, errno);
      }
    }
    return Status::OK();
  }

  virtual Status PunchHole(uint64_t offset, size_t length) OVERRIDE {
#if defined(__linux__)
    TRACE_EVENT1("io", "PosixRWFile::PunchHole", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    if (fallocate(fd_, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, length) < 0) {
      return IOError(filename_, errno);
    }
    return Status::OK();
#else
    return Status::NotSupported("Hole punching not supported on this platform");
#endif
  }

  virtual Status Flush(FlushMode mode, uint64_t offset, size_t length) OVERRIDE {
    TRACE_EVENT1("io", "PosixRWFile::Flush", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
#if defined(__linux__)
    int flags = SYNC_FILE_RANGE_WRITE;
    if (mode == FLUSH_SYNC) {
      flags |= SYNC_FILE_RANGE_WAIT_AFTER;
    }
    if (sync_file_range(fd_, offset, length, flags) < 0) {
      return IOError(filename_, errno);
    }
#else
    if (fsync(fd_) < 0) {
      return IOError(filename_, errno);
    }
#endif
    return Status::OK();
  }

  virtual Status Sync() OVERRIDE {
    TRACE_EVENT1("io", "PosixRWFile::Sync", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    LOG_SLOW_EXECUTION(WARNING, 1000, Substitute("sync call for $0", filename())) {
      if (pending_sync_) {
        pending_sync_ = false;
        RETURN_NOT_OK(DoSync(fd_, filename_));
      }
    }
    return Status::OK();
  }

  virtual Status Close() OVERRIDE {
    TRACE_EVENT1("io", "PosixRWFile::Close", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    Status s;

    if (sync_on_close_) {
      s = Sync();
      if (!s.ok()) {
        LOG(ERROR) << "Unable to Sync " << filename_ << ": " << s.ToString();
      }
    }

    if (close(fd_) < 0) {
      if (s.ok()) {
        s = IOError(filename_, errno);
      }
    }

    fd_ = -1;
    return s;
  }

  virtual Status Size(uint64_t* size) const OVERRIDE {
    TRACE_EVENT1("io", "PosixRWFile::Size", "path", filename_);
    ThreadRestrictions::AssertIOAllowed();
    struct stat st;
    if (fstat(fd_, &st) == -1) {
      return IOError(filename_, errno);
    }
    *size = st.st_size;
    return Status::OK();
  }

  virtual const string& filename() const OVERRIDE {
    return filename_;
  }

 private:
  const std::string filename_;
  int fd_;
  bool sync_on_close_;
  bool pending_sync_;
};

static int LockOrUnlock(int fd, bool lock) {
  ThreadRestrictions::AssertIOAllowed();
  errno = 0;
  struct flock f;
  memset(&f, 0, sizeof(f));
  f.l_type = (lock ? F_WRLCK : F_UNLCK);
  f.l_whence = SEEK_SET;
  f.l_start = 0;
  f.l_len = 0;        // Lock/unlock entire file
  return fcntl(fd, F_SETLK, &f);
}

class PosixFileLock : public FileLock {
 public:
  int fd_;
};

class PosixEnv : public Env {
 public:
  PosixEnv();
  virtual ~PosixEnv() {
    fprintf(stderr, "Destroying Env::Default()\n");
    exit(1);
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   gscoped_ptr<SequentialFile>* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::NewSequentialFile", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    FILE* f = fopen(fname.c_str(), "r");
    if (f == nullptr) {
      return IOError(fname, errno);
    } else {
      result->reset(new PosixSequentialFile(fname, f));
      return Status::OK();
    }
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     gscoped_ptr<RandomAccessFile>* result) OVERRIDE {
    return NewRandomAccessFile(RandomAccessFileOptions(), fname, result);
  }

  virtual Status NewRandomAccessFile(const RandomAccessFileOptions& opts,
                                     const std::string& fname,
                                     gscoped_ptr<RandomAccessFile>* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::NewRandomAccessFile", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    int fd = open(fname.c_str(), O_RDONLY);
    if (fd < 0) {
      return IOError(fname, errno);
    }

    result->reset(new PosixRandomAccessFile(fname, fd));
    return Status::OK();
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 gscoped_ptr<WritableFile>* result) OVERRIDE {
    return NewWritableFile(WritableFileOptions(), fname, result);
  }

  virtual Status NewWritableFile(const WritableFileOptions& opts,
                                 const std::string& fname,
                                 gscoped_ptr<WritableFile>* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::NewWritableFile", "path", fname);
    int fd;
    RETURN_NOT_OK(DoOpen(fname, opts.mode, &fd));
    return InstantiateNewWritableFile(fname, fd, opts, result);
  }

  virtual Status NewTempWritableFile(const WritableFileOptions& opts,
                                     const std::string& name_template,
                                     std::string* created_filename,
                                     gscoped_ptr<WritableFile>* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::NewTempWritableFile", "template", name_template);
    ThreadRestrictions::AssertIOAllowed();
    gscoped_ptr<char[]> fname(new char[name_template.size() + 1]);
    ::snprintf(fname.get(), name_template.size() + 1, "%s", name_template.c_str());
    const int fd = ::mkstemp(fname.get());
    if (fd < 0) {
      return IOError(Substitute("Call to mkstemp() failed on name template $0", name_template),
                     errno);
    }
    *created_filename = fname.get();
    return InstantiateNewWritableFile(*created_filename, fd, opts, result);
  }

  virtual Status NewRWFile(const string& fname,
                           gscoped_ptr<RWFile>* result) OVERRIDE {
    return NewRWFile(RWFileOptions(), fname, result);
  }

  virtual Status NewRWFile(const RWFileOptions& opts,
                           const string& fname,
                           gscoped_ptr<RWFile>* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::NewRWFile", "path", fname);
    int fd;
    RETURN_NOT_OK(DoOpen(fname, opts.mode, &fd));
    result->reset(new PosixRWFile(fname, fd, opts.sync_on_close));
    return Status::OK();
  }

  virtual bool FileExists(const std::string& fname) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::FileExists", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    return access(fname.c_str(), F_OK) == 0;
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::GetChildren", "path", dir);
    ThreadRestrictions::AssertIOAllowed();
    result->clear();
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
      return IOError(dir, errno);
    }
    struct dirent* entry;
    // TODO: lint: Consider using readdir_r(...) instead of readdir(...) for improved thread safety.
    while ((entry = readdir(d)) != nullptr) {
      result->push_back(entry->d_name);
    }
    closedir(d);
    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::DeleteFile", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    Status result;
    if (unlink(fname.c_str()) != 0) {
      result = IOError(fname, errno);
    }
    return result;
  };

  virtual Status CreateDir(const std::string& name) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::CreateDir", "path", name);
    ThreadRestrictions::AssertIOAllowed();
    Status result;
    if (mkdir(name.c_str(), 0755) != 0) {
      result = IOError(name, errno);
    }
    return result;
  };

  virtual Status DeleteDir(const std::string& name) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::DeleteDir", "path", name);
    ThreadRestrictions::AssertIOAllowed();
    Status result;
    if (rmdir(name.c_str()) != 0) {
      result = IOError(name, errno);
    }
    return result;
  };

  virtual Status SyncDir(const std::string& dirname) OVERRIDE {
    TRACE_EVENT1("io", "SyncDir", "path", dirname);
    ThreadRestrictions::AssertIOAllowed();
    if (FLAGS_never_fsync) return Status::OK();
    int dir_fd;
    if ((dir_fd = open(dirname.c_str(), O_DIRECTORY|O_RDONLY)) == -1) {
      return IOError(dirname, errno);
    }
    ScopedFdCloser fd_closer(dir_fd);
    if (fsync(dir_fd) != 0) {
      return IOError(dirname, errno);
    }
    return Status::OK();
  }

  virtual Status DeleteRecursively(const std::string &name) OVERRIDE {
    return Walk(name, POST_ORDER, Bind(&PosixEnv::DeleteRecursivelyCb,
                                       Unretained(this)));
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::GetFileSize", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    Status s;
    struct stat sbuf;
    if (stat(fname.c_str(), &sbuf) != 0) {
      s = IOError(fname, errno);
    } else {
      *size = sbuf.st_size;
    }
    return s;
  }

  virtual Status GetFileSizeOnDisk(const std::string& fname, uint64_t* size) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::GetFileSizeOnDisk", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    Status s;
    struct stat sbuf;
    if (stat(fname.c_str(), &sbuf) != 0) {
      s = IOError(fname, errno);
    } else {
      // From stat(2):
      //
      //   The st_blocks field indicates the number of blocks allocated to
      //   the file, 512-byte units. (This may be smaller than st_size/512
      //   when the file has holes.)
      *size = sbuf.st_blocks * 512;
    }
    return s;
  }

  virtual Status GetBlockSize(const string& fname, uint64_t* block_size) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::GetBlockSize", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    Status s;
    struct stat sbuf;
    if (stat(fname.c_str(), &sbuf) != 0) {
      s = IOError(fname, errno);
    } else {
      *block_size = sbuf.st_blksize;
    }
    return s;
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) OVERRIDE {
    TRACE_EVENT2("io", "PosixEnv::RenameFile", "src", src, "dst", target);
    ThreadRestrictions::AssertIOAllowed();
    Status result;
    if (rename(src.c_str(), target.c_str()) != 0) {
      result = IOError(src, errno);
    }
    return result;
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::LockFile", "path", fname);
    ThreadRestrictions::AssertIOAllowed();
    *lock = nullptr;
    Status result;
    int fd = open(fname.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
      result = IOError(fname, errno);
    } else if (LockOrUnlock(fd, true) == -1) {
      result = IOError("lock " + fname, errno);
      close(fd);
    } else {
      auto my_lock = new PosixFileLock;
      my_lock->fd_ = fd;
      *lock = my_lock;
    }
    return result;
  }

  virtual Status UnlockFile(FileLock* lock) OVERRIDE {
    TRACE_EVENT0("io", "PosixEnv::UnlockFile");
    ThreadRestrictions::AssertIOAllowed();
    PosixFileLock* my_lock = reinterpret_cast<PosixFileLock*>(lock);
    Status result;
    if (LockOrUnlock(my_lock->fd_, false) == -1) {
      result = IOError("unlock", errno);
    }
    close(my_lock->fd_);
    delete my_lock;
    return result;
  }

  virtual Status GetTestDirectory(std::string* result) OVERRIDE {
    string dir;
    const char* env = getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
      dir = env;
    } else {
      char buf[100];
      snprintf(buf, sizeof(buf), "/tmp/kudutest-%d", static_cast<int>(geteuid()));
      dir = buf;
    }
    // Directory may already exist
    ignore_result(CreateDir(dir));
    // /tmp may be a symlink, so canonicalize the path.
    return Canonicalize(dir, result);
  }

  virtual uint64_t gettid() OVERRIDE {
    // Platform-independent thread ID.  We can't use pthread_self here,
    // because that function returns a totally opaque ID, which can't be
    // compared via normal means.
    if (thread_local_id == 0) {
      thread_local_id = Barrier_AtomicIncrement(&cur_thread_local_id_, 1);
    }
    return thread_local_id;
  }

  virtual uint64_t NowMicros() OVERRIDE {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
  }

  virtual void SleepForMicroseconds(int micros) OVERRIDE {
    ThreadRestrictions::AssertWaitAllowed();
    SleepFor(MonoDelta::FromMicroseconds(micros));
  }

  virtual Status GetExecutablePath(string* path) OVERRIDE {
    uint32_t size = 64;
    uint32_t len = 0;
    while (true) {
      gscoped_ptr<char[]> buf(new char[size]);
#if defined(__linux__)
      int rc = readlink("/proc/self/exe", buf.get(), size);
      if (rc == -1) {
        return Status::IOError("Unable to determine own executable path", "", errno);
      } else if (rc >= size) {
        // The buffer wasn't large enough
        size *= 2;
        continue;
      }
      len = rc;
#elif defined(__APPLE__)
      if (_NSGetExecutablePath(buf.get(), &size) != 0) {
        // The buffer wasn't large enough; 'size' has been updated.
        continue;
      }
      len = strlen(buf.get());
#else
#error Unsupported platform
#endif

      path->assign(buf.get(), len);
      break;
    }
    return Status::OK();
  }

  virtual Status IsDirectory(const string& path, bool* is_dir) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::IsDirectory", "path", path);
    ThreadRestrictions::AssertIOAllowed();
    Status s;
    struct stat sbuf;
    if (stat(path.c_str(), &sbuf) != 0) {
      s = IOError(path, errno);
    } else {
      *is_dir = S_ISDIR(sbuf.st_mode);
    }
    return s;
  }

  virtual Status Walk(const string& root, DirectoryOrder order, const WalkCallback& cb) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::Walk", "path", root);
    ThreadRestrictions::AssertIOAllowed();
    // Some sanity checks
    CHECK_NE(root, "/");
    CHECK_NE(root, "./");
    CHECK_NE(root, ".");
    CHECK_NE(root, "");

    // FTS requires a non-const copy of the name. strdup it and free() when
    // we leave scope.
    gscoped_ptr<char, FreeDeleter> name_dup(strdup(root.c_str()));
    char *(paths[]) = { name_dup.get(), nullptr };

    // FTS_NOCHDIR is important here to make this thread-safe.
    gscoped_ptr<FTS, FtsCloser> tree(
        fts_open(paths, FTS_PHYSICAL | FTS_XDEV | FTS_NOCHDIR, nullptr));
    if (!tree.get()) {
      return IOError(root, errno);
    }

    FTSENT *ent = nullptr;
    bool had_errors = false;
    while ((ent = fts_read(tree.get())) != nullptr) {
      bool doCb = false;
      FileType type = DIRECTORY_TYPE;
      switch (ent->fts_info) {
        case FTS_D:         // Directory in pre-order
          if (order == PRE_ORDER) {
            doCb = true;
          }
          break;
        case FTS_DP:        // Directory in post-order
          if (order == POST_ORDER) {
            doCb = true;
          }
          break;
        case FTS_F:         // A regular file
        case FTS_SL:        // A symbolic link
        case FTS_SLNONE:    // A broken symbolic link
        case FTS_DEFAULT:   // Unknown type of file
          doCb = true;
          type = FILE_TYPE;
          break;

        case FTS_ERR:
          LOG(WARNING) << "Unable to access file " << ent->fts_path
                       << " during walk: " << strerror(ent->fts_errno);
          had_errors = true;
          break;

        default:
          LOG(WARNING) << "Unable to access file " << ent->fts_path
                       << " during walk (code " << ent->fts_info << ")";
          break;
      }
      if (doCb) {
        if (!cb.Run(type, DirName(ent->fts_path), ent->fts_name).ok()) {
          had_errors = true;
        }
      }
    }

    if (had_errors) {
      return Status::IOError(root, "One or more errors occurred");
    }
    return Status::OK();
  }

  virtual Status Canonicalize(const string& path, string* result) OVERRIDE {
    TRACE_EVENT1("io", "PosixEnv::Canonicalize", "path", path);
    ThreadRestrictions::AssertIOAllowed();
    gscoped_ptr<char[], FreeDeleter> r(realpath(path.c_str(), nullptr));
    if (!r) {
      return IOError(path, errno);
    }
    *result = string(r.get());
    return Status::OK();
  }

  virtual Status GetTotalRAMBytes(int64_t* ram) OVERRIDE {
#if defined(__APPLE__)
    int mib[2];
    size_t length = sizeof(*ram);

    // Get the Physical memory size
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    CHECK_ERR(sysctl(mib, 2, ram, &length, nullptr, 0)) << "sysctl CTL_HW HW_MEMSIZE failed";
#else
    struct sysinfo info;
    if (sysinfo(&info) < 0) {
      return IOError("sysinfo() failed", errno);
    }
    *ram = info.totalram;
#endif
    return Status::OK();
  }

 private:
  // gscoped_ptr Deleter implementation for fts_close
  struct FtsCloser {
    void operator()(FTS *fts) const {
      if (fts) { fts_close(fts); }
    }
  };

  Status InstantiateNewWritableFile(const std::string& fname,
                                    int fd,
                                    const WritableFileOptions& opts,
                                    gscoped_ptr<WritableFile>* result) {
    uint64_t file_size = 0;
    if (opts.mode == OPEN_EXISTING) {
      RETURN_NOT_OK(GetFileSize(fname, &file_size));
    }
    result->reset(new PosixWritableFile(fname, fd, file_size, opts.sync_on_close));
    return Status::OK();
  }

  Status DeleteRecursivelyCb(FileType type, const string& dirname, const string& basename) {
    string full_path = JoinPathSegments(dirname, basename);
    Status s;
    switch (type) {
      case FILE_TYPE:
        s = DeleteFile(full_path);
        WARN_NOT_OK(s, "Could not delete file");
        return s;
      case DIRECTORY_TYPE:
        s = DeleteDir(full_path);
        WARN_NOT_OK(s, "Could not delete directory");
        return s;
      default:
        LOG(FATAL) << "Unknown file type: " << type;
        return Status::OK();
    }
  }
};

PosixEnv::PosixEnv() {}

}  // namespace

static pthread_once_t once = PTHREAD_ONCE_INIT;
static Env* default_env;
static void InitDefaultEnv() { default_env = new PosixEnv; }

Env* Env::Default() {
  pthread_once(&once, InitDefaultEnv);
  return default_env;
}

}  // namespace kudu
