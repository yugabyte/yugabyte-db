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

#include "yb/encryption/header_manager_impl.h"

#include <string>

#include "yb/encryption/cipher_stream_fwd.h"
#include "yb/encryption/encryption.pb.h"
#include "yb/encryption/header_manager.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/gutil/casts.h"

#include "yb/util/status_fwd.h"
#include "yb/util/errno.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_format.h"

using std::string;

static const string kEncryptionMagic = "encrypt!";

namespace yb {
namespace encryption {

class HeaderManagerImpl : public HeaderManager {
 public:
  explicit HeaderManagerImpl(UniverseKeyManager* universe_key_manager)
      : universe_key_manager_(universe_key_manager) {}

  Result<string> SerializeEncryptionParams(const EncryptionParams& encryption_info) override {
    // 1. Generate the EncrytionParamsPB string to be encrypted.
    EncryptionParamsPB encryption_params_pb;
    encryption_info.ToEncryptionParamsPB(&encryption_params_pb);
    string encryption_params_pb_str;
    encryption_params_pb.SerializeToString(&encryption_params_pb_str);

    // 2. Encrypt the encryption params with the latest universe key.
    auto universe_params = VERIFY_RESULT(universe_key_manager_->GetLatestUniverseParams());
    auto stream = VERIFY_RESULT(BlockAccessCipherStream::FromEncryptionParams(
        std::move(universe_params.params)));
    auto encrypted_data_key = static_cast<char*>(
        EncryptionBuffer::Get()->GetBuffer(encryption_params_pb_str.size()));
    RETURN_NOT_OK(stream->Encrypt(0, encryption_params_pb_str, encrypted_data_key));

    // 3. Serialize the universe key id.
    auto universe_key_id_str = universe_params.version_id;
    char universe_key_size[sizeof(uint32_t)];
    BigEndian::Store32(universe_key_size, narrow_cast<uint32_t>(universe_key_id_str.size()));

    // 4. Serialize the encrypted encryption params.
    char encrypted_data_key_size[sizeof(uint32_t)];
    BigEndian::Store32(
        encrypted_data_key_size, narrow_cast<uint32_t>(encryption_params_pb_str.size()));

    // 5. Generate the encryption metadata string.
    auto metadata_str = string(universe_key_size, sizeof(universe_key_size)) + universe_key_id_str +
                        string(encrypted_data_key_size, sizeof(encrypted_data_key_size)) +
                        string(encrypted_data_key, encryption_params_pb_str.size());

    // 6. Serialize the header size.
    char header_size[sizeof(uint32_t)];
    BigEndian::Store32(header_size, narrow_cast<uint32_t>(metadata_str.size()));

    return kEncryptionMagic + string(header_size, sizeof(header_size)) + metadata_str;
  }

  Result<EncryptionParamsPtr>
  DecodeEncryptionParamsFromEncryptionMetadata(
      const Slice& s, std::string* universe_key_id_output) override {
    Slice s_mutable(s);
    // 1. Get the size of the universe key id.
    RETURN_NOT_OK(CheckSliceCanBeDecoded(s_mutable, sizeof(uint32_t), "universe key id size"));
    auto universe_key_size = BigEndian::Load32(s_mutable.data());
    s_mutable.remove_prefix(sizeof(uint32_t));

    // 2. Get the universe key id.
    RETURN_NOT_OK(CheckSliceCanBeDecoded(s_mutable, universe_key_size, "universe key id"));
    std::string universe_key_id(s_mutable.cdata(), universe_key_size);
    if (universe_key_id_output) {
      *universe_key_id_output = universe_key_id;
    }
    s_mutable.remove_prefix(universe_key_size);

    // 3. Create an encryption stream from the universe key.
    auto universe_params = VERIFY_RESULT(
        universe_key_manager_->GetUniverseParamsWithVersion(universe_key_id));
    auto stream = VERIFY_RESULT(BlockAccessCipherStream::FromEncryptionParams(
        std::move(universe_params)));

    // 4. Get the size of the encryption params pb.
    RETURN_NOT_OK(CheckSliceCanBeDecoded(s_mutable, sizeof(uint32_t), "encryption params size"));
    auto encryption_params_pb_size = BigEndian::Load32(s_mutable.data());
    s_mutable.remove_prefix(sizeof(uint32_t));

    // 5. Decrypt the resulting data key.
    auto decrypted_data_key = static_cast<uint8_t*>(
        EncryptionBuffer::Get()->GetBuffer(s_mutable.size()));
    RETURN_NOT_OK(stream->Decrypt(0, s_mutable, decrypted_data_key));

    // 6. Convert the resulting decrypted data key into encryption params.
    RETURN_NOT_OK(CheckSliceCanBeDecoded(
        s_mutable, encryption_params_pb_size, "encryption params"));
    auto encryption_params_pb = VERIFY_RESULT(pb_util::ParseFromSlice<EncryptionParamsPB>(
        Slice(decrypted_data_key, encryption_params_pb_size)));
    return EncryptionParams::FromEncryptionParamsPB(encryption_params_pb);
  }

  uint32_t GetEncryptionMetadataStartIndex() override {
    return narrow_cast<uint32_t>(kEncryptionMagic.size() + sizeof(uint32_t));
  }

  Result<FileEncryptionStatus> GetFileEncryptionStatusFromPrefix(
      const Slice& s) override {
    FileEncryptionStatus status;
    status.is_encrypted = s.compare_prefix(Slice(kEncryptionMagic)) == 0;
    if (status.is_encrypted) {
      status.header_size = BigEndian::Load32(s.data() + kEncryptionMagic.size());
    }
    return status;
  }

  Result<std::string> GetLatestUniverseKeyId() override {
    auto universe_params = VERIFY_RESULT(universe_key_manager_->GetLatestUniverseParams());
    return universe_params.version_id;
  }

  Result<bool> IsEncryptionEnabled() override {
    return universe_key_manager_->IsEncryptionEnabled();
  }

 private:
  Status CheckSliceCanBeDecoded(const Slice& s, uint32_t expected_length, const string& field) {
    if (s.size() < expected_length) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "Error parsing field $0: expect $1 bytes found $2",
                               field, expected_length, s.size());
    }
    return Status::OK();
  }

  UniverseKeyManager* universe_key_manager_;

};

std::unique_ptr<HeaderManager> DefaultHeaderManager(UniverseKeyManager* universe_key_manager) {
  return std::make_unique<HeaderManagerImpl>(universe_key_manager);
}

} // namespace encryption
} // namespace yb
