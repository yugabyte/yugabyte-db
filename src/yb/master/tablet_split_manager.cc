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

#include "yb/common/schema.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/cdc_consumer_split_driver.h"
#include "yb/master/master_error.h"
#include "yb/master/master_fwd.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/ts_descriptor.h"

#include "yb/server/monitored_task.h"

#include "yb/util/flag_tags.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/unique_lock.h"

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

DEFINE_uint64(prevent_split_for_ttl_tables_for_seconds, 86400,
              "Seconds between checks for whether to split a table with TTL. Checks are disabled "
              "if this value is set to 0.");

DEFINE_uint64(prevent_split_for_small_key_range_tablets_for_seconds, 300,
              "Seconds between checks for whether to split a tablet whose key range is too small "
              "to be split. Checks are disabled if this value is set to 0.");

DEFINE_bool(sort_automatic_tablet_splitting_candidates, true,
            "Whether we should sort candidates for new automatic tablet splits, so the largest "
            "candidates are picked first.");

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

struct SplitCandidate {
  TabletInfoPtr tablet;
  uint64_t leader_sst_size;
};

static inline bool LargestTabletFirst(const SplitCandidate& c1, const SplitCandidate& c2) {
  return c1.leader_sst_size > c2.leader_sst_size;
}

Status TabletSplitManager::ValidateAgainstDisabledList(
    const std::string& id, std::unordered_map<std::string, CoarseTimePoint>* map) {
  UniqueLock<decltype(mutex_)> lock(mutex_);
  const auto entry = map->find(id);
  if (entry != map->end()) {
    const auto ignored_until = entry->second;
    if (ignored_until > CoarseMonoClock::Now()) {
      VLOG(1) << Substitute("Table/tablet is ignored for splitting until $0. id: $1",
                            ToString(ignored_until), id);
      return STATUS_FORMAT(
          NotSupported,
          "Table/tablet is ignored for splitting until $0. id: $1", ToString(ignored_until), id);
    } else {
      map->erase(entry);
    }
  }
  return Status::OK();
}

