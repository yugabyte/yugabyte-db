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

class ObjectLockInfoManager::Impl {
 public:
  Impl(Master* master, CatalogManager* catalog_manager)
      : master_(master),
        catalog_manager_(catalog_manager),
        local_lock_manager_(std::make_shared<tablet::TSLocalLockManager>()) {}

  void LockObject(
      const tserver::AcquireObjectLockRequestPB* req, tserver::AcquireObjectLockResponsePB* resp,
      rpc::RpcContext rpc);

  void UnlockObject(
      const tserver::ReleaseObjectLockRequestPB* req, tserver::ReleaseObjectLockResponsePB* resp,
      rpc::RpcContext rpc);

  Status PersistRequest(LeaderEpoch epoch, const tserver::AcquireObjectLockRequestPB& req)
      EXCLUDES(mutex_);
  Status PersistRequest(LeaderEpoch epoch, const tserver::ReleaseObjectLockRequestPB& req)
      EXCLUDES(mutex_);

  void ExportObjectLockInfo(const std::string& tserver_uuid, tserver::DdlLockEntriesPB* resp)
      EXCLUDES(mutex_);
  void BootstrapLocalLocksFor(const std::string& tserver_uuid) EXCLUDES(mutex_);

  void UpdateObjectLocks(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info)
      EXCLUDES(mutex_);
  void Clear() EXCLUDES(mutex_);

  std::shared_ptr<ObjectLockInfo> GetOrCreateObjectLockInfo(const std::string& key)
      EXCLUDES(mutex_);

