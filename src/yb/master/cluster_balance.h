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

#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/circular_buffer.hpp>

#include "yb/master/catalog_manager.h"
#include "yb/master/master_fwd.h"
#include "yb/master/cluster_balance_util.h"
#include "yb/master/cluster_balance_activity_info.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ysql_tablespace_manager.h"

#include "yb/util/random.h"

DECLARE_int32(load_balancer_max_concurrent_tablet_remote_bootstraps);
DECLARE_int32(load_balancer_max_over_replicated_tablets);
DECLARE_int32(load_balancer_max_concurrent_adds);
DECLARE_int32(load_balancer_max_concurrent_moves);
DECLARE_int32(load_balancer_max_concurrent_removals);

YB_STRONGLY_TYPED_BOOL(StepdownIfLeader);

namespace yb {
namespace master {

//  This class keeps state with regards to the full cluster load of tablets on tablet servers. We
//  count a tablet towards a tablet server's load if it is either RUNNING, or is in the process of
//  starting up, hence NOT_STARTED or BOOTSTRAPPING.
//
//  This class also keeps state for the process of balancing load, which is done by temporarily
//  enlarging the replica set for a tablet by adding a new peer on a less loaded TS, and
//  subsequently removing a peer that is more loaded.
//
//  The policy for balancing the relicas involves a two step process:
//  1) Add replicas to tablet peer groups, if required, potentially leading to temporary
//  over-replication.
//  1.1) If any tablet has less replicas than the configured RF, or if there is any placement block
//  with fewer replicas than the specified minimum in that placement, we will try to add replicas,
//  so as to reach the client requirements.
//  1.2) If any tablet has replicas placed on tablet servers that do not conform to the specified
//  placement, then we should remove these. However, we never want to under-replicate a whole peer
//  group, or any individual placement block, so we will first add a new replica that will allow
//  the invalid ones to be removed.
//  1.3) If we have no placement related issues, then we just want try to equalize load
//  distribution across the cluster, while still maintaining the placement requirements.
//  2) Remove replicas from tablet peer groups if they are either over-replicated, or placed on
//  tablet servers they shouldn't be.
//  2.1) If we have replicas living on tablet servers where they should not, due to placement
//  constraints, or tablet servers being blacklisted, we try to remove those replicas with high
//  priority, but naturally, only if removing said replica does not lead to under-replication.
//  2.2) If we have no placement related issues, then we just try to shrink back any temporarily
//  over-replicated tablet peer groups, while still conforming to the placement requirements.
//
//  This class also balances the leaders on tablet servers, starting from the server with the most
//  leaders and moving some leaders to the servers with less to achieve an even distribution. If
//  a threshold is set in the configuration, the balancer will just keep the numbers of leaders
//  on each server below it instead of maintaining an even distribution.
class ClusterLoadBalancer {
 public:
  explicit ClusterLoadBalancer(CatalogManager* cm);
  virtual ~ClusterLoadBalancer();

  void InitMetrics();

  // Executes one run of the cluster balancing algorithm. This currently does not persist any state,
  // so it needs to scan the in-memory tablet and TS data in the CatalogManager on every run and
  // create a new PerTableLoadState object.
  void RunClusterBalancerWithOptions(
      Options* options, const std::vector<TableInfoPtr>& tables, const TabletInfoMap& tablet_map);

  // Runs the cluster balancer once for the live and all read only clusters, in order
  // of the cluster config.
  void RunClusterBalancer(
      const LeaderEpoch& epoch, const std::vector<TableInfoPtr>& tables,
      const TabletInfoMap& tablet_map);

  // Sets whether to enable or disable the cluster balancer, on demand.
  void SetLoadBalancerEnabled(bool is_enabled) { is_enabled_ = is_enabled; }

  bool IsLoadBalancerEnabled() const;

  bool CanBalanceGlobalLoad() const;

  void ReportMetrics();

  MonoTime LastRunTime() const;

  Status IsIdle() const;
  ClusterBalancerActivityInfo GetLatestActivityInfo() const;

  // Returns the TableInfo of all the tables for whom cluster balancing is being skipped.
  // As of today, this constitutes all the system tables, colocated user tables
  // and tables which have been marked as DELETING OR DELETED.
  // N.B. Currently this function is only used in test code. If using in production be mindful
  // that this function will not return colocated user tables as those are pre-filtered by the
  // table API the cluster balancer uses.
  std::vector<scoped_refptr<TableInfo>> GetAllTablesClusterBalancerSkipped();

