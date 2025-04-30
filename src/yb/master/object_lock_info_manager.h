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

#include "yb/common/transaction.h"

#include "yb/master/leader_epoch.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"

#include "yb/util/status_callback.h"

namespace yb::rpc {
class RpcContext;
}

namespace yb::tserver {
class TSLocalLockManager;
using TSLocalLockManagerPtr = std::shared_ptr<TSLocalLockManager>;
}
namespace yb::tserver {
class AcquireObjectLockRequestPB;
class AcquireObjectLockResponsePB;
class ReleaseObjectLockRequestPB;
class ReleaseObjectLockResponsePB;
class DdlLockEntriesPB;
}  // namespace yb::tserver

namespace yb {
class CountDownLatch;
}

namespace yb::master {
class AcquireObjectLocksGlobalRequestPB;
class AcquireObjectLocksGlobalResponsePB;
class ReleaseObjectLocksGlobalRequestPB;
class ReleaseObjectLocksGlobalResponsePB;

class ObjectLockInfo;

class ObjectLockInfoManager {
 public:
  ObjectLockInfoManager(Master& master, CatalogManager& catalog_manager);
  virtual ~ObjectLockInfoManager();

  void Start();

  void LockObject(
      const AcquireObjectLocksGlobalRequestPB& req, AcquireObjectLocksGlobalResponsePB& resp,
      rpc::RpcContext rpc);

  void UnlockObject(
      const ReleaseObjectLocksGlobalRequestPB& req, ReleaseObjectLocksGlobalResponsePB& resp,
      rpc::RpcContext rpc);
  void ReleaseLocksForTxn(const TransactionId& txn_id);

  Status RefreshYsqlLease(const RefreshYsqlLeaseRequestPB& req, RefreshYsqlLeaseResponsePB& resp,
                          rpc::RpcContext& rpc,
                          const LeaderEpoch& epoch);

  tserver::DdlLockEntriesPB ExportObjectLockInfo();
  void UpdateObjectLocks(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info);
  void RelaunchInProgressRequests(const LeaderEpoch& leader_epoch, const std::string& tserver_uuid);
  void Clear();
  tserver::TSLocalLockManagerPtr TEST_ts_local_lock_manager();
  tserver::TSLocalLockManagerPtr ts_local_lock_manager();

  // Releases any object locks that may have been taken by the specified tservers's previous
  // incarnations.
  std::shared_ptr<CountDownLatch> ReleaseLocksHeldByExpiredLeaseEpoch(
      const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
      std::optional<LeaderEpoch> leader_epoch = std::nullopt);

  std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB> GetLeaseInfos() const;

  void BootstrapLocksPostLoad();

 private:
  template <class Req>
  friend class UpdateAllTServers;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::master
