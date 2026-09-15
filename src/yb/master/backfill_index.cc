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
//

#include "yb/master/backfill_index.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/preprocessor/cat.hpp>

#include "yb/ash/wait_state.h"

#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/common/common_util.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/dockv/reader_projection.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/ysql/ysql_manager_if.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/uuid.h"

using std::vector;
using std::string;

DEFINE_RUNTIME_int32(ysql_index_backfill_rpc_timeout_ms, 5 * 60 * 1000, // 5 min.
    "Timeout used by the master when attempting to backfill a YSQL tablet during index creation.");
TAG_FLAG(ysql_index_backfill_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_rpc_timeout_ms, 1 * 30 * 1000, // 30 sec.
    "Timeout used by the master when attempting to backfill a tablet during index creation.");
TAG_FLAG(index_backfill_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_rpc_max_retries, 150,
    "Number of times to retry backfilling a tablet chunk during index creation.");
TAG_FLAG(index_backfill_rpc_max_retries, advanced);

DEFINE_RUNTIME_int32(index_backfill_rpc_max_delay_ms, 10 * 60 * 1000, // 10 min.
    "Maximum delay before retrying a backfill tablet chunk request during index creation.");
TAG_FLAG(index_backfill_rpc_max_delay_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_wait_for_alter_table_completion_ms, 100,
    "Delay before retrying to see if an in-progress alter table has "
    "completed, during index backfill.");
TAG_FLAG(index_backfill_wait_for_alter_table_completion_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_tablet_split_completion_timeout_sec, 30,
    "Total time to wait for tablet splitting to complete on a table from which we are "
    "running a backfill before aborting the backfill and marking it as failed.");
TAG_FLAG(index_backfill_tablet_split_completion_timeout_sec, advanced);

DEFINE_RUNTIME_int32(index_backfill_tablet_split_completion_poll_freq_ms, 2000,
    "Delay before retrying to see if tablet splitting has completed on the table from "
    "which we are running a backfill.");
TAG_FLAG(index_backfill_tablet_split_completion_poll_freq_ms, advanced);

DEFINE_RUNTIME_bool(defer_index_backfill, false,
    "Defer index backfill so that backfills can be performed as a batch later on.");
TAG_FLAG(defer_index_backfill, advanced);

DEFINE_RUNTIME_bool(allow_batching_non_deferred_indexes, true,
    "If enabled, indexes on the same (YCQL) table may be batched together during "
    "backfill, even if they were not deferred.");
TAG_FLAG(allow_batching_non_deferred_indexes, advanced);

DEFINE_test_flag(int32, slowdown_backfill_alter_table_rpcs_ms, 0,
    "Slows down the send alter table rpc's so that the master may be stopped between "
    "different phases.");

DEFINE_test_flag(int32, slowdown_backfill_job_deletion_ms, 0,
    "Slows down backfill job deletion so that backfill job can be read by test.");

DEFINE_test_flag(bool, block_index_backfill_ordering_generation_release, false,
    "Skip releasing index-backfill ordering generations in the terminal funnel, keeping them "
    "active so tests can run the verification scan against a completed build.");

DEFINE_RUNTIME_bool(ysql_index_backfill_fail_closed_verification, false,
    "Gate unique-index publication on the deferred verification outcome: after a SKIP_ALL "
    "backfill completes, an index reaches READ_WRITE_AND_DELETE only if every index tablet "
    "verifies clean; a violation or an unresolvable inconclusive outcome fails CREATE INDEX "
    "through the existing backfill failure path. Implies running the verification phase even "
    "when ysql_index_backfill_shadow_verification is off.");

DEFINE_RUNTIME_bool(ysql_index_backfill_shadow_verification, false,
    "Run the deferred uniqueness verification scan after a SKIP_ALL unique-index backfill "
    "completes. The scan itself runs on the publication critical path (CREATE INDEX waits "
    "for it), but its outcome is only persisted and logged -- it never gates publication "
    "(the fail-closed gate is a separate, later capability).");

DEFINE_RUNTIME_uint32(index_backfill_shadow_verification_max_concurrent_tablets, 4,
    "Bound on concurrently verified tablets per index during shadow verification.");

DEFINE_RUNTIME_uint64(index_backfill_shadow_verification_dockey_groups_per_rpc, 0,
    "DocKey-group budget per verification RPC (0 = bounded only by the RPC deadline); the "
    "coordinator resumes a paginated tablet from the returned resume key.");

DEFINE_RUNTIME_int32(index_backfill_verify_rpc_timeout_ms, 60000,
    "Deadline for one unique-index verification RPC, i.e. one deadline-bounded page of the "
    "verification scan (the tserver stops a grace margin early and returns a resume key). "
    "A dedicated budget like the backfill chunks' ysql_index_backfill_rpc_timeout_ms rather "
    "than the generic master_ts_rpc_timeout_ms: every page re-establishes iterators, so "
    "sizing pages independently of unrelated master RPCs matters on large tablets.");

DEFINE_test_flag(bool, fail_unique_index_verification_resolution, false,
    "Fail the verification phase's index-table resolution, exercising the coordinator-failure "
    "path before any index is selected for verification.");

DEFINE_test_flag(bool, skip_index_backfill, false,
    "Skips backfilling the data on tservers and leaves the index in inconsistent state.");

DEFINE_test_flag(bool, block_do_backfill, false,
    "Block DoBackfill from proceeding.");

DEFINE_test_flag(bool, pause_compute_safe_time_for_backfill_read, false,
    "Pauses the compute safe time for backfill read.");

DEFINE_test_flag(bool, skip_ddl_requester_liveness_check, false,
    "Skip starting the requester liveness task. Used in tests to simulate the pre-fix behavior "
    "where master continues sending BackfillIndex RPCs after the backend is killed.");

DEFINE_test_flag(bool, simulate_empty_indexes_during_backfill, false,
    "Simulates BackfillTable::indexes_to_build() to return an empty set.");

DEFINE_test_flag(bool, simulate_cannot_enable_compactions, false,
    "Skips updating an index table to GC delete markers and sending of the corresponding RPC "
    "to the TServer.");

DEFINE_test_flag(int32, delay_clearing_fully_applied_ms, 0,
    "Amount of time to delay clearing the fully applied schema.");

DECLARE_bool(ysql_enable_deferred_unique_index_verification);

namespace yb {
namespace master {

using namespace std::literals;
using server::MonitoredTaskState;
using strings::b2a_hex;
using strings::Substitute;
using tserver::TabletServerErrorPB;

namespace {

// Selects the uniqueness-check mode persisted for a new backfill job. The mode is chosen
// exactly once per job and is immutable afterwards: reloads (master failover, retries) must
// read the persisted value instead of re-selecting, so later runtime flag changes cannot
// reinterpret an active job.
UniqueIndexBackfillMode SelectUniqueIndexBackfillMode() {
  // Unknown override values select CHECK_ALL (fail closed for the job).
  if (const auto test_mode = GetUniqueIndexBackfillModeTestOverride()) {
    return *test_mode;
  }
  if (!FLAGS_ysql_enable_deferred_unique_index_verification) {
    return UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_CHECK_ALL;
  }
  // Deferred verification is not yet selectable in production: SKIP_ALL requires the
  // marked-write ordering and verification machinery from later parts of #33444. Until the
  // activation work lands, every job runs the fully checked path even with the capability
  // promoted.
  return UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_CHECK_ALL;
}

// Before advancing index permissions, we need to make sure Postgres side has advanced sufficiently
// - that the state tracked in pg_index haven't fallen behind from the desired permission
// for more than one step.
Result<bool> ShouldProceedWithPgsqlIndexPermissionUpdate(
    CatalogManager* catalog_manager,
    const TableId& idx_id,
    IndexPermissions new_perm) {
  // TODO(alex, jason): Add the appropriate cases for dropping index path
  switch (new_perm) {
    case INDEX_PERM_WRITE_AND_DELETE: {
      const auto db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(idx_id));
      const auto index_oid = VERIFY_RESULT(GetPgsqlTableOid(idx_id));
      auto live = VERIFY_RESULT(
          catalog_manager->GetYsqlManager().GetPgIndexStatus(db_oid, index_oid, "indislive"));
      if (!live) {
        VLOG(1) << "Index " << idx_id << " is not yet live, skipping permission update";
      }
      return live;
    }
    case INDEX_PERM_DO_BACKFILL: {
      const auto db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(idx_id));
      const auto index_oid = VERIFY_RESULT(GetPgsqlTableOid(idx_id));
      auto ready = VERIFY_RESULT(
          catalog_manager->GetYsqlManager().GetPgIndexStatus(db_oid, index_oid, "indisready"));
      if (!ready) {
        VLOG(1) << "Index " << idx_id << " is not yet ready, skipping permission update";
      }
      return ready;
    }
    default:
      // No need to wait for anything
      return true;
  }
}

} // namespace

void MultiStageAlterTable::CopySchemaDetailsToFullyApplied(SysTablesEntryPB* pb) {
  VLOG(4) << "Setting fully_applied_schema_version to " << pb->version();
  pb->mutable_fully_applied_schema()->CopyFrom(pb->schema());
  pb->set_fully_applied_schema_version(pb->version());
  pb->mutable_fully_applied_indexes()->CopyFrom(pb->indexes());
  if (pb->has_index_info()) {
    pb->mutable_fully_applied_index_info()->CopyFrom(pb->index_info());
  }
}

Status MultiStageAlterTable::ClearFullyAppliedAndUpdateState(
    CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& table,
    std::optional<uint32_t> expected_version, bool update_state_to_running,
    const LeaderEpoch& epoch) {
  if (PREDICT_FALSE(FLAGS_TEST_delay_clearing_fully_applied_ms > 0)) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_delay_clearing_fully_applied_ms));
  }
  auto l = table->LockForWrite();
  uint32_t current_version = l->pb.version();
  if (expected_version && *expected_version != current_version) {
    return STATUS(AlreadyPresent, "Table has already moved to a different version.");
  } else if (!l->is_running()) {
    LOG(WARNING) << __func__ << ": The table state is " << l->state_name() << " will stop backfill";
    return STATUS_SUBSTITUTE(
        IllegalState, "Table $0 is not in ALTERING or RUNNING state: $1",
        table->ToString(), l->state_name());
  }
  l.mutable_data()->pb.clear_fully_applied_schema();
  l.mutable_data()->pb.clear_fully_applied_schema_version();
  l.mutable_data()->pb.clear_fully_applied_indexes();
  l.mutable_data()->pb.clear_fully_applied_index_info();
  auto new_state = update_state_to_running ? SysTablesEntryPB::RUNNING : SysTablesEntryPB::ALTERING;
  l.mutable_data()->set_state(new_state, Format("Current schema version=$0", current_version));

  Status s = catalog_manager->sys_catalog_->Upsert(epoch, table);
  if (!s.ok()) {
    LOG(WARNING) << "An error occurred while updating sys-tables: " << s.ToString()
                 << ". This master may not be the leader anymore.";
    return s;
  }

  l.Commit();
  LOG(INFO) << table->ToString() << " - Alter table completed version=" << current_version
            << ", state: " << SysTablesEntryPB::State_Name(new_state);
  return Status::OK();
}

Result<bool> MultiStageAlterTable::UpdateIndexPermission(
    CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table,
    const std::unordered_map<TableId, IndexPermissions>& perm_mapping, const LeaderEpoch& epoch,
    std::optional<uint32_t> current_version) {
  TRACE(__func__);
  DVLOG(3) << __PRETTY_FUNCTION__ << " " << yb::ToString(*indexed_table);
  if (FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms > 0) {
    TRACE("Sleeping for $0 ms", FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms);
    DVLOG(3) << __PRETTY_FUNCTION__ << " " << yb::ToString(*indexed_table) << " sleeping for "
             << FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms
             << "ms BEFORE updating the index permission to " << ToString(perm_mapping);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms));
    DVLOG(3) << __PRETTY_FUNCTION__ << " Done Sleeping";
    TRACE("Done Sleeping");
  }

  bool permissions_updated = false;
  {
    TRACE("Locking indexed table");
    auto l = indexed_table->LockForWrite();
    auto& indexed_table_data = *l.mutable_data();
    auto& indexed_table_pb = indexed_table_data.pb;
    if (current_version && *current_version != indexed_table_pb.version()) {
      LOG(INFO) << "The table schema version "
                << "seems to have already been updated to " << indexed_table_pb.version()
                << " We wanted to do this update at " << *current_version;
      return STATUS_SUBSTITUTE(
          AlreadyPresent, "Schema was already updated to $0 before we got to it (expected $1).",
          indexed_table_pb.version(), *current_version);
    } else if (!indexed_table_data.is_running()) {
      LOG(WARNING) << __func__ << ": The table state is " << indexed_table_data.state_name()
                   << " will stop backfill";
      return STATUS_SUBSTITUTE(
          IllegalState, "Table $0 is not in ALTERING or RUNNING state: $1",
          indexed_table->ToString(), indexed_table_data.state_name());
    }

    CopySchemaDetailsToFullyApplied(&indexed_table_pb);
    bool is_pgsql = indexed_table_pb.table_type() == TableType::PGSQL_TABLE_TYPE;
    for (int i = 0; i < indexed_table_pb.indexes_size(); i++) {
      IndexInfoPB* idx_pb = indexed_table_pb.mutable_indexes(i);
      auto& idx_table_id = idx_pb->table_id();
      if (perm_mapping.find(idx_table_id) != perm_mapping.end()) {
        const auto new_perm = perm_mapping.at(idx_table_id);
        if (idx_pb->index_permissions() >= new_perm) {
          LOG(WARNING) << "Index " << idx_pb->table_id() << " on table "
                       << indexed_table->ToString() << " has index_permission "
                       << IndexPermissions_Name(idx_pb->index_permissions()) << " already past "
                       << IndexPermissions_Name(new_perm) << ". Will not update it";
          continue;
        }
        // TODO(alex, amit): Non-OK status here should be converted to TryAgain,
        //                   which should be handled on an upper level.
        if (is_pgsql && !VERIFY_RESULT(ShouldProceedWithPgsqlIndexPermissionUpdate(catalog_manager,
                                                                                   idx_table_id,
                                                                                   new_perm))) {
          continue;
        }
        idx_pb->set_index_permissions(new_perm);
        permissions_updated = true;
      }
    }

    if (permissions_updated) {
      indexed_table_pb.set_version(indexed_table_pb.version() + 1);
      indexed_table_pb.set_updates_only_index_permissions(true);
    } else {
      VLOG(1) << "Index permissions update skipped, leaving schema_version at "
              << indexed_table_pb.version();
    }
    indexed_table_data.set_state(
        SysTablesEntryPB::ALTERING,
        Format("Update index permission version=$0 ts=$1",
               indexed_table_pb.version(), LocalTimeAsString()));

    // Update sys-catalog with the new indexed table info.
    TRACE("Updating indexed table metadata on disk");
    RETURN_NOT_OK(catalog_manager->sys_catalog_->Upsert(epoch, indexed_table));

    // Update the in-memory state.
    TRACE("Committing in-memory state");
    l.Commit();
  }
  if (PREDICT_FALSE(FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms > 0)) {
    TRACE("Sleeping for $0 ms",
          FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms);
    DVLOG(3) << __PRETTY_FUNCTION__ << " " << yb::ToString(*indexed_table) << " sleeping for "
             << FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms
             << "ms AFTER updating the index permission to " << ToString(perm_mapping);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms));
    DVLOG(3) << __PRETTY_FUNCTION__ << " Done Sleeping";
    TRACE("Done Sleeping");
  }
  return permissions_updated;
}

