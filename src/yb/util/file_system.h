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

#include "yb/util/slice.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

// Options for opening a file to read/write.
struct FileSystemOptions {

  FileSystemOptions() {}

  static const FileSystemOptions kDefault;

  // If true, then allow caching of data in OS buffers.
  bool use_os_buffer = true;
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
  virtual CHECKED_STATUS InvalidateCache(size_t offset, size_t length) {
    return STATUS(NotSupported, "InvalidateCache not supported.");
  }

  // Returns the filename provided when the SequentialFile was constructed.
  virtual const std::string& filename() const = 0;
};

class SequentialFileWrapper : public SequentialFile {
 public:
  explicit SequentialFileWrapper(std::unique_ptr<SequentialFile> t) : target_(std::move(t)) {}

  Status Read(size_t n, Slice* result, uint8_t* scratch) override {
    return target_->Read(n, result, scratch); }

  Status Skip(uint64_t n) override { return target_->Skip(n); }

  Status InvalidateCache(size_t offset, size_t length) override {
    return target_->InvalidateCache(offset, length);
  }

  const std::string& filename() const override { return target_->filename(); }

 private:
  std::unique_ptr<SequentialFile> target_;
};

} // namespace yb

#endif  // YB_UTIL_FILE_SYSTEM_H
