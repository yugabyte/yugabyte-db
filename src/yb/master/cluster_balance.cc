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

#include "yb/master/cluster_balance.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <boost/algorithm/string/join.hpp>
#include <boost/optional/optional.hpp>

#include "yb/common/common.pb.h"

#include "yb/consensus/quorum_util.h"

#include "yb/gutil/casts.h"

#include "yb/master/catalog_manager_util.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"

#include "yb/util/flags.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

DEFINE_RUNTIME_bool(enable_load_balancing, true,
    "Choose whether to enable the load balancing algorithm, to move tablets around.");

DEFINE_RUNTIME_bool(transaction_tables_use_preferred_zones, false,
    "Choose whether transaction tablet leaders respect preferred zones.");

DEFINE_RUNTIME_bool(enable_global_load_balancing, true,
    "Choose whether to allow the load balancer to make moves that strictly only balance "
    "global load. Note that global balancing only occurs after all tables are balanced.");

DEFINE_RUNTIME_int32(leader_balance_threshold, 0,
    "Number of leaders per each tablet server to balance below. If this is configured to "
    "0 (the default), the leaders will be balanced optimally at extra cost.");

DEFINE_RUNTIME_int32(leader_balance_unresponsive_timeout_ms, 3 * 1000,
    "The period of time that a master can go without receiving a heartbeat from a "
    "tablet server before considering it unresponsive. Unresponsive servers are "
    "excluded from leader balancing.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_tablet_remote_bootstraps, 10,
    "Maximum number of tablets being remote bootstrapped across the cluster.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_tablet_remote_bootstraps_per_table, 2,
    "Maximum number of tablets being remote bootstrapped for any table. The maximum "
    "number of remote bootstraps across the cluster is still limited by the flag "
    "load_balancer_max_concurrent_tablet_remote_bootstraps. This flag is meant to prevent "
    "a single table use all the available remote bootstrap sessions and starving other "
    "tables.");

DEFINE_RUNTIME_int32(load_balancer_max_over_replicated_tablets, 1,
    "Maximum number of running tablet replicas that are allowed to be over the configured "
    "replication factor.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_adds, 1,
    "Maximum number of tablet peer replicas to add in any one run of the load balancer.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_removals, 1,
    "Maximum number of over-replicated tablet peer removals to do in any one run of the "
    "load balancer.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_moves, 100,
    "Maximum number of tablet leaders on tablet servers (across the cluster) to move in "
    "any one run of the load balancer.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_moves_per_table, -1,
    "Maximum number of tablet leaders per table to move in any one run of the load "
    "balancer. The maximum number of tablet leader moves across the cluster is still "
    "limited by the flag load_balancer_max_concurrent_moves. This flag is meant to "
    "prevent a single table from using all of the leader moves quota and starving "
    "other tables."
    "If set to -1, the number of leader moves per table is set to the global number of leader "
    "moves (load_balancer_max_concurrent_moves).");

DEFINE_RUNTIME_int32(load_balancer_num_idle_runs, 5,
    "Number of idle runs of load balancer to deem it idle.");

DEFINE_test_flag(bool, load_balancer_handle_under_replicated_tablets_only, false,
                 "Limit the functionality of the load balancer during tests so tests can make "
                 "progress");

// No longer used because leader stepdown is not as slow as it used to be.
DEPRECATE_FLAG(bool, load_balancer_skip_leader_as_remove_victim, "10_2022");

DEFINE_RUNTIME_bool(allow_leader_balancing_dead_node, true,
    "When a tserver is marked as dead, do we continue leader balancing for tables that "
    "have a replica on this tserver");

DEFINE_test_flag(int32, load_balancer_wait_ms, 0,
                 "For testing purposes, number of milliseconds to wait at the start of a load "
                 "balancer iteration.");

DEFINE_test_flag(int32, load_balancer_wait_after_count_pending_tasks_ms, 0,
                 "For testing purposes, number of milliseconds to wait after counting and "
                 "finding pending tasks.");

DECLARE_int32(min_leader_stepdown_retry_interval_ms);
DECLARE_bool(enable_ysql_tablespaces_for_placement);

DEFINE_RUNTIME_bool(load_balancer_count_move_as_add, true,
    "Should we enable state change to count add server triggered by load move as just an "
    "add instead of both an add and remove.");

DEFINE_RUNTIME_bool(load_balancer_drive_aware, true,
    "When LB decides to move a tablet from server A to B, on the target LB "
    "should select the tablet to move from most loaded drive.");

DEFINE_RUNTIME_bool(load_balancer_ignore_cloud_info_similarity, false,
    "If true, ignore the similarity between cloud infos when deciding which tablet to move");

METRIC_DEFINE_gauge_int64(cluster,
                          is_load_balancing_enabled,
                          "Is Load Balancing Enabled",
                          yb::MetricUnit::kUnits,
                          "Is load balancing enabled in the cluster where "
                          "1 indicates it is enabled.");

namespace yb {
namespace master {

using std::unique_ptr;
using std::string;
using std::set;
using std::vector;
using strings::Substitute;

namespace {

vector<set<TabletId>> GetTabletsOnTSToMove(bool drive_aware,
                                         const CBTabletServerMetadata& from_ts_meta) {
  vector<set<TabletId>> all_tablets;
  if (drive_aware) {
    for (const auto& path : from_ts_meta.sorted_path_load_by_tablets_count) {
      auto path_list = from_ts_meta.path_to_tablets.find(path);
      if (path_list == from_ts_meta.path_to_tablets.end()) {
        LOG(INFO) << "Found uninitialized path: " << path;
        continue;
      }
      all_tablets.push_back(path_list->second);
    }
  } else {
    all_tablets.push_back(from_ts_meta.running_tablets);
  }
  return all_tablets;
}

// Returns sorted list of pair tablet id and path on to_ts.
std::vector<std::pair<TabletId, std::string>> GetLeadersOnTSToMove(
    bool drive_aware, const set<TabletId>& leaders, const CBTabletServerMetadata& to_ts_meta) {
  std::vector<std::pair<TabletId, std::string>> peers;
  if (drive_aware) {
    for (const auto& path : to_ts_meta.sorted_path_load_by_leader_count) {
      auto path_list = to_ts_meta.path_to_tablets.find(path);
      if (path_list == to_ts_meta.path_to_tablets.end()) {
        // No tablets on this path, so skip it.
        continue;
      }
      transform(path_list->second.begin(), path_list->second.end(), std::back_inserter(peers),
                [&path_list](const TabletId& tablet_id) -> std::pair<TabletId, std::string> {
                  return make_pair(tablet_id, path_list->first);
                 });
    }
  } else {
    transform(to_ts_meta.running_tablets.begin(), to_ts_meta.running_tablets.end(),
              std::back_inserter(peers),
              [](const TabletId& tablet_id) -> std::pair<TabletId, std::string> {
                return make_pair(tablet_id, "");
               });
  }
  std::vector<std::pair<TabletId, std::string>> intersection;
  copy_if(peers.begin(), peers.end(), std::back_inserter(intersection),
          [&leaders](const std::pair<TabletId, std::string>& tablet) {
            return leaders.count(tablet.first) > 0;
          });
  return intersection;
}

} // namespace

Result<ReplicationInfoPB> ClusterLoadBalancer::GetTableReplicationInfo(
    const scoped_refptr<const TableInfo>& table) const {
  return CatalogManagerUtil::GetTableReplicationInfo(
      table,
      catalog_manager_->GetTablespaceManager(),
      catalog_manager_->ClusterConfig()->LockForRead()->pb.replication_info());
}

void ClusterLoadBalancer::InitTablespaceManager() {
  tablespace_manager_ = catalog_manager_->GetTablespaceManager();
}

Status ClusterLoadBalancer::PopulateReplicationInfo(
    const scoped_refptr<TableInfo>& table, const ReplicationInfoPB& replication_info) {
  PlacementInfoPB pb;

  if (state_->options_->type == LIVE) {
    pb.CopyFrom(replication_info.live_replicas());
  } else if (state_->options_->type == READ_ONLY) {
    if (replication_info.read_replicas_size() == 0) {
      // Should not reach here as tables that should not have read replicas should
      // have already been skipped before reaching here.
      return STATUS_FORMAT(
          IllegalState, "Encountered a table $0 with no read replicas. Placement info $1",
          table->id(), replication_info);
    }
    pb.CopyFrom(GetReadOnlyPlacementFromUuid(replication_info));
  }

  state_->placement_by_table_[table->id()] = std::move(pb);

  bool is_txn_table = table->GetTableType() == TRANSACTION_STATUS_TABLE_TYPE;
  state_->use_preferred_zones_ = !is_txn_table || FLAGS_transaction_tables_use_preferred_zones;
  if (state_->use_preferred_zones_) {
    GetAllAffinitizedZones(replication_info, &state_->affinitized_zones_);
  }

  return Status::OK();
}

Status ClusterLoadBalancer::UpdateTabletInfo(TabletInfo* tablet) {
  // The placement information should already be set.
  if (!state_->placement_by_table_.count(tablet->table()->id())) {
    return STATUS_FORMAT(
        IllegalState, "Encountered a table $0 with no Placement info.", tablet->table()->name());
  }

  return state_->UpdateTablet(tablet);
}

const PlacementInfoPB& ClusterLoadBalancer::GetPlacementByTablet(const TabletId& tablet_id) const {
  const auto& table_id = GetTabletMap().at(tablet_id)->table()->id();
  return state_->placement_by_table_.at(table_id);
}

size_t ClusterLoadBalancer::get_total_wrong_placement() const {
  return state_->tablets_wrong_placement_.size();
}

size_t ClusterLoadBalancer::get_total_blacklisted_servers() const {
  return global_state_->blacklisted_servers_.size();
}

size_t ClusterLoadBalancer::get_total_leader_blacklisted_servers() const {
  return global_state_->leader_blacklisted_servers_.size();
}

size_t ClusterLoadBalancer::get_total_over_replication() const {
  return state_->tablets_over_replicated_.size();
}

size_t ClusterLoadBalancer::get_total_under_replication() const {
  return state_->tablets_missing_replicas_.size();
}

size_t ClusterLoadBalancer::get_total_starting_tablets() const {
  return global_state_->total_starting_tablets_;
}

int ClusterLoadBalancer::get_total_running_tablets() const {
  return state_->total_running_;
}

bool ClusterLoadBalancer::IsLoadBalancerEnabled() const {
  return FLAGS_enable_load_balancing && is_enabled_;
}

// Load balancer class.
ClusterLoadBalancer::ClusterLoadBalancer(CatalogManager* cm)
    : random_(GetRandomSeed32()),
      is_enabled_(FLAGS_enable_load_balancing),
      cbuf_activities_(FLAGS_load_balancer_num_idle_runs),
      epoch_(LeaderEpoch(-1)) {
  ResetGlobalState(false /* initialize_ts_descs */);

  catalog_manager_ = cm;
  last_load_balance_run_ = MonoTime::Min();
}

// Reduce remaining_tasks by pending_tasks value, after sanitizing inputs.
template <class T>
void set_remaining(T pending_tasks, T* remaining_tasks) {
  if (pending_tasks > *remaining_tasks) {
    LOG(WARNING) << "Pending tasks > max allowed tasks: " << pending_tasks << " > "
                 << *remaining_tasks;
    *remaining_tasks = 0;
  } else {
    *remaining_tasks -= pending_tasks;
  }
}

// Needed as we have a unique_ptr to the forward declared PerTableLoadState class.
ClusterLoadBalancer::~ClusterLoadBalancer() = default;

void ClusterLoadBalancer::InitMetrics() {
  is_load_balancing_enabled_metric_ = METRIC_is_load_balancing_enabled.Instantiate(
      catalog_manager_->master_->metric_entity_cluster(), 0);
}

// This function uses the following stratification of vlog levels:
//  - Things that are printed at most once per run can be at any level >= 1
//  - Things that are printed at most once per table per run can be at any level >= 2
//  - Things that are printed multiple times per table per run can be at any level >= 3
void ClusterLoadBalancer::RunLoadBalancerWithOptions(Options* options) {
  if (!IsLoadBalancerEnabled()) {
    YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 10, 1) << "Load balancing is not enabled.";
    return;
  }
  AtomicFlagSleepMs(&FLAGS_TEST_load_balancer_wait_ms);
  VLOG(1) << "Running load balancer";
  VLOG(2) << "Load balancer options: " << options->ToString();

