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

#include "yb/util/encrypted_file.h"

#include "yb/util/env.h"
#include "yb/util/cipher_stream.h"
#include "yb/util/memory/memory.h"
#include "yb/util/header_manager.h"
#include "yb/util/encryption_util.h"

namespace yb {
namespace enterprise {

Status EncryptedRandomAccessFile::Create(
    std::unique_ptr<RandomAccessFile>* result, HeaderManager* header_manager,
    std::unique_ptr<RandomAccessFile> underlying) {
  return CreateRandomAccessFile<EncryptedRandomAccessFile, uint8_t>(
      result, header_manager, std::move(underlying));
}

Status EncryptedRandomAccessFile::Read(
    uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const {
  if (!scratch) {
    return STATUS(InvalidArgument, "scratch argument is null.");
  }
  uint8_t* buf = static_cast<uint8_t*>(EncryptionBuffer::Get()->GetBuffer(n));
  RETURN_NOT_OK(RandomAccessFileWrapper::Read(offset + header_size_, n, result, buf));
  RETURN_NOT_OK(stream_->Decrypt(offset, *result, scratch));
  *result = Slice(scratch, result->size());
  return Status::OK();
}

} // namespace enterprise
} // namespace yb
