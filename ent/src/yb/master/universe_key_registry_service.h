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
#ifndef ENT_SRC_YB_MASTER_UNIVERSE_KEY_REGISTRY_SERVICE_H
#define ENT_SRC_YB_MASTER_UNIVERSE_KEY_REGISTRY_SERVICE_H

#include "yb/util/status.h"
#include "yb/util/result.h"

namespace yb {

class UniverseKeyRegistryPB;
class UniverseKeyEntryPB;

namespace enterprise {

struct EncryptionParams;

}

namespace master {

class ChangeEncryptionInfoRequestPB;
class ChangeEncryptionInfoResponsePB;
class EncryptionInfoPB;

namespace enterprise {

// Rotate a new universe key into the sys catalog. Triggered by the user (yb-admin).
CHECKED_STATUS RotateUniverseKey(const ChangeEncryptionInfoRequestPB* req,
    EncryptionInfoPB* encryption_info, ChangeEncryptionInfoResponsePB* resp);

// Encrypt/Decrypt the registry represented by a slice, using key path, a path to the latest
// universe key.
Result<std::string> DecryptUniverseKeyRegistry(const Slice& s, const std::string& key_path);
Result<std::string> EncryptUniverseKeyRegistry(const Slice& s, const std::string& key_path);

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_UNIVERSE_KEY_REGISTRY_SERVICE_H