  std::shared_ptr<tablet::TSLocalLockManager> TEST_ts_local_lock_manager() EXCLUDES(mutex_) {
    // No need to acquire the leader lock for testing.
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

 private:
  /*
  The local lock manager is used to acquire and release locks on the master itself.

  We will need to recreate the state of the ts_local_lock_manager on the master
  when the master assumes leadership. This will be done by clearing the TSLocalManager and
  replaying the DDL lock requests
  */
  std::shared_ptr<tablet::TSLocalLockManager> ts_local_lock_manager() EXCLUDES(mutex_) {
    catalog_manager_->AssertLeaderLockAcquiredForReading();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  std::shared_ptr<tablet::TSLocalLockManager> ts_local_lock_manager_during_catalog_loading()
      EXCLUDES(mutex_) {
    catalog_manager_->AssertLeaderLockAcquiredForWriting();
    LockGuard lock(mutex_);
    return local_lock_manager_;
  }

  Master* master_;
  CatalogManager* catalog_manager_;
  std::atomic<size_t> next_request_id_{0};

  using MutexType = std::mutex;
  using LockGuard = std::lock_guard<MutexType>;
  mutable MutexType mutex_;
  std::unordered_map<std::string, std::shared_ptr<ObjectLockInfo>> object_lock_infos_map_
      GUARDED_BY(mutex_);

  std::shared_ptr<tablet::TSLocalLockManager> local_lock_manager_ GUARDED_BY(mutex_);
};

template <class Req, class Resp>
class UpdateAllTServers : public std::enable_shared_from_this<UpdateAllTServers<Req, Resp>> {
 public:
  UpdateAllTServers(
      LeaderEpoch epoch, Master* master, CatalogManager* catalog_manager,
      ObjectLockInfoManager::Impl* object_lock_info_manager, const Req& req, Resp* resp,
      rpc::RpcContext rpc);

  void Launch();
  const Req& request() const {
    return req_;
  }

 private:
  void LaunchFrom(size_t from_idx);
  void Done(size_t i, const Status& s);
  void DoneAll();

  LeaderEpoch epoch_;
  Master* master_;
  CatalogManager* catalog_manager_;
  ObjectLockInfoManager::Impl* object_lock_info_manager_;
  TSDescriptorVector ts_descriptors_;
  std::atomic<size_t> ts_pending_;
  std::vector<Status> statuses_;
  const Req req_;
  Resp* resp_;
  rpc::RpcContext context_;
};

template <class Req, class Resp>
class UpdateTServer : public RetrySpecificTSRpcTask {
 public:
  UpdateTServer(
      Master* master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
      std::shared_ptr<UpdateAllTServers<Req, Resp>> shared_all_tservers,
      StdStatusCallback callback);

  server::MonitoredTaskType type() const override { return server::MonitoredTaskType::kObjectLock; }

  std::string type_name() const override { return "Object Lock"; }

  std::string description() const override;

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

  std::shared_ptr<UpdateAllTServers<Req, Resp>> shared_all_tservers_;
};

namespace {

// TODO: Fetch and use the appropriate incarnation Id.
// Incarnation id will be used to identify whether the locks were requested
// by the currently registered tserver. Or is an old lock that should be released
// when the TServer loses its lease.
constexpr int kIncarnationId = 0;

}  // namespace

ObjectLockInfoManager::ObjectLockInfoManager(Master* master, CatalogManager* catalog_manager)
    : impl_(std::make_unique<ObjectLockInfoManager::Impl>(master, catalog_manager)) {}

ObjectLockInfoManager::~ObjectLockInfoManager() = default;

void ObjectLockInfoManager::LockObject(
    const tserver::AcquireObjectLockRequestPB* req, tserver::AcquireObjectLockResponsePB* resp,
    rpc::RpcContext rpc) {
  impl_->LockObject(req, resp, std::move(rpc));
}
void ObjectLockInfoManager::UnlockObject(
    const tserver::ReleaseObjectLockRequestPB* req, tserver::ReleaseObjectLockResponsePB* resp,
    rpc::RpcContext rpc) {
  impl_->UnlockObject(req, resp, std::move(rpc));
}
void ObjectLockInfoManager::ExportObjectLockInfo(
    const std::string& tserver_uuid, tserver::DdlLockEntriesPB* resp) {
  impl_->ExportObjectLockInfo(tserver_uuid, resp);
}
void ObjectLockInfoManager::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  impl_->UpdateObjectLocks(tserver_uuid, info);
}
void ObjectLockInfoManager::Clear() { impl_->Clear(); }

std::shared_ptr<tablet::TSLocalLockManager> ObjectLockInfoManager::TEST_ts_local_lock_manager() {
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
    LeaderEpoch epoch, const tserver::AcquireObjectLockRequestPB& req) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto key = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
  auto lock = object_lock_info->LockForWrite();
  // TODO(Amit) Fetch and use the appropriate incarnation Id.
  auto& sessions_map = (*lock.mutable_data()->pb.mutable_incarnations())[kIncarnationId];
  auto& db_map = (*sessions_map.mutable_sessions())[req.session_id()];
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
    LeaderEpoch epoch, const tserver::ReleaseObjectLockRequestPB& req) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto key = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info = GetOrCreateObjectLockInfo(key);
  auto lock = object_lock_info->LockForWrite();
  // TODO(Amit) Fetch and use the appropriate incarnation Id.
  auto& sessions_map = (*lock.mutable_data()->pb.mutable_incarnations())[kIncarnationId];
  if (req.release_all_locks()) {
    sessions_map.mutable_sessions()->erase(req.session_id());
  } else {
    auto& db_map = (*sessions_map.mutable_sessions())[req.session_id()];
    for (const auto& object_lock : req.object_locks()) {
      auto& object_map = (*db_map.mutable_dbs())[object_lock.database_oid()];
      object_map.mutable_objects()->erase(object_lock.object_oid());
    }
  }

  RETURN_NOT_OK(catalog_manager_->sys_catalog()->Upsert(epoch, object_lock_info));
  lock.Commit();
  return Status::OK();
}