Status MultiStageAlterTable::StartBackfillingData(
    CatalogManager* catalog_manager,
    const scoped_refptr<TableInfo>& indexed_table,
    const std::vector<IndexInfoPB>& idx_infos,
    std::optional<uint32_t> current_version, const LeaderEpoch& epoch,
    std::optional<TransactionMetadata> requester_transaction) {
  // Stay in ALTERING: IsAlterTableDone must not report done while the backfill is running.
  RETURN_NOT_OK(ClearFullyAppliedAndUpdateState(
      catalog_manager, indexed_table, current_version, /* change_state to RUNNING */ false, epoch));

  auto ns_info = catalog_manager->FindNamespaceById(indexed_table->namespace_id());
  RETURN_NOT_OK_PREPEND(ns_info, "Unable to get namespace info for backfill");

  RETURN_NOT_OK(indexed_table->SetIsBackfilling());
  TRACE("Starting backfill process");
  VLOG(0) << __func__ << " starting backfill on " << indexed_table->ToString() << " for "
          << yb::ToString(idx_infos);

  // Retrieve the requester transaction if it was stored during the permission-update phase.
  // Pass current_version so TakePendingBackfillRequesterTransaction rejects stale
  // transactions from earlier backfill attempts.
  if (!requester_transaction && current_version) {
    requester_transaction =
        indexed_table->TakePendingBackfillRequesterTransaction(*current_version);
  }

  if (FLAGS_TEST_skip_index_backfill) {
    TRACE("Skipping backfill of data on tservers");
    LOG(INFO) << "Skipping backfill of data on tservers";
    return Status::OK();
  }

  auto backfill_table = std::make_shared<BackfillTable>(
      catalog_manager->master_, catalog_manager->AsyncTaskPool(), indexed_table, idx_infos,
      *ns_info, epoch, std::move(requester_transaction));
  Status s = backfill_table->Launch();
  if (!s.ok()) {
    indexed_table->ClearIsBackfilling();
  }
  return s;
}

// Returns true, if the said IndexPermissions is a transient state.
// Returns false, if it is a state where the index can be. viz: READ_WRITE_AND_DELETE
// INDEX_UNUSED is considered transcient because it needs to delete the index.
bool IsTransientState(IndexPermissions perm) {
  return perm != INDEX_PERM_READ_WRITE_AND_DELETE && perm != INDEX_PERM_NOT_USED;
}

IndexPermissions NextPermission(IndexPermissions perm) {
  switch (perm) {
    case INDEX_PERM_DELETE_ONLY:
      return INDEX_PERM_WRITE_AND_DELETE;
    case INDEX_PERM_WRITE_AND_DELETE:
      return INDEX_PERM_DO_BACKFILL;
    case INDEX_PERM_DO_BACKFILL:
      CHECK(false) << "Not expected to be here.";
      return INDEX_PERM_DELETE_ONLY;
    case INDEX_PERM_READ_WRITE_AND_DELETE:
      CHECK(false) << "Not expected to be here.";
      return INDEX_PERM_DELETE_ONLY;
    case INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING:
      return INDEX_PERM_DELETE_ONLY_WHILE_REMOVING;
    case INDEX_PERM_DELETE_ONLY_WHILE_REMOVING:
      return INDEX_PERM_INDEX_UNUSED;
    case INDEX_PERM_INDEX_UNUSED:
    case INDEX_PERM_NOT_USED:
      CHECK(false) << "Not expected to be here.";
      return INDEX_PERM_DELETE_ONLY;
  }
  CHECK(false) << "Not expected to be here.";
  return INDEX_PERM_DELETE_ONLY;
}

Status MultiStageAlterTable::LaunchNextTableInfoVersionIfNecessary(
    CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table,
    uint32_t current_version, const LeaderEpoch& epoch,
    std::optional<TransactionMetadata> requester_transaction, bool respect_backfill_deferrals,
    bool update_ysql_to_backfill) {
  DVLOG_WITH_FUNC(3)
      << Format("$0, version: $1, respect_deferrals: $2, update_ysql_to_backfill: $3",
                *indexed_table, current_version, respect_backfill_deferrals,
                update_ysql_to_backfill);

  const bool is_ysql_table = (indexed_table->GetTableType() == TableType::PGSQL_TABLE_TYPE);
  // For YSQL, master won't automatically move the index permission to DO_BACKFILL unless
  // postgres calls CatalogManager::BackfillIndex() because postgres drives permission changes.
  const bool defer_backfill = !is_ysql_table && FLAGS_defer_index_backfill;
  const bool is_backfilling = indexed_table->IsBackfilling();

  std::unordered_map<TableId, IndexPermissions> indexes_to_update;
  vector<IndexInfoPB> indexes_to_backfill;
  vector<IndexInfoPB> deferred_indexes;
  vector<IndexInfoPB> indexes_to_delete;
  {
    TRACE("Locking indexed table");
    VLOG(1) << ("Locking indexed table");
    auto l = indexed_table->LockForRead();
    VLOG(1) << ("Locked indexed table");
    if (current_version != l->pb.version()) {
      LOG(WARNING) << "Somebody launched the next version before we got to it.";
      return Status::OK();
    }

    // Attempt to find an index that requires us to just launch the next state (i.e. not backfill)
    for (int i = 0; i < l->pb.indexes_size(); i++) {
      const IndexInfoPB& idx_pb = l->pb.indexes(i);
      if (!idx_pb.has_index_permissions()) {
        continue;
      }
      if (idx_pb.index_permissions() == INDEX_PERM_DO_BACKFILL) {
        if (respect_backfill_deferrals && (defer_backfill || idx_pb.is_backfill_deferred())) {
          LOG(INFO) << "Deferring index-backfill for " << idx_pb.table_id();
          deferred_indexes.emplace_back(idx_pb);
        } else {
          indexes_to_backfill.emplace_back(idx_pb);
        }
      } else if (idx_pb.index_permissions() == INDEX_PERM_INDEX_UNUSED) {
        indexes_to_delete.emplace_back(idx_pb);
      } else if (!is_ysql_table && idx_pb.index_permissions() != INDEX_PERM_READ_WRITE_AND_DELETE) {
        indexes_to_update.emplace(idx_pb.table_id(), NextPermission(idx_pb.index_permissions()));
      } else if (update_ysql_to_backfill &&
                 idx_pb.index_permissions() != INDEX_PERM_READ_WRITE_AND_DELETE &&
                 idx_pb.index_permissions() != INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING) {
        indexes_to_update.emplace(idx_pb.table_id(), NextPermission(idx_pb.index_permissions()));
      }
    }

    if (!is_backfilling && l.data().pb.backfill_jobs_size() > 0) {
      // If a backfill job was started for a set of indexes and then the leader
      // fails over, we should be careful that we are restarting the backfill job
      // with the same set of indexes.
      // A new index could have been added since the time the last backfill job started on
      // the old master. The safe time calculated for the earlier set of indexes may not be
      // valid for the new index(es) to use.
      DCHECK(l.data().pb.backfill_jobs_size() == 1) << "For now we only expect to have up to 1 "
                                                        "outstanding backfill job.";
      const BackfillJobPB& backfill_job = l.data().pb.backfill_jobs(0);
      VLOG(3) << "Found an in-progress backfill-job " << AsString(backfill_job);
      // Do not allow for any other indexes to piggy back with this backfill.
      indexes_to_backfill.assign(backfill_job.indexes().begin(), backfill_job.indexes().end());
      deferred_indexes.clear();
    }
  }

  if (indexes_to_update.empty() &&
      indexes_to_delete.empty() &&
      (is_backfilling || indexes_to_backfill.empty())) {
    TRACE("Not necessary to launch next version");
    VLOG(1) << "Not necessary to launch next version";
    return ClearFullyAppliedAndUpdateState(
        catalog_manager, indexed_table, current_version, /* change state to RUNNING */ true, epoch);
  }

  const bool batch_backfill_req = FLAGS_allow_batching_non_deferred_indexes && !is_ysql_table;
  if (indexes_to_backfill.size() > 1 && !batch_backfill_req) {
    LOG(INFO) << "Batching of non-deferred index-backfill(s) is disabled. Will be only backfilling "
                 "one index at a time.";
    indexes_to_backfill.resize(1);
  }

  // For YSQL online schema migration of indexes, instead of master driving the schema changes,
  // postgres will drive it.  Postgres will use four of the DocDB index permissions:
  //
  // - INDEX_PERM_WRITE_AND_DELETE (set from the start)
  // - INDEX_PERM_DO_BACKFILL (set by master, when postgres initiates BackfillIndex)
  // - INDEX_PERM_READ_WRITE_AND_DELETE (set by master)
  // - INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING (set by master)
  //
  // This changes how we treat indexes_to_foo:
  //
  // - indexes_to_update: used for moving from WRITE_AND_DELETE to DO_BACKFILL.
  // - indexes_to_delete is impossible to be nonempty, and, in the future, when we do use
  //   INDEX_PERM_INDEX_UNUSED, we want to use some other delete trigger that makes sure no
  //   transactions are left using the index.  Prepare for that by doing nothing when nonempty.
  // - indexes_to_backfill: used to launch StartBackfillingData once the index ready to backfill.

  if (!indexes_to_update.empty()) {
    VLOG(1) << "Updating index permissions for " << yb::ToString(indexes_to_update) << " on "
            << indexed_table->ToString();
    Result<bool> permissions_updated = VERIFY_RESULT(UpdateIndexPermission(
        catalog_manager, indexed_table, indexes_to_update, epoch, current_version));

    if (!permissions_updated.ok()) {
      LOG(WARNING) << "Could not update index permissions."
                   << " Possible that the master-leader has changed, or a race "
                   << "with another thread trying to launch next version: "
                   << permissions_updated.ToString();
    }

    if (permissions_updated.ok() && *permissions_updated) {
      VLOG(1) << "Sending alter table request with updated permissions";
      // Store the requester transaction so StartBackfillingData can retrieve it when the
      // permission change reaches DO_BACKFILL and the second call launches backfill.
      // Store current_version+1 (the new version after this permission update)
      // so TakePendingBackfillRequesterTransaction can verify the transaction
      // belongs to this exact backfill attempt and not a stale one.
      if (requester_transaction) {
        indexed_table->SetPendingBackfillRequesterTransaction(
            std::move(requester_transaction), current_version + 1);
      }
      RETURN_NOT_OK(catalog_manager->SendAlterTableRequest(indexed_table, epoch));
      return Status::OK();
    }
  }

  if (!indexes_to_delete.empty()) {
    const auto& index_info_to_update = indexes_to_delete[0];
    VLOG(3) << "Deleting the index and the entry in the indexed table for "
            << yb::ToString(index_info_to_update);
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_id(index_info_to_update.table_id());
    req.set_is_index_table(true);
    RETURN_NOT_OK(catalog_manager->DeleteTableInternal(&req, &resp, nullptr, epoch));
    return Status::OK();
  }

  if (!indexes_to_backfill.empty()) {
    VLOG(3) << "Backfilling " << yb::ToString(indexes_to_backfill)
            << (deferred_indexes.empty()
                 ? ""
                 : yb::Format(" along with deferred indexes $0",
                              yb::ToString(deferred_indexes)));
    for (auto& deferred_idx : deferred_indexes) {
      indexes_to_backfill.emplace_back(deferred_idx);
    }
    WARN_NOT_OK(
        StartBackfillingData(
            catalog_manager, indexed_table.get(), indexes_to_backfill, current_version, epoch,
            std::move(requester_transaction)),
        yb::Format("Could not launch backfill for $0", indexed_table->ToString()));
  }

  return Status::OK();
}

// -----------------------------------------------------------------------------------------------
// BackfillTableJob
// -----------------------------------------------------------------------------------------------
std::string BackfillTableJob::description() const {
  const std::shared_ptr<BackfillTable> retain_bt = backfill_table_;
  auto curr_state = state();
  if (!IsStateTerminal(curr_state) && retain_bt) {
    return retain_bt->description();
  } else if (curr_state == MonitoredTaskState::kFailed) {
    return Format("Backfilling $0 Failed", requested_index_names_);
  } else if (curr_state == MonitoredTaskState::kAborted) {
    return Format("Backfilling $0 Aborted", requested_index_names_);
  } else {
    DCHECK(curr_state == MonitoredTaskState::kComplete);
    return Format("Backfilling $0 Done", requested_index_names_);
  }
}

MonitoredTaskState BackfillTableJob::AbortAndReturnPrevState(
    const Status& status, bool call_task_finisher) {
  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state,
                                       MonitoredTaskState::kAborted)) {
      MarkDone();
      return old_state;
    }
    old_state = state();
  }
  return old_state;
}

void BackfillTableJob::SetState(MonitoredTaskState new_state) {
  auto old_state = state();
  if (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state, new_state) && IsStateTerminal(new_state)) {
      MarkDone();
    }
  }
}

void BackfillTableJob::MarkDone() {
  completion_timestamp_ = MonoTime::Now();
  if (backfill_table_) {
    backfill_table_->table()->RemoveTask(shared_from_this());
    backfill_table_.reset();
  }
}

// -----------------------------------------------------------------------------------------------
// BackfillTable
// -----------------------------------------------------------------------------------------------

