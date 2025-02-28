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

#include "yb/master/object_lock_info_manager.h"

#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/wire_protocol.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

namespace yb {
namespace master {

using namespace std::literals;
using server::MonitoredTaskState;
using strings::Substitute;
using tserver::AcquireObjectLockRequestPB;
using tserver::AcquireObjectLockResponsePB;
using tserver::ReleaseObjectLockRequestPB;
using tserver::ReleaseObjectLockResponsePB;
using tserver::TabletServerErrorPB;

namespace {

Status ValidateLockRequest(
    const AcquireObjectLockRequestPB& req,
    const std::optional<uint64_t>& requestor_latest_lease_epoch) {
  if (!req.subtxn_id()) {
    return STATUS_FORMAT(
        InvalidArgument, "subtxn_id not set for exclusive object lock req $0",
        req.ShortDebugString());
  }
  if (!requestor_latest_lease_epoch || req.lease_epoch() != *requestor_latest_lease_epoch) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Requestor has a lease epoch of $0 but the latest valid lease epoch for this tserver is $1",
        req.lease_epoch(), requestor_latest_lease_epoch);
  }
  return Status::OK();
}

} // namespace

class ObjectLockInfoManager::Impl {
 public:
  Impl(Master* master, CatalogManager* catalog_manager)
      : master_(master),
        catalog_manager_(catalog_manager),
        clock_(master->clock()),
        local_lock_manager_(std::make_shared<tserver::TSLocalLockManager>(clock_)) {}

  void LockObject(
      AcquireObjectLockRequestPB req, AcquireObjectLocksGlobalResponsePB* resp,
      rpc::RpcContext rpc);
  void LockObject(
      const tserver::AcquireObjectLockRequestPB& req, rpc::RpcContext context,
      StdStatusCallback callback);

  void UnlockObject(
      ReleaseObjectLockRequestPB req, ReleaseObjectLocksGlobalResponsePB* resp,
      rpc::RpcContext rpc);
  void UnlockObject(
      const tserver::ReleaseObjectLockRequestPB& req, std::optional<rpc::RpcContext> context,
      std::optional<LeaderEpoch> leader_epoch, StdStatusCallback callback,
      bool remove_lease_epoch_entry = false);

  void ReleaseLocksHeldByExpiredLeaseEpoch(
      const std::string& tserver_uuid, uint64 max_lease_epoch_to_release, bool wait,
      std::optional<LeaderEpoch> leader_epoch);

  void BootstrapLocksPostLoad();

  Status PersistRequest(LeaderEpoch epoch, const AcquireObjectLockRequestPB& req) EXCLUDES(mutex_);
  Status PersistRequest(
      LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req, bool remove_lease_epoch_entry)
      EXCLUDES(mutex_);

  tserver::DdlLockEntriesPB ExportObjectLockInfo() EXCLUDES(mutex_);
  void BootstrapLocalLocks() EXCLUDES(mutex_);
  void UpdateTabletServerLeaseEpoch(const std::string& tserver_uuid, uint64_t current_lease_epoch)
      EXCLUDES(mutex_);

  void UpdateObjectLocks(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info)
      EXCLUDES(mutex_);
  void Clear() EXCLUDES(mutex_);

  const server::ClockPtr& clock() const {
    return clock_;
  }

  std::shared_ptr<ObjectLockInfo> GetOrCreateObjectLockInfo(const std::string& key)
      EXCLUDES(mutex_);

