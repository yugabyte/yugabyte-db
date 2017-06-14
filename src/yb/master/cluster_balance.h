// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_CLUSTER_BALANCE_H
#define YB_MASTER_CLUSTER_BALANCE_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>

#include "yb/master/catalog_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/util/random.h"

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

  // Executes one run of the load balancing algorithm. This currently does not persist any state,
  // so it needs to scan the in-memory tablet and TS data in the CatalogManager on every run and
  // create a new ClusterLoadState object.
  void RunLoadBalancer();

  // Sets whether to enable or disable the load balancer, on demand.
  void SetLoadBalancerEnabled(bool is_enabled) { is_enabled_ = is_enabled; }

  //
  // Catalog manager indirection methods.
  //
 protected:
  class Options {
   public:
    // If variance between load on TS goes past this number, we should try to balance.
    double kMinLoadVarianceToBalance = 2.0;

    // If variance between leader load on TS goes past this number, we should try to balance.
    double kMinLeaderLoadVarianceToBalance = 2.0;

    // Whether to limit the number of tablets being spun up on the cluster at any given time.
    bool kAllowLimitStartingTablets = true;

    // Max number of tablets being started across the cluster, if we enable limiting this.
    int kMaxStartingTablets = 3;

    // Whether to limit the number of tablets that have more peers than configured at any given
    // time.
    bool kAllowLimitOverReplicatedTablets = true;

    // Max number of running tablet replicas that are over the configured limit.
    int kMaxOverReplicatedTablets = 3;

    // Max number of over-replicated tablet peer removals to do in any one run of the load balancer.
    int kMaxConcurrentRemovals = 3;

    // Max number of tablet peer replicas to add in any one run of the load balancer.
    int kMaxConcurrentAdds = 3;

    // Max number of tablet leaders on tablet servers to move in any one run of the load balancer.
    int kMaxConcurrentLeaderMoves = 3;

    // TODO(bogdan): actually use these...
    // TODO(bogdan): add state for leaders starting remote bootstraps, to limit on that end too.

    // Max number of tablets being started for any one given TS.
    int kMaxStartingTabletsPerTS = 1;

    // Max number of tablets being bootstrapped from any one given TS.
    int kMaxBootstrappingTabletsPerLeaderTS = 1;
  };

  // The knobs we use for tweaking the flow of the algorithm.
  Options options_;

  //
  // Indirection methods to CatalogManager that we override in the subclasses (or testing).
  //

  // Get the list of live TSDescriptors.
  virtual void GetAllLiveDescriptors(TSDescriptorVector* ts_descs) const;

  // Get access to the tablet map across the cluster.
  virtual const TabletInfoMap& GetTabletMap() const;

  // Get access to the table map.
  virtual const TableInfoMap& GetTableMap() const;

  // Get the table info object for given table uuid.
  virtual const scoped_refptr<TableInfo> GetTableInfo(const TableId& table_uuid) const;

  // Get the placement information from the cluster configuration.
  virtual const PlacementInfoPB& GetClusterPlacementInfo() const;

  // Get the blacklist information.
  virtual const BlacklistPB& GetServerBlacklist() const;

  // Should skip load-balancing of this table?
  virtual bool SkipLoadBalancing(const TableInfo& table) const;

  // Increment the provided variables by the number of pending tasks that were found.
  virtual void CountPendingTasks(const TableId& table_uuid,
                                 int* pending_add_replica_tasks,
                                 int* pending_remove_replica_tasks,
                                 int* pending_stepdown_leader_tasks);

  // Wrapper around CatalogManager::GetPendingTasks so it can be mocked by TestLoadBalancer.
  virtual void GetPendingTasks(const string& table_uuid,
                               TabletToTabletServerMap* add_replica_tasks,
                               TabletToTabletServerMap* remove_replica_tasks,
                               TabletToTabletServerMap* stepdown_leader_tasks);

  // Issue the call to CatalogManager to change the config for this particular tablet, either
  // adding or removing the peer at ts_uuid, based on the is_add argument. Removing the peer
  // is optional. When neither adding nor removing peer, it means just moving a leader from one
  // tablet server to another. If new_leader_ts_uuid is empty, a server will be picked by random
  // to be the new leader.
  virtual void SendReplicaChanges(
      scoped_refptr<TabletInfo> tablet, const TabletServerId& ts_uuid, const bool is_add,
      const bool should_remove_leader, const TabletServerId& new_leader_ts_uuid = "");

  //
  // Higher level methods and members.
  //

  // Recreates the ClusterLoadState object.
  void ResetState();

  // Goes over the tablet_map_ and the set of live TSDescriptors to compute the load distribution
  // across the tablets for the given table. Returns false if we encounter transient errors that
  // should stop the load balancing.
  bool AnalyzeTablets(const TableId& table_uuid);

  // Processes any required replica additions, as part of moving load from a highly loaded TS to
  // one that is less loaded.
  //
  // Returns true if a move was actually made.
  bool HandleAddReplicas(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts);

  // Processes any required replica removals, as part of having added an extra replica to a
  // tablet's set of peers, which caused its quorum to be larger than the configured number.
  //
  // Returns true if a move was actually made.
  bool HandleRemoveReplicas(TabletId* out_tablet_id, TabletServerId* out_from_ts);

  // Methods for load preparation, called by ClusterLoadBalancer while analyzing tablets and
  // building the initial state.

  // Method called when initially analyzing tablets, to build up load and usage information.
  // Returns false only if there are transient errors in updating the internal state.
  bool UpdateTabletInfo(TabletInfo* tablet);

  // If a tablet is under-replicated, or has certain placements that have less than the minimum
  // required number of replicas, we need to add extra tablets to its peer set.
  //
  // Returns true if a move was actually made.
  bool HandleAddIfMissingPlacement(TabletId* out_tablet_id, TabletServerId* out_to_ts);

  // If we find a tablet with peers that violate the placement information, we want to move load
  // away from the invalid placement peers, to new peers that are valid. To ensure we do not
  // under-replicate a tablet, we first find the tablet server to add load to, essentially
  // over-replicating the tablet temporarily.
  //
  // Returns true if a move was actually made.
  bool HandleAddIfWrongPlacement(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts);

  // If we find a tablet with peers that violate the placement information, we first over-replicate
  // the peer group, in the add portion of the algorithm. We then eventually remove extra replicas
  // on the remove path, here.
  //
  // Returns true if a move was actually made.
  bool HandleRemoveIfWrongPlacement(TabletId* out_tablet_id, TabletServerId* out_from_ts);

  // Processes any tablet leaders that are on a highly loaded tablet server and need to be moved.
  //
  // Returns true if a move was actually made.
  bool HandleLeaderMoves(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts);

  // Go through sorted_load_ and figure out which tablet to rebalance and from which TS that is
  // serving it to which other TS.
  //
  // Returns true if we could find a tablet to rebalance and sets the three output parameters.
  // Returns false otherwise.
  bool GetLoadToMove(TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts);

  bool GetTabletToMove(
      const TabletServerId& from_ts, const TabletServerId& to_ts, TabletId* moving_tablet_id);

  // Go through sorted_leader_load_ and figure out which leader to rebalance and from which TS
  // that is serving it to which other TS.
  //
  // Returns true if we could find a leader to rebalance and sets the three output parameters.
  // Returns false otherwise.
  bool GetLeaderToMove(TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts);

  // Issue the change config and modify the in-memory state for moving a replica from one tablet
  // server to another.
  void MoveReplica(
      const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts);

  // Issue the change config and modify the in-memory state for adding a replica on the specified
  // tablet server.
  void AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts);

  // Issue the change config and modify the in-memory state for removing a replica on the specified
  // tablet server.
  void RemoveReplica(
      const TabletId& tablet_id, const TabletServerId& ts_uuid, const bool stepdown_if_leader);

  // Issue the change config and modify the in-memory state for moving a tablet leader on the
  // specified tablet server to the other specified tablet server.
  void MoveLeader(
      const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts);

  // Methods called for returning tablet id sets, for figuring out tablets to move around.

  const PlacementInfoPB& GetPlacementByTablet(const TabletId& tablet_id) const;

  // Get access to all the tablets for the given table.
  const CHECKED_STATUS GetTabletsForTable(const TableId& table_uuid,
                                  vector<scoped_refptr<TabletInfo>>* tablets) const;

  // Returns true when not choosing a leader as victim during normal load balance move operation.
  // Currently skips leader for RF=1 case only.
  bool SkipLeaderAsVictim(const TabletId& tablet_id) const;

  //
  // Generic load information methods.
  //

  // Get the total number of extra replicas.
  int get_total_over_replication() const;

  // Convenience methods for getting totals of starting or running tablets.
  int get_total_starting_tablets() const;
  int get_total_running_tablets() const;

  int get_total_wrong_placement() const;
  int get_total_blacklisted_servers() const;

 private:
  // Returns true if at least one member in the tablet's configuration is transitioning into a
  // VOTER, but it's not a VOTER yet.
  bool ConfigMemberInTransitionMode(const TabletId& tablet_id) const;

  // Dump the sorted load on tservers (it is usually per table).
  void DumpSortedLoad() const;

  // The catalog manager of the Master that actually has the Tablet and TS state. The object is not
  // managed by this class, but by the Master's unique_ptr.
  CatalogManager* catalog_manager_;

  // Random number generator for picking items at random from sets, using ReservoirSample.
  ThreadSafeRandom random_;

  // Controls whether to run the load balancing algorithm or not.
  std::atomic<bool> is_enabled_;

  // The state of the load in the cluster, as far as this run of the algorithm is concerned.
  class ClusterLoadState;
  std::unique_ptr<ClusterLoadState> state_;

  DISALLOW_COPY_AND_ASSIGN(ClusterLoadBalancer);
};

}  // namespace master
}  // namespace yb
#endif /* YB_MASTER_CLUSTER_BALANCE_H */
