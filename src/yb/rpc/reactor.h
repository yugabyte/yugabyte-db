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

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include <boost/intrusive/list.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/utility.hpp>
#include <ev++.h> // NOLINT
#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/macros.h"

#include "yb/rpc/outbound_call.h"
#include "yb/rpc/reactor_task.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/async_util.h"
#include "yb/util/condition_variable.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/net/socket.h"
#include "yb/util/shared_lock.h"
#include "yb/util/source_location.h"
#include "yb/util/stack_trace.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace rpc {

using ReactorTaskPtr = std::shared_ptr<ReactorTask>;
using ReactorTasks = std::vector<ReactorTaskPtr>;

// When compiling on Mac OS X, use 'kqueue' instead of the default, 'select', for the event loop.
// Otherwise we run into problems because 'select' can't handle connections when more than 1024
// file descriptors are open by the process.
#if defined(__APPLE__)
constexpr unsigned int kDefaultLibEvFlags = ev::KQUEUE;
#else
constexpr unsigned int kDefaultLibEvFlags = ev::AUTO;
#endif

typedef std::list<ConnectionPtr> ConnectionList;

class DumpRunningRpcsRequestPB;
class DumpRunningRpcsResponsePB;
class Messenger;
class MessengerBuilder;
class Reactor;

// Simple metrics information from within a reactor.
struct ReactorMetrics {
  // Number of client RPC connections currently connected.
  size_t num_client_connections;
  // Number of server RPC connections currently connected.
  size_t num_server_connections;
};

YB_DEFINE_ENUM(ReactorState,
               // The reactor is not fully initialized.
               (kNotStarted)
               // The reactor has been initialized and started running.
               (kRunning)
               // Shutdown() has been called.
               (kClosing)
               // The internal shutdown sequence (running on reactor thread) has completed.
               // Immediately after transitioning to this state, it might still take a small amount
               // of time for the final tasks to be aborted, and for the libev loop to be broken.
               (kClosed));

class DelayedTask;

class Reactor {
 public:
  Reactor(Messenger* messenger, int index, const MessengerBuilder &bld);
  ~Reactor();

  // This may be called from another thread.
  const std::string &name() const { return name_; }

  const std::string& LogPrefix() { return log_prefix_; }

  Messenger& messenger() const { return messenger_; }

  // Returns the latest current time updated by this reactor's thread.
  CoarseTimePoint cur_time() const { return cur_time_.load(std::memory_order_acquire); }

  // Schedule the given task's Run() method to be called on the reactor thread. If the reactor shuts
  // down before it is run, the Abort method will be called.
  //
  // Can be called on any thread, including the reactor thread.
  Status ScheduleReactorTask(ReactorTaskPtr task) {
    return ScheduleReactorTask(std::move(task), false /* even_if_not_running */);
  }

  template<class F>
  Status ScheduleReactorFunctor(const F& f, const SourceLocation& source_location) {
    return ScheduleReactorTask(MakeFunctorReactorTask(f, source_location));
  }

  // This should not be called on reactor thread, as the reactor thread should not even be running
  // yet.
  Status Init() EXCLUDES_REACTOR_THREAD;

  // Start the shutdown process by changing the reactor state to kClosing if it is kRunning. When
  // this function returns, the reactor might still be shutting down.
  void StartShutdown() EXCLUDES_REACTOR_THREAD;

  // Add any connections on this reactor thread into the given status dump.
  Status DumpRunningRpcs(
      const DumpRunningRpcsRequestPB& req, DumpRunningRpcsResponsePB* resp)
      EXCLUDES_REACTOR_THREAD;

  // Causes AsyncHandler() to be called on the reactor thread. We should never need to call this
  // function from the reactor thread.
  void WakeThread() EXCLUDES_REACTOR_THREAD;

