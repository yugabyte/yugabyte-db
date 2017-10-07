// Copyright (c) YugaByte, Inc.

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/tostring.h"
#include "yb/tserver/backup.proxy.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-internal.h"
#include "yb/master/async_snapshot_tasks.h"

namespace yb {
namespace master {

using std::string;
using std::unique_ptr;
using rpc::RpcContext;
using strings::Substitute;
using google::protobuf::RepeatedPtrField;

namespace enterprise {

////////////////////////////////////////////////////////////
// Snapshot Loader
////////////////////////////////////////////////////////////

class SnapshotLoader : public Visitor<PersistentSnapshotInfo> {
 public:
  explicit SnapshotLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const SnapshotId& ss_id, const SysSnapshotEntryPB& metadata) override {
    CHECK(!ContainsKey(catalog_manager_->snapshot_ids_map_, ss_id))
      << "Snapshot already exists: " << ss_id;

    // Setup the snapshot info.
    SnapshotInfo *const ss = new SnapshotInfo(ss_id);
    auto l = ss->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    // Add the snapshot to the IDs map (if the snapshot is not deleted).
    catalog_manager_->snapshot_ids_map_[ss_id] = ss;

    LOG(INFO) << "Loaded metadata for snapshot (id=" << ss_id << "): "
              << ss->ToString() << ": " << metadata.ShortDebugString();
    l->Commit();
    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotLoader);
};

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

Status CatalogManager::RunLoaders() {
  RETURN_NOT_OK(super::RunLoaders());

  // Clear the snapshots.
  snapshot_ids_map_.clear();

  unique_ptr<SnapshotLoader> snapshot_loader(new SnapshotLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(snapshot_loader.get()),
      "Failed while visiting snapshots in sys catalog");

  return Status::OK();
}

Status CatalogManager::CreateSnapshot(const CreateSnapshotRequestPB* req,
                                      CreateSnapshotResponsePB* resp) {
  LOG(INFO) << "Servicing CreateSnapshot request: " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  // Create a new snapshot UUID.
  const SnapshotId snapshot_id = GenerateId();
  vector<scoped_refptr<TabletInfo>> all_tablets;

  scoped_refptr<SnapshotInfo> snapshot(new SnapshotInfo(snapshot_id));
  snapshot->mutable_metadata()->StartMutation();
  snapshot->mutable_metadata()->mutable_dirty()->pb.set_state(SysSnapshotEntryPB::CREATING);

  // Create in memory snapshot data descriptor.
  for (const TableIdentifierPB& table_id_pb : req->tables()) {
    scoped_refptr<TableInfo> table;

    // Lookup the table and verify it exists.
    TRACE("Looking up table");
    RETURN_NOT_OK(FindTable(table_id_pb, &table));
    if (table == nullptr) {
      const Status s = STATUS(NotFound, "Table does not exist", table_id_pb.DebugString());
      SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
      return s;
    }

    vector<scoped_refptr<TabletInfo>> tablets;
    scoped_refptr<NamespaceInfo> ns;

    {
      TRACE("Locking table");
      auto l = table->LockForRead();

      if (table->metadata().state().table_type() != TableType::YQL_TABLE_TYPE) {
        const Status s = STATUS(InvalidArgument, "Invalid table type", table_id_pb.DebugString());
        SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
        return s;
      }

      if (table->IsCreateInProgress()) {
        const Status s = STATUS(IllegalState,
            "Table creation is in progress", table->ToString());
        SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
        return s;
      }

      TRACE("Looking up namespace");
      ns = FindPtrOrNull(namespace_ids_map_, table->namespace_id());
      if (ns == nullptr) {
        const Status s = STATUS(InvalidArgument,
            "Could not find namespace by namespace id", table->namespace_id());
        SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
        return s;
      }

      table->GetAllTablets(&tablets);
    }

    RETURN_NOT_OK(snapshot->AddEntries(ns, table, tablets));
    all_tablets.insert(all_tablets.end(), tablets.begin(), tablets.end());
  }

  VLOG(1) << "Snapshot " << snapshot->ToString()
          << ": PB=" << snapshot->mutable_metadata()->mutable_dirty()->pb.DebugString();

  // Write the snapshot data descriptor to the system catalog (in "creating" state).
  Status s = sys_catalog_->AddItem(snapshot.get());
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute("An error occurred while inserting to sys-tablets: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    CheckIfNoLongerLeaderAndSetupError(s, resp);
    return s;
  }
  TRACE("Wrote snapshot to system catalog");

  // Commit in memory snapshot data descriptor.
  snapshot->mutable_metadata()->CommitMutation();

  // Put the snapshot data descriptor to the catalog manager.
  {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    // Verify that the snapshot does not exist.
    DCHECK(nullptr == FindPtrOrNull(snapshot_ids_map_, snapshot_id));
    snapshot_ids_map_[snapshot_id] = snapshot;

    current_snapshot_id_ = snapshot_id;
  }

  // Send CreateSnapshot requests to all TServers (one tablet - one request).
  for (const scoped_refptr<TabletInfo> tablet : all_tablets) {
    TRACE("Locking tablet");
    auto l = tablet->LockForRead();

    LOG(INFO) << "Sending CreateTabletSnapshot to tablet: " << tablet->ToString();

    // Send Create Tablet Snapshot request to each tablet leader.
    SendCreateTabletSnapshotRequest(tablet, snapshot_id);
  }

  resp->set_snapshot_id(snapshot_id);
  LOG(INFO) << "Successfully started snapshot " << snapshot_id << " creation";
  return Status::OK();
}

Status CatalogManager::IsCreateSnapshotDone(const IsCreateSnapshotDoneRequestPB* req,
                                            IsCreateSnapshotDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<SnapshotInfo> snapshot;

  // Lookup the snapshot and verify if it exists.
  TRACE("Looking up snapshot");
  {
    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");

    snapshot = FindPtrOrNull(snapshot_ids_map_, req->snapshot_id());
    if (snapshot == nullptr) {
      const Status s = STATUS(NotFound, "The snapshot does not exist", req->snapshot_id());
      // TODO: SNAPSHOT_NOT_FOUND
      SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
      return s;
    }
  }

  TRACE("Locking snapshot");
  auto l = snapshot->LockForRead();

  VLOG(1) << "Snapshot " << snapshot->ToString() << " state " << l->data().pb.state();

  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The snapshot was deleted", req->snapshot_id());
    // TODO: SNAPSHOT_NOT_FOUND
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  if (l->data().is_failed()) {
    Status s = STATUS(NotFound, "The snapshot has failed", req->snapshot_id());
    // TODO: SNAPSHOT_FAILED
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  // Verify if the create is in-progress.
  TRACE("Verify if the snapshot creation is in progress for $0", req->snapshot_id());
  resp->set_done(l->data().is_complete());
  return Status::OK();
}

void CatalogManager::SendCreateTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                                     const string& snapshot_id) {
  auto call = std::make_shared<AsyncCreateTabletSnapshot>(
      master_, worker_pool_.get(), tablet, snapshot_id);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send create snapshot request");
}

Status CatalogManager::HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  DCHECK_ONLY_NOTNULL(tablet);

