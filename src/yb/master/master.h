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

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "yb/common/wire_protocol.pb.h"

#include "yb/consensus/consensus.fwd.h"
#include "yb/consensus/metadata.fwd.h"

#include "yb/gutil/thread_annotations.h"
#include "yb/gutil/macros.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_options.h"
#include "yb/master/master_tserver.h"
#include "yb/master/tablet_health_manager.h"

#include "yb/server/server_base.h"

#include "yb/tserver/db_server_base.h"

#include "yb/util/status_fwd.h"

namespace yb {

class MaintenanceManager;
class RpcServer;
class ServerEntryPB;
class ThreadPool;
class AutoFlagsManagerBase;
class AutoFlagsConfigPB;

namespace server {

struct RpcServerOptions;

}

namespace rpc {

class SecureContext;

}

namespace master {

class MasterAutoFlagsManager;

class Master : public tserver::DbServerBase {
 public:
  explicit Master(const MasterOptions& opts);
  virtual ~Master();

  virtual Status InitFlags(rpc::Messenger* messenger) override;
  Status InitAutoFlagsFromMasterLeader(const HostPort& leader_address);
  Status Init() override;
  Status Start() override;

  Status StartAsync();
  Status WaitForCatalogManagerInit();

  // Wait until this Master's catalog manager instance is the leader and is ready.
  // This method is intended for use by unit tests.
  // If 'timeout' time is exceeded, returns Status::TimedOut.
  Status WaitUntilCatalogManagerIsLeaderAndReadyForTests(
    const MonoDelta& timeout = MonoDelta::FromSeconds(15))
      WARN_UNUSED_RESULT;

  void Shutdown() override;

  std::string ToString() const override;

  TSManager* ts_manager() const { return ts_manager_.get(); }

  CatalogManagerIf* catalog_manager() const;

  CatalogManager* catalog_manager_impl() const { return catalog_manager_.get(); }

  TabletSplitManager& tablet_split_manager() const;

  XClusterManagerIf* xcluster_manager() const;

  XClusterManager* xcluster_manager_impl() const;

  FlushManager* flush_manager() const { return flush_manager_.get(); }

  TestAsyncRpcManager* test_async_rpc_manager() const { return test_async_rpc_manager_.get(); }

  TabletHealthManager* tablet_health_manager() const { return tablet_health_manager_.get(); }

  MasterClusterHandler* master_cluster_handler() const { return master_cluster_handler_.get(); }

  YsqlBackendsManager* ysql_backends_manager() const {
    return ysql_backends_manager_.get();
  }

  PermissionsManager& permissions_manager();

  EncryptionManager& encryption_manager();

  MasterAutoFlagsManager* GetAutoFlagsManagerImpl() { return auto_flags_manager_.get(); }

  CloneStateManager* clone_state_manager() const;

  scoped_refptr<MetricEntity> metric_entity_cluster();

  void SetMasterAddresses(std::shared_ptr<server::MasterAddresses> master_addresses) {
    opts_.SetMasterAddresses(std::move(master_addresses));
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
  Status ResetMemoryState(const consensus::RaftConfigPB& new_config);

  void DumpMasterOptionsInfo(std::ostream* out);

  bool IsShellMode() const { return opts_.IsShellMode(); }

  void SetShellMode(bool mode) { opts_.SetShellMode(mode); }

  // Not a full shutdown, but makes this master go into a dormant mode (state_ is still kRunning).
  // Called currently by cluster master leader which is removing this master from the quorum.
  Status GoIntoShellMode();

  SysCatalogTable& sys_catalog() const;

  uint32_t GetAutoFlagConfigVersion() const override;
  AutoFlagsConfigPB GetAutoFlagsConfig() const;

  const std::shared_future<client::YBClient*>& client_future() const;

  const std::shared_future<client::YBClient*>& cdc_state_client_future() const;

  enum MasterMetricType {
    TaskMetric,
    AttemptMetric,
  };

  // Function that returns an object pointer to an RPC's histogram metric. If a histogram
  // metric pointer is not created, it will create a new object pointer and return it.
  scoped_refptr<Histogram> GetMetric(const std::string& metric_identifier,
                                     Master::MasterMetricType type,
                                     const std::string& description);

  std::map<std::string, scoped_refptr<Histogram>>* master_metrics()
    REQUIRES (master_metrics_mutex_) {
      return &master_metrics_;
  }

  Status get_ysql_db_oid_to_cat_version_info_map(
      const tserver::GetTserverCatalogVersionInfoRequestPB& req,
      tserver::GetTserverCatalogVersionInfoResponsePB *resp) const;

  Status ReloadKeysAndCertificates() override;

  std::string GetCertificateDetails() override;

  void WriteServerMetaCacheAsJson(JsonWriter* writer) override;

 protected:
  Status RegisterServices();

  void DisplayGeneralInfoIcons(std::stringstream* output) override;

  Status SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

  Result<std::unordered_set<std::string>> GetAvailableAutoFlagsForServer() const override;

  Result<std::unordered_set<std::string>> GetFlagsForServer() const override;

 private:
  friend class MasterTest;

  void InitCatalogManagerTask();
  Status InitCatalogManager();

  // Initialize registration_.
  // Requires that the web server and RPC server have been started.
  Status InitMasterRegistration();

  client::LocalTabletFilter CreateLocalTabletFilter() override;

  enum MasterState {
    kStopped,
    kInitialized,
    kRunning
  };

  MonoDelta default_client_timeout() override;

  const std::string& permanent_uuid() const override;

  void SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) override;

  std::atomic<MasterState> state_;

  // The metric entity for the cluster.
  scoped_refptr<MetricEntity> metric_entity_cluster_;

  std::unique_ptr<TSManager> ts_manager_;
  std::unique_ptr<CatalogManager> catalog_manager_;
  std::unique_ptr<MasterAutoFlagsManager> auto_flags_manager_;
  std::unique_ptr<YsqlBackendsManager> ysql_backends_manager_;
  std::unique_ptr<MasterPathHandlers> path_handlers_;
  std::unique_ptr<FlushManager> flush_manager_;
  std::unique_ptr<TabletHealthManager> tablet_health_manager_;
  std::unique_ptr<MasterClusterHandler> master_cluster_handler_;

  std::unique_ptr<TestAsyncRpcManager> test_async_rpc_manager_;

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

  // Master's tablet server implementation used to host virtual tables like system.peers.
  std::unique_ptr<MasterTabletServer> master_tablet_server_;

  std::unique_ptr<yb::client::AsyncClientInitializer> cdc_state_client_init_;
  std::mutex master_metrics_mutex_;
  std::map<std::string, scoped_refptr<Histogram>> master_metrics_ GUARDED_BY(master_metrics_mutex_);

  std::unique_ptr<rpc::SecureContext> secure_context_;

  DISALLOW_COPY_AND_ASSIGN(Master);
};

} // namespace master
} // namespace yb
