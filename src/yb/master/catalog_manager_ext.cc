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
#include <queue>
#include <regex>
#include <set>
#include <unordered_set>
#include <google/protobuf/util/message_differencer.h>

#include "yb/common/colocated_util.h"
#include "yb/common/common_fwd.h"
#include "yb/common/constants.h"
#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/pg_system_attr.h"
#include "yb/qlexpr/ql_name.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_type_util.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/ysql_tablegroup_manager.h"

#include "yb/client/client-internal.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/consensus/consensus.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb_pgapi.h"

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
#include "yb/util/flags.h"
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

#include "yb/yql/cql/ql/util/statement_result.h"

#include "ybgate/ybgate_api.h"

using namespace std::literals;
using namespace std::placeholders;

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::unordered_set;
using std::vector;

using strings::Substitute;

DECLARE_int32(master_rpc_timeout_ms);

DEFINE_RUNTIME_bool(enable_transaction_snapshots, true,
    "The flag enables usage of transaction aware snapshots.");
TAG_FLAG(enable_transaction_snapshots, hidden);
TAG_FLAG(enable_transaction_snapshots, advanced);

DEPRECATE_FLAG(bool, allow_consecutive_restore, "10_2022");

DEFINE_test_flag(double, crash_during_sys_catalog_restoration, 0.0,
                 "Probability of crash during the RESTORE_SYS_CATALOG phase.");

DEFINE_test_flag(
    bool, import_snapshot_failed, false,
    "Return a error from ImportSnapshotMeta RPC for testing the RPC failure.");

DEFINE_RUNTIME_uint64(import_snapshot_max_concurrent_create_table_requests, 20,
    "Maximum number of create table requests to the master that can be outstanding "
    "during the import snapshot metadata phase of restore.");

DEFINE_RUNTIME_int32(inflight_splits_completion_timeout_secs, 600,
    "Total time to wait for all inflight splits to complete during Restore.");
TAG_FLAG(inflight_splits_completion_timeout_secs, advanced);

DEFINE_RUNTIME_int32(pitr_max_restore_duration_secs, 600,
    "Maximum amount of time to complete a PITR restore.");
TAG_FLAG(pitr_max_restore_duration_secs, advanced);

DEFINE_RUNTIME_int32(pitr_split_disable_check_freq_ms, 500,
    "Delay before retrying to see if inflight tablet split operations have completed "
    "after which PITR restore can be performed.");
TAG_FLAG(pitr_split_disable_check_freq_ms, advanced);

DEFINE_RUNTIME_bool(
    allow_ycql_transactional_xcluster, false,
    "Determines if xCluster transactional replication on YCQL tables is allowed.");

DEFINE_RUNTIME_bool(
    enable_fast_pitr, true,
    "Whether fast restore of sys catalog on the master is enabled.");

namespace yb {

using rpc::RpcContext;
using pb_util::ParseFromSlice;
using client::internal::RemoteTabletServer;
using client::internal::RemoteTabletPtr;

namespace master {

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

Status CatalogManager::CreateSnapshot(const CreateSnapshotRequestPB* req,
                                      CreateSnapshotResponsePB* resp,
                                      RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing CreateSnapshot request: " << req->ShortDebugString();

  if (FLAGS_enable_transaction_snapshots && req->transaction_aware()) {
    return CreateTransactionAwareSnapshot(*req, resp, rpc->GetClientDeadline());
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

  return CreateNonTransactionAwareSnapshot(req, resp, rpc, epoch);
}

Status CatalogManager::CreateNonTransactionAwareSnapshot(
    const CreateSnapshotRequestPB* req,
    CreateSnapshotResponsePB* resp,
    RpcContext* rpc, const LeaderEpoch& epoch) {
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
  unordered_set<NamespaceId> added_namespaces;
  SysSnapshotEntryPB& pb = snapshot->mutable_metadata()->mutable_dirty()->pb;
  // Note: SysSnapshotEntryPB includes PBs for stored (1) namespaces (2) tables (3) tablets.
  RETURN_NOT_OK(AddNamespaceEntriesToPB(tables, pb.mutable_entries(), &added_namespaces));
  RETURN_NOT_OK(AddTableAndTabletEntriesToPB(
      tables, pb.mutable_entries(), pb.mutable_tablet_snapshots(), &all_tablets));

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
        epoch, TabletSnapshotOperationCallback());
    ScheduleTabletSnapshotOp(call);
  }

