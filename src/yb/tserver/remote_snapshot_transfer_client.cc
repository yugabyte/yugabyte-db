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

#include "yb/tserver/remote_snapshot_transfer_client.h"

#include "yb/fs/fs_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/tserver/remote_bootstrap_snapshots.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/logging.h"

using namespace yb::size_literals;

DECLARE_int32(remote_bootstrap_begin_session_timeout_ms);

namespace yb {
namespace tserver {

using tablet::RaftGroupMetadataPtr;

RemoteSnapshotTransferClient::RemoteSnapshotTransferClient(
    const TabletId& tablet_id, FsManager* fs_manager)
    : RemoteClientBase(tablet_id, fs_manager) {}

RemoteSnapshotTransferClient::~RemoteSnapshotTransferClient() {}

Status RemoteSnapshotTransferClient::Start(
    rpc::ProxyCache* proxy_cache, const PeerId& source_peer_uuid,
    const HostPort& source_tablet_addr, const std::string& rocksdb_dir) {
  CHECK(!started_);

  start_time_micros_ = GetCurrentTimeMicros();

  LOG_WITH_PREFIX(INFO) << "Beginning remote snapshot transfer session"
                        << " from remote peer at address " << source_tablet_addr.ToString();

  // Set up an RPC proxy for the RemoteBootstrapService.
  proxy_.reset(new RemoteBootstrapServiceProxy(proxy_cache, source_tablet_addr));

  BeginRemoteSnapshotTransferSessionRequestPB req;
  req.set_requestor_uuid(permanent_uuid());

  // Set request tablet_id = source_peer_uuid because it is not guaranteed that the source tablet is
  // in the same RAFT group as the consumer tablet. For example, in xCluster native bootstrap, the
  // request is sent to an entirely separate universe.
  req.set_tablet_id(source_peer_uuid);

  rpc::RpcController controller;
  controller.set_timeout(
      MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_begin_session_timeout_ms));

  // Begin the remote snapshot transfer session with the remote peer.
  BeginRemoteSnapshotTransferSessionResponsePB resp;
  auto status = UnwindRemoteError(
      proxy_->BeginRemoteSnapshotTransferSession(req, &resp, &controller), controller);

  if (!status.ok()) {
    status = status.CloneAndPrepend("Unable to begin remote snapshot transfer session");
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  if (!CanServeTabletData(resp.superblock().tablet_data_state())) {
    Status s = STATUS(
        IllegalState,
        "Remote peer (" + source_peer_uuid + ")" + " is currently remotely bootstrapping itself!",
        resp.superblock().ShortDebugString());
    LOG_WITH_PREFIX(WARNING) << s.ToString();
    return s;
  }

  LOG_WITH_PREFIX(INFO) << "Received superblock: " << resp.superblock().ShortDebugString();
  RETURN_NOT_OK(MigrateSuperblock(resp.mutable_superblock()));

  auto* kv_store = resp.mutable_superblock()->mutable_kv_store();
  LOG_WITH_PREFIX(INFO) << "Snapshot files: " << yb::ToString(kv_store->snapshot_files());

  downloader_.Start(
      proxy_, resp.session_id(), MonoDelta::FromMilliseconds(resp.session_idle_timeout_millis()));
  LOG_WITH_PREFIX(INFO) << "Began remote snapshot transfer session " << session_id();

  superblock_.reset(resp.release_superblock());

  // Replace rocksdb_dir in the received superblock with our rocksdb_dir.
  kv_store->set_rocksdb_dir(rocksdb_dir);

  Started();
  return Status::OK();
}

Status RemoteSnapshotTransferClient::FetchSnapshot(
    const SnapshotId& snapshot_id, const SnapshotId& new_snapshot_id) {
  SCHECK(!snapshot_id.empty(), InvalidArgument, "Snapshot id is empty!");

  snapshot_component_.reset(new RemoteBootstrapSnapshotsComponent(&downloader_, superblock_.get()));
  return snapshot_component_->DownloadInto(
      &snapshot_id, new_snapshot_id.empty() ? nullptr : &new_snapshot_id);
}

Status RemoteSnapshotTransferClient::Finish() {
  CHECK(started_);

  succeeded_ = true;

  RETURN_NOT_OK_PREPEND(
      EndRemoteSession(), "Error closing remote snapshot transfer session " + session_id());

  return Status::OK();
}

}  // namespace tserver
}  // namespace yb
