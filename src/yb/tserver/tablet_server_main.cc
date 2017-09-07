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

#include <iostream>

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_server.h"
#include "yb/cqlserver/cql_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

using yb::tserver::TabletServer;
using yb::redisserver::RedisServer;
using yb::redisserver::RedisServerOptions;
using yb::cqlserver::CQLServer;
using yb::cqlserver::CQLServerOptions;

DEFINE_bool(start_redis_proxy, true, "Starts a redis proxy along with the tablet server");
DEFINE_string(redis_proxy_bind_address, "", "Address to bind the redis proxy to");
DEFINE_int32(redis_proxy_webserver_port, 0, "Webserver port for redis proxy");

DEFINE_bool(start_cql_proxy, true, "Starts a CQL proxy along with the tablet server");
DEFINE_string(cql_proxy_broadcast_rpc_address, "",
              "RPC address to broadcast to other nodes. This is the broadcast_address used in the"
                  " system.local table");
DEFINE_string(cql_proxy_bind_address, "", "Address to bind the CQL proxy to");
DEFINE_int32(cql_proxy_webserver_port, 0, "Webserver port for CQL proxy");

DECLARE_string(rpc_bind_addresses);
DECLARE_int32(webserver_port);

namespace yb {
namespace tserver {

static int TabletServerMain(int argc, char** argv) {
  InitYBOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0",
                                                 TabletServer::kDefaultPort);
  FLAGS_webserver_port = TabletServer::kDefaultWebPort;
  FLAGS_redis_proxy_bind_address = strings::Substitute("0.0.0.0:$0", RedisServer::kDefaultPort);
  FLAGS_redis_proxy_webserver_port = RedisServer::kDefaultWebPort;
  FLAGS_cql_proxy_bind_address = strings::Substitute("0.0.0.0:$0", CQLServer::kDefaultPort);
  FLAGS_cql_proxy_webserver_port = CQLServer::kDefaultWebPort;

  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);

  TabletServerOptions tablet_server_options;
  TabletServer server(tablet_server_options);
  LOG(INFO) << "Initializing tablet server...";
  CHECK_OK(server.Init());
  LOG(INFO) << "Starting tablet server...";
  CHECK_OK(server.Start());
  LOG(INFO) << "Tablet server successfully started.";

  std::unique_ptr<RedisServer> redis_server;
  if (FLAGS_start_redis_proxy) {
    RedisServerOptions redis_server_options;
    redis_server_options.rpc_opts.rpc_bind_addresses = FLAGS_redis_proxy_bind_address;
    redis_server_options.webserver_opts.port = FLAGS_redis_proxy_webserver_port;
    redis_server_options.master_addresses_flag =
        yb::HostPort::ToCommaSeparatedString(*tablet_server_options.GetMasterAddresses());
    redis_server_options.dump_info_path =
        (tablet_server_options.dump_info_path.empty()
             ? ""
             : tablet_server_options.dump_info_path + "-redis");
    redis_server.reset(new RedisServer(redis_server_options, &server));
    LOG(INFO) << "Starting redis server...";
    CHECK_OK(redis_server->Start());
    LOG(INFO) << "Redis server successfully started.";
  }

  std::unique_ptr<CQLServer> cql_server;
  if (FLAGS_start_cql_proxy) {
    CQLServerOptions cql_server_options;
    cql_server_options.rpc_opts.rpc_bind_addresses = FLAGS_cql_proxy_bind_address;
    cql_server_options.broadcast_rpc_address = FLAGS_cql_proxy_broadcast_rpc_address;
    cql_server_options.webserver_opts.port = FLAGS_cql_proxy_webserver_port;
    cql_server_options.master_addresses_flag =
        yb::HostPort::ToCommaSeparatedString(*tablet_server_options.GetMasterAddresses());
    cql_server_options.dump_info_path =
        (tablet_server_options.dump_info_path.empty()
             ? ""
             : tablet_server_options.dump_info_path + "-cql");
    boost::asio::io_service io;
    cql_server.reset(new CQLServer(cql_server_options, &io, &server));
    LOG(INFO) << "Starting CQL server...";
    CHECK_OK(cql_server->Start());
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
