// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// An Env is an interface used by the rocksdb implementation to access
// operating system functionality like the filesystem etc.  Callers
// may wish to provide a custom Env object when opening a database to
// get fine gain control; e.g., to rate limit file system operations.
//
// All Env implementations are safe for concurrent access from
// multiple threads without any external synchronization.

#ifndef YB_ROCKSDB_ENV_H
#define YB_ROCKSDB_ENV_H

#include <stdint.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "yb/util/status_fwd.h"
#include "yb/util/file_system.h"
#include "yb/util/slice.h"

#ifdef _WIN32
// Windows API macro interference
#undef DeleteFile
#undef GetCurrentTime
#endif

#define RLOG(...) LogWithContext(__FILE__, __LINE__, ##__VA_ARGS__)
#define RHEADER(...) HeaderWithContext(__FILE__, __LINE__, ##__VA_ARGS__)
#define RDEBUG(...) DebugWithContext(__FILE__, __LINE__, ##__VA_ARGS__)
#define RINFO(...) InfoWithContext(__FILE__, __LINE__, ##__VA_ARGS__)
#define RWARN(...) WarnWithContext(__FILE__, __LINE__, ##__VA_ARGS__)
#define RERROR(...) ErrorWithContext(__FILE__, __LINE__, ##__VA_ARGS__)
#define RFATAL(...) FatalWithContext(__FILE__, __LINE__, ##__VA_ARGS__)

#ifdef NDEBUG
// TODO: it should also fail in release under tests: https://yugabyte.atlassian.net/browse/ENG-1453.
// Idea is to fail in both release and debug builds under tests, but don't fail in production.
// This is useful when some secondary functionality is broken and we don't want to fail the whole
// server, we can just turn that functionality off. But under tests and in debug mode we need to
// fail to be able to catch bugs quickly. One of examples of such functionality is bloom filters.
#define FAIL_IF_NOT_PRODUCTION() do {} while (false)
#else
#define FAIL_IF_NOT_PRODUCTION() assert(false)
#endif


namespace rocksdb {

class FileLock;
class Logger;
class WritableFile;
class Directory;
struct DBOptions;
class RateLimiter;

typedef yb::SequentialFile SequentialFile;
typedef yb::RandomAccessFile RandomAccessFile;

using std::unique_ptr;
using std::shared_ptr;

using Status = yb::Status;

// Options while opening a file to read/write
struct EnvOptions : public yb::FileSystemOptions {

  // construct with default Options
  EnvOptions();

  // construct from Options
  explicit EnvOptions(const DBOptions& options);

  // If true, then use mmap to write data
  bool use_mmap_writes = true;

  // If false, fallocate() calls are bypassed
  bool allow_fallocate = true;

  // If true, set the FD_CLOEXEC on open fd.
  bool set_fd_cloexec = true;

  // Allows OS to incrementally sync files to disk while they are being
  // written, in the background. Issue one request for every bytes_per_sync
  // written. 0 turns it off.
  // Default: 0
  uint64_t bytes_per_sync = 0;

  // If true, we will preallocate the file with FALLOC_FL_KEEP_SIZE flag, which
  // means that file size won't change as part of preallocation.
  // If false, preallocation will also change the file size. This option will
  // improve the performance in workloads where you sync the data on every
  // write. By default, we set it to true for MANIFEST writes and false for
  // WAL writes
  bool fallocate_with_keep_size = true;

  // See DBOPtions doc
  size_t compaction_readahead_size;

  // See DBOPtions doc
  size_t random_access_max_buffer_size;

  // See DBOptions doc
  size_t writable_file_max_buffer_size = 1024 * 1024;

