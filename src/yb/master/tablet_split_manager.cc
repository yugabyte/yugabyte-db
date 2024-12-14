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

#include "yb/master/tablet_split_manager.h"

#include <algorithm>
#include <chrono>
#include <optional>

#include "yb/common/constants.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"

#include "yb/dockv/partition.h"
#include "yb/common/schema.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/xcluster/xcluster_manager_if.h"

#include "yb/server/monitored_task.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/unique_lock.h"
#include "yb/util/shared_lock.h"

using std::vector;

DEFINE_RUNTIME_int32(process_split_tablet_candidates_interval_msec, 0,
             "The minimum time between automatic splitting attempts. The actual splitting time "
             "between runs is also affected by catalog_manager_bg_task_wait_ms, which controls how "
             "long the bg tasks thread sleeps at the end of each loop. The top-level automatic "
             "tablet splitting method, which checks for the time since last run, is run once per "
             "loop.");
DEPRECATE_FLAG(int32, max_queued_split_candidates, "10_2022");

DECLARE_bool(enable_automatic_tablet_splitting);

DEFINE_RUNTIME_uint64(outstanding_tablet_split_limit, 0,
              "Limit of the number of outstanding tablet splits. Limitation is disabled if this "
              "value is set to 0.");

DEFINE_RUNTIME_uint64(outstanding_tablet_split_limit_per_tserver, 1,
              "Limit of the number of outstanding tablet splits per node. Limitation is disabled "
              "if this value is set to 0.");

DECLARE_bool(TEST_validate_all_tablet_candidates);

DEFINE_RUNTIME_bool(enable_tablet_split_of_pitr_tables, true,
    "When set, it enables automatic tablet splitting of tables covered by "
    "Point In Time Restore schedules.");

DEFINE_RUNTIME_uint64(tablet_split_limit_per_table, 0,
              "Limit of the number of tablets per table for tablet splitting. Limitation is "
              "disabled if this value is set to 0.");

DEFINE_RUNTIME_uint64(prevent_split_for_ttl_tables_for_seconds, 86400,
              "Seconds between checks for whether to split a table with TTL. Checks are disabled "
              "if this value is set to 0.");

DEFINE_RUNTIME_uint64(prevent_split_for_small_key_range_tablets_for_seconds, 300,
              "Seconds between checks for whether to split a tablet whose key range is too small "
              "to be split. Checks are disabled if this value is set to 0.");

DEFINE_RUNTIME_bool(sort_automatic_tablet_splitting_candidates, true,
            "Whether we should sort candidates for new automatic tablet splits, so the largest "
            "candidates are picked first.");

DEFINE_RUNTIME_double(tablet_split_min_size_ratio, 0.8,
    "If sorting by size is enabled, a tablet will only be considered for splitting if the ratio "
    "of its size to the largest split candidate is at least this value. "
    "Valid flag values are 0 to 1 (inclusive). "
    "Setting this to 0 means any tablet that does not exceed tserver / global limits can be split. "
    "Setting this to 1 forces the tablet splitting algorithm to always split the largest candidate "
    "(even if that means waiting for existing splits to complete).");

DEFINE_test_flag(bool, skip_partitioning_version_validation, false,
                 "When set, skips partitioning_version checks to prevent tablet splitting.");

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
    split_respects_tablet_replica_limits, false,
    "Whether to check the universe tablet replica limit before splitting a tablet. When this flag "
    "and enforce_tablet_replica_limits are both true, the system will no longer split tablets when "
    "the limit machinery determines the universe cannot support any more tablet replicas.");
TAG_FLAG(split_respects_tablet_replica_limits, advanced);

METRIC_DEFINE_gauge_uint64(server, automatic_split_manager_time,
                           "Automatic Split Manager Time", yb::MetricUnit::kMilliseconds,
                           "Time for one run of the automatic tablet split manager.");

METRIC_DEFINE_counter(
    cluster, split_tablet_too_many_tablets,
    "How many SplitTablet operations have failed because the cluster cannot host any more tablets",
    yb::MetricUnit::kRequests,
    "The number of SplitTablet operations failed because the cluster cannot host any more "
    "tablets.");

