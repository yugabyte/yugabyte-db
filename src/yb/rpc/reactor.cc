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

#include "yb/rpc/reactor.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <sys/types.h>

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <ev++.h>

#include "yb/ash/wait_state.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stringprintf.h"

#include "yb/rpc/connection_context.h"
#include "yb/rpc/connection.h"
#include "yb/rpc/delayed_task.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor_task.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rpc_metrics.h"
#include "yb/rpc/server_event.h"

#include "yb/util/atomic.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/errno.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metric_entity.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/socket.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

DEFINE_UNKNOWN_uint64(rpc_read_buffer_size, 0,
              "RPC connection read buffer size. 0 to auto detect.");
DECLARE_string(local_ip_for_outbound_sockets);
DECLARE_int32(num_connections_to_server);
DECLARE_int32(socket_receive_buffer_size);
DEFINE_RUNTIME_bool(reactor_check_current_thread, true,
                    "Enforce the requirement that operations that require running on a reactor "
                    "thread are always running on the correct reactor thread.");

DEFINE_RUNTIME_int32(stuck_outbound_call_default_timeout_sec, 120,
    "Default timeout for reporting purposes for the Reactor-based stuck OutboundCall tracking and "
    "expiration mechanism. That mechanism itself is controlled by the "
    "reactor_based_outbound_call_expiration_delay_ms flag. Note that this flag does not force a "
    "call to be timed out, it just specifies the interval after which the call is logged.");
TAG_FLAG(stuck_outbound_call_default_timeout_sec, advanced);

DEFINE_RUNTIME_int32(stuck_outbound_call_check_interval_sec, 30,
    "Check and report each stuck outbound call at most once per this number of seconds.");
TAG_FLAG(stuck_outbound_call_check_interval_sec, advanced);

DEFINE_RUNTIME_int32(reactor_based_outbound_call_expiration_delay_ms, 1000,
    "Expire OutboundCalls using Reactor-level logic with this delay after the timeout, as an "
    "additional layer of protection against stuck outbound calls. This safety mechanism is "
    "disabled if this flag is set to 0.");
TAG_FLAG(reactor_based_outbound_call_expiration_delay_ms, advanced);

namespace yb {
namespace rpc {

namespace {

static const char* kShutdownMessage = "Shutdown connection";

const Status& AbortedError() {
  static Status result = STATUS(Aborted, kShutdownMessage, "" /* msg2 */, Errno(ESHUTDOWN));
  return result;
}

const Status& ServiceUnavailableError() {
  static Status result = STATUS(
      ServiceUnavailable, kShutdownMessage, "" /* msg2 */, Errno(ESHUTDOWN));
  return result;
}

// Callback for libev fatal errors (eg running out of file descriptors).
// Unfortunately libev doesn't plumb these back through to the caller, but
// instead just expects the callback to abort.
//
// This implementation is slightly preferable to the built-in one since
// it uses a FATAL log message instead of printing to stderr, which might
// not end up anywhere useful in a daemonized context.
void LibevSysErr(const char* msg) noexcept {
  PLOG(FATAL) << "LibEV fatal error: " << msg;
}

void DoInitLibEv() {
  ev::set_syserr_cb(LibevSysErr);
}

inline bool HasReactorStartedClosing(ReactorState state) {
  return state == ReactorState::kClosing || state == ReactorState::kClosed;
}

size_t PatchReceiveBufferSize(size_t receive_buffer_size) {
  return std::max<size_t>(
      64_KB, FLAGS_rpc_read_buffer_size ? FLAGS_rpc_read_buffer_size : receive_buffer_size);
}

inline bool ShouldCheckCurrentThread() {
#ifdef NDEBUG
  // Release mode, use the flag.
  return FLAGS_reactor_check_current_thread;
#else
  // Debug mode, always check.
  return true;
#endif
}

Result<Socket> CreateClientSocket(const Endpoint& remote) {
  int flags = Socket::FLAG_NONBLOCKING;
  if (remote.address().is_v6()) {
    flags |= Socket::FLAG_IPV6;
  }
  Socket socket;
  Status status = socket.Init(flags);
  if (status.ok()) {
    status = socket.SetNoDelay(true);
  }
  if (!status.ok()) {
    LOG(WARNING) << "failed to create an outbound connection because a new socket could not "
                 << "be created: " << status.ToString();
    return status;
  }
  return std::move(socket);
}

template <class... Args>
Result<std::unique_ptr<Stream>> CreateStream(
    const StreamFactories& factories, const Protocol* protocol, const StreamCreateData& data) {
  auto it = factories.find(protocol);
  if (it == factories.end()) {
    return STATUS_FORMAT(NotFound, "Unknown protocol: $0", protocol);
  }
  return it->second->Create(data);
}

void ShutdownIfRemoteAddressIs(const ConnectionPtr& conn, const IpAddress& address) {
  auto peer = conn->remote();

  if (peer.address() != address) {
    return;
  }

  conn->Close();
  LOG(INFO) << "Dropped connection: " << conn->ToString();
}

// Task to call an arbitrary function within the reactor thread.
template<class F>
class RunFunctionTask : public ReactorTask {
 public:
  RunFunctionTask(const F& f, const SourceLocation& source_location)
      : ReactorTask(source_location), function_(f) {}