  // If not nullptr, write rate limiting is enabled for flush and compaction
  RateLimiter* rate_limiter = nullptr;
};

// RocksDBFileFactory is the implementation of all NewxxxFile Env methods as well as any methods
// that are used to create new files. This class is created to allow easy definition of how we
// create new files without inheriting the whole env.
class RocksDBFileFactory {
 public:
  virtual ~RocksDBFileFactory() {}
  virtual Status NewSequentialFile(const std::string& f, unique_ptr<SequentialFile>* r,
                                           const EnvOptions& options) = 0;
  virtual Status NewRandomAccessFile(const std::string& f,
                                             unique_ptr<RandomAccessFile>* r,
                                             const EnvOptions& options) = 0;
  virtual Status NewWritableFile(const std::string& f, unique_ptr<WritableFile>* r,
                                         const EnvOptions& options) = 0;
  virtual Status ReuseWritableFile(const std::string& fname,
                                   const std::string& old_fname,
                                   unique_ptr<WritableFile>* result,
                                   const EnvOptions& options) = 0;
  virtual Status GetFileSize(const std::string& fname, uint64_t* size) = 0;

  // Does the file factory produce plaintext files.
  virtual bool IsPlainText() const = 0;
};

class RocksDBFileFactoryWrapper : public rocksdb::RocksDBFileFactory {
 public:
  explicit RocksDBFileFactoryWrapper(rocksdb::RocksDBFileFactory* t) : target_(t) {}

  virtual ~RocksDBFileFactoryWrapper() {}

  // The following text is boilerplate that forwards all methods to target()
  Status NewSequentialFile(const std::string& f, unique_ptr<SequentialFile>* r,
                           const rocksdb::EnvOptions& options) override;
  Status NewRandomAccessFile(const std::string& f,
                             unique_ptr <rocksdb::RandomAccessFile>* r,
                             const EnvOptions& options) override;
  Status NewWritableFile(const std::string& f, unique_ptr <rocksdb::WritableFile>* r,
                         const EnvOptions& options) override;

  Status ReuseWritableFile(const std::string& fname,
                           const std::string& old_fname,
                           unique_ptr<WritableFile>* result,
                           const EnvOptions& options) override;

  Status GetFileSize(const std::string& fname, uint64_t* size) override;

  bool IsPlainText() const override {
    return target_->IsPlainText();
  }

 private:
  rocksdb::RocksDBFileFactory* target_;
};

class Env {
 public:
  struct FileAttributes {
    // File name
    std::string name;

    // Size of file in bytes
    uint64_t size_bytes;
  };

  Env() {}

  virtual ~Env();

  // Return a default environment suitable for the current operating
  // system.  Sophisticated users may wish to provide their own Env
  // implementation instead of relying on this default environment.
  //
  // The result of Default() belongs to rocksdb and must never be deleted.
  static Env* Default();

  static RocksDBFileFactory* DefaultFileFactory();

  static std::unique_ptr<Env> NewRocksDBDefaultEnv(
      std::unique_ptr<RocksDBFileFactory> file_factory);

  // Create a brand new sequentially-readable file with the specified name.
  // On success, stores a pointer to the new file in *result and returns OK.
  // On failure stores nullptr in *result and returns non-OK.  If the file does
  // not exist, returns a non-OK status.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewSequentialFile(const std::string& fname,
                                   std::unique_ptr<SequentialFile>* result,
                                   const EnvOptions& options)
                                   = 0;

  // Create a brand new random access read-only file with the
  // specified name.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores nullptr in *result and
  // returns non-OK.  If the file does not exist, returns a non-OK
  // status.
  //
  // The returned file may be concurrently accessed by multiple threads.
  virtual Status NewRandomAccessFile(const std::string& fname,
                                     std::unique_ptr<RandomAccessFile>* result,
                                     const EnvOptions& options)
                                     = 0;

