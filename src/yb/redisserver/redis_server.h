// Copyright (c) YugaByte, Inc.

#ifndef YB_REDIS_SERVER_H
#define YB_REDIS_SERVER_H

#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/redisserver/redis_server_options.h"
#include "yb/server/server_base.h"
#include "yb/util/status.h"

namespace yb {

namespace redisserver {

class RedisServer : public server::RpcServerBase {
 public:
  static const uint16_t kDefaultPort = 6379;

  explicit RedisServer(const RedisServerOptions& opts);

  Status Init();

  Status Start();

 private:
  RedisServerOptions opts_;

  DISALLOW_COPY_AND_ASSIGN(RedisServer);
};

} // namespace redisserver
} // namespace yb
#endif
