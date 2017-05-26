// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef YB_RPC_CONNECTION_H_
#define YB_RPC_CONNECTION_H_

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <ev++.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/connection_types.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/inbound_call.h"
#include "yb/rpc/server_event.h"
#include "yb/sql/sql_session.h"

#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/object_pool.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

namespace yb {
namespace rpc {

class Connection;
class DumpRunningRpcsRequestPB;
class GrowableBuffer;
class Reactor;
class ReactorTask;
class ReactorThread;
class RpcConnectionPB;

// ConnectionContext class is used by connection for doing protocol
// specific logic.
class ConnectionContext {
 public:
  virtual ~ConnectionContext() {}

  // Perform negotiation for a connection
  virtual void RunNegotiation(ConnectionPtr connection, const MonoTime& deadline) = 0;

  // Split slice into separate calls and invoke them.
  // Return number of processed bytes in `consumed`.
  virtual CHECKED_STATUS ProcessCalls(const ConnectionPtr& connection,
                                      Slice slice,
                                      size_t* consumed) = 0;

  // Dump information about status of this connection context to protobuf.
  virtual void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) = 0;

  // Checks whether this connection context is idle.
  virtual bool Idle() = 0;

  // Returns true if connection does not have any pending work and could be safey released.
  virtual bool ReadyToStop() = 0;

  // Reading buffer limit for this connection context.
  // The reading buffer will never be larger than this limit.
  virtual size_t BufferLimit() = 0;

  // Type of this connection context, i.e. YB, Redis, CQL etc.
  virtual ConnectionType Type() = 0;
};

YB_DEFINE_ENUM(ConnectionDirection, (CLIENT)(SERVER));

//
// A connection between an endpoint and us.
//
// Inbound connections are created by AcceptorPools, which eventually schedule
// RegisterConnection() to be called from the reactor thread.
//
// Outbound connections are created by the Reactor thread in order to service
// outbound calls.
//
// Once a Connection is created, it can be used both for sending messages and
// receiving them, but any given connection is explicitly a client or server.
// If a pair of servers are making bidirectional RPCs, they will use two separate
// TCP connections (and Connection objects).
//
// This class is not fully thread-safe.  It is accessed only from the context of a
// single ReactorThread except where otherwise specified.
//
class Connection final : public std::enable_shared_from_this<Connection> {
 public:
  typedef ConnectionDirection Direction;

  // Create a new Connection.
  // reactor_thread: the reactor that owns us.
  // remote: the address of the remote end
  // socket: the socket to take ownership of.
  // direction: whether we are the client or server side
  // context: context for this connection. Context is used by connection to handle
  // protocol specific actions, such as parsing of incoming data into calls.
  Connection(ReactorThread* reactor_thread,
             const Endpoint& remote,
             int socket,
             Direction direction,
             std::unique_ptr<ConnectionContext> context);

  ~Connection();

  // Set underlying socket to non-blocking (or blocking) mode.
  CHECKED_STATUS SetNonBlocking(bool enabled);

  // Register our socket with an epoll loop.  We will only ever be registered in
  // one epoll loop at a time.
  void EpollRegister(ev::loop_ref& loop);  // NOLINT

  MonoTime last_activity_time() const {
    return last_activity_time_;
  }

  // Set the user credentials which should be used to log in.
  void set_user_credentials(const UserCredentials& user_credentials);

  // Modify the user credentials which will be used to log in.
  UserCredentials* mutable_user_credentials() { return &user_credentials_; }

  // Get the user credentials which will be used to log in.
  const UserCredentials& user_credentials() const { return user_credentials_; }

  // Returns true if we are not in the process of receiving or sending a
  // message, and we have no outstanding calls.
  bool Idle() const;

  // Fail any calls which are currently queued or awaiting response.
  // Prohibits any future calls (they will be failed immediately with this
  // same Status).
  void Shutdown(const Status& status);

  // Queue a new call to be made. If the queueing fails, the call will be
  // marked failed.
  // Takes ownership of the 'call' object regardless of whether it succeeds or fails.
  // This may be called from a non-reactor thread.
  void QueueOutboundCall(const OutboundCallPtr& call);

  // The address of the remote end of the connection.
  const Endpoint& remote() const { return remote_; }

  // The address of the local end of the connection.
  const Endpoint& local() const { return local_; }

  // libev callback when there are some events in socket.
  void Handler(ev::io& watcher, int revents); // NOLINT

  void HandleTimeout(ev::timer& watcher, int revents); // NOLINT

  // Invoked when we have something to read.
  CHECKED_STATUS ReadHandler();

