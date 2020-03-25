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

#include <google/protobuf/util/message_differencer.h>

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/cluster_balance.h"

#include "yb/cdc/cdc_service.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/common/common.pb.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master_defaults.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-internal.h"
#include "yb/master/async_snapshot_tasks.h"
#include "yb/master/encryption_manager.h"
#include "yb/tserver/backup.proxy.h"
#include "yb/util/cast.h"
#include "yb/util/service_util.h"
#include "yb/util/tostring.h"
#include "yb/util/string_util.h"
#include "yb/util/random_util.h"
#include "yb/cdc/cdc_consumer.pb.h"

using std::string;
using std::unique_ptr;

using google::protobuf::RepeatedPtrField;
using google::protobuf::util::MessageDifferencer;
using strings::Substitute;

DEFINE_uint64(cdc_state_table_num_tablets, 0,
    "Number of tablets to use when creating the CDC state table."
    "0 to use the same default num tablets as for regular tables.");

DEFINE_int32(cdc_wal_retention_time_secs, 4 * 3600,
             "WAL retention time in seconds to be used for tables for which a CDC stream was "
             "created.");

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
// CDC Stream Loader
////////////////////////////////////////////////////////////

class CDCStreamLoader : public Visitor<PersistentCDCStreamInfo> {
 public:
  explicit CDCStreamLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const CDCStreamId& stream_id, const SysCDCStreamEntryPB& metadata) {
    DCHECK(!ContainsKey(catalog_manager_->cdc_stream_map_, stream_id))
        << "CDC stream already exists: " << stream_id;

    if (!ContainsKey(*catalog_manager_->table_ids_map_, metadata.table_id())) {
      LOG(ERROR) << "Invalid table ID " << metadata.table_id() << " for stream " << stream_id;
      // TODO (#2059): Potentially signals a race condition that table got deleted while stream was
      // being created.
      // Log error and continue without loading the stream.
      return Status::OK();
    }

    // Setup the CDC stream info.
    auto stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
    auto l = stream->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    // Add the CDC stream to the CDC stream map.
    catalog_manager_->cdc_stream_map_[stream->id()] = stream;

    l->Commit();

    LOG(INFO) << "Loaded metadata for CDC stream " << stream->ToString() << ": "
              << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(CDCStreamLoader);
};

////////////////////////////////////////////////////////////
// Universe Replication Loader
////////////////////////////////////////////////////////////

class UniverseReplicationLoader : public Visitor<PersistentUniverseReplicationInfo> {
 public:
  explicit UniverseReplicationLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  Status Visit(const std::string& producer_id, const SysUniverseReplicationEntryPB& metadata) {
    DCHECK(!ContainsKey(catalog_manager_->universe_replication_map_, producer_id))
        << "Producer universe already exists: " << producer_id;

    // Setup the universe replication info.
    UniverseReplicationInfo* const ri = new UniverseReplicationInfo(producer_id);
    auto l = ri->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    if (!l->data().is_active() && !l->data().is_deleted_or_failed()) {
      // Replication was not fully setup.
      LOG(WARNING) << "Universe replication in transient state: " << producer_id;

      // TODO: Should we delete all failed universe replication items?
    }

    // Add universe replication info to the universe replication map.
    catalog_manager_->universe_replication_map_[ri->id()] = ri;
    l->Commit();

    LOG(INFO) << "Loaded metadata for universe replication " << ri->ToString();
    VLOG(1) << "Metadata for universe replication " << ri->ToString() << ": "
            << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(UniverseReplicationLoader);
};

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

Status CatalogManager::RunLoaders(int64_t term) {
  RETURN_NOT_OK(super::RunLoaders(term));

  // Clear the snapshots.
  snapshot_ids_map_.clear();

  // Clear CDC stream map.
  cdc_stream_map_.clear();

  // Clear universe replication map.
  universe_replication_map_.clear();

  LOG(INFO) << __func__ << ": Loading snapshots into memory.";
  unique_ptr<SnapshotLoader> snapshot_loader(new SnapshotLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(snapshot_loader.get()),
      "Failed while visiting snapshots in sys catalog");

  LOG(INFO) << __func__ << ": Loading CDC streams into memory.";
  auto cdc_stream_loader = std::make_unique<CDCStreamLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(cdc_stream_loader.get()),
      "Failed while visiting CDC streams in sys catalog");