  // Create an object that writes to a new file with the specified
  // name.  Deletes any existing file with the same name and creates a
  // new file.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores nullptr in *result and
  // returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewWritableFile(const std::string& fname,
                                 unique_ptr<WritableFile>* result,
                                 const EnvOptions& options) = 0;

  // Reuse an existing file by renaming it and opening it as writable.
  virtual Status ReuseWritableFile(const std::string& fname,
                                   const std::string& old_fname,
                                   unique_ptr<WritableFile>* result,
                                   const EnvOptions& options);

  // Create an object that represents a directory. Will fail if directory
  // doesn't exist. If the directory exists, it will open the directory
  // and create a new Directory object.
  //
  // On success, stores a pointer to the new Directory in
  // *result and returns OK. On failure stores nullptr in *result and
  // returns non-OK.
  virtual Status NewDirectory(const std::string& name,
                              unique_ptr<Directory>* result) = 0;

  // Returns OK if the named file exists.
  //         NotFound if the named file does not exist,
  //                  the calling process does not have permission to determine
  //                  whether this file exists, or if the path is invalid.
  //         IOError if an IO Error was encountered
  virtual Status FileExists(const std::string& fname) = 0;

  virtual bool DirExists(const std::string& fname) { return false; }

  // Store in *result the names of the children of the specified directory.
  // The names are relative to "dir".
  // Original contents of *results are dropped.
  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) = 0;

  void GetChildrenWarnNotOk(const std::string& dir,
                            std::vector<std::string>* result);

  // Store in *result the attributes of the children of the specified directory.
  // In case the implementation lists the directory prior to iterating the files
  // and files are concurrently deleted, the deleted files will be omitted from
  // result.
  // The name attributes are relative to "dir".
  // Original contents of *results are dropped.
  virtual Status GetChildrenFileAttributes(const std::string& dir,
                                           std::vector<FileAttributes>* result);

  // Delete the named file.
  virtual Status DeleteFile(const std::string& fname) = 0;

  // Delete file, print warning on failure.
  void CleanupFile(const std::string& fname);

  // Create the specified directory. Returns error if directory exists.
  virtual Status CreateDir(const std::string& dirname) = 0;

  // Creates directory if missing. Return Ok if it exists, or successful in
  // Creating.
  virtual Status CreateDirIfMissing(const std::string& dirname) = 0;

  // Delete the specified directory.
  virtual Status DeleteDir(const std::string& dirname) = 0;

  // Store the size of fname in *file_size.
  virtual Status GetFileSize(const std::string& fname, uint64_t* file_size) = 0;

  yb::Result<uint64_t> GetFileSize(const std::string& fname);

  // Store the last modification time of fname in *file_mtime.
  virtual Status GetFileModificationTime(const std::string& fname,
                                         uint64_t* file_mtime) = 0;
  // Rename file src to target.
  virtual Status RenameFile(const std::string& src,
                            const std::string& target) = 0;

  // Hard Link file src to target.
  virtual Status LinkFile(const std::string& src, const std::string& target);

  // Lock the specified file.  Used to prevent concurrent access to
  // the same db by multiple processes.  On failure, stores nullptr in
  // *lock and returns non-OK.
  //
  // On success, stores a pointer to the object that represents the
  // acquired lock in *lock and returns OK.  The caller should call
  // UnlockFile(*lock) to release the lock.  If the process exits,
  // the lock will be automatically released.
  //
  // If somebody else already holds the lock, finishes immediately
  // with a failure.  I.e., this call does not wait for existing locks
  // to go away.
  //
  // May create the named file if it does not already exist.
  virtual Status LockFile(const std::string& fname, FileLock** lock) = 0;

  // Release the lock acquired by a previous successful call to LockFile.
  // REQUIRES: lock was returned by a successful LockFile() call
  // REQUIRES: lock has not already been unlocked.
  virtual Status UnlockFile(FileLock* lock) = 0;

  // Priority for scheduling job in thread pool
  enum Priority { LOW, HIGH, TOTAL };

  // Priority for requesting bytes in rate limiter scheduler
  enum IOPriority {
    IO_LOW = 0,
    IO_HIGH = 1,
    IO_TOTAL = 2
  };

  // Arrange to run "(*function)(arg)" once in a background thread, in
  // the thread pool specified by pri. By default, jobs go to the 'LOW'
  // priority thread pool.

  // "function" may run in an unspecified thread.  Multiple functions
  // added to the same Env may run concurrently in different threads.
  // I.e., the caller may not assume that background work items are
  // serialized.
  // When the UnSchedule function is called, the unschedFunction
  // registered at the time of Schedule is invoked with arg as a parameter.
  virtual void Schedule(void (*function)(void* arg), void* arg,
                        Priority pri = LOW, void* tag = nullptr,
                        void (*unschedFunction)(void* arg) = 0) = 0;

  // Arrange to remove jobs for given arg from the queue_ if they are not
  // already scheduled. Caller is expected to have exclusive lock on arg.
  virtual int UnSchedule(void* arg, Priority pri) { return 0; }

  // Start a new thread, invoking "function(arg)" within the new thread.
  // When "function(arg)" returns, the thread will be destroyed.
  virtual void StartThread(void (*function)(void* arg), void* arg) = 0;

  // Wait for all threads started by StartThread to terminate.
  virtual void WaitForJoin() {}

  // Get thread pool queue length for specific thrad pool.
  virtual unsigned int GetThreadPoolQueueLen(Priority pri = LOW) const {
    return 0;
  }

  // *path is set to a temporary directory that can be used for testing. It may
  // or many not have just been created. The directory may or may not differ
  // between runs of the same process, but subsequent calls will return the
  // same directory.
  virtual Status GetTestDirectory(std::string* path) = 0;

  // Create and return a log file for storing informational messages.
  virtual Status NewLogger(const std::string& fname,
                           shared_ptr<Logger>* result) = 0;

  // Returns the number of micro-seconds since some fixed point in time. Only
  // useful for computing deltas of time.
  // However, it is often used as system time such as in GenericRateLimiter
  // and other places so a port needs to return system time in order to work.
  virtual uint64_t NowMicros() = 0;

  // Returns the number of nano-seconds since some fixed point in time. Only
  // useful for computing deltas of time in one run.
  // Default implementation simply relies on NowMicros
  virtual uint64_t NowNanos() {
    return NowMicros() * 1000;
  }

  // Sleep/delay the thread for the perscribed number of micro-seconds.
  virtual void SleepForMicroseconds(int micros) = 0;

  // Get the current host name.
  virtual Status GetHostName(char* name, uint64_t len) = 0;

  // Get the number of seconds since the Epoch, 1970-01-01 00:00:00 (UTC).
  virtual Status GetCurrentTime(int64_t* unix_time) = 0;

  // Get full directory name for this db.
  virtual Status GetAbsolutePath(const std::string& db_path,
      std::string* output_path) = 0;

  // The number of background worker threads of a specific thread pool
  // for this environment. 'LOW' is the default pool.
  // default number: 1
  virtual void SetBackgroundThreads(int number, Priority pri = LOW) = 0;

  // Enlarge number of background worker threads of a specific thread pool
  // for this environment if it is smaller than specified. 'LOW' is the default
  // pool.
  virtual void IncBackgroundThreadsIfNeeded(int number, Priority pri) = 0;

  // Lower IO priority for threads from the specified pool.
  virtual void LowerThreadPoolIOPriority(Priority pool = LOW) {}

  // Converts seconds-since-Jan-01-1970 to a printable string
  virtual std::string TimeToString(uint64_t time) = 0;

  // Generates a unique id that can be used to identify a db
  virtual std::string GenerateUniqueId();

  // OptimizeForLogWrite will create a new EnvOptions object that is a copy of
  // the EnvOptions in the parameters, but is optimized for writing log files.
  // Default implementation returns the copy of the same object.
  virtual EnvOptions OptimizeForLogWrite(const EnvOptions& env_options,
                                         const DBOptions& db_options) const;
  // OptimizeForManifestWrite will create a new EnvOptions object that is a copy
  // of the EnvOptions in the parameters, but is optimized for writing manifest
  // files. Default implementation returns the copy of the same object.
  virtual EnvOptions OptimizeForManifestWrite(const EnvOptions& env_options)
      const;

  virtual bool IsPlainText() const {
    return true;
  }

  // Returns the ID of the current thread.
  virtual uint64_t GetThreadID() const;

 private:
  // No copying allowed
  Env(const Env&);
  void operator=(const Env&);
};

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
class WritableFile : public yb::FileWithUniqueId {
 public:
  WritableFile()
    : last_preallocated_block_(0),
      preallocation_block_size_(0),
      io_priority_(Env::IO_TOTAL) {
  }
  virtual ~WritableFile();

