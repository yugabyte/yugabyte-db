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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/llvm_profile_dumper.h"
#include "yb/common/termination_monitor.h"
#include "yb/common/ysql_operation_lease.h"

#include "yb/consensus/consensus_queue.h"
#include "yb/consensus/log_util.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/skewed_clock.h"

#include "yb/tserver/factory.h"
#include "yb/tserver/metrics_snapshotter.h"
#include "yb/tserver/server_main_util.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_call_home.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/main_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/port_picker.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/ulimit_util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/net/net_util.h"

#include "yb/yql/cql/cqlserver/cql_server.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/process_wrapper/process_wrapper.h"
#include "yb/yql/redis/redisserver/redis_server.h"
#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_wrapper.h"

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

DEFINE_NON_RUNTIME_bool(start_redis_proxy, false,
    "Starts a redis proxy along with the tablet server");

DEFINE_NON_RUNTIME_bool(start_cql_proxy, true, "Starts a CQL proxy along with the tablet server");
DEFINE_NON_RUNTIME_string(cql_proxy_broadcast_rpc_address, "",
              "RPC address to broadcast to other nodes. This is the broadcast_address used in the"
                  " system.local table");

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
DECLARE_string(ysql_hba_conf);
DECLARE_string(metric_node_name);
DECLARE_bool(enable_ysql_conn_mgr);
DECLARE_bool(enable_ysql);
DECLARE_bool(start_pgsql_proxy);
DECLARE_bool(enable_ysql_conn_mgr_stats);
DECLARE_uint32(ysql_conn_mgr_port);
DECLARE_bool(ysql_conn_mgr_use_unix_conn);


namespace yb {
namespace tserver {

const auto kShutdownGraceTimeBeforeDumpingStackThreads = MonoDelta::FromSeconds(30);

void SetProxyAddress(std::string* flag, const std::string& name,
  uint16_t port, bool override_port = false) {
  if (flag->empty() || override_port) {
    std::vector<HostPort> bind_addresses;
    Status status;
    if (flag->empty()) {
      // If the flag is empty, we set ip address to the default rpc bind address.
      status = HostPort::ParseStrings(FLAGS_rpc_bind_addresses, 0, &bind_addresses);
      LOG_IF(DFATAL, !status.ok()) << "Bad public IPs " << FLAGS_rpc_bind_addresses << ": "
             << status;
    } else {
      // If override_port is true, we keep the existing ip addresses and just change the port.
      status = HostPort::ParseStrings(*flag, 0, &bind_addresses);
      LOG_IF(DFATAL, !status.ok()) << "Bad public IPs " << *flag << ": " << status;
    }
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
      // Flag validator PostgresAndYsqlConnMgrPortValidator ensures that the pg port and conn mgr
      // port are either both PgProcessConf::kDefaultPort, or are different.
      // Fixup the pg port so that the ports do not conflict.
      if (addr.port() == FLAGS_ysql_conn_mgr_port) {
        const auto preferred_port = PgProcessConf::kDefaultPortWithConnMgr;
        if (addr.port() != preferred_port) {
          LOG(INFO) << "The connection manager and backend db ports are conflicting on "
                << addr.port() << ". Trying port " << preferred_port << " for backend db.";
          Endpoint preferred_sock_addr(boost::asio::ip::address_v4::loopback(), preferred_port);
          Socket preferred_sock;
          Status preferred_status = preferred_sock.Init(0);
          if (preferred_status.ok() && preferred_sock.Bind(preferred_sock_addr, false).ok()) {
            LOG(INFO) << "Successfully bound to port " << preferred_port << " for backend db.";
            SetProxyAddress(&FLAGS_pgsql_proxy_bind_address, "YSQL", preferred_port, true);
            break;
          }
        }

        LOG(INFO) << "The connection manager and backend db ports are conflicting on "
        << addr.port() << ". Assigning a random available port: " << freeport <<
        " to backend db and " << PgProcessConf::kDefaultPort << " to the connection manager";
        SetProxyAddress(&FLAGS_pgsql_proxy_bind_address, "YSQL", freeport, true);
        break;
      }
    }
  }

