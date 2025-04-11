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

#include "yb/common/pg_catversions.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_operation_lease.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/poller.h"
#include "yb/rpc/rpc_context.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

DEFINE_RUNTIME_uint64(master_ysql_operation_lease_ttl_ms, 10 * 1000,
                      "The lifetime of ysql operation lease extensions. The ysql operation lease "
                      "allows tservers to host pg sessions and serve reads and writes to user data "
                      "through the YSQL API.");
TAG_FLAG(master_ysql_operation_lease_ttl_ms, advanced);

DEFINE_NON_RUNTIME_uint64(object_lock_cleanup_interval_ms, 5000,
                          "The interval between runs of the background cleanup task for "
                          "table-level locks held by unresponsive TServers.");

DEFINE_RUNTIME_uint64(object_lock_cleanup_rpc_timeout_ms, 60000,
                      "The timeout for tasks to release locks held by unresponsive tservers.");

DECLARE_bool(enable_heartbeat_pg_catalog_versions_cache);
DECLARE_bool(TEST_enable_object_locking_for_table_locks);

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

}  // namespace

class ObjectLockInfoManager::Impl {
 public:
  Impl(Master* master, CatalogManager* catalog_manager)
      : master_(master),
        catalog_manager_(catalog_manager),
        clock_(master->clock()),
        local_lock_manager_(
            std::make_shared<tserver::TSLocalLockManager>(clock_, master_->tablet_server())),
        poller_(std::bind(&Impl::CleanupExpiredLeaseEpochs, this)) {}

  void Start() {
    poller_.Start(
        &catalog_manager_->Scheduler(),
        MonoDelta::FromMilliseconds(FLAGS_object_lock_cleanup_interval_ms));
  }

  void LockObject(
      AcquireObjectLockRequestPB req, AcquireObjectLocksGlobalResponsePB* resp,
      rpc::RpcContext rpc);
  void LockObject(
      const tserver::AcquireObjectLockRequestPB& req, rpc::RpcContext context,
      StdStatusCallback callback);

  void PopulateDbCatalogVersionCache(ReleaseObjectLockRequestPB& req);
  void UnlockObject(
      ReleaseObjectLockRequestPB&& req, ReleaseObjectLocksGlobalResponsePB* resp,
      rpc::RpcContext rpc);
  void UnlockObject(
      const tserver::ReleaseObjectLockRequestPB& req, std::optional<rpc::RpcContext> context,
      std::optional<LeaderEpoch> leader_epoch, StdStatusCallback callback,
      std::optional<CoarseTimePoint> deadline);
  void UnlockObject(const TransactionId& txn_id);

  Status RefreshYsqlLease(const RefreshYsqlLeaseRequestPB& req, RefreshYsqlLeaseResponsePB& resp,
                          rpc::RpcContext& rpc,
                          const LeaderEpoch& epoch);

  std::shared_ptr<CountDownLatch> ReleaseLocksHeldByExpiredLeaseEpoch(
      const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
      std::optional<LeaderEpoch> leader_epoch,
      std::optional<CoarseTimePoint> deadline = std::nullopt);

  std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB> GetLeaseInfos() const
      EXCLUDES(mutex_);

  void BootstrapLocksPostLoad();

  Status PersistRequest(LeaderEpoch epoch, const AcquireObjectLockRequestPB& req) EXCLUDES(mutex_);
  Status PersistRequest(
      LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req)
      EXCLUDES(mutex_);

  tserver::DdlLockEntriesPB ExportObjectLockInfo() EXCLUDES(mutex_);
  tserver::DdlLockEntriesPB ExportObjectLockInfoUnlocked() REQUIRES(mutex_);

  void UpdateObjectLocks(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info)
      EXCLUDES(mutex_);
  void Clear() EXCLUDES(mutex_);

  const server::ClockPtr& clock() const {
    return clock_;
  }

