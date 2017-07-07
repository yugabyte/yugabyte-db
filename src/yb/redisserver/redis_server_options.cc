// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_server_options.h"

#include "yb/redisserver/redis_rpc.h"
#include "yb/redisserver/redis_server.h"

namespace yb {
namespace redisserver {

RedisServerOptions::RedisServerOptions() {
  rpc_opts.default_port = RedisServer::kDefaultPort;
  connection_context_factory = &std::make_unique<RedisConnectionContext>;
}

} // namespace redisserver
} // namespace yb
