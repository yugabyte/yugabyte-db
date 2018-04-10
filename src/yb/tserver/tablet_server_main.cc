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

#include <chrono>
#include <iostream>

#include <boost/optional/optional.hpp>
#include <glog/logging.h>

#ifdef TCMALLOC_ENABLED
#include <gperftools/malloc_extension.h>
#endif

#include "yb/gutil/strings/substitute.h"
#include "yb/yql/redis/redisserver/redis_server.h"
#include "yb/yql/cql/cqlserver/cql_server.h"
#include "yb/yql/pgsql/server/pg_server.h"
#include "yb/master/call_home.h"
#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/scheduler.h"
#include "yb/tserver/tablet_server.h"
#include "yb/consensus/log_util.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/main_util.h"
#include "yb/util/size_literals.h"

using yb::redisserver::RedisServer;
using yb::redisserver::RedisServerOptions;

using yb::cqlserver::CQLServer;
using yb::cqlserver::CQLServerOptions;

using namespace yb::size_literals;  // NOLINT
using yb::pgserver::PgServer;
using yb::pgserver::PgServerOptions;

DEFINE_bool(start_redis_proxy, true, "Starts a redis proxy along with the tablet server");

DEFINE_bool(start_cql_proxy, true, "Starts a CQL proxy along with the tablet server");
DEFINE_string(cql_proxy_broadcast_rpc_address, "",
              "RPC address to broadcast to other nodes. This is the broadcast_address used in the"
                  " system.local table");

DEFINE_int64(tserver_tcmalloc_max_total_thread_cache_bytes, 256_MB, "Total number of bytes to "
    "use for the thread cache for tcmalloc across all threads in the tserver.");

DEFINE_bool(start_pgsql_proxy, true, "Starts a PostgreSQL proxy along with the tablet server");

DECLARE_string(rpc_bind_addresses);
DECLARE_bool(callhome_enabled);
DECLARE_int32(webserver_port);
DECLARE_int32(logbuflevel);

DECLARE_string(redis_proxy_bind_address);
DECLARE_int32(redis_proxy_webserver_port);

DECLARE_string(cql_proxy_bind_address);
DECLARE_int32(cql_proxy_webserver_port);

DECLARE_string(pgsql_proxy_bind_address);
DECLARE_int32(pgsql_proxy_webserver_port);

namespace yb {
namespace tserver {

static int TabletServerMain(int argc, char** argv) {
  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0",
                                                 TabletServer::kDefaultPort);
  FLAGS_webserver_port = TabletServer::kDefaultWebPort;

  FLAGS_redis_proxy_bind_address = strings::Substitute("0.0.0.0:$0", RedisServer::kDefaultPort);
  FLAGS_redis_proxy_webserver_port = RedisServer::kDefaultWebPort;

  FLAGS_cql_proxy_bind_address = strings::Substitute("0.0.0.0:$0", CQLServer::kDefaultPort);
  FLAGS_cql_proxy_webserver_port = CQLServer::kDefaultWebPort;
  // Do not sync GLOG to disk for INFO, WARNING.
  // ERRORs, and FATALs will still cause a sync to disk.
  FLAGS_logbuflevel = google::GLOG_WARNING;

  FLAGS_pgsql_proxy_bind_address = strings::Substitute("0.0.0.0:$0", PgServer::kDefaultPort);
  FLAGS_pgsql_proxy_webserver_port = PgServer::kDefaultWebPort;

  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(log::ModifyDurableWriteFlagIfNotODirect());
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(InitYB(TabletServerOptions::kServerType, argv[0]));

#ifdef TCMALLOC_ENABLED
  LOG(INFO) << "Setting tcmalloc max thread cache bytes to: " <<
    FLAGS_tserver_tcmalloc_max_total_thread_cache_bytes;
  if (!MallocExtension::instance()->SetNumericProperty(kTcMallocMaxThreadCacheBytes,
      FLAGS_tserver_tcmalloc_max_total_thread_cache_bytes)) {
    LOG(FATAL) << "Failed to set Tcmalloc property: " << kTcMallocMaxThreadCacheBytes;
  }
#endif

  auto tablet_server_options = TabletServerOptions::CreateTabletServerOptions();
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(tablet_server_options);
  YB_EDITION_NS_PREFIX TabletServer server(*tablet_server_options);
  LOG(INFO) << "Initializing tablet server...";
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server.Init());
  LOG(INFO) << "Starting tablet server...";
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server.Start());
  LOG(INFO) << "Tablet server successfully started.";

  std::unique_ptr<CallHome> call_home;
  if (FLAGS_callhome_enabled) {
    call_home = std::make_unique<CallHome>(&server, ServerType::TSERVER);
    call_home->ScheduleCallHome();
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
    redis_server.reset(new RedisServer(redis_server_options, &server));
    LOG(INFO) << "Starting redis server...";
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(redis_server->Start());
    LOG(INFO) << "Redis server successfully started.";
  }

  std::unique_ptr<PgServer> pgsql_server;
  if (FLAGS_start_pgsql_proxy) {
    PgServerOptions pgsql_server_options;
    pgsql_server_options.rpc_opts.rpc_bind_addresses = FLAGS_pgsql_proxy_bind_address;
    pgsql_server_options.webserver_opts.port = FLAGS_pgsql_proxy_webserver_port;
    pgsql_server_options.master_addresses_flag = tablet_server_options->master_addresses_flag;
    pgsql_server_options.SetMasterAddresses(tablet_server_options->GetMasterAddresses());
    pgsql_server_options.dump_info_path =
        (tablet_server_options->dump_info_path.empty()
             ? ""
             : tablet_server_options->dump_info_path + "-pgsql");
    pgsql_server.reset(new PgServer(pgsql_server_options, &server));
    LOG(INFO) << "Starting Postgresql server...";
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(pgsql_server->Start());
    LOG(INFO) << "Postgresql server successfully started.";
  }

  // TODO(neil): After CQL server is starting, it blocks this thread from moving on.
  // This should be fixed such that all processes or service by tablet server are treated equally
  // by using different threads for each process.
  std::unique_ptr<CQLServer> cql_server;
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
    boost::asio::io_service io;
    cql_server.reset(new CQLServer(cql_server_options, &io, &server));
    LOG(INFO) << "Starting CQL server...";
    LOG_AND_RETURN_FROM_MAIN_NOT_OK(cql_server->Start());
    LOG(INFO) << "CQL server successfully started.";

    // Should run forever unless there are some errors.
    boost::system::error_code ec;
    io.run(ec);
    if (ec) {
      LOG(WARNING) << "IO service run failure: " << ec;
    }

    LOG (WARNING) << "CQL Server shutting down";
    cql_server->Shutdown();
  }

  while (true) {
    SleepFor(MonoDelta::FromSeconds(60));
  }

  return 0;
}

}  // namespace tserver
}  // namespace yb

int main(int argc, char** argv) {
  return yb::tserver::TabletServerMain(argc, argv);
}
