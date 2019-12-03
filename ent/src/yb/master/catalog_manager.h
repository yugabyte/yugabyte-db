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

#ifndef ENT_SRC_YB_MASTER_CATALOG_MANAGER_H
#define ENT_SRC_YB_MASTER_CATALOG_MANAGER_H

#include "../../../../src/yb/master/catalog_manager.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/cdc_consumer_registry_service.h"

namespace yb {

class UniverseKeyRegistryPB;

namespace master {
namespace enterprise {

class CatalogManager : public yb::master::CatalogManager {
  typedef yb::master::CatalogManager super;
 public:
  explicit CatalogManager(yb::master::Master* master) : super(master) {}

  CHECKED_STATUS RunLoaders(int64_t term) override;

  // API to start a snapshot creation.
  CHECKED_STATUS CreateSnapshot(const CreateSnapshotRequestPB* req,
                                CreateSnapshotResponsePB* resp);

  // API to check if this snapshot creation operation has finished.
  CHECKED_STATUS IsSnapshotOpDone(const IsSnapshotOpDoneRequestPB* req,
                                  IsSnapshotOpDoneResponsePB* resp);

  // API to list all available snapshots.
  CHECKED_STATUS ListSnapshots(const ListSnapshotsRequestPB*,
                               ListSnapshotsResponsePB* resp);

  // API to restore a snapshot.
  CHECKED_STATUS RestoreSnapshot(const RestoreSnapshotRequestPB* req,
                                 RestoreSnapshotResponsePB* resp);

  // API to delete a snapshot.
  CHECKED_STATUS DeleteSnapshot(const DeleteSnapshotRequestPB* req,
                                DeleteSnapshotResponsePB* resp);

  CHECKED_STATUS ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                    ImportSnapshotMetaResponsePB* resp);

  CHECKED_STATUS ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                      ChangeEncryptionInfoResponsePB* resp) override;

  CHECKED_STATUS InitCDCConsumer(const std::vector<CDCConsumerStreamInfo>& consumer_info,
                                 const std::string& master_addrs,
                                 const std::string& producer_universe_uuid);

  void HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error);

  void HandleRestoreTabletSnapshotResponse(TabletInfo *tablet, bool error);

  void HandleDeleteTabletSnapshotResponse(SnapshotId snapshot_id, TabletInfo *tablet, bool error);

  void DumpState(std::ostream* out, bool on_disk_dump = false) const override;

  CHECKED_STATUS CheckValidReplicationInfo(const ReplicationInfoPB& replication_info,
                                           const TSDescriptorVector& all_ts_descs,
                                           const vector<Partition>& partitions,
                                           CreateTableResponsePB* resp) override;

  CHECKED_STATUS HandlePlacementUsingReplicationInfo(const ReplicationInfoPB& replication_info,
                                                     const TSDescriptorVector& all_ts_descs,
                                                     consensus::RaftConfigPB* config) override;

  // Populates ts_descs with all tservers belonging to a certain placement.
  void GetTsDescsFromPlacementInfo(const PlacementInfoPB& placement_info,
                                   const TSDescriptorVector& all_ts_descs,
                                   TSDescriptorVector* ts_descs);

  // Fills the heartbeat response with the decrypted universe key registry.
  CHECKED_STATUS FillHeartbeatResponse(const TSHeartbeatRequestPB* req,
                                       TSHeartbeatResponsePB* resp) override;

  // Is encryption at rest enabled for this cluster.
  CHECKED_STATUS IsEncryptionEnabled(const IsEncryptionEnabledRequestPB* req,
                                     IsEncryptionEnabledResponsePB* resp);

  // Create a new CDC stream with the specified attributes.
  CHECKED_STATUS CreateCDCStream(const CreateCDCStreamRequestPB* req,
                                 CreateCDCStreamResponsePB* resp,
                                 rpc::RpcContext* rpc);

