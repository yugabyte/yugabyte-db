// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master_host/master_host.h"

#include <stdexcept>

#include "yb/client/client.h"

#include "yb/common/common_flags.h"
#include "yb/common/version_info.h"
#include "yb/common/version_info.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/casts.h"

#include "yb/master/catalog_entity_parser.h"
#include "yb/master/catalog_loaders.h"
#include "yb/master/master-path-handlers.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_auto_flags_manager.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_encryption.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_util.h"
#include "yb/master/snapshot_coordinator_context.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/sys_catalog_context.h"
#include "yb/master/sys_catalog_restore.h"
#include "yb/master/sys_catalog_writer.h"
#include "yb/master/encryption_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/server/async_client_initializer.h"
#include "yb/server/server_base.pb.h"
#include "yb/server/total_mem_watcher.h"
#include "yb/server/webserver.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/tserver/remote_bootstrap_service.h"
#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"
#include "yb/util/threadpool.h"
#include "yb/util/ulimit_util.h"

#include "master/master/src/ffi.rs.h"

DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(db_history_retention_pin_max_txn_age_sec);
DECLARE_bool(emergency_repair_mode);
DECLARE_bool(enable_db_history_retention_pins);
DECLARE_bool(enable_leader_failure_detection);
DECLARE_int64(inbound_rpc_memory_limit);
DECLARE_int32(master_consensus_svc_queue_length);
DECLARE_bool(master_join_existing_universe);
DECLARE_int32(master_remote_bootstrap_svc_queue_length);
DECLARE_int32(master_svc_queue_length);
DECLARE_int32(master_yb_client_default_timeout_ms);
DECLARE_int32(master_ysql_lease_svc_queue_length);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_string(webserver_authentication_domain);
DECLARE_string(webserver_password_file);
DECLARE_bool(webserver_redirect_http_to_https);

namespace yb::master_host {

using consensus::RaftConfigPB;
using consensus::StartRemoteBootstrapRequestPB;

namespace {

rust::Vec<uint8_t> ToRustBytes(const std::string& bytes) {
  rust::Vec<uint8_t> result;
  result.reserve(bytes.size());
  for (char c : bytes) {
    result.push_back(static_cast<uint8_t>(c));
  }
  return result;
}

void ThrowIfNotOk(const Status& status) {
  if (!status.ok()) {
    throw std::runtime_error(status.ToString());
  }
}

template <class Resp, class Action>
rust::Vec<uint8_t> PerformOnSysCatalogTablet(
    master::SysCatalogTable* sys_catalog, const char* name, const Action& action) {
  // Same responses as CatalogManager::PerformOnSysCatalogTablet.
  Resp resp;
  auto tablet_peer = sys_catalog->tablet_peer();
  auto tablet = tablet_peer ? tablet_peer->shared_tablet_maybe_null() : nullptr;
  if (!tablet) {
    StatusToPB(
        STATUS(NotFound, "The sys catalog tablet was not found."),
        resp.mutable_error()->mutable_status());
    resp.mutable_error()->set_code(master::MasterErrorPB::TABLET_NOT_RUNNING);
  } else {
    LOG(INFO) << name << " started";
    auto status = action(tablet);
    LOG(INFO) << name << " completed: " << status;
    if (!status.ok()) {
      StatusToPB(status, resp.mutable_error()->mutable_status());
      resp.mutable_error()->set_code(master::MasterErrorPB::INTERNAL_ERROR);
    }
  }
  return ToRustBytes(resp.SerializeAsString());
}

// CheckRespErrorOrSetUnknown (master_service_base-internal.h).
template <class Resp>
void SetErrorIfNotOk(const Status& status, Resp* resp) {
  if (status.ok() || resp->has_error()) {
    return;
  }
  const master::MasterError master_error(status);
  StatusToPB(status, resp->mutable_error()->mutable_status());
  resp->mutable_error()->set_code(
      master_error.value() == master::MasterErrorPB::Code()
          ? master::MasterErrorPB::UNKNOWN_ERROR : master_error.value());
}

template <class Req, class Resp, class F>
rust::Vec<uint8_t> HandleRequest(rust::Slice<const uint8_t> request, const F& f) {
  Req req;
  Resp resp;
  Status status;
  if (req.ParseFromArray(request.data(), narrow_cast<int>(request.size()))) {
    status = f(req, &resp);
  } else {
    status = STATUS_FORMAT(InvalidArgument, "Unparsable $0", req.GetTypeName());
  }
  SetErrorIfNotOk(status, &resp);
  return ToRustBytes(resp.SerializeAsString());
}

}  // namespace

class MasterHost::SysCatalogContextImpl : public master::SysCatalogContext {
 public:
  explicit SysCatalogContextImpl(MasterHost* host) : host_(*host) {}

  rpc::Messenger* Messenger() override { return host_.messenger(); }
  rpc::ProxyCache& ProxyCache() override { return host_.proxy_cache(); }
  const std::shared_future<client::YBClient*>& ClientFuture() const override {
    return host_.client_future();
  }
  FsManager* GetFsManager() override { return host_.fs_manager(); }
  server::Clock* GetClock() override { return host_.clock(); }
  const std::shared_ptr<MemTracker>& GetMemTracker() const override {
    return host_.mem_tracker();
  }
  MaintenanceManager* GetMaintenanceManager() override {
    return host_.maintenance_manager_.get();
  }
  const master::MasterOptions& Options() const override { return host_.opts_; }
  CloudInfoPB MakeCloudInfoPB() const override { return host_.MakeCloudInfoPB(); }
  Status GetRegistration(ServerRegistrationPB* reg, server::RpcOnly rpc_only) const override {
    return host_.GetRegistration(reg, rpc_only);
  }

  void SetMasterAddresses(std::shared_ptr<server::MasterAddresses> master_addresses) override {
    host_.opts_.SetMasterAddresses(std::move(master_addresses));
  }
  Status ResetMemoryState(const RaftConfigPB& config) override {
    return host_.ResetMemoryState(config);
  }
  Status InformRemovedMaster(const HostPortPB& hp_pb) override {
    // Only the leader informs removed peers, and this host never leads.
    return STATUS(NotSupported, "The Rust master does not lead in layer 1");
  }

