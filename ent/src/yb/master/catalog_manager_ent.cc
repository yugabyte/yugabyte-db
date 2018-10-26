// Copyright (c) YugaByte, Inc.

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/cluster_balance.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-internal.h"
#include "yb/master/async_snapshot_tasks.h"
#include "yb/tserver/backup.proxy.h"
#include "yb/util/cast.h"
#include "yb/util/tostring.h"

using std::string;
using std::unique_ptr;

using google::protobuf::RepeatedPtrField;
using strings::Substitute;

namespace yb {

using rpc::RpcContext;
using util::to_uchar_ptr;

namespace master {
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

  {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    // Verify that the system is not in snapshot creating/restoring state.
    if (!current_snapshot_id_.empty()) {
      const Status s = STATUS(IllegalState, Substitute(
          "Current snapshot id: $0. Parallel snapshot operations are not supported: $1",
          current_snapshot_id_, req->ShortDebugString()));
      return SetupError(resp->mutable_error(), MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION, s);
    }
  }

  // Create a new snapshot UUID.
  const SnapshotId snapshot_id = GenerateId();
  vector<scoped_refptr<TabletInfo>> all_tablets;

  scoped_refptr<SnapshotInfo> snapshot(new SnapshotInfo(snapshot_id));
  snapshot->mutable_metadata()->StartMutation();
  snapshot->mutable_metadata()->mutable_dirty()->pb.set_state(SysSnapshotEntryPB::CREATING);

  // Create in memory snapshot data descriptor.
  for (const TableIdentifierPB& table_id_pb : req->tables()) {
    scoped_refptr<TableInfo> table;
    scoped_refptr<NamespaceInfo> ns;
    MasterErrorPB::Code error = MasterErrorPB::UNKNOWN_ERROR;

    const Result<TabletInfos> res_tablets = GetTabletsOrSetupError(
        table_id_pb, &error, &table, &ns);
    if (!res_tablets.ok()) {
      return SetupError(resp->mutable_error(), error, res_tablets.status());
    }

    RETURN_NOT_OK(snapshot->AddEntries(ns, table, *res_tablets));
    all_tablets.insert(all_tablets.end(), res_tablets->begin(), res_tablets->end());
  }

  VLOG(1) << "Snapshot " << snapshot->ToString()
          << ": PB=" << snapshot->mutable_metadata()->mutable_dirty()->pb.DebugString();

  // Write the snapshot data descriptor to the system catalog (in "creating" state).
  Status s = sys_catalog_->AddItem(snapshot.get());
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute("An error occurred while inserting to sys-tablets: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
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

Status CatalogManager::IsSnapshotOpDone(const IsSnapshotOpDoneRequestPB* req,
                                        IsSnapshotOpDoneResponsePB* resp) {
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
      return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
    }
  }

  TRACE("Locking snapshot");
  auto l = snapshot->LockForRead();

  VLOG(1) << "Snapshot " << snapshot->ToString() << " state " << l->data().pb.state();

  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The snapshot was deleted", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
  }

  if (l->data().is_failed()) {
    Status s = STATUS(NotFound, "The snapshot has failed", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_FAILED, s);
  }

  if (l->data().is_cancelled()) {
    Status s = STATUS(NotFound, "The snapshot has been cancelled", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_CANCELLED, s);
  }

  // Verify if the create is in-progress.
  TRACE("Verify if the snapshot creation is in progress for $0", req->snapshot_id());
  resp->set_done(l->data().is_complete());
  return Status::OK();
}

Status CatalogManager::ListSnapshots(const ListSnapshotsRequestPB* req,
                                     ListSnapshotsResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  boost::shared_lock<LockType> l(lock_);
  TRACE("Acquired catalog manager lock");

  if (!current_snapshot_id_.empty()) {
    resp->set_current_snapshot_id(current_snapshot_id_);
  }

  auto setup_snapshot_pb_lambda = [resp](scoped_refptr<SnapshotInfo> snapshot_info) {
    auto snapshot_lock = snapshot_info->LockForRead();

    SnapshotInfoPB* const snapshot = resp->add_snapshots();
    snapshot->set_id(snapshot_info->id());
    *snapshot->mutable_entry() = snapshot_info->metadata().state().pb;
  };

  if (req->has_snapshot_id()) {
    TRACE("Looking up snapshot");
    scoped_refptr<SnapshotInfo> snapshot_info =
        FindPtrOrNull(snapshot_ids_map_, req->snapshot_id());
    if (snapshot_info == nullptr) {
      const Status s = STATUS(InvalidArgument, "Could not find snapshot", req->snapshot_id());
      return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
    }

    setup_snapshot_pb_lambda(snapshot_info);
  } else {
    for (const SnapshotInfoMap::value_type& entry : snapshot_ids_map_) {
      setup_snapshot_pb_lambda(entry.second);
    }
  }

  return Status::OK();
}

