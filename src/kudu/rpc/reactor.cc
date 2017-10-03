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

#include "kudu/rpc/reactor.h"

#include <arpa/inet.h>
#include <boost/intrusive/list.hpp>
#include <ev++.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <glog/logging.h>

#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/rpc/connection.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/negotiation.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/rpc/rpc_introspection.pb.h"
#include "kudu/rpc/sasl_client.h"
#include "kudu/rpc/sasl_server.h"
#include "kudu/rpc/transfer.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/errno.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/monotime.h"
#include "kudu/util/thread.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/thread_restrictions.h"
#include "kudu/util/trace.h"
#include "kudu/util/status.h"
#include "kudu/util/net/socket.h"

// When compiling on Mac OS X, use 'kqueue' instead of the default, 'select', for the event loop.
// Otherwise we run into problems because 'select' can't handle connections when more than 1024
// file descriptors are open by the process.
#if defined(__APPLE__)
static const int kDefaultLibEvFlags = ev::KQUEUE;
#else
static const int kDefaultLibEvFlags = ev::AUTO;
#endif

using std::string;
using std::shared_ptr;

DEFINE_int64(rpc_negotiation_timeout_ms, 3000,
             "Timeout for negotiating an RPC connection.");
TAG_FLAG(rpc_negotiation_timeout_ms, advanced);
TAG_FLAG(rpc_negotiation_timeout_ms, runtime);

