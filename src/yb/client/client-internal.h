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

#include <functional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/client/client.h"

#include "yb/common/common_net.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/qlexpr/index.h"
#include "yb/common/transaction.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_admin.fwd.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc.h"

#include "yb/server/server_base_options.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/threadpool.h"

namespace yb {

class HostPort;

namespace client {

YB_STRONGLY_TYPED_BOOL(Retry);

class YBClient::Data {
 public:
  Data();
  ~Data();

  // Selects a TS replica from the given RemoteTablet subject
  // to liveness and the provided selection criteria and blacklist.
  //
  // If no appropriate replica can be found, a non-OK status is returned and 'ts' is untouched.
  //
  // The 'candidates' return parameter indicates tservers that are live and meet the selection
  // criteria, but are possibly filtered by the blacklist. This is useful for implementing
  // retry logic.
  Status GetTabletServer(YBClient* client,
                         const scoped_refptr<internal::RemoteTablet>& rt,
                         ReplicaSelection selection,
                         const std::set<std::string>& blacklist,
                         std::vector<internal::RemoteTabletServer*>* candidates,
                         internal::RemoteTabletServer** ts);

  Status AlterNamespace(YBClient* client,
                        const master::AlterNamespaceRequestPB& req,
                        CoarseTimePoint deadline);

  Status IsCreateNamespaceInProgress(YBClient* client,
                                const std::string& namespace_name,
                                const boost::optional<YQLDatabase>& database_type,
                                const std::string& namespace_id,
                                CoarseTimePoint deadline,
                                bool *create_in_progress);

  Status WaitForCreateNamespaceToFinish(YBClient* client,
                                const std::string& namespace_name,
                                const boost::optional<YQLDatabase>& database_type,
                                const std::string& namespace_id,
                                CoarseTimePoint deadline);

  Status IsDeleteNamespaceInProgress(YBClient* client,
                                     const std::string& namespace_name,
                                     const boost::optional<YQLDatabase>& database_type,
                                     const std::string& namespace_id,
                                     CoarseTimePoint deadline,
                                     bool *delete_in_progress);

  Status WaitForDeleteNamespaceToFinish(YBClient* client,
                                        const std::string& namespace_name,
                                        const boost::optional<YQLDatabase>& database_type,
                                        const std::string& namespace_id,
                                        CoarseTimePoint deadline);

  Status IsCloneNamespaceInProgress(
      YBClient* client, const std::string& source_namespace_id, int clone_seq_no,
      CoarseTimePoint deadline, bool* create_in_progress);
  Status WaitForCloneNamespaceToFinish(
      YBClient* client, const std::string& source_namespace_id, int clone_seq_no,
      CoarseTimePoint deadline);

  Status CreateTable(YBClient* client,
                     const master::CreateTableRequestPB& req,
                     const YBSchema& schema,
                     CoarseTimePoint deadline,
                     std::string* table_id);

  // Take one of table id or name.
  Status IsCreateTableInProgress(YBClient* client,
                                 const YBTableName& table_name,
                                 const std::string& table_id,
                                 CoarseTimePoint deadline,
                                 bool *create_in_progress);

  // Take one of table id or name.
  Status WaitForCreateTableToFinish(
      YBClient* client,
      const YBTableName& table_name,
      const std::string& table_id,
      CoarseTimePoint deadline,
      const uint32_t max_jitter_ms = CoarseBackoffWaiter::kDefaultMaxJitterMs,
      const uint32_t init_exponent = CoarseBackoffWaiter::kDefaultInitExponent);

  // Take one of table id or name.
  Status DeleteTable(YBClient* client,
                             const YBTableName& table_name,
                             const std::string& table_id,
                             bool is_index_table,
                             CoarseTimePoint deadline,
                             YBTableName* indexed_table_name,
                             bool wait = true,
                             const TransactionMetadata *txn = nullptr);

  Status IsDeleteTableInProgress(YBClient* client,
                                 const std::string& table_id,
                                 CoarseTimePoint deadline,
                                 bool *delete_in_progress);

  Status WaitForDeleteTableToFinish(YBClient* client,
                                    const std::string& table_id,
                                    CoarseTimePoint deadline);

  Status TruncateTables(YBClient* client,
                        const std::vector<std::string>& table_ids,
                        CoarseTimePoint deadline,
                        bool wait = true);

  Status IsTruncateTableInProgress(YBClient* client,
                                   const std::string& table_id,
                                   CoarseTimePoint deadline,
                                   bool *truncate_in_progress);