  // Drop all connections with remote address. Used in tests with broken connectivity.
  void DropIncomingWithRemoteAddress(const IpAddress& address) ON_REACTOR_THREAD;
  void DropOutgoingWithRemoteAddress(const IpAddress& address) ON_REACTOR_THREAD;
  void DropWithRemoteAddress(const IpAddress& address) ON_REACTOR_THREAD;

  // Return true if this reactor thread is the thread currently running.
  bool IsCurrentThread() const EXCLUDES_REACTOR_THREAD;

  // Checks that the current thread is this reactor's thread. The check is always performed in
  // debug mode, and in release mode only if FLAGS_reactor_check_current_thread is set.
  // This function should be called from functions that are scheduled on the reactor thread as
  // callbacks and are not supposed to be called directly.
  void CheckCurrentThread() const;

  // Shut down the given connection, removing it from the connection tracking structures of this
  // reactor.
  //
  // The connection is not explicitly deleted -- shared_ptr reference counting may hold on to the
  // object after this, but callers should assume that it _may_ be deleted by this call.
  void DestroyConnection(Connection *conn, const Status &conn_status) ON_REACTOR_THREAD;

  // Queue a new call to be sent. If the reactor is already shut down, marks
  // the call as failed.
  void QueueOutboundCall(OutboundCallPtr call)
      EXCLUDES_REACTOR_THREAD EXCLUDES(outbound_queue_mtx_);

  // Collect metrics. Works by scheduling a task on reactor thread and waiting for it.
  Status GetMetrics(ReactorMetrics *metrics) EXCLUDES_REACTOR_THREAD;

  // Waits up to 30 seconds to join the reactor thread. If that fails, logs a warning and returns
  // in debug mode, but falls back to unlimited join in release mode after logging the warning.
  void Join() EXCLUDES_REACTOR_THREAD;

  // Queues a server event on all the connections, such that every client receives it.
  Status QueueEventOnAllConnections(
      ServerEventListPtr server_event, const SourceLocation& source_location)
      EXCLUDES_REACTOR_THREAD;

  // Queues a server event on all the connections which pass the filter.
  Status QueueEventOnFilteredConnections(
      ServerEventListPtr server_event, const SourceLocation& source_location,
      ConnectionFilter connection_filter);

  // Queue a new incoming connection. Takes ownership of the underlying fd from
  // 'socket', but not the Socket object itself.
  // If the reactor is already shut down, takes care of closing the socket.
  void RegisterInboundSocket(
      Socket *socket, size_t receive_buffer_size, const Endpoint& remote,
      const ConnectionContextFactoryPtr& factory)
      EXCLUDES_REACTOR_THREAD;

  size_t tick() const {
    return tick_.load();
  }

  ThreadIdForStack tid_for_stack() const;

 private:
  friend class Connection;
  friend class AssignOutboundCallTask;
  friend class DelayedTask;

  // Client-side connection map.
  typedef std::unordered_map<const ConnectionId, ConnectionPtr, ConnectionIdHash> ConnectionMap;

  // We use NO_THREAD_SAFETY_ANALYSIS here because state_ is atomic, despite only being written
  // when pending_tasks_mtx_ is locked.
  ReactorState state() const NO_THREAD_SAFETY_ANALYSIS {
    return state_.load(std::memory_order_acquire);
  }

  // Run the main event loop of the reactor.
  void RunThread();

  // Schedules a task on the reactor thread. Returns an ServiceUnavailalbe if the reactor is not
  // in a valid state, defined as:
  // - If even_if_not_running is true, the only invalid state is kClosed.
  // - If even_if_not_running is false, the only valid state is kRunning.
  // Can be called on any thread, including the reactor thread.
  Status ScheduleReactorTask(ReactorTaskPtr task, bool even_if_not_running);

  // Find or create a new connection to the given remote.
  // If such a connection already exists, returns that, otherwise creates a new one.
  // May return a bad Status if the connect() call fails.
  // The resulting connection object is managed internally by the reactor thread.
  // Deadline specifies latest time allowed for initializing the connection.
  Status FindOrStartConnection(
      const ConnectionId &conn_id,
      const std::string& hostname,
      ConnectionPtr* conn) ON_REACTOR_THREAD;

