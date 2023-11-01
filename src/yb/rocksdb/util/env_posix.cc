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
#include <dirent.h>
#include <fcntl.h>
#if defined(__linux__)
#include <linux/fs.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/statfs.h>
#include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <time.h>
#include <algorithm>
// Get nano time includes
#if defined(__linux__) || defined(OS_FREEBSD)
#elif defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
#else
#include <chrono>
#endif
#include <deque>
#include <set>

#include "yb/gutil/casts.h"

#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/util/io_posix.h"
#include "yb/rocksdb/util/thread_posix.h"
#include "yb/rocksdb/util/posix_logger.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/sync_point.h"
#include "yb/rocksdb/util/thread_local.h"

#include "yb/util/logging.h"
#include "yb/util/slice.h"
#include "yb/util/stats/iostats_context_imp.h"
#include "yb/util/string_util.h"

#if !defined(TMPFS_MAGIC)
#define TMPFS_MAGIC 0x01021994
#endif
#if !defined(XFS_SUPER_MAGIC)
#define XFS_SUPER_MAGIC 0x58465342
#endif
#if !defined(EXT4_SUPER_MAGIC)
#define EXT4_SUPER_MAGIC 0xEF53
#endif

#include "yb/util/file_system_posix.h"

namespace rocksdb {

namespace {

// list of pathnames that are locked
static std::set<std::string> lockedFiles;
static port::Mutex mutex_lockedFiles;

static int LockOrUnlock(const std::string& fname, int fd, bool lock) {
  mutex_lockedFiles.Lock();
  if (lock) {
    // If it already exists in the lockedFiles set, then it is already locked,
    // and fail this lock attempt. Otherwise, insert it into lockedFiles.
    // This check is needed because fcntl() does not detect lock conflict
    // if the fcntl is issued by the same thread that earlier acquired
    // this lock.
    if (lockedFiles.insert(fname).second == false) {
      mutex_lockedFiles.Unlock();
      errno = ENOLCK;
      return -1;
    }
  } else {
    // If we are unlocking, then verify that we had locked it earlier,
    // it should already exist in lockedFiles. Remove it from lockedFiles.
    if (lockedFiles.erase(fname) != 1) {
      mutex_lockedFiles.Unlock();
      errno = ENOLCK;
      return -1;
    }
  }
  errno = 0;
  struct flock f;
  memset(&f, 0, sizeof(f));
  f.l_type = (lock ? F_WRLCK : F_UNLCK);
  f.l_whence = SEEK_SET;
  f.l_start = 0;
  f.l_len = 0;        // Lock/unlock entire file
  int value = fcntl(fd, F_SETLK, &f);
  if (value == -1 && lock) {
    // if there is an error in locking, then remove the pathname from lockedfiles
    lockedFiles.erase(fname);
  }
  mutex_lockedFiles.Unlock();
  return value;
}

void SetFD_CLOEXEC(int fd, const EnvOptions* options) {
  if ((options == nullptr || options->set_fd_cloexec) && fd > 0) {
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
  }
}

class PosixFileLock : public FileLock {
 public:
  int fd_;
  std::string filename;
};

class PosixEnv : public Env {
 public:
  PosixEnv();
  explicit PosixEnv(std::unique_ptr<RocksDBFileFactory> file_factory);

  virtual ~PosixEnv() {
    for (const auto tid : threads_to_join_) {
      pthread_join(tid, nullptr);
    }
    for (int pool_id = 0; pool_id < Env::Priority::TOTAL; ++pool_id) {
      thread_pools_[pool_id].JoinAllThreads();
    }
  }

  virtual Status NewSequentialFile(const std::string& fname,
                                   std::unique_ptr<SequentialFile>* result,
                                   const EnvOptions& options) override {
    return file_factory_->NewSequentialFile(fname, result, options);
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     std::unique_ptr<RandomAccessFile>* result,
                                     const EnvOptions& options) override {
    return file_factory_->NewRandomAccessFile(fname, result, options);
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 std::unique_ptr<WritableFile>* result,
                                 const EnvOptions& options) override {
    return file_factory_->NewWritableFile(fname, result, options);
  }

  virtual Status ReuseWritableFile(const std::string& fname,
                                   const std::string& old_fname,
                                   std::unique_ptr<WritableFile>* result,
                                   const EnvOptions& options) override {
    return file_factory_->ReuseWritableFile(fname, old_fname, result, options);
  }