Status TabletSplitManager::ValidateSplitCandidateTable(const TableInfo& table,
    bool ignore_disabled_list) {
  if (PREDICT_FALSE(FLAGS_TEST_validate_all_tablet_candidates)) {
    return Status::OK();
  }
  if (table.is_deleted()) {
    VLOG(1) << Substitute("Table is deleted; ignoring for splitting. table_id: $0", table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Table is deleted; ignoring for splitting. table_id: $0", table.id());
  }

  if (!ignore_disabled_list) {
    RETURN_NOT_OK(ValidateAgainstDisabledList(table.id(), &ignore_table_for_splitting_until_));
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
  if (table.is_system()) {
    VLOG(1) << Substitute("Tablet splitting is not supported for system table: $0 with "
                          "table_id: $1", table.name(), table.id());
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for system table: $0 with table_id: $1",
        table.name(), table.id());
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

Status TabletSplitManager::ValidateSplitCandidateTablet(const TabletInfo& tablet,
    bool ignore_ttl_validation) {
  if (PREDICT_FALSE(FLAGS_TEST_validate_all_tablet_candidates)) {
    return Status::OK();
  }

  Schema schema;
  RETURN_NOT_OK(tablet.table()->GetSchema(&schema));
  auto ts_desc = VERIFY_RESULT(tablet.GetLeader());
  if (!ignore_ttl_validation
      && schema.table_properties().HasDefaultTimeToLive()
      && ts_desc->get_disable_tablet_split_if_default_ttl()) {
    MarkTtlTableForSplitIgnore(tablet.table()->id());
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for tables with default time to live, "
        "tablet_id: $0", tablet.tablet_id());
  }

  if (tablet.colocated()) {
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for colocated tables, tablet_id: $0",
        tablet.tablet_id());
  }

  RETURN_NOT_OK(ValidateAgainstDisabledList(tablet.id(), &ignore_tablet_for_splitting_until_));

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

void TabletSplitManager::MarkTtlTableForSplitIgnore(const TableId& table_id) {
  if (FLAGS_prevent_split_for_ttl_tables_for_seconds != 0) {
    const auto recheck_at = CoarseMonoClock::Now()
        + MonoDelta::FromSeconds(FLAGS_prevent_split_for_ttl_tables_for_seconds);
    UniqueLock<decltype(mutex_)> lock(mutex_);
    ignore_table_for_splitting_until_[table_id] = recheck_at;
  }
}

void TabletSplitManager::MarkSmallKeyRangeTabletForSplitIgnore(const TabletId& tablet_id) {
  if (FLAGS_prevent_split_for_small_key_range_tablets_for_seconds != 0) {
    const auto recheck_at = CoarseMonoClock::Now()
        + MonoDelta::FromSeconds(FLAGS_prevent_split_for_small_key_range_tablets_for_seconds);
    UniqueLock<decltype(mutex_)> lock(mutex_);
    ignore_tablet_for_splitting_until_[tablet_id] = recheck_at;
  }
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

void TabletSplitManager::ScheduleSplits(const unordered_set<TabletId>& splits_to_schedule) {
  for (const auto& tablet_id : splits_to_schedule) {
    auto s = driver_->SplitTablet(tablet_id, false /* is_manual_split */);
    if (!s.ok()) {
      WARN_NOT_OK(s, Format("Failed to start/restart split for tablet_id: $0.", tablet_id));
    } else {
      LOG(INFO) << Substitute("Scheduled split for tablet_id: $0.", tablet_id);
    }
  }
}

void TabletSplitManager::DoSplitting(const TableInfoMap& table_info_map) {
  // Splits which are tracked by an AsyncGetTabletSplitKey or AsyncSplitTablet task.
  unordered_set<TabletId> splits_with_task;
  // Splits for which at least one child tablet is still undergoing compaction.
  unordered_set<TabletId> compacting_splits;
  // Splits that need to be started / restarted.
  unordered_set<TabletId> splits_to_schedule;
  // New split candidates. The chosen candidates are eventually added to splits_to_schedule.
  vector<SplitCandidate> new_split_candidates;

  // Helper method to determine if more splits can be scheduled, or if we should exit early.
  const auto can_split_more = [&]() {
    uint64_t outstanding_splits = splits_with_task.size() +
                                  compacting_splits.size() +
                                  splits_to_schedule.size();
    return outstanding_splits < FLAGS_outstanding_tablet_split_limit;
  };

  // TODO(asrivastava): We might want to loop over all running tables when determining outstanding
  // splits, to avoid missing outstanding splits for tables that have recently become invalid for
  // splitting. This is most critical for tables that frequently switch between being valid and
  // invalid for splitting (e.g. for tables with frequent PITR schedules).
  // https://github.com/yugabyte/yugabyte-db/issues/11459
  vector<TableInfoPtr> valid_tables;
  for (const auto& table : table_info_map) {
    if (ValidateSplitCandidateTable(*table.second).ok()) {
      valid_tables.push_back(table.second);
    }
  }

  for (const auto& table : valid_tables) {
    for (const auto& task : table->GetTasks()) {
      // These tasks will retry automatically until they succeed or fail.
      if (task->type() == yb::server::MonitoredTask::ASYNC_GET_TABLET_SPLIT_KEY ||
          task->type() == yb::server::MonitoredTask::ASYNC_SPLIT_TABLET) {
        const TabletId tablet_id = static_cast<AsyncTabletLeaderTask*>(task.get())->tablet_id();
        splits_with_task.insert(tablet_id);
        LOG(INFO) << Substitute("Found split with ongoing task. Task type: $0. "
                                "Split parent id: $1.", task->type_name(), tablet_id);
        if (!can_split_more()) {
          return;
        }
      }
    }
  }

  for (const auto& table : valid_tables) {
    for (const auto& tablet : table->GetTablets()) {
      if (!can_split_more()) {
        break;
      }
      if (splits_with_task.count(tablet->id())) {
        continue;
      }

      auto tablet_lock = tablet->LockForRead();
      // Ignore a tablet as a new split candidate if it is part of an outstanding split.
      bool ignore_as_candidate = false;
      if (tablet_lock->pb.has_split_parent_tablet_id()) {
        const TabletId& parent_id = tablet_lock->pb.split_parent_tablet_id();
        if (splits_with_task.count(parent_id) != 0) {
          continue;
        }
        if (!tablet_lock->is_running()) {
          // Recently split child is not running; restart the split.
          ignore_as_candidate = true;
          LOG(INFO) << Substitute("Found split child ($0) that is not running. Adding parent ($1) "
                                  "to list of splits to reschedule.", tablet->id(), parent_id);
          splits_to_schedule.insert(parent_id);
        } else if (!AllReplicasHaveFinishedCompaction(*tablet)) {
          // This (running) tablet is the child of a split and is still compacting. We assume that
          // this split will eventually complete for both tablets.
          ignore_as_candidate = true;
          LOG(INFO) << Substitute("Found split child ($0) that is compacting. Adding parent ($1) "
                                  " to list of compacting splits.", tablet->id(), parent_id);
          compacting_splits.insert(parent_id);
        }
        if (splits_to_schedule.count(parent_id) != 0 && compacting_splits.count(parent_id) != 0) {
          // It's possible that one child subtablet leads us to insert the parent tablet id into
          // splits_to_schedule, and another leads us to insert into compacting_splits. In this
          // case, it means one of the children is live, thus both children have been created and
          // the split RPC does not need to be scheduled.
          LOG(INFO) << Substitute("Found compacting split child ($0), so removing split parent "
                                  "($1) from splits to schedule.", tablet->id(), parent_id);
          splits_to_schedule.erase(parent_id);
        }
      }

      if (!ignore_as_candidate) {
        auto drive_info_opt = tablet->GetLeaderReplicaDriveInfo();
        if (!drive_info_opt.ok()) {
          continue;
        }
        if (ValidateSplitCandidateTablet(*tablet).ok() &&
            filter_->ShouldSplitValidCandidate(*tablet, drive_info_opt.get()) &&
            AllReplicasHaveFinishedCompaction(*tablet)) {
          SplitCandidate candidate = {tablet, drive_info_opt.get().sst_files_size};
          new_split_candidates.push_back(std::move(candidate));
        }
      }
    }
    if (!can_split_more()) {
      break;
    }
  }

  // Add any new splits to the set of splits to schedule (while respecting the max number of
  // outstanding splits).
  if (can_split_more()) {
    if (FLAGS_sort_automatic_tablet_splitting_candidates) {
      sort(new_split_candidates.begin(), new_split_candidates.end(), LargestTabletFirst);
    }
    for (const auto& candidate : new_split_candidates) {
      if (!can_split_more()) {
        break;
      }
      splits_to_schedule.insert(candidate.tablet->id());
    }
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