namespace kudu {
namespace rpc {

namespace {
Status ShutdownError(bool aborted) {
  const char* msg = "reactor is shutting down";
  return aborted ?
      Status::Aborted(msg, "", ESHUTDOWN) :
      Status::ServiceUnavailable(msg, "", ESHUTDOWN);
}
} // anonymous namespace

ReactorThread::ReactorThread(Reactor *reactor, const MessengerBuilder &bld)
  : loop_(kDefaultLibEvFlags),
    cur_time_(MonoTime::Now(MonoTime::COARSE)),
    last_unused_tcp_scan_(cur_time_),
    reactor_(reactor),
    connection_keepalive_time_(bld.connection_keepalive_time_),
    coarse_timer_granularity_(bld.coarse_timer_granularity_) {
}

Status ReactorThread::Init() {
  DCHECK(thread_.get() == nullptr) << "Already started";
  DVLOG(6) << "Called ReactorThread::Init()";
  // Register to get async notifications in our epoll loop.
  async_.set(loop_);
  async_.set<ReactorThread, &ReactorThread::AsyncHandler>(this);
  async_.start();

  // Register the timer watcher.
  // The timer is used for closing old TCP connections and applying
  // backpressure.
  timer_.set(loop_);
  timer_.set<ReactorThread, &ReactorThread::TimerHandler>(this); // NOLINT(*)
  timer_.start(coarse_timer_granularity_.ToSeconds(),
               coarse_timer_granularity_.ToSeconds());

  // Create Reactor thread.
  return kudu::Thread::Create("reactor", "rpc reactor", &ReactorThread::RunThread, this, &thread_);
}

void ReactorThread::Shutdown() {
  CHECK(reactor_->closing()) << "Should be called after setting closing_ flag";

  VLOG(1) << name() << ": shutting down Reactor thread.";
  WakeThread();
}

void ReactorThread::ShutdownInternal() {
  DCHECK(IsCurrentThread());

  // Tear down any outbound TCP connections.
  Status service_unavailable = ShutdownError(false);
  VLOG(1) << name() << ": tearing down outbound TCP connections...";
  for (auto c = client_conns_.begin(); c != client_conns_.end();
       c = client_conns_.begin()) {
    const scoped_refptr<Connection>& conn = (*c).second;
    VLOG(1) << name() << ": shutting down " << conn->ToString();
    conn->Shutdown(service_unavailable);
    client_conns_.erase(c);
  }

  // Tear down any inbound TCP connections.
  VLOG(1) << name() << ": tearing down inbound TCP connections...";
  for (const scoped_refptr<Connection>& conn : server_conns_) {
    VLOG(1) << name() << ": shutting down " << conn->ToString();
    conn->Shutdown(service_unavailable);
  }
  server_conns_.clear();

  // Abort any scheduled tasks.
  //
  // These won't be found in the ReactorThread's list of pending tasks
  // because they've been "run" (that is, they've been scheduled).
  Status aborted = ShutdownError(true); // aborted
  for (DelayedTask* task : scheduled_tasks_) {
    task->Abort(aborted); // should also free the task.
  }
  scheduled_tasks_.clear();
}

ReactorTask::ReactorTask() {
}
ReactorTask::~ReactorTask() {
}

Status ReactorThread::GetMetrics(ReactorMetrics *metrics) {
  DCHECK(IsCurrentThread());
  metrics->num_client_connections_ = client_conns_.size();
  metrics->num_server_connections_ = server_conns_.size();
  return Status::OK();
}

Status ReactorThread::DumpRunningRpcs(const DumpRunningRpcsRequestPB& req,
                                      DumpRunningRpcsResponsePB* resp) {
  DCHECK(IsCurrentThread());
  for (const scoped_refptr<Connection>& conn : server_conns_) {
    RETURN_NOT_OK(conn->DumpPB(req, resp->add_inbound_connections()));
  }
  for (const conn_map_t::value_type& entry : client_conns_) {
    Connection* conn = entry.second.get();
    RETURN_NOT_OK(conn->DumpPB(req, resp->add_outbound_connections()));
  }
  return Status::OK();
}

void ReactorThread::WakeThread() {
  async_.send();
}

// Handle async events.  These events are sent to the reactor by other
// threads that want to bring something to our attention, like the fact that
// we're shutting down, or the fact that there is a new outbound Transfer
// ready to send.
void ReactorThread::AsyncHandler(ev::async &watcher, int revents) {
  DCHECK(IsCurrentThread());

  if (PREDICT_FALSE(reactor_->closing())) {
    ShutdownInternal();
    loop_.break_loop(); // break the epoll loop and terminate the thread
    return;
  }

  boost::intrusive::list<ReactorTask> tasks;
  reactor_->DrainTaskQueue(&tasks);

  while (!tasks.empty()) {
    ReactorTask &task = tasks.front();
    tasks.pop_front();
    task.Run(this);
  }
}

void ReactorThread::RegisterConnection(const scoped_refptr<Connection>& conn) {
  DCHECK(IsCurrentThread());

  // Set a limit on how long the server will negotiate with a new client.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(FLAGS_rpc_negotiation_timeout_ms));

  Status s = StartConnectionNegotiation(conn, deadline);
  if (!s.ok()) {
    LOG(ERROR) << "Server connection negotiation failed: " << s.ToString();
    DestroyConnection(conn.get(), s);
  }
  server_conns_.push_back(conn);
}

void ReactorThread::AssignOutboundCall(const shared_ptr<OutboundCall> &call) {
  DCHECK(IsCurrentThread());
  scoped_refptr<Connection> conn;

  // TODO: Move call deadline timeout computation into OutboundCall constructor.
  const MonoDelta &timeout = call->controller()->timeout();
  MonoTime deadline;
  if (!timeout.Initialized()) {
    LOG(WARNING) << "Client call " << call->remote_method().ToString()
                 << " has no timeout set for connection id: "
                 << call->conn_id().ToString();
    deadline = MonoTime::Max();
  } else {
    deadline = MonoTime::Now(MonoTime::FINE);
    deadline.AddDelta(timeout);
  }

  Status s = FindOrStartConnection(call->conn_id(), &conn, deadline);
  if (PREDICT_FALSE(!s.ok())) {
    call->SetFailed(s);
    return;
  }

  conn->QueueOutboundCall(call);
}

//
// Handles timer events.  The periodic timer:
//
// 1. updates Reactor::cur_time_
// 2. every tcp_conn_timeo_ seconds, close down connections older than
//    tcp_conn_timeo_ seconds.
//
void ReactorThread::TimerHandler(ev::timer &watcher, int revents) {
  DCHECK(IsCurrentThread());
  if (EV_ERROR & revents) {
    LOG(WARNING) << "Reactor " << name() << " got an error in "
      "the timer handler.";
    return;
  }
  MonoTime now(MonoTime::Now(MonoTime::COARSE));
  VLOG(4) << name() << ": timer tick at " << now.ToString();
  cur_time_ = now;

  ScanIdleConnections();
}

void ReactorThread::RegisterTimeout(ev::timer *watcher) {
  watcher->set(loop_);
}

void ReactorThread::ScanIdleConnections() {
  DCHECK(IsCurrentThread());
  // enforce TCP connection timeouts
  auto c = server_conns_.begin();
  auto c_end = server_conns_.end();
  uint64_t timed_out = 0;
  for (; c != c_end; ) {
    const scoped_refptr<Connection>& conn = *c;
    if (!conn->Idle()) {
      VLOG(3) << "Connection " << conn->ToString() << " not idle";
      ++c; // TODO: clean up this loop
      continue;
    }

    MonoDelta connection_delta(cur_time_.GetDeltaSince(conn->last_activity_time()));
    if (connection_delta.MoreThan(connection_keepalive_time_)) {
      conn->Shutdown(Status::NetworkError(
                       StringPrintf("connection timed out after %s seconds",
                                    connection_keepalive_time_.ToString().c_str())));
      VLOG(1) << "Timing out connection " << conn->ToString() << " - it has been idle for "
              << connection_delta.ToSeconds() << "s";
      server_conns_.erase(c++);
      ++timed_out;
    } else {
      ++c;
    }
  }

  // TODO: above only times out on the server side.
  // Clients may want to set their keepalive timeout as well.

  VLOG_IF(1, timed_out > 0) << name() << ": timed out " << timed_out << " TCP connections.";
}

const std::string &ReactorThread::name() const {
  return reactor_->name();
}

MonoTime ReactorThread::cur_time() const {
  return cur_time_;
}

Reactor *ReactorThread::reactor() {
  return reactor_;
}

bool ReactorThread::IsCurrentThread() const {
  return thread_.get() == kudu::Thread::current_thread();
}

void ReactorThread::RunThread() {
  ThreadRestrictions::SetWaitAllowed(false);
  ThreadRestrictions::SetIOAllowed(false);
  DVLOG(6) << "Calling ReactorThread::RunThread()...";
  loop_.run(0);
  VLOG(1) << name() << " thread exiting.";

  // No longer need the messenger. This causes the messenger to
  // get deleted when all the reactors exit.
  reactor_->messenger_.reset();
}

Status ReactorThread::FindOrStartConnection(const ConnectionId &conn_id,
                                            scoped_refptr<Connection>* conn,
                                            const MonoTime &deadline) {
  DCHECK(IsCurrentThread());
  conn_map_t::const_iterator c = client_conns_.find(conn_id);
  if (c != client_conns_.end()) {
    *conn = (*c).second;
    return Status::OK();
  }

  // No connection to this remote. Need to create one.
  VLOG(2) << name() << " FindOrStartConnection: creating "
          << "new connection for " << conn_id.remote().ToString();

  // Create a new socket and start connecting to the remote.
  Socket sock;
  RETURN_NOT_OK(CreateClientSocket(&sock));
  bool connect_in_progress;
  RETURN_NOT_OK(StartConnect(&sock, conn_id.remote(), &connect_in_progress));

  // Register the new connection in our map.
  *conn = new Connection(this, conn_id.remote(), sock.Release(), Connection::CLIENT);
  (*conn)->set_user_credentials(conn_id.user_credentials());

  // Kick off blocking client connection negotiation.
  Status s = StartConnectionNegotiation(*conn, deadline);
  if (s.IsIllegalState()) {
    // Return a nicer error message to the user indicating -- if we just
    // forward the status we'd get something generic like "ThreadPool is closing".
    return Status::ServiceUnavailable("Client RPC Messenger shutting down");
  }
  // Propagate any other errors as-is.
  RETURN_NOT_OK_PREPEND(s, "Unable to start connection negotiation thread");

  // Insert into the client connection map to avoid duplicate connection requests.
  client_conns_.insert(conn_map_t::value_type(conn_id, *conn));
  return Status::OK();
}

Status ReactorThread::StartConnectionNegotiation(const scoped_refptr<Connection>& conn,
    const MonoTime &deadline) {
  DCHECK(IsCurrentThread());

  scoped_refptr<Trace> trace(new Trace());
  ADOPT_TRACE(trace.get());
  TRACE("Submitting negotiation task for $0", conn->ToString());
  RETURN_NOT_OK(reactor()->messenger()->negotiation_pool()->SubmitClosure(
      Bind(&Negotiation::RunNegotiation, conn, deadline)));
  return Status::OK();
}

void ReactorThread::CompleteConnectionNegotiation(const scoped_refptr<Connection>& conn,
      const Status &status) {
  DCHECK(IsCurrentThread());
  if (PREDICT_FALSE(!status.ok())) {
    DestroyConnection(conn.get(), status);
    return;
  }

  // Switch the socket back to non-blocking mode after negotiation.
  Status s = conn->SetNonBlocking(true);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(DFATAL) << "Unable to set connection to non-blocking mode: " << s.ToString();
    DestroyConnection(conn.get(), s);
    return;
  }
  conn->MarkNegotiationComplete();
  conn->EpollRegister(loop_);
}

Status ReactorThread::CreateClientSocket(Socket *sock) {
  Status ret = sock->Init(Socket::FLAG_NONBLOCKING);
  if (ret.ok()) {
    ret = sock->SetNoDelay(true);
  }
  LOG_IF(WARNING, !ret.ok()) << "failed to create an "
    "outbound connection because a new socket could not "
    "be created: " << ret.ToString();
  return ret;
}

Status ReactorThread::StartConnect(Socket *sock, const Sockaddr &remote, bool *in_progress) {
  Status ret = sock->Connect(remote);
  if (ret.ok()) {
    VLOG(3) << "StartConnect: connect finished immediately for " << remote.ToString();
    *in_progress = false; // connect() finished immediately.
    return ret;
  }

  int posix_code = ret.posix_code();
  if (Socket::IsTemporarySocketError(posix_code) || (posix_code == EINPROGRESS)) {
    // The connect operation is in progress.
    *in_progress = true;
    VLOG(3) << "StartConnect: connect in progress for " << remote.ToString();
    return Status::OK();
  } else {
    LOG(WARNING) << "failed to create an outbound connection to " << remote.ToString()
                 << " because connect failed: " << ret.ToString();
    return ret;
  }
}

void ReactorThread::DestroyConnection(Connection *conn,
                                      const Status &conn_status) {
  DCHECK(IsCurrentThread());

  conn->Shutdown(conn_status);

  // Unlink connection from lists.
  if (conn->direction() == Connection::CLIENT) {
    ConnectionId conn_id(conn->remote(), conn->user_credentials());
    auto it = client_conns_.find(conn_id);
    CHECK(it != client_conns_.end()) << "Couldn't find connection " << conn->ToString();
    client_conns_.erase(it);
  } else if (conn->direction() == Connection::SERVER) {
    auto it = server_conns_.begin();
    while (it != server_conns_.end()) {
      if ((*it).get() == conn) {
        server_conns_.erase(it);
        break;
      }
      ++it;
    }
  }
}

DelayedTask::DelayedTask(boost::function<void(const Status &)> func,
                         MonoDelta when)
    : func_(std::move(func)), when_(std::move(when)), thread_(nullptr) {}

void DelayedTask::Run(ReactorThread* thread) {
  DCHECK(thread_ == nullptr) << "Task has already been scheduled";
  DCHECK(thread->IsCurrentThread());

  // Schedule the task to run later.
  thread_ = thread;
  timer_.set(thread->loop_);
  timer_.set<DelayedTask, &DelayedTask::TimerHandler>(this);
  timer_.start(when_.ToSeconds(), // after
               0);                // repeat
  thread_->scheduled_tasks_.insert(this);
}

void DelayedTask::Abort(const Status& abort_status) {
  func_(abort_status);
  delete this;
}

void DelayedTask::TimerHandler(ev::timer& watcher, int revents) {
  // We will free this task's memory.
  thread_->scheduled_tasks_.erase(this);

  if (EV_ERROR & revents) {
    string msg = "Delayed task got an error in its timer handler";
    LOG(WARNING) << msg;
    Abort(Status::Aborted(msg)); // Will delete 'this'.
  } else {
    func_(Status::OK());
    delete this;
  }
}

Reactor::Reactor(const shared_ptr<Messenger>& messenger,
                 int index, const MessengerBuilder &bld)
  : messenger_(messenger),
    name_(StringPrintf("%s_R%03d", messenger->name().c_str(), index)),
    closing_(false),
    thread_(this, bld) {
}

Status Reactor::Init() {
  DVLOG(6) << "Called Reactor::Init()";
  return thread_.Init();
}

void Reactor::Shutdown() {
  {
    lock_guard<LockType> l(&lock_);
    if (closing_) {
      return;
    }
    closing_ = true;
  }

  thread_.Shutdown();

  // Abort all pending tasks. No new tasks can get scheduled after this
  // because ScheduleReactorTask() tests the closing_ flag set above.
  Status aborted = ShutdownError(true);
  while (!pending_tasks_.empty()) {
    ReactorTask& task = pending_tasks_.front();
    pending_tasks_.pop_front();
    task.Abort(aborted);
  }
}

Reactor::~Reactor() {
  Shutdown();
}

const std::string &Reactor::name() const {
  return name_;
}

bool Reactor::closing() const {
  lock_guard<LockType> l(&lock_);
  return closing_;
}

// Task to call an arbitrary function within the reactor thread.
class RunFunctionTask : public ReactorTask {
 public:
  explicit RunFunctionTask(boost::function<Status()> f)
      : function_(std::move(f)), latch_(1) {}