namespace yb {
namespace master {

using strings::Substitute;
using namespace std::literals;

namespace {

template <typename IdType>
Status ValidateAgainstDisabledList(const IdType& id,
                                   std::unordered_map<IdType, CoarseTimePoint>* map) {
  const auto entry = map->find(id);
  if (entry == map->end()) {
    return Status::OK();
  }

  const auto ignored_until = entry->second;
  if (ignored_until <= CoarseMonoClock::Now()) {
    map->erase(entry);
    return Status::OK();
  }

  return STATUS_FORMAT(
      IllegalState,
      "Table/tablet is ignored for splitting until $0. Table id: $1",
      ToString(ignored_until), id);
}

} // namespace

TabletSplitManager::TabletSplitManager(
    Master& master,
    const scoped_refptr<MetricEntity>& master_metrics,
    const scoped_refptr<MetricEntity>& cluster_metrics):
    master_(master),
    catalog_manager_(*master.catalog_manager()),
    last_run_time_(CoarseDuration::zero()),
    automatic_split_manager_time_ms_(
        METRIC_automatic_split_manager_time.Instantiate(master_metrics, 0)),
    metric_split_tablet_too_many_tablets_(
        METRIC_split_tablet_too_many_tablets.Instantiate(cluster_metrics)) {}

Status TabletSplitManager::ValidateTableAgainstDisabledLists(const TableId& table_id) {
  UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
  RETURN_NOT_OK(ValidateAgainstDisabledList(table_id, &disable_splitting_for_ttl_table_until_));
  RETURN_NOT_OK(ValidateAgainstDisabledList(table_id,
                                            &disable_splitting_for_backfilling_table_until_));
  return Status::OK();
}

Status TabletSplitManager::ValidateTabletAgainstDisabledList(const TabletId& tablet_id) {
  UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
  return ValidateAgainstDisabledList(tablet_id,
                                     &disable_splitting_for_small_key_range_tablet_until_);
}

Status TabletSplitManager::ValidatePartitioningVersion(const TableInfo& table) {
  if (PREDICT_FALSE(FLAGS_TEST_skip_partitioning_version_validation)) {
    return Status::OK();
  }

  if (!table.is_index()) {
    return Status::OK();
  }

  auto table_locked = table.LockForRead();
  if (!table_locked->schema().has_table_properties() || !table_locked->pb.has_partition_schema()) {
    return Status::OK();
  }

  // Nothing to validate for hash partitioned tables
  if (dockv::PartitionSchema::IsHashPartitioning(table_locked->pb.partition_schema())) {
    return Status::OK();
  }

  // Check partition key version is valid for tablet splitting
  const auto& table_properties = table_locked->schema().table_properties();
  if (table_properties.has_partitioning_version() &&
      table_properties.partitioning_version() > 0) {
    return Status::OK();
  }

  // TODO(tsplit): the message won't appear within automatic tablet splitting loop without vlog is
  // enabled for this module. It might be a good point to spawn this message once (or periodically)
  // to the logs to understand why the automatic splitting is not happen (this might be useful
  // for other types of messages as well).
  const auto msg = Format(
      "Tablet splitting is not supported for the index table"
      " \"$0\" with table_id \"$1\". Please, rebuild the index!",
      table.name(), table.id());
  return STATUS(NotSupported, msg);
}

Status TabletSplitManager::ValidateSplitCandidateTable(
    const TableInfoPtr& table,
    const IgnoreDisabledList ignore_disabled_lists) {
  if (PREDICT_FALSE(FLAGS_TEST_validate_all_tablet_candidates)) {
    return Status::OK();
  }
  auto table_lock = table->LockForRead();
  if (table_lock->started_deleting()) {
    return STATUS_FORMAT(
        NotSupported, "Table is deleted; ignoring for splitting. table: $0", *table);
  }

  if (table_lock->is_index() && table_lock->pb.index_info().has_vector_idx_options()) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for vector index tables, table: $0",
        *table);
  }

  for (const auto& index : table_lock->pb.indexes()) {
    if (index.has_vector_idx_options()) {
      return STATUS_FORMAT(
          NotSupported,
          "Tablet splitting is not supported for tables indexed with vector index: $0",
          *table);
    }
  }

  if (!ignore_disabled_lists) {
    RETURN_NOT_OK(ValidateTableAgainstDisabledLists(table->id()));
  }

  // Check if this table is covered by a PITR schedule.
  const auto& master_snapshot_coordinator = master_.snapshot_coordinator();
  if (!FLAGS_enable_tablet_split_of_pitr_tables &&
      VERIFY_RESULT(master_snapshot_coordinator.IsTableCoveredBySomeSnapshotSchedule(*table))) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for tables that are a part of"
        " some active PITR schedule, table_id: $0", table->id());
  }

  if (table_lock->GetTableType() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for transaction status tables, table: $0",
        *table);
  }
  if (table->is_system()) {
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for system table: $0", *table);
  }
  if (table->id() == kPgSequencesDataTableId) {
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for Sequences table: $0",
        *table);
  }
  if (table_lock->GetTableType() == REDIS_TABLE_TYPE) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for YEDIS tables, table: $0", *table);
  }
  if (table_lock->IsXClusterDDLReplicationTable()) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for xCluster DDL Replication tables, table: $0",
        *table);
  }

  auto replication_info = VERIFY_RESULT(catalog_manager_.GetTableReplicationInfo(table));
  auto s = catalog_manager_.CanAddPartitionsToTable(
      table->NumPartitions() + 1, replication_info.live_replicas());
  if (s.ok() && FLAGS_split_respects_tablet_replica_limits) {
    s = catalog_manager_.CanSupportAdditionalTablet(table, replication_info);
    if (!s.ok()) {
      IncrementCounter(metric_split_tablet_too_many_tablets_);
    }
  }
  if (!s.ok()) {
    return s.CloneAndPrepend(Format("Cannot create more tablets, table: $0", *table));
  }

  if (FLAGS_tablet_split_limit_per_table != 0 &&
      table->NumPartitions() >= FLAGS_tablet_split_limit_per_table) {
    // TODO(tsplit): Avoid tablet server of scanning tablets for the tables that already
    //  reached the split limit of tablet #6220
    return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::REACHED_SPLIT_LIMIT),
                            "Too many tablets for the table, table: $0, limit: $1",
                            *table, FLAGS_tablet_split_limit_per_table);
  }
  if (table->IsBackfilling()) {
    return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS),
                            "Backfill operation in progress, table: $0", *table);
  }

  // Check if this table hosts stateful services. Only sys_catalog and ysql tables are currently
  // marked as is_system tables. Other tables in system namespace are not marked as is_system table.
  // #15998
  if (!table->GetHostedStatefulServices().empty()) {
    return STATUS_EC_FORMAT(
        IllegalState, MasterError(MasterErrorPB::INVALID_REQUEST),
        "Tablet splitting is not supported on tables that host stateful services, table: $0",
        *table);
  }

  return ValidatePartitioningVersion(*table);
}

