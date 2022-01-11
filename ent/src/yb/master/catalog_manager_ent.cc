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

#include <memory>
#include <regex>
#include <set>
#include <unordered_set>
#include <google/protobuf/util/message_differencer.h>

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_error.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/cdc_service.h"

#include "yb/client/client-internal.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_name.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_write_batch.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-internal.h"
#include "yb/master/async_snapshot_tasks.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/encryption_manager.h"
#include "yb/master/restore_sys_catalog_state.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/service_util.h"

#include "yb/util/cast.h"
#include "yb/util/date_time.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/service_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/tostring.h"
#include "yb/util/string_util.h"
#include "yb/util/trace.h"

using namespace std::literals;
using namespace std::placeholders;

using std::string;
using std::unique_ptr;
using std::vector;

using google::protobuf::RepeatedPtrField;
using google::protobuf::util::MessageDifferencer;
using strings::Substitute;

DEFINE_int32(cdc_state_table_num_tablets, 0,
             "Number of tablets to use when creating the CDC state table. "
             "0 to use the same default num tablets as for regular tables.");

DEFINE_int32(cdc_wal_retention_time_secs, 4 * 3600,
             "WAL retention time in seconds to be used for tables for which a CDC stream was "
             "created.");
DECLARE_int32(master_rpc_timeout_ms);

DEFINE_bool(enable_transaction_snapshots, true,
            "The flag enables usage of transaction aware snapshots.");
TAG_FLAG(enable_transaction_snapshots, hidden);
TAG_FLAG(enable_transaction_snapshots, advanced);
TAG_FLAG(enable_transaction_snapshots, runtime);

namespace yb {

using rpc::RpcContext;
using pb_util::ParseFromSlice;

namespace master {
namespace enterprise {

namespace {

CHECKED_STATUS CheckStatus(const Status& status, const char* action) {
  if (status.ok()) {
    return status;
  }

  const Status s = status.CloneAndPrepend(std::string("An error occurred while ") + action);
  LOG(WARNING) << s;
  return s;
}

CHECKED_STATUS CheckLeaderStatus(const Status& status, const char* action) {
  return CheckIfNoLongerLeader(CheckStatus(status, action));
}

template<class RespClass>
CHECKED_STATUS CheckLeaderStatusAndSetupError(
    const Status& status, const char* action, RespClass* resp) {
  return CheckIfNoLongerLeaderAndSetupError(CheckStatus(status, action), resp);
}

} // namespace

////////////////////////////////////////////////////////////
// Snapshot Loader
////////////////////////////////////////////////////////////

class SnapshotLoader : public Visitor<PersistentSnapshotInfo> {
 public:
  explicit SnapshotLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const SnapshotId& snapshot_id, const SysSnapshotEntryPB& metadata) override {
    if (TryFullyDecodeTxnSnapshotId(snapshot_id)) {
      // Transaction aware snapshots should be already loaded.
      return Status::OK();
    }
    return VisitNonTransactionAwareSnapshot(snapshot_id, metadata);
  }

  CHECKED_STATUS VisitNonTransactionAwareSnapshot(
      const SnapshotId& snapshot_id, const SysSnapshotEntryPB& metadata) {

    // Setup the snapshot info.
    auto snapshot_info = make_scoped_refptr<SnapshotInfo>(snapshot_id);
    auto l = snapshot_info->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // Add the snapshot to the IDs map (if the snapshot is not deleted).
    auto emplace_result = catalog_manager_->non_txn_snapshot_ids_map_.emplace(
        snapshot_id, std::move(snapshot_info));
    CHECK(emplace_result.second) << "Snapshot already exists: " << snapshot_id;

    LOG(INFO) << "Loaded metadata for snapshot (id=" << snapshot_id << "): "
              << emplace_result.first->second->ToString() << ": " << metadata.ShortDebugString();
    l.Commit();
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

  Status Visit(const CDCStreamId& stream_id, const SysCDCStreamEntryPB& metadata)
      REQUIRES(catalog_manager_->mutex_) {
    DCHECK(!ContainsKey(catalog_manager_->cdc_stream_map_, stream_id))
        << "CDC stream already exists: " << stream_id;

    scoped_refptr<TableInfo> table =
        FindPtrOrNull(*catalog_manager_->table_ids_map_, metadata.table_id());

    if (!table) {
      LOG(ERROR) << "Invalid table ID " << metadata.table_id() << " for stream " << stream_id;
      // TODO (#2059): Potentially signals a race condition that table got deleted while stream was
      // being created.
      // Log error and continue without loading the stream.
      return Status::OK();
    }

    // Setup the CDC stream info.
    auto stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // If the table has been deleted, then mark this stream as DELETING so it can be deleted by the
    // catalog manager background thread. Otherwise if this stream is missing an entry
    // for state, then mark its state as Active.
    if (table->LockForRead()->is_deleting() && !l.data().is_deleting()) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::DELETING);
    } else if (!l.mutable_data()->pb.has_state()) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::ACTIVE);
    }

    // Add the CDC stream to the CDC stream map.
    catalog_manager_->cdc_stream_map_[stream->id()] = stream;
    catalog_manager_->cdc_stream_tables_count_map_[metadata.table_id()]++;

    l.Commit();

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

  Status Visit(const std::string& producer_id, const SysUniverseReplicationEntryPB& metadata)
      REQUIRES(catalog_manager_->mutex_) {
    DCHECK(!ContainsKey(catalog_manager_->universe_replication_map_, producer_id))
        << "Producer universe already exists: " << producer_id;

    // Setup the universe replication info.
    UniverseReplicationInfo* const ri = new UniverseReplicationInfo(producer_id);
    {
      auto l = ri->LockForWrite();
      l.mutable_data()->pb.CopyFrom(metadata);

      if (!l->is_active() && !l->is_deleted_or_failed()) {
        // Replication was not fully setup.
        LOG(WARNING) << "Universe replication in transient state: " << producer_id;

        // TODO: Should we delete all failed universe replication items?
      }

      // Add universe replication info to the universe replication map.
      catalog_manager_->universe_replication_map_[ri->id()] = ri;

      l.Commit();
    }

    // Also keep track of consumer tables.
    for (const auto& table : metadata.validated_tables()) {
      CDCStreamId stream_id = FindWithDefault(metadata.table_streams(), table.first, "");
      if (stream_id.empty()) {
        LOG(WARNING) << "Unable to find stream id for table: " << table.first;
        continue;
      }
      catalog_manager_->xcluster_consumer_tables_to_stream_map_[table.second].emplace(
          metadata.producer_id(), stream_id);
    }

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

CatalogManager::~CatalogManager() {
  if (StartShutdown()) {
    CompleteShutdown();
  }
}

void CatalogManager::CompleteShutdown() {
  snapshot_coordinator_.Shutdown();
  // Call shutdown on base class before exiting derived class destructor
  // because BgTasks is part of base & uses this derived class on Shutdown.
  super::CompleteShutdown();
}

Status CatalogManager::RunLoaders(int64_t term) {
  RETURN_NOT_OK(super::RunLoaders(term));

  // Clear the snapshots.
  non_txn_snapshot_ids_map_.clear();

  // Clear CDC stream map.
  cdc_stream_map_.clear();
  cdc_stream_tables_count_map_.clear();

  // Clear universe replication map.
  universe_replication_map_.clear();
  xcluster_consumer_tables_to_stream_map_.clear();

  LOG_WITH_FUNC(INFO) << "Loading snapshots into memory.";
  unique_ptr<SnapshotLoader> snapshot_loader(new SnapshotLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(snapshot_loader.get()),
      "Failed while visiting snapshots in sys catalog");

  LOG_WITH_FUNC(INFO) << "Loading CDC streams into memory.";
  auto cdc_stream_loader = std::make_unique<CDCStreamLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(cdc_stream_loader.get()),
      "Failed while visiting CDC streams in sys catalog");

  LOG_WITH_FUNC(INFO) << "Loading universe replication info into memory.";
  auto universe_replication_loader = std::make_unique<UniverseReplicationLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(universe_replication_loader.get()),
      "Failed while visiting universe replication info in sys catalog");

  return Status::OK();
}

Status CatalogManager::CreateSnapshot(const CreateSnapshotRequestPB* req,
                                      CreateSnapshotResponsePB* resp,
                                      RpcContext* rpc) {
  LOG(INFO) << "Servicing CreateSnapshot request: " << req->ShortDebugString();

  if (FLAGS_enable_transaction_snapshots && req->transaction_aware()) {
    return CreateTransactionAwareSnapshot(*req, resp, rpc);
  }

  if (req->has_schedule_id()) {
    auto schedule_id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(req->schedule_id()));
    auto snapshot_id = snapshot_coordinator_.CreateForSchedule(
        schedule_id, leader_ready_term(), rpc->GetClientDeadline());
    if (!snapshot_id.ok()) {
      LOG(INFO) << "Create snapshot failed: " << snapshot_id.status();
      return snapshot_id.status();
    }
    resp->set_snapshot_id(snapshot_id->data(), snapshot_id->size());
    return Status::OK();
  }

  return CreateNonTransactionAwareSnapshot(req, resp, rpc);
}

Status CatalogManager::CreateNonTransactionAwareSnapshot(
    const CreateSnapshotRequestPB* req,
    CreateSnapshotResponsePB* resp,
    RpcContext* rpc) {
  SnapshotId snapshot_id;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    // Verify that the system is not in snapshot creating/restoring state.
    if (!current_snapshot_id_.empty()) {
      return STATUS(IllegalState,
                    Format(
                        "Current snapshot id: $0. Parallel snapshot operations are not supported"
                        ": $1", current_snapshot_id_, req),
                    MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION));
    }

    // Create a new snapshot UUID.
    snapshot_id = GenerateIdUnlocked(SysRowEntryType::SNAPSHOT);
  }

  vector<scoped_refptr<TabletInfo>> all_tablets;

  // Create in memory snapshot data descriptor.
  scoped_refptr<SnapshotInfo> snapshot(new SnapshotInfo(snapshot_id));
  snapshot->mutable_metadata()->StartMutation();
  snapshot->mutable_metadata()->mutable_dirty()->pb.set_state(SysSnapshotEntryPB::CREATING);

  auto tables = VERIFY_RESULT(CollectTables(req->tables(),
                                            req->add_indexes(),
                                            true /* include_parent_colocated_table */));
  std::unordered_set<NamespaceId> added_namespaces;
  for (const auto& table : tables) {
    snapshot->AddEntries(table, &added_namespaces);
    all_tablets.insert(all_tablets.end(), table.tablet_infos.begin(), table.tablet_infos.end());
  }

  VLOG(1) << "Snapshot " << snapshot->ToString()
          << ": PB=" << snapshot->mutable_metadata()->mutable_dirty()->pb.DebugString();

  // Write the snapshot data descriptor to the system catalog (in "creating" state).
  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_->Upsert(leader_ready_term(), snapshot),
      "inserting snapshot into sys-catalog"));
  TRACE("Wrote snapshot to system catalog");

  // Commit in memory snapshot data descriptor.
  snapshot->mutable_metadata()->CommitMutation();

  // Put the snapshot data descriptor to the catalog manager.
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    // Verify that the snapshot does not exist.
    auto inserted = non_txn_snapshot_ids_map_.emplace(snapshot_id, snapshot).second;
    RSTATUS_DCHECK(inserted, IllegalState, Format("Snapshot already exists: $0", snapshot_id));
    current_snapshot_id_ = snapshot_id;
  }

  // Send CreateSnapshot requests to all TServers (one tablet - one request).
  for (const scoped_refptr<TabletInfo>& tablet : all_tablets) {
    TRACE("Locking tablet");
    auto l = tablet->LockForRead();

    LOG(INFO) << "Sending CreateTabletSnapshot to tablet: " << tablet->ToString();

    // Send Create Tablet Snapshot request to each tablet leader.
    auto call = CreateAsyncTabletSnapshotOp(
        tablet, snapshot_id, tserver::TabletSnapshotOpRequestPB::CREATE_ON_TABLET,
        TabletSnapshotOperationCallback());
    ScheduleTabletSnapshotOp(call);
  }

  resp->set_snapshot_id(snapshot_id);
  LOG(INFO) << "Successfully started snapshot " << snapshot_id << " creation";
  return Status::OK();
}

void CatalogManager::Submit(std::unique_ptr<tablet::Operation> operation, int64_t leader_term) {
  operation->SetTablet(tablet_peer()->tablet());
  tablet_peer()->Submit(std::move(operation), leader_term);
}

Result<SysRowEntries> CatalogManager::CollectEntries(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
    CollectFlags flags) {
  RETURN_NOT_OK(CheckIsLeaderAndReady());
  SysRowEntries entries;
  std::unordered_set<NamespaceId> namespaces;
  auto tables = VERIFY_RESULT(CollectTables(table_identifiers, flags, &namespaces));
  if (!namespaces.empty()) {
    SharedLock lock(mutex_);
    for (const auto& ns_id : namespaces) {
      auto ns_info = VERIFY_RESULT(FindNamespaceByIdUnlocked(ns_id));
      AddInfoEntry(ns_info.get(), entries.mutable_entries());
    }
  }
  for (const auto& table : tables) {
    // TODO(txn_snapshot) use single lock to resolve all tables to tablets
    SnapshotInfo::AddEntries(table, entries.mutable_entries(), /* tablet_infos= */ nullptr,
                             &namespaces);
  }

  return entries;
}

Result<SysRowEntries> CatalogManager::CollectEntriesForSnapshot(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) {
  return CollectEntries(
      tables,
      CollectFlags{CollectFlag::kAddIndexes, CollectFlag::kIncludeParentColocatedTable,
                   CollectFlag::kSucceedIfCreateInProgress});
}

server::Clock* CatalogManager::Clock() {
  return master_->clock();
}

Status CatalogManager::CreateTransactionAwareSnapshot(
    const CreateSnapshotRequestPB& req, CreateSnapshotResponsePB* resp, rpc::RpcContext* rpc) {
  CollectFlags flags{CollectFlag::kIncludeParentColocatedTable};
  flags.SetIf(CollectFlag::kAddIndexes, req.add_indexes());
  SysRowEntries entries = VERIFY_RESULT(CollectEntries(req.tables(), flags));

  auto snapshot_id = VERIFY_RESULT(snapshot_coordinator_.Create(
      entries, req.imported(), leader_ready_term(), rpc->GetClientDeadline()));
  resp->set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  return Status::OK();
}

Status CatalogManager::ListSnapshots(const ListSnapshotsRequestPB* req,
                                     ListSnapshotsResponsePB* resp) {
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  {
    SharedLock lock(mutex_);
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
      if (!txn_snapshot_id) {
        TRACE("Looking up snapshot");
        scoped_refptr<SnapshotInfo> snapshot_info =
            FindPtrOrNull(non_txn_snapshot_ids_map_, req->snapshot_id());
        if (snapshot_info == nullptr) {
          return STATUS(InvalidArgument, "Could not find snapshot", req->snapshot_id(),
                        MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
        }

        setup_snapshot_pb_lambda(snapshot_info);
      }
    } else {
      for (const SnapshotInfoMap::value_type& entry : non_txn_snapshot_ids_map_) {
        setup_snapshot_pb_lambda(entry.second);
      }
    }
  }

  if (req->prepare_for_backup() && (!req->has_snapshot_id() || !txn_snapshot_id)) {
    return STATUS(
        InvalidArgument, "Request must have correct snapshot_id", (req->has_snapshot_id() ?
        req->snapshot_id() : "None"), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  RETURN_NOT_OK(snapshot_coordinator_.ListSnapshots(
      txn_snapshot_id, req->list_deleted_snapshots(), resp));

  if (req->prepare_for_backup()) {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    // Repack & extend the backup row entries.
    for (SnapshotInfoPB& snapshot : *resp->mutable_snapshots()) {
      snapshot.set_format_version(2);
      SysSnapshotEntryPB& sys_entry = *snapshot.mutable_entry();
      snapshot.mutable_backup_entries()->Reserve(sys_entry.entries_size());

      for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
        BackupRowEntryPB* const backup_entry = snapshot.add_backup_entries();
        // Setup BackupRowEntryPB fields.
        // Set BackupRowEntryPB::pg_schema_name for YSQL table to disambiguate in case tables
        // in different schema have same name.
        if (entry.type() == SysRowEntryType::TABLE) {
          TRACE("Looking up table");
          scoped_refptr<TableInfo> table_info = FindPtrOrNull(*table_ids_map_, entry.id());
          if (table_info == nullptr) {
            return STATUS(
                InvalidArgument, "Table not found by ID", entry.id(),
                MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
          }

          TRACE("Locking table");
          auto l = table_info->LockForRead();
          // PG schema name is available for YSQL table only.
          // Except '<uuid>.colocated.parent.uuid' table ID.
          if (l->table_type() == PGSQL_TABLE_TYPE && !IsColocatedParentTableId(entry.id())) {
            const string pg_schema_name = VERIFY_RESULT(GetPgSchemaName(table_info));
            VLOG(1) << "PG Schema: " << pg_schema_name << " for table " << table_info->ToString();
            backup_entry->set_pg_schema_name(pg_schema_name);
          }
        }

        // Init BackupRowEntryPB::entry.
        backup_entry->mutable_entry()->Swap(&entry);
      }

      sys_entry.clear_entries();
    }
  }

  return Status::OK();
}

Status CatalogManager::ListSnapshotRestorations(const ListSnapshotRestorationsRequestPB* req,
                                                ListSnapshotRestorationsResponsePB* resp) {
  TxnSnapshotRestorationId restoration_id = TxnSnapshotRestorationId::Nil();
  if (!req->restoration_id().empty()) {
    restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(req->restoration_id()));
  }
  TxnSnapshotId snapshot_id = TxnSnapshotId::Nil();
  if (!req->snapshot_id().empty()) {
    snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(req->snapshot_id()));
  }

  return snapshot_coordinator_.ListRestorations(restoration_id, snapshot_id, resp);
}

