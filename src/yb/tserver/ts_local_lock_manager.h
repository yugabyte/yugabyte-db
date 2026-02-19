//
// Copyright (c) YugabyteDB, Inc.
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
//

#pragma once

#include <memory>

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.h"

#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/server/clock.h"
#include "yb/server/server_fwd.h"

#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/util/metrics.h"
#include "yb/util/status_callback.h"

namespace yb {

class ObjectLockTracker;
class ThreadPool;

namespace master {
class ReleaseObjectLocksGlobalRequestPB;
}
namespace tserver {
// LockManager for acquiring table/object locks of type TableLockType on a given object id.
// TSLocalLockManager uses LockManagerImpl<ObjectLockPrefix> to acheive the locking/unlocking
// behavior, yet the scope of the object lock is not just limited to the scope of the lock rpc
// request. If a lock request is responded with success, the object lock(s) are stored in memory
// at the LockManagerImpl until an explicit Unlock request is issued. In case of failure to lock,
// only the locks acquired so far as part of the same rpc request are released.
//
// TSLocalLockManager is currently used for table locking feature. In brief, all DMLs acquire
// required table/object locks on the local tserver's object lock manager, and all DDLs acquire
// table/object locks on all live tservers.
//
// Note that upon a server crash/restart, all acquired object locks are lost, which is the
// desired behavior for table locks. This is because, all DMLs hosted by the query layer client
// of the corresponding tserver would be aborted, hence we would want to release all object locks
// held by the DMLs. The master leader is responsible for re-acquiring locks corresponding to
// all active DDLs. The same applies on addition of a new tserver node, the master bootstraps
// it with all exisitng DDL (global) locks.
class TSLocalLockManager {
 public:
  TSLocalLockManager(
      const server::ClockPtr& clock, TabletServerIf* tablet_server,
      server::RpcServerBase& messenger_server, ThreadPool* thread_pool,
      const MetricEntityPtr& metric_entity,
      std::shared_ptr<ObjectLockTracker> lock_tracker = nullptr,
      docdb::ObjectLockSharedStateManager* shared_manager = nullptr);
  ~TSLocalLockManager();

  // Tries acquiring object locks with the specified modes and registers them against the given
  // transaction <txn, subtxn>. When locking a batch of keys, if the lock mananger errors while
  // acquiring the lock on the k'th key/record, all acquired locks i.e (1 to k-1) are released
  // and the error is returned back to the client. Note that previous successful locks corresponding
  // to the same txn remain unchanged until an explicit unlock request comes in.
  //
  // Note that the lock manager ignores the transaction's conflict with itself. So a txn can acquire
  // conflicting lock types on a key given that there aren't other txns with active conflciting
  // locks on the key.
  //
  // TODO: Augment the 'pg_locks' path to show the acquired/waiting object/table level locks.
  void AcquireObjectLocksAsync(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline,
      StdStatusCallback&& callback);

  // When subtxn id is set, releases all locks tagged against <txn, subtxn>. Else releases all
  // object locks owned by <txn>.
  //
  // There is no 1:1 mapping that exists among lock and unlock requests. A txn can acquire different
  // lock modes on a key multiple times, and will unlock them all with a single unlock rpc.
  Result<docdb::TxnBlockedTableLockRequests> ReleaseObjectLocks(
      const tserver::ReleaseObjectLockRequestPB& req, CoarseTimePoint deadline);

  void TrackDeadlineForGlobalAcquire(
      const TransactionId& txn_id, const SubTransactionId& subtxn_id,
      CoarseTimePoint apply_after_ht);
  void ScheduleReleaseForLostMessages(
      yb::client::YBClient& client, std::weak_ptr<TSLocalLockManager> lock_manager_weak,
      const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id,
      const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req);

  void Start(docdb::LocalWaitingTxnRegistry* waiting_txn_registry);

  void Shutdown();
  void StartShutdown();

  void DumpLocksToHtml(std::ostream& out);

  Status BootstrapDdlObjectLocks(const tserver::DdlLockEntriesPB& resp);

  bool IsBootstrapped() const;

  server::ClockPtr clock() const;

  void PopulateObjectLocks(
      google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos);

  size_t TEST_GrantedLocksSize();
  size_t TEST_WaitingLocksSize();
  void TEST_MarkBootstrapped();
  std::unordered_map<docdb::ObjectLockPrefix, docdb::LockState>
      TEST_GetLockStateMapForTxn(const TransactionId& txn) const;
  bool IsShutdownInProgress() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

void ReleaseWithRetriesGlobal(
    yb::client::YBClient& client, std::weak_ptr<TSLocalLockManager> lock_manager_weak,
    const TransactionId& txn_id, std::optional<SubTransactionId> subtxn_id,
    const std::shared_ptr<master::ReleaseObjectLocksGlobalRequestPB>& release_req);

void AcquireObjectLockLocallyWithRetries(
    std::weak_ptr<TSLocalLockManager> lock_manager, AcquireObjectLockRequestPB&& req,
    CoarseTimePoint deadline, StdStatusCallback&& lock_cb,
    std::function<Status(CoarseTimePoint)> check_txn_running);

}  // namespace tserver
}  // namespace yb