  LOG(INFO) << __func__ << ": Loading universe replication info into memory.";
  auto universe_replication_loader = std::make_unique<UniverseReplicationLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(universe_replication_loader.get()),
      "Failed while visiting universe replication info in sys catalog");

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
  const SnapshotId snapshot_id = GenerateId(SysRowEntry::SNAPSHOT);
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
  Status s = sys_catalog_->AddItem(snapshot.get(), leader_ready_term_);
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

  std::shared_lock<LockType> l(lock_);
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
  Status s = sys_catalog_->UpdateItem(snapshot.get(), leader_ready_term_);
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
      scoped_refptr<TableInfo> table = FindPtrOrNull(*table_ids_map_, entry.id());
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
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());
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
  Status s = sys_catalog_->UpdateItem(snapshot.get(), leader_ready_term_);
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
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());
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
      case SysRowEntry::SYS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::CDC_STREAM: FALLTHROUGH_INTENDED;
      case SysRowEntry::UNIVERSE_REPLICATION: FALLTHROUGH_INTENDED;
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

Status CatalogManager::ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                            ChangeEncryptionInfoResponsePB* resp) {
  auto l = cluster_config_->LockForWrite();
  auto encryption_info = l->mutable_data()->pb.mutable_encryption_info();

  RETURN_NOT_OK(encryption_manager_->ChangeEncryptionInfo(req, encryption_info));

  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(sys_catalog_->UpdateItem(cluster_config_.get(), leader_ready_term_));
  l->Commit();

  std::lock_guard<simple_spinlock> lock(should_send_universe_key_registry_mutex_);
  for (auto& entry : should_send_universe_key_registry_) {
    entry.second = true;
  }

  return Status::OK();
}

