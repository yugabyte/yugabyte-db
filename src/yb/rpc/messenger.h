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
#ifndef YB_RPC_MESSENGER_H_
#define YB_RPC_MESSENGER_H_

#include <stdint.h>

#include <atomic>
#include <memory>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest_prod.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/connection.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/response_callback.h"
#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

namespace yb {

class Socket;
class ThreadPool;

namespace rpc {

class AcceptorPool;
class DumpRunningRpcsRequestPB;
class DumpRunningRpcsResponsePB;
class InboundCall;
class Messenger;
class OutboundCall;
class Reactor;
class ReactorThread;
class RpcService;

struct AcceptorPoolInfo {
 public:
  explicit AcceptorPoolInfo(Sockaddr bind_address)
      : bind_address_(std::move(bind_address)) {}

  Sockaddr bind_address() const {
    return bind_address_;
  }

 private:
  Sockaddr bind_address_;
};

// Used to construct a Messenger.
class MessengerBuilder {
 public:
  friend class Messenger;
  friend class ReactorThread;

  explicit MessengerBuilder(std::string name);

  // Set the length of time we will keep a TCP connection will alive with no traffic.
  MessengerBuilder &set_connection_keepalive_time(const MonoDelta &keepalive);

  // Set the number of reactor threads that will be used for sending and
  // receiving.
  MessengerBuilder &set_num_reactors(int num_reactors);

  // Set the number of connection-negotiation threads that will be used to handle the
  // blocking connection-negotiation step.
  MessengerBuilder &set_negotiation_threads(int num_negotiation_threads);

  // Set the granularity with which connections are checked for keepalive.
  MessengerBuilder &set_coarse_timer_granularity(const MonoDelta &granularity);

  // Set metric entity for use by RPC systems.
  MessengerBuilder &set_metric_entity(const scoped_refptr<MetricEntity>& metric_entity);

  // Uses the given connection type to handle the incoming connections.
  MessengerBuilder &use_connection_type(ConnectionType type);

  CHECKED_STATUS Build(std::shared_ptr<Messenger> *msgr);

 private:
  CHECKED_STATUS Build(Messenger **msgr);
  const std::string name_;
  MonoDelta connection_keepalive_time_;
  int num_reactors_;
  int num_negotiation_threads_;
  MonoDelta coarse_timer_granularity_;
  scoped_refptr<MetricEntity> metric_entity_;
  ConnectionType connection_type_;
};

// A Messenger is a container for the reactor threads which run event loops
// for the RPC services. If the process is a server, a Messenger can also have
// one or more attached AcceptorPools which accept RPC connections. In this case,
// calls received over the connection are enqueued into the messenger's service_queue
// for processing by a ServicePool.
//
// Users do not typically interact with the Messenger directly except to create
// one as a singleton, and then make calls using Proxy objects.
//
// See rpc-test.cc and rpc-bench.cc for example usages.
class Messenger {
 public:
  friend class MessengerBuilder;
  friend class Proxy;
  friend class Reactor;
  typedef std::vector<std::shared_ptr<AcceptorPool> > acceptor_vec_t;
  typedef std::unordered_map<std::string, scoped_refptr<RpcService> > RpcServicesMap;

  static const uint64_t UNKNOWN_CALL_ID = 0;

  ~Messenger();

  // Stop all communication and prevent further use.
  // It's not required to call this -- dropping the shared_ptr provided
  // from MessengerBuilder::Build will automatically call this method.
  void Shutdown();

  // Add a new acceptor pool listening to the given accept address.
  // You can create any number of acceptor pools you want, including none.
  //
  // The created pool is returned in *pool. The Messenger also retains
  // a reference to the pool, so the caller may safely drop this reference
  // and the pool will remain live.
  //
  // NOTE: the returned pool is not initially started. You must call
  // pool->Start(...) to begin accepting connections.
  CHECKED_STATUS AddAcceptorPool(const Sockaddr &accept_addr,
                         std::shared_ptr<AcceptorPool>* pool);

  // Register a new RpcService to handle inbound requests.
  CHECKED_STATUS RegisterService(const std::string& service_name,
                         const scoped_refptr<RpcService>& service);

  // Unregister currently-registered RpcService.
  CHECKED_STATUS UnregisterService(const std::string& service_name);

  CHECKED_STATUS UnregisterAllServices();

  // Queue a call for transmission. This will pick the appropriate reactor,
  // and enqueue a task on that reactor to assign and send the call.
  void QueueOutboundCall(const OutboundCallPtr& call);

  // Enqueue a call for processing on the server.
  void QueueInboundCall(InboundCallPtr call);

  // Take ownership of the socket via Socket::Release
  void RegisterInboundSocket(Socket *new_socket, const Sockaddr &remote);

  // Dump the current RPCs into the given protobuf.
  CHECKED_STATUS DumpRunningRpcs(const DumpRunningRpcsRequestPB& req,
                         DumpRunningRpcsResponsePB* resp);

  void RemoveScheduledTask(int64_t task_id);

  // This method will run 'func' with an ABORT status argument. It's not guaranteed that the task
  // will cancel because TimerHandler could run before this method.
  void AbortOnReactor(int64_t task_id);

