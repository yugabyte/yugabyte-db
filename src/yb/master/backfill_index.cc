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
//

#include "yb/master/backfill_index.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>

#include <algorithm>
#include <bitset>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/optional.hpp>
#include <boost/preprocessor/cat.hpp>
#include <glog/logging.h>

#include "yb/common/partial_row.h"
#include "yb/common/partition.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/callback.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"

#include "yb/master/master_fwd.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/uuid.h"

using std::vector;
using std::string;

DEFINE_int32(ysql_index_backfill_rpc_timeout_ms, 60 * 1000, // 1 min.
             "Timeout used by the master when attempting to backfill a YSQL tablet during index "
             "creation.");
TAG_FLAG(ysql_index_backfill_rpc_timeout_ms, advanced);
TAG_FLAG(ysql_index_backfill_rpc_timeout_ms, runtime);

DEFINE_int32(index_backfill_rpc_timeout_ms, 1 * 30 * 1000, // 30 sec.
             "Timeout used by the master when attempting to backfill a tablet "
             "during index creation.");
TAG_FLAG(index_backfill_rpc_timeout_ms, advanced);
TAG_FLAG(index_backfill_rpc_timeout_ms, runtime);

DEFINE_int32(index_backfill_rpc_max_retries, 150,
             "Number of times to retry backfilling a tablet chunk "
             "during index creation.");
TAG_FLAG(index_backfill_rpc_max_retries, advanced);
TAG_FLAG(index_backfill_rpc_max_retries, runtime);

DEFINE_int32(index_backfill_rpc_max_delay_ms, 10 * 60 * 1000, // 10 min.
             "Maximum delay before retrying a backfill tablet chunk request "
             "during index creation.");
TAG_FLAG(index_backfill_rpc_max_delay_ms, advanced);
TAG_FLAG(index_backfill_rpc_max_delay_ms, runtime);

DEFINE_int32(index_backfill_wait_for_alter_table_completion_ms, 100,
             "Delay before retrying to see if an in-progress alter table has "
             "completed, during index backfill.");
TAG_FLAG(index_backfill_wait_for_alter_table_completion_ms, advanced);
TAG_FLAG(index_backfill_wait_for_alter_table_completion_ms, runtime);

DEFINE_int32(index_backfill_tablet_split_completion_timeout_sec, 30,
             "Total time to wait for tablet splitting to complete on a table from which we are "
             "running a backfill before aborting the backfill and marking it as failed.");
TAG_FLAG(index_backfill_tablet_split_completion_timeout_sec, advanced);
TAG_FLAG(index_backfill_tablet_split_completion_timeout_sec, runtime);

DEFINE_int32(index_backfill_tablet_split_completion_poll_freq_ms, 2000,
             "Delay before retrying to see if tablet splitting has completed on the table from "
             "which we are running a backfill.");
TAG_FLAG(index_backfill_tablet_split_completion_poll_freq_ms, advanced);
TAG_FLAG(index_backfill_tablet_split_completion_poll_freq_ms, runtime);

DEFINE_bool(defer_index_backfill, false,
            "Defer index backfill so that backfills can be performed as a batch later on.");
TAG_FLAG(defer_index_backfill, advanced);
TAG_FLAG(defer_index_backfill, runtime);

DEFINE_test_flag(int32, slowdown_backfill_alter_table_rpcs_ms, 0,
    "Slows down the send alter table rpc's so that the master may be stopped between "
    "different phases.");

DEFINE_test_flag(
    int32, slowdown_backfill_job_deletion_ms, 0,
    "Slows down backfill job deletion so that backfill job can be read by test.");

DEFINE_test_flag(
    bool, skip_index_backfill, false,
    "Skips backfilling the data on tservers and leaves the index in inconsistent state.");

DEFINE_test_flag(
    bool, block_do_backfill, false,
    "Block DoBackfill from proceeding.");

