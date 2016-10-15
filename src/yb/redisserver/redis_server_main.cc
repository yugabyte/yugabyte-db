// Copyright (c) YugaByte, Inc.

#include <glog/logging.h>
#include <iostream>

#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_server.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

using yb::redisserver::RedisServer;

DECLARE_string(rpc_bind_addresses);
DECLARE_int32(rpc_num_service_threads);

namespace yb {
namespace redisserver {

static int RedisServerMain(int argc, char** argv) {
  InitYBOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0",
                                                 RedisServer::kDefaultPort);

  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);

  RedisServerOptions opts;
  RedisServer server(opts);
  LOG(INFO) << "Initializing redis server...";
  CHECK_OK(server.Init());

  LOG(INFO) << "Starting redis server...";
  CHECK_OK(server.Start());

  LOG(INFO) << "Redis server successfully started.";
  while (true) {
    SleepFor(MonoDelta::FromSeconds(60));
  }

  return 0;
}

} // namespace redisserver
} // namespace yb

int main(int argc, char** argv) {
  return yb::redisserver::RedisServerMain(argc, argv);
}
