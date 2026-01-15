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
#include <utility>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service_context.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/universe_key_client.h"

#include "yb/common/common_flags.h"
#include "yb/common/common_util.h"
#include "yb/common/pg_catversions.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ysql_operation_lease.h"

#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_heartbeat.pb.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/server/async_client_initializer.h"
#include "yb/server/hybrid_clock.h"
#include "yb/server/rpc_server.h"
#include "yb/server/ycql_stat_provider.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/backup_service.h"
#include "yb/tserver/heartbeater.h"
#include "yb/tserver/heartbeater_factory.h"
#include "yb/tserver/metrics_snapshotter.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/pg_table_mutation_count_sender.h"
#include "yb/tserver/remote_bootstrap_service.h"
#include "yb/tserver/stateful_services/pg_auto_analyze_service.h"
#include "yb/tserver/stateful_services/pg_cron_leader_service.h"
#include "yb/tserver/stateful_services/test_echo_service.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver-path-handlers.h"
#include "yb/tserver/tserver_auto_flags_manager.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"
#include "yb/tserver/tserver_xcluster_context.h"
#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/ysql_lease_poller.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/ntp_clock.h"
#include "yb/util/pg_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::make_shared;
using std::shared_ptr;
using std::vector;
using std::string;
using yb::client::internal::RemoteTabletServer;
using yb::client::internal::RemoteTabletServerPtr;
using yb::rpc::ServiceIf;

using namespace std::literals;
using namespace yb::size_literals;
using namespace std::placeholders;

DEPRECATE_FLAG(int32, tablet_server_svc_num_threads, "02_2024");
DEPRECATE_FLAG(int32, ts_admin_svc_num_threads, "02_2024");
DEPRECATE_FLAG(int32, ts_consensus_svc_num_threads, "02_2024");
DEPRECATE_FLAG(int32, ts_remote_bootstrap_svc_num_threads, "02_2024");
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

DEFINE_NON_RUNTIME_bool(enable_ysql_conn_mgr, false,
    "Enable Ysql Connection Manager for the cluster. Tablet Server will start a "
    "Ysql Connection Manager process as a child process.");

DEFINE_NON_RUNTIME_int32(ysql_conn_mgr_max_pools, 10000,
    "Max total pools supported in YSQL Connection Manager.");

DEFINE_UNKNOWN_int64(inbound_rpc_memory_limit, 0, "Inbound RPC memory limit");

DEFINE_UNKNOWN_bool(tserver_enable_metrics_snapshotter, false,
    "Should metrics snapshotter be enabled");

DEFINE_test_flag(uint64, pg_auth_key, 0, "Forces an auth key for the postgres user when non-zero");

DECLARE_int32(num_concurrent_backfills_allowed);

constexpr int kTServerYBClientDefaultTimeoutMs = 60 * 1000;

DEFINE_UNKNOWN_int32(tserver_yb_client_default_timeout_ms, kTServerYBClientDefaultTimeoutMs,
             "Default timeout for the YBClient embedded into the tablet server that is used "
             "for distributed transactions.");

DEFINE_test_flag(bool, echo_service_enabled, false, "Enable the Test Echo service");
DEFINE_test_flag(int32, echo_svc_queue_length, 50, "RPC queue length for the Test Echo service");

DEFINE_test_flag(bool, select_all_status_tablets, false, "");

DEPRECATE_FLAG(int32, ts_backup_svc_num_threads, "02_2024");

DEFINE_UNKNOWN_int32(ts_backup_svc_queue_length, 50,
             "RPC queue length for the TS backup service");
TAG_FLAG(ts_backup_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(xcluster_svc_queue_length, 5000,
             "RPC queue length for the xCluster service");
TAG_FLAG(xcluster_svc_queue_length, advanced);

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

DEFINE_NON_RUNTIME_bool(start_pgsql_proxy, false,
            "Whether to run a PostgreSQL server as a child process of the tablet server");

DEFINE_RUNTIME_uint32(ysql_min_new_version_ignored_count, 10,
    "Minimum consecutive number of times that a tserver is allowed to ignore an older catalog "
    "version that is retrieved from a tserver-master heartbeat response.");

DECLARE_bool(enable_object_locking_for_table_locks);

DECLARE_uint32(ysql_max_invalidation_message_queue_size);

DECLARE_bool(enable_pg_cron);

DEFINE_test_flag(bool, enable_pg_client_mock, false, "Enable mocking of PgClient service in tests");

DEFINE_NON_RUNTIME_int32(stateful_svc_default_queue_length, 50,
    "Default RPC queue length used for stateful services.");

DEFINE_RUNTIME_bool(tserver_heartbeat_add_replication_status, true,
    "Add replication status to heartbeats tserver sends to master");

DEFINE_RUNTIME_int32(
    check_lagging_catalog_versions_interval_secs, 900,
    "Interval at which pg backends are checked for lagging catalog versions.");
TAG_FLAG(check_lagging_catalog_versions_interval_secs, advanced);

DEFINE_RUNTIME_int32(
    min_invalidation_message_retention_time_secs, 60,
    "Minimal time at which a catalog version with invalidation message is retained.");
TAG_FLAG(min_invalidation_message_retention_time_secs, advanced);

DEFINE_test_flag(int32, delay_set_catalog_version_table_mode_count, 0,
    "Delay set catalog version table mode by this many times of heartbeat responses "
    "after tserver starts");

DECLARE_int32(update_min_cdc_indices_interval_secs);

namespace yb::tserver {

namespace {

uint16_t GetPostgresPort() {
  yb::HostPort postgres_address;
  CHECK_OK(postgres_address.ParseString(
      FLAGS_pgsql_proxy_bind_address, yb::pgwrapper::PgProcessConf().kDefaultPort));
  return postgres_address.port();
}

bool PostgresAndYsqlConnMgrPortValidator(const char* flag_name, uint32 value) {
  // This validation depends on the value of other flag(s): enable_ysql_conn_mgr,
  // pgsql_proxy_bind_address.
  DELAY_FLAG_VALIDATION_ON_STARTUP(flag_name);

  if (!FLAGS_enable_ysql_conn_mgr) {
    return true;
  }
  const auto pg_port = GetPostgresPort();
  if (value == pg_port) {
    if (pg_port != pgwrapper::PgProcessConf::kDefaultPort) {
      LOG_FLAG_VALIDATION_ERROR(flag_name, value)
          << "Must be different from the Postgres port pgsql_proxy_bind_address (" << pg_port
          << ")";
      return false;
    } else {
      // Ignore. t-server will resolve the conflict in SetProxyAddresses.
    }
  }

  return true;
}

DEFINE_validator(ysql_conn_mgr_port, &PostgresAndYsqlConnMgrPortValidator);

bool ValidateEnableYsqlConnMgr(const char* flag_name, bool value) {
  if (!value) {
    return true;
  }

  // This validation depends on the value of other flag(s): start_pgsql_proxy, enable_ysql.
  DELAY_FLAG_VALIDATION_ON_STARTUP(flag_name);

  if (!FLAGS_start_pgsql_proxy && !FLAGS_enable_ysql) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, value)
        << "YSQL must be enabled to start the YSQL connection manager.";
    return false;
  }
  return true;
}

DEFINE_validator(enable_ysql_conn_mgr, &ValidateEnableYsqlConnMgr);

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

  Result<uint32> GetAutoFlagsConfigVersion() const override {
    return tablet_server_.ValidateAndGetAutoFlagsConfigVersion();
  }

  Result<HostPort> GetDesiredHostPortForLocal() const override {
    ServerRegistrationPB reg;
    RETURN_NOT_OK(tablet_server_.GetRegistration(&reg, server::RpcOnly::kTrue));
    return HostPortFromPB(DesiredHostPort(reg, tablet_server_.options().MakeCloudInfoPB()));
  }

 private:
  TabletServer& tablet_server_;
};

bool MinimalRetentionTimePassed(CoarseTimePoint message_time, CoarseTimePoint now) {
  return message_time + FLAGS_min_invalidation_message_retention_time_secs * 1s < now;
}

}  // namespace

struct TabletServer::PgClientServiceHolder {
  template <class... Args>
  explicit PgClientServiceHolder(Args&&... args) : impl(std::forward<Args>(args)...) {}

  PgClientServiceImpl impl;
  std::optional<PgClientServiceMockImpl> mock;
};

TabletServer::TabletServer(const TabletServerOptions& opts)
    : DbServerBase("TabletServer", opts, "yb.tabletserver", server::CreateMemTrackerForServer()),
      fail_heartbeats_for_tests_(false),
      opts_(opts),
      auto_flags_manager_(new TserverAutoFlagsManager(clock(), fs_manager_.get())),
      tablet_manager_(new TSTabletManager(fs_manager_.get(), this, metric_registry())),
      path_handlers_(new TabletServerPathHandlers(this)),
      maintenance_manager_(new MaintenanceManager(MaintenanceManager::DEFAULT_OPTIONS)),
      master_config_index_(0),
      xcluster_context_(new TserverXClusterContext()),
      object_lock_tracker_(std::make_shared<ObjectLockTracker>()),
      object_lock_shared_state_manager_(
          new docdb::ObjectLockSharedStateManager(object_lock_tracker_)) {
  SetConnectionContextFactory(rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(
      FLAGS_inbound_rpc_memory_limit, mem_tracker()));
  if (FLAGS_ysql_enable_db_catalog_version_mode) {
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

void TabletServer::SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) {
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
  ysql_lease_poller_->set_master_addresses(new_master_addresses);

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

  ysql_lease_poller_ = std::make_unique<YsqlLeaseClient>(*this, opts_.GetMasterAddresses());

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
          options_.HostsString(), JoinStrings(master_addresses, ","),
          fs_manager()->GetDefaultRootDir());
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

  if (!FLAGS_enable_ysql) {
    RETURN_NOT_OK(SkipSharedMemoryNegotiation());
  }

  RETURN_NOT_OK_PREPEND(tablet_manager_->Init(),
                        "Could not init Tablet Manager");

  initted_.store(true, std::memory_order_release);

  auto shared = shared_object();

  // 5433 is kDefaultPort in src/yb/yql/pgwrapper/pg_wrapper.h.
  RETURN_NOT_OK(pgsql_proxy_bind_address_.ParseString(FLAGS_pgsql_proxy_bind_address, 5433));
  if (PREDICT_FALSE(FLAGS_TEST_pg_auth_key != 0)) {
    shared->SetPostgresAuthKey(FLAGS_TEST_pg_auth_key);
  } else {
    shared->SetPostgresAuthKey(RandomUniformInt<uint64_t>());
  }

  shared->SetTserverUuid(fs_manager()->uuid());

  shared_mem_manager_->SetReadyCallback([this] {
    if (auto* object_lock_state = shared_mem_manager_->SharedData()->object_lock_state()) {
      object_lock_shared_state_manager_->SetupShared(*object_lock_state);
    }
  });

  return Status::OK();
}