namespace yb {
namespace master {

using namespace std::literals;
using server::MonitoredTaskState;
using strings::Substitute;
using tserver::TabletServerErrorPB;

namespace {

// Peek into pg_index table to get an index (boolean) status from YSQL perspective.
Result<bool> GetPgIndexStatus(
    CatalogManager* catalog_manager,
    const TableId& idx_id,
    const std::string& status_col_name) {
  const auto pg_index_id =
      GetPgsqlTableId(VERIFY_RESULT(GetPgsqlDatabaseOid(idx_id)), kPgIndexTableOid);

  const auto catalog_tablet =
      VERIFY_RESULT(catalog_manager->tablet_peer()->shared_tablet_safe());
  const Schema& pg_index_schema =
      VERIFY_RESULT(catalog_tablet->metadata()->GetTableInfo(pg_index_id))->schema();

  Schema projection;
  RETURN_NOT_OK(pg_index_schema.CreateProjectionByNames({"indexrelid", status_col_name},
                                                        &projection,
                                                        pg_index_schema.num_key_columns()));

  const auto indexrelid_col_id = VERIFY_RESULT(projection.ColumnIdByName("indexrelid")).rep();
  const auto status_col_id     = VERIFY_RESULT(projection.ColumnIdByName(status_col_name)).rep();

  const auto idx_oid = VERIFY_RESULT(GetPgsqlTableOid(idx_id));

  auto iter = VERIFY_RESULT(catalog_tablet->NewRowIterator(projection.CopyWithoutColumnIds(),
                                                           {} /* read_hybrid_time */,
                                                           pg_index_id));

  // Filtering by 'indexrelid' == idx_oid.
  {
    auto doc_iter = down_cast<docdb::DocRowwiseIterator*>(iter.get());
    PgsqlConditionPB cond;
    cond.add_operands()->set_column_id(indexrelid_col_id);
    cond.set_op(QL_OP_EQUAL);
    cond.add_operands()->mutable_value()->set_uint32_value(idx_oid);
    const std::vector<docdb::KeyEntryValue> empty_key_components;
    docdb::DocPgsqlScanSpec spec(projection,
                                 rocksdb::kDefaultQueryId,
                                 empty_key_components,
                                 empty_key_components,
                                 &cond,
                                 boost::none /* hash_code */,
                                 boost::none /* max_hash_code */,
                                 nullptr /* where_expr */);
    RETURN_NOT_OK(doc_iter->Init(spec));
  }

  // Expecting one row at most.
  QLTableRow row;
  if (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&row));
    return row.GetColumn(status_col_id)->bool_value();
  }