  ResetGlobalState();

  uint32_t master_errors = 0;

  if (!FLAGS_transaction_tables_use_preferred_zones) {
    VLOG(1) << "FLAGS_transaction_tables_use_preferred_zones is not set. "
            << "Transaction tables will not respect leadership affinity.";
  }

  std::unique_ptr<Options> options_unique_ptr;
  if (options == nullptr) {
    options_unique_ptr = std::make_unique<Options>();
    options = options_unique_ptr.get();
  }

  InitTablespaceManager();

  // Lock the CatalogManager maps for the duration of the load balancer run.
  CatalogManager::SharedLock lock(catalog_manager_->mutex_);

  int remaining_adds = options->kMaxConcurrentAdds;
  int remaining_removals = options->kMaxConcurrentRemovals;
  int remaining_leader_moves = options->kMaxConcurrentLeaderMoves;

  // Loop over all tables to get the count of pending tasks.
  int pending_add_replica_tasks = 0;
  int pending_remove_replica_tasks = 0;
  int pending_stepdown_leader_tasks = 0;

  // Set blacklist upfront since per table states require it.
  // Also, set tservers that have pending deletes.
  SetBlacklistAndPendingDeleteTS();

  for (const auto& table : GetTables()) {
    if (SkipLoadBalancing(*table)) {
      // Populate the list of tables for which LB has been skipped
      // in LB's internal vector.
      skipped_tables_per_run_.push_back(table);
      continue;
    }
    const TableId& table_id = table->id();

    if (tablespace_manager_->NeedsRefreshToFindTablePlacement(table)) {
      // Placement information was not present in catalog manager cache. This is probably a
      // recently created table, skip load balancing for now, hopefully by the next run,
      // the background task in the catalog manager will pick up the placement information
      // for this table from the PG catalog tables.
      // TODO(deepthi) Keep track of the number of times this happens, take appropriate action
      // if placement stays missing over period of time.
      YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 2) << "Skipping load balancing for table " << table_id
          << " as its placement information is not available yet";
      master_errors++;
      continue;
    }

    const auto& replication_info = GetTableReplicationInfo(table);
    if (!replication_info) {
      YB_LOG_EVERY_N_SECS_OR_VLOG(WARNING, 10, 2) << "Skipping load balancing for table "
          << table_id << " as fetching replication info failed with error "
          << StatusToString(replication_info.status());
      master_errors++;
      continue;
    }
    VLOG(2) << Format("Replication info for table $0: $1", table_id,
        replication_info->ShortDebugString());

    if (options->type == READ_ONLY) {
      if (replication_info->read_replicas_size() == 0) {
        // The table has a replication policy without any read replicas present.
        // The LoadBalancer is handling read replicas in this run, so this
        // table can be skipped.
        VLOG(2) << Format("Skipping table $0 with no read replicas configured in read replica run",
            table_id);
        continue;
      }
    }

    ResetTableStatePtr(table_id, options);

    auto populate_ri_status = PopulateReplicationInfo(table, *replication_info);
    if (!populate_ri_status.ok()) {
      YB_LOG_EVERY_N_SECS_OR_VLOG(WARNING, 10, 2) << "Skipping load balancing for table "
          << table_id << ": as populating replication info failed with error "
          << StatusToString(populate_ri_status);
      master_errors++;
      continue;
    }

    InitializeTSDescriptors();

    Status s = CountPendingTasksUnlocked(table_id,
                                         &pending_add_replica_tasks,
                                         &pending_remove_replica_tasks,
                                         &pending_stepdown_leader_tasks);
    if (!s.ok()) {
      // Found uninitialized ts_meta, so don't load balance this table yet.
      LOG(WARNING) << "Skipping load balancing for table " << table_id << ": " << StatusToString(s);
      per_table_states_.erase(table_id);
      master_errors++;
      continue;
    }
    VLOG(2) << "Table " << table_id << " has " << pending_add_replica_tasks << " pending adds, "
            << pending_remove_replica_tasks << " pending removes, "
            << pending_stepdown_leader_tasks << " pending leader stepdowns";
  }

  if (pending_add_replica_tasks + pending_remove_replica_tasks + pending_stepdown_leader_tasks> 0) {
    LOG(INFO) << "Total pending adds=" << pending_add_replica_tasks << ", total pending removals="
              << pending_remove_replica_tasks << ", total pending leader stepdowns="
              << pending_stepdown_leader_tasks;
    if (PREDICT_FALSE(FLAGS_TEST_load_balancer_wait_after_count_pending_tasks_ms > 0)) {
      LOG(INFO) << "Sleeping after finding pending tasks for "
                << FLAGS_TEST_load_balancer_wait_after_count_pending_tasks_ms << " ms";
      SleepFor(
          MonoDelta::FromMilliseconds(FLAGS_TEST_load_balancer_wait_after_count_pending_tasks_ms));
    }
  }

  set_remaining(pending_add_replica_tasks, &remaining_adds);
  set_remaining(pending_remove_replica_tasks, &remaining_removals);
  set_remaining(pending_stepdown_leader_tasks, &remaining_leader_moves);

  // At the start of the run, report LB state that might prevent it from running smoothly.
  ReportUnusualLoadBalancerState();

  // Loop over all tables to analyze the global and per-table load.
  for (const auto& table : GetTables()) {
    if (SkipLoadBalancing(*table)) {
      continue;
    }

    auto it = per_table_states_.find(table->id());
    if (it == per_table_states_.end()) {
      // If the table state doesn't exist, it was not fully initialized in the previous iteration.
      VLOG(2) << "Unable to find the state for table " << table->id() << ". Skipping.";
      continue;
    }
    state_ = it->second.get();

    // Prepare the in-memory structures.
    auto handle_analyze_tablets = AnalyzeTabletsUnlocked(table->id());
    if (!handle_analyze_tablets.ok()) {
      LOG(WARNING) << "Skipping load balancing for table " << table->id() << ": "
                   << StatusToString(handle_analyze_tablets);
      per_table_states_.erase(table->id());
      master_errors++;
    }
  }

  VLOG(1) << "Global state after analyzing all tablets: " << global_state_->ToString();

  bool task_added = false;

