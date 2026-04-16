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

#include "yb/master/clone/clone_tasks.h"

#include "yb/master/master.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace master {

// ============================================================================
//  Class AsyncCloneTablet.
// ============================================================================
AsyncCloneTablet::AsyncCloneTablet(
    Master* master,
    ThreadPool* callback_pool,
    const TabletInfoPtr& tablet,
    LeaderEpoch epoch,
    tablet::CloneTabletRequestPB req)
    : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)),
      req_(std::move(req)) {}

std::string AsyncCloneTablet::description() const {
  return "Clone tablet RPC";
}

void AsyncCloneTablet::HandleResponse(int attempt) {
  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());
    LOG(WARNING) << "CloneTablet for tablet " << tablet_id() << "failed: "
                 << status;
    return;
  }

  TransitionToCompleteState();
}

bool AsyncCloneTablet::SendRequest(int attempt) {
  req_.set_dest_uuid(permanent_uuid());
  req_.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  ts_admin_proxy_->CloneTabletAsync(req_, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Sent clone tablets request to " << tablet_id();
  return true;
}

// ============================================================================
//  Class AsyncClonePgSchema.
// ============================================================================
AsyncClonePgSchema::AsyncClonePgSchema(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
    const std::string& source_db_name, const std::string& target_db_name, HybridTime restore_ht,
    const std::string& source_owner, const std::string& target_owner,
    ClonePgSchemaCallbackType callback, MonoTime deadline)
    : RetrySpecificTSRpcTask(
          master, callback_pool, std::move(permanent_uuid), /* async_task_throttler */ nullptr),
      source_db_name_(source_db_name),
      target_db_name_(target_db_name),
      source_owner_(source_owner),
      target_owner_(target_owner),
      restore_ht_(restore_ht),
      callback_(std::move(callback)) {
  // May not honor unresponsive deadline, refer to UnresponsiveDeadline().
  deadline_ = deadline;  // Time out according to earliest(deadline_,
                         // time of sending request + ysql_clone_pg_schema_rpc_timeout_ms).
}

std::string AsyncClonePgSchema::description() const { return "Async Clone PG Schema RPC"; }

void AsyncClonePgSchema::HandleResponse(int attempt) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "attempt: " << attempt;
  Status resp_status;
  if (resp_.has_error()) {
    resp_status = StatusFromPB(resp_.error().status());
    LOG(WARNING) << "Clone PG Schema Objects for source database: " << source_db_name_
                 << " failed: " << resp_status;
    TransitionToFailedState(state(), resp_status);
  } else {
    TransitionToCompleteState();
  }
}

void AsyncClonePgSchema::Finished(const Status& status) {
  WARN_NOT_OK(callback_(status), "Failed to execute the callback of AsyncClonePgSchema");
}

bool AsyncClonePgSchema::SendRequest(int attempt) {
  tserver::ClonePgSchemaRequestPB req;
  req.set_source_db_name(source_db_name_);
  req.set_target_db_name(target_db_name_);
  req.set_source_owner(source_owner_);
  req.set_target_owner(target_owner_);
  req.set_restore_ht(restore_ht_.ToUint64());
  ts_admin_proxy_->ClonePgSchemaAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << "Sent clone tablets request to " << tablet_id();
  return true;
}

MonoTime AsyncClonePgSchema::ComputeDeadline() const { return deadline_; }

// ============================================================================
//  Class AsyncClearMetacache.
// ============================================================================
AsyncClearMetacache::AsyncClearMetacache(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
    const std::string& namespace_id, ClearMetacacheCallbackType callback)
    : RetrySpecificTSRpcTask(
          master, callback_pool, permanent_uuid, /* async_task_throttler */ nullptr),
      namespace_id(namespace_id),
      callback_(callback) {}

std::string AsyncClearMetacache::description() const { return "Async ClearMetacache RPC"; }

void AsyncClearMetacache::HandleResponse(int attempt) {
  Status resp_status = Status::OK();
  if (resp_.has_error()) {
    resp_status = StatusFromPB(resp_.error().status());
    LOG(WARNING) << "Clear Metacache entries for namespace " << namespace_id
                 << " failed: " << resp_status;
    TransitionToFailedState(state(), resp_status);
  } else {
    TransitionToCompleteState();
  }
  WARN_NOT_OK(callback_(), "Failed to execute the callback of AsyncClearMetacache");
}

bool AsyncClearMetacache::SendRequest(int attempt) {
  tserver::ClearMetacacheRequestPB req;
  req.set_namespace_id(namespace_id);
  ts_proxy_->ClearMetacacheAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG_WITH_PREFIX(1) << Format(
      "Sent clear metacache entries request of namespace: $0 to $1", namespace_id, tablet_id());
  return true;
}

// ============================================================================
//  Class AsyncEnableDbConns.
// ============================================================================
AsyncEnableDbConns::AsyncEnableDbConns(
    Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const std::string& target_db_name, EnableDbConnsCallbackType callback)
    : RetrySpecificTSRpcTask(
          master, callback_pool, std::move(permanent_uuid), /* async_task_throttler */ nullptr),
      target_db_name_(target_db_name),
      callback_(std::move(callback)) {}

std::string AsyncEnableDbConns::description() const {
  return "Enable connections on cloned database " + target_db_name_;
}

void AsyncEnableDbConns::HandleResponse(int attempt) {
  Status resp_status;
  if (resp_.has_error()) {
    resp_status = StatusFromPB(resp_.error().status());
    LOG(WARNING) << "Failed to enable connections on cloned database " << target_db_name_
                 << ". Status: " << resp_status;
    TransitionToFailedState(state(), resp_status);
  } else {
    TransitionToCompleteState();
  }
  WARN_NOT_OK(callback_(resp_status), "Failed to execute callback of AsyncEnableDbConns");
}

bool AsyncEnableDbConns::SendRequest(int attempt) {
  tserver::EnableDbConnsRequestPB req;
  req.set_target_db_name(target_db_name_);
  ts_admin_proxy_->EnableDbConnsAsync(req, &resp_, &rpc_, BindRpcCallback());
  return true;
}

} // namespace master
} // namespace yb