  // For practical purposes, an absent index is the same as having false status column value.
  return false;
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
      auto live = VERIFY_RESULT(GetPgIndexStatus(catalog_manager, idx_id, "indislive"));
      if (!live) {
        VLOG(1) << "Index " << idx_id << " is not yet live, skipping permission update";
      }
      return live;
    }
    case INDEX_PERM_DO_BACKFILL: {
      auto ready = VERIFY_RESULT(GetPgIndexStatus(catalog_manager, idx_id, "indisready"));
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
    CatalogManager* catalog_manager,
    const scoped_refptr<TableInfo>& table,
    boost::optional<uint32_t> expected_version,
    bool update_state_to_running) {
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

  Status s = catalog_manager->sys_catalog_->Upsert(catalog_manager->leader_ready_term(), table);
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
    CatalogManager* catalog_manager,
    const scoped_refptr<TableInfo>& indexed_table,
    const std::unordered_map<TableId, IndexPermissions>& perm_mapping,
    boost::optional<uint32_t> current_version) {
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
    RETURN_NOT_OK(catalog_manager->sys_catalog_->Upsert(
        catalog_manager->leader_ready_term(), indexed_table));

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
    boost::optional<uint32_t> current_version) {
  // We leave the table state as ALTERING so that a master failover can resume the backfill.
  RETURN_NOT_OK(ClearFullyAppliedAndUpdateState(
      catalog_manager, indexed_table, current_version, /* change_state to RUNNING */ false));

  auto ns_info = catalog_manager->FindNamespaceById(indexed_table->namespace_id());
  RETURN_NOT_OK_PREPEND(ns_info, "Unable to get namespace info for backfill");

  RETURN_NOT_OK(indexed_table->SetIsBackfilling());
  TRACE("Starting backfill process");
  VLOG(0) << __func__ << " starting backfill on " << indexed_table->ToString() << " for "
          << yb::ToString(idx_infos);

  if (FLAGS_TEST_skip_index_backfill) {
    TRACE("Skipping backfill of data on tservers");
    LOG(INFO) << "Skipping backfill of data on tservers";
    return Status::OK();
  }

  auto backfill_table = std::make_shared<BackfillTable>(
      catalog_manager->master_, catalog_manager->AsyncTaskPool(), indexed_table, idx_infos,
      *ns_info);
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
    uint32_t current_version, bool respect_backfill_deferrals) {
  DVLOG(3) << __PRETTY_FUNCTION__ << " " << yb::ToString(*indexed_table);

  const bool is_ysql_table = (indexed_table->GetTableType() == TableType::PGSQL_TABLE_TYPE);
  const bool defer_backfill = !is_ysql_table && GetAtomicFlag(&FLAGS_defer_index_backfill);
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
      // For YSQL, there should never be indexes to update from master side because postgres drives
      // permission changes.
      } else if (idx_pb.index_permissions() != INDEX_PERM_READ_WRITE_AND_DELETE && !is_ysql_table) {
        indexes_to_update.emplace(idx_pb.table_id(), NextPermission(idx_pb.index_permissions()));
      }
    }

    // TODO(#6218): Do we really not want to continue backfill
    // across master failovers for YSQL?
    if (!is_ysql_table && !is_backfilling && l.data().pb.backfill_jobs_size() > 0) {
      // If a backfill job was started for a set of indexes and then the leader
      // fails over, we should be careful that we are restarting the backfill job
      // with the same set of indexes.
      // A new index could have been added since the time the last backfill job started on
      // the old master. The safe time calculated for the earlier set of indexes may not be
      // valid for the new index(es) to use.
      DCHECK(l.data().pb.backfill_jobs_size() == 1) << "For now we only expect to have up to 1 "
                                                        "outstanding backfill job.";
      const BackfillJobPB& backfill_job = l.data().pb.backfill_jobs(0);
      VLOG(3) << "Found an in-progress backfill-job " << yb::ToString(backfill_job);
      // Do not allow for any other indexes to piggy back with this backfill.
      indexes_to_backfill.clear();
      deferred_indexes.clear();
      for (int i = 0; i < backfill_job.indexes_size(); i++) {
        const IndexInfoPB& idx_pb = backfill_job.indexes(i);
        indexes_to_backfill.push_back(idx_pb);
      }
    }
  }

  if (indexes_to_update.empty() &&
      indexes_to_delete.empty() &&
      (is_backfilling || indexes_to_backfill.empty())) {
    TRACE("Not necessary to launch next version");
    VLOG(1) << "Not necessary to launch next version";
    return ClearFullyAppliedAndUpdateState(
        catalog_manager, indexed_table, current_version, /* change state to RUNNING */ true);
  }

  // For YSQL online schema migration of indexes, instead of master driving the schema changes,
  // postgres will drive it.  Postgres will use three of the DocDB index permissions:
  //
  // - INDEX_PERM_WRITE_AND_DELETE (set from the start)
  // - INDEX_PERM_READ_WRITE_AND_DELETE (set by master)
  // - INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING (set by master)
  //
  // This changes how we treat indexes_to_foo:
  //
  // - indexes_to_update should always be empty because we never want master to set index
  //   permissions.
  // - indexes_to_delete is impossible to be nonempty, and, in the future, when we do use
  //   INDEX_PERM_INDEX_UNUSED, we want to use some other delete trigger that makes sure no
  //   transactions are left using the index.  Prepare for that by doing nothing when nonempty.
  // - indexes_to_backfill is impossible to be nonempty, but, in the future, we want to set
  //   INDEX_PERM_DO_BACKFILL so that backfill resumes on master leader changes.  Prepare for that
  //   by handling indexes_to_backfill like for YCQL.
  //
  // TODO(jason): when using INDEX_PERM_DO_BACKFILL, update this comment (issue #6218).

  if (!indexes_to_update.empty()) {
    VLOG(1) << "Updating index permissions for " << yb::ToString(indexes_to_update) << " on "
            << indexed_table->ToString();
    Result<bool> permissions_updated =
        VERIFY_RESULT(UpdateIndexPermission(catalog_manager, indexed_table, indexes_to_update,
                                            current_version));

    if (!permissions_updated.ok()) {
      LOG(WARNING) << "Could not update index permissions."
                   << " Possible that the master-leader has changed, or a race "
                   << "with another thread trying to launch next version: "
                   << permissions_updated.ToString();
    }

    if (permissions_updated.ok() && *permissions_updated) {
      VLOG(1) << "Sending alter table request with updated permissions";
      RETURN_NOT_OK(catalog_manager->SendAlterTableRequest(indexed_table));
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
    RETURN_NOT_OK(catalog_manager->DeleteTableInternal(&req, &resp, nullptr));
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
            catalog_manager, indexed_table.get(), indexes_to_backfill, current_version),
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

MonitoredTaskState BackfillTableJob::AbortAndReturnPrevState(const Status& status) {
  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state,
                                       MonitoredTaskState::kAborted)) {
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
    std::vector<IndexInfoPB> indexes, const scoped_refptr<NamespaceInfo>& ns_info)
    : master_(master),
      callback_pool_(callback_pool),
      indexed_table_(indexed_table),
      index_infos_(indexes),
      requested_index_ids_(IndexIdsFromInfos(indexes)),
      requested_index_names_(RetrieveIndexNames(
          master->catalog_manager_impl(), requested_index_ids_)),
      ns_info_(ns_info) {
  auto l = indexed_table_->LockForRead();
  schema_version_ = indexed_table_->metadata().state().pb.version();
  leader_term_ = master_->catalog_manager()->leader_ready_term();
  if (l.data().pb.backfill_jobs_size() > 0) {
    number_rows_processed_.store(l.data().pb.backfill_jobs(0).num_rows_processed());
  } else {
    number_rows_processed_.store(0);
  }

  const auto& pb = indexed_table_->metadata().state().pb;
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
  done_.store(false, std::memory_order_release);
}

