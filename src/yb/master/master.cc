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

#include "yb/master/master.h"

#include <algorithm>
#include <list>
#include <memory>
#include <vector>

#include "yb/client/client.h"

#include "yb/common/pg_catversions.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_meta.h"

#include "yb/gutil/bind.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/clone/clone_state_manager.h"
#include "yb/master/flush_manager.h"
#include "yb/master/master_auto_flags_manager.h"
#include "yb/master/master-path-handlers.h"
#include "yb/master/master_backup.service.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_cluster_handler.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_service.h"
#include "yb/master/master_tablet_service.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/test_async_rpc_manager.h"
#include "yb/master/ysql_backends_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/service_pool.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/server/async_client_initializer.h"
#include "yb/server/hybrid_clock.h"
#include "yb/server/rpc_server.h"

#include "yb/tablet/maintenance_manager.h"

#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/remote_bootstrap_service.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/ntp_clock.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status.h"
#include "yb/util/threadpool.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

DEFINE_UNKNOWN_int32(master_rpc_timeout_ms, 1500,
             "Timeout for retrieving master registration over RPC.");
TAG_FLAG(master_rpc_timeout_ms, experimental);

DEFINE_UNKNOWN_int32(master_yb_client_default_timeout_ms, 60000,
             "Default timeout for the YBClient embedded into the master.");

DEFINE_NON_RUNTIME_int32(master_backup_svc_queue_length, 50,
             "RPC queue length for master backup service");
TAG_FLAG(master_backup_svc_queue_length, advanced);

DECLARE_bool(master_join_existing_universe);

METRIC_DEFINE_entity(cluster);

using namespace std::literals;
using std::min;
using std::vector;
using std::string;

using yb::consensus::RaftPeerPB;
using yb::rpc::ServiceIf;
using yb::tserver::ConsensusServiceImpl;

DEPRECATE_FLAG(int32, master_tserver_svc_num_threads, "02_2024");
DEPRECATE_FLAG(int32, master_svc_num_threads, "02_2024");
DEPRECATE_FLAG(int32, master_consensus_svc_num_threads, "02_2024");
DEPRECATE_FLAG(int32, master_remote_bootstrap_svc_num_threads, "02_2024");

DEFINE_UNKNOWN_int32(master_tserver_svc_queue_length, 1000,
             "RPC queue length for master tserver service");