  // Invoked when socket is ready for writing.
  CHECKED_STATUS WriteHandler();

  // Safe to be called from other threads.
  std::string ToString() const;

  Direction direction() const { return direction_; }

  Socket* socket() { return &socket_; }

  // Go through the process of transferring control of the underlying socket back to the Reactor.
  void CompleteNegotiation(const Status& negotiation_status);

  // Indicate that negotiation is complete and that the Reactor is now in control of the socket.
  void MarkNegotiationComplete();

  // Queue a call response back to the client on the server side.
  //
  // This may be called from a non-reactor thread.
  void QueueOutboundData(OutboundDataPtr outbound_data);

  ReactorThread* reactor_thread() const { return reactor_thread_; }

  CHECKED_STATUS DumpPB(const DumpRunningRpcsRequestPB& req,
                RpcConnectionPB* resp);

  // Do appropriate actions after adding outbound call.
  void OutboundQueued();

  void RunNegotiation(const MonoTime& deadline);

  // An incoming packet has completed on the client side. This parses the
  // call response, looks up the CallAwaitingResponse, and calls the
  // client callback.
  CHECKED_STATUS HandleCallResponse(Slice slice);

  ConnectionContext& context() { return *context_; }

 private:
  CHECKED_STATUS DoWrite();

  // Does actual outbound data queueing. Invoked in appropriate reactor thread.
  void DoQueueOutboundData(OutboundDataPtr call, bool batch);

  void ProcessResponseQueue();

  void ClearSending(const Status& status);

  void CallSent(OutboundCallPtr call);

  CHECKED_STATUS Receive(bool* received);

  // Try to parse received data into calls and process them.
  CHECKED_STATUS TryProcessCalls(bool* had_calls);

  // The reactor thread that created this connection.
  ReactorThread* const reactor_thread_;

  // The socket we're communicating on.
  Socket socket_;

  // The remote address we're talking from.
  Endpoint local_;

  // The remote address we're talking to.
  const Endpoint remote_;

  // whether we are client or server
  Direction direction_;

  // The last time we read or wrote from the socket.
  MonoTime last_activity_time_;

  // The credentials of the user operating on this connection (if a client user).
  UserCredentials user_credentials_;

  // Notifies us when our socket is readable or writable.
  ev::io io_;

  // Set to true when the connection is registered on a loop.
  // This is used for a sanity check in the destructor that we are properly
  // un-registered before shutting down.
  bool is_epoll_registered_ = false;

  // Calls which have been sent and are now waiting for a response.
  std::unordered_map<int32_t, OutboundCallPtr> awaiting_response_;

  // Starts as Status::OK, gets set to a shutdown status upon Shutdown().
  Status shutdown_status_;

  // Whether we completed connection negotiation.
  bool negotiation_complete_ = false;

  // We instantiate and store this metric instance at the level of connection, but not at the level
  // of the class emitting metrics (OutboundTransfer) as recommended in metrics.h. This is on
  // purpose, because OutboundTransfer is instantiated each time we need to send payload over a
  // connection and creating a metric instance each time could be a performance hit, because
  // it involves spin lock and search in a metrics map. Therefore we prepare metric instances
  // at connection level.
  scoped_refptr<Histogram> handler_latency_outbound_transfer_;

  struct CompareExpiration {
    template<class Pair>
    bool operator()(const Pair& lhs, const Pair& rhs) const {
      return rhs.first < lhs.first;
    }
  };

  typedef std::pair<MonoTime, std::weak_ptr<OutboundCall>> ExpirationPair;

  std::priority_queue<ExpirationPair,
                      std::vector<ExpirationPair>,
                      CompareExpiration> expiration_queue_;
  ev::timer timer_;

  // Data received on this connection that has not been processed yet.
  GrowableBuffer read_buffer_;

  // sending_* contain bytes and calls we are currently sending to socket
  std::deque<util::RefCntBuffer> sending_;
  std::deque<OutboundDataPtr> sending_outbound_datas_;
  size_t send_position_ = 0;
  bool waiting_write_ready_ = false;

  simple_spinlock outbound_data_queue_lock_;

  // Responses we are going to process.
  std::vector<OutboundDataPtr> outbound_data_to_process_;

  // Responses that are currently being processed.
  // It could be in function variable, but declared as member for optimization.
  std::vector<OutboundDataPtr> outbound_data_being_processed_;

  std::shared_ptr<ReactorTask> process_response_queue_task_;

  // Connection is responsible for sending and receiving bytes.
  // Context is responsible for what to do with them.
  std::unique_ptr<ConnectionContext> context_;
};

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_CONNECTION_H_
