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

#include "yb/master/universe_key_registry_service.h"

#include "yb/encryption/encryption_util.h"
#include "yb/encryption/encryption.pb.h"

#include "yb/master/catalog_entity_info.pb.h"

#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"

#include "yb/gutil/endian.h"

using std::string;

namespace yb {
namespace master {

Result<std::string> DecryptUniverseKeyRegistry(const Slice& s, const Slice& universe_key) {
  string output;
  output.resize(s.size());
  auto encryption_params = VERIFY_RESULT(encryption::EncryptionParams::FromSlice(universe_key));
  auto stream = VERIFY_RESULT(
      encryption::BlockAccessCipherStream::FromEncryptionParams(std::move(encryption_params)));
  RETURN_NOT_OK(stream->Decrypt(0, s, &output[0]));
  return output;
}

Result<std::string> EncryptUniverseKeyRegistry(const Slice& s, const Slice& universe_key) {
  return DecryptUniverseKeyRegistry(s, universe_key);
}

Status RotateUniverseKey(const Slice& old_universe_key,
                         const Slice& new_universe_key,
                         const encryption::UniverseKeyId& new_key_version_id,
                         bool enable,
                         EncryptionInfoPB* encryption_info) {
  bool prev_enabled = encryption_info->encryption_enabled();

  LOG(INFO) << Format("RotateUniverseKey: prev_enabled: $0, enable: $1, new_key_version_id: $2",
                      prev_enabled, enable, new_key_version_id);

  if (!prev_enabled && !enable) {
    return STATUS(InvalidArgument, "Cannot disable encryption for an already plaintext cluster.");
  }

  Slice registry_decrypted(encryption_info->universe_key_registry_encoded());
  string decrypted_registry;
  if (prev_enabled) {
    // The registry is encrypted, decrypt it and set registry_encoded_decrypted to the newly
    // decrypted registry.
    LOG_IF(DFATAL, old_universe_key.empty());
    decrypted_registry = VERIFY_RESULT(DecryptUniverseKeyRegistry(
        registry_decrypted, old_universe_key));
    registry_decrypted = Slice(decrypted_registry);
  }

  // Decode the registry.
  auto universe_key_registry =
      VERIFY_RESULT(pb_util::ParseFromSlice<encryption::UniverseKeyRegistryPB>(registry_decrypted));
  universe_key_registry.set_encryption_enabled(enable);
  faststring encoded;
  Slice registry_for_flush;
  string encrypted;
  if (!enable) {
    RETURN_NOT_OK(pb_util::SerializeToString(universe_key_registry, &encoded));
    registry_for_flush = Slice(encoded);
  } else {
    LOG_IF(DFATAL, new_universe_key.empty());
    auto params = VERIFY_RESULT(encryption::EncryptionParams::FromSlice(new_universe_key));
    encryption::EncryptionParamsPB params_pb;
    params->ToEncryptionParamsPB(&params_pb);
    (*universe_key_registry.mutable_universe_keys())[new_key_version_id] = params_pb;
    universe_key_registry.set_latest_version_id(new_key_version_id);
    RETURN_NOT_OK(pb_util::SerializeToString(universe_key_registry, &encoded));

    encrypted = VERIFY_RESULT(EncryptUniverseKeyRegistry(Slice(encoded), new_universe_key));
    registry_for_flush = Slice(encrypted);
  }

  encryption_info->set_encryption_enabled(enable);
  encryption_info->set_universe_key_registry_encoded(
      registry_for_flush.data(), registry_for_flush.size());
  encryption_info->set_latest_version_id(new_key_version_id);

  return Status::OK();
}

} // namespace master
} // namespace yb