  // Iterate over all the tables to take actions based on the data collected on the previous loop.
  for (const auto& table : GetTables()) {
    state_ = nullptr;
    if (remaining_adds == 0 && remaining_removals == 0 && remaining_leader_moves == 0) {
      break;
    }
    if (SkipLoadBalancing(*table)) {
      continue;
    }

    auto it = per_table_states_.find(table->id());
    if (it == per_table_states_.end()) {
      // If the table state doesn't exist, it didn't get analyzed by the previous iteration.
      VLOG(2) << "Unable to find table state for table " << table->id()
              << ". Skipping load balancing execution";
      continue;
    }
    state_ = it->second.get();

    // We may have modified global loads, so we need to reset this state's load.
    state_->SortLoad();

    VLOG(2) << "Per table state for table: " << table->id() << ", " << state_->ToString();
    VLOG(2) << "Global state: " << table->id() << ", " << state_->ToString();
    VLOG(2) << "Sorted load: " << table->id() << ", " << GetSortedLoad();
    VLOG(2) << "Global load: " << table->id() << ", " << GetSortedLeaderLoad();

    // Output parameters are unused in the load balancer, but useful in testing.
    TabletId out_tablet_id;
    TabletServerId out_from_ts;
    TabletServerId out_to_ts;

    if (!PREDICT_FALSE(FLAGS_TEST_load_balancer_handle_under_replicated_tablets_only)) {
      // Handle cleanup after over-replication.
      for (; remaining_removals > 0; --remaining_removals) {
        if (state_->allow_only_leader_balancing_) {
          YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 2)
              << "Skipping removing replicas. Only leader balancing table " << table->id();
          break;
        }
        auto handle_remove = HandleRemoveReplicas(&out_tablet_id, &out_from_ts);
        if (!handle_remove.ok()) {
          LOG(WARNING) << "Skipping removing replicas for " << table->id() << ": "
                       << StatusToString(handle_remove);
          master_errors++;
          break;
        }
        if (!*handle_remove) {
          break;
        }

        VLOG(3) << "Sorted load after HandleRemoveReplicas: " << GetSortedLoad();
        task_added = true;
      }
    }

    // Handle adding and moving replicas.
    for ( ; remaining_adds > 0; --remaining_adds) {
      if (state_->allow_only_leader_balancing_) {
        YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 2)
            << "Skipping adding replicas. Only leader balancing table " << table->id();
        break;
      }
      auto handle_add = HandleAddReplicas(&out_tablet_id, &out_from_ts, &out_to_ts);
      if (!handle_add.ok()) {
        LOG(WARNING) << "Skipping add replicas for " << table->id() << ": "
                     << StatusToString(handle_add);
        master_errors++;
        break;
      }
      if (!*handle_add) {
        break;
      }

      VLOG(3) << "Sorted load after HandleAddReplicas: " << GetSortedLoad();
      task_added = true;
    }

    if (PREDICT_FALSE(FLAGS_TEST_load_balancer_handle_under_replicated_tablets_only)) {
      LOG(INFO) << "Skipping remove replicas and leader moves for " << table->id()
                << " because FLAGS_TEST_load_balancer_handle_under_replicated_tablets_only is set.";
      continue;
    }

    // Handle tablet servers with too many leaders.
    // Check the current pending tasks per table to ensure we don't trigger the same task.
    size_t table_remaining_leader_moves = state_->options_->kMaxConcurrentLeaderMovesPerTable;
    set_remaining(state_->pending_stepdown_leader_tasks_[table->id()].size(),
                  &table_remaining_leader_moves);
    VLOG(2) << "Remaining leader moves for table " << table->id() << " is "
            << table_remaining_leader_moves;
    // Keep track of both the global and per table limit on number of moves.
    for ( ;
         remaining_leader_moves > 0 && table_remaining_leader_moves > 0;
         --remaining_leader_moves, --table_remaining_leader_moves) {
      auto handle_leader = HandleLeaderMoves(&out_tablet_id, &out_from_ts, &out_to_ts);
      if (!handle_leader.ok()) {
        LOG(WARNING) << "Skipping leader moves for " << table->id() << ": "
                     << StatusToString(handle_leader);
        master_errors++;
        break;
      }
      if (!*handle_leader) {
        break;
      }

      VLOG(3) << "Sorted leader load after HandleLeaderMoves: " << GetSortedLeaderLoad();
      task_added = true;
    }
  }

  RecordActivity(task_added, master_errors);
}

void ClusterLoadBalancer::RunLoadBalancer(const LeaderEpoch& epoch, Options* options) {
  epoch_ = epoch;
  SysClusterConfigEntryPB config;
  CHECK_OK(catalog_manager_->GetClusterConfig(&config));

  std::unique_ptr<Options> options_unique_ptr =
      std::make_unique<Options>();
  Options* options_ent = options_unique_ptr.get();
  // First, we load balance the live cluster.
  options_ent->type = LIVE;
  if (config.replication_info().live_replicas().has_placement_uuid()) {
    options_ent->placement_uuid = config.replication_info().live_replicas().placement_uuid();
    options_ent->live_placement_uuid = options_ent->placement_uuid;
  } else {
    options_ent->placement_uuid = "";
    options_ent->live_placement_uuid = "";
  }
  RunLoadBalancerWithOptions(options_ent);

  // Then, we balance all read-only clusters.
  options_ent->type = READ_ONLY;
  for (int i = 0; i < config.replication_info().read_replicas_size(); i++) {
    const PlacementInfoPB& read_only_cluster = config.replication_info().read_replicas(i);
    options_ent->placement_uuid = read_only_cluster.placement_uuid();
    RunLoadBalancerWithOptions(options_ent);
  }
}

void ClusterLoadBalancer::RecordActivity(bool tasks_added_in_this_run, uint32_t master_errors) {
  // Update the list of tables for whom load-balancing has been
  // skipped in this run.
  {
    std::lock_guard l(mutex_);
    skipped_tables_ = skipped_tables_per_run_;
  }

  uint32_t table_tasks = 0;
  for (const auto& table : GetTables()) {
    table_tasks += table->NumLBTasks();
  }

  if (!master_errors && !table_tasks && tasks_added_in_this_run) {
    VLOG(1) << "Tasks scheduled by load balancer have already completed. Force setting table tasks "
               "count to 1 so that it does not appear idle";
    ++table_tasks;
  }

  struct ActivityInfo ai {table_tasks, master_errors};

  // Update circular buffer summary.

  if (ai.IsIdle()) {
    num_idle_runs_++;
  } else {
    VLOG(1) <<
      Substitute("Load balancer has $0 table tasks and $1 master errors",
          table_tasks, master_errors);
  }

  if (cbuf_activities_.full()) {
    if (cbuf_activities_.front().IsIdle()) {
      num_idle_runs_--;
    }
  }

  // Mutate circular buffer.
  cbuf_activities_.push_back(std::move(ai));

  // Update state.
  is_idle_.store(num_idle_runs_ == cbuf_activities_.size(), std::memory_order_release);

  // Two interesting cases when updating can_perform_global_operations_ state:
  // If we previously couldn't balance global load, but now the LB is idle, enable global balancing.
  // If we previously could balance global load, but now the LB is busy, then it is busy balancing
  // global load or doing other operations (remove, etc.). In this case, we keep global balancing
  // enabled up until we perform a non-global balancing move (see GetLoadToMove()).
  // TODO(julien) some small improvements can be made here, such as ignoring leader stepdown tasks.
  can_perform_global_operations_ = can_perform_global_operations_ || ai.IsIdle();

  last_load_balance_run_ = MonoTime::Now();
}

MonoTime ClusterLoadBalancer::LastRunTime() const { return last_load_balance_run_; }

Status ClusterLoadBalancer::IsIdle() const {
  if (IsLoadBalancerEnabled() && !is_idle_.load(std::memory_order_acquire)) {
    return STATUS(
        IllegalState,
        "Task or error encountered recently.",
        MasterError(MasterErrorPB::LOAD_BALANCER_RECENTLY_ACTIVE));
  }

  return Status::OK();
}

bool ClusterLoadBalancer::CanBalanceGlobalLoad() const {
  return FLAGS_enable_global_load_balancing && can_perform_global_operations_;
}

void ClusterLoadBalancer::ReportMetrics() {
  is_load_balancing_enabled_metric_->set_value(IsLoadBalancerEnabled());
}

void ClusterLoadBalancer::ReportUnusualLoadBalancerState() const {
  for (const auto& ts_desc : global_state_->ts_descs_) {
    // Report if any ts has a pending delete.
    if (ts_desc->HasTabletDeletePending()) {
      LOG(INFO) << Format("tablet server $0 has a pending delete for tablets $1",
                          ts_desc->permanent_uuid(), ts_desc->PendingTabletDeleteToString());
    }
  }
}

void ClusterLoadBalancer::ResetGlobalState(bool initialize_ts_descs) {
  per_table_states_.clear();
  global_state_ = std::make_unique<GlobalLoadState>();
  global_state_->drive_aware_ = FLAGS_load_balancer_drive_aware;
  if (initialize_ts_descs) {
    // Only call GetAllDescriptors once for a LB run, and then cache it in global_state_.
    GetAllDescriptors(&global_state_->ts_descs_);
  }
  VLOG(1) << "Global state before analyzing tablets: "  << global_state_->ToString();
  skipped_tables_per_run_.clear();
}

void ClusterLoadBalancer::ResetTableStatePtr(const TableId& table_id, Options* options) {
  auto table_state = std::make_unique<PerTableLoadState>(global_state_.get());
  table_state->options_ = options;
  state_ = table_state.get();
  per_table_states_[table_id] = std::move(table_state);

  state_->table_id_ = table_id;
}

