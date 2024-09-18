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

#include <memory>

#include "yb/master/master_fwd.h"
#include "yb/master/xcluster/master_xcluster_types.h"

#include "yb/util/status_fwd.h"

namespace yb {

class HybridTime;
class IsOperationDoneResult;
class JsonWriter;

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace xcluster {
YB_STRONGLY_TYPED_STRING(ReplicationGroupId);
}

namespace master {

class GetXClusterSafeTimeRequestPB;
class GetXClusterSafeTimeResponsePB;
struct LeaderEpoch;
class XClusterConsumerReplicationStatusPB;
struct XClusterStatus;

class XClusterManagerIf {
 public:
  virtual Result<HybridTime> GetXClusterSafeTime(const NamespaceId& namespace_id) const = 0;
  virtual Status RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch) = 0;
  virtual Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap() const = 0;
  virtual Status SetXClusterNamespaceToSafeTimeMap(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) = 0;
  virtual Result<HybridTime> GetXClusterSafeTimeForNamespace(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const XClusterSafeTimeFilter& filter) = 0;
  virtual Status MarkIndexBackfillCompleted(
      const std::unordered_set<TableId>& index_ids, const LeaderEpoch& epoch) = 0;

  virtual Result<XClusterStatus> GetXClusterStatus() const = 0;
  virtual Status PopulateXClusterStatusJson(JsonWriter& jw) const = 0;

  virtual void RunBgTasks(const LeaderEpoch& epoch) = 0;

  virtual std::unordered_set<xcluster::ReplicationGroupId>
  GetInboundTransactionalReplicationGroups() const = 0;

  virtual Status ClearXClusterSourceTableId(TableInfoPtr table_info, const LeaderEpoch& epoch) = 0;

  virtual void NotifyAutoFlagsConfigChanged() = 0;

  virtual void StoreConsumerReplicationStatus(
      const XClusterConsumerReplicationStatusPB& consumer_replication_status) = 0;

  virtual void SyncConsumerReplicationStatusMap(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::Map<std::string, cdc::ProducerEntryPB>& producer_map) = 0;

  virtual Result<bool> HasReplicationGroupErrors(
      const xcluster::ReplicationGroupId& replication_group_id) = 0;

  virtual bool IsTableReplicationConsumer(const TableId& table_id) const = 0;

  virtual void RemoveTableConsumerStreams(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::set<TableId>& tables_to_clear) = 0;

  virtual Status HandleTabletSplit(
      const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids,
      const LeaderEpoch& epoch) = 0;

  virtual void RemoveTableConsumerStream(
      const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id) = 0;

  virtual Status AlterUniverseReplication(
      const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch) = 0;

  virtual Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id, bool skip_health_check) = 0;

 protected:
  virtual ~XClusterManagerIf() = default;
};

}  // namespace master
}  // namespace yb
