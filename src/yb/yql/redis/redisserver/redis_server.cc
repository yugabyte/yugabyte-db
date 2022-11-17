// Copyright (c) YugaByte, Inc.
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

#include "yb/yql/redis/redisserver/redis_server.h"

#include "yb/tserver/tablet_server_interface.h"

#include "yb/util/flags.h"
#include "yb/util/size_literals.h"

#include "yb/yql/redis/redisserver/redis_rpc.h"
#include "yb/yql/redis/redisserver/redis_service.h"

using yb::rpc::ServiceIf;
using namespace yb::size_literals;

DEFINE_UNKNOWN_int32(redis_svc_queue_length, 5000,
             "RPC queue length for redis service");
TAG_FLAG(redis_svc_queue_length, advanced);

DEFINE_UNKNOWN_int64(redis_rpc_block_size, 1_MB, "Redis RPC block size");
DEFINE_UNKNOWN_int64(redis_rpc_memory_limit, 0, "Redis RPC memory limit");

namespace yb {
namespace redisserver {

class RedisConnnectionContextFactory : public rpc::ConnectionContextFactory {
 public:
  explicit RedisConnnectionContextFactory(
      const std::shared_ptr<MemTracker>& parent_mem_tracker)
      : rpc::ConnectionContextFactory(
          FLAGS_redis_rpc_memory_limit, RedisConnectionContext::Name(), parent_mem_tracker),
        allocator_(FLAGS_redis_rpc_block_size, buffer_tracker_) {
  }

  virtual ~RedisConnnectionContextFactory() = default;

  std::unique_ptr<rpc::ConnectionContext> Create(size_t) override {
    return std::make_unique<RedisConnectionContext>(&allocator_, call_tracker_);
  }

 private:
  rpc::GrowableBufferAllocator allocator_;
};

RedisServer::RedisServer(const RedisServerOptions& opts, tserver::TabletServerIf* tserver)
    : RpcAndWebServerBase(
          "RedisServer", opts, "yb.redisserver",
          MemTracker::CreateTracker(
              "Redis", tserver ? tserver->mem_tracker() : MemTracker::GetRootTracker(),
              AddToParent::kTrue, CreateMetrics::kFalse)),
      opts_(opts),
      tserver_(tserver) {
  SetConnectionContextFactory(std::make_shared<RedisConnnectionContextFactory>(
      mem_tracker()->parent()));
}

Status RedisServer::Start() {
  RETURN_NOT_OK(server::RpcAndWebServerBase::Init());

  std::unique_ptr<ServiceIf> redis_service(new RedisServiceImpl(this, opts_.master_addresses_flag));
  RETURN_NOT_OK(RegisterService(FLAGS_redis_svc_queue_length, std::move(redis_service)));

  RETURN_NOT_OK(server::RpcAndWebServerBase::Start());

  return Status::OK();
}

}  // namespace redisserver
}  // namespace yb
