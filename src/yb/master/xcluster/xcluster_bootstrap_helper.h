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

#include "yb/cdc/xcluster_types.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/util/cow_object.h"
#include "yb/util/status_fwd.h"

namespace yb {

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace client {
class YBTableName;
}  // namespace client

namespace master {

class SetupNamespaceReplicationWithBootstrapRequestPB;
class SetupNamespaceReplicationWithBootstrapResponsePB;
class UniverseReplicationBootstrapInfo;
struct PersistentUniverseReplicationBootstrapInfo;

// NOTE:
// SetupUniverseReplicationWithBootstrap is currently not in use. There are no tests that cover
// this, so it should be assumed to have regressions. This code is just kept as a refences for
// future use. Use caution when copying and reusing part of this code.

class SetupUniverseReplicationWithBootstrapHelper
    : public RefCountedThreadSafe<SetupUniverseReplicationWithBootstrapHelper> {
 public:
  ~SetupUniverseReplicationWithBootstrapHelper();

  static Status SetupWithBootstrap(
      Master& master, CatalogManager& catalog_manager,
      const SetupNamespaceReplicationWithBootstrapRequestPB* req,
      SetupNamespaceReplicationWithBootstrapResponsePB* resp, const LeaderEpoch& epoch);

 private:
  SetupUniverseReplicationWithBootstrapHelper(
      Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch);

  Status SetupWithBootstrap(
      const SetupNamespaceReplicationWithBootstrapRequestPB* req,
      SetupNamespaceReplicationWithBootstrapResponsePB* resp);

  Result<scoped_refptr<UniverseReplicationBootstrapInfo>>
  CreateUniverseReplicationBootstrapInfoForProducer(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses, bool transactional);

  void MarkReplicationBootstrapFailed(
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info, const Status& failure_status);
  // Sets the appropriate failure state and the error status on the replication bootstrap and
  // commits the mutation to the sys catalog.
  void MarkReplicationBootstrapFailed(
      const Status& failure_status,
      CowWriteLock<PersistentUniverseReplicationBootstrapInfo>* bootstrap_info_lock,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  // Sets the appropriate state and on the replication bootstrap and commits the
  // mutation to the sys catalog.
  void SetReplicationBootstrapState(
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info,
      const SysUniverseReplicationBootstrapEntryPB::State& state);

  // SetupReplicationWithBootstrap
  Status ValidateReplicationBootstrapRequest(
      const SetupNamespaceReplicationWithBootstrapRequestPB* req);

  typedef std::unordered_map<TableId, xrepl::StreamId> TableBootstrapIdsMap;
  void DoReplicationBootstrap(
      const xcluster::ReplicationGroupId& replication_id,
      const std::vector<client::YBTableName>& tables,
      Result<TableBootstrapIdsMap> bootstrap_producer_result);

  Result<SnapshotInfoPB> DoReplicationBootstrapCreateSnapshot(
      const std::vector<client::YBTableName>& tables,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  Result<std::vector<ImportSnapshotMetaResponsePB_TableMetaPB>>
  DoReplicationBootstrapImportSnapshot(
      const SnapshotInfoPB& snapshot,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  Status DoReplicationBootstrapTransferAndRestoreSnapshot(
      const std::vector<ImportSnapshotMetaResponsePB_TableMetaPB>& tables_meta,
      scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;
  XClusterManager& xcluster_manager_;
  const LeaderEpoch epoch_;

  DISALLOW_COPY_AND_ASSIGN(SetupUniverseReplicationWithBootstrapHelper);
};

}  // namespace master

}  // namespace yb