Status CatalogManager::RestoreSnapshot(const RestoreSnapshotRequestPB* req,
                                       RestoreSnapshotResponsePB* resp) {
  LOG(INFO) << "Servicing RestoreSnapshot request: " << req->ShortDebugString();
  RETURN_NOT_OK(CheckOnline());

  std::lock_guard<LockType> l(lock_);
  TRACE("Acquired catalog manager lock");

  if (!current_snapshot_id_.empty()) {
    const Status s = STATUS(IllegalState, Substitute(
        "Current snapshot id: $0. Parallel snapshot operations are not supported: $1",
        current_snapshot_id_, req->ShortDebugString()));
    return SetupError(resp->mutable_error(), MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION, s);
  }

  TRACE("Looking up snapshot");
  scoped_refptr<SnapshotInfo> snapshot = FindPtrOrNull(snapshot_ids_map_, req->snapshot_id());
  if (snapshot == nullptr) {
    const Status s = STATUS(InvalidArgument, "Could not find snapshot", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
  }

  auto snapshot_lock = snapshot->LockForWrite();

  if (snapshot_lock->data().started_deleting()) {
    Status s = STATUS(NotFound, "The snapshot was deleted", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
  }

  if (!snapshot_lock->data().is_complete()) {
    Status s = STATUS(IllegalState, "The snapshot state is not complete", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_IS_NOT_READY, s);
  }

  TRACE("Updating snapshot metadata on disk");
  SysSnapshotEntryPB& snapshot_pb = snapshot_lock->mutable_data()->pb;
  snapshot_pb.set_state(SysSnapshotEntryPB::RESTORING);

  // Update tablet states.
  SetTabletSnapshotsState(SysSnapshotEntryPB::RESTORING, &snapshot_pb);

  // Update sys-catalog with the updated snapshot state.
  Status s = sys_catalog_->UpdateItem(snapshot.get());
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys tables: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // CataloManager lock 'lock_' is still locked here.
  current_snapshot_id_ = req->snapshot_id();

  // Restore all entries.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    s = RestoreEntry(entry, req->snapshot_id());

    if (!s.ok()) {
      return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
    }
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock->Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " restoring";
  return Status::OK();
}

Status CatalogManager::RestoreEntry(const SysRowEntry& entry, const SnapshotId& snapshot_id) {
  switch (entry.type()) {
    case SysRowEntry::NAMESPACE: { // Restore NAMESPACES.
      TRACE("Looking up namespace");
      scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, entry.id());
      if (ns == nullptr) {
        // Restore Namespace.
        // TODO: implement
        LOG(INFO) << "Restoring: NAMESPACE id = " << entry.id();

        return STATUS(NotSupported, Substitute(
            "Not implemented: restoring namespace: id=$0", entry.type()));
      }
      break;
    }
    case SysRowEntry::TABLE: { // Restore TABLES.
      TRACE("Looking up table");
      scoped_refptr<TableInfo> table = FindPtrOrNull(table_ids_map_, entry.id());
      if (table == nullptr) {
        // Restore Table.
        // TODO: implement
        LOG(INFO) << "Restoring: TABLE id = " << entry.id();

        return STATUS(NotSupported, Substitute(
            "Not implemented: restoring table: id=$0", entry.type()));
      }
      break;
    }
    case SysRowEntry::TABLET: { // Restore TABLETS.
      TRACE("Looking up tablet");
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(tablet_map_, entry.id());
      if (tablet == nullptr) {
        // Restore Tablet.
        // TODO: implement
        LOG(INFO) << "Restoring: TABLET id = " << entry.id();

        return STATUS(NotSupported, Substitute(
            "Not implemented: restoring tablet: id=$0", entry.type()));
      } else {
        TRACE("Locking tablet");
        auto l = tablet->LockForRead();

        LOG(INFO) << "Sending RestoreTabletSnapshot to tablet: " << tablet->ToString();
        // Send RestoreSnapshot requests to all TServers (one tablet - one request).
        SendRestoreTabletSnapshotRequest(tablet, snapshot_id);
      }
      break;
    }
    default:
      return STATUS(InternalError, Substitute(
          "Unexpected entry type in the snapshot: $0", entry.type()));
  }

  return Status::OK();
}

Status CatalogManager::DeleteSnapshot(const DeleteSnapshotRequestPB* req,
                                      DeleteSnapshotResponsePB* resp) {
  LOG(INFO) << "Servicing DeleteSnapshot request: " << req->ShortDebugString();
  RETURN_NOT_OK(CheckOnline());

  std::lock_guard<LockType> l(lock_);
  TRACE("Acquired catalog manager lock");

  TRACE("Looking up snapshot");
  scoped_refptr<SnapshotInfo> snapshot = FindPtrOrNull(snapshot_ids_map_, req->snapshot_id());
  if (snapshot == nullptr) {
    const Status s = STATUS(InvalidArgument, "Could not find snapshot", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
  }

  auto snapshot_lock = snapshot->LockForWrite();

  if (snapshot_lock->data().started_deleting()) {
    Status s = STATUS(NotFound, "The snapshot was deleted", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::SNAPSHOT_NOT_FOUND, s);
  }

  if (snapshot_lock->data().is_restoring()) {
    Status s = STATUS(InvalidArgument, "The snapshot is being restored now", req->snapshot_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION, s);
  }

  TRACE("Updating snapshot metadata on disk");
  SysSnapshotEntryPB& snapshot_pb = snapshot_lock->mutable_data()->pb;
  snapshot_pb.set_state(SysSnapshotEntryPB::DELETING);

  // Update tablet states.
  SetTabletSnapshotsState(SysSnapshotEntryPB::DELETING, &snapshot_pb);

  // Update sys-catalog with the updated snapshot state.
  Status s = sys_catalog_->UpdateItem(snapshot.get());
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys tables: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // Send DeleteSnapshot requests to all TServers (one tablet - one request).
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    if (entry.type() == SysRowEntry::TABLET) {
      TRACE("Looking up tablet");
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(tablet_map_, entry.id());
      if (tablet == nullptr) {
        LOG(WARNING) << "Deleting tablet not found " << entry.id();
      } else {
        TRACE("Locking tablet");
        auto l = tablet->LockForRead();

        LOG(INFO) << "Sending DeleteTabletSnapshot to tablet: " << tablet->ToString();
        // Send DeleteSnapshot requests to all TServers (one tablet - one request).
        SendDeleteTabletSnapshotRequest(tablet, req->snapshot_id());
      }
    }
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock->Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " deletion";
  return Status::OK();
}

Status CatalogManager::ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                          ImportSnapshotMetaResponsePB* resp) {
  LOG(INFO) << "Servicing ImportSnapshotMeta request: " << req->ShortDebugString();
  RETURN_NOT_OK(CheckOnline());

  const SnapshotInfoPB& snapshot_info_pb = req->snapshot();
  const SysSnapshotEntryPB& snapshot_pb = snapshot_info_pb.entry();
  ExternalTableSnapshotDataMap tables_data;
  NamespaceMap namespace_map;
  Status s;

  // PHASE 1: Recreate namespaces, create table's meta data.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    switch (entry.type()) {
      case SysRowEntry::NAMESPACE: // Recreate NAMESPACE.
        s = ImportNamespaceEntry(entry, &namespace_map);
        break;
      case SysRowEntry::TABLE: { // Create TABLE metadata.
          DCHECK(!entry.id().empty());
          ExternalTableSnapshotData& data = tables_data[entry.id()];

          if (data.old_table_id.empty()) {
            data.old_table_id = entry.id();
            data.table_meta = resp->mutable_tables_meta()->Add();
            data.tablet_id_map = data.table_meta->mutable_tablets_ids();
          } else {
            LOG(WARNING) << "Ignoring duplicate table with id " << entry.id()
                         << " in snapshot " << snapshot_info_pb.id() << ".";
          }

          DCHECK(!data.old_table_id.empty());
        }
        break;
      case SysRowEntry::TABLET: // Preprocess original tablets.
        s = PreprocessTabletEntry(entry, &tables_data);
        break;
      case SysRowEntry::UNKNOWN: FALLTHROUGH_INTENDED;
      case SysRowEntry::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::UDTYPE: FALLTHROUGH_INTENDED;
      case SysRowEntry::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntry::VERSION: FALLTHROUGH_INTENDED;
      case SysRowEntry::SNAPSHOT:
        FATAL_INVALID_ENUM_VALUE(SysRowEntry::Type, entry.type());
    }

    if (!s.ok()) {
      LOG(ERROR) << "Failed to preprocess entry type " << entry.type() << ": "
                 << s.ToString();
      return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
    }
  }

  // PHASE 2: Recreate tables.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    if (entry.type() == SysRowEntry::TABLE) {
      ExternalTableSnapshotData& data = tables_data[entry.id()];
      s = ImportTableEntry(entry, namespace_map, &data);

      if (!s.ok()) {
        LOG(ERROR) << "Failed to recreate table: " << s.ToString();
        return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
      }
    }
  }

  // PHASE 3: Restore tablets.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    if (entry.type() == SysRowEntry::TABLET) {
      // Create tablets IDs map.
      s = ImportTabletEntry(entry, &tables_data);

      if (!s.ok()) {
        LOG(ERROR) << "Failed to recreate tablet: " << s.ToString();
        return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportNamespaceEntry(const SysRowEntry& entry,
                                            NamespaceMap* ns_map) {
  DCHECK_EQ(entry.type(), SysRowEntry::NAMESPACE);

  // Parse namespace PB.
  SysNamespaceEntryPB meta;
  const string& data = entry.data();
  RETURN_NOT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));

  TRACE("Looking up namespace");
  scoped_refptr<NamespaceInfo> ns = LockAndFindPtrOrNull(namespace_ids_map_, entry.id());

  if (ns != nullptr && ns->name() == meta.name()) {
    (*ns_map)[entry.id()] = entry.id();
    return Status::OK();
  }

  CreateNamespaceRequestPB req;
  CreateNamespaceResponsePB resp;
  req.set_name(meta.name());
  const Status s = CreateNamespace(&req, &resp, nullptr);

  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s.CloneAndAppend("Failed to create namespace");
  }

  if (s.IsAlreadyPresent()) {
    LOG(INFO) << "Using existing namespace " << meta.name() << ": " << resp.id();
  }

  (*ns_map)[entry.id()] = resp.id();
  return Status::OK();
}

