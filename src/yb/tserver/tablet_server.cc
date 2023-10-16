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
#include "yb/common/common_util.h"
#include "yb/common/pg_catversions.h"
#include "yb/common/wire_protocol.h"

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/hash/city.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/yb_rpc.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/rpc_server.h"
#include "yb/server/secure.h"
#include "yb/server/webserver.h"
#include "yb/server/hybrid_clock.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/heartbeater.h"
#include "yb/tserver/heartbeater_factory.h"
#include "yb/tserver/metrics_snapshotter.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/remote_bootstrap_service.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/pg_table_mutation_count_sender.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver-path-handlers.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/backup_service.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service_context.h"
#include "yb/tserver/stateful_services/pg_auto_analyze_service.h"
#include "yb/tserver/stateful_services/test_echo_service.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/pg_util.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/ntp_clock.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::make_shared;
using std::shared_ptr;
using std::vector;
using std::string;
using yb::rpc::ServiceIf;

using namespace std::literals;
using namespace yb::size_literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_int32(tablet_server_svc_num_threads, -1,
             "Number of RPC worker threads for the TS service. If -1, it is auto configured.");
TAG_FLAG(tablet_server_svc_num_threads, advanced);

DEFINE_UNKNOWN_int32(ts_admin_svc_num_threads, 10,
             "Number of RPC worker threads for the TS admin service");
TAG_FLAG(ts_admin_svc_num_threads, advanced);

DEFINE_UNKNOWN_int32(ts_consensus_svc_num_threads, -1,
             "Number of RPC worker threads for the TS consensus service. If -1, it is auto "
             "configured.");
TAG_FLAG(ts_consensus_svc_num_threads, advanced);

DEFINE_UNKNOWN_int32(ts_remote_bootstrap_svc_num_threads, 10,
             "Number of RPC worker threads for the TS remote bootstrap service");
TAG_FLAG(ts_remote_bootstrap_svc_num_threads, advanced);

DEFINE_UNKNOWN_int32(tablet_server_svc_queue_length,
    yb::tserver::TabletServer::kDefaultSvcQueueLength,
    "RPC queue length for the TS service.");