Status CatalogManager::IsEncryptionEnabled(const IsEncryptionEnabledRequestPB* req,
                                           IsEncryptionEnabledResponsePB* resp) {
  auto l = cluster_config_->LockForRead();
  const auto& encryption_info = l->data().pb.encryption_info();
  return encryption_manager_->IsEncryptionEnabled(encryption_info, resp);
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
    table = LockAndFindPtrOrNull(*table_ids_map_, entry.id());

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
      table = LockAndFindPtrOrNull(*table_ids_map_, table_data->new_table_id);

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
    scoped_refptr<TabletInfo> tablet = LockAndFindPtrOrNull(*tablet_map_, entry.id());

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

  const Status s = sys_catalog_->UpdateItem(snapshot.get(), leader_ready_term_);
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

  const Status s = sys_catalog_->UpdateItem(snapshot.get(), leader_ready_term_);
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

    s = sys_catalog_->DeleteItem(snapshot.get(), leader_ready_term_);

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

    s = sys_catalog_->UpdateItem(snapshot.get(), leader_ready_term_);
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

template <typename Registry, typename Mutex>
bool ShouldResendRegistry(
    const std::string& ts_uuid, bool has_registration, Registry* registry, Mutex* mutex) {
  bool should_resend_registry;
  {
    std::lock_guard<Mutex> lock(*mutex);
    auto it = registry->find(ts_uuid);
    should_resend_registry = (it == registry->end() || it->second || has_registration);
    if (it == registry->end()) {
      registry->emplace(ts_uuid, false);
    } else {
      it->second = false;
    }
  }
  return should_resend_registry;
}

Status CatalogManager::FillHeartbeatResponse(const TSHeartbeatRequestPB* req,
                                             TSHeartbeatResponsePB* resp) {
  SysClusterConfigEntryPB cluster_config;
  RETURN_NOT_OK(GetClusterConfig(&cluster_config));
  RETURN_NOT_OK(FillHeartbeatResponseEncryption(cluster_config, req, resp));
  return FillHeartbeatResponseCDC(cluster_config, req, resp);
}


Status CatalogManager::FillHeartbeatResponseCDC(const SysClusterConfigEntryPB& cluster_config,
                                                const TSHeartbeatRequestPB* req,
                                                TSHeartbeatResponsePB* resp) {
  resp->set_cluster_config_version(cluster_config.version());
  if (!cluster_config.has_consumer_registry() ||
      req->cluster_config_version() >= cluster_config.version()) {
    return Status::OK();
  }
  *resp->mutable_consumer_registry() = cluster_config.consumer_registry();
  return Status::OK();
}

Status CatalogManager::FillHeartbeatResponseEncryption(
    const SysClusterConfigEntryPB& cluster_config,
    const TSHeartbeatRequestPB* req,
    TSHeartbeatResponsePB* resp) {
  const auto& ts_uuid = req->common().ts_instance().permanent_uuid();
  if (!cluster_config.has_encryption_info() ||
      !ShouldResendRegistry(ts_uuid, req->has_registration(), &should_send_universe_key_registry_,
                            &should_send_universe_key_registry_mutex_)) {
    return Status::OK();
  }

  const auto& encryption_info = cluster_config.encryption_info();
  RETURN_NOT_OK(encryption_manager_->FillHeartbeatResponseEncryption(encryption_info, resp));

  return Status::OK();
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

Status CatalogManager::CreateCdcStateTableIfNeeded(rpc::RpcContext *rpc) {
  TableIdentifierPB table_identifier;
  table_identifier.set_table_name(kCdcStateTableName);
  table_identifier.mutable_namespace_()->set_name(kSystemNamespaceName);

  // Check that the namespace exists.
  scoped_refptr<NamespaceInfo> ns_info;
  RETURN_NOT_OK(FindNamespace(table_identifier.namespace_(), &ns_info));
  if (!ns_info) {
    return STATUS(NotFound, "Namespace does not exist", kSystemNamespaceName);
  }

  // If CDC state table exists do nothing, otherwise create it.
  scoped_refptr<TableInfo> table_info;
  RETURN_NOT_OK(FindTable(table_identifier, &table_info));

  if (!table_info) {
    // Set up a CreateTable request internally.
    CreateTableRequestPB req;
    CreateTableResponsePB resp;
    req.set_name(kCdcStateTableName);
    req.mutable_namespace_()->CopyFrom(table_identifier.namespace_());
    req.set_table_type(TableType::YQL_TABLE_TYPE);

    // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
    // will use the same defaults as for regular tables.
    if (FLAGS_cdc_state_table_num_tablets > 0) {
      req.set_num_tablets(FLAGS_cdc_state_table_num_tablets);
    }

    client::YBSchemaBuilder schema_builder;
    schema_builder.AddColumn(master::kCdcStreamId)->HashPrimaryKey()->Type(DataType::STRING);
    schema_builder.AddColumn(master::kCdcTabletId)->HashPrimaryKey()->Type(DataType::STRING);
    schema_builder.AddColumn(master::kCdcCheckpoint)->Type(DataType::STRING);
    schema_builder.AddColumn(master::kCdcData)->Type(QLType::CreateTypeMap(
        DataType::STRING, DataType::STRING));

    client::YBSchema yb_schema;
    CHECK_OK(schema_builder.Build(&yb_schema));

    auto schema = yb::client::internal::GetSchema(yb_schema);
    SchemaToPB(schema, req.mutable_schema());

    Status s = CreateTable(&req, &resp, rpc);
    // We do not lock here so it is technically possible that the table was already created.
    // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
    if (!s.ok() && !s.IsAlreadyPresent()) {
      return s;
    }
  }
  return Status::OK();
}

Status CatalogManager::IsCdcStateTableCreated(IsCreateTableDoneResponsePB* resp) {
  IsCreateTableDoneRequestPB req;

  req.mutable_table()->set_table_name(kCdcStateTableName);
  req.mutable_table()->mutable_namespace_()->set_name(kSystemNamespaceName);

  return IsCreateTableDone(&req, resp);
}

Status CatalogManager::DeleteCDCStreamsForTable(const TableId& table_id) {
  return DeleteCDCStreamsForTables({table_id});
}

Status CatalogManager::DeleteCDCStreamsForTables(const vector<TableId>& table_ids) {
  std::ostringstream tid_stream;
  for (const auto& tid : table_ids) {
    tid_stream << " " << tid;
  }
  LOG(INFO) << "Deleting CDC streams for tables:" << tid_stream.str();

  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  for (const auto& tid : table_ids) {
    auto newstreams = FindCDCStreamsForTable(tid);
    streams.insert(streams.end(), newstreams.begin(), newstreams.end());
  }

  if (streams.empty()) {
    return Status::OK();
  }

  return DeleteCDCStreams(streams);
}

std::vector<scoped_refptr<CDCStreamInfo>> CatalogManager::FindCDCStreamsForTable(
    const TableId& table_id) {
  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  std::shared_lock<LockType> l(lock_);

  for (const auto& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if (ltm->data().table_id() == table_id) {
      streams.push_back(entry.second);
    }
  }
  return streams;
}

void CatalogManager::GetAllCDCStreams(std::vector<scoped_refptr<CDCStreamInfo>>* streams) {
  streams->clear();
  streams->reserve(cdc_stream_map_.size());
  std::shared_lock<LockType> l(lock_);
  for (const CDCStreamInfoMap::value_type& e : cdc_stream_map_) {
    streams->push_back(e.second);
  }
}

Status CatalogManager::CreateCDCStream(const CreateCDCStreamRequestPB* req,
                                       CreateCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "CreateCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();

  RETURN_NOT_OK(CheckOnline());

  TableIdentifierPB table_identifier;
  table_identifier.set_table_id(req->table_id());

  scoped_refptr<TableInfo> table;
  RETURN_NOT_OK(FindTable(table_identifier, &table));
  if (table == nullptr) {
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND,
                      STATUS(NotFound, "Table not found", req->table_id()));
  }

  {
    auto l = table->LockForRead();
    if (l->data().started_deleting()) {
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND,
                        STATUS(NotFound, "Table does not exist", req->table_id()));
    }
  }

  AlterTableRequestPB alter_table_req;
  alter_table_req.mutable_table()->set_table_id(req->table_id());
  alter_table_req.set_wal_retention_secs(FLAGS_cdc_wal_retention_time_secs);
  AlterTableResponsePB alter_table_resp;
  Status s = this->AlterTable(&alter_table_req, &alter_table_resp, rpc);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR,
        STATUS_SUBSTITUTE(InternalError,
            "Unable to change the WAL retention time for table $0", req->table_id()));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    TRACE("Acquired catalog manager lock");
    std::lock_guard<LockType> l(lock_);

    // Construct the CDC stream.
    CDCStreamId new_id = GenerateId(SysRowEntry::CDC_STREAM);
    stream = make_scoped_refptr<CDCStreamInfo>(new_id);
    stream->mutable_metadata()->StartMutation();
    SysCDCStreamEntryPB *metadata = &stream->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_table_id(table->id());
    metadata->mutable_options()->CopyFrom(req->options());

    // Add the stream to the in-memory map.
    cdc_stream_map_[stream->id()] = stream;
    resp->set_stream_id(stream->id());
  }
  TRACE("Inserted new CDC stream into CatalogManager maps");

  // Update the on-disk system catalog.
  s = sys_catalog_->AddItem(stream.get(), leader_ready_term_);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute(
        "An error occurred while inserting CDC stream into sys-catalog: $0", s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }
  TRACE("Wrote CDC stream to sys-catalog");

  // Commit the in-memory state.
  stream->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Created CDC stream " << stream->ToString();

  RETURN_NOT_OK(CreateCdcStateTableIfNeeded(rpc));
  return Status::OK();
}

