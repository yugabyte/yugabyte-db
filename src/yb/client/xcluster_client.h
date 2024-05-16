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

using GetXClusterStreamsCallback =
    std::function<void(Result<master::GetXClusterStreamsResponsePB>)>;
using IsXClusterBootstrapRequiredCallback = std::function<void(Result<bool>)>;

// A wrapper over YBClient to handle xCluster related RPCs.
// This class performs serialization of C++ objects to PBs and vice versa. Which enables us to limit
// the scope of xCluster specific types.
class XClusterClient {
 public:
  explicit XClusterClient(client::YBClient& yb_client);
  virtual ~XClusterClient() = default;

  virtual Status CreateXClusterReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  virtual Result<bool> IsCreateXClusterReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& target_master_addresses);

  Result<std::vector<NamespaceId>> XClusterCreateOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<NamespaceName>& namespace_names);

  Status IsXClusterBootstrapRequired(
      CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& namespace_id, IsXClusterBootstrapRequiredCallback callback);

  Status XClusterDeleteOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id);

  Status GetXClusterStreams(
      CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& namespace_id, const std::vector<TableName>& table_names,
      const std::vector<PgSchemaName>& pg_schema_names, GetXClusterStreamsCallback callback);

 private:
  template <typename ResponsePB, typename RequestPB, typename Method>
  Result<ResponsePB> SyncLeaderMasterRpc(
      const RequestPB& req, const char* method_name, const Method& method);

  client::YBClient& yb_client_;
};

// A wrapper over YBClient to handle xCluster related RPCs sent to a different yb universe.
// This class performs serialization of C++ objects to PBs and vice versa.
class XClusterRemoteClient {
 public:
  XClusterRemoteClient(const std::string& certs_for_cdc_dir, MonoDelta timeout);
  virtual ~XClusterRemoteClient();

  virtual Status Init(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<HostPort>& remote_masters);

  YB_STRONGLY_TYPED_BOOL(Transactional);
  // This requires flag enable_xcluster_api_v2 to be set.
  virtual Result<UniverseUuid> SetupDbScopedUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<HostPort>& source_master_addresses,
      const std::vector<NamespaceName>& namespace_names,
      const std::vector<NamespaceId>& source_namespace_ids,
      const std::vector<TableId>& source_table_ids,
      const std::vector<xrepl::StreamId>& bootstrap_ids);

  virtual Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id);

  Status GetXClusterTableCheckpointInfos(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      const std::vector<TableName>& table_names, const std::vector<PgSchemaName>& pg_schema_names,
      BootstrapProducerCallback user_callback);

 private:
  template <typename ResponsePB, typename RequestPB, typename Method>
  Result<ResponsePB> SyncLeaderMasterRpc(
      const RequestPB& req, const char* method_name, const Method& method);

  const std::string certs_for_cdc_dir_;
  const MonoDelta timeout_;
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;

  std::unique_ptr<client::YBClient> yb_client_;
};

}  // namespace client
}  // namespace yb