TAG_FLAG(tablet_server_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(ts_admin_svc_queue_length, 50,
             "RPC queue length for the TS admin service");
TAG_FLAG(ts_admin_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(ts_consensus_svc_queue_length,
    yb::tserver::TabletServer::kDefaultSvcQueueLength,
    "RPC queue length for the TS consensus service.");
TAG_FLAG(ts_consensus_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(ts_remote_bootstrap_svc_queue_length, 50,
             "RPC queue length for the TS remote bootstrap service");
TAG_FLAG(ts_remote_bootstrap_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(pg_client_svc_queue_length, yb::tserver::TabletServer::kDefaultSvcQueueLength,
             "RPC queue length for the Pg Client service.");
TAG_FLAG(pg_client_svc_queue_length, advanced);

DEFINE_UNKNOWN_bool(enable_direct_local_tablet_server_call,
            true,
            "Enable direct call to local tablet server");
TAG_FLAG(enable_direct_local_tablet_server_call, advanced);

DEFINE_UNKNOWN_string(redis_proxy_bind_address, "", "Address to bind the redis proxy to");
DEFINE_UNKNOWN_int32(redis_proxy_webserver_port, 0, "Webserver port for redis proxy");

DEFINE_UNKNOWN_string(cql_proxy_bind_address, "", "Address to bind the CQL proxy to");
DEFINE_UNKNOWN_int32(cql_proxy_webserver_port, 0, "Webserver port for CQL proxy");

DEFINE_NON_RUNTIME_string(pgsql_proxy_bind_address, "", "Address to bind the PostgreSQL proxy to");
DECLARE_int32(pgsql_proxy_webserver_port);

DEFINE_NON_RUNTIME_PREVIEW_bool(enable_ysql_conn_mgr, false,
    "Enable Ysql Connection Manager for the cluster. Tablet Server will start a "
    "Ysql Connection Manager process as a child process.");

DEFINE_UNKNOWN_int64(inbound_rpc_memory_limit, 0, "Inbound RPC memory limit");

DEFINE_UNKNOWN_bool(tserver_enable_metrics_snapshotter, false,
    "Should metrics snapshotter be enabled");

DEFINE_test_flag(uint64, pg_auth_key, 0, "Forces an auth key for the postgres user when non-zero");

DECLARE_int32(num_concurrent_backfills_allowed);

constexpr int kTServerYbClientDefaultTimeoutMs = 60 * 1000;

DEFINE_UNKNOWN_int32(tserver_yb_client_default_timeout_ms, kTServerYbClientDefaultTimeoutMs,
             "Default timeout for the YBClient embedded into the tablet server that is used "
             "for distributed transactions.");

DEFINE_test_flag(bool, echo_service_enabled, false, "Enable the Test Echo service");
DEFINE_test_flag(int32, echo_svc_queue_length, 50, "RPC queue length for the Test Echo service");

DEFINE_test_flag(bool, select_all_status_tablets, false, "");

DEFINE_UNKNOWN_int32(ts_backup_svc_num_threads, 4,
             "Number of RPC worker threads for the TS backup service");
TAG_FLAG(ts_backup_svc_num_threads, advanced);

DEFINE_UNKNOWN_int32(ts_backup_svc_queue_length, 50,
             "RPC queue length for the TS backup service");
TAG_FLAG(ts_backup_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(xcluster_svc_queue_length, 5000,
             "RPC queue length for the xCluster service");
TAG_FLAG(xcluster_svc_queue_length, advanced);

DECLARE_string(cert_node_filename);
DECLARE_bool(ysql_enable_table_mutation_counter);

DEFINE_NON_RUNTIME_bool(allow_encryption_at_rest, true,
                        "Whether or not to allow encryption at rest to be enabled. Toggling this "
                        "flag does not turn on or off encryption at rest, but rather allows or "
                        "disallows a user from enabling it on in the future.");

DEFINE_UNKNOWN_int32(
    get_universe_key_registry_backoff_increment_ms, 100,
    "Number of milliseconds added to the delay between retries of fetching the full universe key "
    "registry from master leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(get_universe_key_registry_backoff_increment_ms, stable);
TAG_FLAG(get_universe_key_registry_backoff_increment_ms, advanced);

DEFINE_UNKNOWN_int32(
    get_universe_key_registry_max_backoff_sec, 3,
    "Maximum number of seconds to delay between retries of fetching the full universe key registry "
    "from master leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(get_universe_key_registry_max_backoff_sec, stable);
TAG_FLAG(get_universe_key_registry_max_backoff_sec, advanced);

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_port, yb::pgwrapper::PgProcessConf::kDefaultPort,
    "Ysql Connection Manager port to which clients will connect. This must be different from the "
    "postgres port set via pgsql_proxy_bind_address. Default is 5433.");

DEFINE_RUNTIME_int32(check_pg_object_id_allocators_interval_secs, 3600 * 3,
    "Interval at which the TS check pg object id allocators for dropped databases.");
TAG_FLAG(check_pg_object_id_allocators_interval_secs, advanced);

namespace yb {
namespace tserver {

namespace {

uint16_t GetPostgresPort() {
  yb::HostPort postgres_address;
  CHECK_OK(postgres_address.ParseString(
      FLAGS_pgsql_proxy_bind_address, yb::pgwrapper::PgProcessConf().kDefaultPort));
  return postgres_address.port();
}

void PostgresAndYsqlConnMgrPortValidator() {
  if (!FLAGS_enable_ysql_conn_mgr) {
    return;
  }
  const auto pg_port = GetPostgresPort();
  if (FLAGS_ysql_conn_mgr_port == pg_port) {
    if (pg_port != pgwrapper::PgProcessConf::kDefaultPort) {
      LOG(FATAL) << "Postgres port (pgsql_proxy_bind_address: " << pg_port
                 << ") and Ysql Connection Manager port (ysql_conn_mgr_port:"
                 << FLAGS_ysql_conn_mgr_port << ") cannot be the same.";
    } else {
      // Ignore. t-server will resolve the conflict in SetProxyAddresses.
    }
  }
}

// Normally we would have used DEFINE_validator. But this validation depends on the value of another
// flag (pgsql_proxy_bind_address). On process startup flag validations are run as each flag
// gets parsed from the command line parameter. So this would impose a restriction on the user to
// pass the flags in a particular obscure order via command line. YBA has no guarantees on the order
// it uses as well. So, instead we use a Callback with LOG(FATAL) since at startup Callbacks are run
// after all the flags have been parsed.
REGISTER_CALLBACK(ysql_conn_mgr_port, "PostgresAndYsqlConnMgrPortValidator",
    &PostgresAndYsqlConnMgrPortValidator);


class CDCServiceContextImpl : public cdc::CDCServiceContext {
 public:
  explicit CDCServiceContextImpl(TabletServer* tablet_server) : tablet_server_(*tablet_server) {}

  tablet::TabletPeerPtr LookupTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->LookupTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->GetTablet(tablet_id);
  }

  Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const override {
    return tablet_server_.tablet_manager()->GetServingTablet(tablet_id);
  }

  const std::string& permanent_uuid() const override { return tablet_server_.permanent_uuid(); }

  std::unique_ptr<client::AsyncClientInitialiser> MakeClientInitializer(
      const std::string& client_name, MonoDelta default_timeout) const override {
    return std::make_unique<client::AsyncClientInitialiser>(
        client_name, default_timeout, tablet_server_.permanent_uuid(), &tablet_server_.options(),
        tablet_server_.metric_entity(), tablet_server_.mem_tracker(), tablet_server_.messenger());
  }

 private:
  TabletServer& tablet_server_;
};
}  // namespace

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

  if (GetAtomicFlag(&FLAGS_ysql_enable_table_mutation_counter)) {
    pg_table_mutation_count_sender_.reset(new TableMutationCountSender(this));
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

  return RpcAndWebServerBase::InitAutoFlags();
}

Result<std::unordered_set<std::string>> TabletServer::GetAvailableAutoFlagsForServer() const {
  return auto_flags_manager_->GetAvailableAutoFlagsForServer();
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
  // This makes the yb_servers() function return the connection manager port instead
  // of th backend db.
  if (FLAGS_enable_ysql_conn_mgr) {
    reg->set_pg_port(FLAGS_ysql_conn_mgr_port);
  } else {
    reg->set_pg_port(pgsql_proxy_bind_address().port());
  }
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
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(tablet_server_svc_num_threads, num_threads));
    LOG(INFO) << "Auto setting FLAGS_tablet_server_svc_num_threads to "
              << FLAGS_tablet_server_svc_num_threads;
  }

  if (FLAGS_num_concurrent_backfills_allowed == -1) {
    const int32 num_threads = std::max(1, std::min(8, num_cores / 2));
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(num_concurrent_backfills_allowed, num_threads));
    LOG(INFO) << "Auto setting FLAGS_num_concurrent_backfills_allowed to "
              << FLAGS_num_concurrent_backfills_allowed;
  }

  if (FLAGS_ts_consensus_svc_num_threads == -1) {
    // Auto select number of threads for the TS service based on number of cores.
    // But bound it between 64 & 512.
    const int32 num_threads = std::max(64, std::min(512, num_cores * 32));
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(ts_consensus_svc_num_threads, num_threads));
    LOG(INFO) << "Auto setting FLAGS_ts_consensus_svc_num_threads to "
              << FLAGS_ts_consensus_svc_num_threads;
  }
}

Status TabletServer::RegisterServices() {
#if !defined(__APPLE__)
  server::HybridClock::RegisterProvider(
      NtpClock::Name(), [](const std::string&) { return std::make_shared<NtpClock>(); });
#endif

  cdc_service_ = std::make_shared<cdc::CDCServiceImpl>(
      std::make_unique<CDCServiceContextImpl>(this), metric_entity(), metric_registry());

  RETURN_NOT_OK(RegisterService(
      FLAGS_ts_backup_svc_queue_length,
      std::make_shared<TabletServiceBackupImpl>(tablet_manager_.get(), metric_entity())));

  RETURN_NOT_OK(RegisterService(FLAGS_xcluster_svc_queue_length, cdc_service_));

  auto tablet_server_service = std::make_shared<TabletServiceImpl>(this);
  tablet_server_service_ = tablet_server_service;
  LOG(INFO) << "yb::tserver::TabletServiceImpl created at " << tablet_server_service.get();
  RETURN_NOT_OK(RegisterService(
      FLAGS_tablet_server_svc_queue_length, std::move(tablet_server_service)));

  auto admin_service = std::make_shared<TabletServiceAdminImpl>(this);
  LOG(INFO) << "yb::tserver::TabletServiceAdminImpl created at " << admin_service.get();
  RETURN_NOT_OK(RegisterService(FLAGS_ts_admin_svc_queue_length, std::move(admin_service)));

  auto consensus_service = std::make_shared<ConsensusServiceImpl>(
      metric_entity(), tablet_manager_.get());
  LOG(INFO) << "yb::tserver::ConsensusServiceImpl created at " << consensus_service.get();
  RETURN_NOT_OK(RegisterService(FLAGS_ts_consensus_svc_queue_length,
                                std::move(consensus_service),
                                rpc::ServicePriority::kHigh));

  auto remote_bootstrap_service = std::make_shared<RemoteBootstrapServiceImpl>(
          fs_manager_.get(), tablet_manager_.get(), metric_entity(), this->MakeCloudInfoPB(),
          &this->proxy_cache());
  LOG(INFO) << "yb::tserver::RemoteBootstrapServiceImpl created at " <<
    remote_bootstrap_service.get();
  RETURN_NOT_OK(RegisterService(
      FLAGS_ts_remote_bootstrap_svc_queue_length, std::move(remote_bootstrap_service)));
  auto pg_client_service = std::make_shared<PgClientServiceImpl>(
      *this, tablet_manager_->client_future(), clock(),
      std::bind(&TabletServer::TransactionPool, this), mem_tracker(), metric_entity(),
      &messenger()->scheduler(), XClusterContext(xcluster_safe_time_map_, xcluster_read_only_mode_),
      &pg_node_level_mutation_counter_);
  pg_client_service_ = pg_client_service;
  LOG(INFO) << "yb::tserver::PgClientServiceImpl created at " << pg_client_service.get();
  RETURN_NOT_OK(RegisterService(FLAGS_pg_client_svc_queue_length, std::move(pg_client_service)));

  if (FLAGS_TEST_echo_service_enabled) {
    auto test_echo_service = std::make_unique<stateful_service::TestEchoService>(
        permanent_uuid(), metric_entity(), client_future());
    LOG(INFO) << "yb::tserver::stateful_service::TestEchoService created at "
              << test_echo_service.get();
    RETURN_NOT_OK(test_echo_service->Init(tablet_manager_.get()));
    RETURN_NOT_OK(RegisterService(FLAGS_TEST_echo_svc_queue_length, std::move(test_echo_service)));
  }

  auto pg_auto_analyze_service =
      std::make_shared<stateful_service::PgAutoAnalyzeService>(metric_entity(), client_future());
  LOG(INFO) << "yb::tserver::stateful_service::PgAutoAnalyzeService created at "
            << pg_auto_analyze_service.get();
  RETURN_NOT_OK(pg_auto_analyze_service->Init(tablet_manager_.get()));
  RETURN_NOT_OK(RegisterService(
      FLAGS_TEST_echo_svc_queue_length, std::move(pg_auto_analyze_service)));

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

  if (pg_table_mutation_count_sender_) {
    RETURN_NOT_OK(pg_table_mutation_count_sender_->Start());
  }

  RETURN_NOT_OK(maintenance_manager_->Init());

  google::FlushLogFiles(google::INFO); // Flush the startup messages.

  if (FLAGS_enable_ysql) {
    ScheduleCheckObjectIdAllocators();
  }

  return Status::OK();
}

void TabletServer::Shutdown() {
  LOG(INFO) << "TabletServer shutting down...";

  bool expected = true;
  if (initted_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
    auto xcluster_consumer = GetXClusterConsumer();
    if (xcluster_consumer) {
      xcluster_consumer->Shutdown();
    }

    maintenance_manager_->Shutdown();
    WARN_NOT_OK(heartbeater_->Stop(), "Failed to stop TS Heartbeat thread");

    if (FLAGS_tserver_enable_metrics_snapshotter) {
      WARN_NOT_OK(metrics_snapshotter_->Stop(), "Failed to stop TS Metrics Snapshotter thread");
    }

    if (pg_table_mutation_count_sender_) {
      WARN_NOT_OK(pg_table_mutation_count_sender_->Stop(),
          "Failed to stop table mutation count sender thread");
    }

    tablet_manager_->StartShutdown();
    RpcAndWebServerBase::Shutdown();
    tablet_manager_->CompleteShutdown();
  }

  LOG(INFO) << "TabletServer shut down complete. Bye!";
}

Status TabletServer::PopulateLiveTServers(const master::TSHeartbeatResponsePB& heartbeat_resp) {
  std::lock_guard l(lock_);
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
  std::lock_guard l(lock_);
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

void TabletServer::set_cluster_uuid(const std::string& cluster_uuid) {
  std::lock_guard l(lock_);
  cluster_uuid_ = cluster_uuid;
}

std::string TabletServer::cluster_uuid() const {
  std::lock_guard l(lock_);
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

  // YCQL RPCs in Progress.
  string cass_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/rpcz", FLAGS_cql_proxy_bind_address, FLAGS_cql_proxy_webserver_port,
      http_addr_host, &cass_url));
  DisplayIconTile(output, "fa-tasks", "YCQL Live Ops", cass_url);

  // YCQL All Ops
  string cql_all_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/statements", FLAGS_cql_proxy_bind_address, FLAGS_cql_proxy_webserver_port,
      http_addr_host, &cql_all_url));
  DisplayIconTile(output, "fa-tasks", "YCQL All Ops", cql_all_url);

  // RPCs in Progress.
  DisplayIconTile(output, "fa-tasks", "TServer Live Ops", "/rpcz");

  // YEDIS RPCs in Progress.
  string redis_url;
  RETURN_NOT_OK(GetDynamicUrlTile(
      "/rpcz", FLAGS_redis_proxy_bind_address, FLAGS_redis_proxy_webserver_port,
      http_addr_host,  &redis_url));
  DisplayIconTile(output, "fa-tasks", "YEDIS Live Ops", redis_url);

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
    const GetTserverCatalogVersionInfoRequestPB& req,
    GetTserverCatalogVersionInfoResponsePB *resp) const {
  std::lock_guard l(lock_);
  if (req.size_only()) {
    resp->set_num_entries(narrow_cast<uint32_t>(ysql_db_catalog_version_map_.size()));
  } else {
    const auto db_oid = req.db_oid();
    for (const auto& map_entry : ysql_db_catalog_version_map_) {
      if (db_oid == kInvalidOid || db_oid == map_entry.first) {
        auto* entry = resp->add_entries();
        entry->set_db_oid(map_entry.first);
        entry->set_shm_index(map_entry.second.shm_index);
        if (db_oid != kInvalidOid) {
          break;
        }
      }
    }
  }
  return Status::OK();
}