  virtual void Run(ReactorThread *reactor) OVERRIDE {
    status_ = function_();
    latch_.CountDown();
  }
  virtual void Abort(const Status &status) OVERRIDE {
    status_ = status;
    latch_.CountDown();
  }

  // Wait until the function has completed, and return the Status
  // returned by the function.
  Status Wait() {
    latch_.Wait();
    return status_;
  }

 private:
  boost::function<Status()> function_;
  Status status_;
  CountDownLatch latch_;
};

Status Reactor::GetMetrics(ReactorMetrics *metrics) {
  return RunOnReactorThread(boost::bind(&ReactorThread::GetMetrics, &thread_, metrics));
}

Status Reactor::RunOnReactorThread(const boost::function<Status()>& f) {
  RunFunctionTask task(f);
  ScheduleReactorTask(&task);
  return task.Wait();
}

Status Reactor::DumpRunningRpcs(const DumpRunningRpcsRequestPB& req,
                                DumpRunningRpcsResponsePB* resp) {
  return RunOnReactorThread(boost::bind(&ReactorThread::DumpRunningRpcs, &thread_,
                                        boost::ref(req), resp));
}

class RegisterConnectionTask : public ReactorTask {
 public:
  explicit RegisterConnectionTask(const scoped_refptr<Connection>& conn) :
    conn_(conn)
  {}