  // Scan any open connections for idle ones that have been idle longer than
  // connection_keepalive_time_
  void ScanIdleConnections() ON_REACTOR_THREAD;

  // Invoked periodically. Scans tracked_outbound_calls_, checks if any calls have completed
  // (callback has been called) or deleted altogether. For calls that stay in that map for too long,
  // reports them to the log once in a few seconds.
  void ScanForStuckOutboundCalls(CoarseTimePoint now) ON_REACTOR_THREAD;

  // Assign a new outbound call to the appropriate connection object.
  // If this fails, the call is marked failed and completed.
  ConnectionPtr AssignOutboundCall(const OutboundCallPtr &call) ON_REACTOR_THREAD;

  // Register a new connection.
  void RegisterConnection(const ConnectionPtr& conn) ON_REACTOR_THREAD;

  // Actually perform shutdown of the thread, tearing down any connections, etc.
  void ShutdownInternal() ON_REACTOR_THREAD;

  // For each call in outbound_queue_, finds or creates a connection, and notifies all of those
  // connections about queued outbound data.
  void ProcessOutboundQueue() ON_REACTOR_THREAD;

  void CheckReadyToStop() ON_REACTOR_THREAD;

  template<class F>
  Status RunOnReactorThread(const F& f, const SourceLocation& source_location)
      EXCLUDES_REACTOR_THREAD;

  void ShutdownConnection(const ConnectionPtr& conn) ON_REACTOR_THREAD;

  // libev callback for handling async notifications in our libev thread.
  void AsyncHandler(ev::async &watcher, int revents) ON_REACTOR_THREAD; // NOLINT

  // libev callback for handling timer events in our libev thread.
  void TimerHandler(ev::timer &watcher, int revents) ON_REACTOR_THREAD; // NOLINT

  static void AcquireLoop(struct ev_loop* loop) noexcept;
  static void ReleaseLoop(struct ev_loop* loop) noexcept;

  // ----------------------------------------------------------------------------------------------
  // Fields set in the constructor
  // ----------------------------------------------------------------------------------------------

  // parent messenger
  Messenger& messenger_;

  const std::string name_;

  const std::string log_prefix_;

  // Our libev (epoll/kqueue) loop object.
  ev::dynamic_loop loop_;

  // If a connection has been idle for this much time, it is torn down.
  const CoarseMonoClock::Duration connection_keepalive_time_;

  // Scan for idle connections on this granularity.
  const CoarseMonoClock::Duration coarse_timer_granularity_;

  const ReactorTaskPtr process_outbound_queue_task_;

  // Number of outbound connections to create per each destination server address.
  const int num_connections_to_server_;

  const std::shared_ptr<CompletedCallQueue> completed_call_queue_;

  // ----------------------------------------------------------------------------------------------
  // Fields initialized in Init()
  // ----------------------------------------------------------------------------------------------

  scoped_refptr<yb::Thread> thread_;

  // Used by other threads to notify the reactor thread. AsyncHandler is the handler function.
  // Run WakeThread to trigger this async handler.
  ev::async async_;

  // Handles the periodic timer.
  ev::timer timer_;

  // ----------------------------------------------------------------------------------------------
  // Fields protected by pending_tasks_mtx_
  // ----------------------------------------------------------------------------------------------

  mutable simple_spinlock pending_tasks_mtx_;

  // Reactor status, mostly used when shutting down. Guarded by pending_tasks_mtx_ when written, but
  // also read without a lock in some cases.
  std::atomic<ReactorState> state_ GUARDED_BY(pending_tasks_mtx_) {ReactorState::kNotStarted};

  // Tasks to be run within the reactor thread.
  ReactorTasks pending_tasks_ GUARDED_BY(pending_tasks_mtx_);

  // ----------------------------------------------------------------------------------------------
  // Fields protected by outbound_queue_mtx_
  // ----------------------------------------------------------------------------------------------