  // Delete the specified CDCStream.
  CHECKED_STATUS DeleteCDCStream(const DeleteCDCStreamRequestPB* req,
                                 DeleteCDCStreamResponsePB* resp,
                                 rpc::RpcContext* rpc);

  // List CDC streams (optionally, for a given table).
  CHECKED_STATUS ListCDCStreams(const ListCDCStreamsRequestPB* req,
                                ListCDCStreamsResponsePB* resp);

  // Get CDC stream.
  CHECKED_STATUS GetCDCStream(const GetCDCStreamRequestPB* req,
                              GetCDCStreamResponsePB* resp,
                              rpc::RpcContext* rpc);

  // Delete CDC streams for a table.
  CHECKED_STATUS DeleteCDCStreamsForTable(const TableId& table_id) override;
  CHECKED_STATUS DeleteCDCStreamsForTables(const vector<TableId>& table_ids) override;

  // Setup Universe Replication to consume data from another YB universe.
  CHECKED_STATUS SetupUniverseReplication(const SetupUniverseReplicationRequestPB* req,
                                          SetupUniverseReplicationResponsePB* resp,
                                          rpc::RpcContext* rpc);

  // Delete Universe Replication.
  CHECKED_STATUS DeleteUniverseReplication(const DeleteUniverseReplicationRequestPB* req,
                                           DeleteUniverseReplicationResponsePB* resp,
                                           rpc::RpcContext* rpc);

  // Enable/Disable an Existing Universe Replication.
  CHECKED_STATUS SetUniverseReplicationEnabled(const SetUniverseReplicationEnabledRequestPB* req,
                                               SetUniverseReplicationEnabledResponsePB* resp,
                                               rpc::RpcContext* rpc);

  // Get Universe Replication.
  CHECKED_STATUS GetUniverseReplication(const GetUniverseReplicationRequestPB* req,
                                        GetUniverseReplicationResponsePB* resp,
                                        rpc::RpcContext* rpc);

 private:
  friend class SnapshotLoader;
  friend class ClusterLoadBalancer;
  friend class CDCStreamLoader;
  friend class UniverseReplicationLoader;

  CHECKED_STATUS RestoreEntry(const SysRowEntry& entry, const SnapshotId& snapshot_id);

  // Per table structure for external cluster snapshot importing to this cluster.
  // Old IDs mean IDs on external cluster, new IDs - IDs on this cluster.
  struct ExternalTableSnapshotData {
    ExternalTableSnapshotData() : num_tablets(0), tablet_id_map(nullptr), table_meta(nullptr) {}

    NamespaceId old_namespace_id;
    TableId old_table_id;
    TableId new_table_id;
    int num_tablets;
    typedef std::pair<std::string, std::string> PartitionKeys;
    typedef std::map<PartitionKeys, TabletId> PartitionToIdMap;
    PartitionToIdMap new_tablets_map;
    // Mapping: Old tablet ID -> New tablet ID.
    google::protobuf::RepeatedPtrField<IdPairPB>* tablet_id_map;

    ImportSnapshotMetaResponsePB_TableMetaPB* table_meta;
  };

  // Map: old_namespace_id (key) -> new_namespace_id (value).
  typedef std::map<NamespaceId, NamespaceId> NamespaceMap;
  typedef std::map<TableId, ExternalTableSnapshotData> ExternalTableSnapshotDataMap;

  CHECKED_STATUS ImportNamespaceEntry(const SysRowEntry& entry, NamespaceMap* ns_map);
  CHECKED_STATUS ImportTableEntry(
      const SysRowEntry& entry, const NamespaceMap& ns_map, ExternalTableSnapshotData* s_data);
  CHECKED_STATUS PreprocessTabletEntry(
      const SysRowEntry& entry, ExternalTableSnapshotDataMap* table_map);
  CHECKED_STATUS ImportTabletEntry(
      const SysRowEntry& entry, ExternalTableSnapshotDataMap* table_map);

