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

#include <boost/optional/optional.hpp>

#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"
#include "yb/common/snapshot.h"
#include "yb/common/transaction.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_test.proxy.h"
#include "yb/master/master_test.pb.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/tablet_health_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/monitored_task.h"

#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/async_task_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_fwd.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics_fwd.h"

namespace yb {

struct TransactionMetadata;
class ThreadPool;

namespace consensus {
class ConsensusServiceProxy;
}

namespace tserver {
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;
}

namespace master {

class TSDescriptor;
class Master;

class TableInfo;
class TabletInfo;

YB_STRONGLY_TYPED_BOOL(AddPendingDelete);
YB_STRONGLY_TYPED_BOOL(CDCSDKSetRetentionBarriers);

// Interface used by RetryingTSRpcTask to pick the tablet server to
// send the next RPC to.
class TSPicker {
 public:
  TSPicker() {}
  virtual ~TSPicker() {}

  // Sets *ts_desc to the tablet server to contact for the next RPC.
  //
  // This assumes that TSDescriptors are never deleted by the master,
  // so the caller does not take ownership of the returned pointer.
  virtual Status PickReplica(TSDescriptor** ts_desc) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TSPicker);
};

// Implementation of TSPicker which sends to a specific tablet server,
// identified by its UUID.
class PickSpecificUUID : public TSPicker {
 public:
  PickSpecificUUID(Master* master, std::string ts_uuid)
      : master_(master), ts_uuid_(std::move(ts_uuid)) {}

  Status PickReplica(TSDescriptor** ts_desc) override;

 private:
  Master* const master_;
  const std::string ts_uuid_;

  DISALLOW_COPY_AND_ASSIGN(PickSpecificUUID);
};

// Implementation of TSPicker which locates the current leader replica,
// and sends the RPC to that server.
class PickLeaderReplica : public TSPicker {
 public:
  explicit PickLeaderReplica(const scoped_refptr<TabletInfo>& tablet);

  Status PickReplica(TSDescriptor** ts_desc) override;

 private:
  const scoped_refptr<TabletInfo> tablet_;
};

// A background task which continuously retries sending an RPC to a master or tserver.
class RetryingRpcTask : public server::RunnableMonitoredTask {
 public:
  RetryingRpcTask(Master *master,
                  ThreadPool* callback_pool,
                  AsyncTaskThrottlerBase* async_task_throttler);

  ~RetryingRpcTask();

  // Send the subclass RPC request.
  Status Run() override;

  // Abort this task and return its value before it was successfully aborted. If the task entered
  // a different terminal state before we were able to abort it, return that state.
  server::MonitoredTaskState AbortAndReturnPrevState(const Status& status) override;

 protected:
  // Send an RPC request and register a callback.
  // The implementation must return true if the callback was registered, and
  // false if an error occurred and no callback will occur.
  virtual bool SendRequest(int attempt) = 0;

  // Handle the response from the RPC request. On success, MarkSuccess() must
  // be called to mutate the state_ variable. If retry is desired, then
  // no state change is made. Retries will automatically be attempted as long
  // as the state is MonitoredTaskState::kRunning and deadline_ has not yet passed.
  virtual void HandleResponse(int attempt) = 0;

  virtual Status ResetProxies() = 0;

  // Overridable log prefix with reasonable default.
  std::string LogPrefix() const;

  bool PerformStateTransition(
      server::MonitoredTaskState expected, server::MonitoredTaskState new_state)
      WARN_UNUSED_RESULT {
    return state_.compare_exchange_strong(expected, new_state);
  }

  void TransitionToTerminalState(
      server::MonitoredTaskState expected, server::MonitoredTaskState terminal_state,
      const Status& status);
  bool TransitionToWaitingState(server::MonitoredTaskState expected);

  // Transition this task state from running to complete.
  void TransitionToCompleteState();

  // Transition this task state from expected to failed with specified status.
  void TransitionToFailedState(server::MonitoredTaskState expected, const Status& status);

  // Some tasks needs to transition to a certain state in case of replica lookup failure
  virtual std::optional<std::pair<server::MonitoredTaskState, Status>> HandleReplicaLookupFailure(
      const Status& replica_lookup_status) {
    return std::nullopt;
  }

  virtual Status PickReplica() { return Status::OK(); }