Status CatalogManager::DeleteCDCStream(const DeleteCDCStreamRequestPB* req,
                                       DeleteCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteCDCStream request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  if (req->stream_id_size() < 1) {
    Status s = STATUS(InvalidArgument, "No CDC Stream ID given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  std::string streams_str;
  {
    std::shared_lock<LockType> l(lock_);
    for (const auto& stream_id : req->stream_id()) {
      auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);
      if (stream == nullptr) {
        Status s = STATUS(NotFound, "CDC stream does not exist", req->DebugString());
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
      }
      streams.push_back(stream);
      if (!streams_str.empty()) {
        streams_str += ", ";
      }
      streams_str += stream->ToString();
    }
  }

  Status s = DeleteCDCStreams(streams);
  if (!s.ok()) {
    if (s.IsIllegalState()) {
      PANIC_RPC(rpc, s.message().ToString());
    }
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  LOG(INFO) << "Successfully deleted CDC streams " << streams_str
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::DeleteCDCStreams(const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
  std::vector<std::unique_ptr<CDCStreamInfo::lock_type>> locks;
  locks.reserve(streams.size());
  for (auto& stream : streams) {
    locks.push_back(stream->LockForWrite());
  }

  std::vector<CDCStreamInfo*> items;
  items.reserve(streams.size());
  for (const auto& stream : streams) {
    items.push_back(stream.get());
  }

  Status s = sys_catalog_->DeleteItems(items, leader_ready_term_);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys-catalog: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return s;
  }

  // Remove it from the map.
  TRACE("Removing from CDC stream maps");
  {
    std::lock_guard<LockType> l(lock_);
    for (const auto& stream : streams) {
      if (cdc_stream_map_.erase(stream->id()) < 1) {
        return STATUS(IllegalState, "Could not remove CDC stream from map", stream->id());
      }
    }
  }

  for (auto& lock : locks) {
    lock->Commit();
  }
  return Status::OK();
}

Status CatalogManager::GetCDCStream(const GetCDCStreamRequestPB* req,
                                    GetCDCStreamResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "GetCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();
  RETURN_NOT_OK(CheckOnline());


  if (!req->has_stream_id()) {
    Status s = STATUS(InvalidArgument, "CDC Stream ID must be provided", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    std::shared_lock<LockType> l(lock_);

    stream = FindPtrOrNull(cdc_stream_map_, req->stream_id());
    if (stream == nullptr) {
      Status s = STATUS(NotFound, "Could not find CDC stream", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
  }

  auto stream_lock = stream->LockForRead();

  CDCStreamInfoPB* stream_info = resp->mutable_stream();

  stream_info->set_stream_id(stream->id());
  stream_info->set_table_id(stream_lock->data().table_id());
  stream_info->mutable_options()->CopyFrom(stream_lock->data().options());

  return Status::OK();
}

Status CatalogManager::ListCDCStreams(const ListCDCStreamsRequestPB* req,
                                      ListCDCStreamsResponsePB* resp) {

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;
  bool filter_table = req->has_table_id();
  if (filter_table) {
    // Lookup the table and verify that it exists.
    TableIdentifierPB table_identifier;
    table_identifier.set_table_id(req->table_id());

    RETURN_NOT_OK(FindTable(table_identifier, &table));
    if (table == nullptr) {
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND,
                        STATUS(NotFound, "Table not found", req->table_id()));
    }
  }

  std::shared_lock<LockType> l(lock_);

  for (const CDCStreamInfoMap::value_type& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if (filter_table && table->id() != ltm->data().table_id()) {
      continue; // Skip streams from other tables.
    }

    CDCStreamInfoPB* stream = resp->add_streams();
    stream->set_stream_id(entry.second->id());
    stream->set_table_id(ltm->data().table_id());
    stream->mutable_options()->CopyFrom(ltm->data().options());
  }
  return Status::OK();
}

bool CatalogManager::CDCStreamExistsUnlocked(const CDCStreamId& stream_id) {
  DCHECK(lock_.is_locked());
  if (FindPtrOrNull(cdc_stream_map_, stream_id) == nullptr) {
    return false;
  }
  return true;
}

/*
 * UniverseReplication is setup in 3 stages within the Catalog Manager
 * 1. SetupUniverseReplication: Creates the persistent entry and validates input
 * 2. GetTableSchemaCallback:   Validates compatibility between Producer & Consumer Tables
 * 3. CreateCDCStreamCallback:  Setup RPC connections for CDC Streaming
 * 4. InitCDCConsumer:          Initializes the Consumer architecture to begin tailing data
 */
Status CatalogManager::SetupUniverseReplication(const SetupUniverseReplicationRequestPB* req,
                                                SetupUniverseReplicationResponsePB* resp,
                                                rpc::RpcContext* rpc) {
  LOG(INFO) << "SetupUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();

  // Sanity checking section.
  RETURN_NOT_OK(CheckOnline());

  if (!req->has_producer_id()) {
    Status s = STATUS(InvalidArgument, "Producer universe ID must be provided", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  if (req->producer_master_addresses_size() <= 0) {
    Status s = STATUS(InvalidArgument, "Producer master address must be provided",
                      req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  scoped_refptr<UniverseReplicationInfo> ri;
  {
    TRACE("Acquired catalog manager lock");
    std::shared_lock<LockType> l(lock_);

    if (FindPtrOrNull(universe_replication_map_, req->producer_id()) != nullptr) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST,
                        STATUS(InvalidArgument, "Producer already present", req->producer_id()));
    }
  }

  // Create an entry in the system catalog DocDB for this new universe replication.
  ri = new UniverseReplicationInfo(req->producer_id());
  ri->mutable_metadata()->StartMutation();
  SysUniverseReplicationEntryPB *metadata = &ri->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_producer_id(req->producer_id());
  metadata->mutable_producer_master_addresses()->CopyFrom(req->producer_master_addresses());
  metadata->mutable_tables()->CopyFrom(req->producer_table_ids());
  metadata->set_state(SysUniverseReplicationEntryPB::INITIALIZING);

  Status s = sys_catalog_->AddItem(ri.get(), leader_ready_term_);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute(
        "An error occurred while inserting universe replication info into sys-catalog: $0",
        s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }
  TRACE("Wrote universe replication info to sys-catalog");

  // Commit the in-memory state now that it's added to the persistent catalog.
  ri->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Setup universe replication from producer " << ri->ToString();

  {
    std::lock_guard<LockType> l(lock_);
    universe_replication_map_[ri->id()] = ri;
  }

  // Initialize the CDC Stream by querying the Producer server for RPC sanity checks.
  auto result = ri->GetOrCreateCDCRpcTasks(req->producer_master_addresses());
  if (!result.ok()) {
    MarkUniverseReplicationFailed(ri);
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, result.status());
  }
  auto cdc_rpc = *result;

  // For each table, run an async RPC task to verify a sufficient Producer:Consumer schema match.
  for (int i = 0; i < req->producer_table_ids_size(); i++) {
    auto table_info = std::make_shared<client::YBTableInfo>();

    // SETUP CONTINUES after this async call.
    s = cdc_rpc->client()->GetTableSchemaById(
        req->producer_table_ids(i), table_info,
        Bind(&enterprise::CatalogManager::GetTableSchemaCallback, Unretained(this),
             ri->id(), table_info));
    if (!s.ok()) {
      MarkUniverseReplicationFailed(ri);
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
  }

  LOG(INFO) << "Started schema validation for universe replication " << ri->ToString();
  return Status::OK();
}

void CatalogManager::MarkUniverseReplicationFailed(
    scoped_refptr<UniverseReplicationInfo> universe) {
  auto l = universe->LockForWrite();
  if (l->data().pb.state() == SysUniverseReplicationEntryPB::DELETED) {
    l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED_ERROR);
  } else {
    l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
  }

  // Update sys_catalog.
  Status status = sys_catalog_->UpdateItem(universe.get(), leader_ready_term_);
  if (!status.ok()) {
    status = status.CloneAndPrepend(
        Substitute("An error occurred while updating sys-catalog universe replication entry: $0",
                   status.ToString()));
    LOG(WARNING) << status.ToString();
    return;
  }
  l->Commit();
}

void CatalogManager::GetTableSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<client::YBTableInfo>& info,
    const Status& s) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    std::shared_lock<LockType> catalog_lock(lock_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe);
    LOG(ERROR) << "Error getting schema for table " << info->table_id << ": " << s.ToString();
    return;
  }

  // Get corresponding table schema on local universe.
  GetTableSchemaRequestPB req;
  GetTableSchemaResponsePB resp;

  auto* table = req.mutable_table();
  table->set_table_name(info->table_name.table_name());
  table->mutable_namespace_()->set_name(info->table_name.namespace_name());
  table->mutable_namespace_()->set_database_type(
      GetDatabaseTypeForTable(client::YBTable::ClientToPBTableType(info->table_type)));

  // Since YSQL tables are not present in table map, we first need to list tables to get the table
  // ID and then get table schema.
  // Remove this once table maps are fixed for YSQL.
  ListTablesRequestPB list_req;
  ListTablesResponsePB list_resp;

  list_req.set_name_filter(info->table_name.table_name());
  Status status = ListTables(&list_req, &list_resp);
  if (!status.ok() || list_resp.has_error()) {
    LOG(ERROR) << "Error while listing table: " << status.ToString();
    MarkUniverseReplicationFailed(universe);
    return;
  }

  // TODO: This does not work for situation where tables in different YSQL schemas have the same
  // name. This will be fixed as part of #1476.
  for (const auto& t : list_resp.tables()) {
    if (t.name() == info->table_name.table_name() &&
        t.namespace_().name() == info->table_name.namespace_name()) {
      table->set_table_id(t.id());
      break;
    }
  }

  if (!table->has_table_id()) {
    LOG(ERROR) << "Could not find matching table for " << info->table_name.ToString();
    MarkUniverseReplicationFailed(universe);
    return;
  }

  // We have a table match.  Now get the table schema and validate
  status = GetTableSchema(&req, &resp);
  if (!status.ok() || resp.has_error()) {
    LOG(ERROR) << "Error while getting table schema: " << status.ToString();
    MarkUniverseReplicationFailed(universe);
    return;
  }

  auto result = info->schema.Equals(resp.schema());
  if (!result.ok() || !*result) {
    LOG(ERROR) << "Source and target schemas don't match: Source: " << info->table_id
               << ", Target: " << resp.identifier().table_id()
               << ", Source schema: " << info->schema.ToString()
               << ", Target schema: " << resp.schema().DebugString();
    MarkUniverseReplicationFailed(universe);
    return;
  }

  auto l = universe->LockForWrite();
  auto master_addresses = l->data().pb.producer_master_addresses();

  auto res = universe->GetOrCreateCDCRpcTasks(master_addresses);
  if (!res.ok()) {
    LOG(ERROR) << "Error while setting up client for producer " << universe->id();
    l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);

    Status status = sys_catalog_->UpdateItem(universe.get(), leader_ready_term_);
    if (!status.ok()) {
      status = status.CloneAndPrepend(
          Substitute("An error occurred while updating sys-catalog universe replication entry: $0",
                     status.ToString()));
      LOG(WARNING) << status.ToString();
      return;
    }
    l->Commit();
    return;
  }
  auto cdc_rpc = *res;

  if (l->data().is_deleted_or_failed()) {
    // Nothing to do since universe is being deleted.
    return;
  }

  auto map = l->mutable_data()->pb.mutable_validated_tables();
  (*map)[info->table_id] = resp.identifier().table_id();

  // Now, all tables are validated.  Create CDC stream for each.
  if (l->mutable_data()->pb.validated_tables_size() == l->mutable_data()->pb.tables_size()) {
    l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::VALIDATED);

    std::unordered_map<std::string, std::string> options;
    options.reserve(2);
    options.emplace(cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
    options.emplace(cdc::kRecordFormat, CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));

    auto tables = l->data().pb.tables();
    for (const auto& table : tables) {
      LOG(INFO) << "Creating CDC stream on table " << table;
      cdc_rpc->client()->CreateCDCStream(
        table, options, std::bind(&enterprise::CatalogManager::CreateCDCStreamCallback, this,
                                  universe->id(), table, std::placeholders::_1));
    }
  }

  // Update sys_catalog.
  status = sys_catalog_->UpdateItem(universe.get(), leader_ready_term_);
  if (!status.ok()) {
    status = status.CloneAndPrepend(
        Substitute("An error occurred while updating sys-catalog universe replication entry: $0",
                   status.ToString()));
    LOG(WARNING) << status.ToString();
    return;
  }
  l->Commit();
}

