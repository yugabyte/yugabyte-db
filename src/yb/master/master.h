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
#ifndef YB_MASTER_MASTER_H
#define YB_MASTER_MASTER_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "yb/consensus/consensus.pb.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/master/master_options.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/server/server_base.h"
#include "yb/util/metrics.h"
#include "yb/util/promise.h"
#include "yb/util/status.h"

namespace yb {

class MaintenanceManager;
class RpcServer;
struct RpcServerOptions;
class ServerEntryPB;
class ThreadPool;

namespace rpc {
class Messenger;
class ServicePool;
}

using yb::consensus::RaftConfigPB;

namespace master {

class CatalogManager;
class TSManager;
class MasterPathHandlers;

class Master : public server::ServerBase {
 public:
  static const uint16_t kDefaultPort = 7051;
  static const uint16_t kDefaultWebPort = 8051;

  explicit Master(const MasterOptions& opts);
  ~Master();

  Status Init();
  Status Start();

  Status StartAsync();
  Status WaitForCatalogManagerInit();

  // Wait until this Master's catalog manager instance is the leader and is ready.
  // This method is intended for use by unit tests.
  // If 'timeout' time is exceeded, returns Status::TimedOut.
  Status WaitUntilCatalogManagerIsLeaderAndReadyForTests(
    const MonoDelta& timeout = MonoDelta::FromSeconds(10))
      WARN_UNUSED_RESULT;

  void Shutdown();

  std::string ToString() const;

  TSManager* ts_manager() { return ts_manager_.get(); }

  CatalogManager* catalog_manager() { return catalog_manager_.get(); }

  scoped_refptr<MetricEntity> metric_entity_cluster() { return metric_entity_cluster_; }

  void SetMasterAddresses(std::shared_ptr<std::vector<HostPort>> master_addresses) {
    opts_.SetMasterAddresses(master_addresses);
  }

  const MasterOptions& opts() { return opts_; }

  // Get the RPC and HTTP addresses for this master instance.
  Status GetMasterRegistration(ServerRegistrationPB* registration) const;

  // Get node instance, Raft role, RPC and HTTP addresses for all
  // masters from the in-memory options used at master startup.
  // TODO move this to a separate class to be re-used in TS and
  // client; cache this information with a TTL (possibly in another
  // SysTable), so that we don't have to perform an RPC call on every
  // request.
  Status ListMasters(std::vector<ServerEntryPB>* masters) const;

  // Get node instance, Raft role, RPC and HTTP addresses for all
  // masters from the Raft config
  Status ListRaftConfigMasters(std::vector<consensus::RaftPeerPB>* masters) const;

  Status InformRemovedMaster(const HostPortPB& hp_pb);

  bool IsShutdown() const {
    return state_ == kStopped;
  }

  MaintenanceManager* maintenance_manager() {
    return maintenance_manager_.get();
  }

  // Recreates the master list based on the new config peers
  Status ResetMemoryState(const RaftConfigPB& new_config);

  void DumpMasterOptionsInfo(std::ostream* out);

  bool IsShellMode() const { return opts_.IsShellMode(); }

  void SetShellMode(bool mode) { opts_.SetShellMode(mode); }

  // Not a full shutdown, but makes this master go into a dormant mode (state_ is still kRunning).
  // Called currently by cluster master leader which is removing this master from the quorum.
  Status GoIntoShellMode();

 private:
  friend class MasterTest;

  void InitCatalogManagerTask();
  Status InitCatalogManager();

  // Initialize registration_.
  // Requires that the web server and RPC server have been started.
  Status InitMasterRegistration();

  enum MasterState {
    kStopped,
    kInitialized,
    kRunning
  };

  MasterState state_;

  gscoped_ptr<TSManager> ts_manager_;
  gscoped_ptr<CatalogManager> catalog_manager_;
  gscoped_ptr<MasterPathHandlers> path_handlers_;

  // For initializing the catalog manager.
  gscoped_ptr<ThreadPool> init_pool_;

  // The status of the master initialization. This is set
  // by the async initialization task.
  Promise<Status> init_status_;

  MasterOptions opts_;

  ServerRegistrationPB registration_;
  // True once registration_ has been initialized.
  std::atomic<bool> registration_initialized_;

  // The maintenance manager for this master.
  std::shared_ptr<MaintenanceManager> maintenance_manager_;

  // The metric entity for the cluster.
  scoped_refptr<MetricEntity> metric_entity_cluster_;

  DISALLOW_COPY_AND_ASSIGN(Master);
};

} // namespace master
} // namespace yb
#endif
