//
// Copyright (c) YugaByte, Inc.
//
#ifndef YB_RPC_REDIS_RPC_H
#define YB_RPC_REDIS_RPC_H

#include "yb/rpc/inbound_call.h"
#include "yb/rpc/reactor.h"

#include "yb/util/slice.h"

namespace yb {
namespace rpc {

struct RedisClientCommand {
  std::vector<Slice> cmd_args;  // Arguments of current command.
  int num_multi_bulk_args_left = 0;  // Number of multi bulk arguments left to read.
  int64_t current_multi_bulk_arg_len = -1;  // Length of bulk argument in multi bulk request.
};

class RedisInboundTransfer : public AbstractInboundTransfer {
 public:
  RedisInboundTransfer();

  ~RedisInboundTransfer();

  // Read from the socket into our buffer.
  CHECKED_STATUS ReceiveBuffer(Socket& socket) override;

  bool TransferStarted() const override {
    return cur_offset_ != 0;
  }

  bool TransferFinished() const override {
    return done_;
  }

  // Return a string indicating the status of this transfer (number of bytes received, etc.)
  // suitable for logging.
  std::string StatusAsString() const override;

  RedisClientCommand& client_command() { return client_command_; }

  RedisInboundTransfer* ExcessData() const;

 private:
  static constexpr int kProtoIOBufLen = 1024 * 16;  // I/O buffer size for reading client commands.

  // Sets done_ to true if a whole command has been read and parsed from the network.
  //
  // returns Status::OK if there are no error(s) in parsing, regardless of the whole command
  // having been read. returns an appropriate non-OK status upon error.
  CHECKED_STATUS CheckReadCompletely();

  // To be called only for client commands in the multi format.
  // e.g:
  //     "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
  // sets done_ to true if the whole command has been read, and is ready for processing.
  //      done_ remains false if the command has not been read completely.
  // returns Status::OK if there are no error(s) in parsing, regardless of the whole command
  // having been read. returns an appropriate non-OK status upon error.
  CHECKED_STATUS CheckMultiBulkBuffer();

  // To be called only for client commands in the inline format.
  // e.g:
  //     "set foo TEST\r\n"
  // sets done_ to true if the whole command has been read, and is ready for processing.
  //      done_ remains false if the command has not been read completely.
  // returns Status::OK if there are no error(s) in parsing, regardless of the whole command
  // having been read. returns an appropriate non-OK status upon error.
  //
  // Deviation from Redis's implementation in networking.c
  // 1) This does not translate escaped characters, and
  // 2) We don't handle the case where the delimiter only has '\n' instead of '\r\n'
  //
  // We haven't seen a place where this is used by the redis clients. All known places seem to use
  // CheckMultiBulkBuffer(). If we find places where this is used, we can come back and make this
  // compliant. If we are sure this doesn't get used, perhaps, we can get rid of this altogether.
  CHECKED_STATUS CheckInlineBuffer();

  // Returns true if the buffer has the \r\n required at the end of the token.
  bool FindEndOfLine();

  // Parses the number pointed to by parsing_pos_ up to the \r\n. Returns OK if successful, or
  // error.
  CHECKED_STATUS ParseNumber(int64_t* out_number);

  int64_t cur_offset_ = 0;  // index into buf_ where the next byte read from the client is stored.
  RedisClientCommand client_command_;
  bool done_ = false;
  int64_t parsing_pos_ = 0;  // index into buf_, from which the input needs to be parsed.
  int64_t searching_pos_ = 0;  // index into buf_, from which the input needs to be searched.
  // Typically ends up being equal to parsing_pos_ except when we
  // receive partial data. We don't want to search the searched data
  // again. Otherwise, the worst case analysis takes us to O(N^2) where
  // N is the message length.

  DISALLOW_COPY_AND_ASSIGN(RedisInboundTransfer);
};

class RedisConnection : public Connection {
 public:
  RedisConnection(ReactorThread* reactor_thread,
                  Sockaddr remote,
                  int socket,
                  Direction direction);

  virtual void RunNegotiation(const MonoTime& deadline) override;

 protected:
  virtual void CreateInboundTransfer() override;

  TransferCallbacks* GetResponseTransferCallback(gscoped_ptr<InboundCall> call) override;

  virtual void HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) override;

  virtual void HandleFinishedTransfer() override;

  AbstractInboundTransfer* inbound() const override;

 private:
  friend class RedisResponseTransferCallbacks;

  gscoped_ptr<RedisInboundTransfer> inbound_;

  // Used by the WriteHandler to signal that the current call has been
  // responded to, so that the next call can be queued up for handling.
  // Since the redis client calls do not have an associated call id. We
  // need to be careful to ensure that the responses are sent in the
  // same order in which the requests were made. Having more than one
  // client request -- from the same connection -- cannot be allowed to
  // process in parallel.
  // In the current implementation, the Reader thread which receives data
  // from the client, and the Writer thread that responds to the client are
  // one and the same. So we might even be fine having this be a normal "bool"
  // instead of atomic. But, we are just being safe here. There is no reason
  // why the ReadHandler and WriteHandler should be on the same thread always.
  std::atomic<bool> processing_call_;

  void FinishedHandlingACall();
};

class RedisInboundCall : public InboundCall {
 public:
  explicit RedisInboundCall(RedisConnection* conn);

  virtual CHECKED_STATUS ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) override;

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  virtual void SerializeResponseTo(std::vector<Slice>* slices) const override;

  // Serialize a response message for either success or failure. If it is a success,
  // 'response' should be the user-defined response type for the call. If it is a
  // failure, 'response' should be an ErrorStatusPB instance.
  CHECKED_STATUS SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                         bool is_success) override;
  CHECKED_STATUS SerializeResponseBuffer(const RedisResponsePB& redis_response, bool is_success);

  virtual void QueueResponseToConnection() override;
  virtual void LogTrace() const override;
  virtual std::string ToString() const override;
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override;

  virtual MonoTime GetClientDeadline() const override;
  RedisClientCommand& GetClientCommand();

 protected:
  scoped_refptr<Connection> get_connection() override;
  const scoped_refptr<Connection> get_connection() const override;

 private:
  // The connection on which this inbound call arrived.
  scoped_refptr<RedisConnection> conn_;
  faststring response_msg_buf_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_REDIS_RPC_H
