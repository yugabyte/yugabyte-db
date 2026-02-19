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

#include "yb/master/async_rpc_tasks_base.h"

#include "yb/common/constants.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_test.pb.h"
#include "yb/master/tablet_health_manager.h"

#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/status_callback.h"

namespace yb::master {

// Fire off the async create tablet.
// This requires that the new tablet info is locked for write, and the
// consensus configuration information has been filled into the 'dirty' data.
class AsyncCreateReplica : public RetrySpecificTSRpcTaskWithTable {
 public:
  AsyncCreateReplica(Master *master,
                     ThreadPool *callback_pool,
                     const std::string& permanent_uuid,
                     const TabletInfoPtr& tablet,
                     const TabletInfo::ReadLock& tablet_lock,
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
  TabletId tablet_id() const override { return {}; }

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
                     const TabletInfoPtr& tablet,
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
      const scoped_refptr<TableInfo>& table, const TabletInfoPtr& tablet,
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

  const TabletInfoPtr tablet_;
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
      std::optional<int64_t> cas_config_opid_index_less_or_equal, LeaderEpoch epoch,
      AsyncTaskThrottlerBase* async_task_throttler, const std::string& reason)
      : RetrySpecificTSRpcTaskWithTable(
            master, callback_pool, permanent_uuid, table, std::move(epoch), async_task_throttler),
        tablet_id_(std::move(tablet_id)),
        delete_type_(delete_type),
        cas_config_opid_index_less_or_equal_(std::move(cas_config_opid_index_less_or_equal)),
        reason_(reason) {}

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

  void set_exclude_aborting_transaction_id(TransactionId value) {
    exclude_aborting_transaction_id_ = value;
  }

  TabletId tablet_id() const override { return tablet_id_; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void UnregisterAsyncTaskCallback() override;
  bool RetryTaskAfterRPCFailure(const Status& status) override;

  const TabletId tablet_id_;
  const tablet::TabletDataState delete_type_;
  const std::optional<int64_t> cas_config_opid_index_less_or_equal_;
  const std::string reason_;
  tserver::DeleteTabletResponsePB resp_;
  bool hide_only_ = false;
  bool keep_data_ = false;
  std::optional<TransactionId> exclude_aborting_transaction_id_{std::nullopt};

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
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      LeaderEpoch epoch)
      : AsyncTabletLeaderTask(master, callback_pool, tablet, std::move(epoch)) {}

  AsyncAlterTable(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const scoped_refptr<TableInfo>& table, TransactionId transaction_id, LeaderEpoch epoch)
      : AsyncTabletLeaderTask(master, callback_pool, tablet, table, std::move(epoch)),
        transaction_id_(transaction_id) {}

  AsyncAlterTable(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const scoped_refptr<TableInfo>& table, TransactionId transaction_id, LeaderEpoch epoch,
      const xrepl::StreamId& cdc_sdk_stream_id, bool cdc_sdk_require_history_cutoff)
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

  bool SendRequest(int attempt) override;

 private:
  void HandleResponse(int attempt) override;
  virtual void HandleInsertPackedSchema(tablet::ChangeMetadataRequestPB& req) { return; }

  TransactionId transaction_id_ = TransactionId::Nil();
  const xrepl::StreamId cdc_sdk_stream_id_ = xrepl::StreamId::Nil();
  const bool cdc_sdk_require_history_cutoff_ = false;
};

class AsyncBackfillDone : public AsyncAlterTable {
 public:
  AsyncBackfillDone(Master* master,
                    ThreadPool* callback_pool,
                    const TabletInfoPtr& tablet,
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

class AsyncInsertPackedSchemaForXClusterTarget : public AsyncAlterTable {
 public:
  // For colocated alters, `table` should be the table we are modifying (ie not the parent table).
  AsyncInsertPackedSchemaForXClusterTarget(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const scoped_refptr<TableInfo>& table, const SchemaPB& packed_schema, LeaderEpoch epoch)
      : AsyncAlterTable(
            master, callback_pool, tablet, table, TransactionId::Nil(), std::move(epoch)),
        packed_schema_(packed_schema) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kInsertPackedSchemaForXClusterTarget;
  }

  std::string type_name() const override { return "Insert packed schema for xCluster target"; }

 protected:
  void HandleInsertPackedSchema(tablet::ChangeMetadataRequestPB& req) override;

  bool SendRequest(int attempt) override;

 private:
  SchemaPB packed_schema_;
};

// Fetch the latest compatible schema version from a single tablet.
class AsyncGetLatestCompatibleSchemaVersion : public AsyncTabletLeaderTask {
 public:
  AsyncGetLatestCompatibleSchemaVersion(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const scoped_refptr<TableInfo>& table, const SchemaPB& schema, LeaderEpoch epoch,
      std::function<void(const Status&, uint32_t)> callback)
      : AsyncTabletLeaderTask(master, callback_pool, tablet, table, std::move(epoch)),
        schema_(schema),
        callback_(std::move(callback)) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kGetCompatibleSchemaVersion;
  }

