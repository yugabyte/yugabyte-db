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

#include "yb/tserver/tablet_server.h"

#include <algorithm>
#include <limits>
#include <list>
#include <thread>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "yb/client/auto_flags_manager.h"
#include "yb/client/client.h"
#include "yb/client/client_fwd.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/universe_key_client.h"

#include "yb/common/common_flags.h"
#include "yb/common/wire_protocol.h"

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/server/rpc_server.h"
#include "yb/server/webserver.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/heartbeater.h"
#include "yb/tserver/heartbeater_factory.h"
#include "yb/tserver/metrics_snapshotter.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/remote_bootstrap_service.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver-path-handlers.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

using std::make_shared;
using std::shared_ptr;
using std::vector;
using std::string;
using yb::rpc::ServiceIf;
using yb::tablet::TabletPeer;

using namespace std::literals;
using namespace yb::size_literals;
using namespace std::placeholders;

DEFINE_int32(tablet_server_svc_num_threads, -1,
             "Number of RPC worker threads for the TS service. If -1, it is auto configured.");
TAG_FLAG(tablet_server_svc_num_threads, advanced);

DEFINE_int32(ts_admin_svc_num_threads, 10,
             "Number of RPC worker threads for the TS admin service");
TAG_FLAG(ts_admin_svc_num_threads, advanced);

DEFINE_int32(ts_consensus_svc_num_threads, -1,
             "Number of RPC worker threads for the TS consensus service. If -1, it is auto "
             "configured.");
TAG_FLAG(ts_consensus_svc_num_threads, advanced);

DEFINE_int32(ts_remote_bootstrap_svc_num_threads, 10,
             "Number of RPC worker threads for the TS remote bootstrap service");
TAG_FLAG(ts_remote_bootstrap_svc_num_threads, advanced);

DEFINE_int32(tablet_server_svc_queue_length, yb::tserver::TabletServer::kDefaultSvcQueueLength,
             "RPC queue length for the TS service.");
TAG_FLAG(tablet_server_svc_queue_length, advanced);

DEFINE_int32(ts_admin_svc_queue_length, 50,
             "RPC queue length for the TS admin service");
TAG_FLAG(ts_admin_svc_queue_length, advanced);

DEFINE_int32(ts_consensus_svc_queue_length, yb::tserver::TabletServer::kDefaultSvcQueueLength,
             "RPC queue length for the TS consensus service.");
TAG_FLAG(ts_consensus_svc_queue_length, advanced);

DEFINE_int32(ts_remote_bootstrap_svc_queue_length, 50,
             "RPC queue length for the TS remote bootstrap service");
TAG_FLAG(ts_remote_bootstrap_svc_queue_length, advanced);

DEFINE_int32(pg_client_svc_queue_length, yb::tserver::TabletServer::kDefaultSvcQueueLength,
             "RPC queue length for the Pg Client service.");
TAG_FLAG(pg_client_svc_queue_length, advanced);

DEFINE_bool(enable_direct_local_tablet_server_call,
            true,
            "Enable direct call to local tablet server");
TAG_FLAG(enable_direct_local_tablet_server_call, advanced);

DEFINE_string(redis_proxy_bind_address, "", "Address to bind the redis proxy to");
DEFINE_int32(redis_proxy_webserver_port, 0, "Webserver port for redis proxy");

DEFINE_string(cql_proxy_bind_address, "", "Address to bind the CQL proxy to");
DEFINE_int32(cql_proxy_webserver_port, 0, "Webserver port for CQL proxy");

DEFINE_string(pgsql_proxy_bind_address, "", "Address to bind the PostgreSQL proxy to");
DECLARE_int32(pgsql_proxy_webserver_port);

DEFINE_int64(inbound_rpc_memory_limit, 0, "Inbound RPC memory limit");

DEFINE_bool(tserver_enable_metrics_snapshotter, false, "Should metrics snapshotter be enabled");

DEFINE_test_flag(uint64, pg_auth_key, 0, "Forces an auth key for the postgres user when non-zero")

DECLARE_int32(num_concurrent_backfills_allowed);
DECLARE_int32(svc_queue_length_default);

constexpr int kTServerYbClientDefaultTimeoutMs = 60 * 1000;

DEFINE_int32(tserver_yb_client_default_timeout_ms, kTServerYbClientDefaultTimeoutMs,
             "Default timeout for the YBClient embedded into the tablet server that is used "
             "for distributed transactions.");

