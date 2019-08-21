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

#include <stdlib.h>

#include <algorithm>
#include <bitset>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include <glog/logging.h>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "yb/common/common_flags.h"
#include "yb/common/partial_row.h"
#include "yb/common/partition.h"
#include "yb/common/roles_permissions.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/quorum_util.h"
#include "yb/gutil/atomicops.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/walltime.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_loaders.h"
#include "yb/master/catalog_manager_bg_tasks.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/system_tablet.h"
#include "yb/master/tasks_tracker.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/yql_aggregates_vtable.h"
#include "yb/master/yql_auth_resource_role_permissions_index.h"
#include "yb/master/yql_auth_role_permissions_vtable.h"
#include "yb/master/yql_auth_roles_vtable.h"
#include "yb/master/yql_columns_vtable.h"
#include "yb/master/yql_empty_vtable.h"
#include "yb/master/yql_functions_vtable.h"
#include "yb/master/yql_indexes_vtable.h"
#include "yb/master/yql_keyspaces_vtable.h"
#include "yb/master/yql_local_vtable.h"
#include "yb/master/yql_partitions_vtable.h"
#include "yb/master/yql_peers_vtable.h"
#include "yb/master/yql_size_estimates_vtable.h"
#include "yb/master/yql_tables_vtable.h"
#include "yb/master/yql_triggers_vtable.h"
#include "yb/master/yql_types_vtable.h"
#include "yb/master/yql_views_vtable.h"

#include "yb/rpc/messenger.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/yql/redis/redisserver/redis_constants.h"

#include "yb/util/crypt.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/stopwatch.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/uuid.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/tserver/remote_bootstrap_client.h"

DEFINE_int32(index_backfill_rpc_timeout_ms, 1 * 60 * 60 * 1000, // 1 hour.
             "Timeout used by the master when attempting to backfilll a tablet "
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

DEFINE_test_flag(int32, TEST_slowdown_backfill_alter_table_rpcs_ms, 0,
    "Slows down the send alter table rpc's so that the master may be stopped between "
    "different phases.");

namespace yb {
namespace master {

using namespace std::literals;
using strings::Substitute;
using tserver::TabletServerErrorPB;

Status MultiStageAlterTable::UpdateIndexPermission(
    CatalogManager *catalog_manager,
    const scoped_refptr<TableInfo>& indexed_table,
    const TableId& index_table_id,
    IndexPermissions new_perm) {
  DVLOG(3) << __PRETTY_FUNCTION__ << yb::ToString(*indexed_table);
  if (FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms > 0) {
    TRACE("Sleeping for  $0 ms", FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms);
    DVLOG(3) << __PRETTY_FUNCTION__ << yb::ToString(*indexed_table) << " sleeping for "
             << FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms
             << "ms BEFORE updating the index permission to " << IndexPermissions_Name(new_perm);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms));
    DVLOG(3) << __PRETTY_FUNCTION__ << "Done Sleeping";
    TRACE("Done Sleeping");
  }
  {
    TRACE("Locking indexed table");
    auto l = indexed_table->LockForWrite();
    auto &indexed_table_data = *l->mutable_data();

    indexed_table_data.pb.mutable_fully_applied_schema()->CopyFrom(
        indexed_table_data.pb.schema());
    VLOG(1) << "Setting fully_applied_schema_version to "
            << indexed_table_data.pb.version();
    indexed_table_data.pb.set_fully_applied_schema_version(
        indexed_table_data.pb.version());
    indexed_table_data.pb.mutable_fully_applied_indexes()->CopyFrom(
        indexed_table_data.pb.indexes());
    if (indexed_table_data.pb.has_index_info()) {
      indexed_table_data.pb.mutable_fully_applied_index_info()->CopyFrom(
          indexed_table_data.pb.index_info());
    }

    bool updated = false;
    auto old_schema_version = indexed_table_data.pb.version();
    for (int i = 0; i < indexed_table_data.pb.indexes_size(); i++) {
      IndexInfoPB *idx_pb = indexed_table_data.pb.mutable_indexes(i);
      if (idx_pb->table_id() == index_table_id) {
        IndexPermissions old_perm = idx_pb->index_permissions();
        if (old_perm == new_perm) {
          LOG(INFO) << "The index permission"
                    << " for index table id : " << yb::ToString(index_table_id)
                    << " seems to have already been updated to "
                    << IndexPermissions_Name(new_perm)
                    << " by somebody else. This is OK.";
          return STATUS_SUBSTITUTE(
              AlreadyPresent, "IndexPermissions for $0 is already $1", index_table_id, new_perm);
        }
        idx_pb->set_index_permissions(new_perm);
        VLOG(1) << "Updating index permissions "
                << " from " << IndexPermissions_Name(old_perm) << " to "
                << IndexPermissions_Name(new_perm) << " schema_version from " << old_schema_version
                << " to " << old_schema_version + 1 << ". New index info would be "
                << yb::ToString(idx_pb);
        updated = true;
        break;
      }
    }

    if (!updated) {
      LOG(WARNING) << "Could not find the desired index "
                   << yb::ToString(index_table_id)
                   << " to update in the indexed_table "
                   << yb::ToString(indexed_table_data.pb)
                   << "\nThis may be OK, if the index was deleted.";
      return STATUS_SUBSTITUTE(
          InvalidArgument, "Could not find the desired index $0", index_table_id);
    }

    VLOG(1) << "Before updating indexed_table_data.pb.version() is "
            << indexed_table_data.pb.version();
    indexed_table_data.pb.set_version(indexed_table_data.pb.version() + 1);
    VLOG(1) << "After updating indexed_table_data.pb.version() is "
            << indexed_table_data.pb.version();
    indexed_table_data.set_state(SysTablesEntryPB::ALTERING,
                                 Substitute("Alter table version=$0 ts=$1",
                                            indexed_table_data.pb.version(),
                                            LocalTimeAsString()));

    // Update sys-catalog with the new indexed table info.
    TRACE("Updating indexed table metadata on disk");
    RETURN_NOT_OK(catalog_manager->sys_catalog_->UpdateItem(
        indexed_table.get(), catalog_manager->leader_ready_term_));

    // Update the in-memory state.
    TRACE("Committing in-memory state");
    l->Commit();
  }
  if (PREDICT_FALSE(FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms > 0)) {
    TRACE("Sleeping for $0 ms",
          FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms);
    DVLOG(3) << __PRETTY_FUNCTION__ << yb::ToString(*indexed_table) << " sleeping for "
             << FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms
             << "ms AFTER updating the index permission to " << IndexPermissions_Name(new_perm);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms));
    DVLOG(3) << __PRETTY_FUNCTION__ << "Done Sleeping";
    TRACE("Done Sleeping");
  }
  return Status::OK();
}