Status TabletServer::InitFlags(rpc::Messenger* messenger) {
  RETURN_NOT_OK(auto_flags_manager_->Init(messenger, *opts_.GetMasterAddresses()));

  return RpcAndWebServerBase::InitFlags(messenger);
}

Result<std::unordered_set<std::string>> TabletServer::GetAvailableAutoFlagsForServer() const {
  return auto_flags_manager_->GetAvailableAutoFlagsForServer();
}

uint32_t TabletServer::GetAutoFlagConfigVersion() const {
  return auto_flags_manager_->GetConfigVersion();
}

Result<std::unordered_set<std::string>> TabletServer::GetFlagsForServer() const {
  return yb::GetFlagNamesFromXmlFile("tserver_flags.xml");
}

void TabletServer::HandleMasterHeartbeatResponse(
    HybridTime heartbeat_sent_time, std::optional<AutoFlagsConfigPB> new_config) {
  auto_flags_manager_->HandleMasterHeartbeatResponse(heartbeat_sent_time, std::move(new_config));
}

Result<uint32> TabletServer::ValidateAndGetAutoFlagsConfigVersion() const {
  return auto_flags_manager_->ValidateAndGetConfigVersion();
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

  if (FLAGS_num_concurrent_backfills_allowed == -1) {
    const int32 num_threads = std::max(1, std::min(8, num_cores / 2));
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(num_concurrent_backfills_allowed, num_threads));
    LOG(INFO) << "Auto setting FLAGS_num_concurrent_backfills_allowed to "
              << FLAGS_num_concurrent_backfills_allowed;
  }
}

Status TabletServer::RegisterServices() {
#if !defined(__APPLE__)
  server::HybridClock::RegisterProvider(
      NtpClock::Name(), [](const std::string&) { return std::make_shared<NtpClock>(); });
#endif

  cdc_service_ = std::make_shared<cdc::CDCServiceImpl>(
      std::make_unique<CDCServiceContextImpl>(this), metric_entity(), metric_registry(),
      client_future(), []() { return FLAGS_update_min_cdc_indices_interval_secs; });

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
  remote_bootstrap_service_ = remote_bootstrap_service;
  LOG(INFO) << "yb::tserver::RemoteBootstrapServiceImpl created at " <<
    remote_bootstrap_service.get();
  RETURN_NOT_OK(RegisterService(
      FLAGS_ts_remote_bootstrap_svc_queue_length, std::move(remote_bootstrap_service)));

  auto pg_client_service_holder = std::make_shared<PgClientServiceHolder>(
        *this, tablet_manager_->client_future(), clock(),
        std::bind(&TabletServer::TransactionManager, this),
        std::bind(&TabletServer::TransactionPool, this), mem_tracker(), metric_entity(),
        messenger(), permanent_uuid(), options(), xcluster_context_.get(),
        &pg_node_level_mutation_counter_);
  PgClientServiceIf* pg_client_service_if = &pg_client_service_holder->impl;
  LOG(INFO) << "yb::tserver::PgClientServiceImpl created at " << pg_client_service_if;

  if (PREDICT_FALSE(FLAGS_TEST_enable_pg_client_mock)) {
    pg_client_service_holder->mock.emplace(metric_entity(), pg_client_service_if);
    pg_client_service_if = &pg_client_service_holder->mock.value();
    LOG(INFO) << "Mock created for yb::tserver::PgClientServiceImpl";
  }

  pg_client_service_ = pg_client_service_holder;
  RETURN_NOT_OK(RegisterService(
      FLAGS_pg_client_svc_queue_length, std::shared_ptr<PgClientServiceIf>(
          std::move(pg_client_service_holder), pg_client_service_if)));

  if (FLAGS_TEST_echo_service_enabled) {
    auto test_echo_service = std::make_unique<stateful_service::TestEchoService>(
        permanent_uuid(), metric_entity(), client_future());
    LOG(INFO) << "yb::tserver::stateful_service::TestEchoService created at "
              << test_echo_service.get();
    RETURN_NOT_OK(test_echo_service->Init(tablet_manager_.get()));
    RETURN_NOT_OK(
        RegisterService(FLAGS_stateful_svc_default_queue_length, std::move(test_echo_service)));
  }

  auto connect_to_pg = [this](const std::string& database_name,
                              const std::optional<CoarseTimePoint>& deadline) {
    return pgwrapper::CreateInternalPGConnBuilder(pgsql_proxy_bind_address(), database_name,
                                                  GetSharedMemoryPostgresAuthKey(),
                                                  deadline).Connect();
  };
  auto pg_auto_analyze_service =
      std::make_shared<stateful_service::PgAutoAnalyzeService>(metric_entity(), client_future(),
                                                               connect_to_pg);
  LOG(INFO) << "yb::tserver::stateful_service::PgAutoAnalyzeService created at "
            << pg_auto_analyze_service.get();
  RETURN_NOT_OK(pg_auto_analyze_service->Init(tablet_manager_.get()));
  RETURN_NOT_OK(
      RegisterService(FLAGS_stateful_svc_default_queue_length, std::move(pg_auto_analyze_service)));

  if (FLAGS_enable_pg_cron) {
    auto pg_cron_leader_service = std::make_unique<stateful_service::PgCronLeaderService>(
        std::bind(&TabletServer::SetCronLeaderLease, this, _1), metric_entity(), client_future());
    LOG(INFO) << "yb::tserver::stateful_service::PgCronLeaderService created at "
              << pg_cron_leader_service.get();
    RETURN_NOT_OK(pg_cron_leader_service->Init(tablet_manager_.get()));

    RETURN_NOT_OK(RegisterService(
        FLAGS_stateful_svc_default_queue_length, std::move(pg_cron_leader_service)));
  }

  return Status::OK();
}

Status TabletServer::Start() {
  CHECK(initted_.load(std::memory_order_acquire));

  AutoInitServiceFlags();

  RETURN_NOT_OK(RegisterServices());
  RETURN_NOT_OK(DbServerBase::Start());

  RETURN_NOT_OK(tablet_manager_->Start());

  RETURN_NOT_OK(heartbeater_->Start());

  StartTSLocalLockManager();

  if (FLAGS_tserver_enable_metrics_snapshotter) {
    RETURN_NOT_OK(metrics_snapshotter_->Start());
  }

  if (pg_table_mutation_count_sender_) {
    RETURN_NOT_OK(pg_table_mutation_count_sender_->Start());
  }

  RETURN_NOT_OK(maintenance_manager_->Init());

  if (FLAGS_enable_ysql) {
    ScheduleCheckLaggingCatalogVersions();
  }

  google::FlushLogFiles(google::INFO); // Flush the startup messages.

  return Status::OK();
}

void TabletServer::Shutdown() {
  LOG(INFO) << "TabletServer shutting down...";

  bool expected = true;
  if (!initted_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
    return;
  }

  auto xcluster_consumer = GetXClusterConsumer();
  if (xcluster_consumer) {
    xcluster_consumer->Shutdown();
  }

  maintenance_manager_->Shutdown();
  WARN_NOT_OK(heartbeater_->Stop(), "Failed to stop TS Heartbeat thread");
  WARN_NOT_OK(ysql_lease_poller_->Stop(), "Failed to stop ysql lease client thread");

  if (FLAGS_tserver_enable_metrics_snapshotter) {
    WARN_NOT_OK(metrics_snapshotter_->Stop(), "Failed to stop TS Metrics Snapshotter thread");
  }

  if (pg_table_mutation_count_sender_) {
    WARN_NOT_OK(pg_table_mutation_count_sender_->Stop(),
        "Failed to stop table mutation count sender thread");
  }

  if (auto local_lock_manager = ts_local_lock_manager(); local_lock_manager) {
    local_lock_manager->Shutdown();
  }

  client()->RequestAbortAllRpcs();

  tablet_manager_->StartShutdown();
  RpcAndWebServerBase::Shutdown();
  tablet_manager_->CompleteShutdown();

  LOG(INFO) << "TabletServer shut down complete. Bye!";
}

tserver::TSLocalLockManagerPtr TabletServer::ResetAndGetTSLocalLockManager() {
  ts_local_lock_manager()->Shutdown();
  std::lock_guard l(lock_);
  StartTSLocalLockManagerUnlocked();
  return ts_local_lock_manager_;
}

void TabletServer::StartTSLocalLockManager() {
  std::lock_guard l(lock_);
  StartTSLocalLockManagerUnlocked();
}

void TabletServer::StartTSLocalLockManagerUnlocked() {
  if (opts_.server_type == TabletServerOptions::kServerType &&
      PREDICT_FALSE(FLAGS_enable_object_locking_for_table_locks) &&
      PREDICT_TRUE(FLAGS_enable_ysql)) {
    ts_local_lock_manager_ = std::make_shared<tserver::TSLocalLockManager>(
        clock_, this /* TabletServerIf* */, *this /* RpcServerBase& */,
        tablet_manager_->waiting_txn_pool(), metric_entity(),
        object_lock_tracker_, object_lock_shared_state_manager_.get());
    ts_local_lock_manager_->Start(tablet_manager_->waiting_txn_registry());
  }
}

