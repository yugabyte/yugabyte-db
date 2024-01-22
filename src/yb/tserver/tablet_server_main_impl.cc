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

#include "yb/tserver/tablet_server_main_impl.h"

#include <chrono>
#include <iostream>

#include <boost/optional/optional.hpp>

#include "yb/common/termination_monitor.h"
#include "yb/common/llvm_profile_dumper.h"

#include "yb/consensus/log_util.h"
#include "yb/consensus/consensus_queue.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/yql/cql/cqlserver/cql_server.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/process_wrapper/process_wrapper.h"
#include "yb/yql/redis/redisserver/redis_server.h"
#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_wrapper.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/tserver/tserver_call_home.h"
#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/skewed_clock.h"
#include "yb/server/secure.h"
#include "yb/tserver/factory.h"
#include "yb/tserver/metrics_snapshotter.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/main_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/result.h"
#include "yb/util/ulimit_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_log.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/thread.h"
#include "yb/util/port_picker.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/tserver/server_main_util.h"

using std::string;
using namespace std::placeholders;

using yb::redisserver::RedisServer;
using yb::redisserver::RedisServerOptions;

using yb::cqlserver::CQLServer;
using yb::cqlserver::CQLServerOptions;

using yb::pgwrapper::PgProcessConf;
using yb::pgwrapper::PgSupervisor;

using namespace yb::size_literals;  // NOLINT
using namespace std::chrono_literals;

DEFINE_NON_RUNTIME_bool(start_redis_proxy, true,
    "Starts a redis proxy along with the tablet server");

DEFINE_NON_RUNTIME_bool(start_cql_proxy, true, "Starts a CQL proxy along with the tablet server");
DEFINE_NON_RUNTIME_string(cql_proxy_broadcast_rpc_address, "",
              "RPC address to broadcast to other nodes. This is the broadcast_address used in the"
                  " system.local table");

DEFINE_NON_RUNTIME_bool(start_pgsql_proxy, false,
            "Whether to run a PostgreSQL server as a child process of the tablet server");

DECLARE_string(rpc_bind_addresses);
DECLARE_bool(callhome_enabled);
DECLARE_int32(webserver_port);
DECLARE_int32(logbuflevel);
DECLARE_int32(stderrthreshold);

DECLARE_string(redis_proxy_bind_address);
DECLARE_int32(redis_proxy_webserver_port);

DECLARE_string(cql_proxy_bind_address);
DECLARE_int32(cql_proxy_webserver_port);

DECLARE_string(pgsql_proxy_bind_address);

DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);

DECLARE_bool(use_client_to_server_encryption);
DECLARE_string(certs_dir);
DECLARE_string(certs_for_client_dir);
DECLARE_string(cert_node_filename);
DECLARE_string(ysql_hba_conf);
DECLARE_string(ysql_pg_conf);
DECLARE_string(metric_node_name);
DECLARE_bool(enable_ysql_conn_mgr);
DECLARE_bool(enable_ysql);
DECLARE_bool(enable_ysql_conn_mgr_stats);
DECLARE_uint32(ysql_conn_mgr_port);
DECLARE_bool(ysql_conn_mgr_use_unix_conn);


