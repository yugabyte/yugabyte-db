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

#include <glog/logging.h>
#include <iostream>

#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

using yb::tserver::TabletServer;
using yb::redisserver::RedisServer;
using yb::redisserver::RedisServerOptions;

DEFINE_bool(start_redis_proxy, false, "Starts a redis proxy along with the tablet server");
DEFINE_string(redis_proxy_bind_address, "", "address to bind the redis proxy to");
DECLARE_string(rpc_bind_addresses);
DECLARE_string(tserver_master_addrs);
DECLARE_int32(webserver_port);

namespace yb {
namespace tserver {

static int TabletServerMain(int argc, char** argv) {
  InitYBOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0",
                                                 TabletServer::kDefaultPort);
  FLAGS_redis_proxy_bind_address = strings::Substitute("0.0.0.0:$0", RedisServer::kDefaultPort);
  FLAGS_webserver_port = TabletServer::kDefaultWebPort;

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
    redis_server_options.master_addresses_flag =
        yb::HostPort::ToCommaSeparatedString(*tablet_server_options.GetMasterAddresses());
    redis_server.reset(new RedisServer(redis_server_options));
    LOG(INFO) << "Starting redis server...";
    CHECK_OK(redis_server->Start());
    LOG(INFO) << "Redis server successfully started.";
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
