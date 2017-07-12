// Copyright (c) YugaByte, Inc.

#include <iostream>

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_server.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

using yb::redisserver::RedisServer;

DEFINE_string(redis_proxy_bind_address, "", "Address to bind the redis proxy to.");
DEFINE_string(master_addresses, "", "Master addresses for the YB tier that the proxy talks to.");

namespace yb {
namespace redisserver {

static int RedisServerMain(int argc, char** argv) {
  InitYBOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_redis_proxy_bind_address = strings::Substitute("0.0.0.0:$0", RedisServer::kDefaultPort);
  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);

  RedisServerOptions opts;
  opts.rpc_opts.rpc_bind_addresses = FLAGS_redis_proxy_bind_address;
  opts.master_addresses_flag = FLAGS_master_addresses;
  RedisServer server(opts, nullptr /* tserver */);
  LOG(INFO) << "Starting redis server...";
  CHECK_OK(server.Start());

  LOG(INFO) << "Redis server successfully started.";
  while (true) {
    SleepFor(MonoDelta::FromSeconds(60));
  }

  return 0;
}

}  // namespace redisserver
}  // namespace yb

int main(int argc, char** argv) {
  return yb::redisserver::RedisServerMain(argc, argv);
}