  void SendCreateTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                       const std::string& snapshot_id);

  void SendRestoreTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                        const std::string& snapshot_id);

  void SendDeleteTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                       const std::string& snapshot_id);

  static void SetTabletSnapshotsState(SysSnapshotEntryPB::State state,
                                      SysSnapshotEntryPB* snapshot_pb);

  // Create the cdc_state table if needed (i.e. if it does not exist already).
  //
  // This is called at the end of CreateCDCStream.
  CHECKED_STATUS CreateCdcStateTableIfNeeded(rpc::RpcContext *rpc);

  // Check if cdc_state table creation is done.
  CHECKED_STATUS IsCdcStateTableCreated(IsCreateTableDoneResponsePB* resp);

  // Return all CDC streams.
  void GetAllCDCStreams(std::vector<scoped_refptr<CDCStreamInfo>>* streams);

  // Delete specified CDC streams.
  CHECKED_STATUS DeleteCDCStreams(const std::vector<scoped_refptr<CDCStreamInfo>>& streams);

  // Find CDC streams for a table.
  std::vector<scoped_refptr<CDCStreamInfo>> FindCDCStreamsForTable(const TableId& table_id);

  bool CDCStreamExistsUnlocked(const CDCStreamId& stream_id) override;

  CHECKED_STATUS FillHeartbeatResponseEncryption(const SysClusterConfigEntryPB& cluster_config,
                                                 const TSHeartbeatRequestPB* req,
                                                 TSHeartbeatResponsePB* resp);

  CHECKED_STATUS FillHeartbeatResponseCDC(const SysClusterConfigEntryPB& cluster_config,
                                          const TSHeartbeatRequestPB* req,
                                          TSHeartbeatResponsePB* resp);

  template <class Collection>
  typename Collection::value_type::second_type LockAndFindPtrOrNull(
      const Collection& collection, const typename Collection::value_type::first_type& key) {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");
    return FindPtrOrNull(collection, key);
  }

  scoped_refptr<ClusterConfigInfo> GetClusterConfigInfo() const {
    return cluster_config_;
  }

  void GetTableSchemaCallback(
      const std::string& universe_id, const std::shared_ptr<client::YBTableInfo>& info,
      const Status& s);
  void CreateCDCStreamCallback(const std::string& universe_id, const TableId& table,
                               const Result<CDCStreamId>& stream_id);

  void DeleteUniverseReplicationUnlocked(scoped_refptr<UniverseReplicationInfo> info);
  void MarkUniverseReplicationFailed(scoped_refptr<UniverseReplicationInfo> universe);

  // Snapshot map: snapshot-id -> SnapshotInfo.
  typedef std::unordered_map<SnapshotId, scoped_refptr<SnapshotInfo> > SnapshotInfoMap;
  SnapshotInfoMap snapshot_ids_map_;
  SnapshotId current_snapshot_id_;

  // mutex on should_send_universe_key_registry_mutex_.
  mutable simple_spinlock should_send_universe_key_registry_mutex_;
  // Should catalog manager resend latest universe key registry to tserver.
  std::unordered_map<TabletServerId, bool> should_send_universe_key_registry_
  GUARDED_BY(should_send_universe_key_registry_mutex_);

  // CDC Stream map: CDCStreamId -> CDCStreamInfo.
  typedef std::unordered_map<CDCStreamId, scoped_refptr<CDCStreamInfo>> CDCStreamInfoMap;
  CDCStreamInfoMap cdc_stream_map_;

  typedef std::unordered_map<std::string, scoped_refptr<UniverseReplicationInfo>>
      UniverseReplicationInfoMap;
  UniverseReplicationInfoMap universe_replication_map_;

  // mutex on should_send_consumer_registry_mutex_.
  mutable simple_spinlock should_send_consumer_registry_mutex_;
  // Should catalog manager resend latest consumer registry to tserver.
  std::unordered_map<TabletServerId, bool> should_send_consumer_registry_
  GUARDED_BY(should_send_consumer_registry_mutex_);

  DISALLOW_COPY_AND_ASSIGN(CatalogManager);
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CATALOG_MANAGER_H
