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

#include "yb/util/cipher_stream.h"
#include "yb/util/memory/memory.h"
#include "yb/util/encryption_util.h"
#include "yb/util/flag_tags.h"
#include "yb/gutil/endian.h"

namespace yb {
namespace enterprise {

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

  EVP_EncryptInit_ex(encryption_context_.get(), cipher, nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_set_padding(encryption_context_.get(), 0);
  return Status::OK();
}

Status BlockAccessCipherStream::Encrypt(uint64_t file_offset, const Slice& input, void* output) {
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
        block_index, Slice(input_buf, EncryptionParams::kBlockSize), output_buf));
    memcpy(output, output_buf + block_offset, first_block_size);
    block_index++;
    data_size -= first_block_size;
  }

  // Encrypt the rest of the data.
  if (data_size > 0) {
    RETURN_NOT_OK(EncryptByBlock(block_index,
                                 Slice(input.data() + first_block_size, data_size),
                                 static_cast<uint8_t*>(output) + first_block_size));
  }
  return Status::OK();
}

// Decrypt data at the file offset. Since CTR mode uses symmetric XOR operation,
// just calls Encrypt.
Status BlockAccessCipherStream::Decrypt(uint64_t file_offset, const Slice& input, void* output) {
  return Encrypt(file_offset, input, output);
}

Status BlockAccessCipherStream::EncryptByBlock(
    uint64_t block_index, const Slice& input, void* output) {
  uint64_t data_size = input.size();
  // Set the last 4 bytes of the iv based on counter + block_index.
  uint8_t iv[EncryptionParams::kBlockSize];
  memcpy(iv, encryption_params_->nonce, EncryptionParams::kBlockSize - 4);
  uint64_t start_index = encryption_params_->counter + block_index;
  BigEndian::Store32(iv + EncryptionParams::kBlockSize - 4, start_index);

  // Lock the encryption op since we modify encryption context.
  std::lock_guard<simple_spinlock> l(mutex_);
  EVP_EncryptInit_ex(encryption_context_.get(), nullptr, nullptr, encryption_params_->key, iv);
  // Perform the encryption.
  int len;
  EVP_EncryptUpdate(
      encryption_context_.get(), static_cast<uint8_t*>(output), &len, input.data(), data_size);
  DCHECK_EQ(len, data_size);

  return Status::OK();
}

} // namespace enterprise
}  // namespace yb
