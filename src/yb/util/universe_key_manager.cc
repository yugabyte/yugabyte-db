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

#include "yb/util/universe_key_manager.h"

namespace yb {
namespace enterprise {

Result<std::unique_ptr<UniverseKeyManager>> UniverseKeyManager::FromKey(
    const std::string& key_id, const Slice& key_data) {
  auto universe_key_manager = std::make_unique<UniverseKeyManager>();
  UniverseKeyRegistryPB universe_key_registry;
  universe_key_registry.set_encryption_enabled(true);
  universe_key_registry.set_latest_version_id(key_id);
  auto encryption_params = VERIFY_RESULT(yb::enterprise::EncryptionParams::FromSlice(key_data));
  yb::EncryptionParamsPB params_pb;
  encryption_params->ToEncryptionParamsPB(&params_pb);
  (*universe_key_registry.mutable_universe_keys())[key_id] = params_pb;
  universe_key_manager->SetUniverseKeyRegistry(universe_key_registry);
  return universe_key_manager;
}

void UniverseKeyManager::SetUniverseKeyRegistry(
    const UniverseKeyRegistryPB& universe_key_registry) {
  {
    std::lock_guard<std::mutex> l(mutex_);
    universe_key_registry_ = universe_key_registry;
    received_registry_ = true;
  }
  cond_.notify_all();
}

Result<yb::enterprise::EncryptionParamsPtr> UniverseKeyManager::GetUniverseParamsWithVersion(
    const UniverseKeyId& version_id) {
  auto l = EnsureRegistryReceived();
  const auto it = universe_key_registry_.universe_keys().find(version_id);
  if (it == universe_key_registry_.universe_keys().end()) {
    return STATUS_SUBSTITUTE(
        InvalidArgument, "Key with version number $0 does not exist.", version_id);
  }
  return EncryptionParams::FromEncryptionParamsPB(it->second);
}

Result<UniverseKeyParams> UniverseKeyManager::GetLatestUniverseParams() {
  auto l = EnsureRegistryReceived();
  const auto it = universe_key_registry_.universe_keys().find(
      universe_key_registry_.latest_version_id());
  if (it == universe_key_registry_.universe_keys().end()) {
    return STATUS(IllegalState, "Could not find a latest universe key.");
  }

  UniverseKeyParams universe_key_params;
  universe_key_params.version_id = it->first;
  universe_key_params.params = VERIFY_RESULT(EncryptionParams::FromEncryptionParamsPB(it->second));
  return universe_key_params;
}

bool UniverseKeyManager::IsEncryptionEnabled() {
  std::lock_guard<std::mutex> l(mutex_);
  return universe_key_registry_.encryption_enabled();
}

std::unique_lock<std::mutex> UniverseKeyManager::EnsureRegistryReceived() {
  std::unique_lock<std::mutex> l(mutex_);
  cond_.wait(l, [&] { return received_registry_; });
  return l;
}

} // namespace enterprise
} // namespace yb
