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
#ifndef KUDU_TSERVER_TABLET_SERVER_H
#define KUDU_TSERVER_TABLET_SERVER_H

#include <memory>
#include <string>
#include <vector>

#include "kudu/consensus/metadata.pb.h"
#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/server/server_base.h"
#include "kudu/server/webserver_options.h"
#include "kudu/tserver/tablet_server_options.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/status.h"

namespace kudu {

class MaintenanceManager;

namespace tserver {

class Heartbeater;
class ScannerManager;
class TabletServerPathHandlers;
class TSTabletManager;

class TabletServer : public server::ServerBase {
 public:
  // TODO: move this out of this header, since clients want to use this
  // constant as well.
  static const uint16_t kDefaultPort = 7050;
  static const uint16_t kDefaultWebPort = 8050;

  explicit TabletServer(const TabletServerOptions& opts);
  ~TabletServer();

  // Initializes the tablet server, including the bootstrapping of all
  // existing tablets.
  // Some initialization tasks are asynchronous, such as the bootstrapping
  // of tablets. Caller can block, waiting for the initialization to fully
  // complete by calling WaitInited().
  Status Init();

  // Waits for the tablet server to complete the initialization.
  Status WaitInited();

  Status Start();
  void Shutdown();

  std::string ToString() const;

  TSTabletManager* tablet_manager() { return tablet_manager_.get(); }

  ScannerManager* scanner_manager() { return scanner_manager_.get(); }

  Heartbeater* heartbeater() { return heartbeater_.get(); }

  void set_fail_heartbeats_for_tests(bool fail_heartbeats_for_tests) {
    base::subtle::NoBarrier_Store(&fail_heartbeats_for_tests_, 1);
  }

  bool fail_heartbeats_for_tests() const {
    return base::subtle::NoBarrier_Load(&fail_heartbeats_for_tests_);
  }

  MaintenanceManager* maintenance_manager() {
    return maintenance_manager_.get();
  }

 private:
  friend class TabletServerTestBase;

  Status ValidateMasterAddressResolution() const;

  bool initted_;

  // If true, all heartbeats will be seen as failed.
  Atomic32 fail_heartbeats_for_tests_;

  // The options passed at construction time.
  const TabletServerOptions opts_;

  // Manager for tablets which are available on this server.
  gscoped_ptr<TSTabletManager> tablet_manager_;

  // Manager for open scanners from clients.
  // This is always non-NULL. It is scoped only to minimize header
  // dependencies.
  gscoped_ptr<ScannerManager> scanner_manager_;

  // Thread responsible for heartbeating to the master.
  gscoped_ptr<Heartbeater> heartbeater_;

  // Webserver path handlers
  gscoped_ptr<TabletServerPathHandlers> path_handlers_;

  // The maintenance manager for this tablet server
  std::shared_ptr<MaintenanceManager> maintenance_manager_;

  DISALLOW_COPY_AND_ASSIGN(TabletServer);
};

} // namespace tserver
} // namespace kudu
#endif
