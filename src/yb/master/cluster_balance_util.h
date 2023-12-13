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

#pragma once

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/gutil/casts.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/ts_descriptor.h"

DECLARE_int32(leader_balance_threshold);

DECLARE_int32(load_balancer_max_concurrent_tablet_remote_bootstraps);

DECLARE_int32(load_balancer_max_concurrent_tablet_remote_bootstraps_per_table);

DECLARE_int32(load_balancer_max_over_replicated_tablets);

DECLARE_int32(load_balancer_max_concurrent_adds);

DECLARE_int32(load_balancer_max_concurrent_removals);

DECLARE_int32(load_balancer_max_concurrent_moves);

DECLARE_int32(load_balancer_max_concurrent_moves_per_table);

namespace yb {
namespace master {

// enum for replica type, either live (synchronous) or read only (timeline consistent)
enum ReplicaType {
  LIVE,
  READ_ONLY,
};

struct CBTabletMetadata {
  bool is_missing_replicas() { return is_under_replicated || !under_replicated_placements.empty(); }

  bool has_wrong_placements() {
    return !wrong_placement_tablet_servers.empty() || !blacklisted_tablet_servers.empty();
  }

  bool has_blacklisted_leader() {
    return !leader_blacklisted_tablet_servers.empty();
  }

  // Can the TS be added to any of the placements that lack a replica for this tablet.
  bool CanAddTSToMissingPlacements(const std::shared_ptr<TSDescriptor> ts_descriptor) const;

  // Number of running replicas for this tablet.
  int running = 0;

  // TODO(bogdan): actually use this!
  //
  // Number of starting replicas for this tablet.
  int starting = 0;

  // If this tablet has fewer replicas than the configured number in the PlacementInfoPB.
  bool is_under_replicated = false;

  // Set of placement ids that have less replicas available than the configured minimums.
  std::unordered_set<CloudInfoPB, cloud_hash, cloud_equal_to> under_replicated_placements;

  // If this tablet has more replicas than the configured number in the PlacementInfoPB.
  bool is_over_replicated;

  // Set of tablet server ids that can be candidates for removal, due to tablet being
  // over-replicated. For tablets with placement information, this will be all tablet servers
  // that are housing replicas of this tablet, in a placement with strictly more replicas than the
  // configured minimum (as that means there is at least one of them we can remove, and still
  // respect the minimum).
  //
  // For tablets with no placement information, this will be all the tablet servers currently
  // serving this tablet, as we can downsize with no restrictions in this case.
  std::set<TabletServerId> over_replicated_tablet_servers;

  // Set of tablet server ids whose placement information does not match that listed in the
  // table's PlacementInfoPB. This will happen when we change the configuration for the table or
  // the cluster.
  std::set<TabletServerId> wrong_placement_tablet_servers;

  // Set of tablet server ids that have been blacklisted and as such, should not get any more load
  // assigned to them and should be prioritized for removing load.
  std::set<TabletServerId> blacklisted_tablet_servers;
  std::set<TabletServerId> leader_blacklisted_tablet_servers;

  // The tablet server id of the leader in this tablet's peer group.
  TabletServerId leader_uuid;

  // Leader stepdown failures. We use this to prevent retrying the same leader stepdown too soon.
  LeaderStepDownFailureTimes leader_stepdown_failures;

  std::string ToString() const;
};

using PathToTablets = std::unordered_map<std::string, std::set<TabletId>>;

struct CBTabletServerMetadata {
  std::string ToString() const {
    std::stringstream out;
    out << Format("Descriptor: $0, ", descriptor->ToString());
    out << Format("running_tablets: $0, ", running_tablets);
    out << Format("starting_tablets: $0, ", starting_tablets);
    out << Format("leaders: $0, ", leaders);
    out << Format("disabled_by_ts_tablets: $0, ", disabled_by_ts_tablets);
    out << Format("sorted_path_load: $0, ", sorted_path_load_by_tablets_count);
    out << Format("path_to_tablets: $0, ", path_to_tablets);
    return out.str();
  }
  // The TSDescriptor for this tablet server.
  std::shared_ptr<TSDescriptor> descriptor = nullptr;

  // Map from path to the set of tablet ids that this tablet server is currently running
  // on the path.
  PathToTablets path_to_tablets;

  // Map from path to the number of replicas that this tablet server is currently starting
  // on the path.
  std::unordered_map<std::string, int> path_to_starting_tablets_count;

