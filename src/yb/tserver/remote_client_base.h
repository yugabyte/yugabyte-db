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

#include <string>

#include "yb/gutil/macros.h"
#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/tserver/remote_bootstrap_file_downloader.h"
#include "yb/tserver/remote_bootstrap_session.h"
#include "yb/util/status_fwd.h"

namespace yb {

namespace tserver {
class TSTabletManager;

class RemoteBootstrapComponent {
 public:
  virtual Status CreateDirectories(const std::string& db_dir, FsManager* fs) = 0;
  virtual Status Download() = 0;

  virtual ~RemoteBootstrapComponent() = default;
};

// An abstract base class that extract the common logic out of RemoteBootstrapClient and
// RemoteSnapshotTransferClient. The derived classes each have their own corresponding Start methods
// with potentially differing signatures. This class itself implements the cleanup logic of removing
// the remote session from the server side and also decrementing the number of remote clients in the
// destructor.
class RemoteClientBase {
 public:
  RemoteClientBase(const RemoteClientBase&) = delete;
  RemoteClientBase& operator=(const RemoteClientBase&) = delete;

  virtual Status Finish() = 0;

  // Removes session at server.
  Status Remove();

  static int32_t StartedClientsCount();

 protected:
  // Construct the remote client base.
  // 'fs_manager' and 'messenger' must remain valid until this object is destroyed.
  RemoteClientBase(std::string tablet_id, FsManager* fs_manager);

  // Attempt to clean up resources on the remote end by sending an
  // EndRemoteBootstrapSession() RPC
  virtual ~RemoteClientBase();

  // End the remote session.
  Status EndRemoteSession();

  // Increment the number of remote clients.
  void Started();

  // Return standard log prefix.
  const std::string& LogPrefix() const { return log_prefix_; }

  const std::string& session_id() const { return downloader_.session_id(); }

  FsManager& fs_manager() const { return downloader_.fs_manager(); }

  Env& env() const;

  const std::string& permanent_uuid() const;

  static std::atomic<int32_t> remote_clients_started_;

  // Set-once members.
  const std::string tablet_id_;

  // State flags that enforce the progress of remote.
  bool started_ = false;  // Session started.

  std::shared_ptr<RemoteBootstrapServiceProxy> proxy_;
  std::unique_ptr<tablet::RaftGroupReplicaSuperBlockPB> superblock_;

  int64_t start_time_micros_ = 0;

  // We track whether this session succeeded and send this information as part of the
  // EndRemoteBootstrapSessionRequestPB request.
  bool succeeded_ = false;

  bool remove_required_ = false;

  const std::string log_prefix_;
  RemoteBootstrapFileDownloader downloader_;
};

}  // namespace tserver
}  // namespace yb
