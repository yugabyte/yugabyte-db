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

#pragma once

#include <condition_variable>
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
  // From an existing version id, generate encryption params. Used when creating readable files.
  Result<EncryptionParamsPtr> GetUniverseParamsWithVersion(
      const UniverseKeyId& version_id);
  // Get the latest universe key in the registry. Used when creating writable files.
  Result<UniverseKeyParams> GetLatestUniverseParams();
  Result<bool> IsEncryptionEnabled();
 private:
  // Registry from master.
  encryption::UniverseKeyRegistryPB universe_key_registry_ GUARDED_BY(mutex_);
  bool received_universe_key_registry_ GUARDED_BY(mutex_) = false;
  mutable std::mutex mutex_;

};

} // namespace encryption
} // namespace yb