const std::unordered_set<TableId> BackfillTable::indexes_to_build() const {
  std::unordered_set<TableId> indexes_to_build;
  {
    auto l = indexed_table_->LockForRead();
    const auto& indexed_table_pb = l.data().pb;
    if (indexed_table_pb.backfill_jobs_size() == 0) {
      // Some other task already marked the backfill job as done.
      return {};
    }
    DCHECK(indexed_table_pb.backfill_jobs_size() == 1) << "For now we only expect to have up to 1 "
                                                          "outstanding backfill job.";
    for (const auto& kv_pair : indexed_table_pb.backfill_jobs(0).backfill_state()) {
      if (kv_pair.second == BackfillJobPB::IN_PROGRESS) {
        indexes_to_build.insert(kv_pair.first);
      }
    }
  }
  return indexes_to_build;
}

Status BackfillTable::Launch() {
  backfill_job_ = std::make_shared<BackfillTableJob>(shared_from_this());
  backfill_job_->SetState(MonitoredTaskState::kRunning);
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
      RETURN_NOT_OK_PREPEND(
          master_->catalog_manager_impl()->sys_catalog_->Upsert(leader_term(), indexed_table_),
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
  auto tablets = indexed_table_->GetTablets();

  num_tablets_.store(tablets.size(), std::memory_order_release);
  tablets_pending_.store(tablets.size(), std::memory_order_release);
  auto min_cutoff = master()->clock()->Now();
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto get_safetime = std::make_shared<GetSafeTimeForTablet>(
        shared_from_this(), tablet, min_cutoff);
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
  return Format(
      "Backfill Index Table(s) $0 : $1", requested_index_names_,
      (timestamp_chosen()
           ? (done() ? Format("Backfill $0/$1 tablets done", num_pending, num_tablets)
                     : Format(
                           "Backfilling $0/$1 tablets with $2 rows done", num_pending, num_tablets,
                           number_rows_processed_.load()))
           : Format("Waiting to GetSafeTime from $0/$1 tablets", num_pending, num_tablets)));
}

const std::string BackfillTable::GetNamespaceName() const {
  return ns_info_->name();
}

Status BackfillTable::UpdateRowsProcessedForIndexTable(const uint64_t number_rows_processed) {
  auto l = indexed_table_->LockForWrite();

  if (l.data().pb.backfill_jobs_size() == 0) {
    // Some other task already marked the backfill job as done.
    return Status::OK();
  }

  // This is consistent with logic assuming that we have only one backfill job in queue
  // We might in the future change this to a for loop to account for multiple backfill jobs
  number_rows_processed_.fetch_add(number_rows_processed);
  auto* indexed_table_pb = l.mutable_data()->pb.mutable_backfill_jobs(0);
  indexed_table_pb->set_num_rows_processed(number_rows_processed_.load());
  VLOG(2) << "Updated backfill task to having processed " << number_rows_processed
          << " more rows. Total rows processed is: " << number_rows_processed_;

  RETURN_NOT_OK(
      master_->catalog_manager_impl()->sys_catalog_->Upsert(leader_term(), indexed_table_));
  l.Commit();
  return Status::OK();
}

Status BackfillTable::UpdateSafeTime(const Status& s, HybridTime ht) {
  if (!s.ok()) {
    // Move on to ABORTED permission.
    LOG_WITH_PREFIX(ERROR)
        << "Failed backfill. Could not compute safe time for "
        << yb::ToString(indexed_table_) << " " << s;
    if (!timestamp_chosen_.exchange(true)) {
      RETURN_NOT_OK(Abort());
    }
    return Status::OK();
  }

  // Need to guard this.
  HybridTime read_timestamp;
  {
    std::lock_guard<simple_spinlock> l(mutex_);
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
    {
      auto l = indexed_table_->LockForWrite();
      DCHECK_EQ(l.mutable_data()->pb.backfill_jobs_size(), 1);
      auto* backfill_job = l.mutable_data()->pb.mutable_backfill_jobs(0);
      backfill_job->set_backfilling_timestamp(read_timestamp.ToUint64());
      RETURN_NOT_OK_PREPEND(
          master_->catalog_manager_impl()->sys_catalog_->Upsert(
              leader_term(), indexed_table_),
          "Failed to persist backfilling timestamp. Abandoning.");
      l.Commit();
    }
    VLOG_WITH_PREFIX(2) << "Saved " << read_timestamp
                        << " as backfilling_timestamp";
    timestamp_chosen_.store(true, std::memory_order_release);
    Status backfill_status = DoBackfill();
    if (!backfill_status.ok()) {
      // Mark indexes as failed so CREATE INDEX will stop waiting and return.
      RETURN_NOT_OK(Abort());
      return backfill_status;
    }
  }
  return Status::OK();
}

Status BackfillTable::WaitForTabletSplitting() {
  auto* tablet_split_manager = master_->catalog_manager()->tablet_split_manager();
  tablet_split_manager->DisableSplittingForBackfillingTable(indexed_table_->id());
  CoarseTimePoint deadline = CoarseMonoClock::Now() +
                             FLAGS_index_backfill_tablet_split_completion_timeout_sec * 1s;
  while (!tablet_split_manager->IsTabletSplittingComplete(*indexed_table_,
                                                          false /* wait_for_parent_deletion */)) {
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
  if (!timestamp_chosen_.load(std::memory_order_acquire)) {
    RETURN_NOT_OK(LaunchComputeSafeTimeForRead());
  } else {
    RETURN_NOT_OK(DoBackfill());
  }
  return Status::OK();
}

Status BackfillTable::DoBackfill() {
  while (FLAGS_TEST_block_do_backfill) {
    constexpr auto kSpinWait = 100ms;
    LOG(INFO) << Format("Blocking $0 for $1", __func__, kSpinWait);
    SleepFor(kSpinWait);
  }
  VLOG_WITH_PREFIX(1) << "starting backfill with timestamp: "
                      << read_time_for_backfill_;
  auto tablets = indexed_table_->GetTablets();

  num_tablets_.store(tablets.size(), std::memory_order_release);
  tablets_pending_.store(tablets.size(), std::memory_order_release);
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto backfill_tablet = std::make_shared<BackfillTablet>(shared_from_this(), tablet);
    RETURN_NOT_OK(backfill_tablet->Launch());
  }
  return Status::OK();
}

Status BackfillTable::Done(const Status& s, const std::unordered_set<TableId>& failed_indexes) {
  if (!s.ok()) {
    LOG_WITH_PREFIX(ERROR) << "failed to backfill the index: " << yb::ToString(failed_indexes)
                           << " due to " << s;
    RETURN_NOT_OK_PREPEND(
        MarkIndexesAsFailed(failed_indexes, s.message().ToBuffer()),
        "Couldn't mark indexes as failed");
    return CheckIfDone();
  }

  // If OK then move on to READ permissions.
  if (!done() && --tablets_pending_ == 0) {
    LOG_WITH_PREFIX(INFO) << "Completed backfilling the index table.";
    done_.store(true, std::memory_order_release);
    RETURN_NOT_OK_PREPEND(
        MarkAllIndexesAsSuccess(), "Failed to mark indexes as successfully backfilled.");
    RETURN_NOT_OK_PREPEND(UpdateIndexPermissionsForIndexes(), "Failed to complete backfill.");
  } else {
    VLOG_WITH_PREFIX(1) << "Still backfilling " << tablets_pending_ << " more tablets.";
  }
  return Status::OK();
}

Status BackfillTable::MarkIndexesAsFailed(
    const std::unordered_set<TableId>& failed_indexes, const string& message) {
  if (indexes_to_build() == failed_indexes) {
    done_.store(true, std::memory_order_release);
    backfill_job_->SetState(MonitoredTaskState::kFailed);
  }
  return MarkIndexesAsDesired(failed_indexes, BackfillJobPB::FAILED, message);
}

Status BackfillTable::MarkAllIndexesAsFailed() {
  return MarkIndexesAsFailed(indexes_to_build(), "failed");
}

Status BackfillTable::MarkAllIndexesAsSuccess() {
  return MarkIndexesAsDesired(indexes_to_build(), BackfillJobPB::SUCCESS, "");
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
        idx_pb->set_num_rows_processed_by_backfill_job(number_rows_processed_);
      }
    }
    RETURN_NOT_OK(
        master_->catalog_manager_impl()->sys_catalog_->Upsert(leader_term(), indexed_table_));
    l.Commit();
  }
  return Status::OK();
}

