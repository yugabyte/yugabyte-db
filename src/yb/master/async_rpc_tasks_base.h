// Copyright (c) YugabyteDB, Inc.
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

#include "yb/consensus/consensus.pb.h"

#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/monitored_task.h"

#include "yb/tserver/backup.proxy.h"

#include "yb/util/async_task_util.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

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

  virtual Result<TSDescriptorPtr> PickReplica() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TSPicker);
};

// Implementation of TSPicker which sends to a specific tablet server,
// identified by its UUID.
class PickSpecificUUID : public TSPicker {
 public:
  PickSpecificUUID(Master* master, std::string ts_uuid)
      : master_(master), ts_uuid_(std::move(ts_uuid)) {}

  Result<TSDescriptorPtr> PickReplica() override;

 private:
  Master* const master_;
  const std::string ts_uuid_;

  DISALLOW_COPY_AND_ASSIGN(PickSpecificUUID);
};

// Implementation of TSPicker which locates the current leader replica,
// and sends the RPC to that server.
class PickLeaderReplica : public TSPicker {
 public:
  explicit PickLeaderReplica(const TabletInfoPtr& tablet);

  Result<TSDescriptorPtr> PickReplica() override;

 private:
  const TabletInfoPtr tablet_;
};

// A background task which continuously retries sending an RPC to a master or tserver.
class RetryingRpcTask : public server::RunnableMonitoredTask {
 public:
  RetryingRpcTask(Master* master,
                  ThreadPool* callback_pool,
                  AsyncTaskThrottlerBase* async_task_throttler);

  ~RetryingRpcTask();

  // Send the subclass RPC request.
  Status Run() override;

  // Abort this task and return its value before it was successfully aborted. If the task entered
  // a different terminal state before we were able to abort it, return that state.
  server::MonitoredTaskState AbortAndReturnPrevState(
      const Status& status, bool call_task_finisher = true) override;

  Status GetStatus() const;

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

  // Theorethical max deadline, should be used as an upperbound deadline for a single attempt.
  MonoTime UnresponsiveDeadline() const;

  // A deadline for a single retry/attempt.
  virtual MonoTime ComputeDeadline() const;

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

  // Task's overall deadline, which covers all retries/attempts.
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

  mutable simple_spinlock status_mutex_;
  Status final_status_ GUARDED_BY(status_mutex_);

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

  void SaveFinalStatusAndCallFinished(const Status& status);

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
  RetryingTSRpcTask(Master* master,
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

  TSDescriptorPtr target_ts_desc() const;
  TabletServerId permanent_uuid() const;

  virtual bool RetryTaskAfterRPCFailure(const Status& status);

  const std::unique_ptr<TSPicker> replica_picker_;
  mutable simple_spinlock target_ts_mutex_;
  TSDescriptorPtr target_ts_desc_ GUARDED_BY(target_ts_mutex_) = nullptr;

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
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      LeaderEpoch epoch);

  AsyncTabletLeaderTask(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const scoped_refptr<TableInfo>& table, LeaderEpoch epoch);

  ~AsyncTabletLeaderTask();

  std::string description() const override;

  TabletId tablet_id() const override;

 protected:
  TabletInfoPtr tablet_;
};

} // namespace master
} // namespace yb
