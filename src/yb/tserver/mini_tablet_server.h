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
#ifndef YB_TSERVER_MINI_TABLET_SERVER_H
#define YB_TSERVER_MINI_TABLET_SERVER_H

#include <string>

#include "yb/common/schema.h"
#include "yb/gutil/macros.h"
#include "yb/tserver/tablet_server_options.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

namespace yb {

class FsManager;

namespace consensus {
class RaftConfigPB;
} // namespace consensus

namespace tserver {

class TabletServer;

// An in-process tablet server meant for use in test cases.
class MiniTabletServer {
 public:
  MiniTabletServer(const std::string& fs_root, uint16_t rpc_port,
                   const TabletServerOptions& extra_opts = TabletServerOptions(), int index = 0);
  ~MiniTabletServer();

  // Return the options which will be used to start the tablet server.
  // If you wish to make changes to these options, they need to be made
  // before calling Start(), or else they will have no effect.
  TabletServerOptions* options() { return &opts_; }

  // Start a tablet server running on the loopback interface and
  // an ephemeral port. To determine the address that the server
  // bound to, call MiniTabletServer::bound_addr().
  // The TS will be initialized asynchronously and then started.
  CHECKED_STATUS Start();

  // Waits for the tablet server to be fully initialized, including
  // having all tablets bootstrapped.
  CHECKED_STATUS WaitStarted();

  void Shutdown();
  void FlushTablets();
  void CleanTabletLogs();

  // Restart a tablet server on the same RPC and webserver ports.
  CHECKED_STATUS Restart();

  // Add a new tablet to the test server, use the default consensus configuration.
  //
  // Requires that the server has already been started with Start().
  CHECKED_STATUS AddTestTablet(const std::string& table_id,
                       const std::string& tablet_id,
                       const Schema& schema,
                       TableType table_type);

  // Add a new tablet to the test server and specify the consensus configuration
  // for the tablet.
  CHECKED_STATUS AddTestTablet(const std::string& table_id,
                       const std::string& tablet_id,
                       const Schema& schema,
                       const consensus::RaftConfigPB& config,
                       TableType table_type);

  // Create a RaftConfigPB which should be used to create a local-only
  // tablet on the given tablet server.
  consensus::RaftConfigPB CreateLocalConfig() const;

  Endpoint bound_rpc_addr() const;
  Endpoint bound_http_addr() const;

  const TabletServer* server() const { return server_.get(); }
  TabletServer* server() { return server_.get(); }

  bool is_started() const { return started_; }

  void FailHeartbeats();

 private:
  bool started_;
  TabletServerOptions opts_;
  int index_;

  gscoped_ptr<FsManager> fs_manager_;
  gscoped_ptr<TabletServer> server_;
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_MINI_TABLET_SERVER_H