  // Indicates if the class makes use of unbuffered I/O
  virtual bool UseOSBuffer() const {
    return true;
  }

  const size_t c_DefaultPageSize = 4 * 1024;

  // This is needed when you want to allocate
  // AlignedBuffer for use with file I/O classes
  // Used for unbuffered file I/O when UseOSBuffer() returns false
  virtual size_t GetRequiredBufferAlignment() const {
    return c_DefaultPageSize;
  }

  virtual Status Append(const Slice& data) = 0;

  // Positioned write for unbuffered access default forward
  // to simple append as most of the tests are buffered by default
  virtual Status PositionedAppend(const Slice& /* data */, uint64_t /* offset */);

  // Truncate is necessary to trim the file to the correct size
  // before closing. It is not always possible to keep track of the file
  // size due to whole pages writes. The behavior is undefined if called
  // with other writes to follow.
  virtual Status Truncate(uint64_t size);
  virtual Status Close() = 0;
  virtual Status Flush() = 0;
  virtual Status Sync() = 0; // sync data

  /*
   * Sync data and/or metadata as well.
   * By default, sync only data.
   * Override this method for environments where we need to sync
   * metadata as well.
   */
  virtual Status Fsync();

  // true if Sync() and Fsync() are safe to call concurrently with Append()
  // and Flush().
  virtual bool IsSyncThreadSafe() const {
    return false;
  }

