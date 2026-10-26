// Copyright (c) YugabyteDB, Inc.
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


#include "yb/master/sys_catalog_restore.h"

#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/restore_sys_catalog_state.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/util/fault_injection.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/std_util.h"

DEFINE_test_flag(double, crash_during_sys_catalog_restoration, 0.0,
    "Probability of crash during the RESTORE_SYS_CATALOG phase.");

DECLARE_bool(enable_fast_pitr);
DECLARE_bool(enable_ysql);

namespace yb::master {

namespace {

struct RestoreArgs {
  const std::shared_ptr<tablet::TabletPeer>& tablet_peer;
  const docdb::DocReadContext& doc_read_context;
  MasterSnapshotCoordinator* coordinator;
  server::Clock* clock;
  int64_t leader_term;
  const std::string& log_prefix;
};

Status RestoreSysCatalogCommon(
    const RestoreArgs& args,
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
    std::reference_wrapper<const ScopedRWOperation> tablet_pending_op,
    RestoreSysCatalogState* state, docdb::DocWriteBatch* write_batch,
    docdb::KeyValuePairPB* restore_kv) {
  // Restore master snapshot and load it to RocksDB.
  auto db = VERIFY_RESULT(RestoreSysCatalogSnapshotToTmpRocksDb(
      tablet, restoration->snapshot_id, restoration->restore_at, args.log_prefix));
  auto doc_db = docdb::DocDB::FromRegularUnbounded(db.get());

  // db can't be closed concurrently, so it is ok to use dummy ScopedRWOperation.
  auto db_pending_op = ScopedRWOperation();

  // Load objects to restore and determine obsolete objects.
  auto schema_packing_provider = &tablet->GetSchemaPackingProvider();
  RETURN_NOT_OK(state->LoadRestoringObjects(args.doc_read_context, doc_db, db_pending_op));
  // Load existing objects from RocksDB because on followers they are NOT present in loaded sys
  // catalog state.
  RETURN_NOT_OK(
      state->LoadExistingObjects(args.doc_read_context, tablet->doc_db(), tablet_pending_op));
  RETURN_NOT_OK(state->Process());

  // Restore the pg_catalog tables.
  // Since lifetime of tablet_peer and doc_read_context matches in this case. We could
  // use tablet peer reference counter for doc read context.
  auto doc_read_context = SharedField(args.tablet_peer, &args.doc_read_context);
  if (FLAGS_enable_ysql && state->IsYsqlRestoration()) {
    // Restore sequences_data table.
    RETURN_NOT_OK(state->PatchSequencesDataObjects());

    RETURN_NOT_OK(state->ProcessPgCatalogRestores(
        doc_db, tablet->doc_db(), write_batch, doc_read_context, schema_packing_provider,
        tablet->metadata()));
  }

  // Crash for tests.
  MAYBE_FAULT(FLAGS_TEST_crash_during_sys_catalog_restoration);

  // Restore the other tables.
  RETURN_NOT_OK(state->PrepareWriteBatch(
      doc_read_context, schema_packing_provider, write_batch, args.clock->Now()));

  // Updates the restoration state to indicate that sys catalog phase has completed.
  // Also, initializes the master side perceived list of tables/tablets/namespaces
  // that need to be restored for verification post sys catalog load.
  // Also, re-initializes the tablet list since it could have been changed from the
  // time of snapshot creation.
  // Also, generates the restoration state entry.
  // This is to persist the restoration so that on restarts the RESTORE_ON_TABLET
  // rpcs can be retried.
  *restore_kv = VERIFY_RESULT(
      args.coordinator->UpdateRestorationAndGetWritePair(restoration));

  return Status::OK();
}

Status RestoreSysCatalogSlowPitr(
    const RestoreArgs& args,
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet) {
  VLOG(1) << args.log_prefix << __func__ << ": " << restoration->restoration_id;

  // Creating this scopedRWOperation guarantees that rocksdb will be alive during the
  // entire duration of this function. If shutdown is issued in parallel then it will
  // wait for this operation to complete before starting shutdown rocksdb.
  auto tablet_pending_op = tablet->CreateScopedRWOperationBlockingRocksDbShutdownStart();

  RestoreSysCatalogState state(restoration);
  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional, tablet_pending_op);
  docdb::KeyValuePairPB restore_kv;