  tablet::SnapshotCoordinator* SnapshotCoordinator() override {
    return host_.snapshot_coordinator_.get();
  }
  AutoFlagsManagerBase* AutoFlagsManager() override { return host_.auto_flags_manager_.get(); }
  docdb::HistoryCutoff AllowedHistoryCutoff(tablet::RaftGroupMetadata* metadata) override {
    return host_.AllowedHistoryCutoff(metadata);
  }

  scoped_refptr<master::TableInfo> GetTableInfo(const TableId& table_id) override {
    return nullptr;
  }
  Result<TableId> GetVersionSpecificCatalogTableId(const TableId& table_id) const override {
    return STATUS(NotSupported, "YSQL catalog reads are served by the master leader");
  }

 private:
  MasterHost& host_;
};

// The snapshot coordinator's context. LeaderTerm() is always -1, so the coordinator only runs
// what every master runs: Load, ApplyWritePair and the *Replicated apply hooks. The leader-only
// methods are unreachable and fail loudly if that ever changes.
class MasterHost::SnapshotContextImpl : public master::SnapshotCoordinatorContext {
 public:
  explicit SnapshotContextImpl(MasterHost* host) : host_(*host) {}

  master::TabletInfos GetTabletInfos(const std::vector<TabletId>& ids) override {
    return master::TabletInfos(ids.size());
  }
  master::AsyncTabletSnapshotOpPtr CreateAsyncTabletSnapshotOp(
      const master::TabletInfoPtr& tablet, const std::string& snapshot_id,
      tserver::TabletSnapshotOpRequestPB::Operation operation, const master::LeaderEpoch& epoch,
      master::TabletSnapshotOperationCallback callback) override {
    LOG(DFATAL) << "Snapshot tablet operations are only sent by the master leader";
    return nullptr;
  }
  void ScheduleTabletSnapshotOp(const master::AsyncTabletSnapshotOpPtr& operation) override {
    LOG(DFATAL) << "Snapshot tablet operations are only sent by the master leader";
  }
  Result<master::SysRowEntries> CollectEntriesForSnapshot(
      const google::protobuf::RepeatedPtrField<master::TableIdentifierPB>& tables,
      master::IncludeHiddenTables include_hidden_tables) override {
    return NotLeader();
  }
  Status RestoreSysCatalog(
      master::SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet, bool leader_mode,
      Status* complete_status) override {
    auto& sys_catalog = *host_.sys_catalog_;
    auto status = master::RestoreSysCatalogOnApply(
        restoration, tablet, sys_catalog.tablet_peer(), sys_catalog.doc_read_context(),
        host_.snapshot_coordinator_.get(), host_.clock(), /* leader_term= */ [] { return -1; },
        Format("T $0 P $1: ", master::kSysCatalogTabletId, host_.permanent_uuid()));
    // As in CatalogManager::RestoreSysCatalog: a validation failure is reported to the client
    // through the restoration, not as an apply failure.
    if (status.IsNotSupported()) {
      *complete_status = status;
      return Status::OK();
    }
    return status;
  }
  Status VerifyRestoredObjects(
      const std::unordered_map<std::string, master::SysRowEntryType>& objects,
      const google::protobuf::RepeatedPtrField<master::TableIdentifierPB>& tables) override {
    return NotLeader();
  }
  void CleanupHiddenObjects(
      const ScheduleMinRestoreTime& schedule_min_restore_time,
      const master::LeaderEpoch& epoch) override {}
  const Schema& schema() override { return host_.sys_catalog_->schema(); }
  Status Submit(std::unique_ptr<tablet::Operation> operation, int64_t leader_term) override {
    return NotLeader();
  }
  void PrepareRestore() override {
    LOG(DFATAL) << "Only the master leader prepares a restore";
  }
  rpc::Scheduler& Scheduler() override { return host_.messenger()->scheduler(); }
  int64_t LeaderTerm() override { return -1; }
  server::Clock* Clock() override { return host_.clock(); }
  Result<size_t> GetNumLiveTServersForActiveCluster() override { return NotLeader(); }
  Result<scoped_refptr<master::TableInfo>> GetTableById(const TableId& table_id) const override {
    return NotLeader();
  }
  void AddPendingBackFill(const TableId& id) override {}
  Status AdvanceOidCounters(const NamespaceId& namespace_id) override { return NotLeader(); }
  master::PitrCount pitr_count() const override { return host_.sys_catalog_->pitr_count(); }
  Result<master::SysRowEntries> CollectEntriesAsOfTime(
      const NamespaceId& namespace_id, master::CollectFlags flags, HybridTime read_time,
      CoarseTimePoint deadline) override {
    return NotLeader();
  }

 private:
  static Status NotLeader() {
    return STATUS(IllegalState, "Not supported on a follower-only Rust master");
  }

  MasterHost& host_;
};

class MasterHost::TabletPeerLookupImpl : public tserver::TabletPeerLookupIf {
 public:
  explicit TabletPeerLookupImpl(MasterHost* host) : host_(*host) {}

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override {
    return GetServingTablet(Slice(tablet_id));
  }

  // Same checks as CatalogManager::GetServingTablet.
  Result<tablet::TabletPeerPtr> GetServingTablet(const Slice& tablet_id) const override {
    if (!host_.sys_catalog_initialized_.load()) {
      return STATUS(ServiceUnavailable, "Sys catalog is not yet initialized");
    }
    if (host_.opts_.IsShellMode()) {
      return STATUS_FORMAT(
          NotFound, "In shell mode: no tablet_id $0 exists in the master", tablet_id);
    }
    auto peer = host_.sys_catalog_->tablet_peer();
    if (host_.sys_catalog_->tablet_id() == tablet_id && peer && peer->CheckRunning().ok()) {
      return peer;
    }
    return STATUS_FORMAT(
        NotFound, "no SysTable in the RUNNING state exists with tablet_id $0", tablet_id);
  }