  void Run(Reactor *reactor) override {
    status_ = function_(reactor);
    latch_.CountDown();
  }

  // Wait until the function has completed, and return the Status returned by the function.
  Status Wait() {
    latch_.Wait();
    return status_;
  }

 private:
  void DoAbort(const Status &status) override {
    status_ = status;
    latch_.CountDown();
  }

  F function_;
  Status status_;
  CountDownLatch latch_{1};
};

bool ShouldTrackOutboundCalls() {
  return FLAGS_reactor_based_outbound_call_expiration_delay_ms > 0;
}

CoarseTimePoint ExpirationEnforcementTime(CoarseTimePoint expires_at) {
  return expires_at == CoarseTimePoint::max()
      ? CoarseTimePoint::max()
      : expires_at + 1ms * FLAGS_reactor_based_outbound_call_expiration_delay_ms;
}

} // anonymous namespace

// ------------------------------------------------------------------------------------------------
// Public methods of Reactor
// ------------------------------------------------------------------------------------------------

Reactor::Reactor(Messenger* messenger,
                 int index,
                 const MessengerBuilder &bld)
    : messenger_(*messenger),
      name_(StringPrintf("%s_R%03d", messenger->name().c_str(), index)),
      log_prefix_(name_ + ": "),
      loop_(kDefaultLibEvFlags),
      connection_keepalive_time_(bld.connection_keepalive_time()),
      coarse_timer_granularity_(bld.coarse_timer_granularity()),
      process_outbound_queue_task_(
          MakeFunctorReactorTask(std::bind(&Reactor::ProcessOutboundQueue, this),
                                 SOURCE_LOCATION())),
      num_connections_to_server_(bld.num_connections_to_server()),
      completed_call_queue_(std::make_shared<CompletedCallQueue>()),
      cur_time_(CoarseMonoClock::Now()) {
  static std::once_flag libev_once;
  std::call_once(libev_once, DoInitLibEv);

  // Store this to user data to be able to obtain Reactor in ReleaseLoop and AcquireLoop.
  ev_set_userdata(loop_.raw_loop, this);
  ev_set_loop_release_cb(loop_.raw_loop, &ReleaseLoop, &AcquireLoop);

  VLOG_WITH_PREFIX(1) << "Create reactor with keep alive_time: "
                      << yb::ToString(connection_keepalive_time_)
                      << ", coarse timer granularity: " << yb::ToString(coarse_timer_granularity_);

}

Reactor::~Reactor() {
  LOG_IF_WITH_PREFIX(DFATAL, !pending_tasks_.empty())
      << "Not empty pending tasks when destroyed reactor: " << yb::ToString(pending_tasks_);
}

Status Reactor::Init() {
  DVLOG_WITH_PREFIX(6) << "Called Reactor::Init()";
  auto old_state = ReactorState::kNotStarted;
  {
    // Even though state_ is atomic, we still need to take the lock to make sure state_
    // and pending_tasks_ are being modified in a consistent way.
    std::lock_guard pending_tasks_lock(pending_tasks_mtx_);
    SCHECK(
        state_.compare_exchange_strong(
            old_state, ReactorState::kRunning, std::memory_order_acq_rel),
        IllegalState, "State was expected to be $0 but was $1 during reactor initialization",
        old_state, state());
  }
  // Register to get async notifications in our libev loop.
  async_.set(loop_);
  async_.set<Reactor, &Reactor::AsyncHandler>(this);
  async_.start();

  // Register the timer watcher.
  // The timer is used for closing old TCP connections and applying
  // backpressure.
  timer_.set(loop_);
  timer_.set<Reactor, &Reactor::TimerHandler>(this);
  timer_.start(ToSeconds(coarse_timer_granularity_),
               ToSeconds(coarse_timer_granularity_));

  // Create Reactor thread.
  const std::string group_name = messenger_.name() + "_reactor";
  return yb::Thread::Create(group_name, group_name, &Reactor::RunThread, this, &thread_);
}

void Reactor::StartShutdown() {
  LOG_IF_WITH_PREFIX(FATAL, IsCurrentThread())
      << __PRETTY_FUNCTION__ << " cannot be called from the reactor thread";
  auto old_state = ReactorState::kRunning;
  bool state_change_success;
  {
    // Even though state_ is atomic, we still need to take the lock to make sure state_
    // and pending_tasks_ are being modified in a consistent way.
    std::lock_guard pending_tasks_lock(pending_tasks_mtx_);
    state_change_success = state_.compare_exchange_strong(
        old_state, ReactorState::kClosing, std::memory_order_acq_rel);
  }
  if (state_change_success) {
    VLOG_WITH_PREFIX(1) << "shutting down Reactor thread.";
    WakeThread();
  } else if (!HasReactorStartedClosing(old_state)) {
    LOG_WITH_PREFIX(WARNING) << __PRETTY_FUNCTION__ << " called with an unexpected reactor state: "
                             << old_state;
  }
}

void Reactor::ShutdownConnection(const ConnectionPtr& conn) {
  VLOG_WITH_PREFIX(1) << "shutting down " << conn->ToString();
  if (!conn->shutdown_completed()) {
    conn->Shutdown(ServiceUnavailableError());
  }
  if (!conn->context().Idle()) {
    VLOG_WITH_PREFIX(1) << "connection is not idle: " << conn->ToString();
    conn->context().ListenIdle([this, weak_conn = std::weak_ptr(conn)]() {
      auto conn = weak_conn.lock();
      if (conn) {
        VLOG_WITH_PREFIX(1) << "connection became idle " << conn->ToString();
        std::lock_guard lock(waiting_conns_mtx_);
        waiting_conns_.erase(conn);
      }
    });
    std::lock_guard lock(waiting_conns_mtx_);
    waiting_conns_.insert(conn);
  } else {
    VLOG_WITH_PREFIX(1) << "connection is idle: " << conn->ToString();
  }
}

void Reactor::ShutdownInternal() {
  stopping_ = true;

  // Tear down any outbound TCP connections.
  VLOG_WITH_PREFIX(1) << "tearing down outbound TCP connections...";
  decltype(client_conns_) client_conns = std::move(client_conns_);
  for (auto& pair : client_conns) {
    ShutdownConnection(pair.second);
  }
  client_conns.clear();

  // Tear down any inbound TCP connections.
  VLOG_WITH_PREFIX(1) << "tearing down inbound TCP connections...";
  for (const ConnectionPtr& conn : server_conns_) {
    ShutdownConnection(conn);
  }
  server_conns_.clear();

  // Abort any scheduled tasks.
  //
  // These won't be found in the Reactor's list of pending tasks
  // because they've been "run" (that is, they've been scheduled).
  VLOG_WITH_PREFIX(1) << "aborting scheduled tasks";
  Status aborted = AbortedError();
  for (const auto& task : scheduled_tasks_) {
    task->Abort(aborted);
  }
  scheduled_tasks_.clear();

  // async_handler_tasks_ are the tasks added by ScheduleReactorTask.
  VLOG_WITH_PREFIX(1) << "aborting async handler tasks";
  for (const auto& task : pending_tasks_being_processed_) {
    task->Abort(aborted);
  }

  VLOG_WITH_PREFIX(1) << "aborting outbound calls";
  CHECK(processing_outbound_queue_.empty()) << yb::ToString(processing_outbound_queue_);
  {
    std::lock_guard lock(outbound_queue_mtx_);
    outbound_queue_stopped_ = true;
    outbound_queue_.swap(processing_outbound_queue_);
  }
  for (auto& call : processing_outbound_queue_) {
    call->Transferred(aborted, /* conn= */ nullptr);
  }
  processing_outbound_queue_.clear();

  completed_call_queue_->Shutdown();
}

Status Reactor::GetMetrics(ReactorMetrics *metrics) {
  return RunOnReactorThread([metrics](Reactor* reactor) {
    ReactorThreadRoleGuard guard;
    metrics->num_client_connections = reactor->client_conns_.size();
    metrics->num_server_connections = reactor->server_conns_.size();
    return Status::OK();
  }, SOURCE_LOCATION());
}

void Reactor::Join() {
  auto join_result = ThreadJoiner(thread_.get()).give_up_after(30s).Join();
  if (join_result.ok()) {
    return;
  }
  if (join_result.IsInvalidArgument()) {
    LOG_WITH_PREFIX(WARNING) << join_result;
    return;
  }
  LOG_WITH_PREFIX(DFATAL) << "Failed to join Reactor " << thread_->ToString() << ": "
                          << join_result;
  // Fallback to endless join in release mode.
  thread_->Join();
}

Status Reactor::QueueEventOnAllConnections(
    ServerEventListPtr server_event, const SourceLocation& source_location) {
  return ScheduleReactorFunctor([server_event = std::move(server_event)](Reactor* reactor) {
    ReactorThreadRoleGuard guard;
    for (const ConnectionPtr& conn : reactor->server_conns_) {
      auto queuing_status = conn->QueueOutboundData(server_event);
      LOG_IF(DFATAL, !queuing_status.ok())
          << "Could not queue a server event in "
          << __PRETTY_FUNCTION__ << ": " << queuing_status;
    }
  }, source_location);
}

Status Reactor::QueueEventOnFilteredConnections(
    ServerEventListPtr server_event, const SourceLocation& source_location,
    ConnectionFilter connection_filter) {
  return ScheduleReactorFunctor([server_event = std::move(server_event),
      connection_filter = std::move(connection_filter)](Reactor* reactor) {
        ReactorThreadRoleGuard guard;
        for (const ConnectionPtr& conn : reactor->server_conns_) {
          if (connection_filter(conn)) {
            auto queuing_status = conn->QueueOutboundData(server_event);
            LOG_IF(DFATAL, !queuing_status.ok())
                << "Could not queue a server event in "
                << __PRETTY_FUNCTION__ << ": " << queuing_status;
          }
        }
      },
      source_location);
}

Status Reactor::DumpRunningRpcs(const DumpRunningRpcsRequestPB& req,
                                DumpRunningRpcsResponsePB* resp) {
  SCOPED_WAIT_STATUS(DumpRunningRpc_WaitOnReactor);
  return RunOnReactorThread([&req, resp](Reactor* reactor) -> Status {
    ReactorThreadRoleGuard guard;
    for (const ConnectionPtr& conn : reactor->server_conns_) {
      RETURN_NOT_OK(conn->DumpPB(req, resp->add_inbound_connections()));
    }
    for (const auto& entry : reactor->client_conns_) {
      Connection* conn = entry.second.get();
      RETURN_NOT_OK(conn->DumpPB(req, resp->add_outbound_connections()));
    }
    return Status::OK();
  }, SOURCE_LOCATION());
}

void Reactor::WakeThread() {
  async_.send();
}

void Reactor::CheckReadyToStop() {
  bool all_connections_idle;
  {
    std::lock_guard lock(waiting_conns_mtx_);
    all_connections_idle = waiting_conns_.empty();
    if (VLOG_IS_ON(4)) {
      VLOG_WITH_PREFIX(4) << "Check ready to stop: " << thread_->ToString() << ", "
                          << "waiting connections: " << waiting_conns_.size();

      for (const auto& conn : waiting_conns_) {
        VLOG_WITH_PREFIX(4) << "Connection: " << conn->ToString()
                            << ", idle=" << conn->Idle()
                            << ", why: " << conn->ReasonNotIdle();
      }
    }
  }

  if (all_connections_idle) {
    VLOG_WITH_PREFIX(4) << "Reactor ready to stop, breaking loop: " << this;

    VLOG_WITH_PREFIX(2) << "Marking reactor as closed: " << thread_.get()->ToString();
    ReactorTasks final_tasks;
    {
      std::lock_guard pending_tasks_lock(pending_tasks_mtx_);
      state_.store(ReactorState::kClosed, std::memory_order_release);
      final_tasks.swap(pending_tasks_);
    }
    VLOG_WITH_PREFIX(2) << "Running final pending task aborts: " << thread_.get()->ToString();;
    for (auto task : final_tasks) {
      task->Abort(ServiceUnavailableError());
    }
    VLOG_WITH_PREFIX(2) << "Breaking reactor loop: " << thread_.get()->ToString();;
    loop_.break_loop(); // break the epoll loop and terminate the thread
  }
}

// Handle async events.  These events are sent to the reactor by other threads that want to bring
// something to our attention, like the fact that we're shutting down, or the fact that there is a
// new outbound Transfer ready to send.
void Reactor::AsyncHandler(ev::async &watcher, int revents) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << "Events: " << revents;