Status BackfillTable::Abort() {
  LOG(WARNING) << "Backfill failed/aborted.";
  RETURN_NOT_OK(MarkAllIndexesAsFailed());
  return CheckIfDone();
}

Status BackfillTable::CheckIfDone() {
  if (indexes_to_build().empty()) {
    done_.store(true, std::memory_order_release);
    RETURN_NOT_OK_PREPEND(
        UpdateIndexPermissionsForIndexes(), "Could not update index permissions after backfill");
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
      DCHECK_NE(kv_pair.second, BackfillJobPB::IN_PROGRESS)
          << __func__ << " is expected to be only called after all indexes are done.";
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
          master_->catalog_manager_impl(), indexed_table_, permissions_to_set, boost::none),
      "Could not update permissions after backfill. "
      "Possible that the master-leader has changed, or the table was deleted.");
  backfill_job_->SetState(
      all_success ? MonitoredTaskState::kComplete : MonitoredTaskState::kFailed);
  RETURN_NOT_OK(ClearCheckpointStateInTablets());
  indexed_table_->ClearIsBackfilling();
  master_->catalog_manager()->tablet_split_manager()
      ->ReenableSplittingForBackfillingTable(indexed_table_->id());

  VLOG(1) << "Sending alter table requests to the Indexed table";
  RETURN_NOT_OK(master_->catalog_manager_impl()->SendAlterTableRequest(indexed_table_));
  VLOG(1) << "DONE Sending alter table requests to the Indexed table";

  LOG(INFO) << "Done backfill on " << indexed_table_->ToString() << " setting permissions to "
            << yb::ToString(permissions_to_set);
  return Status::OK();
}

