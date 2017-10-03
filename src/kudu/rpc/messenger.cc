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

#include "kudu/rpc/messenger.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <list>
#include <set>
#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/acceptor_pool.h"
#include "kudu/rpc/connection.h"
#include "kudu/rpc/constants.h"
#include "kudu/rpc/reactor.h"
#include "kudu/rpc/rpc_header.pb.h"
#include "kudu/rpc/rpc_service.h"
#include "kudu/rpc/sasl_common.h"
#include "kudu/rpc/transfer.h"
#include "kudu/util/errno.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"
#include "kudu/util/monotime.h"
#include "kudu/util/net/socket.h"
#include "kudu/util/status.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/trace.h"

using std::string;
using std::shared_ptr;
using strings::Substitute;

DEFINE_int32(rpc_default_keepalive_time_ms, 65000,
             "If an RPC connection from a client is idle for this amount of time, the server "
             "will disconnect the client.");
TAG_FLAG(rpc_default_keepalive_time_ms, advanced);

namespace kudu {
namespace rpc {

class Messenger;
class ServerBuilder;

MessengerBuilder::MessengerBuilder(std::string name)
    : name_(std::move(name)),
      connection_keepalive_time_(
          MonoDelta::FromMilliseconds(FLAGS_rpc_default_keepalive_time_ms)),
      num_reactors_(4),
      num_negotiation_threads_(4),
      coarse_timer_granularity_(MonoDelta::FromMilliseconds(100)) {}

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

  lock_guard<percpu_rwlock> guard(&lock_);
  if (closing_) {
    return;
  }
  VLOG(1) << "shutting down messenger " << name_;
  closing_ = true;

  DCHECK(rpc_services_.empty()) << "Unregister RPC services before shutting down Messenger";
  rpc_services_.clear();

  for (const shared_ptr<AcceptorPool>& acceptor_pool : acceptor_pools_) {
    acceptor_pool->Shutdown();
  }
  acceptor_pools_.clear();

  // Need to shut down negotiation pool before the reactors, since the
  // reactors close the Connection sockets, and may race against the negotiation
  // threads' blocking reads & writes.
  negotiation_pool_->Shutdown();

  for (Reactor* reactor : reactors_) {
    reactor->Shutdown();
  }
}

Status Messenger::AddAcceptorPool(const Sockaddr &accept_addr,
                                  shared_ptr<AcceptorPool>* pool) {
  Socket sock;
  RETURN_NOT_OK(sock.Init(0));
  RETURN_NOT_OK(sock.SetReuseAddr(true));
  RETURN_NOT_OK(sock.Bind(accept_addr));
  Sockaddr remote;
  RETURN_NOT_OK(sock.GetSocketAddress(&remote));
  shared_ptr<AcceptorPool> acceptor_pool(new AcceptorPool(this, &sock, remote));

  lock_guard<percpu_rwlock> guard(&lock_);
  acceptor_pools_.push_back(acceptor_pool);
  *pool = acceptor_pool;
  return Status::OK();
}

// Register a new RpcService to handle inbound requests.
Status Messenger::RegisterService(const string& service_name,
                                  const scoped_refptr<RpcService>& service) {
  DCHECK(service);
  lock_guard<percpu_rwlock> guard(&lock_);
  if (InsertIfNotPresent(&rpc_services_, service_name, service)) {
    return Status::OK();
  } else {
    return Status::AlreadyPresent("This service is already present");
  }
}

Status Messenger::UnregisterAllServices() {
  lock_guard<percpu_rwlock> guard(&lock_);
  rpc_services_.clear();
  return Status::OK();
}

// Unregister an RpcService.
Status Messenger::UnregisterService(const string& service_name) {
  lock_guard<percpu_rwlock> guard(&lock_);
  if (rpc_services_.erase(service_name)) {
    return Status::OK();
  } else {
    return Status::ServiceUnavailable(Substitute("service $0 not registered on $1",
                 service_name, name_));
  }
}

void Messenger::QueueOutboundCall(const shared_ptr<OutboundCall> &call) {
  Reactor *reactor = RemoteToReactor(call->conn_id().remote());
  reactor->QueueOutboundCall(call);
}

void Messenger::QueueInboundCall(gscoped_ptr<InboundCall> call) {
  shared_lock<rw_spinlock> guard(&lock_.get_lock());
  scoped_refptr<RpcService>* service = FindOrNull(rpc_services_,
                                                  call->remote_method().service_name());
  if (PREDICT_FALSE(!service)) {
    Status s =  Status::ServiceUnavailable(Substitute("service $0 not registered on $1",
                                                      call->remote_method().service_name(), name_));
    LOG(INFO) << s.ToString();
    call.release()->RespondFailure(ErrorStatusPB::ERROR_NO_SUCH_SERVICE, s);
    return;
  }

  // The RpcService will respond to the client on success or failure.
  WARN_NOT_OK((*service)->QueueInboundCall(call.Pass()), "Unable to handle RPC call");
}

void Messenger::RegisterInboundSocket(Socket *new_socket, const Sockaddr &remote) {
  Reactor *reactor = RemoteToReactor(remote);
  reactor->RegisterInboundSocket(new_socket, remote);
}

Messenger::Messenger(const MessengerBuilder &bld)
  : name_(bld.name_),
    closing_(false),
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
  lock_guard<percpu_rwlock> guard(&lock_);
  CHECK(closing_) << "Should have already shut down";
  STLDeleteElements(&reactors_);
}

Reactor* Messenger::RemoteToReactor(const Sockaddr &remote) {
  uint32_t hashCode = remote.HashCode();
  int reactor_idx = hashCode % reactors_.size();
  // This is just a static partitioning; we could get a lot
  // fancier with assigning Sockaddrs to Reactors.
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
  shared_lock<rw_spinlock> guard(&lock_.get_lock());
  for (Reactor* reactor : reactors_) {
    RETURN_NOT_OK(reactor->DumpRunningRpcs(req, resp));
  }
  return Status::OK();
}

void Messenger::ScheduleOnReactor(const boost::function<void(const Status&)>& func,
                                  MonoDelta when) {
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

  DelayedTask* task = new DelayedTask(func, when);
  chosen->ScheduleReactorTask(task);
}

const scoped_refptr<RpcService> Messenger::rpc_service(const string& service_name) const {
  lock_guard<percpu_rwlock> guard(&lock_);
  scoped_refptr<RpcService> service;
  if (FindCopy(rpc_services_, service_name, &service)) {
    return service;
  } else {
    return scoped_refptr<RpcService>(nullptr);
  }
}

} // namespace rpc
} // namespace kudu
