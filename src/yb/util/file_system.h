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

#ifndef YB_UTIL_FILE_SYSTEM_H
#define YB_UTIL_FILE_SYSTEM_H

#include <string>

#include "yb/util/coding_consts.h"
#include "yb/util/io.h"
#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {

// Options for opening a file to read/write.
struct FileSystemOptions {

  FileSystemOptions() {}

  static const FileSystemOptions kDefault;

  // If true, then allow caching of data in OS buffers.
  bool use_os_buffer = true;

  // If true, then use mmap to read data.
  bool use_mmap_reads = false;

  // If true, then use mmap to write data
  bool use_mmap_writes = true;

  // If false, fallocate() calls are bypassed
  bool allow_fallocate = true;

  // If true, we will preallocate the file with FALLOC_FL_KEEP_SIZE flag, which means that file
  // size won't change as part of preallocation.
  // If false, preallocation will also change the file size. This option will improve the
  // performance in workloads where you sync the data on every write.
  // By default, in rocksdb we set it to true for MANIFEST writes and false for WAL writes.
  bool fallocate_with_keep_size = true;

};

// Interface to filesystem.
class FileSystem {
 public:
};

// A file abstraction for reading sequentially through a file.
class SequentialFile {
 public:
  SequentialFile() {}
  virtual ~SequentialFile() {}

  // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
  // written by this routine.  Sets "*result" to the data that was
  // read (including if fewer than "n" bytes were successfully read).
  // May set "*result" to point at data in "scratch[0..n-1]", so
  // "scratch[0..n-1]" must be live when "*result" is used.
  // If an error was encountered, returns a non-OK status.
  //
  // REQUIRES: External synchronization
  virtual Status Read(size_t n, Slice* result, uint8_t* scratch) = 0;

  // Skip "n" bytes from the file. This is guaranteed to be no
  // slower that reading the same data, but may be faster.
  //
  // If end of file is reached, skipping will stop at the end of the
  // file, and Skip will return OK.
  //
  // REQUIRES: External synchronization
  virtual Status Skip(uint64_t n) = 0;

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  virtual Status InvalidateCache(size_t offset, size_t length);

  // Returns the filename provided when the SequentialFile was constructed.
  virtual const std::string& filename() const = 0;
};

class FileWithUniqueId {
 public:
  static constexpr const auto kPosixFileUniqueIdMaxSize = kMaxVarint64Length * 3;
  virtual ~FileWithUniqueId() {}

  // Tries to get an unique ID for this file that will be the same each time
  // the file is opened (and will stay the same while the file is open).
  // id should have at least capacity of kPosixFileUniqueIdMaxSize to hold the result.
  //
  // This function guarantees, for IDs from a given environment, two unique ids
  // cannot be made equal to each other by adding arbitrary bytes to one of
  // them. That is, no unique ID is the prefix of another.
  //
  // This function guarantees that the returned ID will not be interpretable as
  // a single varint.
  //
  // Note: these IDs are only valid for the duration of the process.
  virtual size_t GetUniqueId(char* id) const = 0;
};

// An interface for a function that validates a sequence of bytes we've just read. Could be used
// e.g. for checksum validation. We are not using boost::function for efficiency, because this
// validation happens on the critical read of read requests.
class ReadValidator {
 public:
  virtual Status Validate(const Slice& s) const = 0;
  virtual ~ReadValidator() = default;
};

// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile : public FileWithUniqueId {
 public:
  RandomAccessFile() {}
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

  // Similar to Read, but uses the given callback to validate the result.
  virtual Status ReadAndValidate(
      uint64_t offset, size_t n, Slice* result, char* scratch, const ReadValidator& validator);

  Status Read(uint64_t offset, size_t n, Slice* result, char* scratch);

  // Returns the size of the file
  virtual Result<uint64_t> Size() const = 0;

  virtual Result<uint64_t> INode() const = 0;

  // Returns the filename provided when the RandomAccessFile was constructed.
  virtual const std::string& filename() const = 0;

  // Returns the approximate memory usage of this RandomAccessFile including
  // the object itself.
  virtual size_t memory_footprint() const = 0;

  virtual bool IsEncrypted() const {
    return false;
  }

  virtual uint64_t GetEncryptionHeaderSize() const {
    return 0;
  }

  // Used by the file_reader_writer to decide if the ReadAhead wrapper
  // should simply forward the call and do not enact buffering or locking.
  virtual bool ShouldForwardRawRequest() const {
    return false;
  }

  // For cases when read-ahead is implemented in the platform dependent layer.
  virtual void EnableReadAhead() {}

  // For documentation, refer to FileWithUniqueId::GetUniqueId()
  virtual size_t GetUniqueId(char* id) const override {
    return 0; // Default implementation to prevent issues with backwards compatibility.
  }

  enum AccessPattern { NORMAL, RANDOM, SEQUENTIAL, WILLNEED, DONTNEED };

  virtual void Hint(AccessPattern pattern) {}

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  virtual Status InvalidateCache(size_t offset, size_t length);
};

} // namespace yb

namespace rocksdb {

// TODO(unify_env): remove `using` statement once filesystem classes are fully merged into yb
// namespace:
using yb::FileWithUniqueId;
using yb::Status;
using yb::IOPriority;

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
class WritableFile : public FileWithUniqueId {
 public:
  WritableFile()
      : last_preallocated_block_(0),
        preallocation_block_size_(0),
        io_priority_(IOPriority::kTotal) {}
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
  virtual void SetIOPriority(IOPriority pri) {
    io_priority_ = pri;
  }

  virtual IOPriority GetIOPriority() { return io_priority_; }

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

  IOPriority io_priority_;
};

} // namespace rocksdb

namespace yb {

class SequentialFileWrapper : public SequentialFile {
 public:
  explicit SequentialFileWrapper(std::unique_ptr<SequentialFile> t) : target_(std::move(t)) {}

  Status Read(size_t n, Slice* result, uint8_t* scratch) override;

  Status Skip(uint64_t n) override;

  Status InvalidateCache(size_t offset, size_t length) override;

  const std::string& filename() const override { return target_->filename(); }

 private:
  std::unique_ptr<SequentialFile> target_;
};

class RandomAccessFileWrapper : public RandomAccessFile {
 public:
  explicit RandomAccessFileWrapper(std::unique_ptr<RandomAccessFile> t) : target_(std::move(t)) {}

  // Return the target to which this RandomAccessFile forwards all calls.
  RandomAccessFile* target() const { return target_.get(); }

  Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override;

  Result<uint64_t> Size() const override;

  Result<uint64_t> INode() const override;

  const std::string& filename() const override { return target_->filename(); }

  size_t memory_footprint() const override { return target_->memory_footprint(); }

  bool IsEncrypted() const override { return target_->IsEncrypted(); }

  uint64_t GetEncryptionHeaderSize() const override { return target_->GetEncryptionHeaderSize(); }

  bool ShouldForwardRawRequest() const override {
    return target_->ShouldForwardRawRequest();
  }

  void EnableReadAhead() override { return target_->EnableReadAhead(); }

  size_t GetUniqueId(char* id) const override {
    return target_->GetUniqueId(id);
  }

  void Hint(AccessPattern pattern) override { return target_->Hint(pattern); }

  Status InvalidateCache(size_t offset, size_t length) override;

 private:
  std::unique_ptr<RandomAccessFile> target_;
};

} // namespace yb

namespace rocksdb {

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
  void SetIOPriority(IOPriority pri) override {
    target_->SetIOPriority(pri);
  }
  IOPriority GetIOPriority() override { return target_->GetIOPriority(); }
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

} // namespace rocksdb

#endif  // YB_UTIL_FILE_SYSTEM_H