TAG_FLAG(master_tserver_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(master_svc_queue_length, 1000,
             "RPC queue length for master service");
TAG_FLAG(master_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(master_consensus_svc_queue_length, 1000,
             "RPC queue length for master consensus service");
TAG_FLAG(master_consensus_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(master_remote_bootstrap_svc_queue_length, 50,
             "RPC queue length for master remote bootstrap service");
TAG_FLAG(master_remote_bootstrap_svc_queue_length, advanced);

DEFINE_test_flag(string, master_extra_list_host_port, "",
                 "Additional host port used in list masters");

DECLARE_int64(inbound_rpc_memory_limit);

DECLARE_int32(master_ts_rpc_timeout_ms);

DECLARE_bool(ysql_enable_db_catalog_version_mode);

namespace yb {
namespace master {

Master::Master(const MasterOptions& opts)
    : DbServerBase("Master", opts, "yb.master", server::CreateMemTrackerForServer()),
      state_(kStopped),
      metric_entity_cluster_(
          METRIC_ENTITY_cluster.Instantiate(metric_registry_.get(), "yb.cluster")),
      ts_manager_(new TSManager()),
      catalog_manager_(new CatalogManager(this)),
      auto_flags_manager_(
          new MasterAutoFlagsManager(clock(), fs_manager_.get(), catalog_manager_impl())),
      ysql_backends_manager_(new YsqlBackendsManager(this, catalog_manager_->AsyncTaskPool())),
      path_handlers_(new MasterPathHandlers(this)),
      flush_manager_(new FlushManager(this, catalog_manager())),
      tablet_health_manager_(new TabletHealthManager(this, catalog_manager())),
      master_cluster_handler_(new MasterClusterHandler(catalog_manager_impl(), ts_manager_.get())),
      tablet_split_manager_(new TabletSplitManager(
          *this, metric_entity(), metric_entity_cluster())),
      clone_state_manager_(CloneStateManager::Create(catalog_manager(), this, &sys_catalog())),
      snapshot_coordinator_(new MasterSnapshotCoordinator(
          catalog_manager_impl(), catalog_manager_impl(), tablet_split_manager())),
      test_async_rpc_manager_(new TestAsyncRpcManager(this, catalog_manager())),
      init_future_(init_status_.get_future()),
      opts_(opts),
      maintenance_manager_(new MaintenanceManager(MaintenanceManager::DEFAULT_OPTIONS)),
      master_tablet_server_(new MasterTabletServer(this, metric_entity())) {
  SetConnectionContextFactory(rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(
      GetAtomicFlag(&FLAGS_inbound_rpc_memory_limit), mem_tracker()));

  LOG(INFO) << "yb::master::Master created at " << this;
  LOG(INFO) << "yb::master::TSManager created at " << ts_manager_.get();
  LOG(INFO) << "yb::master::CatalogManager created at " << catalog_manager_.get();
}

Master::~Master() {
  Shutdown();
}

string Master::ToString() const {
  if (state_.load() != kRunning) {
    return "Master (stopped)";
  }
  return strings::Substitute("Master@$0", yb::ToString(first_rpc_address()));
}

Status Master::Init() {
  CHECK_EQ(kStopped, state_.load());

  RETURN_NOT_OK(ThreadPoolBuilder("init").set_max_threads(1).Build(&init_pool_));

  RETURN_NOT_OK(DbServerBase::Init());

  RETURN_NOT_OK(fs_manager_->ListTabletIds());

  RETURN_NOT_OK(path_handlers_->Register(web_server_.get()));

  auto bound_addresses = rpc_server()->GetBoundAddresses();
  if (!bound_addresses.empty()) {
    shared_object().SetHostEndpoint(bound_addresses.front(), get_hostname());
  }

  cdc_state_client_init_ = std::make_unique<client::AsyncClientInitializer>(
      "cdc_state_client",
      default_client_timeout(),
      "" /* tserver_uuid */,
      &options(),
      metric_entity(),
      mem_tracker(),
      messenger());
  cdc_state_client_init_->builder()
      .set_master_address_flag_name("master_addresses")
      .default_admin_operation_timeout(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms))
      .AddMasterAddressSource([this] {
    return catalog_manager_->GetMasterAddresses();
  });
  cdc_state_client_init_->Start();

  state_ = kInitialized;
  return Status::OK();
}

Status Master::InitFlags(rpc::Messenger* messenger) {
  // Will we be in shell mode if we dont have a sys catalog yet?
  bool is_shell_mode_if_new =
      FLAGS_master_join_existing_universe || !opts().AreMasterAddressesProvided();

  RETURN_NOT_OK(auto_flags_manager_->Init(
      messenger,
      [this]() {
        return fs_manager_->LookupTablet(kSysCatalogTabletId);
      } /* has_sys_catalog_func */,
      is_shell_mode_if_new));

  return RpcAndWebServerBase::InitFlags(messenger);
}

Result<std::unordered_set<std::string>> Master::GetAvailableAutoFlagsForServer() const {
  return auto_flags_manager_->GetAvailableAutoFlagsForServer();
}

Result<std::unordered_set<std::string>> Master::GetFlagsForServer() const {
  return yb::GetFlagNamesFromXmlFile("master_flags.xml");
}

Status Master::InitAutoFlagsFromMasterLeader(const HostPort& leader_address) {
  SCHECK(
      opts().IsShellMode(), IllegalState,
      "Cannot load AutoFlags from another master when not in shell mode.");

  return auto_flags_manager_->LoadFromMasterLeader({{leader_address}});
}

MonoDelta Master::default_client_timeout() {
  return std::chrono::milliseconds(FLAGS_master_yb_client_default_timeout_ms);
}

const std::string& Master::permanent_uuid() const {
  return fs_manager_->uuid();
}

void Master::SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) {
  async_client_init->builder()
      .set_master_address_flag_name("master_addresses")
      .default_admin_operation_timeout(MonoDelta::FromMilliseconds(FLAGS_master_rpc_timeout_ms))
      .AddMasterAddressSource([this] {
        return catalog_manager_->GetMasterAddresses();
  });
}

Status Master::Start() {
  RETURN_NOT_OK(StartAsync());
  RETURN_NOT_OK(WaitForCatalogManagerInit());
  google::FlushLogFiles(google::INFO); // Flush the startup messages.
  return Status::OK();
}

Status Master::RegisterServices() {
#if !defined(__APPLE__)
  server::HybridClock::RegisterProvider(NtpClock::Name(), [](const std::string&) {
    return std::make_shared<NtpClock>();
  });
#endif

  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterAdminService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterBackupService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterClientService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterClusterService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterDclService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterDdlService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterEncryptionService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterHeartbeatService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterReplicationService(this)));
  RETURN_NOT_OK(RegisterService(FLAGS_master_svc_queue_length, MakeMasterTestService(this)));

  RETURN_NOT_OK(RegisterService(
      FLAGS_master_tserver_svc_queue_length,
      std::make_shared<MasterTabletServiceImpl>(master_tablet_server_.get(), this)));

  RETURN_NOT_OK(RegisterService(
      FLAGS_master_consensus_svc_queue_length,
      std::make_shared<ConsensusServiceImpl>(metric_entity(), catalog_manager_.get()),
      rpc::ServicePriority::kHigh));

  RETURN_NOT_OK(RegisterService(
      FLAGS_master_remote_bootstrap_svc_queue_length,
      std::make_shared<tserver::RemoteBootstrapServiceImpl>(
          fs_manager_.get(), catalog_manager_.get(), metric_entity(), opts_.MakeCloudInfoPB(),
          &this->proxy_cache())));

  RETURN_NOT_OK(RegisterService(
      FLAGS_master_svc_queue_length,
      std::make_shared<tserver::PgClientServiceImpl>(
          *master_tablet_server_, client_future(), clock(),
          std::bind(&Master::TransactionPool, this), mem_tracker(), metric_entity(), messenger(),
          fs_manager_->uuid(), &options(), nullptr /* xcluster_context */)));

  return Status::OK();
}

void Master::DisplayGeneralInfoIcons(std::stringstream* output) {
  server::RpcAndWebServerBase::DisplayGeneralInfoIcons(output);
  // Tasks.
  DisplayIconTile(output, "fa-check", "Tasks", "/tasks");
  DisplayIconTile(output, "fa-clone", "Replica Info", "/tablet-replication");
  DisplayIconTile(output, "fa-clock-o", "TServer Clocks", "/tablet-server-clocks");
  DisplayIconTile(output, "fa-tasks", "Load Balancer", "/load-distribution");
}

Status Master::StartAsync() {
  CHECK_EQ(kInitialized, state_.load());

  RETURN_NOT_OK(maintenance_manager_->Init());
  RETURN_NOT_OK(RegisterServices());
  RETURN_NOT_OK(DbServerBase::Start());

  // Now that we've bound, construct our ServerRegistrationPB.
  RETURN_NOT_OK(InitMasterRegistration());

  // Start initializing the catalog manager.
  RETURN_NOT_OK(init_pool_->SubmitClosure(Bind(&Master::InitCatalogManagerTask,
                                               Unretained(this))));

  state_ = kRunning;
  return Status::OK();
}

void Master::InitCatalogManagerTask() {
  Status s = InitCatalogManager();
  if (!s.ok()) {
    LOG(ERROR) << ToString() << ": Unable to init master catalog manager: " << s.ToString();
  }
  init_status_.set_value(s);
}

Status Master::InitCatalogManager() {
  if (catalog_manager_->IsInitialized()) {
    return STATUS(IllegalState, "Catalog manager is already initialized");
  }
  RETURN_NOT_OK_PREPEND(catalog_manager_->Init(),
                        "Unable to initialize catalog manager");
  return Status::OK();
}

Status Master::WaitForCatalogManagerInit() {
  CHECK_EQ(state_.load(), kRunning);

  return init_future_.get();
}

Status Master::WaitUntilCatalogManagerIsLeaderAndReadyForTests(const MonoDelta& timeout) {
  RETURN_NOT_OK(catalog_manager_->WaitForWorkerPoolTests(timeout));
  Status s;
  MonoTime start = MonoTime::Now();
  int backoff_ms = 1;
  const int kMaxBackoffMs = 256;
  do {
    SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_.get());
    if (l.IsInitializedAndIsLeader()) {
      return Status::OK();
    }
    l.Unlock();

    SleepFor(MonoDelta::FromMilliseconds(backoff_ms));
    backoff_ms = min(backoff_ms << 1, kMaxBackoffMs);
  } while (MonoTime::Now().GetDeltaSince(start).LessThan(timeout));
  return STATUS(TimedOut, "Maximum time exceeded waiting for master leadership",
                          s.ToString());
}