  virtual void Finished(const Status& status) {}

  void AbortTask(const Status& status);

  virtual MonoTime ComputeDeadline();
  // Callback meant to be invoked from asynchronous RPC service proxy calls.
  void RpcCallback();

  auto BindRpcCallback() {
    return std::bind(&RetryingRpcTask::RpcCallback, shared_from(this));
  }

  // Handle the actual work of the RPC callback. This is run on the master's worker
  // pool, rather than a reactor thread, so it may do blocking IO operations.
  virtual void DoRpcCallback() = 0;

  // Called when the async task unregisters either successfully or unsuccessfully.
  // Note: This is the last thing function called, to guarantee it's the last work done by the task.
  virtual void UnregisterAsyncTaskCallback();

  // Do not call this unless you know what you are doing. This function may delete the last
  // reference to the task.
  virtual void UnregisterAsyncTaskCallbackInternal() {}

  Master* const master_;
  ThreadPool* const callback_pool_;
  AsyncTaskThrottlerBase* async_task_throttler_;

  void UpdateMetrics(scoped_refptr<Histogram> metric, MonoTime start_time,
                     const std::string& metric_name,
                     const std::string& metric_type);

  MonoTime attempt_start_ts_;
  MonoTime deadline_;

  int attempt_ = 0;
  rpc::RpcController rpc_;

  std::atomic<rpc::ScheduledTaskId> reactor_task_id_{rpc::kInvalidTaskId};

  // Mutex protecting calls to UnregisterAsyncTask to avoid races between Run and user triggered
  // Aborts.
  std::mutex unregister_mutex_;

  // Reschedules the current task after a backoff delay.
  // Returns false if the task was not rescheduled due to reaching the maximum
  // timeout or because the task is no longer in a running state.
  // Returns true if rescheduling the task was successful.
  bool RescheduleWithBackoffDelay();

  // Clean up request and release resources. May call 'delete this'.
  void UnregisterAsyncTask();

 private:
  // Returns true if we should impose a limit in the number of retries for this task type.
  bool RetryLimitTaskType() {
    return type() != server::MonitoredTaskType::kCreateReplica &&
           type() != server::MonitoredTaskType::kDeleteReplica;
  }

  // Returns true if we should not retry for this task type.
  bool NoRetryTaskType() {
    return type() == server::MonitoredTaskType::kFlushTablets;
  }

  // Callback for Reactor delayed task mechanism. Called either when it is time
  // to execute the delayed task (with status == OK) or when the task
  // is cancelled, i.e. when the scheduling timer is shut down (status != OK).
  void RunDelayedTask(const Status& status);

  Status Failed(const Status& status);

  // Only abort this task on reactor if it has been scheduled.
  void AbortIfScheduled();

  virtual int num_max_retries();
  virtual int max_delay_ms();
};

// A background task which continuously retries sending an RPC to master server.
class RetryingMasterRpcTask : public RetryingRpcTask {
 public:
  RetryingMasterRpcTask(Master* master,
                        ThreadPool* callback_pool,
                        consensus::RaftPeerPB&& peer,
                        AsyncTaskThrottlerBase* async_task_throttler = nullptr);

  ~RetryingMasterRpcTask() {}

 protected:
  virtual Status ResetProxies() override;

  // Handle the actual work of the RPC callback. This is run on the master's worker
  // pool, rather than a reactor thread, so it may do blocking IO operations.
  void DoRpcCallback() override;

  consensus::RaftPeerPB peer_;
  std::shared_ptr<master::MasterTestProxy> master_test_proxy_;
  std::shared_ptr<master::MasterClusterProxy> master_cluster_proxy_;
};

// A background task which continuously retries sending an RPC to a tablet server.
// The target tablet server is refreshed before each RPC by consulting the provided
// TSPicker implementation.
class RetryingTSRpcTask : public RetryingRpcTask {
 public:
  RetryingTSRpcTask(Master *master,
                    ThreadPool* callback_pool,
                    std::unique_ptr<TSPicker> replica_picker,
                    AsyncTaskThrottlerBase* async_task_throttler);

  ~RetryingTSRpcTask() {}

 protected:
  // Return the id of the tablet that is the subject of the async request.
  virtual TabletId tablet_id() const = 0;

