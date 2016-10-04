// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_server.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_service.h"

using yb::rpc::ServiceIf;

namespace yb {
namespace redisserver {

RedisServer::RedisServer(const RedisServerOptions& opts)
  : RpcServerBase("RedisServer", opts, "yb.redisserver"),
    opts_(opts) {
}

Status RedisServer::Init() {
  RETURN_NOT_OK(server::RpcServerBase::Init());

  return Status::OK();
}

Status RedisServer::Start() {
  CHECK(initialized_);

  gscoped_ptr<ServiceIf> redis_service(new RedisServiceImpl(this, opts_.master_addresses_flag));
  RETURN_NOT_OK(RegisterService(redis_service.Pass()));

  RETURN_NOT_OK(server::RpcServerBase::StartRpcServer());

  return Status::OK();
}

}  // namespace redisserver
}  // namespace yb