Status CatalogManager::RestoreSnapshot(const RestoreSnapshotRequestPB* req,
                                       RestoreSnapshotResponsePB* resp) {
  LOG(INFO) << "Servicing RestoreSnapshot request: " << req->ShortDebugString();

  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  if (txn_snapshot_id) {
    HybridTime ht;
    if (req->has_restore_ht()) {
      ht = HybridTime(req->restore_ht());
    }
    TxnSnapshotRestorationId id = VERIFY_RESULT(snapshot_coordinator_.Restore(
        txn_snapshot_id, ht, leader_ready_term()));
    resp->set_restoration_id(id.data(), id.size());
    return Status::OK();
  }

  return RestoreNonTransactionAwareSnapshot(req->snapshot_id());
}

Status CatalogManager::RestoreNonTransactionAwareSnapshot(const string& snapshot_id) {
  LockGuard lock(mutex_);
  TRACE("Acquired catalog manager lock");

  if (!current_snapshot_id_.empty()) {
    return STATUS(
        IllegalState,
        Format(
            "Current snapshot id: $0. Parallel snapshot operations are not supported: $1",
            current_snapshot_id_, snapshot_id),
        MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION));
  }

  TRACE("Looking up snapshot");
  scoped_refptr<SnapshotInfo> snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, snapshot_id);
  if (snapshot == nullptr) {
    return STATUS(InvalidArgument, "Could not find snapshot", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  auto snapshot_lock = snapshot->LockForWrite();

  if (snapshot_lock->started_deleting()) {
    return STATUS(NotFound, "The snapshot was deleted", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  if (!snapshot_lock->is_complete()) {
    return STATUS(IllegalState, "The snapshot state is not complete", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_IS_NOT_READY));
  }

  TRACE("Updating snapshot metadata on disk");
  SysSnapshotEntryPB& snapshot_pb = snapshot_lock.mutable_data()->pb;
  snapshot_pb.set_state(SysSnapshotEntryPB::RESTORING);

  // Update tablet states.
  SetTabletSnapshotsState(SysSnapshotEntryPB::RESTORING, &snapshot_pb);

  // Update sys-catalog with the updated snapshot state.
  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_->Upsert(leader_ready_term(), snapshot),
      "updating snapshot in sys-catalog"));

  // CataloManager lock 'lock_' is still locked here.
  current_snapshot_id_ = snapshot_id;

  // Restore all entries.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    RETURN_NOT_OK(RestoreEntry(entry, snapshot_id));
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock.Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " restoring";
  return Status::OK();
}

Status CatalogManager::RestoreEntry(const SysRowEntry& entry, const SnapshotId& snapshot_id) {
  switch (entry.type()) {
    case SysRowEntryType::NAMESPACE: { // Restore NAMESPACES.
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
    case SysRowEntryType::TABLE: { // Restore TABLES.
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
    case SysRowEntryType::TABLET: { // Restore TABLETS.
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
        auto task = CreateAsyncTabletSnapshotOp(
            tablet, snapshot_id, tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET,
            TabletSnapshotOperationCallback());
        ScheduleTabletSnapshotOp(task);
      }
      break;
    }
    default:
      return STATUS_FORMAT(
          InternalError, "Unexpected entry type in the snapshot: $0", entry.type());
  }

  return Status::OK();
}

Status CatalogManager::DeleteSnapshot(const DeleteSnapshotRequestPB* req,
                                      DeleteSnapshotResponsePB* resp,
                                      RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteSnapshot request: " << req->ShortDebugString();

  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  if (txn_snapshot_id) {
    return snapshot_coordinator_.Delete(
        txn_snapshot_id, leader_ready_term(), rpc->GetClientDeadline());
  }

  return DeleteNonTransactionAwareSnapshot(req->snapshot_id());
}

Status CatalogManager::DeleteNonTransactionAwareSnapshot(const SnapshotId& snapshot_id) {
  LockGuard lock(mutex_);
  TRACE("Acquired catalog manager lock");

  TRACE("Looking up snapshot");
  scoped_refptr<SnapshotInfo> snapshot = FindPtrOrNull(
      non_txn_snapshot_ids_map_, snapshot_id);
  if (snapshot == nullptr) {
    return STATUS(InvalidArgument, "Could not find snapshot", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  auto snapshot_lock = snapshot->LockForWrite();

  if (snapshot_lock->started_deleting()) {
    return STATUS(NotFound, "The snapshot was deleted", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  if (snapshot_lock->is_restoring()) {
    return STATUS(InvalidArgument, "The snapshot is being restored now", snapshot_id,
                  MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION));
  }

  TRACE("Updating snapshot metadata on disk");
  SysSnapshotEntryPB& snapshot_pb = snapshot_lock.mutable_data()->pb;
  snapshot_pb.set_state(SysSnapshotEntryPB::DELETING);

  // Update tablet states.
  SetTabletSnapshotsState(SysSnapshotEntryPB::DELETING, &snapshot_pb);

  // Update sys-catalog with the updated snapshot state.
  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), snapshot),
      "updating snapshot in sys-catalog"));

  // Send DeleteSnapshot requests to all TServers (one tablet - one request).
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    if (entry.type() == SysRowEntryType::TABLET) {
      TRACE("Looking up tablet");
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());
      if (tablet == nullptr) {
        LOG(WARNING) << "Deleting tablet not found " << entry.id();
      } else {
        TRACE("Locking tablet");
        auto l = tablet->LockForRead();

        LOG(INFO) << "Sending DeleteTabletSnapshot to tablet: " << tablet->ToString();
        // Send DeleteSnapshot requests to all TServers (one tablet - one request).
        auto task = CreateAsyncTabletSnapshotOp(
            tablet, snapshot_id, tserver::TabletSnapshotOpRequestPB::DELETE_ON_TABLET,
            TabletSnapshotOperationCallback());
        ScheduleTabletSnapshotOp(task);
      }
    }
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock.Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " deletion";
  return Status::OK();
}

Status CatalogManager::ImportSnapshotPreprocess(const SnapshotInfoPB& snapshot_pb,
                                                ImportSnapshotMetaResponsePB* resp,
                                                NamespaceMap* namespace_map,
                                                ExternalTableSnapshotDataMap* tables_data) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    switch (entry.type()) {
      case SysRowEntryType::NAMESPACE: // Recreate NAMESPACE.
        RETURN_NOT_OK(ImportNamespaceEntry(entry, namespace_map));
        break;
      case SysRowEntryType::TABLE: { // Create TABLE metadata.
          LOG_IF(DFATAL, entry.id().empty()) << "Empty entry id";
          ExternalTableSnapshotData& data = (*tables_data)[entry.id()];

          if (data.old_table_id.empty()) {
            data.old_table_id = entry.id();
            data.table_meta = resp->mutable_tables_meta()->Add();
            data.tablet_id_map = data.table_meta->mutable_tablets_ids();
            data.table_entry_pb = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));

            if (backup_entry.has_pg_schema_name()) {
              data.pg_schema_name = backup_entry.pg_schema_name();
            }
          } else {
            LOG_WITH_FUNC(WARNING) << "Ignoring duplicate table with id " << entry.id();
          }

          LOG_IF(DFATAL, data.old_table_id.empty()) << "Not initialized table id";
        }
        break;
      case SysRowEntryType::TABLET: // Preprocess original tablets.
        RETURN_NOT_OK(PreprocessTabletEntry(entry, tables_data));
        break;
      case SysRowEntryType::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UDTYPE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SYS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::CDC_STREAM: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNIVERSE_REPLICATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT:  FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_SCHEDULE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::DDL_LOG_ENTRY: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNKNOWN:
        FATAL_INVALID_ENUM_VALUE(SysRowEntryType, entry.type());
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotCreateObject(const SnapshotInfoPB& snapshot_pb,
                                                  ImportSnapshotMetaResponsePB* resp,
                                                  NamespaceMap* namespace_map,
                                                  ExternalTableSnapshotDataMap* tables_data,
                                                  CreateObjects create_objects) {
  // Create ONLY TABLES or ONLY INDEXES in accordance to the argument.
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::TABLE) {
      ExternalTableSnapshotData& data = (*tables_data)[entry.id()];
      if ((create_objects == CreateObjects::kOnlyIndexes) == data.is_index()) {
        RETURN_NOT_OK(ImportTableEntry(*namespace_map, *tables_data, &data));
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotWaitForTables(const SnapshotInfoPB& snapshot_pb,
                                                   ImportSnapshotMetaResponsePB* resp,
                                                   ExternalTableSnapshotDataMap* tables_data) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::TABLE) {
      ExternalTableSnapshotData& data = (*tables_data)[entry.id()];
      if (!data.is_index()) {
        RETURN_NOT_OK(WaitForCreateTableToFinish(data.new_table_id));
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotProcessTablets(const SnapshotInfoPB& snapshot_pb,
                                                    ImportSnapshotMetaResponsePB* resp,
                                                    ExternalTableSnapshotDataMap* tables_data) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::TABLET) {
      // Create tablets IDs map.
      RETURN_NOT_OK(ImportTabletEntry(entry, tables_data));
    }
  }

  return Status::OK();
}

template <class RespClass>
void ProcessDeleteObjectStatus(const string& obj_name,
                               const string& id,
                               const RespClass& resp,
                               const Status& s) {
  Status result = s;
  if (result.ok() && resp.has_error()) {
    result = StatusFromPB(resp.error().status());
    LOG_IF(DFATAL, result.ok()) << "Expecting error status";
  }

  if (!result.ok()) {
    LOG_WITH_FUNC(WARNING) << "Failed to delete new " << obj_name << " with id=" << id
                           << ": " << result;
  }
}

void CatalogManager::DeleteNewSnapshotObjects(const NamespaceMap& namespace_map,
                                              const ExternalTableSnapshotDataMap& tables_data) {
  for (const ExternalTableSnapshotDataMap::value_type& entry : tables_data) {
    const TableId& old_id = entry.first;
    const TableId& new_id = entry.second.new_table_id;
    const TableType type = entry.second.table_entry_pb.table_type();

    // Do not delete YSQL objects - it must be deleted via PG API.
    if (new_id.empty() || new_id == old_id || type == TableType::PGSQL_TABLE_TYPE) {
      continue;
    }

    LOG_WITH_FUNC(INFO) << "Deleting new table with id=" << new_id << " old id=" << old_id;
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_id(new_id);
    req.set_is_index_table(entry.second.is_index());
    ProcessDeleteObjectStatus("table", new_id, resp, DeleteTable(&req, &resp, nullptr));
  }

  for (const NamespaceMap::value_type& entry : namespace_map) {
    const NamespaceId& old_id = entry.first;
    const NamespaceId& new_id = entry.second.first;
    const YQLDatabase& db_type = entry.second.second;

    // Do not delete YSQL objects - it must be deleted via PG API.
    if (new_id.empty() || new_id == old_id || db_type == YQL_DATABASE_PGSQL) {
      continue;
    }

    LOG_WITH_FUNC(INFO) << "Deleting new namespace with id=" << new_id << " old id=" << old_id;
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_id(new_id);
    ProcessDeleteObjectStatus(
        "namespace", new_id, resp, DeleteNamespace(&req, &resp, nullptr));
  }
}

Status CatalogManager::ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                          ImportSnapshotMetaResponsePB* resp) {
  LOG(INFO) << "Servicing ImportSnapshotMeta request: " << req->ShortDebugString();

  NamespaceMap namespace_map;
  ExternalTableSnapshotDataMap tables_data;
  bool successful_exit = false;

  auto se = ScopeExit([this, &namespace_map, &tables_data, &successful_exit] {
    if (!successful_exit) {
      DeleteNewSnapshotObjects(namespace_map, tables_data);
    }
  });

  const SnapshotInfoPB& snapshot_pb = req->snapshot();

  if (!snapshot_pb.has_format_version() || snapshot_pb.format_version() != 2) {
    return STATUS(InternalError, "Expected snapshot data in format 2",
        snapshot_pb.ShortDebugString(), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  if (snapshot_pb.backup_entries_size() == 0) {
    return STATUS(InternalError, "Expected snapshot data prepared for backup",
        snapshot_pb.ShortDebugString(), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  // PHASE 1: Recreate namespaces, create table's meta data.
  RETURN_NOT_OK(ImportSnapshotPreprocess(snapshot_pb, resp, &namespace_map, &tables_data));

  // PHASE 2: Recreate ONLY tables.
  RETURN_NOT_OK(ImportSnapshotCreateObject(
      snapshot_pb, resp, &namespace_map, &tables_data, CreateObjects::kOnlyTables));

  // PHASE 3: Wait for all tables creation complete.
  RETURN_NOT_OK(ImportSnapshotWaitForTables(snapshot_pb, resp, &tables_data));

  // PHASE 4: Recreate ONLY indexes.
  RETURN_NOT_OK(ImportSnapshotCreateObject(
      snapshot_pb, resp, &namespace_map, &tables_data, CreateObjects::kOnlyIndexes));

  // PHASE 5: Restore tablets.
  RETURN_NOT_OK(ImportSnapshotProcessTablets(snapshot_pb, resp, &tables_data));

  successful_exit = true;
  return Status::OK();
}

Status CatalogManager::ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                            ChangeEncryptionInfoResponsePB* resp) {
  auto l = cluster_config_->LockForWrite();
  auto encryption_info = l.mutable_data()->pb.mutable_encryption_info();

  RETURN_NOT_OK(encryption_manager_->ChangeEncryptionInfo(req, encryption_info));

  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
      "updating cluster config in sys-catalog"));
  l.Commit();

  std::lock_guard<simple_spinlock> lock(should_send_universe_key_registry_mutex_);
  for (auto& entry : should_send_universe_key_registry_) {
    entry.second = true;
  }

  return Status::OK();
}

Status CatalogManager::IsEncryptionEnabled(const IsEncryptionEnabledRequestPB* req,
                                           IsEncryptionEnabledResponsePB* resp) {
  return encryption_manager_->IsEncryptionEnabled(
      cluster_config_->LockForRead()->pb.encryption_info(), resp);
}

Status CatalogManager::ImportNamespaceEntry(const SysRowEntry& entry,
                                            NamespaceMap* namespace_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::NAMESPACE)
      << "Unexpected entry type: " << entry.type();

  SysNamespaceEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));
  const YQLDatabase db_type = GetDatabaseType(meta);
  NamespaceData& ns_data = (*namespace_map)[entry.id()];
  ns_data.second = db_type;

  TRACE("Looking up namespace");
  // First of all try to find the namespace by ID. It will work if we are restoring the backup
  // on the original cluster where the backup was created.
  scoped_refptr<NamespaceInfo> ns;
  {
    SharedLock lock(mutex_);
    ns = FindPtrOrNull(namespace_ids_map_, entry.id());
  }

  if (ns != nullptr && ns->name() == meta.name() && ns->state() == SysNamespaceEntryPB::RUNNING) {
    ns_data.first = entry.id();
    return Status::OK();
  }

  // If the namespace was not found by ID, it's ok on a new cluster OR if the namespace was
  // deleted and created again. In both cases the namespace can be found by NAME.
  if (db_type == YQL_DATABASE_PGSQL) {
    // YSQL database must be created via external call. Find it by name.
    {
      SharedLock lock(mutex_);
      ns = FindPtrOrNull(namespace_names_mapper_[db_type], meta.name());
    }

    if (ns == nullptr) {
      const string msg = Format("YSQL database must exist: $0", meta.name());
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
    }
    if (ns->state() != SysNamespaceEntryPB::RUNNING) {
      const string msg = Format("Found YSQL database must be running: $0", meta.name());
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
    }

    auto ns_lock = ns->LockForRead();
    ns_data.first = ns->id();
  } else {
    CreateNamespaceRequestPB req;
    CreateNamespaceResponsePB resp;
    req.set_name(meta.name());
    const Status s = CreateNamespace(&req, &resp, nullptr);

    if (!s.ok() && !s.IsAlreadyPresent()) {
      return s.CloneAndAppend("Failed to create namespace");
    }

    if (s.IsAlreadyPresent()) {
      LOG_WITH_FUNC(INFO) << "Using existing namespace '" << meta.name() << "': " << resp.id();
    }

    ns_data.first = resp.id();
  }
  return Status::OK();
}

