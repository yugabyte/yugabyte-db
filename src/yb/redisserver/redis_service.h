// Copyright (c) YugaByte, Inc.

#ifndef YB_REDISSERVER_REDIS_SERVICE_H_
#define YB_REDISSERVER_REDIS_SERVICE_H_

#include "yb/redisserver/redis_fwd.h"
#include "yb/redisserver/redis_service.service.h"

#include <memory>

namespace yb {
namespace redisserver {

class RedisServer;

class RedisServiceImpl : public RedisServerServiceIf {
 public:
  RedisServiceImpl(RedisServer* server, std::string yb_tier_master_address);
  ~RedisServiceImpl();

  void Handle(yb::rpc::InboundCallPtr call) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace redisserver
}  // namespace yb

#endif  // YB_REDISSERVER_REDIS_SERVICE_H_