Status ClusterLoadBalancer::AnalyzeTabletsUnlocked(const TableId& table_uuid) {
  auto tablets = VERIFY_RESULT_PREPEND(
      GetTabletsForTable(table_uuid), "Skipping table " + table_uuid + " due to error: ");

  // Loop over tablet map to register the load that is already live in the cluster.
  for (const auto& tablet : tablets) {
    bool tablet_running = false;
    {
      auto tablet_lock = tablet->LockForRead();

      if (!tablet->table()) {
        // Tablet is orphaned or in preparing state, continue.
        continue;
      }
      tablet_running = tablet_lock->is_running();
    }

    // This is from the perspective of the CatalogManager and the on-disk, persisted
    // SysCatalogStatePB. What this means is that this tablet was properly created as part of a
    // CreateTable and the information was sent to the initial set of TS and the tablet got to an
    // initial running state.
    //
    // This is different from the individual, per-TS state of the tablet, which can vary based on
    // the TS itself. The tablet can be registered as RUNNING, as far as the CatalogManager is
    // concerned, but just be underreplicated, and have some TS currently bootstrapping instances
    // of the tablet.
    if (tablet_running) {
      RETURN_NOT_OK(UpdateTabletInfo(tablet.get()));
    }
  }

  // Once we've analyzed both the tablet server information as well as the tablets, we can sort the
  // load and are ready to apply the load balancing rules.
  state_->SortLoad();

  // Since leader load is only needed to rebalance leaders, we keep the sorting separate.
  state_->SortLeaderLoad();

  for (const auto& tablet : tablets) {
    const auto& tablet_id = tablet->id();
    if (state_->pending_remove_replica_tasks_[table_uuid].count(tablet_id) > 0) {
      const auto& from_ts = state_->pending_remove_replica_tasks_[table_uuid][tablet_id];
      VLOG(3) << Format("Adding pending remove replica task for tablet $0 from TS $1", tablet_id,
          from_ts);
      RETURN_NOT_OK(state_->RemoveReplica(tablet_id, from_ts));
    }
    if (state_->pending_stepdown_leader_tasks_[table_uuid].count(tablet_id) > 0) {
      const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
      const auto& from_ts = tablet_meta.leader_uuid;
      const auto& to_ts = state_->pending_stepdown_leader_tasks_[table_uuid][tablet_id];
      VLOG(3) << Format("Adding pending leader stepdown task for tablet $0 from TS $1 to TS $2",
          tablet_id, from_ts, to_ts);
      RETURN_NOT_OK(state_->MoveLeader(tablet->id(), from_ts, to_ts));
    }
    if (state_->pending_add_replica_tasks_[table_uuid].count(tablet_id) > 0) {
      const auto& to_ts = state_->pending_add_replica_tasks_[table_uuid][tablet_id];
      VLOG(3) << Format("Adding pending add replica task for tablet $0 to TS $1", tablet_id,
          to_ts);
      RETURN_NOT_OK(state_->AddReplica(tablet->id(), to_ts));
    }
  }

  return Status::OK();
}