DEFINE_test_flag(bool, select_all_status_tablets, false, "");

DEFINE_NON_RUNTIME_bool(allow_encryption_at_rest, true,
                        "Whether or not to allow encryption at rest to be enabled. Toggling this "
                        "flag does not turn on or off encryption at rest, but rather allows or "
                        "disallows a user from enabling it on in the future.");

DEFINE_int32(
    get_universe_key_registry_backoff_increment_ms, 100,
    "Number of milliseconds added to the delay between retries of fetching the full universe key "
    "registry from master leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(get_universe_key_registry_backoff_increment_ms, stable);
TAG_FLAG(get_universe_key_registry_backoff_increment_ms, advanced);

DEFINE_int32(
    get_universe_key_registry_max_backoff_sec, 3,
    "Maximum number of seconds to delay between retries of fetching the full universe key registry "
    "from master leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(get_universe_key_registry_max_backoff_sec, stable);
TAG_FLAG(get_universe_key_registry_max_backoff_sec, advanced);

namespace yb {
namespace tserver {

TabletServer::TabletServer(const TabletServerOptions& opts)
    : DbServerBase("TabletServer", opts, "yb.tabletserver", server::CreateMemTrackerForServer()),
      fail_heartbeats_for_tests_(false),
      opts_(opts),
      auto_flags_manager_(new AutoFlagsManager("yb-tserver", fs_manager_.get())),
      tablet_manager_(new TSTabletManager(fs_manager_.get(), this, metric_registry())),
      path_handlers_(new TabletServerPathHandlers(this)),
      maintenance_manager_(new MaintenanceManager(MaintenanceManager::DEFAULT_OPTIONS)),
      master_config_index_(0) {
  SetConnectionContextFactory(rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(
      FLAGS_inbound_rpc_memory_limit, mem_tracker()));
  if (FLAGS_TEST_enable_db_catalog_version_mode) {
    ysql_db_catalog_version_index_used_ =
      std::make_unique<std::array<bool, TServerSharedData::kMaxNumDbCatalogVersions>>();
    ysql_db_catalog_version_index_used_->fill(false);
  }
  LOG(INFO) << "yb::tserver::TabletServer created at " << this;
  LOG(INFO) << "yb::tserver::TSTabletManager created at " << tablet_manager_.get();
}

TabletServer::~TabletServer() {
  Shutdown();
}

std::string TabletServer::ToString() const {
  return strings::Substitute("TabletServer : rpc=$0, uuid=$1",
                             yb::ToString(first_rpc_address()),
                             fs_manager_->uuid());
}

MonoDelta TabletServer::default_client_timeout() {
  return std::chrono::milliseconds(FLAGS_tserver_yb_client_default_timeout_ms);
}

void TabletServer::SetupAsyncClientInit(client::AsyncClientInitialiser* async_client_init) {
  // If enabled, creates a proxy to call this tablet server locally.
  if (FLAGS_enable_direct_local_tablet_server_call) {
    proxy_ = std::make_shared<TabletServerServiceProxy>(proxy_cache_.get(), HostPort());
    async_client_init->AddPostCreateHook(
        [proxy = proxy_, uuid = permanent_uuid(), tserver = this](client::YBClient* client) {
      client->SetLocalTabletServer(uuid, proxy, tserver);
    });
  }
}

Status TabletServer::ValidateMasterAddressResolution() const {
  return ResultToStatus(server::ResolveMasterAddresses(*opts_.GetMasterAddresses()));
}

Status TabletServer::UpdateMasterAddresses(const consensus::RaftConfigPB& new_config,
                                           bool is_master_leader) {
  shared_ptr<server::MasterAddresses> new_master_addresses;
  if (is_master_leader) {
    SetCurrentMasterIndex(new_config.opid_index());
    new_master_addresses = make_shared<server::MasterAddresses>();

    SetCurrentMasterIndex(new_config.opid_index());

    for (const auto& peer : new_config.peers()) {
      std::vector<HostPort> list;
      for (const auto& hp : peer.last_known_private_addr()) {
        list.push_back(HostPortFromPB(hp));
      }
      for (const auto& hp : peer.last_known_broadcast_addr()) {
        list.push_back(HostPortFromPB(hp));
      }
      new_master_addresses->push_back(std::move(list));
    }
  } else {
    new_master_addresses = make_shared<server::MasterAddresses>(*opts_.GetMasterAddresses());

    for (auto& list : *new_master_addresses) {
      std::sort(list.begin(), list.end());
    }

    for (const auto& peer : new_config.peers()) {
      std::vector<HostPort> list;
      for (const auto& hp : peer.last_known_private_addr()) {
        list.push_back(HostPortFromPB(hp));
      }
      for (const auto& hp : peer.last_known_broadcast_addr()) {
        list.push_back(HostPortFromPB(hp));
      }
      std::sort(list.begin(), list.end());
      bool found = false;
      for (const auto& existing : *new_master_addresses) {
        if (existing == list) {
          found = true;
          break;
        }
      }
      if (!found) {
        new_master_addresses->push_back(std::move(list));
      }
    }
  }

  LOG(INFO) << "Got new list of " << new_config.peers_size() << " masters at index "
            << new_config.opid_index() << " old masters = "
            << yb::ToString(opts_.GetMasterAddresses())
            << " new masters = " << yb::ToString(new_master_addresses) << " from "
            << (is_master_leader ? "leader." : "follower.");

  opts_.SetMasterAddresses(new_master_addresses);

  heartbeater_->set_master_addresses(new_master_addresses);

  return Status::OK();
}

Status TabletServer::Init() {
  CHECK(!initted_.load(std::memory_order_acquire));

  // Validate that the passed master address actually resolves.
  // We don't validate that we can connect at this point -- it should
  // be allowed to start the TS and the master in whichever order --
  // our heartbeat thread will loop until successfully connecting.
  RETURN_NOT_OK(ValidateMasterAddressResolution());

  RETURN_NOT_OK(DbServerBase::Init());

  RETURN_NOT_OK(path_handlers_->Register(web_server_.get()));

  log_prefix_ = Format("P $0: ", permanent_uuid());

  heartbeater_ = CreateHeartbeater(opts_, this);

  if (GetAtomicFlag(&FLAGS_allow_encryption_at_rest)) {
    // Create the encrypted environment that will allow users to enable encryption.
    std::vector<std::string> master_addresses;
    for (const auto& list : *opts_.GetMasterAddresses()) {
      for (const auto& hp : list) {
        master_addresses.push_back(hp.ToString());
      }
    }

    const auto delay_increment =
        MonoDelta::FromMilliseconds(FLAGS_get_universe_key_registry_backoff_increment_ms);
    const auto max_delay_time =
        MonoDelta::FromSeconds(FLAGS_get_universe_key_registry_max_backoff_sec);
    auto delay_time = delay_increment;

    uint32_t attempts = 1;
    auto start_time = CoarseMonoClock::Now();
    encryption::UniverseKeyRegistryPB universe_key_registry;
    while (true) {
      auto res = client::UniverseKeyClient::GetFullUniverseKeyRegistry(
          options_.HostsString(), JoinStrings(master_addresses, ","), *fs_manager());
      if (res.ok()) {
        universe_key_registry = *res;
        break;
      }
      auto total_time = std::to_string((CoarseMonoClock::Now() - start_time).count()) + "ms";
      LOG(WARNING) << "Getting full universe key registry from master Leader failed: '"
                   << res.status() << "'. Attempts: " << attempts << ", Total Time: " << total_time
                   << ". Retrying...";

      // Delay before retrying so that we don't accidentally DDoS the mater.
      // Time increases linearly by delay_increment up to max_delay.
      SleepFor(delay_time);
      delay_time = std::min(max_delay_time, delay_time + delay_increment);
      attempts++;
    }

    universe_key_manager_ = std::make_unique<encryption::UniverseKeyManager>();
    universe_key_manager_->SetUniverseKeyRegistry(universe_key_registry);
    rocksdb_env_ = NewRocksDBEncryptedEnv(DefaultHeaderManager(universe_key_manager_.get()));
    fs_manager()->SetEncryptedEnv(
        NewEncryptedEnv(DefaultHeaderManager(universe_key_manager_.get())));
  }

  if (FLAGS_tserver_enable_metrics_snapshotter) {
    metrics_snapshotter_.reset(new MetricsSnapshotter(opts_, this));
  }

  RETURN_NOT_OK_PREPEND(tablet_manager_->Init(),
                        "Could not init Tablet Manager");

  initted_.store(true, std::memory_order_release);

  auto bound_addresses = rpc_server()->GetBoundAddresses();
  if (!bound_addresses.empty()) {
    ServerRegistrationPB reg;
    RETURN_NOT_OK(GetRegistration(&reg, server::RpcOnly::kTrue));
    shared_object().SetHostEndpoint(bound_addresses.front(), PublicHostPort(reg).host());
  }

  // 5433 is kDefaultPort in src/yb/yql/pgwrapper/pg_wrapper.h.
  RETURN_NOT_OK(pgsql_proxy_bind_address_.ParseString(FLAGS_pgsql_proxy_bind_address, 5433));
  if (PREDICT_FALSE(FLAGS_TEST_pg_auth_key != 0)) {
    shared_object().SetPostgresAuthKey(FLAGS_TEST_pg_auth_key);
  } else {
    shared_object().SetPostgresAuthKey(RandomUniformInt<uint64_t>());
  }

  return Status::OK();
}

Status TabletServer::InitAutoFlags() {
  if (!VERIFY_RESULT(auto_flags_manager_->LoadFromFile())) {
    RETURN_NOT_OK(auto_flags_manager_->LoadFromMaster(
        options_.HostsString(), *opts_.GetMasterAddresses(), ApplyNonRuntimeAutoFlags::kTrue));
  }

  return Status::OK();
}

uint32_t TabletServer::GetAutoFlagConfigVersion() const {
  return auto_flags_manager_->GetConfigVersion();
}

Status TabletServer::SetAutoFlagConfig(const AutoFlagsConfigPB new_config) {
  return auto_flags_manager_->LoadFromConfig(
      std::move(new_config), ApplyNonRuntimeAutoFlags::kFalse);
}

AutoFlagsConfigPB TabletServer::TEST_GetAutoFlagConfig() const {
  return auto_flags_manager_->GetConfig();
}

Status TabletServer::GetRegistration(ServerRegistrationPB* reg, server::RpcOnly rpc_only) const {
  RETURN_NOT_OK(RpcAndWebServerBase::GetRegistration(reg, rpc_only));
  reg->set_pg_port(pgsql_proxy_bind_address().port());
  return Status::OK();
}

Status TabletServer::WaitInited() {
  return tablet_manager_->WaitForAllBootstrapsToFinish();
}

void TabletServer::AutoInitServiceFlags() {
  const int32 num_cores = base::NumCPUs();

  if (FLAGS_tablet_server_svc_num_threads == -1) {
    // Auto select number of threads for the TS service based on number of cores.
    // But bound it between 64 & 512.
    const int32 num_threads = std::max(64, std::min(512, num_cores * 32));
    CHECK_OK(
        SetFlagDefaultAndCurrent("tablet_server_svc_num_threads", std::to_string(num_threads)));
    LOG(INFO) << "Auto setting FLAGS_tablet_server_svc_num_threads to "
              << FLAGS_tablet_server_svc_num_threads;
  }

  if (FLAGS_num_concurrent_backfills_allowed == -1) {
    const int32 num_threads = std::max(1, std::min(8, num_cores / 2));
    CHECK_OK(
        SetFlagDefaultAndCurrent("num_concurrent_backfills_allowed", std::to_string(num_threads)));
    LOG(INFO) << "Auto setting FLAGS_num_concurrent_backfills_allowed to "
              << FLAGS_num_concurrent_backfills_allowed;
  }

  if (FLAGS_ts_consensus_svc_num_threads == -1) {
    // Auto select number of threads for the TS service based on number of cores.
    // But bound it between 64 & 512.
    const int32 num_threads = std::max(64, std::min(512, num_cores * 32));
    CHECK_OK(
        SetFlagDefaultAndCurrent("ts_consensus_svc_num_threads", std::to_string(num_threads)));
    LOG(INFO) << "Auto setting FLAGS_ts_consensus_svc_num_threads to "
              << FLAGS_ts_consensus_svc_num_threads;
  }
}

Status TabletServer::RegisterServices() {
  auto tablet_server_service = std::make_shared<TabletServiceImpl>(this);
  tablet_server_service_ = tablet_server_service;
  LOG(INFO) << "yb::tserver::TabletServiceImpl created at " << tablet_server_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_tablet_server_svc_queue_length,
                                                     std::move(tablet_server_service)));

  std::unique_ptr<ServiceIf> admin_service(new TabletServiceAdminImpl(this));
  LOG(INFO) << "yb::tserver::TabletServiceAdminImpl created at " << admin_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_ts_admin_svc_queue_length,
                                                     std::move(admin_service)));

  std::unique_ptr<ServiceIf> consensus_service(new ConsensusServiceImpl(metric_entity(),
                                                                        tablet_manager_.get()));
  LOG(INFO) << "yb::tserver::ConsensusServiceImpl created at " << consensus_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_ts_consensus_svc_queue_length,
                                                     std::move(consensus_service),
                                                     rpc::ServicePriority::kHigh));

  std::unique_ptr<ServiceIf> remote_bootstrap_service =
      std::make_unique<RemoteBootstrapServiceImpl>(
          fs_manager_.get(), tablet_manager_.get(), metric_entity(), this->MakeCloudInfoPB(),
          &this->proxy_cache());
  LOG(INFO) << "yb::tserver::RemoteBootstrapServiceImpl created at " <<
    remote_bootstrap_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_ts_remote_bootstrap_svc_queue_length,
                                                     std::move(remote_bootstrap_service)));
  auto pg_client_service = std::make_shared<PgClientServiceImpl>(
      *this, tablet_manager_->client_future(), clock(),
      std::bind(&TabletServer::TransactionPool, this), metric_entity(),
      &messenger()->scheduler(), &xcluster_safe_time_map_);
  pg_client_service_ = pg_client_service;
  LOG(INFO) << "yb::tserver::PgClientServiceImpl created at " << pg_client_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(
      FLAGS_pg_client_svc_queue_length, std::move(pg_client_service)));

  return Status::OK();
}

