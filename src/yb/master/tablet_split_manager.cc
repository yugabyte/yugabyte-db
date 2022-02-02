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

#include <chrono>

#include "yb/common/constants.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/cdc_consumer_split_driver.h"
#include "yb/master/master_error.h"
#include "yb/master/tablet_split_manager.h"

#include "yb/server/monitored_task.h"

#include "yb/util/flag_tags.h"
#include "yb/util/result.h"

DEFINE_int32(process_split_tablet_candidates_interval_msec, 0,
             "The minimum time between automatic splitting attempts. The actual splitting time "
             "between runs is also affected by catalog_manager_bg_task_wait_ms, which controls how "
             "long the bg tasks thread sleeps at the end of each loop. The top-level automatic "
             "tablet splitting method, which checks for the time since last run, is run once per "
             "loop.");
DEFINE_int32(max_queued_split_candidates, 0,
             "DEPRECATED. The max number of pending tablet split candidates we will hold onto. We "
             "potentially iterate through every candidate in the queue for each tablet we process "
             "in a tablet report so this size should be kept relatively small to avoid any "
             "issues.");

DECLARE_bool(enable_automatic_tablet_splitting);

DEFINE_uint64(outstanding_tablet_split_limit, 5,
              "Limit of the number of outstanding tablet splits. Limitation is disabled if this "
              "value is set to 0.");

DECLARE_bool(TEST_validate_all_tablet_candidates);

DEFINE_bool(enable_tablet_split_of_pitr_tables, true,
            "When set, it enables automatic tablet splitting of tables covered by "
            "Point In Time Restore schedules.");
TAG_FLAG(enable_tablet_split_of_pitr_tables, runtime);

DEFINE_bool(enable_tablet_split_of_xcluster_replicated_tables, false,
            "When set, it enables automatic tablet splitting for tables that are part of an "
            "xCluster replication setup");
TAG_FLAG(enable_tablet_split_of_xcluster_replicated_tables, runtime);
TAG_FLAG(enable_tablet_split_of_xcluster_replicated_tables, hidden);

DEFINE_uint64(tablet_split_limit_per_table, 256,
              "Limit of the number of tablets per table for tablet splitting. Limitation is "
              "disabled if this value is set to 0.");

namespace yb {
namespace master {

using strings::Substitute;
using namespace std::literals;

TabletSplitManager::TabletSplitManager(
    TabletSplitCandidateFilterIf* filter,
    TabletSplitDriverIf* driver,
    CDCConsumerSplitDriverIf* cdc_consumer_split_driver):
    filter_(filter),
    driver_(driver),
    cdc_consumer_split_driver_(cdc_consumer_split_driver),
    last_run_time_(CoarseDuration::zero()) {}

Status TabletSplitManager::ValidateSplitCandidateTable(const TableInfo& table) {
  if (PREDICT_FALSE(FLAGS_TEST_validate_all_tablet_candidates)) {
    return Status::OK();
  }
  if (table.is_deleted()) {
    VLOG(1) << Substitute("Table is deleted; ignoring for splitting. table_id: $0", table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Table is deleted; ignoring for splitting. table_id: $0", table.id());
  }
  // Check if this table is covered by a PITR schedule.
  if (!FLAGS_enable_tablet_split_of_pitr_tables &&
      VERIFY_RESULT(filter_->IsTablePartOfSomeSnapshotSchedule(table))) {
    VLOG(1) << Substitute("Tablet splitting is not supported for tables that are a part of"
                          " some active PITR schedule, table_id: $0", table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for tables that are a part of"
        " some active PITR schedule, table_id: $0", table.id());
  }
  // Check if this table is part of a cdc stream.
  if (PREDICT_TRUE(!FLAGS_enable_tablet_split_of_xcluster_replicated_tables) &&
      filter_->IsCdcEnabled(table)) {
    VLOG(1) << Substitute("Tablet splitting is not supported for tables that are a part of"
                          " a CDC stream, table_id: $0", table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for tables that are a part of"
        " a CDC stream, tablet_id: $0", table.id());
  }
  if (table.GetTableType() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    VLOG(1) << Substitute("Tablet splitting is not supported for transaction status tables, "
                          "table_id: $0", table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for transaction status tables, table_id: $0",
        table.id());
  }
  if (table.GetTableType() == REDIS_TABLE_TYPE) {
    VLOG(1) << Substitute("Tablet splitting is not supported for YEDIS tables, table_id: $0",
                          table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for YEDIS tables, table_id: $0", table.id());
  }
  if (FLAGS_tablet_split_limit_per_table != 0 &&
      table.NumPartitions() >= FLAGS_tablet_split_limit_per_table) {
    // TODO(tsplit): Avoid tablet server of scanning tablets for the tables that already
    //  reached the split limit of tablet #6220
    VLOG(1) << Substitute("Too many tablets for the table, table_id: $0, limit: $1",
                          table.id(), FLAGS_tablet_split_limit_per_table);
    return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::REACHED_SPLIT_LIMIT),
                            "Too many tablets for the table, table_id: $0, limit: $1",
                            table.id(), FLAGS_tablet_split_limit_per_table);
  }
  if (table.IsBackfilling()) {
    VLOG(1) << Substitute("Backfill operation in progress, table_id: $0", table.id());
    return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS),
                            "Backfill operation in progress, table_id: $0", table.id());
  }
  return Status::OK();
}