void Master::Shutdown() {
  if (state_.load() == kRunning) {
    string name = ToString();
    LOG(INFO) << name << " shutting down...";
    maintenance_manager_->Shutdown();
    // We shutdown RpcAndWebServerBase here in order to shutdown messenger and reactor threads
    // before shutting down catalog manager. This is needed to prevent async calls callbacks
    // (running on reactor threads) from trying to use catalog manager thread pool which would be
    // already shutdown.
    auto started = catalog_manager_->StartShutdown();
    LOG_IF(DFATAL, !started) << name << " catalog manager shutdown already in progress";
    async_client_init_->Shutdown();
    cdc_state_client_init_->Shutdown();
    RpcAndWebServerBase::Shutdown();
    if (init_pool_) {
      init_pool_->Shutdown();
    }
    if (snapshot_coordinator_) {
      snapshot_coordinator_->Shutdown();
    }
    catalog_manager_->CompleteShutdown();
    LOG(INFO) << name << " shutdown complete.";
  } else {
    LOG(INFO) << ToString() << " did not start, shutting down all that started...";
    RpcAndWebServerBase::Shutdown();
  }
  state_ = kStopped;
}

Status Master::GetMasterRegistration(ServerRegistrationPB* reg) const {
  auto* registration = registration_.get();
  if (!registration) {
    return STATUS(ServiceUnavailable, "Master startup not complete");
  }
  reg->CopyFrom(*registration);
  return Status::OK();
}