  std::shared_ptr<ObjectLockInfo> GetOrCreateObjectLockInfo(const std::string& ts_uuid)
      EXCLUDES(mutex_);
  bool TabletServerHasLiveLease(const std::string& ts_uuid) const EXCLUDES(mutex_);
  void UpdateTxnHostSessionMap(
      const TransactionId& txn_id, const std::string& host_session_uuid, uint64_t lease_epoch)
      EXCLUDES(mutex_);
  void RemoveTxnFromHostSessionMap(const TransactionId& txn_id) EXCLUDES(mutex_);

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
  tserver::TSLocalLockManagerPtr ts_local_lock_manager() EXCLUDES(mutex_) {
    catalog_manager_->AssertLeaderLockAcquiredForReading();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  TSDescriptorVector GetAllTSDescriptorsWithALiveLease() const;

 private:
  std::shared_ptr<tserver::TSLocalLockManager> ts_local_lock_manager_during_catalog_loading()
      EXCLUDES(mutex_) {
    catalog_manager_->AssertLeaderLockAcquiredForWriting();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  std::optional<uint64_t> GetLeaseEpoch(const std::string& ts_uuid) EXCLUDES(mutex_);

  // Called by the poller to mark leases as expired and clean up locks held by expired lease epochs.
  // This should only be called by the poller as it accesses cleanup task state without
  // synchronization.
  void CleanupExpiredLeaseEpochs() EXCLUDES(mutex_);

  Master* master_;
  CatalogManager* catalog_manager_;
  const server::ClockPtr clock_;

  using MutexType = std::mutex;
  using LockGuard = std::lock_guard<MutexType>;
  mutable MutexType mutex_;
  std::unordered_map<std::string, std::shared_ptr<ObjectLockInfo>> object_lock_infos_map_
      GUARDED_BY(mutex_);
  struct TxnHostInfo {
    std::string host_session_uuid;
    uint64_t lease_epoch;
  };

  std::unordered_map<TransactionId, TxnHostInfo> txn_host_info_map_ GUARDED_BY(mutex_);
  std::shared_ptr<tserver::TSLocalLockManager> local_lock_manager_ GUARDED_BY(mutex_);
  // Only accessed from a single thread for now, so no need for synchronization.
  std::unordered_map<TabletServerId, std::shared_ptr<CountDownLatch>>
      expired_lease_epoch_cleanup_tasks_;
  rpc::Poller poller_;
};

template <class Req>
class UpdateAll {
 public:
  UpdateAll() = default;
  virtual ~UpdateAll() = default;
  virtual const Req& request() const = 0;
  virtual CoarseTimePoint GetClientDeadline() const = 0;
  virtual bool TabletServerHasLiveLease(const std::string& uuid) = 0;
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
      std::optional<CoarseTimePoint> deadline);

  void Launch();
  const Req& request() const override {
    return req_;
  }

  CoarseTimePoint GetClientDeadline() const override {
    if (deadline_) {
      return *deadline_;
    } else if (context_) {
      return context_->GetClientDeadline();
    } else {
      return CoarseTimePoint::max();
    }
  }

  bool TabletServerHasLiveLease(const std::string& uuid) override {
    return object_lock_info_manager_->TabletServerHasLiveLease(uuid);
  }

  Trace *trace() const {
    return trace_.get();
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
  std::optional<CoarseTimePoint> deadline_;
  const TracePtr trace_;
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

  const Req& request() const { return shared_all_tservers_->request(); }

  bool RetryTaskAfterRPCFailure(const Status& status) override;

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

void ObjectLockInfoManager::Start() {
  impl_->Start();
}

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

void ObjectLockInfoManager::ReleaseLocksForTxn(const TransactionId& txn_id) {
  impl_->UnlockObject(txn_id);
}

Status ObjectLockInfoManager::RefreshYsqlLease(
    const RefreshYsqlLeaseRequestPB& req, RefreshYsqlLeaseResponsePB& resp, rpc::RpcContext& rpc,
    const LeaderEpoch& epoch) {
  return impl_->RefreshYsqlLease(req, resp, rpc, epoch);
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::ExportObjectLockInfo() {
  return impl_->ExportObjectLockInfo();
}

std::shared_ptr<CountDownLatch> ObjectLockInfoManager::ReleaseLocksHeldByExpiredLeaseEpoch(
    const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
    std::optional<LeaderEpoch> leader_epoch) {
  return impl_->ReleaseLocksHeldByExpiredLeaseEpoch(
      tserver_uuid, max_lease_epoch_to_release, leader_epoch);
}

std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB>
ObjectLockInfoManager::GetLeaseInfos() const {
  return impl_->GetLeaseInfos();
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
    const std::string& ts_uuid) {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(ts_uuid);
  if (it != object_lock_infos_map_.end()) {
    return it->second;
  } else {
    std::shared_ptr<ObjectLockInfo> info = std::make_shared<ObjectLockInfo>(ts_uuid);
    object_lock_infos_map_.insert({ts_uuid, info});
    return info;
  }
}

bool ObjectLockInfoManager::Impl::TabletServerHasLiveLease(const std::string& ts_uuid) const {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(ts_uuid);
  if (it == object_lock_infos_map_.end()) {
    return false;
  }
  return it->second->LockForRead()->pb.lease_info().live_lease();
}

void ObjectLockInfoManager::Impl::UpdateTxnHostSessionMap(
    const TransactionId& txn_id, const std::string& host_session_uuid, uint64_t lease_epoch) {
  LockGuard lock(mutex_);
  txn_host_info_map_[txn_id] = TxnHostInfo{host_session_uuid, lease_epoch};
}

void ObjectLockInfoManager::Impl::RemoveTxnFromHostSessionMap(const TransactionId& txn_id) {
  LockGuard lock(mutex_);
  txn_host_info_map_.erase(txn_id);
}

TSDescriptorVector ObjectLockInfoManager::Impl::GetAllTSDescriptorsWithALiveLease() const {
  auto descriptors = master_->ts_manager()->GetAllDescriptors();
  LockGuard lock(mutex_);
  std::erase_if(descriptors, [this](const auto& desc) NO_THREAD_SAFETY_ANALYSIS {
    auto it = object_lock_infos_map_.find(desc->id());
    if (it == object_lock_infos_map_.end()) {
      return true;
    }
    return !it->second->LockForRead()->pb.lease_info().live_lease();
  });
  return descriptors;
}

Status ObjectLockInfoManager::Impl::PersistRequest(
    LeaderEpoch epoch, const AcquireObjectLockRequestPB& req) {
  TRACE_FUNC();
  VLOG(3) << __PRETTY_FUNCTION__ << req.ShortDebugString();
  const auto& session_host_uuid = req.session_host_uuid();
  const auto lease_epoch = req.lease_epoch();
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
  UpdateTxnHostSessionMap(txn_id, session_host_uuid, lease_epoch);
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(session_host_uuid);
  auto lock = object_lock_info->LockForWrite();
  auto& txns_map = (*lock.mutable_data()->pb.mutable_lease_epochs())[lease_epoch];
  auto& subtxns_map = (*txns_map.mutable_transactions())[txn_id.ToString()];
  auto& object_map = (*subtxns_map.mutable_subtxns())[req.subtxn_id()];
  for (const auto& object_lock : req.object_locks()) {
    RSTATUS_DCHECK(
        !subtxns_map.has_db_id() || subtxns_map.db_id() == object_lock.database_oid(),
        IllegalState, "Multiple db ids found for a txn: $0 vs $1",
        subtxns_map.db_id(), object_lock.database_oid());
    subtxns_map.set_db_id(object_lock.database_oid());
    auto& types = (*object_map.mutable_objects())[object_lock.object_oid()];
    types.add_lock_type(object_lock.lock_type());
  }
  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

Status ObjectLockInfoManager::Impl::PersistRequest(
    LeaderEpoch epoch, const ReleaseObjectLockRequestPB& req) {
  TRACE_FUNC();
  VLOG(3) << __PRETTY_FUNCTION__ << req.ShortDebugString();
  const auto& session_host_uuid = req.session_host_uuid();
  const auto lease_epoch = req.lease_epoch();
  const bool erase_txn = !req.subtxn_id();
  auto txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req.txn_id()));
  if (erase_txn) {
    RemoveTxnFromHostSessionMap(txn_id);
  }
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(session_host_uuid);
  auto lock = object_lock_info->LockForWrite();
  auto& txns_map = (*lock.mutable_data()->pb.mutable_lease_epochs())[lease_epoch];
  if (erase_txn) {
    txns_map.mutable_transactions()->erase(txn_id.ToString());
  } else {
    auto& subtxns_map = (*txns_map.mutable_transactions())[txn_id.ToString()];
    subtxns_map.mutable_subtxns()->erase(req.subtxn_id());
  }
  if (txns_map.transactions().empty()) {
    lock.mutable_data()->pb.mutable_lease_epochs()->erase(lease_epoch);
  }

  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

namespace {

void ExportObjectLocksForTxn(
    uint64_t db_id,
    const master::SysObjectLockEntryPB_ObjectLocksMapPB& objects_map,
    tserver::AcquireObjectLockRequestPB* req) {
  for (const auto& [object_id, lock_types] : objects_map.objects()) {
    for (const auto& type : lock_types.lock_type()) {
      auto* lock = req->add_object_locks();
      lock->set_database_oid(db_id);
      lock->set_object_oid(object_id);
      lock->set_lock_type(TableLockType(type));
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

tserver::DdlLockEntriesPB ObjectLockInfoManager::Impl::ExportObjectLockInfo() {
  LockGuard lock(mutex_);
  return ExportObjectLockInfoUnlocked();
}

tserver::DdlLockEntriesPB ObjectLockInfoManager::Impl::ExportObjectLockInfoUnlocked() {
  VLOG(2) << __PRETTY_FUNCTION__;
  tserver::DdlLockEntriesPB entries;
  for (const auto& [host_uuid, per_host_entry] : object_lock_infos_map_) {
    auto l = per_host_entry->LockForRead();
    if (!l->pb.lease_info().has_lease_epoch() || !l->pb.lease_info().live_lease()) {
      continue;
    }
    auto txns_map_it = l->pb.lease_epochs().find(l->pb.lease_info().lease_epoch());
    if (txns_map_it == l->pb.lease_epochs().end()) {
      continue;
    }
    for (const auto& [txn_id_str, subtxns_map] : txns_map_it->second.transactions()) {
      auto txn_id = CHECK_RESULT(TransactionId::FromString(txn_id_str));
      const auto db_id = subtxns_map.db_id();
      for (const auto& [subtxn_id, objects_map] : subtxns_map.subtxns()) {
        auto* lock_entries_pb = entries.add_lock_entries();
        lock_entries_pb->set_session_host_uuid(host_uuid);
        lock_entries_pb->set_txn_id(txn_id.data(), txn_id.size());
        lock_entries_pb->set_subtxn_id(subtxn_id);
        ExportObjectLocksForTxn(db_id, objects_map, lock_entries_pb);
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
  VLOG(1) << __PRETTY_FUNCTION__ << req.ShortDebugString();
  auto lock_objects = std::make_shared<
      UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLocksGlobalResponsePB>>(
      master_, catalog_manager_, this, req, std::move(callback), std::move(context), std::nullopt,
      GetLeaseEpoch(req.session_host_uuid()), std::nullopt);
  lock_objects->Launch();
}

void ObjectLockInfoManager::Impl::PopulateDbCatalogVersionCache(ReleaseObjectLockRequestPB& req) {
  // TODO: Currently, we fetch and send catalog version of all dbs because the cache invalidation
  // logic on the tserver side expects a full report. Fix it and then optimize the below to only
  // send the catalog version of the db being operated on by the txn.
  DbOidToCatalogVersionMap versions;
  uint64_t fingerprint;
  auto s = catalog_manager_->GetYsqlAllDBCatalogVersions(
      FLAGS_enable_heartbeat_pg_catalog_versions_cache, &versions, &fingerprint);
  if (!s.ok()) {
    // In this case, we fallback to delayed cache invalidation on tserver-master heartbeat path.
    LOG(WARNING) << "Couldn't populate catalog version on exclusive lock release: " << s;
    return;
  }
  if (versions.empty()) {
    return;
  }
  auto* db_catalog_version_data = req.mutable_db_catalog_version_data();
  for (const auto& it : versions) {
    auto* const catalog_version_pb = db_catalog_version_data->add_db_catalog_versions();
    catalog_version_pb->set_db_oid(it.first);
    catalog_version_pb->set_current_version(it.second.current_version);
    catalog_version_pb->set_last_breaking_version(it.second.last_breaking_version);
  }
}

void ObjectLockInfoManager::Impl::UnlockObject(
    ReleaseObjectLockRequestPB&& req, ReleaseObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext context) {
  PopulateDbCatalogVersionCache(req);
  UnlockObject(req, std::move(context), std::nullopt, [resp, clock = clock_](const Status& s) {
    resp->set_propagated_hybrid_time(clock->Now().ToUint64());
    FillErrorIfRequired(s, resp);
  }, std::nullopt);
}

void ObjectLockInfoManager::Impl::UnlockObject(
    const ReleaseObjectLockRequestPB& req, std::optional<rpc::RpcContext> context,
    std::optional<LeaderEpoch> leader_epoch, StdStatusCallback callback,
    std::optional<CoarseTimePoint> deadline) {
  VLOG(1) << __PRETTY_FUNCTION__ << req.ShortDebugString()
          << " leader epoch: " << (leader_epoch ? leader_epoch->ToString() : "none");
  auto unlock_objects = std::make_shared<
      UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>>(
      master_, catalog_manager_, this, req, std::move(callback), std::move(context), leader_epoch,
      /* requestor_latest_lease_epoch */ std::nullopt, deadline);
  unlock_objects->Launch();
}

void ObjectLockInfoManager::Impl::UnlockObject(const TransactionId& txn_id) {
  ReleaseObjectLockRequestPB req;
  req.set_txn_id(txn_id.data(), txn_id.size());
  {
    LockGuard lock(mutex_);
    auto it = txn_host_info_map_.find(txn_id);
    if (it == txn_host_info_map_.end()) {
      return;
    }
    req.set_session_host_uuid(it->second.host_session_uuid);
    req.set_lease_epoch(it->second.lease_epoch);
  }

  PopulateDbCatalogVersionCache(req);
  return UnlockObject(
      std::move(req), /* context */ std::nullopt,
      /* leader epoch */ std::nullopt,
      [txn_id](Status s) {
        LOG_IF(WARNING, !s.ok()) << "Releasing exclusive object locks failed for txn " << txn_id;
      },
      /* deadline */ std::nullopt);
}

Status ObjectLockInfoManager::Impl::RefreshYsqlLease(
    const RefreshYsqlLeaseRequestPB& req, RefreshYsqlLeaseResponsePB& resp, rpc::RpcContext& rpc,
    const LeaderEpoch& epoch) {
  if (!FLAGS_TEST_enable_ysql_operation_lease &&
      !FLAGS_TEST_enable_object_locking_for_table_locks) {
    return STATUS(NotSupported, "The ysql lease is currently a test feature.");
  }
  // Sanity check that the tserver has already registered with the same instance_seqno.
  RETURN_NOT_OK(master_->ts_manager()->LookupTS(req.instance()));
  auto object_lock_info = GetOrCreateObjectLockInfo(req.instance().permanent_uuid());
  auto lock_opt = object_lock_info->RefreshYsqlOperationLease(req.instance());
  if (!lock_opt) {
    resp.mutable_info()->set_new_lease(false);
    if (req.needs_bootstrap()) {
      *resp.mutable_info()->mutable_ddl_lock_entries() = ExportObjectLockInfo();
    }
    return Status::OK();
  }
  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch, object_lock_info));
  resp.mutable_info()->set_new_lease(true);
  resp.mutable_info()->set_lease_epoch(lock_opt->mutable_data()->pb.lease_info().lease_epoch());
  lock_opt->Commit();
  *resp.mutable_info()->mutable_ddl_lock_entries() = ExportObjectLockInfo();
  return Status::OK();
}

std::shared_ptr<CountDownLatch> ObjectLockInfoManager::Impl::ReleaseLocksHeldByExpiredLeaseEpoch(
    const std::string& tserver_uuid, uint64 max_lease_epoch_to_release,
    std::optional<LeaderEpoch> leader_epoch, std::optional<CoarseTimePoint> deadline) {
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
        if (requests_per_txn.empty()) {
          // Set the db catalog cache on just one of the unlock requests, since it would be the same
          // unless a new DDL modified it, in which case it's release would set the latest cache.
          PopulateDbCatalogVersionCache(*request.get());
        }
        requests_per_txn.push_back(request);
      }
    }
  }
  VLOG_WITH_FUNC(2) << "Unlocking " << requests_per_txn.size() << " txns";
  auto latch =
    std::make_shared<CountDownLatch>(requests_per_txn.size());
  for (const auto& request : requests_per_txn) {
    UnlockObject(
        *request, std::nullopt, leader_epoch,
        [latch, request](const Status& s) {
          WARN_NOT_OK(
              s, yb::Format(
                     "Failed to release old object locks $0 $1", request->session_host_uuid(),
                     request->txn_id()));
          latch->CountDown();
        },
        deadline);
  }
  return latch;
}

std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB>
ObjectLockInfoManager::Impl::GetLeaseInfos() const {
  LockGuard lock(mutex_);
  std::unordered_map<std::string, SysObjectLockEntryPB::LeaseInfoPB> result;
  for (const auto& [uuid, object_info] : object_lock_infos_map_) {
    result[uuid] = object_info->LockForRead()->pb.lease_info();
  }
  return result;
}

void ObjectLockInfoManager::Impl::BootstrapLocksPostLoad() {
  CHECK_OK(ts_local_lock_manager_during_catalog_loading()->BootstrapDdlObjectLocks(
      ExportObjectLockInfo()));
}

void ObjectLockInfoManager::Impl::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  // While holding locks concurrently, the established lock order is mutex_ -> LockFor(Read/Write).
  LockGuard lock(mutex_);
  auto [it, inserted] = object_lock_infos_map_.insert_or_assign(tserver_uuid, info);
  DCHECK(inserted) << "UpdateObjectLocks called for an existing tserver_uuid " << tserver_uuid;
  auto object_lock_info = it->second->LockForRead();
  for (const auto& [lease_epoch, txns_map] : object_lock_info.data().pb.lease_epochs()) {
    for (const auto& [txn_id_str, _] : txns_map.transactions()) {
      auto txn_id_res = TransactionId::FromString(txn_id_str);
      if (txn_id_res.ok()) {
        txn_host_info_map_[*txn_id_res] = TxnHostInfo{tserver_uuid, lease_epoch};
      } else {
        LOG(DFATAL) << "Unable to decode transaction id from "
                    << txn_id_str << ": " << txn_id_res.status();
      }
    }
  }
}

void ObjectLockInfoManager::Impl::Clear() {
  catalog_manager_->AssertLeaderLockAcquiredForWriting();
  LockGuard lock(mutex_);
  object_lock_infos_map_.clear();
  local_lock_manager_.reset(new tserver::TSLocalLockManager(clock_, master_->tablet_server()));
}

std::optional<uint64_t> ObjectLockInfoManager::Impl::GetLeaseEpoch(const std::string& ts_uuid) {
  LockGuard lock(mutex_);
  auto it = object_lock_infos_map_.find(ts_uuid);
  if (it == object_lock_infos_map_.end()) {
    return std::nullopt;
  }
  return it->second->LockForRead()->pb.lease_info().lease_epoch();
}

void ObjectLockInfoManager::Impl::CleanupExpiredLeaseEpochs() {
  auto current_time = MonoTime::Now();
  LeaderEpoch leader_epoch;
  {
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
    if (!l.IsInitializedAndIsLeader()) {
      return;
    }
    leader_epoch = l.epoch();
  }
  std::vector<std::shared_ptr<ObjectLockInfo>> expiring_leases;
  std::vector<std::shared_ptr<ObjectLockInfo>>
      infos_with_expired_lease_epochs;
  {
    LockGuard lock(mutex_);
    auto lease_ttl =
        MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_master_ysql_operation_lease_ttl_ms));
    for (const auto& [_, object_info] : object_lock_infos_map_) {
      auto object_info_lock = object_info->LockForRead();
      if (object_info_lock->pb.lease_info().live_lease() &&
          current_time.GetDeltaSince(object_info->last_ysql_lease_refresh()) > lease_ttl) {
        expiring_leases.push_back(object_info);
      } else {
        for (const auto& [lease_epoch, _] : object_info_lock->pb.lease_epochs()) {
          if (lease_epoch != object_info_lock->pb.lease_info().lease_epoch() ||
              !object_info_lock->pb.lease_info().live_lease()) {
            infos_with_expired_lease_epochs.push_back(object_info);
            break;
          }
        }
      }
    }
  }
  if (expiring_leases.empty() && infos_with_expired_lease_epochs.empty()) {
    return;
  }
  // Loop again through the expiring leases to confirm mutations by acquiring the write locks.
  std::vector<ObjectLockInfo*> object_infos_to_write;
  std::vector<ObjectLockInfo::WriteLock> write_locks;
  for (const auto& object_info : expiring_leases) {
    auto object_info_lock = object_info->LockForWrite();
    if (object_info_lock->pb.lease_info().live_lease() &&
        current_time.GetDeltaSince(object_info->last_ysql_lease_refresh()) >
            MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_master_ysql_operation_lease_ttl_ms))) {
      object_info_lock.mutable_data()->pb.mutable_lease_info()->set_live_lease(false);
      object_infos_to_write.push_back(object_info.get());
      if (object_info_lock->pb.lease_epochs_size() > 0) {
        infos_with_expired_lease_epochs.push_back(object_info);
      }
      write_locks.push_back(std::move(object_info_lock));
    }
  }
  auto write_status = catalog_manager_->sys_catalog()->Upsert(leader_epoch, object_infos_to_write);
  if (!write_status.ok()) {
    LOG(WARNING) << Format(
        "Object lock cleanup task failed to mark leases for tservers $0 as expired, got error: $1",
        yb::ToString(std::ranges::views::transform(
            object_infos_to_write, [](const auto& object_lock) { return object_lock->id(); })),
        write_status);
    return;
  }
  for (auto& lock : write_locks) {
    lock.Commit();
  }
  auto deadline = ToCoarse(
      MonoTime::Now() +
      MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_object_lock_cleanup_rpc_timeout_ms)));
  for (const auto& object_info : infos_with_expired_lease_epochs) {
    auto it = expired_lease_epoch_cleanup_tasks_.find(object_info->id());
    if (it != expired_lease_epoch_cleanup_tasks_.end() && it->second->count() > 0) {
      // Already a task in progress to clean this up.
      continue;
    }
    std::optional<uint64_t> max_lease_epoch_to_clean;
    auto object_info_lock = object_info->LockForRead();
    uint64_t min_lease_epoch_to_keep = object_info_lock->pb.lease_info().live_lease()
                                           ? object_info_lock->pb.lease_info().lease_epoch()
                                           : std::numeric_limits<uint64_t>::max();
    for (const auto& [lease_epoch, _] : object_info_lock->pb.lease_epochs()) {
      if (lease_epoch >= min_lease_epoch_to_keep) {
        continue;
      }
      max_lease_epoch_to_clean =
          max_lease_epoch_to_clean ? std::max(*max_lease_epoch_to_clean, lease_epoch) : lease_epoch;
    }
    if (!max_lease_epoch_to_clean) {
      continue;
    }
    expired_lease_epoch_cleanup_tasks_[object_info->id()] = ReleaseLocksHeldByExpiredLeaseEpoch(
        object_info->id(), *max_lease_epoch_to_clean, leader_epoch, deadline);
  }
  // todo(zdrudi): GC tasks which have finished. maybe do that first in this function.
}