Status CatalogManager::RecreateTable(const NamespaceId& new_namespace_id,
                                     const ExternalTableSnapshotDataMap& table_map,
                                     ExternalTableSnapshotData* table_data) {
  const SysTablesEntryPB& meta = DCHECK_NOTNULL(table_data)->table_entry_pb;

  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(meta.name());
  req.set_table_type(meta.table_type());
  req.set_num_tablets(table_data->num_tablets);
  for (const auto& p : table_data->partitions) {
    *req.add_partitions() = p;
  }
  req.mutable_namespace_()->set_id(new_namespace_id);
  *req.mutable_partition_schema() = meta.partition_schema();
  *req.mutable_replication_info() = meta.replication_info();

  SchemaPB* const schema = req.mutable_schema();
  *schema = meta.schema();

  // Setup Index info.
  if (table_data->is_index()) {
    TRACE("Looking up indexed table");
    // First of all try to attach to the new copy of the referenced table,
    // because the table restored from the snapshot is preferred.
    // For that try to map old indexed table ID into new table ID.
    ExternalTableSnapshotDataMap::const_iterator it = table_map.find(meta.indexed_table_id());
    const bool using_existing_table = (it == table_map.end());

    if (using_existing_table) {
      LOG_WITH_FUNC(INFO) << "Try to use old indexed table id " << meta.indexed_table_id();
      req.set_indexed_table_id(meta.indexed_table_id());
    } else {
      LOG_WITH_FUNC(INFO) << "Found new table id " << it->second.new_table_id
                          << " for old table id " << meta.indexed_table_id()
                          << " from the snapshot";
      req.set_indexed_table_id(it->second.new_table_id);
    }

    scoped_refptr<TableInfo> indexed_table;
    {
      SharedLock lock(mutex_);
      // Try to find the specified indexed table by id.
      indexed_table = FindPtrOrNull(*table_ids_map_, req.indexed_table_id());
    }

    if (indexed_table == nullptr) {
      const string msg = Format("Indexed table not found by id: $0", req.indexed_table_id());
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }

    LOG_WITH_FUNC(INFO) << "Found indexed table by id " << req.indexed_table_id();

    // Ensure the main table schema (including column ids) was not changed.
    if (!using_existing_table) {
      Schema new_indexed_schema, src_indexed_schema;
      RETURN_NOT_OK(indexed_table->GetSchema(&new_indexed_schema));
      RETURN_NOT_OK(SchemaFromPB(it->second.table_entry_pb.schema(), &src_indexed_schema));

      if (!new_indexed_schema.Equals(src_indexed_schema)) {
          const string msg = Format(
              "Recreated table has changes in schema: new schema={$0}, source schema={$1}",
              new_indexed_schema.ToString(), src_indexed_schema.ToString());
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
      }

      if (new_indexed_schema.column_ids() != src_indexed_schema.column_ids()) {
          const string msg = Format(
              "Recreated table has changes in column ids: new ids=$0, source ids=$1",
              ToString(new_indexed_schema.column_ids()), ToString(src_indexed_schema.column_ids()));
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
      }
    }

    req.set_is_local_index(meta.is_local_index());
    req.set_is_unique_index(meta.is_unique_index());
    req.set_skip_index_backfill(true);
    // Setup IndexInfoPB - self descriptor.
    IndexInfoPB* const index_info_pb = req.mutable_index_info();
    *index_info_pb = meta.index_info();
    index_info_pb->clear_table_id();
    index_info_pb->set_indexed_table_id(req.indexed_table_id());

    // Reset column ids.
    for (int i = 0; i < index_info_pb->columns_size(); ++i) {
      index_info_pb->mutable_columns(i)->clear_column_id();
    }
  }

  RETURN_NOT_OK(CreateTable(&req, &resp, /* RpcContext */nullptr));
  table_data->new_table_id = resp.table_id();
  LOG_WITH_FUNC(INFO) << "New table id " << table_data->new_table_id << " for "
                      << table_data->old_table_id;
  return Status::OK();
}

Status CatalogManager::RepartitionTable(const scoped_refptr<TableInfo> table,
                                        const ExternalTableSnapshotData* table_data) {
  DCHECK_EQ(table->id(), table_data->new_table_id);
  if (table->GetTableType() != PGSQL_TABLE_TYPE) {
    return STATUS_FORMAT(InvalidArgument,
                         "Cannot repartition non-YSQL table: got $0",
                         TableType_Name(table->GetTableType()));
  }
  LOG_WITH_FUNC(INFO) << "Repartition table " << table->id()
                      << " using external snapshot table " << table_data->old_table_id;

  // Get partitions from external snapshot.
  size_t i = 0;
  vector<Partition> partitions(table_data->partitions.size());
  for (const auto& partition_pb : table_data->partitions) {
    Partition::FromPB(partition_pb, &partitions[i++]);
  }
  VLOG_WITH_FUNC(3) << "Got " << partitions.size()
                    << " partitions from external snapshot for table " << table->id();

  // Change TableInfo to point to the new tablets.
  string deletion_msg;
  vector<scoped_refptr<TabletInfo>> new_tablets;
  vector<scoped_refptr<TabletInfo>> old_tablets;
  {
    // Acquire the TableInfo pb write lock. Although it is not required for some of the individual
    // steps, we want to hold it through so that we guarantee the state does not change during the
    // whole process. Consequently, we hold it through some steps that require mutex_, but since
    // taking mutex_ after TableInfo pb lock is prohibited for deadlock reasons, acquire mutex_
    // first, then release it when it is no longer needed, still holding table pb lock.
    TableInfo::WriteLock table_lock;
    {
      LockGuard lock(mutex_);
      TRACE("Acquired catalog manager lock");

      // Make sure the table is in RUNNING state.
      // This by itself doesn't need a pb write lock: just a read lock. However, we want to prevent
      // other writers from entering from this point forward, so take the write lock now.
      table_lock = table->LockForWrite();
      if (table->old_pb().state() != SysTablesEntryPB::RUNNING) {
        return STATUS_FORMAT(IllegalState,
                             "Table $0 not running: $1",
                             table->ToString(),
                             SysTablesEntryPB_State_Name(table->old_pb().state()));
      }
      // Make sure the table's tablets can be deleted.
      RETURN_NOT_OK_PREPEND(CheckIfForbiddenToDeleteTabletOf(table),
                            Format("Cannot repartition table $0", table->id()));

      // Create and mark new tablets for creation.

      // Use partitions from external snapshot to create new tablets in state PREPARING. The tablets
      // will start CREATING once they are committed in memory.
      for (const auto& partition : partitions) {
        PartitionPB partition_pb;
        partition.ToPB(&partition_pb);
        new_tablets.push_back(CreateTabletInfo(table.get(), partition_pb));
      }

      // Add tablets to catalog manager tablet_map_. This should be safe to do after creating
      // tablets since we're still holding mutex_.
      auto tablet_map_checkout = tablet_map_.CheckOut();
      for (auto& new_tablet : new_tablets) {
        InsertOrDie(tablet_map_checkout.get_ptr(), new_tablet->tablet_id(), new_tablet);
      }
      VLOG_WITH_FUNC(3) << "Prepared creation of " << new_tablets.size()
                        << " new tablets for table " << table->id();

      // mutex_ is no longer needed, so release by going out of scope.
    }
    // The table pb write lock is still held, ensuring that the table state does not change. Later
    // steps, like GetTablets or AddTablets, will acquire/release the TableInfo lock_, but it's
    // probably fine that they are released between the steps since the table pb write lock is held
    // throughout. In other words, there should be no risk that TableInfo tablets_ changes between
    // GetTablets and RemoveTablets.

    // Abort tablet mutations in case of early returns.
    ScopedInfoCommitter<TabletInfo> unlocker_new(&new_tablets);

    // Mark old tablets for deletion.
    old_tablets = table->GetTablets(IncludeInactive::kTrue);
    // Sort so that locking can be done in a deterministic order.
    std::sort(old_tablets.begin(), old_tablets.end(), [](const auto& lhs, const auto& rhs) {
      return lhs->tablet_id() < rhs->tablet_id();
    });
    deletion_msg = Format("Old tablets of table $0 deleted at $1",
                          table->id(), LocalTimeAsString());
    for (auto& old_tablet : old_tablets) {
      old_tablet->mutable_metadata()->StartMutation();
      old_tablet->mutable_metadata()->mutable_dirty()->set_state(
          SysTabletsEntryPB::DELETED, deletion_msg);
    }
    VLOG_WITH_FUNC(3) << "Prepared deletion of " << old_tablets.size() << " old tablets for table "
                      << table->id();

    // Abort tablet mutations in case of early returns.
    ScopedInfoCommitter<TabletInfo> unlocker_old(&old_tablets);

    // Change table's partition schema to the external snapshot's.
    table_lock.mutable_data()->pb.mutable_partition_schema()->CopyFrom(
        table_data->table_entry_pb.partition_schema());

    // Remove old tablets from TableInfo.
    table->RemoveTablets(old_tablets);
    // Add new tablets to TableInfo. This must be done after removing tablets because
    // TableInfo::partitions_ has key PartitionKey, which old and new tablets may conflict on.
    table->AddTablets(new_tablets);

    // Commit table and tablets to disk.
    RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), table, new_tablets, old_tablets));
    VLOG_WITH_FUNC(2) << "Committed to disk: table " << table->id() << " repartition from "
                      << old_tablets.size() << " tablets to " << new_tablets.size() << " tablets";

    // Commit to memory. Commit new tablets (addition) first since that doesn't break anything.
    // Commit table next since new tablets are already committed and ready to be referenced. Commit
    // old tablets (deletion) last since the table is not referencing them anymore.
    unlocker_new.Commit();
    table_lock.Commit();
    unlocker_old.Commit();
    VLOG_WITH_FUNC(1) << "Committed to memory: table " << table->id() << " repartition from "
                      << old_tablets.size() << " tablets to " << new_tablets.size() << " tablets";
  }

  // Finally, now that everything is committed, send the delete tablet requests.
  for (auto& old_tablet : old_tablets) {
    DeleteTabletReplicas(old_tablet.get(), deletion_msg, HideOnly::kFalse);
  }
  VLOG_WITH_FUNC(2) << "Sent delete tablet requests for " << old_tablets.size() << " old tablets"
                    << " of table " << table->id();
  // The create tablet requests should be handled by bg tasks which find the PREPARING tablets after
  // commit.

  return Status::OK();
}

// Helper function for ImportTableEntry.
//
// Given an internal table and an external table snapshot, do some checks to determine if we should
// move forward with using this internal table for import.
//
// table: internal table's info
// snapshot_data: external table's snapshot data
Result<bool> CatalogManager::CheckTableForImport(scoped_refptr<TableInfo> table,
                                                 ExternalTableSnapshotData* snapshot_data) {
  auto table_lock = table->LockForRead();

  // Check if table is live.
  if (!table_lock->visible_to_client()) {
    VLOG_WITH_FUNC(2) << "Table not visible to client: " << table->ToString();
    return false;
  }
  // Check if table names match.
  const string& external_table_name = snapshot_data->table_entry_pb.name();
  if (table_lock->name() != external_table_name) {
    VLOG_WITH_FUNC(2) << "Table names do not match: "
                      << table_lock->name() << " vs " << external_table_name
                      << " for " << table->ToString();
    return false;
  }
  // Check index vs table.
  if (snapshot_data->is_index() ? table->indexed_table_id().empty()
                                : !table->indexed_table_id().empty()) {
    VLOG_WITH_FUNC(2) << "External snapshot table is " << (snapshot_data->is_index() ? "" : "not ")
                      << "index but internal table is the opposite: " << table->ToString();
    return false;
  }
  // Check if table schemas match (if present in snapshot).
  if (!snapshot_data->pg_schema_name.empty()) {
    if (table->GetTableType() != PGSQL_TABLE_TYPE) {
      LOG_WITH_FUNC(DFATAL) << "ExternalTableSnapshotData.pg_schema_name set when table type is not"
          << " PGSQL: schema name: " << snapshot_data->pg_schema_name
          << ", table type: " << TableType_Name(table->GetTableType());
      // If not a debug build, ignore pg_schema_name.
    } else {
      const string internal_schema_name = VERIFY_RESULT(GetPgSchemaName(table));
      const string& external_schema_name = snapshot_data->pg_schema_name;
      if (internal_schema_name != external_schema_name) {
        LOG_WITH_FUNC(INFO) << "Schema names do not match: "
                            << internal_schema_name << " vs " << external_schema_name
                            << " for " << table->ToString();
        return false;
      }
    }
  }

  return true;
}

