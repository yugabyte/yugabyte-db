// Copyright (c) YugabyteDB, Inc.
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
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

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

// Algorithm flags take an EncryptionAlgorithmPB value name without its ENCRYPTION_ALGORITHM_
// prefix, so the enum is the only list of algorithms.
DEFINE_RUNTIME_string(encryption_algorithm_override, "",
    "Overrides the encryption algorithm recorded in the universe key registry for files this "
    "tserver writes, such as AES_256_CTR. Empty uses the registry. For diagnostics only.");
TAG_FLAG(encryption_algorithm_override, advanced);

DEFINE_RUNTIME_string(encryption_new_universe_algorithm, "AES_256_CTR",
    "Encryption algorithm recorded in the universe key registry when encryption at rest is "
    "enabled for the first time, such as AES_256_CTR or AES_128_CTR. Universes that already "
    "enabled encryption at rest keep their recorded algorithm.");
TAG_FLAG(encryption_new_universe_algorithm, advanced);

DEFINE_test_flag(bool, encryption_use_openssl_compatible_counter_overflow, true,
                 "Overflow into the rest of the initialization vector when computing counter"
                 "increment for newly created keys.");

namespace yb {
namespace encryption {

namespace {

constexpr std::string_view kEncryptionAlgorithmPrefix = "ENCRYPTION_ALGORITHM_";

bool ValidateEncryptionAlgorithmFlag(const char* flag_name, const std::string& value) {
  if (value.empty() && flag_name == std::string_view("encryption_algorithm_override")) {
    return true;
  }
  auto algorithm = ParseEncryptionAlgorithm(value);
  if (!algorithm.ok()) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, value) << algorithm.status().message().ToBuffer();
    return false;
  }
  return true;
}

} // namespace

namespace {

std::vector<std::unique_ptr<std::mutex>> crypto_mutexes;

}  // anonymous namespace

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

Result<EncryptionParamsPtr> EncryptionParams::NewEncryptionParams(
    EncryptionAlgorithmPB algorithm) {
  const auto key_size = VERIFY_RESULT(DataKeySize(algorithm));
  auto encryption_params = std::make_unique<EncryptionParams>();
  RAND_bytes(encryption_params->key, key_size);
  RAND_bytes(encryption_params->nonce, kBlockSize - 4);
  RAND_bytes(boost::reinterpret_pointer_cast<uint8_t>(&encryption_params->counter), 4);

  const int64_t ctr_min = FLAGS_encryption_counter_min;
  const int64_t ctr_max = FLAGS_encryption_counter_max;
  if (0 <= ctr_min && ctr_min <= ctr_max && ctr_max <= std::numeric_limits<uint32_t>::max()) {
    encryption_params->counter = narrow_cast<uint32_t>(
        ctr_min + encryption_params->counter % (ctr_max - ctr_min + 1));
  } else {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << "Invalid encrypted counter range: "
        << "[" << ctr_min << ", " << ctr_max << "] specified by --encryption_counter_{min,max}, "
        << "falling back to using the full unsigned 32-bit integer range.";
  }
  encryption_params->key_size = key_size;
  encryption_params->openssl_compatible_counter_overflow =
      FLAGS_TEST_encryption_use_openssl_compatible_counter_overflow;
  return encryption_params;
}

std::vector<std::string> EncryptionAlgorithmNames() {
  std::vector<std::string> names;
  const auto* descriptor = EncryptionAlgorithmPB_descriptor();
  for (int i = 0; i != descriptor->value_count(); ++i) {
    names.push_back(EncryptionAlgorithmName(
        static_cast<EncryptionAlgorithmPB>(descriptor->value(i)->number())));
  }
  return names;
}

Result<EncryptionAlgorithmPB> ParseEncryptionAlgorithm(const std::string& name) {
  EncryptionAlgorithmPB algorithm;
  SCHECK(EncryptionAlgorithmPB_Parse(std::string(kEncryptionAlgorithmPrefix) + name, &algorithm),
         InvalidArgument,
         Format("Unknown encryption algorithm $0; expected one of $1", name,
                EncryptionAlgorithmNames()));
  return algorithm;
}

std::string EncryptionAlgorithmName(EncryptionAlgorithmPB algorithm) {
  const auto& name = EncryptionAlgorithmPB_Name(algorithm);
  return name.starts_with(kEncryptionAlgorithmPrefix)
      ? name.substr(kEncryptionAlgorithmPrefix.size()) : std::to_string(algorithm);
}