bool TabletServer::HasBootstrappedLocalLockManager() const {
  auto lock_manager = ts_local_lock_manager();
  return lock_manager && lock_manager->IsBootstrapped();
}

Status TabletServer::ProcessLeaseUpdate(const master::RefreshYsqlLeaseInfoPB& lease_refresh_info) {
  VLOG(2) << __func__;
  auto lock_manager = ts_local_lock_manager();
  if (lease_refresh_info.new_lease() && lock_manager) {
    if (lock_manager->IsBootstrapped()) {
      // Reset the local lock manager to bootstrap from the given DDL lock entries.
      lock_manager = ResetAndGetTSLocalLockManager();
    }
    RETURN_NOT_OK(lock_manager->BootstrapDdlObjectLocks(lease_refresh_info.ddl_lock_entries()));
  }
  // It is safer to end the pg-sessions after resetting the local lock manager.
  // This way, if a new session gets created it will also be reset. But that is better than
  // having it the other way around, and having an old-session that is not reset.
  auto pg_client_service = pg_client_service_.lock();
  if (pg_client_service) {
    pg_client_service->impl.ProcessLeaseUpdate(lease_refresh_info);
  }
  return Status::OK();
}


Result<YSQLLeaseInfo> TabletServer::GetYSQLLeaseInfo() const {
  if (!IsYsqlLeaseEnabled()) {
    return STATUS(NotSupported, "YSQL lease is not enabled");
  }
  auto pg_client_service = pg_client_service_.lock();
  if (!pg_client_service) {
    RSTATUS_DCHECK(pg_client_service, InternalError, "Unable to get pg_client_service");
  }
  return pg_client_service->impl.GetYSQLLeaseInfo();
}

Status TabletServer::RestartPG() const {
  if (pg_restarter_) {
    return pg_restarter_();
  }
  return STATUS(IllegalState, "PG restarter callback not registered, cannot restart PG");
}

Status TabletServer::KillPg() const {
  if (pg_killer_) {
    return pg_killer_();
  }
  return STATUS(IllegalState, "Pg killer callback not registered, cannot restart PG");
}

bool TabletServer::IsYsqlLeaseEnabled() {
  return GetAtomicFlag(&FLAGS_enable_object_locking_for_table_locks) ||
         GetAtomicFlag(&FLAGS_enable_ysql_operation_lease);
}

Status TabletServer::PopulateLiveTServers(const master::TSHeartbeatResponsePB& heartbeat_resp) {
  std::lock_guard l(lock_);
  // We reset the list each time, since we want to keep the tservers that are live from the
  // master's perspective.
  // TODO: In the future, we should enhance the logic here to keep track information retrieved
  // from the master and compare it with information stored here. Based on this information, we
  // can only send diff updates CQL clients about whether a node came up or went down.
  live_tservers_.clear();
  for (const auto& ts_info_pb : heartbeat_resp.tservers()) {
    const auto& ts_uuid = ts_info_pb.tserver_instance().permanent_uuid();
    live_tservers_[ts_uuid] = ts_info_pb;
    if (!remote_tservers_.contains(ts_uuid)) {
      remote_tservers_[ts_uuid] = std::make_shared<RemoteTabletServer>(ts_info_pb);
    }
  }
  // Prune handles to the TServers that are no longer alive.
  std::erase_if(remote_tservers_, [&](const auto& remote_ts_pair) REQUIRES(lock_) {
    return !live_tservers_.contains(remote_ts_pair.first);
  });
  return Status::OK();
}

Status TabletServer::GetLiveTServers(std::vector<master::TSInformationPB> *live_tservers) const {
  SharedLock l(lock_);
  live_tservers->reserve(live_tservers_.size());
  for (const auto& [_, ts_info_pb] : live_tservers_) {
    live_tservers->push_back(ts_info_pb);
  }
  return Status::OK();
}

Result<std::vector<RemoteTabletServerPtr>> TabletServer::GetRemoteTabletServers() const {
  SharedLock l(lock_);
  std::vector<RemoteTabletServerPtr> remote_tservers;
  remote_tservers.reserve(remote_tservers_.size());
  for (auto& [_, remote_ts_ptr] : remote_tservers_) {
    remote_tservers.push_back(DCHECK_NOTNULL(remote_ts_ptr));
  }
  return remote_tservers;
}

Result<std::vector<RemoteTabletServerPtr>> TabletServer::GetRemoteTabletServers(
    const std::unordered_set<std::string>& ts_uuids) const {
  SharedLock l(lock_);
  std::vector<RemoteTabletServerPtr> remote_tservers;
  remote_tservers.reserve(ts_uuids.size());
  for (auto& ts_uuid : ts_uuids) {
    auto remote_ts = FindPtrOrNull(remote_tservers_, ts_uuid);
    SCHECK(remote_ts, NotFound, Format("Unable to find TServer connection info with id ", ts_uuid));
    remote_tservers.push_back(remote_ts);
  }
  return remote_tservers;
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
  SharedLock l(lock_);
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
  return shared_object()->postgres_auth_key();
}

