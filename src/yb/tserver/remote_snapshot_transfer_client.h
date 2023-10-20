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
#pragma once

#include "yb/tserver/remote_bootstrap_snapshots.h"
#include "yb/tserver/remote_client_base.h"

namespace yb {

namespace tserver {

// Client class for using remote snapshot transfer.
class RemoteSnapshotTransferClient : public RemoteClientBase {
 public:
  // Construct the remote bootstrap client.
  // 'fs_manager' and 'messenger' must remain valid until this object is destroyed.
  RemoteSnapshotTransferClient(const TabletId& tablet_id, FsManager* fs_manager);

  // Attempt to clean up resources on the remote end by sending an
  // EndRemoteBootstrapSession() RPC
  virtual ~RemoteSnapshotTransferClient();

  // Start up a remote snapshot transfer session to download files from the specified tablet. Note
  // that the source tablet does not necessarily have to be in the same RAFT group or even the same
  // universe. The rocksdb_dir passed must be the corresponding path for the current tablet (i.e.
  // the one that is doing the downloading).
  Status Start(
      rpc::ProxyCache* proxy_cache, const PeerId& source_peer_uuid,
      const HostPort& source_tablet_addr, const std::string& rocksdb_dir);

  // Since the producer and consumer universe can have different snapshot IDs, we give the option of
  // downloading producer snapshot files into a specific consumer snapshot directory.
  Status FetchSnapshot(const SnapshotId& snapshot_id, const SnapshotId& new_snapshot_id = "");

  // After downloading all files successfully, write out the completed
  // replacement superblock.
  Status Finish() override;

 private:
  std::unique_ptr<RemoteBootstrapSnapshotsComponent> snapshot_component_;
};

}  // namespace tserver
}  // namespace yb