  Status WaitForTruncateTableToFinish(YBClient* client,
                                      const std::string& table_id,
                                      CoarseTimePoint deadline);

  Status CreateTablegroup(YBClient* client,
                          CoarseTimePoint deadline,
                          const std::string& namespace_name,
                          const std::string& namespace_id,
                          const std::string& tablegroup_id,
                          const std::string& tablespace_id,
                          const TransactionMetadata* txn);

  Status DeleteTablegroup(YBClient* client,
                          CoarseTimePoint deadline,
                          const std::string& tablegroup_id,
                          const TransactionMetadata* txn);

  Status BackfillIndex(YBClient* client,
                       const YBTableName& table_name,
                       const TableId& table_id,
                       CoarseTimePoint deadline,
                       bool wait = true);
  Status IsBackfillIndexInProgress(YBClient* client,
                                   const TableId& table_id,
                                   const TableId& index_id,
                                   CoarseTimePoint deadline,
                                   bool* backfill_in_progress);
  Status WaitForBackfillIndexToFinish(YBClient* client,
                                      const TableId& table_id,
                                      const TableId& index_id,
                                      CoarseTimePoint deadline);

  Result<master::GetBackfillStatusResponsePB> GetBackfillStatus(
    const std::vector<std::string_view>& table_ids,
    const CoarseTimePoint deadline);

  Status AlterTable(YBClient* client,
                    const master::AlterTableRequestPB& req,
                    CoarseTimePoint deadline);

  // Take one of table id or name.
  Status IsAlterTableInProgress(YBClient* client,
                                const YBTableName& table_name,
                                std::string table_id,
                                CoarseTimePoint deadline,
                                bool *alter_in_progress);

  Status WaitForAlterTableToFinish(YBClient* client,
                                   const YBTableName& alter_name,
                                   std::string table_id,
                                   CoarseTimePoint deadline);

  Status FlushTables(YBClient* client,
                     const std::vector<YBTableName>& table_names,
                     bool add_indexes,
                     const CoarseTimePoint deadline,
                     const bool is_compaction);

  Status FlushTables(YBClient* client,
                     const std::vector<TableId>& table_ids,
                     bool add_indexes,
                     const CoarseTimePoint deadline,
                     const bool is_compaction);

  Status IsFlushTableInProgress(YBClient* client,
                                const FlushRequestId& flush_id,
                                const CoarseTimePoint deadline,
                                bool *flush_in_progress);

  Status WaitForFlushTableToFinish(YBClient* client,
                                   const FlushRequestId& flush_id,
                                   const CoarseTimePoint deadline);

  Result<TableCompactionStatus> GetCompactionStatus(
      const YBTableName& table_name, bool show_tablets, const CoarseTimePoint deadline);

  Status GetTableSchema(YBClient* client,
                        const YBTableName& table_name,
                        CoarseTimePoint deadline,
                        YBTableInfo* info);
  Status GetTableSchema(YBClient* client,
                        const TableId& table_id,
                        CoarseTimePoint deadline,
                        YBTableInfo* info,
                        master::GetTableSchemaResponsePB* resp = nullptr);
  Status GetTableSchema(YBClient* client,
                        const YBTableName& table_name,
                        CoarseTimePoint deadline,
                        std::shared_ptr<YBTableInfo> info,
                        StatusCallback callback,
                        master::GetTableSchemaResponsePB* resp_ignored = nullptr);
  Status GetTableSchema(YBClient* client,
                        const TableId& table_id,
                        CoarseTimePoint deadline,
                        std::shared_ptr<YBTableInfo> info,
                        StatusCallback callback,
                        master::GetTableSchemaResponsePB* resp = nullptr);
  Status GetTablegroupSchemaById(YBClient* client,
                                 const TablegroupId& tablegroup_id,
                                 CoarseTimePoint deadline,
                                 std::shared_ptr<std::vector<YBTableInfo>> info,
                                 StatusCallback callback);
  Status GetColocatedTabletSchemaByParentTableId(
      YBClient* client,
      const TableId& parent_colocated_table_id,
      CoarseTimePoint deadline,
      std::shared_ptr<std::vector<YBTableInfo>> info,
      StatusCallback callback);

