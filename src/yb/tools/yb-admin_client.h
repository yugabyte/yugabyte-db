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
#ifndef YB_TOOLS_YB_ADMIN_CLIENT_H
#define YB_TOOLS_YB_ADMIN_CLIENT_H

#include <string>
#include <vector>

#include <boost/optional.hpp>

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
}

namespace tools {

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

  // Set WAL retention time in secs for a table name.
  Status SetWalRetentionSecs(
    const client::YBTableName& table_name, const uint32_t wal_ret_secs);

  Status GetWalRetentionSecs(const client::YBTableName& table_name);

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

  Result<master::DisableTabletSplittingResponsePB> DisableTabletSplitsInternal(
      int64_t disable_duration_ms, const std::string& feature_name);

  Result<master::IsTabletSplittingCompleteResponsePB> IsTabletSplittingCompleteInternal(
      bool wait_for_parent_deletion);

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

  // Skip yb_client_ and related fields' initialization.
  std::unique_ptr<client::YBClient> yb_client_;
  bool initted_ = false;

 private:

  Status DiscoverAllMasters(
    const HostPort& init_master_addr, std::string* all_master_addrs);

  Status FillPlacementInfo(
      master::PlacementInfoPB* placement_info_pb, const std::string& placement_str);

  Result<int> GetReadReplicaConfigFromPlacementUuid(
      master::ReplicationInfoPB* replication_info, const std::string& placement_uuid);

  Result<master::GetMasterClusterConfigResponsePB> GetMasterClusterConfig();

  // Perform RPC call without checking Response structure for error
  template<class Response, class Request, class Object>
  Result<Response> InvokeRpcNoResponseCheck(
      Status (Object::*func)(const Request&, Response*, rpc::RpcController*) const,
      const Object& obj, const Request& req, const char* error_message = nullptr);

  // Perform RPC call by calling InvokeRpcNoResponseCheck
  // and check Response structure for error by using its has_error method (if any)
  template<class Response, class Request, class Object>
  Result<Response> InvokeRpc(
      Status (Object::*func)(const Request&, Response*, rpc::RpcController*) const,
      const Object& obj, const Request& req, const char* error_message = nullptr);

 private:
  using NamespaceMap = std::unordered_map<NamespaceId, master::NamespaceIdentifierPB>;
  Result<const NamespaceMap&> GetNamespaceMap();

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

#endif // YB_TOOLS_YB_ADMIN_CLIENT_H
