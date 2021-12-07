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

#include <string>

#include "yb/util/status_fwd.h"

namespace yb {

class Slice;

namespace master {

class EncryptionInfoPB;

namespace enterprise {

// Decrypt the universe key registry s, with universe key universe_key.
Result<std::string> DecryptUniverseKeyRegistry(const Slice& s, const Slice& universe_key);

// Rotate a new universe key into the sys catalog. Triggered by the user (yb-admin or YW).
CHECKED_STATUS RotateUniverseKey(const Slice& old_universe_key,
                                 const Slice& new_universe_key,
                                 const std::string& new_key_version_id,
                                 bool enable,
                                 EncryptionInfoPB* encryption_info);
} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_UNIVERSE_KEY_REGISTRY_SERVICE_H