Status CatalogManager::ImportTableEntry(const SysRowEntry& entry,
                                        const NamespaceMap& ns_map,
                                        ExternalTableSnapshotData* table_data) {
  DCHECK_EQ(entry.type(), SysRowEntry::TABLE);
  DCHECK_EQ(table_data->old_table_id, entry.id());

  // Parse table PB.
  SysTablesEntryPB meta;
  const string& data = entry.data();
  RETURN_NOT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));

  table_data->old_namespace_id = meta.namespace_id();
  DCHECK(!table_data->old_namespace_id.empty());

  DCHECK(ns_map.find(table_data->old_namespace_id) != ns_map.end());
  const NamespaceId new_namespace_id = ns_map.find(table_data->old_namespace_id)->second;
  DCHECK(!new_namespace_id.empty());

  scoped_refptr<TableInfo> table;

  // Create new table if namespace was changed.
  if (new_namespace_id == table_data->old_namespace_id) {
    TRACE("Looking up table");
    table = LockAndFindPtrOrNull(table_ids_map_, entry.id());

    // Check table is active OR table name was changed.
    if (table != nullptr && (!table->is_running() || table->name() != meta.name())) {
      table.reset();
    }
  }

  if (table == nullptr) {
    CreateTableRequestPB req;
    CreateTableResponsePB resp;
    req.set_name(meta.name());
    req.set_table_type(meta.table_type());
    req.set_num_tablets(table_data->num_tablets);
    req.mutable_namespace_()->set_id(new_namespace_id);
    *req.mutable_partition_schema() = meta.partition_schema();
    *req.mutable_replication_info() = meta.replication_info();

    // Clear column IDs.
    SchemaPB* const schema = req.mutable_schema();
    *schema = meta.schema();
    for (int i = 0; i < schema->columns_size(); ++i) {
      schema->mutable_columns(i)->clear_id();
    }

    RETURN_NOT_OK(CreateTable(&req, &resp, /* RpcContext */nullptr));
    table_data->new_table_id = resp.table_id();

    TRACE("Looking up new table");
    {
      table = LockAndFindPtrOrNull(table_ids_map_, table_data->new_table_id);

      if (table == nullptr) {
        return STATUS_SUBSTITUTE(
            InternalError, "Created table not found: $0", table_data->new_table_id);
      }
    }
  } else {
    table_data->new_table_id = table_data->old_table_id;
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  vector<scoped_refptr<TabletInfo>> new_tablets;
  table->GetAllTablets(&new_tablets);

  for (const scoped_refptr<TabletInfo>& tablet : new_tablets) {
    auto l = tablet->LockForRead();
    const PartitionPB& partition_pb = tablet->metadata().state().pb.partition();
    const ExternalTableSnapshotData::PartitionKeys key(
        partition_pb.partition_key_start(), partition_pb.partition_key_end());
    table_data->new_tablets_map[key] = tablet->id();
  }

  IdPairPB* const namespace_ids = table_data->table_meta->mutable_namespace_ids();
  namespace_ids->set_new_id(new_namespace_id);
  namespace_ids->set_old_id(table_data->old_namespace_id);

  IdPairPB* const table_ids = table_data->table_meta->mutable_table_ids();
  table_ids->set_new_id(table_data->new_table_id);
  table_ids->set_old_id(table_data->old_table_id);

  return Status::OK();
}