namespace {

void ExportObjectLocksForSession(
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
void FillErrorAndRespond(
    tserver::TabletServerErrorPB_Code code, const Status& status, Resp* resp,
    rpc::RpcContext* rpc) {
  resp->mutable_error()->set_code(code);
  StatusToPB(status, resp->mutable_error()->mutable_status());
  rpc->RespondSuccess();
}

}  // namespace

void ObjectLockInfoManager::Impl::BootstrapLocalLocksFor(const std::string& tserver_uuid) {
  tserver::DdlLockEntriesPB entries;
  ExportObjectLockInfo(tserver_uuid, &entries);
  CHECK_OK(ts_local_lock_manager_during_catalog_loading()->BootstrapDdlObjectLocks(entries));
}

void ObjectLockInfoManager::Impl::ExportObjectLockInfo(
    const std::string& tserver_uuid, tserver::DdlLockEntriesPB* resp) {
  VLOG(2) << __PRETTY_FUNCTION__;
  {
    LockGuard lock(mutex_);
    for (const auto& [host_uuid, per_host_entry] : object_lock_infos_map_) {
      auto l = per_host_entry->LockForRead();
      // TODO(Amit) Fetch and use the appropriate incarnation Id.
      auto sessions_map_it = l->pb.incarnations().find(kIncarnationId);
      if (sessions_map_it == l->pb.incarnations().end()) {
        continue;
      }
      for (const auto& [session_id, dbs_map] : sessions_map_it->second.sessions()) {
        auto* lock_entries_pb = resp->add_lock_entries();
        lock_entries_pb->set_session_host_uuid(host_uuid);
        lock_entries_pb->set_session_id(session_id);
        ExportObjectLocksForSession(dbs_map, lock_entries_pb);
      }
    }
  }
  VLOG(3) << "Exported " << yb::ToString(*resp);
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
    const AcquireObjectLockRequestPB* req, AcquireObjectLockResponsePB* resp,
    rpc::RpcContext context) {
  VLOG(3) << __PRETTY_FUNCTION__;
  // First acquire the locks locally.
  std::shared_ptr<tablet::TSLocalLockManager> local_lock_manager;
  LeaderEpoch epoch;
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
    if (!l.IsInitializedAndIsLeader()) {
      FillErrorAndRespond(
          TabletServerErrorPB::NOT_THE_LEADER, STATUS(TryAgain, "Master is not leader"), resp,
          &context);
      return;
    }
    epoch = l.epoch();
    local_lock_manager = ts_local_lock_manager();
  }
  // We could be waiting for a long time here, if another PGClient has already acquired the lock.
  // So let us not hold the leader lock while waiting here.
  auto s = local_lock_manager->AcquireObjectLocks(
      *req, context.GetClientDeadline(), tablet::WaitForBootstrap::kFalse);
  if (!s.ok()) {
    LOG(WARNING) << "Failed to acquire object lock locally." << s;
    FillErrorAndRespond(TabletServerErrorPB::UNKNOWN_ERROR, s, resp, &context);
    return;
  }
  // Persist the request.
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
    if (!l.IsInitializedAndIsLeader()) {
      FillErrorAndRespond(
          TabletServerErrorPB::NOT_THE_LEADER, STATUS(TryAgain, "Master is not leader"), resp,
          &context);
      return;
    }
    if (l.epoch() != epoch) {
      FillErrorAndRespond(
          TabletServerErrorPB::NOT_THE_LEADER, STATUS(TryAgain, "Epoch changed"), resp, &context);
      return;
    }
    s = PersistRequest(epoch, *req);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to update object lock " << s;
      FillErrorAndRespond(TabletServerErrorPB::UNKNOWN_ERROR, s, resp, &context);
      return;
    }
  }

  // TODO: Fix this. GetAllDescriptors may need to change to handle tserver membership reliably.
  auto lock_objects =
      std::make_shared<UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>>(
          epoch, master_, catalog_manager_, this, *req, resp, std::move(context));
  lock_objects->Launch();
}

void ObjectLockInfoManager::Impl::UnlockObject(
    const ReleaseObjectLockRequestPB* req, ReleaseObjectLockResponsePB* resp,
    rpc::RpcContext context) {
  VLOG(3) << __PRETTY_FUNCTION__;
  // Release the locks locally.
  std::shared_ptr<tablet::TSLocalLockManager> local_lock_manager;
  LeaderEpoch epoch;
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
    if (!l.IsInitializedAndIsLeader()) {
      FillErrorAndRespond(
          TabletServerErrorPB::NOT_THE_LEADER, STATUS(TryAgain, "Master is not leader"), resp,
          &context);
      return;
    }
    epoch = l.epoch();
    local_lock_manager = ts_local_lock_manager();
    auto s = local_lock_manager->ReleaseObjectLocks(*req);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to release object lock locally." << s;
      FillErrorAndRespond(TabletServerErrorPB::UNKNOWN_ERROR, s, resp, &context);
      return;
    }

    // Persist the request.
    s = PersistRequest(epoch, *req);
    if (!s.ok()) {
      LOG(WARNING) << "Failed to update object lock " << s;
      FillErrorAndRespond(TabletServerErrorPB::UNKNOWN_ERROR, s, resp, &context);
      return;
    }
  }

  auto unlock_objects =
      std::make_shared<UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>>(
          epoch, master_, catalog_manager_, this, *req, resp, std::move(context));
  unlock_objects->Launch();
}