  // Indicates the upper layers if the current WritableFile implementation
  // uses direct IO.
  virtual bool UseDirectIO() const { return false; }

  /*
   * Change the priority in rate limiter if rate limiting is enabled.
   * If rate limiting is not enabled, this call has no effect.
   */
  virtual void SetIOPriority(Env::IOPriority pri) {
    io_priority_ = pri;
  }

  virtual Env::IOPriority GetIOPriority() { return io_priority_; }

  /*
   * Get the size of valid data in the file.
   */
  virtual uint64_t GetFileSize() {
    return 0;
  }

  /*
   * Get and set the default pre-allocation block size for writes to
   * this file.  If non-zero, then Allocate will be used to extend the
   * underlying storage of a file (generally via fallocate) if the Env
   * instance supports it.
   */
  void SetPreallocationBlockSize(size_t size) {
    preallocation_block_size_ = size;
  }

  virtual void GetPreallocationStatus(size_t* block_size,
                                      size_t* last_allocated_block) {
    *last_allocated_block = last_preallocated_block_;
    *block_size = preallocation_block_size_;
  }

  // For documentation, refer to File::GetUniqueId()
  virtual size_t GetUniqueId(char* id) const override {
    return 0; // Default implementation to prevent issues with backwards
  }

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  // This call has no effect on dirty pages in the cache.
  virtual Status InvalidateCache(size_t offset, size_t length);

  // Sync a file range with disk.
  // offset is the starting byte of the file range to be synchronized.
  // nbytes specifies the length of the range to be synchronized.
  // This asks the OS to initiate flushing the cached data to disk,
  // without waiting for completion.
  // Default implementation does nothing.
  virtual Status RangeSync(uint64_t offset, uint64_t nbytes);

  // PrepareWrite performs any necessary preparation for a write
  // before the write actually occurs.  This allows for pre-allocation
  // of space on devices where it can result in less file
  // fragmentation and/or less waste from over-zealous filesystem
  // pre-allocation.
  void PrepareWrite(size_t offset, size_t len);