namespace yb {
namespace tserver {

void SetProxyAddress(std::string* flag, const std::string& name,
  uint16_t port, bool override_port = false) {
  if (flag->empty() || override_port) {
    std::vector<HostPort> bind_addresses;
    Status status = HostPort::ParseStrings(FLAGS_rpc_bind_addresses, 0, &bind_addresses);
    LOG_IF(DFATAL, !status.ok()) << "Bad public IPs " << FLAGS_rpc_bind_addresses << ": " << status;
    if (!bind_addresses.empty()) {
      for (auto& addr : bind_addresses) {
        addr.set_port(port);
      }
      *flag = HostPort::ToCommaSeparatedString(bind_addresses);
      LOG(INFO) << "Reset " << name << " bind address to " << *flag;
    }
  }
}

// Helper function to set the proxy rpc addresses based on rpc_bind_addresses.
void SetProxyAddresses() {
  LOG(INFO) << "Using parsed rpc = " << FLAGS_rpc_bind_addresses;
  SetProxyAddress(&FLAGS_redis_proxy_bind_address, "YEDIS", RedisServer::kDefaultPort);
  SetProxyAddress(&FLAGS_cql_proxy_bind_address, "YCQL", CQLServer::kDefaultPort);
  if (!FLAGS_enable_ysql_conn_mgr) {
    SetProxyAddress(&FLAGS_pgsql_proxy_bind_address, "YSQL", PgProcessConf::kDefaultPort);
    return;
  }

  // Pick a random for postgres if not provided or clashig with 5433
  PortPicker pp;
  uint16_t freeport = pp.AllocateFreePort();
  if (!FLAGS_pgsql_proxy_bind_address.empty()) {
    std::vector<HostPort> bind_addresses;
    Status status = HostPort::ParseStrings(FLAGS_pgsql_proxy_bind_address, 0, &bind_addresses);
    LOG_IF(DFATAL, !status.ok())
    << "Bad pgsql_proxy_bind_address " << FLAGS_pgsql_proxy_bind_address << ": " << status;
    for (const auto& addr : bind_addresses) {
      if (addr.port() == PgProcessConf::kDefaultPort && addr.port() == FLAGS_ysql_conn_mgr_port) {
        LOG(INFO) << "The connection manager and backend db ports are conflicting on "
        << addr.port() << ". Assigning a random available port: " << freeport <<
        " to backend db and " << PgProcessConf::kDefaultPort << " to the connection manager";
        SetProxyAddress(&FLAGS_pgsql_proxy_bind_address, "YSQL", freeport, true);
        break;
      }
    }
  }

  yb::HostPort postgres_address;
  CHECK_OK(postgres_address.ParseString(FLAGS_pgsql_proxy_bind_address, freeport));
  LOG(INFO) << "ysql connection manager is enabled";
  LOG(INFO) << "Using pgsql_proxy_bind_address = " << FLAGS_pgsql_proxy_bind_address;
  LOG(INFO) << "Using ysql_connection_manager port = " << FLAGS_ysql_conn_mgr_port;
}

Status SetSslConf(const std::unique_ptr<TabletServer> &server,
    yb::ProcessWrapperCommonConfig* config) {
    config->certs_dir = FLAGS_certs_dir.empty()
       ? server::DefaultCertsDir(*server->fs_manager())
       : FLAGS_certs_dir;
    config->certs_for_client_dir = FLAGS_certs_for_client_dir.empty()
       ? config->certs_dir
       : FLAGS_certs_for_client_dir;
    config->enable_tls = FLAGS_use_client_to_server_encryption;

    // Follow the same logic as elsewhere, check FLAGS_cert_node_filename then
    // server_broadcast_addresses then rpc_bind_addresses.
    if (!FLAGS_cert_node_filename.empty()) {
      config->cert_base_name = FLAGS_cert_node_filename;
    } else {
      const auto server_broadcast_addresses =
          HostPort::ParseStrings(server->options().server_broadcast_addresses, 0);
      RETURN_NOT_OK(server_broadcast_addresses);
      const auto rpc_bind_addresses =
          HostPort::ParseStrings(server->options().rpc_opts.rpc_bind_addresses, 0);
      RETURN_NOT_OK(rpc_bind_addresses);
      config->cert_base_name = !server_broadcast_addresses->empty()
                                           ? server_broadcast_addresses->front().host()
                                           : rpc_bind_addresses->front().host();
    }

  return Status::OK();
}

// Runs the IO service in a loop until it is stopped. Invokes trigger_termination_fn if there is an
// error and the IO service has not been stopped.
void RunIOService(
    std::function<void()> trigger_termination_fn, boost::asio::io_service* io_service) {
  // Runs forever unless there is some error or explicitly stopped.
  boost::system::error_code ec;
  io_service->run(ec);

  if (!io_service->stopped()) {
    LOG(WARNING) << "Stopping process as IO service run failed: " << ec;
    trigger_termination_fn();
  }
}

int TabletServerMain(int argc, char** argv) {
#ifndef NDEBUG
  HybridTime::TEST_SetPrettyToString(true);
#endif

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0",
                                                 TabletServer::kDefaultPort);
  FLAGS_webserver_port = TabletServer::kDefaultWebPort;
  FLAGS_redis_proxy_webserver_port = RedisServer::kDefaultWebPort;
  FLAGS_cql_proxy_webserver_port = CQLServer::kDefaultWebPort;