  // Set of paths sorted descending by tablets count.
  std::vector<std::string> sorted_path_load_by_tablets_count;

  // Set of paths sorted ascending by tablet leaders count.
  std::vector<std::string> sorted_path_load_by_leader_count;

  // The set of tablet ids that this tablet server is currently running.
  std::set<TabletId> running_tablets;

  // The set of tablet ids that this tablet server is currently starting.
  std::set<TabletId> starting_tablets;

  // The set of tablet leader ids that this tablet server is currently running.
  std::set<TabletId> leaders;

  // Map from path to the set of tablet leader ids that this tablet server is currently running
  // on the path.
  PathToTablets path_to_leaders;

  // The set of tablet ids that this tablet server disabled (ex. after split).
  std::set<TabletId> disabled_by_ts_tablets;
};

struct CBTabletServerLoadCounts {
  std::string ToString() {
    return Format("{ Running tablets count: $0, starting tablets count: $1, leaders count: $2 }",
                  running_tablets_count, starting_tablets_count, leaders_count);
  }
  // Stores global load counts for a tablet server.
  // See definitions of these counts in CBTabletServerMetadata.
  int running_tablets_count = 0;
  int starting_tablets_count = 0;
  int leaders_count = 0;
};

struct Options {
  Options() {}
  virtual ~Options() {}

  std::string ToString() {
    std::string out =
        Format("{ MinLoadVarianceToBalance: $0, MinGlobalLoadVarianceToBalance: $1, "
                  "MinLeaderLoadVarianceToBalance: $2, MinGlobalLeaderLoadVarianceToBalance: $3, "
                  "AllowLimitStartingTablets: $4, MaxTabletRemoteBootstraps: $5, "
                  "MaxTabletRemoteBootstrapsPerTable: $6, AllowLimitOverReplicatedTablets: $7, "
                  "MaxOverReplicatedTablets: $8, MaxConcurrentRemovals: $9, ",
                  kMinLoadVarianceToBalance, kMinGlobalLoadVarianceToBalance,
                  kMinLeaderLoadVarianceToBalance, kMinGlobalLeaderLoadVarianceToBalance,
                  kAllowLimitStartingTablets, kMaxTabletRemoteBootstraps,
                  kMaxTabletRemoteBootstrapsPerTable, kAllowLimitOverReplicatedTablets,
                  kMaxOverReplicatedTablets, kMaxConcurrentRemovals);

    out += Format("MaxConcurrentAdds: $0, MaxConcurrentLeaderMoves: $1, "
                  "MaxConcurrentLeaderMovesPerTable: $2, ReplicaType: $3, "
                  "LivePlacementUUID: $4, Read Replica Placement UUID: $5}",
                  kMaxConcurrentAdds, kMaxConcurrentLeaderMoves, kMaxConcurrentLeaderMovesPerTable,
                  type, live_placement_uuid, placement_uuid);
    return out;
  }

  // If variance between load on TS goes past this number, we should try to balance.
  double kMinLoadVarianceToBalance = 2.0;

  // If variance between global load on TS goes past this number, we should try to balance.
  double kMinGlobalLoadVarianceToBalance = 2.0;

  // If variance between leader load on TS goes past this number, we should try to balance.
  double kMinLeaderLoadVarianceToBalance = 2.0;

  // If variance between global leader load on TS goes past this number, we should try to balance.
  double kMinGlobalLeaderLoadVarianceToBalance = 2.0;

  // Whether to limit the number of tablets being spun up on the cluster at any given time.
  bool kAllowLimitStartingTablets = true;

  // Max number of tablets being remote bootstrapped across the cluster, if we enable limiting
  // this.
  int kMaxTabletRemoteBootstraps = FLAGS_load_balancer_max_concurrent_tablet_remote_bootstraps;

  // Max number of tablets being remote bootstrapped for a specific table, if we enable limiting
  // this.
  int kMaxTabletRemoteBootstrapsPerTable =
      FLAGS_load_balancer_max_concurrent_tablet_remote_bootstraps_per_table;

  // Whether to limit the number of tablets that have more peers than configured at any given
  // time.
  bool kAllowLimitOverReplicatedTablets = true;

  // Max number of running tablet replicas that are over the configured limit.
  int kMaxOverReplicatedTablets = FLAGS_load_balancer_max_over_replicated_tablets;