Status CatalogManager::PreprocessTabletEntry(const SysRowEntry& entry,
                                             ExternalTableSnapshotDataMap* table_map) {
  DCHECK_EQ(entry.type(), SysRowEntry::TABLET);

  SysTabletsEntryPB meta;
  const string& data = entry.data();
  RETURN_NOT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));

  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];
  ++table_data.num_tablets;
  return Status::OK();
}

Status CatalogManager::ImportTabletEntry(const SysRowEntry& entry,
                                         ExternalTableSnapshotDataMap* table_map) {
  DCHECK_EQ(entry.type(), SysRowEntry::TABLET);

  SysTabletsEntryPB meta;
  const string& data = entry.data();
  RETURN_NOT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));

  DCHECK(table_map->find(meta.table_id()) != table_map->end());
  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];

  // Update tablets IDs map.
  if (table_data.new_table_id == table_data.old_table_id) {
    TRACE("Looking up tablet");
    scoped_refptr<TabletInfo> tablet = LockAndFindPtrOrNull(tablet_map_, entry.id());

    if (tablet != nullptr) {
      IdPairPB* const pair = table_data.tablet_id_map->Add();
      pair->set_old_id(entry.id());
      pair->set_new_id(entry.id());
      return Status::OK();
    }
  }

  const PartitionPB& partition_pb = meta.partition();
  const ExternalTableSnapshotData::PartitionKeys key(
      partition_pb.partition_key_start(), partition_pb.partition_key_end());
  const ExternalTableSnapshotData::PartitionToIdMap::const_iterator it =
      table_data.new_tablets_map.find(key);

  if (it == table_data.new_tablets_map.end()) {
    return STATUS_SUBSTITUTE(NotFound,
                             "Not found new tablet with expected partition keys: $0 - $1",
                             partition_pb.partition_key_start(),
                             partition_pb.partition_key_end());
  }

  IdPairPB* const pair = table_data.tablet_id_map->Add();
  pair->set_old_id(entry.id());
  pair->set_new_id(it->second);
  return Status::OK();
}