Status Master::InitMasterRegistration() {
  CHECK(!registration_.get());

  auto reg = std::make_unique<ServerRegistrationPB>();
  RETURN_NOT_OK(GetRegistration(reg.get()));
  registration_.reset(reg.release());

  return Status::OK();
}

Status Master::ResetMemoryState(const consensus::RaftConfigPB& config) {
  LOG(INFO) << "Memory state set to config: " << config.ShortDebugString();

  auto master_addr = std::make_shared<server::MasterAddresses>();
  for (const RaftPeerPB& peer : config.peers()) {
    master_addr->push_back({HostPortFromPB(DesiredHostPort(peer, opts_.MakeCloudInfoPB()))});
  }

  SetMasterAddresses(std::move(master_addr));

  return Status::OK();
}

void Master::DumpMasterOptionsInfo(std::ostream* out) {
  *out << "Master options : ";
  auto master_addresses_shared_ptr = opts_.GetMasterAddresses();  // ENG-285
  bool first = true;
  for (const auto& list : *master_addresses_shared_ptr) {
    if (first) {
      first = false;
    } else {
      *out << ", ";
    }
    bool need_comma = false;
    for (const HostPort& hp : list) {
      if (need_comma) {
        *out << "/ ";
      }
      need_comma = true;
      *out << hp.ToString();
    }
  }
  *out << "\n";
}

Status Master::ListRaftConfigMasters(std::vector<RaftPeerPB>* masters) const {
  consensus::ConsensusStatePB cpb;
  RETURN_NOT_OK(catalog_manager_->GetCurrentConfig(&cpb));
  if (cpb.has_config()) {
    for (RaftPeerPB peer : cpb.config().peers()) {
      masters->push_back(peer);
    }
    return Status::OK();
  } else {
    return STATUS(NotFound, "No raft config found.");
  }
}