  auto scope_exit = ScopeExit([this] {
    // Thread safety analysis does not realize that this scope exit closure only runs in this
    // function that is already running on reactor thread.
    ReactorThreadRoleGuard guard;
    pending_tasks_being_processed_.clear();
  });

  CHECK(pending_tasks_being_processed_.empty());

  {
    std::lock_guard pending_tasks_lock(pending_tasks_mtx_);
    pending_tasks_being_processed_.swap(pending_tasks_);
  }

  if (PREDICT_FALSE(HasReactorStartedClosing(state()))) {
    // ShutdownInternal will abort every task in pending_tasks_being_processed_.
    ShutdownInternal();
    CheckReadyToStop();
    return;
  }

  for (const auto &task : pending_tasks_being_processed_) {
    task->Run(this);
  }
}

void Reactor::RegisterConnection(const ConnectionPtr& conn) {
  Status s = conn->Start(&loop_);
  if (s.ok()) {
    server_conns_.push_back(conn);
  } else {
    LOG_WITH_PREFIX(WARNING) << "Failed to start connection: " << conn->ToString() << ": " << s;
  }
}

ConnectionPtr Reactor::AssignOutboundCall(const OutboundCallPtr& call) {
  ConnectionPtr conn;
  Status s = FindOrStartConnection(call->conn_id(), call->hostname(), &conn);
  if (PREDICT_FALSE(!s.ok())) {
    call->SetFailed(s);
    return ConnectionPtr();
  }

  call->SetConnection(conn);
  call->SetCompletedCallQueue(completed_call_queue_);
  conn->QueueOutboundCall(call);

  if (ShouldTrackOutboundCalls()) {
    auto expires_at = call->expires_at();
    tracked_outbound_calls_.insert(TrackedOutboundCall {
      .call_id = call->call_id(),
      .call_weak = call,
      .next_check_time = expires_at == CoarseTimePoint::max()
          ? call->start_time() + FLAGS_stuck_outbound_call_default_timeout_sec * 1s
          : expires_at + FLAGS_reactor_based_outbound_call_expiration_delay_ms * 1ms
    });
  }
  return conn;
}

// Handles timer events.  The periodic timer:
//
// 1. updates Reactor::cur_time_
// 2. every tcp_conn_timeo_ seconds, close down connections older than
//    tcp_conn_timeo_ seconds.
//
void Reactor::TimerHandler(ev::timer &watcher, int revents) {
  CheckCurrentThread();

  if (EV_ERROR & revents) {
    LOG_WITH_PREFIX(WARNING) << "Reactor got an error in the timer handler.";
    return;
  }

  if (stopping_) {
    CheckReadyToStop();
    return;
  }

  auto now = CoarseMonoClock::Now();
  VLOG_WITH_PREFIX(4) << "timer tick at " << ToSeconds(now.time_since_epoch());
  cur_time_.store(now, std::memory_order_release);

  ScanIdleConnections();

  ScanForStuckOutboundCalls(now);
}

void Reactor::ScanIdleConnections() {
  if (connection_keepalive_time_ == CoarseMonoClock::Duration::zero()) {
    VLOG_WITH_PREFIX(3) << "Skipping Idle connections check since connection_keepalive_time_ = 0";
    return;
  }

  // enforce TCP connection timeouts
  auto conn_iter = server_conns_.begin();
  auto conn_iter_end = server_conns_.end();
  uint64_t timed_out = 0;
  for (; conn_iter != conn_iter_end; ) {
    const ConnectionPtr& conn = *conn_iter;
    if (!conn->Idle()) {
      VLOG_WITH_PREFIX(3) << "Connection " << conn->ToString() << " not idle";
      ++conn_iter; // TODO: clean up this loop
      continue;
    }

    auto conn_last_activity_time = conn->last_activity_time();
    auto connection_delta = cur_time() - conn_last_activity_time;
    if (connection_delta > connection_keepalive_time_) {
      conn->Shutdown(STATUS_FORMAT(
          NetworkError, "Connection timed out after $0", ToSeconds(connection_delta)));
      LOG_WITH_PREFIX(INFO)
          << "DEBUG: Closing idle connection: " << conn->ToString()
          << " - it has been idle for " << ToSeconds(connection_delta) << "s";
      VLOG(1) << "(delta: " << ToSeconds(connection_delta)
          << ", current time: " << ToSeconds(cur_time().time_since_epoch())
          << ", last activity time: "
          << ToSeconds(conn_last_activity_time.time_since_epoch()) << ")";
      server_conns_.erase(conn_iter++);
      ++timed_out;
    } else {
      ++conn_iter;
    }
  }

  // TODO: above only times out on the server side.
  // Clients may want to set their keepalive timeout as well.

  VLOG_IF_WITH_PREFIX(1, timed_out > 0) << "timed out " << timed_out << " TCP connections.";
}

void Reactor::ScanForStuckOutboundCalls(CoarseTimePoint now) {
  if (!ShouldTrackOutboundCalls()) {
    return;
  }

  auto& index_by_call_id = tracked_outbound_calls_.get<CallIdTag>();
  while (auto call_id_opt = completed_call_queue_->Pop()) {
    index_by_call_id.erase(*call_id_opt);
  }

  auto& index_by_next_check_time = tracked_outbound_calls_.get<NextCheckTimeTag>();
  while (!index_by_next_check_time.empty()) {
    auto& entry = *index_by_next_check_time.begin();
    // It is useful to check the next entry even if its scheduled next check time is later than
    // now, to erase entries corresponding to completed calls as soon as possible. This alone,
    // even without the completed call queue, mostly solves #19090.
    auto call = entry.call_weak.lock();
    if (!call || call->callback_invoked()) {
      index_by_next_check_time.erase(index_by_next_check_time.begin());
      continue;
    }
    if (entry.next_check_time > now) {
      break;
    }

    // Normally, timeout should be enforced at the connection level. Here, we will catch failures
    // to do that.
    const bool forcing_timeout =
        !call->callback_triggered() && now >= ExpirationEnforcementTime(call->expires_at());

    auto call_str = call->DebugString();

    auto conn = call->connection();

    LOG_WITH_PREFIX(WARNING) << "Stuck OutboundCall: " << call_str
                             << (forcing_timeout ? " (forcing a timeout)" : "");
    IncrementCounter(messenger_.rpc_metrics()->outbound_calls_stuck);

    if (forcing_timeout) {
      // Only do this after we've logged the call, so that the log would capture the call state
      // before the forced timeout.
      if (conn) {
        // This calls SetTimedOut so we don't need to do it directly.
        conn->ForceCallExpiration(call);
      } else {
        LOG_WITH_PREFIX(WARNING) << "Connection is not set for a call that is being forcefully "
                                 << "expired: " << call_str;
        call->SetTimedOut();
      }
    }

    index_by_next_check_time.modify(index_by_next_check_time.begin(), [now](auto& tracked_call) {
      tracked_call.next_check_time = now + FLAGS_stuck_outbound_call_check_interval_sec * 1s;
    });
  }

  if (tracked_outbound_calls_.size() >= 1000) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 1)
        << "tracked_outbound_calls_ has a large number of entries: "
        << tracked_outbound_calls_.size();
  }
}