  const NodeInstancePB& NodeInstance() const override { return host_.instance_pb(); }

  Status GetRegistration(ServerRegistrationPB* reg) const override {
    return host_.GetRegistration(reg, server::RpcOnly::kTrue);
  }

  Status StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) override {
    return host_.StartRemoteBootstrap(req);
  }

 private:
  MasterHost& host_;
};

MasterHost::MasterHost(const master::MasterOptions& opts, WebserverOptions public_web_opts)
    : DbServerBase("Master", opts, "yb.master", server::CreateMemTrackerForServer()),
      opts_(opts),
      sys_catalog_context_(std::make_unique<SysCatalogContextImpl>(this)),
      snapshot_context_(std::make_unique<SnapshotContextImpl>(this)),
      tablet_peer_lookup_(std::make_unique<TabletPeerLookupImpl>(this)),
      sys_catalog_(std::make_unique<master::SysCatalogTable>(
          sys_catalog_context_.get(), metric_registry_.get())),
      snapshot_coordinator_(std::make_unique<master::MasterSnapshotCoordinator>(
          snapshot_context_.get(), /* cm= */ nullptr, /* tablet_split_manager= */ nullptr)),
      auto_flags_manager_(std::make_unique<master::MasterAutoFlagsManager>(
          clock(), fs_manager(), /* master= */ nullptr)),
      encryption_manager_(std::make_unique<master::EncryptionManager>()),
      maintenance_manager_(
          std::make_shared<MaintenanceManager>(MaintenanceManager::DEFAULT_OPTIONS)),
      termination_monitor_(TerminationMonitor::Create()),
      public_web_opts_(std::move(public_web_opts)) {
  SetConnectionContextFactory(rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(
      FLAGS_inbound_rpc_memory_limit, mem_tracker()));
}

MasterHost::~MasterHost() {
  ShutdownHost();
}

std::unique_ptr<MasterHost> CreateMasterHost() {
  // This peer must never become the sys catalog leader: nothing above the engine can lead yet.
  // Without failure detection it never starts an election on its own; ElectedAsLeader() steps it
  // down if an explicit leader transfer elects it anyway.
  if (FLAGS_enable_leader_failure_detection) {
    LOG(INFO) << "Disabling enable_leader_failure_detection: the Rust master is follower-only";
    FLAGS_enable_leader_failure_detection = false;
  }
  if (FLAGS_emergency_repair_mode) {
    throw std::runtime_error("emergency_repair_mode needs a master leader; use the C++ master");
  }
  if (!FLAGS_webserver_password_file.empty() || !FLAGS_webserver_authentication_domain.empty() ||
      FLAGS_webserver_redirect_http_to_https) {
    // Fail closed: the Rust web server implements neither web authentication nor the redirect.
    throw std::runtime_error(
        "webserver_password_file, webserver_authentication_domain and "
        "webserver_redirect_http_to_https are not supported by the Rust master yet");
  }
  InitCommonFlags();
  auto opts = master::MasterOptions::CreateMasterOptions();
  if (!opts.ok()) {
    throw std::runtime_error(opts.status().ToString());
  }
  // Rust serves the configured web port and TLS; the C++ Webserver moves behind it, to loopback.
  auto public_web_opts = opts->webserver_opts;
  auto& web = opts->webserver_opts;
  web.bind_interface = "127.0.0.1";
  web.port = 0;
  web.certificate_file.clear();
  web.private_key_file.clear();
  web.private_key_password.clear();
  return std::make_unique<MasterHost>(*opts, std::move(public_web_opts));
}

void MasterHost::InitHost() {
  ThrowIfNotOk(DoInit());
}

Status MasterHost::DoInit() {
  RETURN_NOT_OK(ThreadPoolBuilder("leadership").set_max_threads(1).Build(&leadership_pool_));
  RETURN_NOT_OK(DbServerBase::Init());
  RETURN_NOT_OK(fs_manager_->ListTabletIds(CleanupTemporaryFiles::kTrue));

  RegisterWebPages();
  return Status::OK();
}

