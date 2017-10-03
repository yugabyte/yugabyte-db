// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// An Env is an interface used by the kudu implementation to access
// operating system functionality like the filesystem etc.  Callers
// may wish to provide a custom Env object when opening a database to
// get fine gain control; e.g., to rate limit file system operations.
//
// All Env implementations are safe for concurrent access from
// multiple threads without any external synchronization.

#ifndef STORAGE_LEVELDB_INCLUDE_ENV_H_
#define STORAGE_LEVELDB_INCLUDE_ENV_H_

#include <stdint.h>
#include <cstdarg>
#include <string>
#include <vector>

#include "kudu/gutil/callback_forward.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/util/status.h"

namespace kudu {

class FileLock;
class RandomAccessFile;
class RWFile;
class SequentialFile;
class Slice;
class WritableFile;

struct RandomAccessFileOptions;
struct RWFileOptions;
struct WritableFileOptions;

class Env {
 public:
  // Governs if/how the file is created.
  //
  // enum value                      | file exists       | file does not exist
  // --------------------------------+-------------------+--------------------
  // CREATE_IF_NON_EXISTING_TRUNCATE | opens + truncates | creates
  // CREATE_NON_EXISTING             | fails             | creates
  // OPEN_EXISTING                   | opens             | fails
  enum CreateMode {
    CREATE_IF_NON_EXISTING_TRUNCATE,
    CREATE_NON_EXISTING,
    OPEN_EXISTING
  };

  Env() { }
  virtual ~Env();

  // Return a default environment suitable for the current operating
  // system.  Sophisticated users may wish to provide their own Env
  // implementation instead of relying on this default environment.
  //
  // The result of Default() belongs to kudu and must never be deleted.
  static Env* Default();

  // Create a brand new sequentially-readable file with the specified name.
  // On success, stores a pointer to the new file in *result and returns OK.
  // On failure stores NULL in *result and returns non-OK.  If the file does
  // not exist, returns a non-OK status.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewSequentialFile(const std::string& fname,
                                   gscoped_ptr<SequentialFile>* result) = 0;

  // Create a brand new random access read-only file with the
  // specified name.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores NULL in *result and
  // returns non-OK.  If the file does not exist, returns a non-OK
  // status.
  //
  // The returned file may be concurrently accessed by multiple threads.
  virtual Status NewRandomAccessFile(const std::string& fname,
                                     gscoped_ptr<RandomAccessFile>* result) = 0;

  // Like the previous NewRandomAccessFile, but allows options to be specified.
  virtual Status NewRandomAccessFile(const RandomAccessFileOptions& opts,
                                     const std::string& fname,
                                     gscoped_ptr<RandomAccessFile>* result) = 0;

