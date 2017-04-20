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

#include <stdint.h>
#include <ev++.h>
#include <atomic>
#include <memory>
#include <unordered_map>

#include <limits>
#include <string>
#include <vector>

#include <boost/intrusive/list.hpp>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/connection_types.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/sasl_client.h"
#include "yb/rpc/sasl_server.h"
#include "yb/rpc/inbound_call.h"
#include "yb/rpc/transfer.h"
#include "yb/sql/sql_session.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/object_pool.h"
#include "yb/util/status.h"

namespace yb {
namespace rpc {

class DumpRunningRpcsRequestPB;

class RpcConnectionPB;

class ReactorThread;

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
class Connection : public RefCountedThreadSafe<Connection> {
 public:
  enum Direction {
    // This host is sending calls via this connection.
        CLIENT,
    // This host is receiving calls via this connection.
        SERVER
  };

  // Create a new Connection.
  // reactor_thread: the reactor that owns us.
  // remote: the address of the remote end
  // socket: the socket to take ownership of.
  // direction: whether we are the client or server side
  Connection(ReactorThread* reactor_thread,
             Sockaddr remote,
             int socket,
             Direction direction);

  // Set underlying socket to non-blocking (or blocking) mode.
  CHECKED_STATUS SetNonBlocking(bool enabled);

  // Register our socket with an epoll loop.  We will only ever be registered in
  // one epoll loop at a time.
  void EpollRegister(ev::loop_ref& loop);  // NOLINT

  virtual ~Connection();

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
  const Sockaddr& remote() const { return remote_; }

  // libev callback when data is available to read.
  void ReadHandler(ev::io& watcher, int revents);  // NOLINT

  // libev callback when we may write to the socket.
  void WriteHandler(ev::io& watcher, int revents);  // NOLINT

  // Safe to be called from other threads.
  std::string ToString() const;

  Direction direction() const { return direction_; }

  Socket* socket() { return &socket_; }

  // Perform negotiation for a connection
  virtual void RunNegotiation(const MonoTime& deadline) = 0;

  // Go through the process of transferring control of the underlying socket back to the Reactor.
  void CompleteNegotiation(const Status& negotiation_status);

  // Indicate that negotiation is complete and that the Reactor is now in control of the socket.
  void MarkNegotiationComplete();

  // Queue a call response back to the client on the server side.
  //
  // This may be called from a non-reactor thread.
  void QueueResponseForCall(InboundCallPtr call);

  ReactorThread* reactor_thread() const { return reactor_thread_; }

  CHECKED_STATUS DumpPB(const DumpRunningRpcsRequestPB& req,
                RpcConnectionPB* resp);

 protected:
  friend struct CallAwaitingResponse;

  friend class QueueTransferTask;

  friend class YBResponseTransferCallbacks;

  friend class RedisResponseTransferCallbacks;

  friend class CQLResponseTransferCallbacks;

  // A call which has been fully sent to the server, which we're waiting for
  // the server to process. This is used on the client side only.
  struct CallAwaitingResponse {
    ~CallAwaitingResponse();

    // Notification from libev that the call has timed out.
    void HandleTimeout(ev::timer& watcher, int revents);  // NOLINT

    Connection* conn;
    OutboundCallPtr call;
    ev::timer timeout_timer;
  };

  typedef std::unordered_map<uint64_t, CallAwaitingResponse*> car_map_t;
  typedef std::unordered_map<uint64_t, InboundCallPtr> inbound_call_map_t;

  // Returns the next valid (positive) sequential call ID by incrementing a counter
  // and ensuring we roll over from INT32_MAX to 0.
  // Negative numbers are reserved for special purposes.
  int32_t GetNextCallId() {
    int32_t call_id = next_call_id_;
    if (PREDICT_FALSE(next_call_id_ == std::numeric_limits<int32_t>::max())) {
      next_call_id_ = 0;
    } else {
      next_call_id_++;
    }
    return call_id;
  }