Result<bool> ClusterLoadBalancer::HandleAddIfMissingPlacement(
    TabletId* out_tablet_id, TabletServerId* out_to_ts) {
  for (const auto& tablet_id : state_->tablets_missing_replicas_) {
    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    const auto& placement_info = GetPlacementByTablet(tablet_id);
    const auto& missing_placements = tablet_meta.under_replicated_placements;
    if (VLOG_IS_ON(3)) {
      std::ostringstream out;
      out << Format("Tablet $0 has $1 placement(s) in which it is missing a replica. Finding "
          "valid TSs to add a replica to. Missing placements: ",
          tablet_id, missing_placements.size());
      for (auto& placements : missing_placements) {
        out << placements.ShortDebugString() << ", ";
      }
      VLOG(3) << out.str();
    }
    // Loop through TSs by load to find a TS that matches the placement needed and does not already
    // host this tablet.
    for (const auto& ts_uuid : state_->sorted_load_) {
      bool can_choose_ts = false;
      // If we had no placement information, it means we are just under-replicated, so just check
      // that we can use this tablet server.
      if (placement_info.placement_blocks().empty()) {
        // No need to check placement info, as there is none.
        can_choose_ts = VERIFY_RESULT(
            state_->CanAddTabletToTabletServer(tablet_id, ts_uuid, nullptr /* placement_info */));
        VLOG(3) << "Placement info is not available for tablet " << tablet_id << " so this tablet "
                << "can be added to any valid TS. TS " << ts_uuid
                << ", can choose: " << can_choose_ts;
      } else {
        // We added a tablet to the set with missing replicas both if it is under-replicated, and we
        // added a placement to the tablet_meta under_replicated_placements if the num replicas in
        // that placement is fewer than min_num_replicas. If the under-replicated tablet has a
        // placement that is under-replicated and the ts is not in that placement, then that ts
        // isn't valid.
        const auto& ts_meta = state_->per_ts_meta_[ts_uuid];
        // Either we have specific placement blocks that are under-replicated, so confirm
        // that this TS matches or all the placement blocks have min_num_replicas
        // but overall num_replicas is fewer than expected.
        // In the latter case, we still need to conform to the placement rules.
        VLOG(3) << "Tablet " << tablet_id << " has " << missing_placements.size()
                << " missing placements, checking if we can add to tserver "
                << ts_uuid;
        if (missing_placements.empty() ||
            tablet_meta.CanAddTSToMissingPlacements(ts_meta.descriptor)) {
          // If we don't have any missing placements but are under-replicated then we need to
          // validate placement information in order to avoid adding to a wrong placement block.
          //
          // Do the placement check for both the cases.
          // If we have missing placements then this check is a tautology otherwise it matters.
          can_choose_ts = VERIFY_RESULT(state_->CanAddTabletToTabletServer(tablet_id, ts_uuid,
                                                                                &placement_info));
        }
      }
      // If we've passed the checks, then we can choose this TS to add the replica to.
      if (can_choose_ts) {
        *out_tablet_id = tablet_id;
        *out_to_ts = ts_uuid;
        VLOG(3) << "Found tserver " << ts_uuid << " to add a replica of tablet " << tablet_id;
        RETURN_NOT_OK(AddReplica(tablet_id, ts_uuid));
        state_->tablets_missing_replicas_.erase(tablet_id);
        return true;
      }
    }
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleAddIfWrongPlacement(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  for (const auto& tablet_id : state_->tablets_wrong_placement_) {
    VLOG(3) << "Tablet " << tablet_id << " has copies in wrong placement"
            << " attempting to find a tserver to move this replica.";
    // Skip this tablet, if it is already over-replicated, as it does not need another replica, it
    // should just have one removed in the removal step.
    if (state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }
    if (VERIFY_RESULT(state_->CanSelectWrongPlacementReplicaToMove(
            tablet_id, GetPlacementByTablet(tablet_id), out_from_ts, out_to_ts))) {
      *out_tablet_id = tablet_id;
      VLOG(3) << "Found destination server " << *out_to_ts << " to move tablet replica "
              << tablet_id << " from " << *out_from_ts;
      RETURN_NOT_OK(MoveReplica(tablet_id, *out_from_ts, *out_to_ts));
      return true;
    }
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleAddReplicas(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  DCHECK(!state_->allow_only_leader_balancing_);

  if (state_->options_->kAllowLimitStartingTablets) {
    if (global_state_->total_starting_tablets_ >= state_->options_->kMaxTabletRemoteBootstraps) {
      return STATUS_SUBSTITUTE(TryAgain, "Cannot add replicas. Currently remote bootstrapping $0 "
          "tablets, when our max allowed is $1",
          global_state_->total_starting_tablets_, state_->options_->kMaxTabletRemoteBootstraps);
    } else if (state_->total_starting_ >= state_->options_->kMaxTabletRemoteBootstrapsPerTable) {
      return STATUS_SUBSTITUTE(TryAgain, "Cannot add replicas. Currently remote bootstrapping $0 "
          "tablets for table $1, when our max allowed is $2 per table",
          state_->total_starting_, state_->table_id_,
          state_->options_->kMaxTabletRemoteBootstrapsPerTable);
    }
  }

  if (state_->options_->kAllowLimitOverReplicatedTablets &&
      get_total_over_replication() >=
          implicit_cast<size_t>(state_->options_->kMaxOverReplicatedTablets)) {
    return STATUS_SUBSTITUTE(TryAgain,
        "Cannot add replicas. Currently have a total overreplication of $0, when max allowed is $1"
        ", overreplicated tablets: $2",
        get_total_over_replication(), state_->options_->kMaxOverReplicatedTablets,
        boost::algorithm::join(state_->tablets_over_replicated_, ", "));
  }

  VLOG(3) << "Number of global concurrent remote bootstrap sessions: "
          <<  global_state_->total_starting_tablets_
          << ", max allowed: " << state_->options_->kMaxTabletRemoteBootstraps
          << ". Number of concurrent remote bootstrap sessions for table " << state_->table_id_
          << ": " << state_->total_starting_
          << ", max allowed: " << state_->options_->kMaxTabletRemoteBootstrapsPerTable;

  // Handle missing placements with highest priority, as it means we're potentially
  // under-replicated.
  if (VERIFY_RESULT(HandleAddIfMissingPlacement(out_tablet_id, out_to_ts))) {
    return true;
  }

  // Handle wrong placements as next priority, as these could be servers we're moving off of, so
  // we can decommission ASAP.
  if (VERIFY_RESULT(HandleAddIfWrongPlacement(out_tablet_id, out_from_ts, out_to_ts))) {
    return true;
  }

  // Finally, handle normal load balancing.
  if (!VERIFY_RESULT(GetLoadToMove(out_tablet_id, out_from_ts, out_to_ts))) {
    if (VLOG_IS_ON(2)) {
      VLOG(2) << "Cannot find any more tablets to move for this table, under current constraints. "
              << "Sorted load: " << GetSortedLoad();
    }
    return false;
  }

  return true;
}

string ClusterLoadBalancer::GetSortedLoad() const {
  ssize_t last_pos = state_->sorted_load_.size() - 1;
  std::ostringstream out;
  for (ssize_t left = 0; left <= last_pos; ++left) {
    const TabletServerId& uuid = state_->sorted_load_[left];
    bool blacklisted = global_state_->blacklisted_servers_.contains(uuid);
    out << Format("$0$1: $2 (global=$3), ",
        uuid,
        blacklisted ? "[blacklisted]" : "",
        state_->GetLoad(uuid),
        global_state_->GetGlobalLoad(uuid));
  }
  return out.str();
}

string ClusterLoadBalancer::GetSortedLeaderLoad() const {
  std::ostringstream out;
  for (const auto& leader_set : state_->sorted_leader_load_) {
    for (const auto& ts_uuid : leader_set) {
      bool blacklisted = global_state_->leader_blacklisted_servers_.contains(ts_uuid);
      out << Format("$0$1: $2 (global=$3), ",
          ts_uuid,
          blacklisted ? "[leader blacklisted]" : "",
          state_->GetLeaderLoad(ts_uuid),
          global_state_->GetGlobalLeaderLoad(ts_uuid));
    }
  }
  return out.str();
}

Result<bool> ClusterLoadBalancer::GetLoadToMove(
    TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts) {
  if (state_->sorted_load_.empty()) {
    return false;
  }

  // Start with two indices pointing at left and right most ends of the sorted_load_ structure.
  //
  // We will try to find two TSs that have at least one tablet that can be moved amongst them, from
  // the higher load to the lower load TS. To do this, we will go through comparing the TSs
  // corresponding to our left and right indices, exclude tablets from the right, high loaded TS
  // according to our load balancing rules, such as load variance, starting tablets and not moving
  // already over-replicated tablets. We then compare the remaining set of tablets with the ones
  // hosted by the lower loaded TS and use ReservoirSample to pick a tablet from the set
  // difference. If there were no tablets to pick, we advance our state.
  //
  // The state is defined as the positions of the start and end indices. We always try to move the
  // right index back, until we cannot any more, due to either reaching the left index (cannot
  // rebalance from one TS to itself), or the difference of load between the two TSs is too low to
  // try to rebalance (if load variance is 1, it does not make sense to move tablets between the
  // TSs). When we cannot lower the right index any further, we reset it back to last_pos and
  // increment the left index.
  //
  // We stop the whole algorithm if the left index reaches last_pos, or if we reset the right index
  // and are already breaking the invariance rule, as that means that any further differences in
  // the interval between left and right cannot have load > kMinLoadVarianceToBalance.
  ssize_t last_pos = state_->sorted_load_.size() - 1;
  for (ssize_t left = 0; left <= last_pos; ++left) {
    for (auto right = last_pos; right >= 0; --right) {
      const TabletServerId& low_load_uuid = state_->sorted_load_[left];
      const TabletServerId& high_load_uuid = state_->sorted_load_[right];
      ssize_t load_variance = state_->GetLoad(high_load_uuid) - state_->GetLoad(low_load_uuid);
      bool is_global_balancing_move = false;

      // Check for state change or end conditions.
      if (left == right || load_variance < state_->options_->kMinLoadVarianceToBalance) {
        // Either both left and right are at the end, or there is no load_variance, which means
        // there will be no load_variance for any TSs between left and right, so we can return.
        if (right == last_pos && load_variance == 0) {
          return false;
        }
        // If there is load variance, then there is a chance we can benefit from globally balancing.
        if (load_variance > 0 && CanBalanceGlobalLoad()) {
          int global_load_variance = global_state_->GetGlobalLoad(high_load_uuid) -
                                     global_state_->GetGlobalLoad(low_load_uuid);
          if (global_load_variance < state_->options_->kMinGlobalLoadVarianceToBalance) {
            // Already globally balanced. Since we are sorted by global load, we can return here as
            // there are no other moves for us to make.
            return false;
          }
          VLOG(3) << "Global data load balancing is in effect now";
          // Mark this move as a global balancing move and try to find a tablet to move.
          is_global_balancing_move = true;
        } else {
          // The load_variance is too low, which means we weren't able to find a load to move to
          // the left tserver. Continue and try with the next left tserver.
          break;
        }
      }

      // If we don't find a tablet_id to move between these two TSs, advance the state.
      if (VERIFY_RESULT(GetTabletToMove(high_load_uuid, low_load_uuid, moving_tablet_id))) {
        // If we got this far, we have the candidate we want, so fill in the output params and
        // return. The tablet_id is filled in from GetTabletToMove.
        *from_ts = high_load_uuid;
        *to_ts = low_load_uuid;
        VLOG(3) << "Found tablet " << *moving_tablet_id << " to move from "
                << *from_ts << " to ts " << *to_ts;
        RETURN_NOT_OK(MoveReplica(*moving_tablet_id, high_load_uuid, low_load_uuid));
        // Update global state if necessary.
        if (!is_global_balancing_move) {
          can_perform_global_operations_ = false;
        }
        return true;
      }
    }
  }

  // Should never get here.
  return STATUS(IllegalState, "Load balancing algorithm reached illegal state.");
}

Result<bool> ClusterLoadBalancer::GetTabletToMove(
    const TabletServerId& from_ts, const TabletServerId& to_ts, TabletId* moving_tablet_id) {
  const auto& from_ts_meta = state_->per_ts_meta_[from_ts];
  // If drive aware, all_tablets is sorted by decreasing drive load.
  vector<set<TabletId>> all_tablets_by_drive = GetTabletsOnTSToMove(global_state_->drive_aware_,
                                                                    from_ts_meta);
  vector<set<TabletId>> all_filtered_tablets_by_drive;
  for (const set<TabletId>& drive_tablets : all_tablets_by_drive) {
    set<TabletId> filtered_drive_tablets;
    for (const TabletId& tablet_id : drive_tablets) {
      // We don't want to add a new replica to an already over-replicated tablet.
      //
      // TODO(bogdan): should make sure we pick tablets that this TS is not a leader of, so we
      // can ensure HandleRemoveReplicas removes them from this TS.
      if (state_->tablets_over_replicated_.count(tablet_id)) {
        continue;
      }
      // Don't move a replica right after split
      if (ContainsKey(from_ts_meta.disabled_by_ts_tablets, tablet_id)) {
        continue;
      }

      if (VERIFY_RESULT(
          state_->CanAddTabletToTabletServer(tablet_id, to_ts, &GetPlacementByTablet(tablet_id)))) {
        filtered_drive_tablets.insert(tablet_id);
      }
    }
    all_filtered_tablets_by_drive.push_back(filtered_drive_tablets);
  }

  // Below, we choose a tablet to move. We first filter out any tablets which cannot be moved
  // because of placement limitations. Then, we prioritize moving a tablet whose leader is in the
  // same zone/region it is moving to (for faster remote bootstrapping).
  for (const set<TabletId>& drive_tablets : all_filtered_tablets_by_drive) {
    VLOG(3) << Format("All tablets being considered for movement from ts $0 to ts $1 for this "
        "drive are: $2", from_ts, to_ts, drive_tablets);

    bool found_tablet_to_move = false;
    CatalogManagerUtil::CloudInfoSimilarity chosen_tablet_ci_similarity =
        CatalogManagerUtil::NO_MATCH;
    for (const TabletId& tablet_id : drive_tablets) {
      const auto& placement_info = GetPlacementByTablet(tablet_id);
      // TODO(#15853): this should be augmented as well to allow dropping by one replica, if still
      // leaving us with more than the minimum.
      //
      // If we have placement information, we want to only pick the tablet if it's moving to the
      // same placement, so we guarantee we're keeping the same type of distribution.
      // Since we allow prefixes as well, we can still respect the placement of this tablet
      // even if their placement ids aren't the same. An e.g.
      // placement info of tablet: C.R1.*
      // placement info of from_ts: C.R1.Z1
      // placement info of to_ts: C.R2.Z2
      // Note that we've assumed that for every TS there is a unique placement block to which it
      // can be mapped (see the validation rules in yb_admin-client). If there is no unique
      // placement block then it is simply the C.R.Z of the TS itself.
      auto from_ts_block = state_->GetValidPlacement(from_ts, &placement_info);
      auto to_ts_block = state_->GetValidPlacement(to_ts, &placement_info);
      bool same_placement = false;
      if (to_ts_block.has_value() && from_ts_block.has_value()) {
          same_placement = TSDescriptor::generate_placement_id(*from_ts_block) ==
                                  TSDescriptor::generate_placement_id(*to_ts_block);
      }

      if (!placement_info.placement_blocks().empty() && !same_placement) {
        continue;
      }

      TabletServerId leader_ts = state_->per_tablet_meta_[tablet_id].leader_uuid;
      auto ci_similarity = CatalogManagerUtil::CloudInfoSimilarity::NO_MATCH;
      if (!leader_ts.empty() && !FLAGS_load_balancer_ignore_cloud_info_similarity) {
        const auto leader_ci = state_->per_ts_meta_[leader_ts].descriptor->GetCloudInfo();
        const auto to_ts_ci = state_->per_ts_meta_[to_ts].descriptor->GetCloudInfo();
        ci_similarity = CatalogManagerUtil::ComputeCloudInfoSimilarity(leader_ci, to_ts_ci);
      }

      if (found_tablet_to_move && ci_similarity <= chosen_tablet_ci_similarity) {
        continue;
      }
      // This is the best tablet to move, so far.
      found_tablet_to_move = true;
      *moving_tablet_id = tablet_id;
      chosen_tablet_ci_similarity = ci_similarity;
    }

    // If there is any tablet we can move from this drive, choose it and return.
    if (found_tablet_to_move) {
      VLOG(3) << "Found tablet " << *moving_tablet_id << " for moving from ts " << from_ts
            << " to ts "  << to_ts;
      return true;
    }
  }

  VLOG(3) << Format("Did not find any tablets to move from $0 to $1", from_ts, to_ts);
  return false;
}

Result<bool> ClusterLoadBalancer::GetLeaderToMoveWithinAffinitizedPriorities(
    TabletId* moving_tablet_id,
    TabletServerId* from_ts,
    TabletServerId* to_ts,
    std::string* to_ts_path) {
  for (const auto& leader_set : state_->sorted_leader_load_) {
    if (VERIFY_RESULT(GetLeaderToMove(leader_set, moving_tablet_id, from_ts, to_ts, to_ts_path))) {
      return true;
    }
  }

  return false;
}

Result<bool> ClusterLoadBalancer::GetLeaderToMove(
    const vector<TabletServerId>& sorted_leader_load,
    TabletId* moving_tablet_id,
    TabletServerId* from_ts,
    TabletServerId* to_ts,
    std::string* to_ts_path) {
  if (sorted_leader_load.empty()) {
    return false;
  }

  // Adjust the configured threshold if it is too low for the given configuration.
  size_t adjusted_leader_threshold = implicit_cast<size_t>(
      state_->AdjustLeaderBalanceThreshold(static_cast<int>(sorted_leader_load.size())));

  // Find out if there are leaders to be moved.
  for (auto right = sorted_leader_load.size(); right > 0;) {
    --right;
    const TabletServerId& high_load_uuid = sorted_leader_load[right];
    auto high_leader_blacklisted =
        (global_state_->leader_blacklisted_servers_.find(high_load_uuid) !=
         global_state_->leader_blacklisted_servers_.end());
    auto high_load = state_->GetLeaderLoad(high_load_uuid);
    if (high_leader_blacklisted) {
      if (high_load > 0) {
        // Leader blacklisted tserver with a leader replica.
        break;
      } else {
        // Leader blacklisted tserver without leader replica.
        VLOG(3) << "Tablet server " << high_load_uuid << " is blacklisted but has 0"
                << " leader load for this table, continue to the next ts";
        continue;
      }
    } else {
      if (adjusted_leader_threshold > 0 && high_load <= adjusted_leader_threshold) {
        // Non-leader blacklisted tserver with not too many leader replicas.
        // TODO(Sanket): Even though per table load is below the configured threshold,
        // we might want to do global leader balancing above a certain threshold that is lower
        // than the per table threshold. Can add another gflag/knob here later.
        VLOG(3) << "Tablet server " << high_load_uuid << " is not blacklisted "
                << " and has load below threshold, not found any leader to move";
        return false;
      } else {
        // Non-leader blacklisted tserver with too many leader replicas.
        break;
      }
    }
  }

  // The algorithm to balance the leaders is very similar to the one for tablets:
  //
  // Start with two indices pointing at left and right most ends of the sorted_leader_load_
  // structure. Note that leader blacklisted tserver is considered as having infinite leader load.
  //
  // We will try to find two TSs that have at least one leader that can be moved amongst them, from
  // the higher load to the lower load TS. To do this, we will go through comparing the TSs
  // corresponding to our left and right indices. We go through leaders on the higher loaded TS
  // and find a running replica on the lower loaded TS to move the leader. If no leader can be
  // be picked, we advance our state.
  //
  // The state is defined as the positions of the start and end indices. We always try to move the
  // right index back, until we cannot any more, due to either reaching the left index (cannot
  // rebalance from one TS to itself), or the difference of load between the two TSs is too low to
  // try to rebalance (if load variance is 1, it does not make sense to move leaders between the
  // TSs). When we cannot lower the right index any further, we reset it back to last_pos and
  // increment the left index.
  //
  // We stop the whole algorithm if the left index reaches last_pos, or if we reset the right index
  // and are already breaking the invariance rule, as that means that any further differences in
  // the interval between left and right cannot have load > kMinLeaderLoadVarianceToBalance.
  VLOG(3) << "Determining a leader to move off from affinitized zone to another affinitized zone";
  const auto current_time = MonoTime::Now();
  ssize_t last_pos = sorted_leader_load.size() - 1;
  for (ssize_t left = 0; left <= last_pos; ++left) {
    const TabletServerId& low_load_uuid = sorted_leader_load[left];
    auto low_leader_blacklisted = (global_state_->leader_blacklisted_servers_.find(low_load_uuid)
        != global_state_->leader_blacklisted_servers_.end());
    if (low_leader_blacklisted) {
      // Left marker has gone beyond non-leader blacklisted tservers.
      return false;
    }

    for (auto right = last_pos; right >= 0; --right) {
      const TabletServerId& high_load_uuid = sorted_leader_load[right];
      auto high_leader_blacklisted =
          (global_state_->leader_blacklisted_servers_.find(high_load_uuid) !=
              global_state_->leader_blacklisted_servers_.end());
      ssize_t load_variance =
          state_->GetLeaderLoad(high_load_uuid) - state_->GetLeaderLoad(low_load_uuid);

      bool is_global_balancing_move = false;

      // Check for state change or end conditions.
      if (left == right || (load_variance < state_->options_->kMinLeaderLoadVarianceToBalance &&
            !high_leader_blacklisted)) {
        // Global leader balancing only if per table variance is > 0.
        // If both left and right are same (i.e. load_variance is 0) and right is last_pos
        // or right is last_pos and load_variance is 0 then we can return as we don't
        // have any other moves to make.
        if (load_variance == 0 && right == last_pos) {
          return false;
        }
        // Check if we can benefit from global leader balancing.
        // If we have > 0 load_variance and there are no per table moves left.
        if (load_variance > 0 && CanBalanceGlobalLoad()) {
          int global_load_variance = state_->global_state_->GetGlobalLeaderLoad(high_load_uuid) -
                                        state_->global_state_->GetGlobalLeaderLoad(low_load_uuid);
          // Already globally balanced. Since we are sorted by global load, we can return here as
          // there are no other moves for us to make.
          if (global_load_variance < state_->options_->kMinGlobalLeaderLoadVarianceToBalance) {
            return false;
          }
          VLOG(3) << "This is a global leader balancing pass";
          is_global_balancing_move = true;
        } else {
          break;
        }
      }

      // Find the leaders on the higher loaded TS that have running peers on the lower loaded TS.
      // If there are, we have a candidate we want, so fill in the output params and return.
      const set<TabletId>& leaders = state_->per_ts_meta_[high_load_uuid].leaders;
      for (const auto& tablet : GetLeadersOnTSToMove(global_state_->drive_aware_,
                                                     leaders,
                                                     state_->per_ts_meta_[low_load_uuid])) {
        *moving_tablet_id = tablet.first;
        *to_ts_path = tablet.second;
        *from_ts = high_load_uuid;
        *to_ts = low_load_uuid;
        VLOG(3) << "For leader load balancing found tablet " << tablet.first << " to move from "
                << *from_ts << " to " << *to_ts;
        const auto& per_tablet_meta = state_->per_tablet_meta_;
        const auto tablet_meta_iter = per_tablet_meta.find(tablet.first);
        if (PREDICT_TRUE(tablet_meta_iter != per_tablet_meta.end())) {
          const auto& tablet_meta = tablet_meta_iter->second;
          const auto& stepdown_failures = tablet_meta.leader_stepdown_failures;
          const auto stepdown_failure_iter = stepdown_failures.find(low_load_uuid);
          if (stepdown_failure_iter != stepdown_failures.end()) {
            const auto time_since_failure = current_time - stepdown_failure_iter->second;
            if (time_since_failure.ToMilliseconds() < FLAGS_min_leader_stepdown_retry_interval_ms) {
              LOG(INFO) << "Cannot move tablet " << tablet.first << " leader from TS "
                        << *from_ts << " to TS " << *to_ts << " yet: previous attempt with the same"
                        << " intended leader failed only " << ToString(time_since_failure)
                        << " ago (less " << "than " << FLAGS_min_leader_stepdown_retry_interval_ms
                        << "ms).";
            }
            continue;
          }
        } else {
          LOG(WARNING) << "Did not find load balancer metadata for tablet " << *moving_tablet_id;
        }

        // Leader movement solely due to leader blacklist.
        if (load_variance < state_->options_->kMinLeaderLoadVarianceToBalance &&
            high_leader_blacklisted) {
          LOG(INFO) << "Move tablet " << tablet.first << " leader from leader blacklisted TS "
            << *from_ts << " to TS " << *to_ts;
        }
        if (!is_global_balancing_move) {
          can_perform_global_operations_ = false;
        }
        return true;
      }
    }
  }

  // Should never get here.
  FATAL_ERROR("Load balancing algorithm reached invalid state!");
}

Result<bool> ClusterLoadBalancer::HandleRemoveReplicas(
    TabletId* out_tablet_id, TabletServerId* out_from_ts) {
  DCHECK(!state_->allow_only_leader_balancing_);

  // Give high priority to removing tablets that are not respecting the placement policy.
  if (VERIFY_RESULT(HandleRemoveIfWrongPlacement(out_tablet_id, out_from_ts))) {
    return true;
  }

  for (const auto& tablet_id : state_->tablets_over_replicated_) {
    VLOG(3) << "Tablet " << tablet_id << " is over-replicated, proceeding"
            << " to remove replicas";
    // Skip if there is a pending ADD_SERVER.
    if (VERIFY_RESULT(IsConfigMemberInTransitionMode(tablet_id)) ||
        state_->per_tablet_meta_[tablet_id].starting > 0) {
      VLOG(3) << "Tablet " << tablet_id << " has a pending ADD_SERVER so skipping remove for now";
      continue;
    }

    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    const auto& tablet_servers = tablet_meta.over_replicated_tablet_servers;
    auto comparator = PerTableLoadState::Comparator(state_);
    vector<TabletServerId> sorted_ts;
    // Don't include any tservers where this tablet is still starting.
    std::copy_if(
        tablet_servers.begin(), tablet_servers.end(), std::back_inserter(sorted_ts),
        [&](const TabletServerId& ts_uuid) {
          return !state_->per_ts_meta_[ts_uuid].starting_tablets.count(tablet_id);
        });
    if (sorted_ts.empty()) {
      return STATUS_SUBSTITUTE(IllegalState, "No tservers to remove from over-replicated "
                                             "tablet $0", tablet_id);
    }
    // Sort in reverse to first try to remove a replica from the highest loaded TS.
    sort(sorted_ts.rbegin(), sorted_ts.rend(), comparator);
    string remove_candidate = sorted_ts[0];
    *out_tablet_id = tablet_id;
    *out_from_ts = remove_candidate;
    // Do force leader stepdown, as we are either not the leader or we are allowed to step down.
    RETURN_NOT_OK(RemoveReplica(tablet_id, remove_candidate));
    return true;
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleRemoveIfWrongPlacement(
    TabletId* out_tablet_id, TabletServerId* out_from_ts) {
  for (const auto& tablet_id : state_->tablets_wrong_placement_) {
    VLOG(3) << "Tablet " << tablet_id << " has a wrong placement"
            << ", finding a suitable replica to remove";
    // Skip this tablet if it is not over-replicated.
    if (!state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }
    // Skip if there is a pending ADD_SERVER
    if (VERIFY_RESULT(IsConfigMemberInTransitionMode(tablet_id))) {
      VLOG(3) << "Tablet " << tablet_id << " has a pending ADD_SERVER"
              << " so skipping remove for now";
      continue;
    }
    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    TabletServerId target_uuid;
    // Prioritize blacklisted servers, if any.
    if (!tablet_meta.blacklisted_tablet_servers.empty()) {
      target_uuid = *tablet_meta.blacklisted_tablet_servers.begin();
      VLOG(3) << "TS " << target_uuid << " is blacklisted, removing it now";
    }
    // If no blacklisted server could be chosen, try the wrong placement ones.
    if (target_uuid.empty()) {
      if (!tablet_meta.wrong_placement_tablet_servers.empty()) {
        target_uuid = *tablet_meta.wrong_placement_tablet_servers.begin();
        VLOG(3) << "TS " << target_uuid << " is in wrong placement, removing it now";
      }
    }
    // If we found a tablet server, choose it.
    if (!target_uuid.empty()) {
      *out_tablet_id = tablet_id;
      *out_from_ts = std::move(target_uuid);
      VLOG(3) << "Wrongly placed replica " << *out_from_ts << " needs to be removed";
      // Force leader stepdown if we have wrong placements or blacklisted servers.
      RETURN_NOT_OK(RemoveReplica(tablet_id, *out_from_ts));
      return true;
    }
  }
  return false;
}

Result<bool> ClusterLoadBalancer::GetLeaderToMoveAcrossAffinitizedPriorities(
    TabletId* moving_tablet_id,
    TabletServerId* from_ts,
    TabletServerId* to_ts,
    std::string* to_ts_path) {
  // Similar to normal leader balancing, we double iterate from lowest priority most loaded and
  // higher priority least loaded nodes. For each pair, we check whether there is any tablet
  // intersection and if so, there is a match and we return true.
  //
  // If the current leader load is 0, we know that there is no match in this priority and move to
  // higher priorities.
  for (auto lower_priority = state_->sorted_leader_load_.size(); lower_priority > 1;) {
    lower_priority--;
    auto& leader_set = state_->sorted_leader_load_[lower_priority];
    for (size_t idx = leader_set.size(); idx > 0;) {
      idx--;
      const TabletServerId& from_uuid = leader_set[idx];
      if (state_->GetLeaderLoad(from_uuid) == 0) {
        bool is_blacklisted = global_state_->leader_blacklisted_servers_.find(from_uuid) !=
                              global_state_->leader_blacklisted_servers_.end();
        if (is_blacklisted) {
          // Blacklisted nodes are sorted to the end even if their load is 0.
          // There could still be non-blacklisted nodes with higher loads. So keep looking.
          continue;
        } else {
          // All subsequent non-blacklisted nodes in this priority have no leaders, no match found.
          break;
        }
      }

      const set<TabletId>& leaders = state_->per_ts_meta_[from_uuid].leaders;
      for (size_t higher_priority = 0; higher_priority < lower_priority; higher_priority++) {
        // higher_priority is always guaranteed not to contain blacklisted servers.
        for (const auto& to_uuid : state_->sorted_leader_load_[higher_priority]) {
          auto peers = GetLeadersOnTSToMove(
              global_state_->drive_aware_, leaders, state_->per_ts_meta_[to_uuid]);

          if (!peers.empty()) {
            auto peer = peers.begin();
            *moving_tablet_id = peer->first;
            *to_ts_path = peer->second;
            *from_ts = from_uuid;
            *to_ts = to_uuid;

            VLOG(3) << Format("Can move leader of tablet from TS $1 (priority $2) to TS $3 "
                "(priority $4)", *moving_tablet_id, from_uuid, lower_priority, to_uuid,
                higher_priority);
            return true;
          }
        }
      }
    }
  }

  return false;
}

Result<bool> ClusterLoadBalancer::HandleLeaderMoves(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  // If the user sets 'transaction_tables_use_preferred_zones' gflag to 0 and the tablet
  // being balanced is a transaction tablet, then logical flow will be changed to ignore
  // preferred zones and instead proceed to normal leader balancing.
  string out_ts_ts_path;
  if (state_->use_preferred_zones_ &&
      VERIFY_RESULT(GetLeaderToMoveAcrossAffinitizedPriorities(
          out_tablet_id, out_from_ts, out_to_ts, &out_ts_ts_path))) {
    RETURN_NOT_OK(MoveLeader(*out_tablet_id, *out_from_ts, *out_to_ts, out_ts_ts_path));
    return true;
  }

  if (VERIFY_RESULT(GetLeaderToMoveWithinAffinitizedPriorities(
          out_tablet_id, out_from_ts, out_to_ts, &out_ts_ts_path))) {
    RETURN_NOT_OK(MoveLeader(*out_tablet_id, *out_from_ts, *out_to_ts, out_ts_ts_path));
    return true;
  }
  return false;
}

Status ClusterLoadBalancer::MoveReplica(
    const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Moving replica $0 from $1 to $2", tablet_id, from_ts, to_ts);
  RETURN_NOT_OK(SendReplicaChanges(GetTabletMap().at(tablet_id), to_ts, true /* is_add */,
                                   true /* should_remove_leader */));
  RETURN_NOT_OK(state_->AddReplica(tablet_id, to_ts));
  return GetAtomicFlag(&FLAGS_load_balancer_count_move_as_add) ?
      Status::OK() : state_->RemoveReplica(tablet_id, from_ts);
}

Status ClusterLoadBalancer::AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Adding replica $0 to $1", tablet_id, to_ts);
  // This is an add operation, so the "should_remove_leader" flag is irrelevant.
  RETURN_NOT_OK(SendReplicaChanges(GetTabletMap().at(tablet_id), to_ts, true /* is_add */,
                                   true /* should_remove_leader */));
  return state_->AddReplica(tablet_id, to_ts);
}

Status ClusterLoadBalancer::RemoveReplica(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  LOG(INFO) << Substitute("Removing replica $0 from tablet $1", ts_uuid, tablet_id);
  RETURN_NOT_OK(SendReplicaChanges(GetTabletMap().at(tablet_id), ts_uuid, false /* is_add */,
                                   true /* should_remove_leader */));
  return state_->RemoveReplica(tablet_id, ts_uuid);
}

Status ClusterLoadBalancer::MoveLeader(const TabletId& tablet_id,
                                       const TabletServerId& from_ts,
                                       const TabletServerId& to_ts,
                                       const string& to_ts_path) {
  LOG(INFO) << Substitute("Moving leader of $0 from TS $1 to $2", tablet_id, from_ts, to_ts);
  RETURN_NOT_OK(SendReplicaChanges(GetTabletMap().at(tablet_id), from_ts, false /* is_add */,
                                   false /* should_remove_leader */, to_ts));

  return state_->MoveLeader(tablet_id, from_ts, to_ts, to_ts_path);
}

void ClusterLoadBalancer::GetAllAffinitizedZones(
  const ReplicationInfoPB& replication_info, vector<AffinitizedZonesSet>* affinitized_zones) const {
  CatalogManagerUtil::GetAllAffinitizedZones(replication_info, affinitized_zones);
  if (VLOG_IS_ON(2)) {
    std::stringstream out;
    out << "affinitized_zones for table " << state_->table_id_ << ": [";
    for (size_t i = 0; i < affinitized_zones->size(); ++i) {
      out << "priority " << i << ": [";
      for (const auto& zone : (*affinitized_zones)[i]) {
        out << zone.ShortDebugString() << ", ";
      }
      out << "], ";
    }
    out << "]";
    VLOG(2) << out.str();
  }
}

void ClusterLoadBalancer::AddTSIfBlacklisted(
    const std::shared_ptr<TSDescriptor>& ts_desc, const BlacklistPB& blacklist,
    const bool leader_blacklist) {
  for (const auto& blacklist_hp : blacklist.hosts()) {
    if (ts_desc->IsRunningOn(blacklist_hp)) {
      if (leader_blacklist) {
        VLOG(1) << "Adding leader blacklisted TS " << ts_desc->permanent_uuid()
                << " to leader blacklist";
        global_state_->leader_blacklisted_servers_.insert(ts_desc->permanent_uuid());
      } else {
        VLOG(1) << "Adding blacklisted TS " << ts_desc->permanent_uuid() << " to server blacklist";
        global_state_->blacklisted_servers_.insert(ts_desc->permanent_uuid());
      }
      return;
    }
  }
  if (!leader_blacklist && ts_desc->has_faulty_drive()) {
    VLOG(1) << "Adding TS " << ts_desc->permanent_uuid()
            << " to server blacklist because of faulty drive";
    global_state_->blacklisted_servers_.insert(ts_desc->permanent_uuid());
  }
}

void ClusterLoadBalancer::SetBlacklistAndPendingDeleteTS() {
  // Set the blacklist and leader blacklist so
  // we can also mark the tablet servers as we add them up.
  auto l = catalog_manager_->ClusterConfig()->LockForRead();
  for (const auto& ts_desc : global_state_->ts_descs_) {
    VLOG(1) << "Processing TS for blacklist: " << ts_desc->ToString();
    AddTSIfBlacklisted(ts_desc, l->pb.server_blacklist(), false /* leader_blacklist */);
    AddTSIfBlacklisted(ts_desc, l->pb.leader_blacklist(), true /* leader_blacklist */);
    if (ts_desc->HasTabletDeletePending()) {
      VLOG(1) << "TS " << ts_desc->permanent_uuid() << " has a delete pending, "
              << "adding to global state of servers with pending deletes";
      global_state_->servers_with_pending_deletes_.insert(ts_desc->permanent_uuid());
    }
  }
}

void ClusterLoadBalancer::InitializeTSDescriptors() {
  // Loop over tablet servers to set empty defaults, so we can also have info on those
  // servers that have yet to receive load (have heartbeated to the master, but have not been
  // assigned any tablets yet).
  for (const auto& ts_desc : global_state_->ts_descs_) {
    state_->UpdateTabletServer(ts_desc);
  }
}

// CatalogManager indirection methods that are set as virtual to be bypassed in testing.
//
void ClusterLoadBalancer::GetAllReportedDescriptors(TSDescriptorVector* ts_descs) const {
  catalog_manager_->master_->ts_manager()->GetAllReportedDescriptors(ts_descs);
}

void ClusterLoadBalancer::GetAllDescriptors(TSDescriptorVector* ts_descs) const {
  catalog_manager_->master_->ts_manager()->GetAllDescriptors(ts_descs);
}

const TabletInfoMap& ClusterLoadBalancer::GetTabletMap() const {
  return *catalog_manager_->tablet_map_;
}

const scoped_refptr<TableInfo> ClusterLoadBalancer::GetTableInfo(const TableId& table_uuid) const {
  return catalog_manager_->GetTableInfoUnlocked(table_uuid);
}

Result<TabletInfos> ClusterLoadBalancer::GetTabletsForTable(const TableId& table_uuid) const {
  auto table_info = GetTableInfo(table_uuid);

  if (table_info == nullptr) {
    return STATUS_FORMAT(
        InvalidArgument, "Invalid UUID '$0' - no entry found in catalog manager table map",
        table_uuid);
  }

  return table_info->GetTablets(IncludeInactive::kTrue);
}

TableIndex::TablesRange ClusterLoadBalancer::GetTables() const {
  return catalog_manager_->tables_->GetPrimaryTables();
}

bool ClusterLoadBalancer::SkipLoadBalancing(const TableInfo& table) const {
  // Skip load-balancing of some tables:
  // * system tables: they are virtual tables not hosted by tservers.
  // * colocated user tables: they occupy the same tablet as their colocated parent table, so load
  //   balancing just the colocated parent table is sufficient.
  // * deleted/deleting tables: as they are no longer in effect. For tables that are being deleted
  // currently as well, load distribution wouldn't matter as eventually they would get deleted.
  auto l = table.LockForRead();
  if (catalog_manager_->IsSystemTable(table)) {
    VLOG(3) << "Skipping system table " << table.id() << " for load balancing";
    return true;
  }
  if (table.IsColocatedUserTable()) {
    VLOG(2) << "Skipping colocated user table " << table.id() << " for load balancing";
    return true;
  }
  if (l->started_deleting()) {
    VLOG(2) << "Skipping deleting / deleted table " << table.id() << " for load balancing";
    return true;
  }
  return false;
}

Status ClusterLoadBalancer::CountPendingTasksUnlocked(const TableId& table_uuid,
                                            int* pending_add_replica_tasks,
                                            int* pending_remove_replica_tasks,
                                            int* pending_stepdown_leader_tasks) {
  GetPendingTasks(table_uuid,
                  &state_->pending_add_replica_tasks_[table_uuid],
                  &state_->pending_remove_replica_tasks_[table_uuid],
                  &state_->pending_stepdown_leader_tasks_[table_uuid]);

  *pending_add_replica_tasks += state_->pending_add_replica_tasks_[table_uuid].size();
  *pending_remove_replica_tasks += state_->pending_remove_replica_tasks_[table_uuid].size();
  *pending_stepdown_leader_tasks += state_->pending_stepdown_leader_tasks_[table_uuid].size();
  for (auto e : state_->pending_add_replica_tasks_[table_uuid]) {
    const auto& ts_uuid = e.second;
    const auto& tablet_id = e.first;
    RETURN_NOT_OK(state_->AddStartingTablet(tablet_id, ts_uuid));
  }
  return Status::OK();
}

void ClusterLoadBalancer::GetPendingTasks(const TableId& table_uuid,
                                          TabletToTabletServerMap* add_replica_tasks,
                                          TabletToTabletServerMap* remove_replica_tasks,
                                          TabletToTabletServerMap* stepdown_leader_tasks) {
  catalog_manager_->GetPendingServerTasksUnlocked(
      table_uuid, add_replica_tasks, remove_replica_tasks, stepdown_leader_tasks);
}

Status ClusterLoadBalancer::SendReplicaChanges(
    scoped_refptr<TabletInfo> tablet, const TabletServerId& ts_uuid, const bool is_add,
    const bool should_remove_leader, const TabletServerId& new_leader_ts_uuid) {
  auto l = tablet->LockForRead();
  if (is_add) {
    // These checks are temporary. They will be removed once we are confident that the algorithm is
    // always doing the right thing.
    SCHECK_EQ(state_->pending_add_replica_tasks_[tablet->table()->id()].count(tablet->tablet_id()),
             0U,
             IllegalState,
             "Sending duplicate add replica task.");
    catalog_manager_->SendAddServerRequest(
        tablet, GetDefaultMemberType(), l->pb.committed_consensus_state(), ts_uuid, epoch_);
  } else {
    // If the replica is also the leader, first step it down and then remove.
    if (state_->per_tablet_meta_[tablet->id()].leader_uuid == ts_uuid) {
      SCHECK_EQ(
          state_->pending_stepdown_leader_tasks_[tablet->table()->id()].count(tablet->tablet_id()),
          0U,
          IllegalState,
          "Sending duplicate leader stepdown task.");
      catalog_manager_->SendLeaderStepDownRequest(
          tablet, l->pb.committed_consensus_state(), ts_uuid, should_remove_leader, epoch_,
          new_leader_ts_uuid);
    } else {
      SCHECK_EQ(
          state_->pending_remove_replica_tasks_[tablet->table()->id()].count(tablet->tablet_id()),
          0U,
          IllegalState,
          "Sending duplicate remove replica task.");
      catalog_manager_->SendRemoveServerRequest(
          tablet, l->pb.committed_consensus_state(), ts_uuid, epoch_);
    }
  }
  return Status::OK();
}

consensus::PeerMemberType ClusterLoadBalancer::GetDefaultMemberType() {
  if (state_->options_->type == LIVE) {
    return consensus::PeerMemberType::PRE_VOTER;
  } else {
    return consensus::PeerMemberType::PRE_OBSERVER;
  }
}

Result<bool> ClusterLoadBalancer::IsConfigMemberInTransitionMode(const TabletId &tablet_id) const {
  auto tablet = GetTabletMap().at(tablet_id);
  auto l = tablet->LockForRead();
  auto config = l->pb.committed_consensus_state().config();
  return CountVotersInTransition(config) != 0;
}

const PlacementInfoPB& ClusterLoadBalancer::GetReadOnlyPlacementFromUuid(
    const ReplicationInfoPB& replication_info) const {
  // We assume we have an read replicas field in our replication info.
  for (int i = 0; i < replication_info.read_replicas_size(); i++) {
    const PlacementInfoPB& read_only_placement = replication_info.read_replicas(i);
    if (read_only_placement.placement_uuid() == state_->options_->placement_uuid) {
      VLOG(1) << "Found read only placement uuid " << read_only_placement.placement_uuid();
      return read_only_placement;
    }
  }
  // Should never get here.
  LOG(ERROR) << "Could not find read only cluster with placement uuid: "
             << state_->options_->placement_uuid;
  return replication_info.read_replicas(0);
}

const PlacementInfoPB& ClusterLoadBalancer::GetLiveClusterPlacementInfo() const {
  auto l = catalog_manager_->ClusterConfig()->LockForRead();
  return l->pb.replication_info().live_replicas();
}

vector<scoped_refptr<TableInfo>> ClusterLoadBalancer::GetAllTablesLoadBalancerSkipped() {
  SharedLock<decltype(mutex_)> l(mutex_);
  return skipped_tables_;
}

}  // namespace master
}  // namespace yb