  Result<IndexPermissions> GetIndexPermissions(
      YBClient* client,
      const TableId& table_id,
      const TableId& index_id,
      const CoarseTimePoint deadline);
  Result<IndexPermissions> GetIndexPermissions(
      YBClient* client,
      const YBTableName& table_name,
      const TableId& index_id,
      const CoarseTimePoint deadline);
  Result<IndexPermissions> WaitUntilIndexPermissionsAtLeast(
      YBClient* client,
      const TableId& table_id,
      const TableId& index_id,
      const IndexPermissions& target_index_permissions,
      const CoarseTimePoint deadline,
      const CoarseDuration max_wait = std::chrono::seconds(2));
  Result<IndexPermissions> WaitUntilIndexPermissionsAtLeast(
      YBClient* client,
      const YBTableName& table_name,
      const YBTableName& index_name,
      const IndexPermissions& target_index_permissions,
      const CoarseTimePoint deadline,
      const CoarseDuration max_wait = std::chrono::seconds(2));

  void CreateCDCStream(YBClient* client,
                       const TableId& table_id,
                       const std::unordered_map<std::string, std::string>& options,
                       cdc::StreamModeTransactional transactional,
                       CoarseTimePoint deadline,
                       CreateCDCStreamCallback callback);

  void DeleteCDCStream(
      YBClient* client,
      const xrepl::StreamId& stream_id,
      CoarseTimePoint deadline,
      StatusCallback callback);

  Status BootstrapProducer(
      YBClient* client,
      const YQLDatabase& db_type,
      const NamespaceName& namespace_name,
      const std::vector<PgSchemaName>& pg_schema_names,
      const std::vector<TableName>& table_names,
      CoarseTimePoint deadline,
      BootstrapProducerCallback callback);

  void GetCDCDBStreamInfo(YBClient *client,
    const std::string &db_stream_id,
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> db_stream_info,
    CoarseTimePoint deadline,
    StdStatusCallback callback);

  void GetCDCStream(
      YBClient* client,
      const xrepl::StreamId& stream_id,
      std::shared_ptr<TableId> table_id,
      std::shared_ptr<std::unordered_map<std::string, std::string>> options,
      CoarseTimePoint deadline,
      StdStatusCallback callback);

  void DeleteNotServingTablet(
      YBClient* client, const TabletId& tablet_id, CoarseTimePoint deadline,
      StdStatusCallback callback);

  void GetTableLocations(
      YBClient* client, const TableId& table_id, int32_t max_tablets,
      RequireTabletsRunning require_tablets_running, PartitionsOnly partitions_only,
      CoarseTimePoint deadline, GetTableLocationsCallback callback);

  bool IsTabletServerLocal(const internal::RemoteTabletServer& rts) const;

  Status CreateSnapshot(
    YBClient* client, const std::vector<YBTableName>& tables, CoarseTimePoint deadline,
    CreateSnapshotCallback callback);

  // Returns a non-failed replica of the specified tablet based on the provided selection criteria
  // and tablet server blacklist.
  //
  // In case a local tablet server was marked as failed because the tablet was not in the RUNNING
  // state, we will update the internal state of the local tablet server if the tablet is in the
  // RUNNING state.
  //
  // Returns NULL if there are no valid tablet servers.
  internal::RemoteTabletServer* SelectTServer(
      internal::RemoteTablet* rt,
      const ReplicaSelection selection,
      const std::set<std::string>& blacklist,
      std::vector<internal::RemoteTabletServer*>* candidates);

  // Sets 'master_proxy_' from the address specified by
  // 'leader_master_hostport_'.  Called by
  // GetLeaderMasterRpc::Finished() upon successful completion.
  //
  // See also: SetMasterServerProxyAsync.
  void LeaderMasterDetermined(const Status& status,
                              const HostPort& host_port);

  // Asynchronously sets 'master_proxy_' to the leader master by
  // cycling through servers listed in 'master_server_addrs_' until
  // one responds with a Raft configuration that contains the leader
  // master or 'deadline' expires.
  //
  // Invokes 'cb' with the appropriate status when finished.
  //
  // Works with both a distributed and non-distributed configuration.
  void SetMasterServerProxyAsync(CoarseTimePoint deadline,
                                 bool skip_resolution,
                                 bool wait_for_leader_election,
                                 const StdStatusCallback& cb);

  // Synchronous version of SetMasterServerProxyAsync method above.
  //
  // NOTE: since this uses a Synchronizer, this may not be invoked by
  // a method that's on a reactor thread.
  //
  // TODO (KUDU-492): Get rid of this method and re-factor the client
  // to lazily initialize 'master_proxy_'.
  Status SetMasterServerProxy(CoarseTimePoint deadline,
                              bool skip_resolution = false,
                              bool wait_for_leader_election = true);

