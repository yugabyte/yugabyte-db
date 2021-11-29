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

#ifndef YB_MASTER_ENCRYPTION_MANAGER_H
#define YB_MASTER_ENCRYPTION_MANAGER_H

#include <unordered_set>

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_util.h"
#include "yb/util/locks.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

namespace yb {

class UniverseKeysPB;

namespace master {

using HostPortSet = std::unordered_set<HostPort, HostPortHash>;

class EncryptionManager {
 public:
  EncryptionManager();

  CHECKED_STATUS AddUniverseKeys(const AddUniverseKeysRequestPB* req,
                                 AddUniverseKeysResponsePB* resp);

  CHECKED_STATUS GetUniverseKeyRegistry(const GetUniverseKeyRegistryRequestPB* req,
                                        GetUniverseKeyRegistryResponsePB* resp);

  CHECKED_STATUS HasUniverseKeyInMemory(const HasUniverseKeyInMemoryRequestPB* req,
                                        HasUniverseKeyInMemoryResponsePB* resp);

  CHECKED_STATUS ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                      EncryptionInfoPB* encryption_info);

  CHECKED_STATUS IsEncryptionEnabled(const EncryptionInfoPB& encryption_info,
                                     IsEncryptionEnabledResponsePB* resp);

  CHECKED_STATUS FillHeartbeatResponseEncryption(const EncryptionInfoPB& encryption_info,
                                                 TSHeartbeatResponsePB* resp);

  CHECKED_STATUS GetUniverseKeyRegistry(rpc::ProxyCache* proxy_cache);

  void PopulateUniverseKeys(const UniverseKeysPB& universe_key_registry);

  CHECKED_STATUS AddPeersToGetUniverseKeyFrom(const HostPortSet& hps);

 private:
  Result<std::string> GetLatestUniverseKey(const EncryptionInfoPB* encryption_info);

  Result<std::string> GetUniverseKeyFromRotateRequest(const ChangeEncryptionInfoRequestPB* req);

  std::string UniverseKeyIdsString();

  Result<std::string> GetKeyFromParams(bool in_memory,
                                       const std::string& key_path,
                                       const std::string& version_id);

  mutable simple_spinlock universe_key_mutex_;

  std::unique_ptr<UniverseKeysPB> universe_keys_ PT_GUARDED_BY(universe_key_mutex_);
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_ENCRYPTION_MANAGER_H
