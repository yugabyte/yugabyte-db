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
  virtual CHECKED_STATUS Read(size_t n, Slice* result, uint8_t* scratch) = 0;

  // Skip "n" bytes from the file. This is guaranteed to be no
  // slower that reading the same data, but may be faster.
  //
  // If end of file is reached, skipping will stop at the end of the
  // file, and Skip will return OK.
  //
  // REQUIRES: External synchronization
  virtual CHECKED_STATUS Skip(uint64_t n) = 0;

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  virtual CHECKED_STATUS InvalidateCache(size_t offset, size_t length);

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
  virtual size_t GetUniqueId(char *id) const = 0;
};

// An interface for a function that validates a sequence of bytes we've just read. Could be used
// e.g. for checksum validation. We are not using boost::function for efficiency, because this
// validation happens on the critical read of read requests.
class ReadValidator {
 public:
  virtual CHECKED_STATUS Validate(const Slice& s) const = 0;
  virtual ~ReadValidator() = default;
};

// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile : public FileWithUniqueId {
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
  virtual CHECKED_STATUS Read(uint64_t offset, size_t n, Slice* result,
                              uint8_t *scratch) const = 0;

  // Similar to Read, but uses the given callback to validate the result.
  virtual CHECKED_STATUS ReadAndValidate(
      uint64_t offset, size_t n, Slice* result, char* scratch, const ReadValidator& validator);

  CHECKED_STATUS Read(uint64_t offset, size_t n, Slice* result, char* scratch);

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
  virtual CHECKED_STATUS InvalidateCache(size_t offset, size_t length);
};

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

  CHECKED_STATUS Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override;

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

#endif  // YB_UTIL_FILE_SYSTEM_H
