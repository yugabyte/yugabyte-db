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

#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "yb/cdc/cdc_service.pb.h"
#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"

#include "yb/master/master_admin.pb.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/type_traits.h"
#include "yb/common/entity_ids.h"
#include "yb/consensus/consensus_types.pb.h"
#include "yb/common/snapshot.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_fwd.h"

#include "yb/tools/yb-admin_cli.h"
#include "yb/rpc/rpc_fwd.h"

namespace yb {

class HybridTime;

namespace consensus {
class ConsensusServiceProxy;
}

namespace client {
class YBClient;
class XClusterClient;
}

namespace tools {

// Flags for list_snapshot command.
YB_DEFINE_ENUM(ListSnapshotsFlag, (SHOW_DETAILS)(NOT_SHOW_RESTORED)(SHOW_DELETED)(JSON));
using ListSnapshotsFlags = EnumBitSet<ListSnapshotsFlag>;

// Constants for disabling tablet splitting during PITR restores.
static constexpr double kPitrSplitDisableDurationSecs = 600;
static constexpr double kPitrSplitDisableCheckFreqMs = 500;

struct TypedNamespaceName {
  YQLDatabase db_type = YQL_DATABASE_UNKNOWN;
  std::string name;
};

class TableNameResolver {
 public:
  using Values = std::vector<client::YBTableName>;
  TableNameResolver(
      Values* values,
      std::vector<client::YBTableName>&& tables,
      std::vector<master::NamespaceIdentifierPB>&& namespaces);
  TableNameResolver(TableNameResolver&&);
  ~TableNameResolver();

  Result<bool> Feed(const std::string& value);
  const master::NamespaceIdentifierPB* last_namespace() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

HAS_MEMBER_FUNCTION(error);
HAS_MEMBER_FUNCTION(status);

template<class Response>
Status ResponseStatus(
    const Response& response,
    typename std::enable_if<HasMemberFunction_error<Response>::value, void*>::type = nullptr) {
  // Response has has_error method, use status from it.
  if (response.has_error()) {
    return StatusFromPB(response.error().status());
  }
  return Status::OK();
}

template<class Response>
Status ResponseStatus(
    const Response& response,
    typename std::enable_if<HasMemberFunction_status<Response>::value, void*>::type = nullptr) {
  if (response.has_status()) {
    return StatusFromPB(response.status());
  }
  return Status::OK();
}

class ImportSnapshotTableFilter;

class ClusterAdminClient {
 public:
  enum PeerMode {
    LEADER = 1,
    FOLLOWER
  };

  // Creates an admin client for host/port combination e.g.,
  // "localhost" or "127.0.0.1:7050" with the given timeout.
  // If certs_dir is non-empty, caller will init the yb_client_.
  ClusterAdminClient(std::string addrs, MonoDelta timeout);

  ClusterAdminClient(const HostPort& init_master_addr, MonoDelta timeout);

  virtual ~ClusterAdminClient();

  // Initialized the client and connects to the specified tablet server.
  Status Init();

  // Parse the user-specified change type to consensus change type
  Status ParseChangeType(
      const std::string& change_type,
      consensus::ChangeConfigType* cc_type);

  // Change the configuration of the specified tablet.
  Status ChangeConfig(
      const TabletId& tablet_id,
      const std::string& change_type,
      const PeerId& peer_uuid,
      const boost::optional<std::string>& member_type);

  // Change the configuration of the master tablet.
  Status ChangeMasterConfig(
      const std::string& change_type,
      const std::string& peer_host,
      uint16_t peer_port,
      const std::string& peer_uuid = "");

  Status DumpMasterState(bool to_console);

  // List all the tables.
  Status ListTables(bool include_db_type,
                    bool include_table_id,
                    bool include_table_type);

  // List all tablets of this table
  Status ListTablets(const client::YBTableName& table_name,
                     int max_tablets,
                     bool json,
                     bool followers);

  // Per Tablet list of all tablet servers
  Status ListPerTabletTabletServers(const PeerId& tablet_id);

  // Delete a single table by name.
  Status DeleteTable(const client::YBTableName& table_name);

  // Delete a single table by ID.
  Status DeleteTableById(const TableId& table_id);

  // Delete a single index by name.
  Status DeleteIndex(const client::YBTableName& table_name);

  // Delete a single index by ID.
  Status DeleteIndexById(const TableId& table_id);

  // Delete a single namespace by name.
  Status DeleteNamespace(const TypedNamespaceName& name);

  // Delete a single namespace by ID.
  Status DeleteNamespaceById(const NamespaceId& namespace_id);

