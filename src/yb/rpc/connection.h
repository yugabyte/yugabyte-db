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

#pragma once

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
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/version.hpp>

#include <ev++.h>
#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/stream.h"
#include "yb/rpc/reactor_thread_role.h"

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
    return last_activity_time_.load(std::memory_order_acquire);
  }

  void UpdateLastActivity() override;

  // Returns true if we are not in the process of receiving or sending a
  // message, and we have no outstanding calls.
  // When reason_not_idle is specified it contains reason why this connection is not idle.
  bool Idle(std::string* reason_not_idle = nullptr) const
      ON_REACTOR_THREAD;

  // A human-readable reason why the connection is not idle. Empty string if connection is idle.
  std::string ReasonNotIdle() const ON_REACTOR_THREAD;

  // Fail any calls which are currently queued or awaiting response.
  // Prohibits any future calls (they will be failed immediately with this
  // same Status).
  // Returns true if this call initiates shutdown. false when shutdown was already invoked.
  bool Shutdown(const Status& status) ON_REACTOR_THREAD EXCLUDES(outbound_data_queue_mtx_);

  // Queue a new call to be made. If the queuing fails, the call will be
  // marked failed.
  // Takes ownership of the 'call' object regardless of whether it succeeds or fails.
  // This may be called from a non-reactor thread.
  void QueueOutboundCall(const OutboundCallPtr& call) ON_REACTOR_THREAD
      EXCLUDES(outbound_data_queue_mtx_);

  // The address of the remote end of the connection.
  const Endpoint& remote() const;

  const Protocol* protocol() const;

  // The address of the local end of the connection.
  const Endpoint& local() const;

  void HandleTimeout(ev::timer& watcher, int revents) ON_REACTOR_THREAD;  // NOLINT

  // Safe to be called from other threads.
  std::string ToString() const;

  // Dump the connection state for provided call id. This includes the information realted to
  // connection shutdown time, and whether the call id is present in the active_calls_ or not.
  // call_ptr is used only for logging to correlate with OutboundCall trace.
  void QueueDumpConnectionState(int32_t call_id, const void* call_ptr) const;

  Direction direction() const { return direction_; }

  // Queue a call response back to the client on the server side.
  //
  // This is usually called by the IPC worker thread when the response is set, but in some
  // circumstances may also be called by the reactor thread (e.g. if the service has shut down).
  // In addition to this, it is also called for processing events generated by the server.
  //
  // In case is called outside of the reactor thread, it might return an error if it fails to submit
  // a task to the reactor thread, e.g. when the reactor is shutting down.
  Status QueueOutboundData(OutboundDataPtr outbound_data) EXCLUDES(outbound_data_queue_mtx_);

  void QueueOutboundDataBatch(const OutboundDataBatch& batch) ON_REACTOR_THREAD
      EXCLUDES(outbound_data_queue_mtx_);

  Reactor* reactor() const { return reactor_; }

  Status DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) ON_REACTOR_THREAD;

  // Do appropriate actions after adding outbound call. If the connection is shutting down,
  // returns the connection's shutdown status.
  Status OutboundQueued() ON_REACTOR_THREAD EXCLUDES(outbound_data_queue_mtx_);

  // An incoming packet has completed on the client side. This parses the
  // call response, looks up the CallAwaitingResponse, and calls the
  // client callback.
  Status HandleCallResponse(CallData* call_data) ON_REACTOR_THREAD;

  ConnectionContext& context() { return *context_; }

  Status Start(ev::loop_ref* loop) ON_REACTOR_THREAD;

  // Try to parse already received data.
  void ParseReceived();

  void Close();

  RpcMetrics& rpc_metrics() {
    return rpc_metrics_;
  }

  // Returns the connection's shutdown status, or OK if shutdown has not happened yet.
  Status ShutdownStatus() const EXCLUDES(outbound_data_queue_mtx_);

  bool shutdown_initiated() const {
    return shutdown_initiated_.load(std::memory_order_acquire);
  }

  bool shutdown_completed() const {
    return shutdown_completed_.load(std::memory_order_acquire);
  }

  // Used in Reactor-based stuck outbound call monitoring mechanism.
  void ForceCallExpiration(const OutboundCallPtr& call) ON_REACTOR_THREAD;

  void ReportQueueTime(MonoDelta delta);

 private:
  // Marks the given call as failed and schedules destruction of the connection.
  void FailCallAndDestroyConnection(const OutboundDataPtr& outbound_data,
                                    const Status& status) ON_REACTOR_THREAD;

  void ScheduleDestroyConnection(const Status& status) ON_REACTOR_THREAD;

  // Does actual outbound data queuing.
  //
  // If the `batch` argument is false, calls the OutboundQueued function at the end. See
  // QueueOutboundDataBatch for how this is used.
  //
  // Returns the handle corresponding to the queued call, or std::numeric_limits<size_t>::max() in
  // case the handle is unknown, or an error in case the connection is shutting down.
  Result<size_t> DoQueueOutboundData(OutboundDataPtr call, bool batch) ON_REACTOR_THREAD
      EXCLUDES(outbound_data_queue_mtx_);

  void ProcessResponseQueue() ON_REACTOR_THREAD EXCLUDES(outbound_data_queue_mtx_);

  // Stream context implementation
  void UpdateLastRead() override;

  void UpdateLastWrite() override;

  void Transferred(const OutboundDataPtr& data, const Status& status) override;
  void Destroy(const Status& status) ON_REACTOR_THREAD override;
  Result<size_t> ProcessReceived(ReadBufferFull read_buffer_full)
      ON_REACTOR_THREAD override;
  Status Connected() override;
  StreamReadBuffer& ReadBuffer() override;

  void CleanupExpirationQueue(CoarseTimePoint now) ON_REACTOR_THREAD;
  // call_ptr is used only for logging to correlate with OutboundCall trace.
  void DumpConnectionState(int32_t call_id, const void* call_ptr) const ON_REACTOR_THREAD
      EXCLUDES(outbound_data_queue_mtx_);

  std::string LogPrefix() const;

  // ----------------------------------------------------------------------------------------------
  // Fields set in the constructor
  // ----------------------------------------------------------------------------------------------

  // The reactor thread that created this connection.
  Reactor* const reactor_;

  const std::unique_ptr<Stream> stream_;

  // Whether we are client or server.
  const Direction direction_;

  // RPC related metrics.
  RpcMetrics& rpc_metrics_;

  // Connection is responsible for sending and receiving bytes.
  // Context is responsible for what to do with them.
  const std::unique_ptr<ConnectionContext> context_;

  // We instantiate and store this metric instance at the level of connection, but not at the level
  // of the class emitting metrics (OutboundTransfer) as recommended in metrics.h. This is on
  // purpose, because OutboundTransfer is instantiated each time we need to send payload over a
  // connection and creating a metric instance each time could be a performance hit, because
  // it involves spin lock and search in a metrics map. Therefore we prepare metric instances
  // at connection level.
  scoped_refptr<Histogram> handler_latency_outbound_transfer_;

  // ----------------------------------------------------------------------------------------------
  // Fields that are only accessed on the reactor thread and do not require other synchronization
  // ----------------------------------------------------------------------------------------------

  struct ActiveCall {
    int32_t id = 0; // Call id.
    OutboundCallPtr call; // Call object, null if call has expired.
    CoarseTimePoint expires_at; // Expiration time, kMax when call has expired.
    CallHandle handle = 0; // Call handle in outbound stream.

    std::string ToString(std::optional<CoarseTimePoint> now = std::nullopt) const {
      return YB_STRUCT_TO_STRING(
          id,
          (call, AsString(pointer_cast<const void*>(call.get()))),
          (expires_at, yb::ToStringRelativeToNow(expires_at, now)),
          handle);
    }
  };

  class ExpirationTag;
  // Calls which have been sent and are now waiting for a response.
  using ActiveCalls = boost::multi_index_container<
      ActiveCall,
      boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
          boost::multi_index::member<ActiveCall, int32_t, &ActiveCall::id>
        >,
        boost::multi_index::ordered_non_unique<
          boost::multi_index::tag<ExpirationTag>,
          boost::multi_index::member<ActiveCall, CoarseTimePoint, &ActiveCall::expires_at>
        >>>;
  ActiveCalls active_calls_ GUARDED_BY_REACTOR_THREAD;

  // Responses that are currently being processed.
  // This could be a function-local variable, but is declared as a member for optimization, so that
  // we can reuse the allocated memory between invocations of ProcessResponseQueue.
  std::vector<OutboundDataPtr> outbound_data_being_processed_
      GUARDED_BY_REACTOR_THREAD;

  EvTimerHolder timer_ GUARDED_BY_REACTOR_THREAD;

  // ----------------------------------------------------------------------------------------------
  // Fields protected by outbound_data_queue_mtx_
  // ----------------------------------------------------------------------------------------------

  mutable simple_spinlock outbound_data_queue_mtx_;

  // Responses we are going to process.
  std::vector<OutboundDataPtr> outbound_data_to_process_ GUARDED_BY(outbound_data_queue_mtx_);

  // Starts as Status::OK, gets set to a shutdown status upon Shutdown().
  Status shutdown_status_ GUARDED_BY(outbound_data_queue_mtx_);
  std::atomic<CoarseTimePoint> shutdown_time_{CoarseTimePoint::min()};

  std::shared_ptr<ReactorTask> process_response_queue_task_ GUARDED_BY(outbound_data_queue_mtx_);

  // ----------------------------------------------------------------------------------------------
  // Atomic fields
  // ----------------------------------------------------------------------------------------------

  std::atomic<uint64_t> responded_call_count_{0};
  std::atomic<size_t> active_calls_during_shutdown_{0};
  std::atomic<size_t> calls_queued_after_shutdown_{0};
  std::atomic<size_t> responses_queued_after_shutdown_{0};

  // The last time we read or wrote from the socket.
  std::atomic<CoarseTimePoint> last_activity_time_{CoarseTimePoint::min()};

  std::atomic<bool> queued_destroy_connection_{false};

  std::atomic<bool> shutdown_initiated_{false};
  std::atomic<bool> shutdown_completed_{false};
};

}  // namespace rpc
}  // namespace yb