bool Reactor::IsCurrentThread() const {
  return thread_.get() == yb::Thread::current_thread();
}

void Reactor::CheckCurrentThread() const {
  if (ShouldCheckCurrentThread()) {
    CHECK_EQ(thread_.get(), yb::Thread::current_thread())
        << "Operation is not running on the correct reactor thread";
  }
}

void Reactor::RunThread() {
  ThreadRestrictions::SetWaitAllowed(false);
  ThreadRestrictions::SetIOAllowed(false);
  DVLOG_WITH_PREFIX(6) << "Calling Reactor::RunThread()...";
  IncrementGauge(messenger_.rpc_metrics()->busy_reactors);
  loop_.run(/* flags */ 0);
  DecrementGauge(messenger_.rpc_metrics()->busy_reactors);
  VLOG_WITH_PREFIX(1) << "thread exiting.";
}

Status Reactor::FindOrStartConnection(const ConnectionId &conn_id,
                                      const std::string& hostname,
                                      ConnectionPtr* conn) {
  {
    auto conn_iter = client_conns_.find(conn_id);
    if (conn_iter != client_conns_.end()) {
      *conn = (*conn_iter).second;
      return Status::OK();
    }
  }

  if (state() != ReactorState::kRunning) {
    return ServiceUnavailableError();
  }

  // No connection to this remote. Need to create one.
  VLOG_WITH_PREFIX(2) << "FindOrStartConnection: creating new connection for "
                      << conn_id.ToString();

  // Create a new socket and start connecting to the remote.
  auto sock = VERIFY_RESULT(CreateClientSocket(conn_id.remote()));
  if (messenger_.has_outbound_ip_base_.load(std::memory_order_acquire) &&
      !messenger_.test_outbound_ip_base_.is_unspecified()) {
    auto address_bytes(messenger_.test_outbound_ip_base_.to_v4().to_bytes());
    // Use different addresses for public/private endpoints.
    // Private addresses are even, and public are odd.
    // So if base address is "private" and destination address is "public" we will modify
    // originating address to be "public" also.
    address_bytes[3] |= conn_id.remote().address().to_v4().to_bytes()[3] & 1;
    boost::asio::ip::address_v4 outbound_address(address_bytes);
    auto status = sock.SetReuseAddr(true);
    if (status.ok()) {
      status = sock.Bind(Endpoint(outbound_address, 0));
    }
    LOG_IF_WITH_PREFIX(WARNING, !status.ok()) << "Bind " << outbound_address << " failed: "
                                              << status;
  } else if (FLAGS_local_ip_for_outbound_sockets.empty()) {
    auto outbound_address = conn_id.remote().address().is_v6()
        ? messenger_.outbound_address_v6()
        : messenger_.outbound_address_v4();
    if (!outbound_address.is_unspecified()) {
      auto status = sock.SetReuseAddr(true);
      if (status.ok()) {
        status = sock.Bind(Endpoint(outbound_address, 0));
      }
      LOG_IF_WITH_PREFIX(WARNING, !status.ok()) << "Bind " << outbound_address << " failed: "
                                                << status;
    }
  }

  if (FLAGS_socket_receive_buffer_size) {
    WARN_NOT_OK(sock.SetReceiveBufferSize(FLAGS_socket_receive_buffer_size),
                "Set receive buffer size failed: ");
  }

  auto receive_buffer_size = PatchReceiveBufferSize(VERIFY_RESULT(sock.GetReceiveBufferSize()));

  auto stream = VERIFY_RESULT(CreateStream(
      messenger_.stream_factories_, conn_id.protocol(),
      StreamCreateData {
        .remote = conn_id.remote(),
        .remote_hostname = hostname,
        .socket = &sock,
        .receive_buffer_size = receive_buffer_size,
        .mem_tracker = messenger_.connection_context_factory_->buffer_tracker(),
        .metric_entity = messenger_.metric_entity(),
      }));
  auto context = messenger_.connection_context_factory_->Create(receive_buffer_size);

  // Register the new connection in our map.
  auto connection = std::make_shared<Connection>(
      this,
      std::move(stream),
      ConnectionDirection::CLIENT,
      messenger_.rpc_metrics().get(),
      std::move(context));

  RETURN_NOT_OK(connection->Start(&loop_));

  // Insert into the client connection map to avoid duplicate connection requests.
  CHECK(client_conns_.emplace(conn_id, connection).second);

  conn->swap(connection);
  return Status::OK();
}