template <class Req, class Resp>
UpdateAllTServers<Req, Resp>::UpdateAllTServers(
    Master* master, CatalogManager* catalog_manager, ObjectLockInfoManager::Impl* olm,
    const Req& req, StdStatusCallback callback, std::optional<rpc::RpcContext> context,
    std::optional<LeaderEpoch> epoch, std::optional<uint64_t> requestor_latest_lease_epoch,
    std::optional<CoarseTimePoint> deadline)
    : epoch_(epoch),
      master_(master),
      catalog_manager_(catalog_manager),
      object_lock_info_manager_(olm),
      req_(req),
      callback_(callback),
      context_(std::move(context)),
      requestor_latest_lease_epoch_(requestor_latest_lease_epoch),
      trace_(Trace::CurrentTrace()) {
  VLOG(3) << __PRETTY_FUNCTION__;
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Done(size_t i, const Status& s) {
  if (s.ok() || object_lock_info_manager_->TabletServerHasLiveLease(ts_descriptors_[i]->id())) {
    statuses_[i] = s;
  } else {
    // If the tablet server does not have a live lease then ignore it.
    statuses_[i] = Status::OK();
  }
  TRACE_TO(
      trace(), "Done $0 ($1) : $2", i, ts_descriptors_[i]->permanent_uuid(),
      statuses_[i].ToString());
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

  // todo(zdrudi): special case for 0 tservers with a live lease. This doesn't work.
  ts_descriptors_ = object_lock_info_manager_->GetAllTSDescriptorsWithALiveLease();
  statuses_ = std::vector<Status>{ts_descriptors_.size(), STATUS(Uninitialized, "")};
  LaunchFrom(0);
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::LaunchFrom(size_t start_idx) {
  TRACE("Launching for $0 TServers from $1", ts_descriptors_.size(), start_idx);
  ts_pending_ = ts_descriptors_.size() - start_idx;
  VLOG(1) << __func__ << " launching for " << ts_pending_ << " tservers.";
  for (size_t i = start_idx; i < ts_descriptors_.size(); ++i) {
    auto ts_uuid = ts_descriptors_[i]->permanent_uuid();
    VLOG(1) << "Launching for " << ts_uuid;
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
  TRACE("$0: $1", __func__, s.ToString());
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
      LOG(WARNING) << "Error in acquiring object lock: " << status;
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
  TRACE_FUNC();
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
  TRACE_FUNC();
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
  TRACE_FUNC();
  return Status::OK();
}

template <>
Status
UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLocksGlobalResponsePB>::AfterRpcs() {
  TRACE_FUNC();
  VLOG_WITH_FUNC(2);
  SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
  RETURN_NOT_OK(CheckLeaderLockStatus(l, epoch_));
  // Persist the request.
  auto s = object_lock_info_manager_->PersistRequest(*epoch_, req_);
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
  ADOPT_TRACE(trace());
  TRACE_FUNC();
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
  TRACE_TO(trace(), "Relaunching");
  auto old_size = ts_descriptors_.size();
  auto current_ts_descriptors = object_lock_info_manager_->GetAllTSDescriptorsWithALiveLease();
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

  VLOG(1) << "New TServers were added. Relaunching.";
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

template <class Req, class Resp>
bool UpdateTServer<Req, Resp>::RetryTaskAfterRPCFailure(const Status& status) {
  if (!shared_all_tservers_->TabletServerHasLiveLease(permanent_uuid_)) {
    LOG(WARNING) << "TS " << permanent_uuid_
                 << " no longer has a live lease. Ignoring this tserver for object lock task "
                 << description() << ", rpc status: " << rpc_.status();
    return false;
  }
  return true;
}

}  // namespace master
}  // namespace yb