Result<uint32_t> DataKeySize(EncryptionAlgorithmPB algorithm) {
  switch (algorithm) {
    case ENCRYPTION_ALGORITHM_AES_128_CTR:
      return EncryptionParams::kAes128KeySize;
    case ENCRYPTION_ALGORITHM_AES_256_CTR:
      return EncryptionParams::kAes256KeySize;
    default:
      // A later release, or a bad registry, can send a value this release does not support.
      return STATUS_FORMAT(
          NotSupported, "Unsupported encryption algorithm $0", static_cast<int>(algorithm));
  }
}

namespace {

// A runtime string flag can't be read through FLAGS_ while set_flag may assign it, and
// GetCommandLineOption takes the gflags registry lock, too costly on every file creation. So each
// algorithm flag is parsed into an atomic by its flag callback, or on first use if no callback has
// run yet.
class AlgorithmFlag {
 public:
  explicit AlgorithmFlag(const char* flag_name) : flag_name_(flag_name) {}

  void Reload() { value_.store(Read(), std::memory_order_release); }

  // Returns std::nullopt when the flag is empty.
  std::optional<EncryptionAlgorithmPB> Get() {
    auto value = value_.load(std::memory_order_acquire);
    if (value == kUnloaded) {
      // A concurrent Reload's value wins over this one.
      value_.compare_exchange_strong(value, Read(), std::memory_order_acq_rel);
      value = value_.load(std::memory_order_acquire);
    }
    if (value == kEmpty) {
      return std::nullopt;
    }
    return static_cast<EncryptionAlgorithmPB>(value);
  }

 private:
  static constexpr int kUnloaded = -2;
  static constexpr int kEmpty = -1;

  int Read() const {
    std::string value;
    CHECK(google::GetCommandLineOption(flag_name_, &value)) << flag_name_;
    if (value.empty()) {
      return kEmpty;
    }
    // The flag validator only allows supported algorithms.
    return CHECK_RESULT(ParseEncryptionAlgorithm(value));
  }

  const char* const flag_name_;
  std::atomic<int> value_{kUnloaded};
};

AlgorithmFlag& NewUniverseAlgorithmFlag() {
  static AlgorithmFlag flag("encryption_new_universe_algorithm");
  return flag;
}

AlgorithmFlag& AlgorithmOverrideFlag() {
  static AlgorithmFlag flag("encryption_algorithm_override");
  return flag;
}

} // namespace

Result<EncryptionAlgorithmPB> NewUniverseEncryptionAlgorithm() {
  auto algorithm = NewUniverseAlgorithmFlag().Get();
  SCHECK(algorithm, IllegalState, "encryption_new_universe_algorithm is empty");
  return *algorithm;
}

std::optional<EncryptionAlgorithmPB> EncryptionAlgorithmOverride() {
  return AlgorithmOverrideFlag().Get();
}

Result<uint32_t> UniverseKeySize(EncryptionAlgorithmPB algorithm) {
  return EncryptionParams::kUniverseKeyPrefixSize + VERIFY_RESULT(DataKeySize(algorithm));
}

uint32_t DefaultUniverseKeySize() {
  // The flag validator only allows supported algorithms.
  return CHECK_RESULT(UniverseKeySize(CHECK_RESULT(NewUniverseEncryptionAlgorithm())));
}

Status EncryptionParams::IsValidKeySize(size_t size) {
  if (size != 16 && size != 24 && size != 32) {
    return STATUS_FORMAT(
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

void ReloadEncryptionAlgorithmFlags() {
  NewUniverseAlgorithmFlag().Reload();
  AlgorithmOverrideFlag().Reload();
}

} // namespace encryption
} // namespace yb

DEFINE_validator(encryption_algorithm_override, &yb::encryption::ValidateEncryptionAlgorithmFlag);
DEFINE_validator(encryption_new_universe_algorithm,
                 &yb::encryption::ValidateEncryptionAlgorithmFlag);

REGISTER_CALLBACK(encryption_new_universe_algorithm, "Reload encryption algorithm flags",
                  &yb::encryption::ReloadEncryptionAlgorithmFlags);
REGISTER_CALLBACK(encryption_algorithm_override, "Reload encryption algorithm flags",
                  &yb::encryption::ReloadEncryptionAlgorithmFlags);
