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

#include <string>
#include <google/protobuf/repeated_field.h>

#include "yb/cdc/xrepl_types.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/client/client_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/schema.h"
#include "yb/util/net/net_util.h"
#include "yb/util/strongly_typed_uuid.h"

namespace yb {

YB_STRONGLY_TYPED_UUID_DECL(UniverseUuid);

class IsOperationDoneResult;

namespace master {
class CDCStreamOptionsPB;
class MasterReplicationProxy;
class GetXClusterStreamsResponsePB;
}  // namespace master

namespace rpc {
class Messenger;
class ProxyCache;
class SecureContext;
}  // namespace rpc

namespace client {
class YBClient;
class XClusterClient;

using GetXClusterStreamsCallback =
    std::function<void(Result<master::GetXClusterStreamsResponsePB>)>;
using IsXClusterBootstrapRequiredCallback = std::function<void(Result<bool>)>;

// This class creates and holds a dedicated YbClient, XClusterClient and their dependant objects
// messenger and secure context. The client connects to a remote yb xCluster universe.
class XClusterRemoteClientHolder {
 public:
  static constexpr auto kClientName = "XClusterRemote";

  static Result<std::shared_ptr<XClusterRemoteClientHolder>> Create(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<HostPort>& remote_masters);

  virtual ~XClusterRemoteClientHolder();

  virtual void Shutdown();

  Status SetMasterAddresses(const std::vector<HostPort>& remote_masters);

  Status ReloadCertificates();

  XClusterClient& GetXClusterClient();
  client::YBClient& GetYbClient();

 private:
  friend class MockXClusterRemoteClientHolder;

  explicit XClusterRemoteClientHolder(const xcluster::ReplicationGroupId& replication_group_id);
  Status Init(const std::vector<HostPort>& remote_masters);

  const xcluster::ReplicationGroupId replication_group_id_;
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;

  std::unique_ptr<client::YBClient> yb_client_;
  std::unique_ptr<client::XClusterClient> xcluster_client_;

  DISALLOW_COPY_AND_ASSIGN(XClusterRemoteClientHolder);
};

// A wrapper over YBClient to handle xCluster related RPCs.
// This class performs serialization of C++ objects to PBs and vice versa.
class XClusterClient {
 public:
  explicit XClusterClient(client::YBClient& yb_client);
  virtual ~XClusterClient() = default;

  // Starts the creation of the outbound replication group on the source. IsBootstrapRequired or
  // GetXClusterStreams must be called on each namespace in order to wait for the operation to
  // complete.
  Status CreateOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<NamespaceId>& namespace_ids, bool automatic_ddl_mode = false);

  Status IsBootstrapRequired(
      CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& namespace_id, IsXClusterBootstrapRequiredCallback callback);

  // Count of table_names and pg_schema_names must match. If no table_names are provided then all
  // tables of the namespace are returned.
  Status GetXClusterStreams(
      CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& namespace_id, const std::vector<TableName>& table_names,
      const std::vector<PgSchemaName>& pg_schema_names, GetXClusterStreamsCallback callback);
  // If source_table_ids is not provided, then all tables for the namespace are returned.
  Status GetXClusterStreams(
      CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& namespace_id, const std::vector<TableName>& source_table_ids,
      GetXClusterStreamsCallback callback);

  // Starts the creation of Db scoped inbound replication group from a outbound replication group.
  // IsCreateXClusterReplicationDone must be called in order to wait for the operation to complete.
  virtual Status CreateXClusterReplicationFromCheckpoint(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  virtual Result<IsOperationDoneResult> IsCreateXClusterReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  // target_master_addresses is optional. If set the Inbound replication group on the target will be
  // deleted. If not set the target has to be separately deled with DeleteUniverseReplication.
  Status DeleteOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  Status DeleteUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id, bool ignore_errors,
      const UniverseUuid& target_universe_uuid);

  // Starts the checkpointing of the given namespace. IsBootstrapRequired or
  // GetXClusterStreams must be called on each namespace in order to wait for the operation to
  // complete.
  Status AddNamespaceToOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id);

