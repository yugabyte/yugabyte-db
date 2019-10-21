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

#ifndef YB_UTIL_ENCRYPTED_FILE_H
#define YB_UTIL_ENCRYPTED_FILE_H

#include <memory>

#include "yb/util/env.h"

namespace yb {

namespace enterprise {

class BlockAccessCipherStream;
class HeaderManager;

// An encrypted fie implementation for random access of a file.
class EncryptedRandomAccessFile : public RandomAccessFileWrapper {
 public:
  static Status Create(std::unique_ptr<RandomAccessFile>* result,
                       HeaderManager* header_manager,
                       std::unique_ptr<RandomAccessFile> underlying);

  EncryptedRandomAccessFile(std::unique_ptr<RandomAccessFile> file,
                            std::unique_ptr<yb::enterprise::BlockAccessCipherStream> stream,
                            uint64_t header_size)
      : RandomAccessFileWrapper(std::move(file)), stream_(std::move(stream)),
        header_size_(header_size) {}

  ~EncryptedRandomAccessFile() {}

  CHECKED_STATUS Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override;

  uint64_t GetEncryptionHeaderSize() const override {
    return header_size_;
  }

  Result<uint64_t> Size() const override {
    return VERIFY_RESULT(RandomAccessFileWrapper::Size()) - header_size_;
  }

  virtual bool IsEncrypted() const override {
    return true;
  }

 private:
  std::unique_ptr<BlockAccessCipherStream> stream_;
  uint64_t header_size_;
};

} // namespace enterprise
} // namespace yb

#endif // YB_UTIL_ENCRYPTED_FILE_H
