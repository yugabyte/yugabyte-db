// Copyright (c) YugaByte, Inc.

#ifndef YB_REDIS_SERVICE_H
#define YB_REDIS_SERVICE_H

#include "yb/redisserver/redis_service.service.h"

namespace yb {
namespace redisserver {

class RedisServer;

class RedisServiceImpl : public RedisServerServiceIf {
 public:
  explicit RedisServiceImpl(RedisServer* server);

  void Handle(::yb::rpc::InboundCall* call) OVERRIDE;

 private:
  static const int kMethodCount = 1;
  ::yb::rpc::RpcMethodMetrics metrics_[kMethodCount];
};

} // namespace redisserver
} // namespace yb

#endif
