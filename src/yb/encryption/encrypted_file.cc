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

#include "yb/encryption/encrypted_file.h"

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/cipher_stream_fwd.h"
#include "yb/encryption/header_manager.h"
#include "yb/encryption/encryption_util.h"

#include "yb/util/env.h"
#include "yb/util/cast.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(encryption_counter_overflow_read_path_workaround, true,
            "Enable a read-path workaround for the encryption counter overflow bug #3707. "
            "This is enabled by default and could be disabled to reproduce the bug in testing.");
TAG_FLAG(encryption_counter_overflow_read_path_workaround, advanced);
TAG_FLAG(encryption_counter_overflow_read_path_workaround, hidden);

namespace yb {
namespace encryption {

Status EncryptedRandomAccessFile::Create(
    std::unique_ptr<RandomAccessFile>* result, HeaderManager* header_manager,
    std::unique_ptr<RandomAccessFile> underlying) {
  return CreateRandomAccessFile<EncryptedRandomAccessFile, uint8_t>(
      result, header_manager, std::move(underlying));
}

Status EncryptedRandomAccessFile::ReadInternal(
    uint64_t offset,
    size_t n,
    Slice* result,
    char* scratch,
    EncryptionOverflowWorkaround counter_overflow_workaround) const {
  if (!scratch) {
    return STATUS(InvalidArgument, "scratch argument is null.");
  }
  uint8_t* buf = static_cast<uint8_t*>(EncryptionBuffer::Get()->GetBuffer(n));
  RETURN_NOT_OK(RandomAccessFileWrapper::Read(offset + header_size_, n, result, buf));
  RETURN_NOT_OK(stream_->Decrypt(offset, *result, scratch, counter_overflow_workaround));
  *result = Slice(scratch, result->size());
  return Status::OK();
}

Status EncryptedRandomAccessFile::Read(
    uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const {
  return ReadInternal(offset, n, result, to_char_ptr(scratch),
                      EncryptionOverflowWorkaround::kFalse);
}

Status EncryptedRandomAccessFile::ReadAndValidate(
    uint64_t offset, size_t n, Slice* result, char* scratch, const ReadValidator& validator) {
  if (!FLAGS_encryption_counter_overflow_read_path_workaround ||
      stream_->UseOpensslCompatibleCounterOverflow()) {
    RETURN_NOT_OK(ReadInternal(offset, n, result, scratch, EncryptionOverflowWorkaround::kFalse));
    return validator.Validate(*result);
  }

  Status status_without_workaround;
  for (auto counter_overflow_workaround : EncryptionOverflowWorkaround::kValues) {
    RETURN_NOT_OK(ReadInternal(offset, n, result, scratch, counter_overflow_workaround));

    Status validation_status = validator.Validate(*result);
    if (!counter_overflow_workaround) {
      status_without_workaround = validation_status;
    }

    if (validation_status.ok()) {
      if (counter_overflow_workaround) {
        num_overflow_workarounds_.fetch_add(1, std::memory_order_relaxed);
      }
      return Status::OK();
    }
  }
  return status_without_workaround;
}

Result<uint64_t> EncryptedRandomAccessFile::Size() const {
  return VERIFY_RESULT(RandomAccessFileWrapper::Size()) - header_size_;
}

} // namespace encryption
} // namespace yb