  // Max number of over-replicated tablet peer removals to do in any one run of the load balancer.
  int kMaxConcurrentRemovals = FLAGS_load_balancer_max_concurrent_removals;

  // Max number of tablet peer replicas to add in any one run of the load balancer.
  int kMaxConcurrentAdds = FLAGS_load_balancer_max_concurrent_adds;

  // Max number of tablet leaders on tablet servers (across the cluster) to move in any one run of
  // the load balancer.
  int kMaxConcurrentLeaderMoves = FLAGS_load_balancer_max_concurrent_moves;

  // Max number of tablet leaders per table to move in any one run of the load balancer.
  int kMaxConcurrentLeaderMovesPerTable = FLAGS_load_balancer_max_concurrent_moves_per_table;

  // Either a live replica or a read.
  ReplicaType type;

  std::string placement_uuid;
  std::string live_placement_uuid;

  // TODO(bogdan): add state for leaders starting remote bootstraps, to limit on that end too.
};

// Cluster-wide state and metrics.
// For now it's used to determine how many tablets are being remote bootstrapped across the cluster,
// as well as keeping track of global load counts in order to do global load balancing moves.
class GlobalLoadState {
 public:
  // Get the global load for a certain TS.
  int GetGlobalLoad(const TabletServerId& ts_uuid) const;

  // Get global leader load for a certain TS.
  int GetGlobalLeaderLoad(const TabletServerId& ts_uuid) const;

  std::string ToString() {
    std::string out = "{ drive_aware: " + std::to_string(drive_aware_) + ", ts_info: {[";
    for (const auto& ts_info : ts_descs_) {
      out += " ts_descriptor_info: " + ts_info->ToString();
      if (per_ts_global_meta_.find(ts_info->permanent_uuid()) != per_ts_global_meta_.end()) {
        out += ", global_load_count: " + per_ts_global_meta_[ts_info->permanent_uuid()].ToString();
      }
    }
    out += "]}, total_starting_tablets: " + std::to_string(total_starting_tablets_) + " }";
    return out;
  }

  // Used to determine how many tablets are being remote bootstrapped across the cluster.
  int total_starting_tablets_ = 0;

  TSDescriptorVector ts_descs_;

  bool drive_aware_ = true;

  // The list of tablet server ids that match the blacklist.
  std::set<TabletServerId> blacklisted_servers_;
  std::set<TabletServerId> leader_blacklisted_servers_;

  // List of tablet server ids that have pending deletes.
  std::set<TabletServerId> servers_with_pending_deletes_;

 private:
  // Map from tablet server ids to the global metadata we store for each.
  std::unordered_map<TabletServerId, CBTabletServerLoadCounts> per_ts_global_meta_;

  friend class PerTableLoadState;
};

class PerTableLoadState {
 public:
  TableId table_id_;
  explicit PerTableLoadState(GlobalLoadState* global_state);

  virtual ~PerTableLoadState();

  // Comparators used for sorting by load.
  bool CompareByUuid(const TabletServerId& a, const TabletServerId& b);

  bool CompareByReplica(const TabletReplica& a, const TabletReplica& b);

  // Comparator functor to be able to wrap around the public but non-static compare methods that
  // end up using internal state of the class.
  struct Comparator {
    explicit Comparator(PerTableLoadState* state) : state_(state) {}
    bool operator()(const TabletServerId& a, const TabletServerId& b) {
      return state_->CompareByUuid(a, b);
    }

    bool operator()(const TabletReplica& a, const TabletReplica& b) {
      return state_->CompareByReplica(a, b);
    }

    PerTableLoadState* state_;
  };

  // Comparator to sort tablet servers' leader load.
  struct LeaderLoadComparator {
    explicit LeaderLoadComparator(PerTableLoadState* state, GlobalLoadState* global_state)
      : state_(state), global_state_(global_state) {}
    bool operator()(const TabletServerId& a, const TabletServerId& b);

    PerTableLoadState* state_;
    GlobalLoadState* global_state_;
  };

  // Get the load for a certain TS.
  size_t GetLoad(const TabletServerId& ts_uuid) const;

  // Get the load for a certain TS.
  size_t GetLeaderLoad(const TabletServerId& ts_uuid) const;

  bool IsTsInLivePlacement(TSDescriptor* ts_desc) {
    return ts_desc->placement_uuid() == options_->live_placement_uuid;
  }