  resp->set_snapshot_id(snapshot_id);
  LOG(INFO) << "Successfully started snapshot " << snapshot_id << " creation";
  return Status::OK();
}

Status CatalogManager::Submit(std::unique_ptr<tablet::Operation> operation, int64_t leader_term) {
  auto tablet = VERIFY_RESULT(tablet_peer()->shared_tablet_safe());
  operation->SetTablet(tablet);
  tablet_peer()->Submit(std::move(operation), leader_term);
  return Status::OK();
}

Status CatalogManager::AddNamespaceEntriesToPB(
    const vector<TableDescription>& tables,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    unordered_set<NamespaceId>* namespaces) {
  // Add provided namespaces.
  if (!DCHECK_NOTNULL(namespaces)->empty()) {
    SharedLock lock(mutex_);
    for (const NamespaceId& ns_id : *namespaces) {
      auto ns_info = VERIFY_RESULT(FindNamespaceByIdUnlocked(ns_id));
      TRACE("Locking namespace");
      AddInfoEntryToPB(ns_info.get(), out);
    }
  }

  for (const TableDescription& table : tables) {
    // Add namespace entry.
    if (namespaces->emplace(table.namespace_info->id()).second) {
      TRACE("Locking namespace");
      AddInfoEntryToPB(table.namespace_info.get(), out);
    }
  }

  return Status::OK();
}

Status CatalogManager::AddUDTypeEntriesToPB(
    const vector<TableDescription>& tables,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    const unordered_set<NamespaceId>& namespaces) {
  // Collect all UDType entries.
  unordered_set<UDTypeId> type_ids;
  Schema schema;
  for (const TableDescription& table : tables) {
    RETURN_NOT_OK(table.table_info->GetSchema(&schema));
    for (size_t i = 0; i < schema.num_columns(); ++i) {
      for (const auto &udt_id : schema.column(i).type()->GetUserDefinedTypeIds()) {
        type_ids.insert(udt_id);
      }
    }
  }

  if (!type_ids.empty()) {
    // Add UDType entries.
    SharedLock lock(mutex_);
    for (const UDTypeId& udt_id : type_ids) {
      auto udt_info = VERIFY_RESULT(FindUDTypeByIdUnlocked(udt_id));
      TRACE("Locking user defined type");
      auto l = AddInfoEntryToPB(udt_info.get(), out);

      if (namespaces.find(udt_info->namespace_id()) == namespaces.end()) {
        return STATUS(
            NotSupported, "UDType from another keyspace is not supported",
            udt_info->namespace_id(), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::AddTableAndTabletEntriesToPB(
    const vector<TableDescription>& tables,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>* tablet_snapshot_info,
    vector<scoped_refptr<TabletInfo>>* all_tablets) {
  unordered_set<TabletId> added_tablets;
  for (const TableDescription& table : tables) {
    // Add table entry.
    TRACE("Locking table");
    AddInfoEntryToPB(table.table_info.get(), out);

    // Add tablet entries.
    for (const scoped_refptr<TabletInfo>& tablet : table.tablet_infos) {
      // For colocated tables there could be duplicate tablets, so insert them only once.
      if (added_tablets.insert(tablet->id()).second) {
        TRACE("Locking tablet");
        auto l = AddInfoEntryToPB(tablet.get(), out);

        if (tablet_snapshot_info) {
          SysSnapshotEntryPB::TabletSnapshotPB* const tablet_info = tablet_snapshot_info->Add();
          tablet_info->set_id(tablet->id());
          tablet_info->set_state(SysSnapshotEntryPB::CREATING);
        }

        if (all_tablets) {
          all_tablets->push_back(tablet);
        }
      }
    }
  }

  return Status::OK();
}

Result<SysRowEntries> CatalogManager::CollectEntries(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
    CollectFlags flags) {
  RETURN_NOT_OK(CheckIsLeaderAndReady());
  SysRowEntries entries;
  unordered_set<NamespaceId> namespaces;
  auto tables = VERIFY_RESULT(CollectTables(table_identifiers, flags, &namespaces));

  // Note: the list of entries includes: (1) namespaces (2) UD types (3) tables (4) tablets.
  RETURN_NOT_OK(AddNamespaceEntriesToPB(tables, entries.mutable_entries(), &namespaces));
  if (flags.Test(CollectFlag::kAddUDTypes)) {
    RETURN_NOT_OK(AddUDTypeEntriesToPB(tables, entries.mutable_entries(), namespaces));
  }
  // TODO(txn_snapshot) use single lock to resolve all tables to tablets
  RETURN_NOT_OK(AddTableAndTabletEntriesToPB(tables, entries.mutable_entries()));
  return entries;
}

Result<SysRowEntries> CatalogManager::CollectEntriesForSequencesDataTable() {
  auto sequence_entries_result = CollectEntries(
      CatalogManagerUtil::SequenceDataFilter(),
      CollectFlags{CollectFlag::kSucceedIfCreateInProgress});
  // If there are no sequences yet, then we won't be able to find the table.
  // It is ok and we shouldn't crash. Return an empty SysRowEntries in such a case.
  if (!sequence_entries_result.ok() && sequence_entries_result.status().IsNotFound()) {
    LOG(INFO) << "No sequences_data table created yet, so not including it in snapshot";
    return SysRowEntries();
  }
  return sequence_entries_result;
}

Result<SysRowEntries> CatalogManager::CollectEntriesForSnapshot(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) {
  SysRowEntries entries = VERIFY_RESULT(CollectEntries(
      tables,
      CollectFlags{CollectFlag::kAddIndexes, CollectFlag::kIncludeParentColocatedTable,
                   CollectFlag::kSucceedIfCreateInProgress}));
  // Include sequences_data table if the filter is on a ysql database.
  // For sequences, we have a special sequences_data (id=0000ffff00003000800000000000ffff)
  // table in the system_postgres database.
  // It is a normal YB table that has data partitioned into tablets and replicated using raft.
  // These tablets reside on the tservers. This table is created when the first
  // sequence is created. It stores one row per sequence and also needs to be restored.
  for (const auto& table : tables) {
    if (table.namespace_().database_type() == YQL_DATABASE_PGSQL) {
      auto seq_entries = VERIFY_RESULT(CollectEntriesForSequencesDataTable());
      entries.mutable_entries()->MergeFrom(seq_entries.entries());
      break;
    }
  }
  return entries;
}

server::Clock* CatalogManager::Clock() {
  return master_->clock();
}

Status CatalogManager::CreateTransactionAwareSnapshot(
    const CreateSnapshotRequestPB& req, CreateSnapshotResponsePB* resp, CoarseTimePoint deadline) {
  CollectFlags flags{CollectFlag::kIncludeParentColocatedTable};
  flags.SetIf(CollectFlag::kAddIndexes, req.add_indexes())
       .SetIf(CollectFlag::kAddUDTypes, req.add_ud_types());
  SysRowEntries entries = VERIFY_RESULT(CollectEntries(req.tables(), flags));

  auto snapshot_id = VERIFY_RESULT(snapshot_coordinator_.Create(
      entries, req.imported(), leader_ready_term(), deadline));
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
      txn_snapshot_id, req->list_deleted_snapshots(), req->detail_options(), resp));
  if (req->prepare_for_backup()) {
    RETURN_NOT_OK(RepackSnapshotsForBackup(resp));
  }

  return Status::OK();
}

Status CatalogManager::RepackSnapshotsForBackup(ListSnapshotsResponsePB* resp) {
  SharedLock lock(mutex_);
  TRACE("Acquired catalog manager lock");

  // Repack & extend the backup row entries.
  for (SnapshotInfoPB& snapshot : *resp->mutable_snapshots()) {
    snapshot.set_format_version(2);
    SysSnapshotEntryPB& sys_entry = *snapshot.mutable_entry();
    snapshot.mutable_backup_entries()->Reserve(sys_entry.entries_size());

    unordered_set<TableId> tables_to_skip;
    for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
      BackupRowEntryPB* const backup_entry = snapshot.add_backup_entries();

      // Setup BackupRowEntryPB fields.
      // Set BackupRowEntryPB::pg_schema_name for YSQL table to disambiguate in case tables
      // in different schema have same name.
      if (entry.type() == SysRowEntryType::TABLE) {
        TRACE("Looking up table");
        scoped_refptr<TableInfo> table_info = tables_->FindTableOrNull(entry.id());
        if (table_info == nullptr) {
          return STATUS(
              InvalidArgument, "Table not found by ID", entry.id(),
              MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
        }

        TRACE("Locking table");
        auto l = table_info->LockForRead();
        // PG schema name is available for YSQL table only, except for colocation parent tables.
        if (l->table_type() == PGSQL_TABLE_TYPE && !IsColocationParentTableId(entry.id())) {
          const auto res = GetPgSchemaName(table_info);
          if (!res.ok()) {
            // Check for the scenario where the table is dropped by YSQL but not docdb - this can
            // happen due to a bug with the async nature of drops in PG with docdb.
            // If this occurs don't block the entire backup, instead skip this table(see gh #13361).
            if (res.status().IsNotFound() &&
                res.status().message().ToBuffer().find(kRelnamespaceNotFoundErrorStr)
                    != string::npos) {
              LOG(WARNING) << "Skipping backup of table " << table_info->id() << " : " << res;
              snapshot.mutable_backup_entries()->RemoveLast();
              // Keep track of table so we skip its tablets as well. Note, since tablets always
              // follow their table in sys_entry, we don't need to check previous tablet entries.
              tables_to_skip.insert(table_info->id());
              continue;
            }

            // Other errors cannot be skipped.
            return res.status();
          }
          const string pg_schema_name = res.get();
          VLOG(1) << "PG Schema: " << pg_schema_name << " for table " << table_info->ToString();
          backup_entry->set_pg_schema_name(pg_schema_name);
        }
      } else if (!tables_to_skip.empty() && entry.type() == SysRowEntryType::TABLET) {
        // Note: Ordering here is important, we expect tablet entries only after their table entry.
        SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));
        if (tables_to_skip.contains(meta.table_id())) {
          LOG(WARNING) << "Skipping backup of tablet " << entry.id() << " since its table "
                       << meta.table_id() << " was skipped.";
          snapshot.mutable_backup_entries()->RemoveLast();
          continue;
        }
      }

      // Init BackupRowEntryPB::entry.
      backup_entry->mutable_entry()->Swap(&entry);
    }

    // Clear out redundant/unused fields for backups (reduces size of SnapshotInfoPB file):
    // - Can remove the tablet_snapshots as if the main snapshot state is COMPLETE, then all of the
    //   tablet records are also COMPLETE.
    // - Can remove entries, since all the valid entries are already in backup_entries.
    sys_entry.clear_tablet_snapshots();
    sys_entry.clear_entries();
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

Status CatalogManager::RestoreSnapshot(
    const RestoreSnapshotRequestPB* req, RestoreSnapshotResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing RestoreSnapshot request: " << req->ShortDebugString();

  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  if (txn_snapshot_id) {
    HybridTime ht;
    if (req->has_restore_ht()) {
      ht = HybridTime(req->restore_ht());
    }
    TxnSnapshotRestorationId id =
        VERIFY_RESULT(snapshot_coordinator_.Restore(txn_snapshot_id, ht, epoch.leader_term));
    resp->set_restoration_id(id.data(), id.size());
    return Status::OK();
  }

  return RestoreNonTransactionAwareSnapshot(req->snapshot_id(), epoch);
}

Status CatalogManager::RestoreNonTransactionAwareSnapshot(
    const string& snapshot_id, const LeaderEpoch& epoch) {
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

  // CatalogManager lock 'lock_' is still locked here.
  current_snapshot_id_ = snapshot_id;

  // Restore all entries.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    RETURN_NOT_OK(RestoreEntry(entry, snapshot_id, epoch));
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock.Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " restoring";
  return Status::OK();
}

Status CatalogManager::RestoreEntry(
    const SysRowEntry& entry, const SnapshotId& snapshot_id, const LeaderEpoch& epoch) {
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
      scoped_refptr<TableInfo> table = tables_->FindTableOrNull(entry.id());
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
            epoch, TabletSnapshotOperationCallback());
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

Status CatalogManager::AbortSnapshotRestore(
    const AbortSnapshotRestoreRequestPB* req, AbortSnapshotRestoreResponsePB* resp,
    rpc::RpcContext* rpc) {
  auto txn_restoration_id = TryFullyDecodeTxnSnapshotRestorationId(req->restoration_id());

  if (txn_restoration_id) {
    LOG(INFO) << Substitute(
        "Servicing AbortSnapshotRestore request. restoration id: $0, request: $1",
        txn_restoration_id.ToString(), req->ShortDebugString());
    return snapshot_coordinator_.AbortRestore(
        txn_restoration_id, leader_ready_term(), rpc->GetClientDeadline());
  }

  return STATUS(
      NotSupported, Format("Invalid restoration id: $0", req->restoration_id()));
}

Status CatalogManager::DeleteSnapshot(
    const DeleteSnapshotRequestPB* req,
    DeleteSnapshotResponsePB* resp,
    rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  if (txn_snapshot_id) {
    LOG(INFO) << "Servicing DeleteSnapshot request. id: " << txn_snapshot_id
              << ", request: " << req->ShortDebugString();
    return snapshot_coordinator_.Delete(
        txn_snapshot_id, epoch.leader_term, rpc->GetClientDeadline());
  }
  LOG(INFO) << "Servicing DeleteSnapshot request: " << req->ShortDebugString();
  return DeleteNonTransactionAwareSnapshot(req->snapshot_id(), epoch);
}

Status CatalogManager::DeleteNonTransactionAwareSnapshot(
    const SnapshotId& snapshot_id, const LeaderEpoch& epoch) {
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
            epoch, TabletSnapshotOperationCallback());
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

Status CatalogManager::DoImportSnapshotMeta(
      const SnapshotInfoPB& snapshot_pb,
      const LeaderEpoch& epoch,
      ImportSnapshotMetaResponsePB* resp,
      NamespaceMap* namespace_map,
      UDTypeMap* type_map,
      ExternalTableSnapshotDataMap* tables_data,
      CoarseTimePoint deadline) {
  bool successful_exit = false;

  auto se = ScopeExit([this, &namespace_map, &type_map, &tables_data, &successful_exit, &epoch] {
    if (!successful_exit) {
      DeleteNewSnapshotObjects(*namespace_map, *type_map, *tables_data, epoch);
    }
  });

  if (!snapshot_pb.has_format_version() || snapshot_pb.format_version() != 2) {
    return STATUS(
        InternalError, "Expected snapshot data in format 2", snapshot_pb.ShortDebugString(),
        MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  if (snapshot_pb.backup_entries_size() == 0) {
    return STATUS(
        InternalError, "Expected snapshot data prepared for backup", snapshot_pb.ShortDebugString(),
        MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  // PHASE 1: Recreate namespaces, create type's & table's meta data.
  RETURN_NOT_OK(ImportSnapshotPreprocess(snapshot_pb, epoch, namespace_map, type_map, tables_data));

  // PHASE 2: Recreate UD types.
  RETURN_NOT_OK(ImportSnapshotProcessUDTypes(snapshot_pb, type_map, *namespace_map));

  // PHASE 3: Recreate ONLY tables.
  RETURN_NOT_OK(ImportSnapshotCreateAndWaitForTables(
      snapshot_pb, *namespace_map, *type_map, epoch, tables_data, deadline));

  // PHASE 4: Recreate ONLY indexes.
  RETURN_NOT_OK(
      ImportSnapshotCreateIndexes(snapshot_pb, *namespace_map, *type_map, epoch, tables_data));

  // PHASE 5: Restore tablets.
  RETURN_NOT_OK(ImportSnapshotProcessTablets(snapshot_pb, tables_data));

  if (PREDICT_FALSE(FLAGS_TEST_import_snapshot_failed)) {
    const string msg = "ImportSnapshotMeta interrupted due to test flag";
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  successful_exit = true;
  // Copy the table mapping into the response.
  for (auto& [_, table_data] : *tables_data) {
    if (table_data.table_meta) {
      resp->mutable_tables_meta()->Add()->Swap(&*table_data.table_meta);
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotPreprocess(
    const SnapshotInfoPB& snapshot_pb,
    const LeaderEpoch& epoch,
    NamespaceMap* namespace_map,
    UDTypeMap* type_map,
    ExternalTableSnapshotDataMap* tables_data) {
  // First pass: preprocess namespaces and UDTs
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    switch (entry.type()) {
      case SysRowEntryType::NAMESPACE: // Recreate NAMESPACE.
        RETURN_NOT_OK(ImportNamespaceEntry(entry, epoch, namespace_map));
        break;
      case SysRowEntryType::UDTYPE: // Create TYPE metadata.
        LOG_IF(DFATAL, entry.id().empty()) << "Empty entry id";

        if (type_map->find(entry.id()) != type_map->end()) {
          LOG_WITH_FUNC(WARNING) << "Ignoring duplicate type with id " << entry.id();
        } else {
          ExternalUDTypeSnapshotData& data = (*type_map)[entry.id()];
          data.type_entry_pb = VERIFY_RESULT(ParseFromSlice<SysUDTypeEntryPB>(entry.data()));
          // The value 'new_type_id' will be filled in ImportUDTypeEntry()
          // when the UDT will be found or recreated. Now it's empty value.
        }
        break;
      case SysRowEntryType::TABLE: // Create TABLE metadata.
      case SysRowEntryType::TABLET: // Preprocess original tablets.
        break;
      case SysRowEntryType::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SYS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::CDC_STREAM: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNIVERSE_REPLICATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT:  FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_SCHEDULE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::DDL_LOG_ENTRY: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_RESTORATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::XCLUSTER_SAFE_TIME: FALLTHROUGH_INTENDED;
      case SysRowEntryType::XCLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNIVERSE_REPLICATION_BOOTSTRAP: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNKNOWN:
        FATAL_INVALID_ENUM_VALUE(SysRowEntryType, entry.type());
    }
  }

  // Second pass: preprocess tables and tablets
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    switch (entry.type()) {
      case SysRowEntryType::TABLE: { // Create TABLE metadata.
          LOG_IF(DFATAL, entry.id().empty()) << "Empty entry id";
          ExternalTableSnapshotData& data = (*tables_data)[entry.id()];

          if (data.old_table_id.empty()) {
            data.old_table_id = entry.id();
            data.table_entry_pb = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));
            if (backup_entry.has_pg_schema_name()) {
              data.pg_schema_name = backup_entry.pg_schema_name();
            }
            if (data.table_entry_pb.colocated() && IsColocatedDbParentTableId(data.old_table_id)) {
              // Find the new namespace id of the namespace of the table.
              auto ns_it = namespace_map->find(data.table_entry_pb.namespace_id());
              if (ns_it == namespace_map->end()) {
                const string msg = Format("Namespace not found: $0",
                                          data.table_entry_pb.namespace_id());
                LOG_WITH_FUNC(WARNING) << msg;
                return STATUS(NotFound, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
              }
              const NamespaceId new_namespace_id = ns_it->second.new_namespace_id;
              bool legacy_colocated_database;
              {
                SharedLock lock(mutex_);
                legacy_colocated_database = (colocated_db_tablets_map_.find(new_namespace_id)
                                             != colocated_db_tablets_map_.end());
              }
              if (!legacy_colocated_database) {
                // Colocation migration.
                // Check if the default tablegroup exists.
                PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(new_namespace_id));
                PgOid default_tablegroup_oid =
                    VERIFY_RESULT(sys_catalog_->ReadPgYbTablegroupOid(database_oid,
                                                                      kDefaultTablegroupName));
                if (default_tablegroup_oid == kPgInvalidOid) {
                  // The default tablegroup doesn't exist in the restoring database. This means
                  // there are no colocated tables in both of the backup and restoring database
                  // because the default tablegroup is lazily created along with the creation of the
                  // first colocated table. However, legacy colocated database parent table is
                  // created along with the creation of the database. This is a special case to
                  // handle. Just skip importing the legacy colocated database parent table by
                  // removing this legacy colocated parent table's corresponding entry from
                  // ExternalTableSnapshotDataMap tables_data. Also, we don't add its relevant
                  // entry: TableMetaPB in ImportSnapshotMetaResponsePB resp. Since its entry in
                  // ExternalTableSnapshotDataMap is removed, We will skip processing this parent
                  // table and parent table tablet in [PHASE 3: Recreate ONLY tables] and
                  // [PHASE 5: Restore tablets] in ImportSnapshotMeta(), respectively.
                  tables_data->erase(entry.id());
                  continue;
                }
              }
            }
            data.table_meta = ImportSnapshotMetaResponsePB::TableMetaPB();
          } else {
            LOG_WITH_FUNC(WARNING) << "Ignoring duplicate table with id " << entry.id();
          }

          LOG_IF(DFATAL, data.old_table_id.empty()) << "Not initialized table id";
        }
        break;
      case SysRowEntryType::TABLET: // Preprocess original tablets.
        RETURN_NOT_OK(PreprocessTabletEntry(entry, tables_data));
        break;
      default:
        break;
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotProcessUDTypes(const SnapshotInfoPB& snapshot_pb,
                                                    UDTypeMap* type_map,
                                                    const NamespaceMap& namespace_map) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::UDTYPE) {
      // Create UD type.
      RETURN_NOT_OK(ImportUDTypeEntry(entry.id(), type_map, namespace_map));
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotCreateIndexes(const SnapshotInfoPB& snapshot_pb,
                                                   const NamespaceMap& namespace_map,
                                                   const UDTypeMap& type_map,
                                                   const LeaderEpoch& epoch,
                                                   ExternalTableSnapshotDataMap* tables_data) {
  // Create ONLY INDEXES.
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::TABLE
        && tables_data->find(entry.id()) != tables_data->end()) {
      ExternalTableSnapshotData& data = (*tables_data)[entry.id()];
      if (data.is_index()) {
        // YSQL indices can be in an invalid state. In this state they are omitted by ysql_dump.
        // Assume this is an invalid index that wasn't part of the ysql_dump instead of failing the
        // import here.
        auto s = ImportTableEntry(namespace_map, type_map, *tables_data, epoch, &data);
        if (s.IsInvalidArgument() && MasterError(s) == MasterErrorPB::OBJECT_NOT_FOUND) {
          continue;
        } else if (!s.ok()) {
          return s;
        }
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotCreateAndWaitForTables(
    const SnapshotInfoPB& snapshot_pb, const NamespaceMap& namespace_map,
    const UDTypeMap& type_map, const LeaderEpoch& epoch, ExternalTableSnapshotDataMap* tables_data,
    CoarseTimePoint deadline) {
  std::queue<TableId> pending_creates;
  for (const auto& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    // Only for tables that are not indexes.
    if (entry.type() != SysRowEntryType::TABLE) {
      continue;
    }
    if (tables_data->find(entry.id()) == tables_data->end()) {
      continue;
    }
    // ExternalTableSnapshotData only contains entries for tables, so
    // we access it after the entry type check above.
    ExternalTableSnapshotData& data = (*tables_data)[entry.id()];
    if (data.is_index()) {
      continue;
    }
    // If we are at the limit, wait for the oldest table to be created
    // so that we can send create request for the current table.
    DCHECK_LE(pending_creates.size(), FLAGS_import_snapshot_max_concurrent_create_table_requests);
    while (pending_creates.size() >= FLAGS_import_snapshot_max_concurrent_create_table_requests) {
      RETURN_NOT_OK(WaitForCreateTableToFinish(pending_creates.front(), deadline));
      LOG(INFO) << "ImportSnapshot: Create table finished for " << pending_creates.front()
                << ", time remaining " << ToSeconds(deadline - CoarseMonoClock::Now()) << " secs";
      pending_creates.pop();
    }
    // Ready to send request for this table now.
    RETURN_NOT_OK(ImportTableEntry(namespace_map, type_map, *tables_data, epoch, &data));
    pending_creates.push(data.new_table_id);
  }

  // Pop from queue and wait for those tables to be created.
  while (!pending_creates.empty()) {
    RETURN_NOT_OK(WaitForCreateTableToFinish(pending_creates.front(), deadline));
    LOG(INFO) << "ImportSnapshot: Create table finished for " << pending_creates.front()
              << ", time remaining " << ToSeconds(deadline - CoarseMonoClock::Now()) << " secs";
    pending_creates.pop();
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotProcessTablets(const SnapshotInfoPB& snapshot_pb,
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

void CatalogManager::DeleteNewUDtype(const UDTypeId& udt_id,
                                     const unordered_set<UDTypeId>& type_ids_to_delete) {
  auto res_udt = FindUDTypeById(udt_id);
  if (!res_udt.ok()) {
    return; // Already deleted.
  }

  auto type_info = *res_udt;
  LOG_WITH_FUNC(INFO) << "Deleting new UD type '" << type_info->name() << "' with id=" << udt_id;

  // Try to delete sub-types.
  unordered_set<UDTypeId> sub_type_ids;
  for (int i = 0; i < type_info->field_types_size(); ++i) {
    const Status s = IterateAndDoForUDT(
        type_info->field_types(i),
        [&sub_type_ids](const QLTypePB::UDTypeInfo& udtype_info) -> Status {
          sub_type_ids.insert(udtype_info.id());
          return Status::OK();
        });

    if (!s.ok()) {
      LOG_WITH_FUNC(WARNING) << "Failed IterateAndDoForUDT for type " << udt_id << ": " << s;
    }
  }

  DeleteUDTypeRequestPB req;
  DeleteUDTypeResponsePB resp;
  req.mutable_type()->mutable_namespace_()->set_id(type_info->namespace_id());
  req.mutable_type()->set_type_id(udt_id);
  ProcessDeleteObjectStatus("ud-type", udt_id, resp, DeleteUDType(&req, &resp, nullptr));

  for (const UDTypeId& sub_udt_id : sub_type_ids) {
    // Delete only NEW re-created types. Keep old ones.
    if (type_ids_to_delete.find(sub_udt_id) != type_ids_to_delete.end()) {
      DeleteNewUDtype(sub_udt_id, type_ids_to_delete);
    }
  }
}

void CatalogManager::DeleteNewSnapshotObjects(const NamespaceMap& namespace_map,
                                              const UDTypeMap& type_map,
                                              const ExternalTableSnapshotDataMap& tables_data,
                                              const LeaderEpoch& epoch) {
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
    ProcessDeleteObjectStatus("table", new_id, resp, DeleteTable(&req, &resp, nullptr, epoch));
  }

  unordered_set<UDTypeId> type_ids_to_delete;
  for (const UDTypeMap::value_type& entry : type_map) {
    const UDTypeId& old_id = entry.first;
    const UDTypeId& new_id = entry.second.new_type_id;
    const bool existing = !entry.second.just_created;

    if (existing || new_id.empty() || new_id == old_id) {
      continue;
    }

    type_ids_to_delete.insert(new_id);
  }

  for (auto type_id : type_ids_to_delete) {
    // The UD types are creating a tree. Order in the set collection of ids is random.
    // Recursively delete sub-types together with this type to simplify the code.
    //
    // Example: udt2 --uses--> udt1
    //     DROP udt1 - failed (referenced by udt2)
    //     DROP udt2 - success - drop subtypes:
    //         DROP udt1 - success
    DeleteNewUDtype(type_id, type_ids_to_delete);
  }

  for (const NamespaceMap::value_type& entry : namespace_map) {
    const NamespaceId& old_id = entry.first;
    const NamespaceId& new_id = entry.second.new_namespace_id;
    const YQLDatabase& db_type = entry.second.db_type;
    const bool existing = !entry.second.just_created;

    // Do not delete YSQL objects - it must be deleted via PG API.
    if (existing || new_id.empty() || new_id == old_id || db_type == YQL_DATABASE_PGSQL) {
      continue;
    }

    LOG_WITH_FUNC(INFO) << "Deleting new namespace with id=" << new_id << " old id=" << old_id;
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_id(new_id);
    ProcessDeleteObjectStatus(
        "namespace", new_id, resp, DeleteNamespace(&req, &resp, nullptr, epoch));
  }
}

Status CatalogManager::ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                          ImportSnapshotMetaResponsePB* resp,
                                          rpc::RpcContext* rpc,
                                          const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing ImportSnapshotMeta request: " << req->ShortDebugString();
  NamespaceMap namespace_map;
  UDTypeMap type_map;
  ExternalTableSnapshotDataMap tables_data;

  RETURN_NOT_OK(DoImportSnapshotMeta(
      req->snapshot(), epoch, resp, &namespace_map, &type_map, &tables_data,
      rpc->GetClientDeadline()));

  return Status::OK();
}

Status CatalogManager::GetFullUniverseKeyRegistry(const GetFullUniverseKeyRegistryRequestPB* req,
                                                  GetFullUniverseKeyRegistryResponsePB* resp) {
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForRead();
  if (!l.data().pb.has_encryption_info()) {
    return Status::OK();
  }
  auto encryption_info = l.data().pb.encryption_info();

  return encryption_manager_->GetFullUniverseKeyRegistry(encryption_info, resp);
}

Status CatalogManager::ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                            ChangeEncryptionInfoResponsePB* resp) {
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto encryption_info = l.mutable_data()->pb.mutable_encryption_info();

  RETURN_NOT_OK(encryption_manager_->ChangeEncryptionInfo(req, encryption_info));

  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
      "updating cluster config in sys-catalog"));
  l.Commit();

  std::lock_guard lock(should_send_universe_key_registry_mutex_);
  for (auto& entry : should_send_universe_key_registry_) {
    entry.second = true;
  }

  return Status::OK();
}

Status CatalogManager::IsEncryptionEnabled(const IsEncryptionEnabledRequestPB* req,
                                           IsEncryptionEnabledResponsePB* resp) {
  return encryption_manager_->IsEncryptionEnabled(
      ClusterConfig()->LockForRead()->pb.encryption_info(), resp);
}

Status CatalogManager::ImportNamespaceEntry(const SysRowEntry& entry,
                                            const LeaderEpoch& epoch,
                                            NamespaceMap* namespace_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::NAMESPACE)
      << "Unexpected entry type: " << entry.type();

  SysNamespaceEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));
  ExternalNamespaceSnapshotData& ns_data = (*namespace_map)[entry.id()];
  ns_data.db_type = GetDatabaseType(meta);

  TRACE("Looking up namespace");
  // First of all try to find the namespace by ID. It will work if we are restoring the backup
  // on the original cluster where the backup was created.
  scoped_refptr<NamespaceInfo> ns;
  {
    SharedLock lock(mutex_);
    ns = FindPtrOrNull(namespace_ids_map_, entry.id());
  }

  if (ns != nullptr && ns->name() == meta.name() && ns->state() == SysNamespaceEntryPB::RUNNING) {
    ns_data.new_namespace_id = entry.id();
    return Status::OK();
  }

  // If the namespace was not found by ID, it's ok on a new cluster OR if the namespace was
  // deleted and created again. In both cases the namespace can be found by NAME.
  if (ns_data.db_type == YQL_DATABASE_PGSQL) {
    // YSQL database must be created via external call. Find it by name.
    {
      SharedLock lock(mutex_);
      ns = FindPtrOrNull(namespace_names_mapper_[ns_data.db_type], meta.name());
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
    ns_data.new_namespace_id = ns->id();
  } else {
    CreateNamespaceRequestPB req;
    CreateNamespaceResponsePB resp;
    req.set_name(meta.name());
    const Status s = CreateNamespace(&req, &resp, nullptr, epoch);

    if (s.ok()) {
      // The namespace was successfully re-created.
      ns_data.just_created = true;
    } else if (s.IsAlreadyPresent()) {
      LOG_WITH_FUNC(INFO) << "Using existing namespace '" << meta.name() << "': " << resp.id();
    } else {
      return s.CloneAndAppend("Failed to create namespace");
    }

    ns_data.new_namespace_id = resp.id();
  }
  return Status::OK();
}

Status CatalogManager::UpdateUDTypes(QLTypePB* pb_type, const UDTypeMap& type_map) {
  return IterateAndDoForUDT(
      pb_type,
      [&type_map](QLTypePB::UDTypeInfo* udtype_info) -> Status {
        const UDTypeId& old_udt_id = udtype_info->id();
        auto udt_it = type_map.find(old_udt_id);
        if (udt_it == type_map.end()) {
          const string msg = Format("Not found referenced type id $0", old_udt_id);
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }

        const UDTypeId& new_udt_id = udt_it->second.new_type_id;
        if (new_udt_id.empty()) {
          const string msg = Format("Unknown new id for UD type $0 old id $1",
              udtype_info->name(), old_udt_id);
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }

        if (old_udt_id != new_udt_id) {
          LOG(INFO) << "Replacing UD type '" << udtype_info->name()
                    << "' id from " << old_udt_id << " to " << new_udt_id;
          udtype_info->set_id(new_udt_id);
        }
        return Status::OK();
      });
}

Status CatalogManager::ImportUDTypeEntry(const UDTypeId& udt_id,
                                         UDTypeMap* type_map,
                                         const NamespaceMap& namespace_map) {
  auto udt_it = DCHECK_NOTNULL(type_map)->find(udt_id);
  if (udt_it == type_map->end()) {
    const string msg = Format("Not found metadata for referenced type id $0", udt_id);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  ExternalUDTypeSnapshotData& udt_data = udt_it->second;

  // If the type has been already processed: found or re-created.
  if (!udt_data.new_type_id.empty()) {
    return Status::OK();
  }

  SysUDTypeEntryPB& meta = udt_data.type_entry_pb;

  // First of all find and check referenced namespace.
  auto ns_it = namespace_map.find(meta.namespace_id());
  if (ns_it == namespace_map.end()) {
    const string msg = Format("Unknown keyspace $0 referenced in UD type $1 id $2",
        meta.namespace_id(), meta.name(), udt_id);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
  }

  const ExternalNamespaceSnapshotData& ns_data = ns_it->second;
  if (ns_data.db_type != YQL_DATABASE_CQL) {
    const string msg = Format(
        "UD type $0 id $1 references non CQL namespace: $2 type $3 (old id $4)",
        meta.name(), udt_id, ns_data.new_namespace_id, ns_data.db_type, meta.namespace_id());
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  if (meta.field_names_size() != meta.field_types_size()) {
    const string msg = Format(
        "UD type $0 id $1 has $2 names and $3 types",
        meta.name(), udt_id, meta.field_names_size(), meta.field_types_size());
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  // There are 3 cases:
  // Case 1: Find UDT by ID.
  //         Restoring the backup on the original cluster where the backup was created.
  // Case 2: Find UDT by name in the needed keyspace.
  //         Restoring the backup on the new cluster, but UDT was already created
  //         by the user or in the previous backup restoration.
  // Case 3: Re-create the UDT.
  //         Restoring the backup on the new empty cluster.

  // Case 1: try to find the type by ID.
  scoped_refptr<UDTypeInfo> udt;
  Result<scoped_refptr<UDTypeInfo>> res_udt = FindUDTypeById(udt_id);
  if (res_udt.ok() && (*res_udt)->name() == meta.name() &&
      (*res_udt)->namespace_id() == ns_data.new_namespace_id) {
    // Use found by ID UD type.
    udt_data.new_type_id = udt_id;
    LOG_WITH_FUNC(INFO) << "Using found by id UD type '" << meta.name() << "' in namespace "
                        << ns_data.new_namespace_id << ": " << udt_data.new_type_id;
    udt = *res_udt;
  } else {
    // Case 2 & 3: Try to create the new UD type.

    // Recursively create all referenced sub-types.
    unordered_set<UDTypeId> sub_type_ids;
    for (int i = 0; i < meta.field_types_size(); ++i) {
      RETURN_NOT_OK(
        IterateAndDoForUDT(
          meta.field_types(i),
          [&sub_type_ids](const QLTypePB::UDTypeInfo& udtype_info) -> Status {
            sub_type_ids.insert(udtype_info.id());
            return Status::OK();
          }));
    }

    for (const UDTypeId& sub_udt_id : sub_type_ids) {
      RETURN_NOT_OK(ImportUDTypeEntry(sub_udt_id, type_map, namespace_map));
    }

    // If the type was not found by ID, it's ok on a new cluster OR if the type was
    // deleted and created again. In both cases the type can be found by NAME.
    // By the moment all referenced sub-types must be available (already existing or re-created).
    CreateUDTypeRequestPB req;
    CreateUDTypeResponsePB resp;
    req.mutable_namespace_()->set_id(ns_data.new_namespace_id);
    req.mutable_namespace_()->set_database_type(ns_data.db_type);
    req.set_name(meta.name());
    for (int i = 0; i < meta.field_names_size(); ++i) {
      req.add_field_names(meta.field_names(i));

      QLTypePB* const param = meta.mutable_field_types(i);
      RETURN_NOT_OK(UpdateUDTypes(param, *type_map));
      req.add_field_types()->CopyFrom(*param);
    }

    const Status s = CreateUDType(&req, &resp, nullptr);

    if (s.ok()) {
      // Case 3: UDT was successfully re-created.
      udt_data.just_created = true;
    } else if (s.IsAlreadyPresent()) {
      // Case 2: UDT is found by name.
      LOG_WITH_FUNC(INFO) << "Using existing UD type '" << meta.name() << "': " << resp.id();
    } else {
      return s.CloneAndAppend("Failed to create UD type");
    }

    udt_data.new_type_id = resp.id();
    udt = VERIFY_RESULT(FindUDTypeById(udt_data.new_type_id));
  }

  // Check UDT field names & types.
  // Checking for all cases: found by ID, found by name (AlreadyPresent), re-created.
  bool correct = udt->field_names_size() == meta.field_names_size() &&
                 udt->field_types_size() == meta.field_types_size();
  if (correct) {
    for (int i = 0; i < udt->field_names_size(); ++i) {
      shared_ptr<QLType> found_type = QLType::FromQLTypePB(udt->field_types(i));
      shared_ptr<QLType> src_type = QLType::FromQLTypePB(meta.field_types(i));
      if (udt->field_names(i) != meta.field_names(i) || *found_type != *src_type) {
        correct = false;
        break;
      }
    }
  }

  if (!correct) {
    const string msg = Format(
        "UD type $0 id $1 was changed: {$2} expected {$3}",
        meta.name(), udt_data.new_type_id, udt->ToString(), meta.ShortDebugString());
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  return Status::OK();
}

Status CatalogManager::RecreateTable(const NamespaceId& new_namespace_id,
                                     const UDTypeMap& type_map,
                                     const ExternalTableSnapshotDataMap& table_map,
                                     const LeaderEpoch& epoch,
                                     ExternalTableSnapshotData* table_data) {
  const SysTablesEntryPB& meta = DCHECK_NOTNULL(table_data)->table_entry_pb;

  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(meta.name());
  req.set_table_type(meta.table_type());
  req.set_num_tablets(narrow_cast<int32_t>(table_data->num_tablets));
  for (const auto& p : table_data->partitions) {
    *req.add_partitions() = p;
  }
  req.mutable_namespace_()->set_id(new_namespace_id);
  *req.mutable_partition_schema() = meta.partition_schema();
  *req.mutable_replication_info() = meta.replication_info();

  SchemaPB* const schema = req.mutable_schema();
  *schema = meta.schema();
  // Recursively update ids in used user-defined types.
  for (int i = 0; i < schema->columns_size(); ++i) {
    QLTypePB* const pb_type = schema->mutable_columns(i)->mutable_type();
    RETURN_NOT_OK(UpdateUDTypes(pb_type, type_map));
  }

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
      indexed_table = tables_->FindTableOrNull(req.indexed_table_id());
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

  req.set_is_matview(meta.is_matview());

  if (meta.has_matview_pg_table_id()) {
    req.set_matview_pg_table_id(meta.matview_pg_table_id());
  }

  RETURN_NOT_OK(CreateTable(&req, &resp, /* RpcContext */nullptr, epoch));
  table_data->new_table_id = resp.table_id();
  LOG_WITH_FUNC(INFO) << "New table id " << table_data->new_table_id << " for "
                      << table_data->old_table_id;
  return Status::OK();
}

Status CatalogManager::RepartitionTable(const scoped_refptr<TableInfo> table,
                                        const ExternalTableSnapshotData* table_data,
                                        const LeaderEpoch& epoch) {
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
  vector<dockv::Partition> partitions(table_data->partitions.size());
  for (const auto& partition_pb : table_data->partitions) {
    dockv::Partition::FromPB(partition_pb, &partitions[i++]);
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
          return STATUS_FORMAT(
              IllegalState,
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
    auto& table_pb = table_lock.mutable_data()->pb;
    table_pb.mutable_partition_schema()->CopyFrom(
        table_data->table_entry_pb.partition_schema());
    table_pb.set_partition_list_version(table_pb.partition_list_version() + 1);

    // Remove old tablets from TableInfo.
    table->RemoveTablets(old_tablets);
    // Add new tablets to TableInfo. This must be done after removing tablets because
    // TableInfo::partitions_ has key PartitionKey, which old and new tablets may conflict on.
    table->AddTablets(new_tablets);
    // Since we have added a new set of tablets move the table back to a PREPARING state. It will
    // get marked to RUNNING once all the new tablets have been created.
    table_pb.set_state(SysTablesEntryPB::PREPARING);

    // Commit table and tablets to disk.
    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table, new_tablets, old_tablets));
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
    DeleteTabletReplicas(old_tablet.get(), deletion_msg, HideOnly::kFalse, KeepData::kFalse, epoch);
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
                                        const UDTypeMap& type_map,
                                        const ExternalTableSnapshotDataMap& table_map,
                                        const LeaderEpoch& epoch,
                                        ExternalTableSnapshotData* table_data) {
  const SysTablesEntryPB& meta = DCHECK_NOTNULL(table_data)->table_entry_pb;
  bool is_parent_colocated_table = false;

  table_data->old_namespace_id = meta.namespace_id();
  LOG_IF(DFATAL, table_data->old_namespace_id.empty()) << "No namespace id";

  auto ns_it = namespace_map.find(table_data->old_namespace_id);
  LOG_IF(DFATAL, ns_it == namespace_map.end())
      << "Namespace not found: " << table_data->old_namespace_id;
  const NamespaceId new_namespace_id = ns_it->second.new_namespace_id;
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
      table = tables_->FindTableOrNull(table_data->old_table_id);
    }

    if (table != nullptr) {
      VLOG_WITH_PREFIX(3) << "Begin first search";
      // At this point, namespace id and table id match. Check other properties, like whether the
      // table is active and whether table name matches.
      SharedLock lock(mutex_);
      if (VERIFY_RESULT(CheckTableForImport(table, table_data))) {
        LOG_WITH_FUNC(INFO) << "Found existing table: '" << table->ToString() << "'";
        if (meta.colocated() && IsColocationParentTableId(table_data->old_table_id)) {
          // Parent colocated tables don't have partition info, so make sure to mark them.
          is_parent_colocated_table = true;
        }
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
        RETURN_NOT_OK(RecreateTable(new_namespace_id, type_map, table_map, epoch, table_data));
        break;
      }
      case TableType::PGSQL_TABLE_TYPE: {
        // For YSQL, the table must be created via external call. Therefore, continue the search for
        // the table, this time checking for name matches rather than id matches.

        if (meta.colocated() && IsColocatedDbParentTableId(table_data->old_table_id)) {
          // For the parent colocated table we need to generate the new_table_id ourselves
          // since the names will not match.
          // For normal colocated tables, we are still able to follow the normal table flow, so no
          // need to generate the new_table_id ourselves.
          // Case 1: Legacy colocated database
          // parent table id = <namespace_id>.colocated.parent.uuid
          // Case 2: Migration to colocation database
          // parent table id = <tablegroup_id>.colocation.parent.uuid
          SharedLock lock(mutex_);
          bool legacy_colocated_database =
              colocated_db_tablets_map_.find(new_namespace_id) != colocated_db_tablets_map_.end();
          if (legacy_colocated_database) {
            table_data->new_table_id = GetColocatedDbParentTableId(new_namespace_id);
          } else {
            PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(new_namespace_id));
            PgOid default_tablegroup_oid =
                VERIFY_RESULT(sys_catalog_->ReadPgYbTablegroupOid(database_oid,
                                                                  kDefaultTablegroupName));
            if (default_tablegroup_oid == kPgInvalidOid) {
              // The default tablegroup doesn't exist in the restoring colocated database.
              // This is a special case of colocation migration where only non-colocated tables
              // exist in the database.
              // We should already handle this case in ImportSnapshotPreprocess, such that we don't
              // need to deal with it during the whole import snapshot process.
              // If we get here, there must be something wrong and we should throw an error status.
              // See ImportSnapshotPreprocess for more details.
              const string msg = Format("Unexpected legacy colocated parent table during colocation"
                                        " migration. We should skip processing it.");
              LOG_WITH_FUNC(WARNING) << msg;
              return STATUS(InternalError, msg, MasterError(MasterErrorPB::INTERNAL_ERROR));
            } else {
              table_data->new_table_id =
                  GetColocationParentTableId(GetPgsqlTablegroupId(database_oid,
                                                                  default_tablegroup_oid));
            }
          }
          is_parent_colocated_table = true;
        } else if (meta.colocated() && IsTablegroupParentTableId(table_data->old_table_id)) {
          // Since we preserve tablegroup oid in ysql_dump, for the parent tablegroup table, if we
          // didn't find a match by id in the previous step, then we need to generate the
          // new_table_id ourselves because the namespace id of the namespace where this tablegroup
          // was created changes.
          PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(new_namespace_id));
          PgOid tablegroup_oid =
              VERIFY_RESULT(GetPgsqlTablegroupOid(
                  GetTablegroupIdFromParentTableId(table_data->old_table_id)));
          if (IsColocatedDbTablegroupParentTableId(table_data->old_table_id)) {
            // This tablegroup parent table is in a colocated database, and has string 'colocation'
            // in its id.
            table_data->new_table_id =
                GetColocationParentTableId(GetPgsqlTablegroupId(database_oid, tablegroup_oid));
          } else {
            table_data->new_table_id =
                GetTablegroupParentTableId(GetPgsqlTablegroupId(database_oid, tablegroup_oid));
          }
          is_parent_colocated_table = true;
        } else {
          if (!table_data->new_table_id.empty()) {
            const string msg = Format(
                "$0 expected empty new table id but $1 found", __func__, table_data->new_table_id);
            LOG_WITH_FUNC(WARNING) << msg;
            return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
          }
          SharedLock lock(mutex_);

          for (const auto& table : tables_->GetAllTables()) {

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
              LOG_WITH_FUNC(INFO) << "Found existing table " << table->id() << " for "
                                  << new_namespace_id << "/" << meta.name() << " (old table "
                                  << table_data->old_table_id << ") with schema "
                                  << table_data->pg_schema_name;
              table_data->new_table_id = table->id();
            } else if (table_data->new_table_id != table->id()) {
              const string msg = Format(
                  "Found 2 YSQL tables with the same name: $0 - $1, $2",
                  meta.name(), table_data->new_table_id, table->id());
              LOG_WITH_FUNC(WARNING) << msg;
              return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
            }
          }

          if (table_data->new_table_id.empty()) {
            const string msg = Format("YSQL table not found: $0", meta.name());
            LOG_WITH_FUNC(WARNING) << msg;
            table_data->table_meta = std::nullopt;
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
    table = tables_->FindTableOrNull(table_data->new_table_id);
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
    auto comparator = [](const ColumnSchema& a, const ColumnSchema& b) {
      return ColumnSchema::CompKind(a, b) &&
             ColumnSchema::CompTypeInfo(a, b) &&
             ColumnSchema::CompName(a, b);
    };
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
          RETURN_NOT_OK(RepartitionTable(table, table_data, epoch));
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

    // Table schema update depending on different conditions.
    bool notify_ts_for_schema_change = false;

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
      RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
      l.Commit();
      notify_ts_for_schema_change = true;
    }

    // Restore partition key version.
    if (persisted_schema.table_properties().partitioning_version() !=
        schema.table_properties().partitioning_version()) {
      auto l = table->LockForWrite();
      auto table_props = l.mutable_data()->pb.mutable_schema()->mutable_table_properties();
      table_props->set_partitioning_version(schema.table_properties().partitioning_version());

      l.mutable_data()->pb.set_next_column_id(schema.max_col_id() + 1);
      l.mutable_data()->pb.set_version(l->pb.version() + 1);
      // Update sys-catalog with the new table schema.
      RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
      l.Commit();
      notify_ts_for_schema_change = true;
    }

    // Bump up the schema version to the version of the snapshot if it is less.
    if (meta.version() > table->LockForRead()->pb.version()) {
      auto l = table->LockForWrite();
      l.mutable_data()->pb.set_version(meta.version());
      RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
      l.Commit();
      notify_ts_for_schema_change = true;
    }

    // Update the new table schema in tablets.
    if (notify_ts_for_schema_change) {
      RETURN_NOT_OK(SendAlterTableRequest(table, epoch));
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

  // Recursively collect ids for used user-defined types.
  unordered_set<UDTypeId> type_ids;
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    for (const auto &udt_id : schema.column(i).type()->GetUserDefinedTypeIds()) {
      type_ids.insert(udt_id);
    }
  }

  for (const UDTypeId& udt_id : type_ids) {
    auto type_it = type_map.find(udt_id);
    if (type_it == type_map.end()) {
        return STATUS(InternalError, "UDType was not imported",
            udt_id, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
    }

    IdPairPB* const udt_ids = table_data->table_meta->add_ud_types_ids();
    udt_ids->set_new_id(type_it->second.new_type_id);
    udt_ids->set_old_id(udt_id);
  }

  return Status::OK();
}

Status CatalogManager::PreprocessTabletEntry(const SysRowEntry& entry,
                                             ExternalTableSnapshotDataMap* table_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::TABLET)
      << "Unexpected entry type: " << entry.type();

  SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));

  // Colocation Migration special case. See ImportSnapshotPreprocess for more details.
  if (meta.colocated() && IsColocatedDbParentTableId(meta.table_id())
      && table_map->find(meta.table_id()) == table_map->end()) {
    return Status::OK();
  }

  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];
  if (meta.colocated()) {
    table_data.num_tablets = 1;
  } else {
    ++table_data.num_tablets;
  }
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

  // This is the special case of colocation migration.
  // See ImportSnapshotPreprocess for more details.
  if (meta.colocated() && IsColocatedDbParentTableId(meta.table_id())
      && table_map->find(meta.table_id()) == table_map->end()) {
    return Status::OK();
  }

  LOG_IF(DFATAL, table_map->find(meta.table_id()) == table_map->end())
      << "Table not found: " << meta.table_id();
  if (table_map->find(meta.table_id()) == table_map->end()) {
    return STATUS_FORMAT(
        InvalidArgument, "Cannot find table with id $0 hosted on tablet $1", meta.table_id(),
        entry.id());
  }
  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];
  if (!table_data.table_meta) {
    if (table_data.is_index()) {
      // The metadata for this index was not initialized in ImportTableEntry. We assume it was
      // missing from the ysql_dump and is an invalid YSQL index. Ignore.
      return Status::OK();
    }
    auto msg = Format("Missing metadata for table corresponding to snapshot table $0.$1, id $2",
        table_data.table_entry_pb.namespace_name(), table_data.table_entry_pb.name(),
        table_data.old_table_id);
    DCHECK(false) << msg;
    return STATUS(IllegalState, msg);
  }

  if (meta.colocated() && table_data.table_meta->tablets_ids_size() >= 1) {
    LOG_WITH_FUNC(INFO) << "Already processed this colocated tablet: " << entry.id();
    return Status::OK();
  }

  // Update tablets IDs map.
  if (table_data.new_table_id == table_data.old_table_id) {
    TRACE("Looking up tablet");
    SharedLock lock(mutex_);
    scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());

    if (tablet != nullptr) {
      IdPairPB* const pair = table_data.table_meta->add_tablets_ids();
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

  IdPairPB* const pair = table_data.table_meta->add_tablets_ids();
  pair->set_old_id(entry.id());
  pair->set_new_id(it->second);
  return Status::OK();
}

const Schema& CatalogManager::schema() {
  return sys_catalog()->schema();
}

const docdb::DocReadContext& CatalogManager::doc_read_context() {
  return sys_catalog()->doc_read_context();
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

Result<SchemaVersion> CatalogManager::GetTableSchemaVersion(const TableId& table_id) {
  auto table = VERIFY_RESULT(FindTableById(table_id));
  auto lock = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(lock));
  return lock->pb.version();
}

Result<std::map<std::string, KeyRange>> CatalogManager::GetTableKeyRanges(const TableId& table_id) {
  auto table = VERIFY_RESULT(FindTableById(table_id));
  auto lock = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(lock));

  auto tablets = table->GetTablets();

  std::map<std::string, KeyRange> result;
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto tablet_lock = tablet->LockForRead();
    const auto& partition = tablet_lock->pb.partition();
    result[tablet->tablet_id()].start_key = partition.partition_key_start();
    result[tablet->tablet_id()].end_key = partition.partition_key_end();
  }

  return result;
}

AsyncTabletSnapshotOpPtr CatalogManager::CreateAsyncTabletSnapshotOp(
    const TabletInfoPtr& tablet, const std::string& snapshot_id,
    tserver::TabletSnapshotOpRequestPB::Operation operation,
    const LeaderEpoch& epoch, TabletSnapshotOperationCallback callback) {
  auto result = std::make_shared<AsyncTabletSnapshotOp>(
      master_, AsyncTaskPool(), tablet, snapshot_id, operation, epoch);
  result->SetCallback(std::move(callback));
  tablet->table()->AddTask(result);
  return result;
}

void CatalogManager::ScheduleTabletSnapshotOp(const AsyncTabletSnapshotOpPtr& task) {
  WARN_NOT_OK(ScheduleTask(task), "Failed to send create snapshot request");
}

Status CatalogManager::RestoreSysCatalogCommon(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
    std::reference_wrapper<const ScopedRWOperation> tablet_pending_op,
    RestoreSysCatalogState* state, docdb::DocWriteBatch* write_batch,
    docdb::KeyValuePairPB* restore_kv) {
  // Restore master snapshot and load it to RocksDB.
  auto dir = VERIFY_RESULT(tablet->snapshots().RestoreToTemporary(
      restoration->snapshot_id, restoration->restore_at));
  rocksdb::Options rocksdb_options;
  std::string log_prefix = LogPrefix();
  // Remove ": " to patch suffix.
  log_prefix.erase(log_prefix.size() - 2);
  tablet->InitRocksDBOptions(&rocksdb_options, log_prefix + " [TMP]: ");

  auto db = VERIFY_RESULT(rocksdb::DB::Open(rocksdb_options, dir));
  // db can't be closed concurrently, so it is ok to use dummy ScopedRWOperation.
  auto db_pending_op = ScopedRWOperation();
  auto doc_db = docdb::DocDB::FromRegularUnbounded(db.get());

  // Load objects to restore and determine obsolete objects.
  auto schema_packing_provider = &tablet->GetSchemaPackingProvider();
  RETURN_NOT_OK(state->LoadRestoringObjects(doc_read_context(), doc_db, db_pending_op));
  // Load existing objects from RocksDB because on followers they are NOT present in loaded sys
  // catalog state.
  RETURN_NOT_OK(
      state->LoadExistingObjects(doc_read_context(), tablet->doc_db(), tablet_pending_op));
  RETURN_NOT_OK(state->Process());

  // Restore the pg_catalog tables.
  if (FLAGS_enable_ysql && state->IsYsqlRestoration()) {
    // Restore sequences_data table.
    RETURN_NOT_OK(state->PatchSequencesDataObjects());

    RETURN_NOT_OK(state->ProcessPgCatalogRestores(
        doc_db, tablet->doc_db(), write_batch, doc_read_context(), schema_packing_provider,
        tablet->metadata()));
  }

  // Crash for tests.
  MAYBE_FAULT(FLAGS_TEST_crash_during_sys_catalog_restoration);

  // Restore the other tables.
  RETURN_NOT_OK(state->PrepareWriteBatch(
      schema(), schema_packing_provider, write_batch, master_->clock()->Now()));

  // Updates the restoration state to indicate that sys catalog phase has completed.
  // Also, initializes the master side perceived list of tables/tablets/namespaces
  // that need to be restored for verification post sys catalog load.
  // Also, re-initializes the tablet list since it could have been changed from the
  // time of snapshot creation.
  // Also, generates the restoration state entry.
  // This is to persist the restoration so that on restarts the RESTORE_ON_TABLET
  // rpcs can be retried.
  *restore_kv = VERIFY_RESULT(
      snapshot_coordinator_.UpdateRestorationAndGetWritePair(restoration));

  return Status::OK();
}

Status CatalogManager::RestoreSysCatalogSlowPitr(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << restoration->restoration_id;

  // Creating this scopedRWOperation guarantees that rocksdb will be alive during the
  // entire duration of this function. If shutdown is issued in parallel then it will
  // wait for this operation to complete before starting shutdown rocksdb.
  auto tablet_pending_op = tablet->CreateScopedRWOperationBlockingRocksDbShutdownStart();

  bool restore_successful = false;
  // If sys catalog restoration fails then unblock other RPCs.
  auto scope_exit = ScopeExit([this, &restore_successful] {
    if (!restore_successful) {
      LOG(INFO) << "PITR: Accepting RPCs to the master leader";
      std::lock_guard l(state_lock_);
      is_catalog_loaded_ = true;
    }
  });

  RestoreSysCatalogState state(restoration);
  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional, tablet_pending_op);
  docdb::KeyValuePairPB restore_kv;

  RETURN_NOT_OK(RestoreSysCatalogCommon(
      restoration, tablet, tablet_pending_op, &state, &write_batch, &restore_kv));

  // Apply write batch to RocksDB.
  state.WriteToRocksDB(
      &write_batch, restore_kv, restoration->write_time, restoration->op_id, tablet);

  LOG_WITH_PREFIX(INFO) << "PITR: In leader term " << LeaderTerm()
                        << ", wrote " << write_batch.size() << " entries to rocksdb";

  if (LeaderTerm() >= 0) {
    RETURN_NOT_OK(ElectedAsLeaderCb());
  }

  restore_successful = true;

  return Status::OK();
}

Status CatalogManager::RestoreSysCatalogFastPitr(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << restoration->restoration_id;
  // As of 19 May 2023, rocksdb shutdown can only be issued either on a raft apply path
  // or via TabletPeer shutdown. Since this particular operation is in the raft apply path
  // we can be sure that there won't be any concurrent shutdowns issued since raft apply is
  // sequential and tabletpeer shutdown waits for all pending raft apply to finish before
  // triggering a shutdown. So we don't need a ScopedRWOperation guarding this area against
  // Rocsdb shutdowns. Create a dummy ScopedRWOperation here.
  ScopedRWOperation tablet_pending_op;
  bool restore_successful = false;

  // If sys catalog restoration fails then unblock other RPCs.
  auto scope_exit = ScopeExit([this, &restore_successful] {
    if (!restore_successful) {
      LOG(INFO) << "PITR: Accepting RPCs to the master leader";
      std::lock_guard<simple_spinlock> l(state_lock_);
      is_catalog_loaded_ = true;
    }
  });

  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional, tablet_pending_op);
  RestoreSysCatalogState state(restoration);
  docdb::KeyValuePairPB restore_kv;

  RETURN_NOT_OK(RestoreSysCatalogCommon(
      restoration, tablet, tablet_pending_op, &state, &write_batch, &restore_kv));

  if (state.IsYsqlRestoration()) {
    // Set Hybrid Time filter for pg catalog tables.
    tablet::TabletScopedRWOperationPauses op_pauses = tablet->StartShutdownRocksDBs(
        tablet::DisableFlushOnShutdown::kFalse, tablet::AbortOps::kTrue);

    std::lock_guard<std::mutex> lock(tablet->create_checkpoint_lock_);

    tablet->CompleteShutdownRocksDBs(op_pauses);

    rocksdb::Options rocksdb_opts;
    tablet->InitRocksDBOptions(&rocksdb_opts, tablet->LogPrefix());
    docdb::RocksDBPatcher patcher(tablet->metadata()->rocksdb_dir(), rocksdb_opts);
    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.SetHybridTimeFilter(restoration->db_oid, restoration->restore_at));

    RETURN_NOT_OK(tablet->OpenKeyValueTablet());
    RETURN_NOT_OK(tablet->EnableCompactions(&op_pauses.blocking_rocksdb_shutdown_start));

    // Ensure that op_pauses stays in scope throughout this function.
    for (auto* op_pause : op_pauses.AsArray()) {
      DFATAL_OR_RETURN_NOT_OK(op_pause->status());
    }
  }

  // Apply write batch to RocksDB.
  state.WriteToRocksDB(
      &write_batch, restore_kv, restoration->write_time, restoration->op_id, tablet);

  LOG_WITH_PREFIX(INFO) << "PITR: In leader term " << LeaderTerm() << ", wrote "
                        << write_batch.size() << " entries to rocksdb";

  if (LeaderTerm() >= 0) {
    RETURN_NOT_OK(ElectedAsLeaderCb());
  }

  restore_successful = true;

  return Status::OK();
}

Status CatalogManager::RestoreSysCatalog(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet, Status* complete_status) {
  Status s;
  if (GetAtomicFlag(&FLAGS_enable_fast_pitr)) {
    s = RestoreSysCatalogFastPitr(restoration, tablet);
  } else {
    s = RestoreSysCatalogSlowPitr(restoration, tablet);
  }
  // As RestoreSysCatalog is synchronous on Master it should be ok to set the completion
  // status in case of validation failures so that it gets propagated back to the client before
  // doing any write operations.
  if (s.IsNotSupported()) {
    *complete_status = s;
    return Status::OK();
  }
  return s;
}

Status CatalogManager::VerifyRestoredObjects(
    const std::unordered_map<std::string, SysRowEntryType>& objects,
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) {
  std::unordered_map<std::string, SysRowEntryType> objects_to_restore = objects;
  auto entries = VERIFY_RESULT(CollectEntriesForSnapshot(tables));
  // auto objects_to_restore = restoration.non_system_objects_to_restore;
  VLOG_WITH_PREFIX(1) << "Objects to restore: " << AsString(objects_to_restore);
  // There could be duplicate entries collected, for instance in the case of
  // colocated tables.
  std::unordered_set<std::string> unique_entries;
  for (const auto& entry : entries.entries()) {
    if (!unique_entries.insert(entry.id()).second) {
      continue;
    }
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

void CatalogManager::CleanupHiddenObjects(
    const ScheduleMinRestoreTime& schedule_min_restore_time, const LeaderEpoch& epoch) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(schedule_min_restore_time);

  std::vector<TabletInfoPtr> hidden_tablets;
  std::vector<TableInfoPtr> tables;
  {
    SharedLock lock(mutex_);
    hidden_tablets = hidden_tablets_;
    tables.reserve(tables_->Size());
    for (const auto& p : tables_->GetAllTables()) {
      if (!p->is_system()) {
        tables.push_back(p);
      }
    }
  }
  CleanupHiddenTablets(hidden_tablets, schedule_min_restore_time, epoch);
  CleanupHiddenTables(std::move(tables), schedule_min_restore_time, epoch);
}

void CatalogManager::CleanupHiddenTablets(
    const std::vector<TabletInfoPtr>& hidden_tablets,
    const ScheduleMinRestoreTime& schedule_min_restore_time,
    const LeaderEpoch& epoch) {
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
      cleanup = !RetainedByXRepl(tablet->id());
    }
    if (cleanup) {
      tablets_to_delete.push_back(tablet);
    }
  }
  if (!tablets_to_delete.empty()) {
    LOG_WITH_PREFIX(INFO) << "Cleanup hidden tablets: " << AsString(tablets_to_delete);
    WARN_NOT_OK(DeleteTabletListAndSendRequests(
        tablets_to_delete, "Cleanup hidden tablets", {} /* retained_by_snapshot_schedules */,
        false /* transaction_status_tablets */, epoch),
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

void CatalogManager::CleanupHiddenTables(
    std::vector<TableInfoPtr> tables,
    const ScheduleMinRestoreTime& schedule_min_restore_time,
    const LeaderEpoch& epoch) {
  std::vector<TableInfo::WriteLock> locks;
  EraseIf([this, &locks, &schedule_min_restore_time, &epoch](const TableInfoPtr& table) {
    {
      auto lock = table->LockForRead();
      // If the table is colocated and hidden then remove it from its colocated tablet if
      // it has expired.
      if (lock->is_hidden() && !lock->started_deleting()) {
        auto tablet_info = table->GetColocatedUserTablet();
        if (tablet_info) {
          auto tablet_lock = tablet_info->LockForRead();
          bool cleanup = true;
          auto hide_hybrid_time = HybridTime::FromPB(lock->pb.hide_hybrid_time());

          for (const auto& schedule_id_str : tablet_lock->pb.retained_by_snapshot_schedules()) {
            auto schedule_id = TryFullyDecodeSnapshotScheduleId(schedule_id_str);
            auto it = schedule_min_restore_time.find(schedule_id);
            // If schedule is not present in schedule_min_restore_time then it means that schedule
            // was deleted, so it should not retain the tablet.
            if (it != schedule_min_restore_time.end() && it->second <= hide_hybrid_time) {
              VLOG_WITH_PREFIX(1)
                  << "Retaining colocated table: " << table->id() << ", hide hybrid time: "
                  << hide_hybrid_time << ", because of schedule: " << schedule_id
                  << ", min restore time: " << it->second;
              cleanup = false;
              break;
            }
          }

          if (!cleanup) {
            return true;
          }
          LOG(INFO) << "Cleaning up HIDDEN colocated table " << table->name();
          auto call = std::make_shared<AsyncRemoveTableFromTablet>(
              master_, AsyncTaskPool(), tablet_info, table, epoch);
          table->AddTask(call);
          WARN_NOT_OK(ScheduleTask(call), "Failed to send RemoveTableFromTablet request");
          table->ClearTabletMaps();
        }
      }
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

  Status s = sys_catalog_->Upsert(epoch, tables);
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
    return -1;
  }
  auto consensus_result = peer->GetConsensus();
  if (!consensus_result) {
    return -1;
  }
  return consensus_result.get()->GetLeaderState(/* allow_stale= */ true).term;
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
  auto* tablet_snapshots = l.mutable_data()->pb.mutable_tablet_snapshots();
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
  auto* tablet_snapshots = l.mutable_data()->pb.mutable_tablet_snapshots();
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
  auto* tablet_snapshots = l.mutable_data()->pb.mutable_tablet_snapshots();
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
  LOG(INFO) << "Servicing CreateSnapshotSchedule " << req->ShortDebugString();
  CreateSnapshotScheduleRequestPB req_with_ns_id;
  req_with_ns_id.CopyFrom(*req);
  // Filter should be namespace level and have the namespace id.
  // Set namespace id if not already present in the request.
  if (req->options().filter().tables().tables_size() != 1) {
    return SetupError(
        resp->mutable_error(), MasterErrorPB::INVALID_REQUEST,
        STATUS(NotSupported, "Only one filter can be set on a snapshot schedule"));
  }
  auto& filter = req->options().filter().tables().tables(0).namespace_();
  if (!filter.has_id()) {
    NamespaceIdentifierPB ns_id;
    ns_id.set_database_type(filter.database_type());
    ns_id.set_name(filter.name());
    auto ns = VERIFY_RESULT(FindNamespace(ns_id));
    LOG_WITH_FUNC(INFO) << "Namespace info obtained on master " << ns->ToString();
    TableIdentifierPB* ns_req =
        req_with_ns_id.mutable_options()->mutable_filter()->mutable_tables()->mutable_tables(0);
    ns_req->mutable_namespace_()->set_id(ns->id());
    ns_req->mutable_namespace_()->set_name(ns->name());
    ns_req->mutable_namespace_()->set_database_type(ns->database_type());
    LOG_WITH_FUNC(INFO) << "Modified request " << req_with_ns_id.ShortDebugString();
  }

  auto id = VERIFY_RESULT(snapshot_coordinator_.CreateSchedule(
      req_with_ns_id, leader_ready_term(), rpc->GetClientDeadline()));
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

Status CatalogManager::EditSnapshotSchedule(
    const EditSnapshotScheduleRequestPB* req,
    EditSnapshotScheduleResponsePB* resp,
    rpc::RpcContext* rpc) {
  auto id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());
  *resp->mutable_schedule() = VERIFY_RESULT(snapshot_coordinator_.EditSnapshotSchedule(
      id, *req, leader_ready_term(), rpc->GetClientDeadline()));
  return Status::OK();
}

Status CatalogManager::RestoreSnapshotSchedule(
    const RestoreSnapshotScheduleRequestPB* req,
    RestoreSnapshotScheduleResponsePB* resp,
    rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  auto id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());
  HybridTime ht = HybridTime(req->restore_ht());
  auto deadline = rpc->GetClientDeadline();

  const auto disable_duration_ms = MonoDelta::FromMilliseconds(1000 *
      (FLAGS_inflight_splits_completion_timeout_secs + FLAGS_pitr_max_restore_duration_secs));
  const auto wait_inflight_splitting_until = CoarseMonoClock::Now() +
      MonoDelta::FromMilliseconds(1000 * FLAGS_inflight_splits_completion_timeout_secs);

  // Disable splitting and then wait for all pending splits to complete before starting restoration.
  DisableTabletSplittingInternal(disable_duration_ms, kPitrFeatureName);

  bool inflight_splits_finished = false;
  while (CoarseMonoClock::Now() < std::min(wait_inflight_splitting_until, deadline)) {
    // Wait for existing split operations to complete.
    if (IsTabletSplittingCompleteInternal(true /* wait_for_parent_deletion */, deadline)) {
      inflight_splits_finished = true;
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_pitr_split_disable_check_freq_ms));
  }

  if (!inflight_splits_finished) {
    ReenableTabletSplitting(kPitrFeatureName);
    return STATUS(TimedOut, "Timed out waiting for inflight tablet splitting to complete.");
  }

  return snapshot_coordinator_.RestoreSnapshotSchedule(id, ht, resp, epoch.leader_term, deadline);
}

template <typename Registry, typename Mutex>
bool ShouldResendRegistry(
    const std::string& ts_uuid, bool has_registration, Registry* registry, Mutex* mutex) {
  bool should_resend_registry;
  {
    std::lock_guard lock(*mutex);
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
  auto* tablet_snapshots = snapshot_pb->mutable_tablet_snapshots();

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);
    tablet_info->set_state(state);
  }
}

Status CatalogManager::GetTableSchemaFromSysCatalog(
    const GetTableSchemaFromSysCatalogRequestPB* req, GetTableSchemaFromSysCatalogResponsePB* resp,
    rpc::RpcContext* rpc) {
  uint64_t read_time = std::numeric_limits<uint64_t>::max();
  if (!req->has_read_time()) {
    LOG(INFO) << "Reading latest schema version for: " << req->table().table_id()
              << " from system catalog table";
  } else {
    read_time = req->read_time();
  }
  VLOG(1) << "Get the table: " << req->table().table_id()
          << " specific schema from system catalog with read hybrid time: " << req->read_time();
  Schema schema;
  uint32_t schema_version;
  auto status = sys_catalog_->GetTableSchema(
      req->table().table_id(), ReadHybridTime::FromUint64(read_time), &schema, &schema_version);
  if (!status.ok()) {
    Status s = STATUS_SUBSTITUTE(
        NotFound, "Could not find specific schema from system catalog for request $0.",
        req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }
  SchemaToPB(schema, resp->mutable_schema());
  resp->set_version(schema_version);
  return Status::OK();
}

Status CatalogManager::GetUDTypeMetadata(
    const GetUDTypeMetadataRequestPB* req, GetUDTypeMetadataResponsePB* resp,
    rpc::RpcContext* rpc) {
  auto namespace_info = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);
  uint32_t database_oid;
  {
    namespace_info->LockForRead();
    RSTATUS_DCHECK_EQ(
        namespace_info->database_type(), YQL_DATABASE_PGSQL, InternalError,
        Format("Expected YSQL database, got: $0", namespace_info->database_type()));
    database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_info->id()));
  }
  if (req->pg_enum_info()) {
    std::unordered_map<uint32_t, string> enum_oid_label_map;
    if (req->has_pg_type_oid()) {
      enum_oid_label_map =
          VERIFY_RESULT(sys_catalog_->ReadPgEnum(database_oid, req->pg_type_oid()));
    } else {
      enum_oid_label_map = VERIFY_RESULT(sys_catalog_->ReadPgEnum(database_oid));
    }
    for (const auto& [oid, label] : enum_oid_label_map) {
      PgEnumInfoPB* pg_enum_info_pb = resp->add_enums();
      pg_enum_info_pb->set_oid(oid);
      pg_enum_info_pb->set_label(label);
    }
  } else if (req->pg_composite_info()) {
    RelTypeOIDMap reltype_oid_map;
    if (req->has_pg_type_oid()) {
      reltype_oid_map = VERIFY_RESULT(
          sys_catalog_->ReadCompositeTypeFromPgClass(database_oid, req->pg_type_oid()));
    } else {
      reltype_oid_map = VERIFY_RESULT(sys_catalog_->ReadCompositeTypeFromPgClass(database_oid));
    }

    std::vector<uint32_t> table_oids;
    for (const auto& [reltype, oid] : reltype_oid_map) {
      table_oids.push_back(oid);
    }

    sort(table_oids.begin(), table_oids.end());

    RelIdToAttributesMap attributes_map =
        VERIFY_RESULT(sys_catalog_->ReadPgAttributeInfo(database_oid, table_oids));

    for (const auto& [reltype, oid] : reltype_oid_map) {
      if (attributes_map.find(oid) != attributes_map.end()) {
        PgCompositeInfoPB* pg_composite_info_pb = resp->add_composites();
        pg_composite_info_pb->set_oid(reltype);
        for (auto const& attribute : attributes_map[oid]) {
          *(pg_composite_info_pb->add_attributes()) = attribute;
        }
      } else {
        LOG_WITH_FUNC(INFO) << "No attributes found for attrelid: " << oid
                            << " corresponding to composite type of id: " << reltype;
      }
    }
  }
  return Status::OK();
}

Status CatalogManager::ValidateTableSchema(
    const std::shared_ptr<client::YBTableInfo>& info,
    const SetupReplicationInfo& setup_info,
    GetTableSchemaResponsePB* resp) {
  bool is_ysql_table = info->table_type == client::YBTableType::PGSQL_TABLE_TYPE;
  if (setup_info.transactional && !GetAtomicFlag(&FLAGS_allow_ycql_transactional_xcluster) &&
      !is_ysql_table) {
    return STATUS_FORMAT(
        NotSupported,
        "Transactional replication is not supported for non-YSQL tables: $0",
        info->table_name.ToString());
  }

  // Get corresponding table schema on local universe.
  GetTableSchemaRequestPB req;

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
  SCHECK(status.ok() && !list_resp.has_error(), NotFound,
         Substitute("Error while listing table: $0", status.ToString()));

  const auto& source_schema = client::internal::GetSchema(info->schema);
  for (const auto& t : list_resp.tables()) {
    // Check that table name and namespace both match.
    if (t.name() != info->table_name.table_name() ||
        t.namespace_().name() != info->table_name.namespace_name()) {
      continue;
    }

    // Check that schema name matches for YSQL tables, if the field is empty, fill in that
    // information during GetTableSchema call later.
    bool has_valid_pgschema_name = !t.pgschema_name().empty();
    if (is_ysql_table && has_valid_pgschema_name &&
        t.pgschema_name() != source_schema.SchemaName()) {
      continue;
    }

    // Get the table schema.
    table->set_table_id(t.id());
    status = GetTableSchema(&req, resp);
    SCHECK(status.ok() && !resp->has_error(), NotFound,
           Substitute("Error while getting table schema: $0", status.ToString()));

    // Double-check schema name here if the previous check was skipped.
    if (is_ysql_table && !has_valid_pgschema_name) {
      std::string target_schema_name = resp->schema().pgschema_name();
      if (target_schema_name != source_schema.SchemaName()) {
        table->clear_table_id();
        continue;
      }
    }

    // Verify that the table on the target side supports replication.
    if (is_ysql_table && t.has_relation_type() && t.relation_type() == MATVIEW_TABLE_RELATION) {
      return STATUS_FORMAT(NotSupported,
          "Replication is not supported for materialized view: $0",
          info->table_name.ToString());
    }

    Schema consumer_schema;
    auto result = SchemaFromPB(resp->schema(), &consumer_schema);

    // We now have a table match. Validate the schema.
    SCHECK(result.ok() && consumer_schema.EquivalentForDataCopy(source_schema), IllegalState,
           Substitute("Source and target schemas don't match: "
                      "Source: $0, Target: $1, Source schema: $2, Target schema: $3",
               info->table_id, resp->identifier().table_id(),
               info->schema.ToString(), resp->schema().DebugString()));
    break;
  }

  SCHECK(table->has_table_id(), NotFound, Substitute(
      "Could not find matching table for $0$1", info->table_name.ToString(),
      (is_ysql_table ? " pgschema_name: " + source_schema.SchemaName() : "")));

  // Still need to make map of table id to resp table id (to add to validated map)
  // For colocated tables, only add the parent table since we only added the parent table to the
  // original pb (we use the number of tables in the pb to determine when validation is done).
  if (info->colocated) {
    // We require that colocated tables have the same colocation ID.
    //
    // Backward compatibility: tables created prior to #7378 use YSQL table OID as a colocation ID.
    auto source_clc_id = info->schema.has_colocation_id()
        ? info->schema.colocation_id()
        : CHECK_RESULT(GetPgsqlTableOid(info->table_id));
    auto target_clc_id = (resp->schema().has_colocated_table_id() &&
                          resp->schema().colocated_table_id().has_colocation_id())
        ? resp->schema().colocated_table_id().colocation_id()
        : CHECK_RESULT(GetPgsqlTableOid(resp->identifier().table_id()));
    SCHECK(source_clc_id == target_clc_id, IllegalState,
           Substitute("Source and target colocation IDs don't match for colocated table: "
                      "Source: $0, Target: $1, Source colocation ID: $2, Target colocation ID: $3",
                      info->table_id, resp->identifier().table_id(), source_clc_id, target_clc_id));
  }

  {
    SharedLock lock(mutex_);
    if (xcluster_consumer_tables_to_stream_map_.contains(table->table_id())) {
      return STATUS(IllegalState, "N:1 replication topology not supported");
    }
  }

  return Status::OK();
}

Result<RemoteTabletServer *> CatalogManager::GetLeaderTServer(
    client::internal::RemoteTabletPtr tablet) {
  auto ts = tablet->LeaderTServer();
  if (ts == nullptr) {
    return STATUS(NotFound, "Tablet leader not found for tablet", tablet->tablet_id());
  }
  return ts;
}

Result<SnapshotSchedulesToObjectIdsMap> CatalogManager::MakeSnapshotSchedulesToObjectIdsMap(
    SysRowEntryType type) {
  return snapshot_coordinator_.MakeSnapshotSchedulesToObjectIdsMap(type);
}

Result<bool> CatalogManager::IsTableUndergoingPitrRestore(const TableInfo& table_info) {
  return snapshot_coordinator_.IsTableUndergoingPitrRestore(table_info);
}

Result<bool> CatalogManager::IsTablePartOfSomeSnapshotSchedule(const TableInfo& table_info) {
  return snapshot_coordinator_.IsTableCoveredBySomeSnapshotSchedule(table_info);
}

bool CatalogManager::IsPitrActive() {
  return snapshot_coordinator_.IsPitrActive();
}

Result<size_t> CatalogManager::GetNumLiveTServersForActiveCluster() {
  BlacklistSet blacklist = VERIFY_RESULT(BlacklistSetFromPB());
  TSDescriptorVector ts_descs;
  auto uuid = VERIFY_RESULT(placement_uuid());
  master_->ts_manager()->GetAllLiveDescriptorsInCluster(&ts_descs, uuid, blacklist);
  return ts_descs.size();
}

void CatalogManager::PrepareRestore() {
  LOG_WITH_PREFIX(INFO) << "Disabling concurrent RPCs since restoration is ongoing";
  {
    std::lock_guard l(state_lock_);
    is_catalog_loaded_ = false;
  }
  sys_catalog_->IncrementPitrCount();
}

docdb::HistoryCutoff CatalogManager::AllowedHistoryCutoffProvider(
    tablet::RaftGroupMetadata* metadata) {
  return snapshot_coordinator_.AllowedHistoryCutoffProvider(metadata);
}

}  // namespace master
}  // namespace yb