  // Run 'func' on a reactor thread after 'when' time elapses.
  //
  // The status argument conveys whether 'func' was run correctly (i.e.
  // after the elapsed time) or not.
  int64_t ScheduleOnReactor(const std::function<void(const Status&)>& func,
                            MonoDelta when,
                            const std::shared_ptr<Messenger>& msgr = nullptr);

  ThreadPool* negotiation_pool() const { return negotiation_pool_.get(); }

  std::string name() const {
    return name_;
  }

  bool closing() const {
    shared_lock<rw_spinlock> guard(lock_.get_lock());
    return closing_;
  }

  scoped_refptr<MetricEntity> metric_entity() const { return metric_entity_; }

  const scoped_refptr<RpcService> rpc_service(const std::string& service_name) const;

  scoped_refptr<Histogram> outgoing_queue_time() { return outgoing_queue_time_; }

  size_t number_of_reactors() const { return reactors_.size(); }
  size_t max_concurrent_requests() const;
 private:
  FRIEND_TEST(TestRpc, TestConnectionKeepalive);
  friend class DelayedTask;

  explicit Messenger(const MessengerBuilder &bld);

  Reactor* RemoteToReactor(const Sockaddr &remote, uint32_t idx = 0);
  CHECKED_STATUS Init();
  void RunTimeoutThread();
  void UpdateCurTime();
  void UpdateServicesCache(std::lock_guard<percpu_rwlock>* guard);

  // Called by external-facing shared_ptr when the user no longer holds
  // any references. See 'retain_self_' for more info.
  void AllExternalReferencesDropped();

  const std::string name_;

  const ConnectionType connection_type_;

  // Protects closing_, acceptor_pools_, rpc_services_.
  mutable percpu_rwlock lock_;

  bool closing_;

  // Pools which are listening on behalf of this messenger.
  // Note that the user may have called Shutdown() on one of these
  // pools, so even though we retain the reference, it may no longer
  // be listening.
  acceptor_vec_t acceptor_pools_;

  // RPC services that handle inbound requests.
  RpcServicesMap rpc_services_;
  std::atomic<RpcServicesMap*> rpc_services_cache_ = {nullptr};
  mutable std::atomic<int> rpc_services_lock_count_ = {0};

  std::vector<Reactor*> reactors_;

  gscoped_ptr<ThreadPool> negotiation_pool_;

  const scoped_refptr<MetricEntity> metric_entity_;
  const scoped_refptr<Histogram> outgoing_queue_time_;

  // The ownership of the Messenger object is somewhat subtle. The pointer graph
  // looks like this:
  //
  //    [User Code ]             |      [ Internal code ]
  //                             |
  //     shared_ptr[1]           |
  //         |                   |
  //         v
  //      Messenger    <------------ shared_ptr[2] --- Reactor
  //       ^    |       ----------- bare pointer --> Reactor
  //        \__/
  //     shared_ptr[2]
  //     (retain_self_)
  //
  // shared_ptr[1] instances use Messenger::AllExternalReferencesDropped()
  //   as a deleter.
  // shared_ptr[2] are "traditional" shared_ptrs which call 'delete' on the
  //   object.
  //
  // The teardown sequence is as follows:
  // Option 1): User calls "Shutdown()" explicitly:
  //  - Messenger::Shutdown tells Reactors to shut down
  //  - When each reactor thread finishes, it drops its shared_ptr[2]
  //  - the Messenger::retain_self instance remains, keeping the Messenger
  //    alive.
  //  - The user eventually drops its shared_ptr[1], which calls
  //    Messenger::AllExternalReferencesDropped. This drops retain_self_
  //    and results in object destruction.
  // Option 2): User drops all of its shared_ptr[1] references
  //  - Though the Reactors still reference the Messenger, AllExternalReferencesDropped
  //    will get called, which triggers Messenger::Shutdown.
  //  - AllExternalReferencesDropped drops retain_self_, so the only remaining
  //    references are from Reactor threads. But the reactor threads are shutting down.
  //  - When the last Reactor thread dies, there will be no more shared_ptr[1] references
  //    and the Messenger will be destroyed.
  //
  // The main goal of all of this confusion is that the reactor threads need to be able
  // to shut down asynchronously, and we need to keep the Messenger alive until they
  // do so. So, handing out a normal shared_ptr to users would force the Messenger
  // destructor to Join() the reactor threads, which causes a problem if the user
  // tries to destruct the Messenger from within a Reactor thread itself.
  std::shared_ptr<Messenger> retain_self_;

  // Id that will be assigned to the next task that is scheduled on the reactor.
  std::atomic<uint64_t> next_task_id_;
  std::atomic<uint64_t> num_connections_accepted_;

  std::mutex mutex_scheduled_tasks_;

  std::unordered_map<int64_t, std::shared_ptr<DelayedTask>> scheduled_tasks_;


  DISALLOW_COPY_AND_ASSIGN(Messenger);
};

}  // namespace rpc
}  // namespace yb

#endif  // YB_RPC_MESSENGER_H_