Status BackfillTable::ClearCheckpointStateInTablets() {
  auto tablets = indexed_table_->GetTablets();
  std::vector<TabletInfo*> tablet_ptrs;
  for (scoped_refptr<TabletInfo>& tablet : tablets) {
    tablet_ptrs.push_back(tablet.get());
    tablet->mutable_metadata()->StartMutation();
    auto& pb = tablet->mutable_metadata()->mutable_dirty()->pb;
    for (const auto& idx : requested_index_ids_) {
      pb.mutable_backfilled_until()->erase(idx);
    }
  }
  RETURN_NOT_OK_PREPEND(
      master()->catalog_manager()->sys_catalog()->Upsert(leader_term(), tablet_ptrs),
      "Could not persist that the table is done backfilling.");
  for (scoped_refptr<TabletInfo>& tablet : tablets) {
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
    RETURN_NOT_OK_PREPEND(master_->catalog_manager_impl()->sys_catalog_->Upsert(
                              leader_term(), indexed_table_),
                          "Could not clear backfilling timestamp.");
    l.Commit();
  }
  VLOG_WITH_PREFIX(2) << "Cleared backfilling timestamp.";
  return Status::OK();
}

Status BackfillTable::AllowCompactionsToGCDeleteMarkers(
    const TableId &index_table_id) {
  DVLOG(3) << __PRETTY_FUNCTION__;
  auto res = master_->catalog_manager()->FindTableById(index_table_id);
  if (!res && res.status().IsNotFound()) {
    LOG(ERROR) << "Index " << index_table_id << " was not found."
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
      VLOG(2) << __func__ << ": Trying to lock index table for Read";
      auto l = index_table_info->LockForRead();
      auto state = l->pb.state();
      if (!l->is_running()) {
        LOG(ERROR) << "Index " << index_table_id << " is in state "
                   << SysTablesEntryPB_State_Name(state) << " : cannot enable compactions on it";
        // Treating it as success so that we can proceed with updating other indexes.
        return Status::OK();
      }
      is_ready = state == SysTablesEntryPB::RUNNING;
    }
    VLOG(2) << __func__ << ": Unlocked index table for Read";
  } while (!is_ready);
  {
    TRACE("Locking index table");
    VLOG(2) << __func__ << ": Trying to lock index table for Write";
    auto l = index_table_info->LockForWrite();
    VLOG(2) << __func__ << ": locked index table for Write";
    l.mutable_data()->pb.mutable_schema()->mutable_table_properties()
        ->set_retain_delete_markers(false);

    // Update sys-catalog with the new indexed table info.
    TRACE("Updating index table metadata on disk");
    RETURN_NOT_OK_PREPEND(
        master_->catalog_manager_impl()->sys_catalog_->Upsert(
            leader_term(), index_table_info),
        yb::Format(
            "Could not update index_table_info for $0 to enable compactions.",
            index_table_id));

    // Update the in-memory state.
    TRACE("Committing in-memory state");
    l.Commit();
  }
  VLOG(2) << __func__ << ": Unlocked index table for Read";
  VLOG(1) << "Sending backfill done requests to the Index table";
  RETURN_NOT_OK(SendRpcToAllowCompactionsToGCDeleteMarkers(index_table_info));
  VLOG(1) << "DONE Sending backfill done requests to the Index table";
  return Status::OK();
}