  // Launch backfill for (deferred) indexes on the specified table.
  Status LaunchBackfillIndexForTable(const client::YBTableName& table_name);

  // List all tablet servers known to master
  Status ListAllTabletServers(bool exclude_dead = false);

  // List all masters
  Status ListAllMasters();

  // List the log locations of all tablet servers, by uuid
  Status ListTabletServersLogLocations();

  // List all the tablets a certain tablet server is serving
  Status ListTabletsForTabletServer(const PeerId& ts_uuid);

  Status SetLoadBalancerEnabled(bool is_enabled);

  Status GetLoadBalancerState();

  Status GetLoadMoveCompletion();

  Status GetLeaderBlacklistCompletion();

  Status GetIsLoadBalancerIdle();

  Status ListLeaderCounts(const client::YBTableName& table_name);

  Result<std::unordered_map<std::string, int>> GetLeaderCounts(
      const client::YBTableName& table_name);

  Status SetupRedisTable();

  Status DropRedisTable();

  Status FlushTables(const std::vector<client::YBTableName>& table_names,
                     bool add_indexes,
                     int timeout_secs,
                     bool is_compaction);

  Status FlushTablesById(const std::vector<TableId>& table_id,
                         bool add_indexes,
                         int timeout_secs,
                         bool is_compaction);

  Status CompactionStatus(const client::YBTableName& table_name, bool show_tablets);

  Status FlushSysCatalog();

  Status CompactSysCatalog();

  Status ModifyTablePlacementInfo(const client::YBTableName& table_name,
                                  const std::string& placement_info,
                                  int replication_factor,
                                  const std::string& optional_uuid);

  Status ModifyPlacementInfo(std::string placement_infos,
                             int replication_factor,
                             const std::string& optional_uuid);

  Status ClearPlacementInfo();

  Status AddReadReplicaPlacementInfo(const std::string& placement_info,
                                     int replication_factor,
                                     const std::string& optional_uuid);

  Status ModifyReadReplicaPlacementInfo(const std::string& placement_uuid,
                                        const std::string& placement_info,
                                        int replication_factor);

  Status DeleteReadReplicaPlacementInfo();

  Status GetUniverseConfig();

  Status GetXClusterConfig();

  Status ChangeBlacklist(const std::vector<HostPort>& servers, bool add,
      bool blacklist_leader);

  Result<const master::NamespaceIdentifierPB&> GetNamespaceInfo(YQLDatabase db_type,
                                                                const std::string& namespace_name);

  Status LeaderStepDownWithNewLeader(
      const std::string& tablet_id,
      const std::string& dest_ts_uuid);

  Status MasterLeaderStepDown(
      const std::string& leader_uuid,
      const std::string& new_leader_uuid = std::string());

  Status SplitTablet(const std::string& tablet_id);

  Status DisableTabletSplitting(int64_t disable_duration_ms, const std::string& feature_name);

  Status IsTabletSplittingComplete(bool wait_for_parent_deletion);

  Status CreateTransactionsStatusTable(const std::string& table_name);

  Status AddTransactionStatusTablet(const TableId& table_id);

  Result<TableNameResolver> BuildTableNameResolver(TableNameResolver::Values* tables);

  Result<std::string> GetMasterLeaderUuid();

  Status GetYsqlCatalogVersion();

  Result<rapidjson::Document> DdlLog();

  // Upgrade YSQL cluster (all databases) to the latest version, applying necessary migrations.
  // Note: Works with a tserver but is placed here (and not in yb-ts-cli) because it doesn't
  //       look like this workflow is a good fit there.
  Status UpgradeYsql(bool use_single_connection);

  Status StartYsqlMajorVersionUpgradeInitdb();

  // Returns error Result if the RPC failed, otherwise returns the response for the caller to parse
  // for both whether next version's initdb is done, and whether there is an initdb (non-RPC)
  // error.
  Result<master::IsYsqlMajorVersionUpgradeInitdbDoneResponsePB>
  IsYsqlMajorVersionUpgradeInitdbDone();

  Status WaitForYsqlMajorVersionUpgradeInitdb();

  Status RollbackYsqlMajorVersionUpgrade();

  // Set WAL retention time in secs for a table name.
  Status SetWalRetentionSecs(
    const client::YBTableName& table_name, const uint32_t wal_ret_secs);

  Status GetWalRetentionSecs(const client::YBTableName& table_name);

  Status GetAutoFlagsConfig();

  Status PromoteAutoFlags(
      const std::string& max_flag_class, const bool promote_non_runtime_flags, const bool force);