  bool IsPlainText() const override {
    return file_factory_->IsPlainText();
  }

  virtual Status NewDirectory(const std::string& name,
                              std::unique_ptr<Directory>* result) override {
    result->reset();
    int fd;
    {
      IOSTATS_TIMER_GUARD(open_nanos);
      fd = open(name.c_str(), 0);
    }
    if (fd < 0) {
      return STATUS_IO_ERROR(name, errno);
    } else {
      result->reset(new PosixDirectory(fd));
    }
    return Status::OK();
  }

  Status FileExists(const std::string& fname) override {
    int result = access(fname.c_str(), F_OK);

    if (result == 0) {
      return Status::OK();
    }

    switch (errno) {
      case EACCES:
      case ELOOP:
      case ENAMETOOLONG:
      case ENOENT:
      case ENOTDIR:
        return STATUS(NotFound, "");
      default:
        assert(result == EIO || result == ENOMEM);
        return STATUS(IOError, "Unexpected error(" + ToString(result) +
                               ") accessing file `" + fname + "' ");
    }
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) override {
    result->clear();
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
      return STATUS_IO_ERROR(dir, errno);
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
      result->push_back(entry->d_name);
    }
    closedir(d);
    return Status::OK();
  }

  Status DeleteFile(const std::string& fname) override {
    Status result;
    if (unlink(fname.c_str()) != 0) {
      result = STATUS_IO_ERROR(fname, errno);
    }
    return result;
  };

  Status CreateDir(const std::string& name) override {
    Status result;
    if (mkdir(name.c_str(), 0755) != 0) {
      result = STATUS_IO_ERROR(name, errno);
    }
    return result;
  };

  Status CreateDirIfMissing(const std::string& name) override {
    Status result;
    if (mkdir(name.c_str(), 0755) != 0) {
      if (errno != EEXIST) {
        result = STATUS_IO_ERROR(name, errno);
        LOG(DFATAL) << "Mkdir failed: " << result;
      } else if (!DirExists(name)) { // Check that name is actually a
                                     // directory.
        // Message is taken from mkdir
        result = STATUS(IOError, "`"+name+"' exists but is not a directory");
      }
    }
    return result;
  };

  Status DeleteDir(const std::string& name) override {
    Status result;
    if (rmdir(name.c_str()) != 0) {
      result = STATUS_IO_ERROR(name, errno);
    }
    return result;
  };

  Status GetFileSize(const std::string& fname, uint64_t* size) override {
    return file_factory_->GetFileSize(fname, size);
  }

  virtual Status GetFileModificationTime(const std::string& fname,
                                         uint64_t* file_mtime) override {
    struct stat s;
    if (stat(fname.c_str(), &s) !=0) {
      return STATUS_IO_ERROR(fname, errno);
    }
    *file_mtime = static_cast<uint64_t>(s.st_mtime);
    return Status::OK();
  }
  virtual Status RenameFile(const std::string& src,
                            const std::string& target) override {
    Status result;
    if (rename(src.c_str(), target.c_str()) != 0) {
      result = STATUS_IO_ERROR(src, errno);
    }
    return result;
  }

  virtual Status LinkFile(const std::string& src,
                          const std::string& target) override {
    Status result;
    if (link(src.c_str(), target.c_str()) != 0) {
      if (errno == EXDEV) {
        return STATUS(NotSupported, "No cross FS links allowed");
      }
      result = STATUS_IO_ERROR(src, errno);
    }
    return result;
  }

  Status LockFile(const std::string& fname, FileLock** lock) override {
    *lock = nullptr;
    Status result;
    int fd;
    {
      IOSTATS_TIMER_GUARD(open_nanos);
      fd = open(fname.c_str(), O_RDWR | O_CREAT, 0644);
    }
    if (fd < 0) {
      result = STATUS_IO_ERROR(fname, errno);
    } else if (LockOrUnlock(fname, fd, true) == -1) {
      result = STATUS_IO_ERROR("lock " + fname, errno);
      close(fd);
    } else {
      SetFD_CLOEXEC(fd, nullptr);
      PosixFileLock* my_lock = new PosixFileLock;
      my_lock->fd_ = fd;
      my_lock->filename = fname;
      *lock = my_lock;
    }
    return result;
  }