Status CatalogManager::ImportTableEntry(const NamespaceMap& namespace_map,
                                        const ExternalTableSnapshotDataMap& table_map,
                                        ExternalTableSnapshotData* table_data) {
  const SysTablesEntryPB& meta = DCHECK_NOTNULL(table_data)->table_entry_pb;
  bool is_parent_colocated_table = false;

  table_data->old_namespace_id = meta.namespace_id();
  LOG_IF(DFATAL, table_data->old_namespace_id.empty()) << "No namespace id";

  LOG_IF(DFATAL, namespace_map.find(table_data->old_namespace_id) == namespace_map.end())
      << "Namespace not found: " << table_data->old_namespace_id;
  const NamespaceId new_namespace_id =
      namespace_map.find(table_data->old_namespace_id)->second.first;
  LOG_IF(DFATAL, new_namespace_id.empty()) << "No namespace id";

  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(meta.schema(), &schema));
  const vector<ColumnId>& column_ids = schema.column_ids();
  scoped_refptr<TableInfo> table;

  // First, check if namespace id and table id match. If, in addition, other properties match, we
  // found the destination table.
  if (new_namespace_id == table_data->old_namespace_id) {
    TRACE("Looking up table");
    {
      SharedLock lock(mutex_);
      table = FindPtrOrNull(*table_ids_map_, table_data->old_table_id);
    }

    if (table != nullptr) {
      VLOG_WITH_PREFIX(3) << "Begin first search";
      // At this point, namespace id and table id match. Check other properties, like whether the
      // table is active and whether table name matches.
      SharedLock lock(mutex_);
      if (VERIFY_RESULT(CheckTableForImport(table, table_data))) {
        LOG_WITH_FUNC(INFO) << "Found existing table: '" << table->ToString() << "'";
      } else {
        // A property did not match, so this search by ids failed.
        auto table_lock = table->LockForRead();
        LOG_WITH_FUNC(WARNING) << "Existing table " << table->ToString() << " not suitable: "
                               << table_lock->pb.ShortDebugString()
                               << ", name: " << table->name() << " vs " << meta.name();
        table.reset();
      }
    }
  }

  // Second, if we still didn't find a match...
  if (table == nullptr) {
    VLOG_WITH_PREFIX(3) << "Begin second search";
    switch (meta.table_type()) {
      case TableType::YQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
      case TableType::REDIS_TABLE_TYPE: {
        // For YCQL and YEDIS, simply create the missing table.
        RETURN_NOT_OK(RecreateTable(new_namespace_id, table_map, table_data));
        break;
      }
      case TableType::PGSQL_TABLE_TYPE: {
        // For YSQL, the table must be created via external call. Therefore, continue the search for
        // the table, this time checking for name matches rather than id matches.

        if (meta.colocated() && IsColocatedParentTableId(table_data->old_table_id)) {
          // For the parent colocated table we need to generate the new_table_id ourselves
          // since the names will not match.
          // For normal colocated tables, we are still able to follow the normal table flow, so no
          // need to generate the new_table_id ourselves.
          table_data->new_table_id = new_namespace_id + kColocatedParentTableIdSuffix;
          is_parent_colocated_table = true;
        } else {
          if (!table_data->new_table_id.empty()) {
            const string msg = Format(
                "$0 expected empty new table id but $1 found", __func__, table_data->new_table_id);
            LOG_WITH_FUNC(WARNING) << msg;
            return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
          }
          SharedLock lock(mutex_);

          for (const auto& entry : *table_ids_map_) {
            table = entry.second;

            if (new_namespace_id != table->namespace_id()) {
              VLOG_WITH_FUNC(3) << "Namespace ids do not match: "
                                << table->namespace_id() << " vs " << new_namespace_id
                                << " for " << table->ToString();
              continue;
            }
            if (!VERIFY_RESULT(CheckTableForImport(table, table_data))) {
              // Some other check failed.
              continue;
            }
            // Also check if table is user-created.
            if (!IsUserCreatedTableUnlocked(*table)) {
              VLOG_WITH_FUNC(2) << "Table not user created: " << table->ToString();
              continue;
            }

            // Found the new YSQL table by name.
            if (table_data->new_table_id.empty()) {
              LOG_WITH_FUNC(INFO) << "Found existing table " << entry.first << " for "
                                  << new_namespace_id << "/" << meta.name() << " (old table "
                                  << table_data->old_table_id << ") with schema "
                                  << table_data->pg_schema_name;
              table_data->new_table_id = entry.first;
            } else if (table_data->new_table_id != entry.first) {
              const string msg = Format(
                  "Found 2 YSQL tables with the same name: $0 - $1, $2",
                  meta.name(), table_data->new_table_id, entry.first);
              LOG_WITH_FUNC(WARNING) << msg;
              return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
            }
          }

          if (table_data->new_table_id.empty()) {
            const string msg = Format("YSQL table not found: $0", meta.name());
            LOG_WITH_FUNC(WARNING) << msg;
            return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
          }
        }
        break;
      }
      case TableType::TRANSACTION_STATUS_TABLE_TYPE: {
        return STATUS(
            InvalidArgument,
            Format("Unexpected table type: $0", TableType_Name(meta.table_type())),
            MasterError(MasterErrorPB::INVALID_TABLE_TYPE));
      }
    }
  } else {
    table_data->new_table_id = table_data->old_table_id;
    LOG_WITH_FUNC(INFO) << "Use existing table " << table_data->new_table_id;
  }

  // The destination table should be found or created by now.
  TRACE("Looking up new table");
  {
    SharedLock lock(mutex_);
    table = FindPtrOrNull(*table_ids_map_, table_data->new_table_id);
  }
  if (table == nullptr) {
    const string msg = Format("Created table not found: $0", table_data->new_table_id);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InternalError, msg, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  // Don't do schema validation/column updates on the parent colocated table.
  // However, still do the validation for regular colocated tables.
  if (!is_parent_colocated_table) {
    Schema persisted_schema;
    size_t new_num_tablets = 0;
    {
      TRACE("Locking table");
      auto table_lock = table->LockForRead();
      RETURN_NOT_OK(table->GetSchema(&persisted_schema));
      new_num_tablets = table->NumPartitions();
    }

    // Ignore 'nullable' attribute - due to difference in implementation
    // of PgCreateTable::AddColumn() and PgAlterTable::AddColumn().
    struct CompareColumnsExceptNullable {
      bool operator ()(const ColumnSchema& a, const ColumnSchema& b) {
        return ColumnSchema::CompHashKey(a, b) && ColumnSchema::CompSortingType(a, b) &&
            ColumnSchema::CompTypeInfo(a, b) && ColumnSchema::CompName(a, b);
      }
    } comparator;
    // Schema::Equals() compares only column names & types. It does not compare the column ids.
    if (!persisted_schema.Equals(schema, comparator)
        || persisted_schema.column_ids().size() != column_ids.size()) {
      const string msg = Format(
          "Invalid created $0 table '$1' in namespace id $2: schema={$3}, expected={$4}",
          TableType_Name(meta.table_type()), meta.name(), new_namespace_id,
          persisted_schema, schema);
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
    }

    if (table_data->num_tablets > 0) {
      if (meta.table_type() == TableType::PGSQL_TABLE_TYPE) {
        bool partitions_match = true;
        if (new_num_tablets != table_data->num_tablets) {
          partitions_match = false;
        } else {
          // Check if partition boundaries match.  Only check the starts; assume the ends are fine.
          size_t i = 0;
          vector<PartitionKey> partition_starts(table_data->num_tablets);
          for (const auto& partition_pb : table_data->partitions) {
            partition_starts[i] = partition_pb.partition_key_start();
            LOG_IF(DFATAL, (i == 0) ? partition_starts[i] != ""
                                    : partition_starts[i] <= partition_starts[i-1])
                << "Wrong partition key start: " << b2a_hex(partition_starts[i]);
            i++;
          }
          if (!table->HasPartitions(partition_starts)) {
            LOG_WITH_FUNC(INFO) << "Partition boundaries mismatch for table " << table->id();
            partitions_match = false;
          }
        }

        if (!partitions_match) {
          RETURN_NOT_OK(RepartitionTable(table, table_data));
        }
      } else { // not PGSQL_TABLE_TYPE
        if (new_num_tablets != table_data->num_tablets) {
          const string msg = Format(
              "Wrong number of tablets in created $0 table '$1' in namespace id $2:"
              " $3 (expected $4)",
              TableType_Name(meta.table_type()), meta.name(), new_namespace_id,
              new_num_tablets, table_data->num_tablets);
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }
      }
    }

    // Update the table column ids if it's not equal to the stored ids.
    if (persisted_schema.column_ids() != column_ids) {
      if (meta.table_type() != TableType::PGSQL_TABLE_TYPE) {
        LOG_WITH_FUNC(WARNING) << "Unexpected wrong column ids in "
                               << TableType_Name(meta.table_type()) << " table '" << meta.name()
                               << "' in namespace id " << new_namespace_id;
      }

      LOG_WITH_FUNC(INFO) << "Restoring column ids in " << TableType_Name(meta.table_type())
                          << " table '" << meta.name() << "' in namespace id "
                          << new_namespace_id;
      auto l = table->LockForWrite();
      size_t col_idx = 0;
      for (auto& column : *l.mutable_data()->pb.mutable_schema()->mutable_columns()) {
        // Expecting here correct schema (columns - order, names, types), but with only wrong
        // column ids. Checking correct column order and column names below.
        if (column.name() != schema.column(col_idx).name()) {
          const string msg = Format(
              "Unexpected column name for index=$0: name=$1, expected name=$2",
              col_idx, schema.column(col_idx).name(), column.name());
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }
        // Copy the column id from imported (original) schema.
        column.set_id(column_ids[col_idx++]);
      }

      l.mutable_data()->pb.set_next_column_id(schema.max_col_id() + 1);
      l.mutable_data()->pb.set_version(l->pb.version() + 1);
      // Update sys-catalog with the new table schema.
      RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), table));
      l.Commit();
      // Update the new table schema in tablets.
      RETURN_NOT_OK(SendAlterTableRequest(table));
    }
  }

  // Set the type of the table in the response pb (default is TABLE so only set if colocated).
  if (meta.colocated()) {
    if (is_parent_colocated_table) {
      table_data->table_meta->set_table_type(
          ImportSnapshotMetaResponsePB_TableType_PARENT_COLOCATED_TABLE);
    } else {
      table_data->table_meta->set_table_type(
          ImportSnapshotMetaResponsePB_TableType_COLOCATED_TABLE);
    }
  }

  TabletInfos new_tablets;
  {
    TRACE("Locking table");
    auto table_lock = table->LockForRead();
    new_tablets = table->GetTablets();
  }

  for (const scoped_refptr<TabletInfo>& tablet : new_tablets) {
    auto tablet_lock = tablet->LockForRead();
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
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::TABLET)
      << "Unexpected entry type: " << entry.type();

  SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));

  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];
  ++table_data.num_tablets;
  if (meta.has_partition()) {
    table_data.partitions.push_back(meta.partition());
  }
  return Status::OK();
}

Status CatalogManager::ImportTabletEntry(const SysRowEntry& entry,
                                         ExternalTableSnapshotDataMap* table_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::TABLET)
      << "Unexpected entry type: " << entry.type();

  SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));

  LOG_IF(DFATAL, table_map->find(meta.table_id()) == table_map->end())
      << "Table not found: " << meta.table_id();
  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];

  if (meta.colocated() && table_data.tablet_id_map->size() >= 1) {
    LOG_WITH_FUNC(INFO) << "Already processed this colocated tablet: " << entry.id();
    return Status::OK();
  }

  // Update tablets IDs map.
  if (table_data.new_table_id == table_data.old_table_id) {
    TRACE("Looking up tablet");
    SharedLock lock(mutex_);
    scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());

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
    const string msg = Format(
        "For new table $0 (old table $1, expecting $2 tablets) not found new tablet with "
        "expected [$3]", table_data.new_table_id, table_data.old_table_id,
        table_data.num_tablets, partition_pb);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(NotFound, msg, MasterError(MasterErrorPB::INTERNAL_ERROR));
  }

  IdPairPB* const pair = table_data.tablet_id_map->Add();
  pair->set_old_id(entry.id());
  pair->set_new_id(it->second);
  return Status::OK();
}

const Schema& CatalogManager::schema() {
  return sys_catalog()->schema();
}

TabletInfos CatalogManager::GetTabletInfos(const std::vector<TabletId>& ids) {
  TabletInfos result;
  result.reserve(ids.size());
  SharedLock lock(mutex_);
  for (const auto& id : ids) {
    auto it = tablet_map_->find(id);
    result.push_back(it != tablet_map_->end() ? it->second : nullptr);
  }
  return result;
}

AsyncTabletSnapshotOpPtr CatalogManager::CreateAsyncTabletSnapshotOp(
    const TabletInfoPtr& tablet, const std::string& snapshot_id,
    tserver::TabletSnapshotOpRequestPB::Operation operation,
    TabletSnapshotOperationCallback callback) {
  auto result = std::make_shared<AsyncTabletSnapshotOp>(
      master_, AsyncTaskPool(), tablet, snapshot_id, operation);
  result->SetCallback(std::move(callback));
  tablet->table()->AddTask(result);
  return result;
}

void CatalogManager::ScheduleTabletSnapshotOp(const AsyncTabletSnapshotOpPtr& task) {
  WARN_NOT_OK(ScheduleTask(task), "Failed to send create snapshot request");
}

Status CatalogManager::RestoreSysCatalog(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet, Status* complete_status) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << restoration->restoration_id;
  // Restore master snapshot and load it to RocksDB.
  auto dir = VERIFY_RESULT(tablet->snapshots().RestoreToTemporary(
      restoration->snapshot_id, restoration->restore_at));
  rocksdb::Options rocksdb_options;
  std::string log_prefix = LogPrefix();
  // Remove ": " to patch suffix.
  log_prefix.erase(log_prefix.size() - 2);
  tablet->InitRocksDBOptions(&rocksdb_options, log_prefix + " [TMP]: ");
  auto db = VERIFY_RESULT(rocksdb::DB::Open(rocksdb_options, dir));

  auto doc_db = docdb::DocDB::FromRegularUnbounded(db.get());

  // Load objects to restore and determine obsolete objects.
  RestoreSysCatalogState state(restoration);
  RETURN_NOT_OK(state.LoadRestoringObjects(schema(), doc_db));
  // Load existing objects from RocksDB because on followers they are NOT present in loaded sys
  // catalog state.
  RETURN_NOT_OK(state.LoadExistingObjects(schema(), tablet->doc_db()));
  RETURN_NOT_OK(state.Process());

  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional);

  if (FLAGS_enable_ysql) {
    // Restore the pg_catalog tables.
    const auto* meta = tablet->metadata();
    const auto& pg_yb_catalog_version_schema =
        *VERIFY_RESULT(meta->GetTableInfo(kPgYbCatalogVersionTableId))->schema;
    RETURN_NOT_OK(state.ProcessPgCatalogRestores(
        pg_yb_catalog_version_schema, doc_db, tablet->doc_db(), &write_batch));
  }

  // Restore the other tables.
  RETURN_NOT_OK(state.PrepareWriteBatch(schema(), &write_batch));

  // Apply write batch to RocksDB.
  state.WriteToRocksDB(&write_batch, restoration->write_time, restoration->op_id, tablet);

  // TODO(pitr) Handle master leader failover.
  RETURN_NOT_OK(ElectedAsLeaderCb());

  return Status::OK();
}

Status CatalogManager::VerifyRestoredObjects(const SnapshotScheduleRestoration& restoration) {
  auto entries = VERIFY_RESULT(CollectEntriesForSnapshot(
      restoration.schedules[0].second.tables().tables()));
  auto objects_to_restore = restoration.non_system_objects_to_restore;
  VLOG_WITH_PREFIX(1) << "Objects to restore: " << AsString(objects_to_restore);
  for (const auto& entry : entries.entries()) {
    VLOG_WITH_PREFIX(1)
        << "Alive " << SysRowEntryType_Name(entry.type()) << ": " << entry.id();
    auto it = objects_to_restore.find(entry.id());
    if (it == objects_to_restore.end()) {
      return STATUS_FORMAT(IllegalState, "Object $0/$1 present, but should not be restored",
                           SysRowEntryType_Name(entry.type()), entry.id());
    }
    if (it->second != entry.type()) {
      return STATUS_FORMAT(
          IllegalState, "Restored object $0 has wrong type $1, while $2 expected",
          entry.id(), SysRowEntryType_Name(entry.type()), SysRowEntryType_Name(it->second));
    }
    objects_to_restore.erase(it);
  }
  for (const auto& id_and_type : objects_to_restore) {
    return STATUS_FORMAT(
        IllegalState, "Expected to restore $0/$1, but it is not present after restoration",
        SysRowEntryType_Name(id_and_type.second), id_and_type.first);
  }
  return Status::OK();
}

void CatalogManager::CleanupHiddenObjects(const ScheduleMinRestoreTime& schedule_min_restore_time) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(schedule_min_restore_time);

  std::vector<TabletInfoPtr> hidden_tablets;
  std::vector<TableInfoPtr> tables;
  {
    SharedLock lock(mutex_);
    hidden_tablets = hidden_tablets_;
    tables.reserve(table_ids_map_->size());
    for (const auto& p : *table_ids_map_) {
      if (!p.second->is_system()) {
        tables.push_back(p.second);
      }
    }
  }
  CleanupHiddenTablets(hidden_tablets, schedule_min_restore_time);
  CleanupHiddenTables(std::move(tables));
}

void CatalogManager::CleanupHiddenTablets(
    const std::vector<TabletInfoPtr>& hidden_tablets,
    const ScheduleMinRestoreTime& schedule_min_restore_time) {
  if (hidden_tablets.empty()) {
    return;
  }
  std::vector<TabletInfoPtr> tablets_to_delete;
  std::vector<TabletInfoPtr> tablets_to_remove_from_hidden;

  for (const auto& tablet : hidden_tablets) {
    auto lock = tablet->LockForRead();
    if (!lock->ListedAsHidden()) {
      tablets_to_remove_from_hidden.push_back(tablet);
      continue;
    }
    auto hide_hybrid_time = HybridTime::FromPB(lock->pb.hide_hybrid_time());
    bool cleanup = true;
    for (const auto& schedule_id_str : lock->pb.retained_by_snapshot_schedules()) {
      auto schedule_id = TryFullyDecodeSnapshotScheduleId(schedule_id_str);
      auto it = schedule_min_restore_time.find(schedule_id);
      // If schedule is not present in schedule_min_restore_time then it means that schedule
      // was deleted, so it should not retain the tablet.
      if (it != schedule_min_restore_time.end() && it->second <= hide_hybrid_time) {
        VLOG_WITH_PREFIX(1)
            << "Retaining tablet: " << tablet->tablet_id() << ", hide hybrid time: "
            << hide_hybrid_time << ", because of schedule: " << schedule_id
            << ", min restore time: " << it->second;
        cleanup = false;
        break;
      }
    }
    if (cleanup) {
      SharedLock read_lock(mutex_);
      if (IsTableCdcProducer(*tablet->table())) {
        // We also need to check if this tablet is being kept for xcluster replication.
        auto l = tablet->table()->LockForRead();
        cleanup = hide_hybrid_time.AddSeconds(l->pb.wal_retention_secs()) < master_->clock()->Now();
      }
    }
    if (cleanup) {
      tablets_to_delete.push_back(tablet);
    }
  }
  if (!tablets_to_delete.empty()) {
    LOG_WITH_PREFIX(INFO) << "Cleanup hidden tablets: " << AsString(tablets_to_delete);
    WARN_NOT_OK(DeleteTabletListAndSendRequests(tablets_to_delete, "Cleanup hidden tablets", {}),
                "Failed to cleanup hidden tablets");
  }

  if (!tablets_to_remove_from_hidden.empty()) {
    auto it = tablets_to_remove_from_hidden.begin();
    LockGuard lock(mutex_);
    // Order of tablets in tablets_to_remove_from_hidden matches order in hidden_tablets_,
    // so we could avoid searching in tablets_to_remove_from_hidden.
    auto filter = [&it, end = tablets_to_remove_from_hidden.end()](const TabletInfoPtr& tablet) {
      if (it != end && tablet.get() == it->get()) {
        ++it;
        return true;
      }
      return false;
    };
    hidden_tablets_.erase(std::remove_if(hidden_tablets_.begin(), hidden_tablets_.end(), filter),
                          hidden_tablets_.end());
  }
}

void CatalogManager::CleanupHiddenTables(std::vector<TableInfoPtr> tables) {
  std::vector<TableInfo::WriteLock> locks;
  EraseIf([this, &locks](const TableInfoPtr& table) {
    {
      auto lock = table->LockForRead();
      if (!lock->is_hidden() || lock->started_deleting() || !table->AreAllTabletsDeleted()) {
        return true;
      }
    }
    auto lock = table->LockForWrite();
    if (lock->started_deleting()) {
      return true;
    }
    LOG_WITH_PREFIX(INFO) << "Should delete table: " << AsString(table);
    lock.mutable_data()->set_state(
        SysTablesEntryPB::DELETED, Format("Cleanup hidden table at $0", LocalTimeAsString()));
    locks.push_back(std::move(lock));
    return false;
  }, &tables);
  if (tables.empty()) {
    return;
  }

  Status s = sys_catalog_->Upsert(leader_ready_term(), tables);
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to mark tables as deleted: " << s;
    return;
  }
  for (auto& lock : locks) {
    lock.Commit();
  }
}

rpc::Scheduler& CatalogManager::Scheduler() {
  return master_->messenger()->scheduler();
}

int64_t CatalogManager::LeaderTerm() {
  auto peer = tablet_peer();
  if (!peer) {
    return false;
  }
  auto consensus = peer->shared_consensus();
  if (!consensus) {
    return false;
  }
  return consensus->GetLeaderState(/* allow_stale= */ true).term;
}