Status TabletServer::get_ysql_db_oid_to_cat_version_info_map(
    const GetTserverCatalogVersionInfoRequestPB& req,
    GetTserverCatalogVersionInfoResponsePB *resp) const {
  SharedLock l(lock_);
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

Status TabletServer::GetTserverCatalogMessageLists(
    const GetTserverCatalogMessageListsRequestPB& req,
    GetTserverCatalogMessageListsResponsePB *resp) const {
  SharedLock l(lock_);
  const auto db_oid = req.db_oid();
  const auto ysql_catalog_version = req.ysql_catalog_version();
  const auto num_catalog_versions = req.num_catalog_versions();
  SCHECK_GT(db_oid, 0, IllegalState, "Invalid db_oid");
  SCHECK_GT(num_catalog_versions, 0, IllegalState, "Invalid num_catalog_versions");
  auto it = ysql_db_invalidation_messages_map_.find(db_oid);
  if (it == ysql_db_invalidation_messages_map_.end()) {
    SCHECK_EQ(resp->entries_size(), 0, IllegalState, "Invalid entries_size");
    LOG(WARNING) << "Could not find messages for database " << db_oid;
    return Status::OK();
  }
  const auto& messages_vec = it->second.queue;
  uint64_t expected_version = ysql_catalog_version + 1;
  // Because messages_vec is sorted, we can use std::lower_bound with a custom
  // comparator function to find expected_version.
  auto comp = [](const std::tuple<uint64_t, std::optional<std::string>, CoarseTimePoint>& p,
                 uint64_t expected_version) {
                return std::get<0>(p) < expected_version;
              };
  auto it2 = std::lower_bound(messages_vec.begin(), messages_vec.end(),
                              expected_version, comp);
  // std::lower_bound: returns an iterator pointing to the first element in the range
  // that is not less than (i.e., greater than or equal to) expected_version.
  while (it2 != messages_vec.end() && std::get<0>(*it2) == expected_version) {
    auto* entry = resp->add_entries();
    if (std::get<1>(*it2).has_value()) {
      entry->set_message_list(std::get<1>(*it2).value());
    }
    ++expected_version;
    if (expected_version > ysql_catalog_version + num_catalog_versions) {
      break;
    }
    ++it2;
  }
  // We find a consecutive list (without any holes) matching with what the client asks for.
  if (resp->entries_size() == static_cast<int32_t>(num_catalog_versions)) {
    return Status::OK();
  }

  // The way we populate resp->entries() should ensure this assertion.
  DCHECK_LT(resp->entries_size(), static_cast<int32_t>(num_catalog_versions));
  std::set<uint64_t> current_versions;
  uint64_t last_version = 0;
  for (const auto& info : messages_vec) {
    const auto current_version = std::get<0>(info);
    SCHECK_LT(last_version, current_version, IllegalState, "Not sorted by catalog version");
    last_version = current_version;
    // Because we have verified last_version < current_version, we can assume insert will
    // be successful.
    current_versions.insert(current_version);
  }
  std::string current_versions_str;
  if (!current_versions.empty()) {
    auto max_version = *current_versions.rbegin();
    auto min_version = *current_versions.begin();
    if (max_version - min_version + 1 > current_versions.size()) {
      // There are holes from min_version to max_version. Printing the entire list
      // of versions can cause too long log lines. Skip the early versions less than
      // ysql_catalog_version. This shortens the log line but still allows one to
      // find out holes that are relevant for this request.
      auto it = std::lower_bound(current_versions.begin(), current_versions.end(),
                                 ysql_catalog_version);
      if (it == current_versions.end() ||
          (it != current_versions.begin() && *it > ysql_catalog_version)) {
        --it;
      }
      current_versions_str = yb::RangeToString(it, current_versions.end());
    } else {
      // There are no holes from min_version to max_version. Print a shorthand
      // rather than the entire list of versions.
      current_versions_str = Format("[$0--$1]", min_version, max_version);
    }
  } else {
    current_versions_str = "[]";
  }
  LOG(INFO) << "Could not find a matching consecutive list"
            << ", db_oid: " << db_oid
            << ", ysql_catalog_version: " << ysql_catalog_version
            << ", num_catalog_versions: " << num_catalog_versions
            << ", entries_size: " << resp->entries_size()
            << ", current_versions: " << current_versions_str
            << ", messages_vec.size(): " << messages_vec.size();
  // Clear any entries that might have matched and added to ensure PG backend
  // will do a full catalog cache refresh.
  resp->mutable_entries()->Clear();
  return Status::OK();
}

Status TabletServer::SetTserverCatalogMessageList(
    uint32_t db_oid, bool is_breaking_change, uint64_t new_catalog_version,
    const std::optional<std::string>& message_list) {
  std::lock_guard l(lock_);

  int shm_index = -1;
  InvalidationMessagesQueue *db_message_lists = nullptr;
  auto scope_exit = ScopeExit([this, &shm_index, &db_message_lists, db_oid, is_breaking_change,
                               new_catalog_version] {
    if (shm_index >= 0) {
      shared_object()->SetYsqlDbCatalogVersion(static_cast<size_t>(shm_index), new_catalog_version);
      if (FLAGS_log_ysql_catalog_versions) {
        LOG_WITH_FUNC(INFO) << "set db " << db_oid
                            << " catalog version: " << new_catalog_version
                            << " is_breaking_change: " << is_breaking_change;
      }
      InvalidatePgTableCache({{db_oid, new_catalog_version}} /* db_oids_updated */,
                             {} /* db_oids_deleted */);
    }
    // We may have added one more message to the queue that exceeded the max size.
    if (db_message_lists &&
        db_message_lists->size() > FLAGS_ysql_max_invalidation_message_queue_size) {
      db_message_lists->pop_front();
    }
  });

  // Check all the cases when we should make this call a no-op.
  auto it = ysql_db_catalog_version_map_.find(db_oid);
  if (it == ysql_db_catalog_version_map_.end()) {
    // If db_oid does not already have an existing entry in ysql_db_catalog_version_map_,
    // we will not have a way to find out the breaking version, bail out.
    return STATUS_FORMAT(TryAgain, "db oid $0 not found in db catalog version map", db_oid);
  }
  auto it2 = ysql_db_invalidation_messages_map_.find(db_oid);
  if (it2 == ysql_db_invalidation_messages_map_.end()) {
    // If db_oid does not have an entry yet, bail out. The creation of the entry for
    // db_oid is managed by heartbeat response.
    return STATUS_FORMAT(TryAgain, "db oid $0 not found in db inval messages map", db_oid);
  }
  const auto cutoff_catalog_version = it2->second.cutoff_catalog_version;
  if (new_catalog_version <= cutoff_catalog_version) {
    // This is possible in theory: if the DDL backend that generated the new_catalog_version
    // has exited after sending out the call to SetTserverCatalogMessageList, and background
    // task has advanced cutoff_catalog_version forward.
    return STATUS_FORMAT(TryAgain,
                         "new catalog version $0 is stale due to cut off catalog version $1",
                         new_catalog_version, cutoff_catalog_version);
  }
  db_message_lists = &it2->second.queue;
  if (!db_message_lists->empty() &&
      std::get<0>(*db_message_lists->rbegin()) + 1 < new_catalog_version) {
    // There is at least a hole between the  existing max catalog version and
    // the new version. If we appended the new version and its messages, we would
    // have created a hole that can lead to full catalog cache refresh. In this
    // case let's wait for the heartbeat to propagate the missing versions.
    return STATUS_FORMAT(TryAgain,
                         "new catalog version $0 has a gap from $1",
                         new_catalog_version, std::get<0>(*db_message_lists->rbegin()));
  }

  // First, update catalog version.
  auto& existing_entry = it->second;
  // Only update the existing entry if new_catalog_version is newer.
  if (new_catalog_version > existing_entry.current_version) {
    existing_entry.current_version = new_catalog_version;
    if (is_breaking_change) {
      existing_entry.last_breaking_version = new_catalog_version;
    }
    UpdateCatalogVersionsFingerprintUnlocked();
    existing_entry.new_version_ignored_count = 0;
    shm_index = existing_entry.shm_index;
    CHECK(shm_index >= 0 &&
          shm_index < static_cast<int>(TServerSharedData::kMaxNumDbCatalogVersions))
      << "shm_index: " << shm_index << ", db_oid: " << db_oid;
    // We defer the setting of shared memory catalog version to the last to prevent the
    // case where a PG backend sees the new catalog version in shared memory prematurely
    // before the message_list is set in ysql_db_invalidation_messages_map_.
  } else if (new_catalog_version == existing_entry.current_version && is_breaking_change) {
    // If this new_catalog_version is a breaking change, it must be the same as the
    // existing one.
    CHECK_EQ(existing_entry.last_breaking_version, new_catalog_version) << db_oid;
  }

  // Second, insert the new pair into ysql_db_invalidation_messages_map_[db_oid].

  CoarseTimePoint now = CoarseMonoClock::Now();
  // Insert the new pair to the right position. Because db_message_lists is sorted, we can use
  // std::lower_bound with a custom comparator function to find the right insertion point.
  auto comp = [](const std::tuple<uint64_t, std::optional<std::string>, CoarseTimePoint>& p,
                 uint64_t current_version) {
                return std::get<0>(p) < current_version;
              };
  auto it3 = std::lower_bound(db_message_lists->begin(), db_message_lists->end(),
                              new_catalog_version, comp);
  const auto msg_info = message_list.has_value() ? std::to_string(message_list.value().size())
                                                 : "nullopt";
  if (it3 == db_message_lists->end()) {
    // This means that either the queue is empty, or the new_catalog_version is larger than
    // the last version in the queue (the queue is sorted in catalog version).
    LOG(INFO) << "appending version " << new_catalog_version << ", message_list: " << msg_info
              << ", db " << db_oid;
    db_message_lists->emplace_back(new_catalog_version, message_list, now);
  } else  {
    // std::lower_bound: returns an iterator pointing to the first element in the range
    // that is not less than (i.e., greater than or equal to) new_catalog_version.
    if (std::get<0>(*it3) > new_catalog_version) {
      LOG(INFO) << "inserting version " << new_catalog_version << ", message_list: "
                << msg_info << " before existing version " << std::get<0>(*it3)
                << ", db " << db_oid;
      db_message_lists->insert(it3, std::make_tuple(new_catalog_version, message_list, now));
    } else {
      VLOG(2) << "found existing version: " << new_catalog_version;
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
    shared_object()->SetYsqlCatalogVersion(new_version);
    ysql_last_breaking_catalog_version_ = new_breaking_version;
  }
  if (FLAGS_log_ysql_catalog_versions) {
    LOG_WITH_FUNC(INFO) << "set catalog version: " << new_version << ", breaking version: "
                        << new_breaking_version;
  }
  InvalidatePgTableCache();
}

void TabletServer::SetYsqlDBCatalogVersionsUnlocked(
  const tserver::DBCatalogVersionDataPB& db_catalog_version_data, uint64_t debug_id) {
  DCHECK_GT(db_catalog_version_data.db_catalog_versions_size(), 0);

  bool catalog_changed = false;
  std::unordered_set<uint32_t> db_oid_set;
  std::unordered_map<uint32_t, uint64_t> db_oids_updated;
  std::unordered_set<uint32_t> db_oids_deleted;
  for (int i = 0; i < db_catalog_version_data.db_catalog_versions_size(); i++) {
    const auto& db_catalog_version = db_catalog_version_data.db_catalog_versions(i);
    const uint32_t db_oid = db_catalog_version.db_oid();
    const uint64_t new_version = db_catalog_version.current_version();
    const uint64_t new_breaking_version = db_catalog_version.last_breaking_version();
    if (FLAGS_TEST_check_catalog_version_overflow) {
      CHECK_GE(static_cast<int64_t>(new_version), 0)
          << new_version << " db_oid: " << db_oid
          << " db_catalog_version_data: " << db_catalog_version_data.ShortDebugString();
      CHECK_GE(static_cast<int64_t>(new_breaking_version), 0)
          << new_breaking_version << " db_oid: " << db_oid
          << " db_catalog_version_data: " << db_catalog_version_data.ShortDebugString();
    }
    if (!db_oid_set.insert(db_oid).second) {
      LOG(DFATAL) << "Ignoring duplicate db oid " << db_oid << ", debug_id: " << debug_id;
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
                                                 .shm_index = -1,
                                                 .new_version_ignored_count = 0})));
    if (ysql_db_catalog_version_map_.size() > 1) {
      if (!catalog_version_table_in_perdb_mode_.has_value() ||
          !catalog_version_table_in_perdb_mode_.value()) {
        if (PREDICT_FALSE(FLAGS_TEST_delay_set_catalog_version_table_mode_count > 0)) {
          --FLAGS_TEST_delay_set_catalog_version_table_mode_count;
        } else {
          LOG(INFO) << "set pg_yb_catalog_version table in perdb mode"
                    << ", debug_id: " << debug_id;
          catalog_version_table_in_perdb_mode_ = true;
          shared_object()->SetCatalogVersionTableInPerdbMode(true);
        }
      }
    } else {
      DCHECK_EQ(ysql_db_catalog_version_map_.size(), 1);
    }
    bool row_inserted = it.second;
    bool row_updated = false;
    int shm_index = -1;
    if (!row_inserted) {
      auto& existing_entry = it.first->second;
      if (new_version > existing_entry.current_version) {
        existing_entry.current_version = new_version;
        existing_entry.last_breaking_version = new_breaking_version;
        existing_entry.new_version_ignored_count = 0;
        row_updated = true;
        db_oids_updated.insert({db_oid, new_version});
        shm_index = existing_entry.shm_index;
        CHECK(
            shm_index >= 0 &&
            shm_index < static_cast<int>(TServerSharedData::kMaxNumDbCatalogVersions))
            << "Invalid shm_index: " << shm_index;
      } else if (new_version < existing_entry.current_version) {
        if (!db_catalog_version_data.ignore_catalog_version_staleness_check()) {
          ++existing_entry.new_version_ignored_count;
          // If the new version is continuously older than what we have seen, it implies that
          // master's current version has somehow gone backwards which isn't expected. Crash this
          // tserver to sync up with master again. Do so with RandomUniformInt to reduce the chance
          // that all tservers are crashed at the same time.
          auto new_version_ignored_count = RandomUniformInt<uint32_t>(
              FLAGS_ysql_min_new_version_ignored_count,
              FLAGS_ysql_min_new_version_ignored_count + 180);
          // Because the session that executes the DDL sets its incremented new version in the
          // local tserver, for this local tserver it is possible the heartbeat response has
          // not read the latest version from master yet. It is legitimate to see the following
          // as a WARNING. However we should not see this continuously for new_version_ignored_count
          // times.
          (existing_entry.new_version_ignored_count >= new_version_ignored_count ? LOG(FATAL)
                                                                                 : LOG(WARNING))
              << "Ignoring ysql db " << db_oid << " catalog version update: new version too old. "
              << "New: " << new_version << ", Old: " << existing_entry.current_version
              << ", ignored count: " << existing_entry.new_version_ignored_count
              << ", debug_id: " << debug_id;
        }
      } else {
        // It is possible to have same current_version but a newer last_breaking_version.
        // Following is a scenario that this can happen.
        // Assuming two newly created databases db1 and db2, both have (1, 1) as their
        // initial (current_version, last_breaking_version) in ysql_db_catalog_version_map_.
        // (1) DDL1: a breaking DDL statement executed on a db1 connection yet altering db2
        // so it only caused db2's current_version and last_breaking_version to increment,
        // this means db1 still has (1, 1) and db2 now has (2, 2).
        // (2) DDL2: a non-breaking DDL statement executed on a db2 connection, this means
        // db1 still has (1, 1) and db2 now has (3, 2).
        // If DDL1 and DDL2 are executed back-to-back quickly before the master heartbeat
        // response returns back (2, 2) or (3, 2) to this node, then
        // For DDL1 because MyDatabaseId (db1) != target database id (db2), we do not perform
        // the shared memory catalog version optimization.
        // For DDL2 because MyDatabaseId (db2) == target database id (db2), we do perform
        // the shared memory catalog version optimization: because DDL2 is non-breaking so
        // we only set db2's current_version to 3 and leave its last_breaking_version as 1
        // (see SetTserverCatalogMessageList). So db2's entry now has (3, 1).
        // After a heartbeat delay, this node receives (3, 2) from the master. The existing
        // db2's entry has last_breaking_version as 1, but the incoming new_breaking_version
        // is 2 which is newer than the existing last_breaking_version.
        if (new_breaking_version > existing_entry.last_breaking_version) {
          existing_entry.last_breaking_version = new_breaking_version;
          row_updated = true;
        } else {
          CHECK_EQ(new_breaking_version, existing_entry.last_breaking_version)
              << "db_oid: " << db_oid << ", new_version: " << new_version;
        }
        existing_entry.new_version_ignored_count = 0;
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
        YB_LOG_EVERY_N_SECS(ERROR, 60)
            << "Cannot find free db_catalog_versions_ slot, db_oid: " << db_oid
            << ", debug_id: " << debug_id;
        continue;
      }
      // update the newly inserted entry to have the allocated slot.
      inserted_entry.shm_index = shm_index;
      LOG_WITH_FUNC(INFO) << "inserted new db " << db_oid << ", shm_index: " << shm_index
                          << ", catalog version: " << new_version
                          << ", breaking version: " << new_breaking_version
                          << ", debug_id: " << debug_id;
    }

    if (row_inserted || row_updated) {
      catalog_changed = true;
      // Set the new catalog version in shared memory at slot shm_index.
      shared_object()->SetYsqlDbCatalogVersion(static_cast<size_t>(shm_index), new_version);
      if (FLAGS_log_ysql_catalog_versions) {
        LOG_WITH_FUNC(INFO) << "set db " << db_oid
                            << " catalog version: " << new_version
                            << ", breaking version: " << new_breaking_version
                            << ", debug_id: " << debug_id;
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
        shared_object()->SetYsqlCatalogVersion(new_version);
        ysql_last_breaking_catalog_version_ = new_breaking_version;
      }
      // Create the entry of db_oid if not exists.
      ysql_db_invalidation_messages_map_.insert(
          std::make_pair(db_oid, InvalidationMessagesInfo()));
    }
  }
  if (!catalog_version_table_in_perdb_mode_.has_value() &&
      ysql_db_catalog_version_map_.size() == 1) {
    // We can initialize to false at most one time. Once set,
    // catalog_version_table_in_perdb_mode_ can only go from false to
    // true (i.e., from global mode to perdb mode).
    LOG(INFO) << "set pg_yb_catalog_version table in global mode"
              << ", debug_id: " << debug_id;
    catalog_version_table_in_perdb_mode_ = false;
    shared_object()->SetCatalogVersionTableInPerdbMode(false);
  }

  // We only do full catalog report for now, remove entries that no longer exist.
  for (auto it = ysql_db_catalog_version_map_.begin();
       it != ysql_db_catalog_version_map_.end();) {
    const uint32_t db_oid = it->first;
    if (db_oid_set.count(db_oid) == 0) {
      // This means the entry for db_oid no longer exists.
      db_oids_deleted.insert(db_oid);
      catalog_changed = true;
      auto shm_index = it->second.shm_index;
      CHECK(shm_index >= 0 &&
            shm_index < static_cast<int>(TServerSharedData::kMaxNumDbCatalogVersions))
        << "shm_index: " << shm_index << ", db_oid: " << db_oid;
      // Mark the corresponding shared memory array db_catalog_versions_ slot as free.
      (*ysql_db_catalog_version_index_used_)[shm_index] = false;
      it = ysql_db_catalog_version_map_.erase(it);
      ysql_db_invalidation_messages_map_.erase(db_oid);
      // Also reset the shared memory array db_catalog_versions_ slot to 0 to assist
      // debugging the shared memory array db_catalog_versions_ (e.g., when we can dump
      // the shared memory file to examine its contents).
      shared_object()->SetYsqlDbCatalogVersion(static_cast<size_t>(shm_index), 0);
      LOG_WITH_FUNC(INFO) << "reset deleted db " << db_oid << " catalog version to 0"
                          << ", shm_index: " << shm_index
                          << ", debug_id: " << debug_id;
    } else {
      ++it;
    }
  }
  if (!catalog_changed) {
    return;
  }
  // After we have updated versions, we compute and update its fingerprint.
  UpdateCatalogVersionsFingerprintUnlocked();

  // If we only inserted new rows, then the existing databases do not have
  // any catalog version changes and the current catalog caches are valid.
  if (db_oids_updated.empty() && db_oids_deleted.empty()) {
    return;
  }
  // If many databases have their catalog versions changed, there is
  // a high chance that a global impact DDL statement has incremented the
  // catalog versions of all databases.
  if (db_oids_updated.size() > ysql_db_catalog_version_map_.size() / 2) {
    InvalidatePgTableCache();
  } else {
    InvalidatePgTableCache(db_oids_updated, db_oids_deleted);
  }
}

