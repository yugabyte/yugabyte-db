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
      : master_(master), catalog_manager_(catalog_manager) {}

  void LockObject(
      LeaderEpoch epoch, const tserver::AcquireObjectLockRequestPB* req,
      tserver::AcquireObjectLockResponsePB* resp, rpc::RpcContext rpc);

  void UnlockObject(
      LeaderEpoch epoch, const tserver::ReleaseObjectLockRequestPB* req,
      tserver::ReleaseObjectLockResponsePB* resp, rpc::RpcContext rpc);

  Status RequestDone(LeaderEpoch epoch, const tserver::AcquireObjectLockRequestPB& req)
      EXCLUDES(mutex_);
  Status RequestDone(LeaderEpoch epoch, const tserver::ReleaseObjectLockRequestPB& req)
      EXCLUDES(mutex_);

  void ExportObjectLockInfo(tserver::DdlLockEntriesPB* resp) EXCLUDES(mutex_);

  bool InsertOrAssign(const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info)
      EXCLUDES(mutex_);
  void Clear() EXCLUDES(mutex_);

  std::shared_ptr<ObjectLockInfo> CreateOrGetObjectLockInfo(const std::string& key)
      EXCLUDES(mutex_);

 private:
  Master* master_;
  CatalogManager* catalog_manager_;
  std::atomic<size_t> next_request_id_{0};

  using MutexType = std::mutex;
  using LockGuard = std::lock_guard<MutexType>;
  mutable MutexType mutex_;
  std::unordered_map<std::string, std::shared_ptr<ObjectLockInfo>> object_lock_infos_map_
      GUARDED_BY(mutex_);
};

template <class Req, class Resp>
class UpdateAllTServers : public std::enable_shared_from_this<UpdateAllTServers<Req, Resp>> {
 public:
  UpdateAllTServers(
      LeaderEpoch epoch, Master* master, CatalogManager* catalog_manager,
      ObjectLockInfoManager::Impl* object_lock_info_manager, TSDescriptorVector&& ts_descs,
      const Req& req, rpc::RpcContext rpc);

  void Launch();
  const Req& request() const {
    return req_;
  }

 private:
  void LaunchFrom(size_t from_idx);
  void Done(size_t i, const Status& s);
  void CheckForDone();
  // Relaunches if there have been new TServers who joined. Returns true if relaunched.
  bool RelaunchIfNecessary();

  LeaderEpoch epoch_;
  Master* master_;
  CatalogManager* catalog_manager_;
  ObjectLockInfoManager::Impl* object_lock_info_manager_;
  TSDescriptorVector ts_descriptors_;
  std::atomic<size_t> ts_pending_;
  std::vector<Status> statuses_;
  const Req req_;
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
    LeaderEpoch epoch, const tserver::AcquireObjectLockRequestPB* req,
    tserver::AcquireObjectLockResponsePB* resp, rpc::RpcContext rpc) {
  impl_->LockObject(epoch, req, resp, std::move(rpc));
}
void ObjectLockInfoManager::UnlockObject(
    LeaderEpoch epoch, const tserver::ReleaseObjectLockRequestPB* req,
    tserver::ReleaseObjectLockResponsePB* resp, rpc::RpcContext rpc) {
  impl_->UnlockObject(epoch, req, resp, std::move(rpc));
}
void ObjectLockInfoManager::ExportObjectLockInfo(tserver::DdlLockEntriesPB* resp) {
  impl_->ExportObjectLockInfo(resp);
}
bool ObjectLockInfoManager::InsertOrAssign(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  return impl_->InsertOrAssign(tserver_uuid, info);
}
void ObjectLockInfoManager::Clear() { impl_->Clear(); }

std::shared_ptr<ObjectLockInfo> ObjectLockInfoManager::Impl::CreateOrGetObjectLockInfo(
    const std::string& key) {
  LockGuard lock(mutex_);
  if (object_lock_infos_map_.contains(key)) {
    return object_lock_infos_map_.at(key);
  } else {
    std::shared_ptr<ObjectLockInfo> object_lock_info;
    object_lock_info = std::make_shared<ObjectLockInfo>(key);
    object_lock_infos_map_[key] = object_lock_info;
    return object_lock_info;
  }
}

Status ObjectLockInfoManager::Impl::RequestDone(
    LeaderEpoch epoch, const tserver::AcquireObjectLockRequestPB& req) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto key = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info = CreateOrGetObjectLockInfo(key);

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

Status ObjectLockInfoManager::Impl::RequestDone(
    LeaderEpoch epoch, const tserver::ReleaseObjectLockRequestPB& req) {
  VLOG(3) << __PRETTY_FUNCTION__;
  auto key = req.session_host_uuid();
  std::shared_ptr<ObjectLockInfo> object_lock_info;
  {
    LockGuard lock(mutex_);
    if (!object_lock_infos_map_.contains(key)) {
      VLOG(1) << "Cannot release locks related to untracked host/session. Req: "
              << req.DebugString();
      return Status::OK();
    }
    object_lock_info = object_lock_infos_map_[key];
  }
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

}  // namespace