  Status UnlockFile(FileLock* lock) override {
    PosixFileLock* my_lock = reinterpret_cast<PosixFileLock*>(lock);
    Status result;
    if (LockOrUnlock(my_lock->filename, my_lock->fd_, false) == -1) {
      result = STATUS_IO_ERROR("unlock", errno);
    }
    close(my_lock->fd_);
    delete my_lock;
    return result;
  }

  virtual void Schedule(void (*function)(void* arg1), void* arg,
                        Priority pri = LOW, void* tag = nullptr,
                        void (*unschedFunction)(void* arg) = 0) override;

  int UnSchedule(void* arg, Priority pri) override;

  void StartThread(void (*function)(void* arg), void* arg) override;

  void WaitForJoin() override;

  unsigned int GetThreadPoolQueueLen(Priority pri = LOW) const override;

  Status GetTestDirectory(std::string* result) override {
    const char* env = getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
      *result = env;
    } else {
      char buf[100];
      snprintf(buf, sizeof(buf), "/tmp/rocksdbtest-%ud", geteuid());
      *result = buf;
    }
    // Directory may already exist
    if (!DirExists(*result)) {
      RETURN_NOT_OK(CreateDir(*result));
    }
    return Status::OK();
  }

  static uint64_t gettid(pthread_t tid) {
    uint64_t thread_id = 0;
    memcpy(&thread_id, &tid, std::min(sizeof(thread_id), sizeof(tid)));
    return thread_id;
  }

  static uint64_t gettid() {
    pthread_t tid = pthread_self();
    return gettid(tid);
  }

  uint64_t GetThreadID() const override {
    return gettid(pthread_self());
  }

  virtual Status NewLogger(const std::string& fname,
                           std::shared_ptr<Logger>* result) override {
    FILE* f;
    {
      IOSTATS_TIMER_GUARD(open_nanos);
      f = fopen(fname.c_str(), "w");
    }
    if (f == nullptr) {
      result->reset();
      return STATUS_IO_ERROR(fname, errno);
    } else {
      int fd = fileno(f);
#ifdef ROCKSDB_FALLOCATE_PRESENT
      if (fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 4 * 1024) != 0) {
        LOG(ERROR) << STATUS_IO_ERROR(fname, errno);
      }
#endif
      SetFD_CLOEXEC(fd, nullptr);
      result->reset(new PosixLogger(fname, f, &PosixEnv::gettid, this));
      return Status::OK();
    }
  }

  uint64_t NowMicros() override {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
  }

  uint64_t NowNanos() override {
#if defined(__linux__) || defined(OS_FREEBSD)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#elif defined(__MACH__)
    clock_serv_t cclock;
    mach_timespec_t ts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &ts);
    mach_port_deallocate(mach_task_self(), cclock);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
       std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
  }

  void SleepForMicroseconds(int micros) override { usleep(micros); }

  Status GetHostName(char* name, uint64_t len) override {
    int ret = gethostname(name, static_cast<size_t>(len));
    if (ret < 0) {
      if (errno == EFAULT || errno == EINVAL)
        return STATUS(InvalidArgument, strerror(errno));
      else
        return STATUS_IO_ERROR("GetHostName", errno);
    }
    return Status::OK();
  }

  Status GetCurrentTime(int64_t* unix_time) override {
    time_t ret = time(nullptr);
    if (ret == (time_t) -1) {
      return STATUS_IO_ERROR("GetCurrentTime", errno);
    }
    *unix_time = (int64_t) ret;
    return Status::OK();
  }

  virtual Status GetAbsolutePath(const std::string& db_path,
                                 std::string* output_path) override {
    if (db_path.find('/') == 0) {
      *output_path = db_path;
      return Status::OK();
    }

    char the_path[256];
    char* ret = getcwd(the_path, 256);
    if (ret == nullptr) {
      return STATUS(IOError, strerror(errno));
    }

    *output_path = ret;
    return Status::OK();
  }

  // Allow increasing the number of worker threads.
  void SetBackgroundThreads(int num, Priority pri) override {
    assert(pri >= Priority::LOW && pri <= Priority::HIGH);
    thread_pools_[pri].SetBackgroundThreads(num);
  }

  // Allow increasing the number of worker threads.
  void IncBackgroundThreadsIfNeeded(int num, Priority pri) override {
    assert(pri >= Priority::LOW && pri <= Priority::HIGH);
    thread_pools_[pri].IncBackgroundThreadsIfNeeded(num);
  }

  void LowerThreadPoolIOPriority(Priority pool = LOW) override {
    assert(pool >= Priority::LOW && pool <= Priority::HIGH);
#ifdef __linux__
    thread_pools_[pool].LowerIOPriority();
#endif
  }

  std::string TimeToString(uint64_t secondsSince1970) override {
    const time_t seconds = (time_t)secondsSince1970;
    struct tm t;
    int maxsize = 64;
    std::string dummy;
    dummy.reserve(maxsize);
    dummy.resize(maxsize);
    char* p = &dummy[0];
    localtime_r(&seconds, &t);
    snprintf(p, maxsize,
             "%04d/%02d/%02d-%02d:%02d:%02d ",
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
    return dummy;
  }

  EnvOptions OptimizeForLogWrite(const EnvOptions& env_options,
                                 const DBOptions& db_options) const override {
    EnvOptions optimized = env_options;
    optimized.use_mmap_writes = false;
    optimized.bytes_per_sync = db_options.wal_bytes_per_sync;
    // TODO(icanadi) it's faster if fallocate_with_keep_size is false, but it
    // breaks TransactionLogIteratorStallAtLastRecord unit test. Fix the unit
    // test and make this false
    optimized.fallocate_with_keep_size = true;
    return optimized;
  }

  EnvOptions OptimizeForManifestWrite(
      const EnvOptions& env_options) const override {
    EnvOptions optimized = env_options;
    optimized.use_mmap_writes = false;
    optimized.fallocate_with_keep_size = true;
    return optimized;
  }

  RocksDBFileFactory* GetFileFactory() {
    return file_factory_.get();
  }

 private:
  std::unique_ptr<RocksDBFileFactory> file_factory_;
  // Returns true iff the named directory exists and is a directory.
  bool DirExists(const std::string& dname) override {
    struct stat statbuf;
    if (stat(dname.c_str(), &statbuf) == 0) {
      return S_ISDIR(statbuf.st_mode);
    }
    return false; // stat() failed return false
  }

  bool SupportsFastAllocate(const std::string& path) {
#ifdef ROCKSDB_FALLOCATE_PRESENT
    struct statfs s;
    if (statfs(path.c_str(), &s)) {
      return false;
    }
    switch (s.f_type) {
      case EXT4_SUPER_MAGIC:
        return true;
      case XFS_SUPER_MAGIC:
        return true;
      case TMPFS_MAGIC:
        return true;
      default:
        return false;
    }
#else
    return false;
#endif
  }

  std::vector<ThreadPool> thread_pools_;
  pthread_mutex_t mu_;
  std::vector<pthread_t> threads_to_join_;
};

