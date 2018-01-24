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
  bool is_creating() const {
    return pb.state() == SysSnapshotEntryPB::CREATING;
  }

  bool started_deleting() const {
    return pb.state() == SysSnapshotEntryPB::DELETING ||
           pb.state() == SysSnapshotEntryPB::DELETED;
  }

  bool is_failed() const {
    return pb.state() == SysSnapshotEntryPB::FAILED;
  }

  bool is_cancelled() const {
    return pb.state() == SysSnapshotEntryPB::CANCELLED;
  }

  bool is_complete() const {
    return pb.state() == SysSnapshotEntryPB::COMPLETE;
  }

  bool is_restoring() const {
    return pb.state() == SysSnapshotEntryPB::RESTORING;
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

  std::string ToString() const override;

  // Returns true if the snapshot creation is in-progress.
  bool IsCreateInProgress() const;

  // Returns true if the snapshot restoring is in-progress.
  bool IsRestoreInProgress() const;

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

  CHECKED_STATUS ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                    ImportSnapshotMetaResponsePB* resp);

  void HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error);

  void HandleRestoreTabletSnapshotResponse(TabletInfo *tablet, bool error);

  void DumpState(std::ostream* out, bool on_disk_dump = false) const override;

 private:
  friend class SnapshotLoader;

  CHECKED_STATUS RestoreEntry(const SysRowEntry& entry, const SnapshotId& snapshot_id);

  // Per table structure for external cluster snapshot importing to this cluster.
  // Old IDs mean IDs on external cluster, new IDs - IDs on this cluster.
  struct ExternalTableSnapshotData {
    NamespaceId old_namespace_id;
    NamespaceId new_namespace_id;
    TableId old_table_id;
    TableId new_table_id;
    int num_tablets;
    typedef std::pair<std::string, std::string> PartitionKeys;
    typedef std::map<PartitionKeys, TabletId> PartitionToIdMap;
    PartitionToIdMap new_tablets_map;
    // Mapping: Old tablet ID -> New tablet ID.
    google::protobuf::RepeatedPtrField<IdPairPB>* tablet_id_map;
  };

  CHECKED_STATUS ImportNamespaceEntry(const SysRowEntry& entry, ExternalTableSnapshotData* s_data);
  CHECKED_STATUS ImportTableEntry(const SysRowEntry& entry, ExternalTableSnapshotData* s_data);
  CHECKED_STATUS ImportTabletEntry(const SysRowEntry& entry, ExternalTableSnapshotData* s_data);

  void SendCreateTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                       const std::string& snapshot_id);

  void SendRestoreTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                        const std::string& snapshot_id);

  template <class Collection>
  typename Collection::value_type::second_type LockAndFindPtrOrNull(
      const Collection& collection, const typename Collection::value_type::first_type& key) {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");
    return FindPtrOrNull(collection, key);
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