  // Update the per-tablet information for this tablet.
  Status UpdateTablet(TabletInfo* tablet);

  virtual void UpdateTabletServer(std::shared_ptr<TSDescriptor> ts_desc);

  Result<bool> CanAddTabletToTabletServer(
    const TabletId& tablet_id, const TabletServerId& to_ts, const PlacementInfoPB* placement_info);

  // For a TS specified by ts_uuid, this function checks if there is a placement
  // block in placement_info where this TS can be placed. If there doesn't exist
  // any, it returns boost::none. On the other hand if there is a placement block
  // that satisfies the criteria then it returns the cloud info of that block.
  // If there wasn't any placement information passed in placement_info then
  // it returns the cloud info of the TS itself.
  boost::optional<CloudInfoPB> GetValidPlacement(const TabletServerId& ts_uuid,
                                                 const PlacementInfoPB* placement_info);

  Result<bool> CanSelectWrongPlacementReplicaToMove(
    const TabletId& tablet_id, const PlacementInfoPB& placement_info, TabletServerId* out_from_ts,
    TabletServerId* out_to_ts);

  Status AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts);

  Status RemoveReplica(const TabletId& tablet_id, const TabletServerId& from_ts);

  void SortLoad();

  void SortDriveLoad();

  Status MoveLeader(const TabletId& tablet_id,
                    const TabletServerId& from_ts,
                    const TabletServerId& to_ts = "",
                    const TabletServerId& to_ts_path = "");

  void SortLeaderLoad();

  void SortDriveLeaderLoad();

  int AdjustLeaderBalanceThreshold(int zone_set_size);

  Status AddRunningTablet(const TabletId& tablet_id,
                          const TabletServerId& ts_uuid,
                          const std::string& path);

  Status RemoveRunningTablet(const TabletId& tablet_id, const TabletServerId& ts_uuid);

  Status AddStartingTablet(const TabletId& tablet_id, const TabletServerId& ts_uuid);

  // Note: this does not call SortLeaderLoad.
  Status AddLeaderTablet(const TabletId& tablet_id,
                         const TabletServerId& ts_uuid,
                         const TabletServerId& ts_path);

  // Note: this does not call SortLeaderLoad.
  Status RemoveLeaderTablet(const TabletId& tablet_id, const TabletServerId& ts_uuid);

  Status AddDisabledByTSTablet(const TabletId& tablet_id, const TabletServerId& ts_uuid);

  std::string ToString() const {
    std::stringstream out;
    out << "{ ";
    if (VLOG_IS_ON(3)) {
      out << "per_tablet_meta: [";
      for (const auto& tablet_meta : per_tablet_meta_) {
        out << " " + tablet_meta.first + ": " + tablet_meta.second.ToString();
      }
      out << "], ";
    }
    out << "per_ts_meta: [";
    for (const auto& ts_meta : per_ts_meta_) {
      out << " " + ts_meta.first + ": " + ts_meta.second.ToString();
    }
    out << " ], placement_by_table: [";
    for (const auto& table_placement : placement_by_table_) {
      out << " " + table_placement.first + ": " + table_placement.second.ShortDebugString();
    }
    out << " ], ";

    out << Format("total_running: $0, ", total_running_);
    out << Format("total_starting: $0, ", total_starting_);
    out << Format("sorted_load: $0, ", sorted_load_);
    out << Format("tablets_missing_replicas: $0, ", tablets_missing_replicas_);
    out << Format("tablets_over_replicated: $0, ", tablets_over_replicated_);
    out << Format("tablets_wrong_placement: $0, ", tablets_wrong_placement_);
    out << Format("tablets_added: $0, ", tablets_wrong_placement_);
    out << Format("leader_balance_threshold: $0, ", leader_balance_threshold_);
    out << Format("sorted_leader_load: $0, ", sorted_leader_load_);
    out << Format("use_preferred_zones: $0, ", use_preferred_zones_);
    out << Format("check_ts_liveness_: $0, ", check_ts_liveness_);
    out << Format("allow_only_leader_balancing: $0, ", allow_only_leader_balancing_);

    out << "affinitized_zones: [";
    for (size_t i = 0; i < affinitized_zones_.size(); ++i) {
      out << "priority " << i << ": [";
      for (const auto& zone : affinitized_zones_[i]) {
        out << zone.ShortDebugString() << ", ";
      }
      out << "], ";
    }
    out << " ] }";
    return out.str();
  }

