//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_REDISSERVER_REDIS_RPC_H
#define YB_REDISSERVER_REDIS_RPC_H

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

  CHECKED_STATUS HandleInboundCall(const rpc::ConnectionPtr& connection, Slice redis_command);

  std::unique_ptr<RedisParser> parser_;
};

struct RedisClientCommand {
  // Command arguments. The memory is owned by RedisInboundCall.
  std::vector<Slice> cmd_args;
};

class RedisInboundCall : public rpc::QueueableInboundCall {
 public:
  explicit RedisInboundCall(rpc::ConnectionPtr conn, CallProcessedListener call_processed_listener);

  CHECKED_STATUS ParseFrom(Slice source);

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  void Serialize(std::deque<util::RefCntBuffer>* output) const override;

  // Serialize a response message for either success or failure. If it is a success,
  // 'response' should be the user-defined response type for the call. If it is a
  // failure, 'response' should be an ErrorStatusPB instance.
  CHECKED_STATUS SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                         bool is_success) override;
  void LogTrace() const override;
  std::string ToString() const override;
  void DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;

  MonoTime GetClientDeadline() const override;
  RedisClientCommand& GetClientCommand() { return client_command_; }
 private:
  // The connection on which this inbound call arrived.
  util::RefCntBuffer response_msg_buf_;
  RedisClientCommand client_command_;
};

} // namespace redisserver
} // namespace yb

#endif // YB_REDISSERVER_REDIS_RPC_H