Status TabletServer::Start() {
  CHECK(initted_.load(std::memory_order_acquire));

  AutoInitServiceFlags();

  RETURN_NOT_OK(RegisterServices());
  RETURN_NOT_OK(DbServerBase::Start());

  RETURN_NOT_OK(tablet_manager_->Start());

  RETURN_NOT_OK(heartbeater_->Start());

  if (FLAGS_tserver_enable_metrics_snapshotter) {
    RETURN_NOT_OK(metrics_snapshotter_->Start());
  }

  RETURN_NOT_OK(maintenance_manager_->Init());

  google::FlushLogFiles(google::INFO); // Flush the startup messages.

  return Status::OK();
}

void TabletServer::Shutdown() {
  LOG(INFO) << "TabletServer shutting down...";

  bool expected = true;
  if (initted_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
    maintenance_manager_->Shutdown();
    WARN_NOT_OK(heartbeater_->Stop(), "Failed to stop TS Heartbeat thread");

    if (FLAGS_tserver_enable_metrics_snapshotter) {
      WARN_NOT_OK(metrics_snapshotter_->Stop(), "Failed to stop TS Metrics Snapshotter thread");
    }
    tablet_manager_->StartShutdown();
    RpcAndWebServerBase::Shutdown();
    tablet_manager_->CompleteShutdown();
  }

  LOG(INFO) << "TabletServer shut down complete. Bye!";
}