void CatalogManager::CreateCDCStreamCallback(
    const std::string& universe_id, const TableId& table_id, const Result<CDCStreamId>& stream_id) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    std::shared_lock<LockType> catalog_lock(lock_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!stream_id.ok()) {
    LOG(ERROR) << "Error setting up CDC stream for table " << table_id;
    MarkUniverseReplicationFailed(universe);
    return;
  }

  auto l = universe->LockForWrite();
  if (l->data().is_deleted_or_failed()) {
    // Nothing to do if universe is being deleted.
    return;
  }

  auto map = l->mutable_data()->pb.mutable_table_streams();
  (*map)[table_id] = *stream_id;

  // This functions as a barrier: waiting for the last RPC call from GetTableSchemaCallback.
  if (l->mutable_data()->pb.table_streams_size() == l->data().pb.tables_size()) {
    // Register CDC consumers for all tables and start replication.
    LOG(INFO) << "Registering CDC consumers for universe " << universe->id();

    auto validated_tables = l->data().pb.validated_tables();

    std::vector<CDCConsumerStreamInfo> consumer_info;
    consumer_info.reserve(l->data().pb.tables_size());
    for (const auto& table : validated_tables) {
      CDCConsumerStreamInfo info;
      info.producer_table_id = table.first;
      info.consumer_table_id = table.second;
      info.stream_id = (*map)[info.producer_table_id];
      consumer_info.push_back(info);
    }

    std::vector<HostPort> hp;
    HostPortsFromPBs(l->data().pb.producer_master_addresses(), &hp);

    Status s = InitCDCConsumer(consumer_info, HostPort::ToCommaSeparatedString(hp),
                               l->data().pb.producer_id());
    if (!s.ok()) {
      LOG(ERROR) << "Error registering subscriber: " << s.ToString();
      l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
    } else {
      l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
    }
  }

  // Update sys_catalog with new producer table id info.
  Status status = sys_catalog_->UpdateItem(universe.get(), leader_ready_term_);
  if (!status.ok()) {
    status = status.CloneAndPrepend(
        Substitute("An error occurred while updating sys-catalog universe replication entry: $0",
                   status.ToString()));
    LOG(WARNING) << status.ToString();
    return;
  }
  l->Commit();
}