void CatalogManager::SendCreateTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                                     const string& snapshot_id) {
  auto call = std::make_shared<AsyncTabletSnapshotOp>(
      master_, worker_pool_.get(), tablet, snapshot_id,
      tserver::TabletSnapshotOpRequestPB::CREATE);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send create snapshot request");
}

void CatalogManager::SendRestoreTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                                      const string& snapshot_id) {
  auto call = std::make_shared<AsyncTabletSnapshotOp>(
      master_, worker_pool_.get(), tablet, snapshot_id,
      tserver::TabletSnapshotOpRequestPB::RESTORE);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send restore snapshot request");
}

void CatalogManager::SendDeleteTabletSnapshotRequest(const scoped_refptr<TabletInfo>& tablet,
                                                     const string& snapshot_id) {
  auto call = std::make_shared<AsyncTabletSnapshotOp>(
      master_, worker_pool_.get(), tablet, snapshot_id,
      tserver::TabletSnapshotOpRequestPB::DELETE);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send delete snapshot request");
}

void CatalogManager::HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  DCHECK_ONLY_NOTNULL(tablet);

  LOG(INFO) << "Handling Create Tablet Snapshot Response for tablet " << tablet->ToString()
            << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      LOG(WARNING) << "No active snapshot: " << current_snapshot_id_;
      return;
    }

    snapshot = FindPtrOrNull(snapshot_ids_map_, current_snapshot_id_);

    if (!snapshot) {
      LOG(WARNING) << "Snapshot not found: " << current_snapshot_id_;
      return;
    }
  }

  if (!snapshot->IsCreateInProgress()) {
    LOG(WARNING) << "Snapshot is not in creating state: " << snapshot->id();
    return;
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
  bool finished = true;
  if (error) {
    l->mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
    LOG(WARNING) << "Failed snapshot " << snapshot->id() << " on tablet " << tablet->id();
  } else if (num_tablets_complete == tablet_snapshots->size()) {
    l->mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
    LOG(INFO) << "Completed snapshot " << snapshot->id();
  } else {
    finished = false;
  }

  if (finished) {
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
    return;
  }

  l->Commit();
}