void CatalogManager::HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  LOG(INFO) << "Handling Create Tablet Snapshot Response for tablet "
            << DCHECK_NOTNULL(tablet)->ToString() << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      LOG(WARNING) << "No active snapshot: " << current_snapshot_id_;
      return;
    }

    snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, current_snapshot_id_);

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
      l.mutable_data()->pb.mutable_tablet_snapshots();
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
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
    LOG(WARNING) << "Failed snapshot " << snapshot->id() << " on tablet " << tablet->id();
  } else if (num_tablets_complete == tablet_snapshots->size()) {
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
    LOG(INFO) << "Completed snapshot " << snapshot->id();
  } else {
    finished = false;
  }

  if (finished) {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");
    current_snapshot_id_ = "";
  }

  VLOG(1) << "Snapshot: " << snapshot->id()
          << " PB: " << l.mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();

  const Status s = sys_catalog_->Upsert(leader_ready_term(), snapshot);

  l.CommitOrWarn(s, "updating snapshot in sys-catalog");
}

void CatalogManager::HandleRestoreTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  LOG(INFO) << "Handling Restore Tablet Snapshot Response for tablet "
            << DCHECK_NOTNULL(tablet)->ToString() << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      LOG(WARNING) << "No restoring snapshot: " << current_snapshot_id_;
      return;
    }

    snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, current_snapshot_id_);

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
      l.mutable_data()->pb.mutable_tablet_snapshots();
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
      l.mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
      LOG(WARNING) << "Failed restoring snapshot " << snapshot->id()
                   << " on tablet " << tablet->id();
    } else {
      LOG_IF(DFATAL, num_tablets_complete != tablet_snapshots->size())
          << "Wrong number of tablets";
      l.mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
      LOG(INFO) << "Restored snapshot " << snapshot->id();
    }

    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");
    current_snapshot_id_ = "";
  }

  VLOG(1) << "Snapshot: " << snapshot->id()
          << " PB: " << l.mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();

  const Status s = sys_catalog_->Upsert(leader_ready_term(), snapshot);

  l.CommitOrWarn(s, "updating snapshot in sys-catalog");
}

void CatalogManager::HandleDeleteTabletSnapshotResponse(
    const SnapshotId& snapshot_id, TabletInfo *tablet, bool error) {
  LOG(INFO) << "Handling Delete Tablet Snapshot Response for tablet "
            << DCHECK_NOTNULL(tablet)->ToString() << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, snapshot_id);

    if (!snapshot) {
      LOG(WARNING) << __func__ << " Snapshot not found: " << snapshot_id;
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
      l.mutable_data()->pb.mutable_tablet_snapshots();
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

  if (num_tablets_complete == tablet_snapshots->size()) {
    // Delete the snapshot.
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::DELETED);
    LOG(INFO) << "Deleted snapshot " << snapshot->id();

    const Status s = sys_catalog_->Delete(leader_ready_term(), snapshot);

    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_ == snapshot_id) {
      current_snapshot_id_ = "";
    }

    // Remove it from the maps.
    TRACE("Removing from maps");
    if (non_txn_snapshot_ids_map_.erase(snapshot_id) < 1) {
      LOG(WARNING) << "Could not remove snapshot " << snapshot_id << " from map";
    }

    l.CommitOrWarn(s, "deleting snapshot from sys-catalog");
  } else if (error) {
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
    LOG(WARNING) << "Failed snapshot " << snapshot->id() << " deletion on tablet " << tablet->id();

    const Status s = sys_catalog_->Upsert(leader_ready_term(), snapshot);
    l.CommitOrWarn(s, "updating snapshot in sys-catalog");
  }

  VLOG(1) << "Deleting snapshot: " << snapshot->id()
          << " PB: " << l.mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();
}

Status CatalogManager::CreateSnapshotSchedule(const CreateSnapshotScheduleRequestPB* req,
                                              CreateSnapshotScheduleResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  auto id = VERIFY_RESULT(snapshot_coordinator_.CreateSchedule(
      *req, leader_ready_term(), rpc->GetClientDeadline()));
  resp->set_snapshot_schedule_id(id.data(), id.size());
  return Status::OK();
}

Status CatalogManager::ListSnapshotSchedules(const ListSnapshotSchedulesRequestPB* req,
                                             ListSnapshotSchedulesResponsePB* resp,
                                             rpc::RpcContext* rpc) {
  auto snapshot_schedule_id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());

  return snapshot_coordinator_.ListSnapshotSchedules(snapshot_schedule_id, resp);
}

Status CatalogManager::DeleteSnapshotSchedule(const DeleteSnapshotScheduleRequestPB* req,
                                              DeleteSnapshotScheduleResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  auto snapshot_schedule_id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());

  return snapshot_coordinator_.DeleteSnapshotSchedule(
      snapshot_schedule_id, leader_ready_term(), rpc->GetClientDeadline());
}

void CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  super::DumpState(out, on_disk_dump);

  // TODO: dump snapshots
}