void MultiStageAlterTable::StartBackfillingData(
    CatalogManager *catalog_manager,
    const scoped_refptr<TableInfo> &indexed_table, const IndexInfoPB index_pb) {
  if (!indexed_table->IsBackfilling()) {
    VLOG(1) << __func__ << " starting backfill for "
            << indexed_table->ToString();
    indexed_table->SetIsBackfilling(true);
    auto backfill_table = std::make_shared<BackfillTable>(
        catalog_manager->master_, catalog_manager->worker_pool_.get(),
        indexed_table, std::vector<IndexInfoPB>{index_pb});
    backfill_table->Launch();
  } else {
    LOG(WARNING) << __func__ << " Not starting backfill for "
                 << indexed_table->ToString() << " one is already in progress";
  }
}

bool MultiStageAlterTable::LaunchNextTableInfoVersionIfNecessary(
    CatalogManager* catalog_manager, const scoped_refptr<TableInfo>& indexed_table) {
  DVLOG(3) << __PRETTY_FUNCTION__ << yb::ToString(*indexed_table);

  // Add index info to indexed table and increment schema version.
  bool updated = false;
  IndexInfoPB index_info_to_update;
  {
    TRACE("Locking indexed table");
    VLOG(1) << ("Locking indexed table");
    auto l = indexed_table->LockForRead();
    for (int i = 0; i < l->data().pb.indexes_size(); i++) {
      const IndexInfoPB& idx_pb = l->data().pb.indexes(i);
      if (idx_pb.has_index_permissions() &&
          idx_pb.index_permissions() < INDEX_PERM_READ_WRITE_AND_DELETE) {
        index_info_to_update = idx_pb;
        // Until we get to #2784, we'll have only one index being built at a time.
        LOG_IF(DFATAL, updated) << "For now, we cannot have multiple indices build in parallel.";
        updated = true;
      }
    }
  }

  if (!updated) {
    TRACE("Not necessary to launch next version");
    VLOG(1) << "Not necessary to launch next version : " << yb::ToString(index_info_to_update);
    return false;
  }

  const IndexPermissions old_perm = index_info_to_update.index_permissions();
  if (old_perm == INDEX_PERM_DELETE_ONLY || old_perm == INDEX_PERM_WRITE_AND_DELETE) {
    const IndexPermissions new_perm =
        (old_perm == INDEX_PERM_DELETE_ONLY ? INDEX_PERM_WRITE_AND_DELETE : INDEX_PERM_DO_BACKFILL);
    if (UpdateIndexPermission(
            catalog_manager, indexed_table, index_info_to_update.table_id(), new_perm)
            .ok()) {
      catalog_manager->SendAlterTableRequest(indexed_table);
    }
  } else {
    LOG_IF(DFATAL, old_perm != INDEX_PERM_DO_BACKFILL)
        << "Expect the old permission to be "
        << "INDEX_PERM_DO_BACKFILL found " << IndexPermissions_Name(old_perm)
        << " instead.";
    TRACE("Starting backfill process");
    VLOG(1) << ("Starting backfill process");
    StartBackfillingData(catalog_manager, indexed_table.get(), index_info_to_update);
  }
  return true;
}