namespace {

std::unordered_set<TableId> IndexIdsFromInfos(const std::vector<IndexInfoPB>& indexes) {
  std::unordered_set<TableId> idx_ids;
  for (const auto& idx_info : indexes) {
    idx_ids.insert(idx_info.table_id());
  }
  return idx_ids;
}

std::string RetrieveIndexNames(CatalogManager* mgr,
                               const std::unordered_set<std::string>& index_ids) {
  std::ostringstream out;
  out << "{ ";
  bool first = true;
  for (const auto& index_id : index_ids) {
    const auto table_info = mgr->GetTableInfo(index_id);
    if (!table_info) {
      LOG(WARNING) << "No table info can be found with index table id " << index_id;
      continue;
    }
    if (!first) {
      out << ", ";
    }
    first = false;

    out << table_info->name();
  }
  out << " }";
  return out.str();
}

}  // namespace

BackfillTable::BackfillTable(
    Master* master, ThreadPool* callback_pool, const scoped_refptr<TableInfo>& indexed_table,
    std::vector<IndexInfoPB> indexes, const scoped_refptr<NamespaceInfo>& ns_info,
    LeaderEpoch epoch, std::optional<TransactionMetadata> requester_transaction)
    : master_(master),
      callback_pool_(callback_pool),
      indexed_table_(indexed_table),
      index_infos_(indexes),
      requested_index_ids_(IndexIdsFromInfos(indexes)),
      requested_index_names_(
          RetrieveIndexNames(master->catalog_manager_impl(), requested_index_ids_)),
      ns_info_(ns_info),
      epoch_(std::move(epoch)),
      wait_state_(ash::WaitStateInfo::CreateIfAshIsEnabled<ash::WaitStateInfo>()),
      requester_transaction_(std::move(requester_transaction)) {
  if (wait_state_) {
    if (const auto& current_state = ash::WaitStateInfo::CurrentWaitState()) {
      wait_state_->UpdateMetadata(current_state->metadata());
    }
    wait_state_->UpdateAuxInfo({.method = "BackfillIndex"});
  }
  auto l = indexed_table_->LockForRead();
  schema_version_ = indexed_table_->metadata().state().pb.version();

  const auto& pb = indexed_table_->metadata().state().pb;
  // The uniqueness-check mode and the gating decision are immutable per job: reuse the
  // persisted values when resuming an existing job (missing means CHECK_ALL/observational);
  // select them only for a brand-new job. Launch() persists the selection together with the
  // job, so failover, retries, and runtime flag changes cannot reinterpret an active job.
  if (pb.backfill_jobs_size() > 0) {
    unique_index_backfill_mode_ = pb.backfill_jobs(0).unique_index_backfill_mode();
    verification_gates_publication_ = pb.backfill_jobs(0).verification_gates_publication();
  } else {
    unique_index_backfill_mode_ = SelectUniqueIndexBackfillMode();
    verification_gates_publication_ =
        FLAGS_ysql_index_backfill_fail_closed_verification &&
        unique_index_backfill_mode_ == UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_SKIP_ALL &&
        pb.table_type() == TableType::PGSQL_TABLE_TYPE;
  }
  if (pb.backfill_jobs_size() > 0 && pb.backfill_jobs(0).has_backfilling_timestamp() &&
      read_time_for_backfill_.FromUint64(pb.backfill_jobs(0).backfilling_timestamp()).ok()) {
    DCHECK(pb.backfill_jobs_size() == 1) << "Expect only 1 outstanding backfill job";
    DCHECK(implicit_cast<size_t>(pb.backfill_jobs(0).indexes_size()) == index_infos_.size())
        << "Expect to use the same set of indexes.";
    timestamp_chosen_.store(true, std::memory_order_release);
    VLOG_WITH_PREFIX(1) << "Will be using " << read_time_for_backfill_
                        << " for backfill";
  } else {
    read_time_for_backfill_ = HybridTime::kInvalid;
    timestamp_chosen_.store(false, std::memory_order_release);
  }
  state_.store(State::kRunning, std::memory_order_release);
}

const std::unordered_set<TableId> BackfillTable::indexes_to_build() const {
  std::unordered_set<TableId> indexes_to_build;
  if (PREDICT_FALSE(FLAGS_TEST_simulate_empty_indexes_during_backfill)) {
    LOG_WITH_PREFIX(WARNING) << "Simulating empty indexes.";
    return indexes_to_build;
  }
  {
    auto l = indexed_table_->LockForRead();
    const auto& indexed_table_pb = l.data().pb;
    if (indexed_table_pb.backfill_jobs_size() == 0) {
      // Some other task already marked the backfill job as done.
      LOG_WITH_PREFIX(INFO) << "No backfill jobs found for table " << indexed_table_->ToString()
                            << ". Cannot determine indexes to build.";
      return {};
    }
    DCHECK(indexed_table_pb.backfill_jobs_size() == 1) << "For now we only expect to have up to 1 "
                                                          "outstanding backfill job.";
    for (const auto& kv_pair : indexed_table_pb.backfill_jobs(0).backfill_state()) {
      if (kv_pair.second == BackfillJobPB::IN_PROGRESS) {
        indexes_to_build.insert(kv_pair.first);
      }
    }

    if (indexes_to_build.empty()) {
      std::vector<std::string> details;
      const auto& backfill_state = indexed_table_pb.backfill_jobs(0).backfill_state();
      std::transform(
          backfill_state.begin(), backfill_state.end(), std::back_inserter(details),
          [](const auto& kv_pair) {
            return Substitute("$0: $1", kv_pair.first, BackfillJobPB::State_Name(kv_pair.second));
          });
      LOG_WITH_PREFIX(WARNING) << "No indexes to build. backfill_state: " << yb::ToString(details);
    }
  }
  return indexes_to_build;
}

Status BackfillTable::Launch() {
  backfill_job_ = std::make_shared<BackfillTableJob>(shared_from_this());
  backfill_job_->SetState(MonitoredTaskState::kRunning);
  table()->AddTask(backfill_job_);
  master_->catalog_manager_impl()->jobs_tracker_->AddTask(backfill_job_);

  {
    auto l = indexed_table_->LockForWrite();
    if (l.data().pb.backfill_jobs_size() == 0) {
      auto* backfill_job = l.mutable_data()->pb.add_backfill_jobs();
      for (const auto& idx_info : index_infos_) {
        backfill_job->add_indexes()->CopyFrom(idx_info);
        backfill_job->mutable_backfill_state()->insert(
            {idx_info.table_id(), BackfillJobPB::IN_PROGRESS});
      }
      backfill_job->set_unique_index_backfill_mode(unique_index_backfill_mode_);
      backfill_job->set_verification_gates_publication(verification_gates_publication_);
      LOG_WITH_PREFIX(INFO) << "Selected unique-index backfill mode "
                            << UniqueIndexBackfillMode_Name(unique_index_backfill_mode_)
                            << (verification_gates_publication_
                                    ? " with fail-closed verification"
                                    : "")
                            << " for backfill job";
      RETURN_NOT_OK_PREPEND(
          master_->catalog_manager_impl()->sys_catalog_->Upsert(
              epoch_, indexed_table_),
          "Failed to persist backfill jobs. Abandoning launch.");
      l.Commit();
    }
  }

  // This must be a shared pointer and not just 'this' so we do not accidentally clean up
  // BackfillTable when the last shared pointer to BackfillTable is deleted in Abort() (when the
  // backfill job is deleted).
  Status status = threadpool()->SubmitFunc(
      std::bind(&BackfillTable::LaunchBackfillOrAbort, this->shared_from_this()));
  if (!status.ok()) {
    RETURN_NOT_OK_PREPEND(Abort(), "Failed to run LaunchBackfill.");
    return status;
  }
  return Status::OK();
}

void BackfillTable::LaunchBackfillOrAbort() {
  ADOPT_WAIT_STATE(wait_state_);
  Status status = WaitForTabletSplitting();
  if (!status.ok()) {
    LOG(WARNING) << status;
    WARN_NOT_OK(Abort(), "Failed to abort backfill after backfill failed.");
    return;
  }

  status = DoLaunchBackfill();
  if (!status.ok()) {
    LOG(WARNING) << status;
    WARN_NOT_OK(Abort(), "Failed to abort backfill after backfill failed.");
  }
}

Status BackfillTable::LaunchComputeSafeTimeForRead() {
  RSTATUS_DCHECK(!timestamp_chosen(), IllegalState, "Backfill timestamp already set");
  TEST_PAUSE_IF_FLAG(TEST_pause_compute_safe_time_for_backfill_read);

  std::vector<TableId> index_table_ids;
  std::transform(
      index_infos_.begin(), index_infos_.end(), std::back_inserter(index_table_ids),
      [](const IndexInfoPB& idx_info) { return idx_info.table_id(); });

  auto xcluster_decision =
      VERIFY_RESULT(master_->xcluster_manager()->TryGetXClusterInfoForIndexBackfill(
          index_table_ids, indexed_table_, epoch()));

  switch (xcluster_decision.kind) {
    case XClusterBackfillDecision::Kind::kRunLocalAtHybridTime:
      return SetSafeTimeAndStartBackfill(xcluster_decision.hybrid_time);
    case XClusterBackfillDecision::Kind::kDeferToReplicatedBackfill:
      return FinalizeReplicatedIndexBackfill(xcluster_decision.hybrid_time);
    case XClusterBackfillDecision::Kind::kRunLocalWithTabletSafeTime:
      break;
  }

  {
    auto l = indexed_table_->LockForRead();
    if (l.data().pb.has_transaction() && l.data().pb.transaction().has_using_table_locks() &&
        l.data().pb.transaction().using_table_locks()) {
      using_table_locks_ = true;
    }
  }
  auto tablets = VERIFY_RESULT(indexed_table_->GetTablets());
  num_tablets_.store(tablets.size(), std::memory_order_release);
  tablets_pending_.store(tablets.size(), std::memory_order_release);
  auto min_cutoff = master()->clock()->Now();
  for (const auto& tablet : tablets) {
    auto get_safetime =
      std::make_shared<GetSafeTimeForTablet>(shared_from_this(), tablet, min_cutoff, epoch());
    RETURN_NOT_OK(get_safetime->Launch());
  }
  return Status::OK();
}

std::string BackfillTable::LogPrefix() const {
  return Format("Backfill Index Table(s) $0 ", requested_index_names_);
}

std::string BackfillTable::description() const {
  auto num_pending = tablets_pending_.load(std::memory_order_acquire);
  auto num_tablets = num_tablets_.load(std::memory_order_acquire);
  auto l = indexed_table_->LockForRead();
  const auto& indexed_table_pb = l.data().pb;
  uint64_t num_rows_read_from_table_for_backfill = 0;
  if (indexed_table_pb.backfill_jobs_size() > 0) {
    num_rows_read_from_table_for_backfill =
        indexed_table_pb.backfill_jobs(0).num_rows_read_from_table_for_backfill();
  }

  return Format(
      "Backfill Index Table(s) $0 : $1", requested_index_names_,
      (timestamp_chosen()
           ? (done() ? Format("Backfill $0/$1 tablets done", num_pending, num_tablets)
                     : Format(
                           "Backfilling $0/$1 tablets with $2 rows done", num_pending, num_tablets,
                           num_rows_read_from_table_for_backfill))
           : Format("Waiting to GetSafeTime from $0/$1 tablets", num_pending, num_tablets)));
}

const std::string BackfillTable::GetNamespaceName() const {
  return ns_info_->name();
}

Status BackfillTable::UpdateRowsProcessedForIndexTable(
    const uint64_t num_rows_read_from_table_for_backfill,
    const std::unordered_map<TableId, double>& num_rows_backfilled_in_index) {
  auto l = indexed_table_->LockForWrite();

  if (l.data().pb.backfill_jobs_size() == 0) {
    // Some other task already marked the backfill job as done.
    return Status::OK();
  }

  // This is consistent with logic assuming that we have only one backfill job in queue
  // We might in the future change this to a for loop to account for multiple backfill jobs
  auto* backfill_job_pb = l.mutable_data()->pb.mutable_backfill_jobs(0);
  uint64_t total_num_rows_read_from_table_for_backfill =
      backfill_job_pb->num_rows_read_from_table_for_backfill() +
      num_rows_read_from_table_for_backfill;
  backfill_job_pb->set_num_rows_read_from_table_for_backfill(
      total_num_rows_read_from_table_for_backfill);
  for (const auto& [index_id, num_rows_backfilled] : num_rows_backfilled_in_index) {
    auto* backfill_job_num_rows_backfilled_pb =
        backfill_job_pb->mutable_num_rows_backfilled_in_index();
    if (backfill_job_num_rows_backfilled_pb->find(index_id) ==
        backfill_job_num_rows_backfilled_pb->end()) {
      (*backfill_job_num_rows_backfilled_pb)[index_id] = 0.0;
    }
    (*backfill_job_num_rows_backfilled_pb)[index_id] += num_rows_backfilled;
  }
  VLOG(2) << "Updated backfill task to having processed " << num_rows_read_from_table_for_backfill
          << " more rows. Total rows processed is: " <<
          backfill_job_pb->num_rows_read_from_table_for_backfill();

  RETURN_NOT_OK(master_->catalog_manager_impl()->sys_catalog_->Upsert(
      epoch_, indexed_table_));
  l.Commit();
  return Status::OK();
}

Status BackfillTable::UpdateSafeTime(const Status& s, HybridTime ht) {
  if (!s.ok()) {
    // Move on to ABORTED permission.
    LOG_WITH_PREFIX(DFATAL)
        << "Failed backfill. Could not compute safe time for "
        << AsString(indexed_table_) << " " << s;
    if (!timestamp_chosen_.exchange(true)) {
      RETURN_NOT_OK(Abort());
    }
    return Status::OK();
  }

  // Need to guard this.
  HybridTime read_timestamp;
  {
    std::lock_guard l(mutex_);
    VLOG(2) << "Updating read_time_for_backfill_ to max{ "
            << read_time_for_backfill_.ToString() << ", " << ht.ToString()
            << " }.";
    read_time_for_backfill_.MakeAtLeast(ht);
    read_timestamp = read_time_for_backfill_;
  }

  // If OK then move on to doing backfill.
  if (!timestamp_chosen() && --tablets_pending_ == 0) {
    LOG_WITH_PREFIX(INFO) << "Completed fetching SafeTime for the table "
                          << yb::ToString(indexed_table_) << " will be using "
                          << read_timestamp.ToString();
    return PersistSafeTimeAndStartBackfill();
  }
  return Status::OK();
}