Status CatalogManager::HandlePlacementUsingReplicationInfo(
    const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& all_ts_descs,
    consensus::RaftConfigPB* config) {
  TSDescriptorVector ts_descs;
  GetTsDescsFromPlacementInfo(replication_info.live_replicas(), all_ts_descs, &ts_descs);
  RETURN_NOT_OK(super::HandlePlacementUsingPlacementInfo(replication_info.live_replicas(),
                                                      ts_descs,
                                                      consensus::PeerMemberType::VOTER, config));
  for (int i = 0; i < replication_info.read_replicas_size(); i++) {
    GetTsDescsFromPlacementInfo(replication_info.read_replicas(i), all_ts_descs, &ts_descs);
    RETURN_NOT_OK(super::HandlePlacementUsingPlacementInfo(replication_info.read_replicas(i),
                                                           ts_descs,
                                                           consensus::PeerMemberType::OBSERVER,
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
  RETURN_NOT_OK(snapshot_coordinator_.FillHeartbeatResponse(resp));
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
  // If CDC state table exists do nothing, otherwise create it.
  if (VERIFY_RESULT(TableExists(kSystemNamespaceName, kCdcStateTableName))) {
    return Status::OK();
  }
  // Set up a CreateTable request internally.
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(kCdcStateTableName);
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::YQL_TABLE_TYPE);

  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(master::kCdcTabletId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(master::kCdcStreamId)->PrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(master::kCdcCheckpoint)->Type(DataType::STRING);
  schema_builder.AddColumn(master::kCdcData)->Type(QLType::CreateTypeMap(
      DataType::STRING, DataType::STRING));
  schema_builder.AddColumn(master::kCdcLastReplicationTime)->Type(DataType::TIMESTAMP);

  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  auto schema = yb::client::internal::GetSchema(yb_schema);
  SchemaToPB(schema, req.mutable_schema());
  // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
  // will use the same defaults as for regular tables.
  if (FLAGS_cdc_state_table_num_tablets > 0) {
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(
        FLAGS_cdc_state_table_num_tablets);
  }

  Status s = CreateTable(&req, &resp, rpc);
  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }
  return Status::OK();
}

Status CatalogManager::IsCdcStateTableCreated(IsCreateTableDoneResponsePB* resp) {
  IsCreateTableDoneRequestPB req;

  req.mutable_table()->set_table_name(kCdcStateTableName);
  req.mutable_table()->mutable_namespace_()->set_name(kSystemNamespaceName);

  return IsCreateTableDone(&req, resp);
}

// Helper class to print a vector of CDCStreamInfo pointers.
namespace {

template<class CDCStreamInfoPointer>
std::string JoinStreamsCSVLine(std::vector<CDCStreamInfoPointer> cdc_streams) {
  std::vector<CDCStreamId> cdc_stream_ids;
  for (const auto& cdc_stream : cdc_streams) {
    cdc_stream_ids.push_back(cdc_stream->id());
  }
  return JoinCSVLine(cdc_stream_ids);
}

} // namespace

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

  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  return MarkCDCStreamsAsDeleting(streams);
}

std::vector<scoped_refptr<CDCStreamInfo>> CatalogManager::FindCDCStreamsForTable(
    const TableId& table_id) {
  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  SharedLock lock(mutex_);

  for (const auto& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if (ltm->table_id() == table_id && !ltm->started_deleting()) {
      streams.push_back(entry.second);
    }
  }
  return streams;
}

void CatalogManager::GetAllCDCStreams(std::vector<scoped_refptr<CDCStreamInfo>>* streams) {
  streams->clear();
  SharedLock lock(mutex_);
  streams->reserve(cdc_stream_map_.size());
  for (const CDCStreamInfoMap::value_type& e : cdc_stream_map_) {
    if (!e.second->LockForRead()->is_deleting()) {
      streams->push_back(e.second);
    }
  }
}

Status CatalogManager::CreateCDCStream(const CreateCDCStreamRequestPB* req,
                                       CreateCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "CreateCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();

  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(req->table_id()));

  {
    auto l = table->LockForRead();
    if (l->started_deleting()) {
      return STATUS(NotFound, "Table does not exist", req->table_id(),
                    MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  AlterTableRequestPB alter_table_req;
  alter_table_req.mutable_table()->set_table_id(req->table_id());
  alter_table_req.set_wal_retention_secs(FLAGS_cdc_wal_retention_time_secs);
  AlterTableResponsePB alter_table_resp;
  Status s = this->AlterTable(&alter_table_req, &alter_table_resp, rpc);
  if (!s.ok()) {
    return STATUS(InternalError,
                  "Unable to change the WAL retention time for table", req->table_id(),
                  MasterError(MasterErrorPB::INTERNAL_ERROR));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    TRACE("Acquired catalog manager lock");
    LockGuard lock(mutex_);

    // Construct the CDC stream if the producer wasn't bootstrapped.
    CDCStreamId stream_id;
    stream_id = GenerateIdUnlocked(SysRowEntryType::CDC_STREAM);

    stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
    stream->mutable_metadata()->StartMutation();
    SysCDCStreamEntryPB *metadata = &stream->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_table_id(table->id());
    metadata->mutable_options()->CopyFrom(req->options());
    metadata->set_state(
      req->has_initial_state() ? req->initial_state() : SysCDCStreamEntryPB::ACTIVE);

    // Add the stream to the in-memory map.
    cdc_stream_map_[stream->id()] = stream;
    cdc_stream_tables_count_map_[table->id()]++;
    resp->set_stream_id(stream->id());
  }
  TRACE("Inserted new CDC stream into CatalogManager maps");

  // Update the on-disk system catalog.
  RETURN_NOT_OK(CheckLeaderStatusAndSetupError(
      sys_catalog_->Upsert(leader_ready_term(), stream),
      "inserting CDC stream into sys-catalog", resp));
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

  if (req->stream_id_size() < 1) {
    return STATUS(InvalidArgument, "No CDC Stream ID given", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  {
    SharedLock lock(mutex_);
    for (const auto& stream_id : req->stream_id()) {
      auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);

      if (stream == nullptr || stream->LockForRead()->is_deleting()) {
        resp->add_not_found_stream_ids(stream_id);
        LOG(WARNING) << "CDC stream does not exist: " << stream_id;
      } else {
        streams.push_back(stream);
      }
    }
  }

  if (!resp->not_found_stream_ids().empty() && !req->force()) {
    string missing_streams = JoinElementsIterator(resp->not_found_stream_ids().begin(),
                                                  resp->not_found_stream_ids().end(),
                                                  ",");
    return STATUS(
        NotFound,
        Substitute("Did not find all requested CDC streams. Missing streams: [$0]. Request: $1",
                   missing_streams, req->ShortDebugString()),
        MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  Status s = MarkCDCStreamsAsDeleting(streams);
  if (!s.ok()) {
    if (s.IsIllegalState()) {
      PANIC_RPC(rpc, s.message().ToString());
    }
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  LOG(INFO) << "Successfully deleted CDC streams " << JoinStreamsCSVLine(streams)
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::MarkCDCStreamsAsDeleting(
    const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
  if (streams.empty()) {
    return Status::OK();
  }
  std::vector<CDCStreamInfo::WriteLock> locks;
  std::vector<CDCStreamInfo*> streams_to_mark;
  locks.reserve(streams.size());
  for (auto& stream : streams) {
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::DELETING);
    locks.push_back(std::move(l));
    streams_to_mark.push_back(stream.get());
  }
  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), streams_to_mark),
      "updating CDC streams in sys-catalog"));
  LOG(INFO) << "Successfully marked streams " << JoinStreamsCSVLine(streams_to_mark)
            << " as DELETING in sys catalog";
  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::FindCDCStreamsMarkedAsDeleting(
    std::vector<scoped_refptr<CDCStreamInfo>>* streams) {
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);
  for (const CDCStreamInfoMap::value_type& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();
    if (ltm->is_deleting()) {
      LOG(INFO) << "Stream " << entry.second->id() << " was marked as DELETING";
      streams->push_back(entry.second);
    }
  }
  return Status::OK();
}

Status CatalogManager::CleanUpDeletedCDCStreams(
    const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
  auto ybclient = master_->async_client_initializer().client();
  if (!ybclient) {
    return STATUS(IllegalState, "Client not initialized or shutting down");
  }

  // First. For each deleted stream, delete the cdc state rows.
  // Delete all the entries in cdc_state table that contain all the deleted cdc streams.
  client::TableHandle cdc_table;
  const client::YBTableName cdc_state_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  Status s = cdc_table.Open(cdc_state_table_name, ybclient);
  if (!s.ok()) {
    LOG(WARNING) << "Unable to open table " << master::kCdcStateTableName
                 << " to delete stream ids entries: " << s;
    return s.CloneAndPrepend("Unable to open cdc_state table");
  }

  std::shared_ptr<client::YBSession> session = ybclient->NewSession();
  std::vector<std::pair<CDCStreamId, std::shared_ptr<client::YBqlWriteOp>>> stream_ops;
  std::set<CDCStreamId> failed_streams;
  for (const auto& stream : streams) {
    LOG(INFO) << "Deleting rows for stream " << stream->id();
    TabletInfos tablets;
    scoped_refptr<TableInfo> table;
    {
      TRACE("Acquired catalog manager lock");
      SharedLock lock(mutex_);
      table = FindPtrOrNull(*table_ids_map_, stream->table_id());
    }
    // GetTablets locks lock_ in shared mode.
    if (table) {
      tablets = table->GetTablets();
    }

    for (const auto& tablet : tablets) {
      const auto delete_op = cdc_table.NewDeleteOp();
      auto* delete_req = delete_op->mutable_request();

      QLAddStringHashValue(delete_req, tablet->tablet_id());
      QLAddStringRangeValue(delete_req, stream->id());
      session->Apply(delete_op);
      stream_ops.push_back(std::make_pair(stream->id(), delete_op));
      LOG(INFO) << "Deleting stream " << stream->id() << " for tablet " << tablet->tablet_id()
              << " with request " << delete_req->ShortDebugString();
    }
  }
  // Flush all the delete operations.
  s = session->Flush();
  if (!s.ok()) {
    LOG(ERROR) << "Unable to flush operations to delete cdc streams: " << s;
    return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
  }

  for (const auto& e : stream_ops) {
    if (!e.second->succeeded()) {
      LOG(WARNING) << "Error deleting cdc_state row with tablet id "
                   << e.second->request().hashed_column_values(0).value().string_value()
                   << " and stream id "
                   << e.second->request().range_column_values(0).value().string_value()
                   << ": " << e.second->response().status();
      failed_streams.insert(e.first);
    }
  }

  // TODO: Read cdc_state table and verify that there are not rows with the specified cdc stream
  // and keep those in the map in the DELETED state to retry later.

  std::vector<CDCStreamInfo::WriteLock> locks;
  locks.reserve(streams.size() - failed_streams.size());
  std::vector<CDCStreamInfo*> streams_to_delete;
  streams_to_delete.reserve(streams.size() - failed_streams.size());

  // Delete from sys catalog only those streams that were successfully delete from cdc_state.
  for (auto& stream : streams) {
    if (failed_streams.find(stream->id()) == failed_streams.end()) {
      locks.push_back(stream->LockForWrite());
      streams_to_delete.push_back(stream.get());
    }
  }

  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Delete(leader_ready_term(), streams_to_delete),
      "deleting CDC streams from sys-catalog"));
  LOG(INFO) << "Successfully deleted streams " << JoinStreamsCSVLine(streams_to_delete)
            << " from sys catalog";

  // Remove it from the map.
  TRACE("Removing from CDC stream maps");
  {
    LockGuard lock(mutex_);
    for (const auto& stream : streams_to_delete) {
      if (cdc_stream_map_.erase(stream->id()) < 1) {
        return STATUS(IllegalState, "Could not remove CDC stream from map", stream->id());
      }
      cdc_stream_tables_count_map_[stream->table_id()]--;
    }
  }
  LOG(INFO) << "Successfully deleted streams " << JoinStreamsCSVLine(streams_to_delete)
            << " from stream map";

  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::GetCDCStream(const GetCDCStreamRequestPB* req,
                                    GetCDCStreamResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "GetCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_stream_id()) {
    return STATUS(InvalidArgument, "CDC Stream ID must be provided", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req->stream_id());
  }

  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(NotFound, "Could not find CDC stream", req->ShortDebugString(),
                  MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto stream_lock = stream->LockForRead();

  CDCStreamInfoPB* stream_info = resp->mutable_stream();

  stream_info->set_stream_id(stream->id());
  stream_info->set_table_id(stream_lock->table_id());
  stream_info->mutable_options()->CopyFrom(stream_lock->options());

  return Status::OK();
}

Status CatalogManager::ListCDCStreams(const ListCDCStreamsRequestPB* req,
                                      ListCDCStreamsResponsePB* resp) {

  scoped_refptr<TableInfo> table;
  bool filter_table = req->has_table_id();
  if (filter_table) {
    table = VERIFY_RESULT(FindTableById(req->table_id()));
  }

  SharedLock lock(mutex_);

  for (const CDCStreamInfoMap::value_type& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if ((filter_table && table->id() != ltm->table_id()) || ltm->is_deleting()) {
      continue; // Skip deleting/deleted streams and streams from other tables.
    }

    CDCStreamInfoPB* stream = resp->add_streams();
    stream->set_stream_id(entry.second->id());
    stream->set_table_id(ltm->table_id());
    stream->mutable_options()->CopyFrom(ltm->options());
    // Also add an option for the current state.
    if (ltm->pb.has_state()) {
      auto state_option = stream->add_options();
      state_option->set_key("state");
      state_option->set_value(master::SysCDCStreamEntryPB::State_Name(ltm->pb.state()));
    }
  }
  return Status::OK();
}

bool CatalogManager::CDCStreamExistsUnlocked(const CDCStreamId& stream_id) {
  scoped_refptr<CDCStreamInfo> stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return false;
  }
  return true;
}

Status CatalogManager::UpdateCDCStream(const UpdateCDCStreamRequestPB *req,
                                       UpdateCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "UpdateCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();

  // Check fields.
  if (!req->has_stream_id()) {
    return STATUS(InvalidArgument, "Stream ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_entry()) {
    return STATUS(InvalidArgument, "CDC Stream Entry must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req->stream_id());
  }
  if (stream == nullptr) {
    return STATUS(NotFound, "Could not find CDC stream", req->ShortDebugString(),
                  MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto stream_lock = stream->LockForWrite();
  if (stream_lock->is_deleting()) {
    return STATUS(NotFound, "CDC stream has been deleted", req->ShortDebugString(),
                  MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  stream_lock.mutable_data()->pb.CopyFrom(req->entry());
  // Also need to persist changes in sys catalog.
  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), stream));

  stream_lock.Commit();

  return Status::OK();
}

/*
 * UniverseReplication is setup in 4 stages within the Catalog Manager
 * 1. SetupUniverseReplication: Validates user input & requests Producer schema.
 * 2. GetTableSchemaCallback:   Validates Schema compatibility & requests Producer CDC init.
 * 3. AddCDCStreamToUniverseAndInitConsumer:  Setup RPC connections for CDC Streaming
 * 4. InitCDCConsumer:          Initializes the Consumer settings to begin tailing data
 */
Status CatalogManager::SetupUniverseReplication(const SetupUniverseReplicationRequestPB* req,
                                                SetupUniverseReplicationResponsePB* resp,
                                                rpc::RpcContext* rpc) {
  LOG(INFO) << "SetupUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();

  // Sanity checking section.
  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->producer_master_addresses_size() <= 0) {
    return STATUS(InvalidArgument, "Producer master address must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->producer_bootstrap_ids().size() > 0 &&
      req->producer_bootstrap_ids().size() != req->producer_table_ids().size()) {
    return STATUS(InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  {
    auto l = cluster_config_->LockForRead();
    if (l->pb.cluster_uuid() == req->producer_id()) {
      return STATUS(InvalidArgument, "The request UUID and cluster UUID are identical.",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  {
    auto request_master_addresses = req->producer_master_addresses();
    std::vector<ServerEntryPB> cluster_master_addresses;
    RETURN_NOT_OK(master_->ListMasters(&cluster_master_addresses));
    for (const auto &req_elem : request_master_addresses) {
      for (const auto &cluster_elem : cluster_master_addresses) {
        if (cluster_elem.has_registration()) {
          auto p_rpc_addresses = cluster_elem.registration().private_rpc_addresses();
          for (const auto &p_rpc_elem : p_rpc_addresses) {
            if (req_elem.host() == p_rpc_elem.host() && req_elem.port() == p_rpc_elem.port()) {
              return STATUS(InvalidArgument,
                "Duplicate between request master addresses and private RPC addresses",
                req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
            }
          }

          auto broadcast_addresses = cluster_elem.registration().broadcast_addresses();
          for (const auto &bc_elem : broadcast_addresses) {
            if (req_elem.host() == bc_elem.host() && req_elem.port() == bc_elem.port()) {
              return STATUS(InvalidArgument,
                "Duplicate between request master addresses and broadcast addresses",
                req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
            }
          }
        }
      }
    }
  }

  std::unordered_map<TableId, std::string> table_id_to_bootstrap_id;

  if (req->producer_bootstrap_ids_size() > 0) {
    for (int i = 0; i < req->producer_table_ids().size(); i++) {
      table_id_to_bootstrap_id[req->producer_table_ids(i)] = req->producer_bootstrap_ids(i);
    }
  }

  // We assume that the list of table ids is unique.
  if (req->producer_bootstrap_ids().size() > 0 &&
      req->producer_table_ids().size() != table_id_to_bootstrap_id.size()) {
    return STATUS(InvalidArgument, "When providing bootstrap ids, "
                  "the list of tables must be unique", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> ri;
  {
    TRACE("Acquired catalog manager lock");
    SharedLock lock(mutex_);

    if (FindPtrOrNull(universe_replication_map_, req->producer_id()) != nullptr) {
      return STATUS(InvalidArgument, "Producer already present", req->producer_id(),
                    MasterError(MasterErrorPB::INVALID_REQUEST));
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

  RETURN_NOT_OK(CheckLeaderStatusAndSetupError(
      sys_catalog_->Upsert(leader_ready_term(), ri),
      "inserting universe replication info into sys-catalog", resp));
  TRACE("Wrote universe replication info to sys-catalog");

  // Commit the in-memory state now that it's added to the persistent catalog.
  ri->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Setup universe replication from producer " << ri->ToString();

  {
    LockGuard lock(mutex_);
    universe_replication_map_[ri->id()] = ri;
  }

  // Initialize the CDC Stream by querying the Producer server for RPC sanity checks.
  auto result = ri->GetOrCreateCDCRpcTasks(req->producer_master_addresses());
  LOG(INFO) << "GetOrCreateCDCRpcTasks: " << result.ok();
  if (!result.ok()) {
    MarkUniverseReplicationFailed(ri, ResultToStatus(result));
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, result.status());
  }
  std::shared_ptr<CDCRpcTasks> cdc_rpc = *result;

  // For each table, run an async RPC task to verify a sufficient Producer:Consumer schema match.
  for (int i = 0; i < req->producer_table_ids_size(); i++) {

    // SETUP CONTINUES after this async call.
    Status s;
    if (IsColocatedParentTableId(req->producer_table_ids(i))) {
      auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
      s = cdc_rpc->client()->GetColocatedTabletSchemaById(
          req->producer_table_ids(i), tables_info,
          Bind(&enterprise::CatalogManager::GetColocatedTabletSchemaCallback, Unretained(this),
               ri->id(), tables_info, table_id_to_bootstrap_id));
    } else {
      auto table_info = std::make_shared<client::YBTableInfo>();
      s = cdc_rpc->client()->GetTableSchemaById(
          req->producer_table_ids(i), table_info,
          Bind(&enterprise::CatalogManager::GetTableSchemaCallback, Unretained(this),
               ri->id(), table_info, table_id_to_bootstrap_id));
    }

    if (!s.ok()) {
      MarkUniverseReplicationFailed(ri, s);
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
  }

  LOG(INFO) << "Started schema validation for universe replication " << ri->ToString();
  return Status::OK();
}

void CatalogManager::MarkUniverseReplicationFailed(
    scoped_refptr<UniverseReplicationInfo> universe, const Status& failure_status) {
  auto l = universe->LockForWrite();
  if (l->pb.state() == SysUniverseReplicationEntryPB::DELETED) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED_ERROR);
  } else {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
  }

  universe->SetSetupUniverseReplicationErrorStatus(failure_status);

  // Update sys_catalog.
  const Status s = sys_catalog_->Upsert(leader_ready_term(), universe);

  l.CommitOrWarn(s, "updating universe replication info in sys-catalog");
}

Status CatalogManager::ValidateTableSchema(
    const std::shared_ptr<client::YBTableInfo>& info,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids,
    TableId* consumer_table_id) {
  // Get corresponding table schema on local universe.
  GetTableSchemaRequestPB req;
  GetTableSchemaResponsePB resp;

  auto* table = req.mutable_table();
  table->set_table_name(info->table_name.table_name());
  table->mutable_namespace_()->set_name(info->table_name.namespace_name());
  table->mutable_namespace_()->set_database_type(
      GetDatabaseTypeForTable(client::ClientToPBTableType(info->table_type)));

  // Since YSQL tables are not present in table map, we first need to list tables to get the table
  // ID and then get table schema.
  // Remove this once table maps are fixed for YSQL.
  ListTablesRequestPB list_req;
  ListTablesResponsePB list_resp;

  list_req.set_name_filter(info->table_name.table_name());
  Status status = ListTables(&list_req, &list_resp);
  if (!status.ok() || list_resp.has_error()) {
    return STATUS(NotFound, Substitute("Error while listing table: $0", status.ToString()));
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
    return STATUS(NotFound,
        Substitute("Could not find matching table for $0", info->table_name.ToString()));
  }

  // We have a table match.  Now get the table schema and validate
  status = GetTableSchema(&req, &resp);
  if (!status.ok() || resp.has_error()) {
    return STATUS(NotFound, Substitute("Error while getting table schema: $0", status.ToString()));
  }

  auto result = info->schema.EquivalentForDataCopy(resp.schema());
  if (!result.ok() || !*result) {
    return STATUS(IllegalState,
        Substitute("Source and target schemas don't match: "
                   "Source: $0, Target: $1, Source schema: $2, Target schema: $3",
                   info->table_id, resp.identifier().table_id(),
                   info->schema.ToString(), resp.schema().DebugString()));
  }

  // Still need to make map of table id to resp table id (to add to validated map)
  // For colocated tables, only add the parent table since we only added the parent table to the
  // original pb (we use the number of tables in the pb to determine when validation is done).
  if (info->colocated) {
    // For now we require that colocated tables have the same table oid.
    auto source_oid = CHECK_RESULT(GetPgsqlTableOid(info->table_id));
    auto target_oid = CHECK_RESULT(GetPgsqlTableOid(resp.identifier().table_id()));
    if (source_oid != target_oid) {
    return STATUS(IllegalState,
        Substitute("Source and target table oids don't match for colocated table: "
                   "Source: $0, Target: $1, Source table oid: $2, Target table oid: $3",
                   info->table_id, resp.identifier().table_id(), source_oid, target_oid));
    }
    string parent_table_id = resp.identifier().namespace_().id() + kColocatedParentTableIdSuffix;
    *consumer_table_id = parent_table_id;
  } else {
    *consumer_table_id = resp.identifier().table_id();
  }

  return Status::OK();
}

Status CatalogManager::AddValidatedTableAndCreateCdcStreams(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::unordered_map<TableId, std::string>& table_bootstrap_ids,
      const TableId& producer_table,
      const TableId& consumer_table) {
  auto l = universe->LockForWrite();
  auto master_addresses = l->pb.producer_master_addresses();

  auto res = universe->GetOrCreateCDCRpcTasks(master_addresses);
  if (!res.ok()) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
    const Status s = sys_catalog_->Upsert(leader_ready_term(), universe);
    if (!s.ok()) {
      return CheckStatus(s, "updating universe replication info in sys-catalog");
    }
    l.Commit();
    return STATUS(InternalError,
        Substitute("Error while setting up client for producer $0", universe->id()));
  }
  std::shared_ptr<CDCRpcTasks> cdc_rpc = *res;
  vector<TableId> validated_tables;

  if (l->is_deleted_or_failed()) {
    // Nothing to do since universe is being deleted.
    return STATUS(Aborted, "Universe is being deleted");
  }

  auto map = l.mutable_data()->pb.mutable_validated_tables();
  (*map)[producer_table] = consumer_table;

  // Now, all tables are validated.
  if (l.mutable_data()->pb.validated_tables_size() == l.mutable_data()->pb.tables_size()) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::VALIDATED);
    auto tbl_iter = l->pb.tables();
    validated_tables.insert(validated_tables.begin(), tbl_iter.begin(), tbl_iter.end());
  }

  // TODO: end of config validation should be where SetupUniverseReplication exits back to user
  LOG(INFO) << "UpdateItem in AddValidatedTable";

  // Update sys_catalog.
  Status status = sys_catalog_->Upsert(leader_ready_term(), universe);
  if (!status.ok()) {
    LOG(ERROR) << "Error during UpdateItem: " << status;
    return CheckStatus(status, "updating universe replication info in sys-catalog");
  }
  l.Commit();

  // Create CDC stream for each validated table, after persisting the replication state change.
  if (!validated_tables.empty()) {
    std::unordered_map<std::string, std::string> options;
    options.reserve(2);
    options.emplace(cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
    options.emplace(cdc::kRecordFormat, CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));

    for (const auto& table : validated_tables) {
      string producer_bootstrap_id;
      auto it = table_bootstrap_ids.find(table);
      if (it != table_bootstrap_ids.end()) {
        producer_bootstrap_id = it->second;
      }
      if (!producer_bootstrap_id.empty()) {
        auto table_id = std::make_shared<TableId>();
        auto stream_options = std::make_shared<std::unordered_map<std::string, std::string>>();
        cdc_rpc->client()->GetCDCStream(producer_bootstrap_id, table_id, stream_options,
            std::bind(&enterprise::CatalogManager::GetCDCStreamCallback, this,
                producer_bootstrap_id, table_id, stream_options, universe->id(), table, cdc_rpc,
                std::placeholders::_1));
      } else {
        cdc_rpc->client()->CreateCDCStream(
            table, options,
            std::bind(&enterprise::CatalogManager::AddCDCStreamToUniverseAndInitConsumer, this,
                universe->id(), table, std::placeholders::_1, nullptr /* on_success_cb */));
      }
    }
  }
  return Status::OK();
}

void CatalogManager::GetTableSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<client::YBTableInfo>& info,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids, const Status& s) {
  // First get the universe.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe, s);
    LOG(ERROR) << "Error getting schema for table " << info->table_id << ": " << s;
    return;
  }

  // Validate the table schema.
  TableId table_id;
  Status status = ValidateTableSchema(info, table_bootstrap_ids, &table_id);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while validating table schema for table " << info->table_id
               << ": " << status;
    return;
  }

  status = AddValidatedTableAndCreateCdcStreams(universe,
                                                table_bootstrap_ids,
                                                info->table_id,
                                                table_id);
  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: " << info->table_id
               << ": " << status;
    return;
  }
}

void CatalogManager::GetColocatedTabletSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids, const Status& s) {
  // First get the universe.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe, s);
    std::ostringstream oss;
    for (int i = 0; i < infos->size(); ++i) {
      oss << ((i == 0) ? "" : ", ") << (*infos)[i].table_id;
    }
    LOG(ERROR) << "Error getting schema for tables: [ " << oss.str() << " ]: " << s;
    return;
  }

  if (infos->empty()) {
    LOG(WARNING) << "Received empty list of tables to validate: " << s;
    return;
  }

  // Validate table schemas.
  std::unordered_set<TableId> producer_parent_table_ids;
  std::unordered_set<TableId> consumer_parent_table_ids;
  for (const auto& info : *infos) {
    // Verify that we have a colocated table.
    if (!info.colocated) {
      MarkUniverseReplicationFailed(universe,
          STATUS(InvalidArgument, Substitute("Received non-colocated table: $0", info.table_id)));
      LOG(ERROR) << "Received non-colocated table: " << info.table_id;
      return;
    }
    // Validate each table, and get the parent colocated table id for the consumer.
    TableId consumer_parent_table_id;
    Status table_status = ValidateTableSchema(std::make_shared<client::YBTableInfo>(info),
                                              table_bootstrap_ids,
                                              &consumer_parent_table_id);
    if (!table_status.ok()) {
      MarkUniverseReplicationFailed(universe, table_status);
      LOG(ERROR) << "Found error while validating table schema for table " << info.table_id
                 << ": " << table_status;
      return;
    }
    // Store the parent table ids.
    producer_parent_table_ids.insert(
        info.table_name.namespace_id() + kColocatedParentTableIdSuffix);
    consumer_parent_table_ids.insert(consumer_parent_table_id);
  }

  // Verify that we only found one producer and one consumer colocated parent table id.
  if (producer_parent_table_ids.size() != 1) {
    std::ostringstream oss;
    for (auto it = producer_parent_table_ids.begin(); it != producer_parent_table_ids.end(); ++it) {
      oss << ((it == producer_parent_table_ids.begin()) ? "" : ", ") << *it;
    }
    MarkUniverseReplicationFailed(universe, STATUS(InvalidArgument,
        Substitute("Found incorrect number of producer colocated parent table ids. "
                   "Expected 1, but found: [ $0 ]", oss.str())));
    LOG(ERROR) << "Found incorrect number of producer colocated parent table ids. "
               << "Expected 1, but found: [ " << oss.str() << " ]";
    return;
  }
  if (consumer_parent_table_ids.size() != 1) {
    std::ostringstream oss;
    for (auto it = consumer_parent_table_ids.begin(); it != consumer_parent_table_ids.end(); ++it) {
      oss << ((it == consumer_parent_table_ids.begin()) ? "" : ", ") << *it;
    }
    MarkUniverseReplicationFailed(universe, STATUS(InvalidArgument,
        Substitute("Found incorrect number of consumer colocated parent table ids. "
                   "Expected 1, but found: [ $0 ]", oss.str())));
    LOG(ERROR) << "Found incorrect number of consumer colocated parent table ids. "
               << "Expected 1, but found: [ " << oss.str() << " ]";
    return;
  }

  Status status = AddValidatedTableAndCreateCdcStreams(universe,
                                                       table_bootstrap_ids,
                                                       *producer_parent_table_ids.begin(),
                                                       *consumer_parent_table_ids.begin());
  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: "
               << *producer_parent_table_ids.begin() << ": " << status;
    return;
  }
}

void CatalogManager::GetCDCStreamCallback(
    const CDCStreamId& bootstrap_id,
    std::shared_ptr<TableId> table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>> options,
    const std::string& universe_id,
    const TableId& table,
    std::shared_ptr<CDCRpcTasks> cdc_rpc,
    const Status& s) {
  if (!s.ok()) {
    LOG(ERROR) << "Unable to find bootstrap id " << bootstrap_id;
    AddCDCStreamToUniverseAndInitConsumer(universe_id, table, s);
  } else {
    if (*table_id != table) {
      const Status invalid_bootstrap_id_status = STATUS_FORMAT(
          InvalidArgument, "Invalid bootstrap id for table $0. Bootstrap id $1 belongs to table $2",
          table, bootstrap_id, *table_id);
      LOG(ERROR) << invalid_bootstrap_id_status;
      AddCDCStreamToUniverseAndInitConsumer(universe_id, table, invalid_bootstrap_id_status);
      return;
    }
    // todo check options
    AddCDCStreamToUniverseAndInitConsumer(universe_id, table, bootstrap_id, [&] () {
        // Extra callback on universe setup success - update the producer to let it know that
        // the bootstrapping is complete.
        SysCDCStreamEntryPB new_entry;
        new_entry.set_table_id(*table_id);
        new_entry.mutable_options()->Reserve(narrow_cast<int>(options->size()));
        for (const auto& option : *options) {
          auto new_option = new_entry.add_options();
          new_option->set_key(option.first);
          new_option->set_value(option.second);
        }
        new_entry.set_state(master::SysCDCStreamEntryPB::ACTIVE);

        auto s = cdc_rpc->client()->UpdateCDCStream(bootstrap_id, new_entry);
        if (!s.ok()) {
          LOG(WARNING) << "Unable to update CDC stream options " << s;
        }
      });
  }
}

void CatalogManager::AddCDCStreamToUniverseAndInitConsumer(
    const std::string& universe_id, const TableId& table_id, const Result<CDCStreamId>& stream_id,
    std::function<void()> on_success_cb) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!stream_id.ok()) {
    LOG(ERROR) << "Error setting up CDC stream for table " << table_id;
    MarkUniverseReplicationFailed(universe, ResultToStatus(stream_id));
    return;
  }

  bool merge_alter = false;
  bool validated_all_tables = false;
  {
    auto l = universe->LockForWrite();
    if (l->is_deleted_or_failed()) {
      // Nothing to do if universe is being deleted.
      return;
    }

    auto map = l.mutable_data()->pb.mutable_table_streams();
    (*map)[table_id] = *stream_id;

    // This functions as a barrier: waiting for the last RPC call from GetTableSchemaCallback.
    if (l.mutable_data()->pb.table_streams_size() == l->pb.tables_size()) {
      // All tables successfully validated! Register CDC consumers & start replication.
      validated_all_tables = true;
      LOG(INFO) << "Registering CDC consumers for universe " << universe->id();

      auto& validated_tables = l->pb.validated_tables();

      std::vector<CDCConsumerStreamInfo> consumer_info;
      consumer_info.reserve(l->pb.tables_size());
      for (const auto& table : validated_tables) {
        CDCConsumerStreamInfo info;
        info.producer_table_id = table.first;
        info.consumer_table_id = table.second;
        info.stream_id = (*map)[info.producer_table_id];
        consumer_info.push_back(info);
      }

      std::vector<HostPort> hp;
      HostPortsFromPBs(l->pb.producer_master_addresses(), &hp);

      Status s = InitCDCConsumer(consumer_info, HostPort::ToCommaSeparatedString(hp),
          l->pb.producer_id());
      if (!s.ok()) {
        LOG(ERROR) << "Error registering subscriber: " << s;
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
      } else {
        GStringPiece original_producer_id(universe->id());
        if (original_producer_id.ends_with(".ALTER")) {
          // Don't enable ALTER universes, merge them into the main universe instead.
          merge_alter = true;
        } else {
          l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
        }
      }
    }

    // Update sys_catalog with new producer table id info.
    Status status = sys_catalog_->Upsert(leader_ready_term(), universe);

    // Before committing, run any callbacks on success.
    if (status.ok() && on_success_cb &&
        (l.mutable_data()->pb.state() == SysUniverseReplicationEntryPB::ACTIVE || merge_alter)) {
      on_success_cb();
    }

    l.CommitOrWarn(status, "updating universe replication info in sys-catalog");
  }

  if (validated_all_tables) {
    // Also update the set of consumer tables.
    LockGuard lock(mutex_);
    auto l = universe->LockForRead();
    for (const auto& table : l->pb.validated_tables()) {
      xcluster_consumer_tables_to_stream_map_[table.second].emplace(universe->id(), *stream_id);
    }
  }

  // If this is an 'alter', merge back into primary command now that setup is a success.
  if (merge_alter) {
    MergeUniverseReplication(universe);
  }
}

/*
 * UpdateCDCConsumerOnTabletSplit updates the consumer -> producer tablet mapping after a local
 * tablet split.
 */
Status CatalogManager::UpdateCDCConsumerOnTabletSplit(
    const TableId& consumer_table_id,
    const SplitTabletIds& split_tablet_ids) {
  // Check if this table is consuming a stream.
  XClusterConsumerTableStreamInfoMap stream_infos =
      GetXClusterStreamInfoForConsumerTable(consumer_table_id);

  auto l = cluster_config_->LockForWrite();
  for (const auto& stream_info : stream_infos) {
    std::string universe_id = stream_info.first;
    CDCStreamId stream_id = stream_info.second;
    // Fetch the stream entry so we can update the mappings.
    auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto producer_entry = FindOrNull(*producer_map, universe_id);
    if (!producer_entry) {
      return STATUS_FORMAT(NotFound,
                           "Unable to find the producer entry for universe $0",
                           universe_id);
    }
    auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id);
    if (!stream_entry) {
      return STATUS_FORMAT(NotFound,
                           "Unable to find the stream entry for universe $0, stream $1",
                           universe_id,
                           stream_id);
    }

    RETURN_NOT_OK(UpdateTableMappingOnTabletSplit(stream_entry, split_tablet_ids));

    // We also need to mark this stream as no longer being able to perform 1-1 mappings.
    stream_entry->set_same_num_producer_consumer_tablets(false);
  }

  // Also bump the cluster_config_ version so that changes are propagated to tservers.
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
                            "Updating cluster config in sys-catalog"));
  l.Commit();

  return Status::OK();
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
        stream_info.producer_table_id, stream_info.consumer_table_id, producer_universe_uuid,
        master_addrs, consumer_table_resp, &tserver_addrs, &stream_entry));
    (*producer_entry.mutable_stream_map())[stream_info.stream_id] = std::move(stream_entry);
  }

  // Log the Network topology of the Producer Cluster
  auto master_addrs_list = StringSplit(master_addrs, ',');
  producer_entry.mutable_master_addrs()->Reserve(narrow_cast<int>(master_addrs_list.size()));
  for (const auto& addr : master_addrs_list) {
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, 0));
    HostPortToPB(hp, producer_entry.add_master_addrs());
  }

  producer_entry.mutable_tserver_addrs()->Reserve(narrow_cast<int>(tserver_addrs.size()));
  for (const auto& addr : tserver_addrs) {
    HostPortToPB(addr, producer_entry.add_tserver_addrs());
  }

  auto l = cluster_config_->LockForWrite();
  auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto it = producer_map->find(producer_universe_uuid);
  if (it != producer_map->end()) {
    return STATUS(InvalidArgument, "Already created a consumer for this universe");
  }

  // TServers will use the ClusterConfig to create CDC Consumers for applicable local tablets.
  (*producer_map)[producer_universe_uuid] = std::move(producer_entry);
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
      "updating cluster config in sys-catalog"));
  l.Commit();

  return Status::OK();
}

