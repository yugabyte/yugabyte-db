//
// Copyright (c) YugaByte, Inc.
//
#ifndef YB_RPC_YB_RPC_H
#define YB_RPC_YB_RPC_H

#include "yb/rpc/inbound_call.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/sasl_client.h"
#include "yb/rpc/sasl_server.h"

namespace yb {
namespace rpc {

class YBInboundTransfer : public AbstractInboundTransfer {
 public:
  YBInboundTransfer();

  // Read from the socket into our buffer.
  CHECKED_STATUS ReceiveBuffer(Socket& socket) override;  // NOLINT.

  // Return true if any bytes have yet been received.
  bool TransferStarted() const override {
    return cur_offset_ != 0;
  }

  // Return true if the entire transfer has been received.
  bool TransferFinished() const override {
    return cur_offset_ == total_length_;
  }

  // Return a string indicating the status of this transfer (number of bytes received, etc)
  // suitable for logging.
  std::string StatusAsString() const override;

 private:
  CHECKED_STATUS ProcessInboundHeader();

  int32_t total_length_ = kMsgLengthPrefixLength;

  DISALLOW_COPY_AND_ASSIGN(YBInboundTransfer);
};

class YBConnection : public Connection {
 public:
  YBConnection(ReactorThread* reactor_thread,
               Sockaddr remote,
               int socket,
               Direction direction);

  // Return SASL client instance for this connection.
  SaslClient& sasl_client() { return sasl_client_; }

  // Return SASL server instance for this connection.
  SaslServer& sasl_server() { return sasl_server_; }

  // Initialize SASL client before negotiation begins.
  CHECKED_STATUS InitSaslClient();

  // Initialize SASL server before negotiation begins.
  CHECKED_STATUS InitSaslServer();

  virtual void RunNegotiation(const MonoTime& deadline) override;

 protected:
  virtual void CreateInboundTransfer() override;

  virtual void HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) override;

  virtual void HandleFinishedTransfer() override;

  AbstractInboundTransfer* inbound() const override;

 private:
  gscoped_ptr<YBInboundTransfer> inbound_;
  // SASL client instance used for connection negotiation when Direction == CLIENT.
  SaslClient sasl_client_;

  // SASL server instance used for connection negotiation when Direction == SERVER.
  SaslServer sasl_server_;
};

class YBInboundCall : public InboundCall {
 public:
  explicit YBInboundCall(YBConnection* conn);

  // Parse an inbound call message.
  //
  // This only deserializes the call header, populating the 'header_' and
  // 'serialized_request_' member variables. The actual call parameter is
  // not deserialized, as this may be CPU-expensive, and this is called
  // from the reactor thread.
  virtual CHECKED_STATUS ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) override;

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

  virtual void QueueResponseToConnection() override;
  virtual void LogTrace() const override;
  virtual std::string ToString() const override;
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override;

  // Return true if the deadline set by the client has already elapsed.
  // In this case, the server may stop processing the call, since the
  // call response will be ignored anyway.
  virtual MonoTime GetClientDeadline() const override;
 protected:
  ConnectionPtr get_connection() const override;

 private:
  void NotifyTransferFinished() override;
  void NotifyTransferAborted(const Status& status) override;

  // The header of the incoming call. Set by ParseFrom()
  RequestHeader header_;

  // The buffers for serialized response. Set by SerializeResponseBuffer().
  util::RefCntBuffer response_buf_;

  // The connection on which this inbound call arrived.
  boost::intrusive_ptr<YBConnection> conn_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_YB_RPC_H