Status TabletSplitManager::ValidateSplitCandidateTablet(
    const TabletInfo& tablet,
    const TabletInfoPtr parent,
    const IgnoreTtlValidation ignore_ttl_validation,
    const IgnoreDisabledList ignore_disabled_list) {
  if (PREDICT_FALSE(FLAGS_TEST_validate_all_tablet_candidates)) {
    return Status::OK();
  }

  // Wait for a tablet's parent to be deleted / hidden before trying to split it. This
  // simplifies the algorithm required for PITR, and is unlikely to delay scheduling new
  // splits too much because we wait for post-split compaction to complete anyways (which is
  // probably much slower than parent deletion).
  if (parent != nullptr) {
    auto parent_lock = parent->LockForRead();
    if (!parent_lock->is_hidden() && !parent_lock->is_deleted()) {
      return STATUS_FORMAT(IllegalState, "Cannot split tablet whose parent is not yet deleted. "
          "Child tablet id: $0, parent tablet id: $1.", tablet.tablet_id(), parent->tablet_id());
    }
  }

  bool has_default_ttl = false;
  {
    auto l = tablet.table()->LockForRead();
    // TODO: IMPORTANT - As of 09/15/22 the default ttl in protobuf is unsigned integer
    // while in-memory it is signed integer thus there is an implicit conversion between -1
    // and UINT64_MAX. We should look at this and fix it. Tracked in GI#14028.
    int64_t default_ttl = l->schema().table_properties().has_default_time_to_live() ?
        l->schema().table_properties().default_time_to_live() : kNoDefaultTtl;
    has_default_ttl = (default_ttl != kNoDefaultTtl);
  }

  auto ts_desc = VERIFY_RESULT(tablet.GetLeader());
  if (!ignore_ttl_validation && has_default_ttl
      && ts_desc->get_disable_tablet_split_if_default_ttl()) {
    DisableSplittingForTtlTable(tablet.table()->id());
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for tables with default time to live, "
        "tablet_id: $0", tablet.tablet_id());
  }

  if (tablet.colocated()) {
    return STATUS_FORMAT(
        NotSupported, "Tablet splitting is not supported for colocated tables, tablet_id: $0",
        tablet.tablet_id());
  }

  if (!ignore_disabled_list) {
    RETURN_NOT_OK(ValidateTabletAgainstDisabledList(tablet.id()));
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

Status TabletSplitManager::DisableTabletSplitting(
    const DisableTabletSplittingRequestPB* req, DisableTabletSplittingResponsePB* resp,
    rpc::RpcContext* rpc) {
  const MonoDelta disable_duration = MonoDelta::FromMilliseconds(req->disable_duration_ms());
  DisableSplittingFor(disable_duration, req->feature_name());
  return Status::OK();
}

Status TabletSplitManager::IsTabletSplittingComplete(
    const IsTabletSplittingCompleteRequestPB* req, IsTabletSplittingCompleteResponsePB* resp,
    rpc::RpcContext* rpc) {
  resp->set_is_tablet_splitting_complete(
      IsTabletSplittingComplete(req->wait_for_parent_deletion(), rpc->GetClientDeadline()));
  return Status::OK();
}

bool TabletSplitManager::IsTabletSplittingComplete(
    bool wait_for_parent_deletion, CoarseTimePoint deadline) {
  // All non-colocated tables are primary tables. Only non-colocated tables can be split.
  const auto tables = catalog_manager_.GetTables(GetTablesMode::kAll, PrimaryTablesOnly::kFalse);
  return std::all_of(
      tables.begin(), tables.end(),
      [&](const auto& table) {
          return IsTabletSplittingComplete(*table, wait_for_parent_deletion, deadline);
      });
}

void TabletSplitManager::DisableSplittingFor(
    const MonoDelta& disable_duration, const std::string& feature_name) {
  DCHECK(!feature_name.empty());
  UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
  LOG(INFO) << Substitute("Disabling tablet splitting for $0 milliseconds for feature $1.",
                          disable_duration.ToMilliseconds(), feature_name);
  splitting_disabled_until_[feature_name] = CoarseMonoClock::Now() + disable_duration;
}

void TabletSplitManager::ReenableSplittingFor(const std::string& feature_name) {
  DCHECK(!feature_name.empty());
  UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
  LOG(INFO) << Substitute("Re-enabling tablet splitting for feature $0.", feature_name);
  splitting_disabled_until_.erase(feature_name);
}

void TabletSplitManager::DisableSplittingForTtlTable(const TableId& table_id) {
  if (FLAGS_prevent_split_for_ttl_tables_for_seconds != 0) {
    VLOG(1) << "Disabling splitting for TTL table. Table id: " << table_id;
    const auto recheck_at = CoarseMonoClock::Now()
        + MonoDelta::FromSeconds(FLAGS_prevent_split_for_ttl_tables_for_seconds);
    UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
    disable_splitting_for_ttl_table_until_[table_id] = recheck_at;
  }
}

void TabletSplitManager::DisableSplittingForBackfillingTable(const TableId& table_id) {
  LOG(INFO) << "Disabling splitting for backfilling table. Table id: " << table_id;
  UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
  disable_splitting_for_backfilling_table_until_[table_id] = CoarseTimePoint::max();
}

void TabletSplitManager::ReenableSplittingForBackfillingTable(const TableId& table_id) {
  LOG(INFO) << "Re-enabling splitting for table. Table id: " << table_id;
  UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
  disable_splitting_for_backfilling_table_until_.erase(table_id);
}

void TabletSplitManager::DisableSplittingForSmallKeyRangeTablet(const TabletId& tablet_id) {
  if (FLAGS_prevent_split_for_small_key_range_tablets_for_seconds != 0) {
    VLOG(1) << "Disabling splitting for small key range tablet. Tablet id: " << tablet_id;
    const auto recheck_at = CoarseMonoClock::Now()
        + MonoDelta::FromSeconds(FLAGS_prevent_split_for_small_key_range_tablets_for_seconds);
    UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
    disable_splitting_for_small_key_range_tablet_until_[tablet_id] = recheck_at;
  }
}

Status TabletSplitManager::PrepareForPitr(const CoarseTimePoint& deadline) {
  const auto disable_duration_ms = MonoDelta::FromMilliseconds(1000 *
      (FLAGS_inflight_splits_completion_timeout_secs + FLAGS_pitr_max_restore_duration_secs));
  const auto wait_inflight_splitting_until = CoarseMonoClock::Now() +
      MonoDelta::FromMilliseconds(1000 * FLAGS_inflight_splits_completion_timeout_secs);

  // Disable splitting and then wait for all pending splits to complete before starting restoration.
  DisableSplittingFor(disable_duration_ms, kPitrFeatureName);

  bool inflight_splits_finished = false;
  while (CoarseMonoClock::Now() < std::min(wait_inflight_splitting_until, deadline)) {
    // Wait for existing split operations to complete.
    if (IsTabletSplittingComplete(true /* wait_for_parent_deletion */, deadline)) {
      inflight_splits_finished = true;
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_pitr_split_disable_check_freq_ms));
  }

  if (!inflight_splits_finished) {
    ReenableSplittingFor(kPitrFeatureName);
    return STATUS(TimedOut, "Timed out waiting for inflight tablet splitting to complete.");
  }
  return Status::OK();
}