Status TabletServer::PopulateLiveTServers(const master::TSHeartbeatResponsePB& heartbeat_resp) {
  std::lock_guard<simple_spinlock> l(lock_);
  // We reset the list each time, since we want to keep the tservers that are live from the
  // master's perspective.
  // TODO: In the future, we should enhance the logic here to keep track information retrieved
  // from the master and compare it with information stored here. Based on this information, we
  // can only send diff updates CQL clients about whether a node came up or went down.
  live_tservers_.assign(heartbeat_resp.tservers().begin(), heartbeat_resp.tservers().end());
  return Status::OK();
}

Status TabletServer::GetLiveTServers(
    std::vector<master::TSInformationPB> *live_tservers) const {
  std::lock_guard<simple_spinlock> l(lock_);
  *live_tservers = live_tservers_;
  return Status::OK();
}

Status TabletServer::GetTabletStatus(const GetTabletStatusRequestPB* req,
                                     GetTabletStatusResponsePB* resp) const {
  VLOG(3) << "GetTabletStatus called for tablet " << req->tablet_id();
  auto tablet_peer = VERIFY_RESULT(tablet_manager_->GetTablet(req->tablet_id()));
  tablet_peer->GetTabletStatusPB(resp->mutable_tablet_status());
  return Status::OK();
}

