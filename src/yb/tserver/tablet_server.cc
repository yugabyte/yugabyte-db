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

#include "yb/client/client.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/universe_key_client.h"

#include "yb/common/common_flags.h"
#include "yb/common/wire_protocol.h"

#include "yb/encryption/universe_key_manager.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_heartbeat.pb.h"

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
using yb::rpc::ServiceIf;
using yb::tablet::TabletPeer;

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

DEFINE_bool(start_pgsql_proxy, false,
            "Whether to run a PostgreSQL server as a child process of the tablet server");

DEFINE_bool(tserver_enable_metrics_snapshotter, false, "Should metrics snapshotter be enabled");
DECLARE_int32(num_concurrent_backfills_allowed);
DECLARE_int32(svc_queue_length_default);

constexpr int kTServerYbClientDefaultTimeoutMs = 60 * 1000;

DEFINE_int32(tserver_yb_client_default_timeout_ms, kTServerYbClientDefaultTimeoutMs,
             "Default timeout for the YBClient embedded into the tablet server that is used "
             "for distributed transactions.");

namespace yb {
namespace tserver {

TabletServer::TabletServer(const TabletServerOptions& opts)
    : DbServerBase(
          "TabletServer", opts, "yb.tabletserver", server::CreateMemTrackerForServer()),
      fail_heartbeats_for_tests_(false),
      opts_(opts),
      tablet_manager_(new TSTabletManager(fs_manager_.get(), this, metric_registry())),
      path_handlers_(new TabletServerPathHandlers(this)),
      maintenance_manager_(new MaintenanceManager(MaintenanceManager::DEFAULT_OPTIONS)),
      master_config_index_(0) {
  SetConnectionContextFactory(rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(
      FLAGS_inbound_rpc_memory_limit, mem_tracker()));

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

void TabletServer::SetUniverseKeys(const encryption::UniverseKeysPB& universe_keys) {
  opts_.universe_key_manager->SetUniverseKeys(universe_keys);
}

void TabletServer::GetUniverseKeyRegistrySync() {
  universe_key_client_->GetUniverseKeyRegistrySync();
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

  if (FLAGS_tserver_enable_metrics_snapshotter) {
    metrics_snapshotter_.reset(new MetricsSnapshotter(opts_, this));
  }

  std::vector<HostPort> hps;
  for (const auto& master_addr_vector : *opts_.GetMasterAddresses()) {
    for (const auto& master_addr : master_addr_vector) {
      hps.push_back(master_addr);
    }
  }

  universe_key_client_ = std::make_unique<client::UniverseKeyClient>(
      hps, proxy_cache_.get(), [&] (const encryption::UniverseKeysPB& universe_keys) {
        opts_.universe_key_manager->SetUniverseKeys(universe_keys);
  });
  opts_.universe_key_manager->SetGetUniverseKeysCallback([&]() {
    universe_key_client_->GetUniverseKeyRegistrySync();
  });
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
  shared_object().SetPostgresAuthKey(RandomUniformInt<uint64_t>());

  return Status::OK();
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
    const int32 num_threads = std::min(512, num_cores * 32);
    FLAGS_tablet_server_svc_num_threads = std::max(64, num_threads);
    LOG(INFO) << "Auto setting FLAGS_tablet_server_svc_num_threads to "
              << FLAGS_tablet_server_svc_num_threads;
  }

  if (FLAGS_num_concurrent_backfills_allowed == -1) {
    const int32 num_threads = std::min(8, num_cores / 2);
    FLAGS_num_concurrent_backfills_allowed = std::max(1, num_threads);
    LOG(INFO) << "Auto setting FLAGS_num_concurrent_backfills_allowed to "
              << FLAGS_num_concurrent_backfills_allowed;
  }

  if (FLAGS_ts_consensus_svc_num_threads == -1) {
    // Auto select number of threads for the TS service based on number of cores.
    // But bound it between 64 & 512.
    const int32 num_threads = std::min(512, num_cores * 32);
    FLAGS_ts_consensus_svc_num_threads = std::max(64, num_threads);
    LOG(INFO) << "Auto setting FLAGS_ts_consensus_svc_num_threads to "
              << FLAGS_ts_consensus_svc_num_threads;
  }
}

Status TabletServer::RegisterServices() {
  auto tablet_server_service = std::make_shared<TabletServiceImpl>(this);
  tablet_server_service_ = tablet_server_service;
  LOG(INFO) << "yb::tserver::TabletServiceImpl created at " << tablet_server_service.get();

  std::unique_ptr<ServiceIf> forward_service =
      std::make_unique<TabletServerForwardServiceImpl>(tablet_server_service.get(), this);
  LOG(INFO) << "yb::tserver::ForwardServiceImpl created at " << forward_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_tablet_server_svc_queue_length,
                                                     std::move(forward_service)));
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
          fs_manager_.get(), tablet_manager_.get(), metric_entity());
  LOG(INFO) << "yb::tserver::RemoteBootstrapServiceImpl created at " <<
    remote_bootstrap_service.get();
  RETURN_NOT_OK(RpcAndWebServerBase::RegisterService(FLAGS_ts_remote_bootstrap_svc_queue_length,
                                                     std::move(remote_bootstrap_service)));

  auto pg_client_service = std::make_shared<PgClientServiceImpl>(
      tablet_manager_->client_future(), clock(), std::bind(&TabletServer::TransactionPool, this),
      metric_entity(), &messenger()->scheduler());
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
  tablet::TabletPeerPtr peer;
  if (!tablet_manager_->LookupTablet(req->tablet_id(), &peer)) {
    return STATUS(NotFound, "Tablet not found", req->tablet_id());
  }
  peer->GetTabletStatusPB(resp->mutable_tablet_status());
  return Status::OK();
}

bool TabletServer::LeaderAndReady(const TabletId& tablet_id, bool allow_stale) const {
  tablet::TabletPeerPtr peer;
  if (!tablet_manager_->LookupTablet(tablet_id, &peer)) {
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
  return opts_.env;
}

rocksdb::Env* TabletServer::GetRocksDBEnv() {
  return opts_.rocksdb_env;
}

uint64_t TabletServer::GetSharedMemoryPostgresAuthKey() {
  return shared_object().postgres_auth_key();
}

void TabletServer::SetYSQLCatalogVersion(uint64_t new_version, uint64_t new_breaking_version) {
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
    shared_object().SetYSQLCatalogVersion(new_version);
    ysql_last_breaking_catalog_version_ = new_breaking_version;
  }
  if (FLAGS_log_ysql_catalog_versions) {
    LOG_WITH_FUNC(INFO) << "set catalog version: " << new_version << ", breaking version: "
                        << new_breaking_version;
  }
  auto pg_client_service = pg_client_service_.lock();
  if (pg_client_service) {
    LOG(INFO) << "Invalidating the entire PgTableCache since catalog version incremented";
    pg_client_service->InvalidateTableCache();
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
  return std::bind(&TSTabletManager::PreserveLocalLeadersOnly, tablet_manager(), _1);
}

const std::shared_ptr<MemTracker>& TabletServer::mem_tracker() const {
  return RpcServerBase::mem_tracker();
}

void TabletServer::SetPublisher(rpc::Publisher service) {
  publish_service_ptr_.reset(new rpc::Publisher(std::move(service)));
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