  string host_name;
  if (GetHostname(&host_name).ok()) {
    FLAGS_metric_node_name = strings::Substitute("$0:$1", host_name, TabletServer::kDefaultWebPort);
  } else {
    LOG(INFO) << "Failed to get tablet's host name, keeping default metric_node_name";
  }

  LOG_AND_RETURN_FROM_MAIN_NOT_OK(MasterTServerParseFlagsAndInit(
      TabletServerOptions::kServerType, &argc, &argv));

  auto termination_monitor = TerminationMonitor::Create();

  SetProxyAddresses();

  auto tablet_server_options = TabletServerOptions::CreateTabletServerOptions();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(tablet_server_options);
  Factory factory;

  auto server = factory.CreateTabletServer(*tablet_server_options);
  // ----------------------------------------------------------------------------------------------
  // Starting to instantiate servers
  // ----------------------------------------------------------------------------------------------

  LOG(INFO) << "Initializing tablet server...";
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server->Init());
  LOG(INFO) << "Starting tablet server...";
  UlimitUtil::InitUlimits();
  LOG(INFO) << "ulimit cur(max)..." << UlimitUtil::GetUlimitInfo();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server->Start());
  LOG(INFO) << "Tablet server successfully started.";

  std::unique_ptr<TserverCallHome> call_home;
  call_home = std::make_unique<TserverCallHome>(server.get());
  call_home->ScheduleCallHome();

  std::unique_ptr<PgSupervisor> pg_supervisor;
  if (FLAGS_start_pgsql_proxy || FLAGS_enable_ysql) {
    auto pg_process_conf_result = PgProcessConf::CreateValidateAndRunInitDb(
        FLAGS_pgsql_proxy_bind_address,
        tablet_server_options->fs_opts.data_paths.front() + "/pg_data",
        server->GetSharedMemoryFd());
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(pg_process_conf_result);
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(docdb::DocPgInit());
    auto& pg_process_conf = *pg_process_conf_result;
    pg_process_conf.master_addresses = tablet_server_options->master_addresses_flag;
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(SetSslConf(server, &pg_process_conf));
    LOG(INFO) << "Starting PostgreSQL server listening on "
              << pg_process_conf.listen_addresses << ", port " << pg_process_conf.pg_port;

    pg_supervisor = std::make_unique<PgSupervisor>(pg_process_conf, server.get());
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(pg_supervisor->Start());
  }

  std::unique_ptr<ysql_conn_mgr_wrapper::YsqlConnMgrSupervisor> ysql_conn_mgr_supervisor;
  if (FLAGS_enable_ysql_conn_mgr) {
    LOG(INFO) << "Starting Ysql Connection Manager on port " << FLAGS_ysql_conn_mgr_port;

    ysql_conn_mgr_wrapper::YsqlConnMgrConf ysql_conn_mgr_conf =
        ysql_conn_mgr_wrapper::YsqlConnMgrConf(
          tablet_server_options->fs_opts.data_paths.front());
    ysql_conn_mgr_conf.yb_tserver_key_ = UInt64ToString(
        server->GetSharedMemoryPostgresAuthKey());

    LOG_AND_RETURN_FROM_MAIN_NOT_OK(SetSslConf(server, &ysql_conn_mgr_conf));

    if (FLAGS_use_client_to_server_encryption && !FLAGS_ysql_conn_mgr_use_unix_conn)
      LOG(FATAL) << "Client to server encryption can not be enabled "
                 << " in Ysql Connection Manager with ysql_conn_mgr_use_unix_conn"
                 << " disabled.";

    // Construct the config file for the Ysql Connection Manager process.
    const auto conn_mgr_shmem_key =
        FLAGS_enable_ysql_conn_mgr_stats ? pg_supervisor->GetYsqlConnManagerStatsShmkey() : 0;
    ysql_conn_mgr_supervisor = std::make_unique<ysql_conn_mgr_wrapper::YsqlConnMgrSupervisor>(
        ysql_conn_mgr_conf,
        conn_mgr_shmem_key);

    LOG_AND_RETURN_FROM_MAIN_NOT_OK(ysql_conn_mgr_supervisor->Start());

    // Set the shared memory key for tserver so it can access stats as well.
    server->SetYsqlConnMgrStatsShmemKey(conn_mgr_shmem_key);
  }

  std::unique_ptr<RedisServer> redis_server;
  if (FLAGS_start_redis_proxy) {
    RedisServerOptions redis_server_options;
    redis_server_options.rpc_opts.rpc_bind_addresses = FLAGS_redis_proxy_bind_address;
    redis_server_options.webserver_opts.port = FLAGS_redis_proxy_webserver_port;
    redis_server_options.master_addresses_flag = tablet_server_options->master_addresses_flag;
    redis_server_options.SetMasterAddresses(tablet_server_options->GetMasterAddresses());
    redis_server_options.dump_info_path =
        (tablet_server_options->dump_info_path.empty()
              ? ""
              : tablet_server_options->dump_info_path + "-redis");
    redis_server.reset(new RedisServer(redis_server_options, server.get()));
    LOG(INFO) << "Starting redis server...";
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(redis_server->Start());
    LOG(INFO) << "Redis server successfully started.";
  }

  std::unique_ptr<CQLServer> cql_server;
  std::unique_ptr<boost::asio::io_service> io_service;
  scoped_refptr<Thread> io_service_thread;
  if (FLAGS_start_cql_proxy) {
    CQLServerOptions cql_server_options;
    cql_server_options.rpc_opts.rpc_bind_addresses = FLAGS_cql_proxy_bind_address;
    cql_server_options.broadcast_rpc_address = FLAGS_cql_proxy_broadcast_rpc_address;
    cql_server_options.webserver_opts.port = FLAGS_cql_proxy_webserver_port;
    cql_server_options.master_addresses_flag = tablet_server_options->master_addresses_flag;
    cql_server_options.SetMasterAddresses(tablet_server_options->GetMasterAddresses());
    cql_server_options.dump_info_path =
        (tablet_server_options->dump_info_path.empty()
              ? ""
              : tablet_server_options->dump_info_path + "-cql");

    io_service = std::make_unique<boost::asio::io_service>();
    cql_server = factory.CreateCQLServer(cql_server_options, io_service.get(), server.get());
    LOG(INFO) << "Starting CQL server...";
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(cql_server->Start());
    LOG(INFO) << "CQL server successfully started.";

    LOG_AND_RETURN_FROM_MAIN_NOT_OK(Thread::Create(
        "io_service", "loop", &RunIOService,
        [&termination_monitor]() { termination_monitor->Terminate(); }, io_service.get(),
        &io_service_thread));
  }

  auto llvm_profile_dumper_result = std::make_unique<LlvmProfileDumper>();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(llvm_profile_dumper_result->Start());

  termination_monitor->WaitForTermination();

  llvm_profile_dumper_result.reset();

  if (io_service) {
    io_service->stop();
    if (io_service_thread) {
      LOG_AND_RETURN_FROM_MAIN_NOT_OK(ThreadJoiner(io_service_thread.get()).Join());
    }
  }

  if (cql_server) {
    LOG(WARNING) << "Stopping CQL server";
    cql_server->Shutdown();
  }

  if (redis_server) {
    LOG(WARNING) << "Stopping Redis server";
    redis_server->Shutdown();
  }

  if (pg_supervisor) {
    LOG(WARNING) << "Stopping PostgreSQL";
    pg_supervisor->Stop();
  }

  if (ysql_conn_mgr_supervisor) {
    LOG(WARNING) << "Stopping Ysql Connection Manager process";
    ysql_conn_mgr_supervisor->Stop();
  }

  if (call_home) {
    call_home->Shutdown();
  }

  LOG(WARNING) << "Stopping Tablet server";
  server->Shutdown();

  return EXIT_SUCCESS;
}

}  // namespace tserver
}  // namespace yb
