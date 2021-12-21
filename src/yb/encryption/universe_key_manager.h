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

#ifndef YB_ENCRYPTION_UNIVERSE_KEY_MANAGER_H
#define YB_ENCRYPTION_UNIVERSE_KEY_MANAGER_H

#include <shared_mutex>

#include "yb/encryption/encryption.pb.h"
#include "yb/encryption/encryption_util.h"

namespace yb {
namespace encryption {

// Class is responsible for saving the universe key registry from master on heartbeat for use
// in creating new files and reading exising files.
class UniverseKeyManager {
 public:
  static Result<std::unique_ptr<UniverseKeyManager>> FromKey(
      const std::string& key_id, const Slice& key_data);
  void SetUniverseKeyRegistry(const UniverseKeyRegistryPB& universe_key_registry);
  void SetUniverseKeys(const UniverseKeysPB& universe_keys);
  // From an existing version id, generate encryption params. Used when creating readable files.
  Result<EncryptionParamsPtr> GetUniverseParamsWithVersion(
      const UniverseKeyId& version_id);
  // Get the latest universe key in the registry. Used when creating writable files.
  Result<UniverseKeyParams> GetLatestUniverseParams();
  bool IsEncryptionEnabled();
  bool ReceivedUniverseKeys();

  void SetGetUniverseKeysCallback(std::function<void()> get_universe_keys_callback) {
    get_universe_keys_callback_ = get_universe_keys_callback;
  }

 private:
  // Registry from master.
  encryption::UniverseKeyRegistryPB universe_key_registry_;

  mutable std::mutex mutex_;
  std::condition_variable cond_;

  // Set to true once the registry has been received from master.
  bool received_universe_keys_ = false;

  std::function<void()> get_universe_keys_callback_;
};

} // namespace encryption
} // namespace yb

#endif // YB_ENCRYPTION_UNIVERSE_KEY_MANAGER_H
