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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#ifndef YB_RPC_CONNECTION_H_
#define YB_RPC_CONNECTION_H_

#include <stdint.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/version.hpp>
#include <ev++.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/stream.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/ev_util.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace rpc {

YB_DEFINE_ENUM(ConnectionDirection, (CLIENT)(SERVER));

typedef boost::container::small_vector_base<OutboundDataPtr> OutboundDataBatch;

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
// This class is not fully thread-safe. It is accessed only from the context of a
// single Reactor except where otherwise specified.
//
class Connection final : public StreamContext, public std::enable_shared_from_this<Connection> {
 public:
  typedef ConnectionDirection Direction;

  // Create a new Connection.
  // reactor: the reactor that owns us.
  // remote: the address of the remote end
  // socket: the socket to take ownership of.
  // direction: whether we are the client or server side
  // context: context for this connection. Context is used by connection to handle
  // protocol specific actions, such as parsing of incoming data into calls.
  Connection(Reactor* reactor,
             std::unique_ptr<Stream> stream,
             Direction direction,
             RpcMetrics* rpc_metrics,
             std::unique_ptr<ConnectionContext> context);

  ~Connection();

  CoarseTimePoint last_activity_time() const {
    return last_activity_time_;
  }

  void UpdateLastActivity() override;

  // Returns true if we are not in the process of receiving or sending a
  // message, and we have no outstanding calls.
  // When reason_not_idle is specified it contains reason why this connection is not idle.
  bool Idle(std::string* reason_not_idle = nullptr) const;

  // A human-readable reason why the connection is not idle. Empty string if connection is idle.
  std::string ReasonNotIdle() const;

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
  const Endpoint& remote() const;

  const Protocol* protocol() const;

  // The address of the local end of the connection.
  const Endpoint& local() const;

  void HandleTimeout(ev::timer& watcher, int revents); // NOLINT

  // Safe to be called from other threads.
  std::string ToString() const;

  Direction direction() const { return direction_; }

  // Queue a call response back to the client on the server side.
  //
  // This is usually called by the IPC worker thread when the response is set, but in some
  // circumstances may also be called by the reactor thread (e.g. if the service has shut down).
  // In addition to this, its also called for processing events generated by the server.
  void QueueOutboundData(OutboundDataPtr outbound_data);

  void QueueOutboundDataBatch(const OutboundDataBatch& batch);

  Reactor* reactor() const { return reactor_; }

  CHECKED_STATUS DumpPB(const DumpRunningRpcsRequestPB& req,
                        RpcConnectionPB* resp);

  // Do appropriate actions after adding outbound call.
  void OutboundQueued();

  // An incoming packet has completed on the client side. This parses the
  // call response, looks up the CallAwaitingResponse, and calls the
  // client callback.
  CHECKED_STATUS HandleCallResponse(CallData* call_data);

  ConnectionContext& context() { return *context_; }

  void CallSent(OutboundCallPtr call);

  CHECKED_STATUS Start(ev::loop_ref* loop);

  // Try to parse already received data.
  void ParseReceived();

  void Close();

  RpcMetrics& rpc_metrics() {
    return *rpc_metrics_;
  }

 private:
  CHECKED_STATUS DoWrite();

  // Does actual outbound data queueing. Invoked in appropriate reactor thread.
  size_t DoQueueOutboundData(OutboundDataPtr call, bool batch);

  void ProcessResponseQueue();

  // Stream context implementation
  void UpdateLastRead() override;

  void UpdateLastWrite() override;

  void Transferred(const OutboundDataPtr& data, const Status& status) override;
  void Destroy(const Status& status) override;
  Result<size_t> ProcessReceived(ReadBufferFull read_buffer_full) override;
  void Connected() override;
  StreamReadBuffer& ReadBuffer() override;

  std::string LogPrefix() const;

  // The reactor thread that created this connection.
  Reactor* const reactor_;

  std::unique_ptr<Stream> stream_;

  // whether we are client or server
  Direction direction_;

  // The last time we read or wrote from the socket.
  CoarseTimePoint last_activity_time_;

  // Calls which have been sent and are now waiting for a response.
  std::unordered_map<int32_t, OutboundCallPtr> awaiting_response_;

  // Starts as Status::OK, gets set to a shutdown status upon Shutdown(). Guarded by
  // outbound_data_queue_lock_.
  Status shutdown_status_;

  // We instantiate and store this metric instance at the level of connection, but not at the level
  // of the class emitting metrics (OutboundTransfer) as recommended in metrics.h. This is on
  // purpose, because OutboundTransfer is instantiated each time we need to send payload over a
  // connection and creating a metric instance each time could be a performance hit, because
  // it involves spin lock and search in a metrics map. Therefore we prepare metric instances
  // at connection level.
  scoped_refptr<Histogram> handler_latency_outbound_transfer_;

  struct ExpirationEntry {
    CoarseTimePoint time;
    std::weak_ptr<OutboundCall> call;
    // See Stream::Send for details.
    size_t handle;
  };

  struct CompareExpiration {
    bool operator()(const ExpirationEntry& lhs, const ExpirationEntry& rhs) const {
      return rhs.time < lhs.time;
    }
  };

  std::priority_queue<ExpirationEntry,
                      std::vector<ExpirationEntry>,
                      CompareExpiration> expiration_queue_;

  EvTimerHolder timer_;

  simple_spinlock outbound_data_queue_lock_;

  // Responses we are going to process.
  std::vector<OutboundDataPtr> outbound_data_to_process_;

  // Responses that are currently being processed.
  // It could be in function variable, but declared as member for optimization.
  std::vector<OutboundDataPtr> outbound_data_being_processed_;

  std::shared_ptr<ReactorTask> process_response_queue_task_;

  // RPC related metrics.
  RpcMetrics* rpc_metrics_;

  // Connection is responsible for sending and receiving bytes.
  // Context is responsible for what to do with them.
  std::unique_ptr<ConnectionContext> context_;

  std::atomic<uint64_t> responded_call_count_{0};
};

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_CONNECTION_H_