Status AllReplicasHaveFinishedCompaction(const TabletReplicaMap& replicas) {
  for (const auto& replica : replicas) {
    if (replica.second.drive_info.may_have_orphaned_post_split_data) {
      return STATUS_FORMAT(IllegalState,
          "Tablet replica $0 may have orphaned post split data", replica.second.ToString());
    }
  }
  return Status::OK();
}

// Check if all live replicas are in RaftGroupStatePB::RUNNING state
// (read replicas are ignored) and tablet is not under/over replicated.
// Tablet is over-replicated if number of live replicas > rf,
// otherwise, if live replicas < rf, tablet is under replicated.
// where rf is the replication factor of a table, can get it from
// CatalogManager::GetTableReplicationFactor.
Status CheckLiveReplicasForSplit(
    const TabletId& tablet_id, const TabletReplicaMap& replicas, size_t rf) {
  size_t live_replicas = 0;
  for (const auto& [ts_uuid, replica] : replicas) {
    if (replica.member_type == consensus::PRE_VOTER) {
      return STATUS_FORMAT(NotSupported,
                           "One tablet peer is doing RBS as PRE_VOTER, "
                           "tablet_id: $1, peer_uuid: $2, current RAFT state: $3",
                            tablet_id, ts_uuid,
                            RaftGroupStatePB_Name(replica.state));
    }
    if (replica.member_type == consensus::VOTER) {
      live_replicas++;
      if (replica.state != tablet::RaftGroupStatePB::RUNNING) {
        return STATUS_FORMAT(NotSupported,
                             "At least one tablet peer not running, "
                             "tablet_id: $0, peer_uuid: $1, current RAFT state: $2",
                             tablet_id, ts_uuid,
                             RaftGroupStatePB_Name(replica.state));
      }
    }
  }
  if (live_replicas != rf) {
    return STATUS_FORMAT(NotSupported,
                         "Tablet $0 is $1 replicated, "
                         "has $2 live replicas, expected replication factor is $3",
                         tablet_id, live_replicas < rf ? "under" : "over", live_replicas, rf);
  }
  return Status::OK();
}

void TabletSplitManager::ScheduleSplits(
    const SplitsToScheduleMap& splits_to_schedule, const LeaderEpoch& epoch) {
  VLOG_WITH_FUNC(2) << "Start";
  for (const auto& [tablet_id, size] : splits_to_schedule) {
    auto s = catalog_manager_.SplitTablet(tablet_id, ManualSplit::kFalse, epoch);
    if (!s.ok()) {
      WARN_NOT_OK(s, Format("Failed to start/restart split for tablet_id: $0.", tablet_id));
    } else {
      LOG(INFO) << "Scheduled split for tablet_id: " << tablet_id
                << (size ? Format(" with size $0 bytes", *size) : "");
    }
  }
}

