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
#include "yb/util/encryption_util.h"
#include "yb/util/encryption.pb.h"
#include "yb/master/master.pb.h"
#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"

#include "yb/gutil/endian.h"

namespace yb {
namespace master {
namespace enterprise {

Result<yb::enterprise::EncryptionParamsPtr> ParseUniverseKeyFromFile(
    const std::string& key_path) {
  faststring contents;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), key_path, &contents));
  return yb::enterprise::EncryptionParams::FromKeyFile(Slice(contents));
}


CHECKED_STATUS RotateUniverseKey(const ChangeEncryptionInfoRequestPB* req,
                                 EncryptionInfoPB* encryption_info,
                                 ChangeEncryptionInfoResponsePB* resp) {
  bool prev_enabled = encryption_info->encryption_enabled();

  if (!prev_enabled && !req->encryption_enabled()) {
    return STATUS(InvalidArgument, "Cannot disable encryption for an already plaintext cluster.");
  }

  Slice registry_decrypted(encryption_info->universe_key_registry_encoded());
  string decrypted_registry;
  if (prev_enabled) {
    // The registry is encrypted, decrypt it and set registry_encoded_decrypted to the newly
    // decrypted registry.
    decrypted_registry = VERIFY_RESULT(
        DecryptUniverseKeyRegistry(registry_decrypted, encryption_info->key_path()));
    registry_decrypted = Slice(decrypted_registry);
  }

  // Decode the registry.
  auto universe_key_registry =
      VERIFY_RESULT(pb_util::ParseFromSlice<UniverseKeyRegistryPB>(registry_decrypted));
  universe_key_registry.set_encryption_enabled(req->encryption_enabled());
  faststring encoded;
  Slice registry_for_flush;
  string encrypted;
  if (!req->encryption_enabled()) {
    if (!pb_util::SerializeToString(universe_key_registry, &encoded)) {
      return STATUS(InvalidArgument, "Registry could not be encoded.");
    }
    registry_for_flush = Slice(encoded);
  } else {
    auto universe_key_id = yb::enterprise::UniverseKeyId::GenerateRandom().ToString();
    auto params = VERIFY_RESULT(ParseUniverseKeyFromFile(req->key_path()));
    EncryptionParamsPB params_pb;
    params->ToEncryptionParamsPB(&params_pb);
    (*universe_key_registry.mutable_universe_keys())[universe_key_id] = params_pb;
    universe_key_registry.set_latest_version_id(universe_key_id);
    if (!pb_util::SerializeToString(universe_key_registry, &encoded)) {
      return STATUS(InvalidArgument, "Registry could not be encoded.");
    }
    encrypted = VERIFY_RESULT(EncryptUniverseKeyRegistry(Slice(encoded), req->key_path()));
    registry_for_flush = Slice(encrypted);
  }

  encryption_info->set_encryption_enabled(req->encryption_enabled());
  encryption_info->set_universe_key_registry_encoded(
      registry_for_flush.data(), registry_for_flush.size());
  encryption_info->set_key_path(req->key_path());

  return Status::OK();
}

Result<std::string> DecryptUniverseKeyRegistry(const Slice& s, const std::string& key_path) {
  string output;
  output.resize(s.size());
  auto encryption_params = VERIFY_RESULT(ParseUniverseKeyFromFile(key_path));
  auto stream = VERIFY_RESULT(
      yb::enterprise::BlockAccessCipherStream::FromEncryptionParams(std::move(encryption_params)));
  RETURN_NOT_OK(stream->Decrypt(0, s, &(output)[0]));
  return output;
}

Result<std::string> EncryptUniverseKeyRegistry(const Slice& s, const std::string& key_path) {
  return DecryptUniverseKeyRegistry(s, key_path);
}

} // namespace enterprise
} // namespace master
} // namespace yb