void TabletServer::ResetCatalogVersionsFingerprint() {
  LOG(INFO) << "reset catalog_versions_fingerprint_";
  catalog_versions_fingerprint_.store(std::nullopt, std::memory_order_release);
}

void TabletServer::UpdateCatalogVersionsFingerprintUnlocked() {
  const auto new_fingerprint =
      FingerprintCatalogVersions<DbOidToCatalogVersionInfoMap>(ysql_db_catalog_version_map_);
  catalog_versions_fingerprint_.store(new_fingerprint, std::memory_order_release);
  VLOG_WITH_FUNC(2) << "databases: " << ysql_db_catalog_version_map_.size()
                    << ", new fingerprint: " << new_fingerprint;
}

void TabletServer::SetYsqlDBCatalogVersionsWithInvalMessages(
    const tserver::DBCatalogVersionDataPB& db_catalog_version_data,
    const master::DBCatalogInvalMessagesDataPB& db_catalog_inval_messages_data) {
  // std::atomic needed to avoid tsan reporting data race.
  static std::atomic<uint64_t> next_debug_id{0};
  std::lock_guard l(lock_);
  const auto debug_id = ++next_debug_id;
  SetYsqlDBCatalogVersionsUnlocked(db_catalog_version_data, debug_id);
  SetYsqlDBCatalogInvalMessagesUnlocked(db_catalog_inval_messages_data, debug_id);
}

void TabletServer::SetYsqlDBCatalogInvalMessagesUnlocked(
    const master::DBCatalogInvalMessagesDataPB& db_catalog_inval_messages_data,
    uint64_t debug_id) {
  if (db_catalog_inval_messages_data.db_catalog_inval_messages_size() == 0) {
    LOG(INFO) << "empty db_catalog_inval_messages, debug_id: " << debug_id;
    return;
  }
  uint32_t current_db_oid = 0;
  // ysql_db_invalidation_messages_map_ is just an extended history of pg_yb_invalidation_messages
  // except message_time column. Merge the incoming db_catalog_inval_messages_data from the
  // heartbeat response which has an ordered array of (db_oid, current_version, messages) into
  // ysql_db_invalidation_messages_map_.
  int i = 0;
  int current_start_index = 0;
  for (; i < db_catalog_inval_messages_data.db_catalog_inval_messages_size(); ++i) {
    const auto& db_inval_messages = db_catalog_inval_messages_data.db_catalog_inval_messages(i);
    const uint32_t db_oid = db_inval_messages.db_oid();
    // db_catalog_inval_messages_data has the message history in a sorted order of
    // (db_oid, current_version).
    if (current_db_oid != db_oid) {
      // We see a new db_oid. If we have a valid current_db_oid, it means we have just saw all of
      // current_db_oid's messages, merge current_db_oid's incoming messages into the message
      // queue of current_db_oid.
      if (current_db_oid > 0) {
        MergeInvalMessagesIntoQueueUnlocked(current_db_oid, db_catalog_inval_messages_data,
                                            current_start_index, i, debug_id);
      }
      // Remember the new db_oid as current_db_oid, and also where its messages start.
      current_db_oid = db_oid;
      current_start_index = i;
    }
  }
  // Merge the last db_oid's list.
  DCHECK_GT(current_db_oid, 0);
  DCHECK_LT(current_start_index, i);
  MergeInvalMessagesIntoQueueUnlocked(current_db_oid, db_catalog_inval_messages_data,
                                      current_start_index, i, debug_id);
}

