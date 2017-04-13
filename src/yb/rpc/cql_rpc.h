//
// Copyright (c) YugaByte, Inc.
//
#ifndef YB_RPC_CQL_RPC_H
#define YB_RPC_CQL_RPC_H

#include "yb/rpc/inbound_call.h"
#include "yb/rpc/reactor.h"

namespace yb {
namespace rpc {

class CQLInboundTransfer : public AbstractInboundTransfer {
 public:
  CQLInboundTransfer();

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
  int32_t total_length_ = cqlserver::CQLMessage::kMessageHeaderLength;

  DISALLOW_COPY_AND_ASSIGN(CQLInboundTransfer);
};

class CQLConnection : public Connection {
 public:
  CQLConnection(ReactorThread* reactor_thread,
                Sockaddr remote,
                int socket,
                Direction direction);

  virtual void RunNegotiation(const MonoTime& deadline) override;

  sql::SqlSession::SharedPtr sql_session() const { return sql_session_; }

 protected:
  virtual void CreateInboundTransfer() override;

  TransferCallbacks* GetResponseTransferCallback(InboundCallPtr call) override;

  virtual void HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) override;

  virtual void HandleFinishedTransfer() override;

  AbstractInboundTransfer* inbound() const override;

 private:
  friend class CQLResponseTransferCallbacks;

  gscoped_ptr<CQLInboundTransfer> inbound_;

  void FinishedHandlingACall();

  // SQL session of this CQL client connection.
  // TODO(robert): To get around the need for this RPC layer to link with the SQL layer for the
  // reference to the SqlSession here, the whole SqlSession definition is contained in sql_session.h
  // and #include'd in connection.h/.cc. When SqlSession gets more complicated (say when we support
  // Cassandra ROLE), consider adding a CreateNewConnection method in rpc::ServiceIf so that
  // CQLConnection can be created and returned from CQLServiceImpl.CreateNewConnection().
  sql::SqlSession::SharedPtr sql_session_;
};

class CQLInboundCall : public InboundCall {
 public:
  explicit CQLInboundCall(CQLConnection* conn);

  virtual CHECKED_STATUS ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) override;

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  virtual void SerializeResponseTo(std::vector<Slice>* slices) const override;

  // Serialize a response message for either success or failure. If it is a success,
  // 'response' should be the user-defined response type for the call. If it is a
  // failure, 'response' should be an ErrorStatusPB instance.
  CHECKED_STATUS SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                         bool is_success) override;

  virtual void RecordHandlingStarted(scoped_refptr<Histogram> incoming_queue_time) override;
  virtual void QueueResponseToConnection() override;
  virtual void LogTrace() const override;
  virtual std::string ToString() const override;
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override;

  virtual MonoTime GetClientDeadline() const override;

  // Return the response message buffer.
  faststring& response_msg_buf() {
    return response_msg_buf_;
  }

  // Return the SQL session of this CQL call.
  sql::SqlSession::SharedPtr GetSqlSession() const;

  void SetResumeFrom(Callback<void(void)>* resume_from) {
    resume_from_ = resume_from;
  }

  bool TryResume();

 protected:
  scoped_refptr<Connection> get_connection() override;
  const scoped_refptr<Connection> get_connection() const override;

 private:
  // The connection on which this inbound call arrived.
  scoped_refptr<CQLConnection> conn_;
  faststring response_msg_buf_;

  Callback<void(void)>* resume_from_ = nullptr;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_CQL_RPC_H