  Status RollbackAutoFlags(uint32_t rollback_version);

  Status PromoteSingleAutoFlag(const std::string& process_name, const std::string& flag_name);
  Status DemoteSingleAutoFlag(const std::string& process_name, const std::string& flag_name);

  Status ListAllNamespaces(bool include_nonrunning = false);

  // Snapshot operations.
  Result<master::ListSnapshotsResponsePB> ListSnapshots(const ListSnapshotsFlags& flags);
  Status CreateSnapshot(const std::vector<client::YBTableName>& tables,
                        std::optional<int32_t> retention_duration_hours,
                        const bool add_indexes = true,
                        const int flush_timeout_secs = 0);
  Status CreateNamespaceSnapshot(
      const TypedNamespaceName& ns, std::optional<int32_t> retention_duration_hours,
      bool add_indexes = true);
  Result<master::ListSnapshotRestorationsResponsePB> ListSnapshotRestorations(
      const TxnSnapshotRestorationId& restoration_id);
  Result<rapidjson::Document> CreateSnapshotSchedule(const client::YBTableName& keyspace,
                                                     MonoDelta interval, MonoDelta retention);
  Result<rapidjson::Document> ListSnapshotSchedules(const SnapshotScheduleId& schedule_id);
  Result<rapidjson::Document> DeleteSnapshotSchedule(const SnapshotScheduleId& schedule_id);
  Result<rapidjson::Document> RestoreSnapshotSchedule(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at);
  Result<rapidjson::Document> CloneNamespace(
      const TypedNamespaceName& source_namespace, const std::string& target_namespace_name,
      HybridTime restore_at);
  Result<rapidjson::Document> ListClones(
      const NamespaceId& source_namespace_id, std::optional<uint32_t> seq_no);
  Status RestoreSnapshot(const std::string& snapshot_id, HybridTime timestamp);

  Result<rapidjson::Document> EditSnapshotSchedule(
      const SnapshotScheduleId& schedule_id,
      std::optional<MonoDelta> new_interval,
      std::optional<MonoDelta> new_retention);

  Status DeleteSnapshot(const std::string& snapshot_id);
  Status AbortSnapshotRestore(const TxnSnapshotRestorationId& restoration_id);
  Status CreateSnapshotMetaFile(const std::string& snapshot_id, const std::string& file_name);

  Status ImportSnapshotMetaFile(
      const std::string& file_name, const TypedNamespaceName& keyspace,
      const std::vector<client::YBTableName>& tables, bool selective_import);
  Status ListReplicaTypeCounts(const client::YBTableName& table_name);

  Status SetPreferredZones(const std::vector<std::string>& preferred_zones);

  Status RotateUniverseKey(const std::string& key_path);

  Status DisableEncryption();

  Status IsEncryptionEnabled();

  Status AddUniverseKeyToAllMasters(
      const std::string& key_id, const std::string& universe_key);

  Status AllMastersHaveUniverseKeyInMemory(const std::string& key_id);

  Status RotateUniverseKeyInMemory(const std::string& key_id);

  Status DisableEncryptionInMemory();

  Status WriteUniverseKeyToFile(const std::string& key_id, const std::string& file_name);

  Status CreateCDCSDKDBStream(
      const TypedNamespaceName& ns, const std::string& CheckPointType,
      const cdc::CDCRecordType RecordType,
      const std::string& ConsistentSnapshotOption);

  Status DeleteCDCStream(const std::string& stream_id, bool force_delete = false);

  Status DeleteCDCSDKDBStream(const std::string& db_stream_id);

  Status ListCDCStreams(const TableId& table_id);

  Status ListCDCSDKStreams(const std::string& namespace_name);

  Status GetCDCDBStreamInfo(const std::string& db_stream_id);

  Status YsqlBackfillReplicationSlotNameToCDCSDKStream(
      const std::string& stream_id, const std::string& replication_slot_name);

  Status SetupNamespaceReplicationWithBootstrap(const std::string& replication_id,
                                  const std::vector<std::string>& producer_addresses,
                                  const TypedNamespaceName& ns,
                                  bool transactional);

  Status SetupUniverseReplication(const std::string& replication_group_id,
                                  const std::vector<std::string>& producer_addresses,
                                  const std::vector<TableId>& tables,
                                  const std::vector<std::string>& producer_bootstrap_ids,
                                  bool transactional);

  Status DeleteUniverseReplication(const std::string& replication_group_id,
                                   bool ignore_errors = false);