void ObjectLockInfoManager::Impl::ExportObjectLockInfo(tserver::DdlLockEntriesPB* resp) {
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

void ObjectLockInfoManager::Impl::LockObject(
    LeaderEpoch epoch, const AcquireObjectLockRequestPB* req, AcquireObjectLockResponsePB* resp,
    rpc::RpcContext rpc) {
  VLOG(3) << __PRETTY_FUNCTION__;
  // TODO: Fix this. GetAllDescriptors may need to change to handle tserver membership reliably.
  auto ts_descriptors = master_->ts_manager()->GetAllDescriptors();
  auto lock_objects =
      std::make_shared<UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>>(
          epoch, master_, catalog_manager_, this, std::move(ts_descriptors), *req, std::move(rpc));
  lock_objects->Launch();
}

void ObjectLockInfoManager::Impl::UnlockObject(
    LeaderEpoch epoch, const ReleaseObjectLockRequestPB* req, ReleaseObjectLockResponsePB* resp,
    rpc::RpcContext rpc) {
  VLOG(3) << __PRETTY_FUNCTION__;
  // TODO: Fix this. GetAllDescriptors may need to change to handle tserver membership reliably.
  auto ts_descriptors = master_->ts_manager()->GetAllDescriptors();
  if (ts_descriptors.empty()) {
    // TODO(Amit): Handle the case where the master receives a DDL lock/unlock request
    // before all the TServers have registered
    // (they may have registered with the older master-leader, thus serving DML reqs).
    // Get rid of this check when we have done this.
    rpc.RespondRpcFailure(
        rpc::ErrorStatusPB::ERROR_APPLICATION,
        STATUS(IllegalState, "No TServers registered with the master yet."));
    return;
  }
  auto unlock_objects =
      std::make_shared<UpdateAllTServers<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>>(
          epoch, master_, catalog_manager_, this, std::move(ts_descriptors), *req, std::move(rpc));
  unlock_objects->Launch();
}

bool ObjectLockInfoManager::Impl::InsertOrAssign(
    const std::string& tserver_uuid, std::shared_ptr<ObjectLockInfo> info) {
  LockGuard lock(mutex_);
  return object_lock_infos_map_.insert_or_assign(tserver_uuid, info).second;
}

void ObjectLockInfoManager::Impl::Clear() {
  LockGuard lock(mutex_);
  object_lock_infos_map_.clear();
}

template <class Req, class Resp>
UpdateAllTServers<Req, Resp>::UpdateAllTServers(
    LeaderEpoch epoch, Master* master, CatalogManager* catalog_manager,
    ObjectLockInfoManager::Impl* olm, TSDescriptorVector&& ts_descriptors, const Req& req,
    rpc::RpcContext rpc)
    : epoch_(epoch),
      master_(master),
      catalog_manager_(catalog_manager),
      object_lock_info_manager_(olm),
      ts_descriptors_(std::move(ts_descriptors)),
      statuses_(ts_descriptors_.size(), STATUS(Uninitialized, "")),
      req_(req),
      context_(std::move(rpc)) {
  VLOG(3) << __PRETTY_FUNCTION__;
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Launch() {
  LaunchFrom(0);
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::Done(size_t i, const Status& s) {
  statuses_[i] = s;
  // TODO: There is a potential here for early return if s is not OK.
  if (--ts_pending_ == 0) {
    CheckForDone();
  }
}

template <class Req, class Resp>
void UpdateAllTServers<Req, Resp>::LaunchFrom(size_t start_idx) {
  size_t num_descriptors = ts_descriptors_.size();
  ts_pending_ = num_descriptors - start_idx;
  LOG(INFO) << __func__ << " launching for " << ts_pending_ << " tservers.";
  for (size_t i = start_idx; i < num_descriptors; ++i) {
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
void UpdateAllTServers<Req, Resp>::CheckForDone() {
  for (const auto& status : statuses_) {
    if (!status.ok()) {
      LOG(INFO) << "Error in acquiring object lock: " << status;
      // TBD: Release the taken locks.
      // What is the best way to handle failures? Say one TServer errors out,
      // Should we release the locks taken by the other TServers? What if one of the
      // TServers had given a lock to the same session earlier -- would a release get
      // rid of that lock as well?
      // How to handle this during Release locks?
      //
      // What can cause failures here?
      // - If a TServer goes down, how can we handle it?
      context_.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION, status);
      return;
    }
  }
  if (RelaunchIfNecessary()) {
    return;
  }

  LOG(INFO) << __PRETTY_FUNCTION__ << " is done. Updating sys catalog";
  // We are done.
  //
  // Update the master accordingly, after all the TServers.
  // if master fails over, before all the TServers have responded, the tserver
  // will need to retry the request at the new master-leader.
  auto status = object_lock_info_manager_->RequestDone(epoch_, req_);
  if (status.ok()) {
    context_.RespondSuccess();
  } else {
    LOG(WARNING) << "Failed to update object lock " << status;
    context_.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION, status);
  }
}

template <class Req, class Resp>
bool UpdateAllTServers<Req, Resp>::RelaunchIfNecessary() {
  auto old_size = ts_descriptors_.size();
  auto ts_descriptors = master_->ts_manager()->GetAllDescriptors();
  for (auto ts_descriptor : ts_descriptors) {
    if (std::find(ts_descriptors_.begin(), ts_descriptors_.end(), ts_descriptor) ==
        ts_descriptors_.end()) {
      ts_descriptors_.push_back(ts_descriptor);
      statuses_.push_back(Status::OK());
    }
  }
  if (ts_descriptors.size() == old_size) {
    return false;
  }

  LOG(INFO) << "New TServers were added. Relaunching.";
  LaunchFrom(old_size);
  return true;
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