bool TabletServer::LeaderAndReady(const TabletId& tablet_id, bool allow_stale) const {
  auto peer = tablet_manager_->LookupTablet(tablet_id);
  if (!peer) {
    return false;
  }
  return peer->LeaderStatus(allow_stale) == consensus::LeaderStatus::LEADER_AND_READY;
}

Status TabletServer::SetUniverseKeyRegistry(
    const encryption::UniverseKeyRegistryPB& universe_key_registry) {
  return Status::OK();
}

void TabletServer::set_cluster_uuid(const std::string& cluster_uuid) {
  std::lock_guard<simple_spinlock> l(lock_);
  cluster_uuid_ = cluster_uuid;
}

std::string TabletServer::cluster_uuid() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return cluster_uuid_;
}

Status GetDynamicUrlTile(
  const string& path, const string& hostport, const int port,
  const string& http_addr_host, string* url) {
  // We get an incoming hostport string like '127.0.0.1:5433' or '[::1]:5433' or [::1]
  // and a port 13000 which has to be converted to '127.0.0.1:13000'. If the hostport is
  // a wildcard - 0.0.0.0 - the URLs are formed based on the http address for web instead
  HostPort hp;
  RETURN_NOT_OK(hp.ParseString(hostport, port));
  if (IsWildcardAddress(hp.host())) {
    hp.set_host(http_addr_host);
  }
  hp.set_port(port);

  *url = strings::Substitute("http://$0$1", hp.ToString(), path);
  return Status::OK();
}