  std::shared_ptr<tserver::TSLocalLockManager> TEST_ts_local_lock_manager() EXCLUDES(mutex_) {
    // No need to acquire the leader lock for testing.
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  /*
  The local lock manager is used to acquire and release locks on the master itself.

  We will need to recreate the state of the ts_local_lock_manager on the master
  when the master assumes leadership. This will be done by clearing the TSLocalManager and
  replaying the DDL lock requests
  */
  std::shared_ptr<tserver::TSLocalLockManager> ts_local_lock_manager() EXCLUDES(mutex_) {
    catalog_manager_->AssertLeaderLockAcquiredForReading();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

 private:
  std::shared_ptr<tserver::TSLocalLockManager> ts_local_lock_manager_during_catalog_loading()
      EXCLUDES(mutex_) {
    catalog_manager_->AssertLeaderLockAcquiredForWriting();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  std::optional<uint64_t> GetLeaseEpoch(const std::string& ts_uuid) EXCLUDES(mutex_);

  Master* master_;
  CatalogManager* catalog_manager_;
  const server::ClockPtr clock_;

  using MutexType = std::mutex;
  using LockGuard = std::lock_guard<MutexType>;
  mutable MutexType mutex_;
  std::unordered_map<std::string, std::shared_ptr<ObjectLockInfo>> object_lock_infos_map_
      GUARDED_BY(mutex_);
  // The latest lease epoch for each tserver.
  std::unordered_map<std::string, uint64_t> current_lease_epochs_ GUARDED_BY(mutex_);

  std::shared_ptr<tserver::TSLocalLockManager> local_lock_manager_ GUARDED_BY(mutex_);
};

template <class Req>
class UpdateAll {
 public:
  UpdateAll() = default;
  virtual ~UpdateAll() = default;
  virtual const Req& request() const = 0;
  virtual CoarseTimePoint GetClientDeadline() const = 0;
};

template <class Req, class Resp>
class UpdateAllTServers : public std::enable_shared_from_this<UpdateAllTServers<Req, Resp>>,
                          public UpdateAll<Req> {
 public:
  UpdateAllTServers(
      Master* master, CatalogManager* catalog_manager,
      ObjectLockInfoManager::Impl* object_lock_info_manager, const Req& req,
      StdStatusCallback callback, std::optional<rpc::RpcContext> context,
      std::optional<LeaderEpoch> epoch, std::optional<uint64_t> requestor_latest_lease_epoch,
      bool remove_lease_epoch_entry = false);

  void Launch();
  const Req& request() const override {
    return req_;
  }

  CoarseTimePoint GetClientDeadline() const override {
    return context_ ? context_->GetClientDeadline() : CoarseTimePoint::max();
  }

 private:
  void LaunchFrom(size_t from_idx);
  void Done(size_t i, const Status& s);
  void CheckForDone();
  // Relaunches if there have been new TServers who joined. Returns true if relaunched.
  bool RelaunchIfNecessary();
  void DoneAll();
  Status AfterRpcs();
  Status BeforeRpcs();
  void DoCallbackAndRespond(const Status& s);

  std::shared_ptr<RetrySpecificTSRpcTask> TServerTaskFor(
      const TabletServerId& ts_uuid, StdStatusCallback callback);

  std::optional<LeaderEpoch> epoch_;
  Master* master_;
  CatalogManager* catalog_manager_;
  ObjectLockInfoManager::Impl* object_lock_info_manager_;
  TSDescriptorVector ts_descriptors_;
  std::atomic<size_t> ts_pending_;
  std::vector<Status> statuses_;
  const Req req_;
  StdStatusCallback callback_;
  std::optional<rpc::RpcContext> context_;
  std::optional<uint64_t> requestor_latest_lease_epoch_;
  bool remove_lease_epoch_entry_;
};

template <class Req, class Resp>
class UpdateTServer : public RetrySpecificTSRpcTask {
 public:
  UpdateTServer(
      Master* master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
      std::shared_ptr<UpdateAll<Req>> shared_all_tservers, StdStatusCallback callback);

  server::MonitoredTaskType type() const override { return server::MonitoredTaskType::kObjectLock; }

  std::string type_name() const override { return "Object Lock"; }

  std::string description() const override;

  std::string ToString() const;

 protected:
  void Finished(const Status& status) override;

  const Req& request() const {
    return shared_all_tservers_->request();
  }

 private:
  TabletId tablet_id() const override { return TabletId(); }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  StdStatusCallback callback_;
  Resp resp_;

  std::shared_ptr<UpdateAll<Req>> shared_all_tservers_;
};

namespace {

const std::string kNotTheMasterLeader = "Master is not the leader";
const std::string kEpochChanged = "Epoch changed";

Status CheckLeaderLockStatus(const ScopedLeaderSharedLock& l, std::optional<LeaderEpoch> epoch) {
  if (!l.IsInitializedAndIsLeader()) {
    return STATUS(IllegalState, kNotTheMasterLeader);
  }
  if (epoch.has_value() && l.epoch() != *epoch) {
    return STATUS(IllegalState, kEpochChanged);
  }
  return Status::OK();
}

AcquireObjectLockRequestPB TserverRequestFor(
    const AcquireObjectLocksGlobalRequestPB& master_request) {
  AcquireObjectLockRequestPB req;
  req.set_txn_id(master_request.txn_id());
  req.set_subtxn_id(master_request.subtxn_id());
  req.set_session_host_uuid(master_request.session_host_uuid());
  for (auto& entry : master_request.object_locks()) {
    auto* lock = req.add_object_locks();
    lock->set_database_oid(entry.database_oid());
    lock->set_object_oid(entry.object_oid());
    lock->set_lock_type(entry.lock_type());
  }
  req.set_lease_epoch(master_request.lease_epoch());
  if (master_request.has_ignore_after_hybrid_time()) {
    req.set_ignore_after_hybrid_time(master_request.ignore_after_hybrid_time());
  }
  if (master_request.has_propagated_hybrid_time()) {
    req.set_propagated_hybrid_time(master_request.propagated_hybrid_time());
  }
  return req;
}

ReleaseObjectLockRequestPB TserverRequestFor(
    const ReleaseObjectLocksGlobalRequestPB& master_request) {
  ReleaseObjectLockRequestPB req;
  req.set_txn_id(master_request.txn_id());
  if (master_request.has_subtxn_id()) {
    req.set_subtxn_id(master_request.subtxn_id());
  }
  req.set_session_host_uuid(master_request.session_host_uuid());
  req.set_lease_epoch(master_request.lease_epoch());
  if (master_request.has_apply_after_hybrid_time()) {
    req.set_apply_after_hybrid_time(master_request.apply_after_hybrid_time());
  }
  if (master_request.has_propagated_hybrid_time()) {
    req.set_propagated_hybrid_time(master_request.propagated_hybrid_time());
  }
  return req;
}

}  // namespace

ObjectLockInfoManager::ObjectLockInfoManager(Master* master, CatalogManager* catalog_manager)
    : impl_(std::make_unique<ObjectLockInfoManager::Impl>(master, catalog_manager)) {}

ObjectLockInfoManager::~ObjectLockInfoManager() = default;

void ObjectLockInfoManager::LockObject(
    const AcquireObjectLocksGlobalRequestPB& req, AcquireObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext rpc) {
  impl_->LockObject(TserverRequestFor(req), resp, std::move(rpc));
}

void ObjectLockInfoManager::UnlockObject(
    const ReleaseObjectLocksGlobalRequestPB& req, ReleaseObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext rpc) {
  impl_->UnlockObject(TserverRequestFor(req), resp, std::move(rpc));
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::ExportObjectLockInfo() {
  return impl_->ExportObjectLockInfo();
}

void ObjectLockInfoManager::UpdateTabletServerLeaseEpoch(
    const std::string& tserver_uuid, uint64_t current_lease_epoch) {
  impl_->UpdateTabletServerLeaseEpoch(tserver_uuid, current_lease_epoch);
}

void ObjectLockInfoManager::ReleaseLocksHeldByExpiredLeaseEpoch(
    const std::string& tserver_uuid, uint64 max_lease_epoch_to_release, bool wait,
    std::optional<LeaderEpoch> leader_epoch) {
  impl_->ReleaseLocksHeldByExpiredLeaseEpoch(
      tserver_uuid, max_lease_epoch_to_release, wait, leader_epoch);
}

void ObjectLockInfoManager::BootstrapLocksPostLoad() {
  impl_->BootstrapLocksPostLoad();
}

void ObjectLockInfoManager::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  impl_->UpdateObjectLocks(tserver_uuid, info);
}

void ObjectLockInfoManager::Clear() { impl_->Clear(); }

std::shared_ptr<tserver::TSLocalLockManager> ObjectLockInfoManager::ts_local_lock_manager() {
  return impl_->ts_local_lock_manager();
}

std::shared_ptr<tserver::TSLocalLockManager> ObjectLockInfoManager::TEST_ts_local_lock_manager() {
  return impl_->TEST_ts_local_lock_manager();
}

std::shared_ptr<ObjectLockInfo> ObjectLockInfoManager::Impl::GetOrCreateObjectLockInfo(
    const std::string& key) {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(key);
  if (it != object_lock_infos_map_.end()) {
    return it->second;
  } else {
    std::shared_ptr<ObjectLockInfo> info = std::make_shared<ObjectLockInfo>(key);
    object_lock_infos_map_.insert({key, info});
    return info;
  }
}

Status ObjectLockInfoManager::Impl::PersistRequest(
    LeaderEpoch epoch, const AcquireObjectLockRequestPB& req) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto key = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
  auto lock = object_lock_info->LockForWrite();
  auto& txns_map = (*lock.mutable_data()->pb.mutable_lease_epochs())[req.lease_epoch()];
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
  auto& subtxns_map = (*txns_map.mutable_transactions())[txn_id.ToString()];
  auto& db_map = (*subtxns_map.mutable_subtxns())[req.subtxn_id()];
  for (const auto& object_lock : req.object_locks()) {
    auto& object_map = (*db_map.mutable_dbs())[object_lock.database_oid()];
    auto& types = (*object_map.mutable_objects())[object_lock.object_oid()];
    types.add_lock_type(object_lock.lock_type());
  }
  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

Status ObjectLockInfoManager::Impl::PersistRequest(
    LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req, bool remove_lease_epoch_entry) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto key = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
  auto lock = object_lock_info->LockForWrite();
  auto& txns_map = (*lock.mutable_data()->pb.mutable_lease_epochs())[req.lease_epoch()];
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
  if (!req.subtxn_id()) {
    txns_map.mutable_transactions()->erase(txn_id.ToString());
  } else {
    auto& subtxns_map = (*txns_map.mutable_transactions())[txn_id.ToString()];
    subtxns_map.mutable_subtxns()->erase(req.subtxn_id());
  }
  if (remove_lease_epoch_entry && txns_map.transactions().empty()) {
    lock.mutable_data()->pb.mutable_lease_epochs()->erase(req.lease_epoch());
  }

  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

namespace {

void ExportObjectLocksForTxn(
    const master::SysObjectLockEntryPB_DBObjectsMapPB& dbs_map,
    tserver::AcquireObjectLockRequestPB* req) {
  for (const auto& [db_id, objects_map] : dbs_map.dbs()) {
    for (const auto& [object_id, lock_types] : objects_map.objects()) {
      for (const auto& type : lock_types.lock_type()) {
        auto* lock = req->add_object_locks();
        lock->set_database_oid(db_id);
        lock->set_object_oid(object_id);
        lock->set_lock_type(TableLockType(type));
      }
    }
  }
}

template <class Resp>
void FillErrorIfRequired(const Status& status, Resp* resp) {
  if (!status.ok()) {
    if (status.IsIllegalState() && status.message().ToBuffer() == kNotTheMasterLeader) {
      resp->mutable_error()->set_code(MasterErrorPB::NOT_THE_LEADER);
    } else {
      resp->mutable_error()->set_code(MasterErrorPB::UNKNOWN_ERROR);
    }
    StatusToPB(status, resp->mutable_error()->mutable_status());
  }
}

}  // namespace

void ObjectLockInfoManager::Impl::BootstrapLocalLocks() {
  CHECK_OK(ts_local_lock_manager_during_catalog_loading()->BootstrapDdlObjectLocks(
      ExportObjectLockInfo()));
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::Impl::ExportObjectLockInfo() {
  VLOG(2) << __PRETTY_FUNCTION__;
  tserver::DdlLockEntriesPB entries;
  {
    LockGuard lock(mutex_);
    for (const auto& [host_uuid, per_host_entry] : object_lock_infos_map_) {
      auto l = per_host_entry->LockForRead();
      auto lease_epoch_it = current_lease_epochs_.find(host_uuid);
      if (lease_epoch_it == current_lease_epochs_.end()) {
        continue;
      }
      auto txns_map_it = l->pb.lease_epochs().find(lease_epoch_it->second);
      if (txns_map_it == l->pb.lease_epochs().end()) {
        continue;
      }
      for (const auto& [txn_id_str, subtxns_map] : txns_map_it->second.transactions()) {
        auto txn_id = CHECK_RESULT(TransactionId::FromString(txn_id_str));
        for (const auto& [subtxn_id, dbs_map] : subtxns_map.subtxns()) {
          auto* lock_entries_pb = entries.add_lock_entries();
          lock_entries_pb->set_session_host_uuid(host_uuid);
          lock_entries_pb->set_txn_id(txn_id.data(), txn_id.size());
          lock_entries_pb->set_subtxn_id(subtxn_id);
          ExportObjectLocksForTxn(dbs_map, lock_entries_pb);
        }
      }
    }
  }
  VLOG(3) << "Exported " << yb::ToString(entries);
  return entries;
}

/*
  Taking DDL locks at the master involves 2 steps:
  1. Acquire the locks locally - in the master's TSLocalLockManager.
     The TSLocalLockManager should only contain the DDL lock requests. Thus,
     if there is a conflict detected here, the request waits here before
     requesting the locks from the TServers.
  2. Once the locks are locally acquired -- this guarantees that the lock request
     does not conflict with any other DDL lock request already granted or in progress.
     Thus, we update the persisted state in the SysCatalog, to ensure that any
     new TServer joining the cluster will get the latest state of the locks.
  3. We then proceed to request the locks to be taken at all the registered TServers.
      This is done in parallel. If any of the TServers fail to acquire the lock, the
      request will fail. However the locks that were acquired locally will not be released
      or cleaned up as part of this RPC.
      a) The YBClient may choose to retry this, and run the request to completion (since the
      lock requests are idempotent), or it may bubble up the failure to the Pgclient.
      b) PgClient session is expected to release the lock(s) for any request that it has made,
      ** even if the request/rpc fails **, when the session/transaction finishes/aborts.
         This is easily done using the *release_all_locks* flag in the ReleaseObjectLockRequestPB.
      c) If the PgClient/Session itself fails, these locks will be released when the failure
      is detected and the session is cleaned up.


  The same steps are followed for releasing the locks.
   - But what if the release fails due to miscommunication with the TServer? The Async RPC
     framework will retry the RPC until the TServer loses its YSQL lease.
     If the TServer loses its lease, the TSLocalLockManager should clean up its state and
     bootstrap again from the master based on the persisted state in the SysCatalog.

  Amit: What if we are in a wedged state where
    - incoming RPCs from TServer -> Master (Heartbeats, etc) are fine.
     But,
    - the RPCs from Master -> TServer are failing.
    This is a rare case, but can happen.
    Possible solution:
    We could have a mechanism to detect this and ensure that the master does not
    referesh the YSQL lease for such a TServer which is not able to accept RPCs from the master.
    --- this is not implemented -- just brainstorming. TBD.

  Master failover-
    - The new master should be able to recreate the state of the TSLocalLockManager. However, it
    may not launch in-progress rpc's again? Or should it?
    - The RPCs can be retried by the YBClient/PgClient, or left for the release request to clean up.
*/
void ObjectLockInfoManager::Impl::LockObject(
    AcquireObjectLockRequestPB req, AcquireObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext context) {
  LockObject(req, std::move(context), [resp, clock = clock_](const Status& s) {
    resp->set_propagated_hybrid_time(clock->Now().ToUint64());
    FillErrorIfRequired(s, resp);
  });
}

void ObjectLockInfoManager::Impl::LockObject(
    const AcquireObjectLockRequestPB& req, rpc::RpcContext context, StdStatusCallback callback) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto lock_objects = std::make_shared<
      UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLocksGlobalResponsePB>>(
      master_, catalog_manager_, this, req, std::move(callback), std::move(context), std::nullopt,
      GetLeaseEpoch(req.session_host_uuid()));
  lock_objects->Launch();
}

void ObjectLockInfoManager::Impl::UnlockObject(
    ReleaseObjectLockRequestPB req, ReleaseObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext context) {
  UnlockObject(req, std::move(context), std::nullopt, [resp, clock = clock_](const Status& s) {
    resp->set_propagated_hybrid_time(clock->Now().ToUint64());
    FillErrorIfRequired(s, resp);
  });
}

void ObjectLockInfoManager::Impl::UnlockObject(
    const ReleaseObjectLockRequestPB& req, std::optional<rpc::RpcContext> context,
    std::optional<LeaderEpoch> leader_epoch, StdStatusCallback callback,
    bool remove_lease_epoch_entry) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto unlock_objects = std::make_shared<
      UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>>(
      master_, catalog_manager_, this, req, std::move(callback), std::move(context), leader_epoch,
      /* requestor_latest_lease_epoch */ std::nullopt, remove_lease_epoch_entry);
  unlock_objects->Launch();
}

void ObjectLockInfoManager::Impl::ReleaseLocksHeldByExpiredLeaseEpoch(
    const std::string& tserver_uuid, uint64 max_lease_epoch_to_release, bool wait,
    std::optional<LeaderEpoch> leader_epoch) {
  std::vector<std::shared_ptr<ReleaseObjectLockRequestPB>> requests_per_txn;
  VLOG_WITH_FUNC(2) << "Releasing locks for " << tserver_uuid << " up to lease epoch "
                    << max_lease_epoch_to_release;
  {
    const auto& key = tserver_uuid;
    std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
    auto l = object_lock_info->LockForRead();
    for (const auto& [lease_epoch, txns_map] : l->pb.lease_epochs()) {
      if (lease_epoch > max_lease_epoch_to_release) {
        continue;
      }
      for (const auto& [txn_id_str, _] : txns_map.transactions()) {
        auto request = std::make_shared<ReleaseObjectLockRequestPB>();
        request->set_session_host_uuid(tserver_uuid);
        auto txn_id = CHECK_RESULT(TransactionId::FromString(txn_id_str));
        request->set_txn_id(txn_id.data(), txn_id.size());
        request->set_lease_epoch(max_lease_epoch_to_release + 1);
        requests_per_txn.push_back(request);
      }
    }
  }
  // Do we want to wait for this? Or just let it go?
  VLOG_WITH_FUNC(2) << "Unlocking " << requests_per_txn.size() << " txns";
  std::shared_ptr<CountDownLatch> latch =
      std::make_shared<CountDownLatch>(requests_per_txn.size());
  for (const auto& request : requests_per_txn) {
    UnlockObject(
        *request, std::optional<rpc::RpcContext>(), leader_epoch,
        [latch, request](const Status& s) {
          WARN_NOT_OK(
              s, yb::Format(
                     "Failed to release old object locks $0 $1", request->session_host_uuid(),
                     request->txn_id()));
          latch->CountDown();
        },
        /* remove_lease_epoch_entry */ true);
  }
  if (wait) {
    latch->Wait();
  }
}

void ObjectLockInfoManager::Impl::BootstrapLocksPostLoad() {
  // We just bootstrap locks for tservers with a live lease at catalog load time.
  // todo(zdrudi): spawn tasks here to release all locks held by expired lease epochs.
  auto ts_descs = master_->ts_manager()->GetAllDescriptorsWithALiveLease();
  std::unordered_map<std::string, uint64_t> current_lease_epochs;
  for (const auto& ts_desc : ts_descs) {
    current_lease_epochs.insert({ts_desc->id(), ts_desc->LockForRead()->pb.lease_epoch()});
  }
  {
    LockGuard lock(mutex_);
    current_lease_epochs_.clear();
    current_lease_epochs_ = std::move(current_lease_epochs);
  }
  BootstrapLocalLocks();
}

void ObjectLockInfoManager::Impl::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  {
    LockGuard lock(mutex_);
    auto res = object_lock_infos_map_.insert_or_assign(tserver_uuid, info).second;
    DCHECK(res) << "UpdateObjectLocks called for an existing tserver_uuid " << tserver_uuid;
  }
}

void ObjectLockInfoManager::Impl::Clear() {
  catalog_manager_->AssertLeaderLockAcquiredForWriting();
  LockGuard lock(mutex_);
  object_lock_infos_map_.clear();
  local_lock_manager_.reset(new tserver::TSLocalLockManager(clock_));
}

void ObjectLockInfoManager::Impl::UpdateTabletServerLeaseEpoch(
    const std::string& tserver_uuid, uint64_t current_lease_epoch) {
  LockGuard lock(mutex_);
  current_lease_epochs_[tserver_uuid] = current_lease_epoch;
}

std::optional<uint64_t> ObjectLockInfoManager::Impl::GetLeaseEpoch(const std::string& ts_uuid) {
  LockGuard lock(mutex_);
  const auto it = current_lease_epochs_.find(ts_uuid);
  if (it == current_lease_epochs_.end()) {
    return std::nullopt;
  }
  return it->second;
}

template <class Req, class Resp>
UpdateAllTServers<Req, Resp>::UpdateAllTServers(
    Master* master, CatalogManager* catalog_manager, ObjectLockInfoManager::Impl* olm,
    const Req& req, StdStatusCallback callback, std::optional<rpc::RpcContext> context,
    std::optional<LeaderEpoch> epoch, std::optional<uint64_t> requestor_latest_lease_epoch,
    bool remove_lease_epoch_entry)
    : epoch_(epoch),
      master_(master),
      catalog_manager_(catalog_manager),
      object_lock_info_manager_(olm),
      req_(req),
      callback_(callback),
      context_(std::move(context)),
      requestor_latest_lease_epoch_(requestor_latest_lease_epoch),
      remove_lease_epoch_entry_(remove_lease_epoch_entry) {
  VLOG(3) << __PRETTY_FUNCTION__;
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Done(size_t i, const Status& s) {
  if (ts_descriptors_[i]->HasLiveClientOperationLease()) {
    statuses_[i] = s;
  } else {
    statuses_[i] = Status::OK();
  }
  // TODO: There is a potential here for early return if s is not OK.
  if (--ts_pending_ == 0) {
    CheckForDone();
  }
}

template <>
std::shared_ptr<RetrySpecificTSRpcTask>
UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLocksGlobalResponsePB>::TServerTaskFor(
    const TabletServerId& ts_uuid, StdStatusCallback callback) {
  return std::make_shared<
      master::UpdateTServer<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>>(
      master_, catalog_manager_->AsyncTaskPool(), ts_uuid, this->shared_from_this(), callback);
}

template <>
std::shared_ptr<RetrySpecificTSRpcTask>
UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>::TServerTaskFor(
    const TabletServerId& ts_uuid, StdStatusCallback callback) {
  return std::make_shared<
      master::UpdateTServer<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>>(
      master_, catalog_manager_->AsyncTaskPool(), ts_uuid, this->shared_from_this(), callback);
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Launch() {
  auto s = BeforeRpcs();
  if (!s.ok()) {
    DoCallbackAndRespond(s);
    return;
  }

  ts_descriptors_ = master_->ts_manager()->GetAllDescriptorsWithALiveLease();
  statuses_ = std::vector<Status>{ts_descriptors_.size(), STATUS(Uninitialized, "")};
  LaunchFrom(0);
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::LaunchFrom(size_t start_idx) {
  ts_pending_ = ts_descriptors_.size() - start_idx;
  LOG(INFO) << __func__ << " launching for " << ts_pending_ << " tservers.";
  for (size_t i = start_idx; i < ts_descriptors_.size(); ++i) {
    auto ts_uuid = ts_descriptors_[i]->permanent_uuid();
    LOG(INFO) << "Launching for " << ts_uuid;
    auto callback = std::bind(
        &UpdateAllTServers<Req, Resp>::Done, this->shared_from_this(), i, std::placeholders::_1);
    auto task = TServerTaskFor(ts_uuid, callback);
    WARN_NOT_OK(
        catalog_manager_->ScheduleTask(task),
        yb::Format(
            "Failed to schedule request to UpdateTServer to $0 for $1", ts_uuid,
            request().DebugString()));
  }
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::DoCallbackAndRespond(const Status& s) {
  VLOG_WITH_FUNC(2) << s;
  callback_(s);
  if (context_.has_value()) {
    context_->RespondSuccess();
  }
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::CheckForDone() {
  for (const auto& status : statuses_) {
    if (!status.ok()) {
      LOG(INFO) << "Error in acquiring object lock: " << status;
      DoCallbackAndRespond(status);
      return;
    }
  }
  if (RelaunchIfNecessary()) {
    return;
  }
  DoneAll();
}

template <>
Status
UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLocksGlobalResponsePB>::BeforeRpcs() {
  RETURN_NOT_OK(ValidateLockRequest(req_, requestor_latest_lease_epoch_));
  std::shared_ptr<tserver::TSLocalLockManager> local_lock_manager;
  DCHECK(!epoch_.has_value()) << "Epoch should not yet be set for AcquireObjectLockRequestPB";
  {
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
    RETURN_NOT_OK(CheckLeaderLockStatus(l, std::nullopt));
    epoch_ = l.epoch();
    local_lock_manager = object_lock_info_manager_->ts_local_lock_manager();
  }
  // Update Local State.
  // TODO: Use RETURN_NOT_OK_PREPEND
  auto s = local_lock_manager->AcquireObjectLocks(
      req_, GetClientDeadline(), tserver::WaitForBootstrap::kFalse);
  if (!s.ok()) {
    LOG(WARNING) << "Failed to acquire object locks locally at the master " << s;
    return s.CloneAndReplaceCode(Status::kRemoteError);
  }
  // todo(zdrudi): Do we want to verify the requestor has a valid lease here before persisting?
  // Persist the request.
  {
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
    RETURN_NOT_OK(CheckLeaderLockStatus(l, epoch_));
    auto s = object_lock_info_manager_->PersistRequest(*epoch_, req_);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to update object lock " << s;
      return s.CloneAndReplaceCode(Status::kRemoteError);
    }
  }
  return Status::OK();
}

template <>
Status
UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>::BeforeRpcs() {
  if (!epoch_.has_value()) {
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
    RETURN_NOT_OK(CheckLeaderLockStatus(l, std::nullopt));
    epoch_ = l.epoch();
  }
  return Status::OK();
}

template <>
Status
UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLocksGlobalResponsePB>::AfterRpcs() {
  return Status::OK();
}

template <>
Status
UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>::AfterRpcs() {
  VLOG_WITH_FUNC(2);
  SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
  RETURN_NOT_OK(CheckLeaderLockStatus(l, epoch_));
  // Persist the request.
  auto s = object_lock_info_manager_->PersistRequest(*epoch_, req_, remove_lease_epoch_entry_);
  if (!s.ok()) {
    LOG(WARNING) << "Failed to update object lock " << s;
    return s.CloneAndReplaceCode(Status::kRemoteError);
  }
  // Update Local State.
  auto local_lock_manager = object_lock_info_manager_->ts_local_lock_manager();
  s = local_lock_manager->ReleaseObjectLocks(req_, GetClientDeadline());
  if (!s.ok()) {
    LOG(WARNING) << "Failed to release object lock locally." << s;
    return s.CloneAndReplaceCode(Status::kRemoteError);
  }
  return Status::OK();
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::DoneAll() {
  DoCallbackAndRespond(AfterRpcs());
}

template <>
bool UpdateAllTServers<
    AcquireObjectLockRequestPB, AcquireObjectLocksGlobalResponsePB>::RelaunchIfNecessary() {
  return false;
}

template <>
bool UpdateAllTServers<
    ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>::RelaunchIfNecessary() {
  auto old_size = ts_descriptors_.size();
  auto current_ts_descriptors = master_->ts_manager()->GetAllDescriptorsWithALiveLease();
  for (const auto& ts_descriptor : current_ts_descriptors) {
    if (std::find(ts_descriptors_.begin(), ts_descriptors_.end(), ts_descriptor) ==
        ts_descriptors_.end()) {
      ts_descriptors_.push_back(ts_descriptor);
      statuses_.push_back(STATUS(Uninitialized, ""));
    }
  }
  if (ts_descriptors_.size() == old_size) {
    return false;
  }

  LOG(INFO) << "New TServers were added. Relaunching.";
  LaunchFrom(old_size);
  return true;
}

template <class Req, class Resp>
UpdateTServer<Req, Resp>::UpdateTServer(
    Master* master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
    std::shared_ptr<UpdateAll<Req>> shared_all_tservers, StdStatusCallback callback)
    : RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      callback_(std::move(callback)),
      shared_all_tservers_(shared_all_tservers) {
  deadline_.MakeAtMost(ToSteady(shared_all_tservers_->GetClientDeadline()));
}

template <class Req, class Resp>
std::string UpdateTServer<Req, Resp>::ToString() const {
  return Format("UpdateTServer for $0 ", yb::ToString(request()));
}

template <>
bool UpdateTServer<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>::SendRequest(
    int attempt) {
  VLOG(3) << ToString() << __func__ << " attempt " << attempt;
  ts_proxy_->AcquireObjectLocksAsync(request(), &resp_, &rpc_, BindRpcCallback());
  return true;
}

template <>
bool UpdateTServer<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>::SendRequest(
    int attempt) {
  VLOG(3) << ToString() << __func__ << " attempt " << attempt;
  ts_proxy_->ReleaseObjectLocksAsync(request(), &resp_, &rpc_, BindRpcCallback());
  return true;
}

template <>
std::string UpdateTServer<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>::description()
    const {
  return Format("Acquire object lock for $0 at $1", request().DebugString(), permanent_uuid_);
}

template <>
std::string UpdateTServer<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>::description()
    const {
  return Format("Release object lock for $0 at $1", request().DebugString(), permanent_uuid_);
}

template <class Req, class Resp>
void UpdateTServer<Req, Resp>::HandleResponse(int attempt) {
  VLOG(3) << ToString() << __func__ << " response is " << yb::ToString(resp_);
  Status status;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
    TransitionToFailedState(server::MonitoredTaskState::kRunning, status);
  } else {
    TransitionToCompleteState();
  }
}

template <class Req, class Resp>
void UpdateTServer<Req, Resp>::Finished(const Status& status) {
  VLOG(3) << ToString() << __func__ << " (" << status << ")";
  callback_(status);
}

}  // namespace master
}  // namespace yb
