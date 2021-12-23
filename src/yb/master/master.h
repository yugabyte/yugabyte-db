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
#ifndef YB_MASTER_MASTER_H
#define YB_MASTER_MASTER_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "yb/consensus/consensus.fwd.h"
#include "yb/consensus/metadata.fwd.h"

#include "yb/gutil/thread_annotations.h"
#include "yb/gutil/macros.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_options.h"
#include "yb/master/master_tserver.h"
#include "yb/server/server_base.h"
#include "yb/tserver/db_server_base.h"
#include "yb/util/status_fwd.h"

namespace yb {

class MaintenanceManager;
class RpcServer;
class ServerEntryPB;
class ThreadPool;

namespace server {

struct RpcServerOptions;

}

namespace master {

class Master : public tserver::DbServerBase {
 public:
  explicit Master(const MasterOptions& opts);
  virtual ~Master();

  CHECKED_STATUS Init();
  CHECKED_STATUS Start();

  CHECKED_STATUS StartAsync();
  CHECKED_STATUS WaitForCatalogManagerInit();

  // Wait until this Master's catalog manager instance is the leader and is ready.
  // This method is intended for use by unit tests.
  // If 'timeout' time is exceeded, returns Status::TimedOut.
  CHECKED_STATUS WaitUntilCatalogManagerIsLeaderAndReadyForTests(
    const MonoDelta& timeout = MonoDelta::FromSeconds(15))
      WARN_UNUSED_RESULT;

  void Shutdown();

  std::string ToString() const override;

  TSManager* ts_manager() const { return ts_manager_.get(); }

  CatalogManagerIf* catalog_manager() const;

  enterprise::CatalogManager* catalog_manager_impl() const { return catalog_manager_.get(); }

  FlushManager* flush_manager() const { return flush_manager_.get(); }

  PermissionsManager& permissions_manager();

  EncryptionManager& encryption_manager();

  scoped_refptr<MetricEntity> metric_entity_cluster();

  void SetMasterAddresses(std::shared_ptr<server::MasterAddresses> master_addresses) {
    opts_.SetMasterAddresses(std::move(master_addresses));
  }

  const MasterOptions& opts() { return opts_; }

  // Get the RPC and HTTP addresses for this master instance.
  CHECKED_STATUS GetMasterRegistration(ServerRegistrationPB* registration) const;

  // Get node instance, Raft role, RPC and HTTP addresses for all
  // masters from the in-memory options used at master startup.
  // TODO move this to a separate class to be re-used in TS and
  // client; cache this information with a TTL (possibly in another
  // SysTable), so that we don't have to perform an RPC call on every
  // request.
  CHECKED_STATUS ListMasters(std::vector<ServerEntryPB>* masters) const;

  // Get node instance, Raft role, RPC and HTTP addresses for all
  // masters from the Raft config
  CHECKED_STATUS ListRaftConfigMasters(std::vector<consensus::RaftPeerPB>* masters) const;

  CHECKED_STATUS InformRemovedMaster(const HostPortPB& hp_pb);

  bool IsShutdown() const {
    return state_ == kStopped;
  }

  MaintenanceManager* maintenance_manager() {
    return maintenance_manager_.get();
  }

  // Recreates the master list based on the new config peers
  CHECKED_STATUS ResetMemoryState(const consensus::RaftConfigPB& new_config);

  void DumpMasterOptionsInfo(std::ostream* out);

  bool IsShellMode() const { return opts_.IsShellMode(); }

  void SetShellMode(bool mode) { opts_.SetShellMode(mode); }

  // Not a full shutdown, but makes this master go into a dormant mode (state_ is still kRunning).
  // Called currently by cluster master leader which is removing this master from the quorum.
  CHECKED_STATUS GoIntoShellMode();

  SysCatalogTable& sys_catalog() const;

  yb::client::AsyncClientInitialiser& async_client_initializer() {
    return *async_client_init_;
  }

  enum MasterMetricType {
    TaskMetric,
    AttemptMetric,
  };

  // Functon that returns a object pointer to a RPC's histogram metric. If a histogram
  // metric pointer is not created, it will create a new object pointer and return it.
  scoped_refptr<Histogram> GetMetric(const std::string& metric_identifier,
                                     Master::MasterMetricType type,
                                     const std::string& description);

  std::map<std::string, scoped_refptr<Histogram>>* master_metrics()
    REQUIRES (master_metrics_mutex_) {
      return &master_metrics_;
  }

 protected:
  virtual CHECKED_STATUS RegisterServices();

  void DisplayGeneralInfoIcons(std::stringstream* output) override;

 private:
  friend class MasterTest;

  void InitCatalogManagerTask();
  CHECKED_STATUS InitCatalogManager();

  // Initialize registration_.
  // Requires that the web server and RPC server have been started.
  CHECKED_STATUS InitMasterRegistration();

  const std::shared_future<client::YBClient*>& client_future() const override;

  client::LocalTabletFilter CreateLocalTabletFilter() override;

  enum MasterState {
    kStopped,
    kInitialized,
    kRunning
  };

  MasterState state_;

  std::unique_ptr<TSManager> ts_manager_;
  std::unique_ptr<enterprise::CatalogManager> catalog_manager_;
  std::unique_ptr<MasterPathHandlers> path_handlers_;
  std::unique_ptr<FlushManager> flush_manager_;

  // For initializing the catalog manager.
  std::unique_ptr<ThreadPool> init_pool_;

  // The status of the master initialization. This is set
  // by the async initialization task.
  std::promise<Status> init_status_;
  std::shared_future<Status> init_future_;

  MasterOptions opts_;

  AtomicUniquePtr<ServerRegistrationPB> registration_;

  // The maintenance manager for this master.
  std::shared_ptr<MaintenanceManager> maintenance_manager_;

  // The metric entity for the cluster.
  scoped_refptr<MetricEntity> metric_entity_cluster_;

  // Master's tablet server implementation used to host virtual tables like system.peers.
  std::unique_ptr<MasterTabletServer> master_tablet_server_;

  std::unique_ptr<yb::client::AsyncClientInitialiser> async_client_init_;
  std::mutex master_metrics_mutex_;
  std::map<std::string, scoped_refptr<Histogram>> master_metrics_ GUARDED_BY(master_metrics_mutex_);

  DISALLOW_COPY_AND_ASSIGN(Master);
};

} // namespace master
} // namespace yb
#endif // YB_MASTER_MASTER_H