  LOG(INFO) << "Handling Create Tablet Snapshot Response for tablet " << tablet->ToString()
            << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      return STATUS(IllegalState, "No active snapshot", current_snapshot_id_);
    }

    snapshot = FindPtrOrNull(snapshot_ids_map_, current_snapshot_id_);

    if (!snapshot) {
      return STATUS(IllegalState, "Snapshot not found", current_snapshot_id_);
    }
  }

  if (!snapshot->IsCreateInProgress()) {
    return STATUS(IllegalState, "Snapshot is not in creating state", snapshot->id());
  }

  auto tablet_l = tablet->LockForRead();
  auto l = snapshot->LockForWrite();
  RepeatedPtrField<SysSnapshotEntryPB_TabletSnapshotPB>* tablet_snapshots =
      l->mutable_data()->pb.mutable_tablet_snapshots();
  int num_tablets_complete = 0;

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);

    if (tablet_info->id() == tablet->id()) {
      tablet_info->set_state(error ? SysSnapshotEntryPB::FAILED : SysSnapshotEntryPB::COMPLETE);
    }

    if (tablet_info->state() == SysSnapshotEntryPB::COMPLETE) {
      ++num_tablets_complete;
    }
  }

  // Finish the snapshot.
  if (error || num_tablets_complete == tablet_snapshots->size()) {
    if (error) {
      l->mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
      LOG(WARNING) << "Failed snapshot " << snapshot->id() << " on tablet " << tablet->id();
    } else {
      DCHECK_EQ(num_tablets_complete, tablet_snapshots->size());
      l->mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
      LOG(INFO) << "Completed snapshot " << snapshot->id();
    }

    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");
    current_snapshot_id_ = "";
  }

  VLOG(1) << "Snapshot: " << snapshot->id()
          << " PB: " << l->mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();

  const Status s = sys_catalog_->UpdateItem(snapshot.get());
  if (!s.ok()) {
    LOG(WARNING) << "An error occurred while updating sys-tables: " << s.ToString();
    return s;
  }

  l->Commit();
  return Status::OK();
}

void CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  super::DumpState(out, on_disk_dump);

  // TODO: dump snapshots
}

} // namespace enterprise

////////////////////////////////////////////////////////////
// SnapshotInfo
////////////////////////////////////////////////////////////

SnapshotInfo::SnapshotInfo(SnapshotId id) : snapshot_id_(std::move(id)) {}

std::string SnapshotInfo::ToString() const {
  return Substitute("[id=$0]", snapshot_id_);
}

bool SnapshotInfo::IsCreateInProgress() const {
  auto l = LockForRead();
  return l->data().is_creating();
}

Status SnapshotInfo::AddEntries(const scoped_refptr<NamespaceInfo> ns,
                                const scoped_refptr<TableInfo>& table,
                                const vector<scoped_refptr<TabletInfo>>& tablets) {
  // Note: SysSnapshotEntryPB includes PBs for stored (1) namespaces (2) tables (3) tablets.
  SysSnapshotEntryPB& snapshot_pb = mutable_metadata()->mutable_dirty()->pb;

  // Add namespace entry.
  SysRowEntry* entry = snapshot_pb.add_entries();
  {
    TRACE("Locking namespace");
    auto l = ns->LockForRead();

    entry->set_id(ns->id());
    entry->set_type(ns->metadata().state().type());
    entry->set_data(ns->metadata().state().pb.SerializeAsString());
  }

  // Add table entry.
  entry = snapshot_pb.add_entries();
  {
    TRACE("Locking table");
    auto l = table->LockForRead();

    entry->set_id(table->id());
    entry->set_type(table->metadata().state().type());
    entry->set_data(table->metadata().state().pb.SerializeAsString());
  }

  // Add tablet entries.
  for (const scoped_refptr<TabletInfo> tablet : tablets) {
    SysSnapshotEntryPB_TabletSnapshotPB* const tablet_info = snapshot_pb.add_tablet_snapshots();
    entry = snapshot_pb.add_entries();

    TRACE("Locking tablet");
    auto l = tablet->LockForRead();

    tablet_info->set_id(tablet->id());
    tablet_info->set_state(SysSnapshotEntryPB::CREATING);

    entry->set_id(tablet->id());
    entry->set_type(tablet->metadata().state().type());
    entry->set_data(tablet->metadata().state().pb.SerializeAsString());
  }

  return Status::OK();
}

}  // namespace master
}  // namespace yb