Status BackfillTable::PersistSafeTimeAndStartBackfill() {
  {
    std::lock_guard mutex_lock(mutex_);
    auto l = indexed_table_->LockForWrite();
    DCHECK_EQ(l.mutable_data()->pb.backfill_jobs_size(), 1);
    auto* backfill_job = l.mutable_data()->pb.mutable_backfill_jobs(0);
    backfill_job->set_backfilling_timestamp(read_time_for_backfill_.ToUint64());
    RETURN_NOT_OK_PREPEND(
        master_->catalog_manager_impl()->sys_catalog_->Upsert(
            epoch_, indexed_table_),
        "Failed to persist backfilling timestamp. Abandoning.");
    l.Commit();
    VLOG_WITH_PREFIX(2) << "Saved " << read_time_for_backfill_ << " as backfilling_timestamp";
  }

  timestamp_chosen_.store(true, std::memory_order_release);
  Status backfill_status = DoBackfill();
  if (!backfill_status.ok()) {
    // Mark indexes as failed so CREATE INDEX will stop waiting and return.
    RETURN_NOT_OK(Abort());
    return backfill_status;
  }
  return Status::OK();
}

Status BackfillTable::SetSafeTimeAndStartBackfill(const HybridTime& read_time) {
  RSTATUS_DCHECK(!timestamp_chosen(), IllegalState, "Backfill timestamp already set");
  {
    std::lock_guard mutex_lock(mutex_);
    // We only expect the time to be set once.
    DCHECK(read_time_for_backfill_.is_special());
    read_time_for_backfill_.MakeAtLeast(read_time);
  }

  return PersistSafeTimeAndStartBackfill();
}

Status BackfillTable::FinalizeReplicatedIndexBackfill(HybridTime source_backfill_ht) {
  RSTATUS_DCHECK(
      source_backfill_ht.is_valid() && !source_backfill_ht.is_special(), InvalidArgument,
      "Invalid source backfill hybrid time for replicated xCluster backfill");

  // At this point, the source's backfill writes are already applied on the target:
  // As part of create index, we already wait for AddTableToXClusterTargetTask to wait for
  // safe time to reach beyond the source backfill commit time, so here we can just mark
  // backfill as done.
  num_tablets_.store(0, std::memory_order_release);
  tablets_pending_.store(0, std::memory_order_release);

  const auto namespace_id = indexed_table_->namespace_id();
  auto& xcluster_manager = *master_->xcluster_manager();

  auto safe_time_result = xcluster_manager.GetXClusterSafeTimeForNamespace(
      namespace_id, XClusterSafeTimeFilter::DDL_QUEUE);
  if (!safe_time_result.ok() && !safe_time_result.status().IsNotFound()) {
    return safe_time_result.status();
  }
  SCHECK(
      safe_time_result.ok() && safe_time_result->is_valid() && !safe_time_result->is_special(),
      TryAgain, "xCluster safe time for namespace $0 is not available yet", namespace_id);
  const auto& safe_time = *safe_time_result;
  RSTATUS_DCHECK(
      safe_time >= source_backfill_ht, IllegalState,
      Format(
          "xCluster safe time $0 has not reached source backfill ht $1 for replicated "
          "index backfill. AddTableToXClusterTargetTask should have ensured this before the index "
          "started backfilling",
          safe_time, source_backfill_ht));

  LOG_WITH_PREFIX(INFO) << "Completed backfilling the index table, "
                        << "finalizing replicated index backfill without local backfill.";

  RETURN_NOT_OK_PREPEND(
      MarkAllIndexesAsSuccess(),
      "Failed to mark indexes as successfully backfilled (replicated backfill path)");
  RETURN_NOT_OK_PREPEND(
      UpdateIndexPermissionsForIndexes(),
      "Failed to update index permissions (replicated backfill path)");
  state_.store(State::kSuccess, std::memory_order_release);
  return Status::OK();
}

Status BackfillTable::WaitForTabletSplitting() {
  auto& tablet_split_manager = master_->tablet_split_manager();
  tablet_split_manager.DisableSplittingForBackfillingTable(indexed_table_->id());
  CoarseTimePoint deadline = CoarseMonoClock::Now() +
                             FLAGS_index_backfill_tablet_split_completion_timeout_sec * 1s;
  while (!tablet_split_manager.IsTabletSplittingComplete(*indexed_table_,
                                                          false /* wait_for_parent_deletion */,
                                                          deadline)) {
    if (CoarseMonoClock::Now() > deadline) {
      return STATUS(TimedOut, "Tablet splitting did not complete after being disabled; cannot "
                              "safely backfill the index.");
    }
    SleepFor(FLAGS_index_backfill_tablet_split_completion_poll_freq_ms * 1ms * kTimeMultiplier);
  }

  RETURN_NOT_OK(indexed_table_->CheckAllActiveTabletsRunning());
  return Status::OK();
}

Status BackfillTable::DoLaunchBackfill() {
  if (!timestamp_chosen()) {
    RETURN_NOT_OK(LaunchComputeSafeTimeForRead());
  } else {
    RETURN_NOT_OK(DoBackfill());
  }
  return Status::OK();
}

Status BackfillTable::DoBackfill() {
  StartRequesterLivenessMonitor();
  while (FLAGS_TEST_block_do_backfill) {
    constexpr auto kSpinWait = 100ms;
    LOG(INFO) << Format("Blocking $0 for $1", __func__, kSpinWait);
    SleepFor(kSpinWait);
  }
  if (VLOG_IS_ON(1)) {
    VLOG_WITH_PREFIX(1) << "starting backfill with timestamp: " << read_time_for_backfill();
  }

  if (unique_index_backfill_mode() == UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_SKIP_ALL &&
      !ordering_generation_activated_.exchange(true)) {
    // SKIP_ALL writes are marked (Raft-index write IDs), which every index tablet only accepts
    // under an active ordering generation -- activate before the first chunk. The job continues
    // into LaunchBackfillTablets through OrderingGenerationUpdateDone once every tablet acks.
    // On failover resume this re-runs: re-activation is idempotent (the base moves up).
    return LaunchOrderingGenerationActivation();
  }
  return LaunchBackfillTablets();
}

Status BackfillTable::LaunchBackfillTablets() {
  if (indexes_to_build().empty()) {
    // Post-success resume: a master failover after every index reached SUCCESS but before the
    // terminal funnel (e.g. during the shadow verification phase) re-drives the persisted job
    // with nothing left to backfill. Chunks would carry empty index sets and spin; jump to
    // the completion path instead, which re-enters shadow verification (resuming its
    // persisted window) and the funnel.
    LOG_WITH_PREFIX(INFO)
        << "All job indexes already backfilled; resuming the completion phase";
    State expected = State::kRunning;
    state_.compare_exchange_strong(expected, State::kSuccess, std::memory_order_acq_rel);
    RETURN_NOT_OK_PREPEND(
        MarkAllIndexesAsSuccess(), "Failed to mark indexes as successfully backfilled.");
    return LaunchShadowVerificationOrFinish();
  }
  auto tablets = VERIFY_RESULT(indexed_table_->GetTablets());
  num_tablets_.store(tablets.size(), std::memory_order_release);
  tablets_pending_.store(tablets.size(), std::memory_order_release);
  for (auto& tablet : tablets) {
    auto backfill_tablet = std::make_shared<BackfillTablet>(shared_from_this(), std::move(tablet));
    RETURN_NOT_OK(backfill_tablet->Launch());
  }
  return Status::OK();
}

Result<std::vector<scoped_refptr<TableInfo>>> BackfillTable::GetUniqueIndexTables(
    RestrictToIndexesToBuild restrict_to_indexes_to_build) const {
  const auto to_build = indexes_to_build();
  std::vector<scoped_refptr<TableInfo>> tables;
  for (const auto& index_info : index_infos_) {
    if (!index_info.is_unique()) {
      continue;
    }
    if (restrict_to_indexes_to_build && to_build.count(index_info.table_id()) == 0) {
      continue;
    }
    auto res = master_->catalog_manager()->FindTableById(index_info.table_id());
    if (!res && res.status().IsNotFound()) {
      // Concurrent DROP INDEX; the job will fail through the missing-IndexInfoPB path.
      LOG_WITH_PREFIX(WARNING) << "Index " << index_info.table_id() << " was not found; "
                               << "skipping for ordering-generation update: " << res.status();
      continue;
    }
    tables.push_back(VERIFY_RESULT_PREPEND(
        std::move(res), Format("Could not find the index table $0", index_info.table_id())));
  }
  return tables;
}

Status BackfillTable::LaunchOrderingGenerationActivation() {
  if (indexed_table_->GetTableType() != TableType::PGSQL_TABLE_TYPE) {
    // The SKIP_ALL write path is PGSQL-only (the backfill request mode rides the YSQL chunk
    // requests; YCQL backfill writes are never marked), so generations would only fence
    // splits without protecting anything. Reachable today only via the TEST mode override.
    return LaunchBackfillTablets();
  }
  auto index_tables = VERIFY_RESULT(GetUniqueIndexTables(RestrictToIndexesToBuild::kTrue));
  if (index_tables.empty()) {
    return LaunchBackfillTablets();
  }

  // Fence and drain index-table splitting before activating. The tablet-side fences (the
  // pending-split rejection of CHANGE_METADATA_OP and, once active, the generation split
  // fence) close the append-time races; this master-side drain removes the split/activation
  // TOCTOU window entirely under a single master leader, and the persisted fence in the
  // tablet-split manager (backfill-job state) keeps splits excluded across failover.
  auto& tablet_split_manager = master_->tablet_split_manager();
  const CoarseTimePoint deadline =
      CoarseMonoClock::Now() +
      FLAGS_index_backfill_tablet_split_completion_timeout_sec * 1s * kTimeMultiplier;
  for (const auto& index_table : index_tables) {
    tablet_split_manager.DisableSplittingForBackfillingTable(index_table->id());
    while (!tablet_split_manager.IsTabletSplittingComplete(
        *index_table, false /* wait_for_parent_deletion */, deadline)) {
      if (CoarseMonoClock::Now() > deadline) {
        return STATUS(
            TimedOut,
            "Index-tablet splitting did not complete after being disabled; cannot safely "
            "activate the ordering generation.");
      }
      SleepFor(FLAGS_index_backfill_tablet_split_completion_poll_freq_ms * 1ms * kTimeMultiplier);
    }
  }

  std::vector<std::pair<TabletInfoPtr, TableId>> tablets;
  for (const auto& index_table : index_tables) {
    for (auto& tablet : VERIFY_RESULT(index_table->GetTablets())) {
      tablets.emplace_back(std::move(tablet), index_table->id());
    }
  }
  if (tablets.empty()) {
    return LaunchBackfillTablets();
  }

  LOG_WITH_PREFIX(INFO) << "Activating index-backfill ordering generation on " << tablets.size()
                        << " index tablet(s) before launching backfill chunks";
  activation_tablets_pending_.store(tablets.size(), std::memory_order_release);
  for (auto& [tablet, index_table_id] : tablets) {
    auto task = std::make_shared<UpdateOrderingGenerationForTablet>(
        shared_from_this(), tablet, index_table_id, tablet::ActivateGeneration::kTrue,
        NotifyBackfillTable::kTrue, epoch_);
    RETURN_NOT_OK(task->Launch());
  }
  return Status::OK();
}

void BackfillTable::OrderingGenerationUpdateDone(
    const Status& status, const TabletId& tablet_id) {
  if (done()) {
    return;
  }
  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Ordering-generation activation failed for tablet " << tablet_id
                             << ": " << status << "; aborting backfill";
    WARN_NOT_OK(Abort(), "Failed to abort backfill after activation failure");
    return;
  }
  if (--activation_tablets_pending_ == 0) {
    LOG_WITH_PREFIX(INFO) << "Ordering generation active on all index tablets; "
                          << "launching backfill chunks";
    Status s = LaunchBackfillTablets();
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Failed to launch backfill after activation: " << s;
      WARN_NOT_OK(Abort(), "Failed to abort backfill");
    }
  }
}

Status BackfillTable::SendRpcToReleaseOrderingGenerations() {
  // Release on every unique index of the job, built or not: activation may have partially
  // succeeded before a failure. Fire-and-forget -- the tserver metadata validator and master
  // reload reconciliation converge any straggler.
  auto index_tables = VERIFY_RESULT(GetUniqueIndexTables(RestrictToIndexesToBuild::kFalse));
  for (const auto& index_table : index_tables) {
    for (const auto& tablet : VERIFY_RESULT(index_table->GetTablets())) {
      auto task = std::make_shared<UpdateOrderingGenerationForTablet>(
          shared_from_this(), tablet, index_table->id(), tablet::ActivateGeneration::kFalse,
          NotifyBackfillTable::kFalse, epoch_);
      WARN_NOT_OK(task->Launch(), "Failed to send ordering-generation release");
    }
    master_->tablet_split_manager().ReenableSplittingForBackfillingTable(index_table->id());
  }
  return Status::OK();
}

Status BackfillTable::LaunchShadowVerificationOrFinish() {
  if ((!FLAGS_ysql_index_backfill_shadow_verification && !verification_gates_publication()) ||
      unique_index_backfill_mode() != UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_SKIP_ALL ||
      indexed_table_->GetTableType() != TableType::PGSQL_TABLE_TYPE) {
    return FinishShadowVerification();
  }
  // Not restricted to indexes_to_build(): that filters to IN_PROGRESS backfill states, and
  // this phase runs after MarkAllIndexesAsSuccess. Verification targets the job's unique
  // indexes as built.
  //
  // The tserver metadata validator's generation-release backstop cannot fire during this
  // phase for two independent reasons: index permissions stay at DO_BACKFILL (never a
  // terminal backfill status) until FinishShadowVerification runs the terminal funnel, and
  // GetBackfillStatus additionally reports BACKFILL_UNKNOWN outright while this index's
  // verification state is VERIFY_IN_PROGRESS (the structural guard in
  // CatalogManager::GetBackfillStatus).
  auto index_tables_result = GetUniqueIndexTables(RestrictToIndexesToBuild::kFalse);
  if (PREDICT_FALSE(FLAGS_TEST_fail_unique_index_verification_resolution)) {
    index_tables_result = STATUS(InternalError, "Injected verification resolution failure");
  }
  if (!index_tables_result.ok()) {
    ShadowVerificationPhaseFailed(index_tables_result.status(), "resolve index tables");
    return Status::OK();
  }
  auto index_tables = std::move(*index_tables_result);
  if (index_tables.empty()) {
    return FinishShadowVerification();
  }
  {
    std::lock_guard l(mutex_);
    shadow_verification_.remaining_indexes = std::move(index_tables);
  }
  auto status = StartShadowVerificationForNextIndex();
  if (!status.ok()) {
    ShadowVerificationPhaseFailed(status, "start verification");
  }
  return Status::OK();
}