// A cache of the shared_ptrs to each tablet's replicas, to avoid having to repeatedly lock the
// tablet, and to ensure that we use a consistent set of replicas for each tablet within each
// iteration of the tablet split manager.
class TabletReplicaMapCache {
 public:
  const std::shared_ptr<const TabletReplicaMap> GetOrAdd(const TabletInfo& tablet) {
    auto it = replica_cache_.find(tablet.id());
    if (it != replica_cache_.end()) {
      return it->second;
    } else {
      const std::shared_ptr<const TabletReplicaMap> replicas = tablet.GetReplicaLocations();
      if (replicas->empty()) {
        VLOG(4) << "No replicas found for tablet. Id: " << tablet.id();
      }
      return replica_cache_[tablet.id()] = replicas;
    }
  }

 private:
  std::unordered_map<TabletId, std::shared_ptr<const TabletReplicaMap>> replica_cache_;
};

class OutstandingSplitState {
 public:
  OutstandingSplitState(
      const TabletInfoMap& tablet_info_map, TabletReplicaMapCache* replica_cache):
      tablet_info_map_{tablet_info_map}, replica_cache_{replica_cache} {}

  // Helper method to determine if more splits can be scheduled, or if we should exit early.
  bool CanSplitMoreGlobal() const {
    const auto outstanding_splits =
        splits_with_task_.size() + compacting_splits_.size() + splits_to_schedule_.size();
    if (FLAGS_outstanding_tablet_split_limit != 0 &&
        outstanding_splits >= FLAGS_outstanding_tablet_split_limit) {
      VLOG_WITH_FUNC(2) << Format(
          "Number of outstanding splits will be $0 ($1 + $2 + $3) >= $4, can't do more splits",
          outstanding_splits, splits_with_task_.size(), compacting_splits_.size(),
          splits_to_schedule_.size(), FLAGS_outstanding_tablet_split_limit);
      return false;
    }
    return true;
  }

  Status CanSplitMoreOnReplicas(const TabletReplicaMap& replicas) const {
    if (FLAGS_outstanding_tablet_split_limit_per_tserver == 0) {
      return Status::OK();
    }
    for (const auto& location : replicas) {
      auto it = ts_to_ongoing_splits_.find(location.first);
      if (it != ts_to_ongoing_splits_.end() &&
          it->second.size() >= FLAGS_outstanding_tablet_split_limit_per_tserver) {
        return STATUS_FORMAT(IllegalState,
                             "TServer $0 already has $1 >= $2 ongoing splits, can't do more splits "
                             "there", location.first, it->second.size(),
                             FLAGS_outstanding_tablet_split_limit_per_tserver);
      }
    }
    return Status::OK();
  }

  bool HasSplitWithTask(const TabletId& split_tablet_id) const {
    return splits_with_task_.contains(split_tablet_id);
  }

  void AddSplitWithTask(const TabletId& split_tablet_id) {
    splits_with_task_.insert(split_tablet_id);
    auto it = tablet_info_map_.find(split_tablet_id);
    if (it == tablet_info_map_.end()) {
      LOG(WARNING) << "Split tablet with task not found in tablet info map. ID: "
                   << split_tablet_id;
      return;
    }
    TrackTserverSplits(split_tablet_id, *replica_cache_->GetOrAdd(*it->second));
    auto l = it->second->LockForRead();
    for (auto child_id : l->pb.split_tablet_ids()) {
      // Track split_tablet_id as an ongoing split on its children's tservers.
      TrackTserverSplits(split_tablet_id, child_id);
    }
  }

  void AddSplitToRestart(const TabletId& split_tablet_id, const TabletInfo& split_child) {
    if (!compacting_splits_.contains(split_tablet_id)) {
      auto inserted = splits_to_schedule_.insert({split_tablet_id, std::nullopt});
      if (inserted.second) {
        // Track split_tablet_id as an ongoing split on its tservers. This is required since it is
        // possible that one of the split children is not running yet, but we still want to count
        // the split against the limits of the tservers on which the children will eventually
        // appear.
        TrackTserverSplits(split_tablet_id, split_tablet_id);
      }
    }
    TrackTserverSplits(split_tablet_id, *replica_cache_->GetOrAdd(split_child));
  }

  void AddCompactingSplit(
      const TabletId& split_tablet_id, const TabletInfo& split_child) {
    // It's possible that one child subtablet leads us to insert the parent tablet id into
    // splits_to_schedule, and another leads us to insert into compacting_splits. In this
    // case, it means one of the children is live, thus both children have been created and
    // the split RPC does not need to be scheduled.
    bool was_scheduled_for_split = splits_to_schedule_.erase(split_tablet_id);
    if (was_scheduled_for_split) {
      VLOG(1) << Format("Found compacting split child ($0), so removing split parent "
                        "($1) from splits to schedule.", split_child.id(), split_tablet_id);
    }
    bool inserted_compacting_split = compacting_splits_.insert(split_tablet_id).second;
    if (inserted_compacting_split && !was_scheduled_for_split) {
      // Track split_tablet_id as an ongoing split on its tservers. This is required since it is
      // possible that one of the split children is not running yet, but we still want to count
      // the split against the limits of the tservers on which the children will eventually
      // appear.
      TrackTserverSplits(split_tablet_id, split_tablet_id);
    }
    TrackTserverSplits(split_tablet_id, *replica_cache_->GetOrAdd(split_child));
  }