  Status AlterUniverseReplication(
      const std::string& replication_group_id, const std::vector<std::string>& producer_addresses,
      const std::vector<TableId>& add_tables, const std::vector<TableId>& remove_tables,
      const std::vector<std::string>& producer_bootstrap_ids_to_add,
      const std::string& new_replication_group_id, const NamespaceId& source_namespace_to_remove,
      bool remove_table_ignore_errors = false);

  Status RenameUniverseReplication(const std::string& old_universe_name,
                                   const std::string& new_universe_name);

  Status WaitForReplicationBootstrapToFinish(const std::string& replication_id);

  Status WaitForSetupUniverseReplicationToFinish(const std::string& replication_group_id);

  Status SetUniverseReplicationEnabled(const std::string& replication_group_id,
                                       bool is_enabled);

  Status PauseResumeXClusterProducerStreams(
      const std::vector<std::string>& stream_ids, bool is_paused);

  Status BootstrapProducer(const std::vector<TableId>& table_id);

  Status WaitForReplicationDrain(
      const std::vector<xrepl::StreamId>& stream_ids, const std::string& target_time);

  Status SetupNSUniverseReplication(const std::string& replication_group_id,
                                    const std::vector<std::string>& producer_addresses,
                                    const TypedNamespaceName& producer_namespace);

  Status GetReplicationInfo(const std::string& replication_group_id);

  Result<rapidjson::Document> GetXClusterSafeTime(bool include_lag_and_skew = false);

  Result<bool> IsXClusterBootstrapRequired(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId namespace_id);

  Status WaitForCreateXClusterReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  Status WaitForAlterXClusterReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  client::XClusterClient XClusterClient();

  Status RepairOutboundXClusterReplicationGroupAddTable(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id,
      const xrepl::StreamId& stream_id);

  Status RepairOutboundXClusterReplicationGroupRemoveTable(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id);

  using NamespaceMap = std::unordered_map<NamespaceId, client::NamespaceInfo>;
  Result<const NamespaceMap&> GetNamespaceMap(bool include_nonrunning = false);

 protected:
  // Fetch the locations of the replicas for a given tablet from the Master.
  Status GetTabletLocations(const TabletId& tablet_id,
                            master::TabletLocationsPB* locations);

  // Fetch information about the location of a tablet peer from the leader master.
  Status GetTabletPeer(
      const TabletId& tablet_id,
      PeerMode mode,
      master::TSInfoPB* ts_info);

  // Set the uuid and the socket information for a peer of this tablet.
  Status SetTabletPeerInfo(
      const TabletId& tablet_id,
      PeerMode mode,
      PeerId* peer_uuid,
      HostPort* peer_addr);

  // Fetch the latest list of tablet servers from the Master.
  Status ListTabletServers(
      google::protobuf::RepeatedPtrField<master::ListTabletServersResponsePB_Entry>* servers);

  // Look up the RPC address of the server with the specified UUID from the Master.
  Result<HostPort> GetFirstRpcAddressForTS(const std::string& uuid);

  // Step down the leader of this tablet.
  // If leader_uuid is empty, look it up with the master.
  // If leader_uuid is not empty, must provide a leader_proxy.
  // If new_leader_uuid is not empty, it is used as a suggestion for the StepDown operation.
  Status LeaderStepDown(
      const PeerId& leader_uuid,
      const TabletId& tablet_id,
      const PeerId& new_leader_uuid,
      std::unique_ptr<consensus::ConsensusServiceProxy>* leader_proxy);

  Status StartElection(const std::string& tablet_id);

  Status WaitUntilMasterLeaderReady();

  template <class Resp, class F>
  Status RequestMasterLeader(Resp* resp, const F& f, const MonoDelta& timeout = MonoDelta::kZero) {
    const MonoDelta local_timeout = (timeout == MonoDelta::kZero ? timeout_ : timeout);

    auto deadline = CoarseMonoClock::now() + local_timeout;
    rpc::RpcController rpc;
    rpc.set_timeout(local_timeout);
    for (;;) {
      resp->Clear();
      RETURN_NOT_OK(f(&rpc));

      auto status = ResponseStatus(*resp);
      if (status.ok()) {
        return Status::OK();
      }

      if (!status.IsLeaderHasNoLease() && !status.IsLeaderNotReadyToServe() &&
          !status.IsServiceUnavailable()) {
        return status;
      }

      auto timeout = deadline - CoarseMonoClock::now();
      if (timeout <= MonoDelta::kZero) {
        return status;
      }

      rpc.Reset();
      rpc.set_timeout(timeout);
      ResetMasterProxy();
    }
  }