  virtual Status ResetProxies() override;

  // Handle the actual work of the RPC callback. This is run on the master's worker
  // pool, rather than a reactor thread, so it may do blocking IO operations.
  void DoRpcCallback() override;

  virtual Status PickReplica() override;

  const std::unique_ptr<TSPicker> replica_picker_;
  TSDescriptor* target_ts_desc_ = nullptr;

  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
  std::shared_ptr<tserver::TabletServerAdminServiceProxy> ts_admin_proxy_;
  std::shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy_;
  std::shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy_;
};

// RetryingTSRpcTask subclass which always retries the same tablet server,
// identified by its UUID.
class RetrySpecificTSRpcTask : public RetryingTSRpcTask {
 public:
  RetrySpecificTSRpcTask(Master* master,
                         ThreadPool* callback_pool,
                         const std::string& permanent_uuid,
                         AsyncTaskThrottlerBase* async_task_throttler)
    : RetryingTSRpcTask(master,
                        callback_pool,
                        std::unique_ptr<TSPicker>(new PickSpecificUUID(master, permanent_uuid)),
                        async_task_throttler),
      permanent_uuid_(permanent_uuid) {
  }

  ~RetrySpecificTSRpcTask() {}

 protected:
  const std::string permanent_uuid_;
};

class RetryingTSRpcTaskWithTable : public RetryingTSRpcTask {
 public:
  RetryingTSRpcTaskWithTable(
      Master *master,
      ThreadPool* callback_pool,
      std::unique_ptr<TSPicker> replica_picker,
      scoped_refptr<TableInfo> table,
      LeaderEpoch epoch,
      AsyncTaskThrottlerBase* async_task_throttler);

  ~RetryingTSRpcTaskWithTable();

  const scoped_refptr<TableInfo>& table() const { return table_ ; }
  std::string table_name() const;
  virtual void UnregisterAsyncTaskCallbackInternal() override;

 protected:
  const LeaderEpoch& epoch() const { return epoch_; }

  // May be null (e.g. when SendDeleteTabletRequest is called on an orphaned tablet).
  const scoped_refptr<TableInfo> table_;
  LeaderEpoch epoch_;
};

// RetryingTSRpcTaskWithTable subclass which always retries the same tablet server,
// identified by its UUID.
class RetrySpecificTSRpcTaskWithTable : public RetryingTSRpcTaskWithTable {
 public:
  RetrySpecificTSRpcTaskWithTable(
    Master* master,
    ThreadPool* callback_pool,
    const std::string& permanent_uuid,
    scoped_refptr<TableInfo> table,
    LeaderEpoch epoch,
    AsyncTaskThrottlerBase* async_task_throttler)
    : RetryingTSRpcTaskWithTable(master,
        callback_pool, std::unique_ptr<TSPicker>(new PickSpecificUUID(master, permanent_uuid)),
        table, std::move(epoch), async_task_throttler),
      permanent_uuid_(permanent_uuid) {
  }

  ~RetrySpecificTSRpcTaskWithTable() {}

 protected:
  const std::string permanent_uuid_;
};

// RetryingTSRpcTask subclass which retries sending an RPC to a tablet leader.
class AsyncTabletLeaderTask : public RetryingTSRpcTaskWithTable {
 public:
  AsyncTabletLeaderTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      LeaderEpoch epoch);

  AsyncTabletLeaderTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const scoped_refptr<TableInfo>& table, LeaderEpoch epoch);

  ~AsyncTabletLeaderTask();

  std::string description() const override;

  TabletId tablet_id() const override;

 protected:
  TabletServerId permanent_uuid() const;

  scoped_refptr<TabletInfo> tablet_;
};

// Fire off the async create tablet.
// This requires that the new tablet info is locked for write, and the
// consensus configuration information has been filled into the 'dirty' data.
class AsyncCreateReplica : public RetrySpecificTSRpcTaskWithTable {
 public:
  AsyncCreateReplica(Master *master,
                     ThreadPool *callback_pool,
                     const std::string& permanent_uuid,
                     const scoped_refptr<TabletInfo>& tablet,
                     const std::vector<SnapshotScheduleId>& snapshot_schedules,
                     LeaderEpoch epoch,
                     CDCSDKSetRetentionBarriers cdc_sdk_set_retention_barriers);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kCreateReplica;
  }

  std::string type_name() const override { return "Create Tablet"; }

  std::string description() const override;

 protected:
  TabletId tablet_id() const override { return tablet_id_; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  const TabletId tablet_id_;
  tserver::CreateTabletRequestPB req_;
  tserver::CreateTabletResponsePB resp_;
  const CDCSDKSetRetentionBarriers cdc_sdk_set_retention_barriers_ =
      CDCSDKSetRetentionBarriers::kFalse;
};

