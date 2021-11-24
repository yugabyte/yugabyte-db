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

#ifndef YB_UTIL_FILE_SYSTEM_POSIX_H
#define YB_UTIL_FILE_SYSTEM_POSIX_H

#include "yb/util/file_system.h"

namespace yb {

#if defined(__linux__)
size_t GetUniqueIdFromFile(int fd, uint8_t* id);
#endif // __linux__

class PosixSequentialFile : public SequentialFile {
 public:
  PosixSequentialFile(const std::string& fname, FILE* f, const FileSystemOptions& options);
  virtual ~PosixSequentialFile();

  CHECKED_STATUS Read(size_t n, Slice* result, uint8_t* scratch) override;
  CHECKED_STATUS Skip(uint64_t n) override;
  CHECKED_STATUS InvalidateCache(size_t offset, size_t length) override;

  const std::string& filename() const override { return filename_; }

 private:
  std::string filename_;
  FILE* file_;
  int fd_;
  bool use_os_buffer_;
};

// pread() based random-access file.
class PosixRandomAccessFile : public RandomAccessFile {
 public:
  PosixRandomAccessFile(const std::string& fname, int fd,
                        const FileSystemOptions& options);
  virtual ~PosixRandomAccessFile();

  virtual CHECKED_STATUS Read(uint64_t offset, size_t n, Slice* result,
                      uint8_t* scratch) const override;

  Result<uint64_t> Size() const override;

  Result<uint64_t> INode() const override;

  const std::string& filename() const override { return filename_; }

  size_t memory_footprint() const override;

#ifdef __linux__
  virtual size_t GetUniqueId(char* id) const override;
#endif
  virtual void Hint(AccessPattern pattern) override;
  virtual CHECKED_STATUS InvalidateCache(size_t offset, size_t length) override;

 private:
  std::string filename_;
  int fd_;
  bool use_os_buffer_;
};

} // namespace yb

#endif  // YB_UTIL_FILE_SYSTEM_POSIX_H