  std::string type_name() const override { return "Get Compatible Schema Version"; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  SchemaPB schema_;
  tserver::GetCompatibleSchemaVersionRequestPB req_;
  tserver::GetCompatibleSchemaVersionResponsePB resp_;
  // The schema version returned is undefined if the status is not OK.
  std::function<void(const Status&, SchemaVersion)> callback_;
};

// Send a Truncate() RPC request.
class AsyncTruncate : public AsyncTabletLeaderTask {
 public:
  AsyncTruncate(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
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
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      LeaderEpoch epoch);

  ~CommonInfoForRaftTask();

  TabletId tablet_id() const override;

  virtual std::string change_config_ts_uuid() const { return change_config_ts_uuid_; }

 protected:
  // Used by SendOrReceiveData. Return's false if RPC should not be sent.
  virtual Status PrepareRequest(int attempt) = 0;

  const TabletInfoPtr tablet_;
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
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      LeaderEpoch epoch, const std::string& reason)
      : CommonInfoForRaftTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch)),
        reason_(reason) {}

  std::string type_name() const override { return "ChangeConfig"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  consensus::ChangeConfigRequestPB req_;
  consensus::ChangeConfigResponsePB resp_;

 private:
  std::string reason_;
};

class AsyncAddServerTask : public AsyncChangeConfigTask {
 public:
  AsyncAddServerTask(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      consensus::PeerMemberType member_type, const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid, LeaderEpoch epoch, const std::string& reason)
      : AsyncChangeConfigTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch), reason),
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
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid,
      LeaderEpoch epoch, const std::string& reason)
      : AsyncChangeConfigTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch), reason)
        {}

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
      const TabletInfoPtr& tablet,
      const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid,
      bool should_remove,
      LeaderEpoch epoch,
      const std::string& reason,
      const std::string& new_leader_uuid = "")
      : CommonInfoForRaftTask(
            master, callback_pool, tablet, cstate, change_config_ts_uuid, std::move(epoch)),
        should_remove_(should_remove),
        new_leader_uuid_(new_leader_uuid),
        reason_(reason) {}

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kTryStepDown;
  }

  std::string type_name() const override { return "Stepdown Leader"; }

  std::string description() const override;

  std::string new_leader_uuid() const { return new_leader_uuid_; }

  bool started_by_lb() const override { return true; }

 protected:
  Status PrepareRequest(int attempt) override;
  bool SendRequest(int attempt) override;
  void HandleResponse(int attempt) override;

  const bool should_remove_;
  const std::string new_leader_uuid_;
  const std::string reason_;
  consensus::LeaderStepDownRequestPB stepdown_req_;
  consensus::LeaderStepDownResponsePB stepdown_resp_;
};

// Task to add a table to a tablet. Catalog Manager uses this task to send the request to the
// tserver admin service.
class AsyncAddTableToTablet : public RetryingTSRpcTaskWithTable {
 public:
  AsyncAddTableToTablet(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const scoped_refptr<TableInfo>& table, LeaderEpoch epoch,
      const std::shared_ptr<std::atomic<size_t>>& task_counter);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToTablet;
  }

  std::string type_name() const override { return "Add Table to Tablet"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return tablet_id_; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  TabletInfoPtr tablet_;
  const TabletId tablet_id_;
  tserver::AddTableToTabletRequestPB req_;
  tserver::AddTableToTabletResponsePB resp_;
  std::shared_ptr<std::atomic<size_t>> task_counter_;
};

// Task to remove a table from a tablet. Catalog Manager uses this task to send the request to the
// tserver admin service.
class AsyncRemoveTableFromTablet : public RetryingTSRpcTaskWithTable {
 public:
  AsyncRemoveTableFromTablet(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
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

  const TabletInfoPtr tablet_;
  const TabletId tablet_id_;
  tserver::RemoveTableFromTabletRequestPB req_;
  tserver::RemoveTableFromTabletResponsePB resp_;
};

class AsyncGetTabletSplitKey : public AsyncTabletLeaderTask {
 public:
  struct Data {
    // TODO(nway-tsplit): Consider avoiding the cost of string copy by storing references.
    std::vector<std::string> split_encoded_keys;
    std::vector<std::string> split_partition_keys;
  };
  using DataCallbackType = std::function<void(const Result<Data>&)>;

  AsyncGetTabletSplitKey(
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      ManualSplit is_manual_split, int split_factor, LeaderEpoch epoch,
      DataCallbackType result_cb);

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
      Master* master, ThreadPool* callback_pool, const TabletInfoPtr& tablet,
      const std::vector<TabletId>& new_tablet_ids,
      const std::vector<std::string>& split_encoded_keys,
      const std::vector<std::string>& split_partition_keys, LeaderEpoch epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kSplitTablet;
  }

  std::string type_name() const override { return "Split Tablet"; }

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  tablet::SplitTabletRequestPB req_;
  tserver::SplitTabletResponsePB resp_;
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
  TabletId tablet_id() const override { return {}; }

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
  TabletId tablet_id() const override { return {}; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void Finished(const Status& status) override;

  uint64_t version_;
  StdStatusCallback callback_;
  tserver::UpdateTransactionTablesVersionResponsePB resp_;
};

} // namespace yb::master