Status CatalogManager::InitCDCConsumer(
    const std::vector<CDCConsumerStreamInfo>& consumer_info,
    const std::string& master_addrs,
    const std::string& producer_universe_uuid) {

  std::unordered_set<HostPort, HostPortHash> tserver_addrs;

  // Get the tablets in the consumer table.
  cdc::ProducerEntryPB producer_entry;
  for (const auto& stream_info : consumer_info) {
    GetTableLocationsRequestPB consumer_table_req;
    consumer_table_req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
    GetTableLocationsResponsePB consumer_table_resp;
    TableIdentifierPB table_identifer;
    table_identifer.set_table_id(stream_info.consumer_table_id);
    *(consumer_table_req.mutable_table()) = table_identifer;
    RETURN_NOT_OK(GetTableLocations(&consumer_table_req, &consumer_table_resp));
    cdc::StreamEntryPB stream_entry;
    // Get producer tablets and map them to the consumer tablets
    RETURN_NOT_OK(CreateTabletMapping(
        stream_info.producer_table_id, stream_info.consumer_table_id, master_addrs,
        consumer_table_resp, &tserver_addrs, &stream_entry));
    (*producer_entry.mutable_stream_map())[stream_info.stream_id] = std::move(stream_entry);
  }

  // Log the Network topology of the Producer Cluster
  auto master_addrs_list = StringSplit(master_addrs, ',');
  producer_entry.mutable_master_addrs()->Reserve(master_addrs_list.size());
  for (const auto& addr : master_addrs_list) {
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, 0));
    HostPortToPB(hp, producer_entry.add_master_addrs());
  }

  producer_entry.mutable_tserver_addrs()->Reserve(tserver_addrs.size());
  for (const auto& addr : tserver_addrs) {
    HostPortToPB(addr, producer_entry.add_tserver_addrs());
  }

  auto l = cluster_config_->LockForWrite();
  auto producer_map = l->mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto it = producer_map->find(producer_universe_uuid);
  if (it != producer_map->end()) {
    return STATUS(InvalidArgument, "Already created a consumer for this universe");
  }

  // Store this topology as metadata in DocDB.
  (*producer_map)[producer_universe_uuid] = std::move(producer_entry);
  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(sys_catalog_->UpdateItem(cluster_config_.get(), leader_ready_term_));
  l->Commit();

  return Status::OK();
}