class AsyncMasterTabletHealthTask : public RetryingMasterRpcTask {
 public:
  AsyncMasterTabletHealthTask(
      Master* master,
      ThreadPool* callback_pool,
      consensus::RaftPeerPB&& peer,
      std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kFollowerLag;
  }

  std::string type_name() const override { return "Check Master Follower Lag"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  master::CheckMasterTabletHealthRequestPB req_;
  master::CheckMasterTabletHealthResponsePB resp_;

  std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler_;
};

class AsyncTserverTabletHealthTask : public RetrySpecificTSRpcTask {
 public:
  AsyncTserverTabletHealthTask(
    Master* master,
    ThreadPool* callback_pool,
    std::string permanent_uuid,
    std::vector<TabletId>&& tablets,
    std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kFollowerLag;
  }

  std::string type_name() const override { return "Check Tserver Follower Lag"; }

  std::string description() const override;

 protected:
  // Not associated with a tablet.
  TabletId tablet_id() const override { return TabletId(); }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  tserver::CheckTserverTabletHealthRequestPB req_;
  tserver::CheckTserverTabletHealthResponsePB resp_;

  std::shared_ptr<AreNodesSafeToTakeDownCallbackHandler> cb_handler_;
};

// Task to start election at hinted leader for a newly created tablet.
class AsyncStartElection : public RetrySpecificTSRpcTaskWithTable {
 public:
  AsyncStartElection(Master *master,
                     ThreadPool *callback_pool,
                     const std::string& permanent_uuid,
                     const scoped_refptr<TabletInfo>& tablet,
                     bool initial_election,
                     LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kStartElection;
  }

  std::string type_name() const override { return "Hinted Leader Start Election"; }

  std::string description() const override;

 protected:
  TabletId tablet_id() const override { return tablet_id_; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  const TabletId tablet_id_;
  consensus::RunLeaderElectionRequestPB req_;
  consensus::RunLeaderElectionResponsePB resp_;
};

// Send a PrepareDeleteTransactionTablet() RPC request.
class AsyncPrepareDeleteTransactionTablet : public RetrySpecificTSRpcTaskWithTable {
 public:
  AsyncPrepareDeleteTransactionTablet(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const scoped_refptr<TableInfo>& table, const scoped_refptr<TabletInfo>& tablet,
      const std::string& msg, HideOnly hide_only, LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kPrepareDeleteTransactionTablet;
  }

  std::string type_name() const override { return "Prepare Delete Transaction Tablet"; }

  std::string description() const override;

 protected:
  TabletId tablet_id() const override;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void UnregisterAsyncTaskCallback() override;

  const scoped_refptr<TabletInfo> tablet_;
  const std::string msg_;
  HideOnly hide_only_;
  tserver::PrepareDeleteTransactionTabletResponsePB resp_;
};

// Send a DeleteTablet() RPC request.
class AsyncDeleteReplica : public RetrySpecificTSRpcTaskWithTable {
 public:
  AsyncDeleteReplica(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const scoped_refptr<TableInfo>& table, TabletId tablet_id,
      tablet::TabletDataState delete_type,
      boost::optional<int64_t> cas_config_opid_index_less_or_equal, LeaderEpoch epoch,
      AsyncTaskThrottlerBase* async_task_throttler, std::string reason)
      : RetrySpecificTSRpcTaskWithTable(
            master, callback_pool, permanent_uuid, table, std::move(epoch), async_task_throttler),
        tablet_id_(std::move(tablet_id)),
        delete_type_(delete_type),
        cas_config_opid_index_less_or_equal_(std::move(cas_config_opid_index_less_or_equal)),
        reason_(std::move(reason)) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kDeleteReplica;
  }

  std::string type_name() const override { return "Delete Tablet"; }

  std::string description() const override;

