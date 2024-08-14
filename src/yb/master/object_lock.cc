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

#include "yb/master/object_lock.h"

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

template <class Req, class Resp>
class UpdateAllTServers : public std::enable_shared_from_this<UpdateAllTServers<Req, Resp>> {
 public:
  UpdateAllTServers(
      Master* master, CatalogManager* catalog_manager, TSDescriptorVector&& ts_descs,
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

  Master* master_;
  CatalogManager* catalog_manager_;
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

void LockObject(
    Master* master, CatalogManager* catalog_manager, const AcquireObjectLockRequestPB* req,
    AcquireObjectLockResponsePB* resp, rpc::RpcContext rpc) {
  VLOG(0) << __PRETTY_FUNCTION__;
  // TODO: Fix this. GetAllDescriptors may need to change to handle tserver membership reliably.
  auto ts_descriptors = master->ts_manager()->GetAllDescriptors();
  auto lock_objects =
      std::make_shared<UpdateAllTServers<AcquireObjectLockRequestPB, AcquireObjectLockResponsePB>>(
          master, catalog_manager, std::move(ts_descriptors), *req, std::move(rpc));
  lock_objects->Launch();
}

void UnlockObject(
    Master* master, CatalogManager* catalog_manager, const ReleaseObjectLockRequestPB* req,
    ReleaseObjectLockResponsePB* resp, rpc::RpcContext rpc) {
  VLOG(0) << __PRETTY_FUNCTION__;
  // TODO: Fix this. GetAllDescriptors may need to change to handle tserver membership reliably.
  auto ts_descriptors = master->ts_manager()->GetAllDescriptors();
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
          master, catalog_manager, std::move(ts_descriptors), *req, std::move(rpc));
  unlock_objects->Launch();
}

template <class Req, class Resp>
UpdateAllTServers<Req, Resp>::UpdateAllTServers(
    Master* master, CatalogManager* catalog_manager, TSDescriptorVector&& ts_descriptors,
    const Req& req, rpc::RpcContext rpc)
    : master_(master),
      catalog_manager_(catalog_manager),
      ts_descriptors_(std::move(ts_descriptors)),
      statuses_(ts_descriptors_.size(), STATUS(Uninitialized, "")),
      req_(req),
      context_(std::move(rpc)) {
  VLOG(0) << __PRETTY_FUNCTION__;
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
  if (!RelaunchIfNecessary()) {
    context_.RespondSuccess();
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
  VLOG(0) << ToString() << __func__ << " attempt " << attempt;
  ts_proxy_->AcquireObjectLocksAsync(request(), &resp_, &rpc_, BindRpcCallback());
  return true;
}

template <>
bool UpdateTServer<ReleaseObjectLockRequestPB, ReleaseObjectLockResponsePB>::SendRequest(
    int attempt) {
  VLOG(0) << ToString() << __func__ << " attempt " << attempt;
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
  VLOG(0) << ToString() << __func__ << " response is " << yb::ToString(resp_);
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
  VLOG(0) << ToString() << __func__ << " (" << status << ")";
  callback_(status);
}

}  // namespace master
}  // namespace yb