  // Returns the filename provided when the WritableFile was constructed.
  virtual const std::string& filename() const = 0;

 protected:
  /*
   * Pre-allocate space for a file.
   */
  virtual Status Allocate(uint64_t offset, uint64_t len);

  size_t preallocation_block_size() { return preallocation_block_size_; }

 private:
  size_t last_preallocated_block_;
  size_t preallocation_block_size_;
  // No copying allowed
  WritableFile(const WritableFile&);
  void operator=(const WritableFile&);

 protected:
  friend class WritableFileWrapper;
  friend class WritableFileMirror;

  Env::IOPriority io_priority_;
};

// Directory object represents collection of files and implements
// filesystem operations that can be executed on directories.
class Directory {
 public:
  virtual ~Directory() {}
  // Fsync directory. Can be called concurrently from multiple threads.
  virtual Status Fsync() = 0;
};

enum InfoLogLevel : unsigned char {
  DEBUG_LEVEL = 0,
  INFO_LEVEL,
  WARN_LEVEL,
  ERROR_LEVEL,
  FATAL_LEVEL,
  HEADER_LEVEL,
  NUM_INFO_LOG_LEVELS,
};

// An interface for writing log messages.
class Logger {
 public:
  size_t kDoNotSupportGetLogFileSize = std::numeric_limits<size_t>::max();

  explicit Logger(const InfoLogLevel log_level = InfoLogLevel::INFO_LEVEL)
      : log_level_(log_level) {

  }
  virtual ~Logger();

  // Write a header to the log file with the specified format
  // It is recommended that you log all header information at the start of the
  // application. But it is not enforced.
  virtual void LogHeaderWithContext(const char* file, const int line,
      const char *format, va_list ap) {
    // Default implementation does a simple INFO level log write.
    // Please override as per the logger class requirement.
    LogvWithContext(file, line, InfoLogLevel::HEADER_LEVEL, format, ap);
  }

  // Write an entry to the log file with the specified format.
  virtual void Logv(const char* format, va_list ap) = 0;

  // Write an entry to the log file with the specified log level
  // and format.  Any log with level under the internal log level
  // of *this (see @SetInfoLogLevel and @GetInfoLogLevel) will not be
  // printed.
  virtual void Logv(const rocksdb::InfoLogLevel log_level, const char* format, va_list ap);

  // A version of the Logv function that takes the file and line number of the caller.
  // Accomplished by the RLOG() macro with the __FILE__ and __LINE__ attributes.
  virtual void LogvWithContext(const char* file,
                               const int line,
                               const InfoLogLevel log_level,
                               const char* format,
                               va_list ap);

  virtual size_t GetLogFileSize() const { return kDoNotSupportGetLogFileSize; }
  // Flush to the OS buffers
  virtual void Flush() {}
  virtual InfoLogLevel GetInfoLogLevel() const { return log_level_; }
  virtual void SetInfoLogLevel(const InfoLogLevel log_level) {
    log_level_ = log_level;
  }

  virtual const std::string& Prefix() const {
    static const std::string kEmptyString;
    return kEmptyString;
  }

 private:
  // No copying allowed
  Logger(const Logger&);
  void operator=(const Logger&);
  InfoLogLevel log_level_;
};


// Identifies a locked file.
class FileLock {
 public:
  FileLock() { }
  virtual ~FileLock();
 private:
  // No copying allowed
  FileLock(const FileLock&);
  void operator=(const FileLock&);
};

extern void LogFlush(const shared_ptr<Logger>& info_log);

extern void LogWithContext(const char* file,
                           const int line,
                           const InfoLogLevel log_level,
                           const shared_ptr<Logger>& info_log,
                           const char* format,
                           ...);

// a set of log functions with different log levels.
extern void HeaderWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...);
extern void DebugWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...);
extern void InfoWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...);
extern void WarnWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...);
extern void ErrorWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...);
extern void FatalWithContext(
    const char* file,
    const int line,
    const shared_ptr<Logger> &info_log,
    const char *format, ...);