  Status BeforeSubmitToTaskPool() override;

  Status OnSubmitFailure() override;

  void set_hide_only(bool value) {
    hide_only_ = value;
  }

  void set_keep_data(bool value) {
    keep_data_ = value;
  }

  TabletId tablet_id() const override { return tablet_id_; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void UnregisterAsyncTaskCallback() override;

  const TabletId tablet_id_;
  const tablet::TabletDataState delete_type_;
  const boost::optional<int64_t> cas_config_opid_index_less_or_equal_;
  const std::string reason_;
  tserver::DeleteTabletResponsePB resp_;
  bool hide_only_ = false;
  bool keep_data_ = false;

 private:
  Status SetPendingDelete(AddPendingDelete add_pending_delete);
};

// Send the "Alter Table" with the latest table schema to the leader replica
// for the tablet.
// Keeps retrying until we get an "ok" response.
//  - Alter completed
//  - Tablet has already a newer version
//    (which may happen in case of concurrent alters, or in case a previous attempt timed
//     out but was actually applied).
class AsyncAlterTable : public AsyncTabletLeaderTask {
 public:
  AsyncAlterTable(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      LeaderEpoch epoch)
      : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)) {}

  AsyncAlterTable(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const scoped_refptr<TableInfo>& table, const TransactionId transaction_id, LeaderEpoch epoch)
    : AsyncTabletLeaderTask(master, callback_pool, tablet, table, std::move(epoch)),
        transaction_id_(transaction_id) {}

  AsyncAlterTable(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const scoped_refptr<TableInfo>& table, const TransactionId transaction_id, LeaderEpoch epoch,
      const xrepl::StreamId& cdc_sdk_stream_id, const bool cdc_sdk_require_history_cutoff)
      : AsyncTabletLeaderTask(master, callback_pool, tablet, table, std::move(epoch)),
          transaction_id_(transaction_id),
          cdc_sdk_stream_id_(cdc_sdk_stream_id),
          cdc_sdk_require_history_cutoff_(cdc_sdk_require_history_cutoff) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAlterTable;
  }

  std::string type_name() const override { return "Alter Table"; }

  TableType table_type() const;

 protected:
  uint32_t schema_version_;
  tserver::ChangeMetadataResponsePB resp_;

 private:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  TransactionId transaction_id_ = TransactionId::Nil();
  const xrepl::StreamId cdc_sdk_stream_id_ = xrepl::StreamId::Nil();
  const bool cdc_sdk_require_history_cutoff_ = false;
};

class AsyncBackfillDone : public AsyncAlterTable {
 public:
  AsyncBackfillDone(Master* master,
                    ThreadPool* callback_pool,
                    const scoped_refptr<TabletInfo>& tablet,
                    const std::string& table_id,
                    LeaderEpoch epoch)
    : AsyncAlterTable(master, callback_pool, tablet, std::move(epoch)), table_id_(table_id) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kBackfillDone;
  }

  std::string type_name() const override { return "Mark backfill done."; }

 private:
  bool SendRequest(int attempt) override;

  const std::string table_id_;
};

// Send a Truncate() RPC request.
class AsyncTruncate : public AsyncTabletLeaderTask {
 public:
  AsyncTruncate(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      LeaderEpoch epoch)
      : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kTruncateTablet;
  }

  // TODO: Could move Type to the outer scope and use YB_DEFINE_ENUM for it. So type_name() could
  // be removed.
  std::string type_name() const override { return "Truncate Tablet"; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  tserver::TruncateResponsePB resp_;
};

class CommonInfoForRaftTask : public RetryingTSRpcTaskWithTable {
 public:
  CommonInfoForRaftTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      LeaderEpoch epoch);

  ~CommonInfoForRaftTask();

  TabletId tablet_id() const override;

  virtual std::string change_config_ts_uuid() const { return change_config_ts_uuid_; }

 protected:
  // Used by SendOrReceiveData. Return's false if RPC should not be sent.
  virtual Status PrepareRequest(int attempt) = 0;

  TabletServerId permanent_uuid() const;

  const scoped_refptr<TabletInfo> tablet_;
  const consensus::ConsensusStatePB cstate_;

  // The uuid of the TabletServer we intend to change in the config, for example, the one we are
  // adding to a new config, or the one we intend to remove from the current config.
  //
  // This is different from the target_ts_desc_, which points to the tablet server to whom we
  // issue the ChangeConfig RPC call, which is the Leader in the case of this class, due to the
  // PickLeaderReplica set in the constructor.
  const std::string change_config_ts_uuid_;
};

