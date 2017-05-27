//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_YB_RPC_H
#define YB_RPC_YB_RPC_H

#include "yb/rpc/connection.h"

namespace yb {
namespace rpc {

class SaslClient;
class SaslServer;

class YBConnectionContext : public ConnectionContext {
 public:
  YBConnectionContext();
  ~YBConnectionContext();

  // Return SASL client instance for this connection.
  SaslClient& sasl_client() { return *sasl_client_; }

  // Return SASL server instance for this connection.
  SaslServer& sasl_server() { return *sasl_server_; }

  // Initialize SASL client before negotiation begins.
  CHECKED_STATUS InitSaslClient(Connection* connection);

  // Initialize SASL server before negotiation begins.
  CHECKED_STATUS InitSaslServer(Connection* connection);

 private:
  size_t BufferLimit() override;

  void RunNegotiation(ConnectionPtr connection, const MonoTime& deadline) override;

  CHECKED_STATUS ProcessCalls(const ConnectionPtr& connection,
                              Slice slice,
                              size_t* consumed) override;

  void DumpPB(const DumpRunningRpcsRequestPB& req,
              RpcConnectionPB* resp) override;

  bool Idle() override;

  bool ReadyToStop() override { return calls_being_handled_.empty(); }

  ConnectionType Type() override { return ConnectionType::YB; }

  size_t MaxReceive(Slice existing_data) override;

  void EraseCall(InboundCall* call);
  CHECKED_STATUS HandleCall(const ConnectionPtr& connection, Slice call_data);
  CHECKED_STATUS HandleInboundCall(const ConnectionPtr& connection, Slice call_data);

  // SASL client instance used for connection negotiation when Direction == CLIENT.
  std::unique_ptr<SaslClient> sasl_client_;

  // SASL server instance used for connection negotiation when Direction == SERVER.
  std::unique_ptr<SaslServer> sasl_server_;

  // Calls which have been received on the server and are currently
  // being handled.
  std::unordered_map<uint64_t, InboundCall*> calls_being_handled_;
};

class YBInboundCall : public InboundCall {
 public:
  explicit YBInboundCall(ConnectionPtr conn, CallProcessedListener call_processed_listener);

  // Parse an inbound call message.
  //
  // This only deserializes the call header, populating the 'header_' and
  // 'serialized_request_' member variables. The actual call parameter is
  // not deserialized, as this may be CPU-expensive, and this is called
  // from the reactor thread.
  CHECKED_STATUS ParseFrom(Slice source);

  int32_t call_id() const {
    return header_.call_id();
  }

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
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override;

  MonoTime GetClientDeadline() const override;

 private:
  // The header of the incoming call. Set by ParseFrom()
  RequestHeader header_;

  // The buffers for serialized response. Set by SerializeResponseBuffer().
  util::RefCntBuffer response_buf_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_YB_RPC_H