// -----------------------------------------------------------------------------------------------
// BackfillTable
// -----------------------------------------------------------------------------------------------
void BackfillTable::Launch() {
  LaunchComputeSafeTimeForRead();
}

void BackfillTable::LaunchComputeSafeTimeForRead() {
  vector<scoped_refptr<TabletInfo>> tablets;
  indexed_table_->GetAllTablets(&tablets);

  done_.store(false, std::memory_order_release);
  tablets_pending_.store(tablets.size(), std::memory_order_release);
  auto min_cutoff = master()->clock()->Now();
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto get_safetime = std::make_shared<GetSafeTimeForTablet>(
        shared_from_this(), tablet, min_cutoff);
    get_safetime->Launch();
  }
}

std::string BackfillTable::LogPrefix() const {
  const TableId &index_table_id = indices()[0].table_id();
  return Format("Backfill Index Table(s) <$0>", index_table_id);
}

void BackfillTable::UpdateSafeTime(const Status& s, HybridTime ht) {
  if (!s.ok()) {
    // Move on to ABORTED permission.
    LOG_WITH_PREFIX(ERROR)
        << "Failed backfill. Could not compute safe time for "
        << yb::ToString(indexed_table_) << s;
    if (!done_.exchange(true)) {
      AlterTableStateToAbort();
    } else {
      LOG_WITH_PREFIX(INFO)
          << "Somebody else already aborted the index backfill.";
    }
    return;
  }

  // Need to guard this.
  {
    std::lock_guard<simple_spinlock> l(mutex_);
    VLOG(2) << " Updating read_time_for_backfill_ to max{ "
            << read_time_for_backfill_.ToString() << ", " << ht.ToString()
            << " }.";
    read_time_for_backfill_.MakeAtLeast(ht);
  }

  // If OK then move on to READ permissions.
  if (!done() && --tablets_pending_ == 0) {
    {
      std::lock_guard<simple_spinlock> l(mutex_);
      LOG_WITH_PREFIX(INFO) << "Completed fetching SafeTime for the table "
                            << yb::ToString(indexed_table_) << " will be using "
                            << read_time_for_backfill_.ToString();
    }
    LaunchBackfill();
  }
}

void BackfillTable::LaunchBackfill() {
  vector<scoped_refptr<TabletInfo>> tablets;
  indexed_table_->GetAllTablets(&tablets);

  done_.store(false, std::memory_order_release);
  // TODO: Keep track of explicit tablet names to handle RPC retries.
  tablets_pending_.store(tablets.size(), std::memory_order_release);
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto backfill_tablet = std::make_shared<BackfillTablet>(shared_from_this(), tablet);
    backfill_tablet->Launch();
  }
}

void BackfillTable::Done(const Status& s) {
  if (!s.ok()) {
    // Move on to ABORTED permission.
    LOG_WITH_PREFIX(ERROR) << "Failed to backfill the index " << s;
    if (!done_.exchange(true)) {
      AlterTableStateToAbort();
    } else {
      LOG_WITH_PREFIX(INFO)
          << "Some body else already aborted the index backfill.";
    }
    return;
  }

  // If OK then move on to READ permissions.
  if (!done() && --tablets_pending_ == 0) {
    LOG_WITH_PREFIX(INFO) << "Completed backfilling the index table.";
    done_.store(true, std::memory_order_release);
    AlterTableStateToSuccess();
  }
}