  // PerTableLoadState member fields

  // Map from tablet ids to the metadata we store for each.
  std::unordered_map<TabletId, CBTabletMetadata> per_tablet_meta_;

  // Map from tablet server ids to the metadata we store for each.
  std::unordered_map<TabletServerId, CBTabletServerMetadata> per_ts_meta_;

  // Map from table id to placement information for this table. This will be used for both
  // determining over-replication, by checking num_replicas, but also for az awareness, by keeping
  // track of the placement block policies between cluster and table level.
  std::unordered_map<TableId, PlacementInfoPB> placement_by_table_;

  // Total number of running tablets in the clusters (including replicas).
  int total_running_ = 0;

  // Total number of tablet replicas being started across the cluster.
  int total_starting_ = 0;

  // Set of ts_uuid sorted ascending by load. This is the actual raw data of TS load.
  std::vector<TabletServerId> sorted_load_;

  // Set of tablet ids that have been determined to have missing replicas. This can mean they are
  // generically under-replicated (2 replicas active, but 3 configured), or missing replicas in
  // certain placements (3 replicas active out of 3 configured, but no replicas in one of the AZs
  // listed in the placement blocks).
  std::set<TabletId> tablets_missing_replicas_;

  // Set of tablet ids that have been temporarily over-replicated. This is used to pick tablets
  // to potentially bring back down to their proper configured size, if there are more running than
  // expected.
  std::set<TabletId> tablets_over_replicated_;

  // Set of tablet ids that have been determined to have replicas in incorrect placements.
  std::set<TabletId> tablets_wrong_placement_;

  // List of tablet ids that have been added to a new tablet server.
  std::set<TabletId> tablets_added_;

  // Number of leaders per each tablet server to balance below.
  const int leader_balance_threshold_ = 0;

  // Table server ids that are eligible for leader placement.
  // The outer list is sorted by descending priority (value 1 is highest priority).
  // The inner list servers are sorted by ascending leader load.
  // Blacklisted servers are considered to have least priority and maximum load.
  // Ex: Say we have the following servers:
  // A: 3 leaders, priority 1
  // B: 2 leaders, priority 1
  // C: 5 leaders, priority 2
  // D: 4 leaders, No priority
  // E: 3 leaders, No priority
  // F: 1 leaders, No priority, leader blacklist
  // We will populate in the following manner:
  // [[B,A] [C],[E,D,F]]
  // And if B was also leader blacklisted:
  // [[A] [C],[E,D,B,F]]
  // If affinitized leaders is not enabled, all servers are treated as priority 1.
  std::vector<std::vector<TabletServerId>> sorted_leader_load_;

  std::unordered_map<TableId, TabletToTabletServerMap> pending_add_replica_tasks_;
  std::unordered_map<TableId, TabletToTabletServerMap> pending_remove_replica_tasks_;
  std::unordered_map<TableId, TabletToTabletServerMap> pending_stepdown_leader_tasks_;

  // Time at which we started the current round of load balancing.
  MonoTime current_time_;

  // The knobs we use for tweaking the flow of the algorithm.
  Options* options_;

  // Pointer to the cluster global state so that it can be updated when operations like add or
  // remove are executed.
  GlobalLoadState* global_state_;

  // Boolean whether tablets for this table should respect the affinitized zones.
  bool use_preferred_zones_ = true;

  // check_ts_liveness_ is used to indicate if the TS descriptors
  // need to be checked if they are live and considered for Load balancing.
  // In most scenarios, this would be true, except when we use the cluster_balance_mocked.h
  // for triggering LB scenarios.
  bool check_ts_liveness_ = true;
  // Allow only leader balancing for this table.
  bool allow_only_leader_balancing_ = false;

  // List of availability zones for affinitized leaders.
  std::vector<AffinitizedZonesSet> affinitized_zones_;

 private:
  bool ShouldSkipReplica(const TabletReplica& replica);
  size_t GetReplicaSize(std::shared_ptr<const TabletReplicaMap> replica_map);
  const std::string uninitialized_ts_meta_format_msg =
      "Found uninitialized ts_meta: ts_uuid: $0, table_uuid: $1";

  DISALLOW_COPY_AND_ASSIGN(PerTableLoadState);
}; // PerTableLoadState

} // namespace master
} // namespace yb