  // Return the replication info for 'table'.
  virtual ReplicationInfoPB GetTableReplicationInfo(
      const scoped_refptr<const TableInfo>& table) const;

  //
  // Catalog manager indirection methods.
  //
 protected:
  //
  // Indirection methods to CatalogManager that we override in the subclasses (or testing).
  //

  void InitializeTSDescriptors();

  // Get the list of all TSDescriptors.
  virtual void GetAllDescriptors(TSDescriptorVector* ts_descs) const;

  virtual std::optional<std::reference_wrapper<const TabletInfoPtr>> GetTabletInfo(
      const TabletId& id) const;

  // Should skip cluster balancing of this table?
  bool SkipLoadBalancing(const TableInfo& table) const;

  // Increment the provided variables by the number of pending tasks that were found. Do not call
  // more than once for the same table because it also modifies the internal state.
  Status CountPendingTasks(
      const TableInfoPtr& table,
      int* pending_add_replica_tasks,
      int* pending_remove_replica_tasks,
      int* pending_stepdown_leader_tasks);

  // Wrapper around CatalogManager::GetPendingTasks so it can be mocked by TestLoadBalancer.
  virtual void GetPendingTasks(const TableInfoPtr& table,
                               TabletToTabletServerMap* add_replica_tasks,
                               TabletToTabletServerMap* remove_replica_tasks,
                               TabletToTabletServerMap* stepdown_leader_tasks);

  // Issue the calls to CatalogManager to change the config for this particular tablet.
  virtual Status SendAddReplica(
      const TabletInfoPtr& tablet, const TabletServerId& ts_uuid, const std::string& reason);
  virtual Status SendRemoveReplica(
      const TabletInfoPtr& tablet, const TabletServerId& ts_uuid, const std::string& reason);
  // If new_leader_ts_uuid is empty, a server will be picked by random to be the new leader.
  virtual Status SendMoveLeader(
      const TabletInfoPtr& tablet, const TabletServerId& ts_uuid,
      bool should_remove_leader, const std::string& reason,
      const TabletServerId& new_leader_ts_uuid = "");

  // If type_ is live, return PRE_VOTER, otherwise, return PRE_OBSERVER.
  consensus::PeerMemberType GetDefaultMemberType();

  //
  // Higher level methods and members.
  //

  // Resets the global_state_ object, and the map of per-table states.
  virtual void ResetGlobalState(bool initialize_ts_descs = true);


  // Resets the pointer state_ to point to the correct table's state.
  virtual void ResetTableStatePtr(const TableId& table_id, Options* options = nullptr);

  // Goes over the tablet_map_ and the set of live TSDescriptors to compute the load distribution
  // across the tablets for the given table. Returns an OK status if the method succeeded or an
  // error if there are transient errors in updating the internal state.
  Status AnalyzeTablets(const TableInfoPtr& table);

  // Finds all under-replicated tablets and processes them in order of priority.
  void ProcessUnderReplicatedTablets(
      int& remaining_adds, bool& task_added, TabletId& out_tablet_id, TabletServerId& out_to_ts);

  // Processes any required replica additions, as part of moving load from a highly loaded TS to
  // one that is less loaded.
  //
  // Returns true if a move was actually made.
  Result<bool> HandleAddReplicas(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts);

  // Processes any required replica removals, as part of having added an extra replica to a
  // tablet's set of peers, which caused its quorum to be larger than the configured number.
  //
  // Returns true if a move was actually made.
  Result<bool> HandleRemoveReplicas(TabletId* out_tablet_id, TabletServerId* out_from_ts);

  // Methods for load preparation, called by ClusterLoadBalancer while analyzing tablets and
  // building the initial state.

  virtual void InitTablespaceManager();

  // If a tablet is under-replicated, or has certain placements that have less than the minimum
  // required number of replicas, we need to add extra tablets to its peer set.
  // Takes in a specific tablet id which we will try to fix.
  //
  // Returns true if a move was actually made.
  Result<bool> HandleAddIfMissingPlacement(const TabletId& tablet_id, TabletServerId* out_to_ts);