void BackfillTable::AlterTableStateToSuccess() {
  const TableId& index_table_id = indices()[0].table_id();
  if (MultiStageAlterTable::UpdateIndexPermission(
          master_->catalog_manager(), indexed_table_, index_table_id,
          INDEX_PERM_READ_WRITE_AND_DELETE).ok()) {
    VLOG(1) << "Sending alter table requests to the Indexed table";
    master_->catalog_manager()->SendAlterTableRequest(indexed_table_);
    VLOG(1) << "DONE Sending alter table requests to the Indexed table";
    AllowCompactionsToGCDeleteMarkers(index_table_id);
  }
  indexed_table_->SetIsBackfilling(false);
  ClearCheckpointStateInTablets();
}

void BackfillTable::AlterTableStateToAbort() {
  const TableId& index_table_id = indices()[0].table_id();
  MultiStageAlterTable::UpdateIndexPermission(
      master_->catalog_manager(), indexed_table_, index_table_id, INDEX_PERM_BACKFILL_FAILED);
  master_->catalog_manager()->SendAlterTableRequest(indexed_table_);
  indexed_table_->SetIsBackfilling(false);
  ClearCheckpointStateInTablets();
}

void BackfillTable::ClearCheckpointStateInTablets() {
  vector<scoped_refptr<TabletInfo>> tablets;
  indexed_table_->GetAllTablets(&tablets);
  std::vector<TabletInfo*> tablet_ptrs;
  for (scoped_refptr<TabletInfo>& tablet : tablets) {
    tablet_ptrs.push_back(tablet.get());
    tablet->mutable_metadata()->StartMutation();
    tablet->mutable_metadata()->mutable_dirty()->pb.clear_backfilled_until();
  }
  auto term = master()->catalog_manager()->leader_ready_term();
  WARN_NOT_OK(
      master()->catalog_manager()->sys_catalog()->UpdateItems(tablet_ptrs, term),
      "Could not persist that the table is done backfilling.");
  for (scoped_refptr<TabletInfo>& tablet : tablets) {
    VLOG(2) << "Done backfilling the table. " << yb::ToString(tablet)
            << " clearing backfilled_until";
    tablet->mutable_metadata()->CommitMutation();
  }
}

void BackfillTable::AllowCompactionsToGCDeleteMarkers(const TableId& index_table_id) {
  DVLOG(3) << __PRETTY_FUNCTION__;
  scoped_refptr<TableInfo> index_table_info;
  TableIdentifierPB index_table_id_pb;
  index_table_id_pb.set_table_id(index_table_id);
  {
    if (!master_->catalog_manager()->FindTable(index_table_id_pb, &index_table_info).ok()) {
      LOG_WITH_PREFIX(ERROR)
          << "Could not find table info for the index table "
          << yb::ToString(index_table_id) << " to enable compactions. "
          << "This is ok in case somebody issued a delete index.";
      return;
    }
  }
  // Add a sleep here to wait until the Table is fully created.
  bool is_ready = false;
  bool first_run = true;
  do {
    if (!first_run) {
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Waiting for the previous alter table to "
                                      "complete on the index table "
                                   << yb::ToString(index_table_id);
      SleepFor(
          MonoDelta::FromMilliseconds(FLAGS_index_backfill_wait_for_alter_table_completion_ms));
    }
    first_run = false;
    {
      VLOG(2) << __func__ << ": Trying to lock index table for Read";
      auto l = index_table_info->LockForRead();
      is_ready = (l->data().pb.state() == SysTablesEntryPB::RUNNING);
    }
    VLOG(2) << __func__ << ": Unlocked index table for Read";
  } while (!is_ready);
  {
    TRACE("Locking index table");
    VLOG(2) << __func__ << ": Trying to lock index table for Write";
    auto l = index_table_info->LockForWrite();
    VLOG(2) << __func__ << ": locked index table for Write";
    l->mutable_data()->pb.mutable_schema()->mutable_table_properties()->set_is_backfilling(false);

    // Update sys-catalog with the new indexed table info.
    TRACE("Updating index table metadata on disk");
    const auto leader_term = master_->catalog_manager()->leader_ready_term_;
    auto status =
        master_->catalog_manager()->sys_catalog_->UpdateItem(index_table_info.get(), leader_term);
    if (!status.ok()) {
      LOG_WITH_PREFIX(ERROR) << "Could not update index_table_info for "
                             << index_table_id
                             << " to enable compactions: " << status;
      return;
    }

    // Update the in-memory state.
    TRACE("Committing in-memory state");
    l->Commit();
  }
  VLOG(2) << __func__ << ": Unlocked index table for Read";
  VLOG(1) << "Sending backfill done requests to the Index table";
  SendRpcToAllowCompactionsToGCDeleteMarkers(index_table_info);
  VLOG(1) << "DONE Sending backfill done requests to the Index table";
}