  // Create an object that writes to a new file with the specified
  // name.  Deletes any existing file with the same name and creates a
  // new file.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores NULL in *result and
  // returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewWritableFile(const std::string& fname,
                                 gscoped_ptr<WritableFile>* result) = 0;


  // Like the previous NewWritableFile, but allows options to be
  // specified.
  virtual Status NewWritableFile(const WritableFileOptions& opts,
                                 const std::string& fname,
                                 gscoped_ptr<WritableFile>* result) = 0;

  // Creates a new WritableFile provided the name_template parameter.
  // The last six characters of name_template must be "XXXXXX" and these are
  // replaced with a string that makes the filename unique.
  // The resulting created filename, if successful, will be stored in the
  // created_filename out parameter.
  // The file is created with permissions 0600, that is, read plus write for
  // owner only. The implementation will create the file in a secure manner,
  // and will return an error Status if it is unable to open the file.
  virtual Status NewTempWritableFile(const WritableFileOptions& opts,
                                     const std::string& name_template,
                                     std::string* created_filename,
                                     gscoped_ptr<WritableFile>* result) = 0;

  // Creates a new readable and writable file. If a file with the same name
  // already exists on disk, it is deleted.
  //
  // Some of the methods of the new file may be accessed concurrently,
  // while others are only safe for access by one thread at a time.
  virtual Status NewRWFile(const std::string& fname,
                           gscoped_ptr<RWFile>* result) = 0;

  // Like the previous NewRWFile, but allows options to be specified.
  virtual Status NewRWFile(const RWFileOptions& opts,
                           const std::string& fname,
                           gscoped_ptr<RWFile>* result) = 0;

  // Returns true iff the named file exists.
  virtual bool FileExists(const std::string& fname) = 0;

  // Store in *result the names of the children of the specified directory.
  // The names are relative to "dir".
  // Original contents of *results are dropped.
  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) = 0;

  // Delete the named file.
  virtual Status DeleteFile(const std::string& fname) = 0;

  // Create the specified directory.
  virtual Status CreateDir(const std::string& dirname) = 0;

  // Delete the specified directory.
  virtual Status DeleteDir(const std::string& dirname) = 0;

  // Synchronize the entry for a specific directory.
  virtual Status SyncDir(const std::string& dirname) = 0;

  // Recursively delete the specified directory.
  // This should operate safely, not following any symlinks, etc.
  virtual Status DeleteRecursively(const std::string &dirname) = 0;

  // Store the logical size of fname in *file_size.
  virtual Status GetFileSize(const std::string& fname, uint64_t* file_size) = 0;

  // Store the physical size of fname in *file_size.
  //
  // This differs from GetFileSize() in that it returns the actual amount
  // of space consumed by the file, not the user-facing file size.
  virtual Status GetFileSizeOnDisk(const std::string& fname, uint64_t* file_size) = 0;

  // Store the block size of the filesystem where fname resides in
  // *block_size. fname must exist but it may be a file or a directory.
  virtual Status GetBlockSize(const std::string& fname, uint64_t* block_size) = 0;

  // Rename file src to target.
  virtual Status RenameFile(const std::string& src,
                            const std::string& target) = 0;

  // Lock the specified file.  Used to prevent concurrent access to
  // the same db by multiple processes.  On failure, stores NULL in
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

  // *path is set to a temporary directory that can be used for testing. It may
  // or many not have just been created. The directory may or may not differ
  // between runs of the same process, but subsequent calls will return the
  // same directory.
  virtual Status GetTestDirectory(std::string* path) = 0;

  // Returns the number of micro-seconds since some fixed point in time. Only
  // useful for computing deltas of time.
  virtual uint64_t NowMicros() = 0;

  // Sleep/delay the thread for the perscribed number of micro-seconds.
  virtual void SleepForMicroseconds(int micros) = 0;

  // Get caller's thread id.
  virtual uint64_t gettid() = 0;

  // Return the full path of the currently running executable.
  virtual Status GetExecutablePath(std::string* path) = 0;

  // Checks if the file is a directory. Returns an error if it doesn't
  // exist, otherwise writes true or false into 'is_dir' appropriately.
  virtual Status IsDirectory(const std::string& path, bool* is_dir) = 0;

  // The kind of file found during a walk. Note that symbolic links are
  // reported as FILE_TYPE.
  enum FileType {
    DIRECTORY_TYPE,
    FILE_TYPE,
  };

  // Called for each file/directory in the walk.
  //
  // The first argument is the type of file.
  // The second is the dirname of the file.
  // The third is the basename of the file.
  //
  // Returning an error won't halt the walk, but it will cause it to return
  // with an error status when it's done.
  typedef Callback<Status(FileType,const std::string&, const std::string&)> WalkCallback;

  // Whether to walk directories in pre-order or post-order.
  enum DirectoryOrder {
    PRE_ORDER,
    POST_ORDER,
  };

  // Walk the filesystem subtree from 'root' down, invoking 'cb' for each
  // file or directory found, including 'root'.
  //
  // The walk will not cross filesystem boundaries. It won't change the
  // working directory, nor will it follow symbolic links.
  virtual Status Walk(const std::string& root,
                      DirectoryOrder order,
                      const WalkCallback& cb) = 0;

  // Canonicalize 'path' by applying the following conversions:
  // - Converts a relative path into an absolute one using the cwd.
  // - Converts '.' and '..' references.
  // - Resolves all symbolic links.
  //
  // All directory entries in 'path' must exist on the filesystem.
  virtual Status Canonicalize(const std::string& path, std::string* result) = 0;

  // Get the total amount of RAM installed on this machine.
  virtual Status GetTotalRAMBytes(int64_t* ram) = 0;
 private:
  // No copying allowed
  Env(const Env&);
  void operator=(const Env&);
};

