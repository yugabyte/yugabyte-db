//
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
//

#ifndef YB_YQL_REDIS_REDISSERVER_REDIS_RPC_H
#define YB_YQL_REDIS_REDISSERVER_REDIS_RPC_H

#include <boost/container/small_vector.hpp>

#include "yb/yql/redis/redisserver/redis_fwd.h"
#include "yb/common/redis_protocol.pb.h"

#include "yb/rpc/connection_context.h"
#include "yb/rpc/rpc_with_queue.h"

namespace yb {
namespace redisserver {

class RedisParser;

class RedisConnectionContext : public rpc::ConnectionContextWithQueue {
 public:
  RedisConnectionContext();
  ~RedisConnectionContext();

 private:
  void Connected(const rpc::ConnectionPtr& connection) override {}

  rpc::RpcConnectionPB::StateType State() override {
    return rpc::RpcConnectionPB::OPEN;
  }

  CHECKED_STATUS ProcessCalls(const rpc::ConnectionPtr& connection,
                              Slice slice,
                              size_t* consumed) override;
  size_t BufferLimit() override;

  CHECKED_STATUS HandleInboundCall(const rpc::ConnectionPtr& connection,
                                   size_t commands_in_batch,
                                   Slice source);

  std::unique_ptr<RedisParser> parser_;
  size_t commands_in_batch_ = 0;
};

class RedisInboundCall : public rpc::QueueableInboundCall {
 public:
  explicit RedisInboundCall(rpc::ConnectionPtr conn, CallProcessedListener call_processed_listener);

  CHECKED_STATUS ParseFrom(size_t commands, Slice source);

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  void Serialize(std::deque<RefCntBuffer>* output) const override;

  void LogTrace() const override;
  std::string ToString() const override;
  bool DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;

  MonoTime GetClientDeadline() const override;

  RedisClientBatch& client_batch() { return client_batch_; }

  const std::string& service_name() const override;
  const std::string& method_name() const override;
  void RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) override;

  void RespondFailure(size_t idx, const Status& status);
  void RespondSuccess(size_t idx,
                      const rpc::RpcMethodMetrics& metrics,
                      RedisResponsePB* resp);
 private:
  void Respond(size_t idx, bool is_success, RedisResponsePB* resp);

  // The connection on which this inbound call arrived.
  static constexpr size_t batch_capacity = RedisClientBatch::static_capacity;
  boost::container::small_vector<RedisResponsePB, batch_capacity> responses_;
  boost::container::small_vector<std::atomic<size_t>, batch_capacity> ready_;
  std::atomic<size_t> ready_count_{0};
  std::atomic<bool> had_failures_{false};
  RedisClientBatch client_batch_;

  // Atomic bool to indicate if the command batch has been parsed.
  std::atomic<bool> parsed_ = {false};
};

} // namespace redisserver
} // namespace yb

#endif // YB_YQL_REDIS_REDISSERVER_REDIS_RPC_H
