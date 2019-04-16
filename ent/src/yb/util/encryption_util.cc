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

#include <openssl/rand.h>
#include <memory>
#include <boost/pointer_cast.hpp>

#include "yb/util/encryption_util.h"

#include "yb/util/cipher_stream.h"
#include "yb/util/header_manager.h"
#include "yb/util/encryption_header.pb.h"

#include "yb/gutil/endian.h"

DECLARE_bool(enable_encryption);

namespace yb {
namespace enterprise {

constexpr uint32_t kDefaultKeySize = 16;

void EncryptionParams::ToEncryptionHeaderPB(yb::EncryptionHeaderPB* encryption_header) const {
  encryption_header->set_data_key(key, key_size);
  encryption_header->set_nonce(nonce, kBlockSize - 4);
  encryption_header->set_counter(counter);
}

void EncryptionParams::FromEncryptionHeaderPB(const yb::EncryptionHeaderPB& encryption_header) {
  memcpy(key, encryption_header.data_key().c_str(), encryption_header.data_key().size());
  memcpy(nonce, encryption_header.nonce().c_str(), kBlockSize - 4);
  counter = encryption_header.counter();
  key_size = encryption_header.data_key().size();
}

std::unique_ptr<EncryptionParams> EncryptionParams::NewEncryptionParams() {
  auto encryption_params = std::make_unique<yb::enterprise::EncryptionParams>();
  RAND_bytes(encryption_params->key, kDefaultKeySize);
  RAND_bytes(encryption_params->nonce, kBlockSize - 4);
  RAND_bytes(boost::reinterpret_pointer_cast<uint8_t>(&encryption_params->counter), 4);
  encryption_params->key_size = kDefaultKeySize;
  return encryption_params;
}

void* EncryptionBuffer::GetBuffer(uint32_t size_needed) {
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

Result<uint32_t> GetHeaderSize(rocksdb::SequentialFile* file, HeaderManager* header_manager) {
  if (!header_manager) {
    return STATUS(InvalidArgument, "header_manager argument must be non null.");
  }
  Slice encryption_info;
  auto metadata_start = header_manager->GetEncryptionMetadataStartIndex();
  auto buf = static_cast<char*>(EncryptionBuffer::Get()->GetBuffer(metadata_start));

  RETURN_NOT_OK(file->Read(metadata_start, &encryption_info, buf));
  auto status = VERIFY_RESULT(header_manager->GetFileEncryptionStatusFromPrefix(encryption_info));
  return status.is_encrypted ? (status.header_size + metadata_start) : 0;
}

} // namespace enterprise
} // namespace yb