// The pages MasterPathHandlers::Register adds, as a master that is not the leader serves them:
// the leader's page for every page that needs the catalog, the rest from the master list. Same
// order, titles and nav bar entries.
void MasterHost::RegisterWebPages() {
  struct LeaderPage {
    const char* path;
    const char* alias;
    bool is_styled;
    bool is_on_nav_bar;
    const char* icon;
  };
  static const LeaderPage kLeaderPages[] = {
      {"/", "Home", true, true, "fa fa-home"},
      {"/tablet-servers", "Tablet Servers", true, true, "fa fa-server"},
      {"/tablet-server-clocks", "Tablet Server Clocks", true, false, ""},
      {"/tables", "Tables", true, true, "fa fa-table"},
      {"/namespaces", "Namespaces", true, true, "fa fa-table"},
      {"/table", "", true, false, ""},
      {"/cluster-config", "Cluster Config", true, false, ""},
      {"/xcluster", "xCluster", true, false, ""},
      {"/tasks", "Tasks", true, false, ""},
      {"/tablet-replication", "Tablet Replication Health", true, false, ""},
      {"/load-distribution", "Cluster Balancer View", true, false, ""},
      {"/stateful-services", "Stateful Services", true, false, ""},
      {"/ObjectLockManager", "Object Lock Manager", true, false, ""},
      {"/api/v1/tablet-servers", "Tserver Statuses", false, false, ""},
      {"/api/v1/health-check", "Cluster Health Check", false, false, ""},
      {"/api/v1/tablet-replication", "Tablet Replication Health", false, false, ""},
      {"/api/v1/tablet-under-replication", "Tablet UnderReplication Status", false, false, ""},
      {"/dump-entities", "Dump Entities", false, false, ""},
      {"/api/v1/namespaces", "Namespaces", false, false, ""},
      {"/api/v1/table", "Table Info", false, false, ""},
      {"/api/v1/tables", "Tables", false, false, ""},
      {"/api/v1/cluster-config", "Cluster Config JSON", false, false, ""},
      {"/api/v1/xcluster", "xCluster", false, false, ""},
      {"/api/v1/stateful-services", "Stateful Services", false, false, ""},
  };
  for (const auto& page : kLeaderPages) {
    web_server_->RegisterPathHandler(
        page.path, page.alias,
        [this](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
          ServerRegistrationPB local_reg;
          auto status = GetMasterRegistration(&local_reg);
          master::RedirectToLeader(
              ListMastersForWeb(),
              status.ok() ? Result<ServerRegistrationPB>(std::move(local_reg))
                          : Result<ServerRegistrationPB>(status),
              req, resp);
        },
        page.is_styled, page.is_on_nav_bar, page.icon);
  }

  web_server_->RegisterPathHandler(
      "/masters", "Masters",
      [this](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
        master::RenderMastersPage(ListMastersForWeb(), permanent_uuid(), &resp->output);
      },
      /* is_styled= */ true, /* is_on_nav_bar= */ false);
  web_server_->RegisterPathHandler(
      "/api/v1/is-leader", "Leader Check",
      [](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
        // MasterPathHandlers::HandleCheckIfLeader on a master that is not the leader.
        JsonWriter jw(&resp->output, JsonWriter::COMPACT);
        jw.StartObject();
        resp->code = 503;
      },
      /* is_styled= */ false, /* is_on_nav_bar= */ false);
  web_server_->RegisterPathHandler(
      "/api/v1/masters", "Master Statuses",
      [this](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
        master::RenderMastersJson(ListMastersForWeb(), &resp->output);
      },
      /* is_styled= */ false, /* is_on_nav_bar= */ false);
  web_server_->RegisterPathHandler(
      "/api/v1/version", "YB Version Information",
      [](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
        JsonWriter jw(&resp->output, JsonWriter::PRETTY);
        VersionInfoPB version_info;
        VersionInfo::GetVersionInfoPB(&version_info);
        jw.Protobuf(version_info);
      },
      /* is_styled= */ false, /* is_on_nav_bar= */ false);
  web_server_->RegisterPathHandler(
      "/api/v1/meta-cache", "MetaCache",
      [this](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
        JsonWriter writer(&resp->output, JsonWriter::COMPACT);
        WriteServerMetaCacheAsJson(&writer);
      },
      /* is_styled= */ false, /* is_on_nav_bar= */ false);
}

Result<std::vector<ServerEntryPB>> MasterHost::ListMastersForWeb() const {
  SCHECK(web_master_list_, IllegalState, "The web server is not set up");
  auto bytes = (*web_master_list_)->ListMasters();
  master::ListMastersResponsePB resp;
  SCHECK(resp.ParseFromArray(bytes.data(), narrow_cast<int>(bytes.size())), Corruption,
         "Unparsable ListMastersResponsePB from Rust");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return std::vector<ServerEntryPB>(resp.masters().begin(), resp.masters().end());
}

// Flag values; invalid UTF-8 would make rust::String throw where cxx cannot catch it.
rust::String MasterHost::PublicWebInterface() const {
  return rust::String::lossy(public_web_opts_.bind_interface);
}

uint16_t MasterHost::PublicWebPort() const { return public_web_opts_.port; }

rust::String MasterHost::WebCertificateFile() const {
  return rust::String::lossy(public_web_opts_.certificate_file);
}

rust::String MasterHost::WebPrivateKeyFile() const {
  return rust::String::lossy(public_web_opts_.private_key_file);
}

rust::Vec<uint8_t> MasterHost::WebPrivateKeyPassword() const {
  return ToRustBytes(public_web_opts_.private_key_password);
}

rust::String MasterHost::WebSslCiphers() const {
  return rust::String::lossy(public_web_opts_.ssl_ciphers);
}

void MasterHost::SetWebServer(
    const rust::Vec<rust::String>& bound_ips, uint16_t bound_port,
    rust::Box<WebMasterList> master_list) {
  for (const auto& ip : bound_ips) {
    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(std::string(ip), ec);
    if (ec) {
      throw std::runtime_error(Format("Bad web server address $0: $1", ip, ec.message()));
    }
    public_web_endpoints_.emplace_back(address, bound_port);
  }
  web_master_list_.emplace(std::move(master_list));
}

uint16_t MasterHost::LoopbackWebPort() const {
  std::vector<Endpoint> addrs;
  ThrowIfNotOk(web_server()->GetBoundAddresses(&addrs));
  if (addrs.empty()) {
    throw std::runtime_error("The C++ web server is not bound");
  }
  return addrs[0].port();
}

// RpcAndWebServerBase::GetRegistration and GetStatusPB, for the Rust web server.
Status MasterHost::GetRegistration(ServerRegistrationPB* reg, server::RpcOnly rpc_only) const {
  RETURN_NOT_OK(DbServerBase::GetRegistration(reg, server::RpcOnly::kTrue));
  if (!rpc_only) {
    SCHECK(!public_web_endpoints_.empty(), IllegalState, "The Rust web server is not set up");
    const HostPort input(
        public_web_opts_.bind_interface.empty() ? "0.0.0.0" : public_web_opts_.bind_interface,
        public_web_opts_.port);
    if (IsWildcardAddress(input.host()) || input.port() == 0) {
      RETURN_NOT_OK(AddHostPortPBs(public_web_endpoints_, reg->mutable_http_addresses()));
    } else {
      HostPortsToPBs({input}, reg->mutable_http_addresses());
    }
  }
  return Status::OK();
}

void MasterHost::GetStatusPB(server::ServerStatusPB* status) const {
  RpcServerBase::GetStatusPB(status);
  for (const auto& endpoint : public_web_endpoints_) {
    auto* hp = status->add_bound_http_addresses();
    hp->set_host(endpoint.address().to_string());
    hp->set_port(endpoint.port());
  }
}