class AsyncChangeConfigTask : public CommonInfoForRaftTask {
 public:
  AsyncChangeConfigTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      LeaderEpoch epoch)
      : CommonInfoForRaftTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch)) {}

  std::string type_name() const override { return "ChangeConfig"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  consensus::ChangeConfigRequestPB req_;
  consensus::ChangeConfigResponsePB resp_;
};

class AsyncAddServerTask : public AsyncChangeConfigTask {
 public:
  AsyncAddServerTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      consensus::PeerMemberType member_type, const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid, LeaderEpoch epoch)
      : AsyncChangeConfigTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch)),
        member_type_(member_type) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddServer;
  }

  std::string type_name() const override { return "AddServer ChangeConfig"; }

  bool started_by_lb() const override { return true; }

 protected:
  Status PrepareRequest(int attempt) override;

 private:
  // PRE_VOTER or PRE_OBSERVER (for async replicas).
  consensus::PeerMemberType member_type_;
};

// Task to remove a tablet server peer from an overly-replicated tablet config.
class AsyncRemoveServerTask : public AsyncChangeConfigTask {
 public:
  AsyncRemoveServerTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      LeaderEpoch epoch)
      : AsyncChangeConfigTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch)) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kRemoveServer;
  }

  std::string type_name() const override { return "RemoveServer ChangeConfig"; }

  bool started_by_lb() const override { return true; }

 protected:
  Status PrepareRequest(int attempt) override;
};

// Task to step down tablet server leader and optionally to remove it from an overly-replicated
// tablet config.
class AsyncTryStepDown : public CommonInfoForRaftTask {
 public:
  AsyncTryStepDown(
      Master* master,
      ThreadPool* callback_pool,
      const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid,
      bool should_remove,
      LeaderEpoch epoch,
      const std::string& new_leader_uuid = "")
      : CommonInfoForRaftTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch)),
        should_remove_(should_remove),
        new_leader_uuid_(new_leader_uuid) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kTryStepDown;
  }

  std::string type_name() const override { return "Stepdown Leader"; }

  std::string description() const override {
    return "Async Leader Stepdown";
  }

  std::string new_leader_uuid() const { return new_leader_uuid_; }

  bool started_by_lb() const override { return true; }

 protected:
  Status PrepareRequest(int attempt) override;
  bool SendRequest(int attempt) override;
  void HandleResponse(int attempt) override;

  const bool should_remove_;
  const std::string new_leader_uuid_;
  consensus::LeaderStepDownRequestPB stepdown_req_;
  consensus::LeaderStepDownResponsePB stepdown_resp_;
};

// Task to add a table to a tablet. Catalog Manager uses this task to send the request to the
// tserver admin service.
class AsyncAddTableToTablet : public RetryingTSRpcTaskWithTable {
 public:
  AsyncAddTableToTablet(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const scoped_refptr<TableInfo>& table, LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToTablet;
  }

  std::string type_name() const override { return "Add Table to Tablet"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return tablet_id_; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  scoped_refptr<TabletInfo> tablet_;
  const TabletId tablet_id_;
  tserver::AddTableToTabletRequestPB req_;
  tserver::AddTableToTabletResponsePB resp_;
};

// Task to remove a table from a tablet. Catalog Manager uses this task to send the request to the
// tserver admin service.
class AsyncRemoveTableFromTablet : public RetryingTSRpcTaskWithTable {
 public:
  AsyncRemoveTableFromTablet(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const scoped_refptr<TableInfo>& table, LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kRemoveTableFromTablet;
  }

  std::string type_name() const override { return "Remove Table from Tablet"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return tablet_id_; }

  bool SendRequest(int attempt) override;
  void HandleResponse(int attempt) override;

  const scoped_refptr<TabletInfo> tablet_;
  const TabletId tablet_id_;
  tserver::RemoveTableFromTabletRequestPB req_;
  tserver::RemoveTableFromTabletResponsePB resp_;
};

class AsyncGetTabletSplitKey : public AsyncTabletLeaderTask {
 public:
  struct Data {
    const std::string& split_encoded_key;
    const std::string& split_partition_key;
  };
  using DataCallbackType = std::function<void(const Result<Data>&)>;

