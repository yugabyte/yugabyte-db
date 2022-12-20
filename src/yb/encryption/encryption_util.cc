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

#include "yb/encryption/encryption_util.h"

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <memory>

#include <boost/pointer_cast.hpp>

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/encryption.pb.h"
#include "yb/encryption/header_manager.h"

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/status_format.h"

DEFINE_UNKNOWN_int64(encryption_counter_min, 0,
             "Minimum value (inclusive) for the randomly generated 32-bit encryption counter at "
             "the beginning of a file");
TAG_FLAG(encryption_counter_min, advanced);
TAG_FLAG(encryption_counter_min, hidden);

DEFINE_UNKNOWN_int64(encryption_counter_max, 0x7fffffffLL,
             "Maximum value (inclusive) for the randomly generated 32-bit encryption counter at "
             "the beginning of a file. Setting to 2147483647 by default to reduce the probability "
             "of #3707 until it is fixed. This only reduces the key size by 1 bit but eliminates "
             "the encryption overflow issue for files up to 32 GiB in size.");

TAG_FLAG(encryption_counter_max, advanced);
TAG_FLAG(encryption_counter_max, hidden);

DEFINE_test_flag(bool, encryption_use_openssl_compatible_counter_overflow, true,
                 "Overflow into the rest of the initialization vector when computing counter"
                 "increment for newly created keys.");

namespace yb {
namespace encryption {

namespace {

std::vector<std::unique_ptr<std::mutex>> crypto_mutexes;

}  // anonymous namespace

constexpr uint32_t kDefaultKeySize = 16;

void EncryptionParams::ToEncryptionParamsPB(EncryptionParamsPB* encryption_header) const {
  encryption_header->set_data_key(key, key_size);
  encryption_header->set_nonce(nonce, kBlockSize - 4);
  encryption_header->set_counter(counter);
  encryption_header->set_openssl_compatible_counter_overflow(openssl_compatible_counter_overflow);
}

Result<EncryptionParamsPtr> EncryptionParams::FromEncryptionParamsPB(
    const EncryptionParamsPB& encryption_header) {
  auto encryption_params = std::make_unique<EncryptionParams>();
  memcpy(encryption_params->key, encryption_header.data_key().c_str(),
         encryption_header.data_key().size());
  memcpy(encryption_params->nonce, encryption_header.nonce().c_str(), kBlockSize - 4);
  encryption_params->counter = encryption_header.counter();
  auto size = encryption_header.data_key().size();
  RETURN_NOT_OK(IsValidKeySize(size));
  encryption_params->key_size = narrow_cast<uint32_t>(size);
  encryption_params->openssl_compatible_counter_overflow =
      encryption_header.openssl_compatible_counter_overflow();
  return encryption_params;
}

Result<EncryptionParamsPtr> EncryptionParams::FromSlice(const Slice& s) {
  auto params = std::make_unique<EncryptionParams>();
  Slice mutable_s(s);
  memcpy(params->nonce, s.data(), sizeof(params->nonce));
  memcpy(&params->counter, s.data() + sizeof(params->nonce), sizeof(params->counter));
  mutable_s.remove_prefix(sizeof(params->nonce) + sizeof(params->counter));
  RETURN_NOT_OK(IsValidKeySize(mutable_s.size()));
  memcpy(params->key, mutable_s.data(), mutable_s.size());
  params->key_size = narrow_cast<uint32_t>(mutable_s.size());
  return params;
}

EncryptionParamsPtr EncryptionParams::NewEncryptionParams() {
  auto encryption_params = std::make_unique<EncryptionParams>();
  RAND_bytes(encryption_params->key, kDefaultKeySize);
  RAND_bytes(encryption_params->nonce, kBlockSize - 4);
  RAND_bytes(boost::reinterpret_pointer_cast<uint8_t>(&encryption_params->counter), 4);

  const int64_t ctr_min = GetAtomicFlag(&FLAGS_encryption_counter_min);
  const int64_t ctr_max = GetAtomicFlag(&FLAGS_encryption_counter_max);
  if (0 <= ctr_min && ctr_min <= ctr_max && ctr_max <= std::numeric_limits<uint32_t>::max()) {
    encryption_params->counter = narrow_cast<uint32_t>(
        ctr_min + encryption_params->counter % (ctr_max - ctr_min + 1));
  } else {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << "Invalid encrypted counter range: "
        << "[" << ctr_min << ", " << ctr_max << "] specified by --encryption_counter_{min,max}, "
        << "falling back to using the full unsigned 32-bit integer range.";
  }
  encryption_params->key_size = kDefaultKeySize;
  encryption_params->openssl_compatible_counter_overflow =
      FLAGS_TEST_encryption_use_openssl_compatible_counter_overflow;
  return encryption_params;
}

Status EncryptionParams::IsValidKeySize(size_t size) {
  if (size != 16 && size != 24 && size != 32) {
    return STATUS_SUBSTITUTE(
        InvalidArgument,
        "After parsing nonce and counter, expect 16, 24, or 32 bytes, found $0", size);
  }
  return Status::OK();
}

bool EncryptionParams::Equals(const EncryptionParams& other) {
  return memcmp(key, other.key, other.key_size) == 0 &&
         memcmp(nonce, other.nonce, sizeof(nonce)) == 0 &&
         counter == other.counter &&
         key_size == other.key_size &&
         openssl_compatible_counter_overflow == other.openssl_compatible_counter_overflow;
}

void* EncryptionBuffer::GetBuffer(size_t size_needed) {
  if (size_needed > size) {
    size = size_needed;
    if (buffer) {
      free(buffer);
    }
    buffer = malloc(size_needed);
  }
  return buffer;
}

EncryptionBuffer::~EncryptionBuffer() {
  if (buffer) {
    free(buffer);
  }
}

EncryptionBuffer* EncryptionBuffer::Get() {
  static thread_local EncryptionBuffer encryption_buffer;
  return &encryption_buffer;
}

Result<uint32_t> GetHeaderSize(SequentialFile* file, HeaderManager* header_manager) {
  if (!header_manager) {
    return STATUS(InvalidArgument, "header_manager argument must be non null.");
  }
  Slice encryption_info;
  auto metadata_start = header_manager->GetEncryptionMetadataStartIndex();
  auto buf = static_cast<uint8_t*>(EncryptionBuffer::Get()->GetBuffer(metadata_start));

  RETURN_NOT_OK(file->Read(metadata_start, &encryption_info, buf));
  auto status = VERIFY_RESULT(header_manager->GetFileEncryptionStatusFromPrefix(encryption_info));
  return status.is_encrypted ? (status.header_size + metadata_start) : 0;
}

Status CompleteCreateEncryptionInfoForWrite(const std::string& header,
                                            std::unique_ptr<EncryptionParams> encryption_params,
                                            std::unique_ptr<BlockAccessCipherStream>* stream,
                                            uint32_t* header_size) {
  // Since file doesn't exist or this overwrites, append key to the name and create.
  *stream = std::make_unique<BlockAccessCipherStream>(std::move(encryption_params));
  RETURN_NOT_OK((*stream)->Init());
  if (header.size() > std::numeric_limits<uint32_t>::max()) {
    return STATUS_FORMAT(Corruption, "Invalid encryption header size: $0", header.size());
  }
  *header_size = static_cast<uint32_t>(header.size());
  return Status::OK();
}

} // namespace encryption
} // namespace yb