Status TabletServer::DisplayRpcIcons(std::stringstream* output) {
  ServerRegistrationPB reg;
  RETURN_NOT_OK(GetRegistration(&reg));
  string http_addr_host = reg.http_addresses(0).host();

  // RPCs in Progress.
  DisplayIconTile(output, "fa-tasks", "TServer Live Ops", "/rpcz");
  // YCQL RPCs in Progress.
  string cass_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/rpcz", FLAGS_cql_proxy_bind_address, FLAGS_cql_proxy_webserver_port,
      http_addr_host, &cass_url));
  DisplayIconTile(output, "fa-tasks", "YCQL Live Ops", cass_url);

  // YEDIS RPCs in Progress.
  string redis_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/rpcz", FLAGS_redis_proxy_bind_address, FLAGS_redis_proxy_webserver_port,
      http_addr_host,  &redis_url));
  DisplayIconTile(output, "fa-tasks", "YEDIS Live Ops", redis_url);

  // YSQL RPCs in Progress.
  string sql_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/rpcz", FLAGS_pgsql_proxy_bind_address, FLAGS_pgsql_proxy_webserver_port,
      http_addr_host, &sql_url));
  DisplayIconTile(output, "fa-tasks", "YSQL Live Ops", sql_url);

  // YSQL All Ops
  string sql_all_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/statements", FLAGS_pgsql_proxy_bind_address, FLAGS_pgsql_proxy_webserver_port,
      http_addr_host, &sql_all_url));
  DisplayIconTile(output, "fa-tasks", "YSQL All Ops", sql_all_url);
  return Status::OK();
}

Env* TabletServer::GetEnv() {
  return fs_manager()->encrypted_env();
}

rocksdb::Env* TabletServer::GetRocksDBEnv() {
  return rocksdb_env_ ? rocksdb_env_.get() : rocksdb::Env::Default();
}

uint64_t TabletServer::GetSharedMemoryPostgresAuthKey() {
  return shared_object().postgres_auth_key();
}

Status TabletServer::get_ysql_db_oid_to_cat_version_info_map(
    GetTserverCatalogVersionInfoResponsePB *resp) const {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const auto it : ysql_db_catalog_version_map_) {
    auto* entry = resp->add_entries();
    entry->set_db_oid(it.first);
    entry->set_shm_index(it.second.shm_index);
    entry->set_current_version(it.second.current_version);
  }
  return Status::OK();
}

void TabletServer::SetYsqlCatalogVersion(uint64_t new_version, uint64_t new_breaking_version) {
  {
    std::lock_guard<simple_spinlock> l(lock_);

    if (new_version == ysql_catalog_version_) {
      return;
    } else if (new_version < ysql_catalog_version_) {
      LOG(DFATAL) << "Ignoring ysql catalog version update: new version too old. "
                  << "New: " << new_version << ", Old: " << ysql_catalog_version_;
      return;
    }
    ysql_catalog_version_ = new_version;
    shared_object().SetYsqlCatalogVersion(new_version);
    ysql_last_breaking_catalog_version_ = new_breaking_version;
  }
  if (FLAGS_log_ysql_catalog_versions) {
    LOG_WITH_FUNC(INFO) << "set catalog version: " << new_version << ", breaking version: "
                        << new_breaking_version;
  }
  auto pg_client_service = pg_client_service_.lock();
  if (pg_client_service) {
    LOG(INFO) << "Invalidating PgTableCache cache since catalog version incremented";
    pg_client_service->InvalidateTableCache();
  }
}