// A file abstraction for reading sequentially through a file
class SequentialFile {
 public:
  SequentialFile() { }
  virtual ~SequentialFile();

  // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
  // written by this routine.  Sets "*result" to the data that was
  // read (including if fewer than "n" bytes were successfully read).
  // May set "*result" to point at data in "scratch[0..n-1]", so
  // "scratch[0..n-1]" must be live when "*result" is used.
  // If an error was encountered, returns a non-OK status.
  //
  // REQUIRES: External synchronization
  virtual Status Read(size_t n, Slice* result, uint8_t *scratch) = 0;

  // Skip "n" bytes from the file. This is guaranteed to be no
  // slower that reading the same data, but may be faster.
  //
  // If end of file is reached, skipping will stop at the end of the
  // file, and Skip will return OK.
  //
  // REQUIRES: External synchronization
  virtual Status Skip(uint64_t n) = 0;

  // Returns the filename provided when the SequentialFile was constructed.
  virtual const std::string& filename() const = 0;
};

// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile {
 public:
  RandomAccessFile() { }
  virtual ~RandomAccessFile();

  // Read up to "n" bytes from the file starting at "offset".
  // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
  // to the data that was read (including if fewer than "n" bytes were
  // successfully read).  May set "*result" to point at data in
  // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
  // "*result" is used.  If an error was encountered, returns a non-OK
  // status.
  //
  // Safe for concurrent use by multiple threads.
  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      uint8_t *scratch) const = 0;

  // Returns the size of the file
  virtual Status Size(uint64_t *size) const = 0;

  // Returns the filename provided when the RandomAccessFile was constructed.
  virtual const std::string& filename() const = 0;

  // Returns the approximate memory usage of this RandomAccessFile including
  // the object itself.
  virtual size_t memory_footprint() const = 0;
};

// Creation-time options for WritableFile
struct WritableFileOptions {
  // Call Sync() during Close().
  bool sync_on_close;

  // See CreateMode for details.
  Env::CreateMode mode;

  WritableFileOptions()
    : sync_on_close(false),
      mode(Env::CREATE_IF_NON_EXISTING_TRUNCATE) { }
};

// Options specified when a file is opened for random access.
struct RandomAccessFileOptions {
  RandomAccessFileOptions() {}
};

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
class WritableFile {
 public:
  enum FlushMode {
    FLUSH_SYNC,
    FLUSH_ASYNC
  };

  WritableFile() { }
  virtual ~WritableFile();

  // Pre-allocates 'size' bytes for the file in the underlying filesystem.
  // size bytes are added to the current pre-allocated size or to the current
  // offset, whichever is bigger. In no case is the file truncated by this
  // operation.
  virtual Status PreAllocate(uint64_t size) = 0;

  virtual Status Append(const Slice& data) = 0;

  // If possible, uses scatter-gather I/O to efficiently append
  // multiple buffers to a file. Otherwise, falls back to regular I/O.
  //
  // For implementation specific quirks and details, see comments in
  // implementation source code (e.g., env_posix.cc)
  virtual Status AppendVector(const std::vector<Slice>& data_vector) = 0;

  virtual Status Close() = 0;

  // Flush all dirty data (not metadata) to disk.
  //
  // If the flush mode is synchronous, will wait for flush to finish and
  // return a meaningful status.
  virtual Status Flush(FlushMode mode) = 0;

  virtual Status Sync() = 0;