void CatalogManager::MergeUniverseReplication(scoped_refptr<UniverseReplicationInfo> universe) {
  // Merge back into primary command now that setup is a success.
  GStringPiece original_producer_id(universe->id());
  if (!original_producer_id.ends_with(".ALTER")) {
    return;
  }
  original_producer_id.remove_suffix(sizeof(".ALTER")-1 /* exclude \0 ending */);
  LOG(INFO) << "Merging CDC universe: " << universe->id()
            << " into " << original_producer_id.ToString();

  scoped_refptr<UniverseReplicationInfo> original_universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    original_universe = FindPtrOrNull(universe_replication_map_, original_producer_id.ToString());
    if (original_universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << original_producer_id.ToString();
      return;
    }
  }
  // Merge Cluster Config for TServers.
  {
    auto cl = cluster_config_->LockForWrite();
    auto pm = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto original_producer_entry = pm->find(original_universe->id());
    auto alter_producer_entry = pm->find(universe->id());
    if (original_producer_entry != pm->end() && alter_producer_entry != pm->end()) {
      // Merge the Tables from the Alter into the original.
      auto as = alter_producer_entry->second.stream_map();
      original_producer_entry->second.mutable_stream_map()->insert(as.begin(), as.end());
      // Delete the Alter
      pm->erase(alter_producer_entry);
    } else {
      LOG(WARNING) << "Could not find both universes in Cluster Config: " << universe->id();
    }
    cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
    const Status s = sys_catalog_->Upsert(leader_ready_term(), cluster_config_);
    cl.CommitOrWarn(s, "updating cluster config in sys-catalog");
  }
  // Merge Master Config on Consumer. (no need for Producer changes, since it uses stream_id)
  {
    auto original_lock = original_universe->LockForWrite();
    auto alter_lock = universe->LockForWrite();
    // Merge Table->StreamID mapping.
    auto at = alter_lock.mutable_data()->pb.mutable_tables();
    original_lock.mutable_data()->pb.mutable_tables()->MergeFrom(*at);
    at->Clear();
    auto as = alter_lock.mutable_data()->pb.mutable_table_streams();
    original_lock.mutable_data()->pb.mutable_table_streams()->insert(as->begin(), as->end());
    as->clear();
    auto av = alter_lock.mutable_data()->pb.mutable_validated_tables();
    original_lock.mutable_data()->pb.mutable_validated_tables()->insert(av->begin(), av->end());
    av->clear();
    alter_lock.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED);

    vector<UniverseReplicationInfo*> universes{original_universe.get(), universe.get()};
    const Status s = sys_catalog_->Upsert(leader_ready_term(), universes);
    alter_lock.CommitOrWarn(s, "updating universe replication entries in sys-catalog");
    if (s.ok()) {
      original_lock.Commit();
    }
  }
  // TODO: universe_replication_map_.erase(universe->id()) at a later time.
  //       TwoDCTest.AlterUniverseReplicationTables crashes due to undiagnosed race right now.
  LOG(INFO) << "Done with Merging " << universe->id() << " into " << original_universe->id();
}

Status ReturnErrorOrAddWarning(const Status& s,
                               const DeleteUniverseReplicationRequestPB* req,
                               DeleteUniverseReplicationResponsePB* resp) {
  if (!s.ok()) {
    if (req->force()) {
      // Continue executing, save the status as a warning.
      AppStatusPB* warning = resp->add_warnings();
      StatusToPB(s, warning);
      return Status::OK();
    }
    return s.CloneAndAppend("\nUse 'ignore-errors' to ignore this error.");
  }
  return s;
}

Status CatalogManager::DeleteUniverseReplication(const DeleteUniverseReplicationRequestPB* req,
                                                 DeleteUniverseReplicationResponsePB* resp,
                                                 rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteUniverseReplication request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID required", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> ri;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    ri = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (ri == nullptr) {
      return STATUS(NotFound, "Universe replication info does not exist",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  auto l = ri->LockForWrite();
  l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED);

  // Delete subscribers on the Consumer Registry (removes from TServers).
  LOG(INFO) << "Deleting subscribers for producer " << req->producer_id();
  {
    auto cl = cluster_config_->LockForWrite();
    auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(req->producer_id());
    if (it != producer_map->end()) {
      producer_map->erase(it);
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
          "updating cluster config in sys-catalog"));
      cl.Commit();
    }
  }

  // Delete CDC stream config on the Producer.
  if (!l->pb.table_streams().empty()) {
    auto result = ri->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses());
    if (!result.ok()) {
      LOG(WARNING) << "Unable to create cdc rpc task. CDC streams won't be deleted: " << result;
    } else {
      auto cdc_rpc = *result;
      vector<CDCStreamId> streams;
      std::unordered_map<CDCStreamId, TableId> stream_to_producer_table_id;
      for (const auto& table : l->pb.table_streams()) {
        streams.push_back(table.second);
        stream_to_producer_table_id.emplace(table.second, table.first);
      }

      DeleteCDCStreamResponsePB delete_cdc_stream_resp;
      auto s = cdc_rpc->client()->DeleteCDCStream(streams, req->force(), &delete_cdc_stream_resp);

      if (delete_cdc_stream_resp.not_found_stream_ids().size() > 0) {
        std::ostringstream missing_streams;
        for (auto it = delete_cdc_stream_resp.not_found_stream_ids().begin();
               it != delete_cdc_stream_resp.not_found_stream_ids().end();
               ++it) {
          if (it != delete_cdc_stream_resp.not_found_stream_ids().begin()) {
            missing_streams << ",";
          }
          missing_streams << *it << " (table_id: " << stream_to_producer_table_id[*it] << ")";
        }
        if (s.ok()) {
          // Returned but did not find some streams, so still need to warn the user about those.
          s = STATUS(NotFound,
                     "Could not find the following streams: [" + missing_streams.str() + "].");
        } else {
          s = s.CloneAndPrepend(
              "Could not find the following streams: [" + missing_streams.str() + "].");
        }
      }

      RETURN_NOT_OK(ReturnErrorOrAddWarning(s, req, resp));
    }
  }

  // Delete universe in the Universe Config.
  RETURN_NOT_OK(ReturnErrorOrAddWarning(DeleteUniverseReplicationUnlocked(ri), req, resp));
  l.Commit();

  LOG(INFO) << "Processed delete universe replication " << ri->ToString()
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::DeleteUniverseReplicationUnlocked(
    scoped_refptr<UniverseReplicationInfo> universe) {
  // Assumes that caller has locked universe.
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Delete(leader_ready_term(), universe),
      Substitute("An error occurred while updating sys-catalog, universe_id: $0", universe->id()));

  // Remove it from the map.
  LockGuard lock(mutex_);
  if (universe_replication_map_.erase(universe->id()) < 1) {
    LOG(WARNING) << "Failed to remove replication info from map: universe_id: " << universe->id();
  }
  // Also update the mapping of consumer tables.
  for (const auto& table : universe->metadata().state().pb.validated_tables()) {
    if (xcluster_consumer_tables_to_stream_map_[table.second].erase(universe->id()) < 1) {
      LOG(WARNING) << "Failed to remove consumer table from mapping. "
                   << "table_id: " << table.second << ": universe_id: " << universe->id();
    }
    if (xcluster_consumer_tables_to_stream_map_[table.second].empty()) {
      xcluster_consumer_tables_to_stream_map_.erase(table.second);
    }
  }
  return Status::OK();
}

Status CatalogManager::SetUniverseReplicationEnabled(
    const SetUniverseReplicationEnabledRequestPB* req,
    SetUniverseReplicationEnabledResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing SetUniverseReplicationEnabled request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  // Sanity Checking Cluster State and Input.
  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_is_enabled()) {
    return STATUS(InvalidArgument, "Must explicitly set whether to enable",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  // Update the Master's Universe Config with the new state.
  {
    auto l = universe->LockForWrite();
    if (l->pb.state() != SysUniverseReplicationEntryPB::DISABLED &&
        l->pb.state() != SysUniverseReplicationEntryPB::ACTIVE) {
      return STATUS(
          InvalidArgument,
          Format("Universe Replication in invalid state: $0.  Retry or Delete.",
              SysUniverseReplicationEntryPB::State_Name(l->pb.state())),
          req->ShortDebugString(),
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    if (req->is_enabled()) {
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
    } else { // DISABLE.
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DISABLED);
    }
    RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), universe),
        "updating universe replication info in sys-catalog"));
    l.Commit();
  }

  // Modify the Consumer Registry, which will fan out this info to all TServers on heartbeat.
  {
    auto l = cluster_config_->LockForWrite();
    auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(req->producer_id());
    if (it == producer_map->end()) {
      LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << req->producer_id();
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    (*it).second.set_disable_stream(!req->is_enabled());
    l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
    RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
        "updating cluster config in sys-catalog"));
    l.Commit();
  }

  return Status::OK();
}