Status TabletSplitManager::ValidateSplitCandidateTablet(const TabletInfo& tablet) {
  if (PREDICT_FALSE(FLAGS_TEST_validate_all_tablet_candidates)) {
    return Status::OK();
  }
  if (tablet.colocated()) {
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for colocated tables, tablet_id: $0",
        tablet.tablet_id());
  }
  {
    auto tablet_state = tablet.LockForRead()->pb.state();
    if (tablet_state != SysTabletsEntryPB::RUNNING) {
      return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::TABLET_NOT_RUNNING),
                              "Tablet is not in running state: $0",
                              tablet_state);
    }
  }
  return Status::OK();
}

unordered_set<TabletId> TabletSplitManager::FindSplitsWithTask(
    const vector<TableInfoPtr>& tables) {
  unordered_set<TabletId> splits_with_task;
  // These tasks will retry automatically until they succeed or fail.
  for (const auto& table : tables) {
    for (const auto& task : table->GetTasks()) {
      if (task->type() == yb::server::MonitoredTask::ASYNC_GET_TABLET_SPLIT_KEY ||
          task->type() == yb::server::MonitoredTask::ASYNC_SPLIT_TABLET) {
        splits_with_task.insert(static_cast<AsyncTabletLeaderTask*>(task.get())->tablet_id());
        if (splits_with_task.size() >= FLAGS_outstanding_tablet_split_limit) {
          return splits_with_task;
        }
      }
    }
  }
  return splits_with_task;
}

bool AllReplicasHaveFinishedCompaction(const TabletInfo& tablet_info) {
  auto replica_map = tablet_info.GetReplicaLocations();
  for (auto const& replica : *replica_map) {
    if (replica.second.drive_info.may_have_orphaned_post_split_data) {
      return false;
    }
  }
  return true;
}

bool IsCompactingSplit(const TabletInfo& tablet,
                       const MetadataCowWrapper<PersistentTabletInfo>::ReadLock& tablet_lock) {
  return (tablet_lock->pb.has_split_parent_tablet_id() &&
          tablet_lock->pb.state() == SysTabletsEntryPB::RUNNING &&
          !AllReplicasHaveFinishedCompaction(tablet));
}

bool IsSplitToRestart(const TabletInfo& tablet,
                      const MetadataCowWrapper<PersistentTabletInfo>::ReadLock& tablet_lock) {
  return tablet_lock->pb.has_split_parent_tablet_id() &&
         tablet_lock->pb.state() != SysTabletsEntryPB::RUNNING;
}

bool TabletSplitManager::ShouldSplitTablet(const TabletInfo& tablet) {
  auto tablet_lock = tablet.LockForRead();
  // If no leader for this tablet, skip it for now.
  auto drive_info_opt = tablet.GetLeaderReplicaDriveInfo();
  if (!drive_info_opt.ok()) {
    return false;
  }
  if (ValidateSplitCandidateTablet(tablet).ok() &&
      filter_->ShouldSplitValidCandidate(tablet, drive_info_opt.get()) &&
      AllReplicasHaveFinishedCompaction(tablet)) {
    return true;
  }
  return false;
}

void TabletSplitManager::ScheduleSplits(const unordered_set<TabletId>& splits_to_schedule) {
  for (const auto& tablet_id : splits_to_schedule) {
    auto s = driver_->SplitTablet(tablet_id, false /* select_all_tablets_for_split */);
    if (!s.ok()) {
      WARN_NOT_OK(s, Format("Failed to restart split for tablet_id: $0.", tablet_id));
    }
  }
}