void CatalogManager::HandleRestoreTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  DCHECK_ONLY_NOTNULL(tablet);

  LOG(INFO) << "Handling Restore Tablet Snapshot Response for tablet " << tablet->ToString()
            << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      LOG(WARNING) << "No restoring snapshot: " << current_snapshot_id_;
      return;
    }

    snapshot = FindPtrOrNull(snapshot_ids_map_, current_snapshot_id_);

    if (!snapshot) {
      LOG(WARNING) << "Restoring snapshot not found: " << current_snapshot_id_;
      return;
    }
  }

  if (!snapshot->IsRestoreInProgress()) {
    LOG(WARNING) << "Snapshot is not in restoring state: " << snapshot->id();
    return;
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
      LOG(WARNING) << "Failed restoring snapshot " << snapshot->id()
                   << " on tablet " << tablet->id();
    } else {
      DCHECK_EQ(num_tablets_complete, tablet_snapshots->size());
      l->mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
      LOG(INFO) << "Restored snapshot " << snapshot->id();
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
    return;
  }

  l->Commit();
}

void CatalogManager::HandleDeleteTabletSnapshotResponse(
    SnapshotId snapshot_id, TabletInfo *tablet, bool error) {
  DCHECK_ONLY_NOTNULL(tablet);

  LOG(INFO) << "Handling Delete Tablet Snapshot Response for tablet " << tablet->ToString()
            << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");

    snapshot = FindPtrOrNull(snapshot_ids_map_, snapshot_id);

    if (!snapshot) {
      LOG(WARNING) << "Snapshot not found: " << snapshot_id;
      return;
    }
  }

  if (!snapshot->IsDeleteInProgress()) {
    LOG(WARNING) << "Snapshot is not in deleting state: " << snapshot->id();
    return;
  }

  auto tablet_l = tablet->LockForRead();
  auto l = snapshot->LockForWrite();
  RepeatedPtrField<SysSnapshotEntryPB_TabletSnapshotPB>* tablet_snapshots =
      l->mutable_data()->pb.mutable_tablet_snapshots();
  int num_tablets_complete = 0;

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);

    if (tablet_info->id() == tablet->id()) {
      tablet_info->set_state(error ? SysSnapshotEntryPB::FAILED : SysSnapshotEntryPB::DELETED);
    }

    if (tablet_info->state() != SysSnapshotEntryPB::DELETING) {
      ++num_tablets_complete;
    }
  }

  Status s;
  if (num_tablets_complete == tablet_snapshots->size()) {
    // Delete the snapshot.
    l->mutable_data()->pb.set_state(SysSnapshotEntryPB::DELETED);
    LOG(INFO) << "Deleted snapshot " << snapshot->id();

    s = sys_catalog_->DeleteItem(snapshot.get());

    std::lock_guard<LockType> manager_l(lock_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_ == snapshot_id) {
      current_snapshot_id_ = "";
    }

    // Remove it from the maps.
    TRACE("Removing from maps");
    if (snapshot_ids_map_.erase(snapshot_id) < 1) {
      LOG(WARNING) << "Could not remove snapshot " << snapshot_id << " from map";
    }
  } else if (error) {
    l->mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
    LOG(WARNING) << "Failed snapshot " << snapshot->id() << " deletion on tablet " << tablet->id();

    s = sys_catalog_->UpdateItem(snapshot.get());
  }

  if (!s.ok()) {
    LOG(WARNING) << "An error occurred while updating sys-tables: " << s.ToString();
    return;
  }

  l->Commit();

  VLOG(1) << "Deleting snapshot: " << snapshot->id()
          << " PB: " << l->mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();
}

void CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  super::DumpState(out, on_disk_dump);

  // TODO: dump snapshots
}

Status CatalogManager::CheckValidReplicationInfo(const ReplicationInfoPB& replication_info,
                                                 const TSDescriptorVector& all_ts_descs,
                                                 const vector<Partition>& partitions,
                                                 CreateTableResponsePB* resp) {
  TSDescriptorVector ts_descs;
  GetTsDescsFromPlacementInfo(replication_info.live_replicas(), all_ts_descs, &ts_descs);
  RETURN_NOT_OK(super::CheckValidPlacementInfo(replication_info.live_replicas(), ts_descs,
                                               partitions, resp));
  for (int i = 0; i < replication_info.read_replicas_size(); i++) {
    GetTsDescsFromPlacementInfo(replication_info.read_replicas(i), all_ts_descs, &ts_descs);
    RETURN_NOT_OK(super::CheckValidPlacementInfo(replication_info.read_replicas(i), ts_descs,
                                                 partitions, resp));
  }
  return Status::OK();
}

Status CatalogManager::HandlePlacementUsingReplicationInfo(
    const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& all_ts_descs,
    consensus::RaftConfigPB* config) {
  TSDescriptorVector ts_descs;
  GetTsDescsFromPlacementInfo(replication_info.live_replicas(), all_ts_descs, &ts_descs);
  RETURN_NOT_OK(super::HandlePlacementUsingPlacementInfo(replication_info.live_replicas(),
                                                      ts_descs,
                                                      consensus::RaftPeerPB::VOTER, config));
  for (int i = 0; i < replication_info.read_replicas_size(); i++) {
    GetTsDescsFromPlacementInfo(replication_info.read_replicas(i), all_ts_descs, &ts_descs);
    RETURN_NOT_OK(super::HandlePlacementUsingPlacementInfo(replication_info.read_replicas(i),
                                                           ts_descs,
                                                           consensus::RaftPeerPB::OBSERVER,
                                                           config));
  }
  return Status::OK();
}

void CatalogManager::GetTsDescsFromPlacementInfo(const PlacementInfoPB& placement_info,
                                                 const TSDescriptorVector& all_ts_descs,
                                                 TSDescriptorVector* ts_descs) {
  ts_descs->clear();
  for (const auto& ts_desc : all_ts_descs) {
    TSDescriptor* ts_desc_ent = down_cast<TSDescriptor*>(ts_desc.get());
    if (placement_info.has_placement_uuid()) {
      string placement_uuid = placement_info.placement_uuid();
      if (ts_desc_ent->placement_uuid() == placement_uuid) {
        ts_descs->push_back(ts_desc);
      }
    } else if (ts_desc_ent->placement_uuid() == "") {
      // Since the placement info has no placement id, we know it is live, so we add this ts.
      ts_descs->push_back(ts_desc);
    }
  }
}

void CatalogManager::SetTabletSnapshotsState(SysSnapshotEntryPB::State state,
                                             SysSnapshotEntryPB* snapshot_pb) {
  RepeatedPtrField<SysSnapshotEntryPB_TabletSnapshotPB>* tablet_snapshots =
      snapshot_pb->mutable_tablet_snapshots();

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);
    tablet_info->set_state(state);
  }
}

} // namespace enterprise

////////////////////////////////////////////////////////////
// SnapshotInfo
////////////////////////////////////////////////////////////

SnapshotInfo::SnapshotInfo(SnapshotId id) : snapshot_id_(std::move(id)) {}

SysSnapshotEntryPB::State SnapshotInfo::state() const {
  auto l = LockForRead();
  return l->data().state();
}

const std::string& SnapshotInfo::state_name() const {
  auto l = LockForRead();
  return l->data().state_name();
}

std::string SnapshotInfo::ToString() const {
  return Substitute("[id=$0]", snapshot_id_);
}

bool SnapshotInfo::IsCreateInProgress() const {
  auto l = LockForRead();
  return l->data().is_creating();
}

bool SnapshotInfo::IsRestoreInProgress() const {
  auto l = LockForRead();
  return l->data().is_restoring();
}

bool SnapshotInfo::IsDeleteInProgress() const {
  auto l = LockForRead();
  return l->data().is_deleting();
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