class PosixRocksDBFileFactory : public RocksDBFileFactory {
 public:
  PosixRocksDBFileFactory() {}

  ~PosixRocksDBFileFactory() {}

  Status NewSequentialFile(const std::string& fname,
                           std::unique_ptr<SequentialFile>* result,
                           const EnvOptions& options) override {
    result->reset();
    FILE* f = nullptr;
    do {
      IOSTATS_TIMER_GUARD(open_nanos);
      f = fopen(fname.c_str(), "r");
    } while (f == nullptr && errno == EINTR);
    if (f == nullptr) {
      *result = nullptr;
      return STATUS_IO_ERROR(fname, errno);
    } else {
      int fd = fileno(f);
      SetFD_CLOEXEC(fd, &options);
      *result = std::make_unique<yb::PosixSequentialFile>(fname, f, options);
      return Status::OK();
    }
  }

  Status NewRandomAccessFile(const std::string& fname,
                             std::unique_ptr<RandomAccessFile>* result,
                             const EnvOptions& options) override {
    result->reset();
    Status s;
    int fd;
    {
      IOSTATS_TIMER_GUARD(open_nanos);
      fd = open(fname.c_str(), O_RDONLY);
    }
    SetFD_CLOEXEC(fd, &options);
    if (fd < 0) {
      s = STATUS_IO_ERROR(fname, errno);
    } else if (options.use_mmap_reads && sizeof(void*) >= 8) {
      // Use of mmap for random reads has been removed because it
      // kills performance when storage is fast.
      // Use mmap when virtual address-space is plentiful.
      uint64_t size;
      s = GetFileSize(fname, &size);
      if (s.ok()) {
        void* base = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (base != MAP_FAILED) {
          *result = std::make_unique<PosixMmapReadableFile>(fd, fname, base, size, options);
        } else {
          s = STATUS_IO_ERROR(fname, errno);
        }
      }
      close(fd);
    } else {
      *result = std::make_unique<yb::PosixRandomAccessFile>(fname, fd, options);
    }
    return s;
  }