void Reactor::DropWithRemoteAddress(const IpAddress& address) {
  DropIncomingWithRemoteAddress(address);
  DropOutgoingWithRemoteAddress(address);
}

void Reactor::DropIncomingWithRemoteAddress(const IpAddress& address) {
  VLOG_WITH_PREFIX(1) << "Dropping Incoming connections from " << address;
  for (auto& conn : server_conns_) {
    ShutdownIfRemoteAddressIs(conn, address);
  }
}

void Reactor::DropOutgoingWithRemoteAddress(const IpAddress& address) {
  VLOG_WITH_PREFIX(1) << "Dropping Outgoing connections to " << address;
  for (auto& pair : client_conns_) {
    ShutdownIfRemoteAddressIs(pair.second, address);
  }
}

void Reactor::DestroyConnection(Connection *conn, const Status &conn_status) {
  VLOG_WITH_PREFIX(3) << "DestroyConnection(" << conn->ToString() << ", " << conn_status.ToString()
                      << ")";

  ConnectionPtr retained_conn = conn->shared_from_this();
  if (!conn->Shutdown(conn_status)) {
    // Connection was already destroyed. Error logged by Connection::Shutdown itself.
    return;
  }

  // Unlink connection from lists.
  if (conn->direction() == ConnectionDirection::CLIENT) {
    bool erased = false;
    for (int idx = 0; idx < num_connections_to_server_; idx++) {
      auto it = client_conns_.find(ConnectionId(conn->remote(), idx, conn->protocol()));
      if (it != client_conns_.end() && it->second.get() == conn) {
        client_conns_.erase(it);
        erased = true;
      }
    }
    if (!erased) {
      LOG_WITH_PREFIX(WARNING) << "Looking for " << conn->ToString();
      for (auto &p : client_conns_) {
        LOG_WITH_PREFIX(WARNING) << "  Client connection: " << p.first.ToString() << ", "
                                 << p.second->ToString();
      }
      LOG_WITH_PREFIX(FATAL)
          << "Couldn't find connection for any index to " << conn->ToString()
          << ", destroy reason: " << conn_status;
    }
  } else if (conn->direction() == ConnectionDirection::SERVER) {
    auto it = server_conns_.begin();
    while (it != server_conns_.end()) {
      if ((*it).get() == conn) {
        server_conns_.erase(it);
        break;
      }
      ++it;
    }
  }

  ShutdownConnection(retained_conn);
}