/*
 * We need to merge the incoming messages into the existing queue. This merge assumes both
 * the existing queue and the incoming messages are sorted by catalog version. It uses a
 * simple merge sort like stragety but it is general enough to take care of all of the
 * following situations:
 * (1) due to garbage collection of tserver invalidation messages, some incoming messages
 * may not make the cutoff version and will be skipped/discarded.
 * (2) due to network reordering, an older heartbeat response may arrive earlier than a newer
 * heartbeat response. For example, for db_oid, old versions in pg_yb_invalidation_messages
 * might be v1, v2, new versions in pg_yb_invalidation_messages may be v1, v2, v3, v4, but we
 * may see v1, v2, v3, v4 first and put them into the queue. Later we see v1, v2, the merge
 * algorithm will simply skip them.
 * (3) we assign message timestamps and delete old messages in pg_yb_invalidation_messages,
 * it is possible that a higher version has been assigned a lower timestamp. As a result,
 * for example if pg_yb_invalidation_messages has a history of v1, v2, v3, v4, after deleting
 * old messages, we may end up with v1, v3, v4, if timestamp(v2) < timestamp(v1).
 * If we combine (2) and (3), we may have a situation that we first see response with v1, v3, v4
 * (new response due to network reordering), then see a response with v1, v2, v3, v4. The merge
 * algorithm will skip v1, v3, v4 but will insert v2 into its right spot in the queue.
 */
void TabletServer::MergeInvalMessagesIntoQueueUnlocked(
    uint32_t db_oid,
    const master::DBCatalogInvalMessagesDataPB& db_catalog_inval_messages_data,
    int start_index, int end_index,
    uint64_t debug_id) {
  DCHECK_LT(start_index, end_index);

  VLOG(2) << "merging inval messages for db: " << db_oid << ", debug_id: " << debug_id;
  auto it = ysql_db_invalidation_messages_map_.find(db_oid);
  if (it == ysql_db_invalidation_messages_map_.end()) {
    // The db_oid does not exist in ysql_db_invalidation_messages_map_ yet. This is possible
    // because at master side we read pg_yb_catalog_version and pg_yb_invalidation_messages
    // separately (not a transactional read). As a result, a newly created database may not
    // have been entered into pg_yb_catalog_version yet at the time when master reading
    // pg_yb_catalog_version but by the time master reads pg_yb_invalidation_messages
    // the new database is already inserted there. This case should be rare so we
    // skip processing this db_oid.
    LOG(WARNING) << "db_oid " << db_oid << " not found in ysql_db_invalidation_messages_map_"
                 << ", debug_id: " << debug_id;
    return;
  }
  const auto cutoff_catalog_version = it->second.cutoff_catalog_version;
  // Skip those incoming versions that are <= cutoff version.
  for (; start_index < end_index; ++start_index) {
    const auto& db_inval_messages =
      db_catalog_inval_messages_data.db_catalog_inval_messages(start_index);
    const uint64_t current_version = db_inval_messages.current_version();
    if (current_version > cutoff_catalog_version) {
      break;
    }
  }
  // If none of the incoming versions can make the cutoff catalog version, we can skip all of
  // the incoming messages.
  if (start_index == end_index) {
    return;
  }

  // We do need to perform a merge, because both the queue and the incoming messages are sorted by
  // version, it is similar to a merge sort strategy.
  DoMergeInvalMessagesIntoQueueUnlocked(
      db_oid, db_catalog_inval_messages_data, start_index, end_index, &it->second.queue, debug_id);
}

void TabletServer::DoMergeInvalMessagesIntoQueueUnlocked(
    uint32_t db_oid,
    const master::DBCatalogInvalMessagesDataPB& db_catalog_inval_messages_data,
    int start_index, int end_index, InvalidationMessagesQueue *db_message_lists,
    uint64_t debug_id) {
  bool changed = false;
  auto it = db_message_lists->begin();
  CoarseTimePoint now = CoarseMonoClock::Now();
  // Scan through each incoming pair, and insert it into the queue in the right position if
  // it does not already exist in the queue.
  while (start_index < end_index) {
    if (it == db_message_lists->end()) {
      break;
    }
    // Get the current incoming pair and the current existing version.
    const auto& db_inval_messages =
        db_catalog_inval_messages_data.db_catalog_inval_messages(start_index);
    const uint64_t incoming_version = db_inval_messages.current_version();
    const std::optional<std::string>& incoming_message_list =
        db_inval_messages.has_message_list() ?
        std::optional<std::string>(db_inval_messages.message_list()) : std::nullopt;
    const auto existing_version = std::get<0>(*it);

    // Compare the incoming version with the current existing one.
    if (incoming_version == existing_version) {
      VLOG(2) << "found existing version " << incoming_version << ", debug_id: " << debug_id;
      if (incoming_message_list != std::get<1>(*it)) {
        // same version should have same message.
        LOG(DFATAL) << "message_list mismatch: " << existing_version << ", debug_id: " << debug_id;
      }
      // Advance both "pointers".
      ++it;
      ++start_index;
    } else if (incoming_version < existing_version) {
      std::string msg_info =
          incoming_message_list.has_value() ? std::to_string(incoming_message_list.value().size())
                                            : "nullopt";
      // The incoming version is lower, insert before the iterator.
      LOG(INFO) << "inserting version " << incoming_version << ", incoming_message_list: "
                << msg_info << " before existing version " << existing_version
                << ", db " << db_oid << ", debug_id: " << debug_id;
      it = db_message_lists->insert(it,
          std::make_tuple(incoming_version, incoming_message_list, now));
      changed = true;
      // After insertion, it points to the newly inserted incoming version, advance it to the
      // original existing version.
      ++it;
      ++start_index;
      DCHECK_EQ(std::get<0>(*it), existing_version);
    } else {
      // The incoming version is higher, move iterator to the next existing slot in the queue.
      // Keep start_index unchanged so that it can be compared with the next slot in the queue.
      VLOG(2) << "existing version: " << existing_version
              << ", higher incoming version: " << incoming_version << ", debug_id: " << debug_id;
      ++it;
    }
  }
  // Take care of the remainig incoming messages if any by appending them to the queue.
  for (; start_index < end_index; ++start_index) {
    const auto& db_inval_messages =
        db_catalog_inval_messages_data.db_catalog_inval_messages(start_index);
    const uint64_t current_version = db_inval_messages.current_version();
    const std::optional<std::string>& message_list = db_inval_messages.has_message_list() ?
        std::optional<std::string>(db_inval_messages.message_list()) : std::nullopt;
    std::string msg_info = message_list.has_value() ? std::to_string(message_list.value().size())
                                                    : "nullopt";
    LOG(INFO) << "appending version " << current_version << ", message_list: " << msg_info
              << ", db " << db_oid << ", debug_id: " << debug_id;
    db_message_lists->emplace_back(current_version, message_list, now);
    changed = true;
  }
  if (changed) {
    LOG(INFO) << "queue size: " << db_message_lists->size() << ", debug_id: " << debug_id;
    // We may have added more messages to the queue that exceeded the max size.
    while (db_message_lists->size() > FLAGS_ysql_max_invalidation_message_queue_size) {
      db_message_lists->pop_front();
    }
  }
}

void TabletServer::MaybeClearInvalidationMessageQueueUnlocked(
    uint32_t db_oid,
    const std::vector<uint64_t>& local_catalog_versions,
    std::map<uint32_t, std::vector<uint64_t>> *garbage_collected_db_versions,
    InvalidationMessagesInfo *info) {
  const uint64_t last_queue_version = std::get<0>(info->queue.back());
  // Remember the known maximum queue catalog version, any new connection to db_oid
  // will start with a catalog version from at least this version.
  info->cutoff_catalog_version = std::max(info->cutoff_catalog_version, last_queue_version);
  size_t sz = local_catalog_versions.size();
  if (sz > 0) {
    // Remember the known maximum local catalog version, any new connection to db_oid
    // will start with a catalog version from at least this version.
    const uint64_t last_local_version = local_catalog_versions[sz - 1];
    info->cutoff_catalog_version = std::max(info->cutoff_catalog_version, last_local_version);
  }
  // GC those entries from the front of the queue whose min retention time has passed.
  // Clearing the queue if all entries have their retention times passed. Note that we
  // only garbage collect messages, but not to erase db_oid from the
  // ysql_db_invalidation_messages_map_. That is taken care of elsewhere when the given
  // database is dropped.
  InvalidationMessagesQueue& db_message_lists = info->queue;
  CoarseTimePoint now = CoarseMonoClock::Now();
  std::vector<uint64_t>* gc_versions = nullptr;
  while (!db_message_lists.empty() &&
         MinimalRetentionTimePassed(std::get<2>(db_message_lists.front()), now)) {
    if (!gc_versions) {
      gc_versions = &((*garbage_collected_db_versions)[db_oid]);
    }
    gc_versions->push_back(std::get<0>(db_message_lists.front()));
    db_message_lists.pop_front();
  }
  if (db_message_lists.empty()) {
    // We push 0 to represent that the entire queue of db_oid is cleared.
    if (!gc_versions) {
      gc_versions = &((*garbage_collected_db_versions)[db_oid]);
    }
    *gc_versions = std::vector<uint64_t>(1, 0);
  }
}