  // If we find a tablet with peers that violate the placement information, we want to move load
  // away from the invalid placement peers, to new peers that are valid. To ensure we do not
  // under-replicate a tablet, we first find the tablet server to add load to, essentially
  // over-replicating the tablet temporarily.
  //
  // Returns true if a move was actually made.
  Result<bool> HandleAddIfWrongPlacement(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts);

  // If we find a tablet with peers that violate the placement information, we first over-replicate
  // the peer group, in the add portion of the algorithm. We then eventually remove extra replicas
  // on the remove path, here.
  //
  // Returns true if a move was actually made.
  Result<bool> HandleRemoveIfWrongPlacement(TabletId* out_tablet_id, TabletServerId* out_from_ts);

  struct LeaderMoveDetails {
    TabletId tablet_id;
    TabletServerId from_ts;
    TabletServerId to_ts;
    std::string to_ts_path;
    std::string reason;
  };
  // Move leaders load from a lower priority to a high priority TServers.
  // This is called before normal leader load balancing which balances load within each priority.
  //
  // Returns true if we could find a leader to rebalance and sets the output parameters.
  // Returns false otherwise. If error is found, returns Status.
  Result<std::optional<LeaderMoveDetails>> GetLeaderToMoveAcrossAffinitizedPriorities();

  // Processes any tablet leaders that are on a highly loaded tablet server and need to be moved.
  //
  // Returns true if a move was actually made.
  Result<bool> HandleLeaderMoves(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts);

  virtual void GetAllAffinitizedZones(
      const ReplicationInfoPB& replication_info,
      std::vector<AffinitizedZonesSet>* affinitized_zones) const;

  // Go through sorted_leader_load_ one priority at a time and move leaders so as to get an even
  // balance per table and globally.
  //
  // Returns leader if we could find a leader to rebalance and sets the output parameters.
  // Returns std::nullopt otherwise. If error is found, returns Status.
  Result<std::optional<LeaderMoveDetails>> GetLeaderToMoveWithinAffinitizedPriorities();

  // Go through sorted_load_ and figure out which tablet to rebalance and from which TS that is
  // serving it to which other TS.
  //
  // Returns true if we could find a tablet to rebalance and sets the three output parameters.
  // Returns false otherwise.
  Result<bool> GetLoadToMove(
      TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts);

  Result<std::optional<TabletId>> GetTabletToMove(
      const TabletServerId& from_ts, const TabletServerId& to_ts);

  // Issue the change config and modify the in-memory state for adding or moving a replica on the
  // specified tablet server.
  // from_ts may be empty for adds and is only used for logging. The remove replica for moves
  // happens in a later cluster balancer iteration (once the tablet is over-replicated).
  Status AddOrMoveReplica(
      const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts,
      const std::string& reason);

  // Issue the change config and modify the in-memory state for removing a replica on the specified
  // tablet server.
  Status RemoveReplica(
      const TabletId& tablet_id, const TabletServerId& ts_uuid, const std::string& reason);

  // Select the best leader from the tablet's replicas based on leader affinity / preferred zones.
  // Returns the TabletServerId of the preferred leader, or empty string if no preferred leader
  // could be determined.
  TabletServerId SelectBestLeaderAfterStepdown(
      const TabletId& tablet_id, const TabletServerId& ts_to_exclude);

  // Issue the change config and modify the in-memory state for moving a tablet leader on the
  // specified tablet server to the other specified tablet server.
  Status MoveLeader(const LeaderMoveDetails& move_details);

  // Populates pb with the replication_info in tablet's config at cluster placement_uuid_.
  Status PopulateReplicationInfo(
      const scoped_refptr<TableInfo>& table, const ReplicationInfoPB& replication_info);

  // Returns the read only placement info from placement_uuid_.
  const PlacementInfoPB& GetReadOnlyPlacementFromUuid(
      const ReplicationInfoPB& replication_info) const;

  virtual const PlacementInfoPB& GetLiveClusterPlacementInfo() const;

  void AddTSIfBlacklisted(
      const std::shared_ptr<TSDescriptor>& ts_desc, const BlacklistPB& blacklist,
      const bool leader_blacklist);

  //
  // Generic load information methods.
  //

  // Get the total number of extra replicas.
  size_t get_total_over_replication() const;

  size_t get_total_under_replication() const;

  // Convenience methods for getting totals of starting or running tablets.
  size_t get_total_starting_tablets() const;
  int get_total_running_tablets() const;