// Log the specified data to *info_log if info_log is non-nullptr.
// The default info log level is InfoLogLevel::ERROR.
extern void LogWithContext(const char* file,
                           const int line,
                           const shared_ptr<Logger>& info_log,
                           const char* format,
                           ...)
#   if defined(__GNUC__) || defined(__clang__)
    __attribute__((__format__ (__printf__, 4, 5)))
#   endif
    ; // NOLINT(whitespace/semicolon)

extern void LogFlush(Logger *info_log);

extern void LogWithContext(const char* file,
                           const int line,
                           const InfoLogLevel log_level,
                           Logger* info_log,
                           const char* format,
                           ...);

// The default info log level is InfoLogLevel::ERROR.
extern void LogWithContext(const char* file,
                           const int line,
                           Logger* info_log,
                           const char* format,
                           ...)
#   if defined(__GNUC__) || defined(__clang__)
    __attribute__((__format__ (__printf__, 4, 5)))
#   endif
    ; // NOLINT(whitespace/semicolon)

// a set of log functions with different log levels.
extern void HeaderWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...);
extern void DebugWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...);
extern void InfoWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...);
extern void WarnWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...);
extern void ErrorWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...);
extern void FatalWithContext(const char* file, const int line,
    Logger *info_log, const char *format, ...);

// A utility routine: write "data" to the named file.
extern Status WriteStringToFile(Env* env, const Slice& data,
                                const std::string& fname,
                                bool should_sync = false);

// A utility routine: read contents of named file into *data
extern Status ReadFileToString(Env* env, const std::string& fname,
                               std::string* data);

// An implementation of Env that forwards all calls to another Env.
// May be useful to clients who wish to override just part of the
// functionality of another Env.
class EnvWrapper : public Env {
 public:
  // Initialize an EnvWrapper that delegates all calls to *t
  explicit EnvWrapper(Env* t) : target_(t) { }
  virtual ~EnvWrapper();

  // Return the target to which this Env forwards all calls
  Env* target() const { return target_; }

  // The following text is boilerplate that forwards all methods to target()
  Status NewSequentialFile(const std::string& f, std::unique_ptr<SequentialFile>* r,
                           const EnvOptions& options) override;
  Status NewRandomAccessFile(const std::string& f,
                             unique_ptr<RandomAccessFile>* r,
                             const EnvOptions& options) override;
  Status NewWritableFile(const std::string& f, unique_ptr<WritableFile>* r,
                         const EnvOptions& options) override;
  Status ReuseWritableFile(const std::string& fname,
                           const std::string& old_fname,
                           unique_ptr<WritableFile>* r,
                           const EnvOptions& options) override;
  virtual Status NewDirectory(const std::string& name,
                              unique_ptr<Directory>* result) override;
  Status FileExists(const std::string& f) override;

