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

#include <string>
#include <unordered_map>
#include <boost/functional/hash.hpp>

#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"

namespace yb::rpc {
class RpcContext;
}

namespace yb::tablet {
class TSLocalLockManager;
}
namespace yb::tserver {
class AcquireObjectLockRequestPB;
class AcquireObjectLockResponsePB;
class ReleaseObjectLockRequestPB;
class ReleaseObjectLockResponsePB;
class DdlLockEntriesPB;
}  // namespace yb::tserver

namespace yb::master {
class AcquireObjectLocksGlobalRequestPB;
class AcquireObjectLocksGlobalResponsePB;
class ReleaseObjectLocksGlobalRequestPB;
class ReleaseObjectLocksGlobalResponsePB;

class ObjectLockInfo;

class ObjectLockInfoManager {
 public:
  ObjectLockInfoManager(Master* master, CatalogManager* catalog_manager);
  virtual ~ObjectLockInfoManager();

  void LockObject(
      const AcquireObjectLocksGlobalRequestPB& req, AcquireObjectLocksGlobalResponsePB* resp,
      rpc::RpcContext rpc);

  void UnlockObject(
      const ReleaseObjectLocksGlobalRequestPB& req, ReleaseObjectLocksGlobalResponsePB* resp,
      rpc::RpcContext rpc);

  tserver::DdlLockEntriesPB ExportObjectLockInfo();
  void UpdateObjectLocks(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info);
  void UpdateTabletServerLeaseEpoch(const std::string& tserver_uuid, uint64_t current_lease_epoch);
  void Clear();
  std::shared_ptr<tablet::TSLocalLockManager> TEST_ts_local_lock_manager();
  std::shared_ptr<tablet::TSLocalLockManager> ts_local_lock_manager();

  // Releases any object locks that may have been taken by the specified tservers's previous
  // incarnations.
  void ReleaseLocksHeldByExpiredLeaseEpoch(
      const std::string& tserver_uuid, uint64 max_lease_epoch_to_release, bool wait = false,
      std::optional<LeaderEpoch> leader_epoch = std::nullopt);

  void BootstrapLocksPostLoad();

 private:
  template <class Req, class Resp>
  friend class UpdateAllTServers;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::master