Status BackfillTable::StartShadowVerificationForNextIndex() {
  scoped_refptr<TableInfo> index_table;
  {
    std::lock_guard l(mutex_);
    auto& sv = shadow_verification_;
    if (sv.remaining_indexes.empty()) {
      sv.current_index = nullptr;
      index_table = nullptr;
    } else {
      index_table = sv.remaining_indexes.back();
      sv.remaining_indexes.pop_back();
      sv.current_index = index_table;
      sv.pending_tablets.clear();
      sv.in_flight = 0;
      sv.terminal = false;
    }
  }
  if (!index_table) {
    return FinishShadowVerification();
  }

  // The window's upper bound is chosen ONCE, from cluster hybrid time (never wall clock),
  // and persisted: retries and failover resume verify the same window. Tablets already
  // confirmed clean by a previous incarnation are not re-scanned.
  HybridTime verify_upper_ht;
  std::unordered_set<TabletId> clean_tablets;
  bool resumed = false;
  const auto now = master_->clock()->Now();
  RETURN_NOT_OK(MutateVerificationState(
      index_table->id(),
      [now, &verify_upper_ht, &clean_tablets, &resumed](UniqueIndexVerificationStatePB* state) {
        if (state->state() == UniqueIndexVerificationStatePB::VERIFY_NONE) {
          state->set_state(UniqueIndexVerificationStatePB::VERIFY_IN_PROGRESS);
          state->set_verify_upper_ht(now.ToUint64());
        } else {
          resumed = true;
        }
        verify_upper_ht = HybridTime(state->verify_upper_ht());
        for (const auto& tablet_id : state->clean_tablet_ids()) {
          clean_tablets.insert(tablet_id);
        }
      }));
  LOG_WITH_PREFIX(INFO) << (resumed ? "Resuming" : "Starting") << " shadow verification for "
                        << "unique index " << index_table->id() << ", window upper "
                        << verify_upper_ht << ", " << clean_tablets.size()
                        << " tablet(s) already clean";

  auto tablets = VERIFY_RESULT(index_table->GetTablets());
  if (tablets.empty()) {
    // An empty tablet *set* is not evidence of uniqueness -- there was nothing to scan.
    RETURN_NOT_OK(RecordShadowVerificationOutcome(
        UniqueIndexVerificationStatePB::VERIFY_INCONCLUSIVE, "index has no tablets to verify"));
    return StartShadowVerificationForNextIndex();
  }

  std::vector<TabletInfoPtr> to_launch;
  {
    std::lock_guard l(mutex_);
    auto& sv = shadow_verification_;
    sv.verify_upper_ht = verify_upper_ht;
    for (auto& tablet : tablets) {
      if (clean_tablets.count(tablet->tablet_id()) == 0) {
        sv.pending_tablets.push_back(tablet);
      }
    }
    const size_t bound = std::max<size_t>(
        1, FLAGS_index_backfill_shadow_verification_max_concurrent_tablets);
    while (!sv.pending_tablets.empty() && sv.in_flight < bound) {
      to_launch.push_back(sv.pending_tablets.front());
      sv.pending_tablets.pop_front();
      ++sv.in_flight;
    }
    if (to_launch.empty()) {
      sv.terminal = true;  // Everything was already clean.
    }
  }
  if (to_launch.empty()) {
    RETURN_NOT_OK(RecordShadowVerificationOutcome(
        UniqueIndexVerificationStatePB::VERIFY_CLEAN, std::string()));
    return StartShadowVerificationForNextIndex();
  }
  for (const auto& tablet : to_launch) {
    RETURN_NOT_OK(LaunchShadowVerificationTablet(tablet, std::string()));
  }
  return Status::OK();
}

Status BackfillTable::LaunchShadowVerificationTablet(
    const TabletInfoPtr& tablet, const std::string& start_key) {
  TableId index_table_id;
  HybridTime verify_upper_ht;
  {
    std::lock_guard l(mutex_);
    index_table_id = shadow_verification_.current_index->id();
    verify_upper_ht = shadow_verification_.verify_upper_ht;
  }
  auto task = std::make_shared<VerifyUniqueIndexForTablet>(
      shared_from_this(), tablet, index_table_id, read_time_for_backfill(), verify_upper_ht,
      start_key, epoch_);
  return task->Launch();
}

void BackfillTable::ShadowVerificationTabletDone(
    const TabletInfoPtr& tablet, const Status& status,
    const tserver::VerifyUniqueIndexTabletResponsePB& resp) {
  // The shadow phase runs entirely in the kSuccess state (Done() transitions before
  // launching verification), so done() would swallow every callback; only a failed/aborted
  // job stops the phase.
  if (state_.load(std::memory_order_acquire) != State::kSuccess) {
    return;
  }

  const bool clean = status.ok() &&
                     resp.outcome() == tserver::VerifyUniqueIndexTabletResponsePB::CLEAN;

  // Single-winner protocol: the terminal check and the launch-next / index-done decision are
  // one critical section, so a CLEAN callback racing a short-circuiting VIOLATION can neither
  // overwrite the recorded outcome nor double-advance to the next index. Exactly one callback
  // per index performs a kIndexClean or kTerminalOutcome action.
  enum class Action { kIgnore, kResume, kLaunchNext, kIndexClean, kTerminalOutcome };
  Action action = Action::kIgnore;
  TabletInfoPtr next;
  TableId index_table_id;
  {
    std::lock_guard l(mutex_);
    auto& sv = shadow_verification_;
    if (sv.terminal || !sv.current_index) {
      return;  // A winner already recorded this index's outcome; late responses are ignored.
    }
    index_table_id = sv.current_index->id();
    if (clean && resp.has_resume_key()) {
      action = Action::kResume;  // Pagination: same tablet continues; join counts untouched.
    } else if (clean) {
      --sv.in_flight;
      if (!sv.pending_tablets.empty()) {
        next = sv.pending_tablets.front();
        sv.pending_tablets.pop_front();
        ++sv.in_flight;
        action = Action::kLaunchNext;
      } else if (sv.in_flight == 0) {
        sv.terminal = true;
        action = Action::kIndexClean;
      }  // else: other tablets still in flight; this callback only contributes its clean id.
    } else {
      sv.terminal = true;
      sv.pending_tablets.clear();
      action = Action::kTerminalOutcome;
    }
  }

  switch (action) {
    case Action::kResume: {
      auto s = LaunchShadowVerificationTablet(tablet, resp.resume_key());
      if (!s.ok()) {
        ShadowVerificationPhaseFailed(s, "resume paginated verification");
      }
      return;
    }
    case Action::kIgnore:  [[fallthrough]];
    case Action::kLaunchNext: [[fallthrough]];
    case Action::kIndexClean: {
      // Persist the clean tablet after the join decision: the sys-catalog write is slow, and
      // holding the decision open across it is what created the false-clean race. Losing a
      // clean id on a failure here only costs an idempotent re-scan on resume.
      WARN_NOT_OK(MutateVerificationState(
          index_table_id,
          [&tablet](UniqueIndexVerificationStatePB* state) {
            state->add_clean_tablet_ids(tablet->tablet_id());
          }), "Failed to persist clean tablet");
      if (action == Action::kLaunchNext) {
        auto s = LaunchShadowVerificationTablet(next, std::string());
        if (!s.ok()) {
          ShadowVerificationPhaseFailed(s, "launch next tablet");
        }
      } else if (action == Action::kIndexClean) {
        auto s = RecordShadowVerificationOutcome(
            UniqueIndexVerificationStatePB::VERIFY_CLEAN, std::string());
        if (s.ok()) {
          s = StartShadowVerificationForNextIndex();
        }
        if (!s.ok()) {
          ShadowVerificationPhaseFailed(s, "advance after clean index");
        }
      }
      return;
    }
    case Action::kTerminalOutcome: {
      UniqueIndexVerificationStatePB::State outcome;
      std::string reason;
      if (!status.ok()) {
        // Exhausted retries on a tablet: the window was not fully verified. Value-free by
        // construction -- every status on this path carries encoding classes and counts only.
        outcome = UniqueIndexVerificationStatePB::VERIFY_INCONCLUSIVE;
        reason = status.message().ToBuffer();
      } else if (resp.outcome() == tserver::VerifyUniqueIndexTabletResponsePB::VIOLATION) {
        outcome = UniqueIndexVerificationStatePB::VERIFY_VIOLATION;
        reason = resp.reason();
      } else if (resp.outcome() == tserver::VerifyUniqueIndexTabletResponsePB::INCONCLUSIVE) {
        outcome = UniqueIndexVerificationStatePB::VERIFY_INCONCLUSIVE;
        reason = resp.reason();
      } else {
        outcome = UniqueIndexVerificationStatePB::VERIFY_INCONCLUSIVE;
        reason = "response without an outcome";
      }
      auto s = RecordShadowVerificationOutcome(outcome, reason);
      if (s.ok()) {
        s = StartShadowVerificationForNextIndex();
      }
      if (!s.ok()) {
        ShadowVerificationPhaseFailed(s, "advance after terminal outcome");
      }
      return;
    }
  }
  FATAL_INVALID_ENUM_VALUE(Action, action);
}

// The shadow phase is on the publication critical path (CREATE INDEX waits for it), so no
// coordinator failure may strand the job short of the terminal funnel. Best-effort-record
// INCONCLUSIVE, then always continue into publication.
void BackfillTable::ShadowVerificationPhaseFailed(
    const Status& status, const char* while_doing) {
  const bool gating = verification_gates_publication();
  LOG_WITH_PREFIX(WARNING) << "Verification phase failed (" << while_doing << "): " << status
                           << (gating ? "; failing unverified indexes (fail-closed)"
                                      : "; degrading to INCONCLUSIVE and publishing");
  {
    std::lock_guard l(mutex_);
    shadow_verification_.terminal = true;
    shadow_verification_.pending_tablets.clear();
    shadow_verification_.remaining_indexes.clear();
  }
  // Records INCONCLUSIVE for the in-flight index; in gating mode that also fails it.
  WARN_NOT_OK(
      RecordShadowVerificationOutcome(
          UniqueIndexVerificationStatePB::VERIFY_INCONCLUSIVE, "coordinator error"),
      "Failed to record verification outcome");
  if (gating) {
    // In gating mode only verified-clean unique indexes have been marked (non-unique ones were
    // marked when the chunks completed), so whatever is still IN_PROGRESS is exactly the
    // unverified set: the in-flight index if Record's marking failed, indexes the phase never
    // reached, and indexes never selected because resolution itself failed. All are equally
    // unverified; fail them rather than publish.
    const auto unverified = indexes_to_build();
    if (!unverified.empty()) {
      WARN_NOT_OK(
          MarkIndexesAsFailed(unverified, "unique index verification could not complete"),
          "Failed to mark unverified indexes as failed");
    }
  }
  WARN_NOT_OK(FinishShadowVerification(), "Failed to finish backfill after verification phase");
}

Status BackfillTable::RecordShadowVerificationOutcome(
    UniqueIndexVerificationStatePB::State state, const std::string& reason) {
  TableId index_table_id;
  {
    std::lock_guard l(mutex_);
    if (!shadow_verification_.current_index) {
      // Phase failure before any index was selected (e.g. index-table resolution failed):
      // there is nothing to record per index.
      return Status::OK();
    }
    index_table_id = shadow_verification_.current_index->id();
  }
  RETURN_NOT_OK(MutateVerificationState(
      index_table_id, [state, &reason](UniqueIndexVerificationStatePB* state_pb) {
        state_pb->set_state(state);
        if (!reason.empty()) {
          state_pb->set_reason(reason);
        }
      }));
  const bool gating = verification_gates_publication();
  const auto severity_prefix =
      state == UniqueIndexVerificationStatePB::VERIFY_CLEAN ? "" : "NOT CLEAN: ";
  LOG_WITH_PREFIX(INFO) << "Shadow verification outcome for unique index " << index_table_id
                        << ": " << severity_prefix
                        << UniqueIndexVerificationStatePB::State_Name(state)
                        << (reason.empty() ? "" : Format(" ($0)", reason))
                        << (gating ? " [gating]" : " [observational]");
  if (!gating) {
    // Observational: the outcome is recorded and logged, never enforced.
    return Status::OK();
  }
  // Fail-closed: the outcome decides publication. Clean marks the (deferred) backfill
  // success; anything else fails the index through the existing backfill failure path --
  // never READ_WRITE_AND_DELETE, never indisvalid. Reasons are value-free by construction.
  // A crash between the verification-state write above and the marking below self-heals:
  // failover resumes the phase, re-finds every tablet clean, and re-drives this marking.
  if (state == UniqueIndexVerificationStatePB::VERIFY_CLEAN) {
    return MarkIndexesAsSuccess({index_table_id});
  }
  return MarkIndexesAsFailed(
      {index_table_id},
      Format(
          "unique index verification did not pass: $0$1",
          UniqueIndexVerificationStatePB::State_Name(state),
          reason.empty() ? "" : Format(" ($0)", reason)));
}

Status BackfillTable::FinishShadowVerification() {
  return UpdateIndexPermissionsForIndexes();
}

Status BackfillTable::MutateVerificationState(
    const TableId& index_table_id,
    const std::function<void(UniqueIndexVerificationStatePB*)>& mutator) {
  auto l = indexed_table_->LockForWrite();
  auto& indexed_table_pb = l.mutable_data()->pb;
  if (indexed_table_pb.backfill_jobs_size() == 0) {
    return STATUS(IllegalState, "Backfill job is gone; cannot record verification state");
  }
  auto* verification_map =
      indexed_table_pb.mutable_backfill_jobs(0)->mutable_unique_index_verification();
  mutator(&(*verification_map)[index_table_id]);
  RETURN_NOT_OK_PREPEND(
      master_->catalog_manager_impl()->sys_catalog_->Upsert(epoch_, indexed_table_),
      "Failed to persist verification state");
  l.Commit();
  return Status::OK();
}

