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

#pragma once

#include <openssl/ossl_typ.h>

#include <string>

#include "yb/encryption/cipher_stream_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"

namespace yb {
namespace encryption {

class BufferAllocator;
struct EncryptionParams;
typedef std::unique_ptr<EncryptionParams> EncryptionParamsPtr;

// BlockAccessCipherStream is the base class for any cipher stream.
class BlockAccessCipherStream {
 public:
  static Result<std::unique_ptr<BlockAccessCipherStream>> FromEncryptionParams(
      EncryptionParamsPtr encryption_params);
  explicit BlockAccessCipherStream(EncryptionParamsPtr encryption_params);
  Status Init();

  // Encrypt data at an offset.
  Status Encrypt(
      uint64_t file_offset,
      const Slice& input,
      void* output,
      EncryptionOverflowWorkaround counter_overflow_workaround =
          EncryptionOverflowWorkaround::kFalse);

  // Decrypt data at an offset.
  // counter_overflow_workaround indicates whether we should add one to byte 11 of the initilization
  // vector. Used to as a workaround in case of block checksum mismatches when reading data that is
  // affected by https://github.com/yugabyte/yugabyte-db/issues/3707.
  Status Decrypt(
      uint64_t file_offset,
      const Slice& input,
      void* output,
      EncryptionOverflowWorkaround counter_overflow_workaround =
          EncryptionOverflowWorkaround::kFalse);

  bool UseOpensslCompatibleCounterOverflow();

 private:
  Status EncryptByBlock(
      uint64_t block_index,
      const Slice& input,
      void* output,
      EncryptionOverflowWorkaround counter_overflow_workaround =
          EncryptionOverflowWorkaround::kFalse);

  void IncrementCounter(const uint64_t start_idx, uint8_t* iv,
                        EncryptionOverflowWorkaround counter_overflow_workaround);

  EncryptionParamsPtr encryption_params_;
  std::unique_ptr<EVP_CIPHER_CTX, std::function<void(EVP_CIPHER_CTX*)>> encryption_context_;
  mutable simple_spinlock mutex_;
};

} // namespace encryption
} // namespace yb