  std::shared_ptr<master::MasterAdminProxy> master_admin_proxy() const;
  std::shared_ptr<master::MasterBackupProxy> master_backup_proxy() const;
  std::shared_ptr<master::MasterClientProxy> master_client_proxy() const;
  std::shared_ptr<master::MasterClusterProxy> master_cluster_proxy() const;
  std::shared_ptr<master::MasterDclProxy> master_dcl_proxy() const;
  std::shared_ptr<master::MasterDdlProxy> master_ddl_proxy() const;
  std::shared_ptr<master::MasterReplicationProxy> master_replication_proxy() const;
  std::shared_ptr<master::MasterEncryptionProxy> master_encryption_proxy() const;
  std::shared_ptr<master::MasterTestProxy> master_test_proxy() const;

  HostPort leader_master_hostport() const;

  uint64_t GetLatestObservedHybridTime() const;

  void UpdateLatestObservedHybridTime(uint64_t hybrid_time);

  // API's to add/remove/set the master address list in the client
  Status SetMasterAddresses(const std::string& addresses);
  Status RemoveMasterAddress(const HostPort& addr);
  Status AddMasterAddress(const HostPort& addr);
  // This method reads the master address from the remote endpoint or a file depending on which is
  // specified, and re-initializes the 'master_server_addrs_' variable.
  Status ReinitializeMasterAddresses();

  // Set replication info for the cluster data. Last argument defaults to nullptr to auto-wrap in a
  // retry. It is otherwise used in a RetryFunc to indicate if to keep retrying or not, if we get a
  // version mismatch on setting the config.
  Status SetReplicationInfo(
      YBClient* client, const master::ReplicationInfoPB& replication_info, CoarseTimePoint deadline,
      bool* retry = nullptr);

  // Validate replication info as satisfiable for the cluster data.
  Status ValidateReplicationInfo(
        const master::ReplicationInfoPB& replication_info, CoarseTimePoint deadline);

  // Get disk size of table, calculated as WAL + SST file size.
  // It does not take replication factor into account
  Result<TableSizeInfo> GetTableDiskSize(const TableId& table_id, CoarseTimePoint deadline);

  // Provide the status of the transaction to YB-Master.
  Status ReportYsqlDdlTxnStatus(
      const TransactionMetadata& txn, bool is_committed, const CoarseTimePoint& deadline);

  Status IsYsqlDdlVerificationInProgress(
    const TransactionMetadata& txn,
    CoarseTimePoint deadline,
    bool *ddl_verification_in_progress);

  Status WaitForDdlVerificationToFinish(const TransactionMetadata& txn, CoarseTimePoint deadline);

  Result<bool> CheckIfPitrActive(CoarseTimePoint deadline);

  Status GetXClusterStreams(
      YBClient* client, CoarseTimePoint deadline,
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      const std::vector<TableName>& table_names, const std::vector<PgSchemaName>& pg_schema_names,
      std::function<void(Result<master::GetXClusterStreamsResponsePB>)> user_cb);

  Status IsXClusterBootstrapRequired(
      YBClient* client, CoarseTimePoint deadline,
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      std::function<void(Result<bool>)> user_cb);

  template <class ProxyClass, class ReqClass, class RespClass>
  using SyncLeaderMasterFunc = void (ProxyClass::*)(
      const ReqClass &req, RespClass *response, rpc::RpcController *controller,
      rpc::ResponseCallback callback) const;

  // Retry 'func' until either:
  //
  // 1) Methods succeeds on a leader master.
  // 2) Method fails for a reason that is not related to network
  //    errors, timeouts, or leadership issues.
  // 3) 'deadline' (if initialized) elapses.
  //
  // If 'num_attempts' is not NULL, it will be incremented on every
  // attempt (successful or not) to call 'func'.
  //
  // NOTE: 'rpc_timeout' is a per-call timeout, while 'deadline' is a
  // per operation deadline. If 'deadline' is not initialized, 'func' is
  // retried forever. If 'deadline' expires, 'func_name' is included in
  // the resulting Status.
  template <class ProxyClass, class ReqClass, class RespClass>
  Status SyncLeaderMasterRpc(
      CoarseTimePoint deadline, const ReqClass& req, RespClass* resp, const char* func_name,
      const SyncLeaderMasterFunc<ProxyClass, ReqClass, RespClass>& func, int* attempts = nullptr);

