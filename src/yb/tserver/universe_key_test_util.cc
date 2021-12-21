// Copyright (c) Yugabyte, Inc.
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

#include "yb/tserver/universe_key_test_util.h"

#include "yb/encryption/encryption_util.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/util/random_util.h"

namespace yb {
namespace tserver {

std::unique_ptr<encryption::UniverseKeyManager> GenerateTestUniverseKeyManager() {
  auto universe_key_manager = std::make_unique<encryption::UniverseKeyManager>();
  encryption::UniverseKeyRegistryPB registry;
  auto encryption_params = encryption::EncryptionParams::NewEncryptionParams();
  encryption::EncryptionParamsPB params_pb;
  encryption_params->ToEncryptionParamsPB(&params_pb);
  auto version_id = RandomHumanReadableString(16);
  (*registry.mutable_universe_keys())[version_id] = params_pb;
  registry.set_encryption_enabled(true);
  registry.set_latest_version_id(version_id);
  universe_key_manager->SetUniverseKeyRegistry(registry);
  return universe_key_manager;
}

}  // namespace tserver
}  // namespace yb