Status CatalogManager::AlterUniverseReplication(const AlterUniverseReplicationRequestPB* req,
                                                AlterUniverseReplicationResponsePB* resp,
                                                rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing AlterUniverseReplication request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  // Sanity Checking Cluster State and Input.
  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Verify that there is an existing Universe config
  scoped_refptr<UniverseReplicationInfo> original_ri;
  {
    SharedLock lock(mutex_);

    original_ri = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (original_ri == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  // Currently, config options are mutually exclusive to simplify transactionality.
  int config_count = (req->producer_master_addresses_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_remove_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_add_size() > 0 ? 1 : 0) +
                     (req->has_new_producer_universe_id() ? 1 : 0);
  if (config_count != 1) {
    return STATUS(InvalidArgument, "Only 1 Alter operation per request currently supported",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Config logic...
  if (req->producer_master_addresses_size() > 0) {
    // 'set_master_addresses'
    // TODO: Verify the input. Setup an RPC Task, ListTables, ensure same.

    // 1a. Persistent Config: Update the Universe Config for Master.
    {
      auto l = original_ri->LockForWrite();
      l.mutable_data()->pb.mutable_producer_master_addresses()->CopyFrom(
          req->producer_master_addresses());
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), original_ri),
          "updating universe replication info in sys-catalog"));
      l.Commit();
    }
    // 1b. Persistent Config: Update the Consumer Registry (updates TServers)
    {
      auto l = cluster_config_->LockForWrite();
      auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
      auto it = producer_map->find(req->producer_id());
      if (it == producer_map->end()) {
        LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << req->producer_id();
        return STATUS(NotFound, "Could not find CDC producer universe",
                      req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
      (*it).second.mutable_master_addrs()->CopyFrom(req->producer_master_addresses());
      l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
          "updating cluster config in sys-catalog"));
      l.Commit();
    }
    // 2. Memory Update: Change cdc_rpc_tasks (Master cache)
    {
      auto result = original_ri->GetOrCreateCDCRpcTasks(req->producer_master_addresses());
      if (!result.ok()) {
        return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, result.status());
      }
    }
  } else if (req->producer_table_ids_to_remove_size() > 0) {
    // 'remove_table'
    auto it = req->producer_table_ids_to_remove();
    std::set<string> table_ids_to_remove(it.begin(), it.end());
    // Filter out any tables that aren't in the existing replication config.
    {
      auto l = original_ri->LockForRead();
      auto tbl_iter = l->pb.tables();
      std::set<string> existing_tables(tbl_iter.begin(), tbl_iter.end()), filtered_list;
      set_intersection(table_ids_to_remove.begin(), table_ids_to_remove.end(),
                       existing_tables.begin(), existing_tables.end(),
                       std::inserter(filtered_list, filtered_list.begin()));
      filtered_list.swap(table_ids_to_remove);
    }

    vector<CDCStreamId> streams_to_remove;
    // 1. Update the Consumer Registry (removes from TServers).
    {
      auto cl = cluster_config_->LockForWrite();
      auto pm = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
      auto producer_entry = pm->find(req->producer_id());
      if (producer_entry != pm->end()) {
        // Remove the Tables Specified (not part of the key).
        auto stream_map = producer_entry->second.mutable_stream_map();
        for (auto& p : *stream_map) {
          if (table_ids_to_remove.count(p.second.producer_table_id()) > 0) {
            streams_to_remove.push_back(p.first);
          }
        }
        if (streams_to_remove.size() == stream_map->size()) {
          // If this ends with an empty Map, disallow and force user to delete.
          LOG(WARNING) << "CDC 'remove_table' tried to remove all tables." << req->producer_id();
          return STATUS(
              InvalidArgument,
              "Cannot remove all tables with alter. Use delete_universe_replication instead.",
              req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
        } else if (streams_to_remove.empty()) {
          // If this doesn't delete anything, notify the user.
          return STATUS(InvalidArgument, "Removal matched no entries.",
                        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
        }
        for (auto& key : streams_to_remove) {
          stream_map->erase(stream_map->find(key));
        }
      }
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), cluster_config_),
          "updating cluster config in sys-catalog"));
      cl.Commit();
    }
    // 2. Remove from Master Configs on Producer and Consumer.
    {
      auto l = original_ri->LockForWrite();
      if (!l->pb.table_streams().empty()) {
        // Delete Relevant Table->StreamID mappings on Consumer.
        auto table_streams = l.mutable_data()->pb.mutable_table_streams();
        auto validated_tables = l.mutable_data()->pb.mutable_validated_tables();
        for (auto& key : table_ids_to_remove) {
          table_streams->erase(table_streams->find(key));
          validated_tables->erase(validated_tables->find(key));
        }
        for (int i = 0; i < l.mutable_data()->pb.tables_size(); i++) {
          if (table_ids_to_remove.count(l.mutable_data()->pb.tables(i)) > 0) {
            l.mutable_data()->pb.mutable_tables()->DeleteSubrange(i, 1);
            --i;
          }
        }
        // Delete CDC stream config on the Producer.
        auto result = original_ri->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses());
        if (!result.ok()) {
          LOG(WARNING) << "Unable to create cdc rpc task. CDC streams won't be deleted: " << result;
        } else {
          auto s = (*result)->client()->DeleteCDCStream(streams_to_remove);
          if (!s.ok()) {
            std::stringstream os;
            std::copy(streams_to_remove.begin(), streams_to_remove.end(),
                      std::ostream_iterator<CDCStreamId>(os, ", "));
            LOG(WARNING) << "Unable to delete CDC streams: " << os.str() << s;
          }
        }
      }
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), original_ri),
          "updating universe replication info in sys-catalog"));
      l.Commit();
    }
  } else if (req->producer_table_ids_to_add_size() > 0) {
    // 'add_table'
    string alter_producer_id = req->producer_id() + ".ALTER";

    // If user passed in bootstrap ids, check that there is a bootstrap id for every table.
    if (req->producer_bootstrap_ids_to_add().size() > 0 &&
      req->producer_table_ids_to_add().size() != req->producer_bootstrap_ids_to_add().size()) {
      return STATUS(InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // Verify no 'alter' command running.
    scoped_refptr<UniverseReplicationInfo> alter_ri;
    {
      SharedLock lock(mutex_);
      alter_ri = FindPtrOrNull(universe_replication_map_, alter_producer_id);
    }
    {
      if (alter_ri != nullptr) {
        LOG(INFO) << "Found " << alter_producer_id << "... Removing";
        if (alter_ri->LockForRead()->is_deleted_or_failed()) {
          // Delete previous Alter if it's completed but failed.
          master::DeleteUniverseReplicationRequestPB delete_req;
          delete_req.set_producer_id(alter_ri->id());
          master::DeleteUniverseReplicationResponsePB delete_resp;
          Status s = DeleteUniverseReplication(&delete_req, &delete_resp, rpc);
          if (!s.ok()) {
            if (delete_resp.has_error()) {
              resp->mutable_error()->Swap(delete_resp.mutable_error());
              return s;
            }
            return SetupError(resp->mutable_error(), s);
          }
        } else {
          return STATUS(InvalidArgument, "Alter for CDC producer currently running",
                        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
        }
      }
    }

    // Map each table id to its corresponding bootstrap id.
    std::unordered_map<TableId, std::string> table_id_to_bootstrap_id;
    if (req->producer_bootstrap_ids_to_add().size() > 0) {
      for (int i = 0; i < req->producer_table_ids_to_add().size(); i++) {
        table_id_to_bootstrap_id[req->producer_table_ids_to_add(i)]
          = req->producer_bootstrap_ids_to_add(i);
      }

      // Ensure that table ids are unique. We need to do this here even though
      // the same check is performed by SetupUniverseReplication because
      // duplicate table ids can cause a bootstrap id entry in table_id_to_bootstrap_id
      // to be overwritten.
      if (table_id_to_bootstrap_id.size() != req->producer_table_ids_to_add().size()) {
        return STATUS(InvalidArgument, "When providing bootstrap ids, "
                      "the list of tables must be unique", req->ShortDebugString(),
                      MasterError(MasterErrorPB::INVALID_REQUEST));
      }
    }

    // Only add new tables.  Ignore tables that are currently being replicated.
    auto tid_iter = req->producer_table_ids_to_add();
    std::unordered_set<string> new_tables(tid_iter.begin(), tid_iter.end());
    {
      auto l = original_ri->LockForRead();
      for(auto t : l->pb.tables()) {
        auto pos = new_tables.find(t);
        if (pos != new_tables.end()) {
          new_tables.erase(pos);
        }
      }
    }
    if (new_tables.empty()) {
      return STATUS(InvalidArgument, "CDC producer already contains all requested tables",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // 1. create an ALTER table request that mirrors the original 'setup_replication'.
    master::SetupUniverseReplicationRequestPB setup_req;
    master::SetupUniverseReplicationResponsePB setup_resp;
    setup_req.set_producer_id(alter_producer_id);
    setup_req.mutable_producer_master_addresses()->CopyFrom(
        original_ri->LockForRead()->pb.producer_master_addresses());
    for (auto t : new_tables) {
      setup_req.add_producer_table_ids(t);

      // Add bootstrap id to request if it exists.
      auto bootstrap_id_lookup_result = table_id_to_bootstrap_id.find(t);
      if (bootstrap_id_lookup_result != table_id_to_bootstrap_id.end()) {
        setup_req.add_producer_bootstrap_ids(bootstrap_id_lookup_result->second);
      }
    }

    // 2. run the 'setup_replication' pipeline on the ALTER Table
    Status s = SetupUniverseReplication(&setup_req, &setup_resp, rpc);
    if (!s.ok()) {
      if (setup_resp.has_error()) {
        resp->mutable_error()->Swap(setup_resp.mutable_error());
        return s;
      }
      return SetupError(resp->mutable_error(), s);
    }
    // NOTE: ALTER merges back into original after completion.
  } else if (req->has_new_producer_universe_id()) {
    Status s = RenameUniverseReplication(original_ri, req, resp, rpc);
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), s);
    }
  }

  return Status::OK();
}

Status CatalogManager::RenameUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe,
    const AlterUniverseReplicationRequestPB* req,
    AlterUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  const string old_universe_replication_id = universe->id();
  const string new_producer_universe_id = req->new_producer_universe_id();
  if (old_universe_replication_id == new_producer_universe_id) {
    return STATUS(InvalidArgument, "Old and new replication ids must be different",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  {
    LockGuard lock(mutex_);
    auto l = universe->LockForWrite();
    scoped_refptr<UniverseReplicationInfo> new_ri;

    // Assert that new_replication_name isn't already in use.
    if (FindPtrOrNull(universe_replication_map_, new_producer_universe_id) != nullptr) {
      return STATUS(InvalidArgument, "New replication id is already in use",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // Since the producer_id is used as the key, we need to create a new UniverseReplicationInfo.
    new_ri = new UniverseReplicationInfo(new_producer_universe_id);
    new_ri->mutable_metadata()->StartMutation();
    SysUniverseReplicationEntryPB *metadata = &new_ri->mutable_metadata()->mutable_dirty()->pb;
    metadata->CopyFrom(l->pb);
    metadata->set_producer_id(new_producer_universe_id);

    // Also need to update internal maps.
    auto cl = cluster_config_->LockForWrite();
    auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    (*producer_map)[new_producer_universe_id] =
        std::move((*producer_map)[old_universe_replication_id]);
    producer_map->erase(old_universe_replication_id);

    {
      // Need both these updates to be atomic.
      auto w = sys_catalog_->NewWriter(leader_ready_term());
      RETURN_NOT_OK(w->Mutate(QLWriteRequestPB::QL_STMT_DELETE, universe.get()));
      RETURN_NOT_OK(w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE,
                              new_ri.get(),
                              cluster_config_.get()));
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->SyncWrite(w.get()),
          "Updating universe replication info and cluster config in sys-catalog"));
    }
    new_ri->mutable_metadata()->CommitMutation();
    cl.Commit();

    // Update universe_replication_map after persistent data is saved.
    universe_replication_map_[new_producer_universe_id] = new_ri;
    universe_replication_map_.erase(old_universe_replication_id);
  }

  return Status::OK();
}

Status CatalogManager::GetUniverseReplication(const GetUniverseReplicationRequestPB* req,
                                              GetUniverseReplicationResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  resp->mutable_entry()->CopyFrom(universe->LockForRead()->pb);
  return Status::OK();
}

/*
 * Checks if the universe replication setup has completed.
 * Returns Status::OK() if this call succeeds, and uses resp->done() to determine if the setup has
 * completed (either failed or succeeded). If the setup has failed, then resp->replication_error()
 * is also set.
 */
Status CatalogManager::IsSetupUniverseReplicationDone(
    const IsSetupUniverseReplicationDoneRequestPB* req,
    IsSetupUniverseReplicationDoneResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "IsSetupUniverseReplicationDone from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  GetUniverseReplicationRequestPB universe_req;
  GetUniverseReplicationResponsePB universe_resp;
  universe_req.set_producer_id(req->producer_id());

  RETURN_NOT_OK(GetUniverseReplication(&universe_req, &universe_resp, /* RpcContext */ nullptr));
  if (universe_resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(universe_resp.error().status()));
  }

  // Two cases for completion:
  //  - For a regular SetupUniverseReplication, we want to wait for the universe to become ACTIVE.
  //  - For an AlterUniverseReplication, we need to wait until the .ALTER universe gets merged with
  //    the main universe - at which point the .ALTER universe is deleted.
  bool isAlterRequest = GStringPiece(req->producer_id()).ends_with(".ALTER");
  if ((!isAlterRequest && universe_resp.entry().state() == SysUniverseReplicationEntryPB::ACTIVE) ||
      (isAlterRequest && universe_resp.entry().state() == SysUniverseReplicationEntryPB::DELETED)) {
    resp->set_done(true);
    StatusToPB(Status::OK(), resp->mutable_replication_error());
    return Status::OK();
  }

  // Otherwise we have either failed (see MarkUniverseReplicationFailed), or are still working.
  if (universe_resp.entry().state() == SysUniverseReplicationEntryPB::DELETED_ERROR ||
      universe_resp.entry().state() == SysUniverseReplicationEntryPB::FAILED) {
    resp->set_done(true);

    // Get the more detailed error.
    scoped_refptr<UniverseReplicationInfo> universe;
    {
      SharedLock lock(mutex_);
      universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
      if (universe == nullptr) {
        StatusToPB(
            STATUS(InternalError, "Could not find CDC producer universe after having failed."),
            resp->mutable_replication_error());
        return Status::OK();
      }
    }
    if (!universe->GetSetupUniverseReplicationErrorStatus().ok()) {
      StatusToPB(universe->GetSetupUniverseReplicationErrorStatus(),
                 resp->mutable_replication_error());
    } else {
      LOG(WARNING) << "Did not find setup universe replication error status.";
      StatusToPB(STATUS(InternalError, "unknown error"), resp->mutable_replication_error());
    }
    return Status::OK();
  }

  // Not done yet.
  resp->set_done(false);
  return Status::OK();
}

bool CatalogManager::IsTableCdcProducer(const TableInfo& table_info) const {
  auto it = cdc_stream_tables_count_map_.find(table_info.id());
  if (it == cdc_stream_tables_count_map_.end()) {
    return false;
  }
  return it->second > 0;
}

bool CatalogManager::IsTableCdcConsumer(const TableInfo& table_info) const {
  auto it = xcluster_consumer_tables_to_stream_map_.find(table_info.id());
  if (it == xcluster_consumer_tables_to_stream_map_.end()) {
    return false;
  }
  return !it->second.empty();
}

CatalogManager::XClusterConsumerTableStreamInfoMap
    CatalogManager::GetXClusterStreamInfoForConsumerTable(const TableId& table_id) const {
  SharedLock lock(mutex_);
  auto it = xcluster_consumer_tables_to_stream_map_.find(table_id);
  if (it == xcluster_consumer_tables_to_stream_map_.end()) {
    return {};
  }

  return it->second;
}

bool CatalogManager::IsCdcEnabled(
    const TableInfo& table_info) const {
  SharedLock lock(mutex_);
  return IsTableCdcProducer(table_info) || IsTableCdcConsumer(table_info);
}

void CatalogManager::Started() {
  snapshot_coordinator_.Start();
}

Result<SnapshotSchedulesToObjectIdsMap> CatalogManager::MakeSnapshotSchedulesToObjectIdsMap(
    SysRowEntryType type) {
  return snapshot_coordinator_.MakeSnapshotSchedulesToObjectIdsMap(type);
}

Result<bool> CatalogManager::IsTablePartOfSomeSnapshotSchedule(const TableInfo& table_info) {
  return snapshot_coordinator_.IsTableCoveredBySomeSnapshotSchedule(table_info);
}

void CatalogManager::SysCatalogLoaded(int64_t term) {
  return snapshot_coordinator_.SysCatalogLoaded(term);
}

size_t CatalogManager::GetNumLiveTServersForActiveCluster() {
  BlacklistSet blacklist = BlacklistSetFromPB();
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptorsInCluster(&ts_descs, placement_uuid(), blacklist);
  return ts_descs.size();
}

}  // namespace enterprise
}  // namespace master
}  // namespace yb
