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

#ifndef ENT_SRC_YB_TOOLS_YB_ADMIN_CLIENT_H
#define ENT_SRC_YB_TOOLS_YB_ADMIN_CLIENT_H

#include "../../../../src/yb/tools/yb-admin_client.h"

#include "yb/common/snapshot.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/secure.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"

namespace yb {
namespace tools {
namespace enterprise {

// Flags for list_snapshot command.
YB_DEFINE_ENUM(ListSnapshotsFlag, (SHOW_DETAILS)(NOT_SHOW_RESTORED)(SHOW_DELETED)(JSON));
using ListSnapshotsFlags = EnumBitSet<ListSnapshotsFlag>;

class ClusterAdminClient : public yb::tools::ClusterAdminClient {
  typedef yb::tools::ClusterAdminClient super;
 public:
  ClusterAdminClient(std::string addrs, MonoDelta timeout)
      : super(std::move(addrs), timeout) {}

  ClusterAdminClient(const HostPort& init_master_addrs, MonoDelta timeout)
      : super(init_master_addrs, timeout) {}

  // Snapshot operations.
  CHECKED_STATUS ListSnapshots(const ListSnapshotsFlags& flags);
  CHECKED_STATUS CreateSnapshot(const std::vector<client::YBTableName>& tables,
                                const bool add_indexes = true,
                                const int flush_timeout_secs = 0);
  CHECKED_STATUS CreateNamespaceSnapshot(const TypedNamespaceName& ns);
  Result<rapidjson::Document> ListSnapshotRestorations(
      const TxnSnapshotRestorationId& restoration_id);
  Result<rapidjson::Document> CreateSnapshotSchedule(const client::YBTableName& keyspace,
                                                     MonoDelta interval, MonoDelta retention);
  Result<rapidjson::Document> ListSnapshotSchedules(const SnapshotScheduleId& schedule_id);
  Result<rapidjson::Document> DeleteSnapshotSchedule(const SnapshotScheduleId& schedule_id);
  Result<rapidjson::Document> RestoreSnapshotSchedule(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at);
  CHECKED_STATUS RestoreSnapshot(const std::string& snapshot_id,
                                 HybridTime timestamp);
  CHECKED_STATUS DeleteSnapshot(const std::string& snapshot_id);

  CHECKED_STATUS CreateSnapshotMetaFile(const std::string& snapshot_id,
                                        const std::string& file_name);
  CHECKED_STATUS ImportSnapshotMetaFile(const std::string& file_name,
                                        const TypedNamespaceName& keyspace,
                                        const std::vector<client::YBTableName>& tables);
  CHECKED_STATUS ListReplicaTypeCounts(const client::YBTableName& table_name);

  CHECKED_STATUS SetPreferredZones(const std::vector<string>& preferred_zones);

  CHECKED_STATUS RotateUniverseKey(const std::string& key_path);

  CHECKED_STATUS DisableEncryption();

  CHECKED_STATUS IsEncryptionEnabled();

  CHECKED_STATUS AddUniverseKeyToAllMasters(
      const std::string& key_id, const std::string& universe_key);

  CHECKED_STATUS AllMastersHaveUniverseKeyInMemory(const std::string& key_id);

  CHECKED_STATUS RotateUniverseKeyInMemory(const std::string& key_id);

  CHECKED_STATUS DisableEncryptionInMemory();

  CHECKED_STATUS WriteUniverseKeyToFile(const std::string& key_id, const std::string& file_name);

  CHECKED_STATUS CreateCDCStream(const TableId& table_id);

  CHECKED_STATUS DeleteCDCStream(const std::string& stream_id);

  CHECKED_STATUS ListCDCStreams(const TableId& table_id);

  CHECKED_STATUS SetupUniverseReplication(const std::string& producer_uuid,
                                          const std::vector<std::string>& producer_addresses,
                                          const std::vector<TableId>& tables,
                                          const std::vector<std::string>& producer_bootstrap_ids);

  CHECKED_STATUS DeleteUniverseReplication(const std::string& producer_id, bool force = false);

  CHECKED_STATUS AlterUniverseReplication(
      const std::string& producer_uuid,
      const std::vector<std::string>& producer_addresses,
      const std::vector<TableId>& add_tables,
      const std::vector<TableId>& remove_tables,
      const std::vector<std::string>& producer_bootstrap_ids_to_add,
      const std::string& new_producer_universe_id);

  CHECKED_STATUS RenameUniverseReplication(const std::string& old_universe_name,
                                           const std::string& new_universe_name);

  CHECKED_STATUS WaitForSetupUniverseReplicationToFinish(const string& producer_uuid);

  CHECKED_STATUS SetUniverseReplicationEnabled(const std::string& producer_id,
                                               bool is_enabled);

  CHECKED_STATUS BootstrapProducer(const std::vector<TableId>& table_id);

 private:
  Result<TxnSnapshotId> SuitableSnapshotId(
      const SnapshotScheduleId& schedule_id, HybridTime restore_at, CoarseTimePoint deadline);

  CHECKED_STATUS SendEncryptionRequest(const std::string& key_path, bool enable_encryption);

  Result<HostPort> GetFirstRpcAddressForTS();

  void CleanupEnvironmentOnSetupUniverseReplicationFailure(
    const std::string& producer_uuid, const Status& failure_status);

  DISALLOW_COPY_AND_ASSIGN(ClusterAdminClient);
};

}  // namespace enterprise
}  // namespace tools
}  // namespace yb

#endif // ENT_SRC_YB_TOOLS_YB_ADMIN_CLIENT_H