  void ResetMasterProxy(const HostPort& leader_addr = HostPort());

  std::string master_addr_list_;
  HostPort init_master_addr_;
  const MonoDelta timeout_;
  HostPort leader_addr_;
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<master::MasterAdminProxy> master_admin_proxy_;
  std::unique_ptr<master::MasterBackupProxy> master_backup_proxy_;
  std::unique_ptr<master::MasterClientProxy> master_client_proxy_;
  std::unique_ptr<master::MasterClusterProxy> master_cluster_proxy_;
  std::unique_ptr<master::MasterDdlProxy> master_ddl_proxy_;
  std::unique_ptr<master::MasterEncryptionProxy> master_encryption_proxy_;
  std::unique_ptr<master::MasterReplicationProxy> master_replication_proxy_;
  std::unique_ptr<master::MasterTestProxy> master_test_proxy_;

  // Skip yb_client_ and related fields' initialization.
  std::unique_ptr<client::YBClient> yb_client_;
  bool initted_ = false;

 private:

  Status DiscoverAllMasters(
    const HostPort& init_master_addr, std::string* all_master_addrs);

  // Parses a placement info string of the form
  // "cloud1.region1.zone1[:min_num_replicas],cloud2.region2.zone2[:min_num_replicas],..."
  // and puts the result in placement_info_pb. If no RF is specified for a placement block, a
  // default of 1 is used. This function does not validate correctness; that is done in
  // CatalogManagerUtil::IsPlacementInfoValid.
  Status FillPlacementInfo(
      master::PlacementInfoPB* placement_info_pb, const std::string& placement_str);

  Result<int> GetReadReplicaConfigFromPlacementUuid(
      master::ReplicationInfoPB* replication_info, const std::string& placement_uuid);

  Result<master::GetMasterClusterConfigResponsePB> GetMasterClusterConfig();

  Result<master::GetMasterXClusterConfigResponsePB> GetMasterXClusterConfig();

  // Perform RPC call without checking Response structure for error
  template<class Response, class Request, class Object>
  Result<Response> InvokeRpcNoResponseCheck(
      Status (Object::*func)(const Request&, Response*, rpc::RpcController*) const,
      const Object& obj, const Request& req, const char* error_message = nullptr,
      const MonoDelta timeout = MonoDelta());

  // Perform RPC call by calling InvokeRpcNoResponseCheck
  // and check Response structure for error by using its has_error method (if any)
  template<class Response, class Request, class Object>
  Result<Response> InvokeRpc(
      Status (Object::*func)(const Request&, Response*, rpc::RpcController*) const,
      const Object& obj, const Request& req, const char* error_message = nullptr,
      const MonoDelta timeout = MonoDelta());

  Result<TxnSnapshotId> SuitableSnapshotId(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at, CoarseTimePoint deadline);

  Status SendEncryptionRequest(const std::string& key_path, bool enable_encryption);

  Result<HostPort> GetFirstRpcAddressForTS();

  void CleanupEnvironmentOnSetupUniverseReplicationFailure(
    const std::string& replication_group_id, const Status& failure_status);

  Status DisableTabletSplitsDuringRestore(CoarseTimePoint deadline);

  Result<rapidjson::Document> RestoreSnapshotScheduleDeprecated(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at);

  std::string GetDBTypeName(const master::SysNamespaceEntryPB& pb);
  // Map: Old name -> New name.
  typedef std::unordered_map<NamespaceName, NamespaceName> NSNameToNameMap;
  Status UpdateUDTypes(
      QLTypePB* pb_type, bool* update_meta, const NSNameToNameMap& ns_name_to_name);

  Status ProcessSnapshotInfoPBFile(const std::string& file_name, const TypedNamespaceName& keyspace,
      ImportSnapshotTableFilter *table_filter);

  NamespaceMap namespace_map_;

  DISALLOW_COPY_AND_ASSIGN(ClusterAdminClient);
};

static constexpr const char* kColumnSep = " \t";

std::string RightPadToUuidWidth(const std::string &s);

Result<TypedNamespaceName> ParseNamespaceName(
    const std::string& full_namespace_name,
    const YQLDatabase default_if_no_prefix = YQL_DATABASE_CQL);

void AddStringField(
    const char* name, const std::string& value, rapidjson::Value* out,
    rapidjson::Value::AllocatorType* allocator);

// Renders hybrid time to string for user, time is rendered in local TZ.
std::string HybridTimeToString(HybridTime ht);

}  // namespace tools
}  // namespace yb