void BackfillTable::SendRpcToAllowCompactionsToGCDeleteMarkers(
    const scoped_refptr<TableInfo>& table) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    SendRpcToAllowCompactionsToGCDeleteMarkers(tablet);
  }
}

void BackfillTable::SendRpcToAllowCompactionsToGCDeleteMarkers(
    const scoped_refptr<TabletInfo>& tablet) {
  auto call = std::make_shared<AsyncBackfillDone>(master_, callback_pool_, tablet);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send backfill done request");
}

// -----------------------------------------------------------------------------------------------
// BackfillTablet
// -----------------------------------------------------------------------------------------------
BackfillTablet::BackfillTablet(
    std::shared_ptr<BackfillTable> backfill_table, const scoped_refptr<TabletInfo>& tablet)
    : backfill_table_(backfill_table), tablet_(tablet) {
  {
    auto l = tablet_->LockForRead();
    Partition::FromPB(tablet_->metadata().state().pb.partition(), &partition_);
    if (tablet_->metadata().state().pb.has_backfilled_until()) {
      chunk_start_ = tablet_->metadata().state().pb.backfilled_until();
    } else {
      chunk_start_ = partition_.partition_key_start();
    }
  }
  if (chunk_start_ != partition_.partition_key_start()) {
    VLOG(1) << tablet_->ToString() << " resuming backfill from " << yb::ToString(chunk_start_)
            << " (instead of begining from " << yb::ToString(partition_.partition_key_start());
  } else {
    VLOG(1) << tablet_->ToString() << " begining backfill from " << yb::ToString(chunk_start_);
  }
  chunk_end_ = GetChunkEnd(chunk_start_);
}

void BackfillTablet::LaunchNextChunk() {
  if (!backfill_table_->done()) {
    auto chunk = std::make_shared<BackfillChunk>(shared_from_this(), chunk_start_, chunk_end_);
    chunk->Launch();
  }
}

void BackfillTablet::Done(const Status& status) {
  if (!status.ok()) {
    LOG(INFO) << "Failed to backfill the tablet " << yb::ToString(tablet_) << status;
    backfill_table_->Done(status);
    return;
  }

  processed_until_ = chunk_end_;
  VLOG(2) << "Done backfilling the tablet " << yb::ToString(tablet_) << " until "
          << yb::ToString(processed_until_);
  {
    tablet_->mutable_metadata()->StartMutation();
    tablet_->mutable_metadata()->mutable_dirty()->pb.set_backfilled_until(processed_until_);
    auto term = backfill_table_->master()->catalog_manager()->leader_ready_term();
    WARN_NOT_OK(
        backfill_table_->master()->catalog_manager()->sys_catalog()->UpdateItem(
            tablet_.get(), term),
        "Could not persist that the tablet is done backfilling.");
    tablet_->mutable_metadata()->CommitMutation();
  }

  chunk_start_ = processed_until_;
  chunk_end_ = GetChunkEnd(chunk_start_);

  // This is the last chunk.
  if (chunk_start_ == chunk_end_) {
    LOG(INFO) << "Done backfilling the tablet " << yb::ToString(tablet_);
    backfill_table_->Done(status);
    return;
  }

  LaunchNextChunk();
}

// -----------------------------------------------------------------------------------------------
// GetSafeTimeForTablet
// -----------------------------------------------------------------------------------------------

void GetSafeTimeForTablet::Launch() {
  tablet_->table()->AddTask(shared_from_this());
  Status status = Run();
  WARN_NOT_OK(status, Substitute("Failed to send GetSafeTime request for $0", tablet_->ToString()));

  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  if (status.ok()) {
    VLOG(3) << "Started GetSafeTimeForTablet : " << this->description();
  }
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
                     << tablet_->ToString() << " no further retry: " << status.ToString();
        TransitionToTerminalState(MonitoredTaskState::kRunning, MonitoredTaskState::kFailed);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": GetSafeTime failed for tablet "
                     << tablet_->ToString() << ": " << status.ToString() << " code "
                     << resp_.error().code();
        break;
    }
  } else {
    TransitionToTerminalState(MonitoredTaskState::kRunning, MonitoredTaskState::kComplete);
    // 1
    VLOG(1) << "TS " << permanent_uuid() << ": GetSafeTime complete on tablet "
            << tablet_->ToString();
  }

  server::UpdateClock(resp_, master_->clock());
}