void Reactor::ProcessOutboundQueue() {
  CHECK(processing_outbound_queue_.empty()) << yb::ToString(processing_outbound_queue_);
  {
    std::lock_guard lock(outbound_queue_mtx_);
    outbound_queue_.swap(processing_outbound_queue_);
  }
  if (processing_outbound_queue_.empty()) {
    return;
  }

  // Find the set of unique connections assigned to the given set of calls, and notify each
  // connection that outbound data has been queued. Create new connections as needed.

  CHECK(processing_connections_.empty());
  processing_connections_.reserve(processing_outbound_queue_.size());
  for (auto& call : processing_outbound_queue_) {
    auto conn = AssignOutboundCall(call);
    // The returned connection can be null in case of an error. The call should have already been
    // set as failed in that case.
    if (conn) {
      processing_connections_.push_back(std::move(conn));
    }
  }
  processing_outbound_queue_.clear();

  std::sort(processing_connections_.begin(), processing_connections_.end());
  auto new_end = std::unique(processing_connections_.begin(), processing_connections_.end());
  processing_connections_.erase(new_end, processing_connections_.end());
  for (auto& conn : processing_connections_) {
    if (conn) {
      // If this fails, the connection will be destroyed.
      auto ignored_status = conn->OutboundQueued();
    }
  }
  processing_connections_.clear();
}

