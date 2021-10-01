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
#include "yb/util/pb_util.h"

namespace yb {

Result<std::unique_ptr<UniverseKeyManager>> UniverseKeyManager::FromKey(
    const std::string& key_id, const Slice& key_data) {
  auto universe_key_manager = std::make_unique<UniverseKeyManager>();
  UniverseKeyRegistryPB universe_key_registry;
  universe_key_registry.set_encryption_enabled(true);
  universe_key_registry.set_latest_version_id(key_id);
  auto encryption_params = VERIFY_RESULT(yb::EncryptionParams::FromSlice(key_data));
  yb::EncryptionParamsPB params_pb;
  encryption_params->ToEncryptionParamsPB(&params_pb);
  (*universe_key_registry.mutable_universe_keys())[key_id] = params_pb;
  universe_key_manager->SetUniverseKeyRegistry(universe_key_registry);
  return universe_key_manager;
}

void UniverseKeyManager::SetUniverseKeys(const UniverseKeysPB& universe_keys) {
  {
    std::unique_lock<std::mutex> l(mutex_);
    auto& keys_map = *universe_key_registry_.mutable_universe_keys();
    for (const auto& entry : universe_keys.map()) {
      auto encryption_params_res = EncryptionParams::FromSlice(entry.second);
      if (!encryption_params_res.ok()) {
        return;
      }
      EncryptionParamsPB params_pb;
      (*encryption_params_res)->ToEncryptionParamsPB(&params_pb);
      keys_map[entry.first] = params_pb;
    }
  }
  cond_.notify_all();
}

void UniverseKeyManager::SetUniverseKeyRegistry(
    const UniverseKeyRegistryPB& universe_key_registry) {
  {
    std::unique_lock<std::mutex> l(mutex_);
    universe_key_registry_ = universe_key_registry;
    received_universe_keys_ = true;
  }
  cond_.notify_all();
}

Result<yb::EncryptionParamsPtr> UniverseKeyManager::GetUniverseParamsWithVersion(
    const UniverseKeyId& version_id) {
  std::unique_lock<std::mutex> l(mutex_);
  auto it = universe_key_registry_.universe_keys().find(version_id);
  if (it == universe_key_registry_.universe_keys().end()) {
    if (universe_key_registry_.universe_keys().empty()) {
      l.unlock();
      get_universe_keys_callback_();
      l.lock();
      it = universe_key_registry_.universe_keys().find(version_id);
    }
    if (it == universe_key_registry_.universe_keys().end()) {
      return STATUS_SUBSTITUTE(
          InvalidArgument, "Key with version number $0 does not exist.", version_id);
    }
  }
  return EncryptionParams::FromEncryptionParamsPB(it->second);
}

Result<UniverseKeyParams> UniverseKeyManager::GetLatestUniverseParams() {
  std::unique_lock<std::mutex> l(mutex_);
  cond_.wait(l, [&] { return received_universe_keys_; });
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
  std::unique_lock<std::mutex> l(mutex_);
  return universe_key_registry_.encryption_enabled();
}

bool UniverseKeyManager::ReceivedUniverseKeys() {
  std::unique_lock<std::mutex> l(mutex_);
  return received_universe_keys_;
}

} // namespace yb