void TabletServer::SetYsqlDBCatalogVersions(
  const master::DBCatalogVersionDataPB& db_catalog_version_data) {
  std::lock_guard<simple_spinlock> l(lock_);

  std::unordered_set<uint32_t> db_oid_set;
  for (int i = 0; i < db_catalog_version_data.db_catalog_versions_size(); i++) {
    const auto& db_catalog_version = db_catalog_version_data.db_catalog_versions(i);
    const uint32_t db_oid = db_catalog_version.db_oid();
    const uint64_t new_version = db_catalog_version.current_version();
    const uint64_t new_breaking_version = db_catalog_version.last_breaking_version();
    if (!db_oid_set.insert(db_oid).second) {
      LOG(DFATAL) << "Ignoring duplicate db oid " << db_oid;
      continue;
    }
    // Try to insert a new entry, using -1 as shm_index which will be updated later if the
    // new entry is inserted successfully.
    // Design note:
    // In per-db catalog version mode once a database is allocated a slot in the shared memory
    // array db_catalog_versions_, it will remain allocated and will not change across the
    // life-span of the database. In Yugabyte, a database can be dropped even if there is still
    // a connection to it. However after the database is dropped, that connection will get error
    // if it performs a query on any of the database objects. A query error will trigger a cache
    // refresh which involves a call to YBIsDBConnectionValid, thus terminates that connection.
    // Also in per-db catalog version mode we will reject a connection if we cannot find a slot
    // in db_catalog_versions_ that is allocated for its MyDatabaseId.
    const auto it = ysql_db_catalog_version_map_.insert(
      std::make_pair(db_oid, CatalogVersionInfo({.current_version = new_version,
                                                 .last_breaking_version = new_breaking_version,
                                                 .shm_index = -1})));
    bool row_inserted = it.second;
    bool row_updated = false;
    int shm_index = -1;
    if (!row_inserted) {
      auto& existing_entry = it.first->second;
      if (new_version > existing_entry.current_version) {
        existing_entry.current_version = new_version;
        existing_entry.last_breaking_version = new_breaking_version;
        row_updated = true;
        shm_index = existing_entry.shm_index;
        CHECK(shm_index >= 0 && shm_index < TServerSharedData::kMaxNumDbCatalogVersions)
          << "Invalid shm_index: " << shm_index;
      } else if (new_version < existing_entry.current_version) {
        LOG(DFATAL) << "Ignoring ysql db " << db_oid
                    << " catalog version update: new version too old. "
                    << "New: " << new_version << ", Old: " << existing_entry.current_version;
      } else {
        // It is not possible to have same current_version but different last_breaking_version.
        CHECK_EQ(new_breaking_version, existing_entry.last_breaking_version);
      }
    } else {
      auto& inserted_entry = it.first->second;
      // Allocate a new free slot in shared memory array db_catalog_versions_ for db_oid.
      int count = 0;
      while (count < TServerSharedData::kMaxNumDbCatalogVersions) {
        if (!(*ysql_db_catalog_version_index_used_)[search_starting_index_]) {
          // Found a free slot, remember it.
          shm_index = search_starting_index_;
          // Mark it as used.
          (*ysql_db_catalog_version_index_used_)[shm_index] = true;
          // Adjust search_starting_index_ for next time.
          ++search_starting_index_;
          break;
        }

        // The current slot is used, continue searching.
        ++search_starting_index_;
        if (search_starting_index_ == TServerSharedData::kMaxNumDbCatalogVersions) {
          search_starting_index_ = 0;
        }
        // Will stop if all slots are found used.
        ++count;
      }
      if (shm_index == -1) {
        YB_LOG_EVERY_N_SECS(ERROR, 60) << "Cannot find free db_catalog_versions_ slot, db_oid: "
                                       << db_oid;
        continue;
      }
      // update the newly inserted entry to have the allocated slot.
      inserted_entry.shm_index = shm_index;
    }

    if (row_inserted || row_updated) {
      // Set the new catalog version in shared memory at slot shm_index.
      shared_object().SetYsqlDbCatalogVersion(static_cast<size_t>(shm_index), new_version);
      if (FLAGS_log_ysql_catalog_versions) {
        LOG_WITH_FUNC(INFO) << "set db " << db_oid
                            << " catalog version: " << new_version
                            << ", breaking version: " << new_breaking_version;
      }
    }
  }

  // We only do full catalog report for now, remove entries that no longer exist.
  for (auto it = ysql_db_catalog_version_map_.begin();
       it != ysql_db_catalog_version_map_.end();) {
    const uint32_t db_oid = it->first;
    if (db_oid_set.count(db_oid) == 0) {
      auto shm_index = it->second.shm_index;
      CHECK(shm_index >= 0 &&
            shm_index < TServerSharedData::kMaxNumDbCatalogVersions) << shm_index;
      // Mark the corresponding shared memory array db_catalog_versions_ slot as free.
      (*ysql_db_catalog_version_index_used_)[shm_index] = false;
      it = ysql_db_catalog_version_map_.erase(it);
      // Also reset the shared memory array db_catalog_versions_ slot to 0 to assist
      // debugging the shared memory array db_catalog_versions_ (e.g., when we can dump
      // the shared memory file to examine its contents).
      shared_object().SetYsqlDbCatalogVersion(static_cast<size_t>(shm_index), 0);
    } else {
      ++it;
    }
  }
}