  const SplitsToScheduleMap& GetSplitsToSchedule() const {
    return splits_to_schedule_;
  }

  void AddCandidate(TabletInfoPtr tablet, uint64_t leader_sst_size) {
    largest_candidate_size_ = std::max(largest_candidate_size_, leader_sst_size);
    new_split_candidates_.emplace_back(SplitCandidate{tablet, leader_sst_size});
  }

  void ProcessCandidates() {
    if (VLOG_IS_ON(4)) {
      VLOG(4) << Format("Processing split candidates: $0", new_split_candidates_);
    } else {
      VLOG(2) << Format("Processing $0 split candidates", new_split_candidates_.size());
    }

    // Add any new splits to the set of splits to schedule (while respecting the max number of
    // outstanding splits).
    if (!CanSplitMoreGlobal()) {
      return;
    }

    if (FLAGS_sort_automatic_tablet_splitting_candidates) {
      auto threshold = static_cast<uint64_t>(
          FLAGS_tablet_split_min_size_ratio * largest_candidate_size_);
      VLOG(3) << "Filtering out candidates smaller than "
              << HumanReadableNumBytes::ToString(threshold);
      std::erase_if(
          new_split_candidates_,
          [threshold](const auto& candidate) {
        if (candidate.leader_sst_size < threshold) {
          VLOG(4) << "Rejected: " << candidate.ToString();
          return true;
        }
        return false;
      });
      sort(new_split_candidates_.begin(), new_split_candidates_.end(), LargestTabletFirst);
    }
    for (const auto& candidate : new_split_candidates_) {
      VLOG(4) << Format("Processing split candidate $0 of size $1",
          candidate.tablet->id(), candidate.leader_sst_size);
      if (!CanSplitMoreGlobal()) {
        break;
      }
      auto replicas = replica_cache_->GetOrAdd(*candidate.tablet);
      if (Status s = CanSplitMoreOnReplicas(*replicas); !s.ok()) {
        VLOG(4) << Format("Not scheduling split for tablet $0. $1", candidate.tablet->id(), s);
        continue;
      }
      splits_to_schedule_[candidate.tablet->id()] = candidate.leader_sst_size;
      TrackTserverSplits(candidate.tablet->id(), *replicas);
    }
  }

 private:
  uint64_t largest_candidate_size_ = 0;
  const TabletInfoMap& tablet_info_map_;
  TabletReplicaMapCache* replica_cache_;
  // Splits which are tracked by an AsyncGetTabletSplitKey or AsyncSplitTablet task.
  std::unordered_set<TabletId> splits_with_task_;
  // Splits for which at least one child tablet is still undergoing compaction.
  std::unordered_set<TabletId> compacting_splits_;
  // Splits that need to be started or restarted. If the split is a new split, the map contains
  // the size of the leader tablet.
  SplitsToScheduleMap splits_to_schedule_;

  struct SplitCandidate {
    TabletInfoPtr tablet;
    uint64_t leader_sst_size;

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(tablet, leader_sst_size);
    }
  };
  // New split candidates. The chosen candidates are eventually added to splits_to_schedule.
  std::vector<SplitCandidate> new_split_candidates_;

  std::unordered_map<TabletServerId, std::unordered_set<TabletId>> ts_to_ongoing_splits_;

  // Tracks split_tablet_id as an ongoing split on the replicas of replica_tablet_id, which is
  // either split_tablet_id itself or one of split_tablet_id's children.
  void TrackTserverSplits(const TabletId& split_tablet_id, const TabletId& replica_tablet_id) {
    auto it = tablet_info_map_.find(replica_tablet_id);
    if (it == tablet_info_map_.end()) {
      VLOG(1) << "Tablet not found in tablet info map. ID: " << replica_tablet_id;
      return;
    }
    TrackTserverSplits(split_tablet_id, *replica_cache_->GetOrAdd(*it->second));
  }

  void TrackTserverSplits(const TabletId& tablet_id, const TabletReplicaMap& split_replicas) {
    for (const auto& location : split_replicas) {
      VLOG(4) << Format("Tracking location T $1 P $0", location.first, tablet_id);
      ts_to_ongoing_splits_[location.first].insert(tablet_id);
    }
  }

  static inline bool LargestTabletFirst(const SplitCandidate& c1, const SplitCandidate& c2) {
    return c1.leader_sst_size > c2.leader_sst_size;
  }
};