  Status NewWritableFile(const std::string& fname,
                         std::unique_ptr<WritableFile>* result,
                         const EnvOptions& options) override {
    result->reset();
    Status s;
    int fd = -1;
    do {
      IOSTATS_TIMER_GUARD(open_nanos);
      fd = open(fname.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
      s = STATUS_IO_ERROR(fname, errno);
    } else {
      SetFD_CLOEXEC(fd, &options);
      if (options.use_mmap_writes) {
        if (!checkedDiskForMmap_) {
          // this will be executed once in the program's lifetime.
          // do not use mmapWrite on non ext-3/xfs/tmpfs systems.
          if (!SupportsFastAllocate(fname)) {
            forceMmapOff = true;
          }
          checkedDiskForMmap_ = true;
        }
      }
      if (options.use_mmap_writes && !forceMmapOff) {
        *result = std::make_unique<PosixMmapFile>(fname, fd, page_size_, options);
      } else {
        // disable mmap writes
        EnvOptions no_mmap_writes_options = options;
        no_mmap_writes_options.use_mmap_writes = false;
        *result = std::make_unique<PosixWritableFile>(fname, fd, no_mmap_writes_options);
      }
    }
    return s;
  }

  Status ReuseWritableFile(const std::string& fname,
                           const std::string& old_fname,
                           std::unique_ptr<WritableFile>* result,
                           const EnvOptions& options) override {
    result->reset();
    Status s;
    int fd = -1;
    do {
      IOSTATS_TIMER_GUARD(open_nanos);
      fd = open(old_fname.c_str(), O_RDWR, 0644);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
      s = STATUS_IO_ERROR(fname, errno);
    } else {
      SetFD_CLOEXEC(fd, &options);
      // rename into place
      if (rename(old_fname.c_str(), fname.c_str()) != 0) {
        Status r = STATUS_IO_ERROR(old_fname, errno);
        close(fd);
        return r;
      }
      if (options.use_mmap_writes) {
        if (!checkedDiskForMmap_) {
          // this will be executed once in the program's lifetime.
          // do not use mmapWrite on non ext-3/xfs/tmpfs systems.
          if (!SupportsFastAllocate(fname)) {
            forceMmapOff = true;
          }
          checkedDiskForMmap_ = true;
        }
      }
      if (options.use_mmap_writes && !forceMmapOff) {
        *result = std::make_unique<PosixMmapFile>(fname, fd, page_size_, options);
      } else {
        // disable mmap writes
        EnvOptions no_mmap_writes_options = options;
        no_mmap_writes_options.use_mmap_writes = false;

        *result = std::make_unique<PosixWritableFile>(fname, fd, no_mmap_writes_options);
      }
    }
    return s;
  }

  Status GetFileSize(const std::string& fname, uint64_t* size) override {
    Status s;
    struct stat sbuf;
    if (stat(fname.c_str(), &sbuf) != 0) {
      *size = 0;
      s = STATUS_IO_ERROR(fname, errno);
    } else {
      *size = sbuf.st_size;
    }
    return s;
  }

  bool IsPlainText() const override {
    return true;
  }

 private:
  bool checkedDiskForMmap_ = false;
  bool forceMmapOff = false;
  size_t page_size_ = getpagesize();

  bool SupportsFastAllocate(const std::string& path) {
#ifdef ROCKSDB_FALLOCATE_PRESENT
    struct statfs s;
    if (statfs(path.c_str(), &s)) {
      return false;
    }
    switch (s.f_type) {
      case EXT4_SUPER_MAGIC:
        return true;
      case XFS_SUPER_MAGIC:
        return true;
      case TMPFS_MAGIC:
        return true;
      default:
        return false;
    }
#else
    return false;
#endif
  }

};

PosixEnv::PosixEnv()
    : file_factory_(std::make_unique<PosixRocksDBFileFactory>()), thread_pools_(Priority::TOTAL) {
  ThreadPool::PthreadCall("mutex_init", pthread_mutex_init(&mu_, nullptr));
  for (int pool_id = 0; pool_id < Env::Priority::TOTAL; ++pool_id) {
    thread_pools_[pool_id].SetThreadPriority(
        static_cast<Env::Priority>(pool_id));
    // This allows later initializing the thread-local-env of each thread.
    thread_pools_[pool_id].SetHostEnv(this);
  }
}

PosixEnv::PosixEnv(std::unique_ptr<RocksDBFileFactory> file_factory) :
    file_factory_(std::move(file_factory)), thread_pools_(Priority::TOTAL) {
  ThreadPool::PthreadCall("mutex_init", pthread_mutex_init(&mu_, nullptr));
  for (int pool_id = 0; pool_id < Env::Priority::TOTAL; ++pool_id) {
    thread_pools_[pool_id].SetThreadPriority(
        static_cast<Env::Priority>(pool_id));
    // This allows later initializing the thread-local-env of each thread.
    thread_pools_[pool_id].SetHostEnv(this);
  }
}

void PosixEnv::Schedule(void (*function)(void* arg1), void* arg, Priority pri,
                        void* tag, void (*unschedFunction)(void* arg)) {
  assert(pri >= Priority::LOW && pri <= Priority::HIGH);
  thread_pools_[pri].Schedule(function, arg, tag, unschedFunction);
}

int PosixEnv::UnSchedule(void* arg, Priority pri) {
  return thread_pools_[pri].UnSchedule(arg);
}

unsigned int PosixEnv::GetThreadPoolQueueLen(Priority pri) const {
  assert(pri >= Priority::LOW && pri <= Priority::HIGH);
  return thread_pools_[pri].GetQueueLen();
}

struct StartThreadState {
  void (*user_function)(void*);
  void* arg;
};

static void* StartThreadWrapper(void* arg) {
  StartThreadState* state = reinterpret_cast<StartThreadState*>(arg);
  state->user_function(state->arg);
  delete state;
  return nullptr;
}

void PosixEnv::StartThread(void (*function)(void* arg), void* arg) {
  pthread_t t;
  StartThreadState* state = new StartThreadState;
  state->user_function = function;
  state->arg = arg;
  ThreadPool::PthreadCall(
      "start thread", pthread_create(&t, nullptr, &StartThreadWrapper, state));
  ThreadPool::PthreadCall("lock", pthread_mutex_lock(&mu_));
  threads_to_join_.push_back(t);
  ThreadPool::PthreadCall("unlock", pthread_mutex_unlock(&mu_));
}

void PosixEnv::WaitForJoin() {
  for (const auto tid : threads_to_join_) {
    pthread_join(tid, nullptr);
  }
  threads_to_join_.clear();
}

}  // namespace

