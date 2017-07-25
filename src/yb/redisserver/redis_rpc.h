//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_REDISSERVER_REDIS_RPC_H
#define YB_REDISSERVER_REDIS_RPC_H

#include "yb/redisserver/redis_fwd.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/rpc_with_queue.h"

namespace yb {
namespace redisserver {

class RedisParser;

class RedisConnectionContext : public rpc::ConnectionContextWithQueue {
 public:
  RedisConnectionContext();
  ~RedisConnectionContext();

 private:
  void RunNegotiation(rpc::ConnectionPtr connection, const MonoTime& deadline) override;
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
  void Serialize(std::deque<util::RefCntBuffer>* output) const override;

  void LogTrace() const override;
  std::string ToString() const override;
  void DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;

  MonoTime GetClientDeadline() const override;

  RedisClientBatch& client_batch() { return client_batch_; }

  const std::string& service_name() const override;
  const std::string& method_name() const override;
  void RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) override;

  void RespondFailure(size_t idx, const Status& status);
  void RespondSuccess(size_t idx,
                      const RedisResponsePB& resp,
                      const rpc::RpcMethodMetrics& metrics,
                      bool use_encoded_array = false);
 private:
  void Respond(size_t idx, const util::RefCntBuffer& buffer, bool is_success);

  // The connection on which this inbound call arrived.
  static constexpr size_t batch_capacity = RedisClientBatch::static_capacity;
  boost::container::small_vector<util::RefCntBuffer, batch_capacity> responses_;
  boost::container::small_vector<std::atomic<size_t>, batch_capacity> ready_;
  std::atomic<size_t> ready_count_{0};
  std::atomic<bool> had_failures_{false};
  RedisClientBatch client_batch_;
};

} // namespace redisserver
} // namespace yb

#endif // YB_REDISSERVER_REDIS_RPC_H