  bool DirExists(const std::string& f) override {
    return target_->DirExists(f);
  }

  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* r) override;
  Status GetChildrenFileAttributes(
      const std::string& dir, std::vector<FileAttributes>* result) override;
  Status DeleteFile(const std::string& f) override;
  Status CreateDir(const std::string& d) override;
  Status CreateDirIfMissing(const std::string& d) override;
  Status DeleteDir(const std::string& d) override;
  Status GetFileSize(const std::string& f, uint64_t* s) override;

  Status GetFileModificationTime(const std::string& fname,
                                 uint64_t* file_mtime) override;

  Status RenameFile(const std::string& s, const std::string& t) override;

  Status LinkFile(const std::string& s, const std::string& t) override;

  Status LockFile(const std::string& f, FileLock** l) override;

  Status UnlockFile(FileLock* l) override;

  void Schedule(void (*f)(void* arg), void* a, Priority pri,
                void* tag = nullptr, void (*u)(void* arg) = 0) override {
    return target_->Schedule(f, a, pri, tag, u);
  }

  int UnSchedule(void* tag, Priority pri) override {
    return target_->UnSchedule(tag, pri);
  }

  void StartThread(void (*f)(void*), void* a) override {
    return target_->StartThread(f, a);
  }
  void WaitForJoin() override { return target_->WaitForJoin(); }
  virtual unsigned int GetThreadPoolQueueLen(
      Priority pri = LOW) const override {
    return target_->GetThreadPoolQueueLen(pri);
  }
  virtual Status GetTestDirectory(std::string* path) override;
  virtual Status NewLogger(const std::string& fname,
                           shared_ptr<Logger>* result) override;
  uint64_t NowMicros() override { return target_->NowMicros(); }
  void SleepForMicroseconds(int micros) override {
    target_->SleepForMicroseconds(micros);
  }
  Status GetHostName(char* name, uint64_t len) override;
  Status GetCurrentTime(int64_t* unix_time) override;
  Status GetAbsolutePath(const std::string& db_path,
                         std::string* output_path) override;
  void SetBackgroundThreads(int num, Priority pri) override {
    return target_->SetBackgroundThreads(num, pri);
  }

  void IncBackgroundThreadsIfNeeded(int num, Priority pri) override {
    return target_->IncBackgroundThreadsIfNeeded(num, pri);
  }

  void LowerThreadPoolIOPriority(Priority pool = LOW) override {
    target_->LowerThreadPoolIOPriority(pool);
  }

  std::string TimeToString(uint64_t time) override {
    return target_->TimeToString(time);
  }

  uint64_t GetThreadID() const override {
    return target_->GetThreadID();
  }

  bool IsPlainText() const override {
    return target_->IsPlainText();
  }

 private:
  Env* target_;
};

// An implementation of WritableFile that forwards all calls to another
// WritableFile. May be useful to clients who wish to override just part of the
// functionality of another WritableFile.
// It's declared as friend of WritableFile to allow forwarding calls to
// protected virtual methods.
class WritableFileWrapper : public WritableFile {
 public:
  explicit WritableFileWrapper(std::unique_ptr<WritableFile> t) : target_(std::move(t)) { }

  Status Append(const Slice& data) override;
  Status PositionedAppend(const Slice& data, uint64_t offset) override;
  Status Truncate(uint64_t size) override;
  Status Close() override;
  Status Flush() override;
  Status Sync() override;
  Status Fsync() override;
  bool IsSyncThreadSafe() const override { return target_->IsSyncThreadSafe(); }
  void SetIOPriority(Env::IOPriority pri) override {
    target_->SetIOPriority(pri);
  }
  Env::IOPriority GetIOPriority() override { return target_->GetIOPriority(); }
  uint64_t GetFileSize() override { return target_->GetFileSize(); }
  void GetPreallocationStatus(size_t* block_size,
                              size_t* last_allocated_block) override {
    target_->GetPreallocationStatus(block_size, last_allocated_block);
  }
  size_t GetUniqueId(char* id) const override {
    return target_->GetUniqueId(id);
  }
  Status InvalidateCache(size_t offset, size_t length) override;

  const std::string& filename() const override { return target_->filename(); }

 protected:
  Status Allocate(uint64_t offset, uint64_t len) override;
  Status RangeSync(uint64_t offset, uint64_t nbytes) override;

 private:
  std::unique_ptr<WritableFile> target_;
};

// Returns a new environment that stores its data in memory and delegates
// all non-file-storage tasks to base_env. The caller must delete the result
// when it is no longer needed.
// *base_env must remain live while the result is in use.
Env* NewMemEnv(Env* base_env);

// Returns a new environment that is used for HDFS environment.
// This is a factory method for HdfsEnv declared in hdfs/env_hdfs.h
Status NewHdfsEnv(Env** hdfs_env, const std::string& fsname);

}  // namespace rocksdb

#endif // YB_ROCKSDB_ENV_H