Status MasterHost::InitFlags(rpc::Messenger* messenger) {
  // Same as Master::InitFlags: a new master without a sys catalog loads its AutoFlags config from
  // the leader once it is added to the quorum.
  const bool is_shell_mode_if_new =
      FLAGS_master_join_existing_universe || !opts_.AreMasterAddressesProvided();
  RETURN_NOT_OK(auto_flags_manager_->Init(
      messenger,
      [this] { return fs_manager_->LookupTablet(master::kSysCatalogTabletId); },
      is_shell_mode_if_new));
  return RpcAndWebServerBase::InitFlags(messenger);
}

void MasterHost::StartHost() {
  ThrowIfNotOk(DoStart());
}

Status MasterHost::DoStart() {
  // As master_main.cc does around Master::Start.
  UlimitUtil::InitUlimits();
  LOG(INFO) << "ulimit cur(max)..." << UlimitUtil::GetUlimitInfo();
  RETURN_NOT_OK(maintenance_manager_->Init());
  RETURN_NOT_OK(RegisterService(
      FLAGS_master_consensus_svc_queue_length,
      std::make_shared<tserver::ConsensusServiceImpl>(metric_entity(), tablet_peer_lookup_.get()),
      rpc::ServicePriority::kHigh));
  RETURN_NOT_OK(RegisterService(
      FLAGS_master_remote_bootstrap_svc_queue_length,
      std::make_shared<tserver::RemoteBootstrapServiceImpl>(
          fs_manager_.get(), tablet_peer_lookup_.get(), metric_entity(), opts_.MakeCloudInfoPB(),
          &proxy_cache())));
  RETURN_NOT_OK(DbServerBase::Start());

  auto reg = std::make_unique<ServerRegistrationPB>();
  RETURN_NOT_OK(GetRegistration(reg.get()));
  registration_.reset(reg.release());

  RETURN_NOT_OK(sys_catalog_->Start(Bind(&MasterHost::ElectedAsLeader, Unretained(this))));
  sys_catalog_started_ = true;
  RETURN_NOT_OK(OpenSysCatalog());
  sys_catalog_initialized_ = true;

  RETURN_NOT_OK(Thread::Create(
      "master_host", "pin_refresh", &MasterHost::RefreshHistoryRetentionPinLoop, this,
      &pin_refresh_thread_));
  // Terminates the process past its memory limit, as in master_main.cc.
  total_mem_watcher_ = server::TotalMemWatcher::Create();
  RETURN_NOT_OK(Thread::Create(
      "total_mem_watcher", "loop",
      [this] {
        total_mem_watcher_->MemoryMonitoringLoop([this] { termination_monitor_->Terminate(); });
      },
      &total_mem_watcher_thread_));
  google::FlushLogFiles(google::INFO);
  return Status::OK();
}

// The non-leader half of CatalogManager::InitSysCatalogAsync.
Status MasterHost::OpenSysCatalog() {
  Status s = sys_catalog_->Load(fs_manager());
  if (s.ok() || !s.IsNotFound()) {
    tablet_exists_ = s.ok();
    return s;
  }
  LOG(INFO) << "Did not find previous SysCatalogTable data on disk. " << s;
  if (FLAGS_master_join_existing_universe || !opts_.AreMasterAddressesProvided()) {
    opts_.SetShellMode(true);
    LOG(INFO) << "Starting master in shell mode.";
    return Status::OK();
  }
  // Creating a universe makes this master its first leader.
  return STATUS(
      NotSupported,
      "The Rust master cannot create a new universe yet. Start it on an existing master's data, "
      "or in shell mode (no --master_addresses, or --master_join_existing_universe) and add it "
      "with change_master_config.");
}

Status MasterHost::ElectedAsLeader() {
  // Runs inside the consensus state-change callback, so the step-down happens on another thread.
  return leadership_pool_->SubmitFunc([this] {
    auto peer = sys_catalog_->tablet_peer();
    if (!peer) {
      return;
    }
    auto consensus = peer->GetConsensus();
    if (!consensus.ok()) {
      return;
    }
    LOG(WARNING) << "The Rust master was elected sys catalog leader; stepping down";
    // A step-down can be refused (for example LEADER_NOT_READY_TO_STEP_DOWN while a new master is
    // still a PRE_VOTER). The C++ followers do not elect anyone while this peer heartbeats them, so
    // keep trying until it is no longer leader.
    while ((*consensus)->role() == PeerRole::LEADER) {
      consensus::LeaderStepDownRequestPB req;
      req.set_tablet_id(master::kSysCatalogTabletId);
      req.set_dest_uuid(permanent_uuid());
      consensus::LeaderStepDownResponsePB resp;
      auto status = (*consensus)->StepDown(&req, &resp);
      if (status.ok() && !resp.has_error()) {
        return;
      }
      LOG(WARNING) << "Step-down failed, retrying: " << status << " " << resp.ShortDebugString();
      if (stop_latch_.WaitFor(MonoDelta::FromSeconds(1))) {
        return;
      }
    }
  });
}

Status MasterHost::ResetMemoryState(const RaftConfigPB& config) {
  // Same as Master::ResetMemoryState.
  LOG(INFO) << "Memory state set to config: " << config.ShortDebugString();
  auto master_addr = std::make_shared<server::MasterAddresses>();
  for (const auto& peer : config.peers()) {
    master_addr->push_back({HostPortFromPB(DesiredHostPort(peer, opts_.MakeCloudInfoPB()))});
  }
  opts_.SetMasterAddresses(std::move(master_addr));
  return Status::OK();
}