  // The port-conflict handling above may have overridden the port in pgsql_proxy_bind_address.
  // Parse and validate that all addresses use the same port (PostgreSQL only supports one).
  const auto parsed = CHECK_RESULT(
      pgwrapper::ParsePgBindAddresses(FLAGS_pgsql_proxy_bind_address, freeport));
  LOG(INFO) << "ysql connection manager is enabled";
  LOG(INFO) << "Using pgsql_proxy_bind_address = " << FLAGS_pgsql_proxy_bind_address;
  LOG(INFO) << "Using ysql backend port = " << parsed.port;
  LOG(INFO) << "Using ysql_connection_manager port = " << FLAGS_ysql_conn_mgr_port;
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

struct Services {
  std::unique_ptr<TerminationMonitor> termination_monitor;
  std::unique_ptr<TabletServer> tablet_server;
  std::unique_ptr<TserverCallHome> call_home;
  std::unique_ptr<PgSupervisor> pg_supervisor;
  std::unique_ptr<ysql_conn_mgr_wrapper::YsqlConnMgrSupervisor> ysql_conn_mgr_supervisor;
  std::unique_ptr<RedisServer> redis_server;
  std::unique_ptr<CQLServer> cql_server;
  std::unique_ptr<boost::asio::io_service> io_service;
  scoped_refptr<Thread> io_service_thread;
  std::unique_ptr<LlvmProfileDumper> llvm_profile_dumper;
};

// StartServices borrows its output as a reference so that the Services object is not allocated
// locally inside StartServices. This way partially initialized services do not have their
// destructors called if StartServices returns early due to an error.
Status StartServices(Services& services) {
  services.termination_monitor = TerminationMonitor::Create();

  SetProxyAddresses();

  auto tablet_server_options = VERIFY_RESULT(TabletServerOptions::CreateTabletServerOptions());
  Factory factory;

  services.tablet_server = factory.CreateTabletServer(tablet_server_options);
  // ----------------------------------------------------------------------------------------------
  // Starting to instantiate servers
  // ----------------------------------------------------------------------------------------------

  LOG(INFO) << "Initializing tablet server...";
  RETURN_NOT_OK(services.tablet_server->Init());
  LOG(INFO) << "Starting tablet server...";
  UlimitUtil::InitUlimits();
  LOG(INFO) << "ulimit cur(max)..." << UlimitUtil::GetUlimitInfo();
  RETURN_NOT_OK(services.tablet_server->Start());
  LOG(INFO) << "Tablet server successfully started.";

  services.tablet_server->SharedObject()->SetPid(getpid());

  // Set the locale to match the YB PG defaults. This should be kept in line with
  // the locale set in the initdb process (setlocales)
  setlocale(LC_ALL, "en_US.UTF-8");
  setlocale(LC_COLLATE, "C");

  services.call_home = std::make_unique<TserverCallHome>(services.tablet_server.get());
  services.call_home->ScheduleCallHome();

  bool ysql_lease_enabled = false;
  bool ysql_conn_mgr_enabled = FLAGS_enable_ysql_conn_mgr;
  if (FLAGS_start_pgsql_proxy || FLAGS_enable_ysql) {
    auto pg_process_conf_result = PgProcessConf::CreateValidateAndRunInitDb(
        FLAGS_pgsql_proxy_bind_address,
        tablet_server_options.fs_opts.data_paths.front() + "/pg_data");
    RETURN_NOT_OK(pg_process_conf_result);
    RETURN_NOT_OK(docdb::DocPgInit());
    auto& pg_process_conf = *pg_process_conf_result;
    pg_process_conf.master_addresses = tablet_server_options.master_addresses_flag;
    RETURN_NOT_OK(pg_process_conf.SetSslConf(
        services.tablet_server->options(), *services.tablet_server->fs_manager()));
    LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses
              << ", port " << pg_process_conf.pg_port;

    services.pg_supervisor =
        std::make_unique<PgSupervisor>(pg_process_conf, services.tablet_server.get());
    ysql_lease_enabled = IsYsqlLeaseEnabled();
    if (ysql_lease_enabled) {
      if (ysql_conn_mgr_enabled) {
        // Normally when the lease is enabled we don't spawn a postgres process until acquiring a
        // lease. However the ysql connection manager seems to peel configuration from the postgres
        // process. Starting up the connection manager fails unless postgres is currently running.
        // So if it's configured, keep the pg process alive while we spawn the ysql connection
        // manager.
        RETURN_NOT_OK(services.pg_supervisor->Start());
      } else {
        // If the ysql lease feature is enabled, we don't want to accept pg connections until the
        // tserver acquires a lease from the master leader.
        RETURN_NOT_OK(services.pg_supervisor->InitPaused());
        RETURN_NOT_OK(services.tablet_server->StartYSQLLeaseRefresher());
      }
    } else {
      RETURN_NOT_OK(services.pg_supervisor->Start());
      RETURN_NOT_OK(services.tablet_server->StartYSQLLeaseRefresher());
    }
  }

  if (ysql_conn_mgr_enabled) {
    LOG(INFO) << "Starting Ysql Connection Manager on port " << FLAGS_ysql_conn_mgr_port;

    ysql_conn_mgr_wrapper::YsqlConnMgrConf ysql_conn_mgr_conf =
        ysql_conn_mgr_wrapper::YsqlConnMgrConf(
          tablet_server_options.fs_opts.data_paths.front());
    ysql_conn_mgr_conf.yb_tserver_key_ = UInt64ToString(
        services.tablet_server->GetSharedMemoryPostgresAuthKey());

    RETURN_NOT_OK(ysql_conn_mgr_conf.SetSslConf(
        services.tablet_server->options(), *services.tablet_server->fs_manager()));

    if (FLAGS_use_client_to_server_encryption && !FLAGS_ysql_conn_mgr_use_unix_conn)
      LOG(FATAL) << "Client to server encryption can not be enabled "
                 << " in Ysql Connection Manager with ysql_conn_mgr_use_unix_conn"
                 << " disabled.";

    // Construct the config file for the Ysql Connection Manager process.
    const auto conn_mgr_shmem_key = FLAGS_enable_ysql_conn_mgr_stats
                                        ? services.pg_supervisor->GetYsqlConnManagerStatsShmkey()
                                        : 0;
    services.ysql_conn_mgr_supervisor =
        std::make_unique<ysql_conn_mgr_wrapper::YsqlConnMgrSupervisor>(
            ysql_conn_mgr_conf, conn_mgr_shmem_key);

    RETURN_NOT_OK(services.ysql_conn_mgr_supervisor->Start());

    // Set the shared memory key for tserver so it can access stats as well.
    services.tablet_server->SetYsqlConnMgrStatsShmemKey(conn_mgr_shmem_key);
    if (ysql_lease_enabled) {
      // Now that the connection manager has been spawned we can kill the postgres process and wait
      // for a lease.
      RETURN_NOT_OK(services.pg_supervisor->Pause());
      RETURN_NOT_OK(services.tablet_server->StartYSQLLeaseRefresher());
    }
  }

  if (FLAGS_start_redis_proxy) {
    RedisServerOptions redis_server_options;
    redis_server_options.rpc_opts.rpc_bind_addresses = FLAGS_redis_proxy_bind_address;
    redis_server_options.webserver_opts.port = FLAGS_redis_proxy_webserver_port;
    redis_server_options.master_addresses_flag = tablet_server_options.master_addresses_flag;
    redis_server_options.SetMasterAddresses(tablet_server_options.GetMasterAddresses());
    redis_server_options.dump_info_path =
        (tablet_server_options.dump_info_path.empty()
              ? ""
              : tablet_server_options.dump_info_path + "-redis");
    services.redis_server.reset(
        new RedisServer(redis_server_options, services.tablet_server.get()));
    LOG(INFO) << "Starting redis server...";
    RETURN_NOT_OK(services.redis_server->Start());
    LOG(INFO) << "Redis server successfully started.";
  }

  scoped_refptr<Thread> io_service_thread;
  if (FLAGS_start_cql_proxy) {
    CQLServerOptions cql_server_options;
    cql_server_options.rpc_opts.rpc_bind_addresses = FLAGS_cql_proxy_bind_address;
    cql_server_options.broadcast_rpc_address = FLAGS_cql_proxy_broadcast_rpc_address;
    cql_server_options.webserver_opts.port = FLAGS_cql_proxy_webserver_port;
    cql_server_options.master_addresses_flag = tablet_server_options.master_addresses_flag;
    cql_server_options.SetMasterAddresses(tablet_server_options.GetMasterAddresses());
    cql_server_options.dump_info_path =
        (tablet_server_options.dump_info_path.empty()
              ? ""
              : tablet_server_options.dump_info_path + "-cql");

    services.io_service = std::make_unique<boost::asio::io_service>();
    services.cql_server = factory.CreateCQLServer(
        cql_server_options, services.io_service.get(), services.tablet_server.get());
    LOG(INFO) << "Starting CQL server...";
    RETURN_NOT_OK(services.cql_server->Start());
    LOG(INFO) << "CQL server successfully started.";

    auto termination_monitor_p = services.termination_monitor.get();
    RETURN_NOT_OK(Thread::Create(
        "io_service", "loop", &RunIOService,
        [termination_monitor_p]() { termination_monitor_p->Terminate(); },
        services.io_service.get(), &services.io_service_thread));
  }

  services.llvm_profile_dumper = std::make_unique<LlvmProfileDumper>();
  return services.llvm_profile_dumper->Start();
}

Status ShutdownServicesImpl(Services& services) {
  services.llvm_profile_dumper.reset();

  if (services.io_service) {
    services.io_service->stop();
    if (services.io_service_thread) {
      RETURN_NOT_OK(ThreadJoiner(services.io_service_thread.get()).Join());
    }
  }

  if (services.cql_server) {
    LOG(WARNING) << "Stopping CQL server";
    services.cql_server->Shutdown();
  }

  if (services.redis_server) {
    LOG(WARNING) << "Stopping Redis server";
    services.redis_server->Shutdown();
  }

  // We must stop the pg backend supervisor before shutting down the tserver.
  // Otherwise the tserver could give up its lease while it continues to server queries.
  if (services.pg_supervisor) {
    LOG(WARNING) << "Stopping PostgreSQL";
    services.pg_supervisor->Stop();
  }

  if (services.ysql_conn_mgr_supervisor) {
    LOG(WARNING) << "Stopping Ysql Connection Manager process";
    services.ysql_conn_mgr_supervisor->Stop();
  }

  if (services.call_home) {
    services.call_home->Shutdown();
  }

  LOG(WARNING) << "Stopping Tablet server";
  services.tablet_server->Shutdown();
  return Status::OK();
}

Status ShutdownServices(Services& services) {
  auto shutdown_thread =
      VERIFY_RESULT(Thread::Make("shutdown_thread", "shutdown_thread", [&services]() {
        WARN_NOT_OK(ShutdownServicesImpl(services), "Failed to shut down services");
      }));
  auto status = ThreadJoiner(shutdown_thread.get())
                    .give_up_after(kShutdownGraceTimeBeforeDumpingStackThreads)
                    .Join();
  if (!status.ok()) {
    std::stringstream ss;
    RenderAllThreadStacks(ss);
    LOG(INFO) << "Tablet server services failed to shut down in time, dumping all thread stacks:\n"
              << ss.str();
  }
  return ThreadJoiner(shutdown_thread.get()).Join();
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
      TabletServerOptions::kServerType, /*is_master=*/false, &argc, &argv));

  Services services;
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(StartServices(services));

  services.termination_monitor->WaitForTermination();

  LOG_AND_RETURN_FROM_MAIN_NOT_OK(ShutdownServices(services));

  // Best effort flush of log without any mutex.
  google::FlushLogFilesUnsafe(0);
  return EXIT_SUCCESS;
}

}  // namespace tserver
}  // namespace yb
