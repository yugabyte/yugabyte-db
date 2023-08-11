// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/tserver/remote_client_base.h"

#include "yb/fs/fs_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/logging.h"

DEFINE_UNKNOWN_int32(
    remote_bootstrap_begin_session_timeout_ms, 5000,
    "Tablet server RPC client timeout for BeginRemoteBootstrapSession calls.");
TAG_FLAG(remote_bootstrap_begin_session_timeout_ms, hidden);

DEFINE_UNKNOWN_int32(
    remote_bootstrap_end_session_timeout_sec, 15,
    "Tablet server RPC client timeout for EndRemoteBootstrapSession calls. "
    "The timeout is usually a large value because we have to wait for the remote server "
    "to get a CHANGE_ROLE config change accepted.");
TAG_FLAG(remote_bootstrap_end_session_timeout_sec, hidden);

namespace yb {
namespace tserver {

using tablet::RaftGroupMetadataPtr;

RemoteClientBase::RemoteClientBase(const TabletId& tablet_id, FsManager* fs_manager)
    : tablet_id_(std::move(tablet_id)),
      log_prefix_(Format("T $0 P $1: Remote client base: ", tablet_id_, fs_manager->uuid())),
      downloader_(&log_prefix_, fs_manager) {}

RemoteClientBase::~RemoteClientBase() {
  // Note: Ending the remote session releases anchors on the remote.
  // This assumes that succeeded_ only gets set to true in Finish() just before calling
  // EndRemoteSession. If this didn't happen, then close the session here.
  if (!succeeded_) {
    LOG_WITH_PREFIX(INFO) << "Closing remote client session " << session_id()
                          << " in RemoteClientBase destructor.";
    WARN_NOT_OK(EndRemoteSession(), LogPrefix() + "Unable to close remote session " + session_id());
  }
  if (started_) {
    auto old_count = remote_clients_started_.fetch_sub(1, std::memory_order_acq_rel);
    if (old_count < 1) {
      LOG_WITH_PREFIX(DFATAL) << "Invalid number of remote sessions: " << old_count;
    }
  }
}

Status RemoteClientBase::EndRemoteSession() {
  if (!started_) {
    return Status::OK();
  }

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_remote_bootstrap_end_session_timeout_sec));

  EndRemoteBootstrapSessionRequestPB req;
  req.set_session_id(session_id());
  req.set_is_success(succeeded_);
  req.set_keep_session(succeeded_);
  EndRemoteBootstrapSessionResponsePB resp;

  LOG_WITH_PREFIX(INFO) << "Ending remote session " << session_id();
  auto status = proxy_->EndRemoteBootstrapSession(req, &resp, &controller);
  if (status.ok()) {
    remove_required_ = resp.session_kept();
    LOG_WITH_PREFIX(INFO) << "remote session " << session_id() << " ended successfully";
    return Status::OK();
  }

  if (status.IsTimedOut()) {
    // Ignore timeout errors since the server could have sent the ChangeConfig request and died
    // before replying. We need to check the config to verify that this server's role changed as
    // expected, in which case, the remote was completed successfully.
    LOG_WITH_PREFIX(INFO) << "remote session " << session_id() << " timed out";
    return Status::OK();
  }

  status = UnwindRemoteError(status, controller);
  return status.CloneAndPrepend(Format("Failed to end remote session $0", session_id()));
}

Status RemoteClientBase::Remove() {
  if (!remove_required_) {
    return Status::OK();
  }

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_remote_bootstrap_end_session_timeout_sec));

  RemoveRemoteBootstrapSessionRequestPB req;
  req.set_session_id(session_id());
  RemoveRemoteBootstrapSessionResponsePB resp;

  LOG_WITH_PREFIX(INFO) << "Removing remote session " << session_id();
  const auto status = proxy_->RemoveRemoteBootstrapSession(req, &resp, &controller);
  if (status.ok()) {
    LOG_WITH_PREFIX(INFO) << "remote session " << session_id() << " removed successfully";
    return Status::OK();
  }

  return UnwindRemoteError(status, controller)
      .CloneAndPrepend(Format("Failure removing remote session $0", session_id()));
}

std::atomic<int32_t> RemoteClientBase::remote_clients_started_{0};

int32_t RemoteClientBase::StartedClientsCount() {
  return remote_clients_started_.load(std::memory_order_acquire);
}

void RemoteClientBase::Started() {
  started_ = true;
  auto old_count = remote_clients_started_.fetch_add(1, std::memory_order_acq_rel);
  if (old_count < 0) {
    LOG_WITH_PREFIX(DFATAL) << "Invalid number of remote clients: " << old_count;
    remote_clients_started_.store(0, std::memory_order_release);
  }
}

Env& RemoteClientBase::env() const { return *fs_manager().env(); }

const std::string& RemoteClientBase::permanent_uuid() const { return fs_manager().uuid(); }

}  // namespace tserver
}  // namespace yb