docdb::HistoryCutoff MasterHost::AllowedHistoryCutoff(tablet::RaftGroupMetadata* metadata) {
  // The follower half of CatalogManager::AllowedHistoryCutoffProvider: a follower has no tserver
  // heartbeats, so it holds the pg catalog back to the pin the leader published.
  auto cutoff = snapshot_coordinator_->AllowedHistoryCutoffProvider(metadata);
  const auto now = clock()->Now();
  const auto retention_sec =
      ANNOTATE_UNPROTECTED_READ(FLAGS_timestamp_syscatalog_history_retention_interval_sec);
  if (retention_sec) {
    const auto allowed = now.AddSeconds(-retention_sec);
    cutoff.MakeAtMost({allowed, allowed});
  }
  cutoff.MakeAtMost({metadata->cdc_sdk_safe_time(), metadata->cdc_sdk_safe_time()});
  if (FLAGS_enable_db_history_retention_pins) {
    auto pin = ysql_history_retention_pin_.ysql_pin();
    if (pin.is_valid()) {
      pin.MakeAtLeast(now.AddSeconds(-FLAGS_db_history_retention_pin_max_txn_age_sec));
      cutoff.cotables_cutoff_ht.MakeAtMost(pin);
    }
  }
  return cutoff;
}

void MasterHost::RefreshHistoryRetentionPinLoop() {
  // CatalogManager's background thread refreshes the published pin on followers at this interval.
  const auto interval = MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms);
  do {
    if (FLAGS_enable_db_history_retention_pins && !opts_.IsShellMode()) {
      WARN_NOT_OK(
          sys_catalog_->Load<master::HistoryRetentionPinLoader>(
              "ysql history retention pin", ysql_history_retention_pin_),
          "Failed to refresh the ysql catalog history retention pin");
    }
  } while (!stop_latch_.WaitFor(interval));
}

// Same steps as CatalogManager::StartRemoteBootstrap, minus the CatalogManager background tasks.
Status MasterHost::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  const TabletId& tablet_id = req.tablet_id();
  std::unique_lock lock(remote_bootstrap_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return STATUS_FORMAT(
        AlreadyPresent, "Remote bootstrap of tablet $0 already in progress", tablet_id);
  }
  if (!opts_.IsShellMode()) {
    return STATUS(IllegalState, "Cannot bootstrap a master which is not in shell mode.");
  }
  LOG(INFO) << "Starting remote bootstrap: " << req.ShortDebugString();

  ServerRegistrationPB tablet_leader_peer_conn_info;
  if (!req.is_served_by_tablet_leader()) {
    *tablet_leader_peer_conn_info.mutable_broadcast_addresses() =
        req.tablet_leader_broadcast_addr();
    *tablet_leader_peer_conn_info.mutable_private_rpc_addresses() =
        req.tablet_leader_private_addr();
    *tablet_leader_peer_conn_info.mutable_cloud_info() = req.tablet_leader_cloud_info();
  }
  const auto bootstrap_peer_addr = HostPortFromPB(DesiredHostPort(
      req.bootstrap_source_broadcast_addr(), req.bootstrap_source_private_addr(),
      req.bootstrap_source_cloud_info(), opts_.MakeCloudInfoPB()));
  RETURN_NOT_OK(auto_flags_manager_->LoadFromMasterLeader({{bootstrap_peer_addr}}));

  const auto& bootstrap_peer_uuid = req.bootstrap_source_peer_uuid();
  const int64_t leader_term = req.caller_term();
  const auto& uuid = fs_manager()->uuid();

  std::shared_ptr<tablet::TabletPeer> old_tablet_peer;
  tablet::RaftGroupMetadataPtr meta;
  bool replacing_tablet = false;
  if (tablet_exists_) {
    old_tablet_peer = sys_catalog_->tablet_peer();
    if (old_tablet_peer) {
      meta = old_tablet_peer->tablet_metadata();
      replacing_tablet = true;
    }
  }
  if (replacing_tablet) {
    RETURN_NOT_OK(tserver::HandleReplacingStaleTablet(
        meta, old_tablet_peer, tablet_id, uuid, leader_term));
  }

  LOG(INFO) << "Initiating remote bootstrap from peer " << bootstrap_peer_uuid << " ("
            << bootstrap_peer_addr << ").";
  auto rb_client = std::make_unique<tserver::RemoteBootstrapClient>(tablet_id, fs_manager());
  if (replacing_tablet) {
    RETURN_NOT_OK(rb_client->SetTabletToReplace(meta, leader_term));
  }
  RETURN_NOT_OK(rb_client->Start(
      bootstrap_peer_uuid, &proxy_cache(), bootstrap_peer_addr, tablet_leader_peer_conn_info,
      req.has_pending_config_op_id() ? OpId::FromPB(req.pending_config_op_id()) : OpId(),
      &meta));
  sys_catalog_->SetupTabletPeer(meta);
  tablet_exists_ = true;

  TOMBSTONE_NOT_OK(
      rb_client->FetchAll(sys_catalog_->tablet_peer()->status_listener()), meta, uuid,
      Format("Remote bootstrap: Unable to fetch data from remote peer $0 ($1)",
             bootstrap_peer_uuid, bootstrap_peer_addr),
      nullptr);
  TOMBSTONE_NOT_OK(
      rb_client->Finish(), meta, uuid, "Remote bootstrap: Failed calling Finish()", nullptr);
  RETURN_NOT_OK(tserver::ShutdownAndTombstoneTabletPeerNotOk(
      sys_catalog_->OpenTablet(meta), sys_catalog_->tablet_peer(), meta, uuid,
      "Remote bootstrap: Failed opening sys catalog"));

  // CatalogManager::UpdateMastersListInMemoryAndDisk.
  auto consensus = VERIFY_RESULT(sys_catalog_->tablet_peer()->GetConsensus());
  const auto cstate = consensus->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED);
  SCHECK(cstate.has_config(), NotFound, "No Raft config found");
  RETURN_NOT_OK(sys_catalog_->ConvertConfigToMasterAddresses(cstate.config()));
  RETURN_NOT_OK(sys_catalog_->CreateAndFlushConsensusMeta(
      fs_manager(), cstate.config(), cstate.current_term()));

  opts_.SetShellMode(false);
  // Only after leaving shell mode: before that GetServingTablet fails and the config change that
  // promotes this peer cannot reach it.
  auto status = rb_client->VerifyChangeRoleSucceeded(consensus);
  if (!status.ok()) {
    LOG(WARNING) << "Remote bootstrap finished. Failed calling VerifyChangeRoleSucceeded: "
                 << status;
  } else {
    LOG(INFO) << "Remote bootstrap finished successfully";
  }
  LOG(INFO) << "Master completed remote bootstrap and is out of shell mode.";
  return Status::OK();
}

