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

#include <openssl/evp.h>

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/encryption_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

#include "yb/util/status_format.h"

namespace yb {
namespace encryption {

Result<std::unique_ptr<BlockAccessCipherStream>> BlockAccessCipherStream::FromEncryptionParams(
    EncryptionParamsPtr encryption_params) {
  auto stream = std::make_unique<BlockAccessCipherStream>(std::move(encryption_params));
  RETURN_NOT_OK(stream->Init());
  return stream;
}

BlockAccessCipherStream::BlockAccessCipherStream(
    EncryptionParamsPtr encryption_params) :
    encryption_params_(std::move(encryption_params)),
    encryption_context_(EVP_CIPHER_CTX_new(), [](EVP_CIPHER_CTX* ctx){
      EVP_CIPHER_CTX_cleanup(ctx);
      EVP_CIPHER_CTX_free(ctx);
    }) {}

Status BlockAccessCipherStream::Init() {
  EVP_CIPHER_CTX_init(encryption_context_.get());
  const EVP_CIPHER* cipher;
  switch (encryption_params_->key_size) {
    case 16:
      cipher = EVP_aes_128_ctr();
      break;
    case 24:
      cipher = EVP_aes_192_ctr();
      break;
    case 32:
      cipher = EVP_aes_256_ctr();
      break;
    default:
      return STATUS_SUBSTITUTE(IllegalState, "Expected key size to be one of 16, 24, 32, found $0.",
          encryption_params_->key_size);
  }

  const auto encrypt_init_ex_result = EVP_EncryptInit_ex(
      encryption_context_.get(), cipher, /* impl */ nullptr, /* key */ nullptr,
      /* iv */ nullptr);
  if (encrypt_init_ex_result != 1) {
    return STATUS_FORMAT(InternalError,
                         "EVP_EncryptInit_ex returned $0",
                         encrypt_init_ex_result);
  }

  const auto set_padding_result = EVP_CIPHER_CTX_set_padding(encryption_context_.get(), 0);
  if (set_padding_result != 1) {
    return STATUS_FORMAT(InternalError,
                         "EVP_CIPHER_CTX_set_padding returned $0",
                         set_padding_result);
  }

  return Status::OK();
}

Status BlockAccessCipherStream::Encrypt(
    uint64_t file_offset,
    const Slice& input,
    void* output,
    EncryptionOverflowWorkaround counter_overflow_workaround) {
  if (!output) {
    return STATUS(InvalidArgument, "output argument must be non-null.");
  }
  uint64_t data_size = input.size();
  if (data_size == 0) {
    return Status::OK();
  }

  uint64_t block_index = file_offset / EncryptionParams::kBlockSize;
  uint64_t block_offset = file_offset % EncryptionParams::kBlockSize;
  size_t first_block_size = 0;
  // Encrypt the first block alone if it is not byte aligned with the block size.
  if (block_offset > 0) {
    first_block_size = std::min(data_size, EncryptionParams::kBlockSize - block_offset);
    uint8_t input_buf[EncryptionParams::kBlockSize];
    uint8_t output_buf[EncryptionParams::kBlockSize];
    memcpy(input_buf + block_offset, input.data(), first_block_size);
    RETURN_NOT_OK(EncryptByBlock(
        block_index, Slice(input_buf, EncryptionParams::kBlockSize), output_buf,
        counter_overflow_workaround));
    memcpy(output, output_buf + block_offset, first_block_size);
    block_index++;
    data_size -= first_block_size;
  }

  // Encrypt the rest of the data.
  if (data_size > 0) {
    RETURN_NOT_OK(EncryptByBlock(block_index,
                                 Slice(input.data() + first_block_size, data_size),
                                 static_cast<uint8_t*>(output) + first_block_size,
                                 counter_overflow_workaround));
  }
  return Status::OK();
}

// Decrypt data at the file offset. Since CTR mode uses symmetric XOR operation,
// just calls Encrypt.
Status BlockAccessCipherStream::Decrypt(
    uint64_t file_offset, const Slice& input, void* output,
    EncryptionOverflowWorkaround counter_overflow_workaround) {
  return Encrypt(file_offset, input, output, counter_overflow_workaround);
}

bool BlockAccessCipherStream::UseOpensslCompatibleCounterOverflow() {
  return encryption_params_->openssl_compatible_counter_overflow;
}

Status BlockAccessCipherStream::EncryptByBlock(
    uint64_t block_index, const Slice& input, void* output,
    EncryptionOverflowWorkaround counter_overflow_workaround) {
  if (input.size() == 0) {
    return Status::OK();
  }
  if (input.size() > implicit_cast<size_t>(std::numeric_limits<int>::max())) {
    return STATUS_FORMAT(InternalError,
                         "Cannot encrypt/decrypt $0 bytes at once (it is more than $1 bytes). "
                         "Encryption block index: $2.",
                         input.size(), std::numeric_limits<int>::max(), block_index);
  }
  int data_size = narrow_cast<int>(input.size());

  // Set the last 4 bytes of the iv based on counter + block_index.
  uint8_t iv[EncryptionParams::kBlockSize];
  memcpy(iv, encryption_params_->nonce, EncryptionParams::kBlockSize - 4);

  const uint64_t start_index = encryption_params_->counter + block_index;
  IncrementCounter(start_index, iv, counter_overflow_workaround);

  // Lock the encryption op since we modify encryption context.
  std::lock_guard l(mutex_);

  const int init_result =
      EVP_EncryptInit_ex(encryption_context_.get(), /* cipher */ nullptr, /* impl */ nullptr,
                         encryption_params_->key, iv);
  if (init_result != 1) {
    return STATUS_FORMAT(InternalError,
                         "EVP_EncryptInit_ex returned $0 when encrypting/decrypting $1 bytes "
                         "at block index $2.",
                         init_result, data_size, block_index);
  }

  // Perform the encryption.
  int bytes_updated = 0;
  const int update_result = EVP_EncryptUpdate(
      encryption_context_.get(), static_cast<uint8_t*>(output), &bytes_updated, input.data(),
      data_size);
  if (update_result != 1) {
    return STATUS_FORMAT(InternalError,
                         "EVP_EncryptUpdate returned $0 when encrypting/decrypting $1 bytes "
                         "at block index $2.",
                         update_result, data_size, block_index);
  }
  if (bytes_updated != data_size) {
    return STATUS_FORMAT(InternalError,
                         "EVP_EncryptUpdate had to process $0 bytes but processed $1 bytes "
                         "instead. Encryption block index: $2.",
                         data_size, bytes_updated, block_index);

  }

  return Status::OK();
}

void BlockAccessCipherStream::IncrementCounter(
    const uint64_t start_idx, uint8_t* iv,
    EncryptionOverflowWorkaround counter_overflow_workaround) {
  BigEndian::Store32(iv + EncryptionParams::kBlockSize - 4, static_cast<uint32_t>(start_idx));
  if (start_idx <= std::numeric_limits<uint32_t>::max() ||
      (!UseOpensslCompatibleCounterOverflow() && !counter_overflow_workaround)) {
    return;
  }

  uint64_t carry = start_idx >> 32;

  for (int i = 11; i >= 0 && carry != 0; i--) {
    carry += iv[i];
    iv[i] = carry;
    carry >>= 8;
  }
}

}  // namespace encryption
}  // namespace yb