Status BackfillTable::SendRpcToAllowCompactionsToGCDeleteMarkers(
    const scoped_refptr<TableInfo> &table) {
  auto tablets = table->GetTablets();

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    RETURN_NOT_OK(SendRpcToAllowCompactionsToGCDeleteMarkers(tablet, table->id()));
  }
  return Status::OK();
}

Status BackfillTable::SendRpcToAllowCompactionsToGCDeleteMarkers(
    const scoped_refptr<TabletInfo> &tablet, const std::string &table_id) {
  auto call = std::make_shared<AsyncBackfillDone>(master_, callback_pool_, tablet, table_id);
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
    std::shared_ptr<BackfillTable> backfill_table, const scoped_refptr<TabletInfo>& tablet)
    : backfill_table_(backfill_table), tablet_(tablet) {
  const auto& index_ids = backfill_table->indexes_to_build();
  {
    auto l = tablet_->LockForRead();
    const auto& pb = tablet_->metadata().state().pb;
    Partition::FromPB(pb.partition(), &partition_);
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
    VLOG_WITH_PREFIX(1) << " resuming backfill from "
                        << yb::ToString(backfilled_until_);
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
    VLOG_WITH_PREFIX(2) << "Launching next chunk from " << backfilled_until_;
    auto chunk = std::make_shared<BackfillChunk>(shared_from_this(),
                                                 backfilled_until_);
    return chunk->Launch();
  }
  return Status::OK();
}

Status BackfillTablet::Done(
    const Status& status, const boost::optional<string>& backfilled_until,
    const uint64_t number_rows_processed, const std::unordered_set<TableId>& failed_indexes) {
  if (!status.ok()) {
    LOG(INFO) << "Failed to backfill the tablet " << yb::ToString(tablet_) << ": " << status
              << "\nFailed_indexes are " << yb::ToString(failed_indexes);
    RETURN_NOT_OK(backfill_table_->Done(status, failed_indexes));
  }

  if (backfilled_until) {
    RETURN_NOT_OK_PREPEND(
        UpdateBackfilledUntil(*backfilled_until, number_rows_processed),
        "Could not persist how far the tablet is done backfilling.");
  }

  return LaunchNextChunkOrDone();
}

