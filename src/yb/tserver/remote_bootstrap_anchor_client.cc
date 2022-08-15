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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/remote_bootstrap_anchor_client.h"
#include "yb/tserver/remote_bootstrap_file_downloader.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/callback.h"

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;
using yb::consensus::MakeOpIdPB;

DEFINE_int32(
    remote_bootstrap_anchor_session_timeout_ms, 5000,
    "Tablet server RPC client timeout for RemoteBootstrapAnchor Service calls.");
TAG_FLAG(remote_bootstrap_anchor_session_timeout_ms, hidden);

namespace yb {
namespace tserver {

RemoteBootstrapAnchorClient::RemoteBootstrapAnchorClient(
    const string& rbs_client_uuid,
    const string &owner_info,
    rpc::ProxyCache* proxy_cache,
    const HostPort& tablet_leader_peer_addr)
    : rbs_client_uuid_(rbs_client_uuid), owner_info_(owner_info) {
  proxy_.reset(new RemoteBootstrapServiceProxy(proxy_cache, tablet_leader_peer_addr));
}

Status RemoteBootstrapAnchorClient::RegisterLogAnchor(const string& tablet_id, const OpId& op_id) {
  RegisterLogAnchorRequestPB req;
  req.set_tablet_id(tablet_id);
  *req.mutable_op_id() = MakeOpIdPB(op_id);
  req.set_owner_info(owner_info_);

  RegisterLogAnchorResponsePB resp;

  rpc::RpcController controller;
  controller.set_timeout(
      MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_anchor_session_timeout_ms));

  auto status = UnwindRemoteError(proxy_->RegisterLogAnchor(req, &resp, &controller), controller);

  if (!status.ok()) {
    status = status.CloneAndPrepend(
        "Unable to call RegisterLogAnchor on tablet leader " + tablet_leader_peer_uuid_);
    LOG(WARNING) << status;
    return status;
  }

  return Status::OK();
}

Status RemoteBootstrapAnchorClient::ProcessLogAnchorRefreshStatus() {
  std::lock_guard<std::mutex> lock(log_anchor_status_mutex_);
  return log_anchor_refresh_status_;
}

void RemoteBootstrapAnchorClient::SetLogAnchorRefreshStatus(
    std::shared_ptr<rpc::RpcController> controller) {
  auto status = controller->status();
  if (!status.ok()) {
    std::lock_guard<std::mutex> lock(log_anchor_status_mutex_);
    log_anchor_refresh_status_ = status.CloneAndPrepend(
        "Unable to refresh Log Anchor session " + owner_info_);
  }
}

Status RemoteBootstrapAnchorClient::UpdateLogAnchorAsync(const OpId& op_id) {
  // Check if the last call to update log anchor failed. if so, return the status.
  RETURN_NOT_OK(ProcessLogAnchorRefreshStatus());

  UpdateLogAnchorRequestPB req;
  *req.mutable_op_id() = MakeOpIdPB(op_id);
  req.set_owner_info(owner_info_);

  UpdateLogAnchorResponsePB* resp = new UpdateLogAnchorResponsePB();

  std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
  controller->set_timeout(
      MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_anchor_session_timeout_ms));


  const scoped_refptr<RemoteBootstrapAnchorClient> shared_self(this);

  yb::Callback<void(std::shared_ptr<rpc::RpcController>)>
      callback = yb::Bind(&RemoteBootstrapAnchorClient::SetLogAnchorRefreshStatus,
                          shared_self);

  proxy_->UpdateLogAnchorAsync(
      req, resp, controller.get(),
      std::bind(&yb::Callback<void(std::shared_ptr<rpc::RpcController>)>::Run,
                callback, controller));

  return Status::OK();
}

Status RemoteBootstrapAnchorClient::KeepLogAnchorAliveAsync() {
  // Check if the last call to refresh log anchor session failed. if so, return the status.
  RETURN_NOT_OK(ProcessLogAnchorRefreshStatus());

  KeepLogAnchorAliveRequestPB req;
  req.set_owner_info(owner_info_);

  KeepLogAnchorAliveResponsePB* resp = new KeepLogAnchorAliveResponsePB();
  std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
  controller->set_timeout(
      MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_anchor_session_timeout_ms));

  const scoped_refptr<RemoteBootstrapAnchorClient> shared_self(this);

  yb::Callback<void(std::shared_ptr<rpc::RpcController>)>
      callback = yb::Bind(&RemoteBootstrapAnchorClient::SetLogAnchorRefreshStatus,
                          shared_self);

  proxy_->KeepLogAnchorAliveAsync(
      req, resp, controller.get(),
      std::bind(&yb::Callback<void(std::shared_ptr<rpc::RpcController>)>::Run,
                callback, controller));

  return Status::OK();
}

Status RemoteBootstrapAnchorClient::ChangePeerRole() {
  ChangePeerRoleRequestPB req;
  req.set_owner_info(owner_info_);
  req.set_requestor_uuid(rbs_client_uuid_);

  ChangePeerRoleResponsePB resp;

  rpc::RpcController controller;
  controller.set_timeout(
      MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_anchor_session_timeout_ms));

  auto status = UnwindRemoteError(proxy_->ChangePeerRole(req, &resp, &controller), controller);

  if (!status.ok()) {
    status = status.CloneAndPrepend("ChangePeerRole failed for session: " + owner_info_);
    LOG(WARNING) << status;
    return status;
  }
  return Status::OK();
}

// TODO: UnregisterLogAnchor could be made async, rbs source need not make the call in-line to
// the leader.
Status RemoteBootstrapAnchorClient::UnregisterLogAnchor() {
  UnregisterLogAnchorRequestPB req;
  req.set_owner_info(owner_info_);

  UnregisterLogAnchorResponsePB resp;

  rpc::RpcController controller;
  controller.set_timeout(
      MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_anchor_session_timeout_ms));

  auto status = UnwindRemoteError(proxy_->UnregisterLogAnchor(req, &resp, &controller), controller);

  if (!status.ok()) {
    status = status.CloneAndPrepend(
        "Unable to call UnregisterLogAnchor on tablet leader " + tablet_leader_peer_uuid_);
    LOG(WARNING) << status;
    return status;
  }

  return Status::OK();
}

RemoteBootstrapAnchorClient::~RemoteBootstrapAnchorClient() {}

}  // namespace tserver
}  // namespace yb