void MasterHost::WaitForTermination() const {
  termination_monitor_->WaitForTermination();
}

std::unique_ptr<MasterInfoHandle> MasterHost::InfoHandle() {
  return std::make_unique<MasterInfoHandle>(this);
}

void MasterHost::ShutdownHost() {
  if (stop_latch_.count() == 0) {
    return;
  }
  LOG(INFO) << "Rust master host shutting down...";
  stop_latch_.CountDown();
  if (pin_refresh_thread_) {
    CHECK_OK(ThreadJoiner(pin_refresh_thread_.get()).Join());
  }
  if (total_mem_watcher_thread_) {
    total_mem_watcher_->Shutdown();
    CHECK_OK(ThreadJoiner(total_mem_watcher_thread_.get()).Join());
  }
  maintenance_manager_->Shutdown();
  if (async_client_init_) {
    async_client_init_->Shutdown();
  }
  RpcAndWebServerBase::Shutdown();
  if (leadership_pool_) {
    leadership_pool_->Shutdown();
  }
  snapshot_coordinator_->Shutdown();
  // SysCatalogTable creates its thread pools in Start(); CompleteShutdown assumes they exist.
  if (sys_catalog_started_) {
    sys_catalog_->StartShutdown();
    sys_catalog_->CompleteShutdown();
  }
  LOG(INFO) << "Rust master host shutdown complete.";
}

rust::Vec<uint8_t> MasterHost::InstancePB() const {
  return ToRustBytes(instance_pb().SerializeAsString());
}

Status MasterHost::GetMasterRegistration(ServerRegistrationPB* reg) const {
  auto* registration = registration_.get();
  if (!registration) {
    return STATUS(ServiceUnavailable, "Master startup not complete");
  }
  reg->CopyFrom(*registration);
  return Status::OK();
}

rust::Vec<uint8_t> MasterHost::RegistrationPB() const {
  ServerRegistrationPB reg;
  ThrowIfNotOk(GetMasterRegistration(&reg));
  return ToRustBytes(reg.SerializeAsString());
}

rust::Vec<uint8_t> MasterHost::CommittedConsensusStatePB() const {
  auto peer = sys_catalog_->tablet_peer();
  if (!peer) {
    throw std::runtime_error("Sys catalog tablet peer not initialized");
  }
  auto consensus = peer->GetConsensus();
  if (!consensus.ok()) {
    throw std::runtime_error(consensus.status().ToString());
  }
  return ToRustBytes(
      (*consensus)->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED).SerializeAsString());
}

int32_t MasterHost::RaftRole() const {
  // As CatalogManager::Role.
  if (!sys_catalog_initialized_.load() || opts_.IsShellMode()) {
    return PeerRole::NON_PARTICIPANT;
  }
  auto peer = sys_catalog_->tablet_peer();
  auto consensus = peer ? peer->GetConsensus() : STATUS(IllegalState, "No tablet peer");
  return consensus.ok() ? (*consensus)->role() : PeerRole::NON_PARTICIPANT;
}

bool MasterHost::IsSysCatalogInitialized() const { return sys_catalog_initialized_.load(); }

bool MasterHost::IsInShellMode() const { return opts_.IsShellMode(); }

rust::Vec<uint8_t> MasterHost::FlushSysCatalogResponse() const {
  return PerformOnSysCatalogTablet<master::FlushSysCatalogResponsePB>(
      sys_catalog_.get(), "FlushSysCatalog", [](const tablet::TabletPtr& tablet) {
        return tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kSysCatalogFlush);
      });
}

rust::Vec<uint8_t> MasterHost::CompactSysCatalogResponse() const {
  return PerformOnSysCatalogTablet<master::CompactSysCatalogResponsePB>(
      sys_catalog_.get(), "CompactSysCatalog", [](const tablet::TabletPtr& tablet) {
        return tablet->ForceManualRocksDBCompact();
      });
}