void TabletSplitManager::DoSplitting(
    const std::vector<TableInfoPtr>& tables, const TabletInfoMap& tablet_info_map,
    const LeaderEpoch& epoch) {
  VLOG_WITH_FUNC(2) << "Start";
  // TODO(asrivastava): We might want to loop over all running tables when determining outstanding
  // splits, to avoid missing outstanding splits for tables that have recently become invalid for
  // splitting. This is most critical for tables that frequently switch between being valid and
  // invalid for splitting (e.g. for tables with frequent PITR schedules).
  // https://github.com/yugabyte/yugabyte-db/issues/11459
  vector<TableInfoPtr> valid_tables;
  for (const auto& table : tables) {
    Status status = ValidateSplitCandidateTable(table);
    if (!status.ok()) {
      VLOG(3) << "Skipping table for splitting. " << status;
      continue;
    }
    status = catalog_manager_.XReplValidateSplitCandidateTable(table->id());
    if (!status.ok()) {
      VLOG(3) << "Skipping table for splitting. " << status;
      continue;
    }
    valid_tables.push_back(table);
  }

  TabletReplicaMapCache replica_cache;
  OutstandingSplitState state(tablet_info_map, &replica_cache);
  for (const auto& table : valid_tables) {
    VLOG(3) << "Processing ongoing split tasks for table " << table->id();
    for (const auto& task : table->GetTasks()) {
      // These tasks will retry automatically until they succeed or fail.
      if (task->type() == server::MonitoredTaskType::kGetTabletSplitKey ||
          task->type() == server::MonitoredTaskType::kSplitTablet) {
        const TabletId tablet_id = static_cast<AsyncTabletLeaderTask*>(task.get())->tablet_id();
        auto tablet_info_it = tablet_info_map.find(tablet_id);
        if (tablet_info_it != tablet_info_map.end()) {
          const auto& tablet = tablet_info_it->second;
          state.AddSplitWithTask(tablet->id());
        } else {
          LOG(WARNING) << "Could not find tablet info for tablet with task. Tablet id: "
                      << tablet_id;
        }
        YB_LOG_EVERY_N_SECS(INFO, 30) << Format(
            "Found split with ongoing task. Task type: $0. Split parent id: $1.",
            task->type_name(), tablet_id) << THROTTLE_MSG;
        if (!state.CanSplitMoreGlobal()) {
          return;
        }
      }
    }
  }

  for (const auto& table : valid_tables) {
    VLOG(3) << Format("Processing table $0 for split", table->id());
    auto replication_factor = catalog_manager_.GetTableReplicationFactor(table);
    if (!replication_factor.ok()) {
      YB_LOG_EVERY_N_SECS(WARNING, 30) << "Skipping tablet splitting for table "
                                       << table->id() << ": "
                                       << "as fetching replication factor failed with error "
                                       << StatusToString(replication_factor.status())
                                       << THROTTLE_MSG;
      continue;
    }
    auto tablets_result = table->GetTablets();
    if (!tablets_result) continue;
    for (const auto& tablet : *tablets_result) {
      VLOG(4) << Format("Processing tablet $0 for split", tablet->id());
      if (!state.CanSplitMoreGlobal()) {
        break;
      }
      if (state.HasSplitWithTask(tablet->id())) {
        VLOG(4) << Format("Should not split tablet $0 since it already has a split task",
                          tablet->id());
        continue;
      }

      auto tablet_lock = tablet->LockForRead();
      TabletId parent_id;
      if (!tablet_lock->pb.split_parent_tablet_id().empty()) {
        parent_id = tablet_lock->pb.split_parent_tablet_id();
        if (state.HasSplitWithTask(parent_id)) {
          VLOG(4) << Format("Should not split tablet $0 since its parent already has a "
                            "split task", tablet->id());
          continue;
        }

        // If a split child is not running, schedule a restart for the split.
        if (!tablet_lock->is_running()) {
          VLOG(4) << Format("Should not split child tablet ($0) that is not running. "
                            "Adding parent ($1) to list of splits to reschedule.", tablet->id(),
                            parent_id);
          state.AddSplitToRestart(parent_id, *tablet);
          continue;
        }

        // If this (running) tablet is the child of a split and is still compacting, track it as a
        // compacting split but do not schedule a restart (we assume that this split will eventually
        // complete for both tablets).
        if (Status s = AllReplicasHaveFinishedCompaction(*replica_cache.GetOrAdd(*tablet));
            !s.ok()) {
          VLOG(4) << Format("Should not split child tablet ($0) that is compacting. Adding parent "
                            "($1) to list of compacting splits. ", tablet->id(), parent_id)
                             << s;
          state.AddCompactingSplit(parent_id, *tablet);
          continue;
        }
      }

      VLOG(4) << Format("Evaluating tablet $0 as a split candidate", tablet->id());
      auto ValidateAutomaticSplitCandidateTablet = [&]() -> Result<uint64_t> {
        auto drive_info_opt = tablet->GetLeaderReplicaDriveInfo();
        if (!drive_info_opt.ok()) {
          return drive_info_opt.status();
        }
        std::shared_ptr<TabletInfo> parent = nullptr;
        if (!parent_id.empty()) {
          parent = FindPtrOrNull(tablet_info_map, parent_id);
        }
        RETURN_NOT_OK(ValidateSplitCandidateTablet(*tablet, parent));
        RETURN_NOT_OK(catalog_manager_.ShouldSplitValidCandidate(*tablet, drive_info_opt.get()));

        const auto replicas = replica_cache.GetOrAdd(*tablet);
        RETURN_NOT_OK(
            CheckLiveReplicasForSplit(tablet->tablet_id(), *replicas, replication_factor.get()));
        RETURN_NOT_OK(AllReplicasHaveFinishedCompaction(*replicas));
        RETURN_NOT_OK(state.CanSplitMoreOnReplicas(*replicas));
        return drive_info_opt.get().sst_files_size;
      };
      Result<uint64_t> result = ValidateAutomaticSplitCandidateTablet();
      if (!result.ok()) {
        VLOG(4) << Format("Should not split tablet $0. ", tablet->tablet_id())
                           << result;
        continue;
      }
      state.AddCandidate(tablet, result.get());
    }
    if (!state.CanSplitMoreGlobal()) {
      break;
    }
  }

  // Sort candidates if required and add as many desired candidates to the list of splits to
  // schedule as possible (while respecting the limits on ongoing splits).
  state.ProcessCandidates();
  // Schedule any new splits and any splits that need to be restarted.
  ScheduleSplits(state.GetSplitsToSchedule(), epoch);
}

