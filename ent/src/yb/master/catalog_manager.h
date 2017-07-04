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

  CHECKED_STATUS HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error);

  void DumpState(std::ostream* out, bool on_disk_dump = false) const override;

 private:
  friend class SnapshotLoader;

  void SendCreateTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                       const std::string& snapshot_id);

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