Status Master::ListMasters(std::vector<ServerEntryPB>* masters) const {
  if (IsShellMode()) {
    ServerEntryPB local_entry;
    local_entry.mutable_instance_id()->CopyFrom(catalog_manager_->NodeInstance());
    RETURN_NOT_OK(GetMasterRegistration(local_entry.mutable_registration()));
    local_entry.set_role(IsShellMode() ? PeerRole::NON_PARTICIPANT : PeerRole::LEADER);
    masters->push_back(local_entry);
    return Status::OK();
  }

  consensus::ConsensusStatePB cpb;
  RETURN_NOT_OK(catalog_manager_->GetCurrentConfig(&cpb));
  if (!cpb.has_config()) {
      return STATUS(NotFound, "No raft config found.");
  }

  for (const RaftPeerPB& peer : cpb.config().peers()) {
    // Get all network addresses associated with this peer master
    std::vector<HostPort> addrs;
    for (const auto& hp : peer.last_known_private_addr()) {
      addrs.push_back(HostPortFromPB(hp));
    }
    for (const auto& hp : peer.last_known_broadcast_addr()) {
      addrs.push_back(HostPortFromPB(hp));
    }
    if (!FLAGS_TEST_master_extra_list_host_port.empty()) {
      addrs.push_back(VERIFY_RESULT(HostPort::FromString(
          FLAGS_TEST_master_extra_list_host_port, 0)));
    }

    // Make GetMasterRegistration calls for peer master info.
    ServerEntryPB peer_entry;
    Status s = GetMasterEntryForHosts(
        proxy_cache_.get(), addrs, MonoDelta::FromMilliseconds(FLAGS_master_rpc_timeout_ms),
        &peer_entry);
    if (!s.ok()) {
      // In case of errors talking to the peer master,
      // fill in fields from our catalog best as we can.
      s = s.CloneAndPrepend(
        Format("Unable to get registration information for peer ($0) id ($1)",
              addrs, peer.permanent_uuid()));
      YB_LOG_EVERY_N_SECS(WARNING, 5) << "ListMasters: " << s;
      StatusToPB(s, peer_entry.mutable_error());
      peer_entry.mutable_instance_id()->set_permanent_uuid(peer.permanent_uuid());
      peer_entry.mutable_instance_id()->set_instance_seqno(0);
      auto reg = peer_entry.mutable_registration();
      reg->mutable_private_rpc_addresses()->CopyFrom(peer.last_known_private_addr());
      reg->mutable_broadcast_addresses()->CopyFrom(peer.last_known_broadcast_addr());
    }
    masters->push_back(peer_entry);
  }

  return Status::OK();
}

Status Master::InformRemovedMaster(const HostPortPB& hp_pb) {
  HostPort hp(hp_pb.host(), hp_pb.port());
  MasterClusterProxy proxy(proxy_cache_.get(), hp);
  RemovedMasterUpdateRequestPB req;
  RemovedMasterUpdateResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(FLAGS_master_rpc_timeout_ms));
  RETURN_NOT_OK(proxy.RemovedMasterUpdate(req, &resp, &controller));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

scoped_refptr<Histogram> Master::GetMetric(
    const std::string& metric_identifier, MasterMetricType type, const std::string& description) {
  std::string temp_metric_identifier = Format("$0_$1", metric_identifier,
      (type == TaskMetric ? "Task" : "Attempt"));
  EscapeMetricNameForPrometheus(&temp_metric_identifier);
  {
    std::lock_guard lock(master_metrics_mutex_);
    std::map<std::string, scoped_refptr<Histogram>>* master_metrics_ptr = master_metrics();
    auto it = master_metrics_ptr->find(temp_metric_identifier);
    if (it == master_metrics_ptr->end()) {
      std::unique_ptr<HistogramPrototype> histogram = std::make_unique<OwningHistogramPrototype>(
          "server", temp_metric_identifier, description, yb::MetricUnit::kMicroseconds,
          description, yb::MetricLevel::kInfo, 0, 10000000, 2);
      scoped_refptr<Histogram> temp =
          metric_entity()->FindOrCreateMetric<Histogram>(std::move(histogram));
      (*master_metrics_ptr)[temp_metric_identifier] = temp;
      return temp;
    }
    return it->second;
  }
}

Status Master::GoIntoShellMode() {
  maintenance_manager_->Shutdown();
  RETURN_NOT_OK(catalog_manager_impl()->GoIntoShellMode());
  return Status::OK();
}

scoped_refptr<MetricEntity> Master::metric_entity_cluster() {
  return metric_entity_cluster_;
}

client::LocalTabletFilter Master::CreateLocalTabletFilter() {
  return client::LocalTabletFilter();
}

CatalogManagerIf* Master::catalog_manager() const {
  return CHECK_NOTNULL(catalog_manager_impl());
}

XClusterManagerIf* Master::xcluster_manager() const {
  return catalog_manager_->GetXClusterManager();
}

XClusterManager* Master::xcluster_manager_impl() const {
  return catalog_manager_->GetXClusterManagerImpl();
}

SysCatalogTable& Master::sys_catalog() const {
  return *catalog_manager_->sys_catalog();
}

TabletSplitManager& Master::tablet_split_manager() const {
  return *CHECK_NOTNULL(tablet_split_manager_.get());
}