  // Starts the addition of namespace to inbound replication group.
  // IsAlterXClusterReplicationDone must be called in order to wait for the operation to complete.
  Status AddNamespaceToXClusterReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses, const NamespaceId& source_namespace_id);

  Result<IsOperationDoneResult> IsAlterXClusterReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  // target_master_addresses is optional. If set the Inbound replication group on the target will be
  // deleted. If not set the target has to be separately deled with
  // RemoveNamespaceFromUniverseReplication.
  Status RemoveNamespaceFromOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      const std::string& target_master_addresses);

  Status RemoveNamespaceFromUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& source_namespace_id, const UniverseUuid& target_universe_uuid);

  Status RepairOutboundXClusterReplicationGroupAddTable(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id,
      const xrepl::StreamId& stream_id);

  Status RepairOutboundXClusterReplicationGroupRemoveTable(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id);

  Result<xrepl::StreamId> CreateXClusterStream(
      const TableId& table_id, bool active, cdc::StreamModeTransactional transactional);

  void CreateXClusterStreamAsync(
      const TableId& table_id, bool active, cdc::StreamModeTransactional transactional,
      CreateCDCStreamCallback callback);

  // Returns the outbound replication groups for the given namespace. If namespace_id is empty, then
  // all outbound replication groups are returned.
  Result<std::vector<xcluster::ReplicationGroupId>> GetXClusterOutboundReplicationGroups(
      const NamespaceId& namespace_id = {});

  Result<std::unordered_map<NamespaceId, std::unordered_map<TableId, xrepl::StreamId>>>
  GetXClusterOutboundReplicationGroupInfo(const xcluster::ReplicationGroupId& replication_group_id);

  // Returns list of all universe replication group ids if consumer_namespace_id is empty. If
  // consumer_namespace_id is not empty then returns DB scoped replication groups that contain the
  // namespace.
  Result<std::vector<xcluster::ReplicationGroupId>> GetUniverseReplications(
      const NamespaceId& consumer_namespace_id);

  struct XClusterInboundReplicationGroupInfo {
    XClusterReplicationType replication_type = XClusterReplicationType::XCLUSTER_NON_TRANSACTIONAL;
    std::string source_master_addrs;
    // Map of target namespace id to source namespace id. Only used in db scope replication.
    std::unordered_map<NamespaceId, NamespaceId> db_scope_namespace_id_map;
    bool automatic_ddl_mode = false;

    struct XClusterInboundReplicationGroupTableInfo {
      TableId target_table_id;
      TableId source_table_id;
      xrepl::StreamId stream_id = xrepl::StreamId::Nil();
    };
    std::vector<XClusterInboundReplicationGroupTableInfo> table_infos;
  };
  Result<XClusterInboundReplicationGroupInfo> GetUniverseReplicationInfo(
      const xcluster::ReplicationGroupId& replication_group_id);

  // This requires flag enable_xcluster_api_v2 to be set.
  virtual Result<UniverseUuid> SetupDbScopedUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<HostPort>& source_master_addresses,
      const std::vector<NamespaceName>& namespace_names,
      const std::vector<NamespaceId>& source_namespace_ids,
      const std::vector<TableId>& source_table_ids,
      const std::vector<xrepl::StreamId>& bootstrap_ids, bool automatic_ddl_mode);

  virtual Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id);

  Status GetXClusterTableCheckpointInfos(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      const std::vector<TableName>& table_names, const std::vector<PgSchemaName>& pg_schema_names,
      BootstrapProducerCallback user_callback);

  Status GetXClusterTableCheckpointInfos(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      const std::vector<TableId>& table_ids, BootstrapProducerCallback user_callback);

  virtual Status AddNamespaceToDbScopedUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const UniverseUuid& target_universe_uuid, const NamespaceName& namespace_name,
      const NamespaceId& source_namespace_id, const std::vector<TableId>& source_table_ids,
      const std::vector<xrepl::StreamId>& bootstrap_ids);

 private:
  CoarseTimePoint GetDeadline() const;

  template <typename ResponsePB, typename RequestPB, typename Method>
  Result<ResponsePB> SyncLeaderMasterRpc(
      const RequestPB& req, const char* method_name, const Method& method);

  client::YBClient& yb_client_;
};

google::protobuf::RepeatedPtrField<yb::master::CDCStreamOptionsPB> GetXClusterStreamOptions();

}  // namespace client
}  // namespace yb
