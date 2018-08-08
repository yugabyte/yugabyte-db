// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_CATALOG_MANAGER_H
#define ENT_SRC_YB_MASTER_CATALOG_MANAGER_H

#include "../../../../src/yb/master/catalog_manager.h"

#include "yb/master/master_backup.pb.h"

namespace yb {
namespace master {

// The data related to a snapshot which is persisted on disk.
// This portion of SnapshotInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentSnapshotInfo : public Persistent<SysSnapshotEntryPB, SysRowEntry::SNAPSHOT> {
  SysSnapshotEntryPB::State state() const {
    return pb.state();
  }

  const std::string& state_name() const {
    return SysSnapshotEntryPB::State_Name(state());
  }

  bool is_creating() const {
    return state() == SysSnapshotEntryPB::CREATING;
  }

  bool started_deleting() const {
    return state() == SysSnapshotEntryPB::DELETING ||
           state() == SysSnapshotEntryPB::DELETED;
  }

  bool is_failed() const {
    return state() == SysSnapshotEntryPB::FAILED;
  }

  bool is_cancelled() const {
    return state() == SysSnapshotEntryPB::CANCELLED;
  }

  bool is_complete() const {
    return state() == SysSnapshotEntryPB::COMPLETE;
  }

  bool is_restoring() const {
    return state() == SysSnapshotEntryPB::RESTORING;
  }

  bool is_deleting() const {
    return state() == SysSnapshotEntryPB::DELETING;
  }
};

// The information about a snapshot.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
class SnapshotInfo : public RefCountedThreadSafe<SnapshotInfo>,
                     public MetadataCowWrapper<PersistentSnapshotInfo> {
 public:
  explicit SnapshotInfo(SnapshotId id);

  virtual const std::string& id() const override { return snapshot_id_; };

  SysSnapshotEntryPB::State state() const;

  const std::string& state_name() const;

  std::string ToString() const override;

  // Returns true if the snapshot creation is in-progress.
  bool IsCreateInProgress() const;

  // Returns true if the snapshot restoring is in-progress.
  bool IsRestoreInProgress() const;

  // Returns true if the snapshot deleting is in-progress.
  bool IsDeleteInProgress() const;

  CHECKED_STATUS AddEntries(const scoped_refptr<NamespaceInfo> ns,
                            const scoped_refptr<TableInfo>& table,
                            const std::vector<scoped_refptr<TabletInfo> >& tablets);

 private:
  friend class RefCountedThreadSafe<SnapshotInfo>;
  ~SnapshotInfo() = default;

  // The ID field is used in the sys_catalog table.
  const SnapshotId snapshot_id_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotInfo);
};

namespace enterprise {

class CatalogManager : public yb::master::CatalogManager {
  typedef yb::master::CatalogManager super;
 public:
  explicit CatalogManager(yb::master::Master* master) : super(master) {}

  CHECKED_STATUS RunLoaders() override;

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

 private:
  friend class SnapshotLoader;
  friend class ClusterLoadBalancer;

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

  // Snapshot map: snapshot-id -> SnapshotInfo
  typedef std::unordered_map<SnapshotId, scoped_refptr<SnapshotInfo> > SnapshotInfoMap;
  SnapshotInfoMap snapshot_ids_map_;
  SnapshotId current_snapshot_id_;

  DISALLOW_COPY_AND_ASSIGN(CatalogManager);
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CATALOG_MANAGER_H