  AsyncGetTabletSplitKey(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      ManualSplit is_manual_split, LeaderEpoch epoch, DataCallbackType result_cb);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kGetTabletSplitKey;
  }

  std::string type_name() const override { return "Get Tablet Split Key"; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void Finished(const Status& status) override;

  tserver::GetSplitKeyRequestPB req_;
  tserver::GetSplitKeyResponsePB resp_;
  DataCallbackType result_cb_;
};

// Sends SplitTabletRequest with provided arguments to the service interface of the leader of the
// tablet.
class AsyncSplitTablet : public AsyncTabletLeaderTask {
 public:
  AsyncSplitTablet(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const std::array<TabletId, kNumSplitParts>& new_tablet_ids,
      const std::string& split_encoded_key, const std::string& split_partition_key,
      LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kSplitTablet;
  }

  std::string type_name() const override { return "Split Tablet"; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  tablet::SplitTabletRequestPB req_;
  tserver::SplitTabletResponsePB resp_;
  TabletSplitCompleteHandlerIf* tablet_split_complete_handler_;
};

class AsyncTsTestRetry : public RetrySpecificTSRpcTask {
 public:
  AsyncTsTestRetry(
      Master* master, ThreadPool* callback_pool, const TabletServerId& ts_uuid,
      int32_t num_retries, StdStatusCallback callback);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kTestRetryTs;
  }

  std::string type_name() const override { return "Test retry tserver"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return TabletId(); }
  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  tserver::TestRetryResponsePB resp_;
  int32_t num_retries_;
  StdStatusCallback callback_;
};

class AsyncMasterTestRetry : public RetryingMasterRpcTask {
 public:
  AsyncMasterTestRetry(
      Master *master, ThreadPool *callback_pool, consensus::RaftPeerPB&& peer,
      int32_t num_retries, StdStatusCallback callback);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kTestRetryMaster;
  }

  std::string type_name() const override { return "Test retry master"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  master::TestRetryResponsePB resp_;
  int32_t num_retries_;
  StdStatusCallback callback_;
};

class AsyncUpdateTransactionTablesVersion: public RetrySpecificTSRpcTask {
 public:
  AsyncUpdateTransactionTablesVersion(Master *master,
                                      ThreadPool* callback_pool,
                                      const TabletServerId& ts_uuid,
                                      uint64_t version,
                                      StdStatusCallback callback);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kUpdateTransactionTablesVersion;
  }

  std::string type_name() const override { return "Update Transaction Tables Version"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return TabletId(); }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void Finished(const Status& status) override;

  uint64_t version_;
  StdStatusCallback callback_;
  tserver::UpdateTransactionTablesVersionResponsePB resp_;
};

class AsyncCloneTablet: public AsyncTabletLeaderTask {
 public:
  AsyncCloneTablet(
      Master* master,
      ThreadPool* callback_pool,
      const TabletInfoPtr& tablet,
      LeaderEpoch epoch,
      tablet::CloneTabletRequestPB req);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kCloneTablet;
  }

  std::string type_name() const override { return "Clone Tablet"; }

  std::string description() const override;

 private:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  tablet::CloneTabletRequestPB req_;
  tserver::CloneTabletResponsePB resp_;
};

class AsyncClonePgSchema : public RetrySpecificTSRpcTask {
 public:
  using ClonePgSchemaCallbackType = std::function<Status(Status)>;
  AsyncClonePgSchema(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const std::string& source_db_name, const std::string& target_db_name, HybridTime restore_time,
      ClonePgSchemaCallbackType callback, MonoTime deadline);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kClonePgSchema;
  }

  std::string type_name() const override { return "Clone PG Schema Objects"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  MonoTime ComputeDeadline() override;
  // Not associated with a tablet.
  TabletId tablet_id() const override { return TabletId(); }

 private:
  std::string source_db_name_;
  std::string target_db_name;
  HybridTime restore_ht_;
  tserver::ClonePgSchemaResponsePB resp_;
  ClonePgSchemaCallbackType callback_;
};

} // namespace master
} // namespace yb