  virtual void Run(ReactorThread *thread) OVERRIDE {
    thread->RegisterConnection(conn_);
    delete this;
  }

  virtual void Abort(const Status &status) OVERRIDE {
    // We don't need to Shutdown the connection since it was never registered.
    // This is only used for inbound connections, and inbound connections will
    // never have any calls added to them until they've been registered.
    delete this;
  }

 private:
  scoped_refptr<Connection> conn_;
};

void Reactor::RegisterInboundSocket(Socket *socket, const Sockaddr &remote) {
  VLOG(3) << name_ << ": new inbound connection to " << remote.ToString();
  scoped_refptr<Connection> conn(
    new Connection(&thread_, remote, socket->Release(), Connection::SERVER));
  auto task = new RegisterConnectionTask(conn);
  ScheduleReactorTask(task);
}

// Task which runs in the reactor thread to assign an outbound call
// to a connection.
class AssignOutboundCallTask : public ReactorTask {
 public:
  explicit AssignOutboundCallTask(shared_ptr<OutboundCall> call)
      : call_(std::move(call)) {}

  virtual void Run(ReactorThread *reactor) OVERRIDE {
    reactor->AssignOutboundCall(call_);
    delete this;
  }

  virtual void Abort(const Status &status) OVERRIDE {
    call_->SetFailed(status);
    delete this;
  }

 private:
  shared_ptr<OutboundCall> call_;
};

void Reactor::QueueOutboundCall(const shared_ptr<OutboundCall> &call) {
  DVLOG(3) << name_ << ": queueing outbound call "
           << call->ToString() << " to remote " << call->conn_id().remote().ToString();
  AssignOutboundCallTask *task = new AssignOutboundCallTask(call);
  ScheduleReactorTask(task);
}

void Reactor::ScheduleReactorTask(ReactorTask *task) {
  {
    unique_lock<LockType> l(&lock_);
    if (closing_) {
      // We guarantee the reactor lock is not taken when calling Abort().
      l.unlock();
      task->Abort(ShutdownError(false));
      return;
    }
    pending_tasks_.push_back(*task);
  }
  thread_.WakeThread();
}

bool Reactor::DrainTaskQueue(boost::intrusive::list<ReactorTask> *tasks) { // NOLINT(*)
  lock_guard<LockType> l(&lock_);
  if (closing_) {
    return false;
  }
  tasks->swap(pending_tasks_);
  return true;
}

} // namespace rpc
} // namespace kudu