rust::Vec<uint8_t> MasterHost::HandleOnHost(
    rust::Str method, rust::Slice<const uint8_t> request) {
  const std::string name(method);
  if (name == "RemovedMasterUpdate") {
    return HandleRequest<master::RemovedMasterUpdateRequestPB,
                         master::RemovedMasterUpdateResponsePB>(
        request, [this](const auto&, auto*) { return GoIntoShellMode(); });
  }
  if (name == "CheckMasterTabletHealth") {
    // TabletHealthManager::CheckMasterTabletHealth.
    return HandleRequest<master::CheckMasterTabletHealthRequestPB,
                         master::CheckMasterTabletHealthResponsePB>(
        request, [this](const auto&, auto* resp) -> Status {
          auto peer = sys_catalog_->tablet_peer();
          SCHECK(peer, IllegalState, "Could not get sys catalog tablet consensus");
          auto consensus = VERIFY_RESULT_PREPEND(
              peer->GetRaftConsensus(), "Could not get sys catalog tablet consensus");
          const auto role = consensus->role();
          resp->set_role(role);
          if (role != PeerRole::LEADER) {
            resp->set_follower_lag_ms(consensus->follower_lag_ms());
          }
          return Status::OK();
        });
  }
  if (name == "GetUniverseKeyRegistry") {
    return HandleRequest<master::GetUniverseKeyRegistryRequestPB,
                         master::GetUniverseKeyRegistryResponsePB>(
        request, [this](const auto& req, auto* resp) {
          return encryption_manager_->GetUniverseKeyRegistry(&req, resp);
        });
  }
  if (name == "AddUniverseKeys") {
    return HandleRequest<master::AddUniverseKeysRequestPB, master::AddUniverseKeysResponsePB>(
        request, [this](const auto& req, auto* resp) {
          return encryption_manager_->AddUniverseKeys(&req, resp);
        });
  }
  if (name == "HasUniverseKeyInMemory") {
    return HandleRequest<master::HasUniverseKeyInMemoryRequestPB,
                         master::HasUniverseKeyInMemoryResponsePB>(
        request, [this](const auto& req, auto* resp) {
          return encryption_manager_->HasUniverseKeyInMemory(&req, resp);
        });
  }
  if (name == "DumpSysCatalogEntries") {
    // CatalogManager::DumpSysCatalogEntries and FetchFromSysCatalog.
    return HandleRequest<master::DumpSysCatalogEntriesRequestPB,
                         master::DumpSysCatalogEntriesResponsePB>(
        request, [this](const auto& req, auto* resp) -> Status {
          SCHECK_PB_FIELDS_NOT_EMPTY(req, entry_type);
          auto peer = sys_catalog_->tablet_peer();
          SCHECK(peer, IllegalState, "The sys catalog tablet is not open");
          auto tablet = VERIFY_RESULT(peer->shared_tablet());
          const auto type = req.entry_type();
          const auto& filter = req.entity_id_filter();
          return master::EnumerateSysCatalog(
              tablet.get(), sys_catalog_->schema(), type,
              [&](const Slice& id, const Slice& data) -> Status {
                if (!filter.empty() && filter != id.ToString()) {
                  return Status::OK();
                }
                auto entry_pb = VERIFY_RESULT(master::SliceToCatalogEntityPB(type, data));
                auto* entry = resp->add_entries();
                entry->set_entry_type(type);
                entry->set_entity_id(id.ToString());
                entry->set_pb_debug_string(entry_pb->DebugString());
                return Status::OK();
              });
        });
  }
  if (name == "WriteSysCatalogEntry") {
    // CatalogManager::WriteSysCatalogEntry, which needs emergency_repair_mode; CreateMasterHost
    // refuses to start in it.
    return HandleRequest<master::WriteSysCatalogEntryRequestPB,
                         master::WriteSysCatalogEntryResponsePB>(
        request, [](const auto& req, auto*) -> Status {
          SCHECK_PB_FIELDS_NOT_EMPTY(req, entry_type);
          SCHECK_PB_FIELDS_SET(req, op_type);
          return STATUS(IllegalState, "Updating sys_catalog is only allowed in emergency repair mode");
        });
  }
  throw std::runtime_error("The host does not serve " + name);
}

// Master::GoIntoShellMode and CatalogManager::GoIntoShellMode.
Status MasterHost::GoIntoShellMode() {
  std::lock_guard lock(remote_bootstrap_mutex_);
  if (opts_.IsShellMode()) {
    return STATUS(IllegalState, "Master is already in shell mode.");
  }
  LOG(INFO) << "Starting going into shell mode.";
  opts_.SetShellMode(true);
  maintenance_manager_->Shutdown();
  RETURN_NOT_OK(sys_catalog_->GoIntoShellMode());
  tablet_exists_ = false;
  LOG(INFO) << "Done going into shell mode.";
  return Status::OK();
}

int32_t MasterHost::MasterServiceQueueLength() const { return FLAGS_master_svc_queue_length; }

int32_t MasterHost::YsqlLeaseServiceQueueLength() const {
  return FLAGS_master_ysql_lease_svc_queue_length;
}

Status MasterHost::RegisterRustService(
    size_t queue_limit, rpc::ServiceIfPtr service, rpc::ServicePriority priority) {
  return RegisterService(queue_limit, std::move(service), priority);
}

MonoDelta MasterHost::default_client_timeout() {
  return std::chrono::milliseconds(FLAGS_master_yb_client_default_timeout_ms);
}

void MasterHost::SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) {
  async_client_init->builder()
      .set_master_address_flag_name("master_addresses")
      .default_admin_operation_timeout(default_client_timeout())
      .AddMasterAddressSource([this] {
        std::vector<std::string> result;
        for (const auto& list : *opts_.GetMasterAddresses()) {
          for (const auto& hp : list) {
            result.push_back(hp.ToString());
          }
        }
        return result;
      });
}

client::LocalTabletFilter MasterHost::CreateLocalTabletFilter() {
  return client::LocalTabletFilter();
}

void MasterHost::WriteServerMetaCacheAsJson(JsonWriter* writer) {
  writer->StartObject();
  tserver::DbServerBase::WriteMainMetaCacheAsJson(writer);
  writer->EndObject();
}

const std::string& MasterHost::permanent_uuid() const { return fs_manager_->uuid(); }

// Same as Master: what /varz shows as this process's flags and AutoFlags. Flags declared in Rust
// are not in master_flags.xml, which is generated from yb-master.
Result<std::unordered_set<std::string>> MasterHost::GetAvailableAutoFlagsForServer() const {
  return auto_flags_manager_->GetAvailableAutoFlagsForServer();
}

Result<std::unordered_set<std::string>> MasterHost::GetFlagsForServer() const {
  return yb::GetFlagNamesFromXmlFile("master_flags.xml");
}

uint32_t MasterHost::GetAutoFlagConfigVersion() const {
  return auto_flags_manager_->GetConfigVersion();
}

Status MasterHost::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(DbServerBase::SetupMessengerBuilder(builder));
  secure_context_ = VERIFY_RESULT(rpc::SetupInternalSecureContext(
      options_.HostsString(), fs_manager_->GetDefaultRootDir(), builder));
  return Status::OK();
}

Status MasterHost::ReloadKeysAndCertificates() {
  if (!secure_context_) {
    return Status::OK();
  }
  return rpc::ReloadSecureContextKeysAndCertificates(
      secure_context_.get(), fs_manager_->GetDefaultRootDir(), rpc::SecureContextType::kInternal,
      options_.HostsString());
}

std::string MasterHost::GetCertificateDetails() {
  return secure_context_ ? secure_context_->GetCertificateDetails() : "";
}

}  // namespace yb::master_host