void ObjectLockInfoManager::Impl::UpdateObjectLocks(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  {
    LockGuard lock(mutex_);
    auto res = object_lock_infos_map_.insert_or_assign(tserver_uuid, info).second;
    DCHECK(res) << "UpdateObjectLocks called for an existing tserver_uuid " << tserver_uuid;
  }
  BootstrapLocalLocksFor(tserver_uuid);
}

void ObjectLockInfoManager::Impl::Clear() {
  catalog_manager_->AssertLeaderLockAcquiredForWriting();
  LockGuard lock(mutex_);
  object_lock_infos_map_.clear();
  local_lock_manager_.reset(new tablet::TSLocalLockManager());
}

template <class Req, class Resp>
UpdateAllTServers<Req, Resp>::UpdateAllTServers(
    LeaderEpoch epoch, Master* master, CatalogManager* catalog_manager,
    ObjectLockInfoManager::Impl* olm, const Req& req, Resp* resp, rpc::RpcContext rpc)
    : epoch_(epoch),
      master_(master),
      catalog_manager_(catalog_manager),
      object_lock_info_manager_(olm),
      req_(req),
      resp_(resp),
      context_(std::move(rpc)) {
  VLOG(3) << __PRETTY_FUNCTION__;
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Done(size_t i, const Status& s) {
  // TODO: We should check if a failing TServer has lost it's lease, and
  // if so ignore it/consider it a success.
  statuses_[i] = s;
  // TODO: There is a potential here for early return if s is not OK.
  if (--ts_pending_ == 0) {
    DoneAll();
  }
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Launch() {
  ts_descriptors_ = master_->ts_manager()->GetAllDescriptors();
  ts_pending_ = ts_descriptors_.size();
  statuses_ = std::vector<Status>{ts_descriptors_.size(), STATUS(Uninitialized, "")};

  LOG(INFO) << __func__ << " launching for " << ts_pending_ << " tservers.";
  for (size_t i = 0; i < ts_descriptors_.size(); ++i) {
    auto ts_uuid = ts_descriptors_[i]->permanent_uuid();
    LOG(INFO) << "Launching for " << ts_uuid;
    auto callback = std::bind(&UpdateAllTServers<Req, Resp>::Done, this, i, std::placeholders::_1);
    auto task = std::make_shared<master::UpdateTServer<Req, Resp>>(
        master_, catalog_manager_->AsyncTaskPool(), ts_uuid, this->shared_from_this(), callback);
    WARN_NOT_OK(
        catalog_manager_->ScheduleTask(task),
        yb::Format(
            "Failed to schedule request to UpdateTServer to $0 for $1", ts_uuid,
            request().DebugString()));
  }
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::DoneAll() {
  for (const auto& status : statuses_) {
    if (!status.ok()) {
      LOG(INFO) << "Error in acquiring object lock: " << status;
      // We will not try to clean up the locks here. The locks will be cleaned up
      // when the session/transaction finishes/aborts.
      FillErrorAndRespond(TabletServerErrorPB::UNKNOWN_ERROR, status, resp_, &context_);
      return;
    }
  }
  context_.RespondSuccess();
}

template <class Req, class Resp>
UpdateTServer<Req, Resp>::UpdateTServer(
    Master* master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
    std::shared_ptr<UpdateAllTServers<Req, Resp>> shared_all_tservers, StdStatusCallback callback)
    : RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, /* async_task_throttler */ nullptr),
      callback_(std::move(callback)),
      shared_all_tservers_(shared_all_tservers) {}

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