  virtual TransferCallbacks* GetResponseTransferCallback(InboundCallPtr call) = 0;

  virtual void CreateInboundTransfer() = 0;

  virtual AbstractInboundTransfer* inbound() const = 0;

  // An incoming packet has completed transferring on the server side.
  // This parses the call and delivers it into the call queue.
  virtual void HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) = 0;

  // An incoming packet has completed on the client side. This parses the
  // call response, looks up the CallAwaitingResponse, and calls the
  // client callback.
  void HandleCallResponse(gscoped_ptr<AbstractInboundTransfer> transfer);

  virtual void HandleFinishedTransfer() = 0;

  // The given CallAwaitingResponse has elapsed its user-defined timeout.
  // Set it to Failed.
  void HandleOutboundCallTimeout(CallAwaitingResponse* car);

  // Queue a transfer for sending on this connection.
  // We will take ownership of the transfer.
  // This must be called from the reactor thread.
  void QueueOutbound(gscoped_ptr<OutboundTransfer> transfer);

  // The reactor thread that created this connection.
  ReactorThread* const reactor_thread_;

  // The socket we're communicating on.
  Socket socket_;

  // The remote address we're talking to.
  const Sockaddr remote_;

  // whether we are client or server
  Direction direction_;

  // The last time we read or wrote from the socket.
  MonoTime last_activity_time_;

  // The credentials of the user operating on this connection (if a client user).
  UserCredentials user_credentials_;

  // notifies us when our socket is writable.
  ev::io write_io_;

  // notifies us when our socket is readable.
  ev::io read_io_;

  // Set to true when the connection is registered on a loop.
  // This is used for a sanity check in the destructor that we are properly
  // un-registered before shutting down.
  bool is_epoll_registered_;

  // waiting to be sent
  boost::intrusive::list<OutboundTransfer> outbound_transfers_; // NOLINT(*)

  // Calls which have been sent and are now waiting for a response.
  car_map_t awaiting_response_;

  // Calls which have been received on the server and are currently
  // being handled.
  inbound_call_map_t calls_being_handled_;

  // the next call ID to use
  int32_t next_call_id_;

  // Starts as Status::OK, gets set to a shutdown status upon Shutdown().
  Status shutdown_status_;

  // Temporary vector used when serializing - avoids an allocation
  // when serializing calls.
  std::vector<Slice> slices_tmp_;

  // Pool from which CallAwaitingResponse objects are allocated.
  // Also a funny name.
  ObjectPool<CallAwaitingResponse> car_pool_;
  typedef ObjectPool<CallAwaitingResponse>::scoped_ptr scoped_car;

  // Whether we completed connection negotiation.
  bool negotiation_complete_;

  // We instantiate and store this metric instance at the level of connection, but not at the level
  // of the class emitting metrics (OutboundTransfer) as recommended in metrics.h. This is on
  // purpose, because OutboundTransfer is instantiated each time we need to send payload over a
  // connection and creating a metric instance each time could be a performance hit, because
  // it involves spin lock and search in a metrics map. Therefore we prepare metric instances
  // at connection level.
  scoped_refptr<Histogram> handler_latency_outbound_transfer_;
};

// Callbacks for sending an RPC call response from the server.
// This takes ownership of the InboundCall object so that, once it has
// been responded to, we can free up all of the associated memory.
struct ResponseTransferCallbacks : public TransferCallbacks {
 public:
  ResponseTransferCallbacks() {}

  virtual ~ResponseTransferCallbacks() {}

  virtual void NotifyTransferFinished() override {
    delete this;
  }

  virtual void NotifyTransferAborted(const Status& status) override;

 protected:
  virtual InboundCall* call() = 0;
};

#define RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status) \
  if (PREDICT_FALSE(!status.ok())) {                            \
    if (Socket::IsTemporarySocketError(status.posix_code())) {  \
      return Status::OK(); /* EAGAIN, etc. */                   \
    }                                                           \
    return status;                                              \
  }

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_CONNECTION_H_