void TabletSplitManager::DoSplitting(const TableInfoMap& table_info_map) {
  vector<TableInfoPtr> valid_tables;
  for (const auto& table : table_info_map) {
    if (ValidateSplitCandidateTable(*table.second).ok()) {
      valid_tables.push_back(table.second);
    }
  }

  // Process valid tables to find ongoing AsyncGetTabletSplitKey and AsyncSplitTablet requests.
  const unordered_set<TabletId> splits_with_task = FindSplitsWithTask(valid_tables);

  // Splits for which at least one child tablet is still undergoing compaction.
  unordered_set<TabletId> compacting_splits;
  // Splits that need to be started / restarted.
  unordered_set<TabletId> splits_to_schedule;
  // New split candidates. The chosen candidates are eventually added to splits_to_schedule.
  TabletInfos new_split_candidates;

  // Helper method to determine if more splits can be scheduled, or if we should exit early.
  const auto can_split_more = [&]() {
    uint64_t outstanding_splits = splits_with_task.size() +
                                  compacting_splits.size() +
                                  splits_to_schedule.size();
    return outstanding_splits < FLAGS_outstanding_tablet_split_limit;
  };

  for (const auto& table : valid_tables) {
    for (const auto& tablet : table->GetTablets()) {
      if (!can_split_more()) {
        break;
      }
      if (splits_with_task.count(tablet->id())) {
        continue;
      }

      auto tablet_lock = tablet->LockForRead();
      const TabletId& parent_id = tablet_lock->pb.split_parent_tablet_id();
      if (IsCompactingSplit(*tablet, tablet_lock)) {
        // This tablet is the child of a split and is still compacting. We assume that this split
        // will eventually complete for both tablets.
        compacting_splits.insert(parent_id);
      } else if (IsSplitToRestart(*tablet, tablet_lock)) {
        splits_to_schedule.insert(parent_id);
      } else {
        // Add new split candidates to a list. We add as many as we can into splits_to_schedule
        // after we have found any splits that need to be restarted.
        if (ShouldSplitTablet(*tablet)) {
          new_split_candidates.push_back(tablet);
        }
      }
      if (splits_to_schedule.count(parent_id) != 0 &&
          compacting_splits.count(parent_id) != 0) {
        // It's possible that one child subtablet leads us to insert the parent tablet id into
        // splits_to_schedule, and another leads us to insert into compacting_splits. In this case,
        // it means one of the children is live, thus both children have been created and the split
        // RPC does not need to be scheduled.
        splits_to_schedule.erase(parent_id);
      }
    }
    if (!can_split_more()) {
      break;
    }
  }

  // Add any new splits to the set of splits to schedule (while respecting the max number of
  // outstanding splits).
  for (const auto& tablet : new_split_candidates) {
    if (!can_split_more()) {
      break;
    }
    splits_to_schedule.insert(tablet->id());
  }

  ScheduleSplits(splits_to_schedule);
}

void TabletSplitManager::MaybeDoSplitting(const TableInfoMap& table_info_map) {
  if (!FLAGS_enable_automatic_tablet_splitting || FLAGS_outstanding_tablet_split_limit == 0) {
    return;
  }

  auto time_since_last_run = CoarseMonoClock::Now() - last_run_time_;
  if (time_since_last_run < (FLAGS_process_split_tablet_candidates_interval_msec * 1ms)) {
    return;
  }
  DoSplitting(table_info_map);
  last_run_time_ = CoarseMonoClock::Now();
}

void TabletSplitManager::ProcessSplitTabletResult(
    const Status& status,
    const TableId& consumer_table_id,
    const SplitTabletIds& split_tablet_ids) {
  if (!status.ok()) {
    LOG(WARNING) << "AsyncSplitTablet task failed with status: " << status;
  } else {
    // TODO(JHE) Handle failure cases here (github issue #11030).
    // Update the xCluster tablet mapping.
    Status s = cdc_consumer_split_driver_->UpdateCDCConsumerOnTabletSplit(
        consumer_table_id, split_tablet_ids);
    WARN_NOT_OK(s, Format(
        "Encountered an error while updating the xCluster consumer tablet mapping. "
        "Table id: $0, Split Tablets: $1",
        consumer_table_id, split_tablet_ids.ToString()));
  }
}

}  // namespace master
}  // namespace yb