void TabletServer::DoGarbageCollectionOfInvalidationMessages(
    const std::map<uint32_t, std::vector<uint64_t>>& db_local_catalog_versions_map,
    std::map<uint32_t, std::vector<uint64_t>> *garbage_collected_db_versions,
    std::map<uint32_t, uint64_t> *db_cutoff_catalog_versions) {
  std::lock_guard l(lock_);

  // Do garbage collection for each database in ysql_db_invalidation_messages_map_.
  for (auto& [db_oid, inval_msg] : ysql_db_invalidation_messages_map_) {
    InvalidationMessagesQueue& db_message_lists = inval_msg.queue;

    // If the message queue is already empty, there is nothing to garbage collect.
    if (db_message_lists.empty()) {
      // Return the current cutoff version that was recorded for this queue.
      (*db_cutoff_catalog_versions)[db_oid] = inval_msg.cutoff_catalog_version;
      continue;
    }

    // We do not do frequent garbage collections, take this chance to verify that the
    // queue is in sorted order of catalog versions.
    for (size_t i = 1; i < db_message_lists.size(); ++i) {
      DCHECK_LT(std::get<0>(db_message_lists[i-1]), std::get<0>(db_message_lists[i]))
          << i << " " << db_message_lists.size();
    }

    auto it = db_local_catalog_versions_map.find(db_oid);

    // If db_oid does not exist in db_local_catalog_versions_map, it implies that this
    // database has no connections. Note that if db_local_catalog_versions_map is empty,
    // it means there are no connections at all and we will end up garbage collecting
    // all the databases in the entire ysql_db_invalidation_messages_map_ (however that
    // should not happen because the local catalog version query itself will trigger a
    // connection).
    if (it == db_local_catalog_versions_map.end()) {
      MaybeClearInvalidationMessageQueueUnlocked(
          db_oid, std::vector<uint64_t>(), garbage_collected_db_versions, &inval_msg);
      (*db_cutoff_catalog_versions)[db_oid] = inval_msg.cutoff_catalog_version;
      continue;
    }

    const auto& local_catalog_versions = it->second;
    DCHECK(!local_catalog_versions.empty()) << db_oid;

    const auto min_catalog_version = std::get<0>(db_message_lists[0]);

    // If a lagging backend's local catalog version is less than min_catalog_version - 1,
    // it cannot do incremental catalog cache refresh because it would need the version
    // min_catalog_version - 1 which is no longer available in the queue db_message_lists.
    // We count such a backend as a far lagging backend.
    const auto far_lagging_backend_version = min_catalog_version -1;
    // Because local_catalog_versions is a sorted vector of local catalog versions, we can
    // use std::lower_bound to find the first version that is >= far_lagging_backend_version.
    // Those skipped versions are all < far_lagging_backend_version, so they are all far
    // lagging behind backends.
    auto non_far_lagging_begin =
        std::lower_bound(local_catalog_versions.begin(), local_catalog_versions.end(),
                         far_lagging_backend_version);
    if (non_far_lagging_begin == local_catalog_versions.end()) {
      // If all backends are far lagging for this database, db_message_lists is not useful
      // any more.
      MaybeClearInvalidationMessageQueueUnlocked(
          db_oid, local_catalog_versions, garbage_collected_db_versions, &inval_msg);
    } else {
      CoarseTimePoint now = CoarseMonoClock::Now();
      const auto most_lagging_version = *non_far_lagging_begin;
      // We can garbage collect versions <= most_lagging_version, because the backend that has
      // most_lagging_version will need from most_lagging_version + 1 for doing incremental
      // catalog cache refresh. Those <= most_lagging_version are no longer needed.
      while (!db_message_lists.empty() &&
             std::get<0>(db_message_lists.front()) <= most_lagging_version &&
             MinimalRetentionTimePassed(std::get<2>(db_message_lists.front()), now)) {
        (*garbage_collected_db_versions)[db_oid].push_back(std::get<0>(db_message_lists.front()));
        db_message_lists.pop_front();
      }
      inval_msg.cutoff_catalog_version =
          std::max(inval_msg.cutoff_catalog_version, most_lagging_version);
    }
    (*db_cutoff_catalog_versions)[db_oid] = inval_msg.cutoff_catalog_version;
  }
}

Status TabletServer::CheckYsqlLaggingCatalogVersions() {
  auto deadline = CoarseMonoClock::Now() + default_client_timeout();
  auto pg_conn = VERIFY_RESULT(CreateInternalPGConn("template1", deadline));
  const std::string query = "SELECT datid, local_catalog_version FROM "
                            "yb_pg_stat_get_backend_local_catalog_version(NULL) "
                            "ORDER BY datid ASC, local_catalog_version ASC";
  auto rows = VERIFY_RESULT((pg_conn.FetchRows<pgwrapper::PGOid, pgwrapper::PGUint64>(query)));
  std::map<uint32_t, std::vector<uint64_t>> db_local_catalog_versions_map;
  for (const auto& [datid, local_catalog_version] : rows) {
    db_local_catalog_versions_map[datid].push_back(local_catalog_version);
  }
  LOG(INFO) << "db_local_catalog_versions_map: " << yb::ToString(db_local_catalog_versions_map);
  std::map<uint32_t, std::vector<uint64_t>> garbage_collected_db_versions;
  std::map<uint32_t, uint64_t> db_cutoff_catalog_versions;
  DoGarbageCollectionOfInvalidationMessages(
      db_local_catalog_versions_map, &garbage_collected_db_versions, &db_cutoff_catalog_versions);
  LOG(INFO) << "garbage_collected_db_versions: " << yb::ToString(garbage_collected_db_versions);
  LOG(INFO) << "db_cutoff_catalog_versions: " << yb::ToString(db_cutoff_catalog_versions);
  return Status::OK();
}