void Reactor::QueueOutboundCall(OutboundCallPtr call) {
  DVLOG_WITH_PREFIX(3) << "Queueing outbound call "
                       << call->ToString() << " to remote " << call->conn_id().remote();

  bool was_empty = false;
  bool closing = false;
  {
    std::lock_guard lock(outbound_queue_mtx_);
    if (!outbound_queue_stopped_) {
      was_empty = outbound_queue_.empty();
      outbound_queue_.push_back(call);
    } else {
      closing = true;
    }
  }
  if (closing) {
    call->Transferred(AbortedError(), nullptr /* conn */);
    return;
  }
  if (was_empty) {
    auto scheduling_status = ScheduleReactorTask(process_outbound_queue_task_);
    // We should not call call->Transferred(scheduling_status, ...) here, because the call has
    // already been added to the outbound queue. ShutdownInternal will abort it.
    LOG_IF_WITH_PREFIX(WARNING, !scheduling_status.ok())
      << "Failed to schedule outbound queue processing task: "
      << scheduling_status;
  }
  TRACE_TO(call->trace(), "Scheduled.");
}

void Reactor::RegisterInboundSocket(
    Socket *socket, size_t receive_buffer_size, const Endpoint& remote,
    const ConnectionContextFactoryPtr& factory) {
  VLOG_WITH_PREFIX(3) << "New inbound connection to " << remote;
  receive_buffer_size = PatchReceiveBufferSize(receive_buffer_size);

  auto stream = CreateStream(
      messenger_.stream_factories_, messenger_.listen_protocol_,
      StreamCreateData {
        .remote = remote,
        .remote_hostname = std::string(),
        .socket = socket,
        .receive_buffer_size = receive_buffer_size,
        .mem_tracker = factory->buffer_tracker(),
        .metric_entity = messenger_.metric_entity()
      });
  if (!stream.ok()) {
    LOG_WITH_PREFIX(DFATAL) << "Failed to create stream for " << remote << ": " << stream.status();
    return;
  }
  auto conn = std::make_shared<Connection>(this,
                                           std::move(*stream),
                                           ConnectionDirection::SERVER,
                                           messenger_.rpc_metrics().get(),
                                           factory->Create(receive_buffer_size));

  auto scheduling_status = ScheduleReactorFunctor([conn = std::move(conn)](Reactor* reactor) {
    ReactorThreadRoleGuard guard;
    reactor->RegisterConnection(conn);
  }, SOURCE_LOCATION());
  LOG_IF_WITH_PREFIX(DFATAL, !scheduling_status.ok())
      << "Could not schedule a reactor task in " << __PRETTY_FUNCTION__
      << ": " << scheduling_status;
}