Status BackfillTable::Done(const Status& s, const std::unordered_set<TableId>& failed_indexes) {
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "failed to backfill the index: " << AsString(failed_indexes)
                            << " due to " << s;
    RETURN_NOT_OK_PREPEND(
        MarkIndexesAsFailed(failed_indexes, s.message().ToBuffer()),
        "Couldn't mark indexes as failed");
    return CheckIfDone();
  }

  // If OK then move on to READ permissions.
  if (!done() && --tablets_pending_ == 0) {
    State expected = State::kRunning;
    if (!state_.compare_exchange_strong(
            expected, State::kSuccess, std::memory_order_acq_rel)) {
      return Status::OK();
    }
    LOG_WITH_PREFIX(INFO) << "Completed backfilling the index table.";
    StopLivenessMonitor();
    if (verification_gates_publication()) {
      // Fail-closed: success marking for unique indexes is deferred until each verifies
      // clean (RecordShadowVerificationOutcome); non-unique indexes have nothing to verify.
      RETURN_NOT_OK_PREPEND(
          MarkIndexesAsSuccess(NonUniqueIndexesToBuild()),
          "Failed to mark indexes as successfully backfilled.");
    } else {
      RETURN_NOT_OK_PREPEND(
          MarkAllIndexesAsSuccess(), "Failed to mark indexes as successfully backfilled.");
    }
    RETURN_NOT_OK_PREPEND(
        LaunchShadowVerificationOrFinish(), "Failed to complete backfill.");
  } else {
    VLOG_WITH_PREFIX(1) << "Still backfilling " << tablets_pending_ << " more tablets.";
  }
  return Status::OK();
}

Status BackfillTable::MarkIndexesAsFailed(
    const std::unordered_set<TableId>& failed_indexes, const string& message) {
  if (indexes_to_build() == failed_indexes) {
    state_.store(State::kFailed, std::memory_order_release);
    StopLivenessMonitor();
    backfill_job_->SetState(MonitoredTaskState::kFailed);
  }
  return MarkIndexesAsDesired(failed_indexes, BackfillJobPB::FAILED, message);
}

Status BackfillTable::MarkAllIndexesAsFailed() {
  return MarkIndexesAsFailed(indexes_to_build(), "failed");
}

Status BackfillTable::MarkAllIndexesAsSuccess() {
  return MarkIndexesAsSuccess(indexes_to_build());
}

Status BackfillTable::MarkIndexesAsSuccess(const std::unordered_set<TableId>& index_ids) {
  if (index_ids.empty()) {
    return Status::OK();
  }
  RETURN_NOT_OK(master_->xcluster_manager()->MarkIndexBackfillCompleted(index_ids, epoch_));
  return MarkIndexesAsDesired(index_ids, BackfillJobPB::SUCCESS, "");
}

std::unordered_set<TableId> BackfillTable::NonUniqueIndexesToBuild() const {
  auto index_ids = indexes_to_build();
  for (const auto& index_info : index_infos_) {
    if (index_info.is_unique()) {
      index_ids.erase(index_info.table_id());
    }
  }
  return index_ids;
}

Status BackfillTable::MarkIndexesAsDesired(
    const std::unordered_set<TableId>& index_ids_set, BackfillJobPB_State state,
    const string message) {
  VLOG_WITH_PREFIX(3) << "Marking " << yb::ToString(index_ids_set)
                      << " as " << BackfillJobPB_State_Name(state)
                      << " due to " << message;
  if (!index_ids_set.empty()) {
    auto l = indexed_table_->LockForWrite();
    auto& indexed_table_pb = l.mutable_data()->pb;
    DCHECK_LE(indexed_table_pb.backfill_jobs_size(), 1) << "For now we only expect to have up to 1 "
                                                           "outstanding backfill job.";
    if (indexed_table_pb.backfill_jobs_size() == 0) {
      // Some other task already marked the backfill job as done.
      return Status::OK();
    }
    auto* backfill_state_pb = indexed_table_pb.mutable_backfill_jobs(0)->mutable_backfill_state();
    for (const auto& idx_id : index_ids_set) {
      auto iter = backfill_state_pb->find(idx_id);
      if (iter == backfill_state_pb->end()) {
        LOG(INFO) << "Index " << idx_id << " is not being backfilled. Current backfill_job: "
                  << indexed_table_pb.backfill_jobs(0).ShortDebugString();
        return STATUS_FORMAT(InvalidArgument, "Index $0 is not being backfilled", idx_id);
      }
      backfill_state_pb->at(idx_id) = state;
      VLOG(2) << "Marking index " << idx_id << " as " << BackfillJobPB_State_Name(state);
    }

    for (int i = 0; i < indexed_table_pb.indexes_size(); i++) {
      IndexInfoPB* idx_pb = indexed_table_pb.mutable_indexes(i);
      if (index_ids_set.find(idx_pb->table_id()) != index_ids_set.end()) {
        // Should this also move to the BackfillJob instead?
        if (!message.empty()) {
          idx_pb->set_backfill_error_message(message);
        } else {
          idx_pb->clear_backfill_error_message();
        }
        idx_pb->clear_is_backfill_deferred();

        // We clear the backfill job upon completion - however, we want to persist the number
        // of indexed table rows completed, so we record the information in the index info PB.
        // For partial indexes, the number of rows processed includes non-matching rows of
        // the indexed table.
        auto& num_rows_backfilled_map_pb =
            indexed_table_pb.backfill_jobs(0).num_rows_backfilled_in_index();
        auto num_rows_backfilled_iter = num_rows_backfilled_map_pb.find(idx_pb->table_id());
        if (num_rows_backfilled_iter != num_rows_backfilled_map_pb.end()) {
          idx_pb->set_num_rows_backfilled_in_index(num_rows_backfilled_iter->second);
        } else {
          // If all other tservers are older than the master, they may not include
          // num_rows_backfilled_in_index in the BackfillIndexResponsePB. In this case, the
          // backfill_job's num_rows_backfilled_in_index map would not contain this index. We set
          // the value to 0 in this case.
          idx_pb->set_num_rows_backfilled_in_index(0);
        }
        idx_pb->set_num_rows_read_from_table_for_backfill(
            indexed_table_pb.backfill_jobs(0).num_rows_read_from_table_for_backfill());
      }
    }
    RETURN_NOT_OK(master_->catalog_manager_impl()->sys_catalog_->Upsert(
        epoch_, indexed_table_));
    l.Commit();
  }
  return Status::OK();
}

void BackfillTable::StartRequesterLivenessMonitor() {
  if (!requester_transaction_) {
      return;
  }
  if (PREDICT_FALSE(FLAGS_TEST_skip_ddl_requester_liveness_check)) {
    LOG_WITH_PREFIX(INFO) << "Skipping requester liveness monitor (TEST flag set)";
    return;
  }
  VLOG_WITH_PREFIX(1) << "Starting requester liveness monitor for transaction "
                      << requester_transaction_->transaction_id;

  auto self = shared_from_this();
  BackgroundDdlCallbacks callbacks{
      .done_ = [self] { return self->done(); },
      .abort_ = [self]() { return self->Abort(true); },
  };
  auto task = DdlRequesterLivenessTask::CreateAndStartTask(
      *master_->catalog_manager_impl(),
      indexed_table_,
      *requester_transaction_,
      std::move(callbacks),
      master_->client_future(),
      *master_->messenger(),
      epoch_);

  std::lock_guard l(mutex_);
  DCHECK(!liveness_task_.lock()) << "Liveness task already exists";
  liveness_task_ = task;
}

void BackfillTable::StopLivenessMonitor() {
  std::shared_ptr<DdlRequesterLivenessTask> task;
  {
    std::lock_guard l(mutex_);
    task = liveness_task_.lock();
    liveness_task_.reset();
  }
  if (task) {
    task->AbortAndReturnPrevState(STATUS(Aborted, "BackfillTable is done"));
  }
}

Status BackfillTable::Abort(bool from_liveness) {
  State expected = State::kRunning;
  if (!state_.compare_exchange_strong(
          expected, State::kFailed, std::memory_order_acq_rel)) {
    return Status::OK();
  }
  if (from_liveness) {
    // clear liveness_task_ the subsequent StopLivenessMonitor()
    // call inside MarkIndexesAsFailed will then be a no-op.
    std::lock_guard l(mutex_);
    liveness_task_.reset();
  } else {
    StopLivenessMonitor();
  }
  LOG(WARNING) << "Backfill failed/aborted.";
  RETURN_NOT_OK(MarkAllIndexesAsFailed());
  master_->catalog_manager_impl()->IncrementBackfillAborted();
  return CheckIfDone();
}

Status BackfillTable::CheckIfDone() {
  if (indexes_to_build().empty()) {
    DCHECK(state() != State::kRunning)
        << "CheckIfDone expects callers to transition state out of kRunning before invocation";
    RETURN_NOT_OK_PREPEND(
        UpdateIndexPermissionsForIndexes(),
        "Could not update index permissions after backfill");
  }
  return Status::OK();
}

Status BackfillTable::UpdateIndexPermissionsForIndexes() {
  std::unordered_map<TableId, IndexPermissions> permissions_to_set;
  bool all_success = true;
  {
    auto l = indexed_table_->LockForRead();
    const auto& indexed_table_pb = l.data().pb;
    if (indexed_table_pb.backfill_jobs_size() == 0) {
      // Some other task already marked the backfill job as done.
      return Status::OK();
    }
    DCHECK(indexed_table_pb.backfill_jobs_size() == 1) << "For now we only expect to have up to 1 "
                                                          "outstanding backfill job.";
    for (const auto& kv_pair : indexed_table_pb.backfill_jobs(0).backfill_state()) {
      VLOG(2) << "Reading backfill_state for " << kv_pair.first << " as "
              << BackfillJobPB_State_Name(kv_pair.second);
      if (kv_pair.second == BackfillJobPB::IN_PROGRESS &&
          PREDICT_TRUE(!FLAGS_TEST_simulate_empty_indexes_during_backfill)) {
        // Every path into the funnel marks a terminal state first; reachable only if that
        // marking itself kept failing (sys-catalog writes failing). The mapping below then
        // publishes the index as failed -- the safe direction.
        LOG_WITH_PREFIX(DFATAL) << "Index " << kv_pair.first
                                << " reached the terminal funnel still IN_PROGRESS";
      }
      const bool success = (kv_pair.second == BackfillJobPB::SUCCESS);
      all_success &= success;
      permissions_to_set.emplace(
          kv_pair.first,
          success ? INDEX_PERM_READ_WRITE_AND_DELETE : INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING);
    }
  }

  for (const auto& kv_pair : permissions_to_set) {
    if (kv_pair.second == INDEX_PERM_READ_WRITE_AND_DELETE) {
      RETURN_NOT_OK(AllowCompactionsToGCDeleteMarkers(kv_pair.first));
    }
  }

  RETURN_NOT_OK_PREPEND(
      MultiStageAlterTable::UpdateIndexPermission(
          master_->catalog_manager_impl(), indexed_table_, permissions_to_set, epoch_,
          std::nullopt),
      "Could not update permissions after backfill. "
      "Possible that the master-leader has changed, or the table was deleted.");
  backfill_job_->SetState(
      all_success ? MonitoredTaskState::kComplete : MonitoredTaskState::kFailed);
  RETURN_NOT_OK(ClearCheckpointStateInTablets());
  indexed_table_->ClearIsBackfilling();
  master_->tablet_split_manager().ReenableSplittingForBackfillingTable(indexed_table_->id());
  if (unique_index_backfill_mode() == UniqueIndexBackfillMode::UNIQUE_INDEX_BACKFILL_SKIP_ALL &&
      !FLAGS_TEST_block_index_backfill_ordering_generation_release) {
    // Terminal funnel: every success/failure/abort path of a SKIP_ALL job passes through here,
    // so the ordering generations are released (and index-table splitting re-enabled) on all of
    // them. At-least-once; backstops converge anything this misses (e.g. master death here).
    WARN_NOT_OK(
        SendRpcToReleaseOrderingGenerations(), "Failed to release ordering generations");
  }

  VLOG(1) << "Sending alter table requests to the Indexed table";
  RETURN_NOT_OK(master_->catalog_manager_impl()->SendAlterTableRequest(indexed_table_, epoch_));
  VLOG(1) << "DONE Sending alter table requests to the Indexed table";

  LOG(INFO) << "Done backfill on " << indexed_table_->ToString() << " setting permissions to "
            << yb::ToString(permissions_to_set);
  return Status::OK();
}

Status BackfillTable::ClearCheckpointStateInTablets() {
  auto tablets = VERIFY_RESULT(indexed_table_->GetTablets(GetTabletsMode::kOrderByTabletId));
  for (const auto& tablet : tablets) {
    tablet->mutable_metadata()->StartMutation();
    auto& pb = tablet->mutable_metadata()->mutable_dirty()->pb;
    for (const auto& idx : requested_index_ids_) {
      pb.mutable_backfilled_until()->erase(idx);
    }
  }
  RETURN_NOT_OK_PREPEND(
      master()->catalog_manager()->sys_catalog()->Upsert(epoch_, tablets),
      "Could not persist that the table is done backfilling.");
  for (const auto& tablet : tablets) {
    VLOG(2) << "Done backfilling the table. " << yb::ToString(tablet)
            << " clearing backfilled_until";
    tablet->mutable_metadata()->CommitMutation();
  }

  if (FLAGS_TEST_slowdown_backfill_job_deletion_ms > 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_job_deletion_ms));
  }

  {
    auto l = indexed_table_->LockForWrite();
    DCHECK_LE(l.data().pb.backfill_jobs_size(), 1) << "For now we only expect to have up to 1 "
                                                       "outstanding backfill job.";
    l.mutable_data()->pb.clear_backfill_jobs();
    RETURN_NOT_OK_PREPEND(
        master_->catalog_manager_impl()->sys_catalog_->Upsert(
            epoch_, indexed_table_),
        "Could not clear backfilling timestamp.");
    l.Commit();
  }
  VLOG_WITH_PREFIX(2) << "Cleared backfilling timestamp.";
  return Status::OK();
}

bool BackfillTable::GetIndexTableRetainsDeleteMarkers(const PersistentTableInfo& index_table) {
  CHECK(index_table.is_index());
  return index_table.schema().table_properties().retain_delete_markers();
}