void TabletServer::WriteServerMetaCacheAsJson(JsonWriter* writer) {
  writer->StartObject();

  DbServerBase::WriteMainMetaCacheAsJson(writer);
  if (auto xcluster_consumer = GetXClusterConsumer()) {
    xcluster_consumer->WriteServerMetaCacheAsJson(*writer);
  }

  writer->EndObject();
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

PgMutationCounter& TabletServer::GetPgNodeLevelMutationCounter() {
  return pg_node_level_mutation_counter_;
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

  std::string leader = heartbeater_->get_master_leader_hostport().ToString();
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
  if (!pg_client_service) {
    return;
  }

  LOG(INFO) << "Invalidating the entire PgTableCache cache since catalog version incremented";
  pg_client_service->impl.InvalidateTableCache();
}

void TabletServer::InvalidatePgTableCache(
    const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
    const std::unordered_set<uint32_t>& db_oids_deleted) {
  auto pg_client_service = pg_client_service_.lock();
  if (pg_client_service) {
    string msg = "Invalidating db PgTableCache caches since ";
    if (!db_oids_updated.empty()) {
      msg += Format("catalog version incremented for $0 ", yb::ToString(db_oids_updated));
    }
    if (!db_oids_deleted.empty()) {
      msg += Format("databases $0 are removed", yb::ToString(db_oids_deleted));
    }
    LOG(INFO) << msg;
    pg_client_service->impl.InvalidateTableCache(db_oids_updated, db_oids_deleted);
  }
}
Status TabletServer::SetupMessengerBuilder(rpc::MessengerBuilder* builder) {
  RETURN_NOT_OK(DbServerBase::SetupMessengerBuilder(builder));

  secure_context_ = VERIFY_RESULT(rpc::SetupInternalSecureContext(
      options_.HostsString(), fs_manager_->GetDefaultRootDir(), builder));

  return Status::OK();
}

XClusterConsumerIf* TabletServer::GetXClusterConsumer() const {
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
  std::lock_guard l(xcluster_consumer_mutex_);
  SCHECK(!xcluster_consumer_, IllegalState, "XCluster consumer already exists");

  auto get_leader_term = [this](const TabletId& tablet_id) {
    auto tablet_peer = tablet_manager_->LookupTablet(tablet_id);
    if (!tablet_peer) {
      return yb::OpId::kUnknownTerm;
    }
    return tablet_peer->LeaderTerm();
  };
  auto connect_to_pg = [this](const std::string& database_name, const CoarseTimePoint& deadline) {
    return pgwrapper::CreateInternalPGConnBuilder(
               pgsql_proxy_bind_address(), database_name, GetSharedMemoryPostgresAuthKey(),
               deadline)
        .Connect();
  };
  auto get_namespace_info =
      [this](const TabletId& tablet_id) -> Result<std::pair<NamespaceId, NamespaceName>> {
    auto tablet_peer = tablet_manager_->LookupTablet(tablet_id);
    SCHECK(tablet_peer, NotFound, "Could not find tablet $0", tablet_id);
    return std::make_pair(
        VERIFY_RESULT(tablet_peer->GetNamespaceId()),
        tablet_peer->tablet_metadata()->namespace_name());
  };

  xcluster_consumer_ = VERIFY_RESULT(tserver::CreateXClusterConsumer(
      std::move(get_leader_term), permanent_uuid(), *client(), std::move(connect_to_pg),
      std::move(get_namespace_info), GetXClusterContext(), metric_entity()));

  return Status::OK();
}

Status TabletServer::XClusterPopulateMasterHeartbeatRequest(
    master::TSHeartbeatRequestPB& req, bool needs_full_tablet_report) {
  // If a full report is needed, we will populate it via the metric data provider.
  if (!needs_full_tablet_report && FLAGS_tserver_heartbeat_add_replication_status) {
    auto xcluster_consumer = GetXClusterConsumer();
    if (xcluster_consumer) {
      xcluster_consumer->PopulateMasterHeartbeatRequest(&req, needs_full_tablet_report);
    }
  }

  return Status::OK();
}

Status TabletServer::XClusterHandleMasterHeartbeatResponse(
    const master::TSHeartbeatResponsePB& resp) {
  xcluster_context_->UpdateSafeTimeMap(resp.xcluster_namespace_to_safe_time());
  xcluster_context_->UpdateXClusterInfoPerNamespace(
      resp.xcluster_heartbeat_info().xcluster_info_per_namespace());

  auto* xcluster_consumer = GetXClusterConsumer();

  // Only create a xcluster consumer if consumer_registry is not null.
  const cdc::ConsumerRegistryPB* consumer_registry = nullptr;
  if (resp.has_consumer_registry()) {
    consumer_registry = &resp.consumer_registry();

    if (!xcluster_consumer) {
      RETURN_NOT_OK(CreateXClusterConsumer());
      xcluster_consumer = GetXClusterConsumer();
    }
  }

  if (xcluster_consumer) {
    int32_t cluster_config_version = -1;
    if (resp.has_cluster_config_version()) {
      cluster_config_version = resp.cluster_config_version();
    }

    xcluster_consumer->HandleMasterHeartbeatResponse(consumer_registry, cluster_config_version);
  }

  // Check whether the cluster is a producer of a CDC stream.
  if (resp.has_xcluster_enabled_on_producer() && resp.xcluster_enabled_on_producer()) {
    RETURN_NOT_OK(SetCDCServiceEnabled());
  }

  if (resp.has_xcluster_producer_registry() && resp.has_xcluster_config_version()) {
    RETURN_NOT_OK(SetPausedXClusterProducerStreams(
        resp.xcluster_producer_registry().paused_producer_stream_ids(),
        resp.xcluster_config_version()));
  }

  return Status::OK();
}

Status TabletServer::ClearUniverseUuid() {
  auto instance_universe_uuid_str = VERIFY_RESULT(
      fs_manager_->GetUniverseUuidFromTserverInstanceMetadata());
  auto instance_universe_uuid = VERIFY_RESULT(UniverseUuid::FromString(instance_universe_uuid_str));
  SCHECK_EQ(false, instance_universe_uuid.IsNil(), IllegalState,
      "universe_uuid is not set in instance metadata");
  return fs_manager_->ClearUniverseUuidOnTserverInstanceMetadata();
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

  RETURN_NOT_OK(rpc::ReloadSecureContextKeysAndCertificates(
      secure_context_.get(), fs_manager_->GetDefaultRootDir(), rpc::SecureContextType::kInternal,
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

void TabletServer::RegisterPgProcessRestarter(std::function<Status(void)> restarter) {
  pg_restarter_ = std::move(restarter);
}

void TabletServer::RegisterPgProcessKiller(std::function<Status(void)> killer) {
  pg_killer_ = std::move(killer);
}

Status TabletServer::StartYSQLLeaseRefresher() {
  return ysql_lease_poller_->Start();
}

Status TabletServer::SetCDCServiceEnabled() {
  if (!cdc_service_) {
    LOG(WARNING) << "CDC Service Not Registered";
  } else {
    cdc_service_->SetCDCServiceEnabled();
  }
  return Status::OK();
}

TserverXClusterContextIf& TabletServer::GetXClusterContext() const {
  return *xcluster_context_;
}

void TabletServer::ScheduleCheckLaggingCatalogVersions() {
  LOG(INFO) << __func__;
  messenger()->scheduler().Schedule(
    [this](const Status& status) {
      if (!status.ok()) {
        LOG(INFO) << status;
        return;
      }
      auto s = CheckYsqlLaggingCatalogVersions();
      if (!s.ok()) {
        LOG(WARNING) << "Could not get the set of lagging catalog versions: " << s;
      }
      ScheduleCheckLaggingCatalogVersions();
    },
    std::chrono::seconds(FLAGS_check_lagging_catalog_versions_interval_secs));
}

void TabletServer::SetCQLServer(yb::server::RpcAndWebServerBase* server,
      server::YCQLStatementStatsProvider* stmt_provider) {
  DCHECK_EQ(cql_server_.load(), nullptr);
  DCHECK_EQ(cql_stmt_provider_.load(), nullptr);

  cql_server_.store(server);
  cql_stmt_provider_.store(stmt_provider);
}

Status TabletServer::YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
      tserver::PgYCQLStatementStatsResponsePB* resp) const {
    auto* cql_stmt_provider = cql_stmt_provider_.load();
    SCHECK_NOTNULL(cql_stmt_provider);
    RETURN_NOT_OK(cql_stmt_provider->YCQLStatementStats(req, resp));
    return Status::OK();
}

rpc::Messenger* TabletServer::GetMessenger(ash::Component component) const {
  switch (component) {
    case ash::Component::kYSQL:
    case ash::Component::kMaster:
      return nullptr;
    case ash::Component::kTServer:
      return messenger();
    case ash::Component::kYCQL:
      auto cql_server = cql_server_.load();
      return (cql_server ? cql_server->messenger() : nullptr);
  }
  FATAL_INVALID_ENUM_VALUE(ash::Component, component);
}

void TabletServer::ClearAllMetaCachesOnServer() {
  if (auto xcluster_consumer = GetXClusterConsumer()) {
    xcluster_consumer->ClearAllClientMetaCaches();
  }
  client()->ClearAllMetaCachesOnServer();
}

Status TabletServer::ClearMetacache(const std::string& namespace_id) {
  return client()->ClearMetacache(namespace_id);
}

Result<std::vector<tablet::TabletStatusPB>> TabletServer::GetLocalTabletsMetadata() const {
  std::vector<tablet::TabletStatusPB> result;
  auto peers = tablet_manager_.get()->GetTabletPeers();
  for (const std::shared_ptr<tablet::TabletPeer>& peer : peers) {
    tablet::TabletStatusPB status;
    peer->GetTabletStatusPB(&status);
    status.set_pgschema_name(peer->status_listener()->schema()->SchemaName());
    result.emplace_back(std::move(status));
  }
  return result;
}

Result<std::vector<TserverMetricsInfoPB>> TabletServer::GetMetrics() const {
  std::vector<TserverMetricsInfoPB> result;

  std::vector<double> cpu_usage = VERIFY_RESULT(MetricsSnapshotter::GetCpuUsageInInterval(500));
  TserverMetricsInfoPB cpu_usage_user;
  cpu_usage_user.set_name("cpu_usage_user");
  TserverMetricsInfoPB cpu_usage_system;
  cpu_usage_system.set_name("cpu_usage_system");
  cpu_usage_user.set_value(std::to_string(cpu_usage[0]));
  cpu_usage_system.set_value(std::to_string(cpu_usage[1]));
  result.emplace_back(std::move(cpu_usage_user));
  result.emplace_back(std::move(cpu_usage_system));

  std::vector<uint64_t> memory_usage = VERIFY_RESULT(MetricsSnapshotter::GetMemoryUsage());
  TserverMetricsInfoPB node_memory_total;
  node_memory_total.set_name("memory_total");
  node_memory_total.set_value(std::to_string(memory_usage[0]));
  result.emplace_back(std::move(node_memory_total));
  TserverMetricsInfoPB  node_memory_free;
  node_memory_free.set_name("memory_free");
  node_memory_free.set_value(std::to_string(memory_usage[1]));
  result.emplace_back(std::move(node_memory_free));
  TserverMetricsInfoPB  node_memory_available;
  node_memory_available.set_name("memory_available");
  node_memory_available.set_value(std::to_string(memory_usage[2]));
  result.emplace_back(std::move(node_memory_available));

  auto root_mem_tracker = MemTracker::GetRootTracker();
  int64_t tserver_root_memory_consumption = root_mem_tracker->consumption();
  int64_t tserver_root_memory_limit = root_mem_tracker->limit();
  int64_t tserver_root_memory_soft_limit = root_mem_tracker->soft_limit();
  TserverMetricsInfoPB tserver_root_memory_consumption_metric;
  tserver_root_memory_consumption_metric.set_name("tserver_root_memory_consumption");
  tserver_root_memory_consumption_metric.set_value(
    std::to_string(tserver_root_memory_consumption));
  result.emplace_back(std::move(tserver_root_memory_consumption_metric));
  TserverMetricsInfoPB tserver_root_memory_limit_metric;
  tserver_root_memory_limit_metric.set_name("tserver_root_memory_limit");
  tserver_root_memory_limit_metric.set_value(std::to_string(tserver_root_memory_limit));
  result.emplace_back(std::move(tserver_root_memory_limit_metric));
  TserverMetricsInfoPB tserver_root_memory_soft_limit_metric;
  tserver_root_memory_soft_limit_metric.set_name("tserver_root_memory_soft_limit");
  tserver_root_memory_soft_limit_metric.set_value(std::to_string(tserver_root_memory_soft_limit));
  result.emplace_back(std::move(tserver_root_memory_soft_limit_metric));

  return result;
}

void TabletServer::SetCronLeaderLease(MonoTime cron_leader_lease_end) {
  SharedObject()->SetCronLeaderLease(cron_leader_lease_end);
}

Result<pgwrapper::PGConn> TabletServer::CreateInternalPGConn(
    const std::string& database_name, const std::optional<CoarseTimePoint>& deadline) {
  return pgwrapper::CreateInternalPGConnBuilder(
             pgsql_proxy_bind_address(), database_name, GetSharedMemoryPostgresAuthKey(), deadline)
      .Connect();
}

Result<PgTxnSnapshot> TabletServer::GetLocalPgTxnSnapshot(const PgTxnSnapshotLocalId& snapshot_id) {
  auto pg_client_service = pg_client_service_.lock();
  RSTATUS_DCHECK(pg_client_service, InternalError, "Unable to get pg_client_service");
  return pg_client_service->impl.GetLocalPgTxnSnapshot(snapshot_id);
}

Result<std::string> TabletServer::GetUniverseUuid() const {
  return fs_manager_->GetUniverseUuidFromTserverInstanceMetadata();
}

PgClientServiceImpl* TabletServer::TEST_GetPgClientService() {
  auto holder = pg_client_service_.lock();
  return holder ? &holder->impl : nullptr;
}

PgClientServiceMockImpl* TabletServer::TEST_GetPgClientServiceMock() {
  auto holder = pg_client_service_.lock();
  return holder && holder->mock.has_value() ? &holder->mock.value() : nullptr;
}

}  // namespace yb::tserver
