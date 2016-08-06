// Copyright (c) YugaByte, Inc.

#ifndef YB_REDISSERVER_REDIS_SERVER_OPTIONS_H
#define YB_REDISSERVER_REDIS_SERVER_OPTIONS_H

#include <vector>

#include "yb/server/server_base_options.h"
#include "yb/util/net/net_util.h"

namespace yb {
namespace redisserver {

// Options for constructing a redis server.
class RedisServerOptions : public yb::server::ServerBaseOptions {
 public:
  RedisServerOptions();

  ~RedisServerOptions() {}
};

} // namespace redisserver
} // namespace yb
#endif /* YB_REDISSERVER_REDIS_SERVER_OPTIONS_H */