Status CatalogManager::DeleteUniverseReplication(const DeleteUniverseReplicationRequestPB* req,
                                                 DeleteUniverseReplicationResponsePB* resp,
                                                 rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteUniverseReplication request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  if (!req->has_producer_id()) {
    Status s = STATUS(InvalidArgument, "Producer universe ID required", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  scoped_refptr<UniverseReplicationInfo> ri;
  {
    std::shared_lock<LockType> catalog_lock(lock_);
    TRACE("Acquired catalog manager lock");

    ri = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (ri == nullptr) {
      Status s = STATUS(NotFound, "Universe replication info does not exist", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
  }

  auto l = ri->LockForWrite();
  l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED);

  // Delete subscribers.
  LOG(INFO) << "Deleting subscribers for producer " << req->producer_id();
  {
    auto cl = cluster_config_->LockForWrite();
    auto producer_map = cl->mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(req->producer_id());
    if (it != producer_map->end()) {
      producer_map->erase(it);
    }
    cl->mutable_data()->pb.set_version(cl->mutable_data()->pb.version() + 1);
    RETURN_NOT_OK(sys_catalog_->UpdateItem(cluster_config_.get(), leader_ready_term_));
    cl->Commit();
  }

  if (!l->data().pb.table_streams().empty()) {
    // Delete CDC streams.
    auto result = ri->GetOrCreateCDCRpcTasks(l->data().pb.producer_master_addresses());
    if (!result.ok()) {
      LOG(WARNING) << "Unable to create cdc rpc task. CDC streams won't be deleted: " << result;
    } else {
      auto cdc_rpc = *result;

      vector<CDCStreamId> streams;
      for (const auto& table : l->data().pb.table_streams()) {
        streams.push_back(table.second);
      }
      auto s = cdc_rpc->client()->DeleteCDCStream(streams);
      if (!s.ok()) {
        LOG(WARNING) << "Unable to delete CDC stream " << s;
      }
    }
  }

  // Delete universe.
  DeleteUniverseReplicationUnlocked(ri);
  l->Commit();

  LOG(INFO) << "Processed delete universe replication " << ri->ToString()
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

void CatalogManager::DeleteUniverseReplicationUnlocked(
    scoped_refptr<UniverseReplicationInfo> universe) {
  // Assumes that caller has locked universe.
  Status s = sys_catalog_->DeleteItem(universe.get(), leader_ready_term_);
  if (!s.ok()) {
    LOG(ERROR) << "An error occured while updating sys-catalog: " << s.ToString()
               << ": universe_id: " << universe->id();
    return;
  }

  // Remove it from the map.
  std::lock_guard<LockType> catalog_lock(lock_);
  if (universe_replication_map_.erase(universe->id()) < 1) {
    LOG(ERROR) << "An error occured while removing replication info from map: " << s.ToString()
               << ": universe_id: " << universe->id();
  }
}

Status CatalogManager::SetUniverseReplicationEnabled(
    const SetUniverseReplicationEnabledRequestPB* req,
    SetUniverseReplicationEnabledResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing SetUniverseReplicationEnabled request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  // Sanity Checking Cluster State and Input.
  RETURN_NOT_OK(CheckOnline());

  if (!req->has_producer_id()) {
    Status s = STATUS(InvalidArgument, "Producer universe ID must be provided", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }
  if (!req->has_is_enabled()) {
    Status s = STATUS(InvalidArgument, "Must explicitly set whether to enable", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    std::shared_lock<LockType> l(lock_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      Status s = STATUS(NotFound, "Could not find CDC producer universe", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
  }

  // Update the Master's Universe Config with the new state.
  {
    auto l = universe->LockForWrite();
    if (l->data().pb.state() != SysUniverseReplicationEntryPB::DISABLED &&
        l->data().pb.state() != SysUniverseReplicationEntryPB::ACTIVE) {
      Status s = STATUS(InvalidArgument,
          Format("Universe Replication in invalid state: $0.  Retry or Delete.",
              SysUniverseReplicationEntryPB::State_Name(l->data().pb.state())),
          req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
    if (req->is_enabled()) {
        l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
    } else { // DISABLE.
        l->mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DISABLED);
    }
    RETURN_NOT_OK(sys_catalog_->UpdateItem(universe.get(), leader_ready_term_));
    l->Commit();
  }

  // Modify the Consumer Registry, which will fan out this info to all TServers on heartbeat.
  {
    auto l = cluster_config_->LockForWrite();
    auto producer_map = l->mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(req->producer_id());
    if (it == producer_map->end()) {
      LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << req->producer_id();
      Status s = STATUS(NotFound, "Could not find CDC producer universe", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
    (*it).second.set_disable_stream(!req->is_enabled());
    l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
    RETURN_NOT_OK(sys_catalog_->UpdateItem(cluster_config_.get(), leader_ready_term_));
    l->Commit();
  }

  return Status::OK();
}

Status CatalogManager::GetUniverseReplication(const GetUniverseReplicationRequestPB* req,
                                              GetUniverseReplicationResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();
  RETURN_NOT_OK(CheckOnline());


  if (!req->has_producer_id()) {
    Status s = STATUS(InvalidArgument, "Producer universe ID must be provided", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    std::shared_lock<LockType> l(lock_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      Status s = STATUS(NotFound, "Could not find CDC producer universe", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
  }

  auto l = universe->LockForRead();
  resp->mutable_entry()->CopyFrom(l->data().pb);
  return Status::OK();
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