  virtual uint64_t Size() const = 0;

  // Returns the filename provided when the WritableFile was constructed.
  virtual const std::string& filename() const = 0;

 private:
  // No copying allowed
  WritableFile(const WritableFile&);
  void operator=(const WritableFile&);
};

// Creation-time options for RWFile
struct RWFileOptions {
  // Call Sync() during Close().
  bool sync_on_close;

  // See CreateMode for details.
  Env::CreateMode mode;

  RWFileOptions()
    : sync_on_close(false),
      mode(Env::CREATE_IF_NON_EXISTING_TRUNCATE) { }
};

// A file abstraction for both reading and writing. No notion of a built-in
// file offset is ever used; instead, all operations must provide an
// explicit offset.
//
// All "read" operations are safe for concurrent use by multiple threads,
// but "write" operations must be externally synchronized.
class RWFile {
 public:
  enum FlushMode {
    FLUSH_SYNC,
    FLUSH_ASYNC
  };

  RWFile() {
  }

  virtual ~RWFile();

  // Read exactly 'length' bytes from the file starting at 'offset'.
  // 'scratch[0..length-1]' may be written by this routine. Sets '*result'
  // to the data that was read. May set '*result' to point at data in
  // 'scratch[0..length-1]', which must be live when '*result' is used.
  // If an error was encountered, returns a non-OK status.
  //
  // In the event of a "short read" (fewer bytes read than were requested),
  // an IOError is returned.
  //
  // Safe for concurrent use by multiple threads.
  virtual Status Read(uint64_t offset, size_t length,
                      Slice* result, uint8_t* scratch) const = 0;

  // Writes 'data' to the file position given by 'offset'.
  virtual Status Write(uint64_t offset, const Slice& data) = 0;

  // Preallocates 'length' bytes for the file in the underlying filesystem
  // beginning at 'offset'. It is safe to preallocate the same range
  // repeatedly; this is an idempotent operation.
  //
  // In no case is the file truncated by this operation.
  virtual Status PreAllocate(uint64_t offset, size_t length) = 0;

  // Deallocates space given by 'offset' and length' from the file,
  // effectively "punching a hole" in it. The space will be reclaimed by
  // the filesystem and reads to that range will return zeroes. Useful
  // for making whole files sparse.
  //
  // Filesystems that don't implement this will return an error.
  virtual Status PunchHole(uint64_t offset, size_t length) = 0;

  // Flushes the range of dirty data (not metadata) given by 'offset' and
  // 'length' to disk. If length is 0, all bytes from 'offset' to the end
  // of the file are flushed.
  //
  // If the flush mode is synchronous, will wait for flush to finish and
  // return a meaningful status.
  virtual Status Flush(FlushMode mode, uint64_t offset, size_t length) = 0;

  // Synchronously flushes all dirty file data and metadata to disk. Upon
  // returning successfully, all previously issued file changes have been
  // made durable.
  virtual Status Sync() = 0;

  // Closes the file, optionally calling Sync() on it if the file was
  // created with the sync_on_close option enabled.
  virtual Status Close() = 0;

  // Retrieves the file's size.
  virtual Status Size(uint64_t* size) const = 0;