void GetSafeTimeForTablet::UnregisterAsyncTaskCallback() {
  Status status;
  HybridTime safe_time;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
    VLOG(3) << "GetSafeTime for " << tablet_->ToString() << " got an error. Returning "
            << safe_time;
  } else {
    safe_time = HybridTime(resp_.safe_time());
    if (safe_time.is_special()) {
      LOG(ERROR) << "GetSafeTime for " << tablet_->ToString() << " got " << safe_time;
    } else {
      VLOG(3) << "GetSafeTime for " << tablet_->ToString() << " got " << safe_time;
    }
  }
  backfill_table_->UpdateSafeTime(status, safe_time);
}

// -----------------------------------------------------------------------------------------------
// BackfillChunk
// -----------------------------------------------------------------------------------------------
void BackfillChunk::Launch() {
  backfill_tablet_->tablet()->table()->AddTask(shared_from_this());
  Status status = Run();
  WARN_NOT_OK(
      status, Substitute(
                  "Failed to send backfill Chunk request for $0",
                  backfill_tablet_->tablet().get()->ToString()));

  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  if (status.ok()) {
    LOG(INFO) << "Started BackfillChunk : " << this->description();
  }
}

MonoTime BackfillChunk::ComputeDeadline() {
  MonoTime timeout = MonoTime::Now();
  // We expect this RPC to take a long time.
  // Allow this RPC  about 1 hour before retrying it.
  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_index_backfill_rpc_timeout_ms));
  return MonoTime::Earliest(timeout, deadline_);
}

int BackfillChunk::num_max_retries() {
  return FLAGS_index_backfill_rpc_max_retries;
}

int BackfillChunk::max_delay_ms() {
  return FLAGS_index_backfill_rpc_max_delay_ms;
}

bool BackfillChunk::SendRequest(int attempt) {
  VLOG(1) << __PRETTY_FUNCTION__;
  tserver::BackfillIndexRequestPB req;
  req.set_dest_uuid(permanent_uuid());
  req.set_tablet_id(backfill_tablet_->tablet()->tablet_id());
  req.set_read_at_hybrid_time(backfill_tablet_->read_time_for_backfill().ToUint64());
  req.set_schema_version(backfill_tablet_->schema_version());
  req.set_start_key(start_key_);
  req.set_end_key(end_key_);
  for (const IndexInfoPB& idx_info : backfill_tablet_->indices()) {
    req.add_indexes()->CopyFrom(idx_info);
  }
  req.set_propagated_hybrid_time(backfill_tablet_->master()->clock()->Now().ToUint64());

  ts_admin_proxy_->BackfillIndexAsync(req, &resp_, &rpc_, BindRpcCallback());
  // 1
  VLOG(1) << "Send " << description() << " to " << permanent_uuid()
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

void BackfillChunk::HandleResponse(int attempt) {
  VLOG(1) << __PRETTY_FUNCTION__;
  Status status;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
      case TabletServerErrorPB::MISMATCHED_SCHEMA:
      case TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA:
      case TabletServerErrorPB::OPERATION_NOT_SUPPORTED:
        LOG(WARNING) << "TS " << permanent_uuid() << ": backfill failed for tablet "
                     << backfill_tablet_->tablet()->ToString()
                     << " no further retry: " << status.ToString();
        TransitionToTerminalState(MonitoredTaskState::kRunning, MonitoredTaskState::kFailed);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": backfill failed for tablet "
                     << backfill_tablet_->tablet()->ToString() << ": " << status.ToString()
                     << " code " << resp_.error().code();
        break;
    }
  } else {
    TransitionToTerminalState(MonitoredTaskState::kRunning, MonitoredTaskState::kComplete);
    // 1
    VLOG(1) << "TS " << permanent_uuid() << ": backfill complete on tablet "
            << backfill_tablet_->tablet()->ToString();
  }

  server::UpdateClock(resp_, master_->clock());
}

void BackfillChunk::UnregisterAsyncTaskCallback() {
  Status status;
  if (resp_.has_error()) {
    status = StatusFromPB(resp_.error().status());
  }
  backfill_tablet_->Done(status);
}

}  // namespace master
}  // namespace yb