  simple_spinlock outbound_queue_mtx_;

  bool outbound_queue_stopped_ GUARDED_BY(outbound_queue_mtx_) = false;

  // We add items to this queue in QueueOutboundCall, and remove them in ProcessOutboundQueue and
  // during shutdown.
  std::vector<OutboundCallPtr> outbound_queue_ GUARDED_BY(outbound_queue_mtx_);

  // ----------------------------------------------------------------------------------------------
  // Fields protected by waiting_conns_mtx_
  // ----------------------------------------------------------------------------------------------

  simple_spinlock waiting_conns_mtx_;

  // Set of connections that should be completed before we can stop this thread.
  std::unordered_set<ConnectionPtr> waiting_conns_ GUARDED_BY(waiting_conns_mtx_);

  // ----------------------------------------------------------------------------------------------
  // Atomic fields
  // ----------------------------------------------------------------------------------------------

  // The current monotonic time.  Updated every coarse_timer_granularity_.
  std::atomic<CoarseTimePoint> cur_time_;

  // ----------------------------------------------------------------------------------------------
  // Fields that are only accessed on the reactor thread
  // ----------------------------------------------------------------------------------------------

  // Scheduled (but not yet run) delayed tasks.
  std::set<std::shared_ptr<DelayedTask>> scheduled_tasks_ GUARDED_BY_REACTOR_THREAD;

  // Map of sockaddrs to Connection objects for outbound (client) connections.
  ConnectionMap client_conns_ GUARDED_BY_REACTOR_THREAD;

  // List of current connections coming into the server.
  ConnectionList server_conns_ GUARDED_BY_REACTOR_THREAD;

  // We found that we should shut down, but not all connections are ready for it.
  bool stopping_ GUARDED_BY_REACTOR_THREAD = false;

  // Outbound calls currently being processed. Could be a local variable, but implemented as a
  // member field as an optimization to avoid memory allocation.
  std::vector<OutboundCallPtr> processing_outbound_queue_ GUARDED_BY_REACTOR_THREAD;

  // Used in ProcessOutboundQueue to group outbound calls by connection and find the set of unique
  // connections that we should try to write to. Should always be empty except when
  // ProcessOutboundQueue is executing. An optimization to avoid memory allocation.
  std::vector<ConnectionPtr> processing_connections_ GUARDED_BY_REACTOR_THREAD;

  // Tasks moved from pending_tasks_ that are currently being processed by AsyncHandler.
  ReactorTasks pending_tasks_being_processed_ GUARDED_BY_REACTOR_THREAD;

  std::atomic<size_t> tick_{0};

  // ----------------------------------------------------------------------------------------------
  // A subsystem for proactively tracking stuck OutboundCalls where the callback has never been
  // called. We add calls to tracked_outbound_calls_ in AssignOutboundCall and remove them as soon
  // as we find out the callback has been invoked.

  struct TrackedOutboundCall {
    int32_t call_id = 0;
    OutboundCallWeakPtr call_weak;  // If this cannot be locked, we remove the entry.
    CoarseTimePoint next_check_time;  // Next time to check the callback status.
  };

  class CallIdTag;
  class NextCheckTimeTag;
  using TrackedOutboundCalls = boost::multi_index_container<
      TrackedOutboundCall,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::tag<CallIdTag>,
              boost::multi_index::member<TrackedOutboundCall,
                                         int32_t,
                                         &TrackedOutboundCall::call_id>>,
          boost::multi_index::ordered_non_unique<
              boost::multi_index::tag<NextCheckTimeTag>,
              boost::multi_index::member<TrackedOutboundCall,
                                         CoarseTimePoint,
                                         &TrackedOutboundCall::next_check_time>
          >>>;

  TrackedOutboundCalls tracked_outbound_calls_ GUARDED_BY_REACTOR_THREAD;

  DISALLOW_COPY_AND_ASSIGN(Reactor);
};

}  // namespace rpc
}  // namespace yb
