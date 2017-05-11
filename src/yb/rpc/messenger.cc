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

#include "yb/rpc/messenger.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <list>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <boost/scope_exit.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/acceptor.h"
#include "yb/rpc/connection.h"
#include "yb/rpc/constants.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/rpc/rpc_service.h"
#include "yb/rpc/sasl_common.h"
#include "yb/util/errno.h"
#include "yb/util/flag_tags.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/socket.h"
#include "yb/util/status.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"

using std::string;
using std::shared_ptr;
using strings::Substitute;

DECLARE_int32(num_connections_to_server);
DEFINE_int32(rpc_default_keepalive_time_ms, 65000,
             "If an RPC connection from a client is idle for this amount of time, the server "
             "will disconnect the client.");
TAG_FLAG(rpc_default_keepalive_time_ms, advanced);

namespace yb {
namespace rpc {

class Messenger;
class ServerBuilder;

MessengerBuilder::MessengerBuilder(std::string name)
    : name_(std::move(name)),
      connection_keepalive_time_(
          MonoDelta::FromMilliseconds(FLAGS_rpc_default_keepalive_time_ms)),
      num_reactors_(4),
      num_negotiation_threads_(4),
      coarse_timer_granularity_(MonoDelta::FromMilliseconds(100)),
      connection_type_(ConnectionType::YB) {}

MessengerBuilder& MessengerBuilder::set_connection_keepalive_time(const MonoDelta &keepalive) {
  connection_keepalive_time_ = keepalive;
  return *this;
}

MessengerBuilder& MessengerBuilder::set_num_reactors(int num_reactors) {
  num_reactors_ = num_reactors;
  return *this;
}

MessengerBuilder& MessengerBuilder::set_negotiation_threads(int num_negotiation_threads) {
  num_negotiation_threads_ = num_negotiation_threads;
  return *this;
}

MessengerBuilder& MessengerBuilder::set_coarse_timer_granularity(const MonoDelta &granularity) {
  coarse_timer_granularity_ = granularity;
  return *this;
}

MessengerBuilder &MessengerBuilder::set_metric_entity(
    const scoped_refptr<MetricEntity>& metric_entity) {
  metric_entity_ = metric_entity;
  return *this;
}

MessengerBuilder &MessengerBuilder::use_connection_type(ConnectionType type) {
  connection_type_ = type;
  return *this;
}

Status MessengerBuilder::Build(Messenger **msgr) {
  RETURN_NOT_OK(SaslInit(kSaslAppName)); // Initialize SASL library before we start making requests
  gscoped_ptr<Messenger> new_msgr(new Messenger(*this));
  RETURN_NOT_OK(new_msgr.get()->Init());
  *msgr = new_msgr.release();
  return Status::OK();
}

Status MessengerBuilder::Build(shared_ptr<Messenger> *msgr) {
  Messenger *ptr;
  RETURN_NOT_OK(Build(&ptr));

  // See docs on Messenger::retain_self_ for info about this odd hack.
  *msgr = shared_ptr<Messenger>(
    ptr, std::mem_fun(&Messenger::AllExternalReferencesDropped));
  return Status::OK();
}

// See comment on Messenger::retain_self_ member.
void Messenger::AllExternalReferencesDropped() {
  Shutdown();
  CHECK(retain_self_.get());
  // If we have no more external references, then we no longer
  // need to retain ourself. We'll destruct as soon as all our
  // internal-facing references are dropped (ie those from reactor
  // threads).
  retain_self_.reset();
}

void Messenger::Shutdown() {
  // Since we're shutting down, it's OK to block.
  ThreadRestrictions::ScopedAllowWait allow_wait;

  decltype(reactors_) reactors;
  std::unique_ptr<Acceptor> acceptor;
  {
    std::lock_guard<percpu_rwlock> guard(lock_);
    if (closing_) {
      return;
    }
    VLOG(1) << "shutting down messenger " << name_;
    closing_ = true;

    DCHECK(rpc_services_.empty()) << "Unregister RPC services before shutting down Messenger";
    rpc_services_.clear();

    acceptor.swap(acceptor_);

    // Need to shut down negotiation pool before the reactors, since the
    // reactors close the Connection sockets, and may race against the negotiation
    // threads' blocking reads & writes.
    negotiation_pool_->Shutdown();

    reactors = reactors_;
  }

  if (acceptor) {
    acceptor->Shutdown();
  }

  for (auto* reactor : reactors) {
    reactor->Shutdown();
  }
  for (auto* reactor : reactors) {
    reactor->Join();
  }
}

Status Messenger::AcceptOnAddress(const Sockaddr &accept_addr, Sockaddr* bound_addr) {
  Socket sock;
  RETURN_NOT_OK(sock.Init(0));
  RETURN_NOT_OK(sock.SetReuseAddr(true));
  RETURN_NOT_OK(sock.Bind(accept_addr));
  if (bound_addr) {
    RETURN_NOT_OK(sock.GetSocketAddress(bound_addr));
  }
  Acceptor* acceptor;
  {
    std::lock_guard<percpu_rwlock> guard(lock_);
    if (!acceptor_) {
      acceptor_.reset(new Acceptor(this));
      RETURN_NOT_OK(acceptor_->Start());
    }
    acceptor = acceptor_.get();
  }
  return acceptor->Add(std::move(sock));
}

void Messenger::ShutdownAcceptor() {
  std::unique_ptr<Acceptor> acceptor;
  {
    std::lock_guard<percpu_rwlock> guard(lock_);
    acceptor.swap(acceptor_);
  }
  if (acceptor) {
    acceptor->Shutdown();
  }
}

// Register a new RpcService to handle inbound requests.
Status Messenger::RegisterService(const string& service_name,
                                  const scoped_refptr<RpcService>& service) {
  DCHECK(service);
  std::lock_guard<percpu_rwlock> guard(lock_);
  if (InsertIfNotPresent(&rpc_services_, service_name, service)) {
    UpdateServicesCache(&guard);
    return Status::OK();
  } else {
    return STATUS(AlreadyPresent, "This service is already present");
  }
}

Status Messenger::UnregisterAllServices() {
  decltype(rpc_services_) rpc_services_copy; // Drain rpc services here,
                                             // to avoid deleting them in locked state.
  {
    std::lock_guard<percpu_rwlock> guard(lock_);
    rpc_services_.swap(rpc_services_copy);
    UpdateServicesCache(&guard);
  }
  rpc_services_copy.clear();
  return Status::OK();
}

// Unregister an RpcService.
Status Messenger::UnregisterService(const string& service_name) {
  std::lock_guard<percpu_rwlock> guard(lock_);
  if (rpc_services_.erase(service_name)) {
    UpdateServicesCache(&guard);
    return Status::OK();
  } else {
    return STATUS(ServiceUnavailable, Substitute("service $0 not registered on $1",
                 service_name, name_));
  }
}

void Messenger::QueueOutboundCall(OutboundCallPtr call) {
  Reactor *reactor = RemoteToReactor(call->conn_id().remote(), call->conn_id().idx());
  reactor->QueueOutboundCall(std::move(call));
}

void Messenger::QueueInboundCall(InboundCallPtr call) {
  auto service = rpc_service(call->remote_method().service_name());
  if (PREDICT_FALSE(!service)) {
    Status s =  STATUS(ServiceUnavailable, Substitute("service $0 not registered on $1",
                                                      call->remote_method().service_name(), name_));
    LOG(INFO) << s.ToString();
    call->RespondFailure(ErrorStatusPB::ERROR_NO_SUCH_SERVICE, s);
    return;
  }

  // The RpcService will respond to the client on success or failure.
  service->QueueInboundCall(std::move(call));
}

void Messenger::RegisterInboundSocket(Socket *new_socket, const Sockaddr &remote) {
  int idx = num_connections_accepted_.fetch_add(1) % FLAGS_num_connections_to_server;
  Reactor *reactor = RemoteToReactor(remote, idx);
  reactor->RegisterInboundSocket(new_socket, remote);
}

Messenger::Messenger(const MessengerBuilder &bld)
  : name_(bld.name_),
    connection_type_(bld.connection_type_),
    metric_entity_(bld.metric_entity_),
    retain_self_(this) {
  for (int i = 0; i < bld.num_reactors_; i++) {
    reactors_.push_back(new Reactor(retain_self_, i, bld));
  }
  CHECK_OK(ThreadPoolBuilder("negotiator")
              .set_max_threads(bld.num_negotiation_threads_)
              .Build(&negotiation_pool_));
}

Messenger::~Messenger() {
  std::lock_guard<percpu_rwlock> guard(lock_);
  CHECK(closing_) << "Should have already shut down";
  STLDeleteElements(&reactors_);
  delete rpc_services_cache_.load();
}

size_t Messenger::max_concurrent_requests() const {
  return FLAGS_num_connections_to_server;
}

Reactor* Messenger::RemoteToReactor(const Sockaddr &remote, uint32_t idx) {
  uint32_t hashCode = remote.HashCode();
  int reactor_idx = (hashCode + idx) % reactors_.size();
  // This is just a static partitioning; where each connection
  // to a remote is assigned to a particular reactor. We could
  // get a lot fancier with assigning Sockaddrs to Reactors,
  // but this should be good enough.
  return reactors_[reactor_idx];
}

Status Messenger::Init() {
  Status status;
  for (Reactor* r : reactors_) {
    RETURN_NOT_OK(r->Init());
  }

  return Status::OK();
}

Status Messenger::DumpRunningRpcs(const DumpRunningRpcsRequestPB& req,
                                  DumpRunningRpcsResponsePB* resp) {
  shared_lock<rw_spinlock> guard(lock_.get_lock());
  for (Reactor* reactor : reactors_) {
    RETURN_NOT_OK(reactor->DumpRunningRpcs(req, resp));
  }
  return Status::OK();
}

Status Messenger::QueueEventOnAllReactors(scoped_refptr<ServerEvent> server_event) {
  shared_lock<rw_spinlock> guard(lock_.get_lock());
  for (Reactor* reactor : reactors_) {
    reactor->QueueEventOnAllConnections(server_event);
  }
  return Status::OK();
}

void Messenger::RemoveScheduledTask(int64_t id) {
  CHECK_NE(id, -1);
  std::lock_guard<std::mutex> guard(mutex_scheduled_tasks_);
  scheduled_tasks_.erase(id);
}

void Messenger::AbortOnReactor(int64_t task_id) {
  DCHECK(!reactors_.empty());
  CHECK_NE(task_id, -1);

  std::lock_guard<std::mutex> guard(mutex_scheduled_tasks_);
  auto iter = scheduled_tasks_.find(task_id);
  if (iter != scheduled_tasks_.end()) {
    const auto& task = iter->second;
    task->AbortTask(STATUS(Aborted, "Task aborted by messenger"));
    scheduled_tasks_.erase(iter);
  }
}

int64_t Messenger::ScheduleOnReactor(const std::function<void(const Status&)>& func,
                                     MonoDelta when,
                                     const shared_ptr<Messenger>& msgr) {
  DCHECK(!reactors_.empty());

  // If we're already running on a reactor thread, reuse it.
  Reactor* chosen = nullptr;
  for (Reactor* r : reactors_) {
    if (r->IsCurrentThread()) {
      chosen = r;
    }
  }
  if (chosen == nullptr) {
    // Not running on a reactor thread, pick one at random.
    chosen = reactors_[rand() % reactors_.size()];
  }

  int64_t task_id = 0;
  if (msgr != nullptr) {
    task_id = next_task_id_.fetch_add(1);
  }
  auto task = std::make_shared<DelayedTask>(func, when, task_id, msgr);
  if (msgr != nullptr) {
    std::lock_guard<std::mutex> guard(mutex_scheduled_tasks_);
    scheduled_tasks_[task_id] = task;
  }
  chosen->ScheduleReactorTask(task);
  return task_id;
}

void Messenger::UpdateServicesCache(std::lock_guard<percpu_rwlock>* guard) {
  assert(guard != nullptr);

  auto* new_cache = !rpc_services_.empty() ? new RpcServicesMap(rpc_services_) : nullptr;
  auto* old = rpc_services_cache_.exchange(new_cache);

  if (old) {
    // Service cache update is quite rare situation otherwise rpc_service is quite frequent.
    // Because of this we use "atomic lock" in rpc_service and busy wait in UpdateServicesCache.
    // So we could process rpc_service quickly, and stuck in UpdateServicesCache for sometime.
    while (rpc_services_lock_count_ != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    delete old;
  }
}

const scoped_refptr<RpcService> Messenger::rpc_service(const string& service_name) const {
  BOOST_SCOPE_EXIT(&rpc_services_lock_count_) {
    --rpc_services_lock_count_;
  } BOOST_SCOPE_EXIT_END;
  ++rpc_services_lock_count_;
  scoped_refptr<RpcService> result;
  auto* cache = atomic_load(&rpc_services_cache_);
  if (cache != nullptr) {
    auto it = cache->find(service_name);
    if (it != cache->end()) {
      // Since our cache is a cache of whole rpc_services_ map, we could check only it.
      result = it->second;
    }
  }
  return result;
}

} // namespace rpc
} // namespace yb