  template <class T, class... Args>
  rpc::RpcCommandPtr StartRpc(Args&&... args);

  bool IsMultiMaster();

  void StartShutdown();

  void CompleteShutdown();

  void DoSetMasterServerProxy(
      CoarseTimePoint deadline, bool skip_resolution, bool wait_for_leader_election);
  Result<server::MasterAddresses> ParseMasterAddresses(const Status& reinit_status);

  rpc::Messenger* messenger_ = nullptr;
  std::unique_ptr<rpc::Messenger> messenger_holder_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  scoped_refptr<internal::MetaCache> meta_cache_;
  scoped_refptr<MetricEntity> metric_entity_;

  // Flag name to fetch master addresses from flagfile.
  std::string master_address_flag_name_;
  // This vector holds the list of master server addresses. Note that each entry in this vector
  // can either be a single 'host:port' or a comma separated list of 'host1:port1,host2:port2,...'.
  std::vector<MasterAddressSource> master_address_sources_;
  // User specified master server addresses.
  std::vector<std::string> master_server_addrs_;
  // master_server_addrs_ + addresses from master_address_sources_.
  std::vector<std::string> full_master_server_addrs_;
  mutable simple_spinlock master_server_addrs_lock_;

  bool skip_master_flagfile_ = false;

  // If all masters are available but no leader is present on client init,
  // this flag determines if the client returns failure right away
  // or waits for a leader to be elected.
  bool wait_for_leader_election_on_init_ = true;

  MonoDelta default_admin_operation_timeout_;
  MonoDelta default_rpc_timeout_;

  // The host port of the leader master. This is set in
  // LeaderMasterDetermined, which is invoked as a callback by
  // SetMasterServerProxyAsync.
  HostPort leader_master_hostport_;

  // Proxy to the leader master.
  std::shared_ptr<master::MasterAdminProxy> master_admin_proxy_;
  std::shared_ptr<master::MasterBackupProxy> master_backup_proxy_;
  std::shared_ptr<master::MasterClientProxy> master_client_proxy_;
  std::shared_ptr<master::MasterClusterProxy> master_cluster_proxy_;
  std::shared_ptr<master::MasterDclProxy> master_dcl_proxy_;
  std::shared_ptr<master::MasterDdlProxy> master_ddl_proxy_;
  std::shared_ptr<master::MasterReplicationProxy> master_replication_proxy_;
  std::shared_ptr<master::MasterEncryptionProxy> master_encryption_proxy_;
  std::shared_ptr<master::MasterTestProxy> master_test_proxy_;

  // Ref-counted RPC instance: since 'SetMasterServerProxyAsync' call
  // is asynchronous, we need to hold a reference in this class
  // itself, as to avoid a "use-after-free" scenario.
  rpc::Rpcs rpcs_;
  rpc::Rpcs::Handle leader_master_rpc_;
  std::vector<StdStatusCallback> leader_master_callbacks_;

  // Protects 'leader_master_rpc_', 'leader_master_hostport_',
  // and master_proxy_
  //
  // See: YBClient::Data::SetMasterServerProxyAsync for a more
  // in-depth explanation of why this is needed and how it works.
  mutable simple_spinlock leader_master_lock_;

  AtomicInt<uint64_t> latest_observed_hybrid_time_;

  std::atomic<bool> closing_{false};

  std::atomic<int> running_sync_requests_{0};

  // Cloud info indicating placement information of client.
  CloudInfoPB cloud_info_pb_;

  // When the client is part of a CQL proxy, this denotes the uuid for the associated tserver to
  // aid in detecting local tservers.
  TabletServerId uuid_;

  bool use_threadpool_for_callbacks_;
  std::unique_ptr<ThreadPool> threadpool_;

  server::ClockPtr clock_;
  const ClientId id_;
  const std::string log_prefix_;

  // Used to track requests that were sent to a particular tablet, so it could track different
  // RPCs related to the same write operation and reject duplicates.
  struct TabletRequests {
    RetryableRequestId request_id_seq = 0;
    std::set<RetryableRequestId> running_requests;
  };

  simple_spinlock tablet_requests_mutex_;
  TabletRequests requests_;

  std::array<std::atomic<int>, 2> tserver_count_cached_;

  std::string client_name_;

 private:
  Status FlushTablesHelper(YBClient* client,
                           const CoarseTimePoint deadline,
                           const master::FlushTablesRequestPB& req);

  DISALLOW_COPY_AND_ASSIGN(Data);
};

} // namespace client
} // namespace yb