  size_t get_total_wrong_placement() const;
  size_t get_badly_placed_leaders() const;
  size_t get_total_blacklisted_servers() const;
  size_t get_total_leader_blacklisted_servers() const;

  std::unordered_map<TableId, std::unique_ptr<PerTableLoadState>> per_table_states_;
  // The state of the table load in the cluster, as far as this run of the algorithm is concerned.
  // It points to the appropriate object in per_table_states_.
  PerTableLoadState* state_ = nullptr;

  std::unique_ptr<GlobalLoadState> global_state_;
  std::unique_ptr<PerRunState> per_run_state_;

  // The catalog manager of the Master that actually has the Tablet and TS state. The object is not
  // managed by this class, but by the Master's unique_ptr.
  CatalogManager* catalog_manager_;

  scoped_refptr<AtomicGauge<int64_t>> is_load_balancing_enabled_metric_;
  scoped_refptr<AtomicGauge<uint32_t>> tablets_in_wrong_placement_metric_;
  scoped_refptr<AtomicGauge<uint32_t>> blacklisted_leaders_metric_;
  scoped_refptr<AtomicGauge<uint32_t>> total_table_load_difference_metric_;
  scoped_refptr<AtomicGauge<uint64_t>> estimated_data_to_balance_bytes_metric_;

  std::shared_ptr<YsqlTablespaceManager> tablespace_manager_;

  friend class LoadBalancerMockedBase;

 private:
  // Returns true if at least one member in the tablet's configuration is transitioning into a
  // VOTER, but it's not a VOTER yet.
  Result<bool> IsConfigMemberInTransitionMode(const TabletId& tablet_id) const;

  std::string GetSortedLoad() const;
  std::string GetSortedLeaderLoad() const;

  // Report unusual state at the beginning of a cluster balancer run which may prevent it from
  // making moves.
  void ReportUnusualClusterBalancerState() const;

  Result<std::optional<LeaderMoveDetails>> GetLeaderToMove(
      const std::vector<TabletServerId>& sorted_leader_load);

  virtual void SetBlacklistAndPendingDeleteTS();

  void TrackTask(const std::shared_ptr<RetryingRpcTask>& task);

  struct UnderReplicatedTabletInfo {
    TabletId tablet_id;
    int tablet_count;
    PerTableLoadState* table_state;

    UnderReplicatedTabletInfo(TabletId tablet_id, int tablet_count, PerTableLoadState* table_state)
        : tablet_id(tablet_id), tablet_count(tablet_count), table_state(table_state) {}

    bool operator<(const UnderReplicatedTabletInfo& other) const {
      return (tablet_count < other.tablet_count);
    }
  };

  // Goes over all table states and returns a priority-sorted list of under-replicated tablets.
  std::vector<UnderReplicatedTabletInfo> CollectUnderReplicatedTablets();

  // Checks if we are at our RBS or overreplicated tablets limit.
  Status CanAddReplicas();

  // Random number generator for picking items at random from sets, using ReservoirSample.
  ThreadSafeRandom random_;

  // Controls whether to run the cluster balancing algorithm or not.
  std::atomic<bool> is_enabled_;

  // Circular buffer of cluster balancer activity.
  ClusterBalancerActivityBuffer activity_buffer_;

  // Check if we are able to balance global load. With the current algorithm, we only allow for
  // global load balancing once all tables are themselves balanced.
  // This value is only set to true once is_idle_ becomes true, and this value is only set to false
  // once we perform a non-global move.
  bool can_perform_global_operations_ = false;

  typedef rw_spinlock LockType;
  mutable LockType mutex_;

  // Maintains a list of all tables for whom cluster balancing has been skipped.
  // Currently for consumption by components outside the cluster balancer.
  // Protected by a readers-writers lock. Only the cluster balancer writes to it.
  // Other components such as test, admin UI, etc. should
  // ideally read from it using a shared_lock<>.
  std::vector<scoped_refptr<TableInfo>> skipped_tables_ GUARDED_BY(mutex_);
  // Internal to cluster balancer structure to keep track of skipped tables.
  // skipped_tables_ is set at the end of each cluster balancer run using
  // skipped_tables_per_run_.
  std::vector<scoped_refptr<TableInfo>> skipped_tables_per_run_;

  void UpdatePerRunMetrics();

  LeaderEpoch epoch_;

  DISALLOW_COPY_AND_ASSIGN(ClusterLoadBalancer);
};

}  // namespace master
}  // namespace yb