void TabletServer::UpdateTransactionTablesVersion(uint64_t new_version) {
  const auto transaction_manager = transaction_manager_.load(std::memory_order_acquire);
  if (transaction_manager) {
    transaction_manager->UpdateTransactionTablesVersion(new_version);
  }
}

TabletPeerLookupIf* TabletServer::tablet_peer_lookup() {
  return tablet_manager_.get();
}

const std::shared_future<client::YBClient*>& TabletServer::client_future() const {
  return DbServerBase::client_future();
}

client::TransactionPool& TabletServer::TransactionPool() {
  return DbServerBase::TransactionPool();
}

client::LocalTabletFilter TabletServer::CreateLocalTabletFilter() {
  if (FLAGS_TEST_select_all_status_tablets) {
    return client::LocalTabletFilter();
  }
  return std::bind(&TSTabletManager::PreserveLocalLeadersOnly, tablet_manager(), _1);
}

const std::shared_ptr<MemTracker>& TabletServer::mem_tracker() const {
  return RpcServerBase::mem_tracker();
}

void TabletServer::SetPublisher(rpc::Publisher service) {
  publish_service_ptr_.reset(new rpc::Publisher(std::move(service)));
}

const XClusterSafeTimeMap& TabletServer::GetXClusterSafeTimeMap() const {
  return xcluster_safe_time_map_;
}

void TabletServer::UpdateXClusterSafeTime(const XClusterNamespaceToSafeTimePBMap& safe_time_map) {
  xcluster_safe_time_map_.Update(safe_time_map);
}

Result<bool> TabletServer::XClusterSafeTimeCaughtUpToCommitHt(
    const NamespaceId& namespace_id, HybridTime commit_ht) const {
  return VERIFY_RESULT(xcluster_safe_time_map_.GetSafeTime(namespace_id)) > commit_ht;
}

scoped_refptr<Histogram> TabletServer::GetMetricsHistogram(
    TabletServerServiceRpcMethodIndexes metric) {
  auto tablet_server_service = tablet_server_service_.lock();
  if (tablet_server_service) {
    return tablet_server_service->GetMetric(metric).handler_latency;
  }
  return nullptr;
}

Status TabletServer::ValidateAndMaybeSetUniverseUuid(const UniverseUuid& universe_uuid) {
  auto instance_universe_uuid_str = VERIFY_RESULT(
      fs_manager_->GetUniverseUuidFromTserverInstanceMetadata());
  auto instance_universe_uuid = VERIFY_RESULT(UniverseUuid::FromString(instance_universe_uuid_str));
  if (!instance_universe_uuid.IsNil()) {
    // If there is a mismatch between the received uuid and instance uuid, return an error.
    SCHECK_EQ(universe_uuid, instance_universe_uuid, IllegalState,
        Format("Received mismatched universe_uuid $0 from master when instance metadata "
               "uuid is $1", universe_uuid.ToString(), instance_universe_uuid.ToString()));
    return Status::OK();
  }
  return fs_manager_->SetUniverseUuidOnTserverInstanceMetadata(universe_uuid);
}

}  // namespace tserver
}  // namespace yb