Status TabletSplitManager::WaitUntilIdle(CoarseTimePoint deadline) {
  std::shared_lock l(is_running_mutex_, deadline);
  if (!l.owns_lock()) {
    return STATUS_FORMAT(TimedOut,
        "Tablet split manager iteration did not complete before deadline: $0", deadline);
  }
  return Status::OK();
}

// Wait for the tablet split manager to finish an ongoing run before checking whether splitting is
// complete to avoid the following scenario:
// 1. Thread A: Tablet split manager is about to enqueue a split for table T.
// 2. Thread B: Disables splitting on table T and calls IsTabletSplittingComplete(T), which finds no
//              outstanding splits.
// 3. Thread A: Enqueues the split for table T.
bool TabletSplitManager::IsTabletSplittingComplete(
    const TableInfo& table, bool wait_for_parent_deletion, CoarseTimePoint deadline) {
  if (auto status = WaitUntilIdle(deadline); !status.ok()) {
    LOG(WARNING) << status;
    return false;
  }
  // Deleted tables should not have any splits.
  if (table.is_deleted()) {
    return true;
  }
  // Colocated tables should not have any splits.
  if (table.colocated()) {
    return true;
  }
  for (const auto& task : table.GetTasks()) {
    if (task->type() == server::MonitoredTaskType::kGetTabletSplitKey ||
        task->type() == server::MonitoredTaskType::kSplitTablet) {
      YB_LOG_EVERY_N_SECS(INFO, 10)
          << Format("Tablet Splitting: Table $0 has outstanding splitting tasks", table.id())
          << THROTTLE_MSG;
      return false;
    }
  }

  auto result = table.HasOutstandingSplits(wait_for_parent_deletion);
  return result.ok() && !*result;
}

void TabletSplitManager::MaybeDoSplitting(
    const std::vector<TableInfoPtr>& tables, const TabletInfoMap& tablet_info_map,
    const LeaderEpoch& epoch) {
  if (!FLAGS_enable_automatic_tablet_splitting) {
    VLOG_WITH_FUNC(2) << "Skipping splitting run because enable_automatic_tablet_splitting is not "
                         "set";
    return;
  }

  // This must be acquired before checking the disabled sets, since WaitForIdle expects that the
  // tablet split manager will observe any new disabled set changes by the time its shared_lock
  // of is_running_mutex_ returns.
  std::unique_lock lock(is_running_mutex_);

  {
    UniqueLock<decltype(disabled_sets_mutex_)> lock(disabled_sets_mutex_);
    auto now = CoarseMonoClock::Now();
    for (const auto& pair : splitting_disabled_until_) {
      if (now <= pair.second) {
        VLOG_WITH_FUNC(2) << Format(
            "Skipping splitting run because automatic tablet splitting is disabled until $0 by "
            "feature $1", pair.second, pair.first);
        return;
      }
    }
  }

  auto start_time = CoarseMonoClock::Now();
  auto time_since_last_run = start_time - last_run_time_;
  if (time_since_last_run < (FLAGS_process_split_tablet_candidates_interval_msec * 1ms)) {
    VLOG_WITH_FUNC(2) << Format(
        "Skipping splitting run because time since last run $0 is less than $1 ms",
        time_since_last_run, FLAGS_process_split_tablet_candidates_interval_msec);
    return;
  }

  DoSplitting(tables, tablet_info_map, epoch);
  last_run_time_ = CoarseMonoClock::Now();
  automatic_split_manager_time_ms_->set_value(ToMilliseconds(last_run_time_ - start_time));
}

Status TabletSplitManager::ProcessSplitTabletResult(
    const TableId& split_table_id, const SplitTabletIds& split_tablet_ids,
    const LeaderEpoch& epoch) {
  // Since this can get called multiple times from DoSplitTablet (if a tablet split is retried),
  // everything here needs to be idempotent.
  LOG(INFO) << "Processing split tablet result for table " << split_table_id
            << ", split tablet ids: " << split_tablet_ids.ToString();

  Status s = catalog_manager_.GetXClusterManager()->HandleTabletSplit(
      split_table_id, split_tablet_ids, epoch);
  RETURN_NOT_OK_PREPEND(
      s, Format(
             "Encountered an error while updating the xCluster consumer tablet mapping. "
             "Table id: $0, Split Tablets: $1",
             split_table_id, split_tablet_ids.ToString()));

  s = catalog_manager_.UpdateCDCProducerOnTabletSplit(split_table_id, split_tablet_ids);
  RETURN_NOT_OK_PREPEND(
      s, Format(
             "Encountered an error while updating the CDC producer metadata. Table id: $0, Split "
             "Tablets: $1",
             split_table_id, split_tablet_ids.ToString()));

  TEST_SYNC_POINT("Tabletsplit::AddedChildrenTabletStateTableEntries");

  return Status::OK();
}

}  // namespace master
}  // namespace yb