std::string Env::GenerateUniqueId() {
  std::string uuid_file = "/proc/sys/kernel/random/uuid";

  Status s = FileExists(uuid_file);
  if (s.ok()) {
    std::string uuid;
    s = ReadFileToString(this, uuid_file, &uuid);
    if (s.ok()) {
      return uuid;
    }
  }
  // Could not read uuid_file - generate uuid using "nanos-random"
  Random64 r(time(nullptr));
  uint64_t random_uuid_portion =
    r.Uniform(std::numeric_limits<uint64_t>::max());
  uint64_t nanos_uuid_portion = NowNanos();
  char uuid2[200];
  snprintf(uuid2,
           200,
           "%" PRIu64 "x-%" PRIu64 "x",
           nanos_uuid_portion,
           random_uuid_portion);
  return uuid2;
}

Env* Env::Default() {
  // The following function call initializes the singletons of ThreadLocalPtr
  // right before the static default_env.  This guarantees default_env will
  // always being destructed before the ThreadLocalPtr singletons get
  // destructed as C++ guarantees that the destructions of static variables
  // is in the reverse order of their constructions.
  //
  // Since static members are destructed in the reverse order
  // of their construction, having this call here guarantees that
  // the destructor of static PosixEnv will go first, then the
  // the singletons of ThreadLocalPtr.
  ThreadLocalPtr::InitSingletons();
  // Make sure that SyncPoint inited before PosixEnv, to be deleted after it.
#ifndef NDEBUG
  SyncPoint::GetInstance();
#endif
  static PosixEnv default_env;
  return &default_env;
}

RocksDBFileFactory* Env::DefaultFileFactory() {
  return down_cast<PosixEnv*>(Env::Default())->GetFileFactory();
}

std::unique_ptr<Env> Env::NewRocksDBDefaultEnv(
    std::unique_ptr<RocksDBFileFactory> file_factory) {
  return std::make_unique<PosixEnv>(std::move(file_factory));
}

}  // namespace rocksdb