void TabletServer::SetYsqlCatalogVersion(uint64_t new_version, uint64_t new_breaking_version) {
  {
    std::lock_guard l(lock_);

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
  InvalidatePgTableCache();
}

void TabletServer::SetYsqlDBCatalogVersions(
  const master::DBCatalogVersionDataPB& db_catalog_version_data) {
  DCHECK_GT(db_catalog_version_data.db_catalog_versions_size(), 0);
  std::lock_guard l(lock_);

  bool catalog_changed = false;
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
        CHECK(
            shm_index >= 0 &&
            shm_index < static_cast<int>(TServerSharedData::kMaxNumDbCatalogVersions))
            << "Invalid shm_index: " << shm_index;
      } else if (new_version < existing_entry.current_version) {
        LOG(DFATAL) << "Ignoring ysql db " << db_oid
                    << " catalog version update: new version too old. "
                    << "New: " << new_version << ", Old: " << existing_entry.current_version;
      } else {
        // It is not possible to have same current_version but different last_breaking_version.
        CHECK_EQ(new_breaking_version, existing_entry.last_breaking_version)
            << "db_oid: " << db_oid << ", new_version: " << new_version;
      }
    } else {
      auto& inserted_entry = it.first->second;
      // Allocate a new free slot in shared memory array db_catalog_versions_ for db_oid.
      uint32_t count = 0;
      while (count < TServerSharedData::kMaxNumDbCatalogVersions) {
        if (!(*ysql_db_catalog_version_index_used_)[search_starting_index_]) {
          // Found a free slot, remember it.
          shm_index = search_starting_index_;
          // Mark it as used.
          (*ysql_db_catalog_version_index_used_)[shm_index] = true;
          // Adjust search_starting_index_ for next time.
          ++search_starting_index_;
          if (search_starting_index_ == TServerSharedData::kMaxNumDbCatalogVersions) {
            // Wrap around.
            search_starting_index_ = 0;
          }
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
      catalog_changed = true;
      // Set the new catalog version in shared memory at slot shm_index.
      shared_object().SetYsqlDbCatalogVersion(static_cast<size_t>(shm_index), new_version);
      if (FLAGS_log_ysql_catalog_versions) {
        LOG_WITH_FUNC(INFO) << "set db " << db_oid
                            << " catalog version: " << new_version
                            << ", breaking version: " << new_breaking_version;
      }
      // During upgrade, it is possible that the table pg_yb_catalog_version has
      // just been upgraded to have a row for each database, but there is a race
      // condition where some PG backends have not yet seen this and continue to
      // use global catalog version. Here we also set the global catalog version
      // variables which can be used to check against RPC requests from such lagging
      // PG backends. Note that it is uncommon for database template1 to have
      // a connection. But even if there is a template1 connection operating in
      // per-db-mode, such a connection may receive more catalog version bumps
      // than needed from unrelated connections that are still operating in
      // global-mode, this only results in more RPC rejections but no correctness
      // issue.
      if (db_oid == kTemplate1Oid) {
        ysql_catalog_version_ = new_version;
        shared_object().SetYsqlCatalogVersion(new_version);
        ysql_last_breaking_catalog_version_ = new_breaking_version;
      }
    }
  }

  // We only do full catalog report for now, remove entries that no longer exist.
  for (auto it = ysql_db_catalog_version_map_.begin();
       it != ysql_db_catalog_version_map_.end();) {
    const uint32_t db_oid = it->first;
    if (db_oid_set.count(db_oid) == 0) {
      // This means the entry for db_oid no longer exists.
      catalog_changed = true;
      auto shm_index = it->second.shm_index;
      CHECK(shm_index >= 0 &&
            shm_index < static_cast<int>(TServerSharedData::kMaxNumDbCatalogVersions))
        << "shm_index: " << shm_index << ", db_oid: " << db_oid;
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
  if (!catalog_changed) {
    return;
  }
  // After we have updated versions, we compute and update its fingerprint.
  const auto new_fingerprint =
      FingerprintCatalogVersions<DbOidToCatalogVersionInfoMap>(ysql_db_catalog_version_map_);
  catalog_versions_fingerprint_.store(new_fingerprint, std::memory_order_release);
  VLOG_WITH_FUNC(2) << "databases: " << ysql_db_catalog_version_map_.size()
                    << ", new fingerprint: " << new_fingerprint;

  if (catalog_changed) {
    // TODO(myang): see how to only invalidate per-database tables.
    // https://github.com/yugabyte/yugabyte-db/issues/16114.
    InvalidatePgTableCache();
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

PgMutationCounter& TabletServer::GetPgNodeLevelMutationCounter() {
  return pg_node_level_mutation_counter_;
}

void TabletServer::UpdateXClusterSafeTime(const XClusterNamespaceToSafeTimePBMap& safe_time_map) {
  xcluster_safe_time_map_.Update(safe_time_map);
}

Result<cdc::XClusterRole> TabletServer::TEST_GetXClusterRole() const {
  auto xcluster_consumer_ptr = GetXClusterConsumer();
  if (!xcluster_consumer_ptr) {
    return STATUS(Uninitialized, "XCluster consumer has not been initialized");
  }
  return xcluster_consumer_ptr->TEST_GetXClusterRole();
}

scoped_refptr<Histogram> TabletServer::GetMetricsHistogram(
    TabletServerServiceRpcMethodIndexes metric) {
  auto tablet_server_service = tablet_server_service_.lock();
  if (tablet_server_service) {
    return tablet_server_service->GetMetric(metric).handler_latency;
  }
  return nullptr;
}

Status TabletServer::ListMasterServers(const ListMasterServersRequestPB* req,
                                       ListMasterServersResponsePB* resp) const {
  auto master_addresses = options().GetMasterAddresses();
  auto peer_status = resp->mutable_master_server_and_type();
  // Keeps the mapping of <resolved_addr, address>.
  std::map<std::string, std::string> resolved_addr_map;
  for (const auto& list : *master_addresses) {
    for (const auto& master_addr : list) {
      std::vector<Endpoint> resolved_addresses;
      Status s = master_addr.ResolveAddresses(&resolved_addresses);
      if (!s.ok()) {
        VLOG(1) << "Could not resolve: " << master_addr.ToString();
        continue;
      }
      for (const auto& resolved_addr : resolved_addresses) {
        const auto resolved_addr_str = HostPort(resolved_addr).ToString();
        std::map<std::string, std::string>::iterator it = resolved_addr_map.find(resolved_addr_str);
        // We want to return dns addresses (if available) and not resolved addresses.
        // So, insert into the map if it does not have the resolved address or
        // if the inserted entry has the key (resolved_addr) and value (address) as same.
        if (it == resolved_addr_map.end()) {
          resolved_addr_map.insert({resolved_addr_str, master_addr.ToString()});
        } else if (it->second == resolved_addr_str) {
          it->second = master_addr.ToString();
        }
      }
    }
  }

  std::string leader = heartbeater_->get_leader_master_hostport();
  for (const auto& resolved_master_entry : resolved_addr_map) {
    auto master_entry = peer_status->Add();
    auto master = resolved_master_entry.second;
    master_entry->set_master_server(master);
    if (leader.compare(master) == 0) {
      master_entry->set_is_leader(true);
    } else {
      master_entry->set_is_leader(false);
    }
  }
  return Status::OK();
}

void TabletServer::InvalidatePgTableCache() {
  auto pg_client_service = pg_client_service_.lock();
  if (pg_client_service) {
    LOG(INFO) << "Invalidating the entire PgTableCache cache since catalog version incremented";
    pg_client_service->InvalidateTableCache();
  }
}

Status TabletServer::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(DbServerBase::SetupMessengerBuilder(builder));

  secure_context_ = VERIFY_RESULT(
      server::SetupInternalSecureContext(options_.HostsString(), *fs_manager_, builder));

  return Status::OK();
}

XClusterConsumer* TabletServer::GetXClusterConsumer() const {
  std::lock_guard l(xcluster_consumer_mutex_);
  return xcluster_consumer_.get();
}

encryption::UniverseKeyManager* TabletServer::GetUniverseKeyManager() {
  return universe_key_manager_.get();
}

Status TabletServer::SetUniverseKeyRegistry(
    const encryption::UniverseKeyRegistryPB& universe_key_registry) {
  SCHECK_NOTNULL(universe_key_manager_);
  universe_key_manager_->SetUniverseKeyRegistry(universe_key_registry);
  return Status::OK();
}

Status TabletServer::CreateXClusterConsumer() {
  auto is_leader_clbk = [this](const string& tablet_id) {
    auto tablet_peer = tablet_manager_->LookupTablet(tablet_id);
    if (!tablet_peer) {
      return false;
    }
    return tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
  };

  auto get_leader_term = [this](const TabletId& tablet_id) {
    auto tablet_peer = tablet_manager_->LookupTablet(tablet_id);
    if (!tablet_peer) {
      return yb::OpId::kUnknownTerm;
    }
    return tablet_peer->LeaderTerm();
  };

  xcluster_consumer_ = VERIFY_RESULT(XClusterConsumer::Create(
      std::move(is_leader_clbk), std::move(get_leader_term), proxy_cache_.get(), this));
  return Status::OK();
}

Status TabletServer::SetConfigVersionAndConsumerRegistry(
    int32_t cluster_config_version, const cdc::ConsumerRegistryPB* consumer_registry) {
  std::lock_guard l(xcluster_consumer_mutex_);

  // Only create a cdc consumer if consumer_registry is not null.
  if (!xcluster_consumer_ && consumer_registry) {
    RETURN_NOT_OK(CreateXClusterConsumer());
  }
  if (xcluster_consumer_) {
    xcluster_consumer_->RefreshWithNewRegistryFromMaster(consumer_registry, cluster_config_version);
  }
  return Status::OK();
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

SchemaVersion TabletServer::GetMinXClusterSchemaVersion(const TableId& table_id,
      const ColocationId& colocation_id) const {
  std::lock_guard l(xcluster_consumer_mutex_);
  if (!xcluster_consumer_) {
    return cdc::kInvalidSchemaVersion;
  }

  return xcluster_consumer_->GetMinXClusterSchemaVersion(table_id, colocation_id);
}

int32_t TabletServer::cluster_config_version() const {
  std::lock_guard l(xcluster_consumer_mutex_);
  // If no CDC consumer, we will return -1, which will force the master to send the consumer
  // registry if one exists. If we receive one, we will create a new CDC consumer in
  // SetConsumerRegistry.
  if (!xcluster_consumer_) {
    return -1;
  }
  return xcluster_consumer_->cluster_config_version();
}

Result<uint32_t> TabletServer::XClusterConfigVersion() const {
  SCHECK(cdc_service_, NotFound, "CDC Service not found");
  return cdc_service_->GetXClusterConfigVersion();
}

Status TabletServer::SetPausedXClusterProducerStreams(
    const ::google::protobuf::Map<::std::string, bool>& paused_producer_stream_ids,
    uint32_t xcluster_config_version) {
  SCHECK(cdc_service_, NotFound, "CDC Service not found");
  if (VERIFY_RESULT(XClusterConfigVersion()) < xcluster_config_version) {
    cdc_service_->SetPausedXClusterProducerStreams(
        paused_producer_stream_ids, xcluster_config_version);
  }
  return Status::OK();
}

Status TabletServer::ReloadKeysAndCertificates() {
  if (!secure_context_) {
    return Status::OK();
  }

  RETURN_NOT_OK(server::ReloadSecureContextKeysAndCertificates(
      secure_context_.get(),
      fs_manager_->GetDefaultRootDir(),
      server::SecureContextType::kInternal,
      options_.HostsString()));

  std::lock_guard l(xcluster_consumer_mutex_);
  if (xcluster_consumer_) {
    RETURN_NOT_OK(xcluster_consumer_->ReloadCertificates());
  }

  for (const auto& reloader : certificate_reloaders_) {
    RETURN_NOT_OK(reloader());
  }

  return Status::OK();
}

std::string TabletServer::GetCertificateDetails() {
  if (!secure_context_) return "";

  return secure_context_.get()->GetCertificateDetails();
}

void TabletServer::RegisterCertificateReloader(CertificateReloader reloader) {
  certificate_reloaders_.push_back(std::move(reloader));
}

Status TabletServer::SetCDCServiceEnabled() {
  if (!cdc_service_) {
    LOG(WARNING) << "CDC Service Not Registered";
  } else {
    cdc_service_->SetCDCServiceEnabled();
  }
  return Status::OK();
}

void TabletServer::SetXClusterDDLOnlyMode(bool is_xcluster_read_only_mode) {
  xcluster_read_only_mode_.store(is_xcluster_read_only_mode, std::memory_order_release);
}

Result<std::unordered_set<uint32_t>> TabletServer::GetPgDatabaseOids() {
  LOG(INFO) << "Read pg_database to get the set of database oids";
  std::unordered_set<uint32_t> db_oids;
  auto conn = VERIFY_RESULT(pgwrapper::PGConnBuilder({
    .host = PgDeriveSocketDir(pgsql_proxy_bind_address()),
    .port = pgsql_proxy_bind_address().port(),
    .dbname = "template1",
    .user = "postgres",
    .password = UInt64ToString(GetSharedMemoryPostgresAuthKey()),
  }).Connect());

  auto res = VERIFY_RESULT(conn.Fetch("SELECT oid FROM pg_database"));
  auto lines = PQntuples(res.get());
  for (int i = 0; i != lines; ++i) {
    const auto oid = VERIFY_RESULT(pgwrapper::GetValue<pgwrapper::PGOid>(res.get(), i, 0));
    db_oids.insert(oid);
  }
  LOG(INFO) << "Successfully read " << db_oids.size() << " database oids from pg_database";
  return db_oids;
}

void TabletServer::ScheduleCheckObjectIdAllocators() {
  messenger()->scheduler().Schedule(
      [this](const Status& status) {
        if (!status.ok()) {
          LOG(INFO) << status;
          return;
        }
        Result<std::unordered_set<uint32_t>> db_oids = GetPgDatabaseOids();
        if (db_oids.ok()) {
          auto pg_client_service = pg_client_service_.lock();
          if (pg_client_service) {
            pg_client_service->CheckObjectIdAllocators(*db_oids);
          } else {
            LOG(WARNING) << "Could not call CheckObjectIdAllocators";
          }
        } else {
          LOG(WARNING) << "Could not get the set of database oids: " << ResultToStatus(db_oids);
        }
        ScheduleCheckObjectIdAllocators();
      },
      std::chrono::seconds(FLAGS_check_pg_object_id_allocators_interval_secs));
}

}  // namespace tserver
}  // namespace yb