void BackfillTable::UnsetIndexTableRetainsDeleteMarkers(PersistentTableInfo* index_table) {
  CHECK_NOTNULL(index_table);
  CHECK(index_table->is_index());
  index_table->pb.mutable_schema()->mutable_table_properties()->set_retain_delete_markers(false);
}

Status BackfillTable::AllowCompactionsToGCDeleteMarkers(
    const TableId &index_table_id) {
  DVLOG(3) << __PRETTY_FUNCTION__;
  auto res = master_->catalog_manager()->FindTableById(index_table_id);
  if (!res && res.status().IsNotFound()) {
    LOG(WARNING) << "Index " << index_table_id << " was not found."
                 << " This is ok in case somebody issued a delete index. : " << res.ToString();
    return Status::OK();
  }
  scoped_refptr<TableInfo> index_table_info = VERIFY_RESULT_PREPEND(std::move(res),
      Format("Could not find the index table $0", index_table_id));

  // Add a sleep here to wait until the Table is fully created.
  bool is_ready = false;
  bool first_run = true;
  do {
    if (!first_run) {
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Waiting for the previous alter table to "
                                      "complete on the index table "
                                   << index_table_id;
      SleepFor(
          MonoDelta::FromMilliseconds(FLAGS_index_backfill_wait_for_alter_table_completion_ms));
    }
    first_run = false;
    {
      VLOG_WITH_FUNC(2) << "Trying to lock index table for Read";
      auto index_table_rlock = index_table_info->LockForRead();
      auto state = index_table_rlock->pb.state();
      if (!index_table_rlock->is_running() || FLAGS_TEST_simulate_cannot_enable_compactions) {
        LOG(WARNING) << "Index " << index_table_id << " is in state "
                     << SysTablesEntryPB_State_Name(state) << " : cannot enable compactions on it";
        // Treating it as success so that we can proceed with updating other indexes.
        return Status::OK();
      }
      is_ready = state == SysTablesEntryPB::RUNNING;
    }
    VLOG_WITH_FUNC(2) << "Unlocked index table for Read";
  } while (!is_ready);
  {
    const auto idx_birth_time = read_time_for_backfill();
    TRACE("Locking index table");
    VLOG_WITH_FUNC(2) << "Trying to lock index table for Write";
    auto index_table_wlock = index_table_info->LockForWrite();
    VLOG_WITH_FUNC(2) << "Locked index table for Write";
    UnsetIndexTableRetainsDeleteMarkers(index_table_wlock.mutable_data());

    // Persist the index birth time in the index table's index_info.
    // TODO(#33155): read_time_for_backfill isn't set for xCluster automatic-mode target (where
    // backfill is replicated from the source).
    if (index_table_wlock.mutable_data()->pb.has_index_info() && !idx_birth_time.is_special()) {
      index_table_wlock.mutable_data()->pb.mutable_index_info()->set_birth_time(
          idx_birth_time.ToUint64());
    }

    // Update sys-catalog with the new indexed table info.
    TRACE("Updating index table metadata on disk");
    RETURN_NOT_OK_PREPEND(
        master_->catalog_manager_impl()->sys_catalog_->Upsert(
            epoch_, index_table_info),
        yb::Format(
            "Could not update index_table_info for $0 to enable compactions.", index_table_id));

    // Update the in-memory state.
    TRACE("Committing in-memory state");
    index_table_wlock.Commit();
  }
  VLOG_WITH_FUNC(2) << "Unlocked index table for Read";
  VLOG(1) << "Sending backfill done requests to the Index table";
  RETURN_NOT_OK(SendRpcToAllowCompactionsToGCDeleteMarkers(index_table_info));
  VLOG(1) << "DONE Sending backfill done requests to the Index table";
  return Status::OK();
}

Status BackfillTable::SendRpcToAllowCompactionsToGCDeleteMarkers(
    const scoped_refptr<TableInfo>& table) {
  auto tablets = VERIFY_RESULT(table->GetTablets());

  for (const auto& tablet : tablets) {
    RETURN_NOT_OK(SendRpcToAllowCompactionsToGCDeleteMarkers(tablet, table->id()));
  }
  return Status::OK();
}

Status BackfillTable::SendRpcToAllowCompactionsToGCDeleteMarkers(
    const TabletInfoPtr& tablet, const std::string& table_id) {
  ADOPT_WAIT_STATE(wait_state_);
  // TODO(#33155): read_time_for_backfill isn't set for xCluster automatic-mode target (where
  // backfill is replicated from the source).
  auto idx_birth_time = read_time_for_backfill().is_special()
      ? 0 : read_time_for_backfill().ToUint64();
  auto call = std::make_shared<AsyncBackfillDone>(
      master_, callback_pool_, tablet, table_id, epoch_,
      idx_birth_time);
  tablet->table()->AddTask(call);
  RETURN_NOT_OK_PREPEND(
      master_->catalog_manager()->ScheduleTask(call),
      "Failed to send backfill done request");
  return Status::OK();
}

// -----------------------------------------------------------------------------------------------
// BackfillTablet
// -----------------------------------------------------------------------------------------------
BackfillTablet::BackfillTablet(
    std::shared_ptr<BackfillTable> backfill_table, TabletInfoPtr&& tablet)
    : backfill_table_(backfill_table), tablet_(tablet) {
  const auto& index_ids = backfill_table->indexes_to_build();
  {
    auto l = tablet_->LockForRead();
    const auto& pb = tablet_->metadata().state().pb;
    dockv::Partition::FromPB(pb.partition(), &partition_);
    // calculate backfilled_until_ as the largest key which all (active) indexes have backfilled.
    for (const TableId& idx_id : index_ids) {
      if (pb.backfilled_until().find(idx_id) != pb.backfilled_until().end()) {
        auto key = pb.backfilled_until().at(idx_id);
        if (backfilled_until_.empty() || key.compare(backfilled_until_) < 0) {
          VLOG(2) << "Updating backfilled_until_ as " << key;
          backfilled_until_ = key;
          done_.store(backfilled_until_.empty(), std::memory_order_release);
        }
      }
    }
  }
  if (!backfilled_until_.empty()) {
    VLOG_WITH_PREFIX(1) << " resuming backfill from " << b2a_hex(backfilled_until_);
  } else if (done()) {
    VLOG_WITH_PREFIX(1) << " backfill already done.";
  } else {
    VLOG_WITH_PREFIX(1) << " beginning backfill from "
                        << "<start-of-the-tablet>";
  }
}

std::string BackfillTablet::LogPrefix() const {
  return Format("Backfill Index(es) $0 for tablet $1 ",
                yb::ToString(backfill_table_->indexes_to_build()),
                tablet_->id());
}

Status BackfillTablet::LaunchNextChunkOrDone() {
  if (done()) {
    VLOG_WITH_PREFIX(1) << "is done";
    return backfill_table_->Done(Status::OK(), /* failed_indexes */ {});
  } else if (!backfill_table_->done()) {
    VLOG_WITH_PREFIX(2) << "Launching next chunk from " << b2a_hex(backfilled_until_);
    auto chunk = std::make_shared<BackfillChunk>(shared_from_this(),
                                                 backfilled_until_,
                                                 backfill_table_->epoch());
    return chunk->Launch();
  }
  return Status::OK();
}

Status BackfillTablet::Done(
    const Status& status, const std::optional<string>& backfilled_until,
    const uint64_t num_rows_read_from_table_for_backfill,
    const std::unordered_map<TableId, double>& num_rows_backfilled_in_index,
    const std::unordered_set<TableId>& failed_indexes) {
  if (!status.ok()) {
    LOG(INFO) << "Failed to backfill the tablet " << yb::ToString(tablet_) << ": " << status
              << "\nFailed_indexes are " << yb::ToString(failed_indexes);
    RETURN_NOT_OK(backfill_table_->Done(status, failed_indexes));
  }

  if (backfilled_until) {
    RETURN_NOT_OK_PREPEND(
        UpdateBackfilledUntil(*backfilled_until, num_rows_read_from_table_for_backfill,
                              num_rows_backfilled_in_index),
        "Could not persist how far the tablet is done backfilling.");
  }

  return LaunchNextChunkOrDone();
}

Status BackfillTablet::UpdateBackfilledUntil(
    const string& backfilled_until, const uint64_t num_rows_read_from_table_for_backfill,
    const std::unordered_map<TableId, double>& num_rows_backfilled_in_index) {
  backfilled_until_ = backfilled_until;
  VLOG_WITH_PREFIX(2) << "Done backfilling the tablet " << yb::ToString(tablet_) << " until "
                      << b2a_hex(backfilled_until_);
  {
    auto l = tablet_->LockForWrite();
    for (const auto& idx_id : backfill_table_->indexes_to_build()) {
      l.mutable_data()->pb.mutable_backfilled_until()->insert({idx_id, backfilled_until_});
    }
    RETURN_NOT_OK(
        backfill_table_->master()->catalog_manager()->sys_catalog()->Upsert(
            backfill_table_->epoch(), tablet_));
    l.Commit();
  }

  // This is the last chunk.
  if (backfilled_until_.empty()) {
    LOG(INFO) << "Done backfilling the tablet " << yb::ToString(tablet_);
    done_.store(true, std::memory_order_release);
  }
  return backfill_table_->UpdateRowsProcessedForIndexTable(num_rows_read_from_table_for_backfill,
                                                           num_rows_backfilled_in_index);
}

// -----------------------------------------------------------------------------------------------
// GetSafeTimeForTablet
// -----------------------------------------------------------------------------------------------

Status GetSafeTimeForTablet::Launch() {
  tablet_->table()->AddTask(shared_from_this());
  RETURN_NOT_OK_PREPEND(Run(), Substitute("Failed to send GetSafeTime request for $0. ",
                                            tablet_->ToString()));
  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  VLOG(3) << "Started GetSafeTimeForTablet : " << this->description();
  return Status::OK();
}

bool GetSafeTimeForTablet::SendRequest(int attempt) {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  VLOG(1) << __PRETTY_FUNCTION__;
  tserver::GetSafeTimeRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(tablet_->tablet_id());
  auto now = backfill_table_->master()->clock()->Now().ToUint64();
  req.set_min_hybrid_time_for_backfill(min_cutoff_.ToUint64());
  req.set_propagated_hybrid_time(now);
  req.set_only_abort_txns_not_using_table_locks(backfill_table_->using_table_locks());

  ts_admin_proxy_->GetSafeTimeAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

void GetSafeTimeForTablet::HandleResponse(int attempt) {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  VLOG(1) << __PRETTY_FUNCTION__;
  Status status = Status::OK();
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
      case TabletServerErrorPB::MISMATCHED_SCHEMA:
      case TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA:
      case TabletServerErrorPB::OPERATION_NOT_SUPPORTED:
        LOG(WARNING) << "TS " << permanent_uuid() << ": GetSafeTime failed for tablet "
                     << tablet_->ToString() << " no further retry: " << status;
        TransitionToFailedState(MonitoredTaskState::kRunning, status);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": GetSafeTime failed for tablet "
                     << tablet_->ToString() << ": " << status << " code " << resp_.error().code();
        break;
    }
  } else {
    TransitionToCompleteState();
    VLOG(1) << "TS " << permanent_uuid() << ": GetSafeTime complete on tablet "
            << tablet_->ToString();
  }

  server::UpdateClock(resp_, master_->clock());
}

void GetSafeTimeForTablet::UnregisterAsyncTaskCallback() {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  if (state() == MonitoredTaskState::kAborted) {
    VLOG(1) << " was aborted";
    return;
  }

  Status status;
  HybridTime safe_time;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
    VLOG(3) << "GetSafeTime for " << tablet_->ToString() << " got an error. Returning "
            << safe_time;
  } else if (state() != MonitoredTaskState::kComplete) {
    status = STATUS_FORMAT(InternalError, "$0 in state $1", description(), state());
  } else {
    safe_time = HybridTime(resp_.safe_time());
    if (safe_time.is_special()) {
      status = STATUS_FORMAT(
          InternalError, "GetSafeTime for $0 got $1", tablet_->ToString(), safe_time);
      LOG(DFATAL) << status;
    } else {
      VLOG(3) << "GetSafeTime for " << tablet_->ToString() << " got " << safe_time;
    }
  }
  WARN_NOT_OK(backfill_table_->UpdateSafeTime(status, safe_time),
    "Could not UpdateSafeTime");
}

UpdateOrderingGenerationForTablet::UpdateOrderingGenerationForTablet(
    std::shared_ptr<BackfillTable> backfill_table,
    const TabletInfoPtr& tablet,
    const TableId& index_table_id,
    tablet::ActivateGeneration activate,
    NotifyBackfillTable notify_backfill_table,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          backfill_table->master(), backfill_table->threadpool(),
          std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)), tablet->table(),
          std::move(epoch),
          /* async_task_throttler */ nullptr),
      backfill_table_(backfill_table),
      tablet_(tablet),
      index_table_id_(index_table_id),
      activate_(activate),
      notify_backfill_table_(notify_backfill_table) {
  deadline_ = MonoTime::Max();  // Single-attempt deadline comes from ComputeDeadline().
}

Status UpdateOrderingGenerationForTablet::Launch() {
  tablet_->table()->AddTask(shared_from_this());
  RETURN_NOT_OK_PREPEND(
      Run(),
      Substitute("Failed to send UpdateOrderingGeneration request for $0. ",
                 tablet_->ToString()));
  VLOG(3) << "Started UpdateOrderingGenerationForTablet : " << this->description();
  return Status::OK();
}

std::string UpdateOrderingGenerationForTablet::description() const {
  return Format(
      "$0 ordering generation for index tablet $1 of $2",
      activate_ ? "Activate" : "Release", tablet_id(), index_table_id_);
}

TabletId UpdateOrderingGenerationForTablet::tablet_id() const { return tablet_->id(); }