Status Reactor::ScheduleReactorTask(ReactorTaskPtr task, bool even_if_not_running) {
  // task should never be null, so not using an SCHECK here.
  CHECK_NOTNULL(task);
  bool was_empty;
  {
    // Even though state_ is atomic, we still need to take the lock to make sure state_
    // and pending_tasks_ are being modified in a consistent way.
    std::lock_guard pending_tasks_lock(pending_tasks_mtx_);
    auto current_state = state();
    bool failure = even_if_not_running ? current_state == ReactorState::kClosed
                                       : current_state != ReactorState::kRunning;
    if (failure) {
      auto msg = Format("$0: Cannot schedule a reactor task. "
                        "Current state: $1, even_if_not_running: $2",
                        log_prefix_, current_state, even_if_not_running);
      if (HasReactorStartedClosing(current_state)) {
        // It is important to use Aborted status here, because it is used by downstream code to
        // distinguish between the case when the reactor is closed and the case when the reactor is
        // not running yet.
        return STATUS_FORMAT(Aborted, msg);
      }
      return STATUS_FORMAT(ServiceUnavailable, msg);
    }
    was_empty = pending_tasks_.empty();
    pending_tasks_.push_back(std::move(task));
  }
  if (was_empty) {
    WakeThread();
  }

  return Status::OK();
}

template<class F>
Status Reactor::RunOnReactorThread(const F& f, const SourceLocation& source_location) {
  auto task = std::make_shared<RunFunctionTask<F>>(f, source_location);
  RETURN_NOT_OK(ScheduleReactorTask(task));
  return task->Wait();
}

void Reactor::ReleaseLoop(struct ev_loop* loop) noexcept {
  auto reactor = static_cast<Reactor*>(ev_userdata(loop));
  DecrementGauge(reactor->messenger_.rpc_metrics()->busy_reactors);
}

void Reactor::AcquireLoop(struct ev_loop* loop) noexcept {
  auto reactor = static_cast<Reactor*>(ev_userdata(loop));
  IncrementGauge(reactor->messenger_.rpc_metrics()->busy_reactors);
}

}  // namespace rpc
}  // namespace yb