PermissionsManager& Master::permissions_manager() {
  return *catalog_manager_->permissions_manager();
}

EncryptionManager& Master::encryption_manager() {
  return catalog_manager_->encryption_manager();
}

uint32_t Master::GetAutoFlagConfigVersion() const {
  return auto_flags_manager_->GetConfigVersion();
}

CloneStateManager& Master::clone_state_manager() const {
  return *CHECK_NOTNULL(clone_state_manager_.get());
}

MasterSnapshotCoordinator& Master::snapshot_coordinator() const {
  return *CHECK_NOTNULL(snapshot_coordinator_.get());
}


AutoFlagsConfigPB Master::GetAutoFlagsConfig() const { return auto_flags_manager_->GetConfig(); }

const std::shared_future<client::YBClient*>& Master::client_future() const {
  return async_client_init_->get_client_future();
}

const std::shared_future<client::YBClient*>& Master::cdc_state_client_future() const {
  return cdc_state_client_init_->get_client_future();
}

Status Master::get_ysql_db_oid_to_cat_version_info_map(
    const tserver::GetTserverCatalogVersionInfoRequestPB& req,
    tserver::GetTserverCatalogVersionInfoResponsePB *resp) const {
  DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
  // This function can only be called during initdb time.
  DbOidToCatalogVersionMap versions;
  // We do not use cache/fingerprint which is only used for filling heartbeat
  // response. The heartbeat mechanism is already subject to a heartbeat delay.
  // In other situation where we are not already subject to any delay, we want
  // the latest reading from the table pg_yb_catalog_version.
  RETURN_NOT_OK(catalog_manager_->GetYsqlAllDBCatalogVersions(
      false /* use_cache */, &versions, nullptr /* fingerprint */));
  if (req.size_only()) {
    resp->set_num_entries(narrow_cast<uint32_t>(versions.size()));
  } else {
    const auto db_oid = req.db_oid();
    // If db_oid is kInvalidOid, we ask for catalog version map for all databases. Otherwise
    // we only ask for catalog version info for given database.
    // We assume that during initdb:
    // (1) we only create databases, not drop databases;
    // (2) databases OIDs are allocated increasingly.
    // Based upon these assumptions, we can have a simple shm_index assignment algorithm by
    // doing shm_index++. As a result, a subsequent call to this function will return either
    // identical or a superset of the result of any previous calls. For example, if the first
    // call sees two DB oids [1, 16384], this function will return (1, 0), (16384, 1). If the
    // next call sees 3 DB oids [1, 16384, 16385], we return (1, 0), (16384, 1), (16385, 2)
    // which is a superset of the result of the first call. This is to ensure that the
    // shm_index assigned to a DB oid remains the same during the lifetime of the DB.
    int shm_index = 0;
    uint32_t current_db_oid = kInvalidOid;
    for (const auto& it : versions) {
      CHECK_LT(current_db_oid, it.first);
      current_db_oid = it.first;
      if (db_oid == kInvalidOid || db_oid == current_db_oid) {
        auto* entry = resp->add_entries();
        entry->set_db_oid(current_db_oid);
        entry->set_shm_index(shm_index);
        if (db_oid != kInvalidOid) {
          break;
        }
      }
      shm_index++;
    }
  }
  LOG(INFO) << "resp: " << resp->ShortDebugString();
  return Status::OK();
}

Status Master::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(DbServerBase::SetupMessengerBuilder(builder));

  secure_context_ = VERIFY_RESULT(rpc::SetupInternalSecureContext(
      options_.HostsString(), fs_manager_->GetDefaultRootDir(), builder));

  return Status::OK();
}

Status Master::ReloadKeysAndCertificates() {
  if (!secure_context_) {
    return Status::OK();
  }

  return rpc::ReloadSecureContextKeysAndCertificates(
      secure_context_.get(), fs_manager_->GetDefaultRootDir(), rpc::SecureContextType::kInternal,
      options_.HostsString());
}

std::string Master::GetCertificateDetails() {
  if(!secure_context_) return "";

  return secure_context_.get()->GetCertificateDetails();
}

void Master::WriteServerMetaCacheAsJson(JsonWriter *writer) {
  writer->StartObject();
  tserver::DbServerBase::WriteMainMetaCacheAsJson(writer);
  writer->EndObject();
}

} // namespace master
} // namespace yb