  RETURN_NOT_OK(RestoreSysCatalogCommon(
      args, restoration, tablet, tablet_pending_op, &state, &write_batch, &restore_kv));

  // Apply write batch to RocksDB.
  state.WriteToRocksDB(
      &write_batch, restore_kv, restoration->write_time, restoration->op_id, tablet);

  LOG(INFO) << args.log_prefix << "PITR: In leader term " << args.leader_term
                        << ", wrote " << write_batch.size() << " entries to rocksdb";


  return Status::OK();
}

Status RestoreSysCatalogFastPitr(
    const RestoreArgs& args,
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet) {
  VLOG(1) << args.log_prefix << __func__ << ": " << restoration->restoration_id;
  // As of 19 May 2023, rocksdb shutdown can only be issued either on a raft apply path
  // or via TabletPeer shutdown. Since this particular operation is in the raft apply path
  // we can be sure that there won't be any concurrent shutdowns issued since raft apply is
  // sequential and tabletpeer shutdown waits for all pending raft apply to finish before
  // triggering a shutdown. So we don't need a ScopedRWOperation guarding this area against
  // Rocsdb shutdowns. Create a dummy ScopedRWOperation here.
  ScopedRWOperation tablet_pending_op;

  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional, tablet_pending_op);
  RestoreSysCatalogState state(restoration);
  docdb::KeyValuePairPB restore_kv;

  RETURN_NOT_OK(RestoreSysCatalogCommon(
      args, restoration, tablet, tablet_pending_op, &state, &write_batch, &restore_kv));

  if (state.IsYsqlRestoration()) {
    // Set Hybrid Time filter for pg catalog tables.
    tablet::TabletScopedRWOperationPauses op_pauses = tablet->StartShutdownStorages(
        tablet::DisableFlushOnShutdown::kFalse, tablet::AbortOps::kTrue);

    std::lock_guard<std::mutex> lock(tablet->create_checkpoint_lock_);

    tablet->CompleteShutdownStorages(op_pauses);

    rocksdb::Options rocksdb_opts;
    tablet->InitRocksDBOptions(
        &rocksdb_opts, tablet->LogPrefix(), docdb::StorageDbType::kRegular);
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

  LOG(INFO) << args.log_prefix << "PITR: In leader term " << args.leader_term << ", wrote "
                        << write_batch.size() << " entries to rocksdb";


  return Status::OK();
}

}  // namespace

Result<std::unique_ptr<rocksdb::DB>> RestoreSysCatalogSnapshotToTmpRocksDb(
    tablet::Tablet* tablet, const TxnSnapshotId& snapshot_id, HybridTime restore_at,
    std::string log_prefix) {
  // Remove ": " to patch suffix.
  log_prefix.erase(log_prefix.size() - 2);

  // Restore master snapshot and load it to RocksDB.
  auto dir = VERIFY_RESULT(tablet->snapshots().RestoreToTemporary(snapshot_id, restore_at));
  rocksdb::Options rocksdb_options;
  tablet->InitRocksDBOptions(
      &rocksdb_options, log_prefix + " [TMP]: ", docdb::StorageDbType::kRegular);

  return rocksdb::DB::Open(rocksdb_options, dir);
}

Status RestoreSysCatalogOnApply(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const docdb::DocReadContext& doc_read_context, MasterSnapshotCoordinator* coordinator,
    server::Clock* clock, int64_t leader_term, const std::string& log_prefix) {
  const RestoreArgs args{
      .tablet_peer = tablet_peer,
      .doc_read_context = doc_read_context,
      .coordinator = coordinator,
      .clock = clock,
      .leader_term = leader_term,
      .log_prefix = log_prefix,
  };
  if (FLAGS_enable_fast_pitr) {
    return RestoreSysCatalogFastPitr(args, restoration, tablet);
  }
  return RestoreSysCatalogSlowPitr(args, restoration, tablet);
}

}  // namespace yb::master