bool UpdateOrderingGenerationForTablet::SendRequest(int attempt) {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  tablet::ChangeMetadataRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(tablet_->tablet_id());
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  auto* generation_op = req.mutable_index_backfill_ordering_generation();
  generation_op->set_table_id(index_table_id_);
  generation_op->set_activate(activate_);
  if (activate_) {
    // History at and above backfill_read_time.Decremented() must survive until verification;
    // the record carries the barrier, enforcement lands with the verification read fence.
    generation_op->set_retention_barrier_ht(
        backfill_table_->read_time_for_backfill().Decremented().ToUint64());
    generation_op->set_write_id_floor_version(kIndexBackfillWriteIdFloorVersion);
  }

  ts_admin_proxy_->UpdateIndexBackfillOrderingGenerationAsync(
      req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

void UpdateOrderingGenerationForTablet::HandleResponse(int attempt) {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  Status status = Status::OK();
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
      case TabletServerErrorPB::OPERATION_NOT_SUPPORTED:
        LOG(WARNING) << "TS " << permanent_uuid() << ": " << description()
                     << " failed, no further retry: " << status;
        TransitionToFailedState(MonitoredTaskState::kRunning, status);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": " << description() << " failed: "
                     << status << " code " << resp_.error().code();
        break;
    }
  } else {
    TransitionToCompleteState();
    VLOG(1) << "TS " << permanent_uuid() << ": " << description() << " complete";
  }

  server::UpdateClock(resp_, master_->clock());
}

void UpdateOrderingGenerationForTablet::UnregisterAsyncTaskCallback() {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  if (state() == MonitoredTaskState::kAborted) {
    // Deliberately no join notification (same shape as GetSafeTimeForTablet): external task
    // aborts accompany leadership loss or table teardown, where the job object is dying with
    // us; notifying could double-drive a job that is already unwinding.
    VLOG(1) << " was aborted";
    return;
  }
  if (!notify_backfill_table_) {
    return;
  }

  Status status;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
  } else if (state() != MonitoredTaskState::kComplete) {
    status = STATUS_FORMAT(InternalError, "$0 in state $1", description(), state());
  }
  backfill_table_->OrderingGenerationUpdateDone(status, tablet_->tablet_id());
}

VerifyUniqueIndexForTablet::VerifyUniqueIndexForTablet(
    std::shared_ptr<BackfillTable> backfill_table,
    const TabletInfoPtr& tablet,
    const TableId& index_table_id,
    HybridTime backfill_read_ht,
    HybridTime verify_upper_ht,
    std::string start_key,
    LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(
          backfill_table->master(), backfill_table->threadpool(),
          std::unique_ptr<TSPicker>(new PickLeaderReplica(tablet)), tablet->table(),
          std::move(epoch),
          /* async_task_throttler */ nullptr),
      backfill_table_(backfill_table),
      tablet_(tablet),
      index_table_id_(index_table_id),
      backfill_read_ht_(backfill_read_ht),
      verify_upper_ht_(verify_upper_ht),
      start_key_(std::move(start_key)) {
  deadline_ = MonoTime::Max();  // Single-attempt deadline comes from ComputeDeadline().
}

Status VerifyUniqueIndexForTablet::Launch() {
  tablet_->table()->AddTask(shared_from_this());
  RETURN_NOT_OK_PREPEND(
      Run(),
      Substitute("Failed to send VerifyUniqueIndex request for $0. ", tablet_->ToString()));
  VLOG(3) << "Started VerifyUniqueIndexForTablet : " << this->description();
  return Status::OK();
}

std::string VerifyUniqueIndexForTablet::description() const {
  return Format("Verify unique index $0 tablet $1", index_table_id_, tablet_id());
}

MonoTime VerifyUniqueIndexForTablet::ComputeDeadline() const {
  // One deadline-bounded page per attempt (BackfillChunk::ComputeDeadline is the pattern):
  // the tserver stops a grace margin early and returns a resume key, so the deadline sizes
  // the page, not the whole tablet scan.
  MonoTime timeout = MonoTime::Now();
  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_index_backfill_verify_rpc_timeout_ms));
  return MonoTime::Earliest(timeout, deadline_);
}

TabletId VerifyUniqueIndexForTablet::tablet_id() const { return tablet_->id(); }

bool VerifyUniqueIndexForTablet::SendRequest(int attempt) {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  tserver::VerifyUniqueIndexTabletRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(tablet_->tablet_id());
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  req.set_backfill_read_ht(backfill_read_ht_.ToUint64());
  req.set_verify_upper_ht(verify_upper_ht_.ToUint64());
  req.set_index_table_id(index_table_id_);
  // No generation_base_op_index on purpose: the base is per-tablet (the activation op's own
  // Raft index) and a re-activated generation's higher base is still the generation this
  // job's marked writes live under. The active + index-table check is the semantic guard.
  if (!start_key_.empty()) {
    req.set_start_key(start_key_);
  }
  if (FLAGS_index_backfill_shadow_verification_dockey_groups_per_rpc > 0) {
    req.set_max_dockey_groups(FLAGS_index_backfill_shadow_verification_dockey_groups_per_rpc);
  }

  ts_admin_proxy_->VerifyUniqueIndexTabletAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n" << req.DebugString();
  return true;
}

void VerifyUniqueIndexForTablet::HandleResponse(int attempt) {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  Status status = Status::OK();
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
      case TabletServerErrorPB::OPERATION_NOT_SUPPORTED:
      case TabletServerErrorPB::INVALID_SCHEMA:
        LOG(WARNING) << "TS " << permanent_uuid() << ": " << description()
                     << " failed, no further retry: " << status;
        TransitionToFailedState(MonitoredTaskState::kRunning, status);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": " << description() << " failed: "
                     << status << " code " << resp_.error().code();
        break;
    }
  } else {
    TransitionToCompleteState();
    VLOG(1) << "TS " << permanent_uuid() << ": " << description() << " complete";
  }

  server::UpdateClock(resp_, master_->clock());
}

void VerifyUniqueIndexForTablet::UnregisterAsyncTaskCallback() {
  ADOPT_WAIT_STATE(backfill_table_->wait_state());
  if (state() == MonitoredTaskState::kAborted) {
    // Deliberately no join notification (same shape as GetSafeTimeForTablet): external task
    // aborts accompany leadership loss or table teardown, where the job object is dying with
    // us; notifying could double-drive a job that is already unwinding.
    VLOG(1) << " was aborted";
    return;
  }

  Status status;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
  } else if (state() != MonitoredTaskState::kComplete) {
    status = STATUS_FORMAT(InternalError, "$0 in state $1", description(), state());
  }
  backfill_table_->ShadowVerificationTabletDone(tablet_, status, resp_);
}

BackfillChunk::BackfillChunk(std::shared_ptr<BackfillTablet> backfill_tablet,
                             const std::string& start_key,
                             LeaderEpoch epoch)
    : RetryingTSRpcTaskWithTable(backfill_tablet->master(),
                        backfill_tablet->threadpool(),
                        std::unique_ptr<TSPicker>(new PickLeaderReplica(backfill_tablet->tablet())),
                        backfill_tablet->tablet()->table(),
                        std::move(epoch),
                        /* async_task_throttler */ nullptr),
      indexes_being_backfilled_(backfill_tablet->indexes_to_build()),
      backfill_tablet_(backfill_tablet),
      start_key_(start_key),
      requested_index_names_(RetrieveIndexNames(backfill_tablet->master()->catalog_manager_impl(),
                                                indexes_being_backfilled_)) {
  // No deadline for the task, refer to ComputeDeadline() for a single attempt deadline.
  deadline_ = MonoTime::Max();
}

// -----------------------------------------------------------------------------------------------
// BackfillChunk
// -----------------------------------------------------------------------------------------------
Status BackfillChunk::Launch() {
  backfill_tablet_->tablet()->table()->AddTask(shared_from_this());
  Status status = Run();
  RETURN_NOT_OK_PREPEND(
      status, Substitute(
                  "Failed to send backfill Chunk request for $0",
                  backfill_tablet_->tablet()->ToString()));

  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  if (status.ok()) {
    LOG(INFO) << "Started BackfillChunk : " << this->description();
  }
  return Status::OK();
}

MonoTime BackfillChunk::ComputeDeadline() const {
  MonoTime timeout = MonoTime::Now();
  if (GetTableType() == TableType::PGSQL_TABLE_TYPE) {
    timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_ysql_index_backfill_rpc_timeout_ms));
  } else {
    DCHECK(GetTableType() == TableType::YQL_TABLE_TYPE);
    timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_index_backfill_rpc_timeout_ms));
  }
  // May not honor unresponsive deadline, refer to UnresponsiveDeadline().
  return MonoTime::Earliest(timeout, deadline_);
}

int BackfillChunk::num_max_retries() {
  return FLAGS_index_backfill_rpc_max_retries;
}

int BackfillChunk::max_delay_ms() {
  return FLAGS_index_backfill_rpc_max_delay_ms;
}

std::string BackfillChunk::description() const {
  return yb::Format("Backfilling indexes $0 for tablet $1 from key '$2'",
                    requested_index_names_, tablet_id(),
                    b2a_hex(start_key_));
}

bool BackfillChunk::SendRequest(int attempt) {
  ADOPT_WAIT_STATE(backfill_tablet_->wait_state());
  VLOG(1) << __PRETTY_FUNCTION__;
  if (indexes_being_backfilled_.empty()) {
    TransitionToFailedState(
        MonitoredTaskState::kRunning, STATUS(IllegalState, "No indexes remaining to backfill."));
    return false;
  }

  tserver::BackfillIndexRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(backfill_tablet_->tablet()->tablet_id());
  req.set_read_at_hybrid_time(backfill_tablet_->read_time_for_backfill().ToUint64());
  req.set_schema_version(backfill_tablet_->schema_version());
  req.set_start_key(start_key_);
  req.set_indexed_table_id(backfill_tablet_->indexed_table_id());
  if (GetTableType() == TableType::PGSQL_TABLE_TYPE) {
    req.set_namespace_name(backfill_tablet_->GetNamespaceName());
    req.set_unique_index_backfill_mode(backfill_tablet_->unique_index_backfill_mode());
  }
  std::unordered_set<TableId> found_idxs;
  for (const IndexInfoPB& idx_info : backfill_tablet_->index_infos()) {
    if (indexes_being_backfilled_.find(idx_info.table_id()) != indexes_being_backfilled_.end()) {
      req.add_indexes()->CopyFrom(idx_info);
      found_idxs.insert(idx_info.table_id());
    }
  }
  if (found_idxs.size() != indexes_being_backfilled_.size()) {
    // We could not find the IndexInfoPB for all the requested indexes. This can happen
    // if that index was deleted while the backfill was still going on.
    // We are going to fail fast and mark that index as failed.
    for (auto& idx : indexes_being_backfilled_) {
      if (found_idxs.find(idx) == found_idxs.end()) {
        VLOG_WITH_PREFIX(3) << "Marking " << idx << " as failed";
        *resp_.add_failed_index_ids() = idx;
      }
    }
    const string error_message("Could not find IndexInfoPB for some indexes");
    resp_.mutable_error()->mutable_status()->set_code(AppStatusPB::NOT_FOUND);
    resp_.mutable_error()->mutable_status()->set_message(error_message);
    TransitionToFailedState(MonitoredTaskState::kRunning,
                            STATUS(NotFound, error_message));
    return false;
  }
  req.set_propagated_hybrid_time(backfill_tablet_->master()->clock()->Now().ToUint64());

  ts_admin_proxy_->BackfillIndexAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

void BackfillChunk::HandleResponse(int attempt) {
  VLOG(1) << __PRETTY_FUNCTION__ << " response is " << yb::ToString(resp_);
  Status status;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    switch (resp_.error().code()) {
      case TabletServerErrorPB::MISMATCHED_SCHEMA:
      case TabletServerErrorPB::OPERATION_NOT_SUPPORTED:
      case TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA:
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG(WARNING) << "TS " << permanent_uuid() << ": backfill failed for tablet "
                     << backfill_tablet_->tablet()->ToString() << " no further retry: " << status
                     << " response was " << yb::ToString(resp_);
        TransitionToFailedState(MonitoredTaskState::kRunning, status);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": backfill failed for tablet "
                     << backfill_tablet_->tablet()->ToString() << ": " << status.ToString()
                     << " code " << resp_.error().code();
        break;
    }
  } else {
    TransitionToCompleteState();
    VLOG(1) << "TS " << permanent_uuid() << ": backfill complete on tablet "
            << backfill_tablet_->tablet()->ToString();
  }

  server::UpdateClock(resp_, master_->clock());
}

void BackfillChunk::UnregisterAsyncTaskCallback() {
  if (state() == MonitoredTaskState::kAborted) {
    VLOG(1) << " was aborted";
    return;
  }

  Status status;
  std::unordered_set<TableId> failed_indexes;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
    if (resp_.failed_index_ids_size() > 0) {
      for (int i = 0; i < resp_.failed_index_ids_size(); i++) {
        VLOG(1) << " Added to failed index " << resp_.failed_index_ids(i);
        failed_indexes.insert(resp_.failed_index_ids(i));
      }
    } else {
      // No specific index was marked as a failure. So consider all of them as failed.
      failed_indexes = indexes_being_backfilled_;
    }
  } else if (state() != MonitoredTaskState::kComplete) {
    // There is no response, so the error happened even before we could
    // get a response. Mark all indexes as failed.
    failed_indexes = indexes_being_backfilled_;
    VLOG(3) << "Considering all indexes : "
            << yb::ToString(indexes_being_backfilled_)
            << " as failed.";
    status = STATUS_FORMAT(InternalError, "$0 in state $1", description(), state());
  }

  // The BackfillIndexResponsePB from the tserver may not contain num_rows_backfilled_in_index
  // during a rolling upgrade where the tserver is older than the master. Protobuf does not
  // allow marking map fields as optional. Instead, if the sender does not include this field,
  // we receive an empty map. In this case, we will forward an empty map. Number of rows inserted
  // will not be updated for this chunk and no other special handling is needed.
  for (const auto& [index_id, num_rows_backfilled] : resp_.num_rows_backfilled_in_index()) {
    if (num_rows_backfilled_in_index_.find(index_id) ==
        num_rows_backfilled_in_index_.end()) {
      num_rows_backfilled_in_index_.emplace(index_id, 0);
    }
    num_rows_backfilled_in_index_[index_id] += num_rows_backfilled;
  }

  if (resp_.has_backfilled_until()) {
    WARN_NOT_OK(
        backfill_tablet_->Done(
            status, resp_.backfilled_until(), resp_.num_rows_read_from_table_for_backfill(),
            num_rows_backfilled_in_index_,
            failed_indexes),
        "Failed marking BackfillTablet as done.");
  } else {
    WARN_NOT_OK(
        backfill_tablet_->Done(status, std::nullopt, resp_.num_rows_read_from_table_for_backfill(),
                               num_rows_backfilled_in_index_,
        failed_indexes),
        "Failed marking BackfillTablet as done.");
  }
}

}  // namespace master
}  // namespace yb
