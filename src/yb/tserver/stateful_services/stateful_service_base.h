// Copyright (c) YugaByte, Inc.
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

#include <atomic>
#include <string>
#include "yb/client/client_fwd.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/wire_protocol.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/result.h"

namespace yb {

namespace tserver {
class TSTabletManager;
}

namespace stateful_service {

// Macro to ensure stateful service rpc methods are only served when the service is ready.
// This requires the service to be in active mode and the serving tablet peer to be a leader and
// have the lease. resp->mutable_error() will be set if the service is not ready.
#define STATEFUL_SERVICE_IMPL_METHODS(methods) \
  BOOST_PP_SEQ_FOR_EACH(STATEFUL_SERVICE_IMPL_METHOD_HELPER, ~, methods)

#define STATEFUL_SERVICE_IMPL_METHOD_HELPER(i, data, method_name) \
  Status BOOST_PP_CAT(method_name, Impl)( \
      const BOOST_PP_CAT(method_name, RequestPB) & req, \
      BOOST_PP_CAT(method_name, ResponsePB) * resp); \
  void method_name( \
      const BOOST_PP_CAT(method_name, RequestPB) * req, \
      BOOST_PP_CAT(method_name, ResponsePB) * resp, rpc::RpcContext rpc) override { \
    HandleMessageWithTermCheck( \
        resp, &rpc, [req, resp, this]() { return BOOST_PP_CAT(method_name, Impl)(*req, resp); }); \
  }

class StatefulServiceBase {
 public:
  explicit StatefulServiceBase(
      const StatefulServiceKind service_kind,
      const std::shared_future<client::YBClient*>& client_future);

  virtual ~StatefulServiceBase();

  Status Init(tserver::TSTabletManager* ts_manager);

  void Shutdown() EXCLUDES(service_state_mutex_);

  void RaftConfigChangeCallback(tablet::TabletPeerPtr peer) EXCLUDES(service_state_mutex_);

  const std::string& ServiceName() const { return service_name_; }

  StatefulServiceKind ServiceKind() const { return service_kind_; }

  const client::YBTableName& TableName() const { return table_name_; }

  bool IsActive() const EXCLUDES(service_state_mutex_);

  void StartPeriodicTaskIfNeeded() EXCLUDES(service_state_mutex_);

 protected:
  // Hosting tablet peer has become a leader. RPC messages will only be processed after this
  // function completes.
  virtual void Activate(const int64_t leader_term) = 0;

  // Hosting tablet peer has stepped down to a follower. Release all resources acquired by the
  // stateful services and clear in-mem data.
  virtual void Deactivate() = 0;

  // Periodic task to be executed by the stateful service. Return true if the task should be rerun
  // after PeriodicTaskIntervalMs.
  virtual Result<bool> RunPeriodicTask() { return false; }

  // Interval in milliseconds between periodic tasks. 0 indicates that the task should not run
  // again.
  virtual uint32 PeriodicTaskIntervalMs() const;

  // Get the term when we last activated and make sure we still have a valid lease.
  int64_t GetLeaderTerm() EXCLUDES(service_state_mutex_);

  template <class ResponsePB>
  void HandleMessageWithTermCheck(
      ResponsePB* resp, rpc::RpcContext* rpc, std::function<Status()> method_impl) {
    // Will return a valid term only if we are active and have a valid lease.
    const auto term = GetLeaderTerm();
    Status status;
    if (term != OpId::kUnknownTerm) {
      status = method_impl();
    }

    // Term should still be valid after the method_impl() call.
    if (term == OpId::kUnknownTerm || term != GetLeaderTerm()) {
      status = STATUS(ServiceUnavailable, Format(ServiceName() + " is not active on this server"));
    }

    if (!status.ok()) {
      resp->Clear();
      StatusToPB(status, resp->mutable_error());
    }
    rpc->RespondSuccess();
  }

  std::shared_ptr<client::YBSession> GetYBSession() { return GetYBClient()->NewSession(); }
  Result<client::TableHandle*> GetServiceTable() EXCLUDES(table_handle_mutex_);

 private:
  void ProcessTaskPeriodically() EXCLUDES(service_state_mutex_);
  void ActivateOrDeactivateServiceIfNeeded() EXCLUDES(service_state_mutex_);
  int64_t WaitForLeaderLeaseAndGetTerm(tablet::TabletPeerPtr tablet_peer);
  void DoDeactivate();
  client::YBClient* GetYBClient() { return client_future_.get(); }

  const std::string service_name_;
  const StatefulServiceKind service_kind_;

  std::atomic_bool shutdown_ = false;
  mutable std::shared_mutex service_state_mutex_;
  int64_t leader_term_ GUARDED_BY(service_state_mutex_) = OpId::kUnknownTerm;
  tablet::TabletPeerPtr tablet_peer_ GUARDED_BY(service_state_mutex_);
  tablet::TabletPeerPtr new_tablet_peer_ GUARDED_BY(service_state_mutex_);
  bool raft_config_changed_ GUARDED_BY(service_state_mutex_) = false;

  std::mutex task_enqueue_mutex_;
  bool task_enqueued_ GUARDED_BY(task_enqueue_mutex_) = false;
  std::condition_variable_any task_wait_cond_;
  std::unique_ptr<ThreadPool> thread_pool_;
  std::unique_ptr<ThreadPoolToken> thread_pool_token_;

  FlagCallbackRegistration task_interval_ms_flag_callback_reg_;

  const client::YBTableName table_name_;
  const std::shared_future<client::YBClient*>& client_future_;
  std::mutex table_handle_mutex_;
  std::unique_ptr<client::TableHandle> table_handle_ GUARDED_BY(table_handle_mutex_);

  DISALLOW_COPY_AND_ASSIGN(StatefulServiceBase);
};

template <class RpcServiceIf>
class StatefulRpcServiceBase : public StatefulServiceBase, public RpcServiceIf {
 public:
  explicit StatefulRpcServiceBase(
      const StatefulServiceKind service_kind,
      const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future)
      : StatefulServiceBase(service_kind, client_future), RpcServiceIf(metric_entity) {}

  void Shutdown() override {
    RpcServiceIf::Shutdown();
    StatefulServiceBase::Shutdown();
  }
};

client::YBTableName GetStatefulServiceTableName(const StatefulServiceKind& service_kind);

}  // namespace stateful_service
}  // namespace yb