  // Returns the filename provided when the RWFile was constructed.
  virtual const std::string& filename() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(RWFile);
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

// A utility routine: write "data" to the named file.
extern Status WriteStringToFile(Env* env, const Slice& data,
                                const std::string& fname);

// A utility routine: read contents of named file into *data
extern Status ReadFileToString(Env* env, const std::string& fname,
                               faststring* data);

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
  Status NewSequentialFile(const std::string& f, gscoped_ptr<SequentialFile>* r) OVERRIDE {
    return target_->NewSequentialFile(f, r);
  }
  Status NewRandomAccessFile(const std::string& f,
                             gscoped_ptr<RandomAccessFile>* r) OVERRIDE {
    return target_->NewRandomAccessFile(f, r);
  }
  Status NewRandomAccessFile(const RandomAccessFileOptions& opts,
                             const std::string& f,
                             gscoped_ptr<RandomAccessFile>* r) OVERRIDE {
    return target_->NewRandomAccessFile(opts, f, r);
  }
  Status NewWritableFile(const std::string& f, gscoped_ptr<WritableFile>* r) OVERRIDE {
    return target_->NewWritableFile(f, r);
  }
  Status NewWritableFile(const WritableFileOptions& o,
                         const std::string& f,
                         gscoped_ptr<WritableFile>* r) OVERRIDE {
    return target_->NewWritableFile(o, f, r);
  }
  Status NewTempWritableFile(const WritableFileOptions& o, const std::string& t,
                             std::string* f, gscoped_ptr<WritableFile>* r) OVERRIDE {
    return target_->NewTempWritableFile(o, t, f, r);
  }
  Status NewRWFile(const std::string& f, gscoped_ptr<RWFile>* r) OVERRIDE {
    return target_->NewRWFile(f, r);
  }
  Status NewRWFile(const RWFileOptions& o,
                   const std::string& f,
                   gscoped_ptr<RWFile>* r) OVERRIDE {
    return target_->NewRWFile(o, f, r);
  }
  bool FileExists(const std::string& f) OVERRIDE { return target_->FileExists(f); }
  Status GetChildren(const std::string& dir, std::vector<std::string>* r) OVERRIDE {
    return target_->GetChildren(dir, r);
  }
  Status DeleteFile(const std::string& f) OVERRIDE { return target_->DeleteFile(f); }
  Status CreateDir(const std::string& d) OVERRIDE { return target_->CreateDir(d); }
  Status SyncDir(const std::string& d) OVERRIDE { return target_->SyncDir(d); }
  Status DeleteDir(const std::string& d) OVERRIDE { return target_->DeleteDir(d); }
  Status DeleteRecursively(const std::string& d) OVERRIDE { return target_->DeleteRecursively(d); }
  Status GetFileSize(const std::string& f, uint64_t* s) OVERRIDE {
    return target_->GetFileSize(f, s);
  }
  Status GetFileSizeOnDisk(const std::string& f, uint64_t* s) OVERRIDE {
    return target_->GetFileSizeOnDisk(f, s);
  }
  Status GetBlockSize(const std::string& f, uint64_t* s) OVERRIDE {
    return target_->GetBlockSize(f, s);
  }
  Status RenameFile(const std::string& s, const std::string& t) OVERRIDE {
    return target_->RenameFile(s, t);
  }
  Status LockFile(const std::string& f, FileLock** l) OVERRIDE {
    return target_->LockFile(f, l);
  }
  Status UnlockFile(FileLock* l) OVERRIDE { return target_->UnlockFile(l); }
  virtual Status GetTestDirectory(std::string* path) OVERRIDE {
    return target_->GetTestDirectory(path);
  }
  uint64_t NowMicros() OVERRIDE {
    return target_->NowMicros();
  }
  void SleepForMicroseconds(int micros) OVERRIDE {
    target_->SleepForMicroseconds(micros);
  }
  uint64_t gettid() OVERRIDE {
    return target_->gettid();
  }
  Status GetExecutablePath(std::string* path) OVERRIDE {
    return target_->GetExecutablePath(path);
  }
  Status IsDirectory(const std::string& path, bool* is_dir) OVERRIDE {
    return target_->IsDirectory(path, is_dir);
  }
  Status Walk(const std::string& root,
              DirectoryOrder order,
              const WalkCallback& cb) OVERRIDE {
    return target_->Walk(root, order, cb);
  }
  Status Canonicalize(const std::string& path, std::string* result) OVERRIDE {
    return target_->Canonicalize(path, result);
  }
  Status GetTotalRAMBytes(int64_t* ram) OVERRIDE {
    return target_->GetTotalRAMBytes(ram);
  }
 private:
  Env* target_;
};

}  // namespace kudu

#endif  // STORAGE_LEVELDB_INCLUDE_ENV_H_