Status BackfillTablet::UpdateBackfilledUntil(
    const string& backfilled_until, const uint64_t number_rows_processed) {
  backfilled_until_ = backfilled_until;
  VLOG_WITH_PREFIX(2) << "Done backfilling the tablet " << yb::ToString(tablet_) << " until "
                      << yb::ToString(backfilled_until_);
  {
    auto l = tablet_->LockForWrite();
    for (const auto& idx_id : backfill_table_->indexes_to_build()) {
      l.mutable_data()->pb.mutable_backfilled_until()->insert({idx_id, backfilled_until_});
    }
    RETURN_NOT_OK(backfill_table_->master()->catalog_manager()->sys_catalog()->Upsert(
        backfill_table_->leader_term(), tablet_));
    l.Commit();
  }

  // This is the last chunk.
  if (backfilled_until_.empty()) {
    LOG(INFO) << "Done backfilling the tablet " << yb::ToString(tablet_);
    done_.store(true, std::memory_order_release);
  }
  return backfill_table_->UpdateRowsProcessedForIndexTable(number_rows_processed);
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
  VLOG(1) << __PRETTY_FUNCTION__;
  tserver::GetSafeTimeRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(tablet_->tablet_id());
  auto now = backfill_table_->master()->clock()->Now().ToUint64();
  req.set_min_hybrid_time_for_backfill(min_cutoff_.ToUint64());
  req.set_propagated_hybrid_time(now);

  ts_admin_proxy_->GetSafeTimeAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

void GetSafeTimeForTablet::HandleResponse(int attempt) {
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
      LOG(ERROR) << "GetSafeTime for " << tablet_->ToString() << " got " << safe_time;
    } else {
      VLOG(3) << "GetSafeTime for " << tablet_->ToString() << " got " << safe_time;
    }
  }
  WARN_NOT_OK(backfill_table_->UpdateSafeTime(status, safe_time),
    "Could not UpdateSafeTime");
}

TabletServerId GetSafeTimeForTablet::permanent_uuid() {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

BackfillChunk::BackfillChunk(std::shared_ptr<BackfillTablet> backfill_tablet,
                             const std::string& start_key)
    : RetryingTSRpcTask(backfill_tablet->master(),
                        backfill_tablet->threadpool(),
                        std::unique_ptr<TSPicker>(new PickLeaderReplica(backfill_tablet->tablet())),
                        backfill_tablet->tablet()->table().get(),
                        /* async_task_throttler */ nullptr),
      indexes_being_backfilled_(backfill_tablet->indexes_to_build()),
      backfill_tablet_(backfill_tablet),
      start_key_(start_key),
      requested_index_names_(RetrieveIndexNames(backfill_tablet->master()->catalog_manager_impl(),
                                                indexes_being_backfilled_)) {
  deadline_ = MonoTime::Max(); // Never time out.
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
                  backfill_tablet_->tablet().get()->ToString()));

  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  if (status.ok()) {
    LOG(INFO) << "Started BackfillChunk : " << this->description();
  }
  return Status::OK();
}

MonoTime BackfillChunk::ComputeDeadline() {
  MonoTime timeout = MonoTime::Now();
  if (GetTableType() == TableType::PGSQL_TABLE_TYPE) {
    timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_ysql_index_backfill_rpc_timeout_ms));
  } else {
    DCHECK(GetTableType() == TableType::YQL_TABLE_TYPE);
    timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_index_backfill_rpc_timeout_ms));
  }
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
  VLOG(1) << __PRETTY_FUNCTION__;
  if (indexes_being_backfilled_.empty()) {
    TransitionToCompleteState();
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

  if (resp_.has_backfilled_until()) {
    WARN_NOT_OK(
        backfill_tablet_->Done(
            status, resp_.backfilled_until(), resp_.number_rows_processed(), failed_indexes),
        "Failed marking BackfillTablet as done.");
  } else {
    WARN_NOT_OK(
        backfill_tablet_->Done(status, boost::none, resp_.number_rows_processed(), failed_indexes),
        "Failed marking BackfillTablet as done.");
  }
}

TabletServerId BackfillChunk::permanent_uuid() {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

}  // namespace master
}  // namespace yb
